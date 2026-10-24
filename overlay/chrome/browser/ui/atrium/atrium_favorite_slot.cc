// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_slot.cc
#include "chrome/browser/ui/atrium/atrium_favorite_slot.h"

namespace atrium {

std::optional<size_t> FavoriteIndexFor(int ordinal, size_t favorite_count) {
  if (favorite_count == 0) {
    return std::nullopt;
  }
  if (ordinal == kLastFavorite) {
    return favorite_count - 1;
  }
  if (ordinal < 1 || static_cast<size_t>(ordinal) > favorite_count) {
    return std::nullopt;
  }
  return static_cast<size_t>(ordinal) - 1;
}

}  // namespace atrium
