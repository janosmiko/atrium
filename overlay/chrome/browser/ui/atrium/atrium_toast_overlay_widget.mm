// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_toast_overlay_widget.mm
#include "chrome/browser/ui/atrium/atrium_toast_overlay_widget.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_overlay_shadow_layer.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace atrium {

namespace {

// toast-stack.css's own inset from the window corner.
constexpr int kToastInset = 14;
constexpr int kToastTopInset = 44;

// toast-stack.js's STACK_CAP - the most shadow slots one corner ever needs.
constexpr size_t kToastShadowSlots = 10;

U8CPU ClampToByte(double value) {
  return static_cast<U8CPU>(std::clamp(std::lround(value), 0L, 255L));
}

}  // namespace

ToastOverlayWidget::ToastOverlayWidget(AtriumBrowserWindow* parent,
                                       Corner corner)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "toast",
                             corner == Corner::kTopTrailing
                                 ? "AtriumToastOverlay"
                                 : "AtriumHoverUrlOverlay",
                             /*activatable=*/false),
      parent_(parent),
      corner_(corner) {
  // Nothing to click here - the hover card is built closable:false. Hit-testing
  // it took the pointer off the link underneath, the page dropped the hover,
  // and the card flickered away and back.
  if (corner_ == Corner::kBottomLeading) {
    if (NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
      ns_window.ignoresMouseEvents = YES;
    }
  }
  widget_->AddObserver(this);
  // Removed in the destructor, which AtriumBrowserWindow's destructor runs
  // before resetting its own widget_, so this can never dangle.
  parent_->widget()->AddObserver(this);
  parent_->widget()->GetRootView()->AddObserver(this);

  AtriumChromeContext::PostWhenReady(
      base::BindOnce(&ToastOverlayWidget::OnChromeContextReady,
                     weak_factory_.GetWeakPtr()));
}

ToastOverlayWidget::~ToastOverlayWidget() {
  parent_->widget()->GetRootView()->RemoveObserver(this);
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

void ToastOverlayWidget::PushPaneWidth() {
  if (corner_ != Corner::kBottomLeading) {
    return;
  }
  const int width = parent_->GetWebContentBoundsInScreen().width();
  if (width <= 0 || width == pushed_pane_width_) {
    return;
  }
  pushed_pane_width_ = width;
  CallPage(u"window.atriumSetPaneWidth && atriumSetPaneWidth(" +
           base::UTF8ToUTF16(base::NumberToString(width)) + u")");
}

void ToastOverlayWidget::ApplyStackBounds() {
  // Ahead of the empty-stack return: a sidebar drag under a standing card
  // reaches here through the pane's layout invalidation, and the page has to
  // learn the new width whether or not the card changed size.
  PushPaneWidth();
  if (stack_size_.IsEmpty()) {
    if (widget_->IsVisible()) {
      widget_->Hide();
    }
    return;
  }
  // Window bounds hold the sidebar column and, on the trailing side, the AI
  // panel, so either corner lands over that margin instead of the page
  //. The window only fills in before the pane is up.
  gfx::Rect anchor = parent_->widget()->GetWindowBoundsInScreen();
  const gfx::Rect pane = parent_->GetWebContentBoundsInScreen();
  if (!pane.IsEmpty()) {
    anchor = pane;
  }
  const bool on_left = StackOnLeft();
  // The inset is to the card, not to the widget: stack_outset_ is the room
  // reserved around the stack for the ring, so the widget hangs that much
  // further into the corner and the cards keep their own 14/44.
  const int x = on_left
                    ? anchor.x() + kToastInset - stack_outset_
                    : anchor.right() - kToastInset - stack_size_.width() +
                          stack_outset_;
  const int y = corner_ == Corner::kBottomLeading
                    ? anchor.bottom() - kToastInset - stack_size_.height() +
                          stack_outset_
                    : anchor.y() + kToastTopInset - stack_outset_;
  const gfx::Point previous_origin = last_applied_bounds_.origin();
  last_applied_bounds_ =
      gfx::Rect(x, y, stack_size_.width(), stack_size_.height());
  if (last_applied_bounds_.origin() != previous_origin) {
    // A shadow converts screen to view coordinates once, at Register time
    // (atrium_overlay_shadow_layer.h), so nothing here carries it along. A
    // window move pushes the page nothing, so no later push replaces it.
    ClearCardShadows();
  }
  widget_->SetBounds(last_applied_bounds_);
  if (!widget_->IsVisible()) {
    widget_->ShowInactive();
  }
  // Every show, not only the first: the hover card comes and goes on every
  // link, and a show puts a child window on top of its siblings.
  parent_->OrderOverlayChild(
      widget_.get(),
      corner_ == Corner::kBottomLeading
          ? AtriumBrowserWindow::OverlayLayer::kHoverUrlCard
          : AtriumBrowserWindow::OverlayLayer::kToastStack);
}

bool ToastOverlayWidget::StackOnLeft() const {
  // A confirmation takes the corner opposite the sidebar. A hovered URL stays
  // at the bottom leading edge, where it was before it became a card.
  return corner_ == Corner::kBottomLeading ? !parent_->IsSidebarOnRight()
                                           : parent_->IsSidebarOnRight();
}

void ToastOverlayWidget::OnViewLayoutInvalidated(views::View* observed_view) {
  // Both corners read pane bounds now, so both have to follow a reshape - an
  // AI panel opened under a standing confirmation moves the page out from
  // under it.
  if (pane_layout_reapply_pending_) {
    return;
  }
  pane_layout_reapply_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ToastOverlayWidget::ReapplyBoundsForPaneLayout,
                                weak_factory_.GetWeakPtr()));
}

