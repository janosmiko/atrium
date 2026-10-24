// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_search_engine_mirror.h"

#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "url/gurl.h"

namespace atrium {

namespace {

constexpr char kGuidPrefix[] = "atrium:";

// Atrium stores `%s`, TemplateURL wants `{searchTerms}`.
std::string ToTemplateUrl(const std::string& url) {
  std::string out = url;
  base::ReplaceFirstSubstringAfterOffset(&out, 0, "%s", "{searchTerms}");
  return out;
}

size_t CountSearchTerms(std::string_view url) {
  size_t count = 0;
  for (size_t at = url.find("%s"); at != std::string_view::npos;
       at = url.find("%s", at + 2)) {
    ++count;
  }
  return count;
}

// A host that merely contains "127.0.0.1" or "localhost" (127.0.0.1.evil.com,
// localhost.attacker.net) is not loopback, so this checks the parsed host for
// exact equality rather than pattern-matching the url string.
bool IsLoopbackHost(std::string_view host) {
  return host == "127.0.0.1" || host == "::1" ||
         base::EqualsCaseInsensitiveASCII(host, "localhost");
}

// https is always allowed. Plain http is allowed only to a loopback host, so
// a scenario can point an engine at the VM's own fixture server.
bool IsAllowedEngineUrl(const std::string& url) {
  const GURL gurl(url);
  if (!gurl.is_valid()) {
    return false;
  }
  if (gurl.SchemeIs(url::kHttpsScheme)) {
    return true;
  }
  return gurl.SchemeIs(url::kHttpScheme) &&
         IsLoopbackHost(gurl.HostNoBracketsPiece());
}

// The Rust loader's `valid()`, ported: the pane can hold a blank row the user
// has not filled in yet, and DataFor DCHECKs on one.
bool Usable(const MirroredEngine& e) {
  return !e.id.empty() && !e.name.empty() && !e.keyword.empty() &&
         IsAllowedEngineUrl(e.url) && CountSearchTerms(e.url) == 1;
}

TemplateURLData DataFor(const MirroredEngine& e) {
  TemplateURLData data;
  data.SetShortName(base::UTF8ToUTF16(e.name));
  data.SetKeyword(base::UTF8ToUTF16(e.keyword));
  data.SetURL(ToTemplateUrl(e.url));
  data.sync_guid = kGuidPrefix + e.id;
  // Not auto-replaceable: a site's own OpenSearch must not overwrite the
  // user's entry.
  data.safe_for_autoreplace = false;
  // keyword_provider.cc drops any non-prepopulated engine whose is_active
  // isn't kTrue, so an unset default here left ours invisible to keyword mode.
  data.is_active = TemplateURLData::ActiveStatus::kTrue;
  return data;
}

}  // namespace

std::optional<std::vector<MirroredEngine>> SearchEnginesFromSettings(
    const std::string& settings_json) {
  const std::optional<base::Value> root =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    return std::nullopt;
  }
  const base::ListValue* list = root->GetDict().FindList("searchEngines");
  if (!list) {
    return std::nullopt;
  }
  std::vector<MirroredEngine> out;
  for (const base::Value& item : *list) {
    const base::DictValue* d = item.GetIfDict();
    if (!d) {
      continue;
    }
    const std::string* id = d->FindString("id");
    const std::string* name = d->FindString("name");
    const std::string* keyword = d->FindString("keyword");
    const std::string* url = d->FindString("url");
    if (!id || !name || !keyword || !url) {
      continue;
    }
    MirroredEngine engine{
        *id, *name,
        std::string(base::TrimWhitespaceASCII(*keyword, base::TRIM_ALL)), *url,
        // False, because that is what the Rust loader's serde default reads a
        // missing flag as, and the pane shows the loader's answer.
        d->FindBool("enabled").value_or(false)};
    if (!Usable(engine)) {
      continue;
    }
    out.push_back(std::move(engine));
  }
  return out;
}

