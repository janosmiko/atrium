// Copyright 2026 The Atrium Authors

#include "base/apple/scoped_cftyperef.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#import "ui/events/test/cocoa_test_event_utils.h"

// drives the real AppKit monitor, not the decision-logic seam.
// -[NSApp sendEvent:] invokes a local monitor's handler directly, no run
// loop or OS event injection needed.

namespace atrium {
namespace {

using PinchGestureMonitorBrowserTest = InProcessBrowserTest;

// cocoa_test_event_utils::TestScrollEvent always zeroes the CGEvent's flags
// (see its header comment on flaky keyboard-state reads), so ctrl+wheel
// needs its own builder reusing the same CGEventCreateScrollWheelEvent +
// AttachWindowToCGEvent building blocks.
NSEvent* CtrlWheelEvent(NSWindow* window, CGFloat delta_y) {
  base::apple::ScopedCFTypeRef<CGEventRef> scroll(
      CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine,
                                    /*wheelCount=*/2,
                                    static_cast<int32_t>(delta_y), 0));
  CGEventSetFlags(scroll.get(), kCGEventFlagMaskControl);
  return cocoa_test_event_utils::AttachWindowToCGEvent(scroll.get(), window);
}

NSEvent* PlainWheelEvent(NSWindow* window, CGFloat delta_y) {
  return cocoa_test_event_utils::TestScrollEvent(
      NSMakePoint(10, 10), window, /*delta_x=*/0, delta_y,
      /*has_precise_deltas=*/false, NSEventPhaseNone, NSEventPhaseNone);
}

// Same builder as CtrlWheelEvent, but at a chosen window-local point (Cocoa
// coordinates, origin bottom-left) instead of whatever the default lands on.
NSEvent* CtrlWheelEventAtPoint(NSWindow* window,
                               CGFloat delta_y,
                               NSPoint window_point) {
  base::apple::ScopedCFTypeRef<CGEventRef> scroll(
      CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine,
                                    /*wheelCount=*/2,
                                    static_cast<int32_t>(delta_y), 0));
  CGEventSetLocation(
      scroll.get(),
      cocoa_test_event_utils::ScreenPointFromWindow(window_point, window));
  CGEventSetFlags(scroll.get(), kCGEventFlagMaskControl);
  return cocoa_test_event_utils::AttachWindowToCGEvent(scroll.get(), window);
}

IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       RealCtrlWheelOpensCourtyardWithoutSwipe) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  // Local monitors only fire while the app is active.
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];
  ASSERT_FALSE(window->IsCourtyardShowing());
  const int space_before = atrium_core_active_space_index();

  // 40 then 31 crosses the 70 threshold on the second sample, same split as
  // the decision-logic unit test and the seam-driven test.
  // Over the sidebar because the monitor hit-tests the location since
  // And an unplaced CGEvent lands off it.
  // Mid-height: x alone picks the sidebar column, and the AppKit-to-Views y
  // flip maps the middle to the middle, so this point is over the sidebar in
  // either orientation. The earlier (10, 10) sat on the window's bottom edge.
  const NSPoint over_sidebar =
      NSMakePoint(10, ns_window.contentView.bounds.size.height / 2);
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 40, over_sidebar)];
  EXPECT_FALSE(window->IsCourtyardShowing());
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 31, over_sidebar)];

  // Mutation this pins: if the monitor never installs, or handleEvent: never
  // reaches applyAction:, a real ctrl+wheel leaves the Courtyard closed.
  EXPECT_TRUE(window->IsCourtyardShowing())
      << "a real ctrl+wheel NSEvent routed through the AppKit monitor never "
         "opened the Courtyard";
  EXPECT_EQ(atrium_core_active_space_index(), space_before)
      << "the real event also drove swipe.js's space switch - the monitor "
         "did not consume it (return nil)";
}

IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       PlainWheelPassesThroughAndResetsAccumulator) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];

  // Over the sidebar, or this test proves nothing: off it the monitor returns
  // {0, kNone} and never accumulates, so the reset below has nothing to clear
  // and the expectation passes with the reset deleted.
  // Mid-height: x alone picks the sidebar column, and the AppKit-to-Views y
  // flip maps the middle to the middle, so this point is over the sidebar in
  // either orientation. The earlier (10, 10) sat on the window's bottom edge.
  const NSPoint over_sidebar =
      NSMakePoint(10, ns_window.contentView.bounds.size.height / 2);
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 40, over_sidebar)];
  ASSERT_FALSE(window->IsCourtyardShowing());

  // A plain (non-ctrl) wheel event must drop the partial ctrl+wheel run.
  [NSApp sendEvent:PlainWheelEvent(ns_window, 5)];

  // 31 alone never crosses 70. Had the accumulator not been reset, the
  // leftover 40 would combine with it and open the Courtyard here.
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 31, over_sidebar)];
  EXPECT_FALSE(window->IsCourtyardShowing())
      << "the plain wheel event did not reset the ctrl+wheel accumulator";
}

IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       MonitorFiltersToItsOwnWindow) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  // Local monitors only fire while the app is active.
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];
  ASSERT_FALSE(window->IsCourtyardShowing());

  Browser* second_browser = CreateBrowser(browser()->profile());
  auto* second_window =
      static_cast<atrium::AtriumBrowserWindow*>(second_browser->window());
  ASSERT_TRUE(second_window);
  NSWindow* second_ns_window =
      second_window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(second_ns_window);
  ASSERT_NE(ns_window, second_ns_window);
  [second_ns_window makeKeyAndOrderFront:nil];

  // Attach the event to the SECOND window. The first window's monitor must
  // ignore it (event.window != _window) and let it fall through untouched.
  // Over that window's sidebar, or its own monitor's hit-test refuses it and
  // the second expectation below fails for the wrong reason.
  // Mid-height: x alone picks the sidebar column, and the AppKit-to-Views y
  // flip maps the middle to the middle, so this point is over the sidebar in
  // either orientation. The earlier (10, 10) sat on the window's bottom edge.
  const NSPoint over_sidebar =
      NSMakePoint(10, second_ns_window.contentView.bounds.size.height / 2);
  [NSApp sendEvent:CtrlWheelEventAtPoint(second_ns_window, 40, over_sidebar)];
  [NSApp sendEvent:CtrlWheelEventAtPoint(second_ns_window, 31, over_sidebar)];

  // Mutation this pins: dropping (or inverting) the event.window != _window
  // check would make the first window's monitor react to the second
  // window's events too.
  EXPECT_FALSE(window->IsCourtyardShowing())
      << "the first window's monitor reacted to an event aimed at another "
         "window";
  EXPECT_TRUE(second_window->IsCourtyardShowing())
      << "the second window's own monitor should have opened its Courtyard";

  CloseBrowserSynchronously(second_browser);
}

// While the Courtyard is open a real ctrl+wheel
// reaches the PLANE and nothing else. The overlay covers the whole window, so
// the page under it must never see the event either.
IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       CtrlWheelInsideTheCourtyardZoomsThePlane) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];

  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  const std::string before =
      content::EvalJs(courtyard,
                      "document.querySelector('.plane').style.transform")
          .ExtractString();

  [NSApp sendEvent:CtrlWheelEvent(ns_window, 40)];
  [NSApp sendEvent:CtrlWheelEvent(ns_window, 31)];

  // Mutation this pins: leaving kZoomCanvas unhandled in the monitor, or
  // dropping the ForwardPinchToCourtyard call, leaves the plane untouched and
  // still open - which a "did it close" check alone would call a pass.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard,
                           "document.querySelector('.plane').style.transform")
               .ExtractString() != before;
  })) << "a real ctrl+wheel never reached the plane's transform";
  EXPECT_TRUE(window->IsCourtyardShowing())
      << "a pinch above the zoom floor closed the Courtyard";
}

