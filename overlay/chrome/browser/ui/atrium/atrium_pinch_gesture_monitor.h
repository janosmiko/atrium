// Copyright 2026 The Atrium Authors
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_MONITOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_MONITOR_H_

#import <Cocoa/Cocoa.h>

namespace atrium {
class AtriumBrowserWindow;
}

// Recognised here rather than in surface JS: swipe.js ignores ctrlKey, and a
// pinch over a page body reaches no WebContents at all.
@interface AtriumPinchGestureMonitor : NSObject

// `browser_window` is not retained - the owner must call -invalidate before
// it is destroyed.
- (instancetype)initWithWindow:(NSWindow*)window
                 browserWindow:(atrium::AtriumBrowserWindow*)browser_window;

// Removes the installed NSEvent monitor. Safe to call more than once.
- (void)invalidate;

@end

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_MONITOR_H_
