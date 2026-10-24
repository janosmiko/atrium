// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_model_source_unittest.cc
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"

#include "base/command_line.h"
#include "base/test/scoped_command_line.h"
#include "content/public/common/content_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The dylib wins whenever it answers, and that ordering is correctness rather
// than preference: while Swift owns the model, spaces.json is up to a second
// stale, so preferring the file would serve rows from before the user's last
// drag or close.
TEST(SidebarModelSourceTest, TheDylibWinsWheneverItAnswers) {
  EXPECT_EQ(ChooseSidebarModelSource("{\"spaces\":[]}", true),
            SidebarModelSource::kDylib);
  EXPECT_EQ(ChooseSidebarModelSource("{\"spaces\":[]}", false),
            SidebarModelSource::kDylib);
}

// Every build after the cut, and today an old dylib without the export.
TEST(SidebarModelSourceTest, TheCoreAnswersWhenTheDylibDoesNot) {
  EXPECT_EQ(ChooseSidebarModelSource("", true),
            SidebarModelSource::kRustCore);
}

// No model from either. The caller emits the flat tab list it genuinely
// knows, which shows LESS rather than something made up.
TEST(SidebarModelSourceTest, NoStoreFileMeansTheFlatListOnly) {
  EXPECT_EQ(ChooseSidebarModelSource("", false),
            SidebarModelSource::kFlatTabListOnly);
}

// The probes stage a store through ATRIUM_SPACES_FILE. A reader that ignored
// the override would read the user's real store during a test.
TEST(SidebarModelSourceTest, TheEnvironmentOverrideWins) {
  EXPECT_EQ(SpacesFilePath("/tmp/staged/spaces.json",
                           base::FilePath("/Users/x/Application Support")),
            base::FilePath("/tmp/staged/spaces.json"));
}

TEST(SidebarModelSourceTest, TheDefaultIsUnderApplicationSupport) {
  EXPECT_EQ(SpacesFilePath("", base::FilePath("/Users/x/Application Support")),
            base::FilePath("/Users/x/Application Support/Atrium/spaces.json"));
}

// Nowhere to read from is an empty path, so the caller falls through rather
// than reading from a made-up location.
TEST(SidebarModelSourceTest, NoOverrideAndNoHomeGivesNoPath) {
  EXPECT_TRUE(SpacesFilePath("", base::FilePath()).empty());
}

// A temp --user-data-dir does not move DIR_APP_DATA, so a browsertest without
// the override read and wrote the developer's live store - one local run left
// 28 test tabs in it.
TEST(SidebarModelSourceTest, ATestHarnessNeverResolvesTheRealStore) {
  base::test::ScopedCommandLine scoped;
  scoped.GetProcessCommandLine()->AppendSwitch(switches::kTestType);
  EXPECT_TRUE(
      SpacesFilePath("", base::FilePath("/Users/x/Application Support"))
          .empty());
}

TEST(SidebarModelSourceTest, TheOverrideStillWinsUnderATestHarness) {
  base::test::ScopedCommandLine scoped;
  scoped.GetProcessCommandLine()->AppendSwitch(switches::kTestType);
  EXPECT_EQ(SpacesFilePath("/tmp/staged/spaces.json",
                           base::FilePath("/Users/x/Application Support")),
            base::FilePath("/tmp/staged/spaces.json"));
}

// the strip the Rust core consumes.

TEST(LiveStripJSONTest, AnEmptyStripIsAnEmptyArray) {
  EXPECT_EQ(LiveStripJSON({}), "[]");
}