// The close belongs to the reported zoom, not the threshold alone. A
// spread below 1.0 only zooms the plane back in. The same spread at or
// above 1.0 closes.
// a zoom that lands on a whole number crosses as an int, and the
// report used to be read behind an is_double() gate that dropped it.
IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       AWholeNumberZoomReportIsNotDropped) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  // The floor is 0.35, a fractional report, so the gate below cannot be
  // satisfied by anything left over from before.
  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "for (let i = 0; i < 60; i++) { window.atriumCourtyardPinch(200); }"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->IsCourtyardZoomAtOrAboveOne(); }))
      << "the plane never reported a zoom below 1.0";

  // One sample big enough to clear the ceiling, so the only report between
  // here and the check is the clamped whole number 2.
  ASSERT_TRUE(
      content::ExecJs(courtyard, "window.atriumCourtyardPinch(-4000)"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->IsCourtyardZoomAtOrAboveOne(); }))
      << "the ceiling zoom of 2 never reached the widget";
}

IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       SpreadClosesTheCourtyardOnlyAtOrAboveZoomOne) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  // Drive the plane below zoom 1.0 first, so the first spread below proves
  // the gate blocks the close rather than the threshold never firing.
  ASSERT_TRUE(content::ExecJs(
      courtyard, "for (let i = 0; i < 60; i++) { window.atriumCourtyardPinch(200); }"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->IsCourtyardZoomAtOrAboveOne(); }))
      << "the plane never reported a zoom below 1.0";
  ASSERT_TRUE(window->IsCourtyardShowing());

  [NSApp sendEvent:CtrlWheelEvent(ns_window, -40)];
  [NSApp sendEvent:CtrlWheelEvent(ns_window, -31)];
  EXPECT_TRUE(window->IsCourtyardShowing())
      << "a spread below zoom 1.0 closed the Courtyard";

  // Drive it back to or above zoom 1.0, then the same spread must close.
  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "for (let i = 0; i < 60; i++) { window.atriumCourtyardPinch(-200); }"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->IsCourtyardZoomAtOrAboveOne(); }))
      << "the plane never reported a zoom at or above 1.0";
  ASSERT_TRUE(window->IsCourtyardShowing());

  [NSApp sendEvent:CtrlWheelEvent(ns_window, -40)];
  [NSApp sendEvent:CtrlWheelEvent(ns_window, -31)];

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->IsCourtyardShowing(); }))
      << "a spread at or above zoom 1.0 did not close the Courtyard";
}

// A pinch over the page must reach the page's own zoom, not the
// Courtyard, so it lands far from the sidebar column.
IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       CtrlWheelOverThePageNeverOpensTheCourtyard) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];
  ASSERT_FALSE(window->IsCourtyardShowing());

  const CGFloat width = ns_window.contentView.bounds.size.width;
  const NSPoint over_page = NSMakePoint(width - 10, 10);

  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 40, over_page)];
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 31, over_page)];

  EXPECT_FALSE(window->IsCourtyardShowing())
      << "a ctrl+wheel over the page opened the Courtyard";
}

IN_PROC_BROWSER_TEST_F(PinchGestureMonitorBrowserTest,
                       CtrlWheelOverTheSidebarStillOpensTheCourtyard) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  NSWindow* ns_window = window->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:nil];
  ASSERT_FALSE(window->IsCourtyardShowing());

  // Mid-height: x alone picks the sidebar column, and the AppKit-to-Views y
  // flip maps the middle to the middle, so this point is over the sidebar in
  // either orientation. The earlier (10, 10) sat on the window's bottom edge.
  const NSPoint over_sidebar =
      NSMakePoint(10, ns_window.contentView.bounds.size.height / 2);

  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 40, over_sidebar)];
  [NSApp sendEvent:CtrlWheelEventAtPoint(ns_window, 31, over_sidebar)];

  EXPECT_TRUE(window->IsCourtyardShowing())
      << "a ctrl+wheel over the docked sidebar never opened the Courtyard";
}

}  // namespace
}  // namespace atrium
