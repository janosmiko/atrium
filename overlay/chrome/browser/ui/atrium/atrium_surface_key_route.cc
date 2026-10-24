// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_key_route.cc
#include "chrome/browser/ui/atrium/atrium_surface_key_route.h"

namespace atrium {

bool IsQuitChord(const SurfaceKeyInputs& in) {
  return in.command && !in.shift && !in.option && !in.control &&
         in.character == "q";
}

SurfaceKeyRoute RouteForSurfaceKey(const SurfaceKeyInputs& in) {
  // FIRST, ahead of both the action match and the repeat guard below.
  //
  // Ahead of the match because quitting must not depend on what else happens
  // to be bound: a rebind onto Cmd+Q costs the user the action, not the quit.
  // Ahead of the repeat guard because kDropRepeat is a hard swallow, so a
  // leaned-on Cmd+Q would quit on the first press and then do nothing at all.
  //
  // And ahead of the caller's web content, which is the reason this ordering
  // exists: a live renderer claims the chord and answers asynchronously, so
  // any main-menu route reached after it is never reached at all.
  if (IsQuitChord(in)) {
    return SurfaceKeyRoute::kMainMenu;
  }
  if (!in.atrium_action_matched) {
    return SurfaceKeyRoute::kWebContent;
  }
  if (in.is_repeat) {
    return SurfaceKeyRoute::kDropRepeat;
  }
  return SurfaceKeyRoute::kAtriumAction;
}

}  // namespace atrium
