// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// the surface-message handlers, one per name, and the table that
// binds them. The window used to route these through one if-chain of 68 arms;
// a new message is one row here and one handler, never a new arm. Each
// handler carries its arm verbatim - the name check inside is redundant now
// and kept so the two arms a name once had keep their order.

#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include "base/apple/foundation_util.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "base/auto_reset.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/notimplemented.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/observer_list.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_autofill_bubble_handler.h"
#include "chrome/browser/ui/atrium/atrium_accelerator_lookup.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_mac.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_ollama.h"
#include "chrome/browser/ui/atrium/atrium_ai_route_decision.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"
#include "chrome/browser/ui/atrium/atrium_claim_ledger_decision.h"
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_keychain_mac.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_little_window_registry.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "chrome/browser/ui/atrium/atrium_marginalia_script.h"
#include "chrome/browser/ui/atrium/atrium_media_hub.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_hex_color.h"
#include "chrome/browser/ui/atrium/atrium_hint_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_page_color.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/page_type.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "ui/base/page_transition_types.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "base/json/string_escape.h"
#include "base/path_service.h"
#include "content/public/browser/render_frame_host.h"
#include "chrome/browser/ui/atrium/atrium_favorite_slot.h"
#include "chrome/browser/ui/atrium/atrium_pin_toggle.h"
#include "chrome/browser/ui/atrium/atrium_restore_plan.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/webui_url_constants.h"
#include "ui/views/layout/fill_layout.h"
#include "third_party/skia/include/core/SkColor.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "base/files/file_util.h"
#include "chrome/browser/download/download_prefs.h"
#include "ui/gfx/codec/png_codec.h"
#include "chrome/browser/ui/atrium/atrium_capture.h"
#include "chrome/browser/ui/atrium/atrium_capture_math.h"
#include "chrome/browser/ui/atrium/atrium_capture_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_widget.h"
#include "chrome/browser/ui/atrium/atrium_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_devtools_card_corners.h"
#include "chrome/browser/ui/atrium/atrium_overview_controller.h"
#include "chrome/browser/ui/atrium/atrium_courtyard_widget.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_edge_reveal.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_grip_bounds.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_status_bubble_color_math.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "chrome/browser/ui/atrium/atrium_native_widget_mac.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble_anchor_util_views.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views_context.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/window_metadata/window_metadata_controller.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "components/url_formatter/elide_url.h"
#include "components/url_formatter/url_fixer.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/origin.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/global_accelerator_listener/global_accelerator_listener.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor_extra/shadow.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/native_ui_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/external_focus_tracker.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"
#include "chrome/browser/ui/atrium/atrium_surface_message_router.h"
using atrium::kTitlebarHeight;

namespace atrium {

namespace {

// The hub of the profile this window SHOWS, which a cross-profile slot makes
// different from browser_->profile(). A tab id it does not hold is inert.
AtriumMediaHub* MediaHubFor(Browser* browser) {
  return browser ? AtriumMediaHub::ForProfile(browser->profile()) : nullptr;
}

// The tab id a media command names, or 0 when the surface sent something else.
uint64_t MediaTabArg(const base::ListValue& args) {
  uint64_t tab_id = 0;
  if (args.size() < 1 || !args[0].is_string() ||
      !base::StringToUint64(args[0].GetString(), &tab_id)) {
    return 0;
  }
  return tab_id;
}

}  // namespace

// static
base::span<const AtriumBrowserWindow::SurfaceRoute>
AtriumBrowserWindow::SurfaceRoutes() {
  // A member function, so the table can name the private handlers.
  static constexpr SurfaceRoute kSurfaceRoutes[] = {
    {"foyerReady", &AtriumBrowserWindow::OnSurface_FoyerReady},
    {"foyerMoveToSpace", &AtriumBrowserWindow::OnSurface_FoyerMoveToSpace},
    {"devBarReady", &AtriumBrowserWindow::OnSurface_DevBarReady},
    {"devBarCopyUrl", &AtriumBrowserWindow::OnSurface_DevBarCopyUrl},
    {"devBarPinToggled", &AtriumBrowserWindow::OnSurface_DevBarPinToggled},
    {"devBarShadowSpec", &AtriumBrowserWindow::OnSurface_DevBarShadowSpec},
    {"devBarPointerDown", &AtriumBrowserWindow::OnSurface_DevBarPointerDown},
    {"devBarTabDragStarted", &AtriumBrowserWindow::OnSurface_DevBarTabDragStarted},
    {"devBarTabDragMoved", &AtriumBrowserWindow::OnSurface_DevBarTabDragMoved},
    {"devBarTabDragReleased", &AtriumBrowserWindow::OnSurface_DevBarTabDragReleased},
    {"sidebarOverviewPaneRendered", &AtriumBrowserWindow::OnSurface_SidebarOverviewPaneRendered},
    {"sidebarOverviewPaneActive", &AtriumBrowserWindow::OnSurface_SidebarOverviewPaneActive},
    {"sidebarRowsDrawn", &AtriumBrowserWindow::OnSurface_SidebarRowsDrawn},
    {"sidebarReady", &AtriumBrowserWindow::OnSurface_SidebarReady},
    {"pillActionClicked", &AtriumBrowserWindow::OnSurface_PillActionClicked},
    {"extensionChipContextMenu",
     &AtriumBrowserWindow::OnSurface_ExtensionChipContextMenu},
    {"extensionPinToggled", &AtriumBrowserWindow::OnSurface_ExtensionPinToggled},
    {"quickMenuToggled", &AtriumBrowserWindow::OnSurface_QuickMenuToggled},
    {"sidebarTabClicked", &AtriumBrowserWindow::OnSurface_SidebarTabClicked},
    {"sidebarColdTabClicked", &AtriumBrowserWindow::OnSurface_SidebarColdTabClicked},
    {"sidebarTabClosed", &AtriumBrowserWindow::OnSurface_SidebarTabClosed},
    {"mediaPlayPause", &AtriumBrowserWindow::OnSurface_MediaPlayPause},
    {"mediaPrev", &AtriumBrowserWindow::OnSurface_MediaPrev},
    {"mediaNext", &AtriumBrowserWindow::OnSurface_MediaNext},
    {"mediaMuteToggled", &AtriumBrowserWindow::OnSurface_MediaMuteToggled},
    {"mediaBackToTab", &AtriumBrowserWindow::OnSurface_MediaBackToTab},
    {"mediaClosed", &AtriumBrowserWindow::OnSurface_MediaClosed},
    {"mediaPictureInPicture",
     &AtriumBrowserWindow::OnSurface_MediaPictureInPicture},
    {"mediaVolume", &AtriumBrowserWindow::OnSurface_MediaVolume},
    {"vimVerb", &AtriumBrowserWindow::OnSurface_VimVerb},
    {"sidebarRestoreSlice", &AtriumBrowserWindow::OnSurface_SidebarRestoreSlice},
    {"sidebarTabReordered", &AtriumBrowserWindow::OnSurface_SidebarTabReordered},
    {"sidebarPinnedReordered", &AtriumBrowserWindow::OnSurface_SidebarPinnedReordered},
    {"sidebarTabPinned", &AtriumBrowserWindow::OnSurface_SidebarTabPinned},
    {"sidebarTabUnpinned", &AtriumBrowserWindow::OnSurface_SidebarTabUnpinned},
    {"sidebarPinnedTabRemoved", &AtriumBrowserWindow::OnSurface_SidebarPinnedTabRemoved},
    {"sidebarColdTabRemoved", &AtriumBrowserWindow::OnSurface_SidebarColdTabRemoved},
    {"sidebarTabSnoozed", &AtriumBrowserWindow::OnSurface_SidebarTabSnoozed},
    {"sidebarTabShelved", &AtriumBrowserWindow::OnSurface_SidebarTabShelved},
    {"sidebarFavoriteShelved", &AtriumBrowserWindow::OnSurface_SidebarFavoriteShelved},
    {"sidebarFavoriteRemoved", &AtriumBrowserWindow::OnSurface_SidebarFavoriteRemoved},
    {"sidebarFavoriteMovedOut", &AtriumBrowserWindow::OnSurface_SidebarFavoriteMovedOut},
    {"sidebarTabAddedToDock", &AtriumBrowserWindow::OnSurface_SidebarTabAddedToDock},
    {"sidebarRowFavorited", &AtriumBrowserWindow::OnSurface_SidebarRowFavorited},
    {"sidebarFavoriteGrouped",
     &AtriumBrowserWindow::OnSurface_SidebarFavoriteGrouped},
    {"sidebarTabGrouped", &AtriumBrowserWindow::OnSurface_SidebarTabGrouped},
    {"sidebarTabUngrouped", &AtriumBrowserWindow::OnSurface_SidebarTabUngrouped},
    {"sidebarTabSplit", &AtriumBrowserWindow::OnSurface_SidebarTabSplit},
    {"sidebarFolderMemberReordered",
     &AtriumBrowserWindow::OnSurface_SidebarFolderMemberReordered},
    {"sidebarFolderPinned", &AtriumBrowserWindow::OnSurface_SidebarFolderPinned},
    {"sidebarFolderCreated", &AtriumBrowserWindow::OnSurface_SidebarFolderCreated},
    {"sidebarFolderRenamed", &AtriumBrowserWindow::OnSurface_SidebarFolderRenamed},
    {"sidebarFolderDeleted", &AtriumBrowserWindow::OnSurface_SidebarFolderDeleted},
    {"sidebarFolderIconSet", &AtriumBrowserWindow::OnSurface_SidebarFolderIconSet},
    {"sidebarFolderReordered",
     &AtriumBrowserWindow::OnSurface_SidebarFolderReordered},
    {"sidebarFavoriteReordered", &AtriumBrowserWindow::OnSurface_SidebarFavoriteReordered},
    {"sidebarTabMovedToSpace", &AtriumBrowserWindow::OnSurface_SidebarTabMovedToSpace},
    {"sidebarRunMoved", &AtriumBrowserWindow::OnSurface_SidebarRunMoved},
    {"sidebarTabMirrored", &AtriumBrowserWindow::OnSurface_SidebarTabMirrored},
    {"sidebarTabDragMoved", &AtriumBrowserWindow::OnSurface_SidebarTabDragMoved},
    {"sidebarTabDragReleased", &AtriumBrowserWindow::OnSurface_SidebarTabDragReleased},
    {"sidebarPinnedToggled", &AtriumBrowserWindow::OnSurface_SidebarPinnedToggled},
    {"sidebarTextFocusChanged", &AtriumBrowserWindow::OnSurface_SidebarTextFocusChanged},
    {"sidebarNewTab", &AtriumBrowserWindow::OnSurface_SidebarNewTab},
    {"sidebarAddressRowClicked", &AtriumBrowserWindow::OnSurface_SidebarAddressRowClicked},
    {"devBarUrlClicked", &AtriumBrowserWindow::OnSurface_DevBarUrlClicked},
    {"sidebarHintShow", &AtriumBrowserWindow::OnSurface_SidebarHintShow},
    {"sidebarHintHide", &AtriumBrowserWindow::OnSurface_SurfaceHintHide},
    {"devBarHintShow", &AtriumBrowserWindow::OnSurface_DevBarHintShow},
    {"devBarHintHide", &AtriumBrowserWindow::OnSurface_SurfaceHintHide},
    {"sidebarBackPressed", &AtriumBrowserWindow::OnSurface_SidebarBackPressed},
    {"sidebarForwardPressed", &AtriumBrowserWindow::OnSurface_SidebarForwardPressed},
    {"sidebarReloadPressed", &AtriumBrowserWindow::OnSurface_SidebarReloadPressed},
    {"sidebarBackInNewTab", &AtriumBrowserWindow::OnSurface_SidebarBackInNewTab},
    {"sidebarBackToStart", &AtriumBrowserWindow::OnSurface_SidebarBackToStart},
    {"sidebarForwardInNewTab", &AtriumBrowserWindow::OnSurface_SidebarForwardInNewTab},
    {"sidebarForwardToEnd", &AtriumBrowserWindow::OnSurface_SidebarForwardToEnd},
    {"sidebarReloadHard", &AtriumBrowserWindow::OnSurface_SidebarReloadHard},
    {"sidebarReloadClearingCache", &AtriumBrowserWindow::OnSurface_SidebarReloadClearingCache},
    {"sidebarReloadClearingSite", &AtriumBrowserWindow::OnSurface_SidebarReloadClearingSite},
    {"sidebarMenuOpened", &AtriumBrowserWindow::OnSurface_SidebarMenuOpened},
    {"sidebarMenuClosed", &AtriumBrowserWindow::OnSurface_SidebarMenuClosed},
    {"sidebarToggleRequested", &AtriumBrowserWindow::OnSurface_SidebarToggleRequested},
    {"sidebarOverlayWidthLive", &AtriumBrowserWindow::OnSurface_SidebarOverlayWidthLive},
    {"sidebarOverlayWidthCommit", &AtriumBrowserWindow::OnSurface_SidebarOverlayWidthCommit},
    {"sidebarWindowClose", &AtriumBrowserWindow::OnSurface_SidebarWindowClose},
    {"sidebarWindowMinimize", &AtriumBrowserWindow::OnSurface_SidebarWindowMinimize},
    {"sidebarWindowZoom", &AtriumBrowserWindow::OnSurface_SidebarWindowZoom},
    {"sidebarBackHeld", &AtriumBrowserWindow::OnSurface_SidebarBackHeld},
    {"sidebarTimelineCommit", &AtriumBrowserWindow::OnSurface_SidebarTimelineCommit},
    {"sidebarFolderToggled", &AtriumBrowserWindow::OnSurface_SidebarFolderToggled},
    {"sidebarFavoriteClicked", &AtriumBrowserWindow::OnSurface_SidebarFavoriteClicked},
    {"sidebarSpaceSelected", &AtriumBrowserWindow::OnSurface_SidebarSpaceSelected},
    {"sidebarSpaceScrubBegan", &AtriumBrowserWindow::OnSurface_SidebarSpaceScrubBegan},
    {"sidebarSpaceScrubBlend", &AtriumBrowserWindow::OnSurface_SidebarSpaceScrubBlend},
    {"sidebarSpaceScrubEnded", &AtriumBrowserWindow::OnSurface_SidebarSpaceScrubEnded},
    {"sidebarSwipeHaptic", &AtriumBrowserWindow::OnSurface_SidebarSwipeHaptic},
    {"sidebarSpaceThemeRequested", &AtriumBrowserWindow::OnSurface_SidebarSpaceThemeRequested},
    {"settingsThemePreview", &AtriumBrowserWindow::OnSurface_SettingsThemePreview},
    {"settingsThemeChanged", &AtriumBrowserWindow::OnSurface_SettingsThemeChanged},
    {"sidebarTabArchived", &AtriumBrowserWindow::OnSurface_SidebarTabArchived},
    {"sidebarTabsArchivedBelow", &AtriumBrowserWindow::OnSurface_SidebarTabsArchivedBelow},
    {"sidebarTabDuplicated", &AtriumBrowserWindow::OnSurface_SidebarTabDuplicated},
    {"sidebarTabsCleared", &AtriumBrowserWindow::OnSurface_SidebarTabsCleared},
    {"sidebarSpaceReordered", &AtriumBrowserWindow::OnSurface_SidebarSpaceReordered},
    {"sidebarTabRenamed", &AtriumBrowserWindow::OnSurface_SidebarTabRenamed},
    {"sidebarSpaceCreated", &AtriumBrowserWindow::OnSurface_SidebarSpaceCreated},
    {"sidebarShelfToastClicked", &AtriumBrowserWindow::OnSurface_SidebarShelfToastClicked},
    {"sidebarOverviewSectionOpened",
     &AtriumBrowserWindow::OnSurface_SidebarOverviewSectionOpened},
    {"sidebarSpaceRenamed", &AtriumBrowserWindow::OnSurface_SidebarSpaceRenamed},
    {"sidebarSpaceIconSet", &AtriumBrowserWindow::OnSurface_SidebarSpaceIconSet},
    {"sidebarSpaceIconPickerOpened",
     &AtriumBrowserWindow::OnSurface_SidebarSpaceIconPickerOpened},
    {"sidebarOverviewToggled", &AtriumBrowserWindow::OnSurface_SidebarOverviewToggled},
    {"sidebarCourtyardOpened", &AtriumBrowserWindow::OnSurface_SidebarCourtyardOpened},
    {"openSettingsPane", &AtriumBrowserWindow::OnSurface_OpenSettingsPane},
    {"notesReady", &AtriumBrowserWindow::OnSurface_NotesReady},
    {"notesBodyChanged", &AtriumBrowserWindow::OnSurface_NotesBodyChanged},
    {"marginaliaNoteChanged", &AtriumBrowserWindow::OnSurface_MarginaliaNoteChanged},
    {"marginalMarkJumpRequested", &AtriumBrowserWindow::OnSurface_MarginalMarkJumpRequested},
    {"aiPanelReady", &AtriumBrowserWindow::OnSurface_AiPanelReady},
    {"aiPanelToggled", &AtriumBrowserWindow::OnSurface_AiPanelToggled},
    {"aiPanelWidthChanged", &AtriumBrowserWindow::OnSurface_AiPanelWidthChanged},
    {"aiPanelRowMark", &AtriumBrowserWindow::OnSurface_AiPanelRowMark},
    {"aiPanelSummarizeRequested", &AtriumBrowserWindow::OnSurface_AiPanelSummarizeRequested},
    {"aiPanelChatRequested", &AtriumBrowserWindow::OnSurface_AiPanelChatRequested},
    {"aiPanelToggleRequested", &AtriumBrowserWindow::OnSurface_AiPanelToggleRequested},
    {"extensionSidePanelToggleRequested", &AtriumBrowserWindow::OnSurface_ExtensionSidePanelToggleRequested},
    {"notesListReady", &AtriumBrowserWindow::OnSurface_NotesListReady},
    {"spacesListReady", &AtriumBrowserWindow::OnSurface_SpacesListReady},
    {"shelfListReady", &AtriumBrowserWindow::OnSurface_ShelfListReady},
    {"archivedListReady", &AtriumBrowserWindow::OnSurface_ArchivedListReady},
    {"snoozedListReady", &AtriumBrowserWindow::OnSurface_SnoozedListReady},
    {"downloadsShowInFinder", &AtriumBrowserWindow::OnSurface_DownloadsShowInFinder},
    {"downloadsRemove", &AtriumBrowserWindow::OnSurface_DownloadsRemove},
    {"downloadsDeleteFromDisk", &AtriumBrowserWindow::OnSurface_DownloadsDeleteFromDisk},
    {"shelfListAdd", &AtriumBrowserWindow::OnSurface_ShelfListAdd},
    {"shelfListRemove", &AtriumBrowserWindow::OnSurface_ShelfListRemove},
    {"shelfListOpen", &AtriumBrowserWindow::OnSurface_ShelfListOpen},
    {"notesListCreate", &AtriumBrowserWindow::OnSurface_NotesListCreate},
    {"notesListDismiss", &AtriumBrowserWindow::OnSurface_NotesListDismiss},
    {"notesListDelete", &AtriumBrowserWindow::OnSurface_NotesListDelete},
    {"notesListOpen", &AtriumBrowserWindow::OnSurface_NotesListOpen},
    {"archivedListRestore", &AtriumBrowserWindow::OnSurface_ArchivedListRestore},
    {"archivedListDelete", &AtriumBrowserWindow::OnSurface_ArchivedListDelete},
    {"snoozedListWake", &AtriumBrowserWindow::OnSurface_SnoozedListWake},
    {"snoozedListDelete", &AtriumBrowserWindow::OnSurface_SnoozedListDelete},
  };
  return kSurfaceRoutes;
}

void AtriumBrowserWindow::RegisterSurfaceRoutes() {
  surface_router_.Bind(this, SurfaceRoutes());
}

void AtriumBrowserWindow::OnSurface_FoyerReady(const base::ListValue& args) {
  // Same handshake lesson as the sidebar: a push before this lands in a
  // document with no atriumSetFoyer yet and is silently lost.
  PushFoyerSnapshot();
}

void AtriumBrowserWindow::OnSurface_FoyerMoveToSpace(
    const base::ListValue& args) {
  // Hand this little window's tab to a real space and close the
  // little window. See MoveLittleTabToSpace's own doc for why there is no
  // space picker yet and what it does instead.
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->MoveLittleTabToSpace();
  }
}

