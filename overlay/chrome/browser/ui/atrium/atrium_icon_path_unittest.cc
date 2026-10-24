// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_icon_path_unittest.cc
#include "chrome/browser/ui/atrium/atrium_icon_path.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The id in these paths comes from the renderer's own DOM, so nothing
// downstream trusts it - a miss is a 404 and the row keeps its tint chip.
// What must hold is that a path resolves to the RIGHT cache with the RIGHT
// key: routing a urlicon at the tab-keyed cache answers nothing, and decoding
// its key wrongly answers for a different tile, which is worse.

TEST(AtriumIconPathTest, ATabIconRoutesToTheTabCache) {
  const IconRequest r = ParseIconPath("favicon/s1/1234.png");
  EXPECT_EQ(r.kind, IconKind::kFavicon);
  EXPECT_EQ(r.scope, "s1");
  EXPECT_EQ(r.id, "1234");
}

// Tab ids are a small per-process counter shared by every profile, so a path
// that named no scope would let any surface walk all of them.
TEST(AtriumIconPathTest, ATabIconNamingNoScopeStillClaimsTheRequest) {
  const IconRequest r = ParseIconPath("favicon/1234.png");
  EXPECT_EQ(r.kind, IconKind::kFavicon);
  EXPECT_TRUE(r.scope.empty());
}

TEST(AtriumIconPathTest, AnExtensionIconKeepsItsIdVerbatim) {
  const IconRequest r = ParseIconPath("exticon/abcdefgh.png");
  EXPECT_EQ(r.kind, IconKind::kExtension);
  EXPECT_EQ(r.id, "abcdefgh");
}

TEST(AtriumIconPathTest, AnExtensionIconRoutesToItsScope) {
  const IconRequest r = ParseIconPath("exticon/s1/abcdefgh.png");
  EXPECT_EQ(r.kind, IconKind::kExtension);
  EXPECT_EQ(r.scope, "s1");
  EXPECT_EQ(r.id, "abcdefgh");
}

// The page adds ?r=<rev> so a reloaded icon gets a new URL. Blink reuses an
// image it already holds for the same URL, even after the bytes change.
TEST(AtriumIconPathTest, AnExtensionIconDropsItsRevisionQuery) {
  const IconRequest r = ParseIconPath("exticon/s1/abcdefgh.png?r=3");
  EXPECT_EQ(r.kind, IconKind::kExtension);
  EXPECT_EQ(r.scope, "s1");
  EXPECT_EQ(r.id, "abcdefgh");
}

// The Peek badge carries one the same way when the peeked page's icon changes.
TEST(AtriumIconPathTest, ATabIconDropsItsRevisionQuery) {
  const IconRequest r = ParseIconPath("favicon/s1/1234.png?r=2");
  EXPECT_EQ(r.kind, IconKind::kFavicon);
  EXPECT_EQ(r.scope, "s1");
  EXPECT_EQ(r.id, "1234");
}

// a favourite is keyed by the page it points at, and a URL has to
// survive as one path segment.
//
// The identity that matters is the round trip - what the surface encoded comes
// back byte for byte, because the result is a cache key and a key that is
// merely close finds nothing. A GURL spec never holds a raw space; it holds
// %20 already, and encodeURIComponent makes that %2520. So one decode restores
// the spec, and a decode that also unescaped spaces would corrupt the key
// rather than restore it.
TEST(AtriumIconPathTest, AUrlIconDecodesBackToTheUrlItWasAskedFor) {
  const IconRequest r = ParseIconPath(
      "urlicon/s1/https%3A%2F%2Fexample.com%2Fa%2520b%3Fq%3D1.png");
  EXPECT_EQ(r.kind, IconKind::kUrlIcon);
  EXPECT_EQ(r.scope, "s1");
  EXPECT_EQ(r.id, "https://example.com/a%20b?q=1");
}

// The scope names which profile's cache may answer. The first RAW slash is the
// separator, and the key that follows it holds none of its own.
TEST(AtriumIconPathTest, TheScopeIsTheSegmentBeforeTheEncodedUrl) {
  const IconRequest r =
      ParseIconPath("urlicon/abc123/https%3A%2F%2Fa.example%2Fb%2Fc.png");
  EXPECT_EQ(r.scope, "abc123");
  EXPECT_EQ(r.id, "https://a.example/b/c");
}

// Still a kUrlIcon, so the request filter claims the path and answers 404.
// A kNone would let it fall through to the resource map instead.
TEST(AtriumIconPathTest, APathNamingNoScopeStillClaimsTheRequest) {
  const IconRequest r =
      ParseIconPath("urlicon/https%3A%2F%2Fa.example%2F.png");
  EXPECT_EQ(r.kind, IconKind::kUrlIcon);
  EXPECT_TRUE(r.scope.empty());
}

// The slash is the whole reason the key is encoded. A decode that turned %2F
// back into a separator before the split would have cut the key in two.
TEST(AtriumIconPathTest, EveryEncodedSlashSurvivesTheDecode) {
  const IconRequest r =
      ParseIconPath("urlicon/s1/https%3A%2F%2Fa.example%2Fb%2Fc%2Fd.png");
  EXPECT_EQ(r.id, "https://a.example/b/c/d");
}

// A query and a fragment are ordinary URL characters, not a second path.
TEST(AtriumIconPathTest, AQueryAndAFragmentComeBackWhole) {
  const IconRequest r =
      ParseIconPath("urlicon/s1/https%3A%2F%2Fa.example%2Fp%3Fq%3D1%26r%3D2%23f.png");
  EXPECT_EQ(r.id, "https://a.example/p?q=1&r=2#f");
}

TEST(AtriumIconPathTest, AnUnknownPrefixIsRoutedNowhere) {
  for (const char* path :
       {"", "favicon/", "favicon.png", "urlicon/", "sidebar.js",
        "urlicon/x.jpg", "notacache/1.png"}) {
    const IconRequest r = ParseIconPath(path);
    EXPECT_EQ(r.kind, IconKind::kNone) << path;
    EXPECT_TRUE(r.id.empty()) << path;
  }
}

// The request filter asks only this, and a kNone there means the load fails
// before any data source runs.
TEST(AtriumIconPathTest, TheFilterShorthandAgreesWithTheFullParse) {
  for (const char* path : {"favicon/s1/1.png", "exticon/a.png", "urlicon/s1/b.png",
                           "nope", "favicon/"}) {
    EXPECT_EQ(IconRequestKind(path), ParseIconPath(path).kind) << path;
  }
}

}  // namespace
}  // namespace atrium
