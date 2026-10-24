// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#import <AppKit/AppKit.h>
#include <dlfcn.h>

#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_platform_mac.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

using AtriumWindowCornerRadiusBrowserTest = InProcessBrowserTest;

// jankyborders reads the window-server radius SetServerCornerRadius pushes,
// which a test cannot read back directly. UpdateCornerMask sets this layer's
// cornerRadius in the same call, so it stands in for the private push.
IN_PROC_BROWSER_TEST_F(AtriumWindowCornerRadiusBrowserTest,
                       ContentViewLayerCarriesTheWindowCornerRadius) {
  NSWindow* ns_window =
      browser()->window()->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  ASSERT_TRUE(ns_window.contentView);
  ASSERT_TRUE(ns_window.contentView.wantsLayer)
      << "the content view has no layer - UpdateCornerMask never ran";
  ASSERT_TRUE(ns_window.contentView.layer);
  EXPECT_NEAR(ns_window.contentView.layer.cornerRadius,
              AtriumWindowCornerRadius(), 0.5)
      << "the content view's layer does not carry AtriumWindowCornerRadius() "
      << "- AtriumWindowPlatform::UpdateCornerMask never pushed it, and "
      << "jankyborders will draw square corners around this window";
  EXPECT_TRUE(ns_window.contentView.layer.masksToBounds);
}

// The layer test above passes whether or not the window server heard anything,
// because UpdateCornerMask sets the layer after SetServerCornerRadius returns.
// This one reads the push itself, which is what jankyborders draws from.
IN_PROC_BROWSER_TEST_F(AtriumWindowCornerRadiusBrowserTest,
                       TheWindowServerGetsTheRadiusToo) {
  const ServerCornerRadiusPush push = LastServerCornerRadiusPush();
  EXPECT_GT(push.count, 0)
      << "no SkyLight transaction ever committed - the window server still "
      << "holds 0 and jankyborders will draw square corners";
  EXPECT_NEAR(push.radius, AtriumWindowCornerRadius(), 0.5);
  EXPECT_TRUE(push.system_setter_ran)
      << "only the legacy setter fired. macOS 26 reads the system radius, and "
      << "AppKit does not fill it in for a translucent window";
}

// What jankyborders reads (SLSWindowIteratorGetCornerRadii), once, at order-in.
double ServerCornerRadius(NSWindow* ns_window) {
  using ConnectionFn = int (*)();
  using QueryFn = CFTypeRef (*)(int, CFArrayRef, uint32_t);
  using CopyFn = CFTypeRef (*)(CFTypeRef);
  using AdvanceFn = bool (*)(CFTypeRef);
  using RadiiFn = CFArrayRef (*)(CFTypeRef);
  void* skylight =
      dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
             RTLD_LAZY | RTLD_LOCAL);
  auto connection =
      reinterpret_cast<ConnectionFn>(dlsym(skylight, "SLSMainConnectionID"));
  auto query =
      reinterpret_cast<QueryFn>(dlsym(skylight, "SLSWindowQueryWindows"));
  auto copy = reinterpret_cast<CopyFn>(
      dlsym(skylight, "SLSWindowQueryResultCopyWindows"));
  auto advance =
      reinterpret_cast<AdvanceFn>(dlsym(skylight, "SLSWindowIteratorAdvance"));
  auto radii = reinterpret_cast<RadiiFn>(
      dlsym(skylight, "SLSWindowIteratorGetCornerRadii"));
  if (!connection || !query || !copy || !advance || !radii) {
    return -1;
  }
  NSArray* ids = @[ @(ns_window.windowNumber) ];
  CFTypeRef result = query(connection(), (__bridge CFArrayRef)ids, 0);
  if (!result) {
    return -2;
  }
  double value = -3;
  CFTypeRef iterator = copy(result);
  if (iterator && advance(iterator)) {
    CFArrayRef corners = radii(iterator);
    if (corners && CFArrayGetCount(corners) > 0) {
      CFNumberGetValue(
          static_cast<CFNumberRef>(CFArrayGetValueAtIndex(corners, 0)),
          kCFNumberDoubleType, &value);
    }
  }
  if (iterator) {
    CFRelease(iterator);
  }
  CFRelease(result);
  return value;
}

// The value AppKit writes to the server on each of its own flushes.
double AppKitEffectiveCornerRadius(NSWindow* ns_window) {
  SEL getter = NSSelectorFromString(@"_effectiveCornerRadius");
  if (![ns_window respondsToSelector:getter]) {
    return -1;
  }
  using GetterFn = CGFloat (*)(id, SEL);
  return reinterpret_cast<GetterFn>([ns_window methodForSelector:getter])(
      ns_window, getter);
}

void SpinFor(base::TimeDelta delay) {
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), delay);
  loop.Run();
}

// The push test above passes while jankyborders still draws 9pt corners: AppKit
// rewrites the server radius from its own value on its next flush, before
// order-in, and jankyborders reads once, at order-in.
IN_PROC_BROWSER_TEST_F(AtriumWindowCornerRadiusBrowserTest,
                       AppKitCarriesTheRadiusThroughItsOwnFlush) {
  NSWindow* ns_window =
      browser()->window()->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE(ns_window);
  EXPECT_NEAR(AppKitEffectiveCornerRadius(ns_window),
              AtriumWindowCornerRadius(), 0.5)
      << "AppKit still computes its own radius for this translucent window, "
      << "so its next flush writes that to the server and jankyborders, which "
      << "reads once at order-in, draws 9pt corners";
  NSRect frame = ns_window.frame;
  frame.size.width -= 10;
  [ns_window setFrame:frame display:YES];
  SpinFor(base::Milliseconds(300));
  EXPECT_NEAR(ServerCornerRadius(ns_window), AtriumWindowCornerRadius(), 0.5)
      << "the window server no longer holds AtriumWindowCornerRadius() after "
      << "AppKit flushed the frame change";
}

}  // namespace
}  // namespace atrium