void AtriumBrowserWindow::OnSurface_DevBarReady(const base::ListValue& args) {
  // Same handshake lesson as the sidebar. The widget's own background is
  // cleared here too: at creation the view did not exist yet,
  // and a widget painting its own square showed at the corners.
  if (content::WebContents* bar = DevBarSender()) {
    if (content::RenderWidgetHostView* rwhv = bar->GetRenderWidgetHostView()) {
      rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
    }
    // A split bar is built after the last tint push, so it would open on the
    // defaults.
    PushOverlayTheme(bar);
  }
  PushDevBarState();
  // The bar hosts the nav group while it is up, and a page built after the
  // last navigation has seen no ability push at all.
  PushNavAbility();
  // The bar hosts the pinned strip now, and a page built after the last
  // resync has seen no snapshot at all.
  PushSidebarExtensionsForNewSurface();
}

void AtriumBrowserWindow::OnSurface_DevBarPointerDown(
    const base::ListValue& args) {
  if (!dev_bar_sender_ || !IsSplit()) {
    return;
  }
  AtriumPaneContainerView* panes = root_view_->pane_container();
  VLOG(1) << "Atrium: devbar press on pane " << dev_bar_sender_->index();
  if (dev_bar_sender_->index() < panes->pane_count()) {
    OnPaneWebContentsFocused(
        panes->PaneAt(dev_bar_sender_->index())->web_view());
  }
}

void AtriumBrowserWindow::OnSurface_DevBarTabDragStarted(
    const base::ListValue& args) {
  content::WebContents* tab = NavTargetWebContents();
  if (!tab || !sidebar_contents_) {
    return;
  }
  VLOG(1) << "Atrium: devbar drag started for tab " << atrium::TabId(tab);
  // The drag began in the bar's document, so the sidebar never saw a
  // dragstart and would ignore the drag without being told.
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      u"window.atriumBeginExternalTabDrag && atriumBeginExternalTabDrag(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(
              base::NumberToString(atrium::TabId(tab)))) +
          u")",
      base::NullCallback());
}

void AtriumBrowserWindow::OnSurface_DevBarTabDragMoved(
    const base::ListValue& args) {
  if (args.size() < 2 || !(args[0].is_int() || args[0].is_double()) ||
      !(args[1].is_int() || args[1].is_double()) || !browser_) {
    return;
  }
  const gfx::Point screen_point(static_cast<int>(args[0].GetDouble()),
                                static_cast<int>(args[1].GetDouble()));
  if (PreviewOwnWindowDrop(screen_point, NavTargetWebContents())) {
    return;
  }
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->PreviewDropForPoint(screen_point);
  }
}

