// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/extension_popup_watchdog_policy.cc
#include "chrome/browser/ui/atrium/extension_popup_watchdog_policy.h"

namespace atrium {

ExtensionPopupWatchdogPolicy::ExtensionPopupWatchdogPolicy() = default;
ExtensionPopupWatchdogPolicy::~ExtensionPopupWatchdogPolicy() = default;

void ExtensionPopupWatchdogPolicy::Arm() {
  state_ = State::kArmed;
}

bool ExtensionPopupWatchdogPolicy::OnShown() {
  if (state_ != State::kArmed) {
    return false;
  }
  state_ = State::kShown;
  return true;
}

void ExtensionPopupWatchdogPolicy::OnShowFailed() {
  state_ = State::kIdle;
}

bool ExtensionPopupWatchdogPolicy::OnTimerFired() {
  if (state_ != State::kArmed) {
    return false;
  }
  state_ = State::kIdle;
  return true;
}

void ExtensionPopupWatchdogPolicy::Reset() {
  state_ = State::kIdle;
}

}  // namespace atrium
