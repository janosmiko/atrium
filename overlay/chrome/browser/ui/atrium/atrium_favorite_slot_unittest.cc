// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_slot_unittest.cc

// the window reads the grid out of the sidebar model and this
// decides, the same posture as :pin_toggle beside it.

#include "chrome/browser/ui/atrium/atrium_favorite_slot.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumFavoriteSlotTest, ADigitOpensTheFavouriteAtThatPosition) {
  EXPECT_EQ(FavoriteIndexFor(1, 3), std::make_optional<size_t>(0));
  EXPECT_EQ(FavoriteIndexFor(2, 3), std::make_optional<size_t>(1));
  EXPECT_EQ(FavoriteIndexFor(3, 3), std::make_optional<size_t>(2));
}

// Cmd+4 on a space with three tiles is not "the nearest one" and not the
// first: the chord names a slot that is not there, so the key does nothing and
// falls through.
TEST(AtriumFavoriteSlotTest, ADigitPastTheCountOpensNothing) {
  EXPECT_EQ(FavoriteIndexFor(4, 3), std::nullopt);
  EXPECT_EQ(FavoriteIndexFor(8, 3), std::nullopt);
  EXPECT_EQ(FavoriteIndexFor(1, 0), std::nullopt);
}

// Cmd+9 carries the core's goToSidebarItemLast, not a ninth slot, so it lands
// on the last tile whatever the count - Arc's own reading of that key.
TEST(AtriumFavoriteSlotTest, TheLastSlotFollowsTheCount) {
  EXPECT_EQ(FavoriteIndexFor(kLastFavorite, 3), std::make_optional<size_t>(2));
  EXPECT_EQ(FavoriteIndexFor(kLastFavorite, 1), std::make_optional<size_t>(0));
  EXPECT_EQ(FavoriteIndexFor(kLastFavorite, 12), std::make_optional<size_t>(11));
  EXPECT_EQ(FavoriteIndexFor(kLastFavorite, 0), std::nullopt)
      << "an empty grid has no last tile either";
}

// The ordinal reaches this from a switch over the hotkey enum, so a bad one is
// a programming error rather than user input - but it must not resolve to a
// tile, because the one it would pick is some other digit's.
TEST(AtriumFavoriteSlotTest, AnOrdinalOutsideTheDigitsOpensNothing) {
  EXPECT_EQ(FavoriteIndexFor(0, 3), std::nullopt);
  EXPECT_EQ(FavoriteIndexFor(-2, 3), std::nullopt);
}

}  // namespace
}  // namespace atrium
