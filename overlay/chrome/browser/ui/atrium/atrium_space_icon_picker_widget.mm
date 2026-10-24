// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_space_icon_picker_widget.mm
#include "chrome/browser/ui/atrium/atrium_space_icon_picker_widget.h"

#include <algorithm>
#include <climits>

#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_child_widget_focus.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace atrium {

namespace {

// The grid only ever sends what it rendered: empty (trash), "ph:<name>"
// (kebab-case), or a short emoji glyph. Anything else did not come from it.
bool IsPlausibleSpaceIconValue(const std::string& value) {
  constexpr size_t kMaxLen = 32;
  if (value.empty()) {
    return true;
  }
  if (value.size() > kMaxLen) {
    return false;
  }
  if (value.starts_with("ph:")) {
    return value.size() > 3 &&
           std::all_of(value.begin() + 3, value.end(), [](char c) {
             return base::IsAsciiLower(c) || base::IsAsciiDigit(c) ||
                    c == '-';
           });
  }
  return !base::IsStringASCII(value);
}

// The live sidebar position of the space with `space_id`, or nullopt when it
// no longer exists - a reorder moves it, so the position the picker opened on
// can be stale by the time a pick lands.
std::optional<int> ResolveSpaceIndex(const std::string& space_id) {
  if (space_id.empty()) {
    return std::nullopt;
  }
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return std::nullopt;
  }
  const std::string slot_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(slot_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindInt("index");
}

}  // namespace

AtriumSpaceIconPickerWidget::AtriumSpaceIconPickerWidget(
    AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "spaceicon",
                             "AtriumSpaceIconPicker",
                             /*activatable=*/true),
      parent_(parent) {
  widget_->AddObserver(this);
  parent_->widget()->AddObserver(this);
}

AtriumSpaceIconPickerWidget::~AtriumSpaceIconPickerWidget() {
  if (web_contents_) {
    web_contents_->SetDelegate(nullptr);
  }
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

bool AtriumSpaceIconPickerWidget::RetriesOnMissingChromeContext() const {
  return false;
}

void AtriumSpaceIconPickerWidget::BeforeAttachToContents() {
  web_contents_->SetDelegate(this);
}

void AtriumSpaceIconPickerWidget::OnRenderWidgetHostViewCreated(
    content::RenderWidgetHostView* rwhv) {
  rwhv->EnableAutoResize(gfx::Size(1, 1), gfx::Size(INT_MAX, INT_MAX));
}

void AtriumSpaceIconPickerWidget::ApplyBounds() {
  const gfx::Rect window = parent_->widget()->GetWindowBoundsInScreen();
  if (window.IsEmpty()) {
    return;
  }
  const gfx::Rect in_window =
      PlaceSpaceIconPicker(anchor_in_window_, window.size(), content_size_);
  last_placement_inside_ = gfx::Rect(window.size()).Contains(in_window);
  last_placement_full_size_ =
      in_window.width() == kSpaceIconPickerFallbackSize.width();
  widget_->SetBounds(in_window + window.OffsetFromOrigin());
}

void AtriumSpaceIconPickerWidget::Show(int space_index,
                                       const std::string& value,
                                       const gfx::Rect& anchor_in_window) {
  std::string space_id;
  if (char* raw = atrium_core_space_id_at(space_index)) {
    space_id.assign(raw);
    atrium_core_string_free(raw);
  }
  if (space_id.empty()) {
    VLOG(1) << "Atrium: space icon picker not opened, no space at index "
            << space_index;
    return;
  }
  space_index_ = space_index;
  space_id_ = std::move(space_id);
  pending_value_ = value;
  anchor_in_window_ = anchor_in_window;
  BuildContents();
  if (web_contents_) {
    parent_->PushOverlayTheme(web_contents_.get());
  }
  ReplayOpenValue();
  ApplyBounds();
  intended_visible_ = true;
  VLOG(1) << "Atrium: space icon picker showing at "
          << anchor_in_window_.ToString()
          << " inside=" << last_placement_inside_
          << " full_size=" << last_placement_full_size_;
  parent_->ReleaseStartupHold();
  widget_->Show();
  widget_->Activate();
  FocusPage();
}

void AtriumSpaceIconPickerWidget::ReplayOpenValue() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  const std::u16string script =
      u"window.atriumOpenSpaceIconPicker && atriumOpenSpaceIconPicker(" +
      base::NumberToString16(space_index_) + u", " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_value_)) + u")";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

