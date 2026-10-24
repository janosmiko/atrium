// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_browser_controller.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_CONTROLLER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_CONTROLLER_H_

// this header names no AppKit type at all. The window lookup
// takes gfx::NativeWindow, which on macOS is a plain C++ class wrapping the
// NSWindow, so callers in Objective-C++ and in C++ both compile against it.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_open_reason.h"
#include "chrome/browser/ui/atrium/atrium_edit_focus_gate.h"
#include "chrome/browser/ui/atrium/atrium_extension_popup_host.h"
#include "chrome/browser/ui/atrium/atrium_favorite_parking.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
#include "chrome/browser/ui/atrium/atrium_link_copy_format.h"
#include "chrome/browser/ui/atrium/atrium_row_icon_lookup.h"
#include "chrome/browser/ui/atrium/atrium_drop_target_decision.h"
#include "chrome/browser/ui/atrium/atrium_split_orientation_decision.h"
#include "chrome/browser/ui/atrium/atrium_theme_animator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_ui_types.h"

class Browser;
class GURL;
class Profile;
class SkBitmap;

namespace net {
struct RedirectInfo;
}
namespace network {
class SimpleURLLoader;
namespace mojom {
class URLResponseHead;
}
}  // namespace network
class TabStripModel;
struct NavigateParams;

namespace gfx {
class Image;
}

namespace content {
struct OpenURLParams;
struct Referrer;
class WebContents;
}

