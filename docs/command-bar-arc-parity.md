# Command bar: Arc vs Atrium

Reference material for the command-bar parity work. That work
closed the same gaps in the Swift command bar. That code is deleted, so the Atrium
column below states where the HTML palette stands today, not what the Swift bar had.

**Arc side** - static analysis of the locally installed Arc 1.157.0 (build 83934):
Swift/ObjC reflection metadata and type names in `Arc.app/Contents/MacOS/Arc`, source
paths under `Frameworks/ARC/Sources/CommandBar/`, and `ARC_CommandBar.bundle`
resources. No runtime instrumentation, so behaviors marked *(inferred)* are read off
symbol names rather than observed.

**Atrium side** - the HTML palette that replaced the Swift command bar. The page is
`chrome/browser/resources/atrium/commandbar.js`, `commandbar.css` and
`command-verbs.js`. The producers are `chrome/browser/ui/atrium/atrium_command_bar_matches.cc`,
`atrium_autocomplete_bridge.cc` and `atrium_command_bar_widget.mm`. The Swift package
is deleted, so nothing below describes `CommandBarModel.swift` any more. The palette's
design reference is the `cmdoverlay` mock in `docs/lab/swipe-lab.html`.

## 1. Architecture

| | Arc | Atrium |
|---|---|---|
| Suggestion engine | Own multi-source search client (`SearchClient`, `URLInputSearchClient`), Chromium autocomplete is one source among many | Chromium `AutocompleteController` (default omnibox providers), plus favourites, local actions and a verb grammar the page owns |
| Merge/rank | Feature-scored ranking model with archetype boosts, remote-configurable | Fixed order: local actions, then favourites, then open tabs, then spaces, then omnibox rows in provider order |
| Match payload from Chromium | `ArcAutocompleteMatch`: `destinationUrlSpec`, `relevance`, `matchType`, `contents`, `keyword`, `faviconUrl`, `deletable` | 7 JSON fields: `index`, `title`, `subtitle`, `url`, `kind`, `deletable`, plus `keyword`/`engine` or `inlineCompletion` when present |
| Result passes | Streams passes, gated by a typing state machine | Every pass. One provisional push before the query starts, then one per `OnResultChanged`, with `final` on the last |
| Input field | `NSTextField` + field editor + `localEventMonitor`, own undo manager | One `<input>` with a `keydown` handler covering arrows, Return, Escape, Tab, Backspace, Delete, Cmd+C and inline-completion keys |
| Row list | `NSTableView`, per-kind cell views | Flat `<div role="listbox">`, one CSS class per row kind |

## 2. Suggestion sources

Arc's `sourceTypes` enum (all merged into one ranked list):

`list`, `project`, `document`, `easel`, `typeTravel`, `topDomain`, `please`,
`navigateWhatYouTyped`, `searchWhatYouTyped`, `askGPTWhatYouTyped`, `instantLinks`,
`memoryStore`, `askAssistant`, `assistantHistory`, `siteSearch`, `scopedSearch`,
`chromiumSearchWhatYouTyped`, `chromiumSearchSuggestion`, `chromiumSearchHistory`,
`chromiumHistory`, `chromiumNavigateWhatYouTyped`, `chromiumNavigateSuggestion`,
`chromiumSearchOtherEngine`, `chromiumSearchCalculatorAnswer`, `chromiumBookmark`.

Atrium merges what these producers give it. The extra sources built in Swift did not survive the port:

| Source | Producer | Status |
|---|---|---|
| Chromium matches | `atrium_autocomplete_bridge.cc` | Present. Collapsed to `search` and `url` |
| Favourites | `atrium_command_bar_matches.cc` | Present. Title substring, at most 5 rows |
| Open tabs | `atrium_command_bar_matches.cc` | Present. Ranked by recency, active tab excluded, at most 5 rows |
| Spaces | `atrium_command_bar_matches.cc` | Present. Name substring on a typed query only, at most 5 rows |
| Local actions | `atrium_command_bar_actions.cc` | Present. 8 rows derived from the native hotkey table, a shortcut chip on the 2 that have a binding |
| Verb commands | `command-verbs.js` | Present. `mv`, `close`, `split` over the active space's tabs |
| Calculator answers | Chromium `CALCULATOR` matches | Partial. They arrive as search rows and the payload carries no answer |
| Top domains, other-engine search, instant links, AI rows | None | Missing |