void AtriumSpaceIconPickerWidget::FocusPage() {
  if (!intended_visible_ || !widget_->IsVisible()) {
    return;
  }
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
}

void AtriumSpaceIconPickerWidget::Close(bool return_focus) {
  // Marked BEFORE Hide() so the deactivation Hide() causes is not misread as
  // a click-away and fought with a reentrant Activate.
  intended_visible_ = false;
  // Resolved from the id: a reorder while the picker sat open leaves
  // space_index_ pointing at another space's dot.
  std::optional<int> closed_index;
  if (return_focus) {
    closed_index = ResolveSpaceIndex(space_id_);
  }
  anchor_in_window_ = gfx::Rect();
  space_index_ = -1;
  space_id_.clear();
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: space icon picker closing";
  ReleaseChildWidgetFocus(widget_.get());
  widget_->Hide();
  if (!return_focus) {
    return;
  }
  if (content::WebContents* contents = parent_->sidebar_contents()) {
    contents->Focus();
  }
  parent_->widget()->Activate();
  if (closed_index) {
    const std::u16string script =
        u"window.atriumFocusSpaceDot && atriumFocusSpaceDot(" +
        base::NumberToString16(*closed_index) + u")";
    if (content::WebContents* sidebar = parent_->sidebar_contents()) {
      sidebar->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                         base::NullCallback());
    }
  }
}

bool AtriumSpaceIconPickerWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

void AtriumSpaceIconPickerWidget::OnSurfaceMessage(
    std::string_view name,
    const base::ListValue& args) {
  if (name == "spaceIconPickerReady") {
    renderer_ready_ = true;
    ReplayOpenValue();
    return;
  }
  if (name == "sidebarSpaceIconSet") {
    if (!widget_->IsVisible() || args.size() < 2 || !args[0].is_int() ||
        args[0].GetInt() != space_index_ || !args[1].is_string() ||
        !IsPlausibleSpaceIconValue(args[1].GetString())) {
      return;
    }
    // The id is resolved BEFORE Close() clears it - a reorder while the
    // picker sat open can move its space away from space_index_.
    const std::optional<int> resolved_index = ResolveSpaceIndex(space_id_);
    // Closed FIRST: the picked value is applied to the sidebar's own space
    // list, which this widget does not own.
    Close(/*return_focus=*/true);
    if (!resolved_index) {
      return;  // The space is gone - nothing to write.
    }
    base::ListValue resolved_args;
    resolved_args.Append(*resolved_index);
    resolved_args.Append(args[1].GetString());
    parent_->OnSurfaceMessage(name, resolved_args);
    return;
  }
  // Unknown names are ignored: a renderer running ahead of this build
  // degrades to a no-op.
}

void AtriumSpaceIconPickerWidget::ResizeDueToAutoResize(
    content::WebContents* web_contents,
    const gfx::Size& new_size) {
  DCHECK_EQ(web_contents_.get(), web_contents);
  if (new_size.IsEmpty()) {
    return;
  }
  content_size_ = new_size;
  if (widget_->IsVisible()) {
    ApplyBounds();
  }
}

bool AtriumSpaceIconPickerWidget::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE) {
    Close(/*return_focus=*/true);
    return true;
  }
  return false;
}

void AtriumSpaceIconPickerWidget::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  if (widget != parent_->widget() || !widget_->IsVisible()) {
    return;
  }
  ApplyBounds();
}

void AtriumSpaceIconPickerWidget::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  if (widget != widget_.get()) {
    return;
  }
  if (active) {
    if (intended_visible_) {
      FocusPage();
    }
    return;
  }
  if (!intended_visible_) {
    return;
  }
  last_closed_at_ = base::TimeTicks::Now();
  // A deactivation is not the user's own dismissal gesture - leave focus
  // wherever it landed rather than yanking it back to the sidebar.
  Close(/*return_focus=*/false);
}

}  // namespace atrium
