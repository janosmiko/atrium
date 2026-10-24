// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_browser_controller.cc
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"

#include "base/json/json_reader.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/native_theme/native_theme.h"
#include "third_party/skia/include/core/SkColor.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "chrome/browser/ui/atrium/atrium_tab_cycle.h"
#include "chrome/browser/ui/atrium/atrium_tab_recency_bridge.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/numerics/safe_conversions.h"
#include "base/json/json_writer.h"
#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "base/containers/span.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/favicon/large_icon_service_factory.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/ui/atrium/atrium_auto_mini_player.h"
#include "chrome/browser/ui/atrium/atrium_autocomplete_bridge.h"
#include "chrome/browser/ui/atrium/atrium_hex_color.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_edge_reveal.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_capture.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_actions.h"
#include "chrome/browser/ui/atrium/atrium_devtools_bridge.h"
#include "chrome/browser/ui/atrium/atrium_drop_target_decision.h"
#include "chrome/browser/ui/atrium/atrium_extension_bridge.h"
#include "chrome/browser/ui/atrium/atrium_hibernate_bridge.h"
#include "chrome/browser/ui/atrium/atrium_little_marker.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "chrome/browser/ui/atrium/atrium_move_target_decision.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_drop_decision.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_move_execution_decision.h"
#include "chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/atrium/slot_presentation_table.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "chrome/browser/ui/unload_controller.h"
#include "chrome/common/pref_names.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon/core/large_icon_service.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/redirect_info.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/choice_made_location.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "components/sessions/content/session_tab_helper.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/bubble/bubble_border.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/focused_node_details.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/page_type.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace atrium {

namespace {

// Cocoa reference date, the clock every row stamp in the core is on.
double RowInstantNow() {
  return base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
}

// Every controller ever created, process-lifetime (entries are never
// removed - controllers are never destroyed, see the class comment).
std::vector<BrowserController*>& Registry() {
  static base::NoDestructor<std::vector<BrowserController*>> registry;
  return *registry;
}

// The per-slot Presenter/Visible state machine.
// "Visible" = whose CONTENT is shown in a slot; "presenter" = whose
// physical NSWindow is on screen for that slot. The two diverge exactly when
// ActivateWindow's in-window swap branch has fired for a slot. The decision
// logic lives in the views-free, unit-tested SlotPresentationTable; this file
// only maps ControllerId<->BrowserController* at the boundary and runs the
// Cocoa ops. Process-lifetime, single global instance (never destroyed, like
// the controllers it tracks).
SlotPresentationTable& PresentationTable() {
  static base::NoDestructor<SlotPresentationTable> table;
  return *table;
}

// The space each window slot shows, by id: creating or reordering a space
// shifts rail positions. A slot with no entry shows the document's
// activeSpaceID, which SelectSpace keeps as the last-used space for launch.
std::map<uint64_t, std::string>& SlotSpaces() {
  static base::NoDestructor<std::map<uint64_t, std::string>> spaces;
  return *spaces;
}

// The rail position of the space `slot` shows. A deleted space falls back to
// the document's, which the core already moved off it.
int SpaceIndexForSlot(uint64_t slot) {
  const auto it = SlotSpaces().find(slot);
  if (it != SlotSpaces().end()) {
    if (char* raw = atrium_core_space_slot_for_id(it->second.c_str())) {
      std::optional<base::Value> parsed =
          base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
      atrium_core_string_free(raw);
      if (parsed && parsed->is_dict()) {
        if (std::optional<int> index = parsed->GetDict().FindInt("index")) {
          return *index;
        }
      }
    }
  }
  return atrium_core_active_space_index();
}

void SetSlotSpace(uint64_t slot, int index) {
  if (char* id = atrium_core_space_id_at(index)) {
    SlotSpaces()[slot] = id;
    atrium_core_string_free(id);
  }
}

// Opaque identity <-> controller boundary: the table stores the controller's
// pointer bits and never dereferences them (tests pass small integers). A null
// controller maps to kInvalidControllerId (0), matching the old maps' default
// nullptr.
ControllerId IdOf(BrowserController* controller) {
  return reinterpret_cast<ControllerId>(controller);
}
BrowserController* ControllerOf(ControllerId id) {
  return reinterpret_cast<BrowserController*>(id);
}

// The next never-reused window-slot id CreateWindowSlot/CollectionObserver
// hand out. Starts at 1: kDefaultSlot (0) is reserved for the bootstrap
// window(s) and never allocated here.
uint64_t NextSlotId() {
  static uint64_t next_slot = 1;
  return next_slot++;
}

// process-wide flag `set_auto_pip_enabled` writes (same posture as
// NativeHotkeyMap - a single preference, not per-controller state). Default
// on, matching stock Chromium's own default-enabled
// media::kAutoPictureInPictureForVideoPlayback.
bool& AutoPipEnabledFlag() {
  static bool enabled = true;
  return enabled;
}

// Nudges stock Chromium's per-tab AutoPictureInPictureTabHelper when
// ActivateWindow's cross-profile in-window swap hides one tab and shows
// another WITHOUT touching either tab's own TabStripModel selection - the
// one signal AutoPictureInPictureTabStripObserverHelper actually listens for
// (see its class doc: it recomputes activation purely from
// TabStripModel::GetActiveWebContents(), never from window occlusion or
// focus). Window occlusion is a disabled-by-default feature backed by a
// stub Create() on this platform anyway, so it is not a usable fallback.
// `OnTabActivatedChanged` is the exact public entry point a real
// TabStripModelObserver notification would have called; this drives it
// synthetically from the one activation path that never fires one.
void NudgeAutoPipOnPresenterSwap(content::WebContents* outgoing_contents,
                                 content::WebContents* incoming_contents) {
  if (outgoing_contents && outgoing_contents != incoming_contents) {
    if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(
            outgoing_contents)) {
      // Self-notify quirk: OnTabActivatedChanged(false)'s "notify whoever's
      // newly active" courtesy call resolves via `outgoing_contents`'s OWN
      // tab_observer_helper_->GetActiveWebContents() - i.e. its own,
      // unchanged TabStripModel, which still reports `outgoing_contents`
      // itself as active (this swap never touched that selection). So that
      // courtesy call lands back on the outgoing tab, not on the
      // cross-profile incoming peer a real observer's activation handshake
      // would have found. Functionally benign here - it just re-runs
      // MaybeExitAutoPictureInPicture() on a tab not currently in auto-pip,
      // and PictureInPictureWindowManager's own exit call is idempotent/
      // guarded regardless of caller - but it is NOT full parity with the
      // real observer's peer-to-peer handshake, so don't rely on this path
      // to notify the incoming tab's helper; the explicit call below does
      // that.
      helper->OnTabActivatedChanged(/*is_tab_activated=*/false);
    }
  }
  if (incoming_contents) {
    if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(
            incoming_contents)) {
      helper->OnTabActivatedChanged(/*is_tab_activated=*/true);
    }
  }
}

// True for the duration of Browser::Create() called from BrowserController's
// OWN self-creating constructor. CollectionObserver::OnBrowserCreated also
// observes every Browser creation, including these - this flag lets it skip
// a Browser this class is already in the middle of wiring up itself, so the
// belt-and-suspenders attach path only ever fires for
// Browsers created by some OTHER path, never double-attaching a controller
// to a self-created one. Single-threaded UI-thread invariant, same as every
// other plain static global in this file (Registry(), etc.).
bool& CreatingOwnBrowser() {
  static bool creating = false;
  return creating;
}

// A space switch moves no tab strip, so Chromium never hears that the tab it
// may send to the mini player left the screen, or came back.
void RecheckAutoMiniPlayerTabs() {
  GlobalBrowserCollection::GetInstance()->ForEach(
      [](BrowserWindowInterface* browser) {
        content::WebContents* active =
            browser->GetTabStripModel()->GetActiveWebContents();
        if (auto* helper = active ? AutoPictureInPictureTabHelper::
                                        FromWebContents(active)
                                  : nullptr) {
          helper->RecheckTabActivated();
        }
        return true;
      });
}

}  // namespace

// Tab id contract shared with the Swift sidebar (mirrors M1): the id used to
// be the WebContents pointer value directly. after a tab closes,
// the allocator can hand the freed address to a brand-new WebContents, so a
// pointer-derived id can collide across a close/create cycle - Swift-side
// state keyed by tab id (favicon buffers, MRU rings, pending archive/discard
// intents, the cold-tab adoption queue) could then misattribute a stale
// entry to the wrong tab. Assigned instead as a per-process monotonic
// counter, stamped onto the WebContents as user data the first time this
// tab is seen - ids never repeat within a process, regardless of address
// reuse. Kept as a plain uint64_t (the ABI type every consumer already
// expects), just no longer address-derived.
//
// Declared in atrium_browser_controller.h (not anonymous-namespace-local
// like the rest of this section) and DEFINED here, not mirrored per-TU: a
// stateful id assignment cannot tolerate the old pattern of every .mm having
// its own reinterpret_cast copy of this function, since two independent
// counters would disagree. atrium_peek_intercept.cc and
// atrium_browser_window.cc both call this one definition now.
namespace {

class TabIdData : public base::SupportsUserData::Data {
 public:
  explicit TabIdData(uint64_t id) : id_(id) {}
  uint64_t id() const { return id_; }

 private:
  const uint64_t id_;
};

// Address-of-static as the SupportsUserData key, the standard Chromium idiom
// for a globally-unique key with no separate registration step.
const void* const kTabIdDataKey = &kTabIdDataKey;

// fallback starting page for a little window when OpenLittleWindow
// seeds it with no URL. No home-page concept exists elsewhere in Atrium yet,
// so this reuses the same fixed placeholder CreateForProfile's M2a bootstrap
// path already shows on first run (below) - one obvious default, easy to
// swap for a real one later.
constexpr char kLittleWindowDefaultURL[] = "https://example.com";

// wide enough for the scrubber's tile on a 2x display, and the whole
// reason the timeline can hold sixty of these per tab.
constexpr int kVisitThumbnailWidth = 240;

std::string ThumbnailDataUrl(const std::vector<uint8_t>& jpeg) {
  return base::StrCat({"data:image/jpeg;base64,", base::Base64Encode(jpeg)});
}

}  // namespace

uint64_t TabId(content::WebContents* contents) {
  if (!contents) {
    return 0;
  }
  if (auto* data =
          static_cast<TabIdData*>(contents->GetUserData(kTabIdDataKey))) {
    return data->id();
  }
  // 0 is reserved (see the null-contents case above and every ternary
  // fallback like `active ? TabId(active) : 0` at call sites), so ids start
  // at 1.
  static uint64_t next_id = 1;
  const uint64_t id = next_id++;
  contents->SetUserData(kTabIdDataKey, std::make_unique<TabIdData>(id));
  return id;
}

namespace {

std::optional<std::vector<uint8_t>> EncodeFaviconPng(const gfx::Image& image) {
  if (image.IsEmpty()) {
    return std::nullopt;
  }
  const gfx::ImageSkia skia = image.AsImageSkia();
  // Not image_reps(): the driver's reps are made lazily, so that list is empty
  // here. 1x too, so a 16px-only site is not upscaled.
  const gfx::ImageSkiaRep& rep_2x = skia.GetRepresentation(2.0f);
  const gfx::ImageSkiaRep& rep_1x = skia.GetRepresentation(1.0f);
  const gfx::ImageSkiaRep& best =
      rep_2x.pixel_width() > rep_1x.pixel_width() ? rep_2x : rep_1x;
  if (best.is_null() || best.pixel_width() <= 0) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> png = gfx::PNGCodec::EncodeBGRASkBitmap(
      best.GetBitmap(), /*discard_transparency=*/false);
  if (!png || png->empty()) {
    return std::nullopt;
  }
  return png;
}

}  // namespace

// Per-tab observer emitting granular URL and title events. The strip-level
// OnTabChangedAt coalesces both into TabChangeType::kAll, so the sidebar
// could not tell them apart.
class BrowserController::TabEventForwarder
    : public content::WebContentsObserver,
      public favicon::FaviconDriverObserver {
 public:
  TabEventForwarder(content::WebContents* contents,
                    BrowserController* controller)
      : content::WebContentsObserver(contents), controller_(controller) {
    // Favicon pipeline: the driver exists once TabHelpers attached (every
    // BrowserController tab goes through Browser's tab creation, which
    // attaches them). Guarded anyway - a missing driver just means
    // letter-chip placeholders for this tab.
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(contents)) {
      driver->AddObserver(this);
    }
  }

  ~TabEventForwarder() override {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      driver->RemoveObserver(this);
    }
  }

  // Pushes `image` (the driver's current or freshly-updated favicon) as PNG
  // bytes. Empty images are skipped - a page with no icon never pushes, and
  // the Swift side keeps its host-keyed cache entry (see the retired ABI header's
  // TAB FAVICON block). Encodes the LARGEST pixel representation the driver
  // holds, not As1xPNGBytes: the 1x rep is typically 16px, which upscaled
  // onto a Retina row chip looked visibly blurry - sites usually also provide a 32px 2x rep, and
  // downscaling a larger bitmap stays crisp.
  void PushFavicon(const gfx::Image& image) {
    const std::optional<std::vector<uint8_t>> png = EncodeFaviconPng(image);
    if (!png) {
      return;
    }
    // the HTML sidebar renders these via
    // chrome://atrium-favicon/<id>.png.
    FaviconCache::For(web_contents()->GetBrowserContext())
        .Store(TabId(web_contents()), *png);
    if (AtriumBrowserWindow* w = controller_->window()) {
      w->PushSidebarSnapshot();
    }
  }

  // Announces the driver's CURRENT icon - OnFaviconUpdated only fires for
  // future changes, so a tab that already resolved its icon (adopted tabs,
  // session-restored pages) pushes once at attach time.
  //
  // gated on FaviconIsValid(). GetFavicon() returns the DEFAULT
  // GLOBE - not an empty image - when the page has no official favicon yet
  // (see content/public/browser/favicon_status.h: `valid` distinguishes "got
  // an official favicon" from "just using the default"), so PushFavicon's
  // IsEmpty() guard does not filter it. Pushing it anyway made every
  // freshly-opened tab overwrite that host's good cached icon with the globe,
  // which only came back if a real OnFaviconUpdated happened to land later -
  // the "favicon disappears when I open a tab, cmd+R brings it back" bug.
  void PushCurrentFavicon() {
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      if (!driver->FaviconIsValid()) {
        return;
      }
      PushFavicon(driver->GetFavicon());
    }
  }

  // favicon::FaviconDriverObserver:
  void OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                        NotificationIconType notification_icon_type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override {
    PushFavicon(image);
    // The favourite tile for this same page reads a URL-keyed cache, and its
    // lookup ran at the first push - before this visit, when the service had
    // nothing. This is the moment the answer changed.
    controller_->RetryFaviconForVisitedURL(
        web_contents()->GetLastCommittedURL().spec());
    // The page tint's fallback source for a page declaring no theme-color,
    // which is most of them. This is the moment it becomes readable.
    if (AtriumBrowserWindow* w = controller_->window()) {
      w->ApplyPageTintFor(web_contents());
    }
  }

  // The row a renderer-created tab is owed opens here, a network round trip
  // before the commit, so the sidebar shows the tab as soon as the page does.
  void DidStartNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && !handle->IsSameDocument() &&
        controller_->PersistsTabs()) {
      controller_->OpenDeferredRowForTab(web_contents(), handle->GetURL());
    }
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override {
    // This tab's url is a key in the cross-window map every window's sidebar
    // reads, so the cached copy is wrong from here on.
    atrium::InvalidateLiveTabsCache();
    atrium::NoteFavoriteLandingForTab(web_contents(),
                                      web_contents()->GetLastCommittedURL());
    const std::string url = web_contents()->GetLastCommittedURL().spec();
    VLOG(1) << "Atrium: tab " << TabId(web_contents()) << " url "
            << atrium::LogSafeUrl(web_contents()->GetLastCommittedURL());
    // The row's persisted url has to follow the page. A row's
    // live tab id comes from matching its persisted url against the live
    // strip's url EXACTLY (sidebar::LiveTabIds::take), so a tab that navigated
    // stopped resolving to its own row - and one stale string cost three
    // symptoms at once: no row drew as selected, a close button did nothing,
    // and a click fell through to the cold-row path and opened the page a
    // SECOND time.
    //
    // Here rather than in TitleWasSet, which is where the TITLE catches up:
    // this fires per committed navigation, and a url and a title do not always
    // change together.
    // an incognito navigation is not recorded. The tab has no row to
    // update, and a bare tab id could collide with one that does.
    if (controller_->PersistsTabs()) {
      // Before note_tab_url, which resolves through the binding this opens.
      controller_->OpenDeferredRowForTab(web_contents(),
                                         web_contents()->GetLastCommittedURL());
      atrium_core_note_tab_url(TabId(web_contents()), url.c_str());
      // and a stop on this tab's timeline. Gated the same way as the
      // row above, for the same reason: a timeline outlives the page, so
      // keeping one for an incognito tab is keeping browsing history the mode
      // promises not to keep. The core reads no clock, so the time comes from
      // here.
      if (atrium_core_record_visit(
              TabId(web_contents()), url.c_str(),
              base::UTF16ToUTF8(web_contents()->GetTitle()).c_str(),
              base::Time::Now().InMillisecondsFSinceUnixEpoch())) {
        // Only when the core says it pushed. The thumbnail store is read by
        // position, so a slot added on any other event would slide every stop
        // onto the wrong picture.
        VisitThumbnails().AddStop(TabId(web_contents()));
      }
    }
    // a new document starts with nothing focused, so the answer just
    // changed even though no focus notification fired. Nothing to reset - the
    // live read is already correct the moment Blink clears
    // has_focused_editable_element_.
    controller_->OnPageEditableFocusChanged(web_contents());
    // per-tab extension action state resets on navigation - let the
    // controller rebuild the snapshot if this is its active tab.
    controller_->OnActiveTabNavigated(web_contents());
    controller_->PushFoyerSnapshot();
  }

  void TitleWasSet(content::NavigationEntry* entry) override {
    VLOG(1) << "Atrium: tab " << TabId(web_contents()) << " title "
            << web_contents()->GetTitle();
    // the row was persisted at OnTabAdded, when the page had no
    // title yet, and the sidebar model reads titles off the ROW - so the
    // re-push below redraws the same empty string unless the title reaches
    // the document first. Not a rename: a row the user renamed keeps showing
    // that name.
    // same rule as the url above - a private title never reaches the
    // document.
    bool row_title_unchanged = false;
    if (controller_->PersistsTabs()) {
      const std::string title =
          base::UTF16ToUTF8(web_contents()->GetTitle());
      // false also means "core closed" or "no row", so only a located row
      // proves the title was already there.
      if (!atrium_core_note_tab_title(TabId(web_contents()), title.c_str())) {
        char* row_space = atrium_core_space_id_for_tab(TabId(web_contents()));
        row_title_unchanged = row_space != nullptr;
        atrium_core_string_free(row_space);
      }
      // the timeline's newest stop was recorded at commit, before
      // this title existed. Same url, so this normally folds into that stop
      // rather than adding one - and the thumbnail slot follows whichever it
      // did.
      if (atrium_core_record_visit(
              TabId(web_contents()),
              web_contents()->GetLastCommittedURL().spec().c_str(),
              title.c_str(),
              base::Time::Now().InMillisecondsFSinceUnixEpoch())) {
        VisitThumbnails().AddStop(TabId(web_contents()));
      }
    }
    // The foyer shows only the active tab, and a bound row proves the title
    // did not change.
    if (!row_title_unchanged &&
        controller_->browser_->tab_strip_model()->GetActiveWebContents() ==
            web_contents()) {
      controller_->PushFoyerSnapshot();
    }
    // A bound row already shows this title, so the rebuild would draw the same
    // model. Unbound, favourite and private tabs still push.
    if (row_title_unchanged &&
        !controller_->tabs_owed_a_row_.contains(TabId(web_contents())) &&
        atrium::FavoriteKeyForTab(web_contents()).empty()) {
      return;
    }
    // this is where titles actually ARRIVE. A tab's title is empty
    // when the strip first changes, so the HTML sidebar's rows would stay
    // blank without a re-push here.
    if (AtriumBrowserWindow* w = controller_->window()) {
      w->PushSidebarSnapshot();
    }
  }

  // the scrubber's picture for the stop this page became. A past
  // state cannot be captured after the fact, so the current page is stamped
  // onto the newest stop - and here rather than at PrimaryPageChanged because
  // at commit the new document has not painted, so the copy would return the
  // page being LEFT.
  // The ring around the tab's row and tile runs between these two. A click
  // used to stand in with a fixed window, which a Cmd+R never started.
  void DidStartLoading() override {
    if (AtriumBrowserWindow* w = controller_->window()) {
      w->PushTabLoading(TabId(web_contents()), true);
    }
  }

  void DidStopLoading() override {
    if (AtriumBrowserWindow* w = controller_->window()) {
      w->PushTabLoading(TabId(web_contents()), false);
    }
    if (!controller_->PersistsTabs()) {
      return;
    }
    const uint64_t tab_id = TabId(web_contents());
    // A plain function bound to values, not a weak pointer to this forwarder:
    // the reply needs neither the tab nor the window, so one that lands after
    // either dies has nothing stale to touch.
    CaptureThumbnail(
        web_contents(), kVisitThumbnailWidth,
        base::BindOnce(
            [](uint64_t tab_id, uint64_t serial,
               std::optional<std::vector<uint8_t>> jpeg) {
              if (jpeg && !jpeg->empty()) {
                VisitThumbnails().SetNewestThumbnail(tab_id, serial,
                                                     ThumbnailDataUrl(*jpeg));
              }
            },
            tab_id, VisitThumbnails().StopSerial(tab_id)));
  }

  // the renderer's focused element changed. `is_editable_node` is
  // Chromium's own public projection of has_focused_editable_element_ (the
  // same signal RenderWidgetHostViewMac::FocusedNodeChanged feeds the virtual
  // keyboard from) - see the retired ABI header's EDIT FOCUS doc block for why this
  // rather than content::TextInputManager, which chrome/ may not include.
  void OnFocusChangedInPage(
      const content::FocusedNodeDetails& details) override {
    // `details.is_editable_node` is deliberately ignored: BrowserController
    // reads WebContents::IsFocusedElementEditable() live rather than caching
    // anything, so this notification's only job is "the answer may have
    // changed, re-push it to Swift". Caching the bool here would reintroduce
    // exactly the divergence the live read avoids - a native window reparent
    // (a tab dragged between windows) moves no DOM focus and fires no
    // notification, so a cached record would go missing while the field stays
    // focused, and the gate would fire over live text.
    controller_->OnPageEditableFocusChanged(web_contents());
  }

 private:
  // The controller that owns this forwarder (via forwarders_). Outlives it -
  // BrowserController is process-lifetime, and the forwarder is erased on tab
  // remove / strip teardown. Used for the extension resync hook.
  const raw_ptr<BrowserController> controller_;
};

// Peek mode: observes the standalone peek
// WebContents (never inserted into any TabStripModel, so it has no tab_id
// and can't reuse TabEventForwarder's OnURLChanged/OnTitleChanged - those
// push through UIBridge keyed by tab_id). Modeled on TabEventForwarder but
// narrower: its favicon only badges the source favourite's tile. It sends
// the real push (AtriumOnPeekMetaChanged, gated on peek_connected() inside
// UIBridge::OnPeekMetaChanged itself) - `host` names the owning controller.
class BrowserController::PeekEventForwarder
    : public content::WebContentsObserver,
      public favicon::FaviconDriverObserver {
 public:
  PeekEventForwarder(content::WebContents* contents, BrowserController* host)
      : content::WebContentsObserver(contents), host_(host) {
    // A link-click peek gets no tab helpers, so it has no driver of its own.
    favicon::CreateContentFaviconDriverForWebContents(contents);
    favicon::ContentFaviconDriver::FromWebContents(contents)->AddObserver(this);
  }
  ~PeekEventForwarder() override {
    if (web_contents()) {
      favicon::ContentFaviconDriver::FromWebContents(web_contents())
          ->RemoveObserver(this);
    }
  }

  // favicon::FaviconDriverObserver:
  void OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                        NotificationIconType notification_icon_type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override {
    PushFavicon(image);
  }

  // OnFaviconUpdated only reports later changes, and an adopted popup can
  // already hold its icon.
  void PushCurrentFavicon() {
    auto* driver =
        favicon::ContentFaviconDriver::FromWebContents(web_contents());
    if (driver && driver->FaviconIsValid()) {
      PushFavicon(driver->GetFavicon());
    }
  }

  // content::WebContentsObserver:
  // the peek card's title and URL were reported to the Swift
  // chrome, which drew the card's own strip. Nothing reads them now, so the
  // two observers that carried them are gone. They come back when
  // the card gets an HTML strip.

  // (VM-observed flash mitigation - see PeekRootView::
  // loading_placeholder_'s doc): the peek card's loading placeholder covers
  // the WebView until the fresh WebContents produces its first real
  // compositor frame. ControllerFor (not a stored BrowserController*): this
  // forwarder already only carries `host_`, and ControllerFor's null-safe
  // lookup is the same pattern PeekPromoteOp/PeekDismissOp use, so a dismiss
  // that raced this callback (destroying `host_`'s controller entry) degrades
  // to a no-op instead of touching a stale pointer.
  void DidFirstVisuallyNonEmptyPaint() override {
    VLOG(1) << "Atrium: peek first paint";
    if (BrowserController* controller =
            BrowserController::ControllerFor(host_)) {
      controller->HidePeekLoadingPlaceholder();
    }
  }

  // A popup that redirects (every OAuth flow does) must not leave the chip
  // naming the origin it started on. Same-document commits count.
  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (!navigation->IsInPrimaryMainFrame()) {
      return;
    }
    if (navigation->HasCommitted()) {
      PushOriginChip();
      return;
    }
    // A download or a 204 leaves the popup blank for good, and nothing will
    // paint over the placeholder. A replaced navigation still has a pending
    // entry, so that one keeps waiting.
    if (!web_contents()->GetController().GetPendingEntry()) {
      if (BrowserController* controller =
              BrowserController::ControllerFor(host_)) {
        controller->HidePeekLoadingPlaceholder();
      }
    }
  }

  void TitleWasSet(content::NavigationEntry* entry) override {
    PushOriginChip();
  }

  // A page with no icon never reports one, so the last page's goes here.
  // Not at commit: a page with the same icon would flash the badge off.
  void DidStopLoading() override {
    auto* driver =
        favicon::ContentFaviconDriver::FromWebContents(web_contents());
    if (!driver || !driver->FaviconIsValid()) {
      PushFavicon(gfx::Image());
    }
  }

 private:
  void PushOriginChip() {
    if (BrowserController* controller =
            BrowserController::ControllerFor(host_)) {
      controller->UpdatePeekOriginChip();
    }
  }

  void PushFavicon(const gfx::Image& image) {
    if (BrowserController* controller =
            BrowserController::ControllerFor(host_)) {
      controller->ShowPeekFavicon(image);
    }
  }

  const raw_ptr<BrowserController> host_;
};

// static
void BrowserController::CreateForInitialProfile() {
  DCHECK(Registry().empty());
  // GetLastUsedProfile()'s sync-load is acceptable here: this is the first
  // (and only) window of the session. M2a/M1 fallback (no spaces group):
  // unaffected by M2b lazy restore, always shows the same fixed tab.
  // this M1/M2a single-window fallback always restores a fixed
  // tab ("https://example.com" below), so under the "auto-open only when
  // there is no restored tab" rule (see atrium_profile_bootstrap.cc) the
  // palette never auto-opens here - unlike the multi-profile bootstrap
  // path, this one has no "empty active space" case to open it for.
  new BrowserController(ProfileManager::GetLastUsedProfile(), /*show=*/true,
                        "https://example.com", kDefaultSlot);
}

// static
BrowserController* BrowserController::CreateForProfile(
    Profile* profile,
    bool show,
    const std::string& startup_url,
    uint64_t slot) {
  return new BrowserController(profile, show, startup_url, slot);
}

// static
void BrowserController::ShowLoadedProfile(Profile* profile) {
  const std::string profile_id = profile->GetBaseName().AsUTF8Unsafe();
  // A copy, because CreateForProfile below adds to the registry.
  const std::vector<BrowserController*> controllers = Registry();
  for (BrowserController* presenter : controllers) {
    // Only a window on screen: ActivateWindow shows a slot that has none.
    if (!presenter->browser_ ||
        PresentationTable().ResolvePresenter(presenter->slot_) !=
            IdOf(presenter) ||
        presenter->ActiveSpaceProfileId() != profile_id) {
      continue;
    }
    BrowserController* target = ControllerFor(presenter->slot_, profile_id);
    if (!target && presenter->slot_ != kDefaultSlot) {
      target =
          CreateForProfile(profile, /*show=*/false,
                           /*startup_url=*/std::string(), presenter->slot_);
    }
    ActivateWindow(target);
  }
}

// static
void BrowserController::OpenLittleWindow() {
  if (!browser_) {
    return;
  }
  CreateLittleWindow(browser_->profile(), GURL());
}

