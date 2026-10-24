// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_overlay_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_WIDGET_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace ui {
class Accelerator;
class MouseEvent;
}  // namespace ui

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class Widget;
class WidgetDelegate;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// the region-select overlay's root view - a full-pane view that
// dims the page and punches a "hole" over the user's in-progress drag
// selection (OnPaint below), Escape cancels (widget-level accelerator,
// PeekRootView precedent - atrium_peek_widget.h), mouse-up commits the drag
// as the current selection. Minimal v1 visual treatment per the task: a
// single dim fill with an even-odd SkPath cutout for the selection rect plus
// a thin border, no separate scrim/selection layers.
class CaptureOverlayRootView : public views::View {
 public:
  // Runs once per drag, on mouse-up, with the raw mouse-down/mouse-up points
  // in this view's own local coordinates (NOT yet normalized or mapped to
  // content DIP space - AtriumCaptureOverlayWidget::OnCommit does both, via
  // atrium_capture_overlay_math.h's ComputeCaptureRegionRect, which is also
  // where a zero-size/out-of-pane drag is decided to be a cancel).
  using CommitCallback =
      base::OnceCallback<void(const gfx::Point&, const gfx::Point&)>;
  // Runs on Escape or on losing mouse capture mid-drag (e.g. the window
  // loses focus). A zero-size/out-of-pane drag is NOT routed here - it still
  // goes through CommitCallback above and is decided downstream by
  // AtriumCaptureOverlayWidget::OnCommit (ComputeCaptureRegionRect), the one
  // place that logic lives.
  using CancelCallback = base::RepeatingClosure;

  CaptureOverlayRootView();
  ~CaptureOverlayRootView() override;

  CaptureOverlayRootView(const CaptureOverlayRootView&) = delete;
  CaptureOverlayRootView& operator=(const CaptureOverlayRootView&) = delete;

  void SetCommitCallback(CommitCallback callback);
  void SetCancelCallback(CancelCallback callback);

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

 private:
  // The normalized (non-negative width/height) rect between drag_start_ and
  // drag_current_, in this view's local coordinates. Empty before a drag
  // starts or if the drag hasn't moved yet.
  gfx::Rect NormalizedDragRect() const;

  gfx::Point drag_start_;
  gfx::Point drag_current_;
  bool dragging_ = false;
  CommitCallback commit_callback_;
  CancelCallback cancel_callback_;
};

// A frameless, activatable child views::Widget covering the content pane for
// the region-select screenshot flow. Modeled on PeekWidget's
// parent-bounds tracking (atrium_peek_widget.h - continuous re-application
// via OnWidgetBoundsChanged, sized against
// AtriumBrowserWindow::GetWebContentBoundsInScreen) but NOT PeekWidget's
// build-once/never-rebuilt posture: this widget is one-shot per capture. See
// AtriumBrowserWindow::BeginRegionCapture, which owns exactly one instance at
// a time (capture_overlay_widget_) and replaces it (destroying any prior
// instance first) on re-entry, and AtriumBrowserWindow::~AtriumBrowserWindow,
// which resets it explicitly ahead of widget_.reset() - a child widget must
// not outlive the parent it's parented into.
//
// Activatable (like PeekWidget/CommandBarWidget): it must take key focus so
// its Escape accelerator fires, and it must sit above the page's own
// RenderWidgetHostView so mouse-down/drag/up land on THIS widget's root view
// instead of being consumed by the page.
//
// Coordinate-space conclusion (the CaptureRegion contract, read from
// atrium_capture.h/.cc): this widget's bounds are set to EXACTLY
// GetWebContentBoundsInScreen (ApplyParentBounds below, no inset), so the
// root view's own local origin (0,0) already coincides with the content
// pane's DIP origin - the SAME origin atrium::CaptureRegion's `content_rect`
// parameter is defined against ("DIP, in the WebContents' content coordinate
// space" - atrium_capture.h). OnCommit therefore only clamps/offsets in DIP
// (atrium_capture_overlay_math.h's ComputeCaptureRegionRect) and does NOT
// multiply by any device scale factor. Nothing downstream does either:
// RenderWidgetHostView::CopyFromSurface takes its src_rect in view coordinates
// and applies the scale itself, so DIP is the unit end to end. CaptureRegion
// used to convert here and that WAS the double-conversion this paragraph warns
// about - a 200x200 point selection at dpr 2 came back as an 800x276 image,
// the 4x rect clamped at the surface edge.
class AtriumCaptureOverlayWidget : public views::WidgetObserver {
 public:
  using CommitCallback = base::OnceCallback<void(const gfx::Rect&)>;

