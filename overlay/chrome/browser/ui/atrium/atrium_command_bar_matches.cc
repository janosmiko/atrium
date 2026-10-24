// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_matches.cc
#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_match_text.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace atrium {

CommandBarMatch::CommandBarMatch() = default;
CommandBarMatch::CommandBarMatch(const CommandBarMatch&) = default;
CommandBarMatch& CommandBarMatch::operator=(const CommandBarMatch&) = default;
CommandBarMatch::CommandBarMatch(CommandBarMatch&&) = default;
CommandBarMatch& CommandBarMatch::operator=(CommandBarMatch&&) = default;
CommandBarMatch::~CommandBarMatch() = default;

CommandBarRows::CommandBarRows() = default;
CommandBarRows::CommandBarRows(const CommandBarRows&) = default;
CommandBarRows& CommandBarRows::operator=(const CommandBarRows&) = default;
CommandBarRows::CommandBarRows(CommandBarRows&&) = default;
CommandBarRows& CommandBarRows::operator=(CommandBarRows&&) = default;
CommandBarRows::~CommandBarRows() = default;

CommandBarRowText RowTextForMatch(bool is_search,
                                  const std::string& contents,
                                  const std::string& description) {
  CommandBarRowText text;
  if (is_search) {
    text.title = contents;
    text.subtitle = "Search";
    return text;
  }
  if (description.empty()) {
    text.title = contents;
    return text;
  }
  text.title = description;
  text.subtitle = contents;
  return text;
}

std::vector<CommandBarTab> TabsFromSidebarModel(const std::string& model_json) {
  std::vector<CommandBarTab> tabs;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return tabs;
  }
  const base::ListValue* rows = parsed->GetDict().FindList("tabs");
  if (!rows) {
    return tabs;
  }
  for (const base::Value& row : *rows) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    // A row with no live id is a persisted one - there is no tab to close, so
    // offering it would make a command that resolves and then does nothing,
    // which is the failure this whole change is about.
    if (!id || id->empty()) {
      continue;
    }
    CommandBarTab tab;
    tab.id = *id;
    const std::string* url = dict->FindString("url");
    tab.url = url ? *url : std::string();
    const std::string* title = dict->FindString("title");
    tab.title = (title && !title->empty()) ? *title : tab.url;
    tab.selected = dict->FindBool("selected").value_or(false);
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

std::vector<CommandBarFavorite> FavoritesFromSidebarModel(
    const std::string& model_json) {
  std::vector<CommandBarFavorite> favorites;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return favorites;
  }
  const base::ListValue* rows = parsed->GetDict().FindList("favorites");
  if (!rows) {
    return favorites;
  }
  for (const base::Value& row : *rows) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* url = dict->FindString("id");
    if (!url || url->empty()) {
      continue;
    }
    CommandBarFavorite favorite;
    favorite.url = *url;
    const std::string* title = dict->FindString("title");
    // A favourite with no title falls back to its URL, which is what the tile
    // shows in that case too - an untitled row would be unmatchable and
    // unreadable at once.
    favorite.title = (title && !title->empty()) ? *title : *url;
    favorites.push_back(std::move(favorite));
  }
  return favorites;
}

std::vector<CommandBarFavorite> FavoriteMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarFavorite>& favorites) {
  // The list holds five rows, so the empty query can fill it.
  constexpr size_t kMaxFavoriteRows = 5;
  std::vector<std::pair<int, CommandBarFavorite>> hits;
  for (const CommandBarFavorite& favorite : favorites) {
    // An empty query scores every favourite zero rather than none, which is
    // the lab's rule: opening the palette shows favourites before a key is
    // pressed.
    const std::array<std::string_view, 1> fields = {favorite.title};
    const std::optional<int> score = CommandBarMatchScore(query, fields);
    if (score) {
      hits.emplace_back(*score, favorite);
    }
  }
  // Stable, so favourites scoring the same keep sidebar order - which is all
  // of them for the empty query. Sorted before the cap, or the five nearest
  // the top of the sidebar win whatever the rest scored.
  std::stable_sort(
      hits.begin(), hits.end(),
      [](const auto& a, const auto& b) { return a.first > b.first; });
  if (hits.size() > kMaxFavoriteRows) {
    hits.resize(kMaxFavoriteRows);
  }
  std::vector<CommandBarFavorite> rows;
  rows.reserve(hits.size());
  for (auto& hit : hits) {
    rows.push_back(std::move(hit.second));
  }
  return rows;
}

