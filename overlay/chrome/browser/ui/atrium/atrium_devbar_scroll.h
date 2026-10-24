// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_SCROLL_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_SCROLL_H_

#include <optional>

namespace atrium {

// How far the page has to travel in one direction before the bar moves. A
// trackpad's rest jitter and a page's own settle are under this.
inline constexpr double kDevBarScrollDeadZone = 12.0;

// the floating devbar slides away on a downward scroll and comes
// back on an upward one. Deltas follow Blink: positive means the content
// moves down, which is a scroll toward the top.
class DevBarScrollAway {
 public:
  // The new away state when this delta flips it, else nullopt.
  std::optional<bool> OnScroll(double delta_y);
  // Back to shown. The old state when that was a change, else nullopt.
  std::optional<bool> Reset();
  // Pinned, the bar holds its place over the page and no scroll moves it.
  // Returns the new away state when pinning brought a hidden bar back, else
  // nullopt.
  std::optional<bool> SetPinned(bool pinned);
  bool away() const { return away_; }
  bool pinned() const { return pinned_; }

 private:
  double travel_ = 0.0;
  bool away_ = false;
  bool pinned_ = false;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_SCROLL_H_
