// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_autocomplete_bridge.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOCOMPLETE_BRIDGE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOCOMPLETE_BRIDGE_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/search_engines/choice_made_location.h"
#include "components/search_engines/template_url_service_observer.h"
#include "url/gurl.h"

class Profile;
class TemplateURLService;

// For CommandBarSidebarRows, which RowsSupplier returns.
#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"

namespace atrium {

// One per (host, profile) - see BrowserController's command_bar_bridge_.
// Owns a warm AutocompleteController (constructed at host registration, not
// on first keystroke, so the async InMemoryURLIndex rebuild has a head start)
// and observes TemplateURLService
// to push the default search engine's URL template to Swift
// (AtriumOnDefaultSearchChanged) at registration and on every later change.
//
// Both responsibilities share this one class because they're warmed at the
// exact same moment (host registration) against the exact same Profile* -
// see atrium_browser_controller.cc's construction site.
class AutocompleteBridge : public AutocompleteController::Observer,
                           public TemplateURLServiceObserver {
 public:
  AutocompleteBridge(Profile* profile);
  ~AutocompleteBridge() override;

  AutocompleteBridge(const AutocompleteBridge&) = delete;
  AutocompleteBridge& operator=(const AutocompleteBridge&) = delete;

  // rebuilds controller_ against `profile` if it differs from the
  // current one - ChromeAutocompleteProviderClient fixes its Profile* at
  // construction. True when it rebuilt, taking the previous result with it.
  bool SetQueryProfile(Profile* profile);

  // The current query's default match on the CURRENT profile, re-asked. For an
  // accept arriving after SetQueryProfile wiped the result the page shows - a
  // space switch made with the palette open. Empty when nothing was typed.
  GURL ReQueryDefaultDestination();

  // where a finished pass goes for the HTML palette. Set once, by
  // the controller that owns this bridge, and run with
  // BuildCommandBarMatchesJSON's snapshot.
  //
  // EVERY pass is forwarded, finished or not. Waiting for a finished one lost
  // whole queries: a pass ended by the controller's stop timer notifies nobody
  //, so its rows never reached the page. The highlight that gate
  // protected is held in the page instead, which keeps the selected row across
  // pushes carrying the same query.
  using MatchesCallback = base::RepeatingCallback<void(const std::string&)>;
  void SetMatchesCallback(MatchesCallback callback);

  // Asked on every push, not stored: this object outlives any one snapshot,
  // and a stored copy goes stale exactly when the user edits the grid or
  // closes a tab and then opens the palette. One call, one model.
  using RowsSupplier = base::RepeatingCallback<CommandBarSidebarRows()>;
  void SetRowsSupplier(RowsSupplier supplier);

  // Where the match at `index` in the CURRENT result points. Empty when the
  // index is out of range, which is what a stale index from the page reads as.
  GURL DestinationAt(size_t index) const;

  // command_bar_query op: (re)starts the controller against `text`.
  void Query(const std::string& text);
  // command_bar_stop op, and called from teardown (OnTabStripModelDestroyed)
  // so no async provider keeps running past this bridge's usable lifetime.
  void Stop();
  // command_bar_delete_match op: removes the suggestion at
  // `index` in the CURRENT result from the user's history/shortcuts, the
  // delete-support half of the `deletable` key each match now carries.
  // `index` is the entry's `index` field as forwarded (its position in
  // AutocompleteResult, which is stable only within one pass - Swift must
  // send back the index from the pass it is displaying). Out of range, or a
  // match whose SupportsDeletion() is false, is a logged no-op.
  void DeleteMatch(size_t index);
  // set_default_search_engine / set_default_search_engine_from_choice_screen
  // ops: matches `keyword` against a prepopulated TemplateURL and, if found,
  // makes it the profile's user-selected default. Unknown keyword: logged,
  // no-op.
  //
  // `choice_made_location` is REQUIRED, deliberately without a default
  //: the parameter exists precisely because relying on
  // SetUserSelectedDefaultSearchProvider's own kOther default was the bug.
  // kOther skips RecordChoiceMade(), so Chromium never stamped
  // choice_screen_completion_timestamp/version/program and went on believing
  // no search-engine choice had ever been made. Each op passes the value for
  // the surface it represents - kChoiceScreen for the first-run picker,
  // kSearchSettings for the Settings pane - which is also why there are two
  // ops: the ABI carries the originating surface as the op identity rather
  // than threading an enum across it.
  void SetDefaultSearchEngine(
      const std::string& keyword,
      search_engines::ChoiceMadeLocation choice_made_location);

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  // TemplateURLServiceObserver:
  void OnTemplateURLServiceChanged() override;

 private:
  // Pushes the current default search engine's raw URL template (unexpanded
  // {searchTerms} placeholder) plus the user_chosen signal to
  // AtriumOnDefaultSearchChanged. Called once from the constructor
  // (registration push) and again from OnTemplateURLServiceChanged.
  //
  // user_chosen is RECOMPUTED here every time (DefaultSearchManager::
  // GetDefaultSearchEngineSource() == FROM_USER), not hardcoded to 1 for the
  // "later change" case: a
  // fresh profile's asynchronous TemplateURLService::Load() completing fires
  // OnTemplateURLServiceChanged exactly once even with zero user action (the
  // default provider pointer flips from null to the FROM_FALLBACK engine),
  // so a literal "always 1 on change" would wrongly mark a never-chosen
  // profile's picker as resolved. Recomputing the source is both safe (never
  // a false positive) and behaviorally equivalent to "always 1" for every
  // REAL later change in Atrium's environment (no sync/policy/extensions),
  // since the only other path that flips the default's identity is this
  // class's own SetDefaultSearchEngine(), which always sets FROM_USER.

  // builds the HTML palette's snapshot and runs matches_callback_
  // with it.
  //
  // `with_omnibox_matches` false sends Atrium's own rows alone. Favourites and
  // tabs come from the suppliers and owe the AutocompleteController nothing, but
  // they only ever travelled on a finished pass - so a fresh profile with no
  // history and no network produced no final pass and the palette showed
  // nothing at all, not even the user's own favourites.
  //
  // The omnibox rows are held back rather than sent stale: `index` is a position
  // in the CURRENT AutocompleteResult, so shipping the previous query's rows
  // under this query's label would let an accept open the wrong page.
  void PushMatchesToPage(bool with_omnibox_matches);

  // The deferred half of OnTemplateURLServiceChanged - see the PostTask there.
  void MirrorSearchEnginesFromSettings();

  raw_ptr<Profile> profile_;
  raw_ptr<TemplateURLService> template_url_service_;
  std::unique_ptr<AutocompleteController> controller_;

  // Set once the mirror is SCHEDULED, not once it has run: that collapses a
  // burst of notifications into one mirror, and keeps the mirror's own edits
  // from scheduling another through the notifications they raise.
  bool search_engines_mirrored_ = false;

  // bumped once per Query(), stamped on every pass forwarded for
  // that query. Swift drops a pass whose generation is older than the newest
  // it has seen, so an intermediate pass from the previous query can never
  // be diffed into the current list (the whole point of streaming passes is
  // that late ones now exist). Monotonic for this bridge's lifetime; wrap is
  // not a practical concern (one increment per keystroke burst).

  // the HTML palette's sink, and the text the current pass answers
  // (the snapshot carries it back, so a page holding a newer query can tell
  // that a pass belongs to an older one).
  MatchesCallback matches_callback_;
  RowsSupplier rows_supplier_;
  std::string current_query_;

  base::WeakPtrFactory<AutocompleteBridge> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOCOMPLETE_BRIDGE_H_
