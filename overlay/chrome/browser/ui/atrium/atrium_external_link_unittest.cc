// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// what settings.json says about external links, and what
// the core answers once C++ has read it. The decision's own table lives in the
// Rust crate (navigation.rs); these cover the boundary and the read.

#include "chrome/browser/ui/atrium/atrium_external_link.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumExternalLinkTest, TheSettingIsReadFromAWholeSettingsObject) {
  EXPECT_EQ(
      ExternalLinksOpenInLittleFromSettings(
          R"({"externalLinksOpenInLittle":true})"),
      std::optional<bool>(true));
  EXPECT_EQ(
      ExternalLinksOpenInLittleFromSettings(
          R"({"externalLinksOpenInLittle":false})"),
      std::optional<bool>(false));
}

// A settings file from another build legitimately arrives without the key. The
// caller must leave the default alone rather than read false out of absence -
// absence means ON, which is what the core answers for the same file.
TEST(AtriumExternalLinkTest, AnAbsentKeyIsNoAnswerRatherThanFalse) {
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings(R"({"glassOpacity":0.55})"),
            std::nullopt);
}

TEST(AtriumExternalLinkTest, AnUnreadableObjectIsNoAnswer) {
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings(""), std::nullopt);
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings("not json"), std::nullopt);
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings("[1,2,3]"), std::nullopt);
}

// A field of the wrong type is a file this build cannot honour.
TEST(AtriumExternalLinkTest, AWronglyTypedFieldIsNoAnswer) {
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings(
                R"({"externalLinksOpenInLittle":"true"})"),
            std::nullopt);
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings(
                R"({"externalLinksOpenInLittle":1})"),
            std::nullopt);
  EXPECT_EQ(ExternalLinksOpenInLittleFromSettings(
                R"({"externalLinksOpenInLittle":null})"),
            std::nullopt);
}

// Nothing has seeded it in a unit test, so this is the value a profile that
// has never seen the row runs on.
TEST(AtriumExternalLinkTest, TheProcessValueDefaultsToOn) {
  EXPECT_TRUE(ExternalLinksOpenInLittle());
}

TEST(AtriumExternalLinkTest, TheProcessValueIsWhatWasLastSet) {
  SetExternalLinksOpenInLittle(false);
  EXPECT_FALSE(ExternalLinksOpenInLittle());
  SetExternalLinksOpenInLittle(true);
  EXPECT_TRUE(ExternalLinksOpenInLittle());
}

// The link to the core: these only pass if the crate compiled under GN and the
// header declared the call compatibly.
TEST(AtriumExternalLinkTest, TheCoreDecidesWhereAHandedOverLinkOpens) {
  EXPECT_TRUE(ExternalLinkOpensLittleWindow(/*setting_enabled=*/true,
                                            /*has_url=*/true,
                                            /*is_http_or_https=*/true));
  EXPECT_FALSE(ExternalLinkOpensLittleWindow(/*setting_enabled=*/false,
                                             /*has_url=*/true,
                                             /*is_http_or_https=*/true));
}

// mailto:, file: and javascript: keep the path they already have. The scheme
// outranks the setting, so an on setting cannot put one in a little window.
TEST(AtriumExternalLinkTest, ANonWebSchemeNeverOpensALittleWindow) {
  EXPECT_FALSE(ExternalLinkOpensLittleWindow(/*setting_enabled=*/true,
                                             /*has_url=*/true,
                                             /*is_http_or_https=*/false));
}

TEST(AtriumExternalLinkTest, AnEmptyHandoverOpensNoLittleWindow) {
  EXPECT_FALSE(ExternalLinkOpensLittleWindow(/*setting_enabled=*/true,
                                             /*has_url=*/false,
                                             /*is_http_or_https=*/true));
}

}  // namespace
}  // namespace atrium
