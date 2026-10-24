// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devbar_scroll.h"

namespace atrium {

std::optional<bool> DevBarScrollAway::OnScroll(double delta_y) {
  if (pinned_ || delta_y == 0.0) {
    return std::nullopt;
  }
  // A turn starts the count over, so a long scroll one way does not bank
  // travel against the first move back.
  if ((travel_ < 0.0) != (delta_y < 0.0)) {
    travel_ = 0.0;
  }
  travel_ += delta_y;
  if (!away_ && travel_ <= -kDevBarScrollDeadZone) {
    away_ = true;
    return true;
  }
  if (away_ && travel_ >= kDevBarScrollDeadZone) {
    away_ = false;
    return false;
  }
  return std::nullopt;
}

std::optional<bool> DevBarScrollAway::Reset() {
  travel_ = 0.0;
  if (!away_) {
    return std::nullopt;
  }
  away_ = false;
  return false;
}

std::optional<bool> DevBarScrollAway::SetPinned(bool pinned) {
  pinned_ = pinned;
  // Either direction starts the count over, so the travel banked under the
  // old mode cannot be spent on the first scroll under the new one.
  travel_ = 0.0;
  if (!pinned_ || !away_) {
    return std::nullopt;
  }
  away_ = false;
  return false;
}

}  // namespace atrium
