// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_widget_mac.mm
#include "chrome/browser/ui/atrium/atrium_native_widget_mac.h"

#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#import "chrome/browser/ui/atrium/atrium_command_dispatcher_delegate.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_pinch_gesture_monitor.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#import "chrome/browser/ui/cocoa/browser_window_command_handler.h"
#include "components/input/native_web_keyboard_event.h"
#import "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"
#include "components/remote_cocoa/common/native_widget_ns_window_host.mojom.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/accelerators/global_accelerator_listener/global_accelerator_listener.h"
#include "ui/views/widget/native_widget_mac.h"
#include "ui/views/widget/widget.h"

namespace atrium {
namespace {

using atrium::kTitlebarHeight;

class AtriumNativeWidgetMac : public views::NativeWidgetMac {
 public:
  AtriumNativeWidgetMac(Browser* browser,
                        views::Widget* widget,
                        AtriumBrowserWindow* window)
      : NativeWidgetMac(widget), browser_(browser), window_(window) {}

  ~AtriumNativeWidgetMac() override { [pinch_monitor_ invalidate]; }

  // The NSWindow closes after the browser window and its Browser are gone, so
  // this drops every pointer to them here. A dangling raw_ptr traps on quit.
  void ClientDestroyedWidget() override {
    [pinch_monitor_ invalidate];
    pinch_monitor_ = nil;
    browser_ = nullptr;
    window_ = nullptr;
    NativeWidgetMac::ClientDestroyedWidget();
  }

 private:
  // resolves which Browser a command must act on. In the
  // single-window presenter model this window (browser_, profile A) can show a
  // FOREIGN profile's WebContents in-place, so browser_'s own active tab is
  // OFF screen. Tab- and window-scoped commands must target the VISIBLE tab's
  // browser or they act on the wrong (off-screen) tab - e.g. Cmd+W would close
  // A's hidden tab instead of the tab the user sees. App-scoped commands
  // (IDC_EXIT, IDC_NEW_WINDOW, IDC_OPTIONS) intentionally stay on browser_ -
  // IDC_NEW_WINDOW resolves the visible PROFILE inside CreateWindowSlot
  // instead, which is a different question from which Browser the
  // command runs on.
  // Falls back to browser_ when no visible browser resolves (unknown Browser,
  // no swap in effect, or torn-down visible controller).
  Browser* CommandTarget(int32_t command) const {
    // Which commands belong to the visible tab lives in the Rust core, where
    // the set can be asserted. This function's
    // job is only to resolve that answer against live objects.
    if (!atrium_core_command_acts_on_visible_tab(command)) {
      return browser_.get();
    }
    Browser* visible =
        atrium::BrowserController::VisibleBrowserForBrowser(browser_);
    return visible ? visible : browser_.get();
  }

  // views::NativeWidgetMac:
  void ValidateUserInterfaceItem(
      int32_t command,
      remote_cocoa::mojom::ValidateUserInterfaceItemResult* result) override {
    if (!browser_) {
      result->enable = false;
      return;
    }
    // Enables menu items (and thus their key equivalents) based on browser
    // state, e.g. "Close Tab" while tabs are open. tab/window-scoped
    // commands enable off the VISIBLE tab's browser (CommandTarget) so menu
    // enablement matches the shown tab, not browser_'s off-screen one.
    result->enable = chrome::IsCommandEnabled(CommandTarget(command), command);
  }

