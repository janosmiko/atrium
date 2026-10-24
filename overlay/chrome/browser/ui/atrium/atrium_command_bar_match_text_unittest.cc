// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_match_text_unittest.cc
//
//   autoninja -C out/atrium unit_tests &&
//   out/atrium/unit_tests --gtest_filter=CommandBarMatchText*

#include "chrome/browser/ui/atrium/atrium_command_bar_match_text.h"

#include <optional>
#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::optional<int> Score(std::string_view query,
                         const std::vector<std::string_view>& fields) {
  return CommandBarMatchScore(query, fields);
}

bool Hits(std::string_view query,
          const std::vector<std::string_view>& fields) {
  return Score(query, fields).has_value();
}

// --- the tokenizer -----------------------------------------------------------

TEST(CommandBarMatchTextTest, EveryTokenMustLandSomewhere) {
  EXPECT_TRUE(Hits("gh iss", {"GitHub Issues"}));
  EXPECT_FALSE(Hits("gh nope", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, TokensMatchOutOfOrder) {
  EXPECT_TRUE(Hits("iss gh", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, TokensMayLandOnDifferentFields) {
  EXPECT_TRUE(Hits("issues github", {"Issues", "github.com"}));
}

TEST(CommandBarMatchTextTest, RunsOfWhitespaceAreOneSeparator) {
  EXPECT_EQ(Score("gh   iss", {"GitHub Issues"}),
            Score("gh iss", {"GitHub Issues"}));
  EXPECT_EQ(Score("  gh iss  ", {"GitHub Issues"}),
            Score("gh iss", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, ATypedHostSplitsIntoTheWordsItMustLandOn) {
  // One token carrying the dots would land on no word at all, and typing a
  // host is how a tab with a useless title gets found.
  EXPECT_TRUE(Hits("weather.example", {"Weather", "weather.example"}));
  EXPECT_FALSE(Hits("weather.example", {"Weather", "other.site"}));
}

TEST(CommandBarMatchTextTest, AnEmptyQueryHasNoTokensAndScoresZero) {
  // The callers each keep their own empty-query behaviour, so this only has to
  // stay a match rather than a filter.
  EXPECT_EQ(Score("", {"GitHub Issues"}), 0);
  EXPECT_EQ(Score("   ", {"GitHub Issues"}), 0);
  EXPECT_EQ(Score("...", {"GitHub Issues"}), 0);
}

TEST(CommandBarMatchTextTest, NoFieldIsNoMatch) {
  EXPECT_FALSE(Hits("gh", {}));
  EXPECT_FALSE(Hits("gh", {""}));
}

// --- what counts as a hit ----------------------------------------------------

TEST(CommandBarMatchTextTest, APrefixOfAnyWordHits) {
  EXPECT_TRUE(Hits("iss", {"GitHub Issues"}));
  EXPECT_TRUE(Hits("git", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, ASubsequenceOfOneWordHits) {
  // "gthb" is not a prefix of anything, but its letters run through GitHub in
  // order from its first letter.
  EXPECT_TRUE(Hits("gthb", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, ASubsequenceMustStartTheWord) {
  // Without the shared first character every short query hits every row: "tub"
  // runs through giTHUB in order, and a palette that offered it would be
  // noise.
  EXPECT_FALSE(Hits("tub", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, ASubsequenceDoesNotSpanTwoWords) {
  // "gi" of GitHub then "s" of Issues would match nearly anything.
  EXPECT_FALSE(Hits("gis", {"GitHub Issues"}));
}

TEST(CommandBarMatchTextTest, AMissStaysAMiss) {
  EXPECT_FALSE(Hits("xyz", {"GitHub Issues"}));
  EXPECT_FALSE(Hits("zebra", {"GitHub Issues", "github.com"}));
}

TEST(CommandBarMatchTextTest, PunctuationSeparatesWords) {
  // A host has to break at its dots or "com" is the only way in.
  EXPECT_TRUE(Hits("git", {"github.com"}));
  EXPECT_TRUE(Hits("com", {"github.com"}));
  EXPECT_TRUE(Hits("new", {"docs.google.com/new-doc"}));
}

TEST(CommandBarMatchTextTest, CaseIsFoldedBothWays) {
  EXPECT_TRUE(Hits("GH ISS", {"github issues"}));
  EXPECT_TRUE(Hits("gh iss", {"GITHUB ISSUES"}));
}

// --- ranking -----------------------------------------------------------------

TEST(CommandBarMatchTextTest, PrefixOutranksSubsequence) {
  const std::optional<int> prefix = Score("git", {"GitHub"});
  const std::optional<int> fuzzy = Score("gthb", {"GitHub"});
  ASSERT_TRUE(prefix.has_value());
  ASSERT_TRUE(fuzzy.has_value());
  EXPECT_GT(*prefix, *fuzzy);
}

TEST(CommandBarMatchTextTest, AnEarlierWordOutranksALaterOne) {
  const std::optional<int> early = Score("iss", {"Issues on GitHub"});
  const std::optional<int> late = Score("iss", {"GitHub Issues"});
  ASSERT_TRUE(early.has_value());
  ASSERT_TRUE(late.has_value());
  EXPECT_GT(*early, *late);
}

TEST(CommandBarMatchTextTest, TheBestWordInTheFieldIsTheOneScored) {
  // "iss" is a prefix of the second Issues and a nothing against the first
  // word, so the row scores as the prefix hit it holds.
  EXPECT_EQ(Score("iss", {"GitHub Issues"}), Score("iss", {"zzzzzz Issues"}));
}

TEST(CommandBarMatchTextTest, TheBestFieldIsTheOneScored) {
  const std::optional<int> both = Score("git", {"Something", "github.com"});
  const std::optional<int> host_only = Score("git", {"github.com"});
  EXPECT_EQ(both, host_only);
}

TEST(CommandBarMatchTextTest, ScoresAreOrderedNotAbsolute) {
  // A long field cannot out-penalise a hit into a miss, which is what a
  // position penalty without a floor would do.
  const std::string_view long_field =
      "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn "
      "oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy zzzz target";
  const std::optional<int> late = Score("target", {long_field});
  ASSERT_TRUE(late.has_value());
  EXPECT_GT(*late, 0);
}

}  // namespace
}  // namespace atrium
