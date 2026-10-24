// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_slot.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_SLOT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_SLOT_H_

#include <cstddef>
#include <optional>

namespace atrium {

// The ordinal Cmd+9 carries. The core ships goToSidebarItemLast rather than a
// ninth slot, so that key follows the grid's length instead of naming a
// position.
inline constexpr int kLastFavorite = -1;

// Which favourite tile a Cmd+digit chord opens, counting from 0, or nullopt
// when the active space has no tile there.
std::optional<size_t> FavoriteIndexFor(int ordinal, size_t favorite_count);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_SLOT_H_