namespace atrium {

class AtriumBrowserWindow;
class AutocompleteBridge;
class BrowserController;
class ExtensionActionBridge;

// the exact TabStripModel::DetachTabAtForInsertion +
// TabStripModel::InsertDetachedTabAt pairing ActivateTab's cross-window move
// and MoveLittleTabToSpace's same-profile branch already use (atrium_browser_
// controller.cc), factored out so it can be unit tested without a live
// BrowserController/AtriumBrowserWindow. The WebContents itself moves - its
// NavigationController and render process survive untouched (no reload) -
// and `source_strip`'s kRemoved notification carries
// TabRemovedReason::kInsertedIntoOtherTabStrip, which OnTabStripModelChanged
// already routes to DetachTabForMove rather than a genuine close. Returns
// false (leaving `source_strip` unchanged) if the detach at `source_index`
// fails.
// RouteContentDrop passes ADD_NONE - ADD_ACTIVE would fire ShowTab into the
// target's existing pane before InsertPaneFromDrop can place it in a new one.
bool MoveWebContentsBetweenStrips(TabStripModel* source_strip,
                                  int source_index,
                                  TabStripModel* dest_strip,
                                  int dest_index,
                                  int add_types = AddTabTypes::ADD_ACTIVE);

// Tab id contract shared with the Swift sidebar. assigned as a
// per-process monotonic counter, stamped onto `contents` as user data the
// first time this tab is seen (see the .mm's definition) - NOT the
// WebContents pointer value, which allocator reuse after a close can hand to
// a brand-new, unrelated WebContents. The ONE definition every atrium/*.mm
// translation unit must call for a given WebContents to get the same id
// back (previously each TU had its own reinterpret_cast mirror of this
// function - that duplication is exactly what a stateful id assignment
// cannot tolerate). Returns 0 for a null `contents`.
uint64_t TabId(content::WebContents* contents);

// Wires one Browser's TabStripModel to the AtriumUI sidebar and routes
// sidebar commands back to the Browser.
//
// M2a (spaces group not connected): exactly one instance, created at
// PreBrowserStart from GetLastUsedProfile() and alive for the process
// lifetime - see CreateForInitialProfile().
//
// M2b (spaces group connected): one instance per profile required by the
// user's spaces, created by atrium::ProfileBootstrap - see CreateForProfile().
// Every instance is process-lifetime (never destroyed, per Chromium's own
// singleton precedent here); a controller pointer that arrives from outside
// is resolved against the live registry rather than trusted as a raw cast
// (mirrors ResolveTab's never-trust-the-pointer pattern for tab ids).
//
// M2c: one instance per (window-slot, profile) pair - see the "Window-slot
// addressing" doc in the retired ABI header. Slot 0 is the bootstrap window
// (CreateForInitialProfile / CreateForProfile, both default to it); every
// other slot is created by Cmd+N (CreateWindowSlot) or, as a
// belt-and-suspenders catch-all for any OTHER path that still creates a
// Browser directly, attached to by the
// CollectionObserver installed via InstallCollectionObserver().
//
// Instances remain process-lifetime EVEN AFTER their window closes for real
// (a deliberate design call, not a gap): a controller's address is its
// identity, so `delete`-ing one would free that address for a LATER `new
// BrowserController(...)` to reuse, aliasing the new controller onto the old,
// dead one's stale entries - a real misrouting hazard, for example an
// activate_tab meant for a long-gone window landing on whatever controller
// now happens to sit at that address. The Swift registry that made this
// acute is gone, but the address-reuse hazard is not, so the
// posture stands. OnTabStripModelDestroyed instead nulls browser_,
// releases sidebar_view_, clears this slot's SlotPresentationTable entries,
// and releases the profile keep-alive - a dead controller stays registered
// but permanently, safely inert: discoverable by every lookup, never
// misread, never double-released, and never reused.
class BrowserController : public TabStripModelObserver,
                          private RowIconLookup::Delegate {
 public:
  // The legacy/bootstrap window-slot - see the retired ABI header's window-slot
  // addressing doc. Every pre-M2c call site implicitly targets this slot.
  static constexpr uint64_t kDefaultSlot = 0;

  // M2a fallback: creates the sole process-lifetime controller from
  // GetLastUsedProfile(). Must only be called when UIBridge::connected().
  static void CreateForInitialProfile();

  // M2b: creates one process-lifetime controller for `profile` in `slot`
  // (defaults to kDefaultSlot for every pre-M2c call site). If `show` is
  // true, the window is made visible immediately; a non-empty `startup_url`
  // (from UIBridge::CopyStartupTabJSON, or "about:blank" if it's
  // empty/invalid at the bootstrap call site) is opened as its one tab -
  // lazy restore: every other tab in every space stays cold. An
  // EMPTY `startup_url` with `show` true (M2c Arc-parity: Cmd+N)
  // opens the window with NO tab at all - sidebar + tinted backdrop only,
  // see CreateWindowSlot. If `show` is false, the Browser and its window are
  // created with NO tabs at all and never shown, so the profile is live
  // (and its BrowserController is a valid ActivateWindow() target) without
  // materializing anything until the user visits that space; `startup_url`
  // is ignored in that case. Must only be called when
  // UIBridge::spaces_connected().
  static BrowserController* CreateForProfile(Profile* profile,
                                             bool show,
                                             const std::string& startup_url,
                                             uint64_t slot = kDefaultSlot);

  // Shows `profile`'s strip in every window whose space belongs to it. A switch
  // to a space whose profile was still loading showed the window's own strip.
  static void ShowLoadedProfile(Profile* profile);

  // Cmd+N: opens a NEW window-slot showing the SAME
  // profile currently visible in `invoking_browser`'s slot (it is
  // resolved via VisibleBrowserForBrowser, so a window presenting a foreign
  // profile opens THAT profile, not its own; falls back to
  // `invoking_browser`'s own profile when no swap is in effect) - shown
  // immediately with NO tab (Arc parity: the sidebar renders the
  // seeded space's tabs, none active; the web pane shows the tinted
  // backdrop; the URL pill is focused - see
  // SidebarView.swift's onAppear). The reference
  // product opens no tab at all here. No-op (logged) if `invoking_browser`
  // has no controller or that controller's Browser already tore down. Must
  // only be called when UIBridge::spaces_connected() (mirrors
  // CreateForProfile). Passes the invoking controller's own slot as the new
  // controller's `seed_from_slot` (hard requirement - see the
  // self-creating ctor's doc), so the new window's sidebar renders the
  // right space from
  // its very first frame instead of the unfiltered M1 fallback.
  //
  // Tab detach: `screen_point` positions the new window's origin
  // at that gfx screen point (clamped into the nearest display's work
  // area); nullopt keeps the default centered placement. `open_command_bar`
  // false skips the Cmd+N palette auto-open - the detach caller is about to
  // move a tab into the window, so the empty-window affordance is wrong
  // there (see the retired ABI header's OPEN WINDOW SLOT doc block).
  static BrowserController* CreateWindowSlot(
      Browser* invoking_browser,
      std::optional<gfx::Point> screen_point = std::nullopt,
      bool open_command_bar = true);

  // Multi-window restore, cross-profile: opens a NEW window-slot
  // for `target_profile_id` - NOT the invoking window's own profile, unlike
  // CreateWindowSlot. `invoking_browser` is only used to confirm a live
  // controller exists to invoke through (mirrors CreateWindowSlot's guard);
  // its profile is otherwise irrelevant here. `target_profile_id` must
  // already have a bootstrapped controller at kDefaultSlot (every profile
  // named by any persisted space is a "required profile" -
  // ProfileBootstrap::Bootstrap loads one hidden controller per required id
  // at startup, before restore ever runs) - resolved via
  // ControllerFor(kDefaultSlot, target_profile_id). No-op (logged), returning
  // null, if that profile has no such controller: restore never waits for or
  // triggers a fresh CreateProfileAsync load here, since RequiredProfilesJSON
  // already covers every profile a restored window could name.
  // Always shown immediately with NO tab (same Arc-parity posture as
  // CreateWindowSlot). The new slot shows the invoking window's space when
  // that space belongs to `target_profile_id`, else that profile's first
  // space. `screen_point` behaves exactly as CreateWindowSlot's.
  static BrowserController* CreateWindowSlotForProfile(
      Browser* invoking_browser,
      const std::string& target_profile_id,
      std::optional<gfx::Point> screen_point = std::nullopt);

  // CreateWindowSlotForProfile's resolution, reused for Mirror
  // View, which folds into an existing split instead of a new window. Null
  // (no window, no load triggered) for an unsafe or unbootstrapped id.
  static Profile* ResolveMirrorProfile(const std::string& profile_id);

  // Dock-icon reopen: handles a macOS reopen event (Dock click)
  // for a running Atrium with no stock-handled browser windows. Returns
  // true when Atrium handled it (the stock AppController path - profile
  // picker or a plain CreateBrowser window, both sidebar-less - must then
  // be skipped); false to fall through to stock behavior (spaces bridge not
  // connected, or nothing to reopen with). Two branches, tried in order:
  //   1. A live user-facing window exists: the slot's presenter window is
  //      Show()n - surfacing what the user already has, never constructing
  //      a duplicate. In practice this rescues the app-hidden (Cmd+H)
  //      case - a literally miniaturized window is already caught by the
  //      stock focus branch upstream of the hook (its NSWindow stays
  //      isVisible), which returns before Atrium is consulted. Hidden-pool
  //      controllers (created show=false, never presented) are excluded by
  //      the presenter check, so a headless per-profile Browser can't be
  //      dragged on screen.
  //   2. No user-facing window (the "closed the last Atrium window, clicked
  //      the Dock icon" report): opens a NEW window-slot for the active
  //      space's profile (CopyStartupTabJSON, same resolution bootstrap
  //      uses; falls back to any live controller's profile when the id is
  //      missing/unloaded). Cmd+N posture deliberately - zero tabs +
  //      command bar - rather than re-materializing the startup tab: the
  //      sidebar renders the active space's rows either way, and skipping
  //      the tab keeps this path clear of the cold-adoption matching that
  //      launch-time restore needs.
  static bool ReopenFromDock();

  // Little Atrium: opens a brand-new "little" window - a
  // lightweight, chromeless, SINGLE-TAB window with NO sidebar and NO space
  // rows, showing `url` immediately. Unlike CreateWindowSlot/CreateForProfile,
  // the resulting controller is NEVER registered as a Swift host (no
  // RegisterHostInSlot call - see the little ctor arm's doc) - it never
  // builds a sidebar, command bar, or extension bridge either. It DOES stay
  // in Registry() (a future promote stage's profile-scoped tab-move scan
  // needs to find it there) and it DOES get its own window-slot (NextSlotId())
  // so ActivateWindow/the presentation table work generically for it like
  // any other controller. `screen_point` behaves exactly like
  // CreateWindowSlot's: nullopt keeps the default centered placement,
  // otherwise the new window's origin is that gfx screen point (clamped into
  // the nearest display's work area). No hotkey, no external-link
  // interception, and no promote affordance exist yet - those are Stages
  // 2-4; this is purely the primitive, callable directly for now.
  // `link_params`, when given, replaces the ctor's bare typed seed with one
  // Navigate() carrying the link's referrer and initiator origin.
  static BrowserController* CreateLittleWindow(
      Profile* profile,
      const GURL& url,
      std::optional<gfx::Point> screen_point = std::nullopt,
      const content::OpenURLParams* link_params = nullptr);

  // unlike AdoptTabIntoLittleWindow, never moves a tab - the
  // pinned tab a Doorway comes from must stay put. Focuses an existing
  // little window for `profile`/`url` if one is open, else CreateLittleWindow.
  static BrowserController* OpenOrFocusDoorwayWindow(
      Profile* profile,
      const GURL& url,
      std::optional<gfx::Point> screen_point = std::nullopt);

  // the tear-off entry point - adopts a WebContents that already
  // lives in another live controller's TabStripModel into a brand-new little
  // window, instead of CreateLittleWindow's "always a fresh tab from a URL"
  // contract. Scans Registry() for the controller currently holding
  // `contents` (the same lookup ActivateTab's cross-window branch does; the
  // caller - the sidebar message handler - only has a tab id, not a host
  // handle) and, once found, moves it with MoveWebContentsBetweenStrips
  // rather than creating a new tab, so the NavigationController and render
  // process survive (no reload). The resulting window matches
  // CreateLittleWindow's contract exactly: little_ == true, no
  // seed_from_slot. `screen_point` behaves exactly like CreateLittleWindow's.
  // Returns nullptr (logged) if `contents` is not found in any live
  // controller's strip, or if the detach fails.
  // Activation-moves-tab (DetachTabAtForInsertion / InsertDetachedTabAt
  // template, copied from TabListBridge::MoveTabToWindow):
  // if `tab_id` isn't in `host`'s own strip, searches every OTHER
  // BrowserController on the SAME Profile for one whose strip contains
  // it, and if found, moves the tab there with ADD_ACTIVE (the Arc-style
  // "becomes active in the window that activated it" semantic - the one
  // deliberate deviation from the literal MoveToWindow template, which uses
  // ADD_NONE). A tab that resolves nowhere (including the impossible-by-
  // construction cross-profile case - a WebContents is bound to its creating
  // BrowserContext) is logged and dropped, same as an unknown host/tab_id.
  //
  // Public because AtriumBrowserWindow::ActivateSidebarTab calls it. Its only caller was the
  // retired Swift ABI, so once that arm was gone the whole
  // cross-window move went unreachable - built, documented as Arc parity, and
  // dead.
  static void ActivateTab(BrowserController* host, uint64_t tab_id);

  // The tab claiming `favorite_url` in another same-profile window, for
  // ActivateTab to move here. A favourite click used to search only its own
  // strip, so a second window opened a second copy of the site.
  static std::optional<uint64_t> FavoriteTabElsewhere(
      BrowserController* host, const std::string& favorite_url);

  // The tab `browser`'s window is frozen on after another window took it
  // (AtriumBrowserWindow::ArmMoveOutFreeze), or 0. The sidebar's live strip
  // reads it so the row or tile of that tab stays the selected one.
  static uint64_t FrozenTabIdForBrowser(Browser* browser);

  // The second half of a cross-window ActivateTab: detaches `tab_id` from
  // `source` and inserts it active in `host`. Runs once the losing window
  // froze (see AtriumBrowserWindow::ArmMoveOutFreeze), so both controllers
  // are resolved again here.
  static void CompleteMoveIn(BrowserController* host,
                             BrowserController* source,
                             uint64_t tab_id);

  // A window frozen on `tab_id` (AtriumBrowserWindow::ArmMoveOutFreeze) still
  // wants it. When this window stops showing that favourite's tab, it goes
  // back there, live. Posted: a detach inside a TabStripModel observer is not
  // allowed.
  void HandBackFrozenTab(uint64_t tab_id);
  void HandBackFrozenTabNow(uint64_t tab_id);

  static BrowserController* AdoptTabIntoLittleWindow(
      content::WebContents* contents,
      std::optional<gfx::Point> screen_point = std::nullopt);

  // the release reported for a sidebar drag that
  // resolved no drop kind of its own. Classifies `screen_point` (screen
  // coordinates) against every OTHER live controller's window using
  // ChooseDropTarget (atrium_drop_target_decision.h) and, for the no-match
  // case only, adopts `contents` into a fresh little window via
  // AdoptTabIntoLittleWindow - the tear-off primitive. The
  // sidebar branch's behavior is RouteSidebarDrop's, below; the
  // content branch is RouteContentDrop's.
  // The source window is excluded from the candidate list,
  // so a release back onto it takes the same no-match tear-off path
  // as empty desktop. Hidden and little windows too - see BuildDropCandidates.
  void ClassifyAndRouteDroppedTab(content::WebContents* contents,
                                   const gfx::Point& screen_point);

  // Acts on ClassifyAndRouteDroppedTab's kSidebar
  // branch once it has already resolved a target - `this` is the drag's
  // source controller (the caller already confirmed `contents` lives in its
  // own strip) and `target` is the OTHER live controller whose sidebar the
  // release landed on. Compares `this`'s and `target`'s profile_id_ via
  // ChooseSidebarDropAction (atrium_sidebar_drop_decision.h): same
  // workspace moves the tab live (MoveWebContentsBetweenStrips, same
  // primitive AdoptTabIntoLittleWindow uses) with no prompt; a different
  // workspace is classified but NOT acted on yet - the confirmation UI is
  // Filed separately rather than guessing a dialog design. A
  // no-op (logged) if `contents` is not in `this`'s own strip, or if either
  // controller's Browser already tore down.
  void RouteSidebarDrop(content::WebContents* contents,
                        BrowserController* target);

  // forces RouteSidebarDrop's ask-before-moving answer, since
  // g_should_skip_message_box_for_test can only simulate YES. std::nullopt
  // restores the real prompt.
  static void SetSidebarMoveConfirmResultForTesting(
      std::optional<bool> confirmed);

  // ClassifyAndRouteDroppedTab's kContent branch.
  // A cross-profile `target` reopens the URL and closes the source rather
  // than moving, since a WebContents cannot leave its own BrowserContext.
  void RouteContentDrop(content::WebContents* contents,
                        BrowserController* target,
                        const gfx::Rect& content_bounds,
                        const gfx::Point& screen_point);

  // When set, replaces the real move + insert with just recording (target,
  // orientation) - lets the classification be unit-tested on its own.
  static void SetContentDropInsertionCallbackForTesting(
      base::RepeatingCallback<void(BrowserController* target,
                                    ContentDropOrientation orientation)> callback);

  // The same window for a link another macOS app
  // handed over, which has no invoking window to take a profile from.
  //
  // Resolves one the way ReopenFromDock's branch 2 does - the first live
  // controller's - and returns nullptr when there is none, which is a link
  // that COLD-STARTED Atrium: openURLs runs before any controller exists, so
  // the caller falls through to the stock path rather than inventing a
  // profile. There is no "last-used profile" concept
  // yet.
  static BrowserController* CreateLittleWindowForExternalLink(const GURL& url);

  // closes this controller's open extension popup, if any - a
  // no-op if nothing is open. Public: AtriumBrowserWindow::ToggleQuickMenu
  // and ActivateWindow's in-window swap branch both call it.
  void CloseExtensionPopup();

  // Resolves `host` against the live registry; never trusts the pointer by
  // blindly casting it back. Returns null for an unknown or null host.
  static BrowserController* ControllerFor(BrowserController* host);

  // Shared by the release path and the live preview, so the two cannot
  // classify the same point differently. `resident` is filled in step with the
  // returned candidates - index i of one names index i of the other.
  std::vector<DropTargetCandidate> BuildDropCandidates(
      std::vector<BrowserController*>& resident);

  // The part of a candidate's window a content drop can land in.
  static gfx::Rect ContentBoundsOf(const DropTargetCandidate& candidate);

  // (slot, profile) lookup - the M2c registry key.
  // Infrastructure for a future per-slot cross-profile switch (Q5's
  // hidden-window pool); no current call site needs it, since every slot
  // this task creates gets a freshly-allocated id. Returns null if no
  // controller for that exact pair exists.
  static BrowserController* ControllerFor(uint64_t slot,
                                          const std::string& profile_id);

  // A controller whose live strip runs on `profile`, from `near`'s slot when
  // that slot has one. Matched by pointer, because an incognito controller
  // carries its parent's profile id.
  static BrowserController* LiveControllerForProfile(
      const BrowserController* near,
      const Profile* profile);

  // Reverse lookup for the last-tab-out interception: AtriumBrowserWindow
  // only knows its own Browser*, not the controller observing it, so it
  // resolves back through the registry the same way ControllerFor() does. Returns null for an unknown or null
  // Browser (e.g. a non-Atrium window, or a controller whose Browser already
  // tore down).
  static BrowserController* ControllerForBrowser(Browser* browser);

  // The controller whose window is `window`, or null - the native-hotkeys
  // dispatcher delegate's lookup (it only has the window the key event landed
  // on). Skips torn-down controllers (null browser_).
  static BrowserController* ControllerForNativeWindow(gfx::NativeWindow window);

  // That controller's own Atrium window, for the same caller. A key chord
  // acts on the window it landed on, never on the visible tab's Browser,
  // which under a presenter swap owns a different, off-screen one.
  static AtriumBrowserWindow* WindowForNativeWindow(gfx::NativeWindow window);

  // The window-slot `controller` belongs to, or kDefaultSlot if `controller`
  // is null (never a real answer for a live controller; kDefaultSlot is
  // just a safe default for a null input).
  static uint64_t SlotOf(BrowserController* controller);

  // In-window cross-profile switch:
  // the AtriumBrowserWindow whose NSWindow is PHYSICALLY on screen for
  // `browser`'s slot right now - may be a DIFFERENT controller's window
  // than `browser`'s own (browser_->window()) when this slot's content is
  // currently being presented in-place inside another profile's window
  // (see ActivateWindow's in-window swap branch and kPresenterBySlot's
  // doc in the .mm). AtriumBrowserWindow::HandleKeyboardEvent uses this
  // instead of its own GetNativeWindow() so an unhandled reserved shortcut
  // (e.g. Cmd+Q) from a WebContents whose delegate Browser's own window is
  // off-screen still redispatches to the window the user can actually see.
  // Returns null for an unknown Browser (mirrors ControllerForBrowser) or a
  // slot with no presenter yet.
  static AtriumBrowserWindow* PresentedWindowForBrowser(Browser* browser);

  // Runs `extension_id`'s action against this window's active tab. Public so
  // the strip chips and quick menu tiles share the ABI op's lookup path.
  void ClickExtensionAction(const std::string& extension_id);

  // The owner (see ControllerOwningExtension) with an active tab, or null.
  Browser* BrowserOwningExtensionForContextMenu(
      const std::string& extension_id);

  // Flips the extension's pin in the profile's own ToolbarActionsModel.
  // Chromium owns the state, Atrium stores nothing. A no-op for an id the
  // model does not know.
  void ToggleExtensionPin(const std::string& extension_id);

  // The extension-action snapshot currently in effect for this window, or
  // empty when this window has no extension bridge. For a surface built after
  // the push has already happened - see ExtensionActionBridge.
  std::string CurrentExtensionActionsJSON() const;

  // The controller owning the active space's profile (falls back to `this`).
  // Used by SelectSpace, and by a window rebuilding a surface for its own
  // controller, to push the active space's own snapshot rather than `this`'s.
  BrowserController* ActiveSpaceController();

  // Single-window Cmd+W: the Browser whose CONTENT is currently
  // VISIBLE in `browser`'s slot - the sibling of PresentedWindowForBrowser
  // that resolves the visible content rather than the physical presenter. In
  // the single-window presenter model a slot shows a FOREIGN profile's
  // WebContents in-place (ActivateWindow's in-window swap), so `browser` (the
  // presenter window's own profile) and the visible browser diverge. Tab- and
  // window-scoped commands (IDC_CLOSE_TAB etc.) dispatched on `browser` would
  // act on its OWN off-screen tab; AtriumNativeWidgetMac routes them here so
  // they act on the tab the user can actually see. Returns null for an unknown
  // Browser (mirrors ControllerForBrowser), a slot with no visible entry, or a
  // torn-down visible controller (null browser_) - callers fall back to
  // `browser`.
  static Browser* VisibleBrowserForBrowser(Browser* browser);

  // The rail position of the space `browser`'s window shows. Pass it where a
  // core call takes an active_space_index. The document's active space for a
  // Browser with no controller.
  static int SpaceIndexForBrowser(Browser* browser);

  // the window whose pane actually DISPLAYS `browser`'s own content
  // right now - the third answer the two helpers above cannot express between
  // them. PresentedWindowForBrowser names whichever window is PHYSICALLY on
  // screen for the slot regardless of whose content it shows, so it is the
  // wrong question to ask when you mean "where is MY tab". Three outcomes:
  //   - a window: `browser`'s content is shown there (its own window normally,
  //     the slot's presenter after a cross-profile in-window swap);
  //   - nullptr: shown in NO pane. `browser` presents the slot but is not its
  //     visible one, so its own on-screen window is showing a DIFFERENT
  //     controller's tab and its own is attached nowhere. Callers must not
  //     touch a pane in this state - that is what evicts the visible tab;
  //   - std::nullopt: `browser` is not a live Atrium controller's Browser, so
  //     there is no routing answer and callers keep their own fallback.
  // Decided by SlotPresentationTable::ResolveDisplayRouting, the same function
  // OnTabStripModelChanged routes its pane attach through, so this never
  // becomes a second, drifting copy of that decision.
  static std::optional<AtriumBrowserWindow*> DisplayingWindowForBrowser(
      Browser* browser);

  // One TabStripModel holds every space's tabs and picks the next by index, so
  // a close hands over whatever sat beside the tab that went. False for a row
  // a favourite owns, or one still owed between AddTab and the first commit.
  static bool TabIsOutsideActiveSpace(content::WebContents* contents);

  // M2c Rev3 (bug 3): `browser`'s window just became key/active
  // (AtriumBrowserWindow::OnWidgetActivationChanged) - resolves `browser`'s
  // controller and forwards to Swift (UIBridge::OnWindowActivatedInSlot) so
  // it can decide whether this is a takeover trigger (see the retired ABI header's
  // "Window-focus takeover" doc and SpacesViewModel.windowDidActivate). A
  // no-op for a controller-less window (not an Atrium space window) or
  // without the spaces group connected - mirrors every other spaces-group
  // touch point's AND-gate.

  // Installs the process-wide BrowserCollectionObserver that attaches a
  // controller to any Atrium (TYPE_NORMAL) Browser created OUTSIDE this
  // class's own self-creation paths (bootstrap, CreateWindowSlot) - e.g. a
  // future chrome feature that calls Browser::Create() directly. Idempotent;
  // call once from
  // AtriumMainExtraParts::PreBrowserStart(). No-op for a Browser this class
  // is already in the middle of creating itself (see CreatingOwnBrowser()
  // in the .mm) - never double-attaches.
  static void InstallCollectionObserver();
  // GlobalBrowserCollection dies with BrowserProcessImpl, so this must run
  // before that, or the observation dangles at exit.
  static void UninstallCollectionObserver();

  // Makes `target`'s window the one visible Atrium window IN ITS SLOT: syncs
  // its bounds to that slot's currently-visible window's frame, then
  // cross-fades the two windows' alpha (~150ms, direct NSWindow animation)
  // before hiding the previously-visible one.
  // Every other slot's visible/fade state is untouched - M2c has multiple
  // simultaneously-visible windows, one per slot.
  // No-op if `target` is null or already visible in its slot (still
  // re-confirms to Swift so switcher state can't drift). The very first
  // activation in a slot (no previously-visible window for that slot) is a
  // plain show with no fade partner. An activation that arrives while THAT
  // SLOT's fade is still animating is ignored (logged) rather than racing
  // the in-flight completion handler; a fade in a DIFFERENT slot never
  // blocks this one. Notifies the Swift sidebar via OnWindowShownInSlot once
  // the swap completes.
  // whether this window's tabs may reach the persisted document.
  //
  // False for an incognito profile. Incognito is a PROFILE property, not a
  // browser type, so an incognito window is is_type_normal and the bootstrap
  // gives it an AtriumBrowserWindow and a BrowserController like any other -
  // which put private URLs and titles into spaces.json, its .bak, its .pre-rust
  // and the 30-day archive, and drew them as rows in a NON-incognito space,
  // since the write passes -1 for the space index and the active space decides.
  //
  // SetFaviconForFavoriteOp already guarded incognito for a narrower leak; this
  // is the same rule applied to the write path itself.
  bool PersistsTabs() const;

  static void ActivateWindow(BrowserController* target);

  // Fallback-promotion safety net: a controller created with
  // show=false has zero tabs (lazy restore). If ProfileBootstrap ever needs
  // to show one of these as an emergency substitute (its own intended
  // active profile failed to load), it must not present a totally blank
  // window - adds a plain about:blank tab if the strip is still empty.
  void EnsureAtLeastOneTab();

  // Last-tab-out interception: true if the
  // most recent kRemoved batch emptied this controller's tab strip via a
  // move to another Atrium window (TabRemovedReason::kInsertedIntoOtherTabStrip),
  // rather than a genuine close. AtriumBrowserWindow::OnWindowCloseRequested
  // consults this - via ControllerForBrowser() - when its Browser's tab
  // strip is empty, to tell "last tab moved out, keep the window open
  // showing this space's cold tabs" apart from "last tab closed, let the
  // window close like Chrome's own tabbed-window semantics". Consuming
  // (read-and-reset) keeps a move that didn't empty the strip from
  // corrupting a later, unrelated genuine close of the actual last tab.
  bool ConsumeMoveOutEmptyFlag();

  // true for a little controller (CreateLittleWindow /
  // CreateLittleWindowForExternalLink). AtriumBrowserWindow::
  // OnWindowCloseRequested's "stay open showing cold tabs" suppression below
  // exists for SPACE windows - a little window has no sidebar to show cold
  // tabs in, and MoveLittleTabToSpace's own "Little window closes" contract
  // would otherwise be silently overridden by that suppression the
  // moment its detach empties the strip.
  bool IsLittle() const { return little_; }

  // Activates `tab_id` against this controller's own host - what
  // the activate_tab op did, minus the handle resolution
  // AtriumBrowserWindow doesn't need since it already knows which
  // controller it belongs to (ControllerForBrowser). Used by the
  // frozen-snapshot pane's click handler (Arc-parity revision 2:
  // AtriumBrowserWindow::OnSnapshotClicked) so clicking the snapshot takes
  // the tab back exactly like clicking its sidebar row would.
  void ActivateOwnTab(uint64_t tab_id);

  // FoyerMoveToSpace: hands this little window's one tab to a
  // real space and closes the little window. No space picker exists yet
  // (the real space list isn't wired to the foyer surface), so "the
  // space" is whichever OTHER live, non-little controller Registry() holds -
  // CreateLittleWindow's own doc comment already anticipated this scan.
  // Same-profile: the WebContents moves live (detach/insert, no reload).
  // Cross-profile: a WebContents can't cross BrowserContext, so the URL is
  // reopened fresh in the target's profile instead. A no-op (logged) if this
  // isn't a little controller, its strip isn't exactly one tab, or no space
  // window is open to move into.
  void MoveLittleTabToSpace();

  // asks this controller's presenting window to bring the space's
  // first remaining row back to life, after a genuine close emptied the strip.
  // Posted rather than called inline (see the call site) and re-checks
  // everything itself, because a run-loop turn passes first.
  void PromoteSurvivingRow();

  // Opens (create-if-needed already happened at construction, see the
  // ctors) this controller's own command-bar widget. Public, non-static -
  // mirrors ActivateOwnTab's shape: an unrelated class (AtriumNativeWidgetMac
  // ::ExecuteCommand, which only knows the Browser*, not this controller's
  // private window() accessor) needs to act on a resolved controller from
  // outside. Also used by OpenCommandBarOp (the Swift-initiated
  // open_command_bar path) and CreateWindowSlot's post-show hook. A no-op if
  // this controller's Browser already tore down. `is_startup_open` forwards
  // to AtriumBrowserWindow::OpenCommandBar/CommandBarWidget::Show - see the
  // latter's doc; only the process-launch bootstrap callers pass true.
  void OpenCommandBar(bool is_startup_open = false);

  // what the sidebar's "+ New Tab" means - the palette when the
  // command-bar group is connected, an ordinary tab when it is not, matching
  // HostActions.newTab's documented fallback so both arms' buttons agree.
  void OpenCommandBarOrNewTab();

  // collapses the sidebar, or restores it to the width it had
  // before it was collapsed. False when there is nothing to toggle - no
  // browser, no resolvable window, or a little window, which has no sidebar.
  // The caller must pass that false on rather than swallow it, or Cmd+S stops
  // reaching Save Page on a window that could not toggle anything.
  bool ToggleSidebar();

  // Whether ToggleSidebar would do anything. Asked before the key is claimed,
  // so the two dispatch levels agree about who owns Cmd+S.
  bool CanToggleSidebar();

  // The window a sidebar change must land on - the slot's presenter when
  // there is one, this controller's own otherwise.
  AtriumBrowserWindow* SidebarWindow();

  // moves to the next or previous row in the SIDEBAR's order, which
  // is not the strip's - a space keeps rows for tabs that are not open, and
  // the user reorders rows by dragging. `direction` is +1 or -1. False when
  // there is nowhere to go, so the combo falls through rather than being
  // swallowed.
  bool CycleTabInSidebarOrder(int direction);

  // moves to the next or previous SPACE, wrapping at both ends.
  // `direction` is +1 for the space after the active one in rail order and -1
  // for the one before it. False when there is nowhere to go - no space model
  // at all, or a single space - so the combo falls through rather than being
  // swallowed, exactly as the row cycle above does.
  bool CycleSpace(int direction);

  // makes the space at `index` in rail order active and brings the
  // window's tabs with it, which is the half the rail has never done. The
  // keyboard and the rail both land here so they cannot drift apart. False
  // when the core has no space at that index - it ignores such an index rather
  // than clamping it, so the caller must wrap before it asks.
  bool SelectSpace(int index);

  // Puts the space at `index` in the pane: its remembered tab, its tile's tab,
  // its first row, or nothing. `allow_cold_open` false keeps a recovery off
  // ShowRow's cold branch, which would reopen a pinned row's closed tab.
  void ShowActiveSpaceInPane(AtriumBrowserWindow* sidebar,
                             const base::DictValue& root,
                             int index,
                             bool allow_cold_open);

  // Posted, never called inline: the strip is mid-notification when the drift
  // is spotted and ActivateTabAt re-enters it.
  void ReselectTabInActiveSpace();

  bool TearingDown() const;

  // The backdrop scrub a swipe drives, `index` in rail order.
  // While one is in flight toward `index`, SelectSpace(index) leaves the
  // backdrop to it rather than starting a second cross-fade.
  void BeginSpaceScrub(int index);
  void SetSpaceScrubBlend(double weight);
  void EndSpaceScrub(bool commit);
  // The controller holding `window`'s scrub, or null. A cross-profile switch
  // moves the window's target browser mid-scrub, so the target's controller
  // is not always the one that began it.
  static BrowserController* SpaceScrubOwner(const AtriumBrowserWindow* window);
  bool space_scrub_in_flight_for_testing() const {
    return space_scrub_.has_value();
  }

  // Adds a space on this window's profile and moves to it, the way the rail's
  // own new-space button does. False on an off-the-record window, which must
  // not write to the spaces document.
  bool CreateSpace();

  // Marks the next inserted tab as this favourite's, so the strip observer
  // sees the claim on its first look. Pass an empty string to withdraw it.
  void SetPendingFavoriteKey(const std::string& favorite_url);

  // A user close of a favourite's own page parks it instead of deleting it.
  // False when the tab does not qualify, and the caller closes it as usual.
  bool ParkFavoriteTab(int index);

  // Puts the page parked for `favorite_url` back at the end of the strip,
  // active. False when nothing is parked for it.
  bool RestoreParkedFavorite(const std::string& favorite_url);

  // Drops it in every window of this profile, not only this one.
  void DropParkedFavorite(const std::string& favorite_url);

  AtriumFavoriteParking& favorite_parking_for_testing() {
    return favorite_parking_;
  }

  // Takes it from whichever window of this profile parked it.
  std::unique_ptr<content::WebContents> TakeParkedFavorite(
      const std::string& favorite_url);

  // opens `url` in the space a routing rule named, and returns
  // whether it did.
  //
  // Lives here rather than in the throttle because the move needs `browser_`,
  // which nothing outside this class can reach, and because the same-profile
  // and cross-profile cases differ only in which controller answers - a split
  // the throttle has no business knowing about.
  //
  // False for a rule naming a space that no longer exists, or a profile with no
  // bootstrapped controller. The caller then leaves the navigation alone, which
  // is the right failure: routing is an enhancement.
  // the live edge highlight while a sidebar drag is in flight over
  // another window's content area.
  //
  // Runs the SAME candidate build and the SAME ChooseDropTarget the release
  // path runs, then the same ResolveSplitEdge, so the highlight cannot promise
  // an edge the drop would not pick. That is why it shares BuildDropCandidates
  // rather than re-deriving the geometry: two copies of this arithmetic would
  // drift, and the drift would only ever be visible as a drop landing somewhere
  // the preview did not point.
  //
  // Every window that is not the target is cleared on each move, so a pointer
  // leaving one window and entering another never leaves two highlights up.
  void PreviewDropForPoint(const gfx::Point& screen_point);

  // Clears any highlight this drag put up, on release or on cancel.
  static void ClearDropPreviews();

  // Sends `source` to the space `space_id` names and loads `url` there.
  //
  // The tab MOVES. Same profile keeps the WebContents and carries its row
  // across; another profile cannot - a WebContents is bound to its creating
  // BrowserContext - so it reopens there and closes the source, the same
  // bargain OnSidebarMoveConfirmed strikes.
  //
  // A null `source` has nothing to move, so a fresh tab opens in the space.
  // It declines for the already-active space, which the caller opens itself.
  static bool OpenRoutedURL(Browser* from,
                            content::WebContents* source,
                            const std::string& space_id,
                            const GURL& url);

  // Fails OpenRoutedURL's realised-window guard. Browser::Create realises its
  // window before it returns, so a test has no other way to reach the decline.
  static void SetRoutedOpenTargetUnrealisedForTesting(bool unrealised);

  // whether a text field inside the sidebar surface holds the
  // keyboard. The edit-focus gate reads the command bar's visibility and the
  // active tab's own focus live, but the sidebar is a separate WebContents,
  // so this is the one part of the answer that has to be told.
  void SetAtriumTextFocused(bool focused);

  // resolves an icon for a page this profile has visited and puts it
  // in IconCache, where chrome://atrium.top-chrome/urlicon/<url>.png serves
  // it. A no-op for a URL already cached or already asked for.
  void RequestFaviconForURL(const std::string& url);

  // the same for a whole sidebar snapshot's worth of rows, which is
  // where a restored or pinned tab's icon comes from - it has no live
  // WebContents, so the reactive per-tab pipeline never produces one.
  void RequestFaviconsForRowURLs(const std::vector<std::string>& urls,
                                 bool allow_refetch = true);

  // Re-asks for a favourite's icon after its page has been visited.
  void RetryFaviconForVisitedURL(const std::string& url);

  // The active space's favourites, for the command bar's own rows.
  std::vector<CommandBarFavorite> CommandBarFavorites();
  std::vector<CommandBarTab> CommandBarTabs();
  std::vector<CommandBarSpace> CommandBarSpaces();
  // All three from one model build: a palette push asks for all of them.
  CommandBarSidebarRows CommandBarRowsForPush();

  // Stores space_id's theme (null removes it). Writing is the caller's, so
  // the settings window's one save covers both stores the core marked dirty.
  // False when the core refuses the id or the JSON.
  bool SetSpaceTheme(const std::string& space_id,
                     const std::string* theme_json);

  // Fades the stored theme in once that write has landed. Re-pushes the
  // sidebar snapshot first, because the core derives Space.tint from the
  // theme and the chips would otherwise keep the old colour.
  void ApplySavedSpaceTheme(const std::string& space_id);

  // Paints theme_json without storing it - the live scrub. Returns the window
  // it painted, or null when there is none to paint.
  AtriumBrowserWindow* PreviewSpaceTheme(const std::string& space_id,
                                         const std::string& theme_json);

  // Paints the active space's stored theme on a window that has just become
  // its slot's presenter. SelectSpace was the only other place that paints
  // one, so a relaunch, a restore and every second window opened flat.
  void ApplyActiveSpaceTheme(AtriumBrowserWindow* window);

  // Puts `window`'s ACTIVE space's stored theme back. The edited space is not
  // necessarily the one on screen any more, and the backdrop belongs to
  // whichever space the window is showing.
  void RevertThemePreview(AtriumBrowserWindow* window);

  // Test-only forwards to the private theme entry points.
  void ApplySpaceThemeForTesting(AtriumBrowserWindow* window,
                                 const std::string& space_id,
                                 int transition_ms) {
    ApplySpaceTheme(window, space_id, transition_ms);
  }
  void PreviewThemeForTesting(const std::string& space_id,
                              const std::string& theme_json) {
    PreviewTheme(space_id, theme_json);
  }
  bool theme_animator_running_for_testing() const {
    return theme_animator_.running();
  }

 private:
  std::vector<CommandBarSpace> SpacesWithBindings(const std::string& model);
  std::vector<CommandBarTab> TabsByRecency(const std::string& model);

  // Looks up space_id's stored theme and paints it (or clears the backdrop
  // for a space with none), then reconfigures theme_animator_ for it.
  void ApplySpaceTheme(AtriumBrowserWindow* window,
                       const std::string& space_id,
                       int transition_ms);

  // Paints theme_json on the active window without touching the themes
  // store - Theme Studio's live-scrub preview.
  void PreviewTheme(const std::string& space_id, const std::string& theme_json);

  // Moves the window to space_id if it is showing another one, so a theme
  // edit lands on the space it names. False for an id the core has dropped.
  bool ShowSpaceForThemeEdit(const std::string& space_id);

  // Shared by ApplySpaceTheme, PreviewTheme and SelectSpace (which already
  // has the active space's theme resolved in its own sidebar-model JSON and
  // so skips the by-id lookup). theme_json null clears the backdrop.
  // reset_phase false for a preview: a clock restart per frame pins it at 0.
  void ApplyThemeSpec(AtriumBrowserWindow* window,
                      const std::string* theme_json,
                      int transition_ms,
                      bool reset_phase);

  // theme_animator_'s Tick callback: re-rasters the window ApplyThemeSpec
  // last painted, using its cached spec, grain and dark-mode reading.
  void OnThemeAnimatorTick(double ambient_time, double warmth);

  // AtriumBrowserWindow::SetThemeVisibilityCallback's target - pauses
  // theme_animator_ while the themed window is not on screen.
  void OnThemedWindowVisibilityChanged(bool visible);

  // Stops this controller's theme timer and forgets the window, for when
  // another controller themes that window.
  void ReleaseThemedWindow();

  // One admitted row URL, asked of this profile's favicon database and then,
  // on a miss, of the larger icon types the same database holds.
  void StartFaviconLookup(const std::string& url);
  void StartLargeIconLookup(const std::string& url);

  // Drops the fetch when a 30x aims it somewhere the fetch may not go. Takes
  // page_url by value: erasing ico_loaders_ below frees the loader that owns
  // the bound string this callback runs on.
  void OnIcoRedirect(std::string page_url,
                     const GURL& url_before_redirect,
                     const net::RedirectInfo& redirect_info,
                     const network::mojom::URLResponseHead& response_head,
                     std::vector<std::string>* removed_headers);
  void OnIcoFetched(std::string page_url,
                    std::string icon_url,
                    std::optional<std::string> body);
  void OnIcoDecoded(std::string page_url,
                    std::string icon_url,
                    const SkBitmap& bitmap);
  void OnPageFetched(std::string page_url, std::optional<std::string> body);

  void StoreRowIcon(const std::string& url, std::vector<uint8_t> png);
  // Frees the URL's queue slot and starts whatever the slot lets through.
  void FinishFaviconLookup(const std::string& url);
  // Gives slots back without starting anything, for lookups that were
  // cancelled or can no longer run.
  void ReleaseFaviconSlots(const std::vector<std::string>& urls);

  // RowIconLookup::Delegate: tiers 3-4's I/O, decided by row_icon_lookup_.
  void FetchIcon(const std::string& page_url,
                const GURL& icon_url,
                base::TimeDelta budget) override;
  void FetchPage(const std::string& page_url,
                const GURL& origin_root,
                base::TimeDelta budget) override;
  void CancelFetch(const std::string& page_url) override;
  void DecodeAndStore(const std::string& page_url,
                      const GURL& icon_url,
                      std::string bytes) override;
  void FinishRow(const std::string& page_url) override;
  void AllowRetry(const std::string& key, const GURL& url) override;

 public:

  // opens the palette telling Swift WHY (Cmd+L prefills and
  // selects the current tab's URL; Cmd+T stays an empty new-tab open).
  //
  // The reason cannot be a Show() parameter: the show path runs through
  // AtriumBrowserWindow, whose signature is not this task's to widen. So it
  // is latched here for the duration of ONE synchronous show - set, then
  // OpenCommandBar() drives the existing path down to CommandBarWidget::
  // Show(), which takes it back out via TakePendingCommandBarOpenReason()
  // and prefills the field. The latch is per-controller (so two windows can
  // be open for different reasons) and is cleared unconditionally when
  // this returns, so a window with no palette widget to show cannot leave a
  // stale reason for the next ordinary open to inherit.
  void OpenCommandBarWithReason(CommandBarOpenReason reason);

  // Reads and clears the latch above. Returns kNewTab when nothing set one -
  // i.e. for every older open path, unchanged.
  CommandBarOpenReason TakePendingCommandBarOpenReason();

  // Cmd+T toggle: shows the palette if hidden, closes it if shown, creating
  // no tab (routes to AtriumBrowserWindow::ToggleCommandBar).
  void ToggleCommandBar();

  // toggleOverview: shows the Overview surface over the
  // content pane if hidden, closes it (revealing whatever tab was already
  // active underneath) if shown. Routes to AtriumBrowserWindow::
  // ToggleOverview - same shape as ToggleCommandBar.
  void ToggleOverview();

  // Routes to AtriumBrowserWindow::ToggleAiPanel.
  void ToggleAiPanel();

  // Opens a little window on THIS controller's profile - the
  // profile of the window the user pressed the hotkey in. The instance form
  // exists because ExecuteNativeHotkeyAction holds a controller and nothing
  // public hands out its Browser or Profile; CreateWindowSlot has the same
  // shape for the same reason, resolving from an invoking browser rather than
  // asking callers for a profile.
  void OpenLittleWindow();

  // Cmd+Shift+C - copies the displayed tab's link to the clipboard
  // and flashes a themed confirmation. Returns false (nothing on screen /
  // torn-down controller) so the accelerator intercept can fall through.
  // `format` selects the Markdown/Quote variants (Opt+Shift+Cmd+C /
  // Ctrl+Shift+Cmd+C) - see LinkCopyFormat's doc.
  bool CopyCurrentPageLink(LinkCopyFormat format = LinkCopyFormat::kPlain);

  // Cmd+Shift+D - toggles this window's Developer Mode URL
  // toolbar. Returns false on a torn-down controller, same posture as
  // CopyCurrentPageLink, so the accelerator intercept can fall through.
  bool ToggleDevBar();

  // The four reload strengths, each forwarded to the sidebar reload button's
  // own handler so the key and the click cannot drift apart. False on a
  // torn-down controller, so the chord falls through to its stock meaning.
  bool ReloadShownTab();
  bool HardReloadShownTab();
  bool ReloadShownTabClearingCache();
  bool ReloadShownTabClearingSiteData();

  // Cmd+D - moves the shown tab between the sidebar's pinned and
  // regular lists. False when nothing moved, so the key falls through to
  // Bookmark This Tab.
  bool TogglePinShownTab();

  // Cmd+1..9 - opens the active space's favourite tile in that slot.
  // False when there is no tile there, so the key falls through.
  bool ActivateFavoriteInSlot(int ordinal);

  // openArchive: shows the Overview surface with the Archived section
  // selected.
  void OpenArchive();

  // clearTabs: archives the active space's ungrouped tabs, as the sidebar's
  // Clear icon does. False when there was nothing to archive, so the key
  // falls through.
  bool ClearTabs();

  // closeSplitPane: dissolves the active split, keeping whichever pane is
  // active. False when nothing is split, so the key falls through.
  bool CloseSplitPane();

  // toggleSplitOrientation: flips the active split between side-by-side and
  // stacked, resetting to the default 0.5 ratio like every other EnterSplit
  // call site. False when nothing is split.
  bool ToggleSplitOrientation();

  // toggleDeveloperMode: flips the extensions Developer Mode pref for this
  // profile, distinct from the devbar's ToggleDevBar above.
  bool ToggleDeveloperMode();

  // Stops this controller's warm AutocompleteController, the same effect as the
  // command_bar_stop op (CommandBarStop below), but callable directly from
  // C++ - CommandBarWidget::Close() needs this for its close-on-deactivate
  // path (Cmd+Tab away, clicking another app, or any dismissal that doesn't
  // go through Swift's onEscape/onClose), which previously never reached
  // command_bar_bridge_ at all since it never round-trips through the
  // Swift-initiated command_bar_dismiss op. Public, non-static, mirroring
  // OpenCommandBar's shape - CommandBarWidget only knows this controller's
  // the controller, not command_bar_bridge_ itself, and command_bar_bridge_ is
  // private. A no-op if this controller never built one (command_bar_
  // connected() was false at registration) or its Browser already tore down.
  void StopCommandBar();

  // re-pushes the little window's top strip after its tab
  // navigates or is retitled. A no-op on a regular window.
  void PushFoyerSnapshot();

  // the three things the HTML palette asks for, each arriving as a
  // chrome.send on CommandBarWidget. The widget holds no bridge of its own -
  // the warm AutocompleteController is per (window, profile) and lives here.
  //
  // Every index comes from the renderer, so each of these treats an unknown
  // one as a logged no-op rather than trusting it.
  void QueryCommandBar(const std::string& text);
  void AcceptCommandBarMatch(int index);

  // A favourite row's accept. By url, because a favourite is not in the
  // AutocompleteResult and has no index to resolve.
  void AcceptCommandBarFavorite(const std::string& url);

  // Opens the engine a keyword names, for an Enter on one of the palette's
  // own search rows. Those carry no AutocompleteResult position to accept.
  void AcceptCommandBarEngine(const std::string& keyword);
  void DeleteCommandBarMatch(int index);

  // Peek mode: shift+click intercept target
  // (Browser::OpenURLFromTab's Q1 hook, browser.cc). Closes any peek already
  // showing on this controller's window (no stacking - Global Constraints),
  // creates a standalone content::WebContents on the SAME profile as this
  // controller's Browser (deliberate design call, not OTR), attaches a PeekEventForwarder, loads `url` with `referrer`,
  // and shows it in this window's peek widget. A no-op if this controller's
  // Browser already tore down. `click_origin_screen` is the cursor's screen position at click time
  // (MaybeOpenPeek captures it via display::Screen::GetCursorScreenPoint()
  // - see that call site's doc) - forwarded to the widget so its open
  // animation can zoom in from the clicked link instead of the card center.
  // `source` is the tab the link was in, the active tab when null. False when
  // MayOpenPeek refused, so the caller still has to open the link.
  bool OpenPeek(const GURL& url,
               const content::Referrer& referrer,
               const gfx::Point& click_origin_screen,
               content::WebContents* source = nullptr);

  // Dismisses the current peek (if any): hides the widget and destroys the
  // owned WebContents - the single path every dismiss trigger (Escape, the
  // focus-bounce guard, a second OpenPeek's no-stacking close) funnels
  // through, so there is exactly one place that decides "a peek dismiss
  // happened" (see PeekWidget::SetDismissCallback's doc). Public since
  // PeekWidget's dismiss callback (bound at OpenPeek time) calls it
  // directly. A no-op if no peek is currently open.
  void ClosePeek();

  // Closes `peek` where it is, on the card or parked. The card's dismiss is
  // bound to one peek, so a late close cannot take down the one that replaced
  // it.
  void ClosePeekContents(base::WeakPtr<content::WebContents> peek);

  // Adopts a popup a page opened with window.open into this window's card,
  // closing any peek already open. Taking ownership of the contents content
  // already built is what keeps its opener, profile and storage partition.

  // Returns null once the card holds it, or `contents` back when this window
  // has no card to put it in - the caller must not be left holding nothing.
  // `target_url`: the contents still reads about:blank here, the renderer
  // starts that load only after the window is shown.
  std::unique_ptr<content::WebContents> AdoptPopupAsPeek(
      std::unique_ptr<content::WebContents> contents,
      const gfx::Point& click_origin_screen,
      const GURL& target_url);

  // Pushes the peeked page's host and security state to the card's origin
  // chip. A no-op for a peek that shows no chip.
  void UpdatePeekOriginChip();
  void PushPeekOriginChipFor(const GURL& url);

  // Peek mode: promotes the current peek's WebContents into this
  // controller's own tab strip as a real, active tab, immediately after the
  // tab the peek was OPENED from (peek_source_tab_id_, not whatever happens
  // to be active_index() at promote time - see that member's doc for why)
  // - detaches the WebView binding first (Global Constraints: "detach
  // WebView, then InsertWebContentsAt"), so the SAME live WebContents keeps
  // rendering with no reload. The widget then closes instantly (no dismiss
  // animation - the content visually "becomes" the tab; see
  // PeekWidget::Close's `animate` doc). After insertion, TabStripModel's
  // normal kInserted notification announces the tab to Swift exactly like
  // any other new tab - no special-casing needed here (see the retired ABI header's
  // PEEK doc block). A no-op if no peek is currently open or this
  // controller's Browser already tore down.
  // Two steps: PeekWidget::Promote takes a still of the card, then
  // FinishPromotePeek inserts the tab and the still grows over it.
  void PromotePeek();
  void FinishPromotePeek();

  // the peek control cluster's split button. Same handoff as
  // PromotePeek - the peeked WebContents leaves the card and becomes a real
  // tab - except the tab is inserted WITHOUT taking active, and the window
  // then enters split with it as the SECONDARY pane. The primary is
  // whatever is already active, which is the tab the peek was opened FROM
  // (opening a peek does not change the active tab), so the result reads as
  // "split what I was reading with what I peeked". Side by side at an even
  // ratio; the existing Split View submenu re-orients it
  // afterwards. A no-op if no peek is open, this controller's Browser tore
  // down, or the window is already split - EnterSplit itself declines the
  // last case.
  void SplitPeek();

  // Forwards to AtriumBrowserWindow::HidePeekLoadingPlaceholder -
  // called by PeekEventForwarder::DidFirstVisuallyNonEmptyPaint. Public for
  // the same reason ClosePeek is (a nested-class callback reaching out).
  void HidePeekLoadingPlaceholder();

  // Caches the peeked page's favicon and badges the source favourite's tile.
  void ShowPeekFavicon(const gfx::Image& image);

  content::WebContents* PeekContentsForTesting() const {
    return peek_contents_.get();
  }
  // Whether `peek` is this controller's, on the card or parked.
  bool HoldsPeekForTesting(const content::WebContents* peek) const;
  int FoyerPushesForTesting() const { return foyer_pushes_for_testing_; }

  BrowserController(const BrowserController&) = delete;
  BrowserController& operator=(const BrowserController&) = delete;

  // consumed by AtriumBrowserWindow::Show() the first time an
  // adopted-Browser controller's window (Dock New Window, relaunch restore,
  // promoted popups - the existing-Browser ctor) actually becomes visible.
  // No-op for every other controller; only the existing-Browser ctor sets
  // the pending flag this checks.
  void NotifyShownIfAdopted();

  // this window's current edit-focus kind and the tab it describes,
  // read SYNCHRONOUSLY by AtriumCommandDispatcherDelegate at key-handling time
  // (a key event cannot await anything). Never kUnknown from here - C++ owns
  // the page half natively, so it always has a positive answer; kUnknown
  // exists for the Swift cache's benefit only. The tab id is the strip's
  // CURRENT active tab rather than a remembered one, so on this route the
  // snapshot is stale-by-construction never: it is read in the same
  // synchronous turn it is derived.
  // Not const: the palette half is read live off the widget, and
  // AtriumBrowserWindow's accessor is non-const.
  EditFocusKind edit_focus_kind();
  uint64_t edit_focus_tab_id() const;

  // re-push after the command bar widget's visibility changed. The
  // palette counts as an Atrium text surface (edit_focus_kind() reads its
  // visibility live), so Swift's cache needs the echo even though the native
  // route does not. Public because CommandBarWidget::Show()/Close() call it -
  // they are the single funnel every open and close path reaches, including the
  // deactivation-guarded auto-close that never goes through this class.
  void PushEditFocusForCommandBar();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // The profile this controller's own Browser belongs to. An extension push
  // carries it so the sidebar can keep one strip per profile and swipe the
  // right one in. Never valid as a key for an OTR controller - see below.
  const std::string& ProfileId() const { return profile_id_; }

  // an off-the-record profile's GetBaseName() answers its parent's
  // path, so profile_id_ reads an incognito controller as its parent and
  // anything keyed by it would overwrite the parent's entry.
  bool IsOffTheRecordProfile() const;

 private:
  class TabEventForwarder;
  class PeekEventForwarder;
  class CollectionObserver;

  static std::unique_ptr<CollectionObserver>& CollectionObserverSlot();

  // Self-creating ctor (bootstrap slot 0, CreateWindowSlot): creates its own
  // Browser via Browser::Create(). When `seed_from_slot` is set, the new slot
  // shows the space that slot shows, unless the caller already chose one. A
  // slot with no space follows the document's activeSpaceID.
  // `little` (default false; CreateLittleWindow passes true)
  // skips the whole sidebar/sidebar-overlay/RegisterHostInSlot/command-bar/
  // extension-bridge block below - a little controller stays in Registry()
  // but never registers a Swift host and never builds those bridges/views.
  // Peek and the progress bar are NOT skipped (harmless, cheap, already
  // gated on their own group's connection - see the ctor body).
  BrowserController(Profile* profile,
                    bool show,
                    const std::string& startup_url,
                    uint64_t slot,
                    std::optional<uint64_t> seed_from_slot = std::nullopt,
                    bool little = false);

  // Attaching ctor (CollectionObserver's belt-and-suspenders catch-all):
  // wires up an ALREADY-CREATED Browser instead of
  // creating one. No `show`/`startup_url` - the Browser's own creation path
  // (not this class) owns showing its window and adding its first tab; this
  // just wires the sidebar, host registration, and tab observation onto it,
  // in the same synchronous call stack as OnBrowserCreated so the
  // RegisterHostInSlot-before-OnTabAdded2 ordering contract holds
  // structurally here too (see class comment).
  BrowserController(Browser* existing_browser, uint64_t slot);
  ~BrowserController() override;  // Never runs; process-lifetime instance.

  // Safe cast: the browser window factory creates an AtriumBrowserWindow for
  // every normal-type Browser while the bridge is connected.
  AtriumBrowserWindow* window();

  // The one hop the four reload strengths share.
  bool SendReloadSurfaceMessage(std::string_view name);

  // The tail both peek openers share: records the source tab and hands
  // peek_contents_ to the presenting window's card. False when no window took
  // it (see AtriumBrowserWindow::OpenPeek).
  bool ShowPeekWidget(const gfx::Point& click_origin_screen,
                      content::WebContents* source);

  // ClosePeek for the one caller that runs inside a blocked renderer call and
  // so must not destroy the outgoing page under it.
  void ClosePeekForReplacement();

  // Whether this controller's content is what its slot shows.
  bool ShowsInSlot();

  // A peek opens only from the tab on screen, so a link in a hidden space or
  // profile, or one that a switch took off screen, may not open one.
  bool MayOpenPeek(const content::WebContents* source);

  // A peek belongs to its source tab: the card shows this controller's peek
  // only while the pane shows that tab, and parks every other peek.
  void SyncPeekWithPane();
  void ParkCurrentPeek(AtriumBrowserWindow* window);
  void RestorePeek(AtriumBrowserWindow* window);

  // Returns the live WebContents for `tab_id` (and its index via `index` if
  // non-null), or null. Resolves by scanning the tab strip; never trusts the
  // pointer encoded in a possibly-stale id.
  content::WebContents* ResolveTab(uint64_t tab_id, int* index);

  // `to_visible_slot` is where the tab landed in the strip, or -1 when the
  // caller has no meaningful position - a replay of an adopted window's
  // existing tabs, or a discard swap, neither of which is the user putting a
  // tab somewhere.
  void AddTab(content::WebContents* contents, int to_visible_slot = -1);
  void RemoveTab(content::WebContents* contents);

  // Opens the row AddTab was owed, at the first commit. A tab the renderer
  // creates for a target=_blank link enters the strip BEFORE its navigation
  // starts, so AddTab reads an empty url, and the core refuses a row with no
  // url - leaving the tab live with no row and no binding, so no later title
  // or url update reached it either. `page` is the url the row opens on: the
  // one a navigation just started for, or the one that committed.
  void OpenDeferredRowForTab(content::WebContents* contents, const GURL& page);

  // called by a TabEventForwarder when the renderer reports the
  // focused element in `contents` changed - see the retired ABI header's EDIT FOCUS
  // doc block. Stores NOTHING: the answer is read live off the WebContents (see
  // edit_focus_kind()), so this exists purely to tell Swift its cached snapshot
  // is out of date. A background tab's focus move changes nothing a keystroke
  // could land in, so only the active tab's notification pushes.
  void OnPageEditableFocusChanged(content::WebContents* contents);

  // recomputes this window's edit-focus kind from the two halves it
  // holds (the active tab's page-editable record and atrium_text_focused_) and
  // pushes it to Swift, stamped with the active tab's id. The ONE place the
  // kind is computed, so the raw-NSEvent route below and Swift's route can
  // never disagree about what it currently is.
  void PushEditFocus();

  // called by a TabEventForwarder when `contents` commits a main-
  // frame navigation. Rebuilds the extension-action snapshot (per-tab badge/
  // visible state resets on navigation) but only when `contents` is the active
  // tab - a background tab's navigation never changes the active-tab snapshot.
  // A no-op when the extension group isn't connected (extension_bridge_ null).
  void OnActiveTabNavigated(content::WebContents* contents);

  // A kRemoved reason where TabRemoveReasonUtils::WillDeleteWebContents() is
  // false (the tab id/WebContents survives - see tab_removed_reason.h):
  // kInsertedIntoOtherTabStrip (move to another Atrium window) or
  // kInsertedIntoSidePanel (Atrium wires no side-panel UI; unreachable in
  // practice, handled conservatively). Erases this controller's forwarder
  // (the WebContents is leaving this strip) WITHOUT calling OnTabClosed -
  // that would delete the tab's persisted SpaceTab, the exact data-loss bug
  // to avoid. Only kInsertedIntoOtherTabStrip
  // arms the last-tab-out flag; a side-panel insert (if it ever happened)
  // is not an Atrium-window move and should let a resulting empty strip
  // close normally.
  void DetachTabForMove(content::WebContents* contents,
                        TabRemovedReason reason);

  // RouteSidebarDrop's ask-before-moving continuation, run once
  // the confirmation prompt answers. `contents` is a WeakPtr since the
  // async prompt can outlive the tab closing.
  void OnSidebarMoveConfirmed(base::WeakPtr<content::WebContents> contents,
                              BrowserController* target,
                              bool confirmed);

  // which profile owns the space SelectSpace last moved the rail
  // to. SelectSpace never checks profile_id_, so this can diverge from it.
  // Falls back to profile_id_ when the sidebar model can't be read.
  std::string ActiveSpaceProfileId() const;

  // the Profile the active space's engine should search with. A
  // space's profile owns no window of its own until it is visited, so this
  // falls back to the ProfileManager before it falls back to this window's.
  Profile* ActiveSpaceProfile();

  // The controller whose push last set this window's displayed strip, else
  // `this` - a store extension's id repeats across profiles, so membership
  // in `this`'s own model can't tell whose strip is on screen.
  BrowserController* ControllerOwningExtension(const std::string& extension_id);

  // Null after the tab strip (and thus the Browser) is torn down. The
  // BrowserController object itself is never destroyed even then (process
  // lifetime); ops handlers guard on this being non-null.
  raw_ptr<Browser> browser_ = nullptr;



  // The window-slot this controller belongs to; kDefaultSlot for the
  // bootstrap controller(s). Immutable for the controller's lifetime - a
  // controller never migrates slots.
  uint64_t slot_ = kDefaultSlot;

  // True only for a controller built via CreateLittleWindow.
  // Immutable for the controller's lifetime, same posture as slot_ - a
  // controller never becomes little (or stops being little) after
  // construction. The ctor's own body is the only reader today (it gates the
  // sidebar/spaces/command-bar/extension-bridge block); a later stage's
  // promote/external-link/hotkey logic is expected to read it too.
  bool little_ = false;

  // This controller's profile id (Profile::GetBaseName()), cached so
  // ControllerFor(slot, profile) and CreateWindowSlot's "same profile as the
  // invoking window" lookup work even after browser_ is nulled.
  std::string profile_id_;

  // Per-tab URL/title observers, keyed by tab id.
  std::map<uint64_t, std::unique_ptr<TabEventForwarder>> forwarders_;

  // Tabs AddTab owes a row - see OpenDeferredRowForTab. Never holds a little
  // window's or an incognito window's tabs: AddTab returns before recording
  // one, which is what keeps those out of the space model.
  std::set<uint64_t> tabs_owed_a_row_;
  int foyer_pushes_for_testing_ = 0;

  // Whether one of Swift's own text surfaces (command
  // bar query field, sidebar rename editor) owns the keyboard in THIS window.
  // Only Swift can see it, so it arrives through
  // the set_atrium_text_focus op. Window-scoped, not process-wide: two
  // windows can have different answers at once.
  bool atrium_text_focused_ = false;

  // See ConsumeMoveOutEmptyFlag(). Set in OnTabStripModelChanged, consumed
  // (and reset) by AtriumBrowserWindow::OnWindowCloseRequested; never left
  // set across unrelated events - see that method's implementation.
  bool pending_move_out_empty_ = false;

  // The favourite whose tab is being inserted, claimed by the first insert
  // that follows. A WebContents cannot be marked before it is made, and an
  // unmarked one draws as an ordinary row.
  std::string pending_favorite_key_;

  AtriumFavoriteParking favorite_parking_;

  // The tab ParkFavoriteTab is detaching. Its removal reason says "moved", but
  // nothing receives it, so kRemoved runs the close path for it.
  raw_ptr<content::WebContents> parking_contents_ = nullptr;

  // Which tab each space was last showing, keyed by rail index. Session-only:
  // nothing stamps a tab's lastActiveAt, and a restore already picks the most
  // recent (launch.rs startup_tab_of).
  std::map<int, uint64_t> last_shown_tab_by_space_;

  // true only for a controller built via the existing-Browser
  // (adopt) ctor, until its window's first real hidden->visible transition
  // consumes it via NotifyShownIfAdopted() - fired from whichever of
  // AtriumBrowserWindow::Show()/ShowInactive() gets there first (see both
  // methods' comments; ActivateWindow's kFirstActivation branch
  // calls ShowInactive() then Show(), so it's ShowInactive() that consumes
  // this in that path). That ctor cannot fire the shown notification itself
  // - see its doc: "the Browser's own creation path (not this class) owns
  // showing its window" - so this flag carries the intent forward to
  // whichever call site actually knows the window is on screen.
  bool pending_adopt_shown_ = false;

  // M4: warm AutocompleteController + search-engine push for this host's
  // profile (see atrium_autocomplete_bridge.h). Built at registration for
  // every non-little window - it used to be gated on the
  // dylib exporting the command-bar group, which since the cut is never, and
  // the HTML palette needs it on every window that can open one. Reset
  // (stopping the controller and dropping the TemplateURLService observer)
  // in OnTabStripModelDestroyed, alongside sidebar_view_.
  std::unique_ptr<AutocompleteBridge> command_bar_bridge_;

  // the one-shot open-reason latch - see
  // OpenCommandBarWithReason. Only ever non-default INSIDE that one
  // synchronous call, so no state outlives a show.
  CommandBarOpenReason pending_command_bar_open_reason_ =
      CommandBarOpenReason::kNewTab;

  // observes this host's profile ToolbarActionsModel and streams the
  // active-tab extension-action snapshot + icons to Swift. Built at
  // registration for every non-little window, so every
  // extension op above reaches a real bridge. Reset (dropping the observer) in
  // OnTabStripModelDestroyed, alongside command_bar_bridge_.
  std::unique_ptr<ExtensionActionBridge> extension_bridge_;


  // Hosts at most one open extension popup. Explicitly CloseNow()'d in
  // OnTabStripModelDestroyed BEFORE browser_ is nulled - the popup holds a
  // raw_ptr<Browser>, so that ordering is load-bearing.
  std::unique_ptr<ExtensionPopupHost> extension_popup_host_;


  // Peek mode: the standalone peek WebContents (never inserted into
  // any TabStripModel) and its event forwarder - owned here for promotion
  // (PeekWidget's WebView binds it via SetWebContents, never
  // SetOwnedWebContents, so ownership can move out cleanly on a future
  // promote). Null whenever no peek is open.
  std::unique_ptr<content::WebContents> peek_contents_;
  std::unique_ptr<PeekEventForwarder> peek_forwarder_;

  // Set only for an adopted popup. A link-click peek shows the page the user
  // just clicked to, so its origin is not the surprise a popup's is.
  bool peek_shows_origin_chip_ = false;

  // Peeks whose source tab is off screen, most recently shown first. Each
  // tab keeps its own. The cap counts every controller in this slot's window,
  // and past it the one shown longest ago closes.
  struct ParkedPeek {
    std::unique_ptr<content::WebContents> contents;
    uint64_t source_tab_id = 0;
    bool shows_origin_chip = false;
    uint64_t parked_at = 0;
  };
  static constexpr size_t kMaxParkedPeeks = 4;
  std::vector<ParkedPeek> parked_peeks_;

  // Peek mode: the TabId (see TabId()'s doc above -
  // a monotonic id, not the WebContents pointer value) of the tab
  // that was ACTIVE when OpenPeek() captured it - the
  // "source tab" the promote should land adjacent to. Captured once, at
  // OpenPeek time, deliberately NOT re-read from active_index() at promote
  // time: the peek widget takes key focus on Show() (steals it from the
  // parent's web content), so activation drift between open and promote is
  // unlikely in practice, but it is not structurally guaranteed - e.g. a
  // second window on the same profile could programmatically activate a
  // different tab in THIS window's strip via the activate_tab op while the
  // peek is showing, with no window (de)activation to notice. PromotePeek()
  // resolves this id back to a live index via ResolveTab() (never trusts
  // the pointer - same contract as every other tab_id use in this file); 0
  // (default - no active tab at OpenPeek time, or ResolveTab finds nothing,
  // e.g. the source tab closed/moved away before promote) falls back to
  // active_index() + 1, clamped by TabStripModel::InsertTabAtImpl's own
  // ConstrainInsertionIndex - covers both that case and the drift case
  // above landing on a tab this controller can no longer resolve.
  uint64_t peek_source_tab_id_ = 0;



  // Favicon lookup (request_favicon_for_url): required lifetime anchor for
  // every outstanding favicon::FaviconService::GetRawFaviconForPageURL
  // call this controller has issued - GetRawFaviconForPageURL's contract
  // requires the tracker to outlive the request. One tracker multiplexes
  // any number of concurrent lookups. The lookup callback itself only
  // touches the captured url string and the process-wide UIBridge
  // singleton (never `this`/`browser_`), so no explicit cancellation is
  // needed even though this controller is never destroyed (process
  // lifetime, see the class comment).
  base::CancelableTaskTracker favicon_lookup_tracker_;

  // Whether a sidebar push is already queued for a resolved
  // favourite icon. Each icon used to push on its own, and a push REBUILDS the
  // whole sidebar - so a profile with several favourites rebuilt it several
  // times in one turn, recreating every row's and tile's <img>. A fresh <img>
  // carries no `hasicon` class until it loads, so the placeholder tint shows
  // for a frame each time, which is the blinking that was reported and the
  // "icons behind the favicons" alongside it.
  //
  // One flag rather than a timer: the lookups resolve in the same turn, so
  // collapsing them to a single posted push is enough and adds no delay a user
  // could see.
  bool favicon_push_queued_ = false;

  // The /favicon.ico fetches in flight, by page URL. Owned here rather than
  // by their completion callback so a redirect callback can cancel one: a
  // SimpleURLLoader only stops following a 30x by being deleted.
  std::map<std::string, std::unique_ptr<network::SimpleURLLoader>>
      ico_loaders_;

  // Tiers 3-4's decisions for every row in flight, one deadline each.
  RowIconLookup row_icon_lookup_{this};

  // the profile whose shared backfill queue this controller uses,
  // latched while browser_ is live so teardown can still free its slots.
  // An identity token, compared and passed on, never dereferenced.
  uintptr_t favicon_backfill_key_ = 0;

  // The URLs THIS controller has in flight. The queue is shared by every
  // window of the profile, so a teardown here must free only its own slots -
  // clearing the whole queue would unbound the other windows' lookups.
  std::set<std::string> favicon_in_flight_;

  // The window OnThemeAnimatorTick re-rasters onto - a WeakPtr since it can
  // be destroyed out from under a still-running timer.
  base::WeakPtr<AtriumBrowserWindow> themed_window_;
  std::string themed_theme_json_;
  float themed_grain_ = 0.f;

  // The flags the animator is configured from, read off the spec while
  // ApplyThemeSpec and BeginSpaceScrub raster it.
  struct ThemeFlags {
    bool night_shift = false;
    bool ambient = false;
    float grain = 0.f;
  };
  // Rasters theme_json for `window` into *rgba (null for no theme, a spec
  // that does not parse, or an incognito window). The caller frees *rgba
  // with atrium_core_bytes_free(*rgba, *len).
  ThemeFlags RasterThemeSpec(AtriumBrowserWindow* window,
                             const std::string* theme_json,
                             uint8_t** rgba,
                             size_t* len);

  struct SpaceScrub {
    // The window the layer was staged on. SidebarWindow() can move to
    // another presenter mid-gesture, and the session must close where it
    // opened.
    base::WeakPtr<AtriumBrowserWindow> window;
    int target_index = 0;
    // Both raw, the way the snapshot names them: SetBackdropTintLive applies
    // the incognito and glass choosers itself.
    SkColor from_tint = 0;
    SkColor to_tint = 0;
    // The incoming space's spec, for the bookkeeping the commit owes.
    std::optional<std::string> theme_json;
  };
  std::optional<SpaceScrub> space_scrub_;
  ThemeAnimator theme_animator_{base::BindRepeating(
      &BrowserController::OnThemeAnimatorTick, base::Unretained(this))};

  base::WeakPtrFactory<BrowserController> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_CONTROLLER_H_