void AtriumBrowserWindow::OnSurface_DevBarTabDragReleased(
    const base::ListValue& args) {
  // The sidebar never gets a dragend for this drag either. It answers whether
  // its own drop placed the row, and only then does a pane leave its split.
  content::WebContents* dragged = NavTargetWebContents();
  if (sidebar_contents_) {
    sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
        u"!!(window.atriumEndExternalTabDrag && atriumEndExternalTabDrag())",
        base::BindOnce(&AtriumBrowserWindow::OnSidebarExternalDropEnded,
                       weak_factory_.GetWeakPtr(),
                       dragged ? atrium::TabId(dragged) : 0));
  }
  BrowserController::ClearDropPreviews();
  ClearOwnPaneDropPreviews();
  // A move out of a private window writes where the tab lives, and the
  // sidebar's own drop names are already refused there.
  if (!WritesAllowed()) {
    VLOG(1) << "Atrium: surface message dropped, window is off the record: "
            << "devBarTabDragReleased";
    return;
  }
  content::WebContents* tab = NavTargetWebContents();
  if (args.size() < 2 || !(args[0].is_int() || args[0].is_double()) ||
      !(args[1].is_int() || args[1].is_double()) || !tab || !browser_) {
    return;
  }
  const gfx::Point screen_point(static_cast<int>(args[0].GetDouble()),
                                static_cast<int>(args[1].GetDouble()));
  if (DropTabOnOwnWindow(tab, screen_point)) {
    return;
  }
  VLOG(1) << "Atrium: devbar drag released tab " << atrium::TabId(tab)
          << " outside its window";
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->ClassifyAndRouteDroppedTab(tab, screen_point);
  }
}

void AtriumBrowserWindow::OnSidebarExternalDropEnded(uint64_t tab_id,
                                                     base::Value placed) {
  if (!tab_id || !placed.is_bool() || !placed.GetBool() || !WritesAllowed()) {
    return;
  }
  VLOG(1) << "Atrium: the sidebar placed tab " << tab_id;
  ClosePaneForTab(tab_id, "own-window-drop");
}

void AtriumBrowserWindow::CopyUrlToClipboard(const std::string& url) {
  if (url.empty()) {
    return;
  }
  ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
  scw.WriteText(base::UTF8ToUTF16(url));
  VLOG(1) << "Atrium: copied the url";
  ShowTransientConfirmation(u"Link copied");
}

void AtriumBrowserWindow::OnSurface_DevBarCopyUrl(
    const base::ListValue& args) {
  if (args.empty() || !args.front().is_string()) {
    return;
  }
  CopyUrlToClipboard(args.front().GetString());
}

namespace {

// The one success code atrium_core_save_settings answers with.
constexpr int32_t kAddressBarPinWritten = 1;

// The pin is flipped from the bar, not the settings window, so this is the one
// settings write starting outside AtriumSettingsWindow. Whole-document,
// because the core's writer takes nothing smaller.
int32_t SaveAddressBarPinned(bool pinned) {
  const base::FilePath path = ResolveSettingsFilePath();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(LoadSettingsJSON(path), base::JSON_PARSE_RFC);
  // Writing the one field onto a document this build cannot read would
  // replace every other setting with a default.
  if (!parsed || !parsed->is_dict()) {
    return 0;
  }
  parsed->GetDict().Set("addressBarPinned", pinned);
  std::string json;
  if (!base::JSONWriter::Write(*parsed, &json)) {
    return 0;
  }
  return SaveSettingsJSON(path, json);
}

}  // namespace

void AtriumBrowserWindow::OnSurface_DevBarPinToggled(
    const base::ListValue& args) {
  if (args.empty() || !args.front().is_bool()) {
    return;
  }
  const bool previous = DevBarPinned();
  const bool pinned = args.front().GetBool();
  SetDevBarPinned(pinned);
  // The shared writer, not a runner of our own: the settings window writes the
  // same file, and two sequenced runners are two racers.
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&SaveAddressBarPinned, pinned),
      base::BindOnce(
          [](bool previous, int32_t code) {
            if (code == kAddressBarPinWritten) {
              VLOG(1) << "Atrium: devbar pin written";
              return;
            }
            // The bar lit its own lamp on the click. Left lit over a refused
            // write it would promise a pin no restart brings back.
            LOG(WARNING) << "Atrium: devbar pin not written, code " << code;
            AtriumBrowserWindow::SetDevBarPinned(previous);
          },
          previous));
}

void AtriumBrowserWindow::OnSurface_DevBarShadowSpec(
    const base::ListValue& args) {
  if (args.size() < 8) {
    return;
  }
  const std::optional<double> offset_x = args[0].GetIfDouble();
  const std::optional<double> offset_y = args[1].GetIfDouble();
  const std::optional<double> blur = args[2].GetIfDouble();
  const std::optional<double> radius = args[3].GetIfDouble();
  const std::optional<double> r = args[4].GetIfDouble();
  const std::optional<double> g = args[5].GetIfDouble();
  const std::optional<double> b = args[6].GetIfDouble();
  const std::optional<double> a = args[7].GetIfDouble();
  if (!offset_x || !offset_y || !blur || !radius || !r || !g || !b || !a) {
    return;
  }
  const auto channel = [](double v) {
    return static_cast<U8CPU>(std::clamp(std::lround(v), 0L, 255L));
  };
  AtriumOverlaySurfaceShadow shadow;
  shadow.offset =
      gfx::Vector2d(std::lround(*offset_x), std::lround(*offset_y));
  shadow.blur = static_cast<float>(*blur);
  shadow.corner_radius = static_cast<float>(*radius);
  shadow.color = SkColorSetARGB(channel(*a * 255), channel(*r), channel(*g),
                                channel(*b));
  devbar_shadow_spec_ = shadow;
  // The away slide rides along: it is read from the same stylesheet, in the
  // same call, and C++ cannot see either value.
  if (args.size() >= 10) {
    const std::optional<double> away_shift_y = args[8].GetIfDouble();
    const std::optional<double> away_ms = args[9].GetIfDouble();
    if (away_shift_y && away_ms) {
      devbar_away_shift_ = gfx::Vector2d(0, std::lround(*away_shift_y));
      devbar_away_duration_ = base::Milliseconds(std::lround(*away_ms));
    }
  }
  PushDevBarShadow();
}

void AtriumBrowserWindow::OnSurface_SidebarOverviewPaneRendered(
    const base::ListValue& args) {
  // Overview moved from its own widget into a rail pane, so
  // no window opens any more and nothing else in a run log marks it. A
  // scenario cannot tell a built pane from a missing one without this.
  VLOG(1) << "Atrium: overview pane rendered";
  // the pane's own overviewReady. installOverview sends that, and
  // only the retired widget's document ran it - the pane came up empty.
  EnsureOverviewController();
  overview_controller_->PushAllLists();
}

void AtriumBrowserWindow::OnSurface_SidebarOverviewPaneActive(
    const base::ListValue& args) {
  const bool parked = !args.empty() && args.front().GetIfBool().value_or(false);
  // Whole literals, not a stem plus a word: the scenario grader greps the
  // tree for the pattern it waits on, and cannot see an interpolated one.
  VLOG(1) << (parked ? "Atrium: overview pane entered"
                     : "Atrium: overview pane left");
  SetSidebarOverviewOpen(parked);
}

void AtriumBrowserWindow::OnSurface_SidebarRowsDrawn(
    const base::ListValue& args) {
  sidebar_drawn_ = true;
  // the line a capture can wait on. The rows arrive some time
  // after the surface announces itself, and a fixed settle guessed wrong.
  const int rows = args.empty() ? 0 : args.front().GetIfInt().value_or(0);
  VLOG(1) << "Atrium: sidebar rows drawn " << rows;
  startup_hold_.Drawn();
}

void AtriumBrowserWindow::OnSurface_SidebarReady(const base::ListValue& args) {
  // the handshake is a chrome.send, invisible in a run log, so a
  // surface that never announces itself looks identical to one that did.
  VLOG(1) << "Atrium: sidebar surface announced itself";
  // The renderer announcing it is live. Pushing before this lands in a
  // document that has no atriumSetSidebar yet and is silently lost.
  PushSidebarSnapshotNow();
  PushSidebarExtensionsForNewSurface();
  PushNotificationTimeoutToSidebar();
  PushSwipeTuningToSidebar();
  PushSidebarAppearance();
  // The tint is pushed only when it CHANGES, and the window's first one
  // lands before this document exists. An incognito window never gets a
  // second, so without this it wears no tint at all.
  PushSidebarInk();
  PushOverlayTheme(sidebar_contents_.get());
  PushSidebarOnRight();
  // a freshly (re)built sidebar surface has no devbar-shown
  // state yet either - the same catch-up PushSidebarUrl already gets.
  PushDevBarState();
  PushNavAbility();
  PushSidebarBindings();
  // Creates the profile's hub on the first sidebar that asks, so a tab already
  // playing before this document existed still gets its record.
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->PushForNewSurface();
  }
}

void AtriumBrowserWindow::OnSurface_PillActionClicked(
    const base::ListValue& args) {
  // The sidebar's own chrome-row chips send this same message and take the
  // same controller method - one click-to-open-popup path, not a second.
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  // always set, never only on a hit. A caller with no rect must
  // clear the last one, or its popup anchors to the icon a different surface
  // reported earlier.
  SetExtensionPopupAnchorRect(
      args.size() >= 2 ? ScreenRectFromSurfaceArg(args[1]) : gfx::Rect());
  ClickExtensionActionFromSurface(args[0].GetString());
}

void AtriumBrowserWindow::OnSurface_ExtensionChipContextMenu(
    const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ShowExtensionContextMenuFromSurface(
      args[0].GetString(),
      args.size() >= 2 ? ScreenRectFromSurfaceArg(args[1]) : gfx::Rect());
}

void AtriumBrowserWindow::OnSurface_ExtensionPinToggled(
    const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ToggleExtensionPin(args[0].GetString());
}

void AtriumBrowserWindow::OnSurface_QuickMenuToggled(
    const base::ListValue& args) {
  if (args.size() < 4) {
    return;
  }
  for (size_t i = 0; i < 4; ++i) {
    if (!args[i].is_int()) {
      return;
    }
  }
  // The rect arrives in the SENDING document's own client space, so the
  // surface decides which translation applies. A sidebar that floats and a
  // bar that floats sit in different places.
  content::WebContents* surface =
      dev_bar_sender_ ? dev_bar_sender_->contents() : sidebar_contents();
  const gfx::Rect client(args[0].GetInt(), args[1].GetInt(),
                         args[2].GetInt(), args[3].GetInt());
  ToggleQuickMenu(SurfaceRectToWindow(surface, client));
}

