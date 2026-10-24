// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_EDGE_REVEAL_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_EDGE_REVEAL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"

namespace atrium {

class AtriumBrowserWindow;

// Reveals a hidden sidebar when the pointer reaches the window's docked edge,
// and hides it again when the pointer leaves.
//
// The gesture used to belong to AtriumSidebarOverlayWidget, which
// floated a SECOND sidebar view over the content. That widget needs an
// overlay_sidebar_view from AtriumCreateSidebarForSlot - a Swift entry point
// since deleted - so nothing has been able to construct it since, and
// the reveal was dead rather than broken. This reveals the sidebar the window
// already has, by the same SetSidebarWidth path Cmd+S uses, so there is no
// second view to keep in sync and whichever rail pane was showing comes back
// as it was.
//
// TWO ZONE WIDTHS, not one. While the pane is hidden the zone is a thin strip
// at the edge, so an ordinary pointer crossing does not arm it. While the pane
// is open BY HOVER the zone covers the pane itself - otherwise the pointer
// moving off the strip and onto the sidebar it just revealed would read as
// leaving, and the pane would flicker shut under the cursor. This is the
// eager-dismissal shape fixed for the overlay, restated for the
// docked pane.
//
// A HOVER REVEAL IS NOT A DOCK. Only a reveal this class performed is undone
// when the pointer leaves: if the user presses Cmd+S while the pane is open by
// hover, the pane is theirs from then on and moving the pointer away must not
// take it back.
class AtriumSidebarEdgeReveal {
 public:
  explicit AtriumSidebarEdgeReveal(AtriumBrowserWindow* window);
  ~AtriumSidebarEdgeReveal();

  AtriumSidebarEdgeReveal(const AtriumSidebarEdgeReveal&) = delete;
  AtriumSidebarEdgeReveal& operator=(const AtriumSidebarEdgeReveal&) = delete;

  // Re-reads the pane's state and re-arms the zone at the matching width.
  // Called from AtriumBrowserWindow::SetSidebarWidth, which is the one place
  // every collapse and expand goes through.
  //
  // `collapsed` is the width the caller settled on, not the width on screen -
  // the pane animates, so asking the view would answer about the frame in
  // flight rather than the destination.
  void OnSidebarWidthChanged(bool collapsed);

  // Gives up ownership of the current reveal without closing it: the pane the
  // user just docked stays open when the pointer leaves. Called from the
  // Cmd+S route.
  void ForgetReveal();

  bool revealed_by_hover() const { return revealed_by_hover_; }

 private:
  void OnEnter();
  void OnExit();

  // Installs the zone at `width`, replacing any earlier one. A width of 0
  // removes it, which is what a window with no sidebar gets.
  //
  // `pointer_is_inside` must be true only when the pointer really is within
  // the new zone - the widening that follows a reveal. It suppresses the first
  // mouseEntered, which is what makes the next exit arrive; passing it for the
  // arming strip would suppress the entered event the reveal depends on and
  // the gesture would never fire at all.
  void ArmZone(int width, bool pointer_is_inside);

  // The tracking area and the zone view. Held behind a pointer because this
  // header is included from plain C++ translation units, where an Objective-C
  // member does not compile - `nil` and `__strong` are not declared there.
  struct ObjCState;

  const raw_ptr<AtriumBrowserWindow> window_;
  std::unique_ptr<ObjCState> objc_;
  // Whether the pane currently open was opened by this class. Only then does
  // the pointer leaving close it.
  bool revealed_by_hover_ = false;
  base::WeakPtrFactory<AtriumSidebarEdgeReveal> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_EDGE_REVEAL_H_
