// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_app_icon_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_APP_ICON_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_APP_ICON_MAC_H_

#include <string>

namespace atrium {

// The DOCK only. Cmd+Tab, Force Quit, About and Finder resolve through the
// bundle, so they keep showing app.icns until the next `make icns` and
// relaunch (docs/BUILD.md, "Bundle icon").

// Asynchronous, because the PNG is a file and reading one on the UI thread is
// a fatal DCHECK. A slug with no file behind it leaves the icon alone.
void AtriumApplyAppIcon(const std::string& slug);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_APP_ICON_MAC_H_
