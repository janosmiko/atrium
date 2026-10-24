// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_popup_host.cc
#include "chrome/browser/ui/atrium/atrium_extension_popup_host.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_vim_script.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/extensions/extension_view_views.h"
#include "chrome/browser/ui/views/extensions/security_dialog_tracker.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/extension.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/hit_test.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/border.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/client_view.h"

#if BUILDFLAG(IS_MAC)
#include "base/message_loop/message_pump_apple.h"
#endif

namespace atrium {

namespace {
// Design spec section 9: a popup that never paints must never block the
// window. 10s is generously above any legitimate extension popup's
// load-to-first-paint time (a genuinely slow popup still shows well under
// this) while being short enough that a hung one doesn't strand its anchor
// surface in a "something is open" state for long.
constexpr base::TimeDelta kWatchdogTimeout = base::Seconds(10);

// Ported from $SRC's ExtensionPopup::kMinSize/kMaxSize.
constexpr gfx::Size kMinSize(25, 25);
constexpr gfx::Size kMaxSize(800, 600);

class ExtensionPopupWindow;

// Ported from AtriumSettingsWindow's SettingsClientView: HTCAPTION above the
// titlebar drags the window like a real title bar.
class PopupClientView : public views::ClientView {
 public:
  PopupClientView(views::Widget* widget,
                  views::View* contents_view,
                  ExtensionPopupWindow* popup)
      : ClientView(widget, contents_view), popup_(popup) {}

  int NonClientHitTest(const gfx::Point& point) override;