void MirrorSearchEngines(TemplateURLService* service,
                         const std::vector<MirroredEngine>& engines) {
  if (!service || !service->loaded()) {
    return;
  }

  std::set<std::string> wanted;
  for (const MirroredEngine& e : engines) {
    if (e.enabled) {
      wanted.insert(kGuidPrefix + e.id);
    }
  }

  // Stale entries go first, so a keyword one of them held is free again
  // before the add/update pass below checks for collisions against it.
  std::vector<const TemplateURL*> stale;
  for (const TemplateURL* t : service->GetTemplateURLs()) {
    const std::string& guid = t->sync_guid();
    if (!base::StartsWith(guid, kGuidPrefix) || wanted.contains(guid)) {
      continue;
    }
    // Remove() CHECK-fails on the default provider. Skip it here - the user
    // picks a new default first, and the next mirror pass removes this one.
    if (t == service->GetDefaultSearchProvider()) {
      continue;
    }
    stale.push_back(t);
  }
  for (const TemplateURL* t : stale) {
    service->Remove(t);
  }

  // Two owned entries swapping keywords cannot both land in one pass: whichever
  // moves first collides with the other's old keyword. Every mover parks on a
  // keyword nothing else can hold, so the pass below finds its wanted one free.
  std::map<std::string, std::u16string> parked;
  for (const MirroredEngine& e : engines) {
    if (!e.enabled) {
      continue;
    }
    const std::string guid = kGuidPrefix + e.id;
    TemplateURL* existing = service->GetTemplateURLForGUID(guid);
    if (!existing || existing->keyword() == DataFor(e).keyword()) {
      continue;
    }
    parked[guid] = existing->keyword();
    service->ResetTemplateURL(existing, existing->short_name(),
                              base::UTF8ToUTF16("atrium-tmp-" + e.id),
                              existing->url());
  }

  for (const MirroredEngine& e : engines) {
    if (!e.enabled) {
      continue;
    }
    const std::string guid = kGuidPrefix + e.id;
    const TemplateURLData fresh = DataFor(e);
    // Only ANOTHER mirrored row blocks a keyword. Chromium prepopulates
    // google.com, duckduckgo.com, bing.com and ecosia.org everywhere, so
    // treating any owner as a conflict left those four with no TemplateURL and
    // no profile could switch to them. A duplicate
    // keyword is legal - keyword_to_turl_ is a multimap.
    const TemplateURL* keyword_owner =
        service->GetTemplateURLForKeyword(fresh.keyword());
    const bool keyword_free =
        !keyword_owner || keyword_owner->sync_guid() == guid ||
        !base::StartsWith(keyword_owner->sync_guid(), kGuidPrefix);

    TemplateURL* existing = service->GetTemplateURLForGUID(guid);
    if (!existing) {
      // safe_for_autoreplace is false above, so Add() itself never refuses a
      // colliding keyword - the free-keyword check above is the real guard.
      // Add()'s result is still checked for any other reason it may decline.
      if (!keyword_free ||
          !service->Add(std::make_unique<TemplateURL>(fresh))) {
        LOG(WARNING) << "Atrium: search engine keyword taken: " << e.keyword;
      } else {
        VLOG(1) << "Atrium: search engine mirrored: " << e.keyword;
      }
      continue;
    }
    if (!keyword_free) {
      LOG(WARNING) << "Atrium: search engine keyword taken: " << e.keyword;
      // A refused mover would otherwise be left on its parking keyword, which
      // no settings file names.
      const auto was = parked.find(guid);
      if (was != parked.end() &&
          !service->GetTemplateURLForKeyword(was->second)) {
        service->ResetTemplateURL(existing, existing->short_name(), was->second,
                                  existing->url());
      }
      continue;
    }
    // ResetTemplateURL sets is_active = kTrue as a side effect, so an entry
    // added before this field existed reactivates even with nothing else to
    // change.
    if (existing->keyword() != fresh.keyword() ||
        existing->url() != fresh.url() ||
        existing->short_name() != fresh.short_name() ||
        existing->is_active() != TemplateURLData::ActiveStatus::kTrue) {
      service->ResetTemplateURL(existing, fresh.short_name(), fresh.keyword(),
                                fresh.url());
    }
    VLOG(1) << "Atrium: search engine mirrored: " << e.keyword;
  }
}

}  // namespace atrium
