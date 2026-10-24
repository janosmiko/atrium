// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_overlay_widget.cc
#include "chrome/browser/ui/atrium/atrium_capture_overlay_widget.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_capture_overlay_math.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace atrium {

namespace {

// Minimal v1 visual treatment (per the task): a single dim fill with the
// in-progress selection punched out, plus a thin border - no separate
// scrim/selection layers.
constexpr SkColor kOverlayDimColor = SkColorSetA(SK_ColorBLACK, 0x66);
constexpr SkColor kSelectionBorderColor = SK_ColorWHITE;
constexpr float kSelectionBorderWidth = 1.5f;

}  // namespace

// CaptureOverlayRootView -----------------------------------------------------

CaptureOverlayRootView::CaptureOverlayRootView() {
  // Widget-level Escape cancel - PeekRootView's identical precedent
  // (atrium_peek_widget.h/.mm): registration is deferred by
  // View::AddAccelerator until this view is attached to a real Widget, so
  // calling it here, before AtriumCaptureOverlayWidget has even Init()'d
  // widget_, is safe.
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

CaptureOverlayRootView::~CaptureOverlayRootView() = default;

void CaptureOverlayRootView::SetCommitCallback(CommitCallback callback) {
  commit_callback_ = std::move(callback);
}

void CaptureOverlayRootView::SetCancelCallback(CancelCallback callback) {
  cancel_callback_ = std::move(callback);
}

gfx::Rect CaptureOverlayRootView::NormalizedDragRect() const {
  if (!dragging_) {
    return gfx::Rect();
  }
  const int x = std::min(drag_start_.x(), drag_current_.x());
  const int y = std::min(drag_start_.y(), drag_current_.y());
  const int w = std::abs(drag_current_.x() - drag_start_.x());
  const int h = std::abs(drag_current_.y() - drag_start_.y());
  return gfx::Rect(x, y, w, h);
}

bool CaptureOverlayRootView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  dragging_ = true;
  drag_start_ = event.location();
  drag_current_ = drag_start_;
  SchedulePaint();
  return true;
}

bool CaptureOverlayRootView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!dragging_) {
    return false;
  }
  drag_current_ = event.location();
  SchedulePaint();
  return true;
}

void CaptureOverlayRootView::OnMouseReleased(const ui::MouseEvent& event) {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  drag_current_ = event.location();
  const gfx::Point start = drag_start_;
  const gfx::Point end = drag_current_;
  SchedulePaint();
  // The commit callback (AtriumCaptureOverlayWidget::OnCommit) decides
  // zero-size/out-of-pane cancellation via ComputeCaptureRegionRect - see
  // atrium_capture_overlay_math.h - so a plain click (no movement) still
  // reaches it rather than being special-cased here.
  if (commit_callback_) {
    std::move(commit_callback_).Run(start, end);
  }
}

void CaptureOverlayRootView::OnMouseCaptureLost() {
  if (!dragging_) {
    return;
  }
  // Mouse capture was stolen mid-drag (e.g. the window lost focus) - treat
  // like Escape: cancel rather than commit a possibly-incomplete selection.
  dragging_ = false;
  SchedulePaint();
  if (cancel_callback_) {
    cancel_callback_.Run();
  }
}

void CaptureOverlayRootView::OnPaint(gfx::Canvas* canvas) {
  const gfx::Rect bounds(0, 0, width(), height());
  if (bounds.IsEmpty()) {
    return;
  }

  cc::PaintFlags dim_flags;
  dim_flags.setAntiAlias(true);
  dim_flags.setStyle(cc::PaintFlags::kFill_Style);
  dim_flags.setColor(kOverlayDimColor);

  const gfx::Rect selection = NormalizedDragRect();
  if (selection.IsEmpty()) {
    // No selection in progress yet - dim the whole pane.
    canvas->DrawRect(gfx::RectF(bounds), dim_flags);
    return;
  }

  // Even-odd fill rule over [outer bounds, selection rect]: the two
  // contours' overlapping region (the selection) cancels out, leaving it
  // unpainted - the real page underneath shows through there while
  // everything else stays dimmed. Kept as a single path/single draw call
  // per the task's "keep it minimal for v1" instruction, rather than a
  // second view/layer for the selection hole.
  SkPathBuilder builder(SkPathFillType::kEvenOdd);
  builder.addRect(gfx::RectToSkRect(bounds));
  builder.addRect(gfx::RectToSkRect(selection));
  canvas->DrawPath(builder.detach(), dim_flags);

  cc::PaintFlags border_flags;
  border_flags.setAntiAlias(true);
  border_flags.setStyle(cc::PaintFlags::kStroke_Style);
  border_flags.setStrokeWidth(kSelectionBorderWidth);
  border_flags.setColor(kSelectionBorderColor);
  canvas->DrawRect(gfx::RectF(selection), border_flags);
}

