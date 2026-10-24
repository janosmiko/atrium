// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_ANIMATOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_ANIMATOR_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace base {
class Clock;
}

namespace atrium {

// 30 fps while ambient is on, once a minute for night shift alone, idle
// otherwise. Reduced motion turns ambient off. The tick callback pushes the
// raster itself.
class ThemeAnimator {
 public:
  using Tick = base::RepeatingCallback<void(double ambient_time,
                                            double warmth)>;

  explicit ThemeAnimator(Tick tick);
  ThemeAnimator(const ThemeAnimator&) = delete;
  ThemeAnimator& operator=(const ThemeAnimator&) = delete;
  ~ThemeAnimator();

  // A repeat of the values already held is a no-op: re-arming the timer on
  // every preview frame is how the ambient tick starved.
  void Configure(bool night_shift, bool ambient, bool reduced_motion);
  // The widget visibility hook is the only gate on the 30fps tick - no
  // occlusion check. A repeat is a no-op, as for Configure.
  void SetVisible(bool visible);
  bool running() const;

  // Restarts the ambient clock at phase 0 - call when a NEW theme spec is
  // painted, never for a plain visibility flip or a repeat Configure of the
  // spec already running.
  void ResetPhase();

  // The night-shift hour lookup, exposed so a caller rasters with the same
  // clock (real or injected) this animator's own ticks use.
  double LocalHourNow() const;

  // For tests: the clock LocalHourNow reads.
  void set_clock_for_testing(const base::Clock* clock) { clock_ = clock; }

 private:
  void OnTick();
  bool EffectiveAmbient() const;
  void Restart();

  Tick tick_;
  bool night_shift_ = false;
  bool ambient_ = false;
  bool reduced_motion_ = false;
  bool visible_ = true;
  base::TimeTicks ambient_start_;
  base::RepeatingTimer timer_;
  raw_ptr<const base::Clock> clock_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_ANIMATOR_H_
