// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_BINDINGS_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_BINDINGS_SCRIPT_H_

#include <map>
#include <string>

namespace atrium {

// The script the sidebar document runs to learn every native hotkey's glyph,
// keyed by action id ("toggleSidebar" -> "⌘S"). Guarded because
// window.atriumSetSidebarBindings does not exist until the page defines it.
std::string SidebarBindingsScript(
    const std::map<std::string, std::string>& glyphs_by_action);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_BINDINGS_SCRIPT_H_
