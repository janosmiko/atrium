// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_menu_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_MENU_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_MENU_HOOK_H_

#if defined(__OBJC__)
@class NSMenuItem;
#endif

namespace atrium {

// Points the app menu's Settings item at the Atrium settings window.
//
// main_menu_builder.mm wires that item with an explicit
// .target(app_delegate).action(@selector(showPreferences:)), unlike most items,
// which use .command_id and route through commandDispatch to CommandDispatcher
// and on to the IDC_OPTIONS intercept. An explicit target and action bypasses
// that chain for the click, so the intercept never fires from this item and
// un-gating it would bring nothing back.
//
// Retargets the item at runtime rather than editing upstream: finds it, and
// points it at an Atrium-owned handler. No upstream file changes, and no
// swizzling.
//
// Call once, after chrome::BuildMainMenu and before any browser window exists
// (AtriumMainExtraParts::PreBrowserStart). If the item cannot be found - an
// upstream layout change - it logs and leaves the menu stock.
void InstallSettingsMenuHook();

#if defined(__OBJC__)
// View > Show Side Panel, or nil before the hook ran.
NSMenuItem* SidePanelMenuItemForTesting();
#endif

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_MENU_HOOK_H_