void ToastOverlayWidget::ReapplyBoundsForPaneLayout() {
  pane_layout_reapply_pending_ = false;
  // A side flip only schedules the pane's Layout() pass async, so force it
  // now - otherwise ApplyStackBounds can read the pre-change geometry.
  parent_->widget()->GetRootView()->DeprecatedLayoutImmediately();
  ApplyStackBounds();
}

void ToastOverlayWidget::ShowConfirmation(const std::string& text, bool ok,
                                          const std::string& icon_name,
                                          const std::string& key) {
  BuildContents();
  if (!web_contents_) {
    return;
  }
  parent_->PushOverlayTheme(web_contents_.get());
  pending_show_ = true;
  pending_text_ = text;
  pending_ok_ = ok;
  pending_icon_ = icon_name;
  pending_key_ = key;
  pending_is_hover_ = false;
  PushPending();
}

void ToastOverlayWidget::ShowHoverUrl(const std::string& text) {
  BuildContents();
  if (!web_contents_) {
    return;
  }
  parent_->PushOverlayTheme(web_contents_.get());
  pending_show_ = true;
  pending_is_hover_ = true;
  pending_text_ = text;
  // Before the card, not after: the first hover fits its label the moment
  // atriumHoverUrl lands, well before any size report reaches ApplyStackBounds.
  PushPaneWidth();
  PushPending();
}

void ToastOverlayWidget::SetNavModifiers(bool shift, bool cmd, bool option) {
  if (corner_ != Corner::kBottomLeading) {
    return;
  }
  nav_shift_ = shift;
  nav_cmd_ = cmd;
  nav_option_ = option;
  const auto literal = [](bool held) {
    return std::u16string(held ? u"true" : u"false");
  };
  CallPage(u"window.atriumSetNavModifiers && atriumSetNavModifiers(" +
           literal(shift) + u", " + literal(cmd) + u", " + literal(option) +
           u")");
  VLOG(1) << "Atrium: hover url modifiers shift=" << shift << " cmd=" << cmd
          << " option=" << option;
}

