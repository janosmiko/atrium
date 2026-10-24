// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_autocomplete_bridge.cc
#include "chrome/browser/ui/atrium/atrium_autocomplete_bridge.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/atrium/atrium_search_engine_mirror.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_match_type.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/search_engines/default_search_manager.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"

namespace atrium {

AutocompleteBridge::AutocompleteBridge(Profile* profile)
    : profile_(profile),
      template_url_service_(TemplateURLServiceFactory::GetForProfile(profile)),
      controller_(std::make_unique<AutocompleteController>(
          std::make_unique<ChromeAutocompleteProviderClient>(profile),
          AutocompleteControllerConfig{
              .provider_types =
                  AutocompleteClassifier::DefaultOmniboxProviders()})) {
  controller_->AddObserver(this);
  if (template_url_service_) {
    template_url_service_->AddObserver(this);
    // Nothing to catch up on when the service is already loaded, and a later
    // change notification must not stand in for the load: re-mirroring the
    // cached list would undo the very edit that raised it.
    search_engines_mirrored_ = template_url_service_->loaded();
    // Kick off the async load now (idempotent if already loading/loaded) so
    // an early set_default_search_engine has the best chance of running
    // against a fully loaded service.
    template_url_service_->Load();
  }
}

AutocompleteBridge::~AutocompleteBridge() {
  controller_->Stop(AutocompleteStopReason::kClobbered);
  controller_->RemoveObserver(this);
  if (template_url_service_) {
    template_url_service_->RemoveObserver(this);
  }
}

bool AutocompleteBridge::SetQueryProfile(Profile* profile) {
  if (!profile || profile == profile_) {
    return false;
  }
  controller_->Stop(AutocompleteStopReason::kClobbered);
  controller_->RemoveObserver(this);
  if (template_url_service_) {
    template_url_service_->RemoveObserver(this);
  }
  profile_ = profile;
  template_url_service_ = TemplateURLServiceFactory::GetForProfile(profile);
  controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile),
      AutocompleteControllerConfig{
          .provider_types = AutocompleteClassifier::DefaultOmniboxProviders()});
  controller_->AddObserver(this);
  if (template_url_service_) {
    template_url_service_->AddObserver(this);
    search_engines_mirrored_ = template_url_service_->loaded();
    template_url_service_->Load();
  }
  return true;
}

GURL AutocompleteBridge::ReQueryDefaultDestination() {
  if (current_query_.empty()) {
    return GURL();
  }
  // Through Query, so the page keeps the rows this answered from. It fires
  // matches_callback_ and can land OnResultChanged before returning, so the
  // caller must navigate on the answer immediately.
  Query(current_query_);
  const AutocompleteMatch* match = controller_->result().default_match();
  return match ? match->destination_url : GURL();
}

void AutocompleteBridge::SetRowsSupplier(RowsSupplier supplier) {
  rows_supplier_ = std::move(supplier);
}

void AutocompleteBridge::SetMatchesCallback(MatchesCallback callback) {
  matches_callback_ = std::move(callback);
}

GURL AutocompleteBridge::DestinationAt(size_t index) const {
  const AutocompleteResult& result = controller_->result();
  if (index >= result.size()) {
    LOG(WARNING) << "Atrium: command bar accept index " << index
                 << " out of range (" << result.size() << " matches)";
    return GURL();
  }
  return result.match_at(index).destination_url;
}

namespace {

// `associated_keyword` is the signal the omnibox's own "press Tab to search"
// hint reads. The `keyword` fallback drops the default provider: scoping to the
// engine the palette already searches changes nothing.
const TemplateURL* ScopeEngineForMatch(const AutocompleteMatch& match,
                                       TemplateURLService* service) {
  if (!service) {
    return nullptr;
  }
  if (!match.associated_keyword.empty()) {
    return service->GetTemplateURLForKeyword(match.associated_keyword);
  }
  if (match.keyword.empty() || !AutocompleteMatch::IsSearchType(match.type)) {
    return nullptr;
  }
  const TemplateURL* engine = service->GetTemplateURLForKeyword(match.keyword);
  return engine == service->GetDefaultSearchProvider() ? nullptr : engine;
}

// The default provider leads, which is EngineMatchesForQuery's tie-break for
// two engines whose keyword shares a prefix.
std::vector<CommandBarEngine> EnginesForPrefixOffer(
    TemplateURLService* service) {
  std::vector<CommandBarEngine> engines;
  if (!service) {
    return engines;
  }
  const TemplateURL* default_engine = service->GetDefaultSearchProvider();
  if (default_engine && !default_engine->keyword().empty()) {
    engines.push_back({base::UTF16ToUTF8(default_engine->keyword()),
                       base::UTF16ToUTF8(default_engine->short_name())});
  }
  for (const TemplateURL* engine : service->GetTemplateURLs()) {
    if (engine == default_engine || engine->keyword().empty()) {
      continue;
    }
    engines.push_back({base::UTF16ToUTF8(engine->keyword()),
                       base::UTF16ToUTF8(engine->short_name())});
  }
  return engines;
}

}  // namespace

