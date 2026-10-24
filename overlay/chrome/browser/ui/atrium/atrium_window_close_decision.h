// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_close_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CLOSE_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CLOSE_DECISION_H_

namespace atrium {

// Everything the Last-tab-out interception knows about one close request.
struct WindowCloseInputs {
  bool tab_strip_empty = false;

  // A resolved controller that is not a little window. A controller-less
  // window is not an Atrium space window and keeps stock close behaviour, and
  // a little window has no sidebar to show cold tabs in.
  bool space_window = false;

  // chrome::CloseAllBrowsersAndQuit's teardown sweep. A real quit must never
  // be re-intercepted.
  bool trying_to_quit = false;

  // An incognito window held open would pin an OTR profile that is supposed
  // to evaporate on last-window close.
  bool off_the_record = false;

  // An explicit Cmd+Shift+W. Keep-open exists for an AUTOMATIC empty-strip
  // close, so the user's own request wins over it.
  bool user_initiated = false;
};

// True when the window stays open showing its space's cold tabs instead of
// letting the close commit.
bool ShouldKeepWindowOpen(const WindowCloseInputs& inputs);

// True when a user-initiated close held at a beforeunload prompt is no longer
// in flight, so the flag it armed belongs to nothing. Only CancelWindowClose
// clears the attempt, so a prompt answered "leave" keeps both alive.
bool UserInitiatedCloseWasAbandoned(bool deferred_on_prompt,
                                    bool attempting_to_close);

// What an AppKit close request (windowShouldClose:) means for this window.
enum class AppKitCloseAction {
  // The second pass of a close already draining: CloseWithReason spent the
  // synchronous override, so the commit arrives through AppKit. Let it go.
  kLetCommitThrough,
  // A fresh close from the traffic light or the File menu item.
  kStartUserInitiated,
  // A close is already posted for this window - a second click while the
  // first is still at its beforeunload prompt must not queue another.
  kAlreadyStarting,
};

AppKitCloseAction ChooseAppKitCloseAction(bool expect_commit_close,
                                          bool close_post_pending);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CLOSE_DECISION_H_