BrowserController* BrowserController::CreateWindowSlot(
    Browser* invoking_browser,
    std::optional<gfx::Point> screen_point,
    bool open_command_bar) {
  BrowserController* invoking = ControllerForBrowser(invoking_browser);
  if (!invoking || !invoking->browser_) {
    LOG(WARNING) << "Atrium: CreateWindowSlot dropped - invoking browser has "
                    "no live controller";
    return nullptr;
  }
  // "Profile of the current space of the invoking window".
  // the invoking controller's OWN profile is not that whenever a
  // cross-profile in-window swap (Cmd+Y) is in effect - the window
  // then shows another controller's content, and Cmd+N was opening a window
  // for the profile behind it rather than the one on screen. Resolve the
  // visible browser, the same posture the tab-scoped commands take, and fall
  // back to the invoking controller when nothing else is visible for the slot
  // (the no-swap case, which is today's behavior). seed_from_slot stays
  // invoking->slot_: Swift tracks the active space per SLOT, so the swapped-in
  // space is already the right seed.
  Browser* visible = VisibleBrowserForBrowser(invoking->browser_.get());
  Profile* profile = (visible ? visible : invoking->browser_.get())->profile();
  const uint64_t new_slot = NextSlotId();
  // M2c Arc-parity: an empty startup_url means "no initial tab" -
  // see the ctor. The window opens with zero tabs,
  // sidebar + tinted backdrop only, matching the reference product's Cmd+N
  // (which opens its command bar instead of a blank tab; Atrium's pill is
  // the M4-pending stand-in - see SidebarView.swift).
  // seed_from_slot = invoking->slot_ (a hard requirement): the new
  // slot's active space is seeded from the invoking window's, so its sidebar
  // never renders the unfiltered M1 fallback even for one frame.
  BrowserController* controller = new BrowserController(
      profile, /*show=*/true, /*startup_url=*/"", new_slot, invoking->slot_);
  // A detach drops the new window at the drag's release point -
  // origin at the given gfx screen point, pulled back into the nearest
  // display's work area so a drop near a screen edge can't strand the
  // window offscreen. The ctor centered it; this override runs in the same
  // turn, before the first frame is presented, so there is no visible jump.
  if (screen_point.has_value()) {
    gfx::Rect bounds = controller->window()->GetBounds();
    bounds.set_origin(*screen_point);
    bounds.AdjustToFit(display::Screen::Get()
                           ->GetDisplayNearestPoint(*screen_point)
                           .work_area());
    controller->window()->SetBounds(bounds);
  }
  VLOG(1) << "Atrium: "
          << (screen_point.has_value() ? "open_window_slot" : "Cmd+N")
          << " opened window-slot " << new_slot << " for host "
          << controller
          << (screen_point.has_value() ? " at " + screen_point->ToString()
                                       : std::string());
  // Post-show hook - open the palette for this
  // brand-new, tab-less window. Unconditional since the palette
  // is an HTML surface the browser builds itself, so there is no build left
  // where it is absent and the M2c pill-focus stand-in has to answer for it.
  // Skipped for a detach (open_command_bar false - see the header doc): a
  // tab is about to move in, so the empty-window affordance is wrong.
  if (open_command_bar) {
    controller->OpenCommandBar();
  }
  return controller;
}

// static
BrowserController* BrowserController::CreateWindowSlotForProfile(
    Browser* invoking_browser,
    const std::string& target_profile_id,
    std::optional<gfx::Point> screen_point) {
  BrowserController* invoking = ControllerForBrowser(invoking_browser);
  if (!invoking || !invoking->browser_) {
    LOG(WARNING) << "Atrium: CreateWindowSlotForProfile dropped - invoking "
                    "browser has no live controller";
    return nullptr;
  }
  // unlike CreateWindowSlot, the new slot's profile is NOT the
  // invoking window's own - it's resolved from `target_profile_id`'s
  // existing kDefaultSlot controller (every profile named by a persisted
  // space is bootstrapped hidden there at startup - see the header doc).
  BrowserController* target_default =
      ControllerFor(kDefaultSlot, target_profile_id);
  if (!target_default || !target_default->browser_) {
    LOG(WARNING) << "Atrium: CreateWindowSlotForProfile dropped - profile '"
                 << target_profile_id << "' has no bootstrapped controller";
    // Defense in depth: window-restore profiles are part of the
    // startup (non-deferred) set by construction - the export and the
    // restore driver read the same persisted windowSpaces - so this branch
    // should stay unreachable for a deferred profile. If a stale/divergent
    // set ever lands here anyway, start the load so the profile at least
    // becomes usable (this window is still skipped - restore doesn't
    // queue/retry).
    ProfileBootstrap::EnsureProfileLoaded(target_profile_id);
    return nullptr;
  }
  Profile* profile = target_default->browser_->profile();
  const uint64_t new_slot = NextSlotId();
  SetSlotSpace(new_slot,
               atrium_core_space_for_profile(SpaceIndexForSlot(invoking->slot_),
                                             target_profile_id.c_str()));
  BrowserController* controller = new BrowserController(
      profile, /*show=*/true, /*startup_url=*/"", new_slot, invoking->slot_);
  if (screen_point.has_value()) {
    gfx::Rect bounds = controller->window()->GetBounds();
    bounds.set_origin(*screen_point);
    bounds.AdjustToFit(display::Screen::Get()
                           ->GetDisplayNearestPoint(*screen_point)
                           .work_area());
    controller->window()->SetBounds(bounds);
  }
  VLOG(1) << "Atrium: open_window_slot_for_profile opened window-slot "
          << new_slot << " for host " << controller << " profile '"
          << target_profile_id << "'";
  return controller;
}

// static
Profile* BrowserController::ResolveMirrorProfile(
    const std::string& profile_id) {
  if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
    return nullptr;
  }
  BrowserController* target_default = ControllerFor(kDefaultSlot, profile_id);
  if (!target_default || !target_default->browser_) {
    return nullptr;
  }
  return target_default->browser_->profile();
}

// static
BrowserController* BrowserController::CreateLittleWindow(
    Profile* profile,
    const GURL& url,
    std::optional<gfx::Point> screen_point,
    const content::OpenURLParams* link_params) {
  const uint64_t new_slot = NextSlotId();
  // A little window always shows exactly one tab immediately - `url` if
  // valid, else a fixed default page (kLittleWindowDefaultURL, same
  // "https://example.com" fallback CreateForProfile's M2a bootstrap path
  // uses below). this must NOT collapse to an empty string on an
  // invalid seed - empty startup_url is CreateWindowSlot's deliberate
  // Cmd+N "zero tabs" convention (see the ctor), a different meaning than
  // "no URL was supplied here". Passing a non-empty string always keeps
  // this call out of that branch, so a little window never opens tab-less.
  // No seed_from_slot (little windows have no space to seed - OnSlotCreated
  // is a no-op for it anyway, since it's gated on spaces_connected() and
  // this controller never registers a host regardless).
  // A link click seeds its own tab below instead, so the empty startup_url
  // "zero tabs" arm is deliberate here and the window is never left tab-less.
  BrowserController* controller = new BrowserController(
      profile, /*show=*/true,
      link_params ? std::string()
                  : (url.is_valid() ? url.spec()
                                    : std::string(kLittleWindowDefaultURL)),
      new_slot, /*seed_from_slot=*/std::nullopt, /*little=*/true);
  if (link_params) {
    // Navigate() rather than the ctor's chrome::AddTabAt: one navigation that
    // carries the link's referrer and initiator origin, not a bare typed load.
    const ScopedRoutingBypass no_routing;
    NavigateParams nav(controller->browser_.get(), link_params->url,
                       link_params->transition);
    nav.FillNavigateParamsFromOpenURLParams(*link_params);
    nav.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&nav);
  }
  // Positioning follows CreateWindowSlot: origin at the given
  // gfx screen point, pulled back into the nearest display's work area so a
  // little window can't be stranded offscreen.
  if (screen_point.has_value()) {
    gfx::Rect bounds = controller->window()->GetBounds();
    bounds.set_origin(*screen_point);
    bounds.AdjustToFit(display::Screen::Get()
                           ->GetDisplayNearestPoint(*screen_point)
                           .work_area());
    controller->window()->SetBounds(bounds);
  }
  VLOG(1) << "Atrium: CreateLittleWindow opened window-slot " << new_slot
          << " for host " << controller;
  return controller;
}

// static
BrowserController* BrowserController::OpenOrFocusDoorwayWindow(
    Profile* profile,
    const GURL& url,
    std::optional<gfx::Point> screen_point) {
  // Copied first - SetWindowAppearance's guard against a window closing
  // itself mid-scan applies here too.
  const std::vector<BrowserController*> registry = Registry();
  for (BrowserController* candidate : registry) {
    if (!candidate->browser_ || !candidate->little_ ||
        candidate->browser_->profile() != profile) {
      continue;
    }
    TabStripModel* strip = candidate->browser_->tab_strip_model();
    if (strip->count() != 1) {
      continue;
    }
    content::WebContents* tab = strip->GetWebContentsAt(0);
    if (tab && tab->GetLastCommittedURL() == url) {
      candidate->window()->Show();
      return candidate;
    }
  }
  return CreateLittleWindow(profile, url, screen_point);
}

bool MoveWebContentsBetweenStrips(TabStripModel* source_strip,
                                  int source_index,
                                  TabStripModel* dest_strip,
                                  int dest_index,
                                  int add_types) {
  // Extensions such as Claude for Chrome keep their tab alone in a group and
  // check that group on every call, so a lone tab's group moves with it.
  const std::vector<tab_groups::TabGroupId> emptied =
      source_strip->GetGroupsDestroyedFromRemovingIndices({source_index});
  if (!emptied.empty() && dest_strip->SupportsTabGroups()) {
    // As MoveGroupToWindowImpl: unpaused, sync reads the move as tabs leaving
    // the saved group.
    tab_groups::TabGroupSyncService* sync =
        tab_groups::TabGroupSyncServiceFactory::GetForProfile(
            source_strip->profile());
    std::unique_ptr<tab_groups::ScopedLocalObservationPauser> pause;
    if (sync && sync->GetGroup(emptied.front())) {
      pause = sync->CreateScopedLocalObserverPauser();
    }
    std::unique_ptr<DetachedTabCollection> group =
        source_strip->DetachTabGroupForInsertion(emptied.front());
    // The group insert selects without the deactivate and hide signals that
    // tab-modal dialogs wait for, so ActivateTabAt does it.
    group->active_index_ = std::nullopt;
    const gfx::Range inserted =
        dest_strip->InsertDetachedTabGroupAt(std::move(group), dest_index);
    if (add_types & AddTabTypes::ADD_ACTIVE) {
      dest_strip->ActivateTabAt(static_cast<int>(inserted.start()));
    }
    return true;
  }
  std::unique_ptr<tabs::TabModel> detached =
      source_strip->DetachTabAtForInsertion(source_index);
  if (!detached) {
    return false;
  }
  dest_strip->InsertDetachedTabAt(dest_index, std::move(detached), add_types);
  return true;
}

// A WebContents is bound to its creating BrowserContext, so a tab that crosses
// profiles is reopened in `target` and closed here. Returns the new tab, or
// null if `target` did not take one. `reason` only names the caller in the log.
content::WebContents* ReopenTabInOtherProfile(TabStripModel* source_strip,
                                              int source_index,
                                              Browser* target,
                                              bool foreground,
                                              std::string_view reason) {
  content::WebContents* contents = source_strip->GetWebContentsAt(source_index);
  if (!contents) {
    return nullptr;
  }
  const GURL url = contents->GetLastCommittedURL();
  // Non-routed: this IS the move the user asked for, and re-deciding where it
  // belongs would undo it.
  const ScopedRoutingBypass no_routing;
  chrome::AddTabAt(target, url, -1, foreground);
  TabStripModel* target_strip = target->tab_strip_model();
  content::WebContents* reopened =
      target_strip->empty()
          ? nullptr
          : target_strip->GetWebContentsAt(target_strip->count() - 1);
  source_strip->CloseWebContentsAt(source_index,
                                   TabCloseTypes::CLOSE_USER_GESTURE);
  VLOG(1) << "Atrium: " << reason << " reopened " << atrium::LogSafeUrl(url)
          << " in another profile and closed the source tab";
  return reopened;
}

// static
BrowserController* BrowserController::AdoptTabIntoLittleWindow(
    content::WebContents* contents,
    std::optional<gfx::Point> screen_point) {
  if (!contents) {
    return nullptr;
  }

  // Same registry scan ActivateTab's cross-window branch uses to find the
  // controller currently holding `contents` - the sidebar only hands the
  // wiring subtask a tab id, not a host handle.
  BrowserController* source = nullptr;
  int source_index = TabStripModel::kNoTab;
  for (BrowserController* candidate : Registry()) {
    if (!candidate->browser_) {
      continue;
    }
    const int index = candidate->browser_->tab_strip_model()
                          ->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab) {
      source = candidate;
      source_index = index;
      break;
    }
  }
  if (!source) {
    LOG(WARNING) << "Atrium: AdoptTabIntoLittleWindow found no controller "
                    "owning the given WebContents";
    return nullptr;
  }

  Profile* profile = source->browser_->profile();
  TabStripModel* source_strip = source->browser_->tab_strip_model();

  // Same little-window shape as CreateLittleWindow (little_=true, no
  // seed_from_slot) but with an EMPTY startup_url - the ctor's "zero tabs"
  // branch (see its own doc) - because the tab to show is the one about to
  // be adopted below, not a fresh chrome::AddTabAt call.
  const uint64_t new_slot = NextSlotId();
  BrowserController* controller = new BrowserController(
      profile, /*show=*/true, /*startup_url=*/std::string(), new_slot,
      /*seed_from_slot=*/std::nullopt, /*little=*/true);

  if (!MoveWebContentsBetweenStrips(source_strip, source_index,
                                    controller->browser_->tab_strip_model(),
                                    0)) {
    LOG(WARNING) << "Atrium: AdoptTabIntoLittleWindow detach failed for "
                    "host " << source;
    chrome::CloseWindow(controller->browser_);
    return nullptr;
  }

  if (screen_point.has_value()) {
    gfx::Rect bounds = controller->window()->GetBounds();
    bounds.set_origin(*screen_point);
    bounds.AdjustToFit(display::Screen::Get()
                           ->GetDisplayNearestPoint(*screen_point)
                           .work_area());
    controller->window()->SetBounds(bounds);
  }
  VLOG(1) << "Atrium: AdoptTabIntoLittleWindow opened window-slot "
          << new_slot << " for host " << controller << " from host "
          << source;
  return controller;
}

std::vector<DropTargetCandidate> BrowserController::BuildDropCandidates(
    std::vector<BrowserController*>& resident) {
  // Build one candidate per OTHER live controller with a real window. `this`
  // is left out on purpose - the classifier never sees
  // its own bounds, so a release back onto the source window's own content
  // takes the same kNoMatch path as an empty-desktop release rather than a
  // fourth outcome.
  std::vector<DropTargetCandidate> candidates;
  // ChooseDropTarget takes the first containing rect, so where two windows
  // overlap the front one has to come first - activation order, not creation.
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser_interface) {
        BrowserController* candidate = ControllerForBrowser(
            browser_interface->GetBrowserForMigrationOnly());
        if (!candidate || candidate == this || !candidate->browser_) {
          return true;
        }
        // A little window is single-tab by construction: a second tab gets no
        // sidebar row, and MoveLittleTabToSpace then refuses to move it out.
        if (candidate->little_) {
          return true;
        }
        // An incognito profile's GetPath() is its parent's, so profile_id_
        // would read it as the same workspace and hand it a live regular tab
        // (same skip as CollectAtriumWindows, atrium_browser_for_profile.cc).
        if (candidate->browser_->profile()->IsOffTheRecord()) {
          return true;
        }
        AtriumBrowserWindow* candidate_window = candidate->window();
        // A show=false hidden-pool member still gets full-size centred
        // bounds, so it would swallow drops unseen. Not IsVisible(): a window
        // in its startup hold is real and about to appear.
        if (!candidate_window ||
            candidate_window->IsExcludedFromWindowCycling() ||
            candidate_window->IsMinimized()) {
          return true;
        }
        const gfx::Rect window_bounds = candidate_window->GetBounds();
        resident.push_back(candidate);
        candidates.push_back(
            {.window_bounds = window_bounds,
             .sidebar_bounds =
                 gfx::Rect(window_bounds.x(), window_bounds.y(),
                           candidate_window->LastDockedSidebarWidth(),
                           window_bounds.height()),
             .has_sidebar = true});
        return true;
      },
      BrowserCollection::Order::kActivation);
  return candidates;
}

// static
gfx::Rect BrowserController::ContentBoundsOf(
    const DropTargetCandidate& candidate) {
  gfx::Rect content_bounds = candidate.window_bounds;
  if (candidate.has_sidebar) {
    content_bounds.Inset(
        gfx::Insets::TLBR(0, candidate.sidebar_bounds.width(), 0, 0));
  }
  return content_bounds;
}

void BrowserController::PreviewDropForPoint(const gfx::Point& screen_point) {
  std::vector<BrowserController*> resident;
  std::vector<DropTargetCandidate> candidates = BuildDropCandidates(resident);
  const DropTargetDecision decision =
      ChooseDropTarget(screen_point, candidates);

  BrowserController* target = nullptr;
  SplitEdge edge = SplitEdge::kCenter;
  if (decision.kind == DropTargetKind::kContent) {
    target = resident[decision.index];
    edge = ResolveSplitEdge(ContentBoundsOf(candidates[decision.index]),
                            screen_point);
  }

  // A preview leaves no other trace - it paints a view and returns - so a
  // scenario has nothing to wait for without this.
  VLOG(2) << "Atrium: drop preview at " << screen_point.ToString() << " -> "
          << (target ? "edge " + base::NumberToString(static_cast<int>(edge))
                     : std::string("no target"));

  // Every OTHER window is cleared on each move, not just the one being left.
  // A pointer crossing from one window straight into another never produces a
  // leave event for the first, so clearing only on leave strands a highlight.
  for (BrowserController* candidate : Registry()) {
    if (!candidate->browser_) {
      continue;
    }
    if (candidate == target) {
      candidate->window()->ShowContentDropPreview(edge);
    } else {
      candidate->window()->ClearContentDropPreview();
    }
  }
}

// static
void BrowserController::ClearDropPreviews() {
  for (BrowserController* candidate : Registry()) {
    if (candidate->browser_) {
      candidate->window()->ClearContentDropPreview();
    }
  }
}

void BrowserController::ClassifyAndRouteDroppedTab(
    content::WebContents* contents,
    const gfx::Point& screen_point) {
  std::vector<BrowserController*> resident;
  std::vector<DropTargetCandidate> candidates = BuildDropCandidates(resident);

  DropTargetDecision decision = ChooseDropTarget(screen_point, candidates);
  switch (decision.kind) {
    case DropTargetKind::kNoMatch:
      // Tear the tab off into a little window.
      AdoptTabIntoLittleWindow(contents, screen_point);
      return;
    case DropTargetKind::kSidebar:
      // Same workspace moves silently, a
      // different one is classified but not acted on yet (the confirmation UI
      // is not built) - see RouteSidebarDrop's own doc.
      RouteSidebarDrop(contents, resident[decision.index]);
      return;
    case DropTargetKind::kContent: {
      RouteContentDrop(contents, resident[decision.index],
                        ContentBoundsOf(candidates[decision.index]),
                        screen_point);
      return;
    }
  }
}

namespace {

// see SetSidebarMoveConfirmResultForTesting's doc.
std::optional<bool>& SidebarMoveConfirmResultForTesting() {
  static std::optional<bool> result;
  return result;
}

// see SetContentDropInsertionCallbackForTesting's doc.
base::RepeatingCallback<void(BrowserController*, ContentDropOrientation)>&
ContentDropInsertionCallbackForTesting() {
  static base::NoDestructor<
      base::RepeatingCallback<void(BrowserController*, ContentDropOrientation)>>
      callback;
  return *callback;
}

// See SetRoutedOpenTargetUnrealisedForTesting's doc.
bool& RoutedOpenTargetUnrealisedForTesting() {
  static bool unrealised = false;
  return unrealised;
}

// Whether a routed open may add a tab to `browser`. A Browser with no native
// host aborts the process the moment a tab is added to it.
bool RoutedOpenTargetIsUsable(Browser* browser) {
  return !RoutedOpenTargetUnrealisedForTesting() &&
         AtriumBrowserWindow::HasRealisedWindow(browser);
}

}  // namespace

// static
void BrowserController::SetRoutedOpenTargetUnrealisedForTesting(
    bool unrealised) {
  RoutedOpenTargetUnrealisedForTesting() = unrealised;
}

// static
void BrowserController::SetSidebarMoveConfirmResultForTesting(
    std::optional<bool> confirmed) {
  SidebarMoveConfirmResultForTesting() = confirmed;
}

// static
void BrowserController::SetContentDropInsertionCallbackForTesting(
    base::RepeatingCallback<void(BrowserController* target,
                                  ContentDropOrientation orientation)> callback) {
  ContentDropInsertionCallbackForTesting() = std::move(callback);
}

void BrowserController::RouteContentDrop(content::WebContents* contents,
                                          BrowserController* target,
                                          const gfx::Rect& content_bounds,
                                          const gfx::Point& screen_point) {
  if (!contents || !target || !browser_ || !target->browser_) {
    return;
  }
  const ContentDropOrientation orientation =
      ChooseSplitOrientation(content_bounds, screen_point);
  if (ContentDropInsertionCallbackForTesting()) {
    ContentDropInsertionCallbackForTesting().Run(target, orientation);
    return;
  }

  TabStripModel* source_strip = browser_->tab_strip_model();
  const int source_index = source_strip->GetIndexOfWebContents(contents);
  if (source_index == TabStripModel::kNoTab) {
    LOG(WARNING) << "Atrium: RouteContentDrop given a WebContents not in "
                    "its own source strip";
    return;
  }
  const SplitEdge edge = ResolveSplitEdge(content_bounds, screen_point);
  TabStripModel* target_strip = target->browser_->tab_strip_model();

  if (ChooseSidebarMoveExecution(profile_id_, target->profile_id_) ==
      SidebarMoveExecution::kReopenAndClose) {
    // Background: the drop decides which pane it lands in, so letting it come
    // up active first would make InsertPaneFromDrop refuse it as the pane it
    // is already in.
    if (content::WebContents* reopened = ReopenTabInOtherProfile(
            source_strip, source_index, target->browser_,
            /*foreground=*/false, "RouteContentDrop")) {
      target->window()->InsertPaneFromDrop(reopened, edge);
    }
    return;
  }

  if (!MoveWebContentsBetweenStrips(source_strip, source_index, target_strip,
                                    target_strip->count(),
                                    AddTabTypes::ADD_NONE)) {
    LOG(WARNING) << "Atrium: RouteContentDrop detach failed for host "
                 << this;
    return;
  }
  target->window()->InsertPaneFromDrop(contents, edge);
  VLOG(1) << "Atrium: RouteContentDrop moved a tab from host " << this
          << " into host " << target << "'s content area, orientation "
          << static_cast<int>(orientation);
}

void BrowserController::RouteSidebarDrop(
    content::WebContents* contents,
    BrowserController* target) {
  if (!contents || !target || !browser_ || !target->browser_) {
    return;
  }
  TabStripModel* source_strip = browser_->tab_strip_model();
  const int source_index = source_strip->GetIndexOfWebContents(contents);
  if (source_index == TabStripModel::kNoTab) {
    LOG(WARNING) << "Atrium: RouteSidebarDrop given a WebContents not in "
                    "its own source strip";
    return;
  }

  const SidebarDropAction action =
      ChooseSidebarDropAction(profile_id_, target->profile_id_);
  if (action == SidebarDropAction::kAskBeforeMoving) {
    // The "ask for permission" wording: modal to the
    // TARGET window, since that is the one on screen at drop time.
    base::WeakPtr<content::WebContents> weak_contents =
        contents->GetWeakPtr();
    if (SidebarMoveConfirmResultForTesting().has_value()) {
      OnSidebarMoveConfirmed(weak_contents, target,
                             *SidebarMoveConfirmResultForTesting());
      return;
    }
    chrome::ShowQuestionMessageBoxAsync(
        target->window()->GetNativeWindow(),
        u"Move this tab to a different workspace?",
        u"This tab belongs to a different workspace than this window. Move "
        u"it here?",
        base::BindOnce(
            [](BrowserController* source,
               base::WeakPtr<content::WebContents> contents,
               BrowserController* target, chrome::MessageBoxResult result) {
              source->OnSidebarMoveConfirmed(
                  contents, target,
                  result == chrome::MESSAGE_BOX_RESULT_YES);
            },
            base::Unretained(this), weak_contents, base::Unretained(target)));
    return;
  }

  TabStripModel* target_strip = target->browser_->tab_strip_model();
  if (!MoveWebContentsBetweenStrips(source_strip, source_index, target_strip,
                                    target_strip->count())) {
    LOG(WARNING) << "Atrium: RouteSidebarDrop detach failed for host "
                 << this;
    return;
  }
  VLOG(1) << "Atrium: RouteSidebarDrop moved a tab from host " << this
          << " into same-workspace host " << target;
}

void BrowserController::OnSidebarMoveConfirmed(
    base::WeakPtr<content::WebContents> contents,
    BrowserController* target,
    bool confirmed) {
  if (!confirmed) {
    VLOG(1) << "Atrium: sidebar cross-workspace move cancelled for host "
            << this;
    return;
  }
  if (!contents || !browser_ || !target->browser_) {
    LOG(WARNING) << "Atrium: sidebar cross-workspace move confirmed but the "
                    "tab or a window already went away";
    return;
  }
  TabStripModel* source_strip = browser_->tab_strip_model();
  const int source_index =
      source_strip->GetIndexOfWebContents(contents.get());
  if (source_index == TabStripModel::kNoTab) {
    LOG(WARNING) << "Atrium: sidebar cross-workspace move confirmed but the "
                    "tab left its source strip";
    return;
  }
  if (ChooseSidebarMoveExecution(profile_id_, target->profile_id_) ==
      SidebarMoveExecution::kReopenAndClose) {
    // Foreground: the user just confirmed this move, so the tab they moved is
    // the one they want to be looking at.
    ReopenTabInOtherProfile(source_strip, source_index, target->browser_,
                            /*foreground=*/true, "sidebar cross-profile move");
    return;
  }

  TabStripModel* target_strip = target->browser_->tab_strip_model();
  if (!MoveWebContentsBetweenStrips(source_strip, source_index, target_strip,
                                    target_strip->count())) {
    LOG(WARNING) << "Atrium: RouteSidebarDrop detach failed for host "
                 << this;
    return;
  }
  VLOG(1) << "Atrium: RouteSidebarDrop moved a tab from host " << this
          << " into cross-workspace host " << target << " after confirmation";
}

// static
BrowserController* BrowserController::CreateLittleWindowForExternalLink(
    const GURL& url) {
  // The same scan ReopenFromDock's branch 2 uses, and for the same reason: a
  // handed-over link names no window, so the only profile anything here can
  // resolve is a resident one.
  Profile* profile = nullptr;
  for (BrowserController* controller : Registry()) {
    if (controller->browser_) {
      profile = controller->browser_->profile();
      break;
    }
  }
  if (!profile) {
    // A link that cold-started the app arrives here: openURLs runs before any
    // controller exists. ERROR rather than WARNING for ReopenFromDock's
    // reason - this build filters below ERROR, and a decisive "did nothing"
    // outcome that is invisible reads as a pass.
    LOG(ERROR) << "Atrium: external link found no resident profile; "
                  "falling through to the stock open path";
    return nullptr;
  }
  return CreateLittleWindow(profile, url);
}

// static
bool BrowserController::ReopenFromDock() {
  // this used to require the dylib's spaces group, so since the
  // cut a Dock click fell through to stock Chromium. Everything below is
  // C++ - the presenter table, the registry scan, the new slot - so the gate
  // goes and the Dock icon reopens an Atrium window again.
  // Branch 1 (see the header doc): surface an existing user-facing window
  // (the app-hidden/Cmd+H case). "User-facing" is defined by the
  // presentation table: a controller that is its own slot's presenter has a
  // real on-screen window; hidden-pool controllers never present and a
  // closed window's OnTeardown already cleared its entry, so neither can
  // match here.
  for (BrowserController* controller : Registry()) {
    if (!controller->browser_) {
      continue;
    }
    std::optional<ControllerId> presenter_id =
        PresentationTable().ResolvePresenter(controller->slot_);
    if (presenter_id && *presenter_id == IdOf(controller)) {
      // Crash: a presenter entry does NOT imply a showable window.
      // Closing a window destroys its NSWindow immediately, while the
      // controller's teardown - which clears this very entry - runs later, so
      // in that interval the table still names a controller whose window is
      // already unrealized. Show()ing it aborts the browser on
      // SetExcludedFromWindowCycling's CHECK(host), which is exactly what a
      // Dock click right after closing the last window used to do
      // (reproduced: branch=1 slot=0 realized=0).
      //
      // Skip rather than surface: an unrealized window is not user-facing, so
      // the correct outcome is to keep looking and, failing that, fall
      // through to branch 2 and open a fresh window - which is what the user
      // asked for by clicking the Dock icon.
      if (!controller->window() ||
          !controller->window()->HasRealizedNativeWindow()) {
        VLOG(1) << "Atrium: dock reopen skipping unrealized presenter for "
                   "window-slot "
                << controller->slot_ << " (teardown in flight)";
        continue;
      }
      VLOG(1) << "Atrium: dock reopen surfacing existing window-slot "
              << controller->slot_;
      controller->window()->Show();
      return true;
    }
  }
  // Branch 2: no user-facing window - open a fresh slot for the active
  // space's profile, Cmd+N posture (no tab; palette below).
  // the active space's profile came from CopyStartupTabJSON,
  // which only Swift could answer, so this scan has been the only path that
  // found a profile since the cut. The first question may get a
  // source again, and this stays the fallback.
  Profile* profile = nullptr;
  for (BrowserController* controller : Registry()) {
    if (controller->browser_) {
      profile = controller->browser_->profile();
      break;
    }
  }
  if (!profile) {
    // Nothing to reopen with (no resident profile, no live controller) -
    // let the stock path handle it rather than failing silently.
    // kept at ERROR rather than WARNING. This build filters below
    // ERROR, so as a WARNING this decisive "reopen did nothing" outcome was
    // invisible - which made scripted repro attempts look like they had
    // survived when they had simply fallen through to here.
    LOG(ERROR) << "Atrium: dock reopen found no usable profile; "
                  "falling through to stock reopen";
    return false;
  }
  const uint64_t new_slot = NextSlotId();
  BrowserController* controller = new BrowserController(
      profile, /*show=*/true, /*startup_url=*/"", new_slot, kDefaultSlot);
  VLOG(1) << "Atrium: dock reopen opened window-slot " << new_slot
          << " for host " << controller;
  controller->OpenCommandBar();
  return true;
}

// static
BrowserController* BrowserController::ControllerFor(BrowserController* host) {
  if (!host) {
    return nullptr;
  }
  for (BrowserController* controller : Registry()) {
    if (controller == host) {
      return controller;
    }
  }
  return nullptr;
}

// static
BrowserController* BrowserController::ControllerFor(
    uint64_t slot,
    const std::string& profile_id) {
  for (BrowserController* controller : Registry()) {
    if (controller->slot_ == slot && controller->profile_id_ == profile_id) {
      return controller;
    }
  }
  return nullptr;
}

