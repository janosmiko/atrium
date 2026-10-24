// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_popup_host.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_POPUP_HOST_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_POPUP_HOST_H_

// Implemented in a .mm file (not .cc), matching every other atrium/ class
// that is only ever compiled into the atrium mac-only BUILD.gn block - this
// class has no NSView/ObjC members itself, but it is owned by
// BrowserController (whose header already carries NSView* members that trip
// the chromium-rawptr clang plugin from a plain C++ TU - see
// atrium_browser_controller.cc's GetTabModalDialogHostOverride comment), so
// staying .mm keeps it in the same TU-exemption bucket as its owner.

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/atrium/extension_popup_watchdog_policy.h"
#include "ui/views/widget/widget_observer.h"

class Browser;

namespace extensions {
class ExtensionHost;
class ExtensionViewHost;
}  // namespace extensions

namespace views {
class View;
class Widget;
}  // namespace views

namespace atrium {

// owns at most one open extension popup for a BrowserController.
// The popup is a real macOS window (ExtensionPopupWindow, in the .cc) ported
// from $SRC's ExtensionPopup, minus click-outside and tab-switch close - a
// real window has no anchor-tree activation signal, and Atrium's own
// window/tab lifecycle (this class's Close/CloseNow call sites) covers those
// cases already. This class adds no-stacking, those close call sites, and
// the 10s hang watchdog for a popup that never paints (design spec section
// 9) - a visible popup is dismissed through its own affordances (red light,
// Escape, window.close()) with no placeholder chip added on top in v1.
//
// One instance per BrowserController (see its extension_popup_host_
// member), built once alongside the extension bridge, for the controller's
// lifetime. Never observes anything but the single popup widget it currently
// owns - the watchdog timer and the decision policy are the only state that
// persists between Open() calls.
class ExtensionPopupHost : public views::WidgetObserver {
 public:
  // `browser` outlives this object (owned by the same BrowserController that
  // owns this host, exactly as `browser_` in ExtensionPopup itself - see its
  // header). Never null.
  explicit ExtensionPopupHost(Browser* browser);
  ~ExtensionPopupHost() override;

  ExtensionPopupHost(const ExtensionPopupHost&) = delete;
  ExtensionPopupHost& operator=(const ExtensionPopupHost&) = delete;

  // Opens a popup for `host`, its window framed at `anchor`'s bottom-left.
  // Closes any popup this host currently owns FIRST (CloseNow - synchronous,
  // so the new popup never stacks on a stale one). Arms the 10s hang
  // watchdog - OnPopupShown below disarms it on a genuine show.
  void Open(std::unique_ptr<extensions::ExtensionViewHost> host,
            views::View* anchor);

  // Asks the open popup to close, if any - the normal dismissal path
  // (explicit Close() call sites: space/window swap, quick menu opening).
  // Routes through views::Widget::Close(), which defers to the popup's own
  // CloseDeferredIfNecessary handling (e.g. the Mac nested-run-loop guard) -
  // NOT immediate, so callers that need the popup gone before doing
  // something else (opening a replacement, tearing down the owning
  // Browser) must use CloseNow() instead.
  void Close();

  // Immediately destroys the open popup widget, if any - for teardown
  // (BrowserController::OnTabStripModelDestroyed, BEFORE browser_ is nulled
  // - the popup holds a raw_ptr<Browser>) and for Open()'s own
  // close-existing-first step. Also the watchdog's own reclaim action on a
  // hung popup.
  void CloseNow();

  bool IsShowing() const { return widget_ != nullptr; }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // The popup window's shown callback - `popup_host` is the popup's
  // ExtensionHost on a genuine show, null if it closed before ever showing.
  void OnPopupShown(extensions::ExtensionHost* popup_host);

  // vim hints inside the popup, behind the settings toggle.
  void InjectVimIfEnabled(extensions::ExtensionHost* popup_host);

  // The watchdog timer's callback: the popup never showed within the
  // timeout window.
  void OnWatchdogFired();

  const raw_ptr<Browser> browser_;

  // The currently-open popup's widget, or null. Not owned - the popup window
  // owns itself (NATIVE_WIDGET_OWNS_WIDGET), so this is only an observation
  // handle, nulled by OnWidgetDestroying whenever the widget goes away for
  // any reason, not just this class's own Close/CloseNow.
  raw_ptr<views::Widget> widget_ = nullptr;

  ExtensionPopupWatchdogPolicy watchdog_policy_;
  base::OneShotTimer watchdog_timer_;

  base::WeakPtrFactory<ExtensionPopupHost> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_POPUP_HOST_H_
