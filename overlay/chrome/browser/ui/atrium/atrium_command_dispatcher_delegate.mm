// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_dispatcher_delegate.mm
#import "chrome/browser/ui/atrium/atrium_command_dispatcher_delegate.h"

#include <optional>
#include <string>

#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"

#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"

#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_tab_recency_bridge.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_edit_focus_gate.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "ui/gfx/mac/coordinate_conversion.h"

namespace {

// Carbon kVK_Tab - duplicated from atrium_native_hotkey_map.mm's kVKTab
// rather than shared, because this chord is deliberately NOT part of
// NativeHotkeyMap: Ctrl-Tab is fixed, not user-rebindable, this milestone
// (see the retired ABI header's TAB CYCLING doc block).
constexpr int kVKTabForCycling = 0x30;

// +1 for Ctrl-Tab, -1 for Ctrl-Shift-Tab, nullopt for anything else. Modifier
// match is EXACT over the cmd/shift/opt/ctrl mask (same mask
// NativeHotkeyMap::MatchAction uses), so a chord like Cmd+Ctrl-Tab (not part
// of the grammar today, but future-proofing) never mis-fires this reroute.
std::optional<int32_t> RecencyCycleDirectionForEvent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return std::nullopt;
  }
  if (event.keyCode != kVKTabForCycling) {
    return std::nullopt;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers == NSEventModifierFlagControl) {
    return 1;  // Ctrl-Tab: forward.
  }
  if (modifiers == (NSEventModifierFlagControl | NSEventModifierFlagShift)) {
    return -1;  // Ctrl-Shift-Tab: backward.
  }
  return std::nullopt;
}

// True for the Cmd+Q key equivalent (Quit). this used to match the
// raw event itself - a second definition of the chord atrium_surface_key_route.h
// already owns for the palette and the peek card. Delegates to that
// one now instead of carrying its own copy.
bool IsCmdQKeyEquivalent(NSEvent* event) {
  return atrium::IsQuitChord(atrium::SurfaceKeyInputsForEvent(
      event, /*atrium_action_matched=*/false));
}

// True for the Cmd+Shift+C key equivalent (copy the active tab's
// link). Matched on the character ("c", case-insensitive - Shift is folded
// into charactersIgnoringModifiers) plus an EXACT Command+Shift mask, the same
// layout-independent posture as IsCmdQKeyEquivalent, so a wider chord never
// mis-fires. Intercepting here in the raw NSEvent hook - which runs before
// Chrome's IDC accelerator lookup - is what lets the copy beat the stock
// IDC_DEV_TOOLS_INSPECT binding on the same chord.
bool IsCopyLinkKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"c"] ==
         NSOrderedSame;
}

// Opt+Shift+Cmd+C ("Copy URL as Markdown") - same layout-
// independent character match as IsCopyLinkKeyEquivalent, distinct exact
// modifier mask (adds Option) so it never overlaps that chord.
bool IsCopyLinkAsMarkdownKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != (NSEventModifierFlagCommand | NSEventModifierFlagShift |
                    NSEventModifierFlagOption)) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"c"] ==
         NSOrderedSame;
}

// Ctrl+Shift+Cmd+C ("Copy URL as Quote") - same posture, adds
// Control instead of Option so it never overlaps either chord above.
bool IsCopyLinkAsQuoteKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != (NSEventModifierFlagCommand | NSEventModifierFlagShift |
                    NSEventModifierFlagControl)) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"c"] ==
         NSOrderedSame;
}

// the two chords the suite found dead - Cmd+comma (Settings) and
// Cmd+Opt+I (DevTools). Both work on the SwiftUI surface and do nothing on the
// HTML one, so the sidebar's WebContents is taking them; what is NOT known is
// which of this delegate's hooks still sees them, and that decides whether the
// fix looks like Cmd+Shift+C (claim it in prePerform) or like Cmd+Q
// (kPassToMainMenu in postPerform). Matched on charactersIgnoringModifiers
// with an exact modifier mask, the same layout-independent posture as
// IsCmdQKeyEquivalent.
// Cmd+Shift+W ("Close Window"), same layout-independent posture as the copy
// chords. Matched on the raw event because the File menu's item behind it is
// an AppKit performClose: item, which AppKit alone decides to validate.
bool IsCloseWindowKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"w"] ==
         NSOrderedSame;
}