// static
BrowserController* BrowserController::LiveControllerForProfile(
    const BrowserController* near,
    const Profile* profile) {
  BrowserController* found = nullptr;
  for (BrowserController* controller : Registry()) {
    if (!controller->browser_ || controller->browser_->profile() != profile) {
      continue;
    }
    if (controller->slot_ == near->slot_) {
      return controller;
    }
    found = found ? found : controller;
  }
  return found;
}

// static
BrowserController* BrowserController::ControllerForBrowser(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  for (BrowserController* controller : Registry()) {
    if (controller->browser_ == browser) {
      return controller;
    }
  }
  return nullptr;
}

// static
uint64_t BrowserController::SlotOf(BrowserController* controller) {
  return controller ? controller->slot_ : kDefaultSlot;
}

// static
AtriumBrowserWindow* BrowserController::PresentedWindowForBrowser(
    Browser* browser) {
  BrowserController* owner = ControllerForBrowser(browser);
  if (!owner) {
    return nullptr;
  }
  std::optional<ControllerId> presenter_id =
      PresentationTable().ResolvePresenter(owner->slot_);
  BrowserController* presenter =
      presenter_id ? ControllerOf(*presenter_id) : nullptr;
  if (!presenter || !presenter->browser_) {
    return nullptr;
  }
  return presenter->window();
}

// static
Browser* BrowserController::VisibleBrowserForBrowser(Browser* browser) {
  BrowserController* owner = ControllerForBrowser(browser);
  if (!owner) {
    return nullptr;
  }
  std::optional<ControllerId> visible_id =
      PresentationTable().ResolveVisible(owner->slot_);
  BrowserController* visible =
      visible_id ? ControllerOf(*visible_id) : nullptr;
  if (!visible || !visible->browser_) {
    return nullptr;
  }
  return visible->browser_.get();
}

// static
int BrowserController::SpaceIndexForBrowser(Browser* browser) {
  BrowserController* owner = ControllerForBrowser(browser);
  return owner ? SpaceIndexForSlot(owner->slot_)
               : atrium_core_active_space_index();
}

// static
std::optional<AtriumBrowserWindow*>
BrowserController::DisplayingWindowForBrowser(Browser* browser) {
  BrowserController* owner = ControllerForBrowser(browser);
  if (!owner || !owner->browser_) {
    return std::nullopt;
  }
  SlotPresentationTable& table = PresentationTable();
  BrowserController* presenter = ControllerOf(
      table.ResolvePresenter(owner->slot_).value_or(kInvalidControllerId));
  BrowserController* visible = ControllerOf(
      table.ResolveVisible(owner->slot_).value_or(kInvalidControllerId));
  const DisplayRouting routing = table.ResolveDisplayRouting(
      owner->slot_, IdOf(owner),
      /*presenter_live=*/presenter && presenter->browser_,
      /*visible_live=*/visible && visible->browser_);
  BrowserController* display = ControllerOf(routing.display);
  if (!display || !display->browser_) {
    // kInvalidControllerId is the routing answer for "displayed nowhere" (see
    // the header). A torn-down `display` folds in here too: the routing only
    // ever names `self` or a live presenter, so it is not reachable today, but
    // "no live window shows this content" is the same answer either way.
    return nullptr;
  }
  return display->window();
}

// static
bool BrowserController::TabIsOutsideActiveSpace(
    content::WebContents* contents) {
  if (!contents) {
    return false;
  }
  char* raw = atrium_core_space_id_for_tab(TabId(contents));
  if (!raw) {
    return false;
  }
  const std::string tab_space(raw);
  atrium_core_string_free(raw);
  int space_index = atrium_core_active_space_index();
  for (BrowserController* controller : Registry()) {
    if (controller->browser_ &&
        controller->browser_->tab_strip_model()->GetIndexOfWebContents(
            contents) != TabStripModel::kNoTab) {
      space_index = SpaceIndexForSlot(controller->slot_);
      break;
    }
  }
  char* active = atrium_core_space_id_at(space_index);
  if (!active) {
    return false;
  }
  const std::string active_space(active);
  atrium_core_string_free(active);
  return tab_space != active_space;
}

// static
// Attaches a controller to any Atrium (TYPE_NORMAL) Browser created OUTSIDE
// BrowserController's own self-creation paths - see InstallCollectionObserver
// and the class comment. A future chrome feature that calls Browser::Create()
// directly is the only path this currently exists for;
// Cmd+N never reaches it (CreateWindowSlot creates its own Browser, guarded
// by CreatingOwnBrowser() below).
class BrowserController::CollectionObserver : public BrowserCollectionObserver {
 public:
  CollectionObserver() {
    observation_.Observe(GlobalBrowserCollection::GetInstance());
  }
  CollectionObserver(const CollectionObserver&) = delete;
  CollectionObserver& operator=(const CollectionObserver&) = delete;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser_interface) override {
    if (CreatingOwnBrowser()) {
      // BrowserController's own constructor is on this exact call stack
      // (Browser::Create -> BrowserManagerService::AddBrowser ->
      // OnBrowserCreated, all synchronous) wiring this Browser up itself;
      // attaching a second controller here would double-attach.
      return;
    }
    Browser* browser = browser_interface->GetBrowserForMigrationOnly();
    if (!browser || !browser->is_type_normal()) {
      // Only TYPE_NORMAL Browsers get an AtriumBrowserWindow (same gate the
      // factory applies); anything else (popup, app, devtools) never does.
      return;
    }
    if (ControllerForBrowser(browser)) {
      return;  // Never double-attach (should not happen; defense in depth).
    }
    const uint64_t new_slot = NextSlotId();
    LOG(INFO) << "Atrium: attaching controller to a Browser created outside "
                 "BrowserController's own paths (window-slot " << new_slot
              << ")";
    // NOTE: the sidebar and tab events work immediately, but this path does
    // not call ActivateWindow/OnWindowShownInSlot.
    // A real future caller of this fallback needs to add that.
    new BrowserController(browser, new_slot);
  }

 private:
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

// static
std::unique_ptr<BrowserController::CollectionObserver>&
BrowserController::CollectionObserverSlot() {
  static base::NoDestructor<std::unique_ptr<CollectionObserver>> slot;
  return *slot;
}

// static
void BrowserController::InstallCollectionObserver() {
  if (!CollectionObserverSlot()) {
    CollectionObserverSlot() = std::make_unique<CollectionObserver>();
  }
}

// static
void BrowserController::UninstallCollectionObserver() {
  CollectionObserverSlot().reset();
}

// static
void BrowserController::ActivateWindow(BrowserController* target) {
  if (!target || !target->browser_) {
    return;
  }
  const uint64_t slot = target->slot_;
  SlotPresentationTable& table = PresentationTable();

  // Resolve the current visible/presenter ids and their liveness at the
  // boundary; the table decides the branch from these (it holds only opaque
  // ids, so liveness - controller && controller->browser_ - is computed here).
  BrowserController* previous =
      ControllerOf(table.ResolveVisible(slot).value_or(kInvalidControllerId));
  const bool previous_live = previous && previous->browser_;
  BrowserController* presenter_probe =
      ControllerOf(table.ResolvePresenter(slot).value_or(kInvalidControllerId));
  const bool presenter_live = presenter_probe && presenter_probe->browser_;

  // Committing the decision before the Cocoa ops is safe: nothing reachable
  // from the window calls below re-reads the table within this synchronous
  // call, and the old code's writes landed at the end of each branch anyway.
  const ActivateResult decision =
      table.OnActivate(slot, IdOf(target), previous_live, presenter_live);

  switch (decision.action) {
    case ActivateAction::kAlreadyVisible:
      // Nothing to do, and the label must stay: without it the function falls
      // through into the next case.
      return;

    case ActivateAction::kFirstActivation:
      // First activation in this slot (or the previously-visible controller's
      // Browser is already torn down): no fade partner, plain show. target's
      // own window becomes both this slot's shown content AND its physical
      // presenter - the two things that must be told apart once a later
      // in-window swap can leave them pointing at different controllers.
      target->window()->ShowInactive();
      target->window()->Show();
      // ShowInactive() already made the widget visible, so Show() took its
      // early return and skipped its visibility re-assert - do it here, where
      // the window is genuinely on screen. Covers e.g. the bootstrap
      // failure-promotion path (EnsureAtLeastOneTab on a hidden window, then
      // this activation).
      target->window()->AssertActiveTabVisible();
      target->window()->FocusWebContentsPane();
      // target's own sidebar build/push calls were gated (skipped,
      // not queued) for however long it sat hidden - the table above already
      // records it as this slot's presenter, so this one coalesced catch-up
      // (build, then push) brings its sidebar up to date with the
      // tab/title/favicon events it missed, and builds it in the first place
      // if this hidden-pool controller never had one (construction no longer
      // builds it - see AtriumBrowserWindow's ctor).
      target->window()->CatchUpSidebarForNewPresenter();
      target->SyncPeekWithPane();
      // A panel this Browser took back on a swap is still open, so it shows
      // here.
      target->window()->PresentExtensionPanelFor(target->browser_.get());
      VLOG(1) << "Atrium: activated window for host " << target
              << " in slot " << slot;
      // for an adopted target, ShowInactive() above already
      // consumed pending_adopt_shown_ once (NotifyShownIfAdopted).
      return;

    case ActivateAction::kInWindowSwap: {
      // In-window swap: the only cross-profile activation
      // path - keep this slot's PHYSICAL presenter window exactly as it is
      // (never Show()/Hide() anything) and swap target's active WebContents
      // into the presenter's already-on-screen pane. The table resolves the
      // presenter (not `previous`): after prior swaps `previous` names whoever's
      // CONTENT is shown, not necessarily whose NSWindow is on screen. Covers a
      // genuine cross-profile switch AND switching back to the presenter's own
      // profile - both reduce to "swap the pane, window never moves".
      BrowserController* presenter = ControllerOf(decision.presenter);
      content::WebContents* incoming_contents =
          target->browser_->tab_strip_model()->GetActiveWebContents();
      // capture whatever the presenter's pane is CURRENTLY showing
      // BEFORE ShowTab detaches it - see NudgeAutoPipOnPresenterSwap's doc
      // for why this swap is otherwise invisible to stock Chromium's
      // per-tab auto-pip.
      content::WebContents* outgoing_contents =
          presenter->window()->CurrentlyDisplayedWebContents();
      // Single-embed invariant. `incoming_contents` is target's active
      // WebContents, and target's OWN window pane may still host it from an
      // earlier presentation (target was previously shown as its own presenter,
      // or a prior same-profile switch routed ShowTab to target's window). The
      // ShowTab below re-embeds it into the presenter's pane, but
      // WebView::SetWebContents does NOT detach target's stale holder - leaving
      // one WebContents embedded in two panes' NativeViewHosts. A later detach
      // on one holder nulls the shared WebContentsViewMac::views_host_, and the
      // other holder's next detach hits DCHECK(views_host_) and aborts (the
      // cold/pinned-tab-select crash, and the "swapped-away tab stays visible"
      // symptom). Release it from target's own pane FIRST - before the presenter
      // attach, so views_host_ is repointed cleanly rather than double-nulled -
      // and only when target is not itself the presenter. A switch-back to the
      // presenter's OWN profile has presenter == target, so target->window() ==
      // presenter->window(): there is nothing to release (we'd be detaching from
      // the very pane we're about to attach into), and ShowTab handles it. The
      // identity guard inside ReleaseDisplayedContents makes this safe even if
      // target's pane no longer holds `incoming_contents`.
      // ORDERING CONSTRAINT (do not reorder): the ReleaseDisplayedContents
      // above must stay strictly BEFORE the ShowTab attach below. Releasing
      // first repoints WebContentsViewMac::views_host_ cleanly; attaching first
      // would leave `incoming_contents` embedded in two panes and abort on the
      // next detach (the double-embed DCHECK).
      //
      // `target->window()` is CORRECT here, and deliberately
      // not routed through the display-routing helper the way the other sites
      // in this class were. It is asking a narrower question than "which pane
      // shows target's content": it is asking "does target's OWN pane still
      // hold `incoming_contents` from an earlier presentation". Traced over
      // every state `incoming_contents` can be in at this point, with V = the
      // previously-visible controller, P = presenter (P != target, live):
      //   - target's own pane: REACHABLE, and the only reachable holder. While
      //     target was a background controller the routing sent its tab-strip
      //     updates to its own window (ResolveDisplayRouting's state 3), so its
      //     pane holds this exact WebContents. This release is therefore
      //     load-bearing, not vestigial - do not delete it as dead code.
      //   - P's pane: holds V's content, not target's. For P's pane to hold
      //     target's content, target would have to be the slot's visible
      //     controller, which contradicts target != V.
      //   - a third window's pane: a WebContents belongs to exactly one
      //     Browser's strip, and a same-profile controller in another slot is a
      //     different Browser - it can only hold this contents after an
      //     ActivateTab move, which also removes it from target's strip.
      //   - the peek widget: covered inside ShowTab, which calls
      //     ReleasePeekClaim before claiming.
      //   - P's INACTIVE split pane (P split while presenting target earlier,
      //     then swapped to V): can retain a target-profile tab, but the
      //     ShowTab below then takes its split branch, which either just flips
      //     the active pane index (the contents is already that pane's - no
      //     second attach) or replaces the ACTIVE pane's contents only. Still
      //     one holder per WebContents.
      // So no double-embed is constructible through this line, and rerouting it
      // through the helper would only widen a crash-adjacent path for no
      // behavior change. Left exactly as the single-embed fix wrote it.
      if (presenter != target) {
        target->window()->ReleaseDisplayedContents(incoming_contents);
      }
      // Reuses ShowTab's exact same-profile attach recipe. Deliberately NOT
      // followed by FocusWebContentsPane(): the same-profile tab-switch call
      // site (OnTabStripModelChanged) doesn't call one either, and that path is
      // the one already known to leave Cmd+Q working - WebView::SetWebContents
      // transfers focus to the newly-attached content when its host window is
      // already key, which the presenter always is here (this branch never
      // touches window show/hide/key state).
      // the presenter's on-screen pane is about to show different
      // content - a popup anchored to (or over) it no longer makes sense.
      // Closes `presenter`'s popup, not `target`'s: this branch never
      // shows/hides target's own window (see the comment above), so a popup
      // target had open in its OWN window, if any, is untouched here.
      presenter->CloseExtensionPopup();
      presenter->window()->ShowTab(incoming_contents);
      target->SyncPeekWithPane();
      if (AutoPipEnabledFlag()) {
        NudgeAutoPipOnPresenterSwap(outgoing_contents, incoming_contents);
      }
      // `presenter_` is intentionally left unchanged by
      // this branch (slot_presentation_table.cc's OnActivate comment), so
      // PushSidebarSnapshot's is_presenter gate never re-fires here - it only
      // reacts to a NEW presenter, and this swap keeps the same one. But
      // `visible_` DID just move to `target`, and PushSidebarSnapshot's model
      // is built from SidebarTargetBrowser() (the slot's VISIBLE browser, not
      // the calling window's own), so the presenter's already-on-screen
      // sidebar_contents_ is now showing stale content until something else
      // happens to fire a push. Nothing else does: target's own tab-strip
      // events land on target's own (off-screen, invisible) window, and the
      // presenter's own tab strip did not change. One explicit push here,
      // on the presenter (whose sidebar_contents_ is the one actually on
      // screen), closes that gap.
      presenter->window()->PushSidebarSnapshot();
      // Each profile's Browser keeps its own side panel. The window shows the
      // one it now presents.
      presenter->window()->PresentExtensionPanelFor(target->browser_.get());
      // Same gap for the sidebar and devbar extension chips: target's own
      // toolbar model did not change, so only a forced push replaces the
      // presenter's stale actions with the ones on screen.
      if (target->extension_bridge_) {
        target->extension_bridge_->ResyncActiveTab(/*force=*/true);
      }
      VLOG(1) << "Atrium: in-window swap, host " << target
              << " slot " << slot << " presented via host "
              << presenter;
      return;
    }
  }
}

BrowserController::BrowserController(Profile* profile,
                                     bool show,
                                     const std::string& startup_url,
                                     uint64_t slot,
                                     std::optional<uint64_t> seed_from_slot,
                                     bool little)
    : slot_(slot),
      little_(little),
      profile_id_(profile->GetBaseName().AsUTF8Unsafe()) {
  Registry().push_back(this);
  atrium::SetTabOutsideActiveSpaceCheck(&TabIsOutsideActiveSpace);

  // get the spaces document into memory early, off this thread, so
  // the first sidebar push can build a model without reading a file. Returns
  // straight away and the sidebar falls back until it lands.
  OpenAtriumCoreForProfile(profile);

  Browser::CreateParams params(profile, /*user_gesture=*/true);
  // Chrome's own SessionService is disabled for every Atrium profile (see
  // atrium::ProfileBootstrap), but this is cheap belt-and-suspenders: it
  // also excludes the window from the app/popup untrusted-source checks
  // that ShouldTrackBrowser applies.
  params.omit_from_session_restore = true;
  // See CreatingOwnBrowser()'s doc: this Browser::Create() call synchronously
  // fires GlobalBrowserCollection's OnBrowserCreated (via
  // BrowserManagerService::AddBrowser) before returning - the flag tells
  // CollectionObserver to skip it, since this constructor is already wiring
  // it up.
  CreatingOwnBrowser() = true;
  // CRITICAL: the little marker must be armed around
  // this exact Browser::Create - browser_window_factory.cc reads it
  // synchronously inside CreateBrowserWindow to build the little
  // AtriumBrowserWindow variant. Same manual pairing as CreatingOwnBrowser
  // above (Chromium builds without exceptions, so non-RAII is safe here).
  if (little_) {
    CreatingLittleBrowser() = true;
  }
  browser_ = Browser::Create(params);
  CreatingLittleBrowser() = false;
  CreatingOwnBrowser() = false;
  // Observe before the first tab so the sidebar sees every insertion.
  browser_->tab_strip_model()->AddObserver(this);

  // Refcounted per profile across slots: a no-op if
  // this profile already has a kAtriumSpace keep-alive from an earlier slot
  // (or ProfileBootstrap's own bootstrap pass) - see AcquireProfileKeepAlive.
  // A no-op for the M2a fallback too (ProfileBootstrap::Instance() is null
  // when spaces isn't connected), unchanged from M2a's existing behavior of
  // relying solely on Chrome's own per-Browser keep-alive.
  ProfileBootstrap::AcquireProfileKeepAliveForSlot(profile);

  // Before the sidebar below, because its first frame draws this space. A
  // caller that already chose the slot's space keeps it.
  if (seed_from_slot && !SlotSpaces().contains(slot_)) {
    SetSlotSpace(slot_, SpaceIndexForSlot(*seed_from_slot));
  }

  // Little Atrium: a little window skips the ENTIRE
  // sidebar/sidebar-overlay/RegisterHostInSlot/command-bar/extension-bridge
  // block below - it gets a minimal top chrome strip instead (read-only URL
  // display for Stage 1) and is NEVER registered as a Swift host (no
  // RegisterHostInSlot call at all - a future promote stage's profile-scoped
  // tab-move scan finds it via Registry() instead, same as every other
  // controller). This whole ctor still runs inside the CreatingOwnBrowser()
  // bracket the caller (CreateLittleWindow, like every self-creating factory
  // above) set around Browser::Create(), so CollectionObserver still skips
  // adopting this Browser a second time.
  // Peek mode: built once,
  // here, alongside the sidebar/command bar - unlike those two, peek has no
  // NSView to hand over and no availability gate on the widget's own
  // existence (see UIBridge::peek_connected()'s doc - only the browser.cc
  // intercept is gated), so this call is unconditional.
  window()->EnsurePeekWidget();

  // Peek mode: build the chrome-strip NSView now (mirrors the command
  // bar's construction-time build just above) and attach it into the
  // widget's reserved NativeViewHost strip - built once, hidden until
  // OpenPeek() shows it. Gated on peek_connected() like command_bar_view_
  // above: a dylib without the peek group leaves the strip unattached
  // (PeekRootView renders an empty band).
  if (!little_) {
    // Warm the AutocompleteController and push
    // the default search engine template at registration, not on first
    // keystroke, so the async InMemoryURLIndex rebuild has a head start.
    //
    // built unconditionally. It used to be gated on the dylib
    // exporting the command-bar group, which since the cut is never - and the
    // palette that consumes it is now an HTML surface with no dylib in the
    // path at all.
    command_bar_bridge_ = std::make_unique<AutocompleteBridge>(profile);
    command_bar_bridge_->SetMatchesCallback(base::BindRepeating(
        &AtriumBrowserWindow::PushCommandBarMatches, window()->GetWeakPtr()));
    command_bar_bridge_->SetRowsSupplier(base::BindRepeating(
        &BrowserController::CommandBarRowsForPush, base::Unretained(this)));

    // observe this profile's extension action model and push the
    // initial active-tab snapshot now, unconditional for the same reason as
    // command_bar_bridge_ above - the snapshot's consumers are HTML surfaces.
    extension_bridge_ =
        std::make_unique<ExtensionActionBridge>(browser_.get());
    // same registration-time build as extension_bridge_ above.
    extension_popup_host_ = std::make_unique<ExtensionPopupHost>(browser_.get());
  }

  // A hidden-pool member (show=false) is constructed but never Shown/Hidden,
  // so it never picks up AtriumBrowserWindow::Hide()'s automatic exclusion -
  // exclude it explicitly here so it never clutters the
  // Window menu / Cmd-~ cycling / Mission Control. The show=true path below
  // gets included via its own Show() call.
  if (!show) {
    window()->SetExcludedFromWindowCycling(true);
  }

  // Lazy restore: only the shown controller gets a tab at
  // construction time, and it's the one URL the bootstrap decided to
  // materialize (or the M2a fallback's fixed example.com) - every hidden
  // controller starts with zero tabs and stays cold until the user visits
  // that space (adoptColdTab / cross-profile switchToSpace).
  //
  // An EMPTY startup_url with show=true (M2c Arc-parity: Cmd+N via
  // CreateWindowSlot) opens the window with NO tab at all - sidebar + tinted
  // backdrop, no web content - rather than falling back to about:blank like
  // an invalid one does. AtriumBrowserWindow::Show() tolerates a tab-less
  // strip (guards GetWebContents() the same way AssertActiveTabVisible()
  // already does), and OnTabStripModelChanged's empty-strip ShowTab(nullptr)
  // is the same path that already keeps the backdrop visible for a window
  // whose last tab just went cold - both land here with zero tabs
  // and a visible window, so this is not new territory.
  if (show) {
    if (!startup_url.empty()) {
      GURL gurl(startup_url);
      // the same three schemes a reopen allows, and for the same
      // reason - a startup row comes out of the persisted document, which a
      // compromised chrome://atrium renderer can write to. is_valid() alone let
      // file:/// and javascript: through, and this navigates with
      // PAGE_TRANSITION_TYPED. The core refuses these on the way IN now; this is
      // the second half, because a document written by an older build is still
      // on disk.
      const bool scheme_allowed =
          gurl.SchemeIs(url::kHttpScheme) || gurl.SchemeIs(url::kHttpsScheme) ||
          gurl.SchemeIs(url::kAboutScheme);
      if (!gurl.is_valid() || !scheme_allowed) {
        LOG(WARNING) << "Atrium: refusing startup URL with scheme '"
                     << gurl.scheme() << "'; using about:blank";
        gurl = GURL("about:blank");
      }
      // Non-routed, defensively. This one used to be safe by ordering alone -
      // it runs before this host's routing wires - and relying on that ordering
      // is what the old notes queued a bypass here to stop doing.
      // A startup tab on a favourite's url is that tile's tab. Claimed here,
      // or the tile's first click would open a second copy of it.
      if (atrium_core_is_open() &&
          atrium_core_is_favorite_url(profile_id_.c_str(),
                                      gurl.spec().c_str())) {
        pending_favorite_key_ = gurl.spec();
      }
      const ScopedRoutingBypass no_routing;
      chrome::AddTabAt(browser_.get(), gurl, -1, /*foreground=*/true);
    }
    browser_->window()->Show();
    // a constructor-shown window seeds Visible, so a later
    // ActivateWindow(this) takes its `previous == target` early return and the
    // presenter-seeding first-activation branch never runs - leaving the slot
    // with no physical presenter. OnWindowShown seeds BOTH: every bootstrap/
    // initial/Cmd+N shown window IS its slot's physical presenter at birth.
    // Idempotent with teardown's `== this` clear; the tab-switch and theme/
    // cmd-q paths that resolve the presenter now find a real one instead of
    // falling back off-screen after a cross-profile in-window swap.
    PresentationTable().OnWindowShown(slot_, IdOf(this));
    // the startup tab added above ran OnTabStripModelChanged's
    // PushSidebarSnapshot before the table was seeded, so that push saw
    // "not yet a presenter" and was gated - catch it up now that it is one,
    // or a window shown at birth would open with an unbuilt sidebar model.
    // This is also the first opportunity to build this window's sidebar at
    // all: construction no longer does (see AtriumBrowserWindow's ctor).
    if (AtriumBrowserWindow* w = window()) {
      w->CatchUpSidebarForNewPresenter();
    }
  }
}

BrowserController::BrowserController(Browser* existing_browser, uint64_t slot)
    : slot_(slot),
      profile_id_(existing_browser->profile()->GetBaseName().AsUTF8Unsafe()) {
  Registry().push_back(this);
  atrium::SetTabOutsideActiveSpaceCheck(&TabIsOutsideActiveSpace);
  browser_ = existing_browser;
  // this ctor never fires the shown notification itself (see the
  // class-level doc above - the Browser's own creation path owns showing its
  // window, and it hasn't run yet at ctor time). Arm the flag so it fires
  // once, at whichever of AtriumBrowserWindow::Show()/ShowInactive() is the
  // FIRST to actually transition this window from hidden to visible -
  // ActivateWindow's kFirstActivation branch (a background-restored window's
  // first activation) drives ShowInactive() then Show(), and the latter's
  // early return (already-visible) means ShowInactive() is the one that
  // consumes this, not Show() - or never, for a window that's never shown.
  pending_adopt_shown_ = true;
  // Observe now: if any tabs already exist (not expected on this path - see
  // the ctor's class-level doc on ordering - but never trusted blindly),
  // AddObserver alone does not replay past insertions, so they are announced
  // explicitly just below, AFTER RegisterHostInSlot, preserving the same
  // ordering contract as the self-creating ctor.
  browser_->tab_strip_model()->AddObserver(this);

  ProfileBootstrap::AcquireProfileKeepAliveForSlot(existing_browser->profile());

  // Seed this slot's active space BEFORE the sidebar below is constructed -
  // the same load-bearing ordering the self-creating ctor documents. This
  // adopt path has no invoking slot, so it seeds from the default slot's
  // (persisted) active space - without ANY seed, Swift's
  // sidebarRows(forSlot:) renders the unfiltered M1 fallback (every live
  // tab, no favorites/pinned/groups, no backdrop tint) and, because slot
  // state is plain in-memory Swift state with no later invalidation, the
  // window stays that way until the user happens to switch spaces in it
  // (VM-probe finding, 2026-07-10: any externally-created Browser -
  // startup window without --no-startup-window, Dock "New Window",
  // promoted popups - hit this). Cross-profile nuance: if this Browser's
  // profile differs from the seeded space's, the sidebar shows the seeded
  // space until the user switches - same posture as Cmd+N's own seeding.
  //
  // the seeding itself was the Swift model's and is gone. An
  // adopted window shows the document's own active space until slots get
  // their own.

  // Peek mode: built once,
  // here, alongside the sidebar/command bar - unlike those two, peek has no
  // NSView to hand over and no availability gate on the widget's own
  // existence (see UIBridge::peek_connected()'s doc - only the browser.cc
  // intercept is gated), so this call is unconditional.
  window()->EnsurePeekWidget();

  // M4: same warm-up as the self-creating ctor above.
  command_bar_bridge_ =
      std::make_unique<AutocompleteBridge>(existing_browser->profile());
  command_bar_bridge_->SetMatchesCallback(base::BindRepeating(
      &AtriumBrowserWindow::PushCommandBarMatches, window()->GetWeakPtr()));
  command_bar_bridge_->SetRowsSupplier(base::BindRepeating(
      &BrowserController::CommandBarRowsForPush, base::Unretained(this)));

  extension_bridge_ =
      std::make_unique<ExtensionActionBridge>(browser_.get());
  extension_popup_host_ = std::make_unique<ExtensionPopupHost>(browser_.get());

  // Defensive replay (see ctor doc): announce any tab that beat this
  // constructor to the strip. Expected to be empty in every currently-known
  // caller (CollectionObserver::OnBrowserCreated fires before the creating
  // path's own AddTabAt - see BrowserManagerService::AddBrowser), but a
  // future OnBrowserCreated timing change must not silently drop tabs.
  TabStripModel* tab_strip = browser_->tab_strip_model();
  for (int i = 0; i < tab_strip->count(); ++i) {
    AddTab(tab_strip->GetWebContentsAt(i));
  }
}

void BrowserController::NotifyShownIfAdopted() {
  if (!pending_adopt_shown_) {
    return;
  }
  pending_adopt_shown_ = false;
  // Seeds this slot's presenter and visible state, deferred to
  // the window's real first Show() because the adopt ctor runs before it.
  PresentationTable().OnWindowShown(slot_, IdOf(this));
  // the adopt ctor's defensive AddTab replay ran before this
  // window was a presenter, so those PushSidebarSnapshot calls were gated -
  // catch up now that it is one, and build in the first place if this
  // window's sidebar was never built (construction no longer does).
  if (AtriumBrowserWindow* w = window()) {
    w->CatchUpSidebarForNewPresenter();
  }
}

BrowserController::~BrowserController() = default;

AtriumBrowserWindow* BrowserController::window() {
  // The static_cast is only valid because the browser window factory creates
  // an AtriumBrowserWindow for every normal-type Browser while connected.
  CHECK(browser_);
  BrowserWindow* window = browser_->window();
  CHECK(window);
  return static_cast<AtriumBrowserWindow*>(window);
}

