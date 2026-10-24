// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_matches.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCHES_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCHES_H_

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "url/gurl.h"

namespace atrium {

// One row of the command bar, already resolved. The bridge reads these off the
// live AutocompleteResult; this struct is the omnibox-free hand-off so the
// serialization stays a pure unit (slot_presentation_table's posture).
//
// `index` is the row's position in the AutocompleteResult the browser is
// holding, NOT its position in this vector. The page sends it straight back on
// accept and on delete, so the two must agree about which match is meant.
struct CommandBarMatch {
  CommandBarMatch();
  CommandBarMatch(const CommandBarMatch&);
  CommandBarMatch& operator=(const CommandBarMatch&);
  CommandBarMatch(CommandBarMatch&&);
  CommandBarMatch& operator=(CommandBarMatch&&);
  ~CommandBarMatch();

  int index = 0;
  std::string title;
  std::string subtitle;
  // AutocompleteMatch::destination_url. The row is still ACCEPTED by `index`.
  // This carries where it goes, which is what the dedup keys on.
  std::string url;
  // "search" or "url". The page styles rows by this and nothing else.
  std::string kind;
  // Whether AutocompleteMatch::SupportsDeletion() said yes - the page only
  // offers Shift+Delete on rows that carry it.
  bool deletable = false;
  // The TemplateURL keyword Tab scopes the palette to, empty when this row
  // offers no engine. The page prefixes it to what the user types next, which
  // is how AutocompleteInput is put into keyword mode.
  std::string keyword;
  // That engine's display name, for the chip. Only read when `keyword` is set.
  std::string engine;
  // AutocompleteMatch::inline_autocompletion: the rest of the text the omnibox
  // believes is being typed. The omnibox fills it on the default match only, so
  // it is empty on every other row.
  std::string inline_completion;
};

// An Atrium favourite offered as a command bar row. It carries a URL rather
// than an `index`: a favourite is not in the AutocompleteResult the browser is
// holding, so there is no position to send back on accept.
struct CommandBarFavorite {
  std::string title;
  std::string url;
  // Inherited from the omnibox row this favourite replaced, empty otherwise.
  // A favourite has no provider of its own to fill it (CommandBarRowsForQuery).
  std::string inline_completion;
};

// `index` is a POSITION in the model's `spaces` list, not an id: a space chip
// carries no id, and sidebarSpaceSelected takes the position anyway.
struct CommandBarSpace {
  int index = 0;
  std::string name;
  // The glyph for the goToSpaceN action bound to this rail position, empty
  // when unbound or past the ninth space.
  std::string binding;
};

// One of the profile's search engines, as EngineMatchesForQuery sees it. List
// order breaks a tie between two engines sharing a keyword prefix, so the
// bridge places the default provider first.
struct CommandBarEngine {
  std::string keyword;
  std::string name;
};

// A tab the palette's verb grammar can name. `id` is the live tab id the
// sidebar uses, which is what sidebarTabClosed takes - a row with no live id is
// a persisted row with nothing to act on, so it is not offered.
struct CommandBarTab {
  std::string id;
  std::string title;
  std::string url;
  // The tab the window is already showing.
  bool selected = false;
  // Position in the window's most-recently-active order, 0 first.
  size_t recency = std::numeric_limits<size_t>::max();
  // Inherited from the omnibox row this tab replaced, empty otherwise. Only
  // the rows CommandBarRowsForQuery returns can carry one.
  std::string inline_completion;
};

// Ranks `tabs` by `recency_order`, tab ids most recent first. An id the order
// omits keeps the sentinel and falls behind every ranked row in sidebar order:
// a restored window has activated nothing, so any other rank invents one.
void ApplyTabRecencyOrder(const std::vector<std::string>& recency_order,
                          std::vector<CommandBarTab>* tabs);

// The active space's tabs, read from the same sidebar model snapshot the
// favourites come from.
//
// command-verbs.js resolves `close 3`, `mv slack work` and `split 2 5` against
// this list. It shipped with a parser, a glob matcher and its own tests, and
// nothing ever sent the list - so every command reported that nothing matched.
std::vector<CommandBarTab> TabsFromSidebarModel(const std::string& model_json);

// The favourites carried by a sidebar model snapshot.
//
// The snapshot is the only place that already knows which favourites the active
// space has, and it is built for the sidebar on every push - so the palette
// reads it rather than growing a second path to the same data. A row's `id` IS
// its URL: a favourite has no id of its own.
//
// Anything unparseable yields an empty list. A palette with no favourite rows
// is the state it was in before favourite rows existed, which is a worse palette but not a
// broken one.
std::vector<CommandBarFavorite> FavoritesFromSidebarModel(
    const std::string& model_json);

// The favourites whose title CommandBarMatchScore accepts, best first, capped
// at five so a long list cannot crowd out the autocomplete rows beneath it.

// Title only: a favourite is a name the user chose, and scoring its URL too
// would let a host the title never mentions pull it above a named hit. Equal
// scores keep sidebar order, and the empty query scores them all equal.
std::vector<CommandBarFavorite> FavoriteMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarFavorite>& favorites);

