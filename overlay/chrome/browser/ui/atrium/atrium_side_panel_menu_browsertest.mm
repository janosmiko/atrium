// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include <memory>

#include "base/apple/scoped_objc_class_swizzler.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_settings_menu_hook.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#include "ui/views/view.h"

// A test app run over SSH is never active, so no window becomes key.
static __weak NSWindow* g_fake_key_window = nil;

@interface AtriumFakeKeyWindow : NSObject
@end

@implementation AtriumFakeKeyWindow
- (NSWindow*)keyWindow {
  return g_fake_key_window;
}
@end

namespace atrium {
namespace {

using AtriumSidePanelMenuTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumSidePanelMenuTest,
                       TheViewMenuCarriesShowSidePanel) {
  NSMenuItem* item = SidePanelMenuItemForTesting();
  ASSERT_TRUE(item) << "the hook added no side panel item";
  NSMenu* view_menu = [NSApp.mainMenu itemWithTag:IDC_VIEW_MENU].submenu;
  ASSERT_TRUE(view_menu);
  EXPECT_EQ(item.menu, view_menu);
  EXPECT_NSEQ(@"Show Side Panel", item.title);
  ASSERT_TRUE(item.target);
  // No window has shown a panel yet, so there is nothing to reopen.
  EXPECT_FALSE([item.target validateMenuItem:item]);
}

IN_PROC_BROWSER_TEST_F(AtriumSidePanelMenuTest, AShownPanelFlipsTheTitle) {
  auto* window = static_cast<AtriumBrowserWindow*>(browser()->window());
  g_fake_key_window = window->GetNativeWindow().GetNativeNSWindow();
  base::apple::ScopedObjCClassSwizzler key_window(
      [NSApplication class], [AtriumFakeKeyWindow class], @selector(keyWindow));
  ASSERT_EQ(NSApp.keyWindow, g_fake_key_window);
  window->ShowExtensionPanel(std::make_unique<views::View>(), /*owner=*/nullptr,
                             "abcdefghijklmnopabcdefghijklmnop");
  NSMenuItem* item = SidePanelMenuItemForTesting();
  ASSERT_TRUE(item);

  EXPECT_TRUE([item.target validateMenuItem:item]);
  EXPECT_NSEQ(@"Hide Side Panel", item.title);
}

}  // namespace
}  // namespace atrium