bool CaptureOverlayRootView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  VLOG(1) << "Atrium: capture overlay escape pressed";
  dragging_ = false;
  SchedulePaint();
  if (cancel_callback_) {
    cancel_callback_.Run();
  }
  return true;
}

// AtriumCaptureOverlayWidget --------------------------------------------------

AtriumCaptureOverlayWidget::AtriumCaptureOverlayWidget(
    AtriumBrowserWindow* parent,
    base::WeakPtr<AtriumBrowserWindow> parent_weak,
    CommitCallback callback)
    : parent_(parent),
      parent_weak_(std::move(parent_weak)),
      commit_callback_(std::move(callback)) {
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = "AtriumCaptureOverlay";
  // Translucent (the dim/selection-hole painting relies on unpainted pixels
  // showing the real page through) + activatable (PeekWidget/CommandBarWidget
  // precedent): this widget must take key so its Escape accelerator fires,
  // and it must sit above the page's RenderWidgetHostView so mouse events
  // land on root_view_ instead of the page - see this class's header doc.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.SetParent(parent_->widget());

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanActivate(true);
  root_view_ = widget_delegate_->SetContentsView(
      std::make_unique<CaptureOverlayRootView>());
  params.delegate = widget_delegate_.get();

  widget_->Init(std::move(params));

  root_view_->SetCommitCallback(base::BindOnce(
      &AtriumCaptureOverlayWidget::OnCommit, weak_factory_.GetWeakPtr()));
  root_view_->SetCancelCallback(base::BindRepeating(
      &AtriumCaptureOverlayWidget::OnCancel, weak_factory_.GetWeakPtr()));

  // Continuous parent-bounds tracking - PeekWidget precedent. Removed in the
  // destructor before `this` is torn down.
  parent_->widget()->AddObserver(this);

  ApplyParentBounds();
  // Show() (not ShowInactive) - the overlay needs key focus immediately so
  // Escape and mouse events land on root_view_, same rationale as
  // PeekWidget::Show().
  widget_->Show();
  parent_->OrderOverlayChild(widget_.get(),
                             AtriumBrowserWindow::OverlayLayer::kCaptureOverlay);
}

AtriumCaptureOverlayWidget::~AtriumCaptureOverlayWidget() {
  parent_->widget()->RemoveObserver(this);
}

void AtriumCaptureOverlayWidget::ApplyParentBounds() {
  // Sized to EXACTLY the web-content pane, no inset - see this class's
  // header doc for why that makes root_view_'s local origin coincide with
  // atrium::CaptureRegion's content-coordinate origin. Screen coordinates -
  // top-level widget, InitParams::child never set, same as
  // PeekWidget::ApplyParentBounds.
  gfx::Rect content_bounds = parent_->GetWebContentBoundsInScreen();
  if (content_bounds.IsEmpty()) {
    // Web-content pane not laid out yet - shouldn't happen in practice (a
    // capture can only be started from an already-on-screen window), but
    // fall back to the parent's own window bounds rather than collapsing to
    // nothing - same defensive fallback as PeekWidget::ApplyParentBounds.
    content_bounds = parent_->widget()->GetWindowBoundsInScreen();
  }
  widget_->SetBounds(content_bounds);
}

void AtriumCaptureOverlayWidget::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  // Only the PARENT's moves/resizes matter here; this widget's own bounds
  // changes (including the ones ApplyParentBounds itself causes) are
  // ignored, so this can never self-recurse - PeekWidget::
  // OnWidgetBoundsChanged precedent.
  if (widget != parent_->widget()) {
    return;
  }
  ApplyParentBounds();
}

void AtriumCaptureOverlayWidget::OnCommit(const gfx::Point& drag_start,
                                          const gfx::Point& drag_end) {
  const gfx::Rect content_rect = ComputeCaptureRegionRect(
      drag_start, drag_end, root_view_->GetLocalBounds());

  widget_->Hide();
  // Deferred, not synchronous: this runs from inside root_view_'s own
  // OnMouseReleased dispatch (via commit_callback_) - destroying `this` (and
  // therefore root_view_) synchronously here would free the view while it is
  // still on the call stack. PostTask defers the actual teardown
  // (AtriumBrowserWindow::FinishRegionCaptureIfCurrent resetting the owning
  // unique_ptr) to the next run-loop turn, same posture as PeekWidget's
  // deferred-dismiss check.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AtriumBrowserWindow::FinishRegionCaptureIfCurrent,
                     parent_weak_, weak_factory_.GetWeakPtr()));

  if (!content_rect.IsEmpty() && commit_callback_) {
    std::move(commit_callback_).Run(content_rect);
  }
}

void AtriumCaptureOverlayWidget::OnCancel() {
  widget_->Hide();
  // Same deferred-teardown posture as OnCommit above.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AtriumBrowserWindow::FinishRegionCaptureIfCurrent,
                     parent_weak_, weak_factory_.GetWeakPtr()));
  // commit_callback_ is simply dropped (never run) - it is destroyed along
  // with `this` once the deferred teardown above runs.
}

}  // namespace atrium