void BrowserController::EnsureAtLeastOneTab() {
  if (browser_ && browser_->tab_strip_model()->empty()) {
    const ScopedRoutingBypass no_routing;
    chrome::AddTabAt(browser_.get(), GURL("about:blank"), -1,
                     /*foreground=*/true);
  }
}

bool BrowserController::ConsumeMoveOutEmptyFlag() {
  const bool result = pending_move_out_empty_;
  pending_move_out_empty_ = false;
  return result;
}

content::WebContents* BrowserController::ResolveTab(uint64_t tab_id,
                                                    int* index) {
  if (!browser_) {
    return nullptr;
  }
  TabStripModel* tab_strip = browser_->tab_strip_model();
  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(i);
    if (TabId(contents) == tab_id) {
      if (index) {
        *index = i;
      }
      return contents;
    }
  }
  return nullptr;
}

bool BrowserController::PersistsTabs() const {
  return browser_ && !browser_->profile()->IsOffTheRecord();
}

void BrowserController::AddTab(content::WebContents* contents,
                               int to_visible_slot) {
  const uint64_t id = TabId(contents);
  VLOG(1) << "Atrium: tab " << id << " added";
  // CRITICAL: a little window's tabs must never enter
  // the Swift space/tab pipeline. OnTabAdded2 would register the tab against
  // an unregistered host, TabListModel would stamp it with the AMBIENT
  // active space, and OnTabActivated's slot resolution would default it to
  // slot 0 - after which an ordinary space switch's activate_tab could
  // physically absorb the tab out of the little window via the
  // cross-controller move. Stage 1 skips the whole
  // forwarding set (add/close/activate/URL/title/favicon).
  //
  // it DOES get a forwarder, which is the "dedicated little-chrome
  // update channel" this comment used to anticipate. The forwarder makes no
  // Swift calls of its own; it exists so a navigation or a title change in the
  // little window reaches the foyer, which otherwise showed whatever the tab
  // was when the window opened.
  if (little_) {
    forwarders_[id] = std::make_unique<TabEventForwarder>(contents, this);
    if (AtriumBrowserWindow* w = window()) {
      w->PushFoyerSnapshot();
    }
    return;
  }
  // an incognito window writes nothing. Its rows would land in the
  // active NON-incognito space and outlive the session on disk.
  if (!PersistsTabs()) {
    // Logged so a scenario can assert the guard FIRED, rather than assert the
    // absence of a write - an absence passes just as well when the navigation
    // never happened. No url and no title here: this line exists because those
    // must not be recorded.
    VLOG(1) << "Atrium: tab " << id << " not persisted, window is off the record";
    return;
  }
  // A favourite's tab belongs to the PROFILE, so it joins no space's list.
  // Written into one it became a row of whatever space was showing, which is
  // the row that flashed and why no other space could bind the tile.
  const bool is_favorites_tab = !atrium::FavoriteKeyForTab(contents).empty();
  // the space's persisted tab list gains a row. The space on screen
  // can belong to another profile, which never draws this tab live, so a row
  // there stays cold and each click on it opens a new copy.
  const std::string url = contents->GetVisibleURL().spec();
  if (is_favorites_tab) {
    // A restored startup tab still has the row it came from. Bound, that row
    // hides behind the tile. Unbound, it was drawn cold beside it.
    if (atrium_core_bind_tab_to_row(SpaceIndexForSlot(slot_), id,
                                    url.c_str())) {
      if (AtriumBrowserWindow* w = window()) {
        w->PushSidebarSnapshot();
      }
    }
  } else {
    if (url.empty()) {
      // A renderer-created tab (target=_blank) enters the strip before its
      // navigation starts. The core refuses a row with no url, so the row is
      // owed until the first commit - see OpenDeferredRowForTab.
      tabs_owed_a_row_.insert(id);
    } else if (atrium_core_open_tab(
                   atrium_core_space_for_profile(SpaceIndexForSlot(slot_),
                                                 profile_id_.c_str()),
                   id, url.c_str(),
                   base::UTF16ToUTF8(contents->GetTitle()).c_str(),
                   to_visible_slot)) {
      if (AtriumBrowserWindow* w = window()) {
        w->ApplySidebarGesture(true);
      }
    }
  }
  // A background tab never activates, so this is its only stamp.
  atrium_core_touch_tab(id, RowInstantNow());
  // the tab is bound to its row now, so its Space is
  // resolvable - apply the hibernation policy before anything else touches
  // this tab's auto-discardable state.
  atrium::ApplyHibernatePolicyToTab(contents, id);
  forwarders_[id] = std::make_unique<TabEventForwarder>(contents, this);
  // PushCurrentFavicon, not an announce trio: PrimaryPageChanged and
  // TitleWasSet only fire for future changes, and the URL and title halves
  // went to the Swift model. The favicon half feeds FaviconCache, which is
  // where the HTML sidebar reads every row icon from.
  forwarders_[id]->PushCurrentFavicon();
  if (AtriumBrowserWindow* w = window()) {
    w->PushSidebarSnapshot();
  }
}

void BrowserController::OpenDeferredRowForTab(content::WebContents* contents,
                                              const GURL& page) {
  const uint64_t id = TabId(contents);
  if (!tabs_owed_a_row_.contains(id)) {
    return;
  }
  const std::string url = page.spec();
  if (url.empty()) {
    // Still owed. A commit reporting no url cannot open the row, and clearing
    // the debt here would lose it for every commit after this one.
    return;
  }
  tabs_owed_a_row_.erase(id);
  // The strip index now, not the one AddTab saw: a tab can be moved between
  // being inserted and committing its first page.
  const int to_visible_slot =
      browser_ ? browser_->tab_strip_model()->GetIndexOfWebContents(contents)
               : -1;
  if (atrium_core_open_tab(atrium_core_space_for_profile(
                               SpaceIndexForSlot(slot_), profile_id_.c_str()),
                           id, url.c_str(),
                           base::UTF16ToUTF8(contents->GetTitle()).c_str(),
                           to_visible_slot)) {
    atrium_core_touch_tab(id, RowInstantNow());
    if (AtriumBrowserWindow* w = window()) {
      w->ApplySidebarGesture(true);
    }
  }
  if (AtriumBrowserWindow* w = window()) {
    w->PushSidebarSnapshot();
  }
}

void BrowserController::RemoveTab(content::WebContents* contents) {
  const uint64_t id = TabId(contents);
  VLOG(1) << "Atrium: tab " << id << " closed";
  forwarders_.erase(id);
  tabs_owed_a_row_.erase(id);
  // nothing was written for this window, so there is nothing to
  // forget - and forget_tab/close_tab take a bare tab id, which could match a
  // row a NON-incognito window persisted.
  // before the incognito return below, not after. A timeline is
  // memory-only, so an incognito tab has none to drop - but a tab that closes
  // must never leave one behind whatever mode it was in, and putting this
  // ahead of the return is what makes that true by construction rather than by
  // the gate above happening to agree.
  atrium_core_forget_visits(id);
  VisitThumbnails().Forget(id);

  if (!PersistsTabs()) {
    return;
  }
  // This branch is the whole risk in the task. Chromium destroys
  // EVERY tab of a closing window through this same path, so treating a
  // teardown as a close would empty the space's persisted tab list on every
  // quit - worse than the drift the task exists to fix.
  //
  // A close deletes the row. A teardown only drops the live binding, so the
  // row survives and the next launch still has something to restore.
  if (TearingDown()) {
    atrium_core_forget_tab(id);
    return;  // The surface is going away; the shutdown flush does the save.
  }
  if (atrium_core_close_tab(id)) {
    if (AtriumBrowserWindow* w = window()) {
      w->ApplySidebarGesture(true);
    }
  }
}

void BrowserController::OnActiveTabNavigated(content::WebContents* contents) {
  if (!extension_bridge_ || !browser_) {
    return;
  }
  // A background tab's navigation never changes the active-tab snapshot, so
  // rebuild only when the navigating tab is the active one.
  if (browser_->tab_strip_model()->GetActiveWebContents() == contents) {
    extension_bridge_->ResyncActiveTab();
  }
}

void BrowserController::DetachTabForMove(content::WebContents* contents,
                                         TabRemovedReason reason) {
  const uint64_t id = TabId(contents);
  VLOG(1) << "Atrium: tab " << id << " detached (reason "
          << static_cast<int>(reason) << "), not closed";
  // The WebContents survives this controller (move to another Atrium
  // window, or a side panel) - erase only the forwarder so this controller
  // stops observing it. The destination controller's own kInserted handling
  // (AddTab) attaches a fresh TabEventForwarder; forwarders_ is per-instance,
  // so there is no double-attach on the SAME controller either way. The owed
  // row travels with it: the destination's AddTab decides again.
  forwarders_.erase(id);
  tabs_owed_a_row_.erase(id);
}

void BrowserController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Before the push below, which is the frame the flash lived in: a favourite's
  // tab arrives here unmarked, and an unmarked tab is an ordinary row.
  if (!pending_favorite_key_.empty() &&
      change.type() == TabStripModelChange::kInserted &&
      !change.GetInsert()->contents.empty()) {
    atrium::SetFavoriteKeyForTab(change.GetInsert()->contents.front().contents,
                                 pending_favorite_key_);
    pending_favorite_key_.clear();
  }
  // Before the push, not after: the push reads the cross-window tab list and
  // this change is exactly what makes the cached one wrong.
  atrium::InvalidateLiveTabsCache();

  // the HTML sidebar renders from TabStripModel, so every strip
  // change is a re-push. No-op on the SwiftUI arm.
  if (AtriumBrowserWindow* w = window()) {
    w->PushSidebarSnapshot();
  }

  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        AddTab(inserted.contents, inserted.index);
      }
      // A tab arriving in a window whose strip previously emptied: the
      // empty-strip auto-close was suppressed at the window level
      // (AtriumBrowserWindow::OnWindowCloseRequested keeps the window open
      // showing cold tabs), but UnloadController::TabStripEmpty() had
      // ALREADY latched is_attempting_to_close_browser_ - and that latch
      // makes the NEXT detach in this window run ProcessPendingTabs ->
      // Browser::OnWindowClosing() and destroy the Browser out from under
      // the still-open window (probe-caught: move-out-emptied window given
      // a new tab, that tab closed -> window vanished, browser_ nulled).
      // The window is demonstrably staying open - a tab just landed in it -
      // so unlatch. Guards: a genuine teardown (quit sweep, committed
      // close) must never be cancelled, but no tab can be inserted into a
      // delete-scheduled Browser's strip anyway - both checks are
      // belt-and-suspenders.
      if (browser_ && browser_->IsAttemptingToCloseBrowser() &&
          !browser_->IsDeleteScheduled() &&
          !browser_shutdown::IsTryingToQuit()) {
        VLOG(1) << "Atrium: tab added to a kept-open window - cancelling "
                   "UnloadController's latched close attempt";
        UnloadController::From(browser_.get())->CancelWindowClose();
      }
      break;
    case TabStripModelChange::kRemoved: {
      // Reason inventory (chrome/browser/tab_list/tab_removed_reason.h):
      // kDeleted / kDeletedAndExpandSidePanel actually destroy the
      // WebContents (WillDeleteWebContents() true) - a genuine close, so
      // RemoveTab deletes the tab's persisted state as always.
      // kInsertedIntoOtherTabStrip / kInsertedIntoSidePanel leave the
      // WebContents alive elsewhere (WillDeleteWebContents() false) - never
      // a close; DetachTabForMove suppresses OnTabClosed (a data-safety
      // fix).
      bool batch_had_move_out = false;
      // Split view (implicit
      // dissolution): a genuine close of either split-pane tab collapses
      // the split to the survivor deterministically - captured here (the
      // first WillDeleteWebContents id in this batch that's a split pane;
      // a split has exactly two panes, so at most one relevant removal per
      // batch in practice) rather than acted on inline, because the
      // survivor re-selection below is deferred - see the comment there.
      uint64_t closed_split_tab_id = 0;
      for (const auto& removed : change.GetRemove()->contents) {
        if (removed.contents == parking_contents_ ||
            TabRemoveReasonUtils::WillDeleteWebContents(removed.remove_reason)) {
          // a genuine close (not a move to another strip) drops the
          // tab from the recency list too, so a later Ctrl-Tab never walks
          // to it. TabId first - RemoveTab below only erases this
          // controller's own bookkeeping, not the id itself.
          atrium::RecordTabRecencyClosed(TabId(removed.contents));
          RemoveTab(removed.contents);
          if (closed_split_tab_id == 0) {
            closed_split_tab_id = TabId(removed.contents);
          }
        } else {
          DetachTabForMove(removed.contents, removed.remove_reason);
          if (removed.remove_reason ==
              TabRemovedReason::kInsertedIntoOtherTabStrip) {
            batch_had_move_out = true;
          }
        }
        // the source tab left this strip either way (closed or
        // dragged into another window) - a peek with no tab to belong to
        // must close outright, not sit hidden forever.
        if (peek_contents_ && peek_source_tab_id_ != 0 &&
            TabId(removed.contents) == peek_source_tab_id_) {
          ClosePeek();
        }
        std::erase_if(parked_peeks_, [&](const ParkedPeek& p) {
          return p.source_tab_id == TabId(removed.contents);
        });
        if (AtriumBrowserWindow* w = window()) {
          w->ForgetAiPanelTab(TabId(removed.contents));
        }
      }
      // Only arm the flag if the strip is ACTUALLY empty right now (checked
      // in the same call, before TabStripModel's own TabStripEmpty() fires
      // for this batch) - otherwise a move that leaves other tabs behind
      // would leave a stale flag for a later, unrelated genuine close to
      // wrongly consume (see ConsumeMoveOutEmptyFlag's doc).
      if (batch_had_move_out && tab_strip_model->empty()) {
        pending_move_out_empty_ = true;
      }
      // a genuine close that empties the strip leaves the window
      // blank while the space's other rows are still listed - a dead end the
      // user has to click their way out of. Bring the first survivor back.
      // Only for a genuine delete: a move-out means the tab is alive in
      // another window and the user is watching it there, so reopening
      // something here would be a surprise. Not during a quit either.
      //
      // DEFERRED one run-loop turn for the same reason the split survivor
      // above is: this callback IS TabStripModel's removal dispatch, and
      // adding a tab from inside it re-enters the strip's own machinery.
      // Posted to the controller rather than the window because
      // BrowserController is process-lifetime (see the class doc) and the
      // window is not - PromoteSurvivingRow re-resolves it on the way out.
      if (browser_ && !batch_had_move_out && tab_strip_model->empty() &&
          !browser_shutdown::IsTryingToQuit()) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&BrowserController::PromoteSurvivingRow,
                                      base::Unretained(this)));
      }
      if (browser_ && closed_split_tab_id != 0) {
        // Split state lives on the slot's PRESENTER (SetSplitOp/UpdateSplitOp
        // write it there), which may not be self's own (possibly off-screen)
        // window in the cross-profile in-window swap - route there,
        // not through window(). Falls back to self's window when no
        // presenter is mapped.
        AtriumBrowserWindow* presenter_window = PresentedWindowForBrowser(browser_);
        if (!presenter_window) {
          presenter_window = window();
        }
        // Drops the closed tab's pane now - purely our own views, no
        // TabStripModel interaction, safe from inside this callback. Past
        // two panes only this pane goes, and the rest keep their pages.
        const uint64_t survivor_id =
            presenter_window->ClosePaneForTab(closed_split_tab_id,
                                              "tab_closed");
        if (survivor_id != 0) {
          // The forced re-selection to the survivor is DEFERRED one
          // run-loop turn (unlike ArmMoveOutFreeze's synchronous
          // ActivateOwnTab call - see that method's doc for the contrast):
          // this callback IS TabStripModel's own removal notification
          // dispatch, still in progress - mutating the strip's selection
          // synchronously here would re-enter TabStripModel's own
          // machinery. TabStripModel's own auto-reselect may briefly show
          // the wrong tab for one run-loop turn; this corrects it before
          // any user input can land on it. BrowserController is
          // process-lifetime (see the class doc), so base::Unretained is
          // safe here.
          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&BrowserController::ActivateOwnTab,
                                       base::Unretained(this), survivor_id));
        }
      }
      // Chrome dereferences the incoming contents in OnActiveTabChanged, so
      // that hook never runs for an emptied strip and the floating address bar
      // stayed up over a window with no tab to address.
      if (tab_strip_model->empty()) {
        if (AtriumBrowserWindow* w = window()) {
          w->PushDevBarState();
        }
      }
      break;
    }
    case TabStripModelChange::kReplaced: {
      // Discard/prerender swap: same tab, new WebContents => new id.
      // the linkage between the old and new ids was announced here
      // first, so the model could carry favouriteKey, spaceID and MRU rank
      // across. Nothing carries them now - a discard or prerender swap loses
      // whatever the persisted document knew about that tab. Tab open and close
      // have the same hole.
      // the linkage first, so the new id inherits the old row
      // rather than the pair below deleting it and adding a fresh one, which
      // would lose whatever the document knew about that tab.
      // Gated with the pair below it: neither id was ever written, so the
      // linkage names two rows the document does not have - and a bare tab id
      // could match one a NON-incognito window persisted.
      if (PersistsTabs()) {
        atrium_core_note_tab_replaced(
            TabId(change.GetReplace()->old_contents),
            TabId(change.GetReplace()->new_contents));
      }
      RemoveTab(change.GetReplace()->old_contents);
      AddTab(change.GetReplace()->new_contents);
      // A peek belongs to the tab, not to the page the swap threw away.
      const uint64_t old_id = TabId(change.GetReplace()->old_contents);
      const uint64_t new_id = TabId(change.GetReplace()->new_contents);
      for (ParkedPeek& parked : parked_peeks_) {
        if (parked.source_tab_id == old_id) {
          parked.source_tab_id = new_id;
        }
      }
      if (peek_source_tab_id_ == old_id) {
        peek_source_tab_id_ = new_id;
        // The badge sat on the old id's row.
        if (peek_forwarder_) {
          peek_forwarder_->PushCurrentFavicon();
        }
      }
      break;
    }
    default:
      break;
  }

  if (selection.active_tab_changed()) {
    // feeds the recency list Ctrl-Tab walks. Recorded for every
    // activation, not just user-driven ones - the Rust side ignores this
    // while a cycle is held (see tab_recency.rs), which is what keeps the
    // walk from re-sorting itself under a held Ctrl-Tab.
    if (selection.new_contents) {
      atrium::RecordTabRecencyActivation(TabId(selection.new_contents));
      last_shown_tab_by_space_[SpaceIndexForSlot(slot_)] =
          TabId(selection.new_contents);
      atrium_core_touch_tab(TabId(selection.new_contents), RowInstantNow());
    }
    // Chrome also routes activation through
    // BrowserWindow::OnActiveTabChanged; ShowTab is idempotent
    // (WebView::SetWebContents early-returns on the same contents). A null
    // new_contents (last tab closing) detaches the WebView before the
    // contents dies. ShowTab's return value tells us whether it actually
    // applied `selection.new_contents` or swallowed this call because this
    // window is mid-freeze (Arc-parity revision 2, ArmMoveOutFreeze) - the
    // Swift forward below must follow suit: recording the auto-reselected
    // tab as this slot's selection would reintroduce the exact bug the
    // freeze exists to kill.
    if (browser_ && browser_->window()) {
      // This controller's content may be the one shown for
      // its slot while a DIFFERENT controller's NSWindow is the physical
      // presenter (an in-window cross-profile swap happened earlier - see
      // ActivateWindow). Such a tab-strip update (a tab switch, a new tab, or
      // a tab close) belongs in the presenter's pane, not in this
      // controller's own possibly off-screen window.
      //
      // The 2026-07-26 views_host_ abort: that decision is re-made
      // on EVERY tab-strip event and FLIPS the moment this controller becomes
      // - or stops being - its slot's visible one, which OnActivate does
      // synchronously on any cross-profile space switch. ShowTab attaches into
      // whichever pane it is handed while only detaching THAT pane's previous
      // contents, so across a flip both panes end up holding one WebContents.
      // WebContentsViewMac keeps a SINGLE views_host_ that ViewsHostableAttach
      // overwrites unguarded, so the loser's next detach finds it already
      // nulled and aborts on DCHECK(views_host_) - far from here, in the other
      // window, and naming nothing (which is why the crash surfaced inside
      // ReleaseDisplayedContents five seconds later).
      //
      // ResolveDisplayRouting returns both the winner and the loser;
      // MakePaneAttachPlan then decides whether the loser really needs
      // releasing and in WHICH ORDER. Both live in slot_presentation_table so
      // they are unit-testable without a browser - the guard and the ordering
      // are the two things that would silently reintroduce the abort.
      // ReleaseDisplayedContents is itself pointer-guarded and scans both
      // panes, so a release is a no-op unless that window genuinely still holds
      // the contents.
      //
      // the routing has a THIRD answer - "displayed nowhere" - for
      // when THIS controller is the slot's presenter but not its visible one.
      // Its own window is then the on-screen one, showing the visible
      // controller's tab, so an attach here evicts what the user is looking at.
      // See ResolveDisplayRouting's three-state doc; MakePaneAttachPlan turns
      // that answer into an EMPTY step list, which is why the loop below can
      // stay a plain execute-the-plan loop.
      SlotPresentationTable& table = PresentationTable();
      const ControllerId presenter_id =
          table.ResolvePresenter(slot_).value_or(kInvalidControllerId);
      BrowserController* presenter = ControllerOf(presenter_id);
      BrowserController* visible = ControllerOf(
          table.ResolveVisible(slot_).value_or(kInvalidControllerId));
      const DisplayRouting routing = table.ResolveDisplayRouting(
          slot_, IdOf(this),
          /*presenter_live=*/presenter && presenter->browser_,
          /*visible_live=*/visible && visible->browser_);

      BrowserController* display = ControllerOf(routing.display);
      BrowserController* stale = ControllerOf(routing.stale);
      AtriumBrowserWindow* display_window =
          (display && display->browser_) ? display->window() : window();

      PaneAttachInputs inputs;
      inputs.routing = routing;
      inputs.self = IdOf(this);
      inputs.display_live = display && display->browser_;
      inputs.stale_live = stale && stale->browser_;
      inputs.has_contents = selection.new_contents != nullptr;
      inputs.stale_shares_display_window =
          inputs.stale_live && stale->window() == display_window;
      const PaneAttachPlan plan = MakePaneAttachPlan(inputs);
      auto outcome = AtriumBrowserWindow::ShowTabOutcome::kShown;

      // the empty plan. What must NOT happen is touching a pane.
      if (plan.steps.empty()) {
        VLOG(1) << "Atrium: tab-strip update for host " << this << " slot "
                << slot_
                << " displayed nowhere - this controller presents the slot but "
                   "is not its visible one";
      }
      // Executed in the plan's order, not in a hardcoded one: that is what
      // makes MakePaneAttachPlan's step sequence load-bearing rather than
      // decorative, so a test asserting the order actually pins production.
      for (PaneStep step : plan.steps) {
        switch (step) {
          case PaneStep::kRelease:
            // Guards the dereference on the next line. What makes this hold is
            // upstream and structural, not local: MakePaneAttachPlan only emits
            // kRelease when `inputs.stale_live` was set, and the caller sets it
            // to `stale && stale->browser_` above. So a refactor that breaks
            // that gating is the thing this catches - and it must abort with a
            // labelled message rather than null-deref, because turning silent
            // corruption into a visible, diagnosable failure is the whole point
            // of this fix.
            CHECK(stale);
            stale->window()->ReleaseDisplayedContents(selection.new_contents);
            break;
          case PaneStep::kAttach:
            outcome = display_window->ShowTab(selection.new_contents);
            break;
        }
      }
      // kRefused alone, never a bare "not shown": a kSwallowed auto-reselect
      // is the move-out freeze holding the pane still, and this task's
      // ActivateOwnTab would break it.
      if (outcome == AtriumBrowserWindow::ShowTabOutcome::kRefused &&
          PersistsTabs() && !TearingDown() &&
          TabIsOutsideActiveSpace(selection.new_contents)) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&BrowserController::ReselectTabInActiveSpace,
                           weak_factory_.GetWeakPtr()));
      }
    }
    // After the pane took the new tab, which is what the peek follows.
    SyncPeekWithPane();
    if (selection.old_contents) {
      HandBackFrozenTab(TabId(selection.old_contents));
    }
    // edit focus is per-tab too, and Swift has ALREADY moved its own
    // activeTabID by the time this runs (a sidebar click switches tabs Swift-
    // side first). Until this echo lands, Swift's cached snapshot names the
    // previous tab and its own gate treats it as stale - so this push is what
    // ends that fail-closed window, in one round trip. Unconditional (not
    // gated on `shown`): the strip's active tab is what a keystroke would land
    // in, whether or not this window's pane got to display it.
    PushEditFocus();
    // extension action badge/visible state is per-tab, so a tab
    // switch changes the whole snapshot even with no extension event - rebuild
    // against the now-active tab (see the retired ABI header's EXTENSIONS doc block).
    if (extension_bridge_) {
      extension_bridge_->ResyncActiveTab();
    }
  }
}

void BrowserController::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  forwarders_.clear();
  // A strip destroyed with its tabs still in it sends no removal, so the
  // cached cross-window list would keep this Browser past its death.
  atrium::InvalidateLiveTabsCache();
  // Peek mode: destroy any live peek WebContents BEFORE browser_ is
  // nulled below - ClosePeek()'s normal path calls window()->ClosePeek(),
  // which requires a non-null browser_, but that's moot here anyway (the
  // peek widget itself is about to be torn down by AtriumBrowserWindow's own
  // destructor), so this resets the state directly instead of going through
  // ClosePeek().
  // Except on another controller's presenter window, which outlives this strip.
  if (peek_contents_ && browser_) {
    std::optional<ControllerId> presenter_id =
        PresentationTable().ResolvePresenter(slot_);
    BrowserController* presenter =
        presenter_id ? ControllerOf(*presenter_id) : nullptr;
    if (presenter && presenter != this && presenter->browser_) {
      AtriumBrowserWindow* shared = presenter->window();
      shared->ClosePeek(peek_contents_.get(), /*animate=*/false);
      shared->DetachPeekWebContents(peek_contents_.get());
    }
  }
  peek_forwarder_.reset();
  peek_contents_.reset();
  parked_peeks_.clear();
  // Favicon lookup (cpp-reviewer finding): cancels any outstanding
  // favicon::FaviconService lookups so their TaskId bookkeeping doesn't
  // linger in the tracker for the rest of this process-lifetime controller's
  // life. The callback itself never touches browser_/self (see
  // RequestFaviconForURLOp), so this is purely a bookkeeping cleanup, not a
  // dangling-pointer guard - same "release owned state before browser_ goes
  // null" treatment as command_bar_bridge_.reset() below.
  favicon_lookup_tracker_.TryCancelAll();
  // The cancelled callbacks are what would have freed these slots, so without
  // this the backfill stops for good after enough teardowns. Only this
  // controller's own: the queue is shared by every window of the profile.
  if (!favicon_in_flight_.empty()) {
    VLOG(1) << "Atrium: favicon backfill dropped " << favicon_in_flight_.size()
            << " lookups at strip teardown";
  }
  // AbandonAll runs first so no delegate fetch runs without browser_.
  row_icon_lookup_.AbandonAll();
  ReleaseFaviconSlots({favicon_in_flight_.begin(), favicon_in_flight_.end()});
  // drop the ToolbarActionsModel observer (and every per-extension
  // icon factory) before browser_ is nulled below - same release-on-teardown
  // treatment as command_bar_bridge_, and the observed model outlives the
  // strip so the ScopedObservation unwinds cleanly here.
  extension_bridge_.reset();
  // destroy any open popup synchronously BEFORE browser_ is nulled
  // just below - ExtensionPopupHost (via ExtensionPopup) holds a
  // raw_ptr<Browser>, so this ordering is load-bearing, exactly like
  // extension_bridge_.reset() just above. CloseNow() (not Close()) because
  // this is teardown, not a normal dismissal - the popup must be gone
  // synchronously, not deferred.
  if (extension_popup_host_) {
    extension_popup_host_->CloseNow();
  }
  // The window is closing, so its parked pages go with it.
  favorite_parking_.Clear();
  browser_ = nullptr;
  // M4: stop the controller and drop the TemplateURLService observer before
  // this controller's profile keep-alive is released just below - same
  // release-on-teardown treatment as sidebar_view_ above.
  command_bar_bridge_.reset();
  // Clear this slot's visible/presenter entries, but only where they still
  // name THIS controller (OnTeardown's `== id` guard), never stomping some
  // other still-live controller's entry. this controller's own
  // NSWindow is what just tore down, so if it was the slot's physical
  // presenter (hosting another profile's swapped-in content, or its own),
  // nothing is presenting for the slot anymore.
  const bool was_presenter =
      PresentationTable().ResolvePresenter(slot_) == IdOf(this);
  const bool was_visible =
      PresentationTable().ResolveVisible(slot_) == IdOf(this);
  PresentationTable().OnTeardown(slot_, IdOf(this));
  // This Browser was swapped into another window, which took its own panel
  // back for the swap. Nothing swaps that Browser in again, so show it here.
  if (was_visible && !was_presenter) {
    BrowserController* presenter =
        ControllerOf(PresentationTable().ResolvePresenter(slot_).value_or(
            kInvalidControllerId));
    if (presenter && presenter->browser_) {
      presenter->window()->PresentExtensionPanelFor(presenter->browser_.get());
    }
  }
  // A second window's hidden strips for other profiles have no window left to
  // show them in, and would keep their tabs alive with nothing on screen.
  if (was_presenter && slot_ != kDefaultSlot) {
    SlotSpaces().erase(slot_);
    std::vector<Browser*> hidden;
    for (BrowserController* controller : Registry()) {
      if (controller != this && controller->slot_ == slot_ &&
          controller->browser_) {
        hidden.push_back(controller->browser_.get());
      }
    }
    for (Browser* browser : hidden) {
      browser->window()->Close();
    }
  }
  // The release side of the acquire-only refcount. profile_id_ is the
  // cached string, not browser_->profile() - browser_ is nulled above, by
  // this exact function, so it is unusable here.
  ProfileBootstrap::ReleaseProfileKeepAliveForSlot(profile_id_);
}