The verb grammar is Atrium's own and has no Arc counterpart.

## 3. Ranking

Arc scores every candidate on named features, then applies archetype boosts:

- Features: `urlMatchScore/Position/Percentage`, `titleMatch*`, `hotWordsMatch*`,
  `fuzzySearchConfidence`, `frequencyEver`, `typedCountEver`, `mostVisitedRank`,
  `siteEngagementScore`, `globalSiteRank`, `lastNavigated`, `typeTravelCandidateBump`,
  `chromiumRank`, `chromiumRelevanceScore`.
- Archetypes (boost rules): `exactURL`, `exactBaseDomain`, `exactHotWord`, `baseDomain`,
  `autocompleteDefault`, `autocompleteSearchOnly`, `chromiumSpecialSearchFunction`,
  `customTitlePrefix`, `veryRecentTabOrDocumentBoost`, `topApp`, `recentlyUseTypeTravel`,
  `typeTravelPrefixForLongQuery`, `unboxingPromotedAction`, `highFuzzySearchConfidence`.
- Matching is fuzzy and tokenized (`fuzzyMatchScoreCache`, `tokenizedTextMatchInfoCache`)
  and includes per-item "hot words" alongside title and URL.
- Selection feedback is persisted (`StorableCommandBarAdditionalRankingData`: `query`,
  `siteName`, `archetype`, `selectedAt`, `executed`, `frequency`) and fed back as a boost.
- Config is remote (`resources.arc.net`, `CommandBarRemoteConfiguration`) with a bundled
  fallback `command_bar_behavior.json`.

Atrium ranks nothing beyond a per-source score-then-cap. Favourites, open tabs, and
spaces each match on a token score from `atrium_command_bar_match_text.cc`
(`CommandBarMatchScore`, mirrored in JS as `command-match.js`). The score rewards a
word prefix over a subsequence, and each producer sorts and caps the result at 5 rows.
Actions match their alias words with the same JS scorer (`actionsFor`). Tabs break score ties
by recency. Chromium rows keep the order the providers gave them. No accept is
recorded, and only the action rows carry aliases.

## 4. Result hygiene

| Behavior | Arc | Atrium |
|---|---|---|
| Dedup | URL dedup plus title+host dedup for a shipped site list (figma.com, notion.so, github.com, zoom.us, ... - `command_bar_behavior.json`) | URL dedup across favourites, tabs and omnibox rows. A favourite wins over a tab, a tab wins over an omnibox row (`CommandBarRowsForQuery`) |
| Current tab | `derankCurrentItemURLEnabled` pushes the current page down | `DerankActivePage` pushes it below the first row that is not the active page |
| Titles | `tidyTabTitles` normalizes noisy tab titles | `TidyRowTitle` strips a trailing `- site` or `\| site` suffix that names the row's registrable domain |
| Caps | `maxSuggestions`, `maxCountToDisplay`, `preferredMaxRankConfig`, per-source `timeoutValue` | 5 favourites, 5 open tabs, 5 spaces. Omnibox rows are uncapped inside a fixed-height scroller |
| Flicker | `TypeLetterInCommandBarStateMachine` (states `activeNoQuery`/`activeWithQuery`, events `letterTyped`/`newLetterTyped`/`allResultsCollected`/`noTypingInProgress`) holds the list stable while typing | Passes replace the list. The page re-finds the highlighted row by its text, and holds the no-match row until a `final` pass |

## 5. Input field

- Inline autocomplete: Arc consumes Chromium's inline completion and tracks
  `preventInlineAutocomplete` with backspace state (`isBackspacingStatus`,
  `latestInputAndDidBackspace`). Atrium hands the completion to the surviving row
  after dedup (`completion_sink` in `atrium_command_bar_matches.cc`) and the field
  renders it as trailing selected text. Right, End, or typing over the
  selection accepts it. Backspace or Delete suppress it for that keystroke.
