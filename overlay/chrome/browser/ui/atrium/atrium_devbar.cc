// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devbar.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/string_escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_devbar_layout.h"
#include "chrome/browser/ui/atrium/atrium_overlay_shadow_layer.h"
#include "chrome/browser/ui/atrium/atrium_rounded_hit_test_view.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

namespace atrium {

namespace {

// WebView's re-attach guard compares the holder's view against the page's own
// NSView, never the wrapper AtriumRoundedHitTestView puts in between, so it
// would attach the raw rectangle back over the wrapper without detaching.
class DevBarWebView : public views::WebView {
  METADATA_HEADER(DevBarWebView, views::WebView)

 public:
  using views::WebView::WebView;

  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override {
    if (details.is_add && web_contents() && holder()->native_view() &&
        holder()->native_view() != web_contents()->GetNativeView()) {
      return;
    }
    views::WebView::ViewHierarchyChanged(details);
  }
};

BEGIN_METADATA(DevBarWebView)
END_METADATA

// Bar 0 keeps the id it has always had, so its shadow tests still name it.
std::string ShadowIdFor(size_t index) {
  return index == 0 ? std::string("devbar")
                    : "devbar-" + base::NumberToString(index);
}

}  // namespace

AtriumDevBar::AtriumDevBar(AtriumBrowserWindow* window,
                           views::View* container,
                           size_t index)
    : window_(window),
      container_(container),
      index_(index),
      shadow_id_(ShadowIdFor(index)) {}

AtriumDevBar::~AtriumDevBar() = default;

void AtriumDevBar::Build() {
  AtriumChromeContext::PostWhenReady(base::BindOnce(
      [](base::WeakPtr<AtriumDevBar> self) {
        AtriumChromeContext* context = AtriumChromeContext::GetIfExists();
        if (!self || !self->container_ || !context ||
            !self->container_->children().empty()) {
          return;
        }
        // every bar shares this URL, so a probe naming one by
        // path alone (cdp-eval.mjs) needs the fragment to pick a pane's own.
        std::unique_ptr<content::WebContents> contents =
            context->CreateChromeContents(GURL(
                std::string(chrome::kChromeUIAtriumURL) + "devbar" +
                (self->index_ > 0
                     ? "#" + base::NumberToString(self->index_)
                     : "")));
        if (!contents) {
          return;
        }
        auto web_view =
            std::make_unique<DevBarWebView>(contents->GetBrowserContext());
        web_view->SetWebContents(contents.get());
        // The page rounds its own corners, so the card shows through. Both
        // colours, same as the sidebar: the widget's own paints a square.
        if (content::RenderWidgetHostView* rwhv =
                contents->GetRenderWidgetHostView()) {
          rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
        }
        contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
        self->contents_ = std::move(contents);
        self->AttachToContents(self->contents_.get());
        // The theme fan-out only runs on a tint CHANGE, so a bar built after
        // the last one would open on the defaults.
        self->window_->PushOverlayTheme(self->contents_.get());
        views::WebView* added =
            self->container_->AddChildView(std::move(web_view));
        self->web_view_ = added;
        // A little window builds its bar before the widget exists, and the
        // WebView re-attaches once the tree joins one.
        self->attached_subscription_ =
            added->AddWebContentsAttachedCallback(base::BindRepeating(
                &AtriumDevBar::Wrap, self->weak_factory_.GetWeakPtr()));
        self->Wrap(added);
        self->container_->InvalidateLayout();
      },
      weak_factory_.GetWeakPtr()));
}

void AtriumDevBar::Wrap(views::WebView* web_view) {
  // NativeViewHost::Attach dereferences a wrapper that only exists once the
  // host is in a Widget, so a bar built before the window has one waits.
  if (!contents_ || !web_view->GetWidget()) {
    return;
  }
  views::NativeViewHost* holder = web_view->holder();
  if (hit_test_view_ &&
      holder->native_view() == hit_test_view_->native_view()) {
    return;
  }
  // ViewsHostableDetach pulls the page's NSView out of whatever superview it
  // is in, the wrapper included, so detach before wrapping.
  if (holder->native_view()) {
    holder->Detach();
  }
  if (!hit_test_view_) {
    hit_test_view_ =
        std::make_unique<AtriumRoundedHitTestView>(contents_->GetNativeView());
    if (const std::optional<AtriumOverlaySurfaceShadow>& spec =
            window_->devbar_shadow_spec_) {
      hit_test_view_->SetCornerRadius(spec->corner_radius);
    }
  }
  holder->Attach(hit_test_view_->native_view());
  // Attach parents the page to the WebView, whose GetNativeViewAccessible is
  // that same page, and the self-parent recurses until the stack guard kills
  // the process. WebView repairs its own attach, this one is ours to repair.
  if (views::View* parent = web_view->parent()) {
    holder->SetParentAccessible(parent->GetNativeViewAccessible());
  }
}

void AtriumDevBar::Unwrap() {
  Observe(nullptr);
  attached_subscription_ = {};
  if (hit_test_view_) {
    // Detach only. By now the widget is about to lose its NSWindow host, so
    // re-attaching the page's raw view would CHECK inside
    // NativeViewHostMac::AttachNativeView.
    if (web_view_ &&
        web_view_->holder()->native_view() == hit_test_view_->native_view()) {
      web_view_->holder()->Detach();
    }
    hit_test_view_.reset();
  }
  web_view_ = nullptr;
  container_ = nullptr;
}

void AtriumDevBar::SetTab(content::WebContents* tab) {
  if (web_contents() != tab) {
    Observe(tab);
  }
}

void AtriumDevBar::PushShadow(const AtriumOverlaySurfaceShadow* spec,
                              const gfx::Vector2d& away_shift,
                              base::TimeDelta away_duration) {
  if (!container_ || !container_->GetVisible()) {
    entrance_pending_ = false;
    window_->UnregisterOverlayShadow(shadow_id_);
    return;
  }
  if (!spec) {
    // The entrance survives this one: the page has not pushed its shadow yet,
    // and a bar toggled on before its document loads still has to descend.
    window_->UnregisterOverlayShadow(shadow_id_);
    return;
  }
  gfx::Rect bubble = container_->GetLocalBounds();
  bubble.Inset(gfx::Insets::TLBR(kDevBarShadowPadTop, kDevBarShadowPadX,
                                 kDevBarShadowPadBottom, kDevBarShadowPadX));
  if (bubble.IsEmpty()) {
    entrance_pending_ = false;
    window_->UnregisterOverlayShadow(shadow_id_);
    return;
  }
  if (hit_test_view_) {
    hit_test_view_->SetCornerRadius(spec->corner_radius);
  }
  views::View::ConvertRectToScreen(container_, &bubble);
  // The page's own window.screenX reads the app window's origin, so a scenario
  // that clicks a control in this bar needs the real one.
  CallScript(u"window.atriumDevBarScreenOrigin = [" +
             base::NumberToString16(bubble.x()) + u", " +
             base::NumberToString16(bubble.y()) + u"]");
  if (entrance_pending_) {
    // Away before the register, because the register is what builds the layer
    // and a shift set after it lands on a shadow already painted at rest.
    window_->SetOverlayShadowAway(shadow_id_, true, away_shift,
                                  base::TimeDelta());
  }
  window_->RegisterOverlayShadow(shadow_id_, bubble, *spec);
  if (entrance_pending_) {
    entrance_pending_ = false;
    window_->SetOverlayShadowAway(shadow_id_, false, away_shift,
                                  away_duration);
  }
}

void AtriumDevBar::CallScript(const std::u16string& script) {
  if (contents_) {
    contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                        base::NullCallback());
  }
}

void AtriumDevBar::PushFocused(bool focused, const std::string& ring_hex) {
  focused_for_testing_ = focused;
  focus_ring_for_testing_ = ring_hex;
  CallScript(u"window.atriumSetDevBarFocused && atriumSetDevBarFocused(" +
             base::UTF8ToUTF16(std::string(focused ? "true" : "false")) +
             u", " + base::UTF8ToUTF16(base::GetQuotedJSONString(ring_hex)) +
             u")");
}

void AtriumDevBar::OnSurfaceMessage(std::string_view name,
                                    const base::ListValue& args) {
  window_->OnDevBarSurfaceMessage(this, name, args);
}

void AtriumDevBar::DidFinishNavigation(content::NavigationHandle* handle) {
  if (handle->IsInMainFrame() && !handle->IsSameDocument() &&
      handle->HasCommitted()) {
    window_->OnDevBarTabNavigated();
  }
}

}  // namespace atrium