// The URL of the favourite that owns `home`'s site, or empty if none does.
// Matched by registrable domain: a favourite saved without "www." never
// equals a www.-prefixed engine home as an exact string.
// A home carrying its own path claims nothing: the domain is too coarse to
// redirect google.com/maps at a favourite on google.com without losing /maps.
std::string FavoriteUrlForHost(const std::string& home,
                               const std::vector<CommandBarFavorite>& favorites);

// The site an engine's search url names, query and fragment dropped. The PATH
// stays: it is half the identity of an engine like Google Maps, whose origin is
// plain Google. The whole query goes, because what outlives a substituted
// {searchTerms} configures a search (&tbm=isch), not a home.
// Scheme is NOT validated here - see OpenSidebarColdTab.
GURL EngineHomeUrl(const GURL& search_url);

// The spaces carried by a sidebar model snapshot, in rail order.
std::vector<CommandBarSpace> SpacesFromSidebarModel(
    const std::string& model_json);

// The three lists one push carries, read from ONE model build.
struct CommandBarSidebarRows {
  std::vector<CommandBarFavorite> favorites;
  std::vector<CommandBarTab> tabs;
  std::vector<CommandBarSpace> spaces;
};

// Scored on the space name, best first, capped at five. An empty query offers
// NONE, unlike the favourite and tab producers: the palette already opens on
// those two, and a third list of everything would push them off the screen.
std::vector<CommandBarSpace> SpaceMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarSpace>& spaces);

// Engines whose keyword matches `query`, best first and capped at five.
// `index` is -1 on every row - DestinationAt turns that into a no-op rather
// than resolving against the wrong AutocompleteResult position.
std::vector<CommandBarMatch> EngineMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarEngine>& engines);

// Scored on title and host, best first and most recent within a score, without
// the active tab - the tab on screen is not a move. Host, not the whole URL:
// a path surface makes "com" hit every row carrying it in a parameter.

// The five-row cap lands AFTER the sort, or the oldest win.
std::vector<CommandBarTab> TabMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarTab>& tabs);

// The two lines one row shows.
struct CommandBarRowText {
  std::string title;
  std::string subtitle;
};

// Picks which omnibox string goes on which line.
//
// The omnibox carries two strings per match and their meaning SWAPS by match
// type: for a search suggestion `contents` is the query the user would run and
// `description` is empty, while for a visited page `contents` is the URL and
// `description` is the page title. Reading them positionally puts a raw URL on
// the title line of every history row, which is the state the Swift palette
// shipped with before this was fixed.
CommandBarRowText RowTextForMatch(bool is_search,
                                  const std::string& contents,
                                  const std::string& description);

// What two rows must agree on to be the same page: scheme and host lowercased,
// the fragment dropped, one trailing slash trimmed. An unparseable URL keys on
// its own trimmed text, and an empty one is not a key at all.
std::string CommandBarRowURLKey(const std::string& url);

// `title` with a trailing " - Site Name" or " | Site Name" cut when the suffix
// names `url`'s own site, and runs of whitespace collapsed. Display only, so a
// query for the site name still finds the row by its raw title.
std::string TidyRowTitle(const std::string& title, const std::string& url);

// The rows one push offers, after the hygiene pass.
struct CommandBarRows {
  CommandBarRows();
  CommandBarRows(const CommandBarRows&);
  CommandBarRows& operator=(const CommandBarRows&);
  CommandBarRows(CommandBarRows&&);
  CommandBarRows& operator=(CommandBarRows&&);
  ~CommandBarRows();

  std::vector<CommandBarFavorite> favorites;
  std::vector<CommandBarTab> tabs;
  std::vector<CommandBarMatch> matches;
};

// The three sections the page concatenates, deduplicated by URL. The page
// draws them in this order, so the earlier one keeps the row. The omnibox row
// for the tab already on screen is deranked rather than dropped. `engines`'
// matches are prepended to `matches` unfiltered - they carry no url to dedup.
CommandBarRows CommandBarRowsForQuery(
    const std::string& query,
    const std::vector<CommandBarFavorite>& favorites,
    const std::vector<CommandBarTab>& tabs,
    const std::vector<CommandBarMatch>& matches,
    const std::vector<CommandBarEngine>& engines = {});

// Serializes `matches` into the snapshot window.atriumSetCommandBar takes:
//
//   {"query":"atr","matches":[{"index":0,"title":..,"subtitle":..,
//                              "kind":"url","deletable":true}]}
//
// An empty `matches` yields an empty list, which is how the page is told to
// clear rather than to keep the previous pass on screen.
//
// `tabs` stays whole for the verb grammar to count positions in, and
// `tabMatches` is the same tabs narrowed to what the user typed.
//
// `final_pass` says the omnibox has FINISHED with this query, and only then
// does the page draw its "nothing matched" row. Every other push - the
// provisional one made before the omnibox is asked, and each pass that lands
// while providers are still running - can still be followed by rows, so
// claiming nothing matched over one of them is a flash the user reads as the
// palette being broken.
std::string BuildCommandBarMatchesJSON(
    const std::string& query,
    const std::vector<CommandBarMatch>& matches,
    const std::vector<CommandBarFavorite>& favorites = {},
    const std::vector<CommandBarTab>& tabs = {},
    bool final_pass = false,
    const std::vector<CommandBarSpace>& spaces = {},
    const std::vector<CommandBarEngine>& engines = {});

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCHES_H_