- Scoped/site search: Tab enters a scoped mode with a pill (`.cbscope` in
  `commandbar.css`) that shows the highlighted row's search engine.
  The field keeps the pre-Tab text so Escape or a Backspace on an empty scoped
  field restores it. Arc's own trigger also accepts a space keystroke
  (`siteSearchSpaceTriggerEnabled`), which Atrium has not ported.
- Entry points decide the opening state (`entryPoint`, `beginEditPolicy`,
  `CommandBarSelectionOptions.switchToTabAllowed/showSuggestions/showInCenter`): opening
  from the URL bar prefills and selects the current URL, `keyboardShortcutT` opens empty,
  and `cachedCommandBarQuery`/`didRestoreQuery` can restore the last query. Atrium keeps
  the `CommandBarOpenReason` latch. Cmd+L sets `kEditCurrentURL` and a click on
  either address bar sets `kEditAtAnchor`, which carries the clicked row's rect
  as well; `Show()` calls `TakePrefillForOpenReason` before `ResetPage`, so the
  field opens with the displayed tab's URL prefilled and selected, and an
  anchored open draws the card on the row instead of centred. Cmd+T opens
  empty. Neither restores a prior query.
- Undo/redo inside the field via a dedicated `commandBarUndoManager`. Atrium gets
  whatever the plain `<input>` gives it.

## 6. Keyboard

Arc routes `control:textView:doCommandBySelector:` plus a `localEventMonitor`; the
reducer actions name the surface:

| Key | Arc action | Atrium |
|---|---|---|
| Up / Down | `onKeyboardUp` / `onKeyboardDown` | Arrows, plus Ctrl+N and Ctrl+P |
| Return | `onDidReturn` / `_onKeyboardReturn` | Yes. A Return pressed before the pass lands is honored against the next one |
| Escape | `onEscapePressed` (layered: leaves scoped search first) | Layered the same way. The first Escape leaves a scope, the second dismisses |
| Tab | `onTabKeyPressed` - enter scoped search | Enters scoped search on the highlighted row's engine |
| Backspace | `onBackspacePressed` - leaves scoped search / clears pill | Leaves scoped search when the scoped field is empty |
| Left / Right | `onKeyboardLeft` / `onKeyboardRight` - accept completion, move within instant links *(inferred)* | Right and End accept an inline completion, otherwise plain caret movement |
| Copy | `onAttemptToCopyCommandBarText`, `onCopyCurrentSuggestionURLShortcut` - copies the highlighted suggestion's URL | Cmd+C copies the highlighted row's URL |
| Delete suggestion | `onDeleteSuggestion` -> `deleteAutocompleteMatch:` (Chromium history entry removal) | Shift+Delete, on the rows the browser marked `deletable` |
| Undo / redo | `canUndo`/`canRedo` on the field | System default only |
| Modifier holds | `commandKeyShowsCommandBar`, `controlKeyShowsTypeTravel` (hold Control to reveal the action list) | Not handled - deliberately, see below |

Rows can also render their own bound shortcut (`KeyboardShortcutView`,
`TypeTravelSuggestionView`), and actions come from a main-menu-derived index
(`TypeTravelMainMenuIndex`) with per-action trigger aliases
(`arcTypeTravelTriggers`, `arcTypeTravelMinQueryLengthForMatch`, `arcTypeTravelIcon`).

The HTML palette now ports most of that. The rows come from a C++ table keyed on
the native hotkey actions (`atrium_command_bar_actions.cc`), pushed on every open.
Each row carries alias words, and the two rows with a binding carry its shortcut.
`buildRow` draws that as a chip (`.cbkey`), and a typed query offers no verbs under
two characters. Ranking by
recent use stays unported. The lab also groups rows under section headers. The palette
labels the section on the row instead, so a single favourite costs no header line.

`controlKeyShowsTypeTravel` stays unported. The hold reveals an action list, and the
palette has no catalog for it to reveal.