std::string FavoriteUrlForHost(const std::string& home,
                               const std::vector<CommandBarFavorite>& favorites) {
  const GURL home_url(home);
  // A home that carries its own path is a different destination from any
  // favourite on the same domain, and the domain match below cannot tell them
  // apart - it would send google.com/maps to a google.com favourite.
  if (home_url.path() != "/") {
    return std::string();
  }
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          home_url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty()) {
    return std::string();
  }
  for (const CommandBarFavorite& favorite : favorites) {
    if (net::registry_controlled_domains::GetDomainAndRegistry(
            GURL(favorite.url),
            net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES) ==
        domain) {
      return favorite.url;
    }
  }
  return std::string();
}

GURL EngineHomeUrl(const GURL& search_url) {
  if (!search_url.is_valid()) {
    return GURL();
  }
  GURL::Replacements strip;
  strip.ClearQuery();
  strip.ClearRef();
  return search_url.ReplaceComponents(strip);
}

std::vector<CommandBarSpace> SpacesFromSidebarModel(
    const std::string& model_json) {
  std::vector<CommandBarSpace> spaces;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return spaces;
  }
  const base::ListValue* rows = parsed->GetDict().FindList("spaces");
  if (!rows) {
    return spaces;
  }
  int index = 0;
  for (const base::Value& row : *rows) {
    // Counted before the skips below: the rail numbers every chip it draws, and
    // sidebarSpaceSelected resolves against that numbering.
    const int position = index++;
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* name = dict->FindString("name");
    if (!name || name->empty()) {
      continue;
    }
    CommandBarSpace space;
    space.index = position;
    space.name = *name;
    spaces.push_back(std::move(space));
  }
  return spaces;
}

std::vector<CommandBarSpace> SpaceMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarSpace>& spaces) {
  constexpr size_t kMaxSpaceRows = 5;
  std::vector<std::pair<int, CommandBarSpace>> hits;
  for (const CommandBarSpace& space : spaces) {
    const std::array<std::string_view, 1> fields = {space.name};
    const std::optional<int> score = CommandBarMatchScore(query, fields);
    // A hit scores hundreds and a query holding no token scores zero, so this
    // is where the empty query offers no space at all: the favourite and tab
    // producers already fill the open palette.
    if (score && *score > 0) {
      hits.emplace_back(*score, space);
    }
  }
  std::stable_sort(
      hits.begin(), hits.end(),
      [](const auto& a, const auto& b) { return a.first > b.first; });
  if (hits.size() > kMaxSpaceRows) {
    hits.resize(kMaxSpaceRows);
  }
  std::vector<CommandBarSpace> rows;
  rows.reserve(hits.size());
  for (auto& hit : hits) {
    rows.push_back(std::move(hit.second));
  }
  return rows;
}

std::vector<CommandBarMatch> EngineMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarEngine>& engines) {
  constexpr size_t kMaxEngineRows = 5;
  std::vector<std::pair<int, const CommandBarEngine*>> hits;
  for (const CommandBarEngine& engine : engines) {
    const std::array<std::string_view, 1> fields = {engine.keyword};
    const std::optional<int> score = CommandBarMatchScore(query, fields);
    // The empty query scores every engine zero: filtered out here for the
    // same reason SpaceMatchesForQuery filters it, a third open list would
    // crowd the palette before a key is pressed.
    if (score && *score > 0) {
      hits.emplace_back(*score, &engine);
    }
  }
  std::stable_sort(
      hits.begin(), hits.end(),
      [](const auto& a, const auto& b) { return a.first > b.first; });
  if (hits.size() > kMaxEngineRows) {
    hits.resize(kMaxEngineRows);
  }
  std::vector<CommandBarMatch> rows;
  rows.reserve(hits.size());
  for (const auto& hit : hits) {
    CommandBarMatch row;
    // No AutocompleteResult position backs this row - see the declaration.
    row.index = -1;
    row.title = hit.second->name;
    row.subtitle = "Search";
    row.kind = "search";
    row.keyword = hit.second->keyword;
    row.engine = hit.second->name;
    rows.push_back(std::move(row));
  }
  return rows;
}