// static
uint64_t BrowserController::FrozenTabIdForBrowser(Browser* browser) {
  if (!browser) {
    return 0;
  }
  // The window that DISPLAYS this browser's content is the frozen one, which
  // after a cross-profile in-window swap is not always its own.
  const std::optional<AtriumBrowserWindow*> displaying =
      DisplayingWindowForBrowser(browser);
  AtriumBrowserWindow* window = displaying.value_or(
      static_cast<AtriumBrowserWindow*>(browser->window()));
  return window ? window->FrozenTabId() : 0;
}

// static
std::optional<uint64_t> BrowserController::FavoriteTabElsewhere(
    BrowserController* host, const std::string& favorite_url) {
  BrowserController* self = ControllerFor(host);
  if (!self || favorite_url.empty()) {
    return std::nullopt;
  }
  for (BrowserController* source : Registry()) {
    if (source == self || source->profile_id_ != self->profile_id_ ||
        !source->browser_) {
      continue;
    }
    TabStripModel* strip = source->browser_->tab_strip_model();
    for (int i = 0; i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (contents && FavoriteKeyForTab(contents) == favorite_url) {
        return TabId(contents);
      }
    }
  }
  return std::nullopt;
}

void BrowserController::ActivateTab(BrowserController* host, uint64_t tab_id) {
  BrowserController* self = ControllerFor(host);
  if (!self) {
    LOG(WARNING) << "Atrium: ActivateTab dropped - host " << host
                 << " unresolved";
    return;
  }
  int index = -1;
  if (self->ResolveTab(tab_id, &index)) {
    // Fast path, unchanged: the tab already lives in this host's own strip.
    VLOG(1) << "Atrium: tab " << tab_id << " activated on host " << host;
    self->browser_->tab_strip_model()->ActivateTabAt(index);
    return;
  }
  if (!self->browser_) {
    LOG(WARNING) << "Atrium: ActivateTab dropped for host " << host
                 << " - controller's Browser already torn down";
    return;
  }

  // an off-the-record profile's GetBaseName() answers its
  // parent's path, so profile_id_ alone reads an OTR controller as the same
  // profile as its parent - compare the Profile pointer instead.
  for (BrowserController* source : Registry()) {
    if (source == self || !source->browser_ ||
        source->browser_->profile() != self->browser_->profile()) {
      continue;
    }
    int source_index = -1;
    if (!source->ResolveTab(tab_id, &source_index)) {
      continue;
    }
    // Arc-parity revision 2: if `source`'s window is CURRENTLY displaying
    // the tab being taken, freeze its pane on a desaturated snapshot before
    // the detach below - captured now because CopyFromSurface needs the
    // live compositor surface, which DetachTabAtForInsertion's own
    // TabStripModel notifications will otherwise race (and, absent the
    // freeze, that notification would auto-reselect another of `source`'s
    // own tabs if one is left, silently overriding `source`'s recorded
    // selection - the "per-slot last-active fallback activation on
    // move-out" bug this revision kills). A no-op if `source` isn't
    // currently showing this tab (see ArmMoveOutFreeze's doc).
    // freeze the window that actually DISPLAYS `source`'s content,
    // which is not always `source`'s own. After a cross-profile in-window swap
    // `source` can be its slot's VISIBLE controller while another
    // controller's window is the physical presenter showing its tab; the freeze
    // then has to arm on THAT window, because that is where the pane is and
    // where the auto-reselect's ShowTab will land. Armed on source's own window
    // instead, ArmMoveOutFreeze's "is this what I'm displaying?" check compares
    // against source's empty own pane, always fails, and returns without
    // arming - so nothing suppressed the auto-reselect and it hijacked the
    // on-screen pane, the exact bug this freeze exists to prevent.
    //
    // nullptr means source's content is displayed in no pane at all (source
    // presents the slot but is not its visible one): nothing on screen is
    // showing the departing tab, and arming source's own window would blank the
    // pane that is showing SOMEONE ELSE's tab. Skip entirely. std::nullopt
    // cannot happen here (source came from Registry() with a live browser_) but
    // keeps the old window as its fallback.
    const std::optional<AtriumBrowserWindow*> freeze_window =
        DisplayingWindowForBrowser(source->browser_);
    base::OnceClosure complete = base::BindOnce(
        &BrowserController::CompleteMoveIn, host, source, tab_id);
    if (AtriumBrowserWindow* window =
            freeze_window.value_or(source->window())) {
      // The detach waits for the freeze: the snapshot needs the live surface,
      // and a pane cleared before the frame landed painted blank in between.
      window->ArmMoveOutFreeze(
          source->browser_->tab_strip_model()->GetWebContentsAt(source_index),
          tab_id, std::move(complete));
    } else {
      std::move(complete).Run();
    }
    return;
  }

  // A mismatched host (tab_id belongs to no live, same-profile controller's
  // strip at all) resolves to nothing here - this is the only diagnostic
  // signal a misrouted or stale click leaves behind.
  LOG(WARNING) << "Atrium: ActivateTab dropped for host " << host << " tab "
               << tab_id << " - not found in this host's strip or any "
               << "same-profile controller's strip";
}

// static
void BrowserController::CompleteMoveIn(BrowserController* host,
                                       BrowserController* source,
                                       uint64_t tab_id) {
  BrowserController* self = ControllerFor(host);
  BrowserController* from = ControllerFor(source);
  if (!self || !self->browser_ || !from || !from->browser_) {
    LOG(WARNING) << "Atrium: ActivateTab move dropped for tab " << tab_id
                 << " - a window went away while the freeze was pending";
    return;
  }
  int source_index = -1;
  if (!from->ResolveTab(tab_id, &source_index)) {
    LOG(WARNING) << "Atrium: ActivateTab move dropped for tab " << tab_id
                 << " - it left host " << source
                 << " while the freeze was pending";
    return;
  }
  // ADD_ACTIVE, unlike Chromium's move-to-window: Arc activates the tab here.
  TabStripModel* target_strip = self->browser_->tab_strip_model();
  if (!MoveWebContentsBetweenStrips(from->browser_->tab_strip_model(),
                                    source_index, target_strip,
                                    target_strip->count(),
                                    AddTabTypes::ADD_ACTIVE)) {
    LOG(WARNING) << "Atrium: ActivateTab move dropped for tab " << tab_id
                 << " - detach from host " << source << " failed";
    return;
  }
  VLOG(1) << "Atrium: tab " << tab_id << " moved from host " << source
          << " to host " << host << " on activation";
}

void BrowserController::HandBackFrozenTab(uint64_t tab_id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserController::HandBackFrozenTabNow,
                                weak_factory_.GetWeakPtr(), tab_id));
}

void BrowserController::HandBackFrozenTabNow(uint64_t tab_id) {
  int index = -1;
  if (!browser_ || !ResolveTab(tab_id, &index) ||
      browser_->tab_strip_model()->active_index() == index) {
    return;
  }
  // Favourites only. A tab routing moved here belongs here by rule, and the
  // window it left stays frozen on it until the user picks something else.
  if (FavoriteKeyForTab(browser_->tab_strip_model()->GetWebContentsAt(index))
          .empty()) {
    return;
  }
  for (BrowserController* other : Registry()) {
    if (other == this || other->profile_id_ != profile_id_ ||
        !other->browser_ || !other->window() ||
        other->window()->FrozenTabId() != tab_id) {
      continue;
    }
    VLOG(1) << "Atrium: tab " << tab_id << " handed back to host " << other
            << ", frozen on it";
    ActivateTab(other, tab_id);
    return;
  }
}

void BrowserController::ActivateOwnTab(uint64_t tab_id) {
  ActivateTab(this, tab_id);
}

void BrowserController::MoveLittleTabToSpace() {
  if (!little_ || !browser_) {
    return;
  }
  TabStripModel* strip = browser_->tab_strip_model();
  if (strip->count() != 1) {
    // A little window is single-tab by construction - anything else
    // means that invariant broke elsewhere, so refuse rather than guess
    // which tab the button meant.
    LOG(WARNING) << "Atrium: foyer move-to-space refused - little window has "
                 << strip->count() << " tabs, expected 1";
    return;
  }

  // No space picker exists yet (the real space list isn't wired to
  // this surface) - "the space" is whichever OTHER live controller
  // Registry() holds, same profile preferred; the first controller of ANY
  // profile is the cross-profile fallback. The actual pick is
  // ChooseMoveTarget() (atrium_move_target_decision.h), a pure function
  // extracted so the move-vs-reopen decision has a unit test that does
  // not need Registry()'s private state or a TabStripModel.
  std::vector<BrowserController*> resident;
  std::vector<MoveTargetCandidate> decision_candidates;
  for (BrowserController* candidate : Registry()) {
    if (candidate == this || !candidate->browser_) {
      continue;
    }
    resident.push_back(candidate);
    decision_candidates.push_back(
        {.profile_id = candidate->profile_id_, .is_little = candidate->little_});
  }
  MoveTargetDecision decision =
      ChooseMoveTarget(profile_id_, decision_candidates);

  BrowserController* same_profile_target = nullptr;
  BrowserController* any_target = nullptr;
  if (decision.kind == MoveTargetKind::kSameProfile) {
    same_profile_target = resident[decision.index];
  } else if (decision.kind == MoveTargetKind::kCrossProfile) {
    any_target = resident[decision.index];
  }

  if (!same_profile_target && !any_target) {
    LOG(INFO) << "Atrium: foyer move-to-space pressed, no space window is "
                 "open to move into";
    return;
  }

  content::WebContents* tab = strip->GetWebContentsAt(0);
  const GURL url = tab->GetLastCommittedURL();

  if (same_profile_target) {
    // Same-profile live move: the exact WebContents keeps its renderer,
    // history and scroll position - no reload. Same detach-then-insert
    // pattern as PromotePeek/SplitPeek and the cross-window ActivateTab move
    // above.
    TabStripModel* target_strip = same_profile_target->browser_->tab_strip_model();
    if (!MoveWebContentsBetweenStrips(strip, 0, target_strip,
                                      target_strip->count(),
                                      AddTabTypes::ADD_ACTIVE)) {
      LOG(WARNING) << "Atrium: foyer move-to-space detach failed";
      return;
    }
    VLOG(1) << "Atrium: foyer tab moved live into same-profile space";
  } else {
    // Cross-profile: a WebContents is bound to its creating BrowserContext
    // (same constraint ActivateTab's own comment cites above) - it cannot
    // move. Reopen the URL fresh in the target's profile
    // instead, same call OpenSidebarColdTab uses to reopen a persisted row.
    // Non-routed for the same reason as the sidebar move above: this is the
    // move itself, not a navigation to be placed.
    const ScopedRoutingBypass no_routing;
    chrome::AddTabAt(any_target->browser_, url, -1, /*foreground=*/true);
    VLOG(1) << "Atrium: foyer tab reopened cross-profile at "
            << atrium::LogSafeUrl(url);
  }

  // The little window's only job was this one tab; closing rather than
  // leaving an empty little window behind matches the "Little window
  // closes" rule.
  chrome::CloseWindow(browser_);
}

void BrowserController::PromoteSurvivingRow() {
  // A run-loop turn has passed since the close, so re-check everything rather
  // than trusting what was true at post time: the browser may be gone, a quit
  // may have started, or a tab may have arrived - in which case the window is
  // no longer blank and promoting would open one nobody asked for.
  if (!browser_ || browser_shutdown::IsTryingToQuit()) {
    return;
  }
  if (!browser_->tab_strip_model()->empty()) {
    return;
  }
  // Same presenter resolution as the split survivor above: the strip belongs
  // to this controller, but the window showing it can be another slot's during
  // the cross-profile in-window swap.
  AtriumBrowserWindow* presenter = PresentedWindowForBrowser(browser_);
  if (!presenter) {
    presenter = window();
  }
  if (presenter) {
    presenter->PromoteSurvivingSidebarRow();
  }
}

void BrowserController::OnPageEditableFocusChanged(
    content::WebContents* contents) {
  if (!contents || !browser_) {
    return;
  }
  // A background tab moving its own focus (a timer calling focus(), a
  // background frame) changes nothing a keystroke could land in. Nothing is
  // recorded for it either - the live read in edit_focus_kind() is correct for
  // whichever tab is active whenever it is asked.
  if (browser_->tab_strip_model()->GetActiveWebContents() == contents) {
    PushEditFocus();
  }
}

EditFocusKind BrowserController::edit_focus_kind() {
  // Atrium's own text surface wins over the page: when the command bar's query
  // field has the keyboard the page cannot also have it, and this ordering is
  // what stops a chord being stolen out of the palette while the active tab
  // happens to hold a focused-field record from before the palette opened.
  //
  // The palette is read LIVE from the widget rather than taken from
  // atrium_text_focused_, even though Swift could push it: the widget has close
  // paths Swift never runs (CommandBarWidget's deactivation-guard auto-close,
  // and window teardown), so a pushed flag could strand at true and leave every
  // gated chord permanently unfired - fail-closed forever is a bug, not
  // caution. Visibility is state C++ owns outright, so reading it cannot get
  // stuck. atrium_text_focused_ carries only what C++ genuinely cannot see: the
  // sidebar's own fields, which live inside a Swift-owned NSView.
  if (browser_ && window() && window()->IsCommandBarShowing()) {
    return EditFocusKind::kAtriumText;
  }
  if (atrium_text_focused_) {
    return EditFocusKind::kAtriumText;
  }
  if (!browser_) {
    // No strip to ask, so no page can be typed into. A positive kNone rather
    // than kUnknown: C++ is not guessing here, it knows there is no page.
    return EditFocusKind::kNone;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!active) {
    return EditFocusKind::kNone;
  }
  // Read LIVE, never cached. WebContents::IsFocusedElementEditable() is
  // GetFocusedFrame()->has_focused_editable_element() - the very bool
  // FocusedNodeDetails::is_editable_node projects - so asking it directly is
  // the same source of truth with no window in which a copy could disagree.
  // A cache here would be wrong in a case no notification covers: dragging a
  // tab with a focused field into another window reparents native views without
  // moving DOM focus, so no FocusedElementChanged is sent and a per-tab record
  // in the destination controller would simply be absent - reading as "nothing
  // focused" while the user's caret is still in the field.
  return active->IsFocusedElementEditable() ? EditFocusKind::kPageEditable
                                            : EditFocusKind::kNone;
}

uint64_t BrowserController::edit_focus_tab_id() const {
  if (!browser_) {
    return 0;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  return active ? TabId(active) : 0;
}

void BrowserController::PushEditFocus() {
  // this pushed the focus kind so the Swift model's cache agreed
  // with what the native key route already read live. The native route still
  // reads it live - edit_focus_kind() is C++ - so nothing here is needed for
  // the gate to work. The VLOG stays: the two VM scenarios that assert on the
  // gate read this line to know what the browser thought was focused.
  if (little_) {
    return;
  }
  VLOG(1) << "Atrium: edit focus kind "
          << static_cast<int32_t>(edit_focus_kind()) << " tab "
          << edit_focus_tab_id();
}

void BrowserController::PushEditFocusForCommandBar() {
  // The palette's visibility is part of edit_focus_kind(), so a show/hide has to
  // be echoed for Swift's cache to agree with what the native route already sees
  // live. Called by CommandBarWidget::Show()/Close() - the single funnel every
  // open and close path reaches - AFTER the widget's own show/hide has run, so
  // the read reflects the new state rather than the old one.
  PushEditFocus();
}

void BrowserController::OpenCommandBar(bool is_startup_open) {
  if (!browser_) {
    return;
  }
  window()->OpenCommandBar(is_startup_open);
}

void BrowserController::OpenCommandBarWithReason(CommandBarOpenReason reason) {
  if (!browser_) {
    return;
  }
  pending_command_bar_open_reason_ = reason;
  OpenCommandBar();
  // Cleared unconditionally: the show path consumes the latch, but a window
  // whose command-bar widget was never attached (command_bar_connected()
  // false at registration) never reaches CommandBarWidget::Show(), and a
  // reason left behind would then leak into the next ordinary open.
  pending_command_bar_open_reason_ = CommandBarOpenReason::kNewTab;
}

CommandBarOpenReason BrowserController::TakePendingCommandBarOpenReason() {
  const CommandBarOpenReason reason = pending_command_bar_open_reason_;
  pending_command_bar_open_reason_ = CommandBarOpenReason::kNewTab;
  return reason;
}

void BrowserController::ToggleCommandBar() {
  if (!browser_) {
    return;
  }
  window()->ToggleCommandBar();
}

void BrowserController::ToggleOverview() {
  if (!browser_) {
    return;
  }
  window()->ToggleOverview();
}

void BrowserController::ToggleAiPanel() {
  if (!browser_) {
    return;
  }
  window()->ToggleAiPanel();
}

bool BrowserController::CopyCurrentPageLink(LinkCopyFormat format) {
  if (!browser_) {
    return false;
  }
  return window()->CopyCurrentPageLink(format);
}

bool BrowserController::ToggleDevBar() {
  if (!browser_) {
    return false;
  }
  window()->ToggleDevBarMode();
  return true;
}

bool BrowserController::SendReloadSurfaceMessage(std::string_view name) {
  if (!browser_) {
    return false;
  }
  window()->OnSurfaceMessage(name, base::ListValue());
  return true;
}

bool BrowserController::ReloadShownTab() {
  return SendReloadSurfaceMessage("sidebarReloadPressed");
}

bool BrowserController::HardReloadShownTab() {
  return SendReloadSurfaceMessage("sidebarReloadHard");
}

bool BrowserController::ReloadShownTabClearingCache() {
  return SendReloadSurfaceMessage("sidebarReloadClearingCache");
}

bool BrowserController::ReloadShownTabClearingSiteData() {
  return SendReloadSurfaceMessage("sidebarReloadClearingSite");
}

bool BrowserController::TogglePinShownTab() {
  AtriumBrowserWindow* w = browser_ ? window() : nullptr;
  return w && w->TogglePinShownTab();
}

bool BrowserController::ActivateFavoriteInSlot(int ordinal) {
  AtriumBrowserWindow* w = browser_ ? window() : nullptr;
  return w && w->ActivateFavoriteInSlot(ordinal);
}

void BrowserController::OpenArchive() {
  if (!browser_) {
    return;
  }
  // The hotkey is a toggle, but OpenOverviewAtSection must stay a pure open for
  // the toast click, so the close lives here and not there.
  if (window()->IsOverviewShowing()) {
    window()->ToggleOverview();
    return;
  }
  window()->OpenOverviewAtSection("archived");
}

bool BrowserController::ClearTabs() {
  AtriumBrowserWindow* w = browser_ ? window() : nullptr;
  return w && w->ArchiveSpaceTabs();
}

bool BrowserController::CloseSplitPane() {
  AtriumBrowserWindow* w = browser_ ? window() : nullptr;
  if (!w || !w->IsSplit()) {
    return false;
  }
  w->CloseSplit(nullptr);
  return true;
}

bool BrowserController::ToggleSplitOrientation() {
  AtriumBrowserWindow* w = browser_ ? window() : nullptr;
  if (!w || !w->IsSplit()) {
    return false;
  }
  const SplitOrientation flipped = w->split_orientation() ==
                                            SplitOrientation::kSideBySide
                                        ? SplitOrientation::kStacked
                                        : SplitOrientation::kSideBySide;
  w->UpdateSplit(flipped, 0.5);
  return true;
}

bool BrowserController::ToggleDeveloperMode() {
  if (!browser_) {
    return false;
  }
  Profile* profile = browser_->profile();
  const bool in_developer_mode =
      profile->GetPrefs()->GetBoolean(prefs::kExtensionsUIDeveloperMode);
  extensions::util::SetDeveloperModeForProfile(profile, !in_developer_mode);
  return true;
}

void BrowserController::PushFoyerSnapshot() {
  ++foyer_pushes_for_testing_;
  // A no-op on a regular window: only a little window has a foyer slot.
  if (AtriumBrowserWindow* w = window()) {
    w->PushFoyerSnapshot();
  }
}

bool BrowserController::IsOffTheRecordProfile() const {
  return browser_ && browser_->profile()->IsOffTheRecord();
}

std::string BrowserController::ActiveSpaceProfileId() const {
  if (!browser_) {
    return profile_id_;
  }
  // Reads core state directly rather than building and parsing the whole
  // sidebar snapshot (every tab/favourite/folder) for this one field - this
  // runs on every keystroke via QueryCommandBar.
  char* raw = atrium_core_active_space_profile_id(SpaceIndexForSlot(slot_));
  if (!raw) {
    return profile_id_;
  }
  const std::string id(raw);
  atrium_core_string_free(raw);
  return id.empty() ? profile_id_ : id;
}

Profile* BrowserController::ActiveSpaceProfile() {
  if (!browser_) {
    return nullptr;
  }
  const std::string active_profile_id = ActiveSpaceProfileId();
  if (active_profile_id == profile_id_) {
    return browser_->profile();
  }
  BrowserController* owner = ControllerFor(kDefaultSlot, active_profile_id);
  if (owner && owner->browser_) {
    return owner->browser_->profile();
  }
  // A space whose profile owns no window still searches with that profile's
  // engine: the query needs a Profile, not a Browser.
  if (Profile* resident = atrium::LoadedProfileForId(active_profile_id)) {
    return resident;
  }
  ProfileBootstrap::EnsureProfileLoaded(active_profile_id);
  // Named, because the fallback answers with the wrong engine until the load
  // lands and there is nothing else to say so.
  VLOG(1) << "Atrium: active space profile " << active_profile_id
          << " is not loaded, querying as " << profile_id_ << " instead";
  return browser_->profile();
}

BrowserController* BrowserController::ActiveSpaceController() {
  if (!browser_) {
    return this;
  }
  const std::string active_profile_id = ActiveSpaceProfileId();
  if (active_profile_id == profile_id_) {
    return this;
  }
  // This window's own controller for that profile first, so a second window
  // does not answer with the first window's strip.
  BrowserController* fallback = nullptr;
  for (BrowserController* controller : Registry()) {
    if (controller->profile_id_ != active_profile_id || !controller->browser_) {
      continue;
    }
    if (controller->slot_ == slot_) {
      return controller;
    }
    if (controller->slot_ == kDefaultSlot && !fallback) {
      fallback = controller;
    }
  }
  return fallback ? fallback : this;
}

BrowserController* BrowserController::ControllerOwningExtension(
    const std::string&) {
  // A store extension's id is the same in every profile, so membership can't
  // tell whose strip is on screen - ask the window which push it last showed.
  AtriumBrowserWindow* window = SidebarWindow();
  if (BrowserController* pusher =
          window ? window->LastExtensionActionsController() : nullptr;
      pusher && pusher->browser_) {
    return pusher;
  }
  return this;
}

void BrowserController::StopCommandBar() {
  if (!browser_ || !command_bar_bridge_) {
    return;
  }
  command_bar_bridge_->Stop();
}

void BrowserController::QueryCommandBar(const std::string& text) {
  // the one slash command that exists so far. Intercepted here
  // rather than routed through AutocompleteController, which has no notion of
  // it - the query never reaches command_bar_bridge_.
  if (text == "/note") {
    if (AtriumBrowserWindow* w = window()) {
      w->OpenNotesBesideThisPage();
      w->CloseCommandBar();
    }
    return;
  }
  if (!command_bar_bridge_) {
    return;
  }
  command_bar_bridge_->SetQueryProfile(ActiveSpaceProfile());
  command_bar_bridge_->Query(text);
}

void BrowserController::AcceptCommandBarMatch(int index) {
  if (!command_bar_bridge_ || !window() || index < 0) {
    return;
  }
  // The URL is the one Chromium's own providers resolved for that row, not a
  // string the renderer supplied - the renderer only names WHICH row. An index
  // it made up resolves to an empty GURL, which the window declines.
  const bool repointed =
      command_bar_bridge_->SetQueryProfile(ActiveSpaceProfile());
  // A switch made with the palette open drops `index` on purpose: those rows
  // ranked in the profile just left, so the accept resolves the new profile's
  // own default match - a plain search for what the user typed.
  window()->OpenCommandBarMatch(
      repointed ? command_bar_bridge_->ReQueryDefaultDestination()
                : command_bar_bridge_->DestinationAt(
                      static_cast<size_t>(index)));
}

void BrowserController::DeleteCommandBarMatch(int index) {
  if (!command_bar_bridge_ || index < 0) {
    return;
  }
  command_bar_bridge_->SetQueryProfile(ActiveSpaceProfile());
  command_bar_bridge_->DeleteMatch(static_cast<size_t>(index));
}

bool BrowserController::OpenPeek(const GURL& url,
                                 const content::Referrer& referrer,
                                 const gfx::Point& click_origin_screen,
                                 content::WebContents* source) {
  if (!browser_) {
    return false;
  }
  if (!source) {
    source = browser_->tab_strip_model()->GetActiveWebContents();
  }
  if (!MayOpenPeek(source)) {
    return false;
  }
  // No stacking (Global Constraints): a second peek on the SAME window
  // closes the first (destroying its WebContents) before opening the new
  // one.
  ClosePeek();

  // Standalone WebContents on the SAME profile as this controller's Browser
  // - a deliberate design call (peek is never OTR), not
  // OTR - never inserted into any TabStripModel (no AddTab/tab_id
  // announcement).
  peek_contents_ =
      content::WebContents::Create(content::WebContents::CreateParams(
          browser_->profile()));
  peek_forwarder_ =
      std::make_unique<PeekEventForwarder>(peek_contents_.get(), this);
  VLOG(1) << "Atrium: peek load started for " << atrium::LogSafeUrl(url);
  peek_contents_->GetController().LoadURL(
      url, referrer, ui::PAGE_TRANSITION_LINK, std::string());

  ShowPeekWidget(click_origin_screen, source);
  return true;
}

std::unique_ptr<content::WebContents> BrowserController::AdoptPopupAsPeek(
    std::unique_ptr<content::WebContents> contents,
    const gfx::Point& click_origin_screen,
    const GURL& target_url) {
  if (!browser_ || !contents) {
    return contents;
  }
  // browser.cc adopts only a popup the active tab opened, in the same call.
  content::WebContents* source =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!MayOpenPeek(source)) {
    VLOG(1) << "Atrium: popup adopt refused, its tab is not on screen";
    return contents;
  }
  // Same no-stacking rule as OpenPeek, but the page being replaced cannot be
  // destroyed here: this runs inside the new popup's own window.open, which
  // blocks a renderer the outgoing page usually shares. That deadlocks.
  ClosePeekForReplacement();

  peek_contents_ = std::move(contents);
  peek_forwarder_ =
      std::make_unique<PeekEventForwarder>(peek_contents_.get(), this);
  peek_shows_origin_chip_ = true;

  if (!ShowPeekWidget(click_origin_screen, source)) {
    VLOG(1) << "Atrium: popup adopt refused, this window has no peek card";
    peek_forwarder_.reset();
    peek_shows_origin_chip_ = false;
    return std::move(peek_contents_);
  }

  // Unlike the link-click peek, this one hosts a page the user is expected to
  // sign in on, so it needs the password manager, autofill, permission and
  // Safe Browsing helpers a tab has. Safe to call on an already-adopted one.
  TabHelpers::AttachTabHelpers(peek_contents_.get());
  // The popup has no URL yet, so the chip names the target it is about to
  // load. The commit replaces it, and a redirect moves it on from there.
  PushPeekOriginChipFor(peek_contents_->GetVisibleURL().is_empty()
                            ? target_url
                            : peek_contents_->GetVisibleURL());

  // A blank popup a page writes into never reports a first paint. One that
  // navigates keeps the placeholder, or the card is see-through until it paints.
  if (target_url.is_empty() || target_url.IsAboutBlank()) {
    HidePeekLoadingPlaceholder();
  }
  return nullptr;
}

bool BrowserController::HoldsPeekForTesting(
    const content::WebContents* peek) const {
  return peek && (peek == peek_contents_.get() ||
                  std::ranges::any_of(parked_peeks_, [&](const ParkedPeek& p) {
                    return p.contents.get() == peek;
                  }));
}

bool BrowserController::ShowsInSlot() {
  const std::optional<ControllerId> visible =
      PresentationTable().ResolveVisible(slot_);
  return !visible || ControllerOf(*visible) == this;
}

bool BrowserController::MayOpenPeek(const content::WebContents* source) {
  if (!ShowsInSlot()) {
    VLOG(1) << "Atrium: peek open refused, this controller is not on screen";
    return false;
  }
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  if (!window || window->CurrentlyDisplayedWebContents() != source) {
    VLOG(1) << "Atrium: peek open refused, its tab is not on screen";
    return false;
  }
  return true;
}

void BrowserController::SyncPeekWithPane() {
  if (!browser_ || !ShowsInSlot()) {
    return;
  }
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  if (!window) {
    return;
  }
  content::WebContents* shown = window->CurrentlyDisplayedWebContents();
  const uint64_t shown_id = shown ? TabId(shown) : 0;
  // A peek opened from an empty strip has no tab to follow.
  auto follows_pane = [shown_id](uint64_t source_tab_id) {
    return source_tab_id == 0 || source_tab_id == shown_id;
  };
  if (!follows_pane(peek_source_tab_id_)) {
    ParkCurrentPeek(window);
  }
  // Only peeks a controller still owns: a closing card keeps its animation.
  for (BrowserController* owner : Registry()) {
    if (owner != this && window->ShowsPeek(owner->peek_contents_.get())) {
      owner->ParkCurrentPeek(window);
    }
  }
  if (!peek_contents_) {
    auto parked = std::ranges::find_if(parked_peeks_, [&](const ParkedPeek& p) {
      return follows_pane(p.source_tab_id);
    });
    if (parked == parked_peeks_.end()) {
      return;
    }
    peek_contents_ = std::move(parked->contents);
    peek_source_tab_id_ = parked->source_tab_id;
    peek_shows_origin_chip_ = parked->shows_origin_chip;
    parked_peeks_.erase(parked);
  }
  if (!window->ShowsPeek(peek_contents_.get())) {
    RestorePeek(window);
  }
}

