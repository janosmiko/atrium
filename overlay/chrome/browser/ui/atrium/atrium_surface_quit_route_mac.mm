// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_quit_route_mac.mm
#import "chrome/browser/ui/atrium/atrium_surface_quit_route_mac.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include "base/logging.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/views/widget/widget.h"

// Routes the quit chord to the app main menu before this window's own view
// hierarchy is offered the event, and declines everything else.
//
// Declining the rest is deliberate rather than unfinished. A surface that
// carries this and nothing more behaves for every other chord exactly as it did
// with no delegate at all, so the only thing installing it can change is
// whether Cmd+Q survives.
@interface AtriumSurfaceQuitRouteDelegate : NSObject <CommandDispatcherDelegate>
@end

@implementation AtriumSurfaceQuitRouteDelegate

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  // No Atrium hotkey is bound on this surface, so the route question is asked
  // with atrium_action_matched false and only kMainMenu can come back.
  if (atrium::RouteForSurfaceKey(atrium::SurfaceKeyInputsForEvent(
          event, /*atrium_action_matched=*/false)) ==
      atrium::SurfaceKeyRoute::kMainMenu) {
    VLOG(1) << "Atrium: settings passing key-equivalent to main menu (keyCode "
            << event.keyCode << ")";
    return ui::PerformKeyEquivalentResult::kPassToMainMenu;
  }
  return ui::PerformKeyEquivalentResult::kUnhandled;
}

- (ui::PerformKeyEquivalentResult)postPerformKeyEquivalent:(NSEvent*)event
                                                    window:(NSWindow*)window
                                              isRedispatch:(BOOL)isRedispatch {
  // Required by the protocol, and answered the way an absent delegate is
  // answered. This surface has no parent window whose web content could eat a
  // declined chord - the reason the palette and the peek card force the main
  // menu here - so forcing it would change behaviour this task did not measure.
  return ui::PerformKeyEquivalentResult::kUnhandled;
}

@end

namespace atrium {

namespace {

// CommandDispatcher holds its delegate weakly; this keeps it alive for the
// window's lifetime.
const char kSurfaceQuitRouteDelegateKey = '\0';

}  // namespace

void InstallQuitRouteOnSurfaceWindow(views::Widget* widget) {
  if (!widget) {
    return;
  }
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window ||
      ![ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    return;
  }
  CommandDispatcher* dispatcher =
      [static_cast<id<CommandDispatchingWindow>>(ns_window) commandDispatcher];
  AtriumSurfaceQuitRouteDelegate* delegate =
      [[AtriumSurfaceQuitRouteDelegate alloc] init];
  dispatcher.delegate = delegate;
  objc_setAssociatedObject(ns_window, &kSurfaceQuitRouteDelegateKey, delegate,
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

}  // namespace atrium
