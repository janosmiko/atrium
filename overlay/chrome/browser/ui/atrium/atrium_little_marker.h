// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_MARKER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_MARKER_H_

namespace atrium {

// Little Atrium: true for the duration of Browser::Create()
// called from BrowserController::CreateLittleWindow's self-creating ctor arm
// - lets browser_window_factory.cc build a little-mode AtriumBrowserWindow
// (no sidebar, minimal top chrome strip instead) for that ONE Browser, since
// Browser itself has no SupportsUserData/UnownedUserDataHost slot cheap
// enough to stamp a marker on before Browser::Create() returns it. Same
// bracket-around-Browser::Create() pattern as BrowserController's file-local
// CreatingOwnBrowser() flag (atrium_browser_controller.cc) - deliberately a
// SEPARATE, exported flag rather than widening that one's visibility,
// because CreatingLittleBrowser() must be readable from
// browser_window_factory.cc, a different translation unit with no other
// reason to depend on atrium_browser_controller.h's much heavier surface
// (TabStripModelObserver, content::WebContents, etc.) - this header has no
// dependencies at all, matching slot_presentation_table.h's "fast, standalone
// gtest unit" posture.
//
// Single-threaded UI-thread invariant, same as every other plain static
// global in the atrium layer (BrowserController::Registry(), etc.) - never
// read/written off the main thread.
bool& CreatingLittleBrowser();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_MARKER_H_
