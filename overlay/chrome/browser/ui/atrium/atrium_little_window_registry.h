// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_WINDOW_REGISTRY_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_WINDOW_REGISTRY_H_

class BrowserWindowInterface;

namespace atrium {

// dependency-free (little_marker.h's posture) so
// ProfileBrowserCollection can query it without linking mac-only atrium.
// AtriumBrowserWindow's ctor/dtor are the sole writers.
void RegisterLittleWindow(BrowserWindowInterface* browser);
void UnregisterLittleWindow(BrowserWindowInterface* browser);
bool IsLittleWindow(const BrowserWindowInterface* browser);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_LITTLE_WINDOW_REGISTRY_H_