bool IsSettingsKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != NSEventModifierFlagCommand) {
    return false;
  }
  return [[event charactersIgnoringModifiers] isEqualToString:@","];
}

bool IsDevToolsKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != (NSEventModifierFlagCommand | NSEventModifierFlagOption)) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"i"] ==
         NSOrderedSame;
}

bool IsSavePageKeyEquivalent(NSEvent* event) {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return false;
  }
  const NSEventModifierFlags modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  if (modifiers != NSEventModifierFlagCommand) {
    return false;
  }
  return [[event charactersIgnoringModifiers] caseInsensitiveCompare:@"s"] ==
         NSOrderedSame;
}

// every WebContents-backed surface answers to the same responder
// class, so the class cannot name the surface. Match the responder view
// against each live widget's own view and report that widget's page instead.
std::string DescribeResponderWebContents(NSResponder* responder) {
  if (![responder isKindOfClass:[NSView class]]) {
    return "not-a-view";
  }
  NSView* responder_view = (NSView*)responder;
  std::unique_ptr<content::RenderWidgetHostIterator> hosts =
      content::RenderWidgetHost::GetRenderWidgetHosts();
  while (content::RenderWidgetHost* host = hosts->GetNextHost()) {
    content::RenderWidgetHostView* view = host->GetView();
    if (!view || view->GetNativeView().GetNativeNSView() != responder_view) {
      continue;
    }
    content::RenderViewHost* rvh = content::RenderViewHost::From(host);
    content::WebContents* contents =
        rvh ? content::WebContents::FromRenderViewHost(rvh) : nullptr;
    return base::StrCat(
        {"rwh=", base::NumberToString(host->GetRoutingID()), " url=",
         contents ? contents->GetLastCommittedURL().spec() : "<no-contents>"});
  }
  return "rwh=<unmatched> url=<unmatched>";
}

// Probe. Three investigations turned on "did the
// chord arrive", and each was slow because nothing answered it. Silent for
// every other key.
void LogDeadChordIfInteresting(NSEvent* event, const char* hook, bool redispatch) {
  if (IsSettingsKeyEquivalent(event)) {
    VLOG(1) << "Atrium: dead-chord probe cmd+comma at " << hook
            << " redispatch=" << redispatch;
  } else if (IsDevToolsKeyEquivalent(event)) {
    VLOG(1) << "Atrium: dead-chord probe cmd+opt+i at " << hook
            << " redispatch=" << redispatch;
  } else if (IsSavePageKeyEquivalent(event)) {
    // which surface holds focus decides where the chord dies.
    VLOG(1) << "Atrium: dead-chord probe cmd+s at " << hook
            << " redispatch=" << redispatch << " responder="
            << base::SysNSStringToUTF8(NSStringFromClass(
                   [[[event window] firstResponder] class]))
            << " ptr="
            << (__bridge void*)[[event window] firstResponder] << " "
            << DescribeResponderWebContents([[event window] firstResponder]);
  }
}