  // For ValidateUserInterfaceItem() / ExecuteCommand() to be called the
  // CommandDispatcher must be registered on the NSWindow.
  void OnWindowInitialized() override {
    auto* bridge = GetInProcessNSWindowBridge();
    CHECK(bridge);  // Atrium windows are never hosted in an app shim.
    // browser_->window() is still null here - window_ came
    // through the constructor instead.
    CHECK(window_);
    // Native hotkeys (todo 17 follow-up): the Atrium subclass matches
    // user-bound combos against atrium::NativeHotkeyMap before Chrome's
    // IDC table lookup - see atrium_command_dispatcher_delegate.h.
    bridge->SetCommandDispatcher([[AtriumCommandDispatcherDelegate alloc] init],
                                 [[BrowserWindowCommandHandler alloc] init]);
    // same per-window install site as the command dispatcher
    // above, torn down in the destructor rather than left process-wide.
    pinch_monitor_ = [[AtriumPinchGestureMonitor alloc]
        initWithWindow:bridge->ns_window()
         browserWindow:window_.get()];
  }

  // gathers what the policy needs. Every judgement lives in the
  // Rust core's commands.rs; nothing here decides anything.
  AtriumCommandInterceptInputs InterceptInputs(
      int32_t command,
      bool is_before_first_responder) const {
    AtriumCommandInterceptInputs in = {};
    in.command = command;
    in.is_before_first_responder = is_before_first_responder;
    in.global_accelerators_suspended =
        ui::GlobalAcceleratorListener::GetInstance() &&
        ui::GlobalAcceleratorListener::GetInstance()
            ->IsShortcutHandlingSuspended();

    // Only asked on the early path, which is the only place the answer is
    // used - and it costs a dummy event to ask. IsReservedCommandOrKey
    // ignores that event on macOS, so a default-constructed one is fine.
    if (is_before_first_responder) {
      input::NativeWebKeyboardEvent dummy_event(
          blink::WebInputEvent::Type::kKeyDown, 0, base::TimeTicks());
      in.is_reserved_command =
          browser_->command_controller()->IsReservedCommandOrKey(command,
                                                                 dummy_event);
    }

    in.sidebar_toggle_combo_bound =
        atrium::NativeHotkeyMap::Get()->IsDefaultComboBound(
            atrium::NativeHotkeyAction::kToggleSidebar);
    in.command_bar_combo_bound =
        atrium::NativeHotkeyMap::Get()->IsDefaultComboBound(
            atrium::NativeHotkeyAction::kOpenCommandBar);
    // Cmd+S is claimed only where there is a sidebar to toggle. A
    // little window has none, and claiming the key there would take Save Page
    // away from it and give nothing back.
    if (atrium::BrowserController* controller =
            atrium::BrowserController::ControllerForBrowser(browser_)) {
      in.sidebar_toggle_available = controller->CanToggleSidebar();
    }

    return in;
  }

  bool WillExecuteCommand(int32_t command,
                          WindowOpenDisposition window_open_disposition,
                          bool is_before_first_responder) override {
    if (!browser_) {
      return false;
    }
    const AtriumCommandInterceptInputs in =
        InterceptInputs(command, is_before_first_responder);
    return atrium_core_will_claim_command(&in);
  }