void ApplyTabRecencyOrder(const std::vector<std::string>& recency_order,
                          std::vector<CommandBarTab>* tabs) {
  if (!tabs) {
    return;
  }
  for (CommandBarTab& tab : *tabs) {
    const auto at = std::find(recency_order.begin(), recency_order.end(),
                              tab.id);
    tab.recency = at == recency_order.end()
                      ? std::numeric_limits<size_t>::max()
                      : static_cast<size_t>(at - recency_order.begin());
  }
}

std::vector<CommandBarTab> TabMatchesForQuery(
    const std::string& query,
    const std::vector<CommandBarTab>& tabs) {
  constexpr size_t kMaxTabRows = 5;
  std::vector<std::pair<int, CommandBarTab>> hits;
  for (const CommandBarTab& tab : tabs) {
    // The tab on screen stays out of the rows and stays IN the `tabs` list
    // BuildCommandBarMatchesJSON writes, which the verb grammar counts.
    if (tab.selected) {
      continue;
    }
    // Host, not the whole URL: a path surface makes "com" hit every row
    // carrying it in a parameter.
    // Named, not inlined: host() is a view into this GURL, so a temporary one
    // would leave `fields` pointing at freed bytes.
    const GURL url(tab.url);
    const std::array<std::string_view, 2> fields = {tab.title, url.host()};
    // An empty query scores every tab zero rather than none: the palette opens
    // on what the window already holds rather than on nothing.
    const std::optional<int> score = CommandBarMatchScore(query, fields);
    if (score) {
      hits.emplace_back(*score, tab);
    }
  }
  // Score first, then recency, stable so unranked tabs keep sidebar order.
  // The empty query scores every tab alike, leaving recency alone to decide as
  // it did before. Sorted before the cap, or the five oldest rows win.
  std::stable_sort(hits.begin(), hits.end(),
                   [](const auto& a, const auto& b) {
                     return a.first != b.first
                                ? a.first > b.first
                                : a.second.recency < b.second.recency;
                   });
  if (hits.size() > kMaxTabRows) {
    hits.resize(kMaxTabRows);
  }
  std::vector<CommandBarTab> rows;
  rows.reserve(hits.size());
  for (auto& hit : hits) {
    rows.push_back(std::move(hit.second));
  }
  return rows;
}

std::string CommandBarRowURLKey(const std::string& url) {
  const GURL parsed(url);
  std::string key;
  if (parsed.is_valid()) {
    // GURL has already lowercased the scheme and the host for us.
    GURL::Replacements without_ref;
    without_ref.ClearRef();
    key = parsed.ReplaceComponents(without_ref).spec();
  } else {
    key = base::ToLowerASCII(base::TrimWhitespaceASCII(url, base::TRIM_ALL));
  }
  if (key.size() > 1 && key.back() == '/') {
    key.pop_back();
  }
  return key;
}

std::string TidyRowTitle(const std::string& title, const std::string& url) {
  const std::string collapsed = base::CollapseWhitespaceASCII(title, false);
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          GURL(url),
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty()) {
    // An IP or an intranet host names no site, so no suffix can name it.
    return collapsed;
  }
  const std::string name = domain.substr(0, domain.find('.'));
  for (std::string_view separator : {" - ", " | "}) {
    const size_t at = collapsed.rfind(separator);
    // Nothing is cut when the remainder would be empty: a row with no title
    // reads as a broken row.
    if (at == std::string::npos || at == 0) {
      continue;
    }
    const std::string_view suffix =
        std::string_view(collapsed).substr(at + separator.size());
    if (base::EqualsCaseInsensitiveASCII(suffix, name) ||
        base::EqualsCaseInsensitiveASCII(suffix, domain)) {
      return collapsed.substr(0, at);
    }
  }
  return collapsed;
}