  // `parent` outlives this widget (see the class doc above for ownership).
  // `callback` runs exactly once, only on a successful commit, with the
  // selected region in content DIP coordinates - ready to pass straight to
  // atrium::CaptureRegion. Dropped (never run) if the overlay is cancelled
  // (Escape, a zero-size drag, or this object being destroyed first).
  //
  // Shows itself immediately - there is no separate Show() step, unlike
  // PeekWidget/CommandBarWidget: this widget has no reuse lifecycle to
  // separate construction from display (see the class doc's one-shot note).
  // `parent_weak`: weak handle to the SAME window
  // `parent` points at, used ONLY as the bound receiver of the
  // deferred-teardown PostTask - the raw `parent` is safe for every
  // synchronous use (the window owns this overlay and destroys it first),
  // but a posted task can outlive a Cmd+W'd window, and an Unretained
  // receiver there is a use-after-free.
  AtriumCaptureOverlayWidget(AtriumBrowserWindow* parent,
                             base::WeakPtr<AtriumBrowserWindow> parent_weak,
                             CommitCallback callback);
  ~AtriumCaptureOverlayWidget() override;

  AtriumCaptureOverlayWidget(const AtriumCaptureOverlayWidget&) = delete;
  AtriumCaptureOverlayWidget& operator=(const AtriumCaptureOverlayWidget&) =
      delete;

  // views::WidgetObserver:
  //
  // Continuous parent-bounds tracking, copied from PeekWidget's identically-
  // named override - re-applies this widget's bounds whenever the parent
  // window moves or resizes while the overlay is up. Acts only on the
  // parent widget's events.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

 private:
  // Sizes/positions widget_ against the parent's CURRENT web-content pane
  // (AtriumBrowserWindow::GetWebContentBoundsInScreen), in SCREEN
  // coordinates - top-level widget, InitParams::child never set, same as
  // PeekWidget::ApplyParentBounds.
  void ApplyParentBounds();

  // root_view_'s commit callback: maps the raw widget-local drag start/end
  // points to a content DIP rect (atrium_capture_overlay_math.h's
  // ComputeCaptureRegionRect - normalizes, clamps to the pane, and signals a
  // zero-size or fully-out-of-pane drag by returning an empty rect) and, on
  // a non-empty result, forwards it to commit_callback_. Either way, closes
  // the overlay.
  void OnCommit(const gfx::Point& drag_start, const gfx::Point& drag_end);

  // root_view_'s cancel callback (Escape or lost mouse capture mid-drag):
  // closes the overlay without running commit_callback_.
  void OnCancel();

  const raw_ptr<AtriumBrowserWindow> parent_;
  // See the constructor doc - deferred-teardown receiver only.
  const base::WeakPtr<AtriumBrowserWindow> parent_weak_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<CaptureOverlayRootView> root_view_ = nullptr;
  CommitCallback commit_callback_;

  // Backs the deferred-teardown PostTask in OnCommit/OnCancel (see their
  // comments) - guards against `this` already being gone by the time the
  // posted task runs. Chromium style: must be the last member.
  base::WeakPtrFactory<AtriumCaptureOverlayWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_WIDGET_H_
