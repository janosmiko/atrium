// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_matches_unittest.cc
//
// the command bar's row text and its wire snapshot.
//
//   autoninja -C out/atrium unit_tests &&
//   out/atrium/unit_tests --gtest_filter=CommandBarMatches*

#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

CommandBarMatch Match(int index,
                      const std::string& title,
                      const std::string& subtitle,
                      const std::string& kind,
                      bool deletable) {
  CommandBarMatch match;
  match.index = index;
  match.title = title;
  match.subtitle = subtitle;
  match.kind = kind;
  match.deletable = deletable;
  return match;
}

TEST(CommandBarMatchesTest, SearchRowShowsTheQueryOnTheTitleLine) {
  const CommandBarRowText text =
      RowTextForMatch(/*is_search=*/true, "atrium browser", "");
  EXPECT_EQ("atrium browser", text.title);
  EXPECT_EQ("Search", text.subtitle);
}

TEST(CommandBarMatchesTest, VisitedPageShowsItsTitleAboveItsURL) {
  const CommandBarRowText text = RowTextForMatch(
      /*is_search=*/false, "atrium.example/docs", "Atrium documentation");
  EXPECT_EQ("Atrium documentation", text.title);
  EXPECT_EQ("atrium.example/docs", text.subtitle);
}

// A visited page with no title is the ordinary case for a URL typed once and
// never loaded. Showing an empty title line and the URL underneath reads as a
// blank row, so the URL moves up.
TEST(CommandBarMatchesTest, UntitledPageFallsBackToItsURL) {
  const CommandBarRowText text =
      RowTextForMatch(/*is_search=*/false, "atrium.example/docs", "");
  EXPECT_EQ("atrium.example/docs", text.title);
  EXPECT_EQ("", text.subtitle);
}

// A search suggestion that carries a description must NOT show it: the
// description on a search match is the engine's name, and putting it on the
// subtitle line is what "Search" already says.
TEST(CommandBarMatchesTest, SearchRowIgnoresItsDescription) {
  const CommandBarRowText text =
      RowTextForMatch(/*is_search=*/true, "atrium", "Google Search");
  EXPECT_EQ("atrium", text.title);
  EXPECT_EQ("Search", text.subtitle);
}

TEST(CommandBarMatchesTest, EmptyResultSerializesAsAnEmptyList) {
  EXPECT_EQ(
      R"({"favorites":[],"matches":[],"query":"","spaceMatches":[],)"
      R"("tabMatches":[],"tabs":[]})",
      BuildCommandBarMatchesJSON("", {}));
}

TEST(CommandBarMatchesTest, EveryFieldTheRowNeedsRidesAcross) {
  std::vector<CommandBarMatch> matches = {
      Match(0, "Atrium", "https://atrium.example/", "url", true),
      Match(1, "atrium browser", "Search", "search", false),
  };
  matches[0].url = "https://atrium.example/";
  matches[1].url = "https://search.example/?q=atrium";
  EXPECT_EQ(
      R"({"favorites":[],"matches":[)"
      R"({"deletable":true,"index":0,"kind":"url","subtitle":"https://atrium.example/","title":"Atrium","url":"https://atrium.example/"},)"
      R"({"deletable":false,"index":1,"kind":"search","subtitle":"Search","title":"atrium browser","url":"https://search.example/?q=atrium"}],)"
      R"("query":"atr","spaceMatches":[],"tabMatches":[],"tabs":[]})",
      BuildCommandBarMatchesJSON("atr", matches));
}

// The index is the browser's, not the row's. A result whose first row is
// index 3 must serialize 3 - the page sends it straight back on accept, and a
// renumbered index opens a different page than the one the user clicked.
TEST(CommandBarMatchesTest, IndexIsCarriedNotRenumbered) {
  const std::vector<CommandBarMatch> matches = {
      Match(3, "Third", "https://third.example/", "url", false),
  };
  EXPECT_NE(std::string::npos,
            BuildCommandBarMatchesJSON("t", matches).find("\"index\":3"));
}

// a favourite was not reachable from the palette at all.

CommandBarFavorite Fav(const std::string& title, const std::string& url) {
  CommandBarFavorite favorite;
  favorite.title = title;
  favorite.url = url;
  return favorite;
}

CommandBarEngine Engine(const std::string& keyword, const std::string& name) {
  return {keyword, name};
}

// Same-length keywords, so the length of the string breaks no tie here -
// only the order they arrive in.
std::vector<CommandBarEngine> GoogleAndGitHub() {
  return {Engine("google", "Google"), Engine("github", "GitHub")};
}

TEST(CommandBarMatchesTest, AFavoriteMatchesItsTitleCaseInsensitively) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Atrium Docs", "https://docs.example/"),
      Fav("Weather", "https://weather.example/"),
  };
  const std::vector<CommandBarFavorite> hits =
      FavoriteMatchesForQuery("atr", favorites);
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Atrium Docs", hits[0].title);

  EXPECT_EQ(1u, FavoriteMatchesForQuery("ATR", favorites).size())
      << "the lab lowercases both sides";
  EXPECT_TRUE(FavoriteMatchesForQuery("nothing here", favorites).empty());
}

TEST(CommandBarMatchesTest, AnEmptyQueryOffersFavoritesBeforeAKeyIsPressed) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("One", "https://one.example/"),
  };
  EXPECT_EQ(1u, FavoriteMatchesForQuery("", favorites).size());
}

TEST(CommandBarMatchesTest, FavoriteRowsAreCappedSoTheyCannotCrowdTheList) {
  std::vector<CommandBarFavorite> favorites;
  for (int i = 0; i < 9; ++i) {
    favorites.push_back(Fav("Same " + base::NumberToString(i), "https://x/"));
  }
  EXPECT_EQ(5u, FavoriteMatchesForQuery("same", favorites).size());
}

