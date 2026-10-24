// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_menu_hook.mm
#include "chrome/browser/ui/atrium/atrium_settings_menu_hook.h"

#import <Cocoa/Cocoa.h>

#include "base/logging.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "ui/gfx/native_ui_types.h"

namespace {

// The key window presents whichever profile's Browser is on screen.
atrium::AtriumBrowserWindow* KeyAtriumWindow() {
  NSWindow* key = NSApp.keyWindow;
  return key ? atrium::BrowserController::WindowForNativeWindow(
                   gfx::NativeWindow(key))
             : nullptr;
}

}  // namespace

// Retargeted onto the app menu's Settings item in place of AppController.
// Retained for process lifetime by g_settings_menu_handler below: installed
// once, never torn down.
@interface AtriumSettingsMenuHandler : NSObject
@end

@implementation AtriumSettingsMenuHandler

- (void)atriumShowSettings:(id)sender {
  VLOG(1) << "Atrium: settings menu item claimed";
  atrium::AtriumSettingsWindow::Show();
}

- (void)atriumShowImport:(id)sender {
  VLOG(1) << "Atrium: import menu item claimed";
  atrium::AtriumSettingsWindow::Show("import");
}

- (void)atriumToggleExtensionSidePanel:(id)sender {
  if (atrium::AtriumBrowserWindow* window = KeyAtriumWindow()) {
    window->ToggleExtensionPanel();
  }
}

// The item's enabled state used to come from AppController's
// -validateUserInterfaceItem:. AppKit asks the item's EXPLICIT target, so
// retargeting away from AppController stops that validator running for this
// item, and a handler implementing neither validator leaves autoenablesItems
// in an unpredictable state. So an answer has to be given.
//
// The answer is YES, always, and not merely because it is simplest. Stock
// gates this item on canOpenNewBrowser because chrome://settings is a TAB and
// needs a browser window to host it. The Atrium settings window is its own
// top-level window, one per process, and needs no browser window at all - it
// opens from a running app with every window closed. The stock condition is
// about a constraint this window does not have. The side panel item answers
// for itself above.
- (BOOL)validateMenuItem:(NSMenuItem*)item {
  if (item.action == @selector(atriumToggleExtensionSidePanel:)) {
    atrium::AtriumBrowserWindow* window = KeyAtriumWindow();
    item.title = window && window->ExtensionPanelShowing() ? @"Hide Side Panel"
                                                           : @"Show Side Panel";
    return window && window->CanToggleExtensionPanel();
  }
  return YES;
}

@end

namespace {

AtriumSettingsMenuHandler* g_settings_menu_handler = nil;
NSMenuItem* g_side_panel_menu_item = nil;

}  // namespace

namespace atrium {

void InstallSettingsMenuHook() {
  // Once per process.
  if (g_settings_menu_handler) {
    return;
  }
  NSMenu* main_menu = NSApp.mainMenu;
  if (!main_menu) {
    LOG(WARNING) << "Atrium: no main menu at settings-menu-hook install time; "
                    "the Settings menu item stays stock";
    return;
  }
  // The app menu is the first top-level item's submenu - see
  // main_menu_builder.mm's BuildAppMenu and BuildMainMenu.
  NSMenu* app_menu = main_menu.itemArray.firstObject.submenu;
  if (!app_menu) {
    LOG(WARNING) << "Atrium: no app menu at settings-menu-hook install time; "
                    "the Settings menu item stays stock";
    return;
  }
  g_settings_menu_handler = [[AtriumSettingsMenuHandler alloc] init];

  // Chrome's Mac View menu has no side panel item to retarget, so the hook
  // adds one.
  NSMenu* view_menu = [main_menu itemWithTag:IDC_VIEW_MENU].submenu;
  if (view_menu) {
    [view_menu addItem:[NSMenuItem separatorItem]];
    g_side_panel_menu_item =
        [[NSMenuItem alloc] initWithTitle:@"Show Side Panel"
                                   action:@selector(atriumToggleExtensionSidePanel:)
                            keyEquivalent:@""];
    g_side_panel_menu_item.target = g_settings_menu_handler;
    [view_menu addItem:g_side_panel_menu_item];
    VLOG(1) << "Atrium: side panel menu item added";
  } else {
    LOG(WARNING) << "Atrium: no View menu; the side panel item is missing";
  }

  NSMenuItem* settings_item = nil;
  for (NSMenuItem* item in app_menu.itemArray) {
    if (item.action == @selector(showPreferences:)) {
      settings_item = item;
      break;
    }
  }
  if (!settings_item) {
    LOG(WARNING) << "Atrium: Settings menu item not found; it stays stock";
    return;
  }

  settings_item.target = g_settings_menu_handler;
  settings_item.action = @selector(atriumShowSettings:);
  VLOG(1) << "Atrium: settings menu item retargeted";

  // Chromium's stock item opens chrome://settings/importData, which Atrium
  // does not serve. main_menu_builder.mm tags each item with its command id.
  NSMenuItem* import_item = [app_menu itemWithTag:IDC_IMPORT_SETTINGS];
  if (!import_item) {
    LOG(WARNING) << "Atrium: Import menu item not found; it stays stock";
    return;
  }
  import_item.target = g_settings_menu_handler;
  import_item.action = @selector(atriumShowImport:);
  VLOG(1) << "Atrium: import menu item retargeted";
}

NSMenuItem* SidePanelMenuItemForTesting() {
  return g_side_panel_menu_item;
}

}  // namespace atrium