// `event`'s chord in the shape ComboEditsText classifies, so the raw
// NSEvent hook can ask "would macOS have used this for text editing?" without
// consulting NativeHotkeyMap at all. Deliberately independent of the map: the
// answer is a property of the chord, not of whatever action happens to be bound
// to it, so a user's rebind onto a text-editing chord is gated automatically.
// Layout-independent for the character case, the same posture as
// IsCmdQKeyEquivalent above.
atrium::TextEditingCombo TextEditingComboForEvent(NSEvent* event,
                                                  std::string* character_out) {
  atrium::TextEditingCombo combo;
  if (!event) {
    return combo;
  }
  const NSEventModifierFlags modifiers = event.modifierFlags;
  combo.command = (modifiers & NSEventModifierFlagCommand) != 0;
  combo.shift = (modifiers & NSEventModifierFlagShift) != 0;
  combo.option = (modifiers & NSEventModifierFlagOption) != 0;
  combo.control = (modifiers & NSEventModifierFlagControl) != 0;

  // The arrows are matched by keyCode, exactly as NativeHotkeyMap does for its
  // named keys - charactersIgnoringModifiers returns the private-use function
  // characters for them, which no lowercase-character comparison can express.
  switch (event.keyCode) {
    case atrium::kVKLeftArrow:
    case atrium::kVKRightArrow:
    case atrium::kVKUpArrow:
    case atrium::kVKDownArrow:
      combo.key_code = event.keyCode;
      return combo;
    default:
      break;
  }

  NSString* characters = [event charactersIgnoringModifiers];
  if (characters.length == 1) {
    // `character_out` owns the bytes: TextEditingCombo holds a string_view, so
    // the storage must outlive the ComboEditsText call at the caller.
    *character_out = base::SysNSStringToUTF8([characters lowercaseString]);
    combo.character = *character_out;
  }
  return combo;
}

// whether a hotkey matched on THIS event may fire, or whether the key
// must be left to reach the focused text surface. The raw-NSEvent route's half
// of the gate - see the retired ABI header's EDIT FOCUS doc block. Reads
// `controller`'s kind SYNCHRONOUSLY: a key event cannot await a round trip, and
// on this side it never has to, because C++ owns the page half natively and
// reads the strip's active tab in this same turn.
bool MayFireHotkeyForEvent(NSEvent* event,
                           atrium::BrowserController* controller) {
  std::string character_storage;
  atrium::EditFocusGateInputs inputs;
  inputs.route = atrium::HotkeyRoute::kNativeKeyEquivalent;
  inputs.combo_edits_text = atrium::ComboEditsText(
      TextEditingComboForEvent(event, &character_storage));
  // this used to AND in edit_focus_connected(), because the
  // signal was pushed over the ABI. It never was on this route -
  // edit_focus_kind() reads the command bar's visibility and the active
  // tab's IsFocusedElementEditable(), both of which C++ owns. Turning the
  // gate off with the dylib would have disabled every text-editing chord's
  // hotkey permanently, which is the opposite of fail-safe.
  //
  // One input did come from Swift and is now always false:
  // atrium_text_focused_, the sidebar's own fields.
  inputs.signal_connected = controller != nullptr;
  if (controller) {
    inputs.kind = controller->edit_focus_kind();
    // Both ids come from the same synchronous read, so this route cannot see a
    // stale snapshot - the equality is structural, not a coincidence worth
    // relying on silently, which is why it is written out rather than skipped.
    inputs.snapshot_tab_id = controller->edit_focus_tab_id();
    inputs.active_tab_id = inputs.snapshot_tab_id;
  }
  const atrium::EditFocusGateDecision decision =
      atrium::DecideEditFocusGate(inputs);
  // Logged for a GATED chord only, whichever way it went. Restricting it to
  // gated chords keeps this off the path of every ordinary hotkey (an ungated
  // chord short-circuits to kFire and is not interesting), and logging the FIRE
  // case too is what makes the two outcomes distinguishable in a run log: a VM
  // scenario can then assert "this chord was gated and allowed" separately from
  // "this chord was gated and blocked", instead of inferring the first from the
  // absence of the second.
  if (inputs.combo_edits_text) {
    VLOG(1) << "Atrium: edit focus gate " << EditFocusGateDecisionName(decision)
            << " (native route)";
  }
  return atrium::MayFireHotkey(decision);
}

}  // namespace

