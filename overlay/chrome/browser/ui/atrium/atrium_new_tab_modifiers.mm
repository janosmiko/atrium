// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_new_tab_modifiers.mm
#include "chrome/browser/ui/atrium/atrium_new_tab_modifiers.h"

#import <AppKit/AppKit.h>

namespace atrium {

namespace {

std::optional<bool>& HeldOverride() {
  static std::optional<bool> override;
  return override;
}

std::optional<bool>& OptionOnlyOverride() {
  static std::optional<bool> override;
  return override;
}

}  // namespace

bool NewTabModifiersHeld() {
  if (HeldOverride().has_value()) {
    return *HeldOverride();
  }
  constexpr NSEventModifierFlags kNewTab =
      NSEventModifierFlagCommand | NSEventModifierFlagShift;
  return ([NSEvent modifierFlags] & kNewTab) == kNewTab;
}

void SetNewTabModifiersHeldForTesting(std::optional<bool> held) {
  HeldOverride() = held;
}

bool OptionOnlyModifierHeld() {
  if (OptionOnlyOverride().has_value()) {
    return *OptionOnlyOverride();
  }
  constexpr NSEventModifierFlags kExcluded =
      NSEventModifierFlagCommand | NSEventModifierFlagShift;
  const NSEventModifierFlags flags = [NSEvent modifierFlags];
  return (flags & NSEventModifierFlagOption) != 0 && (flags & kExcluded) == 0;
}

void SetOptionOnlyModifierHeldForTesting(std::optional<bool> held) {
  OptionOnlyOverride() = held;
}

}  // namespace atrium
