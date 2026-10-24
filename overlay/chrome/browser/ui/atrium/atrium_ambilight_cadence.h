// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AMBILIGHT_CADENCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AMBILIGHT_CADENCE_H_

#include <stddef.h>

#include <array>

namespace atrium {

// How often the ambilight reads the page, in milliseconds. 8 Hz carries a
// scroll without stepping. The slow rungs are what a page nobody is touching
// costs, which is most pages most of the time.
inline constexpr std::array<int, 3> kAmbilightRungsMs = {125, 500, 2000};

// Unchanged reads at one rung before dropping to the next. Eight at 8 Hz is a
// second of stillness, which is long enough not to punish a slow animation.
inline constexpr int kAmbilightStepAfter = 8;

// The sampler's rate, stepped down while the page holds still and snapped back
// the moment it moves. Plain milliseconds and no clock of its own, so the
// ladder can be asserted rather than watched.
class AmbilightCadence {
 public:
  // The interval the NEXT read should wait.
  int OnSample(bool changed);

  int interval_ms() const;

  // Back to the top rung, for a sampler starting on a different page.
  void Reset();

 private:
  size_t rung_ = 0;
  int unchanged_ = 0;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AMBILIGHT_CADENCE_H_