void ToastOverlayWidget::PushPending() {
  if (!pending_show_ || !renderer_ready_) {
    return;
  }
  const std::u16string quoted =
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_text_));
  // Both corners, not just the confirmation: the hover card is the one that
  // resizes under a live pointer, and it is the anchor mismatch that slides it.
  const std::u16string side = StackOnLeft() ? u"'left'" : u"'right'";
  CallPage(u"window.atriumToastSide && window.atriumToastSide(" + side + u")");
  if (pending_is_hover_) {
    CallPage(u"window.atriumHoverUrl && window.atriumHoverUrl(" + quoted +
             u")");
    pending_show_ = false;
    return;
  }
  const std::u16string quoted_icon =
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_icon_));
  const std::u16string quoted_key =
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_key_));
  CallPage(u"window.atriumConfirmToast && window.atriumConfirmToast(" + quoted +
           u", '', " + (pending_ok_ ? u"true" : u"false") + u", " +
           quoted_icon + u", " + quoted_key + u")");
  pending_show_ = false;
}

bool ToastOverlayWidget::IsShowingForTesting() const {
  return widget_->IsVisible();
}

bool ToastOverlayWidget::DeclinesMouseForTesting() const {
  NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  return ns_window && ns_window.ignoresMouseEvents;
}

void ToastOverlayWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                               const gfx::Rect& new_bounds) {
  if (widget != parent_->widget()) {
    return;
  }
  // Ungated on visibility, unlike the guard that broke macOS refits
  // the window during the show handshake and the flag is not set yet.
  ApplyStackBounds();
}

void ToastOverlayWidget::OnSurfaceMessage(std::string_view name,
                                          const base::ListValue& args) {
  if (name == "toastReady") {
    renderer_ready_ = true;
    // Nothing reached the page before this, so the width sent during the
    // renderer's build was dropped by CallPage.
    pushed_pane_width_ = 0;
    PushPaneWidth();
    SetNavModifiers(nav_shift_, nav_cmd_, nav_option_);
    PushPending();
    // Only the confirmation corner shows a timed toast. The hover corner
    // never sets dismissAfterMs, so it has no use for the setting.
    if (corner_ == Corner::kTopTrailing) {
      parent_->PushNotificationTimeoutToToastOverlay();
    }
    return;
  }
  if (name == "toastStackEmpty") {
    stack_size_ = gfx::Size();
    ApplyStackBounds();
    ClearCardShadows();
    return;
  }
  if (name == "toastStackSized" && args.size() >= 2) {
    const std::optional<int> w = args[0].GetIfInt();
    const std::optional<int> h = args[1].GetIfInt();
    if (w && h) {
      stack_size_ = gfx::Size(std::max(0, *w), std::max(0, *h));
      std::optional<double> outset;
      if (args.size() >= 3) {
        outset = args[2].GetIfDouble();
      }
      stack_outset_ =
          outset ? std::max(0, static_cast<int>(std::lround(*outset))) : 0;
      ApplyStackBounds();
    }
    return;
  }
  if (name == "toastCardShadows" && !args.empty() && args[0].is_string()) {
    PushCardShadows(args[0].GetString());
  }
}

std::string ToastOverlayWidget::ShadowIdForIndex(size_t index) const {
  const char* prefix =
      corner_ == Corner::kTopTrailing ? "toast-confirm-" : "toast-hover-";
  return base::StrCat({prefix, base::NumberToString(index)});
}

void ToastOverlayWidget::ClearCardShadows() {
  registered_shadow_rects_.clear();
  slot_card_ids_.clear();
  for (size_t i = 0; i < kToastShadowSlots; ++i) {
    parent_->UnregisterOverlayShadow(ShadowIdForIndex(i));
  }
}

