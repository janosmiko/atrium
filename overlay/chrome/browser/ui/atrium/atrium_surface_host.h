// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_HOST_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_HOST_H_

#include <string_view>

#include "base/values.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace atrium {

// Every chrome.send name any route may use.
//
// This list is not an allowlist we chose to keep - it is mandatory.
// content::WebUIImpl::ProcessWebUIMessage NOTREACHEDs on a message with no
// registered callback (content/browser/webui/web_ui_impl.cc), which is FATAL:
// an unregistered name takes down the BROWSER process, not the renderer, so
// the crash observer cannot recover it either. Adding a chrome.send to a route
// without adding it here is a guaranteed startup crash the moment that route
// loads - found exactly that way by "sidebarReady".
inline constexpr const char* kSurfaceMessages[] = {
    // Shared.
    "atriumReady",
    // The vim surface's resolved verb. Its route has existed since the surface
    // landed, but the name never reached this list, so the first verb a user
    // typed crashed the browser process.
    "vimVerb",
    // Quick menu route.
    "extensionPinToggled",
    // Sent by both the strip chips and the quick menu's tiles.
    "pillActionClicked",
    // Right-click on a strip chip or a quick menu tile: opens Chromium's own
    // extension context menu. A rail-pane copy's chip never sends it.
    "extensionChipContextMenu",
    // Sent by the sidebar and the floating bar: [x, y, w, h] of the puzzle
    // button in the sending document's own client coordinates.
    "quickMenuToggled",
    "quickMenuReady",
    "quickMenuManage",
    "quickMenuStore",
    "quickMenuSiteInfo",
    "quickMenuShare",
    "quickMenuCopy",
    // Sidebar route.
    "sidebarReady",
    "sidebarRowsDrawn",
    "sidebarFavoriteCount",
    "sidebarFavoriteClicked",
    "sidebarTabClicked",
    "sidebarColdTabClicked",
    "sidebarTabClosed",
    // The sidebar's media controllers.
    "mediaPlayPause",
    "mediaPrev",
    "mediaNext",
    "mediaMuteToggled",
    "mediaBackToTab",
    "mediaClosed",
    "mediaPictureInPicture",
    "mediaVolume",
    "sidebarFolderToggled",
    "sidebarSpaceSelected",
    // a swipe's backdrop scrub. [spaceIndex] stages the neighbour's
    // theme, [weight] blends it in per frame, [commit] promotes or drops it.
    "sidebarSpaceScrubBegan",
    "sidebarSpaceScrubBlend",
    "sidebarSpaceScrubEnded",
    // [strength], 'soft' or 'plain': a trackpad tap during a sidebar swipe.
    "sidebarSwipeHaptic",
    // The space menu's Change Theme, [spaceIndex] plus the dot's client rect.
    // Opens the theme studio in its own window above that dot.
    "sidebarSpaceThemeRequested",
    // the leftmost space dot, reaching Overview from
    // any space.
    "sidebarOverviewToggled",
    "sidebarShelfToastClicked",
    // The space menu's Downloads, Archived Tabs, Snoozed Tabs, Shelf and Notes:
    // [section], the Overview section to open at.
    "sidebarOverviewSectionOpened",
    // The space menu's Change Space Name: [spaceIndex, name].
    "sidebarSpaceRenamed",
    // The space menu's Change Space Icon: [spaceIndex, icon]. An empty icon
    // clears it.
    "sidebarSpaceIconSet",
    // Opens the space icon picker widget: [spaceIndex, value, x, y, w, h] of
    // the dot in the sidebar document's own client coordinates.
    "sidebarSpaceIconPickerOpened",
    // Space icon picker route (its own widget, not the sidebar).
    "spaceIconPickerReady",
    // the sidebar's Courtyard menu entry.
    "sidebarCourtyardOpened",
    "sidebarNewTab",
    // A click on the sidebar's address row, [left, top, width, height] in
    // the document's own client coordinates.
    "sidebarAddressRowClicked",
    // A hover/hold/modifier hint, routed to HintOverlayWidget so it can draw
    // above the content card: [text, keys, left, top, width, height, ttlMs].
    "sidebarHintShow",
    "sidebarHintHide",
    // a press navigates, a hold opens the history scrubber.
    "sidebarBackPressed",
    "sidebarBackHeld",
    // the pinned chrome row's other three controls.
    "sidebarForwardPressed",
    "sidebarReloadPressed",
    "sidebarToggleRequested",
    // the same three controls under a held modifier. Option is the
    // wider function, Cmd+Shift the widest.
    "sidebarBackInNewTab",
    "sidebarBackToStart",
    "sidebarForwardInNewTab",
    "sidebarForwardToEnd",
    "sidebarReloadHard",
    "sidebarReloadClearingCache",
    "sidebarReloadClearingSite",
    // The sidebar telling the window a menu is up, so the mouse-down monitor
    // pushes a dismiss only while one is.
    "sidebarMenuOpened",
    "sidebarMenuClosed",
    "sidebarOverlayWidthCommit",
    "sidebarOverlayWidthLive",
    "sidebarWindowClose",
    "sidebarWindowMinimize",
    "sidebarWindowZoom",
    // The scrubber's release, [url, index].
    "sidebarTimelineCommit",
    "sidebarPinnedToggled",
    "sidebarTabReordered",
    "sidebarPinnedReordered",
    "sidebarTabPinned",
    "sidebarTabUnpinned",
    "sidebarPinnedTabRemoved",
    // The x on a cold regular row, [visible_index]. A cold row has no tab id
    // for sidebarTabClosed to name.
    "sidebarColdTabRemoved",
    // the context menu's Snooze entry, [visible_index].
    "sidebarTabSnoozed",
    // pinned rows only. Creates a Doorway record, never
    // touches the pinned tab.
    "sidebarTabAddedToDock",
    "sidebarRowFavorited",
    "sidebarFavoriteMovedOut",
    "sidebarFavoriteGrouped",
    "sidebarTabMovedToSpace",
    // A split row's panes, [fromSpace, [slots], toSpace, toSlot].
    "sidebarRunMoved",
    "sidebarFavoriteReordered",
    "sidebarTabGrouped",
    "sidebarTabUngrouped",
    "sidebarFolderMemberReordered",
    "sidebarFolderPinned",
    // the four folder verbs. Creation reports the new id back
    // to the surface. The other three name the existing folder they act on.
    "sidebarFolderCreated",
    "sidebarFolderRenamed",
    "sidebarFolderDeleted",
    "sidebarFolderIconSet",
    // the fifth folder verb, a folder reordered among its siblings.
    "sidebarFolderReordered",
    // A row dropped on an edge quarter of another row.
    // [draggedTabId, targetTabId, "left"|"right"|"top"|"bottom"].
    "sidebarTabSplit",
    // a drag released outside the sidebar with no drop kind
    // resolved. Handled now, by OnSurface_SidebarTabDragReleased, with its own
    // browsertest.
    "sidebarTabDragReleased",
    // the same drag, still in flight. [tabId, screenX, screenY],
    // sent while the pointer moves so another window can show where the drop
    // would land. Separate from the release above because a preview must not
    // be able to move a tab, whatever it is sent.
    "sidebarTabDragMoved",
    // Put on Shelf, from the row context menu or a drag onto
    // the sidebar's own edge drop zone. [tabId].
    "sidebarTabShelved",
    "sidebarTabArchived",
    "sidebarTabDuplicated",
    "sidebarTabsArchivedBelow",
    "sidebarTabsCleared",
    // Put on Shelf from a favourite tile, which has no tab id
    // to resolve - [url, title] instead.
    "sidebarFavoriteShelved",
    "sidebarFavoriteRemoved",
    // a field inside the sidebar has taken or lost the keyboard.
    // The hotkey gate cannot see it the way it sees a page field, because the
    // sidebar is its own WebContents rather than the active tab.
    "sidebarTextFocusChanged",
    // a whole window - or one space slice - put back to a state
    // Rewind saved. The one sidebar verb that is not per-gesture: order,
    // selection and folder membership have no verb at all, so a restore built
    // out of the others would be a long, racy, half-lossy replay. Carries the
    // slice whole, as one JSON string.
    "sidebarRestoreSlice",
    // Foyer route (Little Atrium's top strip).
    "foyerReady",
    "foyerMoveToSpace",
    // Settings route. The pane id travels rather than an
    // index, so a rail reordered later cannot change what a selection
    // means.
    "settingsReady",
    "settingsPaneSelected",
    // an edited settings object on its way to the writer. The
    // WHOLE object rather than one field, because the core refuses a partial
    // one rather than completing it from defaults.
    "settingsChanged",
    // An edited routing rule list, [routingJson]. Off settingsChanged - it is
    // routing.json's own object, not a settings.json key.
    "settingsRoutingRulesChanged",
    // The Import pane: preview an installed browser's own store, [source],
    // then copy what it finds into Atrium's, [source].
    "settingsImportPreview",
    "settingsImportRun",
    // An edited hotkeys map, [hotkeysJson]. Off settingsChanged for the same
    // reason - hotkeys.json is its own store, not a settings.json key.
    "settingsHotkeysChanged",
    // the custom endpoint API key, [apiKey]. Kept off
    // settingsChanged on purpose - the key must never enter settings.json.
    "settingsCustomEndpointApiKeyChanged",
    // the custom endpoint reachability probe, [url].
    "settingsCheckCustomEndpointConnection",
    // the settings window's own pin, [floating]. Off
    // settingsChanged because it edits the widget, not settings.json.
    "settingsAlwaysOnTopChanged",
    // About > Licenses. Carries no argument - the destination is
    // fixed in the browser rather than sent by the page, because a renderer
    // that could name the URL could name any of them.
    "settingsOpenCredits",
    // the Spaces pane's Auto-archive row, [spaceIndex, ttl].
    "settingsSpaceArchiveTtlChanged",
    // the Spaces pane's own rename, New Space and Open
    // actions.
    "settingsSpaceRenamed",
    "settingsSpaceCreated",
    "settingsSpaceOpened",
    // Theme Studio, [spaceId, spec] and [spaceId, spec | null]. A null spec
    // puts the stored theme back, which is how closing the studio unsaved
    // undoes its preview.
    "settingsThemePreview",
    "settingsThemeChanged",
    // the Profiles pane. Every name below that carries a profile
    // id is checked against ProfileBootstrap::IsSafeProfileId before it
    // touches a Chromium profile.
    "settingsProfileAdded",
    "settingsProfileRenamed",
    "settingsProfileDeleted",
    "settingsSpaceProfileChanged",
    "settingsProfileSearchEngineChanged",
    "settingsProfileHibernateChanged",
    "settingsProfileSuggestChanged",
    "settingsProfileDownloadDirPick",
    "settingsProfileOpenPage",
    "settingsSidebarHiddenChanged",
    "settingsMakeDefaultBrowser",
    // open the settings window on a named pane. Sent by the
    // sidebar's space menu and the command bar, so it carries no surface
    // prefix.
    "openSettingsPane",
    // Command bar route (the Cmd+T palette).
    "commandBarReady",
    "commandBarQuery",
    "commandBarAccept",
    "commandBarFavoriteAccept",
    "commandBarEngineAccept",
    "commandBarDismiss",
    "commandBarDeleteMatch",
    // Notes route: the per-space markdown pad.
    "notesReady",
    "notesBodyChanged",
    // a Marginalia card's note field, [markID, text].
    "marginaliaNoteChanged",
    // a card's quote clicked, [markID] - jump to its mark.
    "marginalMarkJumpRequested",
    // AI panel route: shell only, stub summarize/chat.
    "aiPanelReady",
    "aiPanelToggled",
    "aiPanelWidthChanged",
    // [tabId] the panel is attached to, empty for none, relayed to
    // the sidebar so it can mark that row.
    "aiPanelRowMark",
    // on-device summarize request/response round trip.
    "aiPanelSummarizeRequested",
    // [tabId, requestId, question], answered on the summarize reply path.
    "aiPanelChatRequested",
    // The command bar's Toggle AI panel verb.
    "aiPanelToggleRequested",
    // The command bar's Toggle side panel verb.
    "extensionSidePanelToggleRequested",
    // the Overview RAIL PANE reporting it drew. Sent by the
    // sidebar, not the overview route - the pane lives in the sidebar's
    // document now, and opens no window a scenario could count instead.
    "sidebarOverviewPaneRendered",
    // The rail entering or leaving that pane, [parked]. Separate from the line
    // above, which reports a settle and so never fires on the way out.
    "sidebarOverviewPaneActive",
    // Courtyard route: the handshake, and Escape/click-away
    // asking CourtyardWidget to close. Room clicks and room-to-room moves
    // reuse sidebarSpaceSelected/sidebarTabMovedToSpace above.
    "courtyardReady",
    "courtyardDismiss",
    // the plane reporting its zoom, so the pinch decision can
    // close only once the zoom has reached the floor.
    "courtyardZoom",
    // the canvas positions. Handled by CourtyardWidget, which
    // hands them to the browser window's core calls - the reorder, pin,
    // favourite and move drops reuse the sidebar names above.
    "courtyardRoomRect",
    "courtyardCardX",
    // a card click, by room, section and visible index. Not
    // sidebarTabClicked: only the active room's rows carry a live tab id.
    "courtyardCardActivated",
    // Downloads row actions, handled by OverviewController and
    // routed through OnSurface_OverviewSection.
    "downloadsShowInFinder",
    "downloadsRemove",
    "downloadsDeleteFromDisk",
    // Notes overview list actions, handled by OverviewController.
    // notesListReady: the section's lazy-mount catch-up push (its own
    // overviewReady push landed before this section registered its hook).
    "notesListReady",
    "notesListCreate",
    "notesListOpen",
    "notesListDismiss",
    "notesListDelete",
    // Shelf overview list actions, handled by OverviewController.
    // shelfListAdd's UI trigger is not built - registered now so its
    // round trip is reachable.
    "spacesListReady",
    "sidebarSpaceReordered",
    "sidebarSpaceCreated",
    "sidebarTabRenamed",
    "shelfListReady",  // Lazy-mount catch-up push, same shape as notesListReady.
    "shelfListAdd",
    "shelfListRemove",
    "shelfListOpen",
    // Archived/Snoozed overview list actions, handled by
    // OverviewController.
    "archivedListReady",  // Lazy-mount catch-up push.
    "archivedListRestore",
    "archivedListDelete",
    "snoozedListReady",  // Lazy-mount catch-up push.
    "snoozedListWake",
    "snoozedListDelete",
    // Mirror View's open verb, [url, profileId]. profileId is
    // either an id a space names or mirror.js's LOGGED_OUT sentinel (a
    // leading-space string) - see AtriumBrowserWindow::MirrorTabToProfile.
    "sidebarTabMirrored",
    // the /devbar route's handshake, same shape as foyerReady.
    "devBarReady",
    "devBarCopyUrl",
    // A click on the floating bar's URL, same payload as the sidebar row's.
    "devBarUrlClicked",
    // the pin, [bool]. It persists, so the window writes settings.
    "devBarPinToggled",
    // the bar's own hover hint, same payload as the sidebar's.
    // Its own name because the anchor rect is measured in the bar's document,
    // and the window translates out of whichever surface sent it.
    "devBarHintShow",
    "devBarHintHide",
    // the bar's resolved --ov-shadow-tight and its away slide, for
    // AtriumOverlayShadowLayer - the stylesheet owns both, C++ cannot.
    "devBarShadowSpec",
    // a press on a split bar, sent before its click so the
    // click acts on that bar's pane.
    "devBarPointerDown",
    // the bar's grip drags its tab. [], [sx, sy], [sx, sy].
    "devBarTabDragStarted",
    "devBarTabDragMoved",
    "devBarTabDragReleased",
    // the /permsheet route's handshake and its two verbs.
    "permissionSheetReady",
    "permissionSheetAllow",
    "permissionSheetBlock",
    // The /peekctl route's handshake and its one verb, [action].
    "peekControlsReady",
    "peekControl",
    // The /hint route's handshake.
    "hintReady",
    // the /toast route's handshake, and the stack's own size, which
    // the widget follows so it covers its cards rather than the window.
    "toastReady",
    "toastStackSized",
    "toastStackEmpty",
    // Each visible card's rect and resolved shadow spec, one
    // JSON string - only the page knows where a card sits, unlike the bar's
    // devBarShadowSpec above which carries the spec alone.
    "toastCardShadows",
    // the palette's capture verbs, each taking no arguments and
    // acting on the window. captureRegion opens the selection overlay first.
    "captureViewport",
    "captureRegion",
    "captureFullPage",
    // The /find route's handshake, its live query, and its three walk verbs.
    "findReady",
    "findQueryChanged",
    "findNext",
    "findPrevious",
    "findClose",
    // The /theme route's handshake, its header drag, and its close.
    "themeStudioReady",
    "themeStudioDragBy",
    "themeStudioClose",
};