void BrowserController::ParkCurrentPeek(AtriumBrowserWindow* window) {
  if (!peek_contents_) {
    return;
  }
  window->ParkPeek(peek_contents_.get());
  peek_contents_->SetAudioMuted(true);
  peek_forwarder_.reset();
  static uint64_t park_count = 0;
  parked_peeks_.insert(parked_peeks_.begin(),
                       ParkedPeek{std::move(peek_contents_),
                                  peek_source_tab_id_,
                                  peek_shows_origin_chip_, ++park_count});
  peek_source_tab_id_ = 0;
  peek_shows_origin_chip_ = false;
  size_t parked = 0;
  BrowserController* oldest = nullptr;
  for (BrowserController* owner : Registry()) {
    if (owner->slot_ != slot_ || owner->parked_peeks_.empty()) {
      continue;
    }
    parked += owner->parked_peeks_.size();
    if (!oldest || owner->parked_peeks_.back().parked_at <
                       oldest->parked_peeks_.back().parked_at) {
      oldest = owner;
    }
  }
  if (parked > kMaxParkedPeeks) {
    // Off this call stack, like ClosePeekForReplacement: a new tab's park can
    // run inside the window.open that made it.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(oldest->parked_peeks_.back().contents));
    oldest->parked_peeks_.pop_back();
  }
}

void BrowserController::RestorePeek(AtriumBrowserWindow* window) {
  if (!window->RestorePeek(peek_contents_.get(),
                           base::BindRepeating(
                               &BrowserController::ClosePeekContents,
                               weak_factory_.GetWeakPtr(),
                               peek_contents_->GetWeakPtr()),
                           base::BindRepeating(&BrowserController::PromotePeek,
                                               base::Unretained(this)))) {
    return;
  }
  VLOG(1) << "Atrium: peek restored for tab " << peek_source_tab_id_;
  peek_contents_->SetAudioMuted(false);
  UpdatePeekOriginChip();
  if (!peek_forwarder_) {
    peek_forwarder_ =
        std::make_unique<PeekEventForwarder>(peek_contents_.get(), this);
  }
  // The badge's one push: the page's icon may have come in while parked.
  peek_forwarder_->PushCurrentFavicon();
}

void BrowserController::ClosePeekForReplacement() {
  if (!peek_contents_) {
    return;
  }
  if (browser_) {
    AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
    if (!window) {
      window = this->window();
    }
    if (window) {
      window->ClosePeek(peek_contents_.get());
    }
  }
  peek_forwarder_.reset();
  peek_shows_origin_chip_ = false;
  // Off this call stack, not never: window.closed on the opener's handle only
  // goes true once this WebContents is really gone.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(peek_contents_));
}

void BrowserController::UpdatePeekOriginChip() {
  if (!peek_contents_) {
    return;
  }
  PushPeekOriginChipFor(peek_contents_->GetVisibleURL());
}

void BrowserController::PushPeekOriginChipFor(const GURL& url) {
  if (!peek_shows_origin_chip_ || !peek_contents_ || !browser_) {
    return;
  }
  // A URL with no host (about:, data:, blob:) has no origin to name, and the
  // scheme is the part that should worry the reader anyway.
  std::string label(url.host());
  if (label.empty()) {
    label = url.IsAboutBlank() ? std::string("about:blank")
                               : std::string(url.scheme()) + ":";
  }

  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  if (window) {
    window->SetPeekOriginChip(peek_contents_.get(), base::UTF8ToUTF16(label),
                              url.SchemeIs(url::kHttpsScheme));
  }
}

bool BrowserController::ShowPeekWidget(const gfx::Point& click_origin_screen,
                                       content::WebContents* source) {
  // Null only for an empty strip (M2c's tab-less new window).
  peek_source_tab_id_ = source ? TabId(source) : 0;

  // In single-window mode a cross-profile switch swaps content into the slot's
  // presenter window, so the peek widget must live there, not on self's own
  // (invisible) window. Falls back to self's window when no presenter is
  // mapped. PresentedWindowForBrowser's OpenPeek binds peek_contents_.get()
  // into the widget's WebView (SetWebContents, non-owning - the widget never
  // owns this WebContents) and wires ClosePeekContents, bound to this peek,
  // as the widget's own-detected-dismiss callback (Escape / the focus-bounce
  // guard / window.close).
  //
  // This resolve-now, resolve-again-at-close pattern (repeated by ClosePeek,
  // PromotePeek, and the split ops below) is only safe because of the
  // presenter-stickiness invariant documented at SlotPresentationTable::
  // OnActivate's kInWindowSwap branch: the presenter can't change between
  // this call and the later close, so both resolve to the same window.
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  if (!window) {
    return false;
  }
  const bool opened = window->OpenPeek(
      peek_contents_.get(),
      base::BindRepeating(&BrowserController::ClosePeekContents,
                          weak_factory_.GetWeakPtr(),
                          peek_contents_->GetWeakPtr()),
      // The cluster's popout button must promote THIS controller's peek
      // regardless of which controller window->browser() maps back to.
      base::BindRepeating(&BrowserController::PromotePeek,
                          base::Unretained(this)),
      click_origin_screen);
  // After peek_source_tab_id_ is set, which the badge is keyed on.
  if (opened && peek_forwarder_) {
    peek_forwarder_->PushCurrentFavicon();
  }
  return opened;
}

void BrowserController::ClosePeek() {
  if (!peek_contents_) {
    return;
  }
  // Moved out, not reset: destroying it now clears the WebView it is still
  // bound to mid-animation. `on_hidden` below keeps it alive until the
  // dismiss animation actually finishes.
  std::unique_ptr<content::WebContents> outgoing = std::move(peek_contents_);
  if (browser_) {
    // See OpenPeek's comment: route to the slot's presenter window.
    AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
    if (!window) {
      window = this->window();
    }
    const content::WebContents* peek = outgoing.get();
    window->ClosePeek(peek, /*animate=*/true,
                      base::DoNothingWithBoundArgs(std::move(outgoing)));
  }
  peek_forwarder_.reset();
  peek_shows_origin_chip_ = false;
}

void BrowserController::ClosePeekContents(
    base::WeakPtr<content::WebContents> peek) {
  if (!peek) {
    return;
  }
  if (peek.get() == peek_contents_.get()) {
    ClosePeek();
    return;
  }
  std::erase_if(parked_peeks_, [&](const ParkedPeek& parked) {
    return parked.contents.get() == peek.get();
  });
}

void BrowserController::HidePeekLoadingPlaceholder() {
  if (browser_) {
    // See OpenPeek's comment: route to the slot's presenter window.
    AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
    if (!window) {
      window = this->window();
    }
    window->HidePeekLoadingPlaceholder(peek_contents_.get());
  }
}

void BrowserController::ShowPeekFavicon(const gfx::Image& image) {
  if (!peek_contents_ || peek_source_tab_id_ == 0 || !browser_) {
    return;
  }
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  if (!window || !window->ShowsPeek(peek_contents_.get())) {
    return;
  }
  // No icon clears the badge, as Arc shows none for a page without one.
  std::optional<std::vector<uint8_t>> png = EncodeFaviconPng(image);
  if (!png) {
    window->SetSidebarPeekBadge(peek_contents_.get(), 0, 0, 0);
    return;
  }
  // Keyed by the peek's own tab id, which a promote carries into the strip.
  const uint64_t icon_id = TabId(peek_contents_.get());
  FaviconCache::For(peek_contents_->GetBrowserContext())
      .Store(icon_id, std::move(*png));
  // The badge's URL changes with every new icon, or Blink keeps the old one.
  static uint64_t rev = 0;
  window->SetSidebarPeekBadge(peek_contents_.get(), peek_source_tab_id_,
                              icon_id, ++rev);
}

void BrowserController::PromotePeek() {
  if (!peek_contents_ || !browser_) {
    return;
  }
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  window->PromotePeek(peek_contents_.get(),
                      base::BindOnce(&BrowserController::FinishPromotePeek,
                                     weak_factory_.GetWeakPtr()));
}

void BrowserController::FinishPromotePeek() {
  if (!peek_contents_ || !browser_) {
    return;
  }
  // See OpenPeek's comment: route to the slot's presenter window. Resolved
  // once and reused below for the matching ClosePeek(/*animate=*/false) -
  // both calls must target the SAME window this peek was actually shown on.
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  // Detach the WebView binding before moving ownership out (Global
  // Constraints: "detach WebView, then InsertWebContentsAt") - the
  // WebContents no longer belongs to the peek widget once this returns.
  window->DetachPeekWebContents(peek_contents_.get());
  peek_forwarder_.reset();

  // Adjacent-insertion: resolve peek_source_tab_id_ -
  // the tab that was ACTIVE when this peek was OPENED (see that member's
  // header doc) - back to a live index, rather than trusting
  // active_index() cold at promote time (the controller carry-in this task
  // exists to close: active_index() can have drifted between open and
  // promote via a programmatic activate_tab call, even though the peek
  // holding key focus makes that unlikely in practice). ResolveTab returns
  // nullptr (leaving `source_index` untouched) if the source tab closed or
  // moved to another window since - `index` then keeps its
  // active_index()-relative fallback, which InsertTabAtImpl's
  // ConstrainInsertionIndex clamps to a valid position regardless.
  int index = browser_->tab_strip_model()->active_index() + 1;
  int source_index = -1;
  if (ResolveTab(peek_source_tab_id_, &source_index)) {
    index = source_index + 1;
  }
  // ADD_ACTIVE: same flag the
  // M2c cross-window ActivateTab move uses for its InsertDetachedTabAt call
  // above (line ~1170) - tab_strip_model.cc's InsertTabAtImpl computes
  // `active = (add_types & ADD_ACTIVE) || empty()`, so this is what makes
  // the promoted tab the window's active tab on insert; no separate
  // ActivateTabAt call is needed afterward, matching that precedent.
  browser_->tab_strip_model()->InsertWebContentsAt(
      index, std::move(peek_contents_), AddTabTypes::ADD_ACTIVE);
  // No ClosePeek here: the card is still growing over the new tab and hides
  // itself when the spring lands (PeekWidget::Promote).
}
void BrowserController::SplitPeek() {
  if (!peek_contents_ || !browser_) {
    return;
  }
  // Same window resolution and detach ordering as PromotePeek - see its
  // comments; both must target the window the peek was actually shown on.
  AtriumBrowserWindow* window = PresentedWindowForBrowser(browser_);
  if (!window) {
    window = this->window();
  }
  // A stale controller's split would still move its peek into its strip and
  // split the shared window.
  if (!window->ShowsPeek(peek_contents_.get())) {
    return;
  }
  // Card goes without its dismiss animation, same as promote: the content
  // becomes a real tab, so animating the card away would double against the
  // split's own layout. Before the detach, which the close is keyed on.
  window->ClosePeek(peek_contents_.get(), /*animate=*/false);
  window->DetachPeekWebContents(peek_contents_.get());
  peek_forwarder_.reset();

  // Keep a raw handle before ownership moves into the tab strip - the split
  // needs to name this exact WebContents as the secondary pane.
  content::WebContents* peeked = peek_contents_.get();

  int index = browser_->tab_strip_model()->active_index() + 1;
  int source_index = -1;
  if (ResolveTab(peek_source_tab_id_, &source_index)) {
    index = source_index + 1;
  }
  // ADD_NONE, unlike PromotePeek's ADD_ACTIVE: the split's PRIMARY pane is
  // whatever is active (EnterSplit reads ActivePane()), and that has to stay
  // the tab the peek was opened from. Taking active here would make the
  // peeked page primary and then hand EnterSplit the same WebContents for
  // both panes - the exact single-host corruption EnterSplit's own
  // defensive check exists to refuse.
  browser_->tab_strip_model()->InsertWebContentsAt(index,
                                                   std::move(peek_contents_),
                                                   AddTabTypes::ADD_NONE);

  // Side by side at an even split. EnterSplit declines (and logs) if the
  // window is already split, which is the right outcome: the peeked page is
  // by then a normal adjacent tab, so nothing is lost.
  window->EnterSplit(peeked, SplitOrientation::kSideBySide, 0.5);
}


// what the sidebar's "+ New Tab" means. The palette when there is
// one, an ordinary tab when there is not - matching HostActions.newTab's
// documented fallback, so both arms' buttons do the same thing.
// Presenter-aware, the same resolution every sidebar op used: in
// single-window mode a cross-profile switch swaps content into the slot's
// presenter window, so a sidebar change must land there rather than on this
// controller's own, invisible one.
AtriumBrowserWindow* BrowserController::SidebarWindow() {
  if (!browser_) {
    return nullptr;
  }
  AtriumBrowserWindow* target = PresentedWindowForBrowser(browser_);
  return target ? target : window();
}

namespace {

constexpr int32_t kThemeBackdropWidth = 192;
constexpr int32_t kThemeBackdropHeight = 108;

bool IsDarkWindow(AtriumBrowserWindow* window) {
  return window->GetNativeTheme()->preferred_color_scheme() ==
        ui::NativeTheme::PreferredColorScheme::kDark;
}

}  // namespace

BrowserController::ThemeFlags BrowserController::RasterThemeSpec(
    AtriumBrowserWindow* window,
    const std::string* theme_json,
    uint8_t** rgba,
    size_t* len) {
  ThemeFlags flags;
  *rgba = nullptr;
  *len = 0;
  if (theme_json) {
    if (std::optional<base::Value> parsed =
            base::JSONReader::Read(*theme_json, base::JSON_PARSE_RFC);
        parsed && parsed->is_dict()) {
      const base::DictValue& spec = parsed->GetDict();
      flags.night_shift = spec.FindBool("nightShiftEnabled").value_or(false);
      flags.ambient = spec.FindBool("ambientEnabled").value_or(false);
      if (const base::DictValue* effects = spec.FindDict("effects")) {
        flags.grain = std::clamp(
            static_cast<float>(effects->FindDouble("grain").value_or(0.0)),
            0.f, 1.f);
      }
    } else {
      LOG(WARNING) << "Atrium: theme spec did not parse as an object, "
                      "clearing the backdrop";
      theme_json = nullptr;
    }
  }

  // SetBackdropTheme itself refuses an incognito window, so a raster for one
  // would only be freed unused, and the animator must not run for a
  // backdrop that never paints.
  if (!theme_json || IsIncognitoWindow(window->browser())) {
    return flags;
  }
  const bool dark = IsDarkWindow(window);
  const double warmth =
      flags.night_shift
          ? atrium_core_night_shift_offset(theme_animator_.LocalHourNow())
          : 0.0;
  const double ambient_time = flags.ambient ? 0.0 : -1.0;
  *rgba = atrium_core_theme_raster(theme_json->c_str(), kThemeBackdropWidth,
                                   kThemeBackdropHeight, dark, warmth,
                                   ambient_time, len);
  return flags;
}

void BrowserController::ApplyThemeSpec(AtriumBrowserWindow* window,
                                       const std::string* theme_json,
                                       int transition_ms,
                                       bool reset_phase) {
  if (!window) {
    return;
  }
  if (themed_window_.get() != window) {
    if (AtriumBrowserWindow* previous = themed_window_.get()) {
      previous->SetThemeVisibilityCallback(base::RepeatingCallback<void(bool)>());
    }
    window->SetThemeVisibilityCallback(
        base::BindRepeating(&BrowserController::OnThemedWindowVisibilityChanged,
                            weak_factory_.GetWeakPtr()));
    themed_window_ = window->GetWeakPtr();
  }
  // Settings and the hotkeys can reach one window through two controllers.
  // Each ambient timer repaints its own picture, so two of them blink.
  for (BrowserController* other : Registry()) {
    if (other != this && other->themed_window_.get() == window) {
      other->ReleaseThemedWindow();
    }
  }

  uint8_t* rgba = nullptr;
  size_t out_len = 0;
  const ThemeFlags flags = RasterThemeSpec(window, theme_json, &rgba, &out_len);
  const bool night_shift = flags.night_shift;
  const bool ambient = flags.ambient;
  const float grain = flags.grain;

  if (!rgba) {
    window->SetBackdropTheme(nullptr, 0, 0, 0, transition_ms);
    themed_theme_json_.clear();
    window->SetThemeVisibilityCallback(base::RepeatingCallback<void(bool)>());
    themed_window_.reset();
    theme_animator_.Configure(/*night_shift=*/false, /*ambient=*/false,
                              /*reduced_motion=*/false);
    return;
  }
  window->SetBackdropTheme(rgba, kThemeBackdropWidth, kThemeBackdropHeight,
                           grain, transition_ms);
  atrium_core_bytes_free(rgba, out_len);

  themed_theme_json_ = *theme_json;
  themed_grain_ = grain;
  theme_animator_.SetVisible(window->IsVisible());
  theme_animator_.Configure(night_shift, ambient,
                            gfx::Animation::PrefersReducedMotion());
  if (reset_phase) {
    theme_animator_.ResetPhase();
  }
}

void BrowserController::BeginSpaceScrub(int index) {
  AtriumBrowserWindow* sidebar = SidebarWindow();
  if (!browser_ || !sidebar || !PersistsTabs()) {
    return;
  }
  // A restage (the rail crossed back over its origin) drops the old side
  // first, so the window never holds two staged layers.
  EndSpaceScrub(/*commit=*/false);

  char* raw_id = atrium_core_space_id_at(index);
  if (!raw_id) {
    VLOG(1) << "Atrium: scrub toward a space the core has no slot for: "
            << index;
    return;
  }
  const std::string space_id(raw_id);
  atrium_core_string_free(raw_id);

  // The two flat tints, both off the sidebar model: the active space's at the
  // top level, the neighbour's in its chip.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::DictValue& root = parsed->GetDict();
  const std::string* from_hex = root.FindString("spaceTint");
  const base::ListValue* spaces = root.FindList("spaces");
  const base::DictValue* chip =
      spaces && index >= 0 && static_cast<size_t>(index) < spaces->size()
          ? (*spaces)[index].GetIfDict()
          : nullptr;
  const std::string* to_hex = chip ? chip->FindString("color") : nullptr;
  const std::optional<SkColor> from_tint =
      from_hex ? ParseHexColor(*from_hex) : std::nullopt;
  const std::optional<SkColor> to_tint =
      to_hex ? ParseHexColor(*to_hex) : std::nullopt;
  if (!from_tint || !to_tint) {
    return;
  }

  SpaceScrub scrub;
  scrub.window = sidebar->GetWeakPtr();
  scrub.target_index = index;
  scrub.from_tint = *from_tint;
  scrub.to_tint = *to_tint;
  if (char* raw_theme = atrium_core_space_theme(space_id.c_str())) {
    scrub.theme_json = std::string(raw_theme);
    atrium_core_string_free(raw_theme);
  }
  uint8_t* rgba = nullptr;
  size_t len = 0;
  const ThemeFlags flags = RasterThemeSpec(
      sidebar, scrub.theme_json ? &*scrub.theme_json : nullptr, &rgba, &len);
  sidebar->SetBackdropScrubTheme(rgba, kThemeBackdropWidth,
                                 kThemeBackdropHeight, flags.grain);
  if (rgba) {
    atrium_core_bytes_free(rgba, len);
  }
  space_scrub_ = std::move(scrub);
  VLOG(1) << "Atrium: space scrub staged toward " << index;
}

void BrowserController::SetSpaceScrubBlend(double weight) {
  AtriumBrowserWindow* sidebar = space_scrub_ ? space_scrub_->window.get()
                                              : nullptr;
  if (!sidebar) {
    return;
  }
  const double w = std::clamp(weight, 0.0, 1.0);
  sidebar->SetBackdropScrubBlend(w);
  sidebar->SetBackdropTintLive(gfx::Tween::ColorValueBetween(
      w, space_scrub_->from_tint, space_scrub_->to_tint));
}

// static
BrowserController* BrowserController::SpaceScrubOwner(
    const AtriumBrowserWindow* window) {
  for (BrowserController* controller : Registry()) {
    if (controller->space_scrub_ &&
        controller->space_scrub_->window.get() == window) {
      return controller;
    }
  }
  return nullptr;
}

void BrowserController::EndSpaceScrub(bool commit) {
  if (!space_scrub_) {
    return;
  }
  const SpaceScrub scrub = std::move(*space_scrub_);
  space_scrub_.reset();
  AtriumBrowserWindow* sidebar = scrub.window.get();
  if (!sidebar) {
    return;
  }
  sidebar->SetBackdropScrubBlendEnd(commit ? 1 : 0, /*settle_ms=*/0);
  if (!commit) {
    sidebar->SetBackdropTintLive(scrub.from_tint);
    sidebar->PushBackdropInk();
    VLOG(1) << "Atrium: space scrub dropped";
    return;
  }
  // The promotion painted the picture. This is the bookkeeping SelectSpace
  // skipped for it: the animator's spec and the tint the next clear reverts
  // to. No transition - the frame is already on screen.
  sidebar->SetBackdropTintLive(scrub.to_tint);
  sidebar->PushBackdropInk();
  ApplyThemeSpec(sidebar, scrub.theme_json ? &*scrub.theme_json : nullptr,
                 /*transition_ms=*/0, /*reset_phase=*/true);
  VLOG(1) << "Atrium: space scrub promoted";
}

void BrowserController::ApplySpaceTheme(AtriumBrowserWindow* window,
                                        const std::string& space_id,
                                        int transition_ms) {
  char* raw = atrium_core_space_theme(space_id.c_str());
  if (!raw) {
    ApplyThemeSpec(window, nullptr, transition_ms, /*reset_phase=*/true);
    return;
  }
  const std::string theme_json(raw);
  atrium_core_string_free(raw);
  ApplyThemeSpec(window, &theme_json, transition_ms, /*reset_phase=*/true);
}

void BrowserController::PreviewTheme(const std::string& space_id,
                                     const std::string& theme_json) {
  AtriumBrowserWindow* window = SidebarWindow();
  if (!window) {
    return;
  }
  VLOG(1) << "Atrium: theme preview for space " << space_id;
  ApplyThemeSpec(window, &theme_json, /*transition_ms=*/0,
                 /*reset_phase=*/false);
}

bool BrowserController::ShowSpaceForThemeEdit(const std::string& space_id) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    VLOG(1) << "Atrium: a theme edit named a space the core has dropped";
    return false;
  }
  const std::string slot_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(slot_json, base::JSON_PARSE_RFC);
  const std::optional<int> index =
      parsed && parsed->is_dict() ? parsed->GetDict().FindInt("index")
                                  : std::nullopt;
  if (!index) {
    return false;
  }
  if (*index == SpaceIndexForSlot(slot_)) {
    return true;
  }
  // The same path settingsSpaceOpened takes, so the window arrives at the
  // space with its tabs rather than only its backdrop.
  return SelectSpace(*index);
}

AtriumBrowserWindow* BrowserController::PreviewSpaceTheme(
    const std::string& space_id,
    const std::string& theme_json) {
  // The backdrop belongs to whichever space the window is showing, so an edit
  // to another one has to move the window there or it paints over the wrong
  // space and leaves it there.
  if (!ShowSpaceForThemeEdit(space_id)) {
    return nullptr;
  }
  AtriumBrowserWindow* window = SidebarWindow();
  if (!window) {
    return nullptr;
  }
  PreviewTheme(space_id, theme_json);
  VLOG(1) << "Atrium: theme preview applied";
  return window;
}

void BrowserController::RevertThemePreview(AtriumBrowserWindow* window) {
  if (!window || !browser_) {
    return;
  }
  // Resolved from the sidebar model rather than by id, the way SelectSpace's
  // own tail does: only the model knows which space the window is on now.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::Value* theme = parsed->GetDict().Find("spaceTheme");
  if (theme && !theme->is_none()) {
    std::string theme_json;
    base::JSONWriter::Write(*theme, &theme_json);
    ApplyThemeSpec(window, &theme_json, 300, /*reset_phase=*/true);
  } else {
    ApplyThemeSpec(window, nullptr, 300, /*reset_phase=*/true);
  }
  VLOG(1) << "Atrium: theme preview reverted";
}

bool BrowserController::SetSpaceTheme(const std::string& space_id,
                                      const std::string* theme_json) {
  if (!atrium_core_set_space_theme(
          space_id.c_str(), theme_json ? theme_json->c_str() : nullptr)) {
    LOG(WARNING) << "Atrium: the core refused a theme for space " << space_id;
    return false;
  }
  return true;
}

void BrowserController::ApplySavedSpaceTheme(const std::string& space_id) {
  if (!ShowSpaceForThemeEdit(space_id)) {
    return;
  }
  AtriumBrowserWindow* window = SidebarWindow();
  if (!window) {
    return;
  }
  window->PushSidebarSnapshotNow();
  ApplySpaceTheme(window, space_id, 300);
}

void BrowserController::ApplyActiveSpaceTheme(AtriumBrowserWindow* window) {
  if (!window || !browser_) {
    return;
  }
  // The same read SelectSpace's tail does - a rail index carries no space id
  // C++ can hand atrium_core_space_theme.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::Value* theme = parsed->GetDict().Find("spaceTheme");
  if (!theme || theme->is_none()) {
    // Nothing to clear: this runs before the window has painted a theme at
    // all, and a clear here would steal the animator from a themed sibling.
    return;
  }
  std::string theme_json;
  base::JSONWriter::Write(*theme, &theme_json);
  // No transition - the window has only its flat tint up, so a fade would
  // just be the first frame arriving late.
  ApplyThemeSpec(window, &theme_json, /*transition_ms=*/0,
                 /*reset_phase=*/true);
  VLOG(1) << "Atrium: active space theme painted on a new window";
}

void BrowserController::OnThemeAnimatorTick(double ambient_time,
                                            double warmth) {
  AtriumBrowserWindow* window = themed_window_.get();
  if (!window) {
    // The themed window is gone - nothing left to re-raster onto.
    theme_animator_.Configure(/*night_shift=*/false, /*ambient=*/false,
                              /*reduced_motion=*/false);
    return;
  }
  if (themed_theme_json_.empty()) {
    return;
  }
  const bool dark = IsDarkWindow(window);
  size_t out_len = 0;
  uint8_t* rgba = atrium_core_theme_raster(
      themed_theme_json_.c_str(), kThemeBackdropWidth, kThemeBackdropHeight,
      dark, warmth, ambient_time, &out_len);
  if (!rgba) {
    return;
  }
  window->SetBackdropTheme(rgba, kThemeBackdropWidth, kThemeBackdropHeight,
                           themed_grain_, /*transition_ms=*/0);
  atrium_core_bytes_free(rgba, out_len);
}

void BrowserController::ReleaseThemedWindow() {
  themed_window_.reset();
  themed_theme_json_.clear();
  theme_animator_.Configure(/*night_shift=*/false, /*ambient=*/false,
                            /*reduced_motion=*/false);
}

void BrowserController::OnThemedWindowVisibilityChanged(bool visible) {
  theme_animator_.SetVisible(visible);
}

void BrowserController::AcceptCommandBarFavorite(const std::string& url) {
  // A scenario cannot otherwise tell this path from an accept that did
  // nothing: the activate branch below opens no tab, so an absent
  // cold-tab line proves nothing on its own.
  VLOG(1) << "Atrium: command bar favourite accepted "
          << atrium::LogSafeUrl(GURL(url));
  // The same verb a favourite tile click takes, via ActivateFavoriteRow
  // - see OpenOrActivateFavorite's own doc for why not
  // OpenOrActivateUrl.
  if (AtriumBrowserWindow* target = window(); target && !url.empty()) {
    target->OpenOrActivateFavorite(url);
  }
}

void BrowserController::AcceptCommandBarEngine(const std::string& keyword) {
  AtriumBrowserWindow* target = window();
  if (!target || !browser_ || keyword.empty()) {
    return;
  }
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(ActiveSpaceProfile());
  const TemplateURL* engine =
      service ? service->GetTemplateURLForKeyword(base::UTF8ToUTF16(keyword))
              : nullptr;
  if (!engine) {
    VLOG(1) << "Atrium: command bar engine accept found no engine for keyword "
            << keyword;
    return;
  }
  // The engine's HOME, not a search for nothing: Enter on a bare keyword names
  // the site, and the empty-terms search url lands on a results page instead.
  const GURL search = engine->GenerateSearchURL(service->search_terms_data());
  const GURL home = EngineHomeUrl(search);
  if (!home.is_valid()) {
    return;
  }
  // A favourite already claims this site: go to its own tab rather than
  // opening the engine beside it.
  const std::string favorite_url =
      FavoriteUrlForHost(home.spec(), CommandBarFavorites());
  if (!favorite_url.empty()) {
    VLOG(1) << "Atrium: command bar engine accept found the site's favourite";
    target->OpenOrActivateFavorite(favorite_url);
    return;
  }
  // The keyword, because LogSafeUrl reduces the home to scheme plus registrable
  // domain - the very path this line exists to prove survived.
  VLOG(1) << "Atrium: command bar engine accepted " << keyword << " -> "
          << atrium::LogSafeUrl(home);
  target->OpenOrActivateUrl(home.spec());
}

std::vector<CommandBarFavorite> BrowserController::CommandBarFavorites() {
  // The same snapshot the sidebar renders from, so the palette cannot show a
  // grid the sidebar disagrees with. Built on demand rather than cached: this
  // controller outlives any one snapshot, and a stored copy would go stale
  // exactly when the user edits their favourites and opens the palette to use
  // them.
  return FavoritesFromSidebarModel(
      atrium::BuildSidebarTabsJSON(browser_.get()));
}

std::vector<CommandBarSpace> BrowserController::CommandBarSpaces() {
  return SpacesWithBindings(atrium::BuildSidebarTabsJSON(browser_.get()));
}

std::vector<CommandBarSpace> BrowserController::SpacesWithBindings(
    const std::string& model) {
  std::vector<CommandBarSpace> spaces = SpacesFromSidebarModel(model);
  for (CommandBarSpace& space : spaces) {
    const std::optional<NativeHotkeyAction> action =
        NativeHotkeyActionForSpaceIndex(space.index);
    if (!action) {
      continue;
    }
    const std::vector<std::string> combos =
        NativeHotkeyMap::Get()->CombosFor(*action);
    if (!combos.empty()) {
      space.binding = CommandBarShortcutGlyphs(combos.front());
    }
  }
  return spaces;
}

std::vector<CommandBarTab> BrowserController::CommandBarTabs() {
  // The same snapshot the sidebar renders from, and built on demand for the
  // same reason as CommandBarFavorites above: a stored copy goes stale exactly
  // when the user opens the palette to act on what they are looking at.
  return TabsByRecency(atrium::BuildSidebarTabsJSON(browser_.get()));
}

CommandBarSidebarRows BrowserController::CommandBarRowsForPush() {
  const std::string model = atrium::BuildSidebarTabsJSON(browser_.get());
  return {FavoritesFromSidebarModel(model), TabsByRecency(model),
          SpacesWithBindings(model)};
}

