// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_key_route_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_MAC_H_

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/atrium/atrium_surface_key_route.h"

namespace atrium {

// Reads `event`'s chord into the shape RouteForSurfaceKey decides over, so the
// palette and the peek card ask the same question of the same values instead
// of each matching the chord themselves - which is how they came to make the
// same promise about Cmd+Q and both break it.
//
// Anything that is not a key event yields an empty chord, which routes to the
// web content exactly as an unrecognised key does.
SurfaceKeyInputs SurfaceKeyInputsForEvent(NSEvent* event,
                                          bool atrium_action_matched);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_MAC_H_