TEST(CommandBarMatchesTest, AnEngineHomeKeepsThePathThatIdentifiesTheSite) {
  EXPECT_EQ("https://www.google.com/maps",
            EngineHomeUrl(GURL("https://www.google.com/maps?q=")).spec());
}

TEST(CommandBarMatchesTest, AnEngineHomeDropsTheWholeQueryNotOnlyTheTerms) {
  // GenerateSearchURL has already substituted the terms away, so what is left
  // beside the empty q= configures a SEARCH - an image search is not a home.
  EXPECT_EQ("https://www.google.com/search",
            EngineHomeUrl(GURL("https://www.google.com/search?tbm=isch&q="))
                .spec());
}

TEST(CommandBarMatchesTest, AnEngineHomeDropsTheFragment) {
  EXPECT_EQ("https://example.test/find",
            EngineHomeUrl(GURL("https://example.test/find?q=#results")).spec());
}

// The placeholder can sit in the PATH rather than the query, which leaves the
// emptied segment behind. That trailing slash is the engine's own directory,
// so it stays rather than being guessed away.
TEST(CommandBarMatchesTest, AnEngineHomeKeepsAPathEmptiedOfItsSearchTerms) {
  EXPECT_EQ("https://www.google.com/maps/search/",
            EngineHomeUrl(GURL("https://www.google.com/maps/search/")).spec());
}

// A doubled slash is a real path segment, so it is carried through rather than
// tidied: a fixture that joined a spec ending in "/" with "/search" is the bug,
// and collapsing it here would hide the next one.
TEST(CommandBarMatchesTest, AnEngineHomeKeepsADoubledSlashRatherThanTidyingIt) {
  EXPECT_EQ("http://site.test:49167//search",
            EngineHomeUrl(GURL("http://site.test:49167//search?q=")).spec());
}

TEST(CommandBarMatchesTest, AnEngineHomeKeepsAPortSoLoopbackEnginesResolve) {
  EXPECT_EQ(
      "http://127.0.0.1:8000/engine-alpha.html",
      EngineHomeUrl(GURL("http://127.0.0.1:8000/engine-alpha.html?q=")).spec());
}

TEST(CommandBarMatchesTest, AnEngineHomeIsInvalidWhenTheUrlDoesNotParse) {
  EXPECT_FALSE(EngineHomeUrl(GURL("not a url")).is_valid());
  EXPECT_FALSE(EngineHomeUrl(GURL()).is_valid());
}

// This is a url transform, NOT a sanitiser: both of these parse, so they come
// back unchanged. The scheme guards are the mirror's IsAllowedEngineUrl on the
// way in (https, or http to loopback) and OpenSidebarColdTab on the way out.
TEST(CommandBarMatchesTest, AnEngineHomeDoesNotRejectADangerousScheme) {
  EXPECT_TRUE(EngineHomeUrl(GURL("javascript:alert(1)")).is_valid());
  EXPECT_TRUE(EngineHomeUrl(GURL("data:text/html,hi")).is_valid());
}

// A favourite saved on youtube.com never matched an engine
// home of www.youtube.com by exact string, so Enter on the engine row always
// opened a second tab instead of switching to the favourite's.
TEST(CommandBarMatchesTest, AFavoriteWithoutWwwStillClaimsAWwwEngineHome) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("YouTube", "https://youtube.com/"),
  };
  EXPECT_EQ("https://youtube.com/",
           FavoriteUrlForHost("https://www.youtube.com/", favorites));
}

// The domain match is too coarse to redirect an engine whose home carries a
// path: sending google.com/maps to a google.com favourite drops /maps, which is
// the whole thing the home derivation exists to keep.
TEST(CommandBarMatchesTest, NoFavoriteClaimsAnEngineHomeThatCarriesAPath) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Google", "https://www.google.com/"),
  };
  EXPECT_EQ("", FavoriteUrlForHost("https://www.google.com/maps", favorites));
}

TEST(CommandBarMatchesTest, AFavoriteStillClaimsARootEngineHome) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Google", "https://www.google.com/"),
  };
  EXPECT_EQ("https://www.google.com/",
            FavoriteUrlForHost("https://www.google.com/", favorites));
}

TEST(CommandBarMatchesTest, AFavoriteOnASubpageStillClaimsTheBareEngineHome) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Docs", "https://docs.github.com/atrium"),
  };
  EXPECT_EQ("https://docs.github.com/atrium",
           FavoriteUrlForHost("https://github.com/", favorites));
}

TEST(CommandBarMatchesTest, NoFavoriteClaimsAnUnrelatedEngineHome) {
  const std::vector<CommandBarFavorite> favorites = {
      // A host that merely CONTAINS the engine's domain as a prefix - this
      // must not match, or an engine offer could be hijacked to a page on a
      // different registrable domain entirely.
      Fav("Not YouTube", "https://youtube.com.attacker.net/"),
      Fav("Not It", "https://vimeo.com/"),
  };
  EXPECT_EQ("", FavoriteUrlForHost("https://www.youtube.com/", favorites));
}