  bool ExecuteCommand(int32_t command,
                      WindowOpenDisposition window_open_disposition,
                      bool is_before_first_responder) override {
    if (!browser_) {
      return true;
    }
    const AtriumCommandInterceptInputs in =
        InterceptInputs(command, is_before_first_responder);
    const bool claimed = atrium_core_will_claim_command(&in);
    // Probe: separates "claimed and dropped here" from "fell
    // through to the stock handler, which then did nothing".
    VLOG(1) << "Atrium: execute command " << command
            << " before_first_responder=" << in.is_before_first_responder
            << " sidebar_toggle_available=" << in.sidebar_toggle_available
            << " claimed=" << claimed;
    if (!claimed) {
      return false;
    }

    // which intercept runs is decided in the Rust core's
    // commands.rs and asserted there. What each one DOES is here, because it
    // needs a Browser and a controller.
    switch (static_cast<AtriumCommandIntercept>(
        atrium_core_command_intercept(&in))) {
      case ATRIUM_COMMAND_INTERCEPT_TOGGLE_SIDEBAR:
        // Shared helper: the raw dispatcher hook runs the SAME implementation,
        // so which level caught the key is unobservable. Normally the hook
        // wins and this only fires by residual paths.
        //
        // Returns what the action returned rather than true. A decline has to
        // reach the caller, or Cmd+S is swallowed on a window that could not
        // toggle anything and Save Page never runs.
        return atrium::ExecuteNativeHotkeyAction(
            atrium::BrowserController::ControllerForBrowser(browser_),
            atrium::NativeHotkeyAction::kToggleSidebar);

      case ATRIUM_COMMAND_INTERCEPT_EXIT:
        // quitting must not depend on there being a tab. The
        // catch-all below hands IDC_EXIT to BrowserCommandController, which
        // returns early when the window has no selected tab - and returns
        // TRUE, so the caller is told the quit happened while Exit() never
        // ran. An Atrium window is routinely tabless. chrome::Exit runs
        // AttemptUserExit, so confirm-to-quit still applies.
        chrome::Exit();
        return true;

      case ATRIUM_COMMAND_INTERCEPT_RESTORE_TAB: {
        // The same wall as IDC_EXIT. Gated on IsCommandEnabled
        // because for THIS command the enablement is the meaningful question
        // and does not depend on the strip at all: it tracks whether the
        // restore service has an entry.
        Browser* target = CommandTarget(command);
        if (target && chrome::IsCommandEnabled(target, command)) {
          chrome::RestoreTab(target);
        }
        return true;
      }

      case ATRIUM_COMMAND_INTERCEPT_CLOSE_WINDOW:
        // Same wall as IDC_EXIT/IDC_RESTORE_TAB. window_, not
        // CommandTarget(command): under a presenter swap the visible tab's
        // Browser owns a DIFFERENT, off-screen window.
        VLOG(1) << "Atrium: close-window intercept reached";
        window_->CloseUserInitiated();
        return true;

      case ATRIUM_COMMAND_INTERCEPT_COMMAND_PALETTE:
        // M4: Cmd+T TOGGLES the palette and creates NO tab. A real tab is
        // created only when the user accepts an entry.
        if (atrium::ExecuteNativeHotkeyAction(
                atrium::BrowserController::ControllerForBrowser(browser_),
                atrium::NativeHotkeyAction::kOpenCommandBar)) {
          return true;
        }
        // The action can decline - no palette in this build. Fall through to
        // the blank tab rather than doing nothing.
        [[fallthrough]];

      case ATRIUM_COMMAND_INTERCEPT_BLANK_TAB:
        // Chrome's own IDC_NEW_TAB handler opens chrome://newtab, which is
        // FATAL for an Atrium window - no BrowserView-backed footer
        // controller. add to the VISIBLE tab's browser so the tab
        // lands in the window the user sees.
        chrome::AddTabAt(CommandTarget(command), GURL("about:blank"), -1,
                         /*foreground=*/true);
        return true;

      case ATRIUM_COMMAND_INTERCEPT_NEW_WINDOW_SLOT:
        // Falling through would reach OpenEmptyWindow, which hands the
        // factory-created Browser NO controller. Open a wired window slot
        // showing the same profile this window is showing.
        atrium::BrowserController::CreateWindowSlot(browser_);
        return true;

      case ATRIUM_COMMAND_INTERCEPT_SWALLOW:
        // The stock handlers do not degrade on a chromeless window, they die:
        // split view NOTREACHEDs with no BrowserView-backed TabCollection,
        // and the find bar dereferences a null FindBarOwner. Swallowed here,
        // in atrium-owned dispatch, rather than gated upstream.
        return true;

      case ATRIUM_COMMAND_INTERCEPT_FIND_OPEN:
        // window_, not CommandTarget(command), for the reason
        // ATRIUM_COMMAND_INTERCEPT_CLOSE_WINDOW gives: under a presenter swap
        // the visible tab's Browser owns a different, off-screen window.
        window_->OpenFindBar();
        return true;

      case ATRIUM_COMMAND_INTERCEPT_FIND_NEXT:
        window_->FindNextMatch(/*forward=*/true);
        return true;

      case ATRIUM_COMMAND_INTERCEPT_FIND_PREVIOUS:
        window_->FindNextMatch(/*forward=*/false);
        return true;

      case ATRIUM_COMMAND_INTERCEPT_NONE:
        break;
    }

    // Cmd+W and File > Close Tab. A page's window.close(), an extension's
    // tabs.remove and a window close never reach this, so they never park.
    if (command == IDC_CLOSE_TAB) {
      Browser* target = CommandTarget(command);
      atrium::BrowserController* controller =
          atrium::BrowserController::ControllerForBrowser(target);
      if (controller && controller->ParkFavoriteTab(
                            target->tab_strip_model()->active_index())) {
        return true;
      }
    }

    // tab/window-scoped commands dispatch on the VISIBLE tab's
    // browser (CommandTarget); app-scoped commands stay on browser_.
    chrome::ExecuteCommandWithDisposition(CommandTarget(command), command,
                                          window_open_disposition);
    return true;
  }