std::vector<CommandBarTab> BrowserController::TabsByRecency(
    const std::string& model) {
  std::vector<CommandBarTab> tabs = TabsFromSidebarModel(model);

  // The strip's own activation stamp, set on every WasShown and so already
  // true of restored tabs. The Ctrl-Tab list in tab_recency.rs ranks nothing
  // until the user activates something, and costs a new ABI export to read.
  TabStripModel* strip = browser_ ? browser_->tab_strip_model() : nullptr;
  std::vector<std::pair<base::TimeTicks, std::string>> by_last_active;
  for (int i = 0; strip && i < strip->count(); ++i) {
    if (content::WebContents* contents = strip->GetWebContentsAt(i)) {
      by_last_active.emplace_back(contents->GetLastActiveTimeTicks(),
                                  base::NumberToString(TabId(contents)));
    }
  }
  std::stable_sort(
      by_last_active.begin(), by_last_active.end(),
      [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<std::string> recency_order;
  recency_order.reserve(by_last_active.size());
  for (auto& entry : by_last_active) {
    recency_order.push_back(std::move(entry.second));
  }
  ApplyTabRecencyOrder(recency_order, &tabs);
  return tabs;
}

void BrowserController::RetryFaviconForVisitedURL(const std::string& url) {
  if (url.empty() || !browser_ ||
      IconCache::UrlFor(browser_->profile()).Find(url)) {
    return;
  }
  IconBackfillQueueFor(reinterpret_cast<uintptr_t>(browser_->profile()))
      .AllowRetry(url);
  RequestFaviconForURL(url);
}

void BrowserController::RequestFaviconForURL(const std::string& url) {
  RequestFaviconsForRowURLs({url});
}

void BrowserController::RequestFaviconsForRowURLs(
    const std::vector<std::string>& urls,
    bool allow_refetch) {
  if (!browser_) {
    return;
  }
  // Security review finding (favicon-not-shown fix): FaviconServiceFactory
  // redirects an OTR profile's EXPLICIT_ACCESS request to its ORIGINAL
  // profile's favicon database (favicon_service_factory.cc) - without this
  // guard, an incognito window's favourites would silently pull favicon data
  // sourced from the user's regular browsing history, a cross-mode leak the
  // reactive per-tab pipeline never had, since that only ever reflects the OTR
  // tab's own live navigation.
  if (browser_->profile()->IsOffTheRecord()) {
    return;
  }
  if (!FaviconServiceFactory::GetForProfile(
          browser_->profile(), ServiceAccessType::EXPLICIT_ACCESS)) {
    return;
  }
  // Filtered BEFORE the queue, never inside the lookup: a URL admitted and
  // then dropped would hold a slot that nothing ever finishes.
  std::vector<std::string> wanted;
  IconCache& icons = IconCache::UrlFor(browser_->profile());
  for (const std::string& url : urls) {
    if (!icons.Find(url) && GURL(url).is_valid()) {
      wanted.push_back(url);
    }
  }
  // Latched here, where browser_ is known live. Teardown nulls browser_ but
  // still has to reach the queue to free this controller's slots.
  favicon_backfill_key_ = reinterpret_cast<uintptr_t>(browser_->profile());
  const std::vector<std::string> starting =
      IconBackfillQueueFor(favicon_backfill_key_).Admit(wanted, allow_refetch);
  // The count, never the URLs: this is the one place a reader can tell "the
  // backfill found nothing to do" from "it never ran".
  if (!starting.empty()) {
    VLOG(1) << "Atrium: favicon backfill starting " << starting.size()
            << " of " << wanted.size() << " rows missing an icon";
  }
  for (const std::string& url : starting) {
    StartFaviconLookup(url);
  }
}

void BrowserController::StartFaviconLookup(const std::string& url) {
  // OnTabStripModelDestroyed nulls browser_, and this runs from a lookup
  // callback that may land after it. window() CHECKs browser_, so an
  // unguarded continuation is a crash rather than a missing icon.
  if (!browser_) {
    // The slot is this controller's to give back - nothing else will.
    ReleaseFaviconSlots({url});
    return;
  }
  favicon_in_flight_.insert(url);
  row_icon_lookup_.Begin(url);
  const GURL page_url(url);
  // Non-null: RequestFaviconsForRowURLs admits nothing without it, so a queue
  // slot can never be taken by a lookup that has no way to answer.
  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(browser_->profile(),
                                           ServiceAccessType::EXPLICIT_ACCESS);
  favicon_service->GetRawFaviconForPageURL(
      page_url, {favicon_base::IconType::kFavicon},
      // fallback_to_host=true: a favourite may reference a host that
      // IS visited under a different path than the one it was imported with -
      // falling back to any icon known for the host, not just this exact page
      // URL, still resolves that case.
      /*desired_size_in_pixel=*/0, /*fallback_to_host=*/true,
      base::BindOnce(
          // Unretained: a BrowserController is process-lifetime and never
          // destroyed - the same reason ProfileBootstrap binds itself this
          // way. The tracker below cancels outstanding lookups when the strip
          // goes away, which is the state that actually changes.
          [](BrowserController* self, std::string url,
             const favicon_base::FaviconRawBitmapResult& result) {
            if (!result.is_valid() || !result.bitmap_data) {
              self->StartLargeIconLookup(url);
              return;
            }
            VLOG(1) << "Atrium: favicon tier 1 (database) answered "
                    << LogSafeUrl(GURL(url));
            self->StoreRowIcon(url, std::vector<uint8_t>(
                                        result.bitmap_data->begin(),
                                        result.bitmap_data->end()));
            self->row_icon_lookup_.EndEarly(url);
          },
          base::Unretained(this), url),
      &favicon_lookup_tracker_);
}

void BrowserController::StartLargeIconLookup(const std::string& url) {
  // Second and last source, still a read of this profile's own database: it
  // considers touch and manifest icons as well as kFavicon, so a site that
  // ships only a 180px apple-touch-icon resolves here and nowhere else.
  favicon::LargeIconService* large_icon_service =
      LargeIconServiceFactory::GetForBrowserContext(browser_->profile());
  if (!large_icon_service) {
    row_icon_lookup_.EndEarly(url);
    return;
  }
  large_icon_service->GetLargeIconRawBitmapOrFallbackStyleForPageUrl(
      GURL(url), /*min_source_size_in_pixel=*/16,
      // 0 keeps whatever size the database holds. Resizing here would cost a
      // decode and re-encode per row, and the sidebar draws these at 16px.
      /*desired_size_in_pixel=*/0,
      base::BindOnce(
          [](BrowserController* self, std::string url,
             const favicon_base::LargeIconResult& result) {
            // A fallback style rather than a bitmap means this profile has
            // never stored an icon for the page - a favourite added by URL and
            // never opened. Only then does the site itself get asked.
            if (result.bitmap.is_valid() && result.bitmap.bitmap_data) {
              VLOG(1) << "Atrium: favicon tier 2 (large icon) answered "
                      << LogSafeUrl(GURL(url));
              self->StoreRowIcon(url,
                                 std::vector<uint8_t>(
                                     result.bitmap.bitmap_data->begin(),
                                     result.bitmap.bitmap_data->end()));
              self->row_icon_lookup_.EndEarly(url);
              return;
            }
            self->row_icon_lookup_.AskSite(url);
          },
          // Unretained: a BrowserController is process-lifetime and never
          // destroyed - the same reason ProfileBootstrap binds itself this way.
          base::Unretained(this), url),
      &favicon_lookup_tracker_);
}

void BrowserController::FetchIcon(const std::string& page_url,
                                  const GURL& icon_url,
                                  base::TimeDelta budget) {
  if (!browser_) {
    return;
  }
  // Small enough that a misconfigured host cannot spend real memory on a row
  // chip drawn at 16px.
  constexpr size_t kMaxIcoBytes = 256 * 1024;

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = icon_url;
  resource_request->method = "GET";
  // Anonymous, and never from the profile's cache: this must not read or write
  // the user's cookies, and it must not look like the user opened the site.
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->load_flags = net::LOAD_DISABLE_CACHE;
  // Many sites answer /favicon.ico with a 30x. OnIcoRedirect drops a hop
  // that leaves http(s) or lands on a local address.
  resource_request->redirect_mode = network::mojom::RedirectMode::kFollow;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("atrium_row_favicon", R"(
        semantics {
          sender: "Atrium Sidebar - Row Favicon"
          description:
            "Fetches /favicon.ico, or the icon a page names in its head, from "
            "a site the user already has a sidebar row for - a restored tab, "
            "a pinned tab or a favourite - so the row can show that site's "
            "icon. Only sent when the local favicon database holds no icon "
            "for the page, which is the case for a favourite the user added "
            "by URL and never opened."
          trigger: "A sidebar row is rendered whose page this profile has no "
            "stored icon for. Sent at most once per URL per session."
          data: "None. The request carries no cookies, no credentials and no "
            "identifiers - it is a plain anonymous GET for the icon."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Remove the tab, pinned tab or favourite from the sidebar "
            "and no request is made on its behalf."
          policy_exception_justification:
              "Not implemented - the origin is one the user themselves put in "
              "the sidebar, and this is the same request the browser would "
              "make on opening that row."
        })");

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(resource_request),
                                       traffic_annotation);
  loader->SetTimeoutDuration(budget);
  loader->SetOnRedirectCallback(base::BindRepeating(
      &BrowserController::OnIcoRedirect, base::Unretained(this), page_url));
  network::SimpleURLLoader* raw_loader = loader.get();
  // The queue admits each URL once, so nothing is in the map under this key.
  ico_loaders_[page_url] = std::move(loader);
  raw_loader->DownloadToString(
      browser_->profile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&BrowserController::OnIcoFetched, base::Unretained(this),
                     page_url, icon_url.spec()),
      kMaxIcoBytes);
}

void BrowserController::OnIcoRedirect(
    std::string page_url,
    const GURL& url_before_redirect,
    const net::RedirectInfo& redirect_info,
    const network::mojom::URLResponseHead& response_head,
    std::vector<std::string>* removed_headers) {
  if (RedirectTargetAllowedForIcoFetch(redirect_info.new_url)) {
    return;
  }
  // The target, which the machine's own refusal log cannot carry.
  VLOG(1) << "Atrium: favicon refused a redirect to "
          << LogSafeUrl(redirect_info.new_url) << " for "
          << LogSafeUrl(GURL(page_url));
  // Deleting the loader is the cancel. Its completion callback never runs
  // after this, so the slot has to be given back here.
  ico_loaders_.erase(page_url);
  row_icon_lookup_.OnRedirectRefused(page_url);
}

void BrowserController::OnIcoFetched(std::string page_url,
                                     std::string icon_url,
                                     std::optional<std::string> body) {
  // Unlike tiers one and two, this fetch is not under favicon_lookup_tracker_
  // and nothing cancels it - a slow host can answer well after its window went.
  const auto loader = ico_loaders_.extract(page_url);
  if (!browser_) {
    row_icon_lookup_.OnIconFetched(page_url, GURL(icon_url),
                                   net::ERR_ABORTED, std::nullopt);
    return;
  }
  const int net_error =
      loader.empty() ? net::ERR_FAILED : loader.mapped()->NetError();
  if (net_error != net::OK || !body || body->empty()) {
    // The status code, which the machine's own failure log cannot carry.
    const network::mojom::URLResponseHead* head =
        loader.empty() ? nullptr : loader.mapped()->ResponseInfo();
    VLOG(1) << "Atrium: favicon fetch http="
            << (head && head->headers ? head->headers->response_code() : 0)
            << " for " << LogSafeUrl(GURL(page_url));
  }
  row_icon_lookup_.OnIconFetched(page_url, GURL(icon_url), net_error,
                                 std::move(body));
}

void BrowserController::DecodeAndStore(const std::string& page_url,
                                       const GURL& icon_url,
                                       std::string bytes) {
  // Out of process, always. These are bytes a remote host or the page itself
  // chose, and the browser process is the last place to hand them to an
  // image decoder.
  data_decoder::DecodeImageIsolated(
      base::as_byte_span(bytes), data_decoder::mojom::ImageCodec::kDefault,
      /*shrink_to_fit=*/true, /*max_size_in_bytes=*/4 * 1024 * 1024,
      // A .ico usually holds several sizes. The row chip is 16px at 2x.
      gfx::Size(32, 32),
      base::BindOnce(&BrowserController::OnIcoDecoded, base::Unretained(this),
                     page_url, icon_url.spec()));
}

void BrowserController::OnIcoDecoded(std::string page_url,
                                     std::string icon_url,
                                     const SkBitmap& bitmap) {
  // Read before OnIconStored below, which can erase the row.
  const int tier = static_cast<int>(row_icon_lookup_.TierOf(page_url));
  // Same reason as OnIcoFetched: the decode is out of process and answers on
  // its own schedule, which can be after this window closed.
  if (!browser_ || bitmap.drawsNothing()) {
    VLOG(1) << "Atrium: favicon tier " << tier
            << " decode produced nothing for " << LogSafeUrl(GURL(page_url));
    row_icon_lookup_.OnIconStored(page_url, false);
    return;
  }
  // Re-encoded rather than stored as fetched: the sidebar's WebUI serves these
  // bytes back to a renderer, and it may only ever serve an image this process
  // produced itself from a decode it controlled.
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (!png || png->empty()) {
    row_icon_lookup_.OnIconStored(page_url, false);
    return;
  }
  VLOG(1) << "Atrium: favicon tier " << tier << " answered "
          << LogSafeUrl(GURL(page_url)) << " from "
          << LogSafeUrl(GURL(icon_url));
  StoreRowIcon(page_url, *std::move(png));
  // SetOnDemandFavicons, not SetFavicons: it refuses to overwrite a real icon
  // and marks the entry expired for a later visit to replace. Skipped for
  // data: - it has no URL the database can name.
  if (GURL(icon_url).SchemeIsHTTPOrHTTPS()) {
    if (favicon::FaviconService* favicon_service =
            FaviconServiceFactory::GetForProfile(
                browser_->profile(), ServiceAccessType::EXPLICIT_ACCESS)) {
      favicon_service->SetOnDemandFavicons(
          GURL(page_url), GURL(icon_url), favicon_base::IconType::kFavicon,
          gfx::Image::CreateFrom1xBitmap(bitmap), base::DoNothing());
    }
  }
  row_icon_lookup_.OnIconStored(page_url, true);
}

void BrowserController::FetchPage(const std::string& page_url,
                                  const GURL& origin_root,
                                  base::TimeDelta budget) {
  if (!browser_) {
    return;
  }
  // Fourth tier. Nothing at /favicon.ico, so the site's origin root is asked
  // which icon it names - not the saved page, so an unauthenticated GET to a
  // saved deep path on an intranet app is never a side effect of this.
  constexpr size_t kMaxPageBytes = 256 * 1024;

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = origin_root;
  resource_request->method = "GET";
  resource_request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                                      "text/html");
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->load_flags = net::LOAD_DISABLE_CACHE;
  resource_request->redirect_mode = network::mojom::RedirectMode::kFollow;

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("atrium_row_favicon_page", R"(
        semantics {
          sender: "Atrium Sidebar - Row Favicon"
          description:
            "Fetches the origin root of a site the user has a sidebar row "
            "for, to read which icon it names in a link tag. Only sent when "
            "the local favicon database holds no icon for the page and the "
            "site has nothing at /favicon.ico."
          trigger: "A sidebar row is rendered whose page this profile has no "
            "stored icon for and whose origin answered /favicon.ico with an "
            "error. Sent at most once per URL per session."
          data: "None. The request carries no cookies, no credentials and no "
            "identifiers - it is a plain anonymous GET for the origin root."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Remove the tab, pinned tab or favourite from the sidebar "
            "and no request is made on its behalf."
          policy_exception_justification:
              "Not implemented - the page is one the user themselves put in "
              "the sidebar, and this is the same request the browser would "
              "make on opening that row."
        })");

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(resource_request),
                                       traffic_annotation);
  loader->SetTimeoutDuration(budget);
  // The head is what matters, so a long page is cut rather than refused.
  loader->SetAllowPartialResults(true);
  loader->SetOnRedirectCallback(base::BindRepeating(
      &BrowserController::OnIcoRedirect, base::Unretained(this), page_url));
  network::SimpleURLLoader* raw_loader = loader.get();
  ico_loaders_[page_url] = std::move(loader);
  raw_loader->DownloadToString(
      browser_->profile()
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&BrowserController::OnPageFetched, base::Unretained(this),
                     page_url),
      kMaxPageBytes);
}

void BrowserController::OnPageFetched(std::string page_url,
                                      std::optional<std::string> body) {
  const auto loader = ico_loaders_.extract(page_url);
  const network::mojom::URLResponseHead* head =
      loader.empty() ? nullptr : loader.mapped()->ResponseInfo();
  const GURL final_url = loader.empty() ? GURL() : loader.mapped()->GetFinalURL();
  row_icon_lookup_.OnPageFetched(page_url, final_url,
                                 head ? head->mime_type : "",
                                 std::move(body));
}

void BrowserController::CancelFetch(const std::string& page_url) {
  ico_loaders_.erase(page_url);
}

void BrowserController::FinishRow(const std::string& page_url) {
  FinishFaviconLookup(page_url);
}

void BrowserController::AllowRetry(const std::string& key, const GURL& url) {
  if (IconBackfillQueue* queue = FindIconBackfillQueue(favicon_backfill_key_)) {
    queue->AllowRetry(key);
  }
}

void BrowserController::StoreRowIcon(const std::string& url,
                                     std::vector<uint8_t> png) {
  // The cache belongs to a profile, and a lookup can answer after the strip
  // that named the profile went. Nothing renders these rows any more either.
  if (!browser_) {
    return;
  }
  if (IconBackfillQueue* queue = FindIconBackfillQueue(favicon_backfill_key_);
      queue && !png.empty()) {
    queue->MarkFound(url);
  }
  IconCache::UrlFor(browser_->profile()).Store(url, std::move(png));
  // ONE push for the batch. A push rebuilds the WHOLE sidebar, so pushing per
  // icon recreated every row's <img> once per resolved icon and flashed each
  // placeholder that many times.
  if (favicon_push_queued_) {
    return;
  }
  favicon_push_queued_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](BrowserController* self) {
                       self->favicon_push_queued_ = false;
                       // window() CHECKs browser_, which the strip's teardown
                       // nulls - so this guard is the difference between a
                       // missing icon and a crash.
                       if (!self->browser_) {
                         return;
                       }
                       // Every slot's visible window, since every rail shows
                       // every space. A hidden window forwards only a push
                       // that shows its own rows, and these icons can be for
                       // a rail pane.
                       std::vector<AtriumBrowserWindow*> shown;
                       for (BrowserController* c : Registry()) {
                         AtriumBrowserWindow* w =
                             c->browser_
                                 ? PresentedWindowForBrowser(c->browser_.get())
                                 : nullptr;
                         if (w && std::ranges::find(shown, w) == shown.end()) {
                           shown.push_back(w);
                         }
                       }
                       if (shown.empty()) {
                         shown.push_back(self->window());
                       }
                       for (AtriumBrowserWindow* w : shown) {
                         w->PushSidebarSnapshot();
                       }
                     },
                     base::Unretained(this)));
}

void BrowserController::FinishFaviconLookup(const std::string& url) {
  IconBackfillQueue* queue = FindIconBackfillQueue(favicon_backfill_key_);
  if (!queue) {
    return;
  }
  favicon_in_flight_.erase(url);
  const std::vector<std::string> next = queue->Finish(url);
  // Handing these to StartFaviconLookup with no browser_ would only make it
  // release each one again, one nested call per waiting URL.
  if (!browser_) {
    ReleaseFaviconSlots(next);
    return;
  }
  for (const std::string& start : next) {
    StartFaviconLookup(start);
  }
}

void BrowserController::ReleaseFaviconSlots(
    const std::vector<std::string>& urls) {
  IconBackfillQueue* queue = FindIconBackfillQueue(favicon_backfill_key_);
  if (!queue) {
    return;
  }
  for (const std::string& url : urls) {
    favicon_in_flight_.erase(url);
  }
  queue->Release(urls);
}

void BrowserController::SetAtriumTextFocused(bool focused) {
  if (atrium_text_focused_ == focused) {
    return;
  }
  atrium_text_focused_ = focused;
  // Logs the new answer, which is what the two edit-focus VM scenarios read.
  PushEditFocus();
}

namespace {

// Puts `row` in front of the user: activates its live tab, or - for a COLD
// row, one in the space with no live tab to activate - reopens its URL, which
// is what the surface does for a click on one. `window` is the window the
// reopen lands in; it applies the same scheme and realised-window checks.
bool ShowRow(BrowserController* controller,
             AtriumBrowserWindow* window,
             const CycleRow& row) {
  uint64_t tab_id = 0;
  if (!row.tab_id.empty() && base::StringToUint64(row.tab_id, &tab_id) &&
      tab_id != 0) {
    controller->ActivateOwnTab(tab_id);
    // ActivateTab can drop the call on a mismatched host, so confirm it
    // actually landed rather than trusting the call alone.
    if (!window) {
      return true;
    }
    content::WebContents* active = window->SidebarActiveTab();
    return active && TabId(active) == tab_id;
  }
  if (!row.url.empty() && window) {
    return window->OpenSidebarColdTab(row.url, /*foreground=*/true) != nullptr;
  }
  return false;
}

// A pinned row outlives its tab (mutate::close_tab keeps it cold), so a
// reselect handed to ShowRow's cold branch reopens the tab just closed.
const CycleRow* FirstRowWithLiveTab(const std::vector<CycleRow>& rows) {
  for (const CycleRow& row : rows) {
    uint64_t tab_id = 0;
    if (!row.tab_id.empty() && base::StringToUint64(row.tab_id, &tab_id) &&
        tab_id != 0) {
      return &row;
    }
  }
  return nullptr;
}

// A tab whose URL is a favourite gets a TILE and no row (sidebar.rs's
// is_favorite filter), so DrawnOrderRows alone under-reports which live tabs
// the space accounts for.
bool FavoriteTileHoldsTab(const base::DictValue& root,
                          const std::string& tab_id) {
  const base::ListValue* tiles = root.FindList("favorites");
  if (!tiles || tab_id.empty()) {
    return false;
  }
  for (const base::Value& tile : *tiles) {
    const base::DictValue* dict = tile.GetIfDict();
    const std::string* bound = dict ? dict->FindString("tabId") : nullptr;
    if (bound && *bound == tab_id) {
      return true;
    }
  }
  return false;
}


}  // namespace

