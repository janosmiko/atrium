// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_app_icon_variant_unittest.cc
#include "chrome/browser/ui/atrium/atrium_browser_window.h"

#include <optional>
#include <string>

#include "chrome/browser/ui/atrium/atrium_icon_source.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

using Variant = std::optional<std::string>;

// The reading that reaches the Dock. Spelled as a partial object because the
// reader is asked about ONE field - the whole-object rule belongs to the
// writer, which refuses a partial one (atrium_core_abi_unittest).
TEST(AtriumAppIconVariantTest, TheFieldIsRead) {
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":"crimson-paper"})"),
            Variant("crimson-paper"));
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":"tokyo-night-accent"})"),
            Variant("tokyo-night-accent"));
}

// AtriumApplyAppIcon answers this slug by clearing the Dock override rather
// than applying a stack. It must still reach that caller as a value, not as
// "the file says nothing".
TEST(AtriumAppIconVariantTest, AutoIsAVariantLikeAnyOther) {
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":"auto"})"),
            Variant("auto"));
  EXPECT_TRUE(IsAppIconSlug("auto"));
}

// A settings file written before the picker existed. The caller must leave the
// icon alone rather than read absence as the default - the bundle icon IS the
// default, and re-applying it on every save would be work for nothing.
TEST(AtriumAppIconVariantTest, AnAbsentFieldIsNotTheDefault) {
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"glassOpacity":0.55})"),
            std::nullopt);
}

// The reader answers an unreadable file with defaults rather than an error, so
// an empty string does reach here.
TEST(AtriumAppIconVariantTest, NothingUsableYieldsNoAnswer) {
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(""), std::nullopt);
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings("not json"),
            std::nullopt);
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings("[1,2,3]"),
            std::nullopt);
}

// A file from a later build could carry any shape here.
TEST(AtriumAppIconVariantTest, AFieldOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":true})"),
            std::nullopt);
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":7})"),
            std::nullopt);
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":null})"),
            std::nullopt);
  EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(
                R"({"appIconVariant":["crimson-ink"]})"),
            std::nullopt);
}

// The value becomes a file name. A hand-edited settings.json is the reachable
// case, and it must not be able to name a file outside the icons directory.
TEST(AtriumAppIconVariantTest, ASlugThatCouldNameAnotherFileIsRefused) {
  for (const char* json : {R"({"appIconVariant":"../../../etc/passwd"})",
                           R"({"appIconVariant":"a/b"})",
                           R"({"appIconVariant":"Crimson-Paper"})",
                           R"({"appIconVariant":"crimson paper"})",
                           R"({"appIconVariant":"crimson.paper"})",
                           R"({"appIconVariant":""})"}) {
    EXPECT_EQ(AtriumBrowserWindow::AppIconVariantFromSettings(json),
              std::nullopt)
        << json;
  }
}

// The same alphabet, asserted where the path is actually built, so a caller
// that reaches AppIconPath by another route is covered too.
TEST(AtriumAppIconVariantTest, TheSlugAlphabetIsLowercaseDigitsAndHyphens) {
  EXPECT_TRUE(IsAppIconSlug("crimson-paper"));
  EXPECT_TRUE(IsAppIconSlug("monokai-pro-accent"));
  EXPECT_TRUE(IsAppIconSlug("a1"));
  for (const char* slug :
       {"", "A", "crimson_paper", "crimson/paper", "..", "crimson.png", "é"}) {
    EXPECT_FALSE(IsAppIconSlug(slug)) << slug;
  }
}

// A refused slug must yield NO path at all. Returning the directory, or a
// path with the slug stripped, would turn a rejection into a read.
TEST(AtriumAppIconVariantTest, ARefusedSlugResolvesToNoPath) {
  EXPECT_TRUE(AppIconPath("../evil").empty());
  EXPECT_TRUE(AppIconPath("").empty());
  EXPECT_FALSE(AppIconPath("crimson-paper").empty());
  EXPECT_EQ(AppIconPath("crimson-paper").BaseName().value(),
            "crimson-paper.png");
  EXPECT_EQ(AppIconPath("crimson-paper").DirName().BaseName().value(),
            "atrium-icons");
}

}  // namespace
}  // namespace atrium