 private:
  raw_ptr<ExtensionPopupWindow> popup_;
};

// A real macOS window for one extension popup page - ported from $SRC's
// ExtensionPopup, minus click-outside, tab-switch close, and DevTools
// observation (see the host header's doc). `this` is both the WidgetDelegate
// and the contents view, reimplementing views::WidgetDelegateView's own
// pairing since that type's constructor is friend-gated to a fixed
// allowlist - self-deletes on native window close like that type would.
class ExtensionPopupWindow : public views::WidgetDelegate,
                             public views::View,
                             public ExtensionViewViews::Container,
                             public extensions::ExtensionRegistryObserver,
                             public content::WebContentsObserver {
 public:
  ExtensionPopupWindow(
      Browser* browser,
      std::unique_ptr<extensions::ExtensionViewHost> host,
      base::OnceCallback<void(extensions::ExtensionHost*)> shown_callback)
      : browser_(browser),
        host_(std::move(host)),
        shown_callback_(std::move(shown_callback)) {}

  ExtensionPopupWindow(const ExtensionPopupWindow&) = delete;
  ExtensionPopupWindow& operator=(const ExtensionPopupWindow&) = delete;

  ~ExtensionPopupWindow() override {
    if (shown_callback_) {
      std::move(shown_callback_).Run(nullptr);
    }
  }

  int titlebar_height() const { return titlebar_height_; }

  // Builds the widget, framed at `anchor`'s bottom-left and parented to
  // `browser_widget`. Not shown yet - deferred until the page loads, same as
  // $SRC's ExtensionPopup.
  views::Widget* Init(views::View* anchor, views::Widget* browser_widget) {
    Profile* profile = Profile::FromBrowserContext(host_->browser_context());
    extension_view_ = AddChildView(
        std::make_unique<ExtensionViewViews>(profile, host_.get()));
    extension_view_->SetContainer(this);
    extension_view_->Init();
    SetLayoutManager(std::make_unique<views::FillLayout>());

    SetCanActivate(true);
    SetCanResize(false);
    SetInitiallyFocusedView(extension_view_);
    SetClientViewFactory(base::BindOnce(
        [](ExtensionPopupWindow* popup, views::Widget* widget,
          views::View* contents_view) -> std::unique_ptr<views::ClientView> {
          return std::make_unique<PopupClientView>(widget, contents_view,
                                                    popup);
        },
        this));

    auto* widget = new views::Widget();
    views::Widget::InitParams params(
        views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW);
    params.name = "AtriumExtensionPopup";
    params.parent = browser_widget->GetNativeView();
    // Taller than kMinSize: the native title bar still takes its height here,
    // and a zero-height content rect fails a DCHECK on Mac.
    const gfx::Point origin = InitialOrigin(anchor, browser_widget);
    params.bounds =
        gfx::Rect(origin, gfx::Size(kMinSize.width(), kMaxSize.height()));
    params.delegate = this;
    widget->Init(std::move(params));
    widget_ = widget;

    // The real height only exists once the NSWindow does.
    titlebar_height_ = AtriumBrowserWindow::AddTrafficLights(widget_);
    SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(titlebar_height_, 0, 0, 0)));
    // From `origin`, not the current frame: the style change above moves the
    // frame by the title bar height.
    widget_->SetBounds(gfx::Rect(
        origin,
        gfx::Size(kMinSize.width(), kMinSize.height() + titlebar_height_)));

    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));

    // Handle the containing view calling window.close(). base::Unretained is
    // safe: this object owns `host_`, so the callback outlives it never.
    host_->SetCloseHandler(base::BindOnce(
        &ExtensionPopupWindow::HandleCloseExtensionHost,
        base::Unretained(this)));

    extension_registry_observation_.Observe(
        extensions::ExtensionRegistry::Get(host_->browser_context()));

    // A host that finished loading already would miss the notification
    // below and never show - matches ExtensionPopup's own comment.
    if (host_->has_loaded_once()) {
      ShowWindow();
    } else {
      Observe(host_->host_contents());
    }
    return widget_;
  }

  // ExtensionViewViews::Container:
  gfx::Size GetMinBounds() override { return kMinSize; }
  gfx::Size GetMaxBounds() override {
    gfx::Size max_size = kMaxSize;
    if (widget_) {
      const gfx::Rect work_area = display::Screen::Get()
                                      ->GetDisplayNearestWindow(
                                          widget_->GetNativeWindow())
                                      .work_area();
      max_size.SetToMin(gfx::Size(
          work_area.width(),
          std::max(0, work_area.height() - titlebar_height_)));
    }
    max_size.SetToMax(kMinSize);
    return max_size;
  }

  // views::View:
  void ChildPreferredSizeChanged(views::View* child) override {
    if (child != extension_view_ || !widget_) {
      return;
    }
    gfx::Size page_size = extension_view_->GetPreferredSize();
    page_size.SetToMax(kMinSize);
    page_size.SetToMin(GetMaxBounds());
    gfx::Rect bounds = widget_->GetWindowBoundsInScreen();
    bounds.set_size(
        gfx::Size(page_size.width(), page_size.height() + titlebar_height_));
    // The display only, not the browser window: a user drag may have put the
    // popup outside the browser on purpose.
    bounds.AdjustToFit(
        display::Screen::Get()->GetDisplayMatching(bounds).work_area());
    widget_->SetBounds(bounds);
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    CloseDeferredIfNecessary();
    return true;
  }

  // extensions::ExtensionRegistryObserver:
  void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const extensions::Extension* extension,
      extensions::UnloadedExtensionReason reason) override {
    if (extension->id() != host_->extension_id()) {
      return;
    }
    // The view goes first, same order as $SRC's ExtensionPopup, so it
    // cannot reach into `host_` while it is torn down.
    RemoveChildViewT(extension_view_.ExtractAsDangling());
    extension_registry_observation_.Reset();
    host_.reset();
    CloseDeferredIfNecessary();
  }

  // content::WebContentsObserver:
  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    ShowWindow();
    Observe(nullptr);
  }

  // views::WidgetDelegate:
  views::Widget* GetWidget() override { return View::GetWidget(); }
  const views::Widget* GetWidget() const override {
    return View::GetWidget();
  }
  views::View* GetContentsView() override { return this; }

 private:
  void ShowWindow() {
    // Same guard as $SRC's ExtensionPopup: a popup over a security dialog
    // can spoof it.
    if (extensions::SecurityDialogTracker::GetInstance()
            ->BrowserHasVisibleSecurityDialogs(browser_)) {
      CloseDeferredIfNecessary();
      return;
    }
    widget_->Show();
    host_->host_contents()->Focus();
    if (shown_callback_) {
      std::move(shown_callback_).Run(host_.get());
    }
  }

  void CloseDeferredIfNecessary() {
#if BUILDFLAG(IS_MAC)
    // Defer close if a nested run loop is running (e.g. a context menu), to
    // avoid messaging deallocated objects.
    if (base::message_pump_apple::IsHandlingSendEvent()) {
      deferred_close_weak_ptr_factory_.InvalidateWeakPtrs();
      auto weak_ptr = deferred_close_weak_ptr_factory_.GetWeakPtr();
      CFRunLoopPerformBlock(CFRunLoopGetCurrent(), kCFRunLoopDefaultMode, ^{
        if (weak_ptr) {
          weak_ptr->widget_->CloseWithReason(
              views::Widget::ClosedReason::kUnspecified);
        }
      });
      return;
    }
#endif  // BUILDFLAG(IS_MAC)
    widget_->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
  }

  void HandleCloseExtensionHost(extensions::ExtensionHost* host) {
    CloseDeferredIfNecessary();
  }

  // Only on open. After that AppKit moves the child window with the browser,
  // and a user drag may take it outside.
  static gfx::Rect ClampToWindowAndDisplay(gfx::Rect placement,
                                           views::Widget* browser_widget) {
    placement.AdjustToFit(browser_widget->GetWindowBoundsInScreen());
    placement.AdjustToFit(
        display::Screen::Get()->GetDisplayMatching(placement).work_area());
    return placement;
  }

  static gfx::Point InitialOrigin(views::View* anchor,
                                  views::Widget* browser_widget) {
    gfx::Rect placement(anchor->GetBoundsInScreen().bottom_left(), kMinSize);
    return ClampToWindowAndDisplay(placement, browser_widget).origin();
  }

  raw_ptr<Browser> browser_;
  std::unique_ptr<extensions::ExtensionViewHost> host_;
  raw_ptr<ExtensionViewViews> extension_view_ = nullptr;
  raw_ptr<views::Widget> widget_ = nullptr;
  int titlebar_height_ = 0;
  base::OnceCallback<void(extensions::ExtensionHost*)> shown_callback_;

  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      extension_registry_observation_{this};