namespace {

void RememberOfferedURL(const std::string& url, std::set<std::string>* offered) {
  std::string key = CommandBarRowURLKey(url);
  if (!key.empty()) {
    offered->insert(std::move(key));
  }
}

std::string ActiveTabURLKey(const std::vector<CommandBarTab>& tabs) {
  for (const CommandBarTab& tab : tabs) {
    if (tab.selected) {
      return CommandBarRowURLKey(tab.url);
    }
  }
  return std::string();
}

// The page already on screen keeps its row and loses the top slot, which is
// the one Enter takes.
void DerankActivePage(const std::string& active_key,
                      std::vector<CommandBarMatch>* matches) {
  if (active_key.empty() || matches->empty() ||
      CommandBarRowURLKey(matches->front().url) != active_key) {
    return;
  }
  auto first_other = matches->begin() + 1;
  while (first_other != matches->end() &&
         CommandBarRowURLKey(first_other->url) == active_key) {
    ++first_other;
  }
  if (first_other == matches->end()) {
    // Every row is the same page, so there is nothing to put above it.
    return;
  }
  std::rotate(matches->begin(), first_other, first_other + 1);
}

}  // namespace

CommandBarRows CommandBarRowsForQuery(
    const std::string& query,
    const std::vector<CommandBarFavorite>& favorites,
    const std::vector<CommandBarTab>& tabs,
    const std::vector<CommandBarMatch>& matches,
    const std::vector<CommandBarEngine>& engines) {
  CommandBarRows rows;
  std::set<std::string> offered;
  // A completion names a DESTINATION, so the row that survives for a URL takes
  // it. Without this it left with the dropped row and the palette stopped
  // completing open or starred pages.
  std::map<std::string, std::string*> completion_sink;

  rows.favorites = FavoriteMatchesForQuery(query, favorites);
  for (CommandBarFavorite& favorite : rows.favorites) {
    RememberOfferedURL(favorite.url, &offered);
    const std::string key = CommandBarRowURLKey(favorite.url);
    if (!key.empty()) {
      completion_sink.emplace(key, &favorite.inline_completion);
    }
  }

  // Filtered BEFORE TabMatchesForQuery, so its five-row cap lands on tabs that
  // survive rather than on tabs about to be dropped.
  std::vector<CommandBarTab> offerable;
  for (const CommandBarTab& tab : tabs) {
    const std::string key = CommandBarRowURLKey(tab.url);
    if (!key.empty() && offered.count(key) != 0) {
      continue;
    }
    offerable.push_back(tab);
  }
  rows.tabs = TabMatchesForQuery(query, offerable);
  for (CommandBarTab& tab : rows.tabs) {
    RememberOfferedURL(tab.url, &offered);
    const std::string key = CommandBarRowURLKey(tab.url);
    if (!key.empty()) {
      // emplace, so a favourite that claimed this URL first keeps it - that is
      // the row the page draws, and the one it would accept.
      completion_sink.emplace(key, &tab.inline_completion);
    }
  }

  for (const CommandBarMatch& match : matches) {
    const std::string key = CommandBarRowURLKey(match.url);
    if (!key.empty() && !offered.insert(key).second) {
      auto sink = completion_sink.find(key);
      if (sink != completion_sink.end() && sink->second->empty()) {
        *sink->second = match.inline_completion;
      }
      continue;
    }
    rows.matches.push_back(match);
  }
  DerankActivePage(ActiveTabURLKey(tabs), &rows.matches);

  // After deranking, so an engine offer always leads rather than landing
  // behind a page rotated out of the top slot.
  std::vector<CommandBarMatch> engine_offers =
      EngineMatchesForQuery(query, engines);
  rows.matches.insert(rows.matches.begin(),
                      std::make_move_iterator(engine_offers.begin()),
                      std::make_move_iterator(engine_offers.end()));

  // Last, and only on the rows being drawn: every producer above matched on
  // the raw title, so a query for the site name still finds its row.
  for (CommandBarTab& tab : rows.tabs) {
    tab.title = TidyRowTitle(tab.title, tab.url);
  }
  for (CommandBarMatch& match : rows.matches) {
    match.title = TidyRowTitle(match.title, match.url);
  }
  return rows;
}