namespace {

// One AtriumCommandDispatcherDelegate is
// installed per Atrium window (AtriumNativeWidgetMac::OnWindowInitialized,
// one per Browser - which in the single-window presenter model is one per
// profile per slot, same fan-out as SidebarSwipeCatcher).
// -addLocalMonitorForEventsMatchingMask is process-wide, not scoped to any
// one window or object, so an instance-owned monitor would mean N monitors
// all firing on every flagsChanged event app-wide. Kept process-wide but
// refcounted down to ONE installed monitor (and one pair of teardown
// observers - see below) shared across every live delegate instance.
NSUInteger g_control_release_refcount = 0;
id g_control_release_monitor = nil;
id g_resign_key_observer = nil;
id g_resign_active_observer = nil;
// Shares the refcount above, and is separate only so the two masks stay
// independent - a mouse-down carries no modifier meaning.
id g_menu_dismiss_monitor = nil;
id g_menu_dismiss_resign_active_observer = nil;

// the window whose Ctrl-Tab hold is walking the current cycle, set
// on every successful StepTabRecencyCycle call and cleared whenever the
// cycle ends. NSWindowDidResignKeyNotification is process-wide (object:nil,
// see above) because a LOCAL monitor cannot scope itself to one window's
// notifications, so the handler has to do that filtering itself: some other
// window in this app resigning key mid-walk is the app rearranging its own
// windows, not the user leaving the one holding the cycle, and only the
// latter should end it. __weak so a window closed mid-cycle does not leave
// this dangling.
__weak NSWindow* g_recency_cycle_window = nil;

}  // namespace

@implementation AtriumCommandDispatcherDelegate

- (instancetype)init {
  if ((self = [super init])) {
    if (g_control_release_refcount == 0) {
      // no existing code in this checkout watches for a bare
      // modifier key's release (grepped for
      // flagsChanged/addLocalMonitorForEventsMatchingMask across
      // chrome/browser/ui/atrium, chrome/browser/ui/cocoa - nothing). A
      // Ctrl-Tab cycle otherwise has no end: prePerformKeyEquivalent only
      // ever sees the Tab key itself, never Control's own up/down.
      // NSEventMaskFlagsChanged is the standard AppKit signal for a
      // modifier-only change, and a LOCAL monitor (this process's own
      // events only, never the global one) is the smallest thing that fits.
      g_control_release_monitor = [NSEvent
          addLocalMonitorForEventsMatchingMask:NSEventMaskFlagsChanged
                                        handler:^NSEvent*(NSEvent* event) {
                                          // Already the one place that sees
                                          // a bare modifier move.
                                          atrium::AtriumBrowserWindow::
                                              SetSidebarNavModifiers(
                                                  (event.modifierFlags &
                                                   NSEventModifierFlagShift) != 0,
                                                  (event.modifierFlags &
                                                   NSEventModifierFlagCommand) != 0,
                                                  (event.modifierFlags &
                                                   NSEventModifierFlagOption) != 0);
                                          if (!(event.modifierFlags &
                                                NSEventModifierFlagControl)) {
                                            // Control is up in this event -
                                            // end whatever cycle is held. A
                                            // no-op on the Rust side when
                                            // none is (see
                                            // TabRecencyList::end_cycle), so
                                            // this fires unconditionally
                                            // rather than tracking
                                            // prior-flags state here too.
                                            VLOG(1) << "Atrium: recency cycle "
                                                        "end trigger flagsChanged";
                                            g_recency_cycle_window = nil;
                                            atrium::EndTabRecencyCycle();
                                          }
                                          return event;
                                        }];
      // Control's release is not the only
      // way a cycle needs to end. If the cycling window loses key status
      // (click to another window, a space switch that changes which window
      // is key) or the app deactivates (Cmd-Tab away, a space switch to
      // another app) mid-hold, no flagsChanged event for THIS app's release
      // of Control may ever arrive - the sidebar scrub hit the identical bug and
      // fixed it with exactly these two notifications.
      // EndTabRecencyCycle is the same documented no-op here, so both fire
      // unconditionally.
      g_resign_key_observer = [[NSNotificationCenter defaultCenter]
          addObserverForName:NSWindowDidResignKeyNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* notification) {
                    // object:nil means this fires for ANY window
                    // in the app resigning key, including ones that resign
                    // as a side effect of the very tab activation this walk
                    // just performed. Only the window that owns the held
                    // cycle resigning key is the user actually leaving it.
                    if (notification.object != g_recency_cycle_window) {
                      return;
                    }
                    VLOG(1) << "Atrium: recency cycle end trigger "
                               "resignKey";
                    g_recency_cycle_window = nil;
                    atrium::EndTabRecencyCycle();
                  }];
      g_resign_active_observer = [[NSNotificationCenter defaultCenter]
          addObserverForName:NSApplicationDidResignActiveNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* notification) {
                    VLOG(1) << "Atrium: recency cycle end trigger "
                               "resignActive";
                    g_recency_cycle_window = nil;
                    atrium::EndTabRecencyCycle();
                    // The monitor above is LOCAL, so a modifier released in
                    // the app the user just switched to never reaches it and
                    // the nav row would stay armed until they came back.
                    atrium::AtriumBrowserWindow::SetSidebarNavModifiers(
                        false, false, false);
                  }];
      // The sidebar document only sees presses inside itself, so a click on
      // the page, the devbar or native chrome left its menu open. The event is
      // returned unchanged - that click still has to land.
      g_menu_dismiss_monitor = [NSEvent
          addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                       handler:^NSEvent*(NSEvent* event) {
                                         const NSPoint in_screen =
                                             event.window
                                                 ? [event.window
                                                       convertPointToScreen:
                                                           event.locationInWindow]
                                                 : NSEvent.mouseLocation;
                                         atrium::AtriumBrowserWindow::
                                             DismissSidebarMenusForMouseDown(
                                                 gfx::ScreenPointFromNSPoint(
                                                     in_screen));
                                         return event;
                                       }];
      // A press in another app never reaches the monitor above, and nothing
      // in this one will be clicked while it is away.
      g_menu_dismiss_resign_active_observer =
          [[NSNotificationCenter defaultCenter]
              addObserverForName:NSApplicationDidResignActiveNotification
                          object:nil
                           queue:nil
                      usingBlock:^(NSNotification* notification) {
                        atrium::AtriumBrowserWindow::DismissSidebarMenus();
                      }];
    }
    ++g_control_release_refcount;
  }
  return self;
}

