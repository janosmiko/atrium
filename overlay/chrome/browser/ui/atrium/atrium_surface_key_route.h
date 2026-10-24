// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_key_route.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_H_

#include <string>

namespace atrium {

// what an activatable Atrium child surface does with a key
// equivalent BEFORE its own view hierarchy is offered the event.
//
// Two surfaces take key away from the browser window and host a WebContents of
// their own: the command palette (CommandBarWidget) and the peek card
// (PeekWidget). Both install a CommandDispatcherDelegate that promises to hand
// unclaimed chords - "Cmd+Q above all" - to the app main menu. Both made that
// promise from postPerformKeyEquivalent, and that is the wrong hook:
// CommandDispatcher only calls postPerform when the window's view hierarchy
// DECLINES the event, and a live RenderWidgetHostViewCocoa never declines. It
// returns YES and forwards the key to its renderer asynchronously, so the
// main-menu route is skipped entirely.
//
// The promise therefore only held while the surface had no live page. The
// startup palette is exactly that - it opens before the reserved chrome
// context exists, logs "command bar has no chrome context yet", and shows an
// empty scrim - which is why Cmd+Q kept working there and the gap stayed
// hidden. A palette opened later (Cmd+N's new window slot, Cmd+T) has a real
// page, and Cmd+Q vanished into its renderer.
//
// So the route is decided here, before the web content, and the decision is
// values in / an answer out - no AppKit, no views:: - the same posture as
// the core's commands.rs and atrium_peek_decision.h.
struct SurfaceKeyInputs {
  bool command = false;
  bool shift = false;
  bool option = false;
  bool control = false;

  // charactersIgnoringModifiers, lowercased. Empty for a key with no character
  // (arrows, function keys) and for anything that is not a key event. Owned by
  // value rather than viewed: these structs are built per keystroke from an
  // autoreleased NSString, and a view into that is a lifetime trap for one
  // byte of copying.
  std::string character;

  // The key is held down.
  bool is_repeat = false;

  // An Atrium hotkey (Cmd+T, Cmd+S, ...) is bound to this chord.
  bool atrium_action_matched = false;
};

// True for the chord the app menu's Quit item is bound to: Command alone plus
// "q". Matched on the character rather than a physical keyCode so it stays
// correct on non-QWERTY layouts, the way the menu item itself is.
bool IsQuitChord(const SurfaceKeyInputs& in);

enum class SurfaceKeyRoute {
  // Hand to the app main menu now, before the surface's own view hierarchy
  // sees the event. The only route a live renderer cannot intercept.
  kMainMenu,
  // A held Atrium combo. Swallowed so it cannot strobe the palette/sidebar.
  kDropRepeat,
  // Run the Atrium action bound to this chord.
  kAtriumAction,
  // Decline. The surface's own web content takes it - edit combos belong to
  // the focused field - and the postPerform main-menu route catches whatever
  // that declines.
  kWebContent,
};

// Which route the chord takes.
SurfaceKeyRoute RouteForSurfaceKey(const SurfaceKeyInputs& in);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_KEY_ROUTE_H_