bool BrowserController::CycleTabInSidebarOrder(int direction) {
  if (!browser_) {
    return false;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const std::vector<CycleRow> rows = CycleOrderRows(parsed->GetDict());
  const std::optional<size_t> target = NextCycleTarget(rows, direction);
  if (!target.has_value()) {
    return false;
  }
  return ShowRow(this, window(), rows[*target]);
}

bool BrowserController::CycleSpace(int direction) {
  if (!browser_) {
    return false;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue& root = parsed->GetDict();
  // No spaces key at all is the core-shut fall-through, which has no space
  // model to cycle. Declining lets the chord fall through rather than be
  // swallowed, the same bargain CycleTabInSidebarOrder strikes.
  const base::ListValue* spaces = root.FindList("spaces");
  if (!spaces) {
    return false;
  }
  // The Overview is not a space, so the core still names the space it opened
  // from. Stepping from that one skipped the first space.
  AtriumBrowserWindow* sidebar = SidebarWindow();
  const bool on_overview = sidebar && sidebar->IsOverviewShowing();
  const int active = root.FindInt("activeSpace").value_or(-1);
  const std::optional<size_t> target =
      NextSpaceIndex(spaces->size(), on_overview ? -1 : active, direction);
  if (!target.has_value()) {
    return false;
  }
  // The core refuses to select the space it already has, which would leave the
  // rail on the Overview and hand the chord to its stock meaning.
  // PersistsTabs mirrors SelectSpace's off-the-record refusal.
  if (on_overview && static_cast<int>(*target) == active && PersistsTabs()) {
    sidebar->GlideSidebarToSpace(active);
    return true;
  }
  return SelectSpace(static_cast<int>(*target));
}

// static
bool BrowserController::OpenRoutedURL(Browser* from,
                                      content::WebContents* source,
                                      const std::string& space_id,
                                      const GURL& url) {
  BrowserController* invoking = ControllerForBrowser(from);
  if (!invoking) {
    return false;
  }
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    // A rule naming a space the user has since deleted. Routing nothing is the
    // only safe answer - any fallback would be a guess about where they meant.
    return false;
  }
  const std::string slot_json(raw);
  atrium_core_string_free(raw);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(slot_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const std::optional<int> index = parsed->GetDict().FindInt("index");
  const std::string* profile_id = parsed->GetDict().FindString("profileId");
  if (!index.has_value() || !profile_id) {
    return false;
  }
  // The invoking window's own strip for that profile. Only the first window
  // has one per profile from the start, and SelectSpace makes one for another
  // window. The first window's strip would switch the first window instead.
  const auto target_for_profile = [&]() -> BrowserController* {
    BrowserController* target = ControllerFor(invoking->slot_, *profile_id);
    if ((!target || !target->browser_) && invoking->slot_ != kDefaultSlot) {
      invoking->SelectSpace(*index);
      target = ControllerFor(invoking->slot_, *profile_id);
    }
    return target && target->browser_ ? target : nullptr;
  };

  if (!source) {
    if (*index == SpaceIndexForSlot(invoking->slot_)) {
      // The URL is headed for the space already on screen, and the caller
      // opens it there itself. Switching to it and opening a second time is
      // the whole of what declining avoids.
      return false;
    }
    const ScopedRoutingBypass no_routing;
    if (*profile_id != invoking->profile_id_) {
      BrowserController* target = target_for_profile();
      if (!target) {
        return false;
      }
      // A profile bootstrapped hidden has a Browser but no native host, and
      // adding a tab to one aborts the process.
      if (!RoutedOpenTargetIsUsable(target->browser_.get())) {
        VLOG(1) << "Atrium: routed open declined - target window not realised";
        return false;
      }
      target->SelectSpace(*index);
      content::WebContents* opened = chrome::AddAndReturnTabAt(
          target->browser_.get(), url, -1, /*foreground=*/true);
      if (!opened) {
        return false;
      }
      // Only the in-window swap reaches the presenter's pane, and SelectSpace
      // has just emptied it - without this the rail moves to a blank space.
      ActivateWindow(target);
      VLOG(1) << "Atrium: routed " << atrium::LogSafeUrl(url) << " into space "
              << space_id << ", opened there";
      return true;
    }
    if (!invoking->SelectSpace(*index)) {
      // Declining leaves the caller to open the URL where it stands, which
      // beats opening it in a space the rail is not showing.
      return false;
    }
    chrome::AddTabAt(invoking->browser_.get(), url, -1, /*foreground=*/true);
    VLOG(1) << "Atrium: routed " << atrium::LogSafeUrl(url) << " into space "
            << space_id << ", opened there";
    return true;
  }

  // The reopen and the in-place load must not be routed again - two rules
  // pointing at each other's spaces would otherwise bounce this tab forever.
  const ScopedRoutingBypass no_routing;

  if (*profile_id != invoking->profile_id_) {
    BrowserController* target = target_for_profile();
    if (!target) {
      return false;
    }
    // A profile bootstrapped hidden has a Browser but no native host, and
    // adding a tab to one aborts the process. Declining leaves the throttle to
    // load the URL where the user already was.
    if (!RoutedOpenTargetIsUsable(target->browser_.get())) {
      VLOG(1) << "Atrium: routed move declined - target window not realised";
      return false;
    }
    target->SelectSpace(*index);
    content::WebContents* opened = chrome::AddAndReturnTabAt(
        target->browser_.get(), url, -1, /*foreground=*/true);
    if (!opened) {
      return false;
    }
    // The other half of a move, and the half this used to skip: the tab the
    // user typed in was left sitting in the space they typed it from.
    TabStripModel* strip = invoking->browser_->tab_strip_model();
    const int at = strip->GetIndexOfWebContents(source);
    if (at != TabStripModel::kNoTab) {
      // Closing the source's last tab takes its window with it, accepted the
      // same way the reopen-and-close move at line ~1225 accepts it.
      strip->CloseWebContentsAt(at, TabCloseTypes::CLOSE_USER_GESTURE);
    }
    // Only the in-window swap reaches the presenter's pane, and SelectSpace has
    // just emptied it. Last, so the close above cannot present anything else.
    ActivateWindow(target);
    VLOG(1) << "Atrium: routed " << atrium::LogSafeUrl(url)
            << " into space " << space_id << ", source tab closed";
    return true;
  }

  // Same profile: the row travels with the tab, so the tab keeps its renderer
  // and its history and the user is not looking at a copy of it.
  if (!atrium_core_move_tab_id_to_space(TabId(source), space_id.c_str())) {
    // The space is already known to exist and the already-there case is
    // filtered by the throttle before this runs - so the only way in is a
    // tab with no row at all.
    VLOG(1) << "Atrium: routed tab had no row to move into space " << space_id;
  }
  if (!invoking->SelectSpace(*index)) {
    // SelectSpace's own save runs only on its success path, so the row
    // already moved above still needs to reach disk on its own.
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
        base::BindOnce([] {
          if (atrium_core_save() < 0) {
            LOG(WARNING) << "Atrium: a routed tab's move could not be "
                            "written; it is in memory only and a relaunch "
                            "loses it";
          }
        }));
  }
  // SelectSpace shows the target's remembered or first row, which is not
  // `source` when the target already held other tabs.
  TabStripModel* invoking_strip = invoking->browser_->tab_strip_model();
  const int source_index = invoking_strip->GetIndexOfWebContents(source);
  if (source_index != TabStripModel::kNoTab) {
    invoking_strip->ActivateTabAt(source_index);
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  source->GetController().LoadURLWithParams(params);
  VLOG(1) << "Atrium: routed " << atrium::LogSafeUrl(url) << " into space "
          << space_id << ", tab moved in place";
  return true;
}

bool BrowserController::SelectSpace(int index) {
  AtriumBrowserWindow* sidebar = SidebarWindow();
  if (!browser_ || !sidebar) {
    return false;
  }
  // Here rather than at the surface gate: the command bar, the settings window
  // and the overview all reach this directly. The switch also writes the
  // document's last-used space, which an incognito window must not touch.
  if (!PersistsTabs()) {
    VLOG(1) << "Atrium: space select refused, window is off the record";
    return false;
  }
  // Read before the move, so the log below names both ends.
  const int from = SpaceIndexForSlot(slot_);
  if (index == from) {
    return false;
  }
  // No id past the rail's end, and moving the strip to a space the rail is not
  // showing is worse than doing nothing.
  char* target = atrium_core_space_id_at(index);
  if (!target) {
    return false;
  }
  SlotSpaces()[slot_] = target;
  atrium_core_string_free(target);
  // A window with no space of its own follows the document, so it keeps its
  // space here before the document moves under it.
  const int last_used = atrium_core_active_space_index();
  for (BrowserController* controller : Registry()) {
    if (controller->browser_ && !SlotSpaces().contains(controller->slot_)) {
      SetSlotSpace(controller->slot_, last_used);
    }
  }
  // Launch and a new window with no invoking window open the last-used space.
  atrium_core_set_active_space_index(index);
  RecheckAutoMiniPlayerTabs();
  // Only the first window had a hidden controller per profile, so a second
  // window kept its own profile's strip on another profile's space. A profile
  // still loading shows its strip from ShowLoadedProfile when the load lands.
  const std::string active_profile_id = ActiveSpaceProfileId();
  Profile* active_profile = atrium::LoadedProfileForId(active_profile_id);
  if (!active_profile) {
    ProfileBootstrap::EnsureProfileLoaded(active_profile_id);
  } else if (slot_ != kDefaultSlot && ActiveSpaceController()->slot_ != slot_) {
    CreateForProfile(active_profile, /*show=*/false,
                     /*startup_url=*/std::string(), slot_);
  }
  // Unconditional: ActiveSpaceController() answers `this` for a space back on
  // this profile, and the strip still carries the profile we came from.
  BrowserController* owner = ActiveSpaceController();
  ExtensionActionBridge::PushActionsToWindow(
      sidebar, owner, owner->CurrentExtensionActionsJSON());
  // ask the page to GLIDE first. The swipe route animates because
  // the gesture itself moves the rail and only then reports where it landed; the
  // keyboard route had nothing equivalent, so the rail simply appeared at the
  // destination. window.atriumSwipeTo drives the same settle machinery a
  // released swipe uses and had never been called from here.
  //
  // BEFORE the rebuild below, and that order is the whole of it: a rebuild draws
  // the destination, and a glide told to travel somewhere it is already showing
  // has nowhere to travel from.
  sidebar->GlideSidebarToSpace(index);
  // Writes the choice and redraws the rail. This half already worked.
  sidebar->ApplySidebarGesture(true);

  // Another profile's space opens its rows in that profile's strip. Opened
  // here, a cold row became a tab the space never draws live.
  BrowserController* shown = owner->slot_ == slot_ ? owner : this;
  if (VisibleBrowserForBrowser(browser_.get()) != shown->browser_.get()) {
    ActivateWindow(shown);
  }

  // The other half, and the whole of the window still shows the old
  // space's tabs until one of the new space's rows is opened. Rebuilt rather
  // than read from the caller's snapshot, because the core has moved since and
  // only the ACTIVE space's rows are resolved against the live strip.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(shown->browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return true;
  }
  const base::DictValue& root = parsed->GetDict();
  const std::vector<CycleRow> rows = DrawnOrderRows(root);
  const std::string* name = root.FindString("spaceName");
  // Both ends, because which way a route moves is an open question that
  // neither endpoint alone answers.
  VLOG(1) << "Atrium: active space " << from << " -> " << index << " '"
          << (name ? name->c_str() : "") << "', " << rows.size() << " rows";
  // the new space's own tint, already resolved by the core -
  // ink continuity across a switch was the one caller SetBackdropTint never
  // had.
  // A swipe's scrub already has this space's backdrop under the finger
  //, and a cross-fade on top would replay it.
  const BrowserController* scrubber = SpaceScrubOwner(sidebar);
  const bool scrubbed =
      scrubber && scrubber->space_scrub_->target_index == index;
  if (const std::string* tint = root.FindString("spaceTint");
      tint && !scrubbed) {
    if (std::optional<SkColor> color = ParseHexColor(*tint)) {
      sidebar->SetBackdropTint(*color);
    }
  }
  // The new space's theme, already resolved by the core (Snapshot's
  // spaceTheme) - a rail index carries no space id C++ can hand
  // atrium_core_space_theme, so this skips straight to raster.
  if (scrubbed) {
    // EndSpaceScrub does the theme bookkeeping when the swipe lands.
  } else if (const base::Value* theme = root.Find("spaceTheme");
             theme && !theme->is_none()) {
    std::string theme_json;
    base::JSONWriter::Write(*theme, &theme_json);
    ApplyThemeSpec(sidebar, &theme_json, 300, /*reset_phase=*/true);
  } else {
    ApplyThemeSpec(sidebar, nullptr, 300, /*reset_phase=*/true);
  }
  shown->ShowActiveSpaceInPane(sidebar, root, index, /*allow_cold_open=*/true);
  return true;
}

void BrowserController::ShowActiveSpaceInPane(AtriumBrowserWindow* sidebar,
                                              const base::DictValue& root,
                                              int index,
                                              bool allow_cold_open) {
  const std::vector<CycleRow> rows = DrawnOrderRows(root);
  // Where this space was, not its first row. Honoured only while that tab
  // still belongs here - a row, or a tile of this profile - so one that has
  // since moved out does not come back with it.
  const auto remembered = last_shown_tab_by_space_.find(index);
  const std::string last_shown =
      remembered == last_shown_tab_by_space_.end()
          ? std::string()
          : base::NumberToString(remembered->second);
  const auto row = std::ranges::find_if(
      rows, [&last_shown](const CycleRow& r) {
        return !last_shown.empty() && r.tab_id == last_shown;
      });
  // nothing to open, or a row that fails to open, must clear the
  // pane. One shared TabStripModel means nothing else changes what it shows.
  auto clear_pane = [sidebar] {
    // ShowTab(nullptr) already runs the detach and empty-card path.
    sidebar->ShowTab(nullptr);
    // The strip's active tab stays, so nothing else re-runs the tint.
    sidebar->ApplyPageTint();
  };
  if (row != rows.end()) {
    uint64_t expected_id = 0;
    base::StringToUint64(row->tab_id, &expected_id);
    if (ShowRow(this, sidebar, *row)) {
      // re-activating a tab already active notifies nobody, so
      // this puts the page back if leaving the space emptied the pane.
      sidebar->ShowActiveTabIfPaneIsEmpty(expected_id);
    } else {
      clear_pane();
    }
  } else if (uint64_t remembered_id = 0;
             !last_shown.empty() && FavoriteTileHoldsTab(root, last_shown) &&
             base::StringToUint64(last_shown, &remembered_id)) {
    // A favourite's tab is a row of no space, so the tile its profile draws is
    // the only thing that can recognise it as this space's. A bound tile is a
    // live tab by construction, so there is nothing further to check.
    ActivateOwnTab(remembered_id);
    sidebar->ShowActiveTabIfPaneIsEmpty(remembered_id);
  } else if (const CycleRow* opener =
                 allow_cold_open
                     ? (rows.empty() ? nullptr : &rows.front())
                     : FirstRowWithLiveTab(rows);
             opener) {
    uint64_t opener_id = 0;
    base::StringToUint64(opener->tab_id, &opener_id);
    if (ShowRow(this, sidebar, *opener)) {
      // A cold open fills the pane through the tab add. A live row fills it
      // only when it is still the strip's active tab.
      sidebar->ShowActiveTabIfPaneIsEmpty(opener_id);
    } else {
      clear_pane();
    }
  } else {
    clear_pane();
  }
  // A cleared pane changes no active tab, so no tab-strip event does this.
  SyncPeekWithPane();
}

bool BrowserController::TearingDown() const {
  return browser_shutdown::IsTryingToQuit() ||
         (browser_ && (browser_->IsAttemptingToCloseBrowser() ||
                       browser_->IsDeleteScheduled()));
}

void BrowserController::ReselectTabInActiveSpace() {
  AtriumBrowserWindow* sidebar = SidebarWindow();
  if (!browser_ || !sidebar || TearingDown()) {
    return;
  }
  // The routing can flip between the post and this turn, and in the
  // "displayed nowhere" state the on-screen pane belongs to another
  // controller - touching it evicts what the user is looking at.
  const std::optional<AtriumBrowserWindow*> displaying =
      DisplayingWindowForBrowser(browser_.get());
  if (!displaying.has_value() || *displaying != sidebar) {
    return;
  }
  // Re-read rather than trust the drift that scheduled this: a second close,
  // or the user's own click, may have already moved the strip back.
  if (!TabIsOutsideActiveSpace(
          browser_->tab_strip_model()->GetActiveWebContents())) {
    return;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser_.get()), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  ShowActiveSpaceInPane(sidebar, parsed->GetDict(), SpaceIndexForSlot(slot_),
                        /*allow_cold_open=*/false);
}

bool BrowserController::CreateSpace() {
  AtriumBrowserWindow* sidebar = SidebarWindow();
  if (!browser_ || !sidebar) {
    return false;
  }
  // Same refusal as SelectSpace: the spaces document is one document-wide
  // value and an off-the-record window must not write to it.
  if (!PersistsTabs()) {
    VLOG(1) << "Atrium: space create refused, window is off the record";
    return false;
  }
  char* raw = atrium_core_create_space(
      "New Space", browser_->profile()->GetBaseName().MaybeAsASCII().c_str());
  if (!raw) {
    return false;
  }
  atrium_core_string_free(raw);
  sidebar->ApplySidebarGesture(true);
  return true;
}

void BrowserController::SetPendingFavoriteKey(const std::string& favorite_url) {
  pending_favorite_key_ = favorite_url;
}

bool BrowserController::ParkFavoriteTab(int index) {
  if (!browser_ || TearingDown()) {
    return false;
  }
  TabStripModel* strip = browser_->tab_strip_model();
  if (!strip->ContainsIndex(index)) {
    return false;
  }
  content::WebContents* contents = strip->GetWebContentsAt(index);
  const std::string favorite_url = atrium::FavoriteKeyForTab(contents);
  if (favorite_url.empty() || contents->IsCrashed() ||
      contents->GetController().GetLastCommittedEntry()->GetPageType() ==
          content::PAGE_TYPE_ERROR ||
      !atrium::IsFavoriteAtHome(contents->GetLastCommittedURL(),
                                GURL(favorite_url),
                                atrium::FavoriteLandingForTab(contents))) {
    return false;
  }
  const std::optional<base::TimeDelta> keep_alive =
      FavoriteKeepAliveFromSettings(AtriumBrowserWindow::LastSettingsJson());
  if (!keep_alive) {
    return false;
  }
  const uint64_t id = TabId(contents);
  parking_contents_ = contents;
  std::unique_ptr<content::WebContents> parked =
      strip->DetachWebContentsAtForInsertion(index);
  parking_contents_ = nullptr;
  favorite_parking_.Park(favorite_url, std::move(parked), *keep_alive);
  VLOG(1) << "Atrium: favourite parked tab " << id;
  return true;
}

bool BrowserController::RestoreParkedFavorite(
    const std::string& favorite_url) {
  // Not TearingDown(): an emptied strip latches IsAttemptingToCloseBrowser
  // even when the window stays open, and this insert is what unlatches it
  // (see the kInserted case in OnTabStripModelChanged).
  if (!browser_ || browser_shutdown::IsTryingToQuit() ||
      browser_->IsDeleteScheduled()) {
    return false;
  }
  std::unique_ptr<content::WebContents> contents =
      TakeParkedFavorite(favorite_url);
  if (!contents) {
    return false;
  }
  const uint64_t id = TabId(contents.get());
  TabStripModel* strip = browser_->tab_strip_model();
  strip->InsertWebContentsAt(strip->count(), std::move(contents),
                             AddTabTypes::ADD_ACTIVE);
  VLOG(1) << "Atrium: favourite restored its parked tab " << id;
  return true;
}

// The window that parked the page is not always the one the user clicks in.
// Searching only here left the other window holding a live renderer nothing
// could reach until the keep-alive ran out.
std::unique_ptr<content::WebContents> BrowserController::TakeParkedFavorite(
    const std::string& favorite_url) {
  if (std::unique_ptr<content::WebContents> here =
          favorite_parking_.Take(favorite_url)) {
    return here;
  }
  for (BrowserController* source : Registry()) {
    if (source == this || source->profile_id_ != profile_id_) {
      continue;
    }
    if (std::unique_ptr<content::WebContents> elsewhere =
            source->favorite_parking_.Take(favorite_url)) {
      return elsewhere;
    }
  }
  return nullptr;
}

void BrowserController::DropParkedFavorite(const std::string& favorite_url) {
  for (BrowserController* source : Registry()) {
    if (source == this || source->profile_id_ == profile_id_) {
      source->favorite_parking_.Drop(favorite_url);
    }
  }
}

bool BrowserController::CanToggleSidebar() {
  AtriumBrowserWindow* target = SidebarWindow();
  return target && target->HasSidebar();
}

bool BrowserController::ToggleSidebar() {
  if (!CanToggleSidebar()) {
    return false;
  }
  AtriumBrowserWindow* target = SidebarWindow();
  // a pane standing open because the pointer is at the edge is not
  // yet the user's. Cmd+S on it DOCKS it - it keeps the pane and stops the
  // pointer leaving from taking it back. Collapsing instead would make the
  // chord's meaning depend on how the pane came to be open, which is not
  // something the user can see.
  if (auto* reveal = target->sidebar_edge_reveal();
      reveal && reveal->revealed_by_hover()) {
    reveal->ForgetReveal();
    return true;
  }
  // SetSidebarWidth leaves the remembered width alone when it collapses, so
  // the restore below reads the width the user last had rather than a
  // default. That was already true for the Swift toggle - see its comment on
  // last_docked_sidebar_width_, which names Cmd+S.
  //
  // What this does to the width the store holds: it overrides
  // it for the session and never writes it. A launch seeds the stored width
  // through this same SetSidebarWidth, so last_docked_sidebar_width_ is the
  // stored width by the time a toggle can be pressed, and expanding comes back
  // to it rather than to the 220 the slot was built at. Nothing in this build
  // writes appState.sidebarWidth, so no amount of toggling can move the file -
  // the next launch opens at the stored width whatever the last session did.
  target->SetSidebarWidth(
      target->IsSidebarCollapsed() ? target->LastDockedSidebarWidth() : 0);
  return true;
}

void BrowserController::OpenCommandBarOrNewTab() {
  // always the palette. The ordinary-tab arm existed for a dylib
  // with no command-bar group, and the palette no longer comes from a dylib
  // at all - see NewTab(this) for the fallback this replaced.
  OpenCommandBar();
}

// declared in atrium_feature_check.h; the mac body lives HERE, not
// in atrium_feature_check.cc, because it needs atrium_browser_controller.h /
// atrium_browser_window.h - headers whose NSView* members trip the
// chromium-rawptr clang plugin from a plain C++ TU (.mm TUs are exempt).
// See the header's doc for the contract.
views::Widget* GetTabModalDialogHostOverride(
    BrowserWindowInterface* browser_window_interface) {
  if (!browser_window_interface) {
    return nullptr;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // ControllerForBrowser non-null is the "this is an Atrium-managed Browser"
  // gate, same invariant atrium_fedcm_anchor_hook.cc's
  // GetFedCmFallbackAnchorView relies on for its static_cast below.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return nullptr;
  }
  AtriumBrowserWindow* presented =
      BrowserController::PresentedWindowForBrowser(browser);
  AtriumBrowserWindow* own_window =
      static_cast<AtriumBrowserWindow*>(browser->window());
  if (!presented || presented == own_window) {
    // Not swapped out (the overwhelmingly common case) - stock resolution
    // via BrowserElementsViewsImpl::GetPrimaryWindowWidget() already
    // resolves to `own_window`'s widget, so returning null here keeps that
    // path byte-identical.
    return nullptr;
  }
  return presented->widget();
}

// declared in atrium_feature_check.h; the mac body lives HERE, not
// atrium_feature_check.cc, same chromium-rawptr reason as
// GetTabModalDialogHostOverride above - it needs AtriumBrowserWindow's split
// API, not just BrowserWindowInterface. Re-routes into AtriumPaneContainerView
// split (EnterSplit/CloseSplit) because Atrium's TabStripModel never
// populates the TabCollection tree stock split (AddToNewSplit) relies on.
bool HighlightSelectionInAtriumNotes(
    BrowserWindowInterface* browser_window_interface,
    const std::string& quote) {
  if (!browser_window_interface) {
    return false;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // Same "this is an Atrium-managed Browser" gate as OpenLinkInAtriumSplit.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return false;
  }
  AtriumBrowserWindow* window =
      BrowserController::PresentedWindowForBrowser(browser);
  if (!window) {
    window = static_cast<AtriumBrowserWindow*>(browser->window());
  }
  if (!window) {
    return false;
  }
  return window->HighlightSelectionOnActivePage(quote);
}

bool OpenLinkInAtriumSplit(BrowserWindowInterface* browser_window_interface,
                           content::WebContents* source_web_contents,
                           const content::OpenURLParams& url_params,
                           bool stacked) {
  if (!browser_window_interface || !source_web_contents) {
    return false;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // Same "this is an Atrium-managed Browser" gate as GetTabModalDialogHostOverride
  // above - defensive here since the caller (render_view_context_menu.cc)
  // already gates on IsAtriumBrowserWindowEnabled() before reaching this.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return false;
  }
  // See SetSplitOp's comment: route to the slot's presenter window, falling
  // back to the browser's own window (mirrors AtriumCanDockDevtools's
  // fallback, the other free-function caller of PresentedWindowForBrowser).
  AtriumBrowserWindow* window =
      BrowserController::PresentedWindowForBrowser(browser);
  if (!window) {
    window = static_cast<AtriumBrowserWindow*>(browser->window());
  }
  if (!window) {
    return false;
  }

  if (window->IsSplit()) {
    // Mirrors stock OpenLinkInSplitView's split branch: navigate the OTHER
    // pane to the link, leaving the source pane untouched.
    const uint64_t partner_tab_id =
        window->SplitPartnerTabId(TabId(source_web_contents));
    if (partner_tab_id == 0) {
      return false;
    }
    content::WebContents* partner_contents = nullptr;
    TabStripModel* tab_strip_model = browser->tab_strip_model();
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(i);
      if (TabId(contents) == partner_tab_id) {
        partner_contents = contents;
        break;
      }
    }
    if (!partner_contents) {
      return false;
    }
    partner_contents->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(url_params));
    return true;
  }

  // Not split yet: open the link as a new tab, then fold it into a fresh
  // split on this window (mirrors stock's "create new background tab, then
  // AddToNewSplit" branch, minus the TabCollection call that NOTREACHEDs
  // here).
  content::WebContents* new_contents = source_web_contents->OpenURL(
      url_params, /*navigation_handle_callback=*/{});
  if (!new_contents) {
    return false;
  }
  window->EnterSplit(new_contents,
                     stacked ? SplitOrientation::kStacked
                             : SplitOrientation::kSideBySide,
                     0.5);
  return true;
}

// Declared in atrium_feature_check.h. The mac body lives HERE, same
// chromium-rawptr reason as GetTabModalDialogHostOverride above.
bool OpenLinkInAtriumLittleWindow(
    BrowserWindowInterface* browser_window_interface,
    const content::OpenURLParams& url_params) {
  if (!browser_window_interface) {
    return false;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // Same "this is an Atrium-managed Browser" gate as OpenLinkInAtriumSplit.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return false;
  }
  return BrowserController::CreateLittleWindow(
             browser->profile(), url_params.url, /*screen_point=*/std::nullopt,
             &url_params) != nullptr;
}

// declared in atrium_feature_check.h; mac body lives HERE, same
// chromium-rawptr reason as GetTabModalDialogHostOverride above - it needs
// AtriumBrowserWindow::IsSplit(), not just BrowserWindowInterface.
bool IsAtriumBrowserWindowSplit(
    BrowserWindowInterface* browser_window_interface,
    bool* stacked) {
  if (!browser_window_interface) {
    return false;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // Same ControllerForBrowser gate as OpenLinkInAtriumSplit above.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return false;
  }
  // Same presenter-first resolution as OpenLinkInAtriumSplit - the label
  // must reflect whichever window that call would actually act on.
  AtriumBrowserWindow* window =
      BrowserController::PresentedWindowForBrowser(browser);
  if (!window) {
    window = static_cast<AtriumBrowserWindow*>(browser->window());
  }
  const bool split = window && window->IsSplit();
  if (split && stacked) {
    *stacked = window->split_orientation() == SplitOrientation::kStacked;
  }
  return split;
}

// declared in atrium_devtools_bridge.h; the mac body lives HERE,
// not atrium_devtools_bridge.cc, for the same chromium-rawptr reason
// GetTabModalDialogHostOverride's does above.
bool AtriumCanDockDevtools(BrowserWindowInterface* browser_window_interface) {
  if (!browser_window_interface) {
    return false;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // Same ControllerForBrowser gate as GetTabModalDialogHostOverride above -
  // "this is an Atrium-managed Browser".
  return browser && BrowserController::ControllerForBrowser(browser);
}

bool AtriumIsChromeSurface(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  AtriumChromeContext* context = AtriumChromeContext::GetIfExists();
  return context && web_contents->GetBrowserContext() == context->context();
}

void MaybeUpdateAtriumDockedDevtools(
    BrowserWindowInterface* browser_window_interface,
    content::WebContents* inspected_web_contents) {
  if (!AtriumCanDockDevtools(browser_window_interface)) {
    return;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // PresentedWindowForBrowser, not a plain static_cast<AtriumBrowserWindow*>
  // of browser->window() - during a cross-profile in-window presenter swap
  // (as GetTabModalDialogHostOverride does) the VISIBLE window
  // may belong to a different Browser than `browser`; docked devtools must
  // track whichever window is actually presenting the inspected tab.
  AtriumBrowserWindow* window =
      BrowserController::PresentedWindowForBrowser(browser);
  if (!window) {
    // Fall back to the browser's own window (the
    // SetBackdropTintOp/SetSplitOp convention) instead of silently dropping
    // the update - unreachable in the steady state (a shown window is its
    // slot's presenter), but AtriumCanDockDevtools gates only on
    // ControllerForBrowser, so a momentary presenter-table gap must degrade
    // to attach-on-own-window, not to a docked DevTools that never shows.
    window = static_cast<AtriumBrowserWindow*>(browser->window());
    if (!window) {
      return;
    }
  }
  window->UpdateDockedDevtools(inspected_web_contents);
}

void BrowserController::CloseExtensionPopup() {
  if (extension_popup_host_) {
    extension_popup_host_->Close();
  }
}

// The click path itself, reached from the Swift arm via the op above and from
// the HTML arm via chrome.send("pillActionClicked") - one implementation, so
// the two arms cannot drift on which extensions are clickable or how popups
// are anchored.
std::string BrowserController::CurrentExtensionActionsJSON() const {
  return extension_bridge_ ? extension_bridge_->CurrentActionsJSON()
                           : std::string();
}

void BrowserController::ToggleExtensionPin(const std::string& extension_id) {
  if (!browser_) {
    return;
  }
  // A pushed snapshot can now name an id from a different profile's model
  // (see ControllerOwningExtension) - resolve which one, or a cross-profile
  // toggle reads as unknown.
  BrowserController* owner = ControllerOwningExtension(extension_id);
  if (!owner->browser_) {
    return;
  }
  ToolbarActionsModel* model = ToolbarActionsModel::Get(owner->browser_->profile());
  // The renderer supplies the id straight from the DOM, so an id the model
  // has forgotten is a logged no-op rather than a crash. The next resync
  // drops its tile.
  if (!model || !model->action_ids().contains(extension_id)) {
    VLOG(1) << "Atrium: extension pin toggle dropped, unknown id";
    return;
  }
  model->SetActionVisibility(extension_id, !model->IsActionPinned(extension_id));
  // owner's own OnToolbarPinnedActionsChanged already resynced its cached
  // snapshot, but owner is not this slot's presenter, so its own push (inside
  // PushActionsToHtmlArm) resolves no window - push the fresh JSON here too.
  if (owner != this && owner->extension_bridge_) {
    if (AtriumBrowserWindow* sidebar = SidebarWindow()) {
      ExtensionActionBridge::PushActionsToWindow(
          sidebar, owner, owner->extension_bridge_->CurrentActionsJSON());
    }
  }
}

void BrowserController::ClickExtensionAction(const std::string& extension_id) {
  BrowserController* self = this;
  // The clicked action can belong to a different profile's model (see
  // ControllerOwningExtension) - the tab, extension and popup all have to
  // come from that profile, or the lookups miss.
  BrowserController* owner = ControllerOwningExtension(extension_id);
  if (!owner->browser_) {
    return;
  }
  // Enabled-only lookup is deliberate here (unlike UninstallExtensionOp
  // below): a disabled extension has no action to run - RunAction below needs
  // a live ExtensionAction, and clicking a disabled extension's (absent)
  // toolbar icon is not a state Swift can even present.
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(owner->browser_->profile())
          ->enabled_extensions()
          .GetByID(extension_id);
  if (!extension) {
    LOG(WARNING) << "Atrium: extension_action_click - unknown/disabled "
                    "extension " << extension_id;
    return;
  }

  content::WebContents* active =
      owner->browser_->tab_strip_model()->GetActiveWebContents();
  extensions::ExtensionAction::TabID tab_id =
      extensions::ExtensionAction::kDefaultTabId;
  if (active) {
    extensions::ExtensionActionRunner* runner =
        extensions::ExtensionActionRunner::GetForWebContents(active);
    if (!runner) {
      return;
    }
    // A non-popup, non-side-panel action has already run end to end by the
    // time RunAction returns (the extension's own onClicked handler).
    const extensions::ExtensionAction::ShowAction show_action =
        runner->RunAction(extension, /*grant_tab_permissions=*/true);
    // RunAction only reports the toggle. Chrome's toolbar model performs it,
    // and Atrium has no toolbar model.
    if (show_action ==
        extensions::ExtensionAction::ShowAction::kToggleSidePanel) {
      extensions::side_panel_util::ToggleExtensionSidePanel(owner->browser_,
                                                            extension_id);
      return;
    }
    if (show_action != extensions::ExtensionAction::ShowAction::kShowPopup) {
      return;
    }
    tab_id = sessions::SessionTabHelper::IdForTab(active).id();
  }
  // extension_popup_host_ is built for every non-little window, so a null one
  // is not reachable in practice - guarded anyway against a page-triggered crash.
  if (!self->extension_popup_host_) {
    LOG(WARNING) << "Atrium: extension " << extension_id
                 << " requested a popup but this window has no popup host";
    return;
  }
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(owner->browser_->profile())
          ->GetExtensionAction(*extension);
  DCHECK(action);
  // With no active tab there is no permission grant and no onClicked handler
  // to run - a popup only opens if the action declares one for every tab.
  if (!active &&
      (!action->GetIsVisible(tab_id) || !action->HasPopup(tab_id))) {
    VLOG(1) << "Atrium: extension_action_click dropped - no active tab and "
               "no default popup for " << extension_id;
    return;
  }
  const GURL popup_url = action->GetPopupUrl(tab_id);
  // The popup's WebContents needs owner's profile - self's own is not always
  // the same one (see the comment above).
  std::unique_ptr<extensions::ExtensionViewHost> popup_host =
      extensions::ExtensionViewHostFactory::CreatePopupHost(
          *extension, popup_url, owner->browser_.get());
  if (!popup_host) {
    LOG(WARNING) << "Atrium: extension " << extension_id
                 << " popup host creation failed for "
                 << atrium::LogSafeUrl(popup_url);
    return;
  }
  // The popup window's Init() dereferences the anchor for its screen bounds,
  // so a null one is a logged no-op rather than a crash.
  views::View* anchor = self->window()->GetExtensionPopupAnchor().GetIfView();
  if (!anchor) {
    LOG(WARNING) << "Atrium: extension " << extension_id
                 << " popup dropped - no anchor";
    return;
  }
  self->extension_popup_host_->Open(std::move(popup_host), anchor);
}

Browser* BrowserController::BrowserOwningExtensionForContextMenu(
    const std::string& extension_id) {
  BrowserController* owner = ControllerOwningExtension(extension_id);
  if (!owner->browser_ ||
      !owner->browser_->tab_strip_model()->GetActiveWebContents()) {
    return nullptr;
  }
  return owner->browser_.get();
}

// static
BrowserController* BrowserController::ControllerForNativeWindow(
    gfx::NativeWindow window) {
  if (!window) {
    return nullptr;
  }
  for (BrowserController* controller : Registry()) {
    if (!controller->browser_) {
      continue;
    }
    // Crash: GetNSWindowForAnimation is CHECK(host) + CHECK(window),
    // and this loop calls it on EVERY registered controller while looking for
    // one. So a single entry whose window is already torn down aborts the whole
    // scan, whatever window the caller was actually asking about - which is why
    // the repro is "close a window, then press ANY key equivalent":
    // prePerformKeyEquivalent resolves the key window through here and walks
    // into the dead entry on the way.
    //
    // Guarded here rather than by loosening GetNSWindowForAnimation. Its only
    // other caller (atrium_sidebar_overlay_widget.mm) asks a specific parent it
    // has every reason to expect realized, and softening the CHECK would take
    // that invariant away to fix a defect it does not have.
    //
    // Same shape as patch 0018 and a collection entry does not imply
    // a realized window, and the skip names the entry so the next occurrence is
    // not the same investigation again.
    if (!controller->window() ||
        !controller->window()->HasRealizedNativeWindow()) {
      VLOG(1) << "Atrium: ControllerForNativeWindow skipping unrealized "
                 "window-slot "
              << controller->slot_ << " (teardown in flight)";
      continue;
    }
    if (controller->window()->GetNativeWindow() == window) {
      return controller;
    }
  }
  return nullptr;
}

// static
AtriumBrowserWindow* BrowserController::WindowForNativeWindow(
    gfx::NativeWindow window) {
  BrowserController* controller = ControllerForNativeWindow(window);
  return controller ? controller->window() : nullptr;
}

}  // namespace atrium