- (void)dealloc {
  if (g_control_release_refcount > 0 && --g_control_release_refcount == 0) {
    if (g_control_release_monitor) {
      [NSEvent removeMonitor:g_control_release_monitor];
      g_control_release_monitor = nil;
    }
    if (g_resign_key_observer) {
      [[NSNotificationCenter defaultCenter]
          removeObserver:g_resign_key_observer];
      g_resign_key_observer = nil;
    }
    if (g_resign_active_observer) {
      [[NSNotificationCenter defaultCenter]
          removeObserver:g_resign_active_observer];
      g_resign_active_observer = nil;
    }
    if (g_menu_dismiss_monitor) {
      [NSEvent removeMonitor:g_menu_dismiss_monitor];
      g_menu_dismiss_monitor = nil;
    }
    if (g_menu_dismiss_resign_active_observer) {
      [[NSNotificationCenter defaultCenter]
          removeObserver:g_menu_dismiss_resign_active_observer];
      g_menu_dismiss_resign_active_observer = nil;
    }
  }
}

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  LogDeadChordIfInteresting(event, "prePerform", false);
  // Respect keyboard lock exactly like the superclass - a locked page owns
  // its keys outright. Duplicated check: the fallthrough path below re-runs
  // it inside super, harmlessly.
  NSResponder* responder = [window firstResponder];
  if ([responder respondsToSelector:@selector(isKeyLocked:)]) {
    if ([(id)responder isKeyLocked:event]) {
      return ui::PerformKeyEquivalentResult::kUnhandled;
    }
  }

  // Arc-parity tab-switching: Ctrl-Tab / Ctrl-Shift-Tab
  // reroute to the instant recency cycle when AtriumOnTabCycle resolved.
  // Checked here (the raw NSEvent hook) rather than in the
  // WillExecuteCommand/ExecuteCommand IDC intercept
  // (AtriumBrowserWindow::WillExecuteCommand only receives an int32_t
  // command id, never the triggering NSEvent - see atrium_browser_window.cc)
  // because that seam cannot distinguish this literal chord from
  // Cmd+Opt+Left/Right and Ctrl+PgDn/PgUp, which funnel to the SAME
  // IDC_SELECT_NEXT_TAB/IDC_SELECT_PREVIOUS_TAB commands
  // (global_keyboard_shortcuts_mac.mm) and MUST keep their stock
  // space-cycle/strip-cycle meaning untouched - only the raw event carries
  // the actual key pressed. Hardcoded (not NativeHotkeyMap::MatchAction)
  // because Ctrl-Tab is deliberately not rebindable this milestone; checked
  // BEFORE the map lookup below for the same reason. Repeat events
  // (isARepeat, a held key) are NOT filtered here, unlike the map-driven
  // actions below - each key-equivalent event, repeats included, advances
  // the walk one more step, matching the "rapid repeated presses walk
  // deeper" design (no overlay to strobe).
  if (const std::optional<int32_t> recency_direction =
          RecencyCycleDirectionForEvent(event)) {
    // the recency list and the walk live in Rust
    // (atrium_core_tab_recency_step, wrapped by StepTabRecencyCycle) - this
    // hook only asks it for the next tab and shows it. Falls through to
    // super (stock strip cycling) whenever there is no controller to show
    // the result on, or nowhere to walk to (one tab, or none) - the same
    // bargain every other combo in this file strikes rather than swallowing
    // the key.
    //
    // WIRED: BrowserController::OnTabStripModelChanged calls
    // RecordTabRecencyActivation/RecordTabRecencyClosed, and this file's own
    // -init installs a local NSEventMaskFlagsChanged monitor that calls
    // EndTabRecencyCycle on Control's release (see the ivar's doc above).
    // chrome/browser/ui/BUILD.gn's "ui" target depends on
    // //chrome/browser/ui/atrium:tab_recency_bridge, so both call sites
    // compile and link - a 2026-08-10 gate run confirmed this indirectly:
    // atrium_browser_controller.cc's other VLOGs fired live in that run,
    // which could not happen if the bridge symbols it calls were unresolved.
    // That run's four failed vlog assertions were the scenario's doing, not
    // this code's: ctrl-tab-recency-cycle.json opens tabs B and C
    // with cmd+t, which is now the
    // openCommandBar chord, not new-tab - so the run never had more than one
    // tab to cycle through and StepTabRecencyCycle correctly returned
    // nothing every time (see a_single_tab_has_nothing_to_cycle_to in
    // rust/atrium-core/src/tab_recency.rs).
    if (atrium::BrowserController* recency_controller =
            atrium::BrowserController::ControllerForNativeWindow(
                gfx::NativeWindow(window))) {
      if (const std::optional<uint64_t> target =
              atrium::StepTabRecencyCycle(*recency_direction)) {
        VLOG(1) << "Atrium: recency cycle step direction "
                << *recency_direction << " target " << *target;
        // record which window owns this walk so the resignKey
        // observer above can tell "this window's own hold ended" apart from
        // "some other window in the app resigned key", which the
        // ActivateOwnTab call below can itself trigger.
        g_recency_cycle_window = window;
        recency_controller->ActivateOwnTab(*target);
        return ui::PerformKeyEquivalentResult::kHandled;
      }
    }
    return [super prePerformKeyEquivalent:event window:window];
  }

  // Cmd+comma and Cmd+Opt+I used to be claimed here. They were added
  // those claims because both chords reached prePerform and never postPerform
  // on an HTML surface, so no route behind them could run. The reason was found and
  // fixed the reason - every CreateChromeContents surface was delegate-less, so
  // the renderer's unhandled-key ack died in WebContentsImpl::HandleKeyboardEvent
  // and nothing was ever redispatched. With the forwarder in place the round
  // trip completes, and both chords reach their own routes again. The scenario
  // shared-forwarder-covers-chords is what stops that silently
  // regressing: with no claim here, it can only pass through the forwarder.

  // Cmd+Shift+C copies the displayed tab's link. Matched here in the
  // raw NSEvent hook so it lands BEFORE Chrome's IDC accelerator lookup and
  // wins over the stock IDC_DEV_TOOLS_INSPECT (inspect element) binding on the
  // same chord. Hardcoded rather than driven through NativeHotkeyMap because,
  // like Cmd+Q and Ctrl-Tab, it is not user-rebindable this milestone. A held
  // key must not re-copy or strobe the confirmation, so a repeat is dropped
  // (same posture as the map path below). Falls through to super only when no
  // controller/page resolves (degraded / empty window), preserving the stock
  // chord meaning in that edge.
  if (IsCopyLinkKeyEquivalent(event)) {
    if (event.isARepeat) {
      return ui::PerformKeyEquivalentResult::kDrop;
    }
    atrium::BrowserController* copy_controller =
        atrium::BrowserController::ControllerForNativeWindow(gfx::NativeWindow(window));
    if (copy_controller && copy_controller->CopyCurrentPageLink()) {
      return ui::PerformKeyEquivalentResult::kHandled;
    }
    return [super prePerformKeyEquivalent:event window:window];
  }

  // Opt+Shift+Cmd+C / Ctrl+Shift+Cmd+C - the Markdown/Quote
  // "Copy URL" variants, same hardcoded/non-rebindable posture and repeat
  // guard as the plain Cmd+Shift+C chord above (not routed through
  // NativeHotkeyMap this milestone).
  if (IsCopyLinkAsMarkdownKeyEquivalent(event)) {
    if (event.isARepeat) {
      return ui::PerformKeyEquivalentResult::kDrop;
    }
    atrium::BrowserController* copy_controller =
        atrium::BrowserController::ControllerForNativeWindow(gfx::NativeWindow(window));
    if (copy_controller &&
        copy_controller->CopyCurrentPageLink(
            atrium::LinkCopyFormat::kMarkdown)) {
      return ui::PerformKeyEquivalentResult::kHandled;
    }
    return [super prePerformKeyEquivalent:event window:window];
  }
  if (IsCopyLinkAsQuoteKeyEquivalent(event)) {
    if (event.isARepeat) {
      return ui::PerformKeyEquivalentResult::kDrop;
    }
    atrium::BrowserController* copy_controller =
        atrium::BrowserController::ControllerForNativeWindow(gfx::NativeWindow(window));
    if (copy_controller &&
        copy_controller->CopyCurrentPageLink(atrium::LinkCopyFormat::kQuote)) {
      return ui::PerformKeyEquivalentResult::kHandled;
    }
    return [super prePerformKeyEquivalent:event window:window];
  }

  // Cmd+Shift+W closes the window the user selected, tab or no tab. Resolved
  // off `window` rather than the visible tab's Browser, which under a
  // presenter swap owns a different, off-screen window.
  if (IsCloseWindowKeyEquivalent(event)) {
    if (event.isARepeat) {
      return ui::PerformKeyEquivalentResult::kDrop;
    }
    if (atrium::AtriumBrowserWindow* close_window =
            atrium::BrowserController::WindowForNativeWindow(
                gfx::NativeWindow(window))) {
      close_window->CloseUserInitiated();
      return ui::PerformKeyEquivalentResult::kHandled;
    }
    return [super prePerformKeyEquivalent:event window:window];
  }

  // M3: when extension commands land, revisit ordering - super consults the
  // views FocusManager (extension accelerators, high priority) before its
  // IDC lookup, so a matched native combo here currently shadows them.
  const std::optional<atrium::NativeHotkeyAction> action =
      atrium::NativeHotkeyMap::Get()->MatchAction(event);
  if (!action) {
    return [super prePerformKeyEquivalent:event window:window];
  }

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForNativeWindow(gfx::NativeWindow(window));

  // a matched action still does not fire if this chord is one macOS
  // uses for text editing and a text surface currently owns the keyboard.
  // Falling through to super (rather than returning kDrop) is the whole point -
  // the key must go on to reach the field, so the edit the user was making
  // happens instead of being silently swallowed.
  //
  // ORDERING IS LOAD-BEARING: this runs BEFORE the repeat guard below, not
  // after. kDrop makes CommandDispatcher::performKeyEquivalent: return YES
  // without ever calling defaultPerformKeyEquivalent:, so a dropped event never
  // reaches the responder chain at all - it is a hard swallow, not a decline.
  // With the guard first, holding a gated chord in a text field would extend
  // the selection once (the non-repeat keyDown falls through correctly) and then
  // silently stop, every auto-repeat being dropped before the gate was ever
  // consulted, while the key is visibly still held. That is precisely the
  // keystroke-eating this gate exists to prevent, reached through the repeat
  // guard instead of through the gate.
  // one line per outcome, so a chord that reached the window and
  // was declined names its reason, and one the harness never delivered leaves
  // no line at all. Both used to read as a docked pane and a guess.
  std::string_view action_id = "?";
  for (const atrium::NativeHotkeyDefault& entry :
       atrium::NativeHotkeyDefaults()) {
    if (entry.action == *action) {
      action_id = entry.action_id;
    }
  }
  if (!MayFireHotkeyForEvent(event, controller)) {
    VLOG(1) << "Atrium: hotkey " << action_id << " declined by the edit gate";
    return [super prePerformKeyEquivalent:event window:window];
  }

  // A held key must not strobe the palette/sidebar - the same posture as
  // super's IDC_NEW_TAB/NEW_WINDOW repeat guard. Applied only once the gate has
  // agreed the action may fire, so it now suppresses repeats of an action that
  // WOULD have run, never repeats the page was entitled to receive.
  if (event.isARepeat) {
    return ui::PerformKeyEquivalentResult::kDrop;
  }

  if (!controller) {
    VLOG(1) << "Atrium: hotkey " << action_id
            << " declined, no controller for this window";
    return [super prePerformKeyEquivalent:event window:window];
  }
  if (atrium::ExecuteNativeHotkeyAction(controller, *action)) {
    VLOG(1) << "Atrium: hotkey " << action_id << " fired";
    return ui::PerformKeyEquivalentResult::kHandled;
  }
  VLOG(1) << "Atrium: hotkey " << action_id << " declined by its action";

  // Gate failed (degraded dylib) or no controller resolved: fall through to
  // the stock delegate so the combo keeps its stock meaning - on a default
  // combo that is exactly today's IDC path (about:blank tab / native Save
  // Page), on a custom combo it is inert (fallback matrix).
  return [super prePerformKeyEquivalent:event window:window];
}

