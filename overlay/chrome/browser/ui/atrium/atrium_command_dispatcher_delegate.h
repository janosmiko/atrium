// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_dispatcher_delegate.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_DISPATCHER_DELEGATE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_DISPATCHER_DELEGATE_H_

#import "chrome/browser/ui/cocoa/chrome_command_dispatcher_delegate.h"

// Native hotkeys: the raw pre-first-responder hook that
// makes the C++-owned actions (command bar, sidebar toggle) rebindable.
// Chrome's delegate only recognizes combos its menu/shortcut tables already
// map to IDC commands - a user combo like cmd+shift+k maps to nothing, so
// the IDC intercepts can never see it (see the retired ABI header's NATIVE HOTKEYS
// doc block). This subclass matches the raw NSEvent against
// atrium::NativeHotkeyMap FIRST and falls through to the stock delegate for
// everything else, so non-Atrium keys behave byte-identically to upstream.
// M3 (Arc-parity tab-switching): also matches a hardcoded, non-rebindable
// Ctrl-Tab/Ctrl-Shift-Tab chord BEFORE the NativeHotkeyMap lookup, rerouting
// to the recency tab cycle (AtriumOnTabCycle axis 0) - see the retired ABI header's
// TAB CYCLING doc block for why this chord can't be caught at the IDC seam.
// Installed per Atrium window in AtriumNativeWidgetMac::OnWindowInitialized;
// BrowserView windows (if any ever coexist) keep the stock delegate.
@interface AtriumCommandDispatcherDelegate : ChromeCommandDispatcherDelegate
@end

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_DISPATCHER_DELEGATE_H_