// A favourite carries a URL and NO index. The page sends an index straight back
// to the AutocompleteResult, and a favourite is not in it - serializing one
// would accept an unrelated row.
TEST(CommandBarMatchesTest, AFavoriteSerializesWithItsUrlAndNoIndex) {
  const std::string json = BuildCommandBarMatchesJSON(
      "atr", {}, {Fav("Atrium Docs", "https://docs.example/")});
  EXPECT_NE(std::string::npos, json.find(R"("kind":"favorite")"));
  EXPECT_NE(std::string::npos, json.find(R"("url":"https://docs.example/")"));
  EXPECT_EQ(std::string::npos, json.find(R"("index")"));
}

TEST(CommandBarMatchesTest, FavoritesAreReadOffTheSidebarModel) {
  const std::string model = R"({"favorites":[
      {"id":"https://docs.example/","title":"Atrium Docs"},
      {"id":"https://weather.example/","title":"Weather"}]})";
  const std::vector<CommandBarFavorite> favorites =
      FavoritesFromSidebarModel(model);
  ASSERT_EQ(2u, favorites.size());
  EXPECT_EQ("Atrium Docs", favorites[0].title);
  EXPECT_EQ("https://docs.example/", favorites[0].url);
}

TEST(CommandBarMatchesTest, AnUntitledFavoriteFallsBackToItsUrl) {
  // The tile does the same. A row with an empty title would be unmatchable and
  // unreadable at once.
  const std::vector<CommandBarFavorite> favorites = FavoritesFromSidebarModel(
      R"({"favorites":[{"id":"https://x.example/","title":""}]})");
  ASSERT_EQ(1u, favorites.size());
  EXPECT_EQ("https://x.example/", favorites[0].title);
}

TEST(CommandBarMatchesTest, AModelWithNothingUsableYieldsNoFavorites) {
  // Each of these is a real shape: the pre-dylib fall-through snapshot carries
  // no favourites key at all, and a row with no id names nothing to open.
  EXPECT_TRUE(FavoritesFromSidebarModel("").empty());
  EXPECT_TRUE(FavoritesFromSidebarModel("not json").empty());
  EXPECT_TRUE(FavoritesFromSidebarModel(R"({"tabs":[]})").empty());
  EXPECT_TRUE(
      FavoritesFromSidebarModel(R"({"favorites":[{"title":"No id"}]})").empty());
}


// TASK: the verb grammar resolved against a list nothing ever sent, so every
// command reported that nothing matched.
TEST(CommandBarMatchesTest, TabsComeFromTheSidebarModel) {
  const std::string model = R"({"tabs":[
      {"id":"t1","title":"Team chat","url":"https://slack.example/a"},
      {"id":"","title":"Persisted","url":"https://cold.example/"},
      {"id":"t3","title":"","url":"https://docs.example/"}]})";

  const std::vector<CommandBarTab> tabs = TabsFromSidebarModel(model);

  ASSERT_EQ(2u, tabs.size()) << "the row with no live id has nothing to act on";
  EXPECT_EQ("t1", tabs[0].id);
  EXPECT_EQ("Team chat", tabs[0].title);
  EXPECT_EQ("https://docs.example/", tabs[1].title)
      << "an untitled row falls back to its url, or it is unmatchable";
}

TEST(CommandBarMatchesTest, SnapshotCarriesEveryTabNotJustMatches) {
  std::vector<CommandBarTab> tabs;
  CommandBarTab tab;
  tab.id = "t1";
  tab.title = "Docs";
  tab.url = "https://docs.example/";
  tabs.push_back(tab);

  // A query that matches nothing in the list: the grammar resolves its own
  // targets, so filtering by the typed text would break `close 1`.
  const std::string json =
      BuildCommandBarMatchesJSON("zzz", {}, {}, tabs);

  EXPECT_NE(std::string::npos, json.find("\"tabs\""));
  EXPECT_NE(std::string::npos, json.find("\"t1\""));
}

// A window with no favourites opened a blank palette: the only rows the empty
// query could produce were favourites, and a fresh profile has none.

CommandBarTab Tab(const std::string& id,
                  const std::string& title,
                  const std::string& url) {
  CommandBarTab tab;
  tab.id = id;
  tab.title = title;
  tab.url = url;
  return tab;
}

std::vector<CommandBarTab> ThreeOpenTabs() {
  return {Tab("t1", "Team chat", "https://slack.example/a"),
          Tab("t2", "Atrium docs", "https://docs.atrium.example/guide"),
          Tab("t3", "Weather", "https://weather.example/today")};
}

TEST(CommandBarMatchesTest, AnEmptyQueryOffersEveryOpenTabInSidebarOrder) {
  const std::vector<CommandBarTab> hits =
      TabMatchesForQuery("", ThreeOpenTabs());
  ASSERT_EQ(3u, hits.size());
  EXPECT_EQ("t1", hits[0].id);
  EXPECT_EQ("t3", hits[2].id);
}

TEST(CommandBarMatchesTest, ATabMatchesItsTitleCaseInsensitively) {
  const std::vector<CommandBarTab> hits =
      TabMatchesForQuery("CHAT", ThreeOpenTabs());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Team chat", hits[0].title);
}

// The host is how a tab is named when its title says nothing useful - half the
// web titles a page "Home".
TEST(CommandBarMatchesTest, ATabMatchesItsHost) {
  const std::vector<CommandBarTab> hits =
      TabMatchesForQuery("weather.example", ThreeOpenTabs());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("t3", hits[0].id);
}

// The path is NOT matched: "guide" appears in one URL and in no title or host,
// and a hit here would make a query like "com" match every row carrying it in
// a parameter.
TEST(CommandBarMatchesTest, ATabDoesNotMatchOnItsPath) {
  EXPECT_TRUE(TabMatchesForQuery("guide", ThreeOpenTabs()).empty());
}

TEST(CommandBarMatchesTest, TabRowsAreCappedAtTheHeightOfTheList) {
  std::vector<CommandBarTab> tabs;
  for (int i = 0; i < 9; ++i) {
    tabs.push_back(Tab("t" + base::NumberToString(i), "Same",
                       "https://same.example/"));
  }
  EXPECT_EQ(5u, TabMatchesForQuery("", tabs).size());
  EXPECT_EQ(5u, TabMatchesForQuery("same", tabs).size());
}