- (ui::PerformKeyEquivalentResult)postPerformKeyEquivalent:(NSEvent*)event
                                                    window:(NSWindow*)window
                                              isRedispatch:(BOOL)isRedispatch {
  LogDeadChordIfInteresting(event, "postPerform", isRedispatch);
  // Cmd+Q -> app main menu. This branch is in
  // practice UNREACHABLE, and the premise it was written on ("Cmd+Q
  // is not a browser IDC accelerator, so the stock delegate declines it") is
  // wrong: MenuCommandForKeyEvent resolves Cmd+Q to IDC_EXIT off the app
  // menu's terminate: item, IDC_EXIT is reserved, so super's
  // prePerformKeyEquivalent claims the chord on the FIRST pass and
  // postPerform is never reached - not on an empty window, not with web
  // content, and never on a redispatch pass either (there is no redispatch
  // once prePerform returns kHandled). Kept as a harmless fallback for any
  // future path that declines the chord earlier; the real quit route runs
  // through chrome::AttemptUserExit and its confirm-to-quit gate (see
  // confirm_quit.cc). Every non-Cmd+Q key still defers to super so browser
  // commands (Cmd+W et al.) keep their stock postPerform handling.
  if (IsCmdQKeyEquivalent(event)) {
    return ui::PerformKeyEquivalentResult::kPassToMainMenu;
  }
  return [super postPerformKeyEquivalent:event
                                  window:window
                            isRedispatch:isRedispatch];
}

@end
