// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"

#include <map>
#include <optional>
#include <utility>

#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace atrium {

namespace {

// `key` is "url" for a row and "id" for a favourite tile.
void CollectList(const base::ListValue* rows,
                 const char* key,
                 base::FunctionRef<bool(const std::string&)> has_icon,
                 std::set<std::string>& seen,
                 std::vector<std::string>& out) {
  if (!rows) {
    return;
  }
  for (const base::Value& row : *rows) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    if (const std::string* kind = dict->FindString("kind");
        kind && *kind == "folder") {
      // A pinned folder carries no url of its own - its members do.
      CollectList(dict->FindList("tabs"), key, has_icon, seen, out);
      continue;
    }
    const std::string* url = dict->FindString(key);
    if (!url || url->empty() || has_icon(*url) || !seen.insert(*url).second) {
      continue;
    }
    out.push_back(*url);
  }
}

void CollectPane(const base::DictValue& pane,
                 base::FunctionRef<bool(const std::string&)> has_icon,
                 std::set<std::string>& seen,
                 std::vector<std::string>& out) {
  CollectList(pane.FindList("tabs"), "url", has_icon, seen, out);
  CollectList(pane.FindList("pinned"), "url", has_icon, seen, out);
  CollectList(pane.FindList("favorites"), "id", has_icon, seen, out);
  if (const base::ListValue* folders = pane.FindList("folders")) {
    for (const base::Value& folder : *folders) {
      if (const base::DictValue* dict = folder.GetIfDict()) {
        CollectList(dict->FindList("tabs"), "url", has_icon, seen, out);
      }
    }
  }
}

}  // namespace

std::vector<std::string> SelectMissingIconURLs(
    const base::DictValue& model,
    const std::string& profile_id,
    const std::string& unowned_id,
    base::FunctionRef<bool(const std::string&)> has_icon) {
  const auto owner = [&](const base::DictValue* space) {
    const std::string* id = space ? space->FindString("profileId") : nullptr;
    return id && !id->empty() ? *id : unowned_id;
  };
  const base::ListValue* spaces = model.FindList("spaces");
  const auto active =
      static_cast<size_t>(model.FindInt("activeSpace").value_or(0));
  const base::DictValue* shown = spaces && active < spaces->size()
                                     ? (*spaces)[active].GetIfDict()
                                     : nullptr;
  std::vector<std::string> out;
  std::set<std::string> seen;
  if (owner(shown) == profile_id) {
    CollectPane(model, has_icon, seen, out);
  }
  // Every space, not only the active one: each rail pane renders its own
  // space's rows, so stopping here leaves a row iconless in the pane beside
  // the one that resolved it.
  if (spaces) {
    for (const base::Value& space : *spaces) {
      const base::DictValue* dict = space.GetIfDict();
      if (dict && owner(dict) == profile_id) {
        CollectPane(*dict, has_icon, seen, out);
      }
    }
  }
  return out;
}

std::string DefaultFaviconURLFor(const std::string& page_url) {
  const GURL url(page_url);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  // Through the origin rather than url.Resolve: Resolve keeps any user:pass@
  // the page URL carries, and this request must not send them anywhere.
  const url::Origin origin = url::Origin::Create(url);
  if (origin.opaque()) {
    return std::string();
  }
  return origin.GetURL().Resolve("/favicon.ico").spec();
}

bool IcoFailureReachesLinkTagTier(int net_error, bool in_link_tag_tier) {
  if (net_error == net::OK ||
      net_error == net::ERR_HTTP_RESPONSE_CODE_FAILURE) {
    return true;
  }
  return in_link_tag_tier;
}

bool RedirectTargetAllowedForIcoFetch(const GURL& target) {
  if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS() ||
      net::IsLocalhost(target)) {
    return false;
  }
  if (!target.HostIsIPAddress()) {
    return true;
  }
  net::IPAddress address;
  return net::ParseURLHostnameToAddress(target.host(), &address) &&
         address.IsPubliclyRoutable();
}

bool PageMayNameIcons(const GURL& page_url,
                      const GURL& final_url,
                      std::string_view mime_type) {
  if (!page_url.is_valid() || !final_url.is_valid()) {
    return false;
  }
  if (mime_type != "text/html" && mime_type != "application/xhtml+xml") {
    return false;
  }
  using net::registry_controlled_domains::GetDomainAndRegistry;
  using net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES;
  const std::string page_domain =
      GetDomainAndRegistry(page_url, INCLUDE_PRIVATE_REGISTRIES);
  const std::string final_domain =
      GetDomainAndRegistry(final_url, INCLUDE_PRIVATE_REGISTRIES);
  // An intranet host has no registry, so fall back to the host itself.
  return page_domain.empty() ? page_url.host() == final_url.host()
                             : page_domain == final_domain;
}