TEST(CommandBarMatchesTest, ATabRowSerializesWithItsLiveIdAndNoIndex) {
  const std::string json = BuildCommandBarMatchesJSON(
      "", {}, {}, {Tab("t1", "Docs", "https://docs.example/")});
  EXPECT_NE(std::string::npos, json.find(R"("kind":"tab")"));
  EXPECT_NE(std::string::npos, json.find(R"("id":"t1")"));
  EXPECT_EQ(std::string::npos, json.find(R"("index")"));
}

// The verb grammar resolves `close 3` against `tabs` by position, so that list
// must stay whole even while `tabMatches` narrows to what was typed.
TEST(CommandBarMatchesTest, FilteringTheTabRowsLeavesTheVerbListWhole) {
  const std::string json =
      BuildCommandBarMatchesJSON("chat", {}, {}, ThreeOpenTabs());
  size_t rows = 0;
  for (size_t at = json.find(R"("kind":"tab")"); at != std::string::npos;
       at = json.find(R"("kind":"tab")", at + 1)) {
    ++rows;
  }
  EXPECT_EQ(1u, rows) << "only Team chat matches";
  EXPECT_NE(std::string::npos, json.find(R"("id":"t2")"))
      << "the verb grammar indexes `tabs` by position, so it stays whole";
  EXPECT_NE(std::string::npos, json.find(R"("id":"t3")"));
}

TEST(CommandBarMatchesTest, TheSelectedFlagComesFromTheSidebarModel) {
  const std::vector<CommandBarTab> tabs = TabsFromSidebarModel(R"({"tabs":[
      {"id":"t1","title":"Team chat","url":"https://slack.example/a"},
      {"id":"t2","title":"Weather","url":"https://weather.example/",
       "selected":true}]})");

  ASSERT_EQ(2u, tabs.size());
  EXPECT_FALSE(tabs[0].selected);
  EXPECT_TRUE(tabs[1].selected);
}

TEST(CommandBarMatchesTest, TheActiveTabIsNotOffered) {
  std::vector<CommandBarTab> tabs = ThreeOpenTabs();
  tabs[1].selected = true;

  const std::vector<CommandBarTab> hits = TabMatchesForQuery("", tabs);

  ASSERT_EQ(2u, hits.size()) << "the tab already on screen is not somewhere to "
                                "switch to";
  EXPECT_EQ("t1", hits[0].id);
  EXPECT_EQ("t3", hits[1].id);
}

TEST(CommandBarMatchesTest, TheActiveTabStaysInTheVerbList) {
  std::vector<CommandBarTab> tabs = ThreeOpenTabs();
  tabs[1].selected = true;

  const std::string json = BuildCommandBarMatchesJSON("", {}, {}, tabs);
  size_t rows = 0;
  for (size_t at = json.find(R"("kind":"tab")"); at != std::string::npos;
       at = json.find(R"("kind":"tab")", at + 1)) {
    ++rows;
  }

  EXPECT_EQ(2u, rows) << "the active tab is not a row to pick";
  EXPECT_NE(std::string::npos, json.find(R"("id":"t2")"))
      << "`close 2` still counts positions against every tab the window holds";
}

TEST(CommandBarMatchesTest, TabRowsFollowTheRecencyOrder) {
  std::vector<CommandBarTab> tabs = ThreeOpenTabs();
  ApplyTabRecencyOrder({"t3", "t1", "t2"}, &tabs);

  const std::vector<CommandBarTab> hits = TabMatchesForQuery("", tabs);

  ASSERT_EQ(3u, hits.size());
  EXPECT_EQ("t3", hits[0].id);
  EXPECT_EQ("t1", hits[1].id);
  EXPECT_EQ("t2", hits[2].id);
}

TEST(CommandBarMatchesTest, AnUnrankedTabSortsBehindEveryRankedOne) {
  std::vector<CommandBarTab> tabs = ThreeOpenTabs();
  // A window the user has not touched since restore ranks nothing: the rows
  // must still come out in sidebar order rather than in an arbitrary one.
  ApplyTabRecencyOrder({"t3"}, &tabs);

  const std::vector<CommandBarTab> hits = TabMatchesForQuery("", tabs);

  ASSERT_EQ(3u, hits.size());
  EXPECT_EQ("t3", hits[0].id);
  EXPECT_EQ("t1", hits[1].id);
  EXPECT_EQ("t2", hits[2].id);
}

// The cap used to be applied while walking sidebar order, so on a window with
// more tabs than rows the five oldest won and the tab the user came from was
// never offered.
TEST(CommandBarMatchesTest, TheCapKeepsTheMostRecentTabsNotTheFirstRows) {
  std::vector<CommandBarTab> tabs;
  std::vector<std::string> recency_order;
  for (int i = 0; i < 9; ++i) {
    const std::string id = "t" + base::NumberToString(i);
    tabs.push_back(Tab(id, "Same", "https://same.example/"));
    recency_order.insert(recency_order.begin(), id);
  }
  ApplyTabRecencyOrder(recency_order, &tabs);

  const std::vector<CommandBarTab> hits = TabMatchesForQuery("", tabs);

  ASSERT_EQ(5u, hits.size());
  EXPECT_EQ("t8", hits[0].id);
  EXPECT_EQ("t4", hits[4].id);
}

// --- spaces as rows ---------------------------------------------------------

std::vector<CommandBarSpace> TwoSpaces() {
  return SpacesFromSidebarModel(R"({"spaces":[
      {"name":"Work","icon":"W"},
      {"name":"Personal","icon":"P"}]})");
}

