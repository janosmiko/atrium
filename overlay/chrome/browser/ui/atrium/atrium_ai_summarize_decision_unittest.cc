// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_ai_summarize_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumAiSummarizeDecisionTest, DisabledWinsOverEverythingElse) {
  EXPECT_EQ(DecideSummarizeAction(/*ai_enabled=*/false,
                                  /*model_available=*/true, "hello"),
            SummarizeOutcome::kDisabled);
}

TEST(AtriumAiSummarizeDecisionTest, UnavailableWinsOverEmptyPage) {
  EXPECT_EQ(DecideSummarizeAction(/*ai_enabled=*/true,
                                  /*model_available=*/false, ""),
            SummarizeOutcome::kUnavailable);
}

TEST(AtriumAiSummarizeDecisionTest, EmptyPageAfterEnabledAndAvailable) {
  EXPECT_EQ(
      DecideSummarizeAction(/*ai_enabled=*/true, /*model_available=*/true, ""),
      SummarizeOutcome::kEmptyPage);
}

TEST(AtriumAiSummarizeDecisionTest, WhitespaceOnlyPageCountsAsEmpty) {
  EXPECT_EQ(DecideSummarizeAction(/*ai_enabled=*/true,
                                  /*model_available=*/true, "   \n\t  "),
            SummarizeOutcome::kEmptyPage);
}

TEST(AtriumAiSummarizeDecisionTest, EnabledAvailableNonEmptyCallsModel) {
  EXPECT_EQ(DecideSummarizeAction(/*ai_enabled=*/true,
                                  /*model_available=*/true, "Real content."),
            SummarizeOutcome::kCallModel);
}

TEST(AtriumAiSummarizeDecisionTest, ClampLeavesShortTextUnchanged) {
  EXPECT_EQ(ClampPageTextForModel("short", 0), "short");
}

TEST(AtriumAiSummarizeDecisionTest, ClampTruncatesAtTheCharacterLimit) {
  const std::string long_text(kMaxSummarizeInputChars + 500, 'a');
  const std::string clamped = ClampPageTextForModel(long_text, 0);
  // the cut text, then the marker that tells the prompt it was cut.
  EXPECT_EQ(clamped,
            std::string(kMaxSummarizeInputChars, 'a') +
                std::string(kTruncatedMarker));
}

// The pair the marker depends on: a page that fits must not claim it was cut,
// or every answer hedges about text the model was actually given in full.
TEST(AtriumAiSummarizeDecisionTest, ClampAtExactlyTheLimitIsUnchanged) {
  const std::string exact_text(kMaxSummarizeInputChars, 'b');
  const std::string clamped = ClampPageTextForModel(exact_text, 0);
  EXPECT_EQ(clamped, exact_text);
  EXPECT_EQ(clamped.find(kTruncatedMarker), std::string::npos);
}

// The settings row is the cap, and the built-in number is
// only what a settings read that found nothing falls back to.
TEST(AtriumAiSummarizeDecisionTest, TheConfiguredCapWinsOverTheBuiltInOne) {
  const std::string text(200, 'c');
  EXPECT_EQ(ClampPageTextForModel(text, 50),
            std::string(50, 'c') + std::string(kTruncatedMarker));
  EXPECT_EQ(ClampPageTextForModel(text, 500), text);
}

// The old 6000 is the number the task exists to raise. A page between it and
// the new default has to arrive whole.
TEST(AtriumAiSummarizeDecisionTest, APageWellPastTheOldCapIsNotCut) {
  const std::string text(60000, 'd');
  const std::string clamped = ClampPageTextForModel(text, 0);
  EXPECT_EQ(clamped, text);
  EXPECT_EQ(clamped.find(kTruncatedMarker), std::string::npos);
}

TEST(AtriumAiSummarizeDecisionTest, StrippingTheMarkerReportsWhetherItWasThere) {
  std::string cut = "page" + std::string(kTruncatedMarker);
  EXPECT_TRUE(StripTruncationMarker(cut));
  EXPECT_EQ(cut, "page");

  std::string whole = "page";
  EXPECT_FALSE(StripTruncationMarker(whole));
  EXPECT_EQ(whole, "page");
}

}  // namespace
}  // namespace atrium