TEST(LiveStripJSONTest, TheIdIsANumberRatherThanAString) {
  // As a string, or as the `11.0` base::Value writes for a double, the core's
  // u64 refuses the whole strip and every row goes cold.
  EXPECT_EQ(LiveStripJSON({{"https://example.com/", 11}}),
            R"([{"url":"https://example.com/","id":11,"active":false}])");
}

// the claim travels only when set, so a strip with no claims is
// byte-identical to what every reader was built against. It names the
// favourite because a redirect moves the tab off that url.
TEST(LiveStripJSONTest, AClaimedTabNamesItsFavouriteAndAnUnclaimedOneSaysNothing) {
  LiveTab claimed{"https://landing.example/", 5, false};
  claimed.claimed = "https://fav.example/";
  EXPECT_EQ(
      LiveStripJSON({claimed}),
      R"([{"url":"https://landing.example/","id":5,"active":false,"claimed":"https://fav.example/"}])");
  EXPECT_EQ(LiveStripJSON({{"https://plain.example/", 6, true}}),
            R"([{"url":"https://plain.example/","id":6,"active":true}])");
}

// A favourite is made from a page the user visited, so its key carries
// whatever that url held. Unquoted, one double quote ends the string and the
// core refuses the whole strip.
// Only when set, for the same reason as the claim: the common strip stays
// byte-identical.
TEST(LiveStripJSONTest, ATabInAnotherWindowIsFlaggedAndALocalOneIsNot) {
  LiveTab away{"https://away.example/", 9, true};
  away.elsewhere = true;
  EXPECT_EQ(
      LiveStripJSON({away}),
      R"([{"url":"https://away.example/","id":9,"active":true,"elsewhere":true}])");
  EXPECT_EQ(LiveStripJSON({{"https://here.example/", 6}}),
            R"([{"url":"https://here.example/","id":6,"active":false}])");
}

TEST(LiveStripJSONTest, TheClaimKeyIsQuoted) {
  LiveTab claimed{"https://landing.example/", 7, false};
  claimed.claimed = "https://fav.example/?q=\"x\"";
  EXPECT_EQ(
      LiveStripJSON({claimed}),
      R"([{"url":"https://landing.example/","id":7,"active":false,"claimed":"https://fav.example/?q=\"x\""}])");
}

TEST(LiveStripJSONTest, AnIdTooBigForAnIntSurvives) {
  // base::Value has no 64-bit integer, which is why this is written by hand.
  EXPECT_EQ(
      LiveStripJSON({{"https://example.com/", 9007199254740993ull}}),
      R"([{"url":"https://example.com/","id":9007199254740993,"active":false}])");
}

TEST(LiveStripJSONTest, StripOrderIsKept) {
  // The core assigns ids by consuming this queue, so order IS the mapping.
  EXPECT_EQ(
      LiveStripJSON({{"https://a/", 1}, {"https://b/", 2}}),
      R"([{"url":"https://a/","id":1,"active":false},{"url":"https://b/","id":2,"active":false}])");
}

TEST(LiveStripJSONTest, TwoRowsSharingAUrlStayTwoEntries) {
  // Deduplicating here would give both rows the same tab, and then both would
  // activate it.
  EXPECT_EQ(
      LiveStripJSON({{"https://a/", 1}, {"https://a/", 2}}),
      R"([{"url":"https://a/","id":1,"active":false},{"url":"https://a/","id":2,"active":false}])");
}

TEST(LiveStripJSONTest, AUrlWithAQuoteIsEscaped) {
  // A URL is attacker-influenced. Unescaped, a quote ends the string and the
  // core sees malformed JSON - which it treats as an empty strip.
  const std::string out = LiveStripJSON({{"https://x/\"a", 1}});
  EXPECT_EQ(out, "[{\"url\":\"https://x/\\\"a\",\"id\":1,\"active\":false}]")
      << out;
}

TEST(LiveStripJSONTest, TheActiveFlagIsCarried) {
  // the core reads this to pick which row gets the active pill.
  EXPECT_EQ(
      LiveStripJSON({{"https://a/", 1, true}, {"https://b/", 2, false}}),
      R"([{"url":"https://a/","id":1,"active":true},{"url":"https://b/","id":2,"active":false}])");
}

}  // namespace
}  // namespace atrium