TEST(CommandBarMatchesTest, SpacesComeFromTheSidebarModel) {
  const std::vector<CommandBarSpace> spaces = TwoSpaces();

  ASSERT_EQ(2u, spaces.size());
  EXPECT_EQ("Work", spaces[0].name);
  EXPECT_EQ(0, spaces[0].index);
  EXPECT_EQ("Personal", spaces[1].name);
  EXPECT_EQ(1, spaces[1].index)
      << "sidebarSpaceSelected takes the rail position, so it must survive";
}

TEST(CommandBarMatchesTest, AModelWithNothingUsableYieldsNoSpaces) {
  EXPECT_TRUE(SpacesFromSidebarModel("").empty());
  EXPECT_TRUE(SpacesFromSidebarModel("not json").empty());
  EXPECT_TRUE(SpacesFromSidebarModel(R"({"tabs":[]})").empty());
}

TEST(CommandBarMatchesTest, AnUnnamedSpaceIsNotOffered) {
  // The row would be blank, so there would be nothing to read and nothing to
  // type at - and the position it holds still counts for the ones after it.
  const std::vector<CommandBarSpace> spaces = SpacesFromSidebarModel(
      R"({"spaces":[{"name":""},{"name":"Second"}]})");

  ASSERT_EQ(1u, spaces.size());
  EXPECT_EQ("Second", spaces[0].name);
  EXPECT_EQ(1, spaces[0].index);
}

TEST(CommandBarMatchesTest, ASpaceMatchesItsNameCaseInsensitively) {
  const std::vector<CommandBarSpace> hits =
      SpaceMatchesForQuery("PERSON", TwoSpaces());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Personal", hits[0].name);
  EXPECT_EQ(1, hits[0].index);
}

TEST(CommandBarMatchesTest, AnEmptyQueryOffersNoSpaces) {
  EXPECT_TRUE(SpaceMatchesForQuery("", TwoSpaces()).empty());
}

TEST(CommandBarMatchesTest, SpaceRowsAreCappedAtTheHeightOfTheList) {
  std::vector<CommandBarSpace> spaces;
  for (int i = 0; i < 9; ++i) {
    CommandBarSpace space;
    space.index = i;
    space.name = "Same " + base::NumberToString(i);
    spaces.push_back(space);
  }
  EXPECT_EQ(5u, SpaceMatchesForQuery("same", spaces).size());
}

TEST(CommandBarMatchesTest, ASpaceRowSerializesWithItsRailPosition) {
  CommandBarSpace space;
  space.index = 2;
  space.name = "Personal";

  const std::string json = BuildCommandBarMatchesJSON(
      "person", {}, {}, {}, /*final_pass=*/false, {space});

  EXPECT_NE(std::string::npos, json.find(R"("spaceMatches")"));
  EXPECT_NE(std::string::npos, json.find(R"("kind":"space")"));
  EXPECT_NE(std::string::npos, json.find(R"("index":2)"));
  EXPECT_NE(std::string::npos, json.find(R"("title":"Personal")"));
}

// the field BrowserController::CommandBarSpaces() fills from
// NativeHotkeyMap, read by the page's shared keycaps rendering.
TEST(CommandBarMatchesTest, ASpaceRowWithABindingCarriesItsGlyph) {
  CommandBarSpace space;
  space.index = 0;
  space.name = "Personal";
  space.binding = "⇧⌘1";

  const std::string json = BuildCommandBarMatchesJSON(
      "person", {}, {}, {}, /*final_pass=*/false, {space});

  EXPECT_NE(std::string::npos, json.find(R"("binding":"⇧⌘1")"));
}

TEST(CommandBarMatchesTest, ASpaceRowWithNoBindingOmitsTheField) {
  CommandBarSpace space;
  space.index = 0;
  space.name = "Personal";

  const std::string json = BuildCommandBarMatchesJSON(
      "person", {}, {}, {}, /*final_pass=*/false, {space});

  EXPECT_EQ(std::string::npos, json.find(R"("binding")"));
}

TEST(CommandBarMatchesTest, TheEmptyQuerySnapshotCarriesNoSpaceRows) {
  CommandBarSpace space;
  space.name = "Work";

  const std::string json =
      BuildCommandBarMatchesJSON("", {}, {}, {}, /*final_pass=*/false, {space});

  EXPECT_EQ(std::string::npos, json.find(R"("kind":"space")"));
}

// --- Tab-to-search scope ----------------------------------------------------

TEST(CommandBarMatchesTest, ARowWithAnEngineCarriesItsKeywordAndName) {
  CommandBarMatch match = Match(0, "atrium", "Search", "search", false);
  match.keyword = "bing.com";
  match.engine = "Bing";

  const std::string json = BuildCommandBarMatchesJSON("atr", {match});

  EXPECT_NE(std::string::npos, json.find(R"("keyword":"bing.com")"));
  EXPECT_NE(std::string::npos, json.find(R"("engine":"Bing")"));
}

// The page reads the keyword to decide whether Tab offers a scope at all, so a
// row with no engine must carry neither field - and the byte-pinned snapshot
// above is the same assertion from the other side.
TEST(CommandBarMatchesTest, ARowWithNoEngineCarriesNeitherField) {
  const std::string json = BuildCommandBarMatchesJSON(
      "atr", {Match(0, "Atrium", "https://atrium.example/", "url", false)});

  EXPECT_EQ(std::string::npos, json.find(R"("keyword")"));
  EXPECT_EQ(std::string::npos, json.find(R"("engine")"));
}

// --- Tab-to-search scope: keyword prefixes ----------------------------------