#if BUILDFLAG(IS_MAC)
  base::WeakPtrFactory<ExtensionPopupWindow> deferred_close_weak_ptr_factory_{
      this};
#endif
};

int PopupClientView::NonClientHitTest(const gfx::Point& point) {
  if (popup_ && point.y() < popup_->titlebar_height()) {
    return HTCAPTION;
  }
  return ClientView::NonClientHitTest(point);
}

}  // namespace

ExtensionPopupHost::ExtensionPopupHost(Browser* browser)
    : browser_(browser) {}

ExtensionPopupHost::~ExtensionPopupHost() {
  CloseNow();
}

void ExtensionPopupHost::Open(std::unique_ptr<extensions::ExtensionViewHost> host,
                              views::View* anchor) {
  // No stacking: any popup this host still owns is gone, synchronously,
  // before the new one is created.
  CloseNow();

  watchdog_policy_.Arm();
  watchdog_timer_.Start(FROM_HERE, kWatchdogTimeout,
                        base::BindOnce(&ExtensionPopupHost::OnWatchdogFired,
                                       weak_factory_.GetWeakPtr()));

  auto* browser_window = static_cast<AtriumBrowserWindow*>(browser_->window());
  auto* popup = new ExtensionPopupWindow(
      browser_, std::move(host), base::BindOnce(&ExtensionPopupHost::OnPopupShown,
                                      weak_factory_.GetWeakPtr()));
  widget_ = popup->Init(anchor, browser_window->widget());
  widget_->AddObserver(this);
}

// The boundary in docs/vim-mode-design.md: this popup's main frame,
// the isolated world, and only while the toggle is on.
void ExtensionPopupHost::InjectVimIfEnabled(
    extensions::ExtensionHost* popup_host) {
  if (!AtriumBrowserWindow::VimModeEnabled()) {
    return;
  }
  content::WebContents* contents = popup_host->host_contents();
  if (!contents || !contents->GetPrimaryMainFrame() ||
      !contents->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    return;
  }
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      BuildVimPopupInstallScript(), base::NullCallback(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void ExtensionPopupHost::Close() {
  if (widget_) {
    widget_->Close();
  }
}

void ExtensionPopupHost::CloseNow() {
  watchdog_timer_.Stop();
  watchdog_policy_.Reset();
  if (!widget_) {
    return;
  }
  views::Widget* widget = widget_;
  // Cleared and unobserved BEFORE CloseNow() below: this call is already the
  // full teardown, so the OnWidgetDestroying notification the CloseNow()
  // triggers would just be redundant re-entry into this same method.
  widget_ = nullptr;
  widget->RemoveObserver(this);
  widget->CloseNow();
}

void ExtensionPopupHost::OnWidgetDestroying(views::Widget* widget) {
  if (widget_ != widget) {
    return;
  }
  widget_ = nullptr;
  watchdog_timer_.Stop();
  watchdog_policy_.Reset();
}

void ExtensionPopupHost::OnPopupShown(extensions::ExtensionHost* popup_host) {
  if (popup_host) {
    if (watchdog_policy_.OnShown()) {
      watchdog_timer_.Stop();
    }
    InjectVimIfEnabled(popup_host);
    return;
  }
  // The popup closed itself before ever showing (e.g. the extension
  // unloaded mid-load) - nothing left to watch for.
  watchdog_policy_.OnShowFailed();
  watchdog_timer_.Stop();
}

void ExtensionPopupHost::OnWatchdogFired() {
  if (!watchdog_policy_.OnTimerFired()) {
    return;
  }
  VLOG(1) << "Atrium: extension popup watchdog reclaimed a hung/never-shown "
             "popup after "
          << kWatchdogTimeout.InSeconds() << "s";
  CloseNow();
}

}  // namespace atrium
