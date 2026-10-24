// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_peek_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The shape every case below varies one field of: an off-site link click on a
// pinned tab, which is the whole point of the Keyhole.
PeekDecisionInputs OffSiteClickOnPinnedTab() {
  PeekDecisionInputs in;
  in.source_has_committed_url = true;
  in.is_http_or_https = true;
  in.is_same_domain_or_host = false;
  in.has_atrium_controller = true;
  in.is_peek_source_tab = true;
  return in;
}

TEST(PeekDecisionTest, OffSiteClickOnAPinnedTabPeeks) {
  EXPECT_TRUE(ShouldAutoPeekForInputs(OffSiteClickOnPinnedTab()));
}

// The criterion this task exists for: the pinned page must not navigate away.
// A plain tab is the control - same click, same URL, no peek.
TEST(PeekDecisionTest, TheSameClickOnAPlainTabDoesNotPeek) {
  PeekDecisionInputs in = OffSiteClickOnPinnedTab();
  in.is_peek_source_tab = false;
  EXPECT_FALSE(ShouldAutoPeekForInputs(in));
}

// Same-site stays in place on purpose, so a pinned Gmail or Notion keeps
// working as an app rather than throwing a card at every internal link.
TEST(PeekDecisionTest, SameSiteNavigatesInPlaceEvenWhenPinned) {
  PeekDecisionInputs in = OffSiteClickOnPinnedTab();
  in.is_same_domain_or_host = true;
  EXPECT_FALSE(ShouldAutoPeekForInputs(in));
}

// mailto:, javascript:, chrome:// and friends are not pages to peek.
TEST(PeekDecisionTest, NonHttpSchemeDoesNotPeek) {
  PeekDecisionInputs in = OffSiteClickOnPinnedTab();
  in.is_http_or_https = false;
  EXPECT_FALSE(ShouldAutoPeekForInputs(in));
}

// A tab with nothing committed has no origin to be off-site FROM, so the
// same-site question cannot be asked and the answer must not be "peek".
TEST(PeekDecisionTest, SourceWithNoCommittedUrlDoesNotPeek) {
  PeekDecisionInputs in = OffSiteClickOnPinnedTab();
  in.source_has_committed_url = false;
  EXPECT_FALSE(ShouldAutoPeekForInputs(in));
}

// The peek's own contents, DevTools and any other Browser this window does
// not manage fall through to ordinary navigation. Without this a link inside
// a peek could open a peek inside the peek.
TEST(PeekDecisionTest, NonAtriumBrowserDoesNotPeek) {
  PeekDecisionInputs in = OffSiteClickOnPinnedTab();
  in.has_atrium_controller = false;
  EXPECT_FALSE(ShouldAutoPeekForInputs(in));
}

// Every predicate is independently necessary. Stated as its own case because
// a decision table that happens to be an OR of two fields would pass all of
// the above and still be wrong.
TEST(PeekDecisionTest, EveryPredicateIsIndividuallyRequired) {
  const PeekDecisionInputs base = OffSiteClickOnPinnedTab();
  ASSERT_TRUE(ShouldAutoPeekForInputs(base));

  for (int field = 0; field < 5; ++field) {
    PeekDecisionInputs in = base;
    switch (field) {
      case 0: in.source_has_committed_url = false; break;
      case 1: in.is_http_or_https = false; break;
      case 2: in.is_same_domain_or_host = true; break;
      case 3: in.has_atrium_controller = false; break;
      case 4: in.is_peek_source_tab = false; break;
    }
    EXPECT_FALSE(ShouldAutoPeekForInputs(in))
        << "flipping field " << field << " alone must suppress the peek";
  }
}

// The shape the popup rule exists for: the sign-in button on the page the
// window is showing calls window.open with a size, so the target is blank.
PopupPeekDecisionInputs SignInPopupFromTheActiveTab() {
  PopupPeekDecisionInputs in;
  in.has_atrium_controller = true;
  in.source_is_active_tab = true;
  in.target_is_peekable_scheme = true;
  in.targets_another_display = false;
  return in;
}

TEST(PeekDecisionTest, ASignInPopupFromTheActiveTabPeeks) {
  EXPECT_TRUE(ShouldPeekPopupForInputs(SignInPopupFromTheActiveTab()));
}

// A card over the wrong page is worse than a window: the user is reading
// another tab and never asked for this one.
TEST(PeekDecisionTest, APopupFromABackgroundTabKeepsItsOwnWindow) {
  PopupPeekDecisionInputs in = SignInPopupFromTheActiveTab();
  in.source_is_active_tab = false;
  EXPECT_FALSE(ShouldPeekPopupForInputs(in));
}

// The card is a child of the opener's window, so a popup placed on another
// screen has to stay a real window to land where the page asked.
TEST(PeekDecisionTest, APopupAimedAtAnotherDisplayKeepsItsOwnWindow) {
  PopupPeekDecisionInputs in = SignInPopupFromTheActiveTab();
  in.targets_another_display = true;
  EXPECT_FALSE(ShouldPeekPopupForInputs(in));
}

TEST(PeekDecisionTest, EveryPopupPredicateIsIndividuallyRequired) {
  const PopupPeekDecisionInputs base = SignInPopupFromTheActiveTab();
  ASSERT_TRUE(ShouldPeekPopupForInputs(base));

  for (int field = 0; field < 4; ++field) {
    PopupPeekDecisionInputs in = base;
    switch (field) {
      case 0: in.has_atrium_controller = false; break;
      case 1: in.source_is_active_tab = false; break;
      case 2: in.target_is_peekable_scheme = false; break;
      case 3: in.targets_another_display = true; break;
    }
    EXPECT_FALSE(ShouldPeekPopupForInputs(in))
        << "flipping field " << field << " alone must keep the stock popup";
  }
}

}  // namespace
}  // namespace atrium