TEST(CommandBarMatchesTest, AOneLetterPrefixOffersEveryEngineItMatches) {
  const std::vector<CommandBarMatch> hits =
      EngineMatchesForQuery("g", GoogleAndGitHub());
  ASSERT_EQ(2u, hits.size());
  EXPECT_EQ("google", hits[0].keyword);
  EXPECT_EQ("github", hits[1].keyword);
}

// The tie-break is the order the engines arrived in, not their name or
// keyword length - "google" and "github" are both six letters.
TEST(CommandBarMatchesTest, ATiedPrefixKeepsTheCallersEngineOrder) {
  const std::vector<CommandBarMatch> googleFirst =
      EngineMatchesForQuery("g", GoogleAndGitHub());
  EXPECT_EQ("google", googleFirst[0].keyword);

  const std::vector<CommandBarMatch> githubFirst = EngineMatchesForQuery(
      "g", {Engine("github", "GitHub"), Engine("google", "Google")});
  EXPECT_EQ("github", githubFirst[0].keyword);
}

TEST(CommandBarMatchesTest, ALongerPrefixNarrowsToTheOneEngineItMatches) {
  const std::vector<CommandBarMatch> hits =
      EngineMatchesForQuery("gi", GoogleAndGitHub());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("github", hits[0].keyword);
  EXPECT_EQ("GitHub", hits[0].engine);
}

TEST(CommandBarMatchesTest, APrefixMatchingNoEngineOffersNone) {
  EXPECT_TRUE(EngineMatchesForQuery("xyz", GoogleAndGitHub()).empty());
}

TEST(CommandBarMatchesTest, AnEnginePrefixMatchesCaseInsensitively) {
  const std::vector<CommandBarMatch> hits =
      EngineMatchesForQuery("G", GoogleAndGitHub());
  EXPECT_EQ(2u, hits.size());
}

// The row Tab acts on is whichever one the page highlights, and the page
// highlights row zero - so the best engine has to be the row this function
// puts first, not merely somewhere in the list.
TEST(CommandBarMatchesTest, TabTakesTheFirstRowsEngine) {
  const std::vector<CommandBarMatch> hits =
      EngineMatchesForQuery("g", GoogleAndGitHub());
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ("google", hits[0].keyword);
  EXPECT_EQ("Google", hits[0].engine);
  EXPECT_EQ(-1, hits[0].index)
      << "there is no AutocompleteResult row behind an engine offer";
}

// A user typing "git" for the word still gets the ordinary matches - the
// engine offer rides alongside them rather than taking their place.
TEST(CommandBarMatchesTest, APrefixOfferSitsAlongsideOrdinaryMatches) {
  const std::vector<CommandBarMatch> matches = {
      Match(0, "git - Google Search", "Search", "search", false)};

  const CommandBarRows rows =
      CommandBarRowsForQuery("git", {}, {}, matches, GoogleAndGitHub());

  ASSERT_EQ(2u, rows.matches.size());
  EXPECT_EQ("github", rows.matches[0].keyword)
      << "the engine offer leads, since it is what Tab would act on";
  EXPECT_EQ("git - Google Search", rows.matches[1].title)
      << "the plain search for the same letters is still reachable";
}

// Nothing here touches ScopeEngineForMatch's own path: a row the bridge
// already scoped to a full keyword keeps its keyword untouched when the
// query also happens to prefix-match the profile's engine list.
TEST(CommandBarMatchesTest, AFullKeywordRowIsUnaffectedByThePrefixOffer) {
  CommandBarMatch match = Match(0, "atrium", "Search", "search", false);
  match.keyword = "google";
  match.engine = "Google";

  const CommandBarRows rows =
      CommandBarRowsForQuery("google", {}, {}, {match}, GoogleAndGitHub());

  ASSERT_EQ(2u, rows.matches.size());
  EXPECT_EQ("google", rows.matches[1].keyword);
  EXPECT_EQ("atrium", rows.matches[1].title)
      << "the full-keyword row itself is untouched";
}

// --- result hygiene ---------------------------------------------------------

CommandBarMatch UrlMatch(int index,
                         const std::string& title,
                         const std::string& url) {
  CommandBarMatch match = Match(index, title, url, "url", false);
  match.url = url;
  return match;
}

TEST(CommandBarMatchesTest, TheURLKeyIgnoresCaseTrailingSlashAndFragment) {
  const std::string key = CommandBarRowURLKey("https://docs.example.com/guide");

  EXPECT_EQ(key, CommandBarRowURLKey("https://docs.example.com/guide/"));
  EXPECT_EQ(key, CommandBarRowURLKey("HTTPS://DOCS.EXAMPLE.COM/guide"));
  EXPECT_EQ(key, CommandBarRowURLKey("https://docs.example.com/guide#intro"));
  EXPECT_NE(key, CommandBarRowURLKey("https://docs.example.com/other"));
  // A row with nowhere to go must not collapse into every other such row.
  EXPECT_TRUE(CommandBarRowURLKey("").empty());
}

TEST(CommandBarMatchesTest, OneURLInThreeSectionsCollapsesToTheFavorite) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Atrium Docs", "https://docs.example.com/guide")};
  const std::vector<CommandBarTab> tabs = {
      Tab("t1", "Atrium docs", "https://docs.example.com/guide/"),
      Tab("t2", "Weather", "https://weather.example.com/")};
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Atrium docs", "https://docs.example.com/guide#intro")};

  const CommandBarRows rows =
      CommandBarRowsForQuery("docs", favorites, tabs, matches);

  ASSERT_EQ(1u, rows.favorites.size());
  EXPECT_TRUE(rows.tabs.empty()) << "the favourite already offers that page";
  EXPECT_TRUE(rows.matches.empty()) << "and so does the tab above it";
}

