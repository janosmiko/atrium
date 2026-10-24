// Copyright 2026 The Atrium Authors

#import <AppKit/AppKit.h>

#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_settings_titlebar_mac.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/hit_test.h"
#include "ui/color/color_provider_key.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/widget/widget.h"

namespace atrium {
namespace {

using AtriumSettingsWindowTitlebarBrowserTest = InProcessBrowserTest;

// Opens the window and waits through its ready handshake, the same sequence
// AtriumNativeChromeOverlaysBrowserTest uses.
AtriumSettingsWindow* OpenReadySettings() {
  AtriumSettingsWindow::Show();
  AtriumSettingsWindow* settings = AtriumSettingsWindow::ForTesting();
  if (!settings) {
    return nullptr;
  }
  content::WebContents* page = settings->WebContentsForTesting();
  if (!page || !content::WaitForLoadStop(page)) {
    return nullptr;
  }
  base::ListValue ready;
  settings->OnSurfaceMessage("settingsReady", ready);
  return settings;
}

IN_PROC_BROWSER_TEST_F(AtriumSettingsWindowTitlebarBrowserTest,
                       OpensWithNoTitleTextAndKeepsTrafficLights) {
  AtriumSettingsWindow* settings = OpenReadySettings();
  ASSERT_TRUE(settings);
  NSWindow* ns_window =
      settings->WidgetForTesting()->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);

  // No titlebar text, and no titlebar space above the content either.
  EXPECT_EQ(ns_window.titleVisibility, NSWindowTitleHidden);
  EXPECT_TRUE(ns_window.titlebarAppearsTransparent);
  EXPECT_TRUE(ns_window.styleMask & NSWindowStyleMaskFullSizeContentView);

  // The traffic lights are still there to click.
  EXPECT_TRUE([ns_window standardWindowButton:NSWindowCloseButton]);
  EXPECT_TRUE([ns_window standardWindowButton:NSWindowMiniaturizeButton]);
  EXPECT_TRUE([ns_window standardWindowButton:NSWindowZoomButton]);

  // Still resizable.
  EXPECT_TRUE(ns_window.styleMask & NSWindowStyleMaskResizable);
}

// System Settings' look, with Native app view left off.
IN_PROC_BROWSER_TEST_F(AtriumSettingsWindowTitlebarBrowserTest,
                       WearsSidebarVibrancyAndTheSystemGround) {
  AtriumSettingsWindow* settings = OpenReadySettings();
  ASSERT_TRUE(settings);
  NSWindow* ns_window =
      settings->WidgetForTesting()->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);

  EXPECT_FALSE(ns_window.opaque) << "an opaque window hides the vibrancy";
  NSVisualEffectView* vibrancy = nil;
  for (NSView* child in ns_window.contentView.subviews) {
    if ([child isKindOfClass:[NSVisualEffectView class]]) {
      vibrancy = static_cast<NSVisualEffectView*>(child);
    }
  }
  ASSERT_TRUE(vibrancy) << "no NSVisualEffectView under the page";
  EXPECT_EQ(vibrancy.material, NSVisualEffectMaterialSidebar);
  EXPECT_EQ(vibrancy.blendingMode, NSVisualEffectBlendingModeBehindWindow);
  EXPECT_EQ(vibrancy.state, NSVisualEffectStateFollowsWindowActiveState);

  content::WebContents* page = settings->WebContentsForTesting();
  EXPECT_EQ(true, content::EvalJs(page, "document.documentElement.classList."
                                        "contains('atrium-native-chrome')"))
      << "settings must be native whatever Native app view says";
  const SkColor light = SystemWindowBackgroundColor(/*dark=*/false);
  const SkColor dark = SystemWindowBackgroundColor(/*dark=*/true);
  EXPECT_NE(light, dark);
  const bool is_dark = settings->WidgetForTesting()->GetColorMode() ==
                       ui::ColorProviderKey::ColorMode::kDark;
  const SkColor expected = is_dark ? dark : light;
  EXPECT_EQ(
      base::StringPrintf("#%02x%02x%02x", SkColorGetR(expected),
                         SkColorGetG(expected), SkColorGetB(expected)),
      content::EvalJs(page, "document.documentElement.style.getPropertyValue("
                            "'--atrium-surround-color')"));
}

IN_PROC_BROWSER_TEST_F(AtriumSettingsWindowTitlebarBrowserTest,
                       CloseButtonActuallyClosesTheWindow) {
  AtriumSettingsWindow* settings = OpenReadySettings();
  ASSERT_TRUE(settings);
  NSWindow* ns_window =
      settings->WidgetForTesting()->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);

  [ns_window performClose:nil];
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !AtriumSettingsWindow::IsShowing(); }))
      << "the traffic-light close button never closed the window";
}

// The page's own drag strip (settings.css's .settings-drag-region)
// resolves to HTCAPTION once the renderer has reported it, and a control
// below it - here, the search field - keeps its own clicks.
// The page lets an Escape on an empty search field fall through
// (settings.js), so the window has to answer it.
IN_PROC_BROWSER_TEST_F(AtriumSettingsWindowTitlebarBrowserTest,
                       EscapeClosesTheWindow) {
  AtriumSettingsWindow* settings = OpenReadySettings();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);

  // Handed to the delegate the way the renderer hands back an unhandled key.
  // A synthetic press cannot round-trip here: the Mac forwarder redispatches
  // the native NSEvent, and a synthetic one has none.
  input::NativeWebKeyboardEvent escape(
      blink::WebKeyboardEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now());
  escape.windows_key_code = ui::VKEY_ESCAPE;
  ASSERT_TRUE(page->GetDelegate());
  EXPECT_TRUE(page->GetDelegate()->HandleKeyboardEvent(page, escape));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !AtriumSettingsWindow::IsShowing(); }))
      << "Escape never closed the settings window";
}

IN_PROC_BROWSER_TEST_F(AtriumSettingsWindowTitlebarBrowserTest,
                       DragRegionCapturesDragAndSparesControls) {
  AtriumSettingsWindow* settings = OpenReadySettings();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);

  const gfx::Point in_drag_region(50, 15);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return settings->NonClientHitTestForTesting(in_drag_region) == HTCAPTION;
  })) << "the drag strip never registered as a draggable region";

  // Well inside the search field the sidebar column now carries beneath the
  // drag strip - a mutation that dropped the carve would report HTCAPTION
  // here too and no click would ever reach the field.
  const gfx::Point in_search_field(50, 65);
  EXPECT_NE(settings->NonClientHitTestForTesting(in_search_field), HTCAPTION)
      << "the drag region swallowed a click meant for the search field";
}

}  // namespace
}  // namespace atrium