void AutocompleteBridge::PushMatchesToPage(bool with_omnibox_matches) {
  const AutocompleteResult& result = controller_->result();
  std::vector<CommandBarMatch> matches;
  size_t completion_chars = 0;
  for (size_t i = 0; with_omnibox_matches && i < result.size(); ++i) {
    const AutocompleteMatch& match = result.match_at(i);
    if (match.destination_url.is_empty()) {
      // A row with nowhere to go cannot be accepted, so it is not offered. The
      // emitted `index` stays the AutocompleteResult position rather than a
      // post-skip one, so an accept or a delete resolves against the same list
      // C++ is holding.
      continue;
    }
    const bool is_search = AutocompleteMatch::IsSearchType(match.type);
    const CommandBarRowText text =
        RowTextForMatch(is_search, base::UTF16ToUTF8(match.contents),
                        base::UTF16ToUTF8(match.description));
    CommandBarMatch row;
    row.index = static_cast<int>(i);
    row.title = text.title;
    row.subtitle = text.subtitle;
    row.url = match.destination_url.spec();
    row.kind = is_search ? "search" : "url";
    row.deletable = match.SupportsDeletion();
    row.inline_completion = base::UTF16ToUTF8(match.inline_autocompletion);
    if (completion_chars == 0) {
      completion_chars = row.inline_completion.size();
    }
    if (const TemplateURL* engine =
            ScopeEngineForMatch(match, template_url_service_)) {
      row.keyword = base::UTF16ToUTF8(engine->keyword());
      row.engine = base::UTF16ToUTF8(engine->short_name());
    }
    matches.push_back(std::move(row));
  }
  // The omnibox has finished with this query only when the controller says so
  // AND this push carries its rows. The provisional push is never final: it is
  // made before the controller is asked anything.
  const bool final_pass = with_omnibox_matches && controller_->done();

  // This line closes a gap: the query length above says
  // the text arrived, and nothing said what came back. A palette that shows an
  // empty list has three causes - no pass, a pass that found nothing, or a push
  // the page dropped - and only the first two are visible from here.
  //
  // The count, never the rows: a match title is the user's browsing data. Same
  // for `completion` - a field that did not complete has two causes, nothing
  // offered or the page declining it, and only these counts separate them.
  VLOG(1) << "Atrium: command bar results " << matches.size()
          << (with_omnibox_matches ? " answered" : " pending")
          << (final_pass ? " final" : "") << " completion " << completion_chars;
  const CommandBarSidebarRows rows =
      rows_supplier_ ? rows_supplier_.Run() : CommandBarSidebarRows();
  matches_callback_.Run(BuildCommandBarMatchesJSON(
      current_query_, matches, rows.favorites, rows.tabs, final_pass,
      rows.spaces, EnginesForPrefixOffer(template_url_service_)));
}

void AutocompleteBridge::Query(const std::string& text) {
  // traffic-routing waits for a "command bar results" line that
  // never arrives, and the same run shows the bar opening, taking key, and
  // being dismissed. Two explanations look identical from the outside: nothing
  // was ever typed, or something was typed and produced no matches. This
  // separates them.
  //
  // Logs the LENGTH, never the text. A command bar query is a URL or a search
  // term, which is exactly the browsing data that has no business in a log
  // file - the same reason a window title was taken out of the earlier probe.
  VLOG(1) << "Atrium: command bar query length " << text.size();
  current_query_ = text;
  // Atrium's own rows go out NOW, before the controller is asked for anything.
  // They are the sidebar's favourites and tabs, so they do not depend on a pass
  // landing - and on a profile where no pass ever lands they were the whole of
  // what the palette should have shown. Before Start, not after: a
  // Start that completes synchronously pushes the full set, and this must not
  // land on top of it and blank the omnibox rows again.
  if (matches_callback_) {
    PushMatchesToPage(/*with_omnibox_matches=*/false);
  }
  const std::u16string text16 = base::UTF8ToUTF16(text);
  AutocompleteInput input(text16, metrics::OmniboxEventProto::OTHER,
                         ChromeAutocompleteSchemeClassifier(profile_.get()));
  controller_->Start(input);
}