TEST(CommandBarMatchesTest, AnOpenTabBeatsTheOmniboxRowForTheSamePage) {
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Team chat", "https://slack.example.com/a"),
      UrlMatch(1, "Team chat archive", "https://slack.example.com/archive")};

  const CommandBarRows rows = CommandBarRowsForQuery(
      "chat", {}, {Tab("t1", "Team chat", "https://slack.example.com/a")},
      matches);

  ASSERT_EQ(1u, rows.tabs.size());
  ASSERT_EQ(1u, rows.matches.size())
      << "only the row the tab already offers is dropped";
  EXPECT_EQ(1, rows.matches[0].index);
}

TEST(CommandBarMatchesTest, TwoOmniboxRowsForOnePageCollapse) {
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Atrium", "https://atrium.example.com/"),
      UrlMatch(1, "Atrium", "https://atrium.example.com")};

  const CommandBarRows rows = CommandBarRowsForQuery("atrium", {}, {}, matches);

  ASSERT_EQ(1u, rows.matches.size());
  EXPECT_EQ(0, rows.matches[0].index) << "the higher-ranked row is the keeper";
}

std::vector<CommandBarTab> TabsWithGuideOnScreen() {
  std::vector<CommandBarTab> tabs = {
      Tab("t1", "Team chat", "https://slack.example.com/a"),
      Tab("t2", "Atrium guide", "https://docs.example.com/guide")};
  tabs[1].selected = true;
  return tabs;
}

TEST(CommandBarMatchesTest, TheOpenPageDoesNotTakeTheTopOmniboxSlot) {
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Atrium guide", "https://docs.example.com/guide"),
      UrlMatch(1, "Atrium downloads", "https://docs.example.com/downloads")};

  const CommandBarRows rows = CommandBarRowsForQuery(
      "atrium", {}, TabsWithGuideOnScreen(), matches);

  ASSERT_EQ(2u, rows.matches.size());
  EXPECT_EQ(1, rows.matches[0].index) << "Enter must not reload the same page";
  EXPECT_EQ(0, rows.matches[1].index) << "deranked, not dropped";
}

TEST(CommandBarMatchesTest, TheOpenPageKeepsTheTopSlotWhenNothingElseMatched) {
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Atrium guide", "https://docs.example.com/guide")};

  const CommandBarRows rows = CommandBarRowsForQuery(
      "guide", {}, TabsWithGuideOnScreen(), matches);

  ASSERT_EQ(1u, rows.matches.size())
      << "a deranked row with nothing to go behind is still the whole list";
  EXPECT_EQ(0, rows.matches[0].index);
}

TEST(CommandBarMatchesTest, ATrailingSiteNameIsCutFromTheTitle) {
  EXPECT_EQ("Export the ring",
            TidyRowTitle("Export the ring - GitHub",
                         "https://github.com/janosmiko/atrium/issues/1"));
  EXPECT_EQ("Roadmap",
            TidyRowTitle("Roadmap | Notion", "https://www.notion.so/roadmap"));
  EXPECT_EQ("Weekly notes",
            TidyRowTitle("Weekly notes - github.com", "https://github.com/w"));
  EXPECT_EQ("Atrium weekly",
            TidyRowTitle("Atrium\n   weekly ", "https://github.com/w"))
      << "runs of whitespace read as a broken title";
}

TEST(CommandBarMatchesTest, ASuffixThatIsNotTheSiteNameStays) {
  EXPECT_EQ("Atrium - the browser",
            TidyRowTitle("Atrium - the browser", "https://github.com/atrium"));
  EXPECT_EQ("- GitHub", TidyRowTitle(" - GitHub", "https://github.com/"))
      << "cutting would leave nothing to read";
  EXPECT_EQ("Docs - Intranet", TidyRowTitle("Docs - Intranet", "http://wiki/d"))
      << "a host with no registry names no site";
  EXPECT_EQ("Docs - GitHub",
            TidyRowTitle("Docs - GitHub", "https://gitlab.com/d"))
      << "the suffix must name this row's own site";
}

// The pass has to run inside the serializer, or every rule above is a helper
// nothing calls - which is how an inert derank once shipped.
TEST(CommandBarMatchesTest, TheSnapshotAppliesEveryHygieneRule) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Atrium Docs", "https://docs.github.com/atrium")};
  std::vector<CommandBarTab> tabs = {
      Tab("t1", "Atrium docs - GitHub", "https://docs.github.com/atrium/"),
      Tab("t2", "Atrium roadmap - GitHub", "https://github.com/atrium/roadmap"),
      Tab("t3", "Atrium home - GitHub", "https://github.com/atrium")};
  tabs[2].selected = true;
  const std::vector<CommandBarMatch> matches = {
      UrlMatch(0, "Atrium home - GitHub", "https://github.com/atrium"),
      UrlMatch(1, "Atrium releases - GitHub",
               "https://github.com/atrium/releases")};

  const std::string json =
      BuildCommandBarMatchesJSON("atrium", matches, favorites, tabs);

  size_t tab_rows = 0;
  for (size_t at = json.find(R"("kind":"tab")"); at != std::string::npos;
       at = json.find(R"("kind":"tab")", at + 1)) {
    ++tab_rows;
  }
  EXPECT_EQ(1u, tab_rows) << "the favourite's page and the open page are gone";
  EXPECT_NE(std::string::npos, json.find(R"("id":"t2","kind":"tab")"));
  EXPECT_NE(std::string::npos, json.find(R"("title":"Atrium roadmap")"));
  EXPECT_NE(std::string::npos, json.find(R"("title":"Atrium home - GitHub")"))
      << "the verb grammar's list keeps every tab and its raw title";
  EXPECT_LT(json.find(R"("index":1)"), json.find(R"("index":0)"))
      << "the page already on screen is deranked";
}

