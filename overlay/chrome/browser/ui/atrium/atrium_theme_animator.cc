// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_theme_animator.h"

#include "base/functional/bind.h"
#include "base/time/default_clock.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

namespace {
constexpr base::TimeDelta kNightShiftInterval = base::Minutes(1);
}

ThemeAnimator::ThemeAnimator(Tick tick)
    : tick_(std::move(tick)),
      ambient_start_(base::TimeTicks::Now()),
      clock_(base::DefaultClock::GetInstance()) {}

ThemeAnimator::~ThemeAnimator() = default;

void ThemeAnimator::Configure(bool night_shift,
                              bool ambient,
                              bool reduced_motion) {
  // Restart() stops and re-arms the timer, so a repeat of the flags already
  // running would postpone the next tick. A theme scrub reconfigures faster
  // than 30fps fires, which pinned the ambient phase at 0.
  if (night_shift_ == night_shift && ambient_ == ambient &&
      reduced_motion_ == reduced_motion) {
    return;
  }
  night_shift_ = night_shift;
  ambient_ = ambient;
  reduced_motion_ = reduced_motion;
  Restart();
}

void ThemeAnimator::SetVisible(bool visible) {
  if (visible_ == visible) {
    return;  // Same postponement as Configure's - the scrub calls both.
  }
  visible_ = visible;
  Restart();
}

bool ThemeAnimator::running() const {
  return timer_.IsRunning();
}

bool ThemeAnimator::EffectiveAmbient() const {
  return ambient_ && !reduced_motion_;
}

void ThemeAnimator::ResetPhase() {
  ambient_start_ = base::TimeTicks::Now();
}

void ThemeAnimator::Restart() {
  timer_.Stop();
  if (!visible_ || (!EffectiveAmbient() && !night_shift_)) {
    return;
  }
  const base::TimeDelta interval =
      EffectiveAmbient() ? base::Hertz(30) : kNightShiftInterval;
  timer_.Start(FROM_HERE, interval,
              base::BindRepeating(&ThemeAnimator::OnTick,
                                  base::Unretained(this)));
}

void ThemeAnimator::OnTick() {
  const double ambient_time =
      EffectiveAmbient() ? (base::TimeTicks::Now() - ambient_start_).InSecondsF()
                        : -1.0;
  const double warmth =
      night_shift_ ? atrium_core_night_shift_offset(LocalHourNow()) : 0.0;
  tick_.Run(ambient_time, warmth);
}

double ThemeAnimator::LocalHourNow() const {
  base::Time::Exploded exploded;
  clock_->Now().LocalExplode(&exploded);
  return exploded.hour + exploded.minute / 60.0 + exploded.second / 3600.0;
}

}  // namespace atrium
