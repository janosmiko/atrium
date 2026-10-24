// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ambilight_cadence.h"

namespace atrium {

int AmbilightCadence::OnSample(bool changed) {
  if (changed) {
    Reset();
    return interval_ms();
  }
  ++unchanged_;
  if (unchanged_ >= kAmbilightStepAfter && rung_ + 1 < kAmbilightRungsMs.size()) {
    ++rung_;
    unchanged_ = 0;
  }
  return interval_ms();
}

int AmbilightCadence::interval_ms() const {
  return kAmbilightRungsMs[rung_];
}

void AmbilightCadence::Reset() {
  rung_ = 0;
  unchanged_ = 0;
}

}  // namespace atrium
