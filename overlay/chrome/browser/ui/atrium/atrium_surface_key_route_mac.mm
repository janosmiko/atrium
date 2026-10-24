// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_key_route_mac.mm
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"

#include "base/strings/sys_string_conversions.h"

namespace atrium {

SurfaceKeyInputs SurfaceKeyInputsForEvent(NSEvent* event,
                                          bool atrium_action_matched) {
  SurfaceKeyInputs in;
  in.atrium_action_matched = atrium_action_matched;
  if (!event || (event.type != NSEventTypeKeyDown &&
                 event.type != NSEventTypeKeyUp)) {
    return in;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  in.command = (modifiers & NSEventModifierFlagCommand) != 0;
  in.shift = (modifiers & NSEventModifierFlagShift) != 0;
  in.option = (modifiers & NSEventModifierFlagOption) != 0;
  in.control = (modifiers & NSEventModifierFlagControl) != 0;
  in.is_repeat = event.isARepeat;

  // charactersIgnoringModifiers, lowercased: the chord is matched on the
  // character the menu item is bound to, not a physical keyCode, so it holds
  // on non-QWERTY layouts. Multi-character strings (dead keys) name no chord
  // Atrium routes and are left empty.
  NSString* characters = event.charactersIgnoringModifiers;
  if (characters.length == 1) {
    in.character = base::SysNSStringToUTF8(characters.lowercaseString);
  }
  return in;
}

}  // namespace atrium