// The native side of one chrome surface, reachable from its WebUI controller.
//
// chrome.send lands in AtriumUI, but the thing that needs to act on it is the
// widget that owns the WebContents - and the controller has no handle on it.
// This is the seam: a widget attaches itself to its own WebContents, and the
// controller looks it up.
//
// Attached via SupportsUserData rather than a static map keyed by WebContents*.
// A map would have to be pruned on teardown, and a stale entry there is a
// use-after-free waiting for the next surface with the same address; user data
// is destroyed with the WebContents by construction.
class SurfaceHost {
 public:
  virtual ~SurfaceHost();

  // `name` is the chrome.send message; `args` its argument list. Hosts ignore
  // names they do not know, so a renderer running ahead of a browser build is
  // a no-op HERE - but see kSurfaceMessages: it must still have been
  // registered, or the message never reaches this method at all.
  virtual void OnSurfaceMessage(std::string_view name,
                                const base::ListValue& args) = 0;

 protected:
  // Attaches `this` to `contents`. Does NOT take ownership: the host outlives
  // the attachment in every current case (the widget owns the WebContents), so
  // the stored pointer is cleared by Detach in the host's destructor.
  void AttachToContents(content::WebContents* contents);
  void DetachFromContents(content::WebContents* contents);
};

// Null when no host is attached - which is the normal case for any surface
// that has no native counterpart yet.
SurfaceHost* SurfaceHostFor(content::WebContents* contents);

// A surface's [x, y, width, height] argument, in screen coordinates. Anything
// else reads as an empty rect rather than as a rect at the origin, which is
// where an extension popup fed a bad one used to land.
gfx::Rect ScreenRectFromSurfaceArg(const base::Value& arg);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_HOST_H_