void ToastOverlayWidget::PushCardShadows(const std::string& json) {
  registered_shadow_rects_.clear();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    ClearCardShadows();
    return;
  }
  // last_applied_bounds_, not GetWindowBoundsInScreen(), which can still lag
  // one SetBounds behind mid-burst over remote_cocoa's async round trip.
  const gfx::Point origin = last_applied_bounds_.origin();
  std::vector<int> next_slot_card_ids;
  size_t index = 0;
  for (const base::Value& entry : parsed->GetList()) {
    if (index >= kToastShadowSlots) {
      break;
    }
    if (!entry.is_dict()) {
      parent_->UnregisterOverlayShadow(ShadowIdForIndex(index));
      next_slot_card_ids.push_back(0);
      ++index;
      continue;
    }
    const base::DictValue& card = entry.GetDict();
    const std::optional<double> left = card.FindDouble("left");
    const std::optional<double> top = card.FindDouble("top");
    const std::optional<double> width = card.FindDouble("width");
    const std::optional<double> height = card.FindDouble("height");
    const std::optional<double> radius = card.FindDouble("radius");
    const std::optional<double> offset_x = card.FindDouble("offsetX");
    const std::optional<double> offset_y = card.FindDouble("offsetY");
    const std::optional<double> blur = card.FindDouble("blur");
    const std::optional<double> r = card.FindDouble("r");
    const std::optional<double> g = card.FindDouble("g");
    const std::optional<double> b = card.FindDouble("b");
    const std::optional<double> a = card.FindDouble("a");
    if (!left || !top || !width || !height || !radius || !offset_x ||
        !offset_y || !blur || !r || !g || !b || !a) {
      parent_->UnregisterOverlayShadow(ShadowIdForIndex(index));
      next_slot_card_ids.push_back(0);
      ++index;
      continue;
    }
    const gfx::Rect rect_in_screen(
        origin.x() + static_cast<int>(std::lround(*left)),
        origin.y() + static_cast<int>(std::lround(*top)),
        static_cast<int>(std::lround(*width)),
        static_cast<int>(std::lround(*height)));
    AtriumOverlaySurfaceShadow shadow;
    shadow.corner_radius = static_cast<float>(*radius);
    shadow.blur = static_cast<float>(*blur);
    shadow.offset = gfx::Vector2d(std::lround(*offset_x),
                                  std::lround(*offset_y));
    shadow.color = SkColorSetARGB(ClampToByte(*a * 255), ClampToByte(*r),
                                  ClampToByte(*g), ClampToByte(*b));
    // The stack prepends, so a slot changes hands. A settled newcomer sends
    // nothing that clears the away shift the last card left there, so without
    // this it inherits that fade-out and never comes back.
    const int card_id = card.FindInt("id").value_or(0);
    next_slot_card_ids.push_back(card_id);
    if (index >= slot_card_ids_.size() || slot_card_ids_[index] != card_id) {
      parent_->UnregisterOverlayShadow(ShadowIdForIndex(index));
    }
    // Ahead of the register, not after it: the away state is what Register
    // reads to build the layer already invisible. Set afterwards it would
    // paint one solid frame first, which is the whole complaint.
    const bool entering = card.FindBool("entering").value_or(false);
    if (entering) {
      parent_->SetOverlayShadowAway(ShadowIdForIndex(index), true,
                                    gfx::Vector2d(), base::TimeDelta());
    }
    parent_->RegisterOverlayShadow(ShadowIdForIndex(index), rect_in_screen,
                                   shadow);
    // Sent once, on the push that ends the card's entry keyframe. A later push
    // re-registers the same ring and must leave that fade running, so it sends
    // zero rather than starting the fade again from nothing.
    const int enter_ms =
        static_cast<int>(std::lround(card.FindDouble("enterMs").value_or(0)));
    if (enter_ms > 0) {
      parent_->SetOverlayShadowAway(ShadowIdForIndex(index), false,
                                    gfx::Vector2d(),
                                    base::Milliseconds(enter_ms));
    }
    registered_shadow_rects_.push_back(rect_in_screen);
    // A dismissed card keeps its box for the whole fade, so its shadow has to
    // fade too or it stands alone over a card nobody can see.
    if (card.FindBool("fading").value_or(false)) {
      parent_->SetOverlayShadowAway(
          ShadowIdForIndex(index), true, gfx::Vector2d(),
          base::Milliseconds(
              std::lround(card.FindDouble("fadeMs").value_or(0))));
    }
    ++index;
  }
  // Only the slots no card claimed. Releasing a slot that is registered again
  // in the same push destroys its layer, and with it any fade in flight -
  // atrium_setShadow's guard against cutting an animation short never sees it.
  for (size_t i = index; i < kToastShadowSlots; ++i) {
    parent_->UnregisterOverlayShadow(ShadowIdForIndex(i));
  }
  slot_card_ids_ = std::move(next_slot_card_ids);
}

}  // namespace atrium