void AtriumBrowserWindow::OnSurface_SidebarTabClicked(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string()) {
    ActivateSidebarTab(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarColdTabClicked(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    OpenSidebarColdTab(args[0].GetString(), /*foreground=*/true);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabClosed(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string()) {
    CloseSidebarTab(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_MediaPlayPause(
    const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->PlayPause(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaPrev(const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->PreviousTrack(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaNext(const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->NextTrack(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaMuteToggled(
    const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->ToggleMute(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaBackToTab(
    const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->BackToTab(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaClosed(const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->CloseMedia(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaPictureInPicture(
    const base::ListValue& args) {
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->TogglePictureInPicture(MediaTabArg(args));
  }
}

void AtriumBrowserWindow::OnSurface_MediaVolume(const base::ListValue& args) {
  if (args.size() < 2 || !(args[1].is_double() || args[1].is_int())) {
    return;
  }
  if (AtriumMediaHub* hub = MediaHubFor(SidebarTargetBrowser())) {
    hub->SetVolume(MediaTabArg(args), args[1].GetDouble());
  }
}

// a surface names a browser verb the same way vim-page.js does, so
// both reach the one dispatch rather than each growing its own.
void AtriumBrowserWindow::OnSurface_VimVerb(const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && VimModeEnabled()) {
    RunVimBrowserVerb(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarRestoreSlice(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    RestoreSidebarSlice(args[0].GetString());
  }
}

namespace {

// A trailing space index, or the window's own space - the sidebar never sends
// one, but the Courtyard shows every room at once and must say which. A -1
// holds the place of a later argument, like a favourite's drop slot.
int SpaceArg(const base::ListValue& args, size_t at, int window_space) {
  return args.size() > at && args[at].is_int() && args[at].GetInt() >= 0
             ? args[at].GetInt()
             : window_space;
}

}  // namespace

void AtriumBrowserWindow::OnSurface_SidebarTabReordered(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_reorder_tab(SpaceArg(args, 2, WindowSpaceIndex()),
                                args[0].GetInt(), args[1].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarPinnedReordered(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_reorder_pinned_item(SpaceArg(args, 2, WindowSpaceIndex()),
                                        args[0].GetInt(), args[1].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderReordered(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_reorder_folder(SpaceArg(args, 2, WindowSpaceIndex()),
                                   args[0].GetInt(), args[1].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabPinned(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_pin_tab(SpaceArg(args, 2, WindowSpaceIndex()),
                            args[0].GetInt(), args[1].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabUnpinned(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_unpin_tab(SpaceArg(args, 2, WindowSpaceIndex()),
                              args[0].GetInt(), args[1].GetInt()));
  }
}

namespace {

// The rows of one sidebar-model list, in the order the surface drew them.
std::vector<PinToggleRow> PinToggleRowsFrom(const base::ListValue* list) {
  std::vector<PinToggleRow> rows;
  if (!list) {
    return rows;
  }
  for (const base::Value& row : *list) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    PinToggleRow entry;
    const std::string* id = dict->FindString("id");
    if (const std::string* kind = dict->FindString("kind");
        kind && *kind == "folder") {
      entry.folder_id = id ? *id : std::string();
      entry.members = PinToggleRowsFrom(dict->FindList("tabs"));
      rows.push_back(std::move(entry));
      continue;
    }
    if (id) {
      entry.tab_id = *id;
    }
    entry.selected = dict->FindBool("selected").value_or(false);
    rows.push_back(std::move(entry));
  }
  return rows;
}

// A favourite's open page was claimed when it opened, so the strip observer
// gave it no row. Call after the favourite is gone: a favourite's url hides
// the row that matches it. An off-the-record window writes no rows.
bool OpenRowForUnclaimedTab(content::WebContents* tab, Browser* browser) {
  const atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser);
  return tab && controller && controller->PersistsTabs() &&
         atrium_core_open_tab(
             atrium_core_space_for_profile(
                 atrium::BrowserController::SpaceIndexForBrowser(browser),
                 controller->ProfileId().c_str()),
             atrium::TabId(tab), tab->GetVisibleURL().spec().c_str(),
             base::UTF16ToUTF8(tab->GetTitle()).c_str(), -1);
}

}  // namespace

// Cmd+D. Not a surface message, but it drives the same two core entry points
// as the pinned drop handlers above and shares their reading of the model.
bool AtriumBrowserWindow::TogglePinShownTab() {
  Browser* target = SidebarTargetBrowser();
  if (!target) {
    return false;
  }
  // The model the surface DREW, so the row this moves is the one the user can
  // see and its pinned index is the one the core's pinned list uses.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(target), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue& root = parsed->GetDict();
  std::vector<PinToggleRow> regular = PinToggleRowsFrom(root.FindList("tabs"));
  if (const base::ListValue* folders = root.FindList("folders")) {
    for (const base::Value& folder : *folders) {
      if (const base::DictValue* dict = folder.GetIfDict()) {
        std::vector<PinToggleRow> in_folder =
            PinToggleRowsFrom(dict->FindList("tabs"));
        regular.insert(regular.end(),
                       std::make_move_iterator(in_folder.begin()),
                       std::make_move_iterator(in_folder.end()));
      }
    }
  }
  const PinToggleAction action =
      DecidePinToggle(PinToggleRowsFrom(root.FindList("pinned")), regular);

  bool moved = false;
  switch (action.kind) {
    case PinToggleKind::kNothing:
      VLOG(1) << "Atrium: cmd+d found no row to pin or unpin";
      return false;
    case PinToggleKind::kPin: {
      uint64_t tab_id = 0;
      if (!base::StringToUint64(action.tab_id, &tab_id) || tab_id == 0) {
        return false;
      }
      // The same core entry point the drag onto the pinned strip uses. It
      // appends, because a key press names no slot to land in.
      moved = atrium_core_pin_tab_by_tab_id(tab_id, -1);
      VLOG(1) << "Atrium: cmd+d pinned tab " << tab_id << " "
              << (moved ? "moved" : "refused");
      break;
    }
    case PinToggleKind::kUnpin:
      // Slot 0, the top of the regular list, is the one destination that
      // always resolves: a slot past the last visible row is refused, and a
      // space whose rows are all grouped or favourited has none to aim at.
      moved = atrium_core_unpin_tab(
          WindowSpaceIndex(), base::checked_cast<int>(action.pinned_index), 0);
      VLOG(1) << "Atrium: cmd+d unpinned row " << action.pinned_index << " "
              << (moved ? "moved" : "refused");
      break;
    case PinToggleKind::kUnpinMember:
      // The drag out of a pinned folder's own entry point, to the same slot 0.
      moved = atrium_core_ungroup_member(
          WindowSpaceIndex(), action.folder_id.c_str(),
          base::checked_cast<int>(action.member_index), 0, 0);
      VLOG(1) << "Atrium: cmd+d unpinned folder member " << action.member_index
              << " " << (moved ? "moved" : "refused");
      break;
  }
  ApplySidebarGesture(moved);
  return moved;
}

// Cmd+1..9. Reads the grid the surface DREW and then goes back in through the
// click's own message, so a live tile activates its tab and a cold one reopens
// with no second copy of either rule here.
bool AtriumBrowserWindow::ActivateFavoriteInSlot(int ordinal) {
  Browser* target = SidebarTargetBrowser();
  if (!target) {
    return false;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(target), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  // The top-level grid is the ACTIVE space's, which is the one the key means.
  const base::ListValue* favorites = parsed->GetDict().FindList("favorites");
  if (!favorites) {
    return false;
  }
  const std::optional<size_t> index =
      FavoriteIndexFor(ordinal, favorites->size());
  if (!index) {
    VLOG(1) << "Atrium: favourite slot " << ordinal << " is past the "
            << favorites->size() << " this space has";
    return false;
  }
  const base::DictValue* tile = (*favorites)[*index].GetIfDict();
  const std::string* url = tile ? tile->FindString("id") : nullptr;
  if (!url || url->empty()) {
    return false;
  }
  const std::string* tab_id = tile->FindString("tabId");
  base::ListValue args;
  args.Append(*url);
  args.Append(tab_id ? *tab_id : std::string());
  VLOG(1) << "Atrium: favourite slot " << ordinal << " is tile " << *index;
  OnSurfaceMessage("sidebarFavoriteClicked", args);
  return true;
}

void AtriumBrowserWindow::OnSurface_SidebarPinnedTabRemoved(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_remove_pinned_tab(WindowSpaceIndex(), args[0].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarColdTabRemoved(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_remove_cold_tab(WindowSpaceIndex(), args[0].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabShelved(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    ShelveSidebarTab(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteShelved(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_string() &&
      browser_) {
    ShelveFavorite(args[0].GetString(), args[1].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteRemoved(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    const std::string url = args[0].GetString();
    const bool removed =
        atrium_core_remove_favorite(WindowSpaceIndex(), url.c_str());
    // The page goes with its tile, else it stays open with no row to reach it.
    // A refused removal must not cost the user a page, so the core goes first.
    Browser* target = removed ? SidebarTargetBrowser() : nullptr;
    if (atrium::BrowserController* controller =
            atrium::BrowserController::ControllerForBrowser(target)) {
      controller->DropParkedFavorite(url);
    }
    TabStripModel* strip = target ? target->tab_strip_model() : nullptr;
    for (int i = 0; strip && i < strip->count(); ++i) {
      content::WebContents* tab = strip->GetWebContentsAt(i);
      if (tab && atrium::FavoriteKeyForTab(tab) == url) {
        VLOG(1) << "Atrium: favourite removed - closing its tab "
                << atrium::TabId(tab);
        atrium::ClearFavoriteKeyForTab(tab);
        strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
        break;
      }
    }
    ApplySidebarGesture(removed);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteMovedOut(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_string() && args[1].is_string() &&
      args[2].is_int() && browser_) {
    const std::string url = args[0].GetString();
    // Every branch below either activates, opens or
    // refuses, and none of them said which. A drag that resolved to nothing
    // left no trace at all.
    VLOG(1) << "Atrium: favourite moved out to " << args[1].GetString()
            << " slot " << args[2].GetInt() << " url "
            << atrium::LogSafeUrl(GURL(url));
    content::WebContents* moved = nullptr;
    if (ActivateFavoriteTab(url) || ActivateTabWithUrl(url)) {
      Browser* target = SidebarTargetBrowser();
      TabStripModel* strip = target ? target->tab_strip_model() : nullptr;
      moved = strip ? strip->GetActiveWebContents() : nullptr;
    } else {
      // A favourite with no live tab has no row either, so the drag opens the
      // page. Leaving it cold was the alternative, and it would have left the
      // tiles that most need removing behind a row showing nothing.
      moved = OpenSidebarColdTab(url, /*foreground=*/true);
    }
    // The tab stops being a favourite's page the moment the favourite goes.
    // Left in place, the claim binds a tile that no longer exists and keeps
    // the tab looking like something it is not.
    atrium::ClearFavoriteKeyForTab(moved);
    const bool removed =
        atrium_core_remove_favorite(WindowSpaceIndex(), url.c_str());
    if (atrium::BrowserController* controller =
            removed ? atrium::BrowserController::ControllerForBrowser(
                          SidebarTargetBrowser())
                    : nullptr) {
      controller->DropParkedFavorite(url);
    }
    const bool opened =
        OpenRowForUnclaimedTab(moved, SidebarTargetBrowser());
    ApplySidebarGesture(removed || opened);
    VLOG(1) << "Atrium: favourite moved out - tab "
            << (moved ? base::NumberToString(atrium::TabId(moved))
                      : std::string("none"))
            << ", favourite " << (removed ? "removed" : "NOT REMOVED");
    // By tab id, not by row position: until the favourite was removed the page
    // had no row to count, so there is no visible index to send.
    if (moved && args[1].GetString() == "pinned") {
      const bool pinned =
          atrium_core_pin_tab_by_tab_id(atrium::TabId(moved), args[2].GetInt());
      ApplySidebarGesture(pinned);
      VLOG(1) << "Atrium: favourite moved out - pinned " << pinned;
    } else if (moved && args[1].GetString() == "tabs") {
      // A new row opens at the top, not where the preview showed it.
      ApplySidebarGesture(atrium_core_move_tab_by_tab_id(atrium::TabId(moved),
                                                         args[2].GetInt()));
    }
  }
}

namespace {

struct SnapshotRowTab {
  raw_ptr<content::WebContents> tab = nullptr;
  std::string url;
};

// The live tab behind row `index` of the snapshot's `section` list. The index
// counts the rows the sidebar draws, not strip positions.
SnapshotRowTab TabForSnapshotRow(Browser* target,
                                 const char* section,
                                 int index) {
  SnapshotRowTab found;
  if (!target) {
    return found;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(
      BuildSidebarTabsJSON(target), base::JSON_PARSE_RFC);
  const base::ListValue* rows =
      parsed && parsed->is_dict() ? parsed->GetDict().FindList(section)
                                  : nullptr;
  const base::DictValue* row =
      rows && index >= 0 && static_cast<size_t>(index) < rows->size()
          ? (*rows)[index].GetIfDict()
          : nullptr;
  const std::string* id = row ? row->FindString("id") : nullptr;
  const std::string* url = row ? row->FindString("url") : nullptr;
  TabStripModel* strip = target->tab_strip_model();
  for (int i = 0; id && !id->empty() && url && i < strip->count(); ++i) {
    content::WebContents* tab = strip->GetWebContentsAt(i);
    if (tab && base::NumberToString(atrium::TabId(tab)) == *id) {
      found.tab = tab;
      found.url = *url;
      break;
    }
  }
  return found;
}

}  // namespace

void AtriumBrowserWindow::OnSurface_SidebarTabAddedToDock(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    const int pinned_index = args[0].GetInt();
    ApplySidebarGesture(
        atrium_core_add_doorway(WindowSpaceIndex(), pinned_index));
    // No tab is ever pinned in the strip, so the row's tab is found by id.
    Browser* target = SidebarTargetBrowser();
    const SnapshotRowTab row = TabForSnapshotRow(target, "pinned", pinned_index);
    if (row.tab) {
      atrium::BrowserController::OpenOrFocusDoorwayWindow(
          target->profile(), row.tab->GetLastCommittedURL());
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarRowFavorited(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_string() &&
      browser_) {
    const bool pinned = args[1].GetString() == "pinned";
    const int window_space = WindowSpaceIndex();
    const int space = SpaceArg(args, 2, window_space);
    // The row's live tab, read before the favourite lands: the tile claims it
    // so the row goes and the tile's click comes back to this same tab. The
    // snapshot's rows are the active space's, so only that space can claim.
    Browser* target = space == window_space ? SidebarTargetBrowser() : nullptr;
    const SnapshotRowTab row =
        TabForSnapshotRow(target, pinned ? "pinned" : "tabs", args[0].GetInt());
    content::WebContents* row_tab = row.tab;
    const std::string& row_url = row.url;
    // Absent or non-numeric keeps the -1 append the row menu's own gesture
    // sends, which has no grid position to name.
    const int slot =
        (args.size() >= 4 && args[3].is_int()) ? args[3].GetInt() : -1;
    const bool added =
        atrium_core_favorite_row(space, args[0].GetInt(), pinned ? 1 : 0, slot);
    if (added && row_tab) {
      atrium::SetFavoriteKeyForTab(row_tab, row_url);
      VLOG(1) << "Atrium: favourited row claims its tab "
              << atrium::TabId(row_tab);
    }
    ApplySidebarGesture(added);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteGrouped(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_string() && args[1].is_string() &&
      args[2].is_int() && browser_) {
    const std::string url = args[0].GetString();
    VLOG(1) << "Atrium: favourite joins folder " << args[1].GetString()
            << " slot " << args[2].GetInt() << " url "
            << atrium::LogSafeUrl(GURL(url));
    // The same activate-or-open the move-out route takes: a favourite with no
    // live tab has no row either, so the drag has to open the page before
    // anything can be put in a folder.
    content::WebContents* moved = nullptr;
    if (ActivateFavoriteTab(url) || ActivateTabWithUrl(url)) {
      Browser* target = SidebarTargetBrowser();
      TabStripModel* strip = target ? target->tab_strip_model() : nullptr;
      moved = strip ? strip->GetActiveWebContents() : nullptr;
    } else {
      moved = OpenSidebarColdTab(url, /*foreground=*/true);
    }
    if (!moved) {
      return;
    }
    atrium::ClearFavoriteKeyForTab(moved);
    const bool removed =
        atrium_core_remove_favorite(WindowSpaceIndex(), url.c_str());
    if (atrium::BrowserController* controller =
            removed ? atrium::BrowserController::ControllerForBrowser(
                          SidebarTargetBrowser())
                    : nullptr) {
      controller->DropParkedFavorite(url);
    }
    const bool opened =
        OpenRowForUnclaimedTab(moved, SidebarTargetBrowser());
    const bool grouped = atrium_core_group_tab_by_tab_id(
        atrium::TabId(moved), args[1].GetString().c_str(), args[2].GetInt());
    ApplySidebarGesture(removed || opened || grouped);
    VLOG(1) << "Atrium: favourite joins folder - removed " << removed
            << ", grouped " << grouped;
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabGrouped(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_int() && args[1].is_string() &&
      args[2].is_string() && browser_) {
    const int section = args[2].GetString() == "pinned" ? 1 : 0;
    // Absent or non-numeric keeps the -1 "beside the last member" default.
    const int member_slot =
        (args.size() >= 4 && args[3].is_int()) ? args[3].GetInt() : -1;
    ApplySidebarGesture(atrium_core_assign_tab_to_group(
        WindowSpaceIndex(), section, args[0].GetInt(),
        args[1].GetString().c_str(), member_slot));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabUngrouped(
    const base::ListValue& args) {
  if (args.size() >= 4 && args[0].is_string() && args[1].is_int() &&
      args[2].is_string() && args[3].is_int() && browser_) {
    const int to_section = args[2].GetString() == "pinned" ? 1 : 0;
    const bool moved = atrium_core_ungroup_member(
        SpaceArg(args, 4, WindowSpaceIndex()), args[0].GetString().c_str(),
        args[1].GetInt(), to_section, args[3].GetInt());
    VLOG(1) << "Atrium: ungrouped member " << args[1].GetInt() << " of "
            << args[0].GetString() << " " << (moved ? "moved" : "refused");
    ApplySidebarGesture(moved);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabSplit(
    const base::ListValue& args) {
  if (args.size() < 3 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string() || !browser_) {
    return;
  }
  const std::string& dragged_id = args[0].GetString();
  const std::string& target_id = args[1].GetString();
  const std::string& side = args[2].GetString();
  atrium::SplitEdge edge;
  if (side == "left") {
    edge = atrium::SplitEdge::kLeft;
  } else if (side == "right") {
    edge = atrium::SplitEdge::kRight;
  } else if (side == "top") {
    edge = atrium::SplitEdge::kTop;
  } else if (side == "bottom") {
    edge = atrium::SplitEdge::kBottom;
  } else {
    LOG(WARNING) << "Atrium: sidebarTabSplit dropped - unknown side " << side;
    return;
  }
  Browser* target = SidebarTargetBrowser();
  TabStripModel* strip = target ? target->tab_strip_model() : nullptr;
  if (!strip || dragged_id == target_id) {
    return;
  }
  // Refused here as well as in InsertAtLeaf, so a drop the tree will not take
  // does not first activate and re-show the tab it was dropped on.
  if (root_view_->pane_container()->pane_count() >= atrium::kMaxSplitLeaves) {
    LOG(WARNING) << "Atrium: sidebarTabSplit dropped - the split already has "
                 << atrium::kMaxSplitLeaves << " panes";
    return;
  }
  content::WebContents* dragged = nullptr;
  content::WebContents* dropped_on = nullptr;
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    const std::string id = base::NumberToString(atrium::TabId(contents));
    if (id == dragged_id) {
      dragged = contents;
    } else if (id == target_id) {
      dropped_on = contents;
    }
  }
  if (!dragged || !dropped_on) {
    LOG(WARNING) << "Atrium: sidebarTabSplit dropped - " << dragged_id << " or "
                 << target_id << " is not a live tab in the target strip";
    return;
  }
  // The renderer resolved these ids against the rows it drew, and a compromised
  // one is free to name a tab of another space.
  if (atrium::BrowserController::TabIsOutsideActiveSpace(dragged) ||
      atrium::BrowserController::TabIsOutsideActiveSpace(dropped_on)) {
    LOG(WARNING) << "Atrium: sidebarTabSplit dropped - tab " << dragged_id
                 << " or " << target_id << " is not in the active space";
    return;
  }
  ActivateTabWithId(target_id);
  // An activation the window is not currently displaying for never reaches
  // ShowTab, and anything but kShown leaves the pane it splits against empty.
  if (ShowTab(dropped_on) != ShowTabOutcome::kShown) {
    LOG(WARNING)
        << "Atrium: sidebarTabSplit dropped - the pane would not show tab "
        << target_id;
    return;
  }
  InsertPaneFromDrop(dragged, edge);
  VLOG(1) << "Atrium: sidebar split tab " << dragged_id << " " << side
          << " of tab " << target_id;
}

void AtriumBrowserWindow::OnSurface_SidebarFolderMemberReordered(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_string() && args[1].is_int() &&
      args[2].is_int() && browser_) {
    ApplySidebarGesture(atrium_core_reorder_group_member(
        WindowSpaceIndex(), args[0].GetString().c_str(), args[1].GetInt(),
        args[2].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderPinned(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_int() &&
      browser_) {
    const bool pinned = atrium_core_pin_folder(
        WindowSpaceIndex(), args[0].GetString().c_str(), args[1].GetInt());
    VLOG(1) << "Atrium: pinned folder " << args[0].GetString() << " "
            << (pinned ? "moved" : "refused");
    ApplySidebarGesture(pinned);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderCreated(
    const base::ListValue& args) {
  if (browser_) {
    char* id = atrium_core_create_group(WindowSpaceIndex());
    const bool created = id != nullptr;
    if (id) {
      atrium_core_string_free(id);
    }
    ApplySidebarGesture(created);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderRenamed(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_string() &&
      browser_) {
    ApplySidebarGesture(atrium_core_rename_group(WindowSpaceIndex(),
                                                 args[0].GetString().c_str(),
                                                 args[1].GetString().c_str()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderDeleted(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    ApplySidebarGesture(atrium_core_delete_group(WindowSpaceIndex(),
                                                 args[0].GetString().c_str()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFolderIconSet(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_string() &&
      browser_) {
    ApplySidebarGesture(atrium_core_set_group_icon(
        WindowSpaceIndex(), args[0].GetString().c_str(),
        args[1].GetString().c_str()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteReordered(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_reorder_favorite(SpaceArg(args, 2, WindowSpaceIndex()),
                                     args[0].GetInt(), args[1].GetInt()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabMovedToSpace(
    const base::ListValue& args) {
  if (args.size() >= 4 && args[0].is_int() && args[1].is_int() &&
      args[2].is_string() && args[3].is_int() && browser_) {
    // The 5th argument is the destination slot. Absent
    // means -1, which reads as "append" - so a surface that does not send it
    // keeps the previous behaviour.
    const int to_slot =
        args.size() >= 5 && args[4].is_int() ? args[4].GetInt() : -1;
    ApplySidebarGesture(atrium_core_move_tab_to_space(
        args[0].GetInt(), args[1].GetInt(),
        args[2].GetString() == "pinned" ? 1 : 0, args[3].GetInt(), to_slot));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarRunMoved(
    const base::ListValue& args) {
  if (args.size() < 4 || !args[0].is_int() || !args[1].is_list() ||
      !args[2].is_int() || !args[3].is_int() || !browser_) {
    return;
  }
  std::optional<std::string> slots = base::WriteJson(args[1]);
  if (!slots) {
    return;
  }
  ApplySidebarGesture(atrium_core_move_tab_run(
      args[0].GetInt(), slots->c_str(), args[2].GetInt(), args[3].GetInt()));
}

void AtriumBrowserWindow::OnSurface_SidebarTabMirrored(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_string() &&
      browser_) {
    MirrorTabToProfile(args[0].GetString(), args[1].GetString());
  }
}

namespace {

content::WebContents* TabInStripWithId(Browser* browser,
                                       const std::string& tab_id) {
  TabStripModel* model = browser ? browser->tab_strip_model() : nullptr;
  for (int i = 0; model && i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && base::NumberToString(atrium::TabId(contents)) == tab_id) {
      return contents;
    }
  }
  return nullptr;
}

}  // namespace

void AtriumBrowserWindow::OnSurface_SidebarTabDragMoved(
    const base::ListValue& args) {
  if (args.size() < 3) {
    BrowserController::ClearDropPreviews();
    ClearOwnPaneDropPreviews();
    return;
  }
  if ((args[1].is_int() || args[1].is_double()) &&
      (args[2].is_int() || args[2].is_double()) && browser_) {
    const gfx::Point screen_point(static_cast<int>(args[1].GetDouble()),
                                  static_cast<int>(args[2].GetDouble()));
    content::WebContents* dragged =
        args[0].is_string()
            ? TabInStripWithId(browser_.get(), args[0].GetString())
            : nullptr;
    if (SidebarTargetBrowser() == browser_.get() && WritesAllowed() &&
        PreviewOwnWindowDrop(screen_point, dragged)) {
      return;
    }
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(browser_.get())) {
      controller->PreviewDropForPoint(screen_point);
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabDragReleased(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_string() &&
      (args[1].is_int() || args[1].is_double()) &&
      (args[2].is_int() || args[2].is_double()) && browser_) {
    const std::string tab_id = args[0].GetString();
    const gfx::Point screen_point(static_cast<int>(args[1].GetDouble()),
                                  static_cast<int>(args[2].GetDouble()));
    Browser* target = SidebarTargetBrowser();
    content::WebContents* dragged_contents = TabInStripWithId(target, tab_id);
    // Cleared before the routing runs, not after: RouteContentDrop can rebuild
    // the pane, and a highlight left up across that would belong to a view
    // that no longer exists.
    BrowserController::ClearDropPreviews();
    // The panes this window shows belong to browser_, not a swapped-in slot.
    if (dragged_contents && target == browser_.get()) {
      // A split is a document write, but a tear-off from a private window is
      // not, so only the own-window drop is refused here.
      if (!WritesAllowed()) {
        if (OwnWindowDropAt(screen_point).kind !=
            atrium::OwnWindowDropKind::kElsewhere) {
          VLOG(1) << "Atrium: own-window drop of tab " << tab_id
                  << " refused - the window is off the record";
          return;
        }
      } else if (DropTabOnOwnWindow(dragged_contents, screen_point)) {
        return;
      }
    }
    if (dragged_contents) {
      if (BrowserController* controller =
              BrowserController::ControllerForBrowser(target)) {
        controller->ClassifyAndRouteDroppedTab(dragged_contents,
                                                screen_point);
      }
    } else {
      VLOG(1) << "Atrium: sidebarTabDragReleased for tab " << tab_id
              << " - no live tab in the strip carries that id";
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarPinnedToggled(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_bool() && browser_) {
    ApplySidebarGesture(atrium_core_set_pinned_collapsed(WindowSpaceIndex(),
                                                         args[0].GetBool()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTextFocusChanged(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_bool() && browser_) {
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(browser_.get())) {
      controller->SetAtriumTextFocused(args[0].GetBool());
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarNewTab(const base::ListValue& args) {
  if (browser_) {
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(SidebarTargetBrowser())) {
      controller->OpenCommandBarOrNewTab();
    }
  }
}

void AtriumBrowserWindow::HandleAddressRowClicked(
    content::WebContents* surface,
    const base::ListValue& args) {
  // A second click while the palette is up does nothing: it is already
  // showing the row's URL, and reopening would throw away what was typed.
  if (IsCommandBarShowing() || args.size() < 4) {
    return;
  }
  for (size_t i = 0; i < 4; ++i) {
    if (!args[i].is_int()) {
      return;
    }
  }
  const gfx::Rect client(args[0].GetInt(), args[1].GetInt(),
                         args[2].GetInt(), args[3].GetInt());
  // dev_bar_sender_ is only set for the duration of this dispatch, so a
  // split bar's own pane has to be captured now for the commit to reach.
  content::WebContents* target = NavTargetWebContents();
  command_bar_target_tab_ = target ? target->GetWeakPtr() : nullptr;
  OpenCommandBar(CommandBarOpenReason::kEditAtAnchor,
                 SurfaceRectToWindow(surface, client));
}

void AtriumBrowserWindow::OnSurface_SidebarAddressRowClicked(
    const base::ListValue& args) {
  HandleAddressRowClicked(sidebar_contents(), args);
}

void AtriumBrowserWindow::OnSurface_DevBarUrlClicked(
    const base::ListValue& args) {
  HandleAddressRowClicked(DevBarSender(), args);
}

gfx::Rect AtriumBrowserWindow::SurfaceRectToWindow(
    content::WebContents* surface,
    const gfx::Rect& client_rect) const {
  content::RenderWidgetHostView* view =
      surface ? surface->GetRenderWidgetHostView() : nullptr;
  // widget_ rather than widget(): that accessor is non-const and this is a
  // read.
  if (!view || !widget_) {
    return client_rect;
  }
  // The render widget's own screen bounds rather than a view's position in
  // the layout: the sidebar moves into its own child widget when it floats,
  // and the screen origin is the one answer that survives both.
  const gfx::Rect surface_screen = view->GetViewBounds();
  const gfx::Rect window_screen = widget_->GetWindowBoundsInScreen();
  return gfx::Rect(client_rect.x() + surface_screen.x() - window_screen.x(),
                   client_rect.y() + surface_screen.y() - window_screen.y(),
                   client_rect.width(), client_rect.height());
}

void AtriumBrowserWindow::HandleHintShow(content::WebContents* surface,
                                         const base::ListValue& args) {
  if (args.size() < 7 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  for (size_t i = 2; i < 7; ++i) {
    if (!args[i].is_int()) {
      return;
    }
  }
  const gfx::Rect client(args[2].GetInt(), args[3].GetInt(),
                         args[4].GetInt(), args[5].GetInt());
  EnsureHintOverlayWidget();
  hint_overlay_widget_->Show(args[0].GetString(), args[1].GetString(),
                             SurfaceRectToWindow(surface, client),
                             args[6].GetInt());
}

void AtriumBrowserWindow::OnSurface_SidebarHintShow(
    const base::ListValue& args) {
  HandleHintShow(sidebar_contents(), args);
}

void AtriumBrowserWindow::OnSurface_DevBarHintShow(
    const base::ListValue& args) {
  HandleHintShow(DevBarSender(), args);
}

void AtriumBrowserWindow::OnSurface_SurfaceHintHide(
    const base::ListValue& args) {
  if (hint_overlay_widget_) {
    hint_overlay_widget_->Hide();
  }
}

void AtriumBrowserWindow::OnSurface_SidebarBackPressed(
    const base::ListValue& args) {
  if (content::WebContents* displayed = NavTargetWebContents()) {
    content::NavigationController& history = displayed->GetController();
    // Logged before the CanGoBack test, not after: the scenario has to be
    // able to tell "the press reached the shown tab" from "that tab had
    // nowhere to go", and a log inside the branch conflates the two.
    VLOG(1) << "Atrium: sidebar back on tab " << TabId(displayed);
    if (history.CanGoBack()) {
      history.GoBack();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarForwardPressed(
    const base::ListValue& args) {
  if (content::WebContents* displayed = NavTargetWebContents()) {
    content::NavigationController& history = displayed->GetController();
    VLOG(1) << "Atrium: sidebar forward on tab " << TabId(displayed);
    if (history.CanGoForward()) {
      history.GoForward();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarReloadPressed(
    const base::ListValue& args) {
  if (content::WebContents* displayed = NavTargetWebContents()) {
    VLOG(1) << "Atrium: sidebar reload on tab " << TabId(displayed);
    // check_for_repost, so a POSTed page asks before it is sent again -
    // Chromium's own reload button passes the same.
    displayed->GetController().Reload(content::ReloadType::NORMAL,
                                      /*check_for_repost=*/true);
  }
}

// Back with a modifier held. Every VLOG sits before the range
// test, the same way the plain handler's does - a scenario has to tell "the
// click arrived" from "that tab had nowhere to go".
void AtriumBrowserWindow::OnSurface_SidebarBackInNewTab(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  VLOG(1) << "Atrium: sidebar back in a new tab from tab " << TabId(displayed);
  content::NavigationController& history = displayed->GetController();
  // OpenSidebarColdTab holds the scheme and realised-window checks a
  // renderer-supplied URL needs. Background, like a Cmd-clicked link.
  if (content::NavigationEntry* previous = history.GetEntryAtOffset(-1)) {
    OpenSidebarColdTab(previous->GetURL().spec(), /*foreground=*/false);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarBackToStart(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  VLOG(1) << "Atrium: sidebar back to the first page of tab "
          << TabId(displayed);
  content::NavigationController& history = displayed->GetController();
  if (history.GetLastCommittedEntryIndex() > 0) {
    history.GoToIndex(0);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarForwardInNewTab(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  VLOG(1) << "Atrium: sidebar forward in a new tab from tab "
          << TabId(displayed);
  content::NavigationController& history = displayed->GetController();
  if (content::NavigationEntry* next = history.GetEntryAtOffset(1)) {
    OpenSidebarColdTab(next->GetURL().spec(), /*foreground=*/false);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarForwardToEnd(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  VLOG(1) << "Atrium: sidebar forward to the last page of tab "
          << TabId(displayed);
  content::NavigationController& history = displayed->GetController();
  const int last = history.GetEntryCount() - 1;
  if (history.GetLastCommittedEntryIndex() < last) {
    history.GoToIndex(last);
  }
}

namespace {

// The cookie and storage backends are keyed by registrable domain, so an
// origin filter would clear nothing. Falls back to the bare host, which is
// what an IP address or an intranet name canonicalises to.
std::string ClearableDomainFor(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? std::string(url.host()) : domain;
}

// The removal is asynchronous and the tab can close while it runs, so the
// reload waits on a weak handle and this deletes itself when it fires.
class ReloadAfterRemoval : public content::BrowsingDataRemover::Observer {
 public:
  ReloadAfterRemoval(content::BrowsingDataRemover* remover,
                     base::WeakPtr<content::WebContents> contents,
                     const char* removed)
      : remover_(remover), contents_(std::move(contents)), removed_(removed) {
    // RemoveWithFilterAndReply DCHECKs that its observer already registered.
    remover_->AddObserver(this);
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    VLOG(1) << "Atrium: sidebar " << removed_ << " cleared, failed types "
            << failed_data_types;
    if (content::WebContents* contents = contents_.get()) {
      contents->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                       /*check_for_repost=*/true);
    }
    remover_->RemoveObserver(this);
    delete this;
  }

 private:
  const raw_ptr<content::BrowsingDataRemover> remover_;
  const base::WeakPtr<content::WebContents> contents_;
  const char* const removed_;
};

std::u16string JsBool(bool value) {
  return value ? u"true" : u"false";
}

}  // namespace

void AtriumBrowserWindow::OnSurface_SidebarReloadHard(
    const base::ListValue& args) {
  if (content::WebContents* displayed = NavTargetWebContents()) {
    VLOG(1) << "Atrium: sidebar hard reload on tab " << TabId(displayed);
    displayed->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                      /*check_for_repost=*/true);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarReloadClearingSite(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  const std::string domain =
      ClearableDomainFor(displayed->GetLastCommittedURL());
  VLOG(1) << "Atrium: sidebar hard reload clearing site data for '" << domain
          << "' on tab " << TabId(displayed);
  if (domain.empty()) {
    // about:blank and the WebUI routes own no site data, so this is the
    // plain hard reload rather than a removal over an empty filter.
    displayed->GetController().Reload(content::ReloadType::BYPASSING_CACHE,
                                      /*check_for_repost=*/true);
    return;
  }
  content::BrowsingDataRemover* remover =
      displayed->GetBrowserContext()->GetBrowsingDataRemover();
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter->AddRegisterableDomain(domain);
  // DATA_TYPE_SITE_DATA carries DATA_TYPE_COOKIES and the DOM storage
  // backends with it. The cache is not in it - BYPASSING_CACHE covers that.
  remover->RemoveWithFilterAndReply(
      base::Time::Min(), base::Time::Max(),
      chrome_browsing_data_remover::DATA_TYPE_SITE_DATA,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
          content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
      std::move(filter),
      new ReloadAfterRemoval(remover, displayed->GetWeakPtr(), "site data"));
}

void AtriumBrowserWindow::OnSurface_SidebarReloadClearingCache(
    const base::ListValue& args) {
  content::WebContents* displayed = NavTargetWebContents();
  if (!displayed) {
    return;
  }
  VLOG(1) << "Atrium: sidebar hard reload emptying the cache on tab "
          << TabId(displayed);
  // Whole-profile and unfiltered, which is what IDC_RELOAD_CLEARING_CACHE
  // means - the HTTP cache is not keyed by site the way storage is. Site
  // data is deliberately untouched: that is the rung above this one.
  content::BrowsingDataRemover* remover =
      displayed->GetBrowserContext()->GetBrowsingDataRemover();
  remover->RemoveAndReply(
      base::Time::Min(), base::Time::Max(),
      content::BrowsingDataRemover::DATA_TYPE_CACHE,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      new ReloadAfterRemoval(remover, displayed->GetWeakPtr(), "cache"));
}

void AtriumBrowserWindow::PushNavModifiers(bool shift, bool cmd, bool option) {
  const std::u16string script =
      u"window.atriumSetNavModifiers && atriumSetNavModifiers(" +
      JsBool(shift) + u", " + JsBool(cmd) + u", " + JsBool(option) + u")";
  // the two surfaces are separate documents, and whichever holds
  // the nav group has to be armed. Both, rather than the shown one: the group
  // moves on a Cmd+Shift+D that can arrive while a modifier is already held.
  if (sidebar_contents_) {
    sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
        script, base::NullCallback());
  }
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->CallScript(script);
  }
  VLOG(1) << "Atrium: nav modifiers shift=" << shift << " cmd=" << cmd
          << " option=" << option;
}

void AtriumBrowserWindow::PushNavAbility() {
  content::WebContents* displayed = CurrentlyDisplayedWebContents();
  // The tab the buttons ACT on, which is the one the panes show rather than
  // the tab strip's active one - the same source every nav handler reads.
  const auto script_for = [](content::WebContents* tab) {
    return u"window.atriumSetNavAbility && atriumSetNavAbility({back: " +
           JsBool(tab && tab->GetController().CanGoBack()) + u", forward: " +
           JsBool(tab && tab->GetController().CanGoForward()) + u"})";
  };
  const bool back = displayed && displayed->GetController().CanGoBack();
  const bool forward = displayed && displayed->GetController().CanGoForward();
  // Both surfaces, for the reason PushNavModifiers pushes to both: the group
  // moves between them and the one holding it must carry the state. A split
  // bar greys by its own pane, the one NavTargetWebContents acts on.
  if (sidebar_contents_) {
    sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
        script_for(displayed), base::NullCallback());
  }
  for (size_t i = 0; i < dev_bars_.size(); ++i) {
    dev_bars_[i]->CallScript(script_for(DevBarTab(i)));
  }
  VLOG(1) << "Atrium: nav ability back=" << back << " forward=" << forward;
}

void AtriumBrowserWindow::OnSurface_SidebarMenuOpened(
    const base::ListValue& args) {
  sidebar_menu_open_ = true;
  VLOG(1) << "Atrium: sidebar menu open=" << sidebar_menu_open_;
}

void AtriumBrowserWindow::OnSurface_SidebarMenuClosed(
    const base::ListValue& args) {
  sidebar_menu_open_ = false;
  VLOG(1) << "Atrium: sidebar menu open=" << sidebar_menu_open_;
}

void AtriumBrowserWindow::OnSurface_SidebarToggleRequested(
    const base::ListValue& args) {
  if (browser_) {
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(browser_.get())) {
      VLOG(1) << "Atrium: sidebar toggle requested from the chrome row";
      controller->ToggleSidebar();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarOverlayWidthLive(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_double()) {
    if (args.empty() || !args[0].is_int()) {
      return;
    }
  }
  const double width =
      args[0].is_int() ? args[0].GetInt() : args[0].GetDouble();
  SetSidebarOverlayWidthLive(width);
}

void AtriumBrowserWindow::OnSurface_SidebarOverlayWidthCommit(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_double()) {
    if (args.empty() || !args[0].is_int()) {
      return;
    }
  }
  const double width =
      args[0].is_int() ? args[0].GetInt() : args[0].GetDouble();
  SetSidebarOverlayWidth(width);
}

void AtriumBrowserWindow::HandleWindowControlDismiss() {
  // Not because the panel blocks the command - it does not, the hidden
  // buttons did - but because a hover panel has no business floating over a
  // window that is minimising, zooming or closing.
  if (sidebar_overlay_widget_ && sidebar_overlay_widget_->IsShowing()) {
    sidebar_overlay_widget_->DismissForWindowCommand();
  }
}

void AtriumBrowserWindow::OnSurface_SidebarWindowClose(
    const base::ListValue& args) {
  VLOG(1) << "Atrium: sidebar window control sidebarWindowClose";
  HandleWindowControlDismiss();
  // The real red button's path. A plain widget close drains the tabs and
  // comes back as an automatic close, which the last-tab-out guard keeps
  // open with cold tabs.
  CloseUserInitiated();
}

void AtriumBrowserWindow::OnSurface_SidebarWindowMinimize(
    const base::ListValue& args) {
  VLOG(1) << "Atrium: sidebar window control sidebarWindowMinimize";
  HandleWindowControlDismiss();
  window_platform_->MinimizeWindow();
}

void AtriumBrowserWindow::OnSurface_SidebarWindowZoom(
    const base::ListValue& args) {
  VLOG(1) << "Atrium: sidebar window control sidebarWindowZoom";
  HandleWindowControlDismiss();
  // The green dot is full screen on macOS, and the menu's own command keeps
  // the fullscreen controller in step.
  chrome::ExecuteCommand(browser_.get(), IDC_FULLSCREEN);
}

void AtriumBrowserWindow::OnSurface_SidebarFolderToggled(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_bool() &&
      browser_) {
    ApplySidebarGesture(atrium_core_set_group_collapsed(
        WindowSpaceIndex(), args[0].GetString().c_str(), args[1].GetBool()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarFavoriteClicked(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string() && browser_) {
    // The tab id is tried before the URL: a favourite that redirects on load
    // commits a URL the tile does not carry, so matching on it stacked a second
    // tab every click.
    const std::string tab_id = args.size() >= 2 && args[1].is_string()
                                   ? args[1].GetString()
                                   : std::string();
    // Log the entry rather than the outcomes: the four
    // branches below log only on success, so a click where every one of them
    // missed left nothing in the log at all.
    VLOG(1) << "Atrium: favourite clicked " << args[0].GetString() << " tile tab '"
            << tab_id << "'";
    ActivateFavoriteRow(args[0].GetString(), tab_id);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceSelected(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    // the same path the Cmd+Shift+Arrow chords take, so the rail and
    // the keyboard cannot answer this differently. It records the choice AND
    // brings the window's tabs across, which is the half this handler used to
    // be missing. Resolved against the browser the rows came from, as the
    // other row gestures are.
    const int target = args[0].GetInt();

    // A switch to the edited space is the preview's own doing. Any other
    // switch leaves the studio editing a theme the window stopped showing.
    if (IsThemeStudioShowing() && ThemeStudioSpaceIndex() != target) {
      CloseThemeStudio();
    }
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(SidebarTargetBrowser())) {
      controller->SelectSpace(target);
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceScrubBegan(
    const base::ListValue& args) {
  // Same resolution as sidebarSpaceSelected, so the scrub and the switch it
  // ends in land on one controller.
  BrowserController* controller =
      BrowserController::ControllerForBrowser(SidebarTargetBrowser());
  if (controller && !args.empty() && args[0].is_int()) {
    // A scrub left on another profile's controller would stay staged.
    if (BrowserController* previous = BrowserController::SpaceScrubOwner(this);
        previous && previous != controller) {
      previous->EndSpaceScrub(/*commit=*/false);
    }
    controller->BeginSpaceScrub(args[0].GetInt());
  }
}

// The scrub's owner, not the target browser's controller: a switch onto
// another profile's space moves the target before the scrub ends.
void AtriumBrowserWindow::OnSurface_SidebarSpaceScrubBlend(
    const base::ListValue& args) {
  BrowserController* controller = BrowserController::SpaceScrubOwner(this);
  if (controller && !args.empty() && args[0].GetIfDouble()) {
    controller->SetSpaceScrubBlend(*args[0].GetIfDouble());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceScrubEnded(
    const base::ListValue& args) {
  BrowserController* controller = BrowserController::SpaceScrubOwner(this);
  if (controller && !args.empty() && args[0].is_bool()) {
    controller->EndSpaceScrub(args[0].GetBool());
  }
}

namespace {

// Whether the core still has a space with this id.
bool SpaceIdIsKnown(const std::string& space_id) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return false;
  }
  atrium_core_string_free(raw);
  return true;
}

// A null pointer and unreadable JSON both read as no theme.
base::Value ThemeSpecValue(char* raw) {
  if (!raw) {
    return base::Value();
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  atrium_core_string_free(raw);
  return parsed && parsed->is_dict() ? std::move(*parsed) : base::Value();
}

// The sidebar model carries no space ids - sidebar.rs keeps SpaceChip's off
// the wire - and the studio addresses a space by id, so a rail index is
// resolved here, once per open, on a thread allowed to block.
std::string ThemeStudioPayload(const std::string& spaces_json, int index) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  const base::ListValue* spaces =
      parsed && parsed->is_dict() ? parsed->GetDict().FindList("spaces")
                                  : nullptr;
  if (!spaces || index < 0 || static_cast<size_t>(index) >= spaces->size()) {
    return std::string();
  }
  const base::DictValue* space = (*spaces)[index].GetIfDict();
  const std::string* id = space ? space->FindString("id") : nullptr;
  if (!id || id->empty()) {
    return std::string();
  }
  const std::string* name = space->FindString("name");
  const std::string* tint = space->FindString("tint");
  // The one-stop draft off the tint is what the Spaces pane opens an unthemed
  // space on, so a first Save from either surface changes nothing visible.
  base::Value spec = ThemeSpecValue(atrium_core_space_theme(id->c_str()));
  if (spec.is_none() && tint) {
    spec = ThemeSpecValue(atrium_core_single_stop_theme(tint->c_str()));
  }
  const base::ListValue* stops =
      spec.is_dict() ? spec.GetDict().FindList("stops") : nullptr;
  const base::DictValue* first =
      stops && !stops->empty() ? (*stops)[0].GetIfDict() : nullptr;
  const std::string* accent = first ? first->FindString("color") : nullptr;

  base::DictValue payload;
  payload.Set("spaceIndex", index);
  payload.Set("spaceId", *id);
  payload.Set("spaceName", name ? *name : std::string());
  payload.Set("accent", accent ? *accent : std::string());
  payload.Set("spec", std::move(spec));
  std::string out;
  base::JSONWriter::Write(payload, &out);
  return out;
}

}  // namespace

void AtriumBrowserWindow::ApplySidebarThemeEdit(std::string_view name,
                                                const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    LOG(WARNING) << "Atrium: " << name << " from the sidebar carried no space";
    return;
  }
  const std::string space_id = args[0].GetString();
  // The gate AtriumSettingsWindow::HandleThemePreview keeps. Without it a
  // bogus id reads as a themeless space and is acted on.
  if (!SpaceIdIsKnown(space_id)) {
    VLOG(1) << "Atrium: " << name << " from the sidebar named an unknown space";
    return;
  }
  // A missing second argument reads the same as an explicit null - both mean
  // "no spec", which each branch resolves its own way.
  const base::Value* spec = args.size() > 1 ? &args[1] : nullptr;
  const bool has_spec = spec && spec->is_dict();
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  if (!controller) {
    return;
  }
  std::string theme_json;
  if (has_spec) {
    base::JSONWriter::Write(*spec, &theme_json);
  }
  if (name == "settingsThemePreview") {
    if (has_spec) {
      controller->PreviewSpaceTheme(space_id, theme_json);
    } else {
      controller->RevertThemePreview(this);
    }
    return;
  }
  if (!controller->SetSpaceTheme(space_id, has_spec ? &theme_json : nullptr)) {
    return;
  }
  // The paint waits for the write, so the window never fades in a theme
  // themes.json does not hold.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([] { return atrium_core_save() >= 0; }),
      base::BindOnce(
          [](base::WeakPtr<AtriumBrowserWindow> window, std::string space_id,
             bool wrote) {
            if (!wrote) {
              LOG(WARNING) << "Atrium: sidebar theme save failed - it is in "
                              "memory only and a relaunch loses it";
              return;
            }
            VLOG(1) << "Atrium: sidebar theme saved";
            AtriumSettingsWindow::RefreshSpaces();
            if (!window) {
              return;
            }
            if (BrowserController* controller =
                    BrowserController::ControllerForBrowser(
                        window->browser())) {
              controller->ApplySavedSpaceTheme(space_id);
            }
          },
          GetWeakPtr(), space_id));
}

void AtriumBrowserWindow::OnSurface_SettingsThemePreview(
    const base::ListValue& args) {
  ApplySidebarThemeEdit("settingsThemePreview", args);
}

void AtriumBrowserWindow::OnSurface_SettingsThemeChanged(
    const base::ListValue& args) {
  ApplySidebarThemeEdit("settingsThemeChanged", args);
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceThemeRequested(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && sidebar_contents_) {
    // The dot's rect in the sidebar document, turned into a screen rect with
    // the sidebar view's own screen bounds. A missing rect anchors at the
    // column's bottom-left corner, where the rail sits.
    const gfx::Rect column = sidebar_contents_->GetContainerBounds();
    gfx::Rect anchor(column.x() + 24, column.bottom() - 40, 14, 14);
    if (args.size() > 1 && args[1].is_dict()) {
      const base::DictValue& rect = args[1].GetDict();
      anchor = gfx::Rect(column.x() + rect.FindInt("x").value_or(0),
                         column.y() + rect.FindInt("y").value_or(0),
                         rect.FindInt("width").value_or(14),
                         rect.FindInt("height").value_or(14));
    }
    base::FilePath app_support;
    base::PathService::Get(base::DIR_APP_DATA, &app_support);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&LoadSpacesJSON,
                       SpacesFilePath(DevOnlyPathOverride("ATRIUM_SPACES_FILE"),
                                      app_support)),
        base::BindOnce(
            [](base::WeakPtr<AtriumBrowserWindow> window, int index,
               gfx::Rect anchor, std::string spaces_json) {
              const std::string payload =
                  ThemeStudioPayload(spaces_json, index);
              if (payload.empty()) {
                LOG(WARNING) << "Atrium: no space to theme at rail index "
                             << index;
                return;
              }
              if (!window) {
                return;
              }
              window->ShowThemeStudio(payload, index, anchor);
              VLOG(1) << "Atrium: theme studio popover opened for space "
                      << index;
            },
            GetWeakPtr(), args[0].GetInt(), anchor));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabDuplicated(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string()) {
    DuplicateSidebarTab(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceReordered(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_int() && args[1].is_int() && browser_) {
    ApplySidebarGesture(
        atrium_core_reorder_space(args[0].GetInt(), args[1].GetInt()),
        base::BindOnce(&AtriumSettingsWindow::RefreshSpaces));
    if (overview_controller_) {
      overview_controller_->PushSpacesList();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabRenamed(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_int() && args[1].is_int() &&
      args[2].is_string() && browser_) {
    ApplySidebarGesture(
        atrium_core_rename_tab(WindowSpaceIndex(), args[0].GetInt(),
                               args[1].GetInt(), args[2].GetString().c_str()));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceCreated(
    const base::ListValue& args) {
  if (browser_) {
    char* raw = atrium_core_create_space(
        "New Space", browser_->profile()->GetBaseName().MaybeAsASCII().c_str());
    if (raw) {
      atrium_core_string_free(raw);
      ApplySidebarGesture(true,
                          base::BindOnce(&AtriumSettingsWindow::RefreshSpaces));
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarShelfToastClicked(
    const base::ListValue& args) {
  if (browser_) {
    OpenOverviewAtSection("shelf");
  }
}

// An unknown section is safe to pass on: the Overview ignores a name it has no
// button for.
void AtriumBrowserWindow::OnSurface_SidebarOverviewSectionOpened(
    const base::ListValue& args) {
  if (browser_ && !args.empty() && args[0].is_string()) {
    OpenOverviewAtSection(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceRenamed(
    const base::ListValue& args) {
  if (browser_ && args.size() >= 2 && args[0].is_int() && args[1].is_string()) {
    ApplySidebarGesture(
        atrium_core_rename_space(args[0].GetInt(), args[1].GetString().c_str()),
        base::BindOnce(&AtriumSettingsWindow::RefreshSpaces));
    if (overview_controller_) {
      overview_controller_->PushSpacesList();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceIconSet(
    const base::ListValue& args) {
  if (browser_ && args.size() >= 2 && args[0].is_int() && args[1].is_string()) {
    ApplySidebarGesture(
        atrium_core_set_space_icon(args[0].GetInt(), args[1].GetString().c_str()),
        base::BindOnce(&AtriumSettingsWindow::RefreshSpaces));
    if (overview_controller_) {
      overview_controller_->PushSpacesList();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceIconPickerOpened(
    const base::ListValue& args) {
  if (args.size() < 6 || !args[0].is_int() || !args[1].is_string() ||
      !sidebar_contents_) {
    return;
  }
  for (size_t i = 2; i < 6; ++i) {
    if (!args[i].is_int()) {
      return;
    }
  }
  const gfx::Rect client(args[2].GetInt(), args[3].GetInt(),
                         args[4].GetInt(), args[5].GetInt());
  ShowSpaceIconPicker(args[0].GetInt(), args[1].GetString(),
                      SurfaceRectToWindow(sidebar_contents(), client));
}

void AtriumBrowserWindow::OnSurface_SidebarOverviewToggled(
    const base::ListValue& args) {
  if (browser_) {
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(SidebarTargetBrowser())) {
      controller->ToggleOverview();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarCourtyardOpened(
    const base::ListValue& args) {
  if (browser_) {
    OpenCourtyard();
  }
}

void AtriumBrowserWindow::OnSurface_OpenSettingsPane(
    const base::ListValue& args) {
  AtriumSettingsWindow::Show(args.empty() || !args[0].is_string()
                                 ? std::string()
                                 : args[0].GetString());
}

void AtriumBrowserWindow::OnSurface_NotesReady(const base::ListValue& args) {
  PushNoteToNotesContents();
}

void AtriumBrowserWindow::OnSurface_MarginaliaNoteChanged(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_string()) {
    atrium_core_set_marginal_note(args[0].GetString().c_str(),
                                  args[1].GetString().c_str());
  }
}

void AtriumBrowserWindow::OnSurface_MarginalMarkJumpRequested(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string()) {
    ScrollToMarginalMarkOnActivePage(args[0].GetString());
  }
}

void AtriumBrowserWindow::OnSurface_AiPanelReady(const base::ListValue& args) {
  // the construction catch-up - a push before this lands in
  // a document with no atriumSetAiPanel* yet and is silently lost, same
  // handshake lesson as sidebarReady/foyerReady above.
  PushAiPanelOnRight();
  PushAiPanelInk();
  PushAiPanelTarget();
  PushAiPanelEnabled();
  PushAiPanelHotkey();
  PushAiPanelAutoSummarize();
  PushAiPanelTranslateLanguage();
}

void AtriumBrowserWindow::OnSurface_AiPanelToggled(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_bool()) {
    SetAiPanelOpen(args[0].GetBool());
  }
}

void AtriumBrowserWindow::OnSurface_AiPanelWidthChanged(
    const base::ListValue& args) {
  if (args.size() >= 1 && (args[0].is_int() || args[0].is_double())) {
    SetAiPanelWidth(base::ClampRound<int>(args[0].GetDouble()));
  }
}

void AtriumBrowserWindow::OnSurface_AiPanelRowMark(
    const base::ListValue& args) {
  PushAiPanelRowMark(args.size() >= 1 && args[0].is_string()
                         ? args[0].GetString()
                         : std::string());
}

namespace {

// the pages a pinned panel has travelled, which ai-panel.js sends
// as a trailing list. Absent from an older surface, which is why a missing or
// wrong-typed argument is an empty list rather than a dropped request.
std::vector<std::string> AiPanelExtraTabIds(const base::ListValue& args,
                                            size_t index) {
  std::vector<std::string> ids;
  if (args.size() <= index || !args[index].is_list()) {
    return ids;
  }
  for (const base::Value& id : args[index].GetList()) {
    if (id.is_string() && !id.GetString().empty()) {
      ids.push_back(id.GetString());
    }
  }
  // The panel caps this, but the cap is the renderer's and a renderer is not
  // trusted to have applied it.
  if (ids.size() > kMaxAiPanelExtraPages) {
    ids.resize(kMaxAiPanelExtraPages);
  }
  return ids;
}

}  // namespace

void AtriumBrowserWindow::OnSurface_AiPanelSummarizeRequested(
    const base::ListValue& args) {
  const std::string tab_id =
      (args.size() >= 1 && args[0].is_string()) ? args[0].GetString() : "";
  const int request_id =
      (args.size() >= 2 && (args[1].is_int() || args[1].is_double()))
          ? base::ClampRound<int>(args[1].GetDouble())
          : 0;
  HandleAiPanelSummarizeRequested(tab_id, request_id, std::string(),
                                  AiPanelExtraTabIds(args, 2));
}

void AtriumBrowserWindow::OnSurface_AiPanelChatRequested(
    const base::ListValue& args) {
  if (args.size() < 3 || !args[0].is_string() ||
      !(args[1].is_int() || args[1].is_double()) || !args[2].is_string() ||
      args[2].GetString().empty()) {
    return;
  }
  HandleAiPanelSummarizeRequested(args[0].GetString(),
                                  base::ClampRound<int>(args[1].GetDouble()),
                                  args[2].GetString(),
                                  AiPanelExtraTabIds(args, 3));
}

void AtriumBrowserWindow::OnSurface_AiPanelToggleRequested(
    const base::ListValue& args) {
  ToggleAiPanel();
}

void AtriumBrowserWindow::OnSurface_ExtensionSidePanelToggleRequested(
    const base::ListValue& args) {
  ToggleExtensionPanel();
}

void AtriumBrowserWindow::OnSurface_NotesListReady(const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("notesListReady", args);
}

void AtriumBrowserWindow::OnSurface_SpacesListReady(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("spacesListReady", args);
}

void AtriumBrowserWindow::OnSurface_ShelfListReady(const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("shelfListReady", args);
}

void AtriumBrowserWindow::OnSurface_ArchivedListReady(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("archivedListReady", args);
}

void AtriumBrowserWindow::OnSurface_SnoozedListReady(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("snoozedListReady", args);
}

void AtriumBrowserWindow::OnSurface_DownloadsShowInFinder(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("downloadsShowInFinder", args);
}

void AtriumBrowserWindow::OnSurface_DownloadsRemove(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("downloadsRemove", args);
}

void AtriumBrowserWindow::OnSurface_DownloadsDeleteFromDisk(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("downloadsDeleteFromDisk", args);
}

void AtriumBrowserWindow::OnSurface_ShelfListAdd(const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("shelfListAdd", args);
}

void AtriumBrowserWindow::OnSurface_ShelfListRemove(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("shelfListRemove", args);
}

void AtriumBrowserWindow::OnSurface_ShelfListOpen(const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("shelfListOpen", args);
}

void AtriumBrowserWindow::OnSurface_NotesListCreate(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("notesListCreate", args);
}

void AtriumBrowserWindow::OnSurface_NotesListDismiss(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("notesListDismiss", args);
}

void AtriumBrowserWindow::OnSurface_NotesListDelete(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("notesListDelete", args);
}

void AtriumBrowserWindow::OnSurface_NotesListOpen(const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("notesListOpen", args);
}

void AtriumBrowserWindow::OnSurface_ArchivedListRestore(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("archivedListRestore", args);
}

void AtriumBrowserWindow::OnSurface_ArchivedListDelete(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("archivedListDelete", args);
}

void AtriumBrowserWindow::OnSurface_SnoozedListWake(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("snoozedListWake", args);
}

void AtriumBrowserWindow::OnSurface_SnoozedListDelete(
    const base::ListValue& args) {
  EnsureOverviewController();
  overview_controller_->OnSurfaceMessage("snoozedListDelete", args);
}

}  // namespace atrium