std::string BuildCommandBarMatchesJSON(
    const std::string& query,
    const std::vector<CommandBarMatch>& matches,
    const std::vector<CommandBarFavorite>& favorites,
    const std::vector<CommandBarTab>& tabs,
    bool final_pass,
    const std::vector<CommandBarSpace>& spaces,
    const std::vector<CommandBarEngine>& engines) {
  // Every caller gets the hygiene pass, so no push can reach the page with the
  // same URL in three sections.
  const CommandBarRows rows =
      CommandBarRowsForQuery(query, favorites, tabs, matches, engines);

  base::ListValue list;
  for (const CommandBarMatch& match : rows.matches) {
    base::DictValue entry;
    entry.Set("index", match.index);
    entry.Set("title", match.title);
    entry.Set("subtitle", match.subtitle);
    entry.Set("url", match.url);
    entry.Set("kind", match.kind);
    entry.Set("deletable", match.deletable);
    if (!match.keyword.empty()) {
      // Omitted when there is no engine, so a row that offers no scope encodes
      // the bytes the page tests pin.
      entry.Set("keyword", match.keyword);
      entry.Set("engine", match.engine);
    }
    if (!match.inline_completion.empty()) {
      // Omitted when empty for the same reason, and every row but the default
      // one is empty.
      entry.Set("inlineCompletion", match.inline_completion);
    }
    list.Append(std::move(entry));
  }
  // Favourites lead, as they do in the lab: they are the user's own list, and
  // burying them under whatever the omnibox ranked first would make the
  // section decorative.
  base::ListValue favorite_list;
  for (const CommandBarFavorite& favorite : rows.favorites) {
    base::DictValue entry;
    entry.Set("title", favorite.title);
    entry.Set("url", favorite.url);
    entry.Set("kind", "favorite");
    if (!favorite.inline_completion.empty()) {
      entry.Set("inlineCompletion", favorite.inline_completion);
    }
    favorite_list.Append(std::move(entry));
  }

  // Every tab, not the ones matching `query`: the grammar resolves its own
  // targets by index, host glob or title fragment, and a list pre-filtered by
  // the typed text would break `close 3` the moment the text stopped matching
  // the third row.
  base::ListValue tab_list;
  for (const CommandBarTab& tab : tabs) {
    base::DictValue entry;
    entry.Set("id", tab.id);
    entry.Set("title", tab.title);
    entry.Set("url", tab.url);
    tab_list.Append(std::move(entry));
  }

  // The rows the user picks from, narrowed by the typed text. Separate from
  // `tabs` above and not a replacement for it: one is a list to read, the other
  // is a list to count positions in.
  base::ListValue tab_match_list;
  for (const CommandBarTab& tab : rows.tabs) {
    base::DictValue entry;
    entry.Set("id", tab.id);
    entry.Set("title", tab.title);
    entry.Set("url", tab.url);
    entry.Set("kind", "tab");
    if (!tab.inline_completion.empty()) {
      entry.Set("inlineCompletion", tab.inline_completion);
    }
    tab_match_list.Append(std::move(entry));
  }

  base::ListValue space_match_list;
  for (const CommandBarSpace& space : SpaceMatchesForQuery(query, spaces)) {
    base::DictValue entry;
    entry.Set("index", space.index);
    entry.Set("title", space.name);
    // The page keys a held highlight by id, and two spaces are often both
    // called "New Space" - a name alone would move the highlight to the wrong
    // one between passes.
    entry.Set("id", base::NumberToString(space.index));
    entry.Set("kind", "space");
    if (!space.binding.empty()) {
      entry.Set("binding", space.binding);
    }
    space_match_list.Append(std::move(entry));
  }

  base::DictValue snapshot;
  snapshot.Set("spaceMatches", std::move(space_match_list));
  snapshot.Set("tabMatches", std::move(tab_match_list));
  snapshot.Set("tabs", std::move(tab_list));
  snapshot.Set("query", query);
  snapshot.Set("favorites", std::move(favorite_list));
  snapshot.Set("matches", std::move(list));
  if (final_pass) {
    // Omitted when false, so an unfinished pass encodes the bytes the page
    // tests pin.
    snapshot.Set("final", true);
  }

  std::string json;
  base::JSONWriter::Write(snapshot, &json);
  return json;
}

}  // namespace atrium