void AutocompleteBridge::Stop() {
  controller_->Stop(AutocompleteStopReason::kClobbered);
}

void AutocompleteBridge::DeleteMatch(size_t index) {
  const AutocompleteResult& result = controller_->result();
  if (index >= result.size()) {
    LOG(WARNING) << "Atrium: command_bar_delete_match index " << index
                 << " out of range (" << result.size() << " matches)";
    return;
  }
  const AutocompleteMatch& match = result.match_at(index);
  if (!match.SupportsDeletion()) {
    LOG(WARNING) << "Atrium: command_bar_delete_match on a match that does "
                    "not support deletion; ignored";
    return;
  }
  // Passing the reference straight through is the same posture
  // OmniboxEditModel takes - DeleteMatch copies what it needs before it
  // mutates the result this reference points into.
  controller_->DeleteMatch(match);
}

void AutocompleteBridge::SetDefaultSearchEngine(
    const std::string& keyword,
    search_engines::ChoiceMadeLocation choice_made_location) {
  if (!template_url_service_) {
    return;
  }
  TemplateURL* match =
      template_url_service_->GetTemplateURLForKeyword(base::UTF8ToUTF16(keyword));
  if (!match) {
    LOG(WARNING) << "Atrium: set_default_search_engine dropped - unknown "
                    "keyword '"
                 << keyword << "'";
    return;
  }
  template_url_service_->SetUserSelectedDefaultSearchProvider(
      match, choice_made_location);
}

void AutocompleteBridge::OnResultChanged(AutocompleteController* controller,
                                        bool default_match_changed) {
  // EVERY pass, not the finished ones. A pass ends either because all its
  // providers finished or because AutocompleteController's stop timer ends it
  // after 1500ms - and that second ending sends no notification at all: Stop()
  // requests one and cancels it on the next line (autocomplete_controller.cc).
  // So a query whose providers never finish produced rows the page was never
  // told about, and the palette held an empty list over a result with matches
  // in it.
  //
  // The highlight is what this used to protect, and the page protects it now:
  // it keeps the selected row across pushes that carry the same query, so a
  // pass landing behind an arrow key no longer moves the selection.
  if (matches_callback_) {
    PushMatchesToPage(/*with_omnibox_matches=*/true);
  }
}

void AutocompleteBridge::OnTemplateURLServiceChanged() {
  // the default search engine used to be pushed to the Swift
  // command bar from here. The HTML palette has no consumer for it, so the
  // observation is kept as the seam and does nothing until one exists.

  // Catches up the window-creation mirror call, which is a no-op until this
  // service finishes its async load.
  if (!search_engines_mirrored_ && template_url_service_ &&
      template_url_service_->loaded()) {
    search_engines_mirrored_ = true;
    // Posted, never called here: this runs inside TemplateURLService's observer
    // loop, and the mirror's own Add/Remove notify from within it - the
    // re-entrancy CHECK in base/observer_list.h aborts the browser.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AutocompleteBridge::MirrorSearchEnginesFromSettings,
                       weak_factory_.GetWeakPtr()));
  }
}

void AutocompleteBridge::MirrorSearchEnginesFromSettings() {
  if (const std::optional<std::vector<MirroredEngine>> engines =
          SearchEnginesFromSettings(AtriumBrowserWindow::LastSettingsJson())) {
    MirrorSearchEngines(template_url_service_, *engines);
  }
  // The window's own apply pass ran before this service finished loading, so
  // it found no GUID to select.
  if (profile_) {
    ApplyStoredProfileSearchEngine(profile_->GetBaseName().AsUTF8Unsafe());
  }
}

}  // namespace atrium