std::vector<GURL> IconCandidatesFromLinks(const std::string& links_json,
                                          const GURL& page_url) {
  std::vector<GURL> out;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(links_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return out;
  }
  const base::DictValue& dict = parsed->GetDict();
  GURL resolved_base = page_url;
  if (const std::string* base_href = dict.FindString("base")) {
    const GURL resolved = page_url.Resolve(*base_href);
    if (resolved.is_valid() && resolved.SchemeIsHTTPOrHTTPS()) {
      resolved_base = resolved;
    }
  }
  const base::ListValue* icons = dict.FindList("icons");
  if (!icons) {
    return out;
  }
  for (const base::Value& icon : *icons) {
    if (out.size() >= kMaxIconCandidates) {
      break;
    }
    const base::DictValue* entry = icon.GetIfDict();
    const std::string* href = entry ? entry->FindString("href") : nullptr;
    if (!href) {
      continue;
    }
    const GURL target = resolved_base.Resolve(*href);
    if (!target.is_valid()) {
      continue;
    }
    const bool data_image =
        target.SchemeIs(url::kDataScheme) &&
        base::StartsWith(target.GetContent(), "image/",
                         base::CompareCase::INSENSITIVE_ASCII);
    if (data_image || RedirectTargetAllowedForIcoFetch(target)) {
      out.push_back(target);
    }
  }
  return out;
}

namespace {
std::map<uintptr_t, IconBackfillQueue>& Queues() {
  static base::NoDestructor<std::map<uintptr_t, IconBackfillQueue>> queues;
  return *queues;
}
}  // namespace

IconBackfillQueue::IconBackfillQueue(size_t max_in_flight)
    : max_in_flight_(max_in_flight) {}

IconBackfillQueue::~IconBackfillQueue() = default;

std::vector<std::string> IconBackfillQueue::Admit(
    const std::vector<std::string>& urls,
    bool allow_refetch) {
  for (const std::string& url : urls) {
    if (url.empty() || running_.count(url) || queued_.count(url)) {
      continue;
    }
    bool refetch = false;
    const auto found = found_.Peek(url);
    if (allow_refetch && found != found_.end()) {
      found_.Erase(found);
      refetch = true;
    }
    if (admitted_.Get(url) != admitted_.end() && !refetch) {
      continue;
    }
    admitted_.Put(std::string(url));
    queued_.insert(url);
    waiting_.push_back(url);
  }
  return StartWhatFits();
}

void IconBackfillQueue::MarkFound(const std::string& url) {
  found_.Put(std::string(url));
}

std::vector<std::string> IconBackfillQueue::Finish(const std::string& url) {
  running_.erase(url);
  return StartWhatFits();
}

void IconBackfillQueue::AllowRetry(const std::string& url) {
  // A lookup that has not answered yet IS the retry. Forgetting the URL here
  // would let the next push queue a second copy of the same lookup.
  if (running_.count(url) || queued_.count(url)) {
    return;
  }
  const auto it = admitted_.Peek(url);
  if (it != admitted_.end()) {
    admitted_.Erase(it);
  }
}

void IconBackfillQueue::Release(const std::vector<std::string>& urls) {
  for (const std::string& url : urls) {
    running_.erase(url);
  }
}

IconBackfillQueue& IconBackfillQueueFor(uintptr_t profile_key) {
  // Eight at once: a cold launch names every row in the profile in one go, and
  // each ask is a database read before it is ever a network fetch.
  static constexpr size_t kMaxInFlight = 8;
  return Queues().try_emplace(profile_key, kMaxInFlight).first->second;
}

IconBackfillQueue* FindIconBackfillQueue(uintptr_t profile_key) {
  const auto it = Queues().find(profile_key);
  return it == Queues().end() ? nullptr : &it->second;
}

void ForgetIconBackfillQueue(uintptr_t profile_key) {
  Queues().erase(profile_key);
}

std::vector<std::string> IconBackfillQueue::StartWhatFits() {
  std::vector<std::string> started;
  while (running_.size() < max_in_flight_ && !waiting_.empty()) {
    std::string url = std::move(waiting_.front());
    waiting_.pop_front();
    queued_.erase(url);
    running_.insert(url);
    started.push_back(std::move(url));
  }
  return started;
}

}  // namespace atrium