  // views::NativeWidget:
  views::internal::NativeWidgetPrivate* AsNativeWidgetPrivate() override {
    return this;
  }

  void PopulateCreateWindowParams(
      const views::Widget::InitParams& widget_params,
      remote_cocoa::mojom::CreateWindowParams* params) override {
    // Chromeless Arc-style window, mirroring the reference: the content view
    // fills the whole frame (the sidebar reaches the window's very top), the
    // titlebar is transparent with no title text, and only the traffic
    // lights remain, floating over the sidebar's top-left.
    params->style_mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                         NSWindowStyleMaskMiniaturizable |
                         NSWindowStyleMaskResizable |
                         NSWindowStyleMaskFullSizeContentView;
    params->window_class = remote_cocoa::mojom::WindowClass::kBrowser;
    params->titlebar_appears_transparent = true;
    params->window_title_hidden = true;
    params->animation_enabled = true;
    // the liquid-glass window is non-opaque (clear background,
    // InstallLiquidGlassBackdrop), so AppKit no longer corner-masks what we
    // draw - the glass + tint surface rendered square corners against the
    // frame's rounded shadow.
    //
    // The silhouette is rounded by UpdateWindowCornerMask at
    // AtriumWindowCornerRadius(), NOT by this flag. A measurement showed
    // BrowserWindowFrame's 20 as too small (50px against Safari and Finder's
    // 63px at 2x; the toolbar-class 26 is the match), and the flag only
    // reaches the out-of-process CreateWindow path - this window takes
    // CreateInProcessNSWindowBridge, which never assigns
    // rounds_content_corners_to_window_. Left set as a harmless declaration of
    // intent; see atrium_window_corner_radius.h for the radius that applies.
    params->rounds_content_corners_to_window = true;
  }

  // Consumed by BrowserWindowFrame::_titlebarHeight (window_class kBrowser,
  // browser_native_widget_window_mac.mm): sizes the invisible titlebar band
  // and thereby vertically centers the floating traffic lights within it
  // (_shouldCenterTrafficLights returns YES there).
  void GetWindowFrameTitlebarHeight(bool* override_titlebar_height,
                                    float* titlebar_height) override {
    *override_titlebar_height = true;
    *titlebar_height = kTitlebarHeight;
  }

  raw_ptr<Browser> browser_;
  raw_ptr<AtriumBrowserWindow> window_;
  AtriumPinchGestureMonitor* __strong pinch_monitor_ = nil;
};

}  // namespace

views::NativeWidget* CreateAtriumNativeWidget(Browser* browser,
                                              views::Widget* widget,
                                              AtriumBrowserWindow* window) {
  return new AtriumNativeWidgetMac(browser, widget, window);
}

}  // namespace atrium