// --- Inline autocomplete ----------------------------------------------------

TEST(CommandBarMatchesTest, TheDefaultMatchCarriesItsInlineCompletion) {
  CommandBarMatch match =
      Match(0, "Atrium", "https://atrium.example/", "url", false);
  match.inline_completion = "ium.example";

  const std::string json = BuildCommandBarMatchesJSON("atr", {match});

  EXPECT_NE(std::string::npos, json.find(R"("inlineCompletion":"ium.example")"));
}

// The omnibox fills inline_autocompletion on the default match alone, so every
// other row would spend four bytes on an empty string - and the page reads the
// key's presence, not its length.
TEST(CommandBarMatchesTest, ARowWithNoCompletionCarriesNoField) {
  const std::string json = BuildCommandBarMatchesJSON(
      "atr", {Match(0, "Atrium", "https://atrium.example/", "url", false)});

  EXPECT_EQ(std::string::npos, json.find(R"("inlineCompletion")"));
}

// A completion belongs to a DESTINATION, not to the provider that offered it.
// Before this it left with the dropped row, so the palette stopped completing
// exactly the pages the user has open or has starred.
TEST(CommandBarMatchesTest, AFavoriteInheritsTheCompletionOfTheRowItReplaces) {
  CommandBarMatch match = UrlMatch(0, "Atrium", "https://atrium.example/");
  match.inline_completion = "ium.example";

  const std::string json = BuildCommandBarMatchesJSON(
      "atr", {match}, {Fav("Atrium", "https://atrium.example/")});

  EXPECT_EQ(std::string::npos, json.find(R"("kind":"url")"))
      << "the favourite took the row, so the completion has nowhere else to be";
  EXPECT_NE(std::string::npos, json.find(R"("kind":"favorite")"));
  EXPECT_NE(std::string::npos, json.find(R"("inlineCompletion":"ium.example")"));
}

TEST(CommandBarMatchesTest, AnOpenTabInheritsTheCompletionOfTheRowItReplaces) {
  CommandBarMatch match = UrlMatch(0, "Atrium", "https://atrium.example/");
  match.inline_completion = "ium.example";

  const std::string json = BuildCommandBarMatchesJSON(
      "atr", {match}, {}, {Tab("t1", "Atrium", "https://atrium.example/")});

  EXPECT_EQ(std::string::npos, json.find(R"("kind":"url")"));
  EXPECT_NE(std::string::npos, json.find(R"("kind":"tab")"));
  EXPECT_NE(std::string::npos, json.find(R"("inlineCompletion":"ium.example")"));
}

TEST(CommandBarMatchesTest, AnOmniboxRowKeepsItsCompletionWhenNothingTakesIt) {
  CommandBarMatch match = UrlMatch(0, "Atrium", "https://atrium.example/");
  match.inline_completion = "ium.example";

  const std::string json = BuildCommandBarMatchesJSON("atr", {match});

  EXPECT_NE(std::string::npos, json.find(R"("kind":"url")"));
  EXPECT_NE(std::string::npos, json.find(R"("inlineCompletion":"ium.example")"));
}

// --- the three producers share one matcher -----------------------------------

TEST(CommandBarMatchesTest, TwoQueryWordsFindAFavoriteNeitherOneSpells) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("GitHub Issues", "https://github.example/issues"),
      Fav("Weather", "https://weather.example/"),
  };
  const std::vector<CommandBarFavorite> hits =
      FavoriteMatchesForQuery("gh iss", favorites);
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("GitHub Issues", hits[0].title);
}

TEST(CommandBarMatchesTest, AFavoriteMissStaysAMiss) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("GitHub Issues", "https://github.example/issues"),
  };
  EXPECT_TRUE(FavoriteMatchesForQuery("xyz", favorites).empty());
  EXPECT_TRUE(FavoriteMatchesForQuery("gh xyz", favorites).empty())
      << "one unmatched word disqualifies the row";
}

// Ranking, not just filtering: the fuzzy half would otherwise put a row the
// user half-remembered above the one they typed the start of.
TEST(CommandBarMatchesTest, AFavoritePrefixHitOutranksAFuzzyOne) {
  const std::vector<CommandBarFavorite> favorites = {
      Fav("Gist Notes", "https://gist.example/"),
      Fav("GitHub", "https://github.example/"),
  };
  const std::vector<CommandBarFavorite> hits =
      FavoriteMatchesForQuery("git", favorites);
  ASSERT_EQ(2u, hits.size());
  EXPECT_EQ("GitHub", hits[0].title);
}

TEST(CommandBarMatchesTest, TwoQueryWordsFindATabNeitherOneSpells) {
  const std::vector<CommandBarTab> hits =
      TabMatchesForQuery("atr doc", ThreeOpenTabs());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("t2", hits[0].id);
}

TEST(CommandBarMatchesTest, ATabQueryStillSplitsAcrossTitleAndHost) {
  const std::vector<CommandBarTab> hits =
      TabMatchesForQuery("weather example", ThreeOpenTabs());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("t3", hits[0].id);
}

TEST(CommandBarMatchesTest, ASpaceIsFoundByAFuzzyName) {
  const std::vector<CommandBarSpace> hits =
      SpaceMatchesForQuery("prsnl", TwoSpaces());
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("Personal", hits[0].name);
}

TEST(CommandBarMatchesTest, AQueryOfOnlyPunctuationOffersNoSpaces) {
  // It tokenizes to nothing, which is the empty query by another spelling.
  EXPECT_TRUE(SpaceMatchesForQuery("...", TwoSpaces()).empty());
}

}  // namespace
}  // namespace atrium
