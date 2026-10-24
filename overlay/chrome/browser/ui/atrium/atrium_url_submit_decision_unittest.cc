// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_url_submit_decision.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

TEST(AtriumUrlSubmitDecisionTest, AnOrdinaryTabTakesEverySubmissionInPlace) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(false, GURL(), GURL("https://example.org/")));
}

TEST(AtriumUrlSubmitDecisionTest, AnotherSiteOpensBesideAFavourite) {
  EXPECT_EQ(SubmitTarget::kNewTab,
            DecideUrlSubmitTarget(true, GURL("https://example.com/mail"),
                                  GURL("https://example.org/")));
}

TEST(AtriumUrlSubmitDecisionTest, TheFavouritesOwnSiteStaysInItsTab) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL("https://example.com/mail"),
                                  GURL("https://example.com/contacts")));
}

// The tile names one page of a site, and the whole site is that tab's to
// browse - a subdomain included, or clicking through a sign-in host would
// spawn a tab every time.
TEST(AtriumUrlSubmitDecisionTest, ASubdomainOfTheFavouriteIsTheSameSite) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL("https://example.com/mail"),
                                  GURL("https://accounts.example.com/login")));
}

// INCLUDE_PRIVATE_REGISTRIES, so two tenants of one hosting domain are two
// sites. Excluding them would read both as "appspot.com" and keep a different
// person's app in the favourite's tab.
TEST(AtriumUrlSubmitDecisionTest, TwoTenantsOfAPrivateRegistryAreTwoSites) {
  EXPECT_EQ(SubmitTarget::kNewTab,
            DecideUrlSubmitTarget(true, GURL("https://one.appspot.com/"),
                                  GURL("https://two.appspot.com/")));
}

// A host with no registry at all compares whole, which is what makes a
// localhost or IP fixture a usable cross-site pair.
TEST(AtriumUrlSubmitDecisionTest, HostsWithNoRegistryCompareWhole) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL("http://127.0.0.1:8000/a.html"),
                                  GURL("http://127.0.0.1:8000/b.html")));
  EXPECT_EQ(SubmitTarget::kNewTab,
            DecideUrlSubmitTarget(true, GURL("http://127.0.0.1:8000/a.html"),
                                  GURL("http://localhost:8000/b.html")));
}

// The claim is a string a tab carries, and an unclaimed tab carries "". Both
// halves have to survive that rather than open a tab on every submission.
TEST(AtriumUrlSubmitDecisionTest, AnUnreadableUrlOnEitherSideStaysInPlace) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL(""), GURL("https://example.org/")));
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL("https://example.com/"), GURL("")));
}

// A favourite on a scheme with no host - the sidebar allows about: - has no
// site to compare, and every submission would otherwise leave its tab.
TEST(AtriumUrlSubmitDecisionTest, AHostlessFavouriteKeepsItsTab) {
  EXPECT_EQ(SubmitTarget::kSameTab,
            DecideUrlSubmitTarget(true, GURL("about:blank"),
                                  GURL("https://example.org/")));
}

}  // namespace
}  // namespace atrium
