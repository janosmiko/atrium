// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_page_url_unittest.cc
#include "chrome/browser/ui/atrium/atrium_profile_page_url.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumProfilePageUrlTest, EachKnownPageMapsToItsChromePage) {
  EXPECT_EQ(ProfilePageUrl("privacy"), "chrome://settings/privacy");
  EXPECT_EQ(ProfilePageUrl("passwords"), "chrome://settings/passwords");
  EXPECT_EQ(ProfilePageUrl("payments"), "chrome://settings/payments");
  EXPECT_EQ(ProfilePageUrl("notifications"),
            "chrome://settings/content/notifications");
  EXPECT_EQ(ProfilePageUrl("clearBrowsingData"),
            "chrome://settings/clearBrowserData");
  EXPECT_EQ(ProfilePageUrl("extensions"), "chrome://extensions/");
}

// The whole reason the page travels as an enum name and not as a URL: a
// renderer that could name the destination could name any of them.
TEST(AtriumProfilePageUrlTest, AnUnknownNameAnswersEmpty) {
  EXPECT_EQ(ProfilePageUrl("history"), "");
  EXPECT_EQ(ProfilePageUrl(""), "");
  EXPECT_EQ(ProfilePageUrl("chrome://settings/passwords"), "");
  EXPECT_EQ(ProfilePageUrl("file:///etc/passwd"), "");
  EXPECT_EQ(ProfilePageUrl("javascript:alert(1)"), "");
}

// Case matters. A lookup that folded case would accept a spelling the page
// never sends, which is a second grammar nobody maintains.
TEST(AtriumProfilePageUrlTest, TheLookupIsCaseSensitive) {
  EXPECT_EQ(ProfilePageUrl("Passwords"), "");
  EXPECT_EQ(ProfilePageUrl("clearbrowsingdata"), "");
}

}  // namespace
}  // namespace atrium
