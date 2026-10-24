// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_browser_window.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/auto_reset.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/notimplemented.h"
#include "base/numerics/safe_conversions.h"
#include "base/observer_list.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_destroyer.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_accelerator_lookup.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_mac.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_ollama.h"
#include "chrome/browser/ui/atrium/atrium_ai_route_decision.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"
#include "chrome/browser/ui/atrium/atrium_app_icon_mac.h"
#include "chrome/browser/ui/atrium/atrium_autofill_bubble_handler.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_capture.h"
#include "chrome/browser/ui/atrium/atrium_capture_math.h"
#include "chrome/browser/ui/atrium/atrium_capture_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_claim_ledger_decision.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_actions.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_widget.h"
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"
#include "chrome/browser/ui/atrium/atrium_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_courtyard_widget.h"
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_keychain_mac.h"
#include "chrome/browser/ui/atrium/atrium_devbar_layout.h"
#include "chrome/browser/ui/atrium/atrium_devtools_card_corners.h"
#include "chrome/browser/ui/atrium/atrium_extension_bridge.h"
#include "chrome/browser/ui/atrium/atrium_extension_panel_view.h"
#include "chrome/browser/ui/atrium/atrium_extension_panel_width.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_find_bar_widget.h"
#include "chrome/browser/ui/atrium/atrium_hex_color.h"
#include "chrome/browser/ui/atrium/atrium_hint_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_theme_studio_widget.h"
#include "chrome/browser/ui/atrium/atrium_toast_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
#include "chrome/browser/ui/atrium/atrium_icon_source.h"
#include "chrome/browser/ui/atrium/atrium_little_window_registry.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "chrome/browser/ui/atrium/atrium_marginalia_script.h"
#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"
#include "chrome/browser/ui/atrium/atrium_vim_script.h"
#include "chrome/browser/ui/atrium/atrium_menu_dismiss_decision.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_native_widget_mac.h"
#include "chrome/browser/ui/atrium/atrium_nav_carve_decision.h"
#include "chrome/browser/ui/atrium/atrium_overlay_theme_script.h"
#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"
#include "chrome/browser/ui/atrium/atrium_overview_controller.h"
#include "chrome/browser/ui/atrium/atrium_page_color.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/atrium/atrium_permission_sheet_widget.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/atrium/atrium_quick_menu_widget.h"
#include "chrome/browser/ui/atrium/atrium_restore_plan.h"
#include "chrome/browser/ui/atrium/atrium_search_engine_mirror.h"
#include "chrome/browser/ui/atrium/atrium_settings_json_cache.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_side_panel_ui.h"
#include "chrome/browser/ui/atrium/atrium_space_icon_picker_widget.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_bindings_script.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_edge_reveal.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_frost_layer.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_grip_bounds.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overview_width.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_seam_line.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/atrium/atrium_url_submit_decision.h"
#include "chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h"
#include "chrome/browser/ui/atrium/atrium_window_close_decision.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_context.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/bubble_anchor_util_views.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views_context.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/window_metadata/window_metadata_controller.h"
#include "chrome/browser/ui/zoom/browser_window_zoom_observer.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/search_engines/template_url_service.h"
#include "components/url_formatter/elide_url.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/page_type.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/url_util.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "third_party/blink/public/common/loader/loader_constants.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkColor.h"
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
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/compositor_extra/shadow.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/native_ui_util.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/event_monitor.h"
#include "ui/views/focus/external_focus_tracker.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_targeter.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"
#include "url/origin.h"

#if !BUILDFLAG(IS_MAC)
#error "This file is macOS-only and should not be included on other platforms."
#endif

namespace atrium {

namespace {

// radius for a shape inset `inset` from the window edge. The rule
// itself (and why it matters) lives in atrium_corner_radius.h; this just binds
// it to this window's own corner.
float ConcentricRadius(int inset) {
  return ConcentricCornerRadius(AtriumWindowCornerRadius(), inset);
}

// Whether `contents` is the page opened for the favourite at `favorite_url`.
// The key lives with the sidebar model, which reads it for the tile's
// selection.
bool IsTabForFavorite(content::WebContents* contents,
                      const std::string& favorite_url) {
  return contents && !favorite_url.empty() &&
         atrium::FavoriteKeyForTab(contents) == favorite_url;
}

// Pane-claim ledger - a PERMANENT invariant guard, not scratch
// instrumentation.
//
// A WebContents can be hosted by exactly one views::WebView at a time
// (ViewsHostableAttach overwrites views_host_ unguarded; ViewsHostableDetach
// DCHECKs it non-null). The crash surfaces LATER, in whichever holder
// detaches second, so it never names the code that created the double claim.
// This ledger records which holders currently claim each tab WebContents and
// screams at the moment an invariant breaks, not at the abort.
//
// It began as a temporary probe for a crash
// and stays as a keeper, for evidence rather than caution. One fix was
// believed to be the root cause and dropped the core-Chromium DCHECK backstop
// on the argument that the DCHECK stays loud as the tripwire. A later audit then
// found TWO more sites resolving the same presenter-vs-visible question wrongly
// - and the damage they do is SILENT: a single embed into the wrong pane
// evicts the tab the user is looking at and trips no DCHECK, no CHECK and no
// log anywhere. A tripwire that only fires on an abort cannot see that class at
// all; a ledger that speaks at CLAIM time can. Finding the third instance
// needed exactly this signal, and a temporary probe had to be built to get it.
// Removal criterion, so this is falsifiable rather than open-ended: retire it
// once every pane attach in this file routes through one resolved
// "which window displays this content" answer, so that two holders are
// unrepresentable rather than merely unobserved.
//
// the value is a LIST, not a single tag. It used to be one string,
// and DOUBLE-CLAIM overwrote it with the newcomer - so the moment the ledger
// detected the invariant break it also destroyed the evidence of who broke it.
// The 2026-07-26 abort is exactly that: the double claim was logged at
// 07:04:04.959, but the detach that finally aborted five seconds later could
// only report DETACH-UNCLAIMED, because the first holder's entry had been
// overwritten and then erased by the second holder's own detach. Keeping every
// live claim means a later detach can always name the holders it raced.
// the ledger is keyed on a RAW WebContents*, and a claimed contents
// can die without ever being detached through TraceClaim - views::WebView is
// itself a WebContentsObserver and self-clears on WebContentsDestroyed(),
// which does not call here. A window torn down while showing a tab therefore
// leaves that tab's entry behind forever. That is a slow leak on its own, but
// the sharper problem is pointer reuse: a later WebContents allocated at the
// same address inherits the dead holder list and reports a DOUBLE-CLAIM that
// never happened - a false alarm in the one log a real double claim has to be
// trusted in. One observer per claimed contents erases the entry at
// destruction.
class ClaimReaper;

struct ClaimEntry {
  std::vector<std::string> holders;
  std::unique_ptr<ClaimReaper> reaper;
};

std::map<content::WebContents*, ClaimEntry>& ClaimLedger() {
  static base::NoDestructor<std::map<content::WebContents*, ClaimEntry>> ledger;
  return *ledger;
}

class ClaimReaper : public content::WebContentsObserver {
 public:
  explicit ClaimReaper(content::WebContents* contents) { Observe(contents); }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override {
    content::WebContents* key = web_contents();
    // Stop observing BEFORE the erase below: that erase destroys the
    // ClaimEntry that owns `this`, so nothing may touch a member afterwards.
    Observe(nullptr);
    ClaimLedger().erase(key);  // deletes `this` - must be the last statement
  }
};

// Renders a ledger entry for logging: "a, b". Only ever called on the error
// paths, so the allocation cost is irrelevant.
std::string JoinHolders(const std::vector<std::string>& holders) {
  return base::JoinString(holders, ", ");
}

}  // namespace

// static
void AtriumBrowserWindow::TraceClaim(views::WebView* web_view,
                                     content::WebContents* next,
                                     const std::string& holder) {
  content::WebContents* prev = web_view ? web_view->web_contents() : nullptr;
  auto& ledger = ClaimLedger();
  if (prev && prev != next) {
    auto it = ledger.find(prev);
    if (it == ledger.end()) {
      LOG(ERROR) << "[pane-claim] DETACH-UNCLAIMED contents=" << prev
                 << " detached by " << holder
                 << " but no holder was recorded (this is the shape that "
                    "aborts in ViewsHostableDetach)";
    } else {
      // drop only THIS owner's claim. Any other owner's claim stays
      // recorded, so if a second holder is still live the next detach names it
      // instead of degrading to DETACH-UNCLAIMED.
      if (atrium::ApplyDetach(it->second.holders, holder)) {
        // owner-only comparison - see ClaimOwner's doc. A role
        // mismatch alone (e.g. pane-active vs. this window's own
        // pane-heal-detach) is the normal heal cycle, not a race.
        LOG(ERROR) << "[pane-claim] STALE-DETACH contents=" << prev
                   << " is claimed by " << JoinHolders(it->second.holders)
                   << " but " << holder << " is detaching it";
      }
      if (it->second.holders.empty()) {
        ledger.erase(it);
      }
    }
  }
  if (next && next != prev) {
    ClaimEntry& entry = ledger[next];
    if (!entry.reaper) {
      // First claim on this contents - arm the destruction hook (see
      // ClaimReaper). Armed here rather than at every claim so re-claims by the
      // same owner do not churn an observer registration.
      entry.reaper = std::make_unique<ClaimReaper>(next);
    }
    // Read before ApplyClaim mutates it - DOUBLE-CLAIM logs who held it.
    const std::vector<std::string> previous_holders = entry.holders;
    if (atrium::ApplyClaim(entry.holders, holder)) {
      LOG(ERROR) << "[pane-claim] DOUBLE-CLAIM contents=" << next
                 << " already claimed by " << JoinHolders(previous_holders)
                 << ", now claimed by " << holder
                 << " -- the earlier holder will abort when it next detaches";
    }
  }
}

std::string AtriumBrowserWindow::ClaimTag(const char* what) const {
  return base::StringPrintf("win%p/%s", this, what);
}

// static
size_t AtriumBrowserWindow::ClaimHoldersForTesting(
    content::WebContents* contents) {
  auto& ledger = ClaimLedger();
  auto it = ledger.find(contents);
  return it == ledger.end() ? 0u : it->second.holders.size();
}

namespace {

// The sidebar pane is now dynamically sized (was a
// fixed constant through M2b-M4) - SetSidebarWidth animates sidebar_slot()'s
// preferred width in response to Swift's set_sidebar_width op, and
// AtriumClientView::NonClientHitTest reads the CURRENT preferred width
// directly rather than a constant, so hit-testing always matches wherever
// the pane actually is (including mid-animation and collapsed/0). This
// constant is only the INITIAL width before the first op arrives (at
// registration - see BrowserController::SetSidebarWidthOp's call site via
// SpacesViewModel.applySidebarWidth) - must match SpacesStore.
// defaultSidebarWidth (220) so that brief window is never visibly wrong. M1's
// separate kAtriumSidebarWidth constant (content_shell, a different binary
// entirely) is intentionally untouched - that patch layer is frozen (see the
// SIDEBAR WIDTH doc block in the retired ABI header).
constexpr int kDefaultSidebarWidth = 220;
// The live-resize range, matching Swift's
// SpacesStore.sidebarWidthRange (160-500) - re-enforced here too,
// defensively (SetSidebarWidth must not trust a caller's clamp alone).
// floor lowered 180 -> 160 in lockstep with the Swift range so a
// drag below 180 is no longer re-clamped back up at runtime.
constexpr int kMinSidebarWidth = 160;
constexpr int kMaxSidebarWidth = 500;
// matches ai-panel.js's own MIN_WIDTH/MAX_WIDTH_CAP/
// MAX_WIDTH_FRACTION - the native clamp is defensive, not authoritative,
// so it must agree with the JS clampWidth() it re-checks.
constexpr int kAiPanelMinWidth = 220;
constexpr int kAiPanelMaxWidth = 400;
constexpr double kAiPanelMaxWidthFraction = 0.45;
// Collapse convention (see the retired ABI header's SIDEBAR WIDTH doc block): 0
// means collapsed. Any caller-sent width close enough to 0 to be a rounding
// artifact (rather than a genuine drag-clamped value, which is always
// >= kMinSidebarWidth) also reads as collapse - see SetSidebarWidth.
constexpr int kCollapsedSidebarWidth = 0;
constexpr double kSidebarCollapseThreshold = 0.5;
constexpr base::TimeDelta kSidebarResizeAnimationDuration =
    base::Milliseconds(150);
// Backdrop-tint crossfade length on a space switch - slightly
// longer than the sidebar slide so the color settles just after the rows.
constexpr base::TimeDelta kBackdropTintFadeDuration = base::Milliseconds(300);
// The glow after a page load arrives slowly: at 300ms the page's colours
// snapped in the moment the load stopped.
constexpr base::TimeDelta kEdgeTintLoadFadeDuration = base::Milliseconds(1000);
// ~60Hz animation tick.
constexpr base::TimeDelta kSidebarResizeAnimationTick = base::Milliseconds(16);
// A staged scrub blend already this close to full weight has
// nothing visibly left to ease - SetBackdropScrubBlendEnd promotes
// immediately instead of starting a timer for a fraction of a percent.
constexpr double kScrubBlendSettleDoneThreshold = 0.99;

// Defensive clamp for the two overlay-width ops, same posture as
// SetSidebarWidth's non-zero branch but with no collapse branch: the overlay
// has no width-0 form, so every value lands in the real range. Shared by the
// live and commit ops so their clamps cannot drift apart.
int ClampSidebarOverlayWidth(double width) {
  return std::clamp(base::ClampRound<int>(width), kMinSidebarWidth,
                    kMaxSidebarWidth);
}

// Chromeless Arc-style window (spec section 6 "Page floats rounded on the
// space-tinted backdrop"): height of the transparent titlebar band the
// traffic lights float in, over the sidebar's top-left. Must match Swift's
// SidebarMetrics.trafficLightBandHeight (the sidebar pads its content below
// this band). Same value as WebUIBrowserNativeWidgetMac's override.
using atrium::kTitlebarHeight;

// Little Atrium: height of the little window's minimal top
// chrome strip - matches PeekChromeView's own "~44px band" precedent
// (atrium_peek_widget.mm), the closest existing floating-strip chrome. The
// strip's Swift content pads its leading edge behind the native traffic
// lights itself (SidebarMetrics.trafficLightClearance, same value the
// sidebar's own content already reserves), so no separate C++-side inset is
// needed here.
constexpr int kLittleChromeStripHeight = 44;

// The web content renders as a rounded-corner card inset from the sidebar
// edge and the window edges; the tinted backdrop shows through the gap as a
// thin border around the card.
// Mutable since the appearance pane moves it, and
// ApplyWindowAppearance writes it here. Process-wide, not per-window, because
// the setting is - CurrentWindowAppearance()'s posture. UI thread only.
int g_content_card_inset = 10;

int ContentCardInset() {
  return g_content_card_inset;
}
// concentric with the window corner across the card's gutter, so
// the tinted gap stays an even width around the corner instead of pinching.
// Re-derived per call rather than cached, because the gutter now moves.
float ContentCardCornerRadius() {
  return ConcentricRadius(ContentCardInset());
}

// How far the page behind an open peek steps back. The dimmed page against
// the undimmed ring it uncovers makes even a small step read large.
constexpr float kPeekBackdropScale = 0.985f;
// Runs from the click on the same clock as the card's open spring and lands
// at its first peak (~280ms, kPeekOpenSpringStiffness in
// atrium_peek_widget.mm). Change them together.
constexpr base::TimeDelta kPeekBackdropDuration = base::Milliseconds(280);
// The card is gone after kPeekDismissDuration (170ms); the page is back a
// beat later so it does not snap while the card still fades.
constexpr base::TimeDelta kPeekBackdropReturnDuration = base::Milliseconds(200);

void ScaleLayerAboutCenter(ui::Layer* layer,
                           float scale,
                           base::TimeDelta duration,
                           gfx::Tween::Type tween) {
  if (!layer) {
    return;
  }
  const gfx::SizeF size(layer->bounds().size());
  gfx::Transform transform;
  transform.Translate(size.width() / 2, size.height() / 2);
  transform.Scale(scale, scale);
  transform.Translate(-size.width() / 2, -size.height() / 2);
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(duration);
  settings.SetTweenType(tween);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  layer->SetTransform(transform);
}

// Drop shadow the card casts onto the tinted backdrop (Arc parity). Elevation
// 4 gives ui::Shadow's Material-Design key shadow an offset of (0, 4) and a
// blur of 16 - see gfx::ShadowValue::MakeMdShadowValues - matching the spec's
// offset exactly and its ~18px blur closely. The color map override tightens
// both the key and ambient shadow alpha to ~0.28, the same shadow family
// SidebarView.swift's hover-overlay panel documents itself as following.
constexpr int kContentCardShadowElevation = 4;
constexpr SkColor kContentCardShadowColor = SkColorSetA(SK_ColorBLACK, 0x47);

// Initial window content size. Note: on machines running an AX-based tiling
// window manager (e.g. AeroSpace) the window is re-tiled right after Show()
// via the accessibility API; the size below is the pre-tiling default.
constexpr int kInitialWidth = 1200;
constexpr int kInitialHeight = 800;

// The split divider and the card gutter share one value: a widened border
// beside a divider still at 10 reads as a bug, not as two ideas.
int SplitDividerWidth() {
  return ContentCardInset();
}
// Upstream multi_contents_view.cc's kMinWebContentsWidth/
// kMinWebContentsWidthPercentage/kConstrainedMinWebContentsWidth, copied
// verbatim (plan section 2: "upstream's ClampToMinWidth 10%-of-available
// flexibility rule").
constexpr int kMinPaneExtent = 200;
constexpr int kConstrainedMinPaneExtent = 50;
constexpr double kMinPaneExtentPercentage = 0.1;
// Upstream's snap_points_ = {0.5} and kSnapDistance.
constexpr double kSplitSnapPoint = 0.5;
constexpr int kSplitSnapDistance = 15;
// Divider grab-line hover treatment (sidebar-polish's SidebarView.swift
// resizeHandle: a 2pt rounded line inset 10pt from each end, drawn only on
// hover - reused here as the native equivalent, since the divider is a
// views::View, not SwiftUI).
constexpr int kGrabLineThickness = 2;
constexpr int kGrabLineInset = 10;
constexpr SkColor kGrabLineColor = SkColorSetA(SK_ColorWHITE, 0xB3);

// Brand book crimson (docs/brand/brand-book.html), the seam's colour while
// the overlay accent is the brand one. A theme or the system accent replaces
// it through AtriumRootView::SetSeamAccent. Never the space's own tint.
constexpr SkColor kSidebarSeamLineAccent = SkColorSetRGB(0xB0, 0x3A, 0x48);
// How long the band must stay hovered before the seam line reveals, so a
// pointer that crosses the band and leaves never flashes it. A drag does not
// wait - see SidebarSeamLineOpacity.
constexpr base::TimeDelta kSidebarSeamLineRevealDelay =
    base::Milliseconds(500);
// The line eases in over this once the delay elapses rather than appearing in
// one frame. Same length and tick as the status bubble's fade below, which is
// the window's other hover-driven fade.
constexpr base::TimeDelta kSidebarSeamLineFadeDuration =
    base::Milliseconds(150);
constexpr base::TimeDelta kSidebarSeamLineFadeTick = base::Milliseconds(16);

// Debounce before the FIRST show of a hover run, so a quick mouse pass over a
// link doesn't flicker the bubble in and out. Retargeting to a NEW link while
// already visible skips this (Safari-style live update, no re-debounce).
constexpr base::TimeDelta kStatusBubbleShowDelay = base::Milliseconds(75);

}  // namespace

SplitOrientation ParseSplitOrientation(const char* orientation) {
  if (orientation && std::string_view(orientation) == "stacked") {
    return SplitOrientation::kStacked;
  }
  if (!orientation || std::string_view(orientation) != "sideBySide") {
    LOG_IF(WARNING, orientation)
        << "Atrium: unrecognized split orientation '" << orientation
        << "' - defaulting to sideBySide";
  }
  return SplitOrientation::kSideBySide;
}

const char* SplitOrientationToString(SplitOrientation orientation) {
  return orientation == SplitOrientation::kStacked ? "stacked" : "sideBySide";
}

// the drop-preview band. Heavier than the frozen wash below on
// purpose - that one marks a resting state a user may look at for a while,
// this one answers a question they are asking right now, with the pointer in
// motion.
constexpr SkColor kDropPreviewWash = SkColorSetARGB(0x66, 0x4C, 0x8B, 0xF5);

// AtriumPaneView::SnapshotOverlayView ----------------------------------------

// The frozen-snapshot pane (Arc-parity revision 2): a plain ImageView with a
// click handler, so clicking the frozen page - like clicking its sidebar row
// - takes the tab back (AtriumBrowserWindow::OnSnapshotClicked).
class AtriumPaneView::SnapshotOverlayView : public views::ImageView {
  METADATA_HEADER(SnapshotOverlayView, views::ImageView)

 public:
  void SetClickCallback(base::RepeatingClosure callback) {
    click_callback_ = std::move(callback);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (click_callback_) {
      click_callback_.Run();
    }
    return true;
  }

  // mark the frame as frozen. Before this it was pixel-identical to
  // the live page - measured at delta 0.00 across a move - so a user could not
  // tell a frozen pane from a live one, which reads as a stale window rather
  // than as a considered state. The comments elsewhere described a desaturated
  // snapshot and nothing produced one.
  //
  // A neutral wash rather than a grayscale conversion: it costs one rect
  // instead of a per-pixel pass over a full-window bitmap, and it dulls the
  // colour enough to read as inactive while leaving the page recognisable -
  // which is the point, since the frame is what tells the user WHICH page is
  // waiting for them.
  void OnPaint(gfx::Canvas* canvas) override {
    views::ImageView::OnPaint(canvas);
    canvas->FillRect(GetLocalBounds(), kFrozenWash);
  }

  // Light and cool, so it reads as "not live" rather than as "disabled". Alpha
  // is deliberately modest: the pane is still the user's own page and a heavy
  // scrim would hide what it is.
  static constexpr SkColor kFrozenWash = SkColorSetARGB(0x2E, 0xEC, 0xF0, 0xF4);

 private:
  base::RepeatingClosure click_callback_;
};

BEGIN_METADATA(AtriumPaneView, SnapshotOverlayView)
END_METADATA

// AtriumSurroundBackground ---------------------------------------------------

// The native surround: a flat fill with the page glow on top, so page tint
// reads in this mode as it does on glass and on a theme.
class AtriumSurroundBackground : public views::Background {
 public:
  AtriumSurroundBackground(SkColor color,
                           AtriumPaneContainerView::GlowPainter glow)
      : color_(color), glow_(std::move(glow)) {
    // RootBackgroundColorForTesting reads the colour back through color().
    SetColor(color);
  }
  AtriumSurroundBackground(const AtriumSurroundBackground&) = delete;
  AtriumSurroundBackground& operator=(const AtriumSurroundBackground&) = delete;
  ~AtriumSurroundBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    canvas->FillRect(view->GetLocalBounds(), color_);
    if (glow_) {
      glow_.Run(canvas, view);
    }
  }

 private:
  const SkColor color_;
  const AtriumPaneContainerView::GlowPainter glow_;
};

namespace {

// A loading page paints nothing, so the backdrop showed through the card.
// Chrome's ContentsWebView fills the gap. Only its LAYER shows here, since
// the page reaches this pane through holder()'s ui::Layer.
class PaneWebView : public views::WebView {
  METADATA_HEADER(PaneWebView, views::WebView)

 public:
  explicit PaneWebView(content::BrowserContext* context)
      : views::WebView(context) {
    SetPaintToLayer(ui::LAYER_SOLID_COLOR);
  }

  void SetWebContents(content::WebContents* contents) override {
    views::WebView::SetWebContents(contents);
    FillUntilFirstFrame();
  }

 protected:
  void OnThemeChanged() override {
    views::WebView::OnThemeChanged();
    FillUntilFirstFrame();
  }

  // A cross-process navigation builds a fresh widget, transparent again.
  // Every iframe lands here too, and none of those own the widget.
  void RenderFrameCreated(
      content::RenderFrameHost* render_frame_host) override {
    views::WebView::RenderFrameCreated(render_frame_host);
    if (!render_frame_host->GetParent()) {
      FillUntilFirstFrame();
    }
  }

 private:
  void FillUntilFirstFrame() {
    // Null until the view joins a widget, which a pane built for a split
    // does only after construction.
    const ui::ColorProvider* colors = GetColorProvider();
    if (!colors) {
      return;
    }
    // SetBackgroundColor CHECKs on a part-transparent colour.
    const SkColor fill = SkColorSetA(
        colors->GetColor(kColorWebContentsBackground), SK_AlphaOPAQUE);
    layer()->SetColor(fill);
    // The card's corners live on holder()'s layer, and this one covers it.
    if (const ui::Layer* clip = holder()->layer()) {
      layer()->SetRoundedCornerRadius(clip->rounded_corner_radii());
      layer()->SetIsFastRoundedCorner(true);
    }
    if (!web_contents()) {
      return;
    }
    if (content::RenderWidgetHostView* view =
            web_contents()->GetRenderWidgetHostView()) {
      view->SetBackgroundColor(fill);
    }
  }
};

BEGIN_METADATA(PaneWebView)
END_METADATA

}  // namespace

// AtriumPaneView::PeekScrimView ----------------------------------------------

// The dimmed backdrop behind an open peek card. A plain views::View with one
// added behavior: its own mouse press dismisses the peek, same
// SetPressCallback/OnMousePressed shape as SnapshotOverlayView above.
class AtriumPaneView::PeekScrimView : public views::View {
  METADATA_HEADER(PeekScrimView, views::View)

 public:
  void SetPressCallback(base::RepeatingClosure callback) {
    press_callback_ = std::move(callback);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (press_callback_) {
      press_callback_.Run();
    }
    return true;
  }

 private:
  base::RepeatingClosure press_callback_;
};

BEGIN_METADATA(AtriumPaneView, PeekScrimView)
END_METADATA

// AtriumPaneView --------------------------------------------------------

AtriumPaneView::AtriumPaneView(Profile* profile) {
  // empty_card_/web_view_/snapshot_view_ occupy the SAME slot and toggle
  // visibility to swap which one is shown (AtriumBrowserWindow::ShowTab/
  // ArmMoveOutFreeze). No LayoutManager: the Layout(PassKey) override
  // below handles both the no-devtools case (FillLayout-equivalent - every
  // child gets the full local bounds, zero pixel diff from the FillLayout
  // this replaced) and the docked-devtools case (ApplyDevToolsContentsResizing
  // Strategy insets the contents) uniformly, the same posture
  // AtriumPaneContainerView's own Layout(PassKey) already uses for its
  // single-pane/split cases.

  // A layer of its own so SetPeekScrimVisible can scale the whole card,
  // live page included, under an open peek.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Empty-window placeholder (Arc parity, user-requested): the bottom layer
  // of the card slot - a slightly lighter rounded panel over the tint, so a
  // tab-less window still shows "here is where the page goes" instead of one
  // undifferentiated tint. Visible whenever no tab is shown (ShowTab).
  auto empty_card = std::make_unique<views::View>();
  empty_card->SetPaintToLayer();
  empty_card->layer()->SetFillsBoundsOpaquely(false);
  empty_card->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(ContentCardCornerRadius()));
  empty_card->layer()->SetIsFastRoundedCorner(true);
  empty_card->SetBackground(
      views::CreateSolidBackground(SkColorSetA(SK_ColorWHITE, 0x1A)));
  empty_card_ = AddChildView(std::move(empty_card));

  auto web_view = std::make_unique<PaneWebView>(profile);
  web_view_ = AddChildView(std::move(web_view));
  // No WebContents attached yet (a tab-less Cmd+N window may never attach
  // one) - hidden so the root view's tinted backdrop shows through instead
  // of WebView's own default paint. ShowTab() toggles this as tabs come and
  // go.
  web_view_->SetVisible(false);

  // Painted to its own layer with the card's rounded corners so the frozen
  // snapshot clips identically to the live pane (web_view_'s corners are
  // rounded on its holder() layer - see AtriumBrowserWindow's constructor).
  auto snapshot_view = std::make_unique<SnapshotOverlayView>();
  snapshot_view_ = AddChildView(std::move(snapshot_view));
  snapshot_view_->SetPaintToLayer();
  snapshot_view_->layer()->SetFillsBoundsOpaquely(false);
  snapshot_view_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(ContentCardCornerRadius()));
  snapshot_view_->layer()->SetIsFastRoundedCorner(true);
  snapshot_view_->SetVisible(false);

  // Docked DevTools, split-view Phase C: built hidden/unattached -
  // AtriumBrowserWindow::UpdateDockedDevtools is the sole writer of its
  // WebContents/visibility/strategy. Added BELOW scrim_view_ (constructed
  // next) so the scrim keeps painting on top of everything, matching its
  // "always on top" doc.
  auto devtools_web_view = std::make_unique<views::WebView>(profile);
  devtools_web_view_ = AddChildView(std::move(devtools_web_view));
  devtools_web_view_->SetVisible(false);

  // The dimming scrim, added last so it paints on top of whichever of the
  // three views above is currently visible - see scrim_view()'s header doc.
  auto scrim = std::make_unique<PeekScrimView>();
  scrim->SetBackground(
      views::CreateSolidBackground(SkColorSetA(SK_ColorBLACK, 0x59)));
  scrim->SetVisible(false);
  scrim_view_ = AddChildView(std::move(scrim));
  // give the scrim the card's radii, exactly as snapshot_view_ gets
  // them above. Without this it was the one tile in the slot drawn to the
  // SQUARE bounding box, so its 35% black darkened the four r x r notches
  // between the card's arc and that box - reading as square corners on the
  // page behind an open peek, while the card's own arc was untouched.
  // CreateLayer propagates visible_, so a layer made after SetVisible(false)
  // starts hidden - no always-on black rectangle.
  scrim_view_->SetPaintToLayer();
  scrim_view_->layer()->SetFillsBoundsOpaquely(false);
  scrim_view_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(ContentCardCornerRadius()));
  scrim_view_->layer()->SetIsFastRoundedCorner(true);
}

void AtriumPaneView::ShowDropPreview(SplitEdge edge) {
  if (!drop_preview_view_) {
    auto preview = std::make_unique<views::View>();
    // The lab's accent wash, not a solid fill: the page underneath is what
    // tells the user which window they are about to drop into.
    preview->SetBackground(views::CreateSolidBackground(kDropPreviewWash));
    // Never in the event path. A drag in flight must not be swallowed by the
    // thing drawing where it would land - the same rule the peek scrim follows.
    preview->SetCanProcessEventsWithinSubtree(false);
    preview->SetVisible(false);
    drop_preview_view_ = AddChildView(std::move(preview));
    drop_preview_view_->SetPaintToLayer();
    drop_preview_view_->layer()->SetFillsBoundsOpaquely(false);
  }
  drop_preview_edge_ = edge;
  // kCenter is a swap, not a split. Filling the whole pane would read as a
  // split into itself, so the centre shows nothing and the drop still works.
  drop_preview_view_->SetVisible(edge != SplitEdge::kCenter);
  // Positioned NOW, not on the next layout pass. A drag produces a highlight
  // between layouts, and waiting for one drew the band at zero size - which a
  // visibility check alone would have called a pass. The scrim is laid out over
  // exactly the rect a content drop can land in, so it is the content bounds.
  LayoutDropPreview(scrim_view_->bounds());
  InvalidateLayout();
}

void AtriumPaneView::ClearDropPreview() {
  if (drop_preview_view_) {
    drop_preview_view_->SetVisible(false);
  }
}

AtriumPaneView::~AtriumPaneView() = default;

// The band an edge names, as a share of the pane. Half the 25% zone the
// classifier uses: a band as wide as the zone reads like a second pane already
// opened, which is a promise the drop has not made yet.
void AtriumPaneView::LayoutDropPreview(const gfx::Rect& bounds) {
  if (!drop_preview_view_) {
    return;
  }
  const int w = bounds.width() / 8;
  const int h = bounds.height() / 8;
  gfx::Rect band = bounds;
  switch (drop_preview_edge_) {
    case SplitEdge::kLeft:
      band.set_width(w);
      break;
    case SplitEdge::kRight:
      band.set_x(bounds.right() - w);
      band.set_width(w);
      break;
    case SplitEdge::kTop:
      band.set_height(h);
      break;
    case SplitEdge::kBottom:
      band.set_y(bounds.bottom() - h);
      band.set_height(h);
      break;
    case SplitEdge::kCenter:
      break;  // Hidden anyway - see ShowDropPreview.
  }
  drop_preview_view_->SetBoundsRect(band);
}

void AtriumPaneView::SetMirrorBadge(const std::u16string& text, SkColor color) {
  if (!mirror_badge_) {
    auto badge = std::make_unique<views::Label>();
    badge->SetPaintToLayer();
    badge->layer()->SetFillsBoundsOpaquely(false);
    badge->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(10));
    badge->layer()->SetIsFastRoundedCorner(true);
    badge->SetEnabledColor(SK_ColorWHITE);
    badge->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
    mirror_badge_ = AddChildView(std::move(badge));
  }
  mirror_badge_->SetText(text);
  mirror_badge_->SetBackground(views::CreateRoundedRectBackground(color, 10));
  mirror_badge_->SetVisible(true);
  InvalidateLayout();
}

void AtriumPaneView::ClearMirrorBadge() {
  if (mirror_badge_) {
    mirror_badge_->SetVisible(false);
  }
}

std::u16string AtriumPaneView::MirrorBadgeTextForTesting() const {
  return mirror_badge_ && mirror_badge_->GetVisible()
             ? std::u16string(mirror_badge_->GetText())
             : std::u16string();
}

void AtriumPaneView::SetDevToolsContentsResizingStrategy(
    const DevToolsContentsResizingStrategy& strategy) {
  devtools_contents_resizing_strategy_.CopyFrom(strategy);
}

void AtriumPaneView::InstallDevToolsFocusTracker() {
  devtools_focus_tracker_ = std::make_unique<views::ExternalFocusTracker>(
      devtools_web_view_, GetFocusManager());
}

void AtriumPaneView::ClearDevToolsFocusTracker() {
  devtools_focus_tracker_.reset();
}

void AtriumPaneView::RestoreDevToolsFocus() {
  if (devtools_focus_tracker_) {
    devtools_focus_tracker_->FocusLastFocusedExternalView();
  }
}

views::View* AtriumPaneView::ActiveContentView() {
  // The pane's own invariant: exactly one of these three is visible at a
  // time (ShowTab/ArmMoveOutFreeze toggle visibility, never leaving more
  // than one shown) - default to empty_card_ if somehow none are (a
  // not-yet-ShowTab'd fresh pane).
  if (web_view_->GetVisible()) {
    return web_view_;
  }
  if (snapshot_view_->GetVisible()) {
    return snapshot_view_;
  }
  return empty_card_;
}

void AtriumPaneView::UpdateDevToolsZOrder(bool hide_inspected_contents) {
  views::View* active_content = ActiveContentView();
  std::optional<size_t> devtools_index = GetIndexOf(devtools_web_view_);
  std::optional<size_t> contents_index = GetIndexOf(active_content);
  if (!devtools_index || !contents_index) {
    return;
  }
  // Mirrors DevtoolsWebViewController::UpdateDevtools's devtools_is_on_top
  // reorder (devtools_ui_controller.cc): when hide_inspected_contents()
  // wants devtools covering the contents, devtools must be on top; the
  // normal docked case wants the live pane content on top (non-overlapping
  // bounds - see Layout()).
  bool devtools_is_on_top = *devtools_index > *contents_index;
  if (hide_inspected_contents != devtools_is_on_top) {
    ReorderChildView(active_content, *devtools_index);
  }
}

float AtriumPaneView::CardCornerRadius() const {
  return immersive_ ? 0 : ContentCardCornerRadius();
}

void AtriumPaneView::SetImmersive(bool immersive) {
  if (immersive_ == immersive) {
    return;
  }
  immersive_ = immersive;
  // Layout() re-runs ApplyCardCornerRadii, which now computes against the new
  // radius; its applied_*_corners_ guard sees the change and pushes.
  InvalidateLayout();
}

void AtriumPaneView::RefreshCardCornerRadii() {
  // Not CardCornerRadius(): nothing re-pushes these three on a fullscreen
  // exit, so taking its immersive 0 would strand them square afterwards.
  const gfx::RoundedCornersF corners(ContentCardCornerRadius());
  // The same three tiles the constructor rounds. web_view_ is not among them:
  // its holder layer is rounded per corner by ApplyCardCornerRadii, which the
  // relayout below re-runs.
  for (views::View* tile :
       {static_cast<views::View*>(empty_card_),
        static_cast<views::View*>(snapshot_view_),
        static_cast<views::View*>(scrim_view_)}) {
    if (tile && tile->layer()) {
      tile->layer()->SetRoundedCornerRadius(corners);
    }
  }
  InvalidateLayout();
}

float AtriumPaneView::TileCornerRadiusForTesting() const {
  return empty_card_->layer()->rounded_corner_radii().upper_left();
}

void AtriumPaneView::ApplyCardCornerRadii(const gfx::Rect& card,
                                          const gfx::Rect& contents,
                                          const gfx::Rect& devtools) {
  const gfx::RoundedCornersF contents_corners =
      CardCornerRadii(card, contents, CardCornerRadius());
  const gfx::RoundedCornersF devtools_corners =
      CardCornerRadii(card, devtools, CardCornerRadius());
  // Radii only change when devtools docks, undocks, or moves to another edge -
  // never on a plain resize, which relayouts every frame while the user drags
  // the window. Worth the guard because NativeViewHost::SetCornerRadii builds
  // a custom mask layer underneath.
  if (contents_corners == applied_contents_corners_ &&
      devtools_corners == applied_devtools_corners_) {
    return;
  }
  applied_contents_corners_ = contents_corners;
  applied_devtools_corners_ = devtools_corners;
  empty_card_->layer()->SetRoundedCornerRadius(contents_corners);
  snapshot_view_->layer()->SetRoundedCornerRadius(contents_corners);
  // the scrim shares the contents rect in both Layout() branches,
  // so contents_corners is its correct set too - which makes the docked
  // devtools, split and immersive (radius 0) cases track for free.
  scrim_view_->layer()->SetRoundedCornerRadius(contents_corners);
  web_view_->holder()->SetCornerRadii(contents_corners);
  // The loading fill sits under holder() and has to clip the same way, and
  // this is the only place the radius changes - immersive can flip it while
  // the pane keeps its bounds, so the view never hears about it otherwise.
  web_view_->layer()->SetRoundedCornerRadius(contents_corners);
  devtools_web_view_->holder()->SetCornerRadii(devtools_corners);
}

void AtriumPaneView::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  if (mirror_badge_) {
    const gfx::Size pref = mirror_badge_->GetPreferredSize();
    mirror_badge_->SetBoundsRect(gfx::Rect(bounds.x() + 10, bounds.y() + 10,
                                           pref.width(), pref.height()));
  }
  if (!devtools_web_view_->GetVisible()) {
    // No docked devtools: exact FillLayout-equivalent - every
    // child gets the full local bounds, zero pixel diff from the
    // FillLayout this Layout() override replaced.
    empty_card_->SetBoundsRect(bounds);
    web_view_->SetBoundsRect(bounds);
    snapshot_view_->SetBoundsRect(bounds);
    scrim_view_->SetBoundsRect(bounds);
    LayoutDropPreview(bounds);
    devtools_web_view_->SetBoundsRect(gfx::Rect());
    // Undocking hands every corner back to the contents tile.
    ApplyCardCornerRadii(bounds, bounds, gfx::Rect());
    return;
  }
  gfx::Rect new_devtools_bounds;
  gfx::Rect new_contents_bounds;
  ApplyDevToolsContentsResizingStrategy(devtools_contents_resizing_strategy_,
                                        bounds, &new_devtools_bounds,
                                        &new_contents_bounds);
  devtools_web_view_->SetBoundsRect(new_devtools_bounds);
  empty_card_->SetBoundsRect(new_contents_bounds);
  web_view_->SetBoundsRect(new_contents_bounds);
  snapshot_view_->SetBoundsRect(new_contents_bounds);
  scrim_view_->SetBoundsRect(new_contents_bounds);
  LayoutDropPreview(new_contents_bounds);
  // Split card: the tiles own the silhouette, the seam between them is flat.
  ApplyCardCornerRadii(bounds, new_contents_bounds, new_devtools_bounds);
}

BEGIN_METADATA(AtriumPaneView)
END_METADATA

// AtriumPaneContainerView::AtriumSplitResizeArea -----------------------

// The divider between the two panes while split: the resize-drag surface itself, plus the sidebar-polish grab
// line hover treatment (a thin rounded line at the true edge, visible only
// on hover). Modeled on multi_contents_resize_area.{h,cc}'s
// MultiContentsResizeArea - pattern-reused, not inherited: upstream's
// views::ResizeArea base hardcodes horizontal (x-only) drag tracking
// ("An invisible area that acts like a horizontal resizer" - resize_area.h),
// which only ever needed to serve upstream's side-by-side-only split. Atrium
// needs BOTH orientations (sideBySide AND stacked, plan section 1), so this
// reimplements ResizeArea's SetInitialPosition/ReportResizeAmount idiom
// generalized on axis rather than subclassing it. Keyboard-focusable resize
// (upstream's MultiContentsResizeHandle) is deferred (plan section 8
// non-goal); double-click-to-swap is also not carried over (no "Swap panes"
// menu item in v1 - plan section 3 stretch, cut).
class AtriumPaneContainerView::AtriumSplitResizeArea : public views::View {
  METADATA_HEADER(AtriumSplitResizeArea, views::View)

 public:
  AtriumSplitResizeArea(AtriumPaneContainerView* container, size_t gutter_index)
      : container_(container), gutter_index_(gutter_index) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);

    auto grab_line = std::make_unique<views::View>();
    grab_line->SetPaintToLayer();
    grab_line->layer()->SetFillsBoundsOpaquely(false);
    grab_line->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kGrabLineThickness / 2.0f));
    grab_line->SetBackground(views::CreateSolidBackground(kGrabLineColor));
    grab_line->SetVisible(false);
    grab_line_ = AddChildView(std::move(grab_line));
  }

  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    if (!GetEnabled()) {
      return ui::Cursor();
    }
    return ui::Cursor(Axis() == Direction::kCol
                          ? ui::mojom::CursorType::kNorthSouthResize
                          : ui::mojom::CursorType::kEastWestResize);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    SetInitialPosition(AxisValue(event));
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (!event.IsLeftMouseButton()) {
      return false;
    }
    ReportResize(AxisValue(event), /*done_resizing=*/false);
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (is_resizing_) {
      ReportResize(AxisValue(event), /*done_resizing=*/true);
    }
  }

  void OnMouseCaptureLost() override {
    // ReportResize double-converts initial_position_ (already screen-space)
    // and pins the ratio at its clamp - go to OnGutterResize directly.
    is_resizing_ = false;
    container_->OnGutterResize(gutter_index_, /*resize_amount=*/0,
                               /*done_resizing=*/true);
    UpdateGrabLineVisibility();
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    UpdateGrabLineVisibility();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    UpdateGrabLineVisibility();
  }

  void SetVisible(bool visible) override {
    views::View::SetVisible(visible);
    UpdateGrabLineVisibility();
  }

  void Layout(PassKey) override {
    // The grab line sits flush with the card-facing edge of the divider
    // strip (the "true edge" - sidebar-polish's own resize-handle lesson),
    // inset kGrabLineInset from each end along the resize axis.
    const gfx::Rect bounds = GetLocalBounds();
    if (Axis() == Direction::kCol) {
      grab_line_->SetBoundsRect(
          gfx::Rect(kGrabLineInset, (bounds.height() - kGrabLineThickness) / 2,
                    std::max(0, bounds.width() - 2 * kGrabLineInset),
                    kGrabLineThickness));
    } else {
      grab_line_->SetBoundsRect(
          gfx::Rect((bounds.width() - kGrabLineThickness) / 2, kGrabLineInset,
                    kGrabLineThickness,
                    std::max(0, bounds.height() - 2 * kGrabLineInset)));
    }
  }

 private:
  // This gutter's own axis - kRow (E-W resize) unless the tree has already
  // shrunk out from under a stale index (RebuildGutterViews keeps this from
  // happening in practice).
  Direction Axis() const {
    return gutter_index_ < container_->gutters_.size()
               ? container_->gutters_[gutter_index_].axis
               : Direction::kRow;
  }

  // The mouse position along the resize axis (x for kRow, y for kCol) -
  // upstream ResizeArea's event.x(), generalized.
  int AxisValue(const ui::MouseEvent& event) const {
    return Axis() == Direction::kCol ? event.y() : event.x();
  }

  void SetInitialPosition(int axis_value) {
    gfx::Point point = AxisPoint(axis_value);
    ConvertPointToScreen(this, &point);
    initial_position_ = Axis() == Direction::kCol ? point.y() : point.x();
  }

  void ReportResize(int axis_value, bool done_resizing) {
    gfx::Point point = AxisPoint(axis_value);
    ConvertPointToScreen(this, &point);
    const int screen_value = Axis() == Direction::kCol ? point.y() : point.x();
    is_resizing_ = !done_resizing;
    container_->OnGutterResize(gutter_index_, screen_value - initial_position_,
                               done_resizing);
    if (done_resizing) {
      UpdateGrabLineVisibility();
    }
  }

  // ConvertPointToScreen needs a full gfx::Point - the unused axis is
  // irrelevant (only x is read for kRow, only y for kCol).
  gfx::Point AxisPoint(int axis_value) const {
    return Axis() == Direction::kCol ? gfx::Point(0, axis_value)
                                     : gfx::Point(axis_value, 0);
  }

  void UpdateGrabLineVisibility() {
    grab_line_->SetVisible(GetVisible() && (is_resizing_ || IsMouseHovered()));
  }

  raw_ptr<AtriumPaneContainerView> container_;
  size_t gutter_index_ = 0;
  raw_ptr<views::View> grab_line_ = nullptr;
  bool is_resizing_ = false;
  int initial_position_ = 0;
};

BEGIN_METADATA(AtriumPaneContainerView, AtriumSplitResizeArea)
END_METADATA

namespace {

// the widget is bigger than the bubble so the shadow has somewhere
// to fall. Only the bubble may take the pointer, or the transparent margin
// swallows clicks meant for the page under it.
//
// that guard never ran - AppKit hit-tests the WebView's own NSView
// before ViewTargeter, and FillLayout used to size that NSView to fill the
// padding too. Layout(PassKey) below sizes it to BubbleBounds instead.
class DevBarContainerView : public views::View,
                            public views::ViewTargeterDelegate {
  METADATA_HEADER(DevBarContainerView, views::View)

 public:
  DevBarContainerView() {
    SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
  }
  DevBarContainerView(const DevBarContainerView&) = delete;
  DevBarContainerView& operator=(const DevBarContainerView&) = delete;
  ~DevBarContainerView() override = default;

  // The bubble's rect in this view's own coordinates - devbar.css puts it at
  // exactly the same padding.
  gfx::Rect BubbleBounds() const {
    gfx::Rect bubble = GetLocalBounds();
    bubble.Inset(gfx::Insets::TLBR(kDevBarShadowPadTop, kDevBarShadowPadX,
                                   kDevBarShadowPadBottom, kDevBarShadowPadX));
    return bubble;
  }

  // views::ViewTargeterDelegate:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    return BubbleBounds().Intersects(rect);
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect bubble = BubbleBounds();
    for (views::View* child : children()) {
      child->SetBoundsRect(bubble);
    }
  }

  // No OnPaint here: a View painting in this hierarchy is
  // composited under the page. AtriumBrowserWindow::PushDevBarShadow pushes
  // BubbleBounds to AtriumOverlayShadowLayer instead.
};

BEGIN_METADATA(DevBarContainerView)
END_METADATA

}  // namespace

// AtriumPaneContainerView ------------------------------------------------

namespace {

constexpr float kActivePaneRingStroke = 2.f;

// The stroke around the selected card while split. Layer-backed so it paints
// over the live page, and it takes no events so a click still reaches the
// page under it.
class ActivePaneRingView : public views::View {
  METADATA_HEADER(ActivePaneRingView, views::View)

 public:
  ActivePaneRingView() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    SetVisible(false);
  }

  void SetAccent(SkColor accent) {
    if (accent_ == accent) {
      return;
    }
    accent_ = accent;
    SchedulePaint();
  }
  SkColor accent() const { return accent_; }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kActivePaneRingStroke);
    flags.setColor(atrium::SidebarSeamLineColor(accent_, 1.f));
    gfx::RectF rect(GetLocalBounds());
    rect.Inset(kActivePaneRingStroke / 2);
    canvas->DrawRoundRect(rect, ContentCardCornerRadius(), flags);
  }

 private:
  SkColor accent_ = kSidebarSeamLineAccent;
};

BEGIN_METADATA(ActivePaneRingView)
END_METADATA

}  // namespace

AtriumPaneContainerView::AtriumPaneContainerView(Profile* profile)
    : profile_(profile) {
  // No LayoutManager: Layout(PassKey) below handles both single-pane
  // (FillLayout-equivalent) and split (two panes + divider) geometry - a
  // plain FillLayout can't express the split case, so a custom layout replaces it
  // uniformly rather than switching layout managers at split time.
  //
  // Card inset: top/right/bottom only. The gutter between the sidebar and
  // the card is owned by the SIDEBAR (its SwiftUI trailing padding) - the
  // two are visually identical (both are bare tinted glass), but sidebar
  // ownership puts the resize handle and its grab line exactly at the card
  // boundary, which is where the eye says the sidebar ends (the definitive
  // resolution of the handle-position complaints; the earlier card-owned
  // gutter left the handle 10px short of the visual edge).
  SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
      ContentCardInset(), 0, ContentCardInset(), ContentCardInset())));

  // Casts the primary card's drop shadow onto the tinted backdrop. A
  // dedicated, paint-less view rather than attaching to the pane's
  // empty_card_/web_view_/snapshot_view_ directly: those three toggle
  // visibility to swap which one occupies the card slot (ShowTab/
  // ArmMoveOutFreeze), and a shadow attached to one of them would disappear
  // whenever that one is hidden. Layout(PassKey) below sets this view's
  // bounds to match the primary card's rect every pass (including sidebar
  // resize/collapse and split-ratio ticks), so ViewShadow's bounds-observer
  // keeps the shadow glued to the card for free.
  auto card_shadow_host = std::make_unique<views::View>();
  card_shadow_host_ = AddChildView(std::move(card_shadow_host));
  card_shadow_ = std::make_unique<views::ViewShadow>(
      card_shadow_host_.get(), kContentCardShadowElevation);
  card_shadow_->SetRoundedCornerRadius(
      static_cast<int>(ContentCardCornerRadius()));
  card_shadow_->shadow()->SetElevationToColorsMap(
      {{kContentCardShadowElevation,
        {kContentCardShadowColor, kContentCardShadowColor}}});

  auto pane = std::make_unique<AtriumPaneView>(profile);
  raw_ptr<AtriumPaneView> primary = AddChildView(std::move(pane));
  pane_tree_ =
      AtriumSplitTree<raw_ptr<AtriumPaneView>>::MakeSingleLeaf(primary);

  // above every pane (a split's included) rather than per-pane,
  // matching the lab's devbar/devwrap nesting. Hidden until SetDevBarsShown -
  // Layout(PassKey) below only reserves the strip while visible.
  AddDevBarContainer();

  active_pane_ring_ = AddChildView(std::make_unique<ActivePaneRingView>());
}

views::View* AtriumPaneContainerView::AddDevBarContainer() {
  auto container = std::make_unique<DevBarContainerView>();
  container->SetVisible(false);
  return dev_bar_containers_.emplace_back(AddChildView(std::move(container)));
}

AtriumPaneContainerView::~AtriumPaneContainerView() = default;

void AtriumPaneContainerView::SetActivePaneRingAccent(SkColor accent) {
  static_cast<ActivePaneRingView*>(active_pane_ring_)->SetAccent(accent);
}

SkColor AtriumPaneContainerView::ActivePaneRingAccent() const {
  return static_cast<ActivePaneRingView*>(active_pane_ring_)->accent();
}

void AtriumPaneContainerView::UpdateActivePaneRing() {
  if (!is_split()) {
    active_pane_ring_->SetVisible(false);
    return;
  }
  // Every pane a split adds lands after the ring, and its layer then covers
  // the stroke. Keep the ring last so it paints over all the panes.
  ReorderChildView(active_pane_ring_, children().size() - 1);
  active_pane_ring_->SetBoundsRect(active_pane()->bounds());
  active_pane_ring_->SetVisible(true);
}

ui::Layer* AtriumPaneContainerView::ShadowLayerFor(AtriumPaneView* pane) {
  if (pane == primary_pane()) {
    return card_shadow_->shadow()->layer();
  }
  if (secondary_card_shadow_ && pane == secondary_pane()) {
    return secondary_card_shadow_->shadow()->layer();
  }
  return nullptr;
}

namespace {
AtriumSplitTree<raw_ptr<AtriumPaneView>>::Direction ToTreeDirection(
    SplitOrientation orientation) {
  using Direction = AtriumSplitTree<raw_ptr<AtriumPaneView>>::Direction;
  return orientation == SplitOrientation::kStacked ? Direction::kCol
                                                   : Direction::kRow;
}

void CollectLeavesUnder(
    const AtriumSplitTree<raw_ptr<AtriumPaneView>>::Node& node,
    std::vector<raw_ptr<AtriumPaneView>>& out) {
  if (node.is_leaf()) {
    out.push_back(node.leaf());
    return;
  }
  for (const auto& kid : node.kids()) {
    CollectLeavesUnder(*kid, out);
  }
}
}  // namespace

bool AtriumPaneContainerView::EnterSplit(SplitOrientation orientation,
                                         double ratio) {
  if (is_split()) {
    return false;
  }
  const Direction dir = ToTreeDirection(orientation);
  // kRight/kBottom match MakeTwoLeafSplit's old primary-then-secondary
  // ordering exactly - target (primary) spliced first, drop (secondary)
  // second, for both kRow and kCol (see SpliceAtLeaf's edge check).
  const atrium::SplitEdge edge = dir == Direction::kRow
                                     ? atrium::SplitEdge::kRight
                                     : atrium::SplitEdge::kBottom;
  if (!InsertPaneAtLeaf(pane_tree_.LeafAt(0), edge)) {
    return false;
  }
  gutters_[0].ratio =
      ClampSplitRatio(ratio, ExtentForDirection(dir), LayoutParams());
  InvalidateLayout();
  return true;
}

AtriumPaneView* AtriumPaneContainerView::InsertPaneAtLeaf(
    AtriumPaneView* target,
    atrium::SplitEdge edge) {
  if (edge == atrium::SplitEdge::kCenter) {
    return nullptr;
  }
  if (pane_tree_.leaf_count() >= atrium::kMaxSplitLeaves) {
    return nullptr;
  }
  const bool was_unsplit = !is_split();
  AtriumPaneView* active = active_pane();
  if (was_unsplit) {
    active_pane_index_ = 0;
    auto secondary_card_shadow_host = std::make_unique<views::View>();
    secondary_card_shadow_host_ =
        AddChildView(std::move(secondary_card_shadow_host));
    secondary_card_shadow_ = std::make_unique<views::ViewShadow>(
        secondary_card_shadow_host_.get(), kContentCardShadowElevation);
    secondary_card_shadow_->SetRoundedCornerRadius(
        static_cast<int>(ContentCardCornerRadius()));
    secondary_card_shadow_->shadow()->SetElevationToColorsMap(
        {{kContentCardShadowElevation,
          {kContentCardShadowColor, kContentCardShadowColor}}});
  }

  auto new_pane_owned = std::make_unique<AtriumPaneView>(profile_);
  raw_ptr<AtriumPaneView> new_pane = AddChildView(std::move(new_pane_owned));
  auto insertion = atrium::InsertAtLeaf<raw_ptr<AtriumPaneView>>(
      pane_tree_, raw_ptr<AtriumPaneView>(target), new_pane, edge);
  if (insertion.outcome != atrium::SplitInsertOutcome::kInserted) {
    RemoveChildViewT(new_pane.ExtractAsDangling());
    if (was_unsplit) {
      secondary_card_shadow_.reset();
      RemoveChildViewT(secondary_card_shadow_host_.ExtractAsDangling());
    }
    return nullptr;
  }
  pane_tree_ = std::move(insertion.tree);
  SelectPaneByIdentity(active);

  // holder() layer only exists once the WebView is in a widget - true here
  // since this only ever runs on an already-Init()'d, already-shown window.
  new_pane->web_view()->holder()->SetCornerRadii(
      gfx::RoundedCornersF(ContentCardCornerRadius()));
  // splitting DURING immersive fullscreen would otherwise give the
  // new pane the windowed radii/shadow the rest of the card has dropped.
  new_pane->SetImmersive(immersive_);
  if (secondary_card_shadow_host_) {
    secondary_card_shadow_host_->SetVisible(!immersive_);
  }

  RebuildGutterViews();
  if (was_unsplit) {
    gutters_[0].ratio = ClampSplitRatio(
        last_split_ratio_.value_or(0.5),
        ExtentForDirection(pane_tree_.root()->dir()), LayoutParams());
  }
  InvalidateLayout();
  return new_pane.get();
}

void AtriumPaneContainerView::ExitSplit() {
  if (!is_split()) {
    return;
  }
  // Every leaf, not just the second: InsertPaneAtLeaf grows the tree to
  // kMaxSplitLeaves, and a pane dropped from the tree but left as a child
  // view keeps painting its last bounds over the collapsed window.
  std::vector<raw_ptr<AtriumPaneView>> leaves = pane_tree_.Leaves();
  if (!gutters_.empty()) {
    last_split_ratio_ = gutters_[0].ratio;
  }
  pane_tree_ =
      AtriumSplitTree<raw_ptr<AtriumPaneView>>::MakeSingleLeaf(leaves[0]);
  RebuildGutterViews();
  for (size_t i = 1; i < leaves.size(); ++i) {
    RemoveChildViewT(leaves[i].ExtractAsDangling());
  }
  secondary_card_shadow_.reset();
  RemoveChildViewT(secondary_card_shadow_host_.ExtractAsDangling());
  active_pane_index_ = 0;
  InvalidateLayout();
}

void AtriumPaneContainerView::UpdateSplitLayout(SplitOrientation orientation,
                                                double ratio) {
  if (!is_split()) {
    return;
  }
  const Direction dir = ToTreeDirection(orientation);
  if (pane_tree_.root()->dir() != dir) {
    // Transpose, rather than rebuild from leaves 0 and 1: a rebuild dropped
    // panes 3 and 4 as orphan views and left gutters_ describing a tree that
    // no longer existed, which the layout's gutter CHECK then killed.
    using PaneTree = AtriumSplitTree<raw_ptr<AtriumPaneView>>;
    struct Transpose {
      static std::unique_ptr<PaneTree::Node> Run(const PaneTree::Node& n) {
        if (n.is_leaf()) {
          return PaneTree::Node::MakeLeaf(n.leaf());
        }
        std::vector<std::unique_ptr<PaneTree::Node>> kids;
        for (const auto& kid : n.kids()) {
          kids.push_back(Run(*kid));
        }
        const Direction flipped =
            n.dir() == Direction::kRow ? Direction::kCol : Direction::kRow;
        return PaneTree::Node::MakeBranch(flipped, std::move(kids));
      }
    };
    pane_tree_ = PaneTree(Transpose::Run(*pane_tree_.root()));
  }
  gutters_[0].ratio =
      ClampSplitRatio(ratio, ExtentForDirection(dir), LayoutParams());
  InvalidateLayout();
}

void AtriumPaneContainerView::SetActivePaneIndex(int index) {
  const int count = static_cast<int>(pane_tree_.leaf_count());
  active_pane_index_ = (index > 0 && index < count) ? index : 0;
  UpdateActivePaneRing();
}

bool AtriumPaneContainerView::RemovePane(AtriumPaneView* pane) {
  // Two leaves keep the ExitSplit path: it also tears down the secondary
  // card shadow, and its caller re-homes the survivor's page onto leaf 0.
  if (pane_tree_.leaf_count() <= 2) {
    return false;
  }
  // Null when the removed pane was the active one - RemoveChildViewT below
  // destroys it, so this must never become a pointer to compare after that.
  AtriumPaneView* active = active_pane() == pane ? nullptr : active_pane();
  if (!pane_tree_.RemoveLeaf(raw_ptr<AtriumPaneView>(pane))) {
    return false;
  }
  RemoveChildViewT(pane);
  RebuildGutterViews();
  SelectPaneByIdentity(active);
  InvalidateLayout();
  return true;
}

void AtriumPaneContainerView::SelectPaneByIdentity(AtriumPaneView* pane) {
  int index = 0;
  for (size_t i = 0; i < pane_tree_.leaf_count(); ++i) {
    if (pane && pane_tree_.LeafAt(i) == pane) {
      index = static_cast<int>(i);
      break;
    }
  }
  SetActivePaneIndex(index);
}

void AtriumPaneContainerView::SetCollapsedContentInsets(bool collapsed) {
  collapsed_ = collapsed;
  ApplyContentInsets();
}

void AtriumPaneContainerView::SetImmersive(bool immersive) {
  if (immersive_ == immersive) {
    return;
  }
  immersive_ = immersive;
  ApplyContentInsets();
  // Every leaf, not only the first two: a split can grow to
  // four panes, and each keeps windowed radii otherwise.
  for (size_t i = 0; i < pane_count(); ++i) {
    PaneAt(i)->SetImmersive(immersive);
  }
  // The drop shadow exists to lift the card off the tinted backdrop; edge to
  // edge there is no backdrop left to lift it off, and the blur would darken
  // the screen border.
  card_shadow_host_->SetVisible(!immersive);
  if (secondary_card_shadow_host_) {
    secondary_card_shadow_host_->SetVisible(!immersive);
  }
}

void AtriumPaneContainerView::SetDevBarsShown(size_t count) {
  bool changed = false;
  for (size_t i = 0; i < dev_bar_containers_.size(); ++i) {
    const bool shown = i < count;
    if (dev_bar_containers_[i]->GetVisible() != shown) {
      dev_bar_containers_[i]->SetVisible(shown);
      changed = true;
    }
  }
  if (changed) {
    InvalidateLayout();
  }
}

void AtriumPaneContainerView::ApplyContentInsets() {
  // Edge to edge: no gutter, no reserved traffic-light band (the lights are
  // hidden by the fullscreen transition itself).
  if (immersive_) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets()));
    InvalidateLayout();
    return;
  }
  int top = ContentCardInset();
  int left = ContentCardInset();
  int right = ContentCardInset();
  // The 0-inset side (when not collapsed) is the gutter adjacent to the
  // docked sidebar - it hugs whichever edge the sidebar is currently on.
  if (!collapsed_) {
    if (sidebar_on_right_) {
      right = 0;
    } else {
      left = 0;
    }
  }
  // The open AI panel owns the gutter on its own side, the edge opposite the
  // sidebar. Its grip is page-drawn and clips at its container, so a gutter
  // held here is one the mark cannot reach.
  if (ai_panel_open_ || extension_panel_open_) {
    if (sidebar_on_right_) {
      left = 0;
    } else {
      right = 0;
    }
  }
  // Sidebar on right: right-docked, the native traffic lights stay
  // at the window's top-left - now OVER the content card, not the sidebar -
  // so this card must reserve the same titlebar band the left-docked
  // sidebar reserves, keeping the lights off live web content. Left-docked
  // geometry is untouched (top stays the card inset - the lights sit
  // over the sidebar there, not this card).
  if (sidebar_on_right_) {
    top = static_cast<int>(kTitlebarHeight);
  }
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(top, left, ContentCardInset(), right)));
  InvalidateLayout();
}

void AtriumPaneContainerView::OnContentCardInsetChanged() {
  ApplyContentInsets();
  // Deliberately not skipped while immersive. The shadows are only hidden,
  // and SetImmersive(false) re-shows them without re-deriving a radius, so a
  // width changed during fullscreen would come back at the old corner.
  const int radius = static_cast<int>(ContentCardCornerRadius());
  card_shadow_->SetRoundedCornerRadius(radius);
  if (secondary_card_shadow_) {
    secondary_card_shadow_->SetRoundedCornerRadius(radius);
  }
  for (size_t i = 0; i < pane_count(); ++i) {
    PaneAt(i)->RefreshCardCornerRadii();
  }
}

void AtriumPaneContainerView::SetSidebarOnRight(bool on_right) {
  if (sidebar_on_right_ == on_right) {
    return;
  }
  sidebar_on_right_ = on_right;
  InvalidateLayout();
}

void AtriumPaneContainerView::SetAiPanelOpen(bool open) {
  if (ai_panel_open_ == open) {
    return;
  }
  ai_panel_open_ = open;
  ApplyContentInsets();
}

void AtriumPaneContainerView::SetExtensionPanelOpen(bool open) {
  if (extension_panel_open_ == open) {
    return;
  }
  extension_panel_open_ = open;
  ApplyContentInsets();
}

void AtriumPaneContainerView::SetOpaqueSurround(bool enabled, SkColor color) {
  // No layer promotion: a non-layer-backed background paints into this view's
  // paint layer, beneath the native NSViews the panes host (the mechanism
  // the peek scrim uses) - under the card and over the glass,
  // which is exactly where the surround belongs. Nothing else ever sets this
  // view's background, so the fill needs no re-application after a split.
  if (enabled) {
    SetBackground(std::make_unique<AtriumSurroundBackground>(
        color, surround_glow_painter_));
  } else {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

void AtriumPaneContainerView::SetSurroundGlowPainter(GlowPainter painter) {
  surround_glow_painter_ = std::move(painter);
}

SplitOrientation AtriumPaneContainerView::orientation() const {
  if (!is_split()) {
    return SplitOrientation::kSideBySide;
  }
  return pane_tree_.root()->dir() == Direction::kCol
             ? SplitOrientation::kStacked
             : SplitOrientation::kSideBySide;
}

double AtriumPaneContainerView::ratio() const {
  return gutters_.empty() ? 0.5 : gutters_[0].ratio;
}

int AtriumPaneContainerView::ExtentForDirection(Direction dir) const {
  const gfx::Rect content = GetLocalBounds();
  const gfx::Insets insets = GetInsets();
  const int width = std::max(0, content.width() - insets.width());
  const int height = std::max(0, content.height() - insets.height());
  return dir == Direction::kCol ? height : width;
}

int AtriumPaneContainerView::SplitExtent() const {
  return ExtentForDirection(is_split() ? pane_tree_.root()->dir()
                                       : Direction::kRow);
}

SplitLayoutParams AtriumPaneContainerView::LayoutParams() const {
  return SplitLayoutParams{
      .divider_extent = SplitDividerWidth(),
      .min_pane_extent = kMinPaneExtent,
      .constrained_min_pane_extent = kConstrainedMinPaneExtent,
      .min_pane_extent_percentage = kMinPaneExtentPercentage,
  };
}

double AtriumPaneContainerView::SnapRatio(double ratio, int extent) const {
  const int divider = SplitDividerWidth();
  const int available = std::max(0, extent - divider);
  if (available <= 0) {
    return ratio;
  }
  if (std::abs(kSplitSnapPoint * available - ratio * available) <
      kSplitSnapDistance) {
    return kSplitSnapPoint;
  }
  return ratio;
}

void AtriumPaneContainerView::RebuildGutterViews() {
  const size_t leaves = pane_tree_.leaf_count();
  const size_t needed = leaves > 0 ? leaves - 1 : 0;
  while (gutters_.size() < needed) {
    const size_t index = gutters_.size();
    GutterState gutter;
    gutter.view =
        AddChildView(std::make_unique<AtriumSplitResizeArea>(this, index));
    gutters_.push_back(std::move(gutter));
  }
  while (gutters_.size() > needed) {
    RemoveChildViewT(gutters_.back().view.ExtractAsDangling());
    gutters_.pop_back();
  }
}

views::View* AtriumPaneContainerView::gutter_view_for_testing(size_t index) {
  return index < gutters_.size() ? gutters_[index].view.get() : nullptr;
}

std::vector<raw_ptr<AtriumPaneView>>
AtriumPaneContainerView::LeavesUnderBranch(const SplitNodePath& path) const {
  using Node = AtriumSplitTree<raw_ptr<AtriumPaneView>>::Node;
  const Node* node = pane_tree_.root();
  for (size_t index : path) {
    node = node->kids()[index].get();
  }
  std::vector<raw_ptr<AtriumPaneView>> leaves;
  CollectLeavesUnder(*node, leaves);
  return leaves;
}

std::vector<gfx::Rect> AtriumPaneContainerView::GetClientRegions(
    const gfx::Rect& available_bounds) const {
  // `available_bounds` names the coordinate space the returned rects are
  // expressed in (this container's own bounds() - AtriumClientView::
  // NonClientHitTest passes its own root-view-local bounds(), and this
  // container's origin lands in that same space, placed by AtriumRootView's
  // BoxLayout). This container's own position within it (origin(), below)
  // plus each child's own laid-out LOCAL bounds gives the answer directly -
  // matches Layout(PassKey) by construction, unlike A1's now-retired
  // GetActivePaneCardBounds (which recomputed the rect from scratch instead
  // of reading live child bounds).
  const gfx::Vector2d to_root = origin().OffsetFromOrigin();
  std::vector<gfx::Rect> regions;
  const size_t leaves = pane_tree_.leaf_count();
  for (size_t i = 0; i < leaves; ++i) {
    regions.push_back(pane_tree_.LeafAt(i)->bounds() + to_root);
  }
  for (const auto& gutter : gutters_) {
    if (gutter.view) {
      regions.push_back(gutter.view->bounds() + to_root);
    }
  }
  // the devbar hosts a real WebContents (url text, copy button) -
  // without this it would hit-test as HTCAPTION and swallow its own clicks.
  for (views::View* bar : dev_bar_containers_) {
    if (bar->GetVisible()) {
      regions.push_back(bar->bounds() + to_root);
    }
  }
  return regions;
}

double AtriumPaneContainerView::OnGutterResize(size_t gutter_index,
                                               int resize_amount,
                                               bool done_resizing) {
  if (gutter_index >= gutters_.size()) {
    return 0.5;
  }
  GutterState& gutter = gutters_[gutter_index];
  const int extent = gutter.total_extent;
  const int divider = SplitDividerWidth();
  const int available = std::max(0, extent - divider);
  if (!gutter.resize_initial_extent.has_value()) {
    gutter.resize_initial_extent = gutter.ratio * available;
  }
  const double new_primary_extent =
      gutter.resize_initial_extent.value() + resize_amount;
  double new_ratio =
      available > 0 ? new_primary_extent / available : gutter.ratio;
  new_ratio = SnapRatio(new_ratio, extent);
  gutter.ratio = ClampSplitRatio(new_ratio, extent, LayoutParams());
  if (done_resizing) {
    gutter.resize_initial_extent.reset();
  }
  InvalidateLayout();
  // A2 review fix (finding 2): drag-end only, root gutter only - see
  // SetDividerCommittedCallback's doc.
  if (done_resizing && gutter_index == 0 && divider_committed_callback_) {
    divider_committed_callback_.Run();
  }
  return gutter.ratio;
}

void AtriumPaneContainerView::SetDividerCommittedCallback(
    base::RepeatingClosure callback) {
  divider_committed_callback_ = std::move(callback);
}

void AtriumPaneContainerView::Layout(PassKey) {
  gfx::Rect content = GetLocalBounds();
  content.Inset(GetInsets());

  if (!is_split()) {
    primary_pane()->SetBoundsRect(content);
    card_shadow_host_->SetBoundsRect(content);
    LayoutDevBars(content);
    UpdateActivePaneRing();
    return;
  }

  // One entry per ratio_for call below, so write-back can persist what this
  // pass actually rendered instead of leaving a stale ratio for next time.
  std::vector<double> resolved_ratios;
  const std::function<double(const SplitNodePath&)> ratio_for =
      [this, &resolved_ratios](const SplitNodePath& path) {
        // gutters_[0] is always the root gutter (RebuildGutterViews only
        // grows/shrinks the vector from the back), so its ratio survives any
        // insert - unlike a nested branch, matched by leaves below.
        if (path.empty()) {
          const double ratio = gutters_.empty() ? 0.5 : gutters_[0].ratio;
          resolved_ratios.push_back(ratio);
          return ratio;
        }
        const std::vector<raw_ptr<AtriumPaneView>> leaves =
            LeavesUnderBranch(path);
        for (const GutterState& gutter : gutters_) {
          if (gutter.leaves == leaves) {
            resolved_ratios.push_back(gutter.ratio);
            return gutter.ratio;
          }
        }
        // A branch built since the last pass - starts neutral.
        resolved_ratios.push_back(0.5);
        return 0.5;
      };
  const auto result = LayoutSplitTree<raw_ptr<AtriumPaneView>>(
      pane_tree_, content, LayoutParams(), ratio_for);

  for (const auto& leaf : result.leaves) {
    leaf.leaf->SetBoundsRect(leaf.rect);
  }
  CHECK(!result.leaves.empty());
  card_shadow_host_->SetBoundsRect(result.leaves[0].rect);
  if (result.leaves.size() > 1) {
    secondary_card_shadow_host_->SetBoundsRect(result.leaves[1].rect);
  }

  CHECK_EQ(gutters_.size(), result.gutters.size());
  CHECK_EQ(resolved_ratios.size(), result.gutters.size());
  for (size_t i = 0; i < gutters_.size(); ++i) {
    gutters_[i].leaves = LeavesUnderBranch(result.gutters[i].path);
    gutters_[i].ratio = resolved_ratios[i];
    gutters_[i].axis = result.gutters[i].axis;
    gutters_[i].total_extent = result.gutters[i].total_extent;
    gutters_[i].view->SetBoundsRect(result.gutters[i].rect);
  }
  LayoutDevBars(content);
  UpdateActivePaneRing();
}

void AtriumPaneContainerView::LayoutDevBars(const gfx::Rect& content) {
  // A split adds its panes after the bars, and a pane's page then covers
  // them. Keep every bar after the last pane.
  if (is_split()) {
    size_t last_pane = 0;
    for (size_t i = 0; i < pane_count(); ++i) {
      last_pane = std::max(last_pane, GetIndexOf(PaneAt(i)).value_or(0));
    }
    for (views::View* bar : dev_bar_containers_) {
      if (GetIndexOf(bar).value_or(0) < last_pane) {
        ReorderChildView(bar, children().size() - 1);
      }
    }
  }
  // the bar floats over the card as a centred bubble, so the panes
  // keep the full content rect. while split, bar i floats over
  // PaneAt(i) instead.
  for (size_t i = 0; i < dev_bar_containers_.size(); ++i) {
    views::View* bar = dev_bar_containers_[i];
    if (!bar->GetVisible()) {
      bar->SetBoundsRect(gfx::Rect());
      continue;
    }
    const gfx::Rect over = !is_split()          ? content
                           : i < pane_count()   ? PaneAt(i)->bounds()
                                                : gfx::Rect();
    bar->SetBoundsRect(DevBarBoundsInRect(over));
  }
}

BEGIN_METADATA(AtriumPaneContainerView)
END_METADATA

// AtriumRootView ------------------------------------------------------------

// the sidebar's width grip, as a VIEW rather than a page element.
//
// A page-side handle was built first and never received a press. It is provably
// correct - a synthetic dispatch over it sends the right messages - and
// provably hit-testable, and the window's caption carve was ruled out, yet six
// VM runs saw no press reach the surface. Views owns mouse handling for its own
// children, so a grip here needs nothing delivered to a renderer at all.
//
// It also belongs here on the merits: the width is the native side's property.
// The page was reporting a number for C++ to apply, which is a round trip to
// ask the layout to change itself.
class AtriumSidebarGrip : public views::View {
  METADATA_HEADER(AtriumSidebarGrip, views::View)

 public:
  // `on_live` runs per drag step and `on_commit` once on release - the split
  // the host's two entry points exist for: resizing per step without animating,
  // then animating to the settled width and remembering it.
  AtriumSidebarGrip(base::RepeatingCallback<void(int)> on_live,
                    base::RepeatingCallback<void(int)> on_commit,
                    base::RepeatingCallback<int()> current_width,
                    base::RepeatingCallback<bool()> sidebar_on_right,
                    int min_width,
                    int max_width,
                    int reset_width)
      : on_live_(std::move(on_live)),
        on_commit_(std::move(on_commit)),
        current_width_(std::move(current_width)),
        sidebar_on_right_(std::move(sidebar_on_right)),
        min_width_(min_width),
        max_width_(max_width),
        reset_width_(reset_width) {
    SetPreferredSize(gfx::Size(kSidebarGripWidth, 1));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!event.IsOnlyLeftMouseButton()) {
      return false;
    }
    // A double-click returns the default width, the affordance the lab's own
    // handle carries. Views reports it as a second press with the flag set
    // rather than as its own event.
    if (event.flags() & ui::EF_IS_DOUBLE_CLICK) {
      // The release of this press still arrives below. Without the flag it
      // would commit WidthFor() off the first click's press_width_ and take
      // the sidebar straight back to where it was.
      reset_pending_ = true;
      on_commit_.Run(reset_width_);
      return true;
    }
    press_x_ = event.root_location().x();
    press_width_ = current_width_.Run();
    SetDragging(true);
    return true;  // Claiming the press is what makes the drags arrive.
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    // press_width_/press_x_ still hold the pre-reset press, so ignore drags
    // until the release clears reset_pending_.
    if (reset_pending_) {
      return true;
    }
    on_live_.Run(WidthFor(event));
    return true;
  }

  // A lost capture ends the drag with no release, and the commit is what
  // clears the page's drag deadline.
  void OnMouseCaptureLost() override {
    if (!dragging_) {
      return;
    }
    SetDragging(false);
    reset_pending_ = false;
    on_commit_.Run(current_width_.Run());
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    SetDragging(false);
    // A drag that ends away from the seam gets no exit event of its own, and
    // the line would stay lit under a pointer that has left.
    const bool on_band = GetLocalBounds().Contains(event.location());
    SetHovered(on_band);
    // The drag has already earned the reveal, and OutOfBandWatcher disarmed it
    // the moment the pointer crossed the 6px band. Serving the delay again here
    // would blink the line out under a pointer that never left the seam.
    if (on_band) {
      RevealSeamLine();
    }
    if (reset_pending_) {
      reset_pending_ = false;
      return;
    }
    on_commit_.Run(WidthFor(event));
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    SetHovered(true);
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    SetHovered(false);
  }

  ui::Cursor GetCursor(const ui::MouseEvent& event) override {
    return ui::mojom::CursorType::kColumnResize;
  }

  // the line the deleted page-side strip drew on hover, moved here
  // with its owner. Only the seam column can show it - the rest of the band
  // lies over the sidebar's own web layer, which composites above Views paint.
  void OnPaint(gfx::Canvas* canvas) override {
    const float opacity = SidebarSeamLineOpacity(reveal_t_, dragging_);
    if (opacity <= 0.f) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(atrium::SidebarSeamLineColor(accent_, opacity));
    canvas->DrawRoundRect(
        SidebarSeamLineBounds(GetLocalBounds(), sidebar_on_right_.Run()),
        kSidebarSeamLineRadius, flags);
  }

  void SetAccent(SkColor accent) {
    if (accent_ == accent) {
      return;
    }
    accent_ = accent;
    SchedulePaint();
  }

  SkColor accent() const { return accent_; }

 private:
  int WidthFor(const ui::MouseEvent& event) const {
    return std::clamp(
        SidebarWidthForDrag(press_width_, event.root_location().x() - press_x_,
                            sidebar_on_right_.Run()),
        min_width_, max_width_);
  }

  void SetHovered(bool hovered) {
    if (hovered_ != hovered) {
      hovered_ = hovered;
      if (hovered_) {
        reveal_delay_timer_.Start(
            FROM_HERE, kSidebarSeamLineRevealDelay,
            base::BindOnce(&AtriumSidebarGrip::RevealSeamLine,
                           base::Unretained(this)));
      } else {
        // Cancels an in-flight delay, so a pass over the band shorter than
        // kSidebarSeamLineRevealDelay never reveals the line at all.
        reveal_delay_timer_.Stop();
        reveal_fade_timer_.Stop();
        if (reveal_t_ > 0.f) {
          reveal_t_ = 0.f;
          SchedulePaint();
        }
      }
    }
    // Tied to hover, not to the individual call sites, so the watch's
    // lifetime can't drift out of sync with what it's watching for.
    if (hovered_) {
      StartOutOfBandWatch();
    } else {
      StopOutOfBandWatch();
    }
  }

  // Stops the delay as well, so a caller that reveals early does not leave a
  // pending fire behind to re-arm a line the pointer has since left.
  void RevealSeamLine() {
    reveal_delay_timer_.Stop();
    if (reveal_t_ >= 1.f || reveal_fade_timer_.IsRunning()) {
      return;
    }
    reveal_fade_start_ = base::TimeTicks::Now();
    reveal_fade_timer_.Start(
        FROM_HERE, kSidebarSeamLineFadeTick,
        base::BindRepeating(&AtriumSidebarGrip::StepRevealFade,
                            base::Unretained(this)));
  }

  void StepRevealFade() {
    reveal_t_ = std::clamp<float>((base::TimeTicks::Now() - reveal_fade_start_) /
                                      kSidebarSeamLineFadeDuration,
                                  0.f, 1.f);
    if (reveal_t_ >= 1.f) {
      reveal_fade_timer_.Stop();
    }
    SchedulePaint();
  }

  // A drag leaves the band almost immediately, and the exit that follows must
  // not take the line with it while the pointer is still holding the seam.
  void SetDragging(bool dragging) {
    if (dragging_ != dragging) {
      dragging_ = dragging;
      SchedulePaint();
    }
  }

  // Watches raw window-wide moves, since a WebContents past the band's edge
  // is its own NSView and invisible to Views' hit-testing. A separate class
  // from the grip: views::View's own OnEvent(ui::Event*) would hide this one.
  class OutOfBandWatcher : public ui::EventObserver {
   public:
    explicit OutOfBandWatcher(AtriumSidebarGrip* grip)
        : grip_(grip),
          monitor_(views::EventMonitor::CreateWindowMonitor(
              this, grip->GetWidget()->GetNativeWindow(),
              {ui::EventType::kMouseMoved, ui::EventType::kMouseDragged})) {}

    // SetHovered(false) may reset monitor_, destroying this from inside its
    // own dispatch. Safe only because EventMonitorMac's block holds a
    // WeakPtr and touches nothing on the object after invoking OnEvent.
    void OnEvent(const ui::Event& event) override {
      if (!grip_->GetBoundsInScreen().Contains(
              monitor_->GetLastMouseLocation())) {
        grip_->SetHovered(false);
      }
    }

   private:
    raw_ptr<AtriumSidebarGrip> grip_;
    std::unique_ptr<views::EventMonitor> monitor_;
  };

  void StartOutOfBandWatch() {
    if (out_of_band_watch_ || !GetWidget()) {
      return;
    }
    out_of_band_watch_ = std::make_unique<OutOfBandWatcher>(this);
  }

  void StopOutOfBandWatch() { out_of_band_watch_.reset(); }

  base::RepeatingCallback<void(int)> on_live_;
  base::RepeatingCallback<void(int)> on_commit_;
  base::RepeatingCallback<int()> current_width_;
  base::RepeatingCallback<bool()> sidebar_on_right_;
  const int min_width_;
  const int max_width_;
  const int reset_width_;
  int press_x_ = 0;
  int press_width_ = kDefaultSidebarWidth;
  bool reset_pending_ = false;
  bool hovered_ = false;
  SkColor accent_ = kSidebarSeamLineAccent;
  bool dragging_ = false;
  // The reveal fade's progress, 0 to 1. Rises only once hovered_ has held true
  // for kSidebarSeamLineRevealDelay. Dragging paints the line without waiting
  // on this - see SidebarSeamLineOpacity.
  float reveal_t_ = 0.f;
  base::TimeTicks reveal_fade_start_;
  base::OneShotTimer reveal_delay_timer_;
  base::RepeatingTimer reveal_fade_timer_;
  std::unique_ptr<OutOfBandWatcher> out_of_band_watch_;
};

BEGIN_METADATA(AtriumSidebarGrip)
END_METADATA

void AtriumRootView::SetSeamAccent(SkColor accent) {
  if (sidebar_grip_) {
    static_cast<AtriumSidebarGrip*>(sidebar_grip_)->SetAccent(accent);
  }
  // The extension panel's grip is the same control on the other edge.
  if (extension_panel_ && extension_panel_->grip()) {
    static_cast<AtriumSidebarGrip*>(extension_panel_->grip())
        ->SetAccent(accent);
  }
  if (pane_container_) {
    pane_container_->SetActivePaneRingAccent(accent);
  }
}

void AtriumRootView::OnGripDragged(int width) {
  // through the window, not the slot. A resize animation may be in
  // flight - the hover reveal starts a collapse from this band's outer half -
  // and only SetSidebarWidthLive stops its timer.
  on_live_resize_.Run(width);
}

void AtriumRootView::OnGripReleased(int width) {
  OnGripDragged(width);
  // Through the window, not straight to the store. The window is what holds
  // the width a reveal opens the panel at, and a drag that only wrote the
  // store left that at its pre-drag value - so resizing the pane and then
  // hiding it produced a panel at the old width. Written on release
  // rather than per tick, so a gesture writes once.
  on_commit_resize_.Run(width);
}

void AtriumRootView::SetSidebarCollapsed(bool collapsed) {
  sidebar_collapsed_ = collapsed;
  UpdateSidebarGripVisibility();
}

void AtriumRootView::UpdateSidebarGripVisibility() {
  if (!sidebar_grip_) {
    return;
  }
  // no seam while the pane is collapsed. The band would otherwise
  // stand at the window's left edge with nothing beside it to resize, and
  // during a reveal it reads as a handle floating outside the panel. The
  // panel carries its own handle in the page, which is where the lab puts it
  // too (.ovresize, swipe-lab.html:42).
  sidebar_grip_->SetVisible(!immersive_ && !sidebar_collapsed_);
}

void AtriumRootView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  // Only while visible: BoxLayout re-assigns the column from scratch on every
  // pass, which is what keeps the reach below idempotent. It skips a hidden
  // child instead, and reaching over a stale rect would compound each pass.
  if (sidebar_grip_ && sidebar_grip_->GetVisible()) {
    // A panel is up: the seam the pointer sees is its trailing edge, not the
    // collapsed pane's zero-width column. The band still lives in this window
    // - the panel stops at that edge, so nothing covers it.
    sidebar_grip_->SetBoundsRect(
        SidebarGripBounds(sidebar_grip_->bounds(), sidebar_on_right_));
    // A native band is invisible from a page and from a screenshot, so without
    // this line "the cursor is continuous across the seam" cannot be checked
    // from outside at all - see resizer-input-probe.mjs, which reads it.
    VLOG(1) << "Atrium: sidebar grip band "
            << sidebar_grip_->bounds().ToString();
  }
}

views::View* AtriumRootView::TargetForRect(views::View* root,
                                           const gfx::Rect& rect) {
  // The band overlaps the sidebar slot, and the slot is the later child while
  // the sidebar docks right - Views would hand it the press, and its WebView
  // would swallow the whole gesture. See SetEventTargeter's note.
  // GetVisible() as well: a hidden grip keeps the bounds it had, and immersive
  // fullscreen would otherwise leave a band that still swallows the pointer.
  if (root == this && sidebar_grip_ && sidebar_grip_->GetVisible() &&
      sidebar_grip_->bounds().Contains(rect.CenterPoint())) {
    return sidebar_grip_;
  }
  return views::ViewTargeterDelegate::TargetForRect(root, rect);
}

int AtriumRootView::CurrentSidebarWidth() const {
  return sidebar_web_container_ ? sidebar_web_container_->GetPreferredSize().width()
                                : kDefaultSidebarWidth;
}

int AtriumRootView::CurrentExtensionPanelWidth() const {
  return extension_panel_->GetPreferredSize().width();
}

AtriumRootView::AtriumRootView(
    Profile* profile,
    base::RepeatingCallback<void(double)> on_live_resize,
    base::RepeatingCallback<void(double)> on_commit_resize,
    bool little)
    : on_live_resize_(std::move(on_live_resize)),
      on_commit_resize_(std::move(on_commit_resize)) {
  // A little window stacks [top chrome strip, pane
  // container] vertically instead of [sidebar, pane container] horizontally
  // - no sidebar slot is built at all.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      little ? views::BoxLayout::Orientation::kVertical
             : views::BoxLayout::Orientation::kHorizontal));

  if (little) {
    // the foyer's slot. A plain container holding a WebView added
    // later, once the reserved chrome context exists - the same shape the HTML
    // sidebar's slot below uses, and for the same reason.
    auto container = std::make_unique<views::View>();
    container->SetLayoutManager(std::make_unique<views::FillLayout>());
    // BoxLayout takes the fixed strip height from the preferred size; the
    // width stretches to fill the cross axis (the full window width).
    container->SetPreferredSize(gfx::Size(1, kLittleChromeStripHeight));
    little_chrome_container_ = AddChildView(std::move(container));
  } else {
    // the sidebar's slot. A plain container holding a WebView added
    // later, once the reserved chrome context exists.
    auto container = std::make_unique<views::View>();
    container->SetLayoutManager(std::make_unique<views::FillLayout>());
    container->SetPreferredSize(gfx::Size(kDefaultSidebarWidth, 1));
    sidebar_web_container_ = AddChildView(std::move(container));
  }

  // the width grip sits BETWEEN the sidebar and the pane, which
  // is where the seam is. Not built for a little window - it has no sidebar to
  // resize - so this is inside the same branch the sidebar slot is.
  if (!little) {
    sidebar_grip_ = AddChildView(std::make_unique<AtriumSidebarGrip>(
        base::BindRepeating(&AtriumRootView::OnGripDragged,
                            base::Unretained(this)),
        base::BindRepeating(&AtriumRootView::OnGripReleased,
                            base::Unretained(this)),
        base::BindRepeating(&AtriumRootView::CurrentSidebarWidth,
                            base::Unretained(this)),
        base::BindRepeating(&AtriumRootView::sidebar_on_right,
                            base::Unretained(this)),
        kMinSidebarWidth, kMaxSidebarWidth, kDefaultSidebarWidth));
  }

  // the grab band reaches back over the sidebar, and which of the two
  // is the later child - the one Views hit-tests first - flips with the dock
  // side. TargetForRect names the owner so child order cannot decide it.
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));

  auto pane_container = std::make_unique<AtriumPaneContainerView>(profile);
  pane_container_ = AddChildView(std::move(pane_container));
  layout->SetFlexForView(pane_container_, 1);

  // Added right after pane_container_ - starts on the edge opposite the
  // sidebar slot above. SetSidebarOnRight reorders both together.
  auto ai_panel_container = std::make_unique<views::View>();
  ai_panel_container->SetLayoutManager(std::make_unique<views::FillLayout>());
  ai_panel_container->SetPreferredSize(gfx::Size(0, 1));
  ai_panel_container_ = AddChildView(std::move(ai_panel_container));
  extension_panel_ =
      AddChildView(std::make_unique<AtriumExtensionPanelView>());
  extension_card_shadow_ = std::make_unique<views::ViewShadow>(
      extension_panel_->card(), kContentCardShadowElevation);
  extension_card_shadow_->shadow()->SetElevationToColorsMap(
      {{kContentCardShadowElevation,
        {kContentCardShadowColor, kContentCardShadowColor}}});
  ApplyExtensionCardShape();

  // Sane preferred size for any delegate-derived sizing path
  // (WidgetDelegate::GetDesiredWidgetBounds uses the contents view's
  // preferred size).
  SetPreferredSize(gfx::Size(kInitialWidth, kInitialHeight));
}

AtriumRootView::~AtriumRootView() = default;

void AtriumRootView::SetCollapsedContentInsets(bool collapsed) {
  pane_container_->SetCollapsedContentInsets(collapsed);
}

void AtriumRootView::SetImmersive(bool immersive) {
  // the seam goes with the sidebar. Left visible, the grip keeps a
  // resize band down the left edge of a fullscreen video - it was 6px wide and
  // is now the full grab area, so it stops being easy to miss.
  immersive_ = immersive;
  UpdateSidebarSlotVisibility();
  UpdateSidebarGripVisibility();
  // A little window has no slot, so this strip is the whole of its chrome -
  // hide it too, or video fullscreen is edge to edge only on a full window.
  if (little_chrome_container_) {
    little_chrome_container_->SetVisible(!immersive);
  }
  pane_container_->SetImmersive(immersive);
  ApplyExtensionCardShape();
  InvalidateLayout();
}

bool AtriumRootView::ExtensionCardShadowVisibleForTesting() const {
  return extension_card_shadow_->shadow()->layer()->visible();
}

void AtriumRootView::ApplyExtensionCardShape() {
  const float radius = ContentCardCornerRadius();
  extension_panel_->SetImmersive(immersive_);
  extension_panel_->SetCardShape(immersive_ ? 0 : ContentCardInset(),
                                 immersive_ ? 0 : radius);
  // Kept at the windowed radius while hidden, like the page card's shadow, so
  // leaving fullscreen needs no re-derive.
  extension_card_shadow_->SetRoundedCornerRadius(static_cast<int>(radius));
  extension_card_shadow_->shadow()->layer()->SetVisible(
      !immersive_ && extension_panel_->card()->GetVisible());
}

void AtriumRootView::OnContentCardInsetChanged() {
  ApplyExtensionCardShape();
  if (!pane_container_) {
    return;
  }
  pane_container_->OnContentCardInsetChanged();
  InvalidateLayout();
}

void AtriumRootView::SetCourtyardOpen(bool open) {
  courtyard_open_ = open;
  UpdateSidebarSlotVisibility();
}

void AtriumRootView::UpdateSidebarSlotVisibility() {
  views::View* slot = sidebar_slot();
  if (!slot) {
    return;
  }
  // An invisible child drops out of BoxLayout entirely, so the card takes the
  // full window width rather than leaving a gap where the sidebar was.
  slot->SetVisible(!immersive_ && !courtyard_open_);
}

void AtriumRootView::SetSidebarOnRight(bool on_right) {
  views::View* slot = sidebar_slot();
  // A little window has no sidebar to dock, and no side to propagate: its
  // pane container would otherwise reserve a gutter beside a column that does
  // not exist.
  if (!slot) {
    return;
  }
  if (sidebar_on_right_ == on_right) {
    return;
  }
  sidebar_on_right_ = on_right;
  // BoxLayout lays out children in child order left-to-right - the sidebar
  // must be the first child to dock left, the last child to dock right.
  ReorderChildView(slot, on_right ? children().size() - 1 : size_t{0});
  // the grip IS the seam, so it follows the slot to the other end.
  // Left where it was, a right-docked window grew a resize column against the
  // pane's far edge and the real seam had no handle on it at all.
  if (sidebar_grip_) {
    ReorderChildView(sidebar_grip_,
                     on_right ? children().size() - 2 : size_t{1});
  }
  // the ai-panel slot mirrors to whichever end the sidebar just
  // vacated, so it stays on the edge opposite it.
  if (ai_panel_container_) {
    ReorderChildView(ai_panel_container_,
                     on_right ? size_t{0} : children().size() - 1);
  }
  if (extension_panel_) {
    ReorderChildView(extension_panel_,
                     on_right ? size_t{0} : children().size() - 1);
    extension_panel_->SetPanelOnRight(!on_right);
  }
  pane_container_->SetSidebarOnRight(on_right);
  InvalidateLayout();
}

std::vector<gfx::Rect> AtriumRootView::GetClientRegions(
    const gfx::Rect& available_bounds) const {
  std::vector<gfx::Rect> regions =
      pane_container_->GetClientRegions(available_bounds);
  // The AI panel is this view's own child, not one of pane_container_'s
  // leaves - without this it hit-tests as HTCAPTION and every press drags
  // the window. Width 0 means collapsed/closed.
  if (ai_panel_container_ && ai_panel_container_->width() > 0) {
    regions.push_back(ai_panel_container_->bounds());
  }
  // Same reason as the AI panel above: a child of this view, not a pane leaf.
  if (extension_panel_ && extension_panel_->width() > 0) {
    regions.push_back(extension_panel_->bounds());
  }
  return regions;
}

// AtriumBrowserWindow::StubLocationBar ---------------------------------------

// A minimal LocationBar so BrowserWindow's GetLocationBar() contract (never
// null) holds. Atrium has no omnibox in M2a; every method mirrors
// WebUIStubLocationBar's stub bodies.
class AtriumBrowserWindow::StubLocationBar : public LocationBar {
 public:
  explicit StubLocationBar(Browser* browser)
      : LocationBar(browser->command_controller()) {}
  ~StubLocationBar() override = default;

  void FocusLocation(bool is_user_initiated,
                     bool clear_focus_if_failed) override {}
  void FocusSearch() override {}
  void UpdateFocusBehavior(bool toolbar_visible) override {}
  void UpdateContentSettingsIcons() override {}
  void SaveStateToContents(content::WebContents* contents) override {}
  void Revert() override {}
  OmniboxView* GetOmniboxView() override {
    return nullptr;
  }
  OmniboxPopupView* GetOmniboxPopupView() override {
    return nullptr;
  }
  OmniboxController* GetOmniboxController() override {
    return nullptr;
  }
  bool ShouldCloseOmniboxPopup(ui::MouseEvent* event) override {
    return false;
  }
  ChipController* GetChipController() override {
    return nullptr;
  }
  content::WebContents* GetWebContents() override {
    return nullptr;
  }
  LocationBarModel* GetLocationBarModel() override {
    return nullptr;
  }
  std::optional<bubble_anchor_util::AnchorConfiguration> GetChipAnchor()
      override {
    return std::nullopt;
  }
  ui::TrackedElement* GetAnchorOrNull() override {
    return nullptr;
  }
  Browser* GetBrowser() override {
    return nullptr;
  }
  Profile* GetProfile() override {
    return nullptr;
  }
  void OnChanged() override {}
  void UpdateWithoutTabRestore() override {}
  bool IsInitialized() const override {
    return true;
  }
  bool IsVisible() const override {
    return true;
  }
  bool IsDrawn() const override {
    // This stub has no real views::LocationBarView, so GetChipController()
    // can never return a usable object (see below). The permission-prompt
    // code treats IsDrawn() as the signal for whether the chip UI is
    // available and null-checks accordingly, so this must report false -
    // reporting true here is what let a crash through.
    return false;
  }
  bool IsFullscreen() const override {
    return false;
  }
  bool IsEditingOrEmpty() const override {
    return false;
  }
  bool IsMouseHovered() const override {
    return false;
  }
  void InvalidateLayout() override {}
  gfx::Rect Bounds() const override {
    return gfx::Rect();
  }
  gfx::Rect BoundsInScreen() const override {
    return gfx::Rect();
  }
  gfx::Size MinimumSize() const override {
    return gfx::Size();
  }
  gfx::Size PreferredSize() const override {
    return gfx::Size();
  }
  void Update(content::WebContents* contents) override {}
  void ResetTabState(content::WebContents* contents) override {}
  bool HasSecurityStateChanged() override {
    return false;
  }
  LocationBarTesting* GetLocationBarForTesting() override {
    return nullptr;
  }
};

// AtriumBrowserWindow::ModalDialogHost ---------------------------------------

// Minimal WebContentsModalDialogHost, replicating
// WebUIBrowserModalDialogHost's positioning logic against this window's
// widget instead of a WebUI-tracked content region.
class AtriumBrowserWindow::ModalDialogHost
    : public web_modal::WebContentsModalDialogHost {
 public:
  explicit ModalDialogHost(AtriumBrowserWindow* window) : window_(window) {}
  ~ModalDialogHost() override {
    observers_.Notify(&web_modal::ModalDialogHostObserver::OnHostDestroying);
  }

  gfx::NativeView GetHostView() const override {
    return gfx::GetViewForWindow(EffectiveWindow()->GetNativeWindow());
  }

  gfx::Point GetDialogPosition(const gfx::Size& size) override {
    // Access to `ActivePane()` (private on AtriumBrowserWindow) is legal
    // here: ModalDialogHost is a nested class of AtriumBrowserWindow. Routed
    // through the active-pane accessor - see its doc - rather than a raw
    // web_view_ member, so dialogs anchor to whichever pane is
    // authoritative (the single pane outside a split, or the
    // active-pane tracking while split).
    AtriumBrowserWindow* const host = EffectiveWindow();
    gfx::Rect contents_bounds =
        host->ActivePane()->web_view()->GetBoundsInScreen();
    gfx::Rect window_bounds = host->GetBounds();
    contents_bounds -= window_bounds.OffsetFromOrigin();
    return gfx::Point(
        contents_bounds.x() + (contents_bounds.width() - size.width()) / 2,
        contents_bounds.y() - 10);
  }

  bool ShouldActivateDialog() const override {
    return GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser() ==
           EffectiveWindow()->browser();
  }

  gfx::Size GetMaximumDialogSize() override {
    return EffectiveWindow()
        ->ActivePane()
        ->web_view()
        ->GetBoundsInScreen()
        .size();
  }

  void AddObserver(web_modal::ModalDialogHostObserver* observer) override {
    observers_.AddObserver(observer);
  }
  void RemoveObserver(web_modal::ModalDialogHostObserver* observer) override {
    observers_.RemoveObserver(observer);
  }

 private:
  // `window_` is this tab's OWN Browser's window, but during a
  // cross-profile kInWindowSwap (BrowserController::ActivateWindow) that
  // window can be hidden while `window_`'s WebContents renders inside a
  // different profile's presenter window - see PresentedWindowForBrowser's
  // doc. Anchoring a tab-modal dialog's position/host-view against `window_`
  // in that state would compute geometry for an off-screen window and hand
  // out a NativeView the user cannot see. Returns the slot's current
  // presenter window when `window_` is swapped out, else `window_` itself
  // (the overwhelmingly common case, and byte-identical to the old
  // unconditional `window_` use).
  AtriumBrowserWindow* EffectiveWindow() const {
    AtriumBrowserWindow* presenter =
        BrowserController::PresentedWindowForBrowser(window_->browser());
    return presenter ? presenter : window_.get();
  }

  base::ObserverList<web_modal::ModalDialogHostObserver> observers_;
  const raw_ptr<AtriumBrowserWindow> window_;
};

// AtriumBrowserWindow::ExclusiveAccessContextImpl -----------------------------

// ExclusiveAccessManager dereferences its context on ordinary paths
// (Browser::ContentsMouseEvent -> OnUserInput on every mouse event over tab
// contents; Browser::AddNewContents popup handling ->
// FullscreenController::IsFullscreenForBrowser; DevTools CDP), so a real
// object is required. Unlike BrowserView's equivalent (a member of the view,
// browser_view.cc's BrowserView::ExclusiveAccessContextImpl, which this now
// follows closely) this is an owned member of AtriumBrowserWindow.
//
// This also serves as the ExclusiveAccessBubbleViewsContext,
// so page-initiated fullscreen shows Chromium's real notice - "<origin> is now
// full screen - press esc to exit". Leaving that stubbed while element
// fullscreen works is a fullscreen-phishing hole: a page that owns every pixel
// can paint fake browser chrome, and Atrium has no omnibox left as a trust
// anchor because it is chromeless and this feature hides the sidebar. The
// bubble is a separate browser-owned widget at ui::ZOrderLevel::
// kSecuritySurface (kCGScreenSaverWindowLevel - 1 on macOS, see
// CGWindowLevelForZOrderLevel in native_widget_mac.mm), which is the point:
// page content cannot paint over it. Which entries warrant a notice is
// Chromium's decision, not ours - GetExclusiveAccessExitBubbleType returns
// TYPE_NONE for a plain green-button fullscreen and a tab-fullscreen type for
// the page-initiated one, so the green button stays quiet for free.
class AtriumBrowserWindow::ExclusiveAccessContextImpl
    : public ExclusiveAccessContext,
      public ExclusiveAccessBubbleViewsContext,
      public ui::AcceleratorProvider {
 public:
  explicit ExclusiveAccessContextImpl(AtriumBrowserWindow* window)
      : window_(window) {}
  ExclusiveAccessContextImpl(const ExclusiveAccessContextImpl&) = delete;
  ExclusiveAccessContextImpl& operator=(const ExclusiveAccessContextImpl&) =
      delete;
  ~ExclusiveAccessContextImpl() override = default;

  // ExclusiveAccessContext:
  Profile* GetProfile() override { return window_->browser_->profile(); }

  bool IsFullscreen() const override {
    return window_->widget_->IsFullscreen();
  }

  // this is the path a video player's own fullscreen button takes
  // (ExclusiveAccessManager -> here), as opposed to macOS window fullscreen
  // via the green button. It used to be a no-op, so YouTube's request arrived
  // and was dropped - the button appeared dead.
  void EnterFullscreen(const url::Origin& origin,
                       ExclusiveAccessBubbleType bubble_type,
                       FullscreenTabParams fullscreen_tab_params) override {
    window_->SetImmersiveFullscreen(true);
  }

  void ExitFullscreen() override { window_->SetImmersiveFullscreen(false); }

  void UpdateExclusiveAccessBubble(
      const ExclusiveAccessBubbleParams& params,
      ExclusiveAccessBubbleHideCallback first_hide_callback) override {
    // TYPE_NONE means there is no exclusive-access state left to announce, so
    // tear the notice down - except when it accompanies a download, where the
    // previous type is kept and only the download line is added.
    if (params.type == EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE &&
        !params.has_download) {
      if (first_hide_callback) {
        std::move(first_hide_callback)
            .Run(ExclusiveAccessBubbleHideReason::kNotShown);
      }
      if (!bubble_ || bubble_destruction_task_id_) {
        return;
      }
      // Runs the current bubble's callback with kInterrupted, if any.
      bubble_->HideImmediately();
      // Destroy asynchronously: state updates inside the bubble can re-enter
      // this method, and freeing it inline from there is a use-after-free
      // (crbug.com/40063714 - same reason BrowserView posts this).
      bubble_destruction_task_id_ = bubble_task_tracker_.PostTask(
          base::SingleThreadTaskRunner::GetCurrentDefault().get(), FROM_HERE,
          base::BindOnce(
              &ExclusiveAccessContextImpl::DestroyAnyExclusiveAccessBubble,
              weak_ptr_factory_.GetWeakPtr()));
      return;
    }

    if (bubble_) {
      if (bubble_destruction_task_id_) {
        // A destruction task is pending but the bubble is wanted again.
        bubble_task_tracker_.TryCancel(bubble_destruction_task_id_.value());
        bubble_destruction_task_id_.reset();
      }
      bubble_->Update(params, std::move(first_hide_callback));
      return;
    }

    bubble_ = std::make_unique<ExclusiveAccessBubbleViews>(
        this, params, std::move(first_hide_callback));
  }

  bool IsExclusiveAccessBubbleDisplayed() const override {
    return bubble_ && (bubble_->IsShowing() || bubble_->IsVisible());
  }

  // Re-shows the notice after its snooze window, so a page cannot simply wait
  // out the first four seconds and then own the screen unannounced.
  void OnExclusiveAccessUserInput() override {
    if (bubble_) {
      bubble_->OnUserInput();
    }
  }

  content::WebContents* GetWebContentsForExclusiveAccess() override {
    return window_->browser_->tab_strip_model()->GetActiveWebContents();
  }

  bool CanUserEnterFullscreen() const override { return true; }

  bool CanUserExitFullscreen() const override { return true; }

  // ExclusiveAccessBubbleViewsContext:
  ExclusiveAccessManager* GetExclusiveAccessManager() override {
    return window_->browser_->GetFeatures().exclusive_access_manager();
  }

  ui::AcceleratorProvider* GetAcceleratorProvider() override { return this; }

  gfx::NativeView GetBubbleParentView() const override {
    return window_->widget_->GetNativeView();
  }

  gfx::Rect GetClientAreaBoundsInScreen() const override {
    return window_->widget_->GetClientAreaBoundsInScreen();
  }

  // ChromeOS's auto-hiding-toolbar mode, which suppresses the notice because
  // the real chrome is one gesture away. Unrelated to Atrium's own immersive
  // fullscreen, which hides the sidebar outright - so never true here.
  bool IsImmersiveModeEnabled() const override { return false; }

  // Only read on ChromeOS (ExclusiveAccessBubbleViews::GetPopupRect); a
  // chromeless window has no top container, so report the window itself.
  gfx::Rect GetTopContainerBoundsInScreen() override {
    return window_->widget_->GetClientAreaBoundsInScreen();
  }

  void DestroyAnyExclusiveAccessBubble() override {
    bubble_.reset();
    bubble_destruction_task_id_.reset();
  }

  // the macOS fullscreen transition resizes the window AFTER the
  // show-state change that creates the notice, so its first placement is
  // computed against the pre-fullscreen bounds. Re-anchor as the window
  // settles (AtriumBrowserWindow::OnWidgetBoundsChanged). No-op when nothing
  // is on screen.
  void RepositionExclusiveAccessBubble() {
    if (bubble_) {
      bubble_->RepositionIfVisible();
    }
  }

  // ui::AcceleratorProvider: the bubble asks for IDC_FULLSCREEN to render
  // "press Ctrl+Cmd+F to exit" for browser-initiated fullscreen (the
  // page-initiated notice says "esc" and never consults this). Atrium has no
  // toolbar to route through as BrowserView does, so read the Mac keymap
  // directly. Must answer for IDC_FULLSCREEN - the bubble DCHECKs on a miss.
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override {
    return atrium::AcceleratorForCommand(command_id, accelerator);
  }

 private:
  const raw_ptr<AtriumBrowserWindow> window_;
  // The fullscreen notice, alive only while one is on screen. Torn down on
  // fullscreen exit (AtriumBrowserWindow::OnWidgetShowStateChanged) and in
  // ~AtriumBrowserWindow, both ahead of widget_.reset() - it is a child widget
  // parented into widget_ and must not outlive it.
  std::unique_ptr<ExclusiveAccessBubbleViews> bubble_;
  base::CancelableTaskTracker bubble_task_tracker_;
  std::optional<base::CancelableTaskTracker::TaskId>
      bubble_destruction_task_id_;
  base::WeakPtrFactory<ExclusiveAccessContextImpl> weak_ptr_factory_{this};
};

// AtriumBrowserWindow ---------------------------------------------------------

namespace {

// Modeled on WebUIBrowserNativeWidgetMac (webui_browser_window_mac.mm),
// which is itself derived from BrowserNativeWidgetMac: registers chrome's
// CommandDispatcher on the NSWindow so browser shortcuts (Cmd+T/W/L, ...)
// validate against and execute on `browser_`, winning over the renderer's
// key handling for reserved commands regardless of focus. Adopts the
// reference's chromeless full-size-content styling (M2c Arc-style window:
// no titlebar, traffic lights floating over the SwiftUI sidebar, which pads
// its content below them - SidebarMetrics.contentTopPadding). One deliberate
// divergence from the reference remains:
// - No app-shim branch in OnWindowInitialized: Atrium browsers are always
//   in-process (no remote-cocoa PWA hosting), so only the in-process bridge
//   path is kept.

// AtriumClientView ------------------------------------------------------------

// With no titlebar the window needs explicit drag regions
// (WebUIBrowserClientView precedent: ClientView::NonClientHitTest returning
// HTCAPTION makes BridgedContentView's hitTest return nil for the point, so
// the mouse-down falls through to the frame view, which turns it into a
// window drag - see cr_mouseDownOnFrameView). Draggable: the traffic-light
// band over the sidebar and the tinted backdrop margins around the content
// card. NOT draggable: the card itself (web clicks/selection) and the
// sidebar below the band (SwiftUI rows/buttons keep their clicks).
class AtriumClientView : public views::ClientView {
  METADATA_HEADER(AtriumClientView, views::ClientView)

 public:
  AtriumClientView(views::Widget* widget,
                   views::View* contents_view,
                   AtriumRootView* root_view,
                   AtriumBrowserWindow* window)
      : ClientView(widget, contents_view),
        root_view_(root_view),
        window_(window) {}

  // The AppKit close routes - the red traffic light and File > Close Window's
  // performClose: item - arrive HERE (windowShouldClose:), never through
  // Widget::CloseWithReason, so this is where they become user-initiated.
  views::CloseRequestResult OnWindowCloseRequested() override {
    return window_->OnAppKitCloseRequested();
  }

  int NonClientHitTest(const gfx::Point& point) override {
    // Sidebar width is dynamic (live resize/
    // collapse) - read the CURRENT preferred width from the NativeViewHost
    // directly instead of a fixed constant, so this hit test always matches
    // wherever the pane actually is right now, including mid-animation and
    // collapsed (0).
    // null on little windows (no sidebar) - width 0 keeps the hit
    // test correct there (no sidebar band to exclude).
    const int sidebar_width =
        root_view_->sidebar_slot()
            ? root_view_->sidebar_slot()->GetPreferredSize().width()
            : 0;
    const bool collapsed = sidebar_width == 0;
    // The client (non-drag) regions: both cards while split, plus the
    // resize-divider strip between them (the divider is NOT a
    // window-drag region) - just the single card outside a split, as with
    // a single pane. Sourced from the pane container (via
    // AtriumRootView's forwarding accessor) rather than derived inline.
    for (const gfx::Rect& region : root_view_->GetClientRegions(bounds())) {
      if (region.Contains(point)) {
        return ClientView::NonClientHitTest(point);
      }
    }
    // The in-band control row must receive CLICKS, not start a window drag.
    // atrium_nav_carve_decision.h owns the carve geometry as a standalone
    // gtest unit (the group's half now tracks sidebar_width).
    const bool on_right = root_view_->sidebar_on_right();
    const int win_w = width();
    if (!collapsed && point.y() < kTitlebarHeight &&
        PointInNavCarve(point,
                        {.win_w = win_w,
                         .sidebar_width = sidebar_width,
                         .on_right = on_right,
                         .nav_group_in_row = !root_view_->dev_bar_shown()})) {
      return ClientView::NonClientHitTest(point);
    }
    // Backdrop margins around the card, or the sidebar's traffic-light band -
    // mirrored for a right-docked sidebar (the backdrop is then to the LEFT
    // of the sidebar instead of to its right). (Points on the window edges
    // still resize, not drag: cr_mouseDownOnFrameView checks
    // _resizeDirectionForMouseLocation before starting the drag.)
    // the width grip is carved out of the caption region, the
    // same way the in-band control row above is. It sits exactly ON the seam -
    // at x >= sidebar_width by construction, since it is the next child in the
    // layout - so without this carve the backdrop test below claims it and the
    // press becomes a WINDOW DRAG before Views ever sees it. That is what
    // defeated six attempts at a page-side handle and the first attempt at this
    // one: the grip was provably present and provably hit-testable in its own
    // surface, and the window took the pointer first.
    views::View* grip = root_view_->sidebar_grip();
    if (grip && grip->GetVisible()) {
      // Compared in the grip's OWN coordinates rather than converting a rect:
      // ConvertPointToTarget handles the root-view-to-client-view offset, and a
      // point is what this method is given.
      gfx::Point in_grip = point;
      views::View::ConvertPointToTarget(this, grip, &in_grip);
      if (gfx::Rect(grip->size()).Contains(in_grip)) {
        return ClientView::NonClientHitTest(point);
      }
    }
    const bool in_backdrop = on_right ? (point.x() < win_w - sidebar_width)
                                      : (point.x() >= sidebar_width);
    if (in_backdrop || point.y() < kTitlebarHeight) {
      return HTCAPTION;
    }
    return ClientView::NonClientHitTest(point);
  }

 private:
  raw_ptr<AtriumRootView> root_view_;
  raw_ptr<AtriumBrowserWindow> window_;
};

BEGIN_METADATA(AtriumClientView)
END_METADATA

// From WebUIBrowserWindow's mac HandleKeyboardEvent path.
// The character the key produces. event.text carries it already decoded, so
// this stays correct on a non-QWERTY layout the way IsQuitChord does.
std::string VimCharacterOf(const input::NativeWebKeyboardEvent& event) {
  // A control chord's text is the 0x01 to 0x1A control byte it produces, which
  // names no verb, so the chord travels under the letter on the key instead.
  if ((event.GetModifiers() & blink::WebInputEvent::kControlKey) != 0) {
    const int code = event.windows_key_code;
    if (code < 'A' || code > 'Z') {
      return std::string();
    }
    return std::string("ctrl-") + static_cast<char>(code - 'A' + 'a');
  }
  if (event.text[0] == 0) {
    return std::string();
  }
  return base::UTF16ToUTF8(std::u16string(1, event.text[0]));
}

bool VimFocusInChildFrame(Browser* browser) {
  content::WebContents* active =
      browser ? browser->tab_strip_model()->GetActiveWebContents() : nullptr;
  content::RenderFrameHost* focused =
      active ? active->GetFocusedFrame() : nullptr;
  return focused && focused != active->GetPrimaryMainFrame();
}

bool ShouldHandleKeyboardEvent(const input::NativeWebKeyboardEvent& event,
                               const atrium::AtriumWindowPlatform& platform) {
  // |event.skip_if_unhandled| is true when it shouldn't be handled by the
  // browser if it was ignored by the renderer. See http://crbug.com/41018016.
  if (event.skip_if_unhandled) {
    return false;
  }

  // Ignore synthesized keyboard events. See http://crbug.com/41007517.
  if (event.GetType() == input::NativeWebKeyboardEvent::Type::kChar) {
    return false;
  }

  // Do not fire shortcuts on key up, and only forward shortcuts if we have an
  // underlying os event.
  return platform.IsKeyDownEvent(event);
}

}  // namespace

// WebContentsObserver on the window's active tab. Retargeted
// via Retarget(). `role_` picks which callbacks fire, since kLoadProgress's
// pane target and kOther's tab-strip target can differ while split.
// the log line that dates the sidebar's first frame. The startup
// hold cannot wait for it - a window macOS does not count as visible never
// gets one - so a run log has to show where it landed against the show.
class AtriumBrowserWindow::SidebarPaintWatch
    : public content::WebContentsObserver {
 public:
  explicit SidebarPaintWatch(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}
  ~SidebarPaintWatch() override = default;

  void DidFirstVisuallyNonEmptyPaint() override {
    VLOG(1) << "Atrium: sidebar painted";
  }
};

class AtriumBrowserWindow::ActiveTabObserver
    : public content::WebContentsObserver,
      public content::RenderWidgetHost::InputEventObserver {
 public:
  enum class Role { kLoadProgress, kOther };

  ActiveTabObserver(AtriumBrowserWindow* window, Role role)
      : window_(window), role_(role) {}
  ~ActiveTabObserver() override { Unhook(); }

  void Retarget(content::WebContents* contents) {
    Unhook();
    Observe(contents);
    if (role_ == Role::kOther) {
      Hook(contents ? contents->GetPrimaryMainFrame() : nullptr);
    }
  }

  // content::WebContentsObserver:
  void DidStartLoading() override {
    if (role_ != Role::kLoadProgress) {
      return;
    }
    // A same-document navigation raises the raw loading state too, and its
    // waiting->done pair restarted the fill after the page had finished.
    if (web_contents() && web_contents()->ShouldShowLoadingUI()) {
      window_->OnActiveTabStartedLoading();
    }
  }
  void DidStopLoading() override {
    if (role_ == Role::kLoadProgress) {
      window_->OnActiveTabStoppedLoading();
    }
  }
  void LoadProgressChanged(double progress) override {
    if (role_ == Role::kLoadProgress) {
      window_->OnActiveTabLoadProgress(progress);
    }
  }
  // TitleWasSet, not DidStopLoading: Chromium's WebContentsImpl only fires it
  // when the entry's title string actually changes (UpdateTitleForEntryImpl),
  // so a busy page's repeated same-title updates don't re-trigger this.
  void TitleWasSet(content::NavigationEntry* entry) override {
    if (role_ == Role::kOther) {
      window_->PushAiPanelTarget();
    }
  }
  // On LOAD, not on commit. DidFinishNavigation fires before the body is
  // parsed, so the wrap found no text and nothing retried.
  void DocumentOnLoadCompletedInPrimaryMainFrame() override {
    if (role_ == Role::kOther) {
      window_->ReanchorMarginaliaOnActiveTab();
    }
  }
  // Vim installs a parse-time hook earlier than marginalia's, measured in
  // docs/vim-mode-design.md and pinned by VimIsLiveBeforeOnload.
  void PrimaryMainDocumentElementAvailable() override {
    if (role_ == Role::kOther) {
      window_->InstallVimOnActiveTab();
    }
  }
  void DidGetUserInteraction(const blink::WebInputEvent& event) override {
    if (role_ == Role::kOther) {
      window_->DrainMarginaliaMarkClicks();
      window_->DrainVimMode();
    }
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (role_ != Role::kOther) {
      return;
    }
    if (handle->IsInMainFrame() && !handle->IsSameDocument() &&
        handle->HasCommitted()) {
      window_->PushSidebarUrl();
      window_->PushDevBarState();
      window_->ApplyPageTint();
    }
  }
  void DidChangeThemeColor() override {
    if (role_ == Role::kOther) {
      window_->ApplyPageTint();
    }
  }

  // the widget host changes on a cross-site navigation, and the
  // scroll deltas come from whichever one is current.
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override {
    if (role_ == Role::kOther && new_host &&
        new_host->IsInPrimaryMainFrame()) {
      Unhook();
      Hook(new_host);
    }
  }

  void WebContentsDestroyed() override { Unhook(); }

  void OnInputEvent(const content::RenderWidgetHost& host,
                    const blink::WebInputEvent& event,
                    InputEventSource source) override {
    if (role_ != Role::kOther) {
      return;
    }
    if (event.GetType() == blink::WebInputEvent::Type::kMouseWheel) {
      window_->OnPageScrolled(
          static_cast<const blink::WebMouseWheelEvent&>(event).delta_y);
    } else if (event.GetType() ==
               blink::WebInputEvent::Type::kGestureScrollUpdate) {
      window_->OnPageScrolled(static_cast<const blink::WebGestureEvent&>(event)
                                  .data.scroll_update.delta_y);
    }
  }

 private:
  void Hook(content::RenderFrameHost* frame) {
    if (!frame) {
      return;
    }
    host_ = frame->GetRenderWidgetHost();
    if (host_) {
      host_->AddInputEventObserver(this);
    }
  }

  void Unhook() {
    if (host_) {
      host_->RemoveInputEventObserver(this);
      host_ = nullptr;
    }
  }

  const raw_ptr<AtriumBrowserWindow> window_;
  const Role role_;
  raw_ptr<content::RenderWidgetHost> host_ = nullptr;
};

namespace {

// every window that exists right now, so a settings edit reaches all
// of them. Entries come out again in the destructor, unlike BrowserController's
// own Registry() - these objects really are destroyed, and a fan-out over a
// dangling one would repaint freed memory.
std::vector<AtriumBrowserWindow*>& LiveWindows() {
  static base::NoDestructor<std::vector<AtriumBrowserWindow*>> windows;
  return *windows;
}

// the appearance the last read of settings.json produced. A window
// created later seeds itself from this, which is what Swift's registration-time
// push did - without it, a second window opened
// after an edit paints the old look until the next edit.
WindowAppearance& CurrentWindowAppearance() {
  // NoDestructor gave the struct a std::string member: a plain
  // static then needs an exit-time destructor, which -Werror refuses. It was a
  // plain static before precisely because NoDestructor static-asserts against
  // a trivially destructible type, so the two swap as the struct changes.
  static base::NoDestructor<WindowAppearance> appearance;
  return *appearance;
}

// "fiveSeconds" matches settings.rs's DEFAULT_NOTIFICATION_TIMEOUT, so a
// window built before the first settings read still gates correctly.
std::string& CurrentNotificationTimeoutStep() {
  static base::NoDestructor<std::string> step("fiveSeconds");
  return *step;
}

// Both match settings.rs's own defaults. Night Shift is TRUE here for the same
// reason it is there: the effect is the feature, so a build that has not read
// the file yet paints the sky rather than starting flat.
std::string& CurrentSkylightWeather() {
  static base::NoDestructor<std::string> weather("clear");
  return *weather;
}

bool& CurrentSkylightNightShift() {
  static bool night_shift = true;
  return night_shift;
}

// Matches settings.rs's DEFAULT_SWIPE_* constants (the lab's Default preset),
// for the same reason the statics above do.
base::DictValue& CurrentSwipeTuning() {
  static base::NoDestructor<base::DictValue> tuning([] {
    base::DictValue d;
    d.Set("swipeCommitThreshold", 0.15);
    d.Set("swipeSlowCommit", 0.30);
    d.Set("swipeBlendFactor", 6.0);
    d.Set("swipeHoldSilenceGap", 0.5);
    d.Set("swipeOvershootCap", 0.40);
    d.Set("swipeSettleDur", 0.28);
    d.Set("swipeSpringDamp", 0.86);
    d.Set("swipeFollowTau", 0.15);
    return d;
  }());
  return *tuning;
}

// false, not settings.rs's true default: a wrong guess here is a stray AI
// surface (soon a stray network call), so this fails closed instead. Plain
// static, not NoDestructor - bool is trivially destructible.
bool& CurrentAiEnabled() {
  static bool enabled = false;
  return enabled;
}

// '`', matching ai-panel.js's own state.hotkey default - unconfigured must
// read as backtick here too, or the catch-up push would remap a user who
// never asked for a remap.
std::string& CurrentAiPanelHotkey() {
  static base::NoDestructor<std::string> hotkey("`");
  return *hotkey;
}

// false, not settings.rs's true default - same fail-closed reasoning as
// CurrentAiEnabled: a wrong guess here is a stray model call, not just a
// stray surface.
bool& CurrentAiPanelAutoSummarize() {
  static bool auto_summarize = false;
  return auto_summarize;
}

// off is also settings.rs's default. A wrong guess here takes
// single unmodified characters away from every site that binds its own.
bool& CurrentVimModeEnabled() {
  static bool enabled = false;
  return enabled;
}

// Empty is a real value here, not a missing one: it tells the panel to answer
// in its own locale.
std::string& CurrentAiPanelTranslateLanguage() {
  static base::NoDestructor<std::string> tag;
  return *tag;
}

// Cocoa reference date, not Unix epoch - matches atrium_sidebar_tabs.cc's
// MigrationInstantNow(), which atrium_core_set_space_note's `now` also
// follows.
double NotesInstantNow() {
  return base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
}

// Backs LastSettingsJson() - see its declaration.
std::string& MutableLastSettingsJson() {
  static base::NoDestructor<std::string> json;
  return *json;
}

}  // namespace

AtriumBrowserWindow::AtriumBrowserWindow(Browser* browser, bool little)
    : browser_(browser) {
  RegisterSurfaceRoutes();
  // makes FindTabbedBrowser exclude this window.
  if (little) {
    atrium::RegisterLittleWindow(browser_.get());
  }
  location_bar_ = std::make_unique<StubLocationBar>(browser_.get());
  modal_dialog_host_ = std::make_unique<ModalDialogHost>(this);
  exclusive_access_context_ =
      std::make_unique<ExclusiveAccessContextImpl>(this);

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanResize(browser_->create_params().can_resize);
  widget_delegate_->SetCanMaximize(browser_->create_params().can_maximize);
  widget_delegate_->SetCanFullscreen(browser_->create_params().can_fullscreen);
  widget_delegate_->SetCanMinimize(true);
  // GetWindowTitleForCurrentTab moved off Browser onto WindowMetadataController
  // in 151 (registered unconditionally per-Browser as UnownedUserData).
  widget_delegate_->SetTitle(
      WindowMetadataController::From(browser_.get())
          ->GetWindowTitleForCurrentTab(/*include_app_name=*/true));
  // Must be set before Init(): the default WidgetDelegate::CreateClientView(),
  // invoked during Init(), transfers ownership of whatever contents view was
  // set here. A TYPE_WINDOW widget has a non-client (framed) view, so the
  // simpler post-Init Widget::SetContentsView(View*) path WebUIBrowserWindow
  // avoids is not usable here either -- it DCHECKs on framed widgets.
  root_view_ =
      widget_delegate_->SetContentsView(std::make_unique<AtriumRootView>(
          browser_->profile(),
          base::BindRepeating(&AtriumBrowserWindow::SetSidebarWidthLive,
                              weak_factory_.GetWeakPtr()),
          base::BindRepeating(&AtriumBrowserWindow::SetSidebarWidthCommit,
                              weak_factory_.GetWeakPtr()),
          little));
  root_view_->pane_container()->SetSurroundGlowPainter(RootGlowPainter());

  // no unconditional BuildHtmlSidebar() call here any more. A
  // hidden-pool profile (show=false) used to build and navigate its sidebar
  // WebContents at construction time regardless of whether it would ever be
  // shown - real renderer work (layout, JS) for a surface nobody was looking
  // at, paid at the worst possible moment (boot, competing with every other
  // profile's own construction). BuildHtmlSidebar is idempotent (guards on
  // `container->children().empty()`), so it is now called instead at each
  // place BrowserController newly records this window as its slot's
  // presenter - the same catch-up points PushSidebarSnapshot's gate already
  // uses (see atrium_sidebar_snapshot_gate.h): the self-creating ctor's
  // `show` block, ActivateWindow's kFirstActivation branch, and
  // NotifyShownIfAdopted. A window built there is never lazier than "first
  // presented", so the empty-sidebar-while-it-catches-up window this could
  // otherwise open is the same one PushSidebarSnapshot already closes via the
  // sidebarReady handshake below.
  // no-op unless this is a little window, which is the only kind
  // with a foyer slot.
  BuildHtmlFoyer();
  // unconditional (no per-window laziness gate like the
  // sidebar's catch-up) - built once, up front, same as the foyer.
  BuildHtmlAiPanel();
  BuildExtensionPanelChrome();
  // unconditional too - every window gets a devbar slot, hidden
  // until Developer Mode shows it.
  BuildHtmlDevBar();
  // any layout pass that could move the devbar re-derives its
  // shadow rect - see OnViewLayoutInvalidated. Removed in the destructor,
  // ahead of root_view_ = nullptr below.
  root_view_->AddObserver(this);
  web_view_ = root_view_->web_view();
  root_view_->snapshot_view()->SetClickCallback(base::BindRepeating(
      &AtriumBrowserWindow::OnSnapshotClicked, weak_factory_.GetWeakPtr()));
  root_view_->pane_container()->active_pane()->scrim_view()->SetPressCallback(
      base::BindRepeating(&AtriumBrowserWindow::OnPeekScrimPressed,
                          weak_factory_.GetWeakPtr()));
  // re-registers the seam left unwired when
  // the Swift target was deleted - see
  // AtriumPaneContainerView::SetDividerCommittedCallback.
  root_view_->pane_container()->SetDividerCommittedCallback(
      base::BindRepeating(&AtriumBrowserWindow::OnDividerResizeCommitted,
                          weak_factory_.GetWeakPtr()));
  RebuildPaneFocusSubscriptions();
  // Chromeless window drag regions (see AtriumClientView). Replaces the
  // default plain-ClientView factory; runs during Init() below. root_view_
  // is bound here (rather than read from `contents_view` inside the lambda)
  // since it's already known - AtriumClientView needs it directly (not just
  // as a generic views::View) to read the live sidebar width for hit-testing.
  widget_delegate_->SetClientViewFactory(base::BindOnce(
      [](AtriumRootView* root_view, AtriumBrowserWindow* window,
         views::Widget* widget,
         views::View* contents_view) -> std::unique_ptr<views::ClientView> {
        return std::make_unique<AtriumClientView>(widget, contents_view,
                                                  root_view, window);
      },
      root_view_.get(), this));

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  params.name = "AtriumBrowserWindow";
  params.bounds = gfx::Rect(0, 0, kInitialWidth, kInitialHeight);
  params.delegate = widget_delegate_.get();
  params.native_widget = CreateNativeWidget();
  // Liquid-glass window (design session v3, stage 2): the widget's compositor
  // surface carries alpha so the root view's TRANSLUCENT tint (SetBackdropTint)
  // lets the NSVisualEffectView installed behind it show through - the whole
  // window ground reads as tinted glass over the desktop. The web content card
  // stays fully opaque on top.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  widget_->Init(std::move(params));
  window_platform_ =
      std::make_unique<atrium::AtriumWindowPlatform>(widget_.get());
  InstallLiquidGlassBackdrop();
  // InitParams::bounds gives the size but leaves the window at the top-left
  // corner; center it. (a 1080x720 window after Show() is not a Views bug:
  // an AX tiling window manager resized the window post-Show.)
  widget_->CenterWindow(gfx::Size(kInitialWidth, kInitialHeight));
  widget_->MakeCloseSynchronous(base::BindOnce(
      &AtriumBrowserWindow::OnWindowCloseRequested, base::Unretained(this)));
  // Round the live pane to the content card's corners. On the holder's
  // ui::Layer (the WebContents' layer attaches under it via ViewsHostable),
  // so the clip follows whatever WebContents ShowTab later swaps in. Must
  // run after Init(): the holder's native wrapper (which owns the layer)
  // only exists once the WebView is in a widget. The frozen-snapshot pane
  // gets the identical radius on its own layer (AtriumRootView's ctor).
  web_view_->holder()->SetCornerRadii(
      gfx::RoundedCornersF(ContentCardCornerRadius()));
  // M2c Rev3 bug 3: observe this widget's own activation so the frozen/live
  // swap can follow window focus - see OnWidgetActivationChanged's header
  // comment.
  widget_->AddObserver(this);

  // Cmd+Plus/Minus/0 are Chromium's own zoom commands with no hook of their
  // own into this window - this callback is the only way to notice one fired.
  if (auto* zoom_observer = BrowserWindowZoomObserver::From(browser_.get())) {
    zoom_changed_subscription_ =
        zoom_observer->RegisterZoomChangedCallback(base::BindRepeating(
            &AtriumBrowserWindow::OnZoomChanged, weak_factory_.GetWeakPtr()));
  }

  // The active-tab observers. Point at nothing until
  // ShowTab's first RetargetLoadProgress / the first OnActiveTabChanged.
  load_progress_observer_ = std::make_unique<ActiveTabObserver>(
      this, ActiveTabObserver::Role::kLoadProgress);
  active_tab_observer_ = std::make_unique<ActiveTabObserver>(
      this, ActiveTabObserver::Role::kOther);

  // last, because it paints - root_view_, window_platform_ and the
  // widget all have to exist first. A window opened after a settings edit
  // therefore opens looking the way the file says, rather than snapping to it
  // on the next edit.
  //
  // Registered here rather than at the top of the constructor, and that is the
  // same rule the destructor's erase follows from the other end: an entry in
  // this list is a window a settings write may paint at any moment, so it must
  // not name one whose root view and widget do not exist yet.
  LiveWindows().push_back(this);
  ApplyWindowAppearance(CurrentWindowAppearance());
  // the space tint before the sidebar exists, so the first frame
  // is the window's own colour and not the system fill.
  RestoreSpaceTint();

  // Off this thread: only the first profile's window is built before
  // BrowserMainLoop disallows blocking, so a read here aborted every later one.
  atrium::SettingsJsonCache::GetInstance().Get(
      base::ThreadPool::CreateTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE}),
      base::BindOnce([] {
        return atrium::LoadSettingsJSON(atrium::ResolveSettingsFilePath());
      }),
      base::BindOnce(&AtriumBrowserWindow::OnSettingsJsonLoaded,
                     weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::OnSettingsJsonLoaded(const std::string& json) {
  MutableLastSettingsJson() = json;
  // No-op until TemplateURLService loads - OnTemplateURLServiceChanged
  // catches up once it does, from the cache this fills.
  if (const std::optional<std::vector<atrium::MirroredEngine>> engines =
          atrium::SearchEnginesFromSettings(json)) {
    atrium::MirrorSearchEngines(
        TemplateURLServiceFactory::GetForProfile(browser_->profile()),
        *engines);
  }
  // After the mirror, which is what puts the stored choice's GUID in the
  // service. A service still loading mirrors nothing, and AutocompleteBridge's
  // catch-up pass applies the choice instead.
  atrium::ApplyStoredProfileSearchEngine(
      browser_->profile()->GetBaseName().AsUTF8Unsafe());
}

AtriumBrowserWindow::~AtriumBrowserWindow() {
  // Out of the list before anything else is torn down: from here on this window
  // cannot paint, and a settings write landing mid-teardown must not find it.
  std::erase(LiveWindows(), this);
  atrium::UnregisterLittleWindow(browser_.get());
  widget_->RemoveObserver(this);
  // AtriumPermissionPrompt owns the sheet, not this window, and nothing
  // enforces that its tab tears down first - dismiss before widget_ goes.
  if (live_permission_sheet_) {
    live_permission_sheet_->DismissForWindowTeardown();
    live_permission_sheet_ = nullptr;
  }
  // the fullscreen notice is a child widget parented into widget_,
  // so it must not outlive it - same posture as the child-widget resets below.
  // Ahead of TearDownPreBrowserWindowDestruction too, which takes the
  // ExclusiveAccessManager the bubble reads from with it.
  if (exclusive_access_context_) {
    exclusive_access_context_->DestroyAnyExclusiveAccessBubble();
  }
  // A view from another profile's Browser outlives this window. Hand it back
  // before the view tree goes, or its destruction fires that onClosed.
  if (extension_panel_owner_ &&
      extension_panel_owner_->browser() != browser_.get()) {
    ReturnExtensionPanelToOwner();
  }
  browser_->GetFeatures().TearDownPreBrowserWindowDestruction();
  // Same posture as the devtools detach below, for the devbar's wrapper: it
  // comes off the window through the NativeViewHost, not from a destructor.
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->Unwrap();
  }
  // detach both panes' docked-devtools WebViews before the view
  // tree unwinds (widget_.reset() below) - runs WebView's own detach path
  // (SetWebContents(nullptr)) deterministically here, rather than letting a
  // still-attached devtools WebContents get torn down implicitly by the
  // view tree's own destruction. Same "child must not outlive what it's
  // attached into" posture as the child-widget resets below, just for a
  // child WebContents attachment instead of a child widget.
  if (root_view_) {
    AtriumPaneContainerView* container = root_view_->pane_container();
    // Clear each pane's focus tracker EXPLICITLY before
    // detaching its devtools WebView - previously safe only by implicit
    // member-destruction order (the tracker died before the child views);
    // make the invariant non-accidental so a member-layout refactor can't
    // silently break it. No RestoreDevToolsFocus here: the whole window is
    // going away, there is nothing meaningful to restore focus to.
    for (size_t i = 0; i < container->pane_count(); ++i) {
      container->PaneAt(i)->ClearDevToolsFocusTracker();
      container->PaneAt(i)->devtools_web_view()->SetWebContents(nullptr);
    }
    root_view_->RemoveObserver(this);
  }
  web_view_ = nullptr;
  sidebar_web_ = nullptr;
  ai_panel_web_ = nullptr;
  extension_popup_anchor_ = nullptr;
  root_view_ = nullptr;
  // Destroy the child command-bar + sidebar-overlay + peek widgets before
  // their parent - a child widget must not outlive widget_.
  status_bubble_show_timer_.Stop();
  command_bar_widget_.reset();
  find_bar_widget_.reset();
  theme_studio_widget_.reset();
  overview_controller_.reset();
  courtyard_widget_.reset();
  sidebar_overlay_widget_.reset();
  peek_widget_.reset();
  capture_overlay_widget_.reset();
  quick_menu_widget_.reset();
  space_icon_picker_widget_.reset();
  ResetExtensionMenuState();
  hint_overlay_widget_.reset();
  toast_overlay_widget_.reset();
  hover_url_widget_.reset();
  // It holds a raw_ptr to widget_, which traps on quit if it dangles.
  window_platform_.reset();
  widget_.reset();
}

void AtriumBrowserWindow::OnWidgetActivationChanged(views::Widget* widget,
                                                    bool active) {
  // becoming key told the Swift model which window-slot the user
  // had moved to, and its SpacesViewModel decided whether to take a tab back.
  // Nothing on this side makes that decision yet.
  if (widget == widget_.get() && active) {
    // A tab dragged in from another window brings its history with it and
    // fires no navigation here, so this is the only signal that catches it.
    PushNavAbility();
  }
}

// the authoritative fullscreen-state signal, covering BOTH entry
// paths - the video player's button (via ExclusiveAccessContext) and macOS's
// own green button / Esc, which never touches ExclusiveAccessContext at all.
void AtriumBrowserWindow::OnWidgetShowStateChanged(views::Widget* widget) {
  if (widget != widget_.get()) {
    return;
  }
  UpdateWindowCornerMask();
  ReapplyWindowControlsVisibility();
  if (immersive_fullscreen_ && !widget_->IsFullscreen()) {
    // Left fullscreen without going through ExitFullscreen - e.g. the green
    // button while a video was fullscreen. Without this the sidebar would
    // stay hidden and the card edge-to-edge in a restored window.
    immersive_fullscreen_ = false;
    if (root_view_) {
      root_view_->SetImmersive(false);
    }
  }
  // drop the fullscreen notice the moment the window stops being
  // fullscreen. WindowFullscreenStateChanged below only refreshes the bubble
  // on the way IN, so a green-button or Esc exit would otherwise leave it
  // floating at security-surface level over a restored window.
  if (!widget_->IsFullscreen() && exclusive_access_context_) {
    exclusive_access_context_->DestroyAnyExclusiveAccessBubble();
  }
  // Reconcile the renderer, which still believes it is fullscreen after a
  // native exit; this is the same notification Browser sends.
  browser_->GetFeatures()
      .exclusive_access_manager()
      ->fullscreen_controller()
      ->WindowFullscreenStateChanged();
}

void AtriumBrowserWindow::OnWidgetBoundsChanged(views::Widget* widget,
                                                const gfx::Rect& new_bounds) {
  if (widget != widget_.get()) {
    return;
  }
  // The half-window cap follows the window, not only the drag.
  if (ExtensionPanelShowing()) {
    const int clamped =
        ClampExtensionPanelWidth(extension_panel_width_, new_bounds.width());
    if (clamped != extension_panel_width_) {
      extension_panel_width_ = clamped;
      ApplyExtensionPanelLayout();
    }
  }
  // a fullscreen transition (either path) resizes the window, and
  // the style mask only reflects the new state once it has. This is where the
  // corner mask actually settles - SetImmersiveFullscreen's own call races the
  // async native transition.
  UpdateWindowCornerMask();
  // The lights answer that same transition, and the show-state change comes
  // too early to see it: the window is not full screen yet when it fires. An
  // in-flight collapse owns them until it lands, so it is left alone.
  if (light_slide_span_ == 0) {
    ReapplyWindowControlsVisibility();
  }
  // Same race for the fullscreen notice: it is created off the show-state
  // change, before the window has actually grown to the screen.
  if (exclusive_access_context_) {
    exclusive_access_context_->RepositionExclusiveAccessBubble();
  }
}

void AtriumBrowserWindow::OnWidgetVisibilityChanged(views::Widget* widget,
                                                    bool visible) {
  if (widget == widget_.get() && theme_visibility_callback_) {
    theme_visibility_callback_.Run(visible);
  }
}

void AtriumBrowserWindow::SetThemeVisibilityCallback(
    base::RepeatingCallback<void(bool)> callback) {
  theme_visibility_callback_ = std::move(callback);
}

views::NativeWidget* AtriumBrowserWindow::CreateNativeWidget() {
  return atrium::CreateAtriumNativeWidget(browser_.get(), widget_.get(), this);
}

AtriumPaneView* AtriumBrowserWindow::ActivePane() const {
  return root_view_->pane_container()->active_pane();
}

content::WebContents* AtriumBrowserWindow::DisplayedContents() const {
  return root_view_ ? ActivePane()->web_view()->web_contents() : nullptr;
}

views::View* AtriumRootView::sidebar_slot() {
  return sidebar_web_container_.get();
}

void AtriumBrowserWindow::SetSidebarExtensionActionsJSON(
    const std::string& json,
    const std::string& profile_id) {
  last_sidebar_extension_actions_json_ = json;
  // Kept per profile so a rail pane of another one can draw that profile's own
  // pins. An empty id has no profile to file it under, an incognito
  // controller's above all - it still reaches the strip on screen.
  if (!profile_id.empty()) {
    sidebar_extension_actions_by_profile_[profile_id] = json;
  }
  if (!sidebar_contents_) {
    return;
  }
  // JSON.parse rather than interpolation - the same reason
  // PushSidebarSnapshot's script gives (extension names are attacker-
  // controlled and this executes at browser privilege).
  const std::u16string script =
      u"window.atriumSetSidebarExtensions && "
      u"atriumSetSidebarExtensions(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"), " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(profile_id)) + u")";
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

const std::string&
AtriumBrowserWindow::SidebarExtensionActionsJSONForTesting() const {
  return last_sidebar_extension_actions_json_;
}

const std::string&
AtriumBrowserWindow::SidebarExtensionActionsJSONForProfileForTesting(
    const std::string& profile_id) const {
  static const base::NoDestructor<std::string> kNone;
  auto it = sidebar_extension_actions_by_profile_.find(profile_id);
  return it == sidebar_extension_actions_by_profile_.end() ? *kNone
                                                           : it->second;
}

void AtriumBrowserWindow::SetLastExtensionActionsController(
    BrowserController* controller) {
  last_extension_actions_controller_ = controller;
}

BrowserController* AtriumBrowserWindow::LastExtensionActionsController()
    const {
  return last_extension_actions_controller_;
}

void AtriumBrowserWindow::SetDevBarExtensionActionsJSON(
    const std::string& json) {
  // The sidebar takes its scope off the snapshot, which this surface never
  // gets. Without it extIconPath() returns an empty src and every chip draws
  // the browser's broken-image glyph.
  const std::u16string scope_script =
      u"window.atriumSetIconScope && atriumSetIconScope(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(
          IconScopeFor(browser_ ? browser_->profile() : nullptr))) +
      u")";
  // JSON.parse rather than interpolation - extension names are attacker
  // controlled and this executes at browser privilege.
  const std::u16string script =
      u"window.atriumSetSidebarExtensions && "
      u"atriumSetSidebarExtensions(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->CallScript(scope_script);
    bar->CallScript(script);
  }
}

BrowserController* AtriumBrowserWindow::DisplayedController() const {
  Browser* displayed =
      BrowserController::VisibleBrowserForBrowser(browser_.get());
  return BrowserController::ControllerForBrowser(displayed ? displayed
                                                           : browser_.get());
}

void AtriumBrowserWindow::ToggleExtensionPin(const std::string& extension_id) {
  if (BrowserController* controller = DisplayedController()) {
    controller->ToggleExtensionPin(extension_id);
  }
}

void AtriumBrowserWindow::ToggleQuickMenu(const gfx::Rect& anchor_in_window) {
  if (!quick_menu_widget_) {
    quick_menu_widget_ = std::make_unique<AtriumQuickMenuWidget>(this);
  }
  // A second press on the same button closes, which is what makes the
  // puzzle glyph a toggle rather than a re-open.
  if (quick_menu_widget_->IsShowing() &&
      quick_menu_widget_->anchor_in_window() == anchor_in_window) {
    quick_menu_widget_->Close();
    return;
  }
  // Usually the press already closed it: the mouse-down took key back before
  // the renderer forwarded the click, so this message IS the toggle's close
  // half and reopening here would make the button never shut the menu.
  const base::TimeTicks last_closed = quick_menu_widget_->last_closed_at();
  if (!last_closed.is_null() &&
      base::TimeTicks::Now() - last_closed < kQuickMenuReopenGuard) {
    return;
  }
  if (BrowserController* controller = DisplayedController()) {
    quick_menu_widget_->SetActionsJSON(
        controller->CurrentExtensionActionsJSON());
  }
  quick_menu_widget_->Show(anchor_in_window);
}

bool AtriumBrowserWindow::IsQuickMenuShowing() const {
  return quick_menu_widget_ && quick_menu_widget_->IsShowing();
}

content::WebContents* AtriumBrowserWindow::QuickMenuContentsForTesting()
    const {
  return quick_menu_widget_ ? quick_menu_widget_->web_contents() : nullptr;
}

void AtriumBrowserWindow::SetQuickMenuActionsJSON(const std::string& json) {
  if (quick_menu_widget_) {
    quick_menu_widget_->SetActionsJSON(json);
  }
}

namespace {

// Whether `space_id` still names a live space. The write path resolves the
// same id to a live position, so only a REMOVED space needs to close the
// picker early - a reorder alone no longer strands it.
bool SpaceIdStillExists(const std::string& space_id) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return false;
  }
  atrium_core_string_free(raw);
  return true;
}

}  // namespace

void AtriumBrowserWindow::ShowSpaceIconPicker(
    int space_index,
    const std::string& value,
    const gfx::Rect& anchor_in_window) {
  if (!space_icon_picker_widget_) {
    space_icon_picker_widget_ =
        std::make_unique<AtriumSpaceIconPickerWidget>(this);
  }
  // A right click while the picker for the SAME dot is up reopens it fresh
  // rather than toggling: the entry point is a menu item, not a button that
  // shares its own click with the close.
  const base::TimeTicks last_closed = space_icon_picker_widget_->last_closed_at();
  if (!last_closed.is_null() &&
      base::TimeTicks::Now() - last_closed < kSpaceIconPickerReopenGuard) {
    return;
  }
  space_icon_picker_widget_->Show(space_index, value, anchor_in_window);
}

bool AtriumBrowserWindow::IsSpaceIconPickerShowing() const {
  return space_icon_picker_widget_ && space_icon_picker_widget_->IsShowing();
}

content::WebContents* AtriumBrowserWindow::SpaceIconPickerContentsForTesting()
    const {
  return space_icon_picker_widget_ ? space_icon_picker_widget_->web_contents()
                                   : nullptr;
}

bool AtriumBrowserWindow::SendSpaceIconPickerEscapeForTesting() {
  if (!space_icon_picker_widget_ ||
      !space_icon_picker_widget_->web_contents()) {
    return false;
  }
  input::NativeWebKeyboardEvent escape(
      blink::WebKeyboardEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now());
  escape.windows_key_code = ui::VKEY_ESCAPE;
  return space_icon_picker_widget_->HandleKeyboardEvent(
      space_icon_picker_widget_->web_contents(), escape);
}

std::string AtriumBrowserWindow::CurrentQuickMenuSiteJSON() {
  base::DictValue site;
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  const GURL url = contents ? contents->GetVisibleURL() : GURL();
  // The rule insecure-origin.js applies on the page side: plain http is
  // insecure unless the host is loopback.
  const bool insecure =
      url.SchemeIs(url::kHttpScheme) && !net::IsLocalhost(url);
  site.Set("host", url.host());
  site.Set("secure", !insecure);
  // Nothing but a web page can go through the share sheet.
  site.Set("shareable", url.SchemeIsHTTPOrHTTPS());
  std::string json;
  base::JSONWriter::Write(site, &json);
  return json;
}

void AtriumBrowserWindow::ShowPageInfoForQuickMenu(
    const gfx::Rect& anchor_in_window) {
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  if (!contents || !widget_) {
    return;
  }
  const GURL url = contents->GetVisibleURL();
  // A null anchor plus an anchor rect: an Atrium window has no BrowserView,
  // so ShowPageInfoDialog cannot resolve one through a location bar.
  PageInfoBubbleSpecification::Builder builder(
      views::BubbleAnchor(), widget_->GetNativeWindow(), contents, url);
  const gfx::Rect window = widget_->GetWindowBoundsInScreen();
  builder.AddAnchorRect(anchor_in_window + window.OffsetFromOrigin());
  views::BubbleDialogDelegateView* bubble =
      PageInfoBubbleView::CreatePageInfoBubble(builder.Build());
  if (!bubble || !bubble->GetWidget()) {
    LOG(WARNING) << "Atrium: page info bubble did not open";
    return;
  }
  VLOG(1) << "Atrium: page info bubble opened";
  bubble->GetWidget()->Show();
}

void AtriumBrowserWindow::OpenExtensionsPage() {
  Browser* target = SidebarTargetBrowser();
  if (!target) {
    return;
  }
  NavigateParams params(target, GURL(chrome::kChromeUIExtensionsURL),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void AtriumBrowserWindow::OpenExtensionStorePage() {
  Browser* target = SidebarTargetBrowser();
  if (!target) {
    return;
  }
  NavigateParams params(
      target,
      GURL("https://chromewebstore.google.com/category/extensions"),
      ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void AtriumBrowserWindow::CopyCurrentPageUrl() {
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  if (!contents) {
    return;
  }
  CopyUrlToClipboard(contents->GetVisibleURL().spec());
}

void AtriumBrowserWindow::ClickExtensionActionFromSurface(
    const std::string& extension_id) {
  if (BrowserController* controller = DisplayedController()) {
    controller->ClickExtensionAction(extension_id);
  }
}

void AtriumBrowserWindow::ShowExtensionContextMenuFromSurface(
    const std::string& extension_id, const gfx::Rect& screen_rect) {
  BrowserController* controller = DisplayedController();
  Browser* owner_browser =
      controller ? controller->BrowserOwningExtensionForContextMenu(
                       extension_id)
                 : nullptr;
  if (!owner_browser) {
    return;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(owner_browser->profile())
          ->enabled_extensions()
          .GetByID(extension_id);
  if (!extension) {
    LOG(WARNING) << "Atrium: extension context menu - unknown/disabled "
                    "extension " << extension_id;
    return;
  }
  // Reset before building: a stale close callback then has no old
  // runner/adapter/model left to fire into, so it cannot clear the new menu.
  ResetExtensionMenuState();
  const bool is_pinned =
      ToolbarActionsModel::Get(owner_browser->profile())
          ->IsActionPinned(extension_id);
  // A null delegate hides Inspect Popup: this path has no DevTools popup host.
  extension_menu_model_ =
      std::make_unique<extensions::ExtensionContextMenuModel>(
          extension, owner_browser, is_pinned, /*delegate=*/nullptr,
          ToolbarActionsModel::CanShowActionsInToolbar(*owner_browser),
          extensions::ExtensionContextMenuModel::ContextMenuSource::
              kToolbarAction);
  extension_menu_adapter_ = std::make_unique<views::MenuModelAdapter>(
      extension_menu_model_.get(),
      base::BindRepeating(&AtriumBrowserWindow::OnExtensionContextMenuClosed,
                          weak_factory_.GetWeakPtr()));
  extension_menu_runner_ = std::make_unique<views::MenuRunner>(
      extension_menu_adapter_->CreateMenu(),
      views::MenuRunner::CONTEXT_MENU | views::MenuRunner::HAS_MNEMONICS);
  extension_menu_owner_closed_subscription_ =
      owner_browser->RegisterBrowserDidClose(base::BindRepeating(
          &AtriumBrowserWindow::OnExtensionContextMenuOwnerClosed,
          weak_factory_.GetWeakPtr()));
  extension_menu_runner_->RunMenuAt(
      widget_.get(), nullptr, screen_rect,
      views::MenuAnchorPosition::kTopLeft, ui::mojom::MenuSourceType::kMouse);
}

bool AtriumBrowserWindow::IsExtensionContextMenuShowingForTesting() const {
  return extension_menu_runner_ && extension_menu_runner_->IsRunning();
}

bool AtriumBrowserWindow::ExtensionContextMenuHasSidePanelEntryForTesting()
    const {
  return extension_menu_model_ &&
         extension_menu_model_
             ->GetIndexOfCommandId(
                 extensions::ExtensionContextMenuModel::
                     TOGGLE_SIDE_PANEL_VISIBILITY)
             .has_value();
}

void AtriumBrowserWindow::
    ExecuteExtensionContextMenuSidePanelEntryForTesting() {
  CHECK(extension_menu_model_);
  extension_menu_model_->ExecuteCommand(
      extensions::ExtensionContextMenuModel::TOGGLE_SIDE_PANEL_VISIBILITY,
      /*event_flags=*/0);
}

std::u16string AtriumBrowserWindow::ExtensionContextMenuPinLabelForTesting()
    const {
  if (!extension_menu_model_) {
    return std::u16string();
  }
  // The label text itself carries is_pinned: extension_context_menu_model.cc
  // picks between the pin and unpin string ids on that one bool.
  const std::optional<size_t> index =
      extension_menu_model_->GetIndexOfCommandId(
          extensions::ExtensionContextMenuModel::TOGGLE_VISIBILITY);
  return index ? extension_menu_model_->GetLabelAt(*index) : std::u16string();
}

void AtriumBrowserWindow::CloseExtensionContextMenuForTesting() {
  if (extension_menu_runner_) {
    extension_menu_runner_->Cancel();
  }
}

// views::MenuRunner is documented safe to delete at any point, so runner ->
// adapter -> model -> subscription is safe in every caller below.
void AtriumBrowserWindow::ResetExtensionMenuState() {
  extension_menu_runner_.reset();
  extension_menu_adapter_.reset();
  extension_menu_model_.reset();
  extension_menu_owner_closed_subscription_ = base::CallbackListSubscription();
}

void AtriumBrowserWindow::OnExtensionContextMenuClosed() {
  ResetExtensionMenuState();
}

void AtriumBrowserWindow::OnExtensionContextMenuOwnerClosed(
    BrowserWindowInterface*) {
  ResetExtensionMenuState();
}

namespace {

// Every PushX() below repeats this null check before dispatching to a
// surface's main frame - one place to do it instead.
void CallSurface(content::WebContents* contents,
                 const std::u16string& script) {
  if (!contents) {
    return;
  }
  contents->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                      base::NullCallback());
}

}  // namespace

void AtriumBrowserWindow::PushSidebarExtensionsForNewSurface() {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser_.get());
  if (!controller) {
    return;
  }
  // Route through the same helper SelectSpace and the pin/click paths use, so
  // a surface rebuilt mid cross-profile swap gets the active space's own
  // snapshot and the tracker stays in sync with what is actually on screen.
  BrowserController* owner = controller->ActiveSpaceController();
  // Every profile this window has heard from, so a rail pane of one the user
  // has visited before draws its pins rather than an empty strip. The active
  // one goes last, because the page reads the final push as the active strip.
  const std::string active_profile_id = owner->ProfileId();
  // Copied: the call below writes back into the map it is reading.
  const std::map<std::string, std::string> stored =
      sidebar_extension_actions_by_profile_;
  for (const auto& [profile_id, json] : stored) {
    if (profile_id != active_profile_id) {
      SetSidebarExtensionActionsJSON(json, profile_id);
    }
  }
  ExtensionActionBridge::PushActionsToWindow(
      this, owner, owner->CurrentExtensionActionsJSON());
}

void AtriumBrowserWindow::PushNotificationTimeoutToSidebar() {
  const std::u16string script =
      u"window.atriumSetNotificationTimeout && "
      u"atriumSetNotificationTimeout(" +
      base::UTF8ToUTF16(
          base::GetQuotedJSONString(CurrentNotificationTimeoutStep())) +
      u")";
  CallSurface(sidebar_contents_.get(), script);
}

void AtriumBrowserWindow::PushNotificationTimeoutToToastOverlay() {
  if (!toast_overlay_widget_) {
    return;
  }
  const std::u16string script =
      u"window.atriumSetNotificationTimeout && "
      u"atriumSetNotificationTimeout(" +
      base::UTF8ToUTF16(
          base::GetQuotedJSONString(CurrentNotificationTimeoutStep())) +
      u")";
  CallSurface(toast_overlay_widget_->web_contents(), script);
}

// static
std::optional<std::string> AtriumBrowserWindow::ArchiveThresholdFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* threshold =
      parsed->GetDict().FindString("archiveThreshold");
  return threshold ? std::make_optional(*threshold) : std::nullopt;
}

void AtriumBrowserWindow::PushSkylightToSidebar() {
  // One call for both, because the surface applies them together - a weather
  // change with a stale Night Shift would repaint the backdrop twice.
  const std::u16string script =
      u"window.atriumSetSkylight && atriumSetSkylight(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(CurrentSkylightWeather())) +
      u", " + (CurrentSkylightNightShift() ? u"true" : u"false") + u")";
  CallSurface(sidebar_contents_.get(), script);
}

// static
void AtriumBrowserWindow::SetSkylight(const std::string& weather,
                                      bool night_shift) {
  CurrentSkylightWeather() = weather;
  CurrentSkylightNightShift() = night_shift;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushSkylightToSidebar();
  }
}

// static
std::optional<std::pair<std::string, bool>>
AtriumBrowserWindow::SkylightFromSettings(std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = parsed->GetDict();
  const std::string* weather = dict.FindString("skylightWeather");
  if (!weather) {
    return std::nullopt;
  }
  // An absent Night Shift key means not yet chosen, so it takes the same
  // default settings.rs gives it rather than reading as off.
  return std::make_pair(*weather,
                        dict.FindBool("skylightNightShift").value_or(true));
}

// static
void AtriumBrowserWindow::SetNotificationTimeoutStep(const std::string& step) {
  CurrentNotificationTimeoutStep() = step;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushNotificationTimeoutToSidebar();
    window->PushNotificationTimeoutToToastOverlay();
  }
}

// static
std::optional<std::string> AtriumBrowserWindow::NotificationTimeoutFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* step = parsed->GetDict().FindString("notificationTimeout");
  return step ? std::make_optional(*step) : std::nullopt;
}

void AtriumBrowserWindow::PushSwipeTuningToSidebar() {
  std::string json;
  base::JSONWriter::Write(CurrentSwipeTuning(), &json);
  const std::u16string script =
      u"window.atriumSetSwipeTuning && atriumSetSwipeTuning(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  CallSurface(sidebar_contents_.get(), script);
}

// Two window values the sidebar has to draw and C++ cannot draw
// for it: the glass alpha, because the floating panel is a child window whose
// material samples nothing and its own alpha IS the effect, and the native-app
// chrome mode, whose fill lands on the pane container and stops at the sidebar
// column.
//
// Written straight onto the root element, the way --atrium-docked-width and
// the floating flag are, rather than through a function on the page. Both are
// pure CSS state, and a push that calls into page code is a push that is
// silently dropped whenever it lands before the page installs its own.
//
// The alpha crosses as a fraction here because the property is one: the
// percent the settings file spells is turned into it once, in C++, so 184/255
// is not rounded differently by whoever reads the property next.
void AtriumBrowserWindow::PushSidebarAppearance() {
  if (!sidebar_contents_) {
    return;
  }
  // The chrome's ink follows the backdrop it sits on everywhere else, and over
  // a space tint InkForBackdrop answers black or white.
  // A native surround is the one background that rule never covered - it is the
  // system's neutral, which is near-white in a light appearance, and white ink
  // on it is no ink at all. Derived from the colour rather than the appearance
  // so a surround the user picked answers too.
  const SkColor ink = color_utils::GetColorWithMaxContrast(
      SkColorSetA(static_cast<SkColor>(opaque_surround_argb_), SK_AlphaOPAQUE));
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(base::StringPrintf(
          // Optional: a push can land before the document has a root, and
          // there is nothing to receive it then. Unguarded it threw a
          // TypeError per property, with no source file to trace it to.
          "document.documentElement?.style.setProperty("
          "'--atrium-glass-alpha', '%.2f');"
          "document.documentElement?.style.setProperty("
          "'--atrium-surround-color', '#%02x%02x%02x');"
          "document.documentElement?.style.setProperty("
          "'--atrium-surround-ink', '%d %d %d');"
          "document.documentElement?.classList.toggle("
          "'atrium-native-chrome', %s)",
          backdrop_glass_alpha_ / 255.0, (opaque_surround_argb_ >> 16) & 0xFF,
          (opaque_surround_argb_ >> 8) & 0xFF, opaque_surround_argb_ & 0xFF,
          SkColorGetR(ink), SkColorGetG(ink), SkColorGetB(ink),
          opaque_surround_enabled_ ? "true" : "false")),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

// static
void AtriumBrowserWindow::SetSwipeTuning(base::DictValue tuning) {
  for (auto [key, value] : tuning) {
    CurrentSwipeTuning().Set(key, value.Clone());
  }
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushSwipeTuningToSidebar();
  }
}

// static
base::DictValue AtriumBrowserWindow::SwipeTuningFromSettings(
    std::string_view settings_json) {
  base::DictValue out;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return out;
  }
  static constexpr const char* kSwipeTuningKeys[] = {
      "swipeCommitThreshold", "swipeSlowCommit",   "swipeBlendFactor",
      "swipeHoldSilenceGap",  "swipeOvershootCap", "swipeSettleDur",
      "swipeSpringDamp",      "swipeFollowTau",
  };
  const base::DictValue& dict = parsed->GetDict();
  for (const char* key : kSwipeTuningKeys) {
    if (std::optional<double> value = dict.FindDouble(key)) {
      out.Set(key, *value);
    }
  }
  // Rides along with the tuning because swipe.js owns the tap pattern too.
  if (const std::string* haptics = dict.FindString("swipeHaptics")) {
    out.Set("swipeHaptics", *haptics);
  }
  return out;
}

// static
void AtriumBrowserWindow::SetAiEnabled(bool enabled) {
  CurrentAiEnabled() = enabled;
  // A copy of the list, not the list - see SetWindowAppearance for why a
  // window closing itself mid-broadcast would otherwise invalidate this loop.
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushAiPanelEnabled();
    if (!enabled) {
      // teardownAiSurface() never sends aiPanelToggled(false) - clear the
      // open/pending-focus state here or it survives to a later reshow.
      const bool had_focus =
          window->ai_panel_web_ && window->ai_panel_web_->HasFocus();
      window->ai_panel_focus_pending_ = false;
      window->ai_panel_open_ = false;
      window->SetAiPanelWidth(window->ai_panel_width_);
      if (had_focus) {
        window->FocusWebContentsPane();
      }
    }
  }
}

// The chrome's ink by rule, which sidebar.css sets as --fg at
// rgba(255,255,255,.92), and the fallback for a space tint nothing can read.
constexpr char kChromeInkHex[] = "#FFFFFF";

// The neutral the glass sits on, standing in for the desktop the window
// cannot read.
SkColor AppearanceBase() {
  const bool dark =
      ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
      ui::NativeTheme::PreferredColorScheme::kDark;
  return dark ? SkColorSetRGB(0x1E, 0x1E, 0x1E)
              : SkColorSetRGB(0xF2, 0xF2, 0xF7);
}

std::string HexOf(SkColor color) {
  return base::StringPrintf("#%02X%02X%02X", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

// Whether this page's colours are the SITE's. An error page is Chromium's own
// chrome in the site's clothes - it declares white - so without this every
// failed load on the web washes the window the same way.
bool PageColourIsTheSites(content::WebContents* contents) {
  const content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  return !entry || entry->GetPageType() != content::PAGE_TYPE_ERROR;
}

// The page's BRAND colour, for pulling a grey glow toward. The favicon comes
// first here, the reverse of PageColorHex: theme-color is white on most light
// sites, and a white brand pulls nothing anywhere.
std::optional<SkColor> BrandColor(content::WebContents* contents) {
  if (!PageColourIsTheSites(contents)) {
    return std::nullopt;
  }
  if (auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents);
      driver && driver->FaviconIsValid()) {
    if (std::optional<SkColor> icon =
            atrium::DominantColorOfFavicon(driver->GetFavicon())) {
      return icon;
    }
  }
  return contents->GetThemeColor();
}

// The colour this page reads as, "" when it has none. theme-color wins because
// a site declaring one has said what it wants. Most declare none, and a favicon
// is the next thing on screen that carries the site's own colour.
std::string PageColorHex(content::WebContents* contents) {
  if (!PageColourIsTheSites(contents)) {
    return std::string();
  }
  if (std::optional<SkColor> theme = contents->GetThemeColor()) {
    return HexOf(*theme);
  }
  // GetFavicon answers with the DEFAULT GLOBE, not an empty image, until a
  // page's own icon resolves. Tinting toward that would move every
  // unresolved tab's chrome the same way.
  auto* driver = favicon::ContentFaviconDriver::FromWebContents(contents);
  if (!driver || !driver->FaviconIsValid()) {
    return std::string();
  }
  if (std::optional<SkColor> icon =
          atrium::DominantColorOfFavicon(driver->GetFavicon())) {
    return HexOf(*icon);
  }
  return std::string();
}

// The space tint stays the base; the page colour is a bounded shift
// on top of it, resolved by the core so the bound and the contrast floor are
// one rule rather than one per caller.
//
// SAMPLE RATE: a handful per page, at most. A navigation commit,
// DidChangeThemeColor and OnFaviconUpdated are the only moments either source
// changes. Nothing reads the framebuffer and nothing polls, so scrolling,
// resizing and animating a page all cost zero samples.
void AtriumBrowserWindow::ClearPageTint() {
  if (!ChromePageTintEnabled()) {
    return;
  }
  // the last tab closed. Its colour has nothing to hold it.
  VLOG(1) << "Atrium: page tint cleared, no active tab";
  StopAmbilightSampling();
  SetEdgeTint(atrium::EdgeTint());
  RestoreSpaceTint();
}

void AtriumBrowserWindow::ApplyPageTint() {
  if (!ChromePageTintEnabled()) {
    return;
  }
  content::WebContents* contents = DisplayedContents();
  if (!contents) {
    ClearPageTint();
    return;
  }
  // A page mid-load turned the band its favicon's colour at once and then
  // jumped to the sampled colours. The band holds until the load stops.
  if (contents->IsLoading()) {
    VLOG(1) << "Atrium: page tint held for the load";
    return;
  }
  // the page's colour goes to the band only, so the backdrop needs
  // the space tint set once, or it stays on the root's default fill.
  if (!has_backdrop_tint_) {
    RestoreSpaceTint();
  }
  // A page declaring nothing is the common case, not an error: it must leave
  // the space looking as it does with no page at all, which is what the core
  // answers an empty string with.
  const std::string page = PageColorHex(contents);
  VLOG(1) << "Atrium: page tint sampled " << (page.empty() ? "(none)" : page);
  ambilight_brand_ = BrandColor(contents);
  // A colourless commit used to answer with the plain space tint, which is
  // the jump to the default a user sees between the old tab's glow and the
  // new page's first read. While the sampler is live the glow holds instead.
  if (page.empty() && ambilight_live_) {
    VLOG(1) << "Atrium: page tint held for the sampler";
    return;
  }
  if (std::optional<SkColor> color = ChromeColorForPage(page, ChromeInkHex())) {
    // One colour on all four edges, which is all a declared tag or a favicon
    // can say. SetEdgeTint routes that to the crossfading flat setter, so the
    // chrome still DRIFTS toward the page rather than snapping.
    SetEdgeTint(atrium::UniformEdgeTint(*color));
  }
}

// What the core grades the drift against: a light tint graded against white
// refuses every shift, so the space keeps its own colour and no glow is
// painted at all.
std::string AtriumBrowserWindow::ChromeInkHex() {
  // The space's OWN tint, which an empty page asks for. The drifted one on
  // screen is what this ink decides, so reading it back would close the loop.
  char* raw = atrium_core_page_tint(WindowSpaceIndex(), "", "",
                                    backdrop_glass_alpha_ / 255.0);
  if (!raw) {
    return kChromeInkHex;
  }
  const std::string space_tint(raw);
  atrium_core_string_free(raw);
  const std::optional<SkColor> tint = ParseHexColor(space_tint);
  if (!tint) {
    return kChromeInkHex;
  }
  return HexOf(atrium::InkForSpaceTint(
      atrium::EffectiveSpaceTint(browser_.get(), *tint)));
}

// The core owns the bound and the contrast floor. Each ambilight edge goes
// through here on its own, so a page readable on one side and not the other is
// pulled back only on the side that needs it.
std::optional<SkColor> AtriumBrowserWindow::ChromeColorForPage(
    const std::string& page_hex,
    const std::string& ink_hex) {
  // The core mixes over the ACTIVE SPACE's tint and takes a space index, not a
  // base, so an incognito window cannot ask for the mix it wants. It refuses
  // the drift, which also keeps the page's colour off its chrome.
  if (atrium::IsIncognitoWindow(browser_.get())) {
    return atrium::kIncognitoTint;
  }
  // The core's drift moves the SPACE tint a little toward the page, which is
  // a grey band on a neutral native surround. That mode carries no ink of the
  // space's, so the page's own colour is the glow, and no colour is no glow.
  if (opaque_surround_enabled_) {
    return page_hex.empty() ? std::make_optional(SK_ColorTRANSPARENT)
                            : ParseHexColor(page_hex);
  }
  char* raw =
      atrium_core_page_tint(WindowSpaceIndex(), page_hex.c_str(),
                            ink_hex.c_str(), backdrop_glass_alpha_ / 255.0);
  if (!raw) {
    return std::nullopt;
  }
  const std::string resolved(raw);
  atrium_core_string_free(raw);
  return ParseHexColor(resolved);
}

void AtriumBrowserWindow::StartAmbilightSampling() {
  if (!ChromePageTintEnabled()) {
    return;
  }
  // Every segment would resolve to the flat constant above, so the readback
  // would paint nothing - and reading this window's pixels at all is the one
  // thing it exists to avoid.
  if (atrium::IsIncognitoWindow(browser_.get())) {
    VLOG(1) << "Atrium: ambilight sampler refused, window is off the record";
    return;
  }
  ambilight_live_ = true;
  ++ambilight_generation_;
  // A new page deserves the top rung however still the last one went.
  ambilight_cadence_.Reset();
  // ApplyPageTint has already put the space tint on the window by now, so
  // "same colours as last time" is no longer what is on screen. Without this
  // the glow stayed dropped out until some colour happened to differ.
  last_sampled_ = atrium::EdgeTint();
  ScheduleNextAmbilightSample(ambilight_cadence_.interval_ms());
}

void AtriumBrowserWindow::StopAmbilightSampling() {
  ambilight_live_ = false;
  ++ambilight_generation_;
  ambilight_timer_.Stop();
  ambilight_cadence_.Reset();
}

// Every read ends here, including the ones that resolved nothing. A rung
// change is logged because it is the only moment the rate is observable: a
// still page stops producing edge lines, which is the point of backing off.
void AtriumBrowserWindow::AdvanceAmbilightCadence(bool changed) {
  const int before = ambilight_cadence_.interval_ms();
  const int next = ambilight_cadence_.OnSample(changed);
  if (next != before) {
    VLOG(1) << "Atrium: ambilight cadence " << before << "ms -> " << next
            << "ms";
  }
  ScheduleNextAmbilightSample(next);
}

void AtriumBrowserWindow::ScheduleNextAmbilightSample(int interval_ms) {
  ambilight_timer_.Start(
      FROM_HERE, base::Milliseconds(interval_ms),
      base::BindOnce(&AtriumBrowserWindow::SampleAmbilightFrame,
                     base::Unretained(this)));
}

void AtriumBrowserWindow::SampleAmbilightFrame() {
  if (!ChromePageTintEnabled()) {
    return;
  }
  content::WebContents* contents = DisplayedContents();
  // Reading an occluded tab keeps a buried window's GPU busy for a glow nobody
  // can see. Rescheduled anyway, at the slowest rung, because nothing else
  // here hears the window come back to the front.
  if (!contents || contents->GetVisibility() != content::Visibility::VISIBLE ||
      contents->IsLoading()) {
    ScheduleNextAmbilightSample(atrium::kAmbilightRungsMs.back());
    return;
  }
  // The same rule the declared-tag path applies, and it has to be the same one:
  // gating only the readback still let an error page's white reach the chrome.
  // Incognito IS sampled - no colour here is persisted or leaves the process.
  if (!PageColourIsTheSites(contents)) {
    AdvanceAmbilightCadence(false);
    return;
  }
  atrium::CaptureViewportFrame(
      contents,
      gfx::Size(atrium::kAmbilightSampleSize, atrium::kAmbilightSampleSize),
      base::BindOnce(&AtriumBrowserWindow::OnAmbilightFrame,
                     weak_factory_.GetWeakPtr(), ambilight_generation_));
}

void AtriumBrowserWindow::OnAmbilightFrame(int generation,
                                           const SkBitmap& frame) {
  // Rescheduling here would also restart a sampler that was stopped.
  if (generation != ambilight_generation_) {
    return;
  }
  const std::optional<atrium::EdgeTint> sampled =
      atrium::EdgeTintFromFrame(frame);
  // A refused copy keeps whatever the event path last resolved rather than
  // dropping the chrome to nothing, and counts as no change so a page that
  // cannot be read backs off instead of retrying at full rate forever.
  if (!sampled) {
    AdvanceAmbilightCadence(false);
    return;
  }
  // A white edge stays white on a native surround, which is the "less glow
  // where the page is light" the mode reads as. The brand pull is for glass.
  const atrium::EdgeTint blended =
      opaque_surround_enabled_
          ? *sampled
          : atrium::BlendTowardBrand(*sampled, ambilight_brand_);
  // every segment walks the core's contrast check on its own, so
  // one stretch of a page cannot drag its neighbours under the ink's floor.
  // Each distinct colour is resolved once: a core call costs about 15ms,
  // and a flat page repeats one colour across its 28 slots.
  std::map<SkColor, SkColor> seen;
  // Once a frame, not once a colour: the ink follows the space's tint, which
  // no segment of the page can move, and reading it is a core call too.
  const std::string ink = ChromeInkHex();
  const auto resolve = [&](SkColor raw) -> std::optional<SkColor> {
    if (const auto it = seen.find(raw); it != seen.end()) {
      return it->second;
    }
    const std::optional<SkColor> color = ChromeColorForPage(HexOf(raw), ink);
    if (color) {
      seen[raw] = *color;
    }
    return color;
  };
  atrium::EdgeTint resolved;
  for (atrium::Edge edge : {atrium::Edge::kLeft, atrium::Edge::kRight,
                            atrium::Edge::kTop, atrium::Edge::kBottom}) {
    // A null from the core is a read that resolved nothing, the same as a
    // refused copy: back off and keep going. A bare return here stopped the
    // sampler until the next tab switch.
    const std::optional<SkColor> mean = resolve(blended.For(edge));
    if (!mean) {
      AdvanceAmbilightCadence(false);
      return;
    }
    resolved.SetEdge(edge, *mean);
    for (int i = 0; i < atrium::kEdgeSegments; ++i) {
      const std::optional<SkColor> color = resolve(blended.Run(edge)[i]);
      if (!color) {
        AdvanceAmbilightCadence(false);
        return;
      }
      resolved.Run(edge)[i] = *color;
    }
  }
  const bool changed = !(resolved == last_sampled_);
  AdvanceAmbilightCadence(changed);
  // An unchanged read paints nothing and logs nothing. At the top rung it
  // arrives eight times a second, and repainting the window for a colour that
  // did not move is the cost this whole ladder exists to avoid.
  if (!changed) {
    return;
  }
  last_sampled_ = resolved;
  VLOG(1) << "Atrium: ambilight edges L=" << HexOf(resolved.left)
          << " R=" << HexOf(resolved.right) << " T=" << HexOf(resolved.top)
          << " B=" << HexOf(resolved.bottom)
          << (resolved.IsUniform() ? " uniform" : " varied") << " left-run "
          << (resolved.left_run.front() != resolved.left_run.back() ? "varied"
                                                                    : "flat")
          << " next=" << ambilight_cadence_.interval_ms() << "ms";
  SetEdgeTint(resolved);
}

void AtriumBrowserWindow::ApplyPageTintFor(content::WebContents* contents) {
  if (!browser_ ||
      browser_->tab_strip_model()->GetActiveWebContents() != contents) {
    return;
  }
  ApplyPageTint();
}

// static
bool& AtriumBrowserWindow::ChromePageTintEnabled() {
  static bool enabled = false;
  return enabled;
}

// static
void AtriumBrowserWindow::SetChromePageTint(bool enabled) {
  ChromePageTintEnabled() = enabled;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    if (enabled) {
      window->ApplyPageTint();
      window->StartAmbilightSampling();
    } else {
      window->StopAmbilightSampling();
      // the band is the page colour's only home now, so the off
      // path clears it. Restoring the space tint alone left the glow up.
      window->SetEdgeTint(atrium::EdgeTint());
      window->RestoreSpaceTint();
      VLOG(1) << "Atrium: page tint off, glow cleared";
    }
  }
}

// turning the drift off puts the window back on the space's own
// tint. ApplyPageTint cannot do it - it declines to touch the window at all
// while the setting is off, which is what keeps it free on every navigation.
void AtriumBrowserWindow::RestoreSpaceTint() {
  // An empty page colour is what the core returns the unshifted space tint
  // for, so "off" and "this page declares nothing" reach the same value
  // through the same path.
  char* raw =
      atrium_core_page_tint(WindowSpaceIndex(), "", ChromeInkHex().c_str(),
                            backdrop_glass_alpha_ / 255.0);
  if (!raw) {
    return;
  }
  const std::string resolved(raw);
  atrium_core_string_free(raw);
  if (std::optional<SkColor> color = ParseHexColor(resolved)) {
    SetBackdropTint(*color);
  }
}

// static
std::optional<bool> AtriumBrowserWindow::ChromePageTintFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("chromePageTint");
}

// static
bool& AtriumBrowserWindow::DevBarPinned() {
  static bool pinned = false;
  return pinned;
}

// static
void AtriumBrowserWindow::SetSidebarNavModifiers(bool shift,
                                                 bool cmd,
                                                 bool option) {
  // flagsChanged also fires for Control and caps lock, and a held key repeats
  // it. Without this every one of those would run script in every sidebar for
  // a state that did not change.
  static bool last_shift = false;
  static bool last_cmd = false;
  static bool last_option = false;
  if (shift == last_shift && cmd == last_cmd && option == last_option) {
    return;
  }
  last_shift = shift;
  last_cmd = cmd;
  last_option = option;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushNavModifiers(shift, cmd, option);
    // The hovered link's card names where the link would open, and its widget
    // is a third document PushNavModifiers does not reach.
    if (window->hover_url_widget_) {
      window->hover_url_widget_->SetNavModifiers(shift, cmd, option);
    }
  }
}

// static
void AtriumBrowserWindow::DismissSidebarMenusForMouseDown(
    const gfx::Point& screen_point) {
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    gfx::Rect bounds;
    if (content::WebContents* contents = window->sidebar_contents_.get()) {
      if (content::RenderWidgetHostView* view =
              contents->GetRenderWidgetHostView()) {
        bounds = view->GetViewBounds();
      }
    }
    if (MouseDownDismissesSidebarMenu(window->sidebar_menu_open_, bounds,
                                      screen_point)) {
      window->PushDismissMenus();
    }
  }
}

// static
void AtriumBrowserWindow::DismissSidebarMenus() {
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    if (window->sidebar_menu_open_) {
      window->PushDismissMenus();
    }
  }
}

void AtriumBrowserWindow::PushDismissMenus() {
  content::WebContents* contents = sidebar_contents_.get();
  if (!contents) {
    return;
  }
  CallSurface(contents,
             u"window.atriumDismissMenus && window.atriumDismissMenus()");
  // The page answers with sidebarMenuClosed, but that is a round trip. Clearing
  // here keeps the rest of a click burst off the script path.
  sidebar_menu_open_ = false;
  VLOG(1) << "Atrium: sidebar menus dismissed by an outside press";
}

// static
void AtriumBrowserWindow::SetDevBarPinned(bool pinned) {
  DevBarPinned() = pinned;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->ApplyDevBarPinned();
  }
}

// static
std::optional<bool> AtriumBrowserWindow::AddressBarPinnedFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("addressBarPinned");
}

// static
void AtriumBrowserWindow::SetAppIconVariant(const std::string& slug) {
  AtriumApplyAppIcon(slug);
}

// static
std::optional<std::string> AtriumBrowserWindow::AppIconVariantFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* slug = parsed->GetDict().FindString("appIconVariant");
  // A refused spelling reads as "this file says nothing", which leaves the
  // Dock on the bundle icon. Answering the default instead would overwrite a
  // variant a newer build had legitimately set.
  if (!slug || !IsAppIconSlug(*slug)) {
    return std::nullopt;
  }
  return *slug;
}

// static
std::optional<bool> AtriumBrowserWindow::AiEnabledFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("aiEnabled");
}

// static
void AtriumBrowserWindow::SetAiPanelHotkey(const std::string& key) {
  CurrentAiPanelHotkey() = key;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushAiPanelHotkey();
  }
}

// static
std::optional<std::string> AtriumBrowserWindow::AiPanelHotkeyFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* key = parsed->GetDict().FindString("aiPanelHotkey");
  return key ? std::make_optional(*key) : std::nullopt;
}

// static
void AtriumBrowserWindow::SetAiPanelAutoSummarize(bool enabled) {
  CurrentAiPanelAutoSummarize() = enabled;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushAiPanelAutoSummarize();
  }
}

// static
std::optional<bool> AtriumBrowserWindow::AutoSummarizeFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("autoSummarize");
}

// static
void AtriumBrowserWindow::SetVimModeEnabled(bool enabled) {
  CurrentVimModeEnabled() = enabled;
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushVimModeEnabled();
  }
  if (enabled) {
    return;
  }
  // Switching off has to reach pages that are already open. The script stays
  // injected, so switching back on needs no reload.
  for (AtriumBrowserWindow* window : windows) {
    window->HideVimOnActiveTab();
  }
}

// the surfaces run the grammar themselves, so each one needs the
// toggle rather than asking the browser per keystroke.
void AtriumBrowserWindow::PushVimModeEnabled() {
  const std::u16string script =
      CurrentVimModeEnabled()
          ? u"window.atriumSetVimModeEnabled && atriumSetVimModeEnabled(true)"
          : u"window.atriumSetVimModeEnabled && atriumSetVimModeEnabled(false)";
  // Every surface that runs the grammar needs the toggle. A surface that is
  // not open yet picks it up when it installs.
  CallSurface(sidebar_contents_.get(), script);
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::HideVimOnActiveTab() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildVimTeardownScript(), base::NullCallback(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

// static
bool AtriumBrowserWindow::VimModeEnabled() {
  return CurrentVimModeEnabled();
}

// static
std::optional<bool> AtriumBrowserWindow::VimModeEnabledFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("vimModeEnabled");
}

// static
void AtriumBrowserWindow::SetAiPanelTranslateLanguage(std::string tag) {
  CurrentAiPanelTranslateLanguage() = std::move(tag);
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->PushAiPanelTranslateLanguage();
  }
}

// static
std::optional<std::string> AtriumBrowserWindow::TranslateLanguageFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* tag = parsed->GetDict().FindString("translateLanguage");
  if (!tag) {
    return std::nullopt;
  }
  // settings.rs's valid_language_tag, repeated because the settings window
  // pushes the saved value to the panel before the Rust write returns, and
  // this one ends up inside a model prompt.
  if (tag->size() > 40 ||
      !std::ranges::all_of(*tag, [](char c) {
        return base::IsAsciiAlphaNumeric(c) || c == '-';
      })) {
    return std::string();
  }
  return *tag;
}

// static
bool AtriumBrowserWindow::AiEnabled() {
  return CurrentAiEnabled();
}

// static
const std::string& AtriumBrowserWindow::LastSettingsJson() {
  return MutableLastSettingsJson();
}

// static
void AtriumBrowserWindow::SetLastSettingsJson(std::string json) {
  MutableLastSettingsJson() = std::move(json);
}

void AtriumBrowserWindow::SetExtensionPopupAnchorRect(
    const gfx::Rect& screen_rect) {
  if (!root_view_) {
    return;
  }
  if (!extension_popup_anchor_) {
    auto probe = std::make_unique<views::View>();
    // Ignored by the layout, or the root view's BoxLayout would give it a
    // column of its own and move it off the icon on the next pass.
    probe->SetProperty(views::kViewIgnoredByLayoutKey, true);
    probe->SetCanProcessEventsWithinSubtree(false);
    extension_popup_anchor_ = root_view_->AddChildView(std::move(probe));
  }
  extension_popup_anchor_->SetBoundsRect(
      screen_rect.IsEmpty()
          ? gfx::Rect()
          : views::View::ConvertRectFromScreen(root_view_, screen_rect));
}

// The popup must anchor in the MAIN window, which is activatable, so its own
// webview can take key. The quick menu is closed by the time this runs.
views::View* AtriumBrowserWindow::ExtensionPopupAnchorView() {
  if (!root_view_) {
    return nullptr;
  }
  // the icon itself when the click reported one. The fallbacks
  // below are whole surfaces, and a bubble anchored to one lands at its corner
  // rather than at the icon the user pressed.
  if (extension_popup_anchor_ &&
      !extension_popup_anchor_->bounds().IsEmpty()) {
    return extension_popup_anchor_;
  }
  views::View* bar = root_view_->dev_bar_container();
  if (bar && bar->GetVisible()) {
    return bar;
  }
  views::View* slot = root_view_->sidebar_slot();
  return slot ? slot : static_cast<views::View*>(root_view_);
}

views::BubbleAnchor AtriumBrowserWindow::GetExtensionPopupAnchor() {
  views::View* anchor = ExtensionPopupAnchorView();
  return anchor ? views::BubbleAnchor(anchor) : views::BubbleAnchor();
}

bool AtriumBrowserWindow::HasSidebar() const {
  return root_view_->sidebar_slot() != nullptr;
}

bool AtriumBrowserWindow::IsSidebarOnRight() const {
  return root_view_->sidebar_on_right();
}

bool AtriumBrowserWindow::IsSidebarCollapsed() const {
  // little windows have no sidebar host - report "not collapsed"
  // (there is no sidebar to be collapsed; collapse-driven traffic-light and
  // inset logic must stay on their expanded defaults).
  // the slot covers both arms; a collapsed HTML sidebar must report
  // collapsed too, or the traffic-light and inset logic reads it as expanded.
  views::View* slot = root_view_->sidebar_slot();
  return slot && slot->GetPreferredSize().width() <= 0;
}

void AtriumBrowserWindow::EnsurePeekWidget() {
  if (peek_widget_) {
    return;  // Never rebuilt - see PeekWidget's class doc.
  }
  peek_widget_ = std::make_unique<PeekWidget>(this);
}

PeekControlsWidget* AtriumBrowserWindow::PeekControlsForTesting() {
  return peek_widget_ ? peek_widget_->controls() : nullptr;
}

int AtriumBrowserWindow::PeekShowGenerationForTesting() const {
  return peek_widget_ ? peek_widget_->ShowGenerationForTesting() : 0;
}

bool AtriumBrowserWindow::PeekPromotingForTesting() const {
  return peek_widget_ && peek_widget_->PromotingForTesting();
}

void AtriumBrowserWindow::ShowContentDropPreview(SplitEdge edge) {
  if (AtriumPaneView* pane = ActivePane()) {
    pane->ShowDropPreview(edge);
  }
}

void AtriumBrowserWindow::ClearContentDropPreview() {
  if (AtriumPaneView* pane = ActivePane()) {
    pane->ClearDropPreview();
  }
}

atrium::OwnWindowDrop AtriumBrowserWindow::OwnWindowDropAt(
    const gfx::Point& screen_point) {
  views::View* slot = root_view_->sidebar_slot();
  const gfx::Rect sidebar =
      slot && slot->GetVisible() ? slot->GetBoundsInScreen() : gfx::Rect();
  AtriumPaneContainerView* panes = root_view_->pane_container();
  std::vector<gfx::Rect> pane_bounds;
  for (size_t i = 0; i < panes->pane_count(); ++i) {
    pane_bounds.push_back(panes->PaneAt(i)->GetBoundsInScreen());
  }
  return atrium::ChooseOwnWindowDrop(screen_point, sidebar, pane_bounds);
}

int AtriumBrowserWindow::PaneIndexShowing(content::WebContents* tab) {
  AtriumPaneContainerView* panes = root_view_->pane_container();
  for (size_t i = 0; tab && i < panes->pane_count(); ++i) {
    if (panes->PaneAt(i)->web_view()->web_contents() == tab) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AtriumBrowserWindow::ClearOwnPaneDropPreviews() {
  AtriumPaneContainerView* panes = root_view_->pane_container();
  for (size_t i = 0; i < panes->pane_count(); ++i) {
    panes->PaneAt(i)->ClearDropPreview();
  }
}

bool AtriumBrowserWindow::PreviewOwnWindowDrop(gfx::Point screen_point,
                                               content::WebContents* tab) {
  const atrium::OwnWindowDrop drop = OwnWindowDropAt(screen_point);
  ClearOwnPaneDropPreviews();
  if (drop.kind == atrium::OwnWindowDropKind::kElsewhere) {
    return false;
  }
  // The pane the point is in wins over another window's highlight.
  BrowserController::ClearDropPreviews();
  // A centre drop would hide the pane's own tab behind the dragged one while
  // the strip still names it, so only an edge is a target here.
  if (drop.kind == atrium::OwnWindowDropKind::kPane &&
      drop.edge != atrium::SplitEdge::kCenter &&
      drop.pane_index != PaneIndexShowing(tab)) {
    root_view_->pane_container()
        ->PaneAt(static_cast<size_t>(drop.pane_index))
        ->ShowDropPreview(drop.edge);
  }
  return true;
}

bool AtriumBrowserWindow::DropTabOnOwnWindow(content::WebContents* tab,
                                             gfx::Point screen_point) {
  ClearOwnPaneDropPreviews();
  const atrium::OwnWindowDrop drop = OwnWindowDropAt(screen_point);
  if (drop.kind == atrium::OwnWindowDropKind::kElsewhere) {
    return false;
  }
  if (!tab) {
    return true;
  }
  const uint64_t tab_id = TabId(tab);
  if (drop.kind == atrium::OwnWindowDropKind::kSidebar) {
    // The sidebar handles its own drop, or refuses it.
    VLOG(1) << "Atrium: own-window drop of tab " << tab_id << " on the sidebar";
    return true;
  }
  AtriumPaneContainerView* panes = root_view_->pane_container();
  content::WebContents* target =
      panes->PaneAt(static_cast<size_t>(drop.pane_index))
          ->web_view()
          ->web_contents();
  if (drop.edge == atrium::SplitEdge::kCenter || !target || target == tab) {
    return true;
  }
  // A sidebar row can name a tab that is not a pane yet, so it adds a leaf.
  const bool is_pane = PaneIndexShowing(tab) >= 0;
  if (!is_pane && (panes->pane_count() >= atrium::kMaxSplitLeaves ||
                   BrowserController::TabIsOutsideActiveSpace(tab))) {
    LOG(WARNING) << "Atrium: own-window drop of tab " << tab_id
                 << " refused - the split is full or the tab is in another "
                    "space";
    return true;
  }
  if (is_pane) {
    ClosePaneForTab(tab_id, "own-window-drop");
  }
  // Closing a pane renumbers the leaves, so the target is found again.
  const int target_index = PaneIndexShowing(target);
  if (target_index < 0) {
    return true;
  }
  panes->SetActivePaneIndex(target_index);
  VLOG(1) << "Atrium: own-window drop of tab " << tab_id << " at edge "
          << static_cast<int>(drop.edge) << " of tab " << TabId(target);
  InsertPaneFromDrop(tab, drop.edge);
  return true;
}

bool AtriumBrowserWindow::IsContentDropPreviewVisibleForTesting() const {
  AtriumPaneView* pane = ActivePane();
  views::View* preview = pane ? pane->drop_preview_view() : nullptr;
  return preview && preview->GetVisible();
}

gfx::Rect AtriumBrowserWindow::ContentDropPreviewBoundsForTesting() const {
  AtriumPaneView* pane = ActivePane();
  views::View* preview = pane ? pane->drop_preview_view() : nullptr;
  return preview && preview->GetVisible() ? preview->bounds() : gfx::Rect();
}

namespace {

// the core's timeline with each stop's capture folded in as `thumb`,
// the data: URL history-scrubber.js sets on an <img>. Positional, which is why
// the store shares the core's cap and eviction rule.
std::string TimelineWithThumbnails(const std::string& timeline_json,
                                   uint64_t tab_id) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(timeline_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return timeline_json;
  }
  base::ListValue& stops = parsed->GetList();
  for (size_t i = 0; i < stops.size(); ++i) {
    const std::string_view thumb = VisitThumbnails().ThumbnailAt(tab_id, i);
    base::DictValue* stop = stops[i].GetIfDict();
    if (stop && !thumb.empty()) {
      stop->Set("thumb", std::string(thumb));
    }
  }
  return base::WriteJson(*parsed).value_or(timeline_json);
}

// The url this tab's timeline holds at `index`, empty when it holds none.
std::string TimelineUrlAt(uint64_t tab_id, int index) {
  char* raw = atrium_core_visits_for_tab(tab_id);
  if (!raw) {
    return std::string();
  }
  const std::string json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list() || index < 0 ||
      static_cast<size_t>(index) >= parsed->GetList().size()) {
    return std::string();
  }
  const base::DictValue* stop = parsed->GetList()[index].GetIfDict();
  const std::string* url = stop ? stop->FindString("url") : nullptr;
  return url ? *url : std::string();
}

}  // namespace

void AtriumBrowserWindow::OnSurfaceMessage(std::string_view name,
                                           const base::ListValue& args) {
  // Before the router, so no handler has to remember. A renderer is not bound
  // by what the page chose to draw, so a hidden menu item and an undraggable
  // row refuse nothing on their own.
  if (!WritesAllowed() && atrium::SurfaceMessageWritesDocument(name)) {
    // The name LAST: a scenario greps the source for the literal ahead of the
    // first wildcard in its pattern, and an interpolation in the middle leaves
    // it nothing to find - so the assertion cannot name the message.
    VLOG(1) << "Atrium: surface message dropped, window is off the record: "
            << name;
    return;
  }
  // the table in atrium_browser_window_surface_routes.cc answers
  // every named message. Only the capture verbs, which are parsed rather than
  // named, still come through here.
  if (surface_router_.Route(name, args)) {
    return;
  }
  if (browser_ && MaybeRunCaptureVerb(name)) {
    return;
  }
}

// The renderer supplies the id from the DOM, so it is not trusted: both of
// these resolve it against the live strip and do nothing if it is unknown.
//
// This window's own browser_ is not always the one whose content it shows: a
// cross-profile in-window swap (kInWindowSwap) keeps the presenter's window on
// screen and puts ANOTHER controller's tab into its pane. The sidebar's rows
// come from the SLOT's active space - that other controller - so selection,
// activation, close and new-tab have to resolve against the same Browser the
// rows came from, exactly as CommandTarget() already does for the per-tab
// commands.
//
// Falls back to browser_ when nothing else is visible for the slot, which is
// the no-swap case and today's behaviour.
Browser* AtriumBrowserWindow::SidebarTargetBrowser() const {
  Browser* visible =
      atrium::BrowserController::VisibleBrowserForBrowser(browser_.get());
  // The slot's visible controller can be a window that was never REALISED.
  // With --no-startup-window and one browser per profile, most of them are:
  // the Browser exists, its widget does not. Adding a tab to one drives
  // ShowTab into NativeViewHostMac::AttachNativeView, whose CHECK(window_host)
  // is fatal rather than recoverable.
  if (visible && visible != browser_.get() && !HasRealisedWindow(visible)) {
    return browser_.get();
  }
  return visible ? visible : browser_.get();
}

int AtriumBrowserWindow::WindowSpaceIndex() const {
  return atrium::BrowserController::SpaceIndexForBrowser(browser_.get());
}

content::WebContents* AtriumBrowserWindow::SidebarActiveTab() const {
  Browser* target = SidebarTargetBrowser();
  return target ? target->tab_strip_model()->GetActiveWebContents() : nullptr;
}

bool AtriumBrowserWindow::WritesAllowed() const {
  Browser* shown = SidebarTargetBrowser();
  return atrium::ProfilesMayWriteDocument(
      browser_ ? browser_->profile() : nullptr,
      shown ? shown->profile() : nullptr);
}

// Whether `browser` has a native window to attach content to.
bool AtriumBrowserWindow::HasRealisedWindow(Browser* browser) {
  return browser && browser->window() &&
         static_cast<bool>(browser->window()->GetNativeWindow());
}

content::WebContents* AtriumBrowserWindow::OpenSidebarColdTab(
    const std::string& url,
    bool foreground) {
  if (!browser_) {
    return nullptr;
  }
  // The string comes from the renderer, so it is attacker-controlled in the
  // same way a page title is: validate rather than trust. Only the schemes a
  // restored tab can legitimately carry - anything else (javascript:, file:,
  // chrome:) is dropped rather than navigated to.
  const GURL target(url);
  if (!atrium::IsOrdinarySidebarUrl(target)) {
    VLOG(1) << "Atrium: cold sidebar row refused - scheme not allowed";
    return nullptr;
  }
  // Belt and braces after a tab added to a window with no native
  // host aborts the browser process, so refuse rather than risk it. Doing
  // nothing is a bad click; crashing loses the session.
  Browser* into = SidebarTargetBrowser();
  if (!HasRealisedWindow(into)) {
    VLOG(1) << "Atrium: cold sidebar row refused - target window has no "
               "realised native window";
    return nullptr;
  }
  // the url is logged rather than the row, because a cold row has no
  // tab id yet - that is what makes it cold.
  // The path itself is not logged; its length stands in for it.
  VLOG(1) << "Atrium: cold sidebar row opening " << atrium::LogSafeUrl(target)
          << " (path len " << target.path().size() << ")";
  return chrome::AddAndReturnTabAt(into, target, -1, foreground);
}

bool AtriumBrowserWindow::ActivateFavoriteTab(const std::string& favorite_url) {
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return false;
  }
  for (int i = 0; i < model->count(); ++i) {
    if (IsTabForFavorite(model->GetWebContentsAt(i), favorite_url)) {
      model->ActivateTabAt(i);
      return true;
    }
  }
  return false;
}

void AtriumBrowserWindow::OpenFavoriteTab(const std::string& favorite_url) {
  // Armed BEFORE the insert, because the strip observer runs inside it and
  // draws an unclaimed tab as an ordinary row.
  atrium::BrowserController* const controller =
      atrium::BrowserController::ControllerForBrowser(SidebarTargetBrowser());
  if (controller) {
    controller->SetPendingFavoriteKey(favorite_url);
  }
  // The tab itself, not the active one read back afterwards. That read had to
  // guard on the tab's spec equalling `favorite_url`, which never held for a
  // stored URL that GURL canonicalises on the way in.
  content::WebContents* opened =
      OpenSidebarColdTab(favorite_url, /*foreground=*/true);
  if (controller) {
    controller->SetPendingFavoriteKey(std::string());
  }
  if (!opened) {
    VLOG(1) << "Atrium: favourite opened nothing to claim";
    return;
  }
  // Again, for the window with no controller to have armed the claim.
  atrium::SetFavoriteKeyForTab(opened, favorite_url);
  VLOG(1) << "Atrium: favourite claimed tab " << atrium::TabId(opened);
}

void AtriumBrowserWindow::ActivateFavoriteRow(const std::string& favorite_url,
                                              const std::string& tab_id) {
  // The tab id is tried before the URL: a favourite that redirects on load
  // commits a URL the tile does not carry, so matching on it stacked a second
  // tab every click.
  if (ActivateTabWithId(tab_id)) {
    VLOG(1) << "Atrium: favourite activated its own tab " << tab_id;
    return;
  }
  if (ActivateFavoriteTab(favorite_url)) {
    VLOG(1) << "Atrium: favourite activated the tab it opened, by key";
    return;
  }
  // Never by url: an ordinary tab that navigated to the favourite's address
  // is a different tab. A startup tab restored on that url is
  // claimed at restore, so the claim above already found it.
  // Another window on this profile may hold the favourite's tab. Moved here,
  // as a row click moves its tab: one instance of the site, whichever window
  // asks.
  if (atrium::BrowserController* controller =
          atrium::BrowserController::ControllerForBrowser(
              SidebarTargetBrowser())) {
    if (controller->RestoreParkedFavorite(favorite_url)) {
      return;
    }
    if (const std::optional<uint64_t> elsewhere =
            atrium::BrowserController::FavoriteTabElsewhere(controller,
                                                            favorite_url)) {
      VLOG(1) << "Atrium: favourite moved its tab " << *elsewhere
              << " in from another window";
      atrium::BrowserController::ActivateTab(controller, *elsewhere);
      return;
    }
  }
  // Nothing live to return to, so open it once and claim it - the claim is
  // what makes the NEXT click find it rather than stack another copy.
  OpenFavoriteTab(favorite_url);
}

bool AtriumBrowserWindow::ActivateTabWithUrl(const std::string& url) {
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return false;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && contents->GetVisibleURL().spec() == url) {
      model->ActivateTabAt(i);
      return true;
    }
  }
  return false;
}

void AtriumBrowserWindow::OpenOrActivateUrl(const std::string& url) {
  if (ActivateTabWithUrl(url)) {
    return;
  }
  // Not live: reopen it. Validates the scheme and refuses an unrealised
  // window - see OpenSidebarColdTab's doc.
  OpenSidebarColdTab(url, /*foreground=*/true);
}

void AtriumBrowserWindow::OpenOrActivateFavorite(
    const std::string& favorite_url) {
  ActivateFavoriteRow(favorite_url, std::string());
}

void AtriumBrowserWindow::OpenCommandBarMatch(const GURL& target) {
  if (!browser_ || !target.is_valid()) {
    return;
  }
  // A palette match can legitimately point at chrome:// or file://, so the
  // scheme list a restored sidebar row is held to would be wrong here. What
  // stays refused is javascript:, which the omnibox itself refuses to navigate
  // for the same reason.
  if (target.SchemeIs(url::kJavaScriptScheme)) {
    return;
  }
  // A favourite's tab navigated off its site leaves the tile bound to a page
  // it does not name, so only its own site moves it. The claim the tab
  // carries, not its URL - a favourite that redirects has already moved that.
  content::WebContents* displayed = command_bar_target_tab_.get()
                                        ? command_bar_target_tab_.get()
                                        : CurrentlyDisplayedWebContents();
  const std::string favorite_key =
      displayed ? atrium::FavoriteKeyForTab(displayed) : std::string();
  const bool loads_in_the_favourites_tab =
      !favorite_key.empty() &&
      atrium::DecideUrlSubmitTarget(/*active_is_favourite=*/true,
                                    GURL(favorite_key), target) ==
          atrium::SubmitTarget::kSameTab;
  Browser* into = SidebarTargetBrowser();
  // Asked before the tab exists, and ahead of the favourite above: a rule
  // sends the URL to another space, and loading it in the favourite's tab
  // first only means the throttle drags that tab out of the space it is in.
  if (HasRealisedWindow(into) && !atrium::ScopedRoutingBypass::IsActive()) {
    // A command-bar open has no source space, so per-rule exclusions do not
    // apply.
    const std::optional<std::string> space =
        atrium::RouteForURL(target, std::string());
    if (space && atrium::BrowserController::OpenRoutedURL(
                     into, /*source=*/nullptr, *space, target)) {
      return;
    }
  }
  if (loads_in_the_favourites_tab) {
    content::NavigationController::LoadURLParams params(target);
    params.transition_type = ui::PAGE_TRANSITION_TYPED;
    displayed->GetController().LoadURLWithParams(params);
    return;
  }
  // Belt and braces after a tab added to a window with no native
  // host aborts the browser process, so refuse rather than risk it.
  if (!HasRealisedWindow(into)) {
    return;
  }
  if (!favorite_key.empty()) {
    VLOG(1) << "Atrium: command bar opened a new tab beside the favourite's, "
               "tab " << TabId(displayed) << " stays put";
  }
  chrome::AddTabAt(into, target, -1, /*foreground=*/true);
}

void AtriumBrowserWindow::PromoteSurvivingSidebarRow() {
  if (!browser_) {
    return;
  }
  // Resolved the same way the sidebar's rows are (BuildSidebarTabsJSON takes
  // SidebarTargetBrowser), so the row picked below belongs to the strip
  // checked here rather than to a different, swapped-away one.
  Browser* target = SidebarTargetBrowser();
  if (!target || !target->tab_strip_model()->empty()) {
    // A tab arrived while this was queued - the window is not blank after all,
    // and promoting now would open one the user did not ask for.
    return;
  }
  // was CopySidebarModelJSON, which has answered empty in every
  // build since the cut, so this promoted nothing. BuildSidebarTabsJSON is
  // the producer the sidebar itself renders from and resolves through the
  // same SidebarTargetBrowser, so the row picked here is a row the user can
  // see.
  const std::string model = atrium::BuildSidebarTabsJSON(target);
  if (model.empty()) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const base::ListValue* rows = parsed->GetDict().FindList("tabs");
  if (!rows) {
    return;
  }
  // The FIRST remaining row, not the one adjacent to what closed: by the time
  // this runs the closed tab is already out of the model, and sidebar order is
  // the space's own, not the strip's, so there is no index to be adjacent to.
  // With one row left - the common case, since a space's siblings are cold -
  // the two answers are identical anyway.
  for (const base::Value& row : *rows) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* url = dict->FindString("url");
    if (url && !url->empty()) {
      VLOG(1) << "Atrium: promoting surviving sidebar row after the strip "
                 "emptied";
      // Validates the scheme and refuses an unrealised window - see its doc.
      // Foreground: this is the tab the window is left showing.
      OpenSidebarColdTab(*url, /*foreground=*/true);
      return;
    }
  }
}

// every path here says what it did. This function used to return
// silently on all three, so a row that failed to activate looked exactly like
// one that worked: rows inert with nothing
// in the log to say so.
bool AtriumBrowserWindow::ActivateTabWithId(const std::string& tab_id) {
  if (tab_id.empty()) {
    return false;
  }
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return false;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && base::NumberToString(atrium::TabId(contents)) == tab_id) {
      model->ActivateTabAt(i);
      return true;
    }
  }
  return false;
}

void AtriumBrowserWindow::ActivateSidebarTab(const std::string& tab_id) {
  Browser* target = SidebarTargetBrowser();
  if (!target || !target->tab_strip_model()) {
    VLOG(1) << "Atrium: sidebar row " << tab_id
            << " not activated - no target browser";
    return;
  }
  if (ActivateTabWithId(tab_id)) {
    VLOG(1) << "Atrium: sidebar row " << tab_id << " activated";
    return;
  }
  // not in THIS window's strip, but it may be live in another
  // window on this profile. BrowserController::ActivateTab already handles
  // that whole case - it finds the tab across same-profile windows, freezes the
  // losing pane on a desaturated snapshot, and inserts here with ADD_ACTIVE.
  // Before this fell through, that branch had no caller from a sidebar click
  // and the row read as stale instead.
  uint64_t numeric_id = 0;
  if (base::StringToUint64(tab_id, &numeric_id)) {
    if (atrium::BrowserController* controller =
            atrium::BrowserController::ControllerForBrowser(target)) {
      atrium::BrowserController::ActivateTab(controller, numeric_id);
      return;
    }
  }
  // A live row whose id no longer matches any tab in the strip. The cold-row
  // path handles rows that never had one; this is the id going stale.
  VLOG(1) << "Atrium: sidebar row " << tab_id
          << " not activated - no tab in the strip carries that id";
}

void AtriumBrowserWindow::DuplicateSidebarTab(const std::string& tab_id) {
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && base::NumberToString(atrium::TabId(contents)) == tab_id) {
      if (chrome::CanDuplicateTabAt(target, i)) {
        chrome::DuplicateTabAt(target, i);
      }
      return;
    }
  }
}

void AtriumBrowserWindow::CloseSidebarTab(const std::string& tab_id) {
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents && base::NumberToString(atrium::TabId(contents)) == tab_id) {
      atrium::BrowserController* controller =
          atrium::BrowserController::ControllerForBrowser(target);
      if (!controller || !controller->ParkFavoriteTab(i)) {
        model->CloseWebContentsAt(i, TabCloseTypes::CLOSE_USER_GESTURE);
      }
      return;
    }
  }
}

void AtriumBrowserWindow::ShelveFavorite(const std::string& url,
                                         const std::string& title) {
  char* raw = atrium_core_shelf_add("page", title.c_str(), url.c_str(),
                                    /*space_id=*/nullptr, NotesInstantNow());
  if (raw) {
    atrium_core_string_free(raw);
  }
  if (overview_controller_) {
    overview_controller_->RefreshShelfList();
  }
}

void AtriumBrowserWindow::ShelveSidebarTab(const std::string& tab_id) {
  Browser* target = SidebarTargetBrowser();
  TabStripModel* model = target ? target->tab_strip_model() : nullptr;
  if (!model) {
    return;
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents || base::NumberToString(atrium::TabId(contents)) != tab_id) {
      continue;
    }
    const std::string title = base::UTF16ToUTF8(contents->GetTitle());
    const std::string url = contents->GetLastCommittedURL().spec();
    char* raw = atrium_core_shelf_add("page", title.c_str(), url.c_str(),
                                      /*space_id=*/nullptr, NotesInstantNow());
    if (raw) {
      atrium_core_string_free(raw);
    }
    if (overview_controller_) {
      overview_controller_->RefreshShelfList();
    }
    // Dispatched HERE, after the row is really on the shelf, so a
    // shelve that found no tab shows nothing rather than a toast that lies.
    // The title is what the Shelf list itself shows, url when untitled.
    ShowShelfToast(title.empty() ? url : title);
    return;
  }
}

void AtriumBrowserWindow::ShowShelfToast(const std::string& label) {
  if (!sidebar_contents_) {
    return;
  }
  // Quoted rather than interpolated: a page title is attacker-controlled and
  // this executes at browser privilege. Not through JSON.parse, which threw on
  // a bare title and left this toast never appearing.
  const std::u16string script =
      u"window.atriumShelfToast && window.atriumShelfToast(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(label)) + u")";
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

// The core validates the payload, rewrites the document and hands
// back what it cannot do itself: a document has no tabs in it.
//
// ORDER MATTERS. Close first, so a tab the moment did not hold is gone before
// anything is reopened - reopening first would leave the strip briefly holding
// both, and a restore that flashes the union of two states reads as a bug.
// Reopen next: the core has already written those rows COLD, so the open
// adopts the row rather than adding a second one. Activate last, because both
// of the other two move the selection.
void AtriumBrowserWindow::RestoreSidebarSlice(const std::string& payload) {
  if (!browser_) {
    return;
  }
  char* raw = atrium_core_restore_slice(payload.c_str());
  if (!raw) {
    // Refused: unparseable, self-contradicting, or naming a space that does
    // not exist. The document is untouched, so there is nothing to save and
    // nothing to redraw.
    VLOG(1) << "Atrium: rewind restore refused; the window is unchanged";
    return;
  }
  const std::string plan_json(raw);
  atrium_core_string_free(raw);

  const atrium::RestorePlan plan = atrium::ParseRestorePlan(plan_json);
  if (!plan.parsed) {
    // The document has already been rewritten, so this still has to save and
    // redraw - it just cannot do the browser half.
    LOG(WARNING) << "atrium: rewind restore plan did not parse; the document "
                    "was restored but the strip was not";
    ApplySidebarGesture(true);
    return;
  }

  for (uint64_t id : plan.close_tab_ids) {
    CloseSidebarTab(base::NumberToString(id));
  }

  for (const atrium::RestoreReopenStep& row :
       atrium::ReopenRowsForActiveSpace(plan)) {
    // Same funnel as a cold-row click, so the scheme check and the
    // realised-window check hold here too. The URL came from the renderer.
    // Only the row the moment was showing comes up in front; without that
    // the last row reopened would take the selection.
    OpenSidebarColdTab(row.url, row.selected);
  }

  if (plan.select_tab_id) {
    ActivateSidebarTab(base::NumberToString(*plan.select_tab_id));
  }

  ApplySidebarGesture(true);
  // Only here: every refusal above returns before this, so a
  // restore the core declined shows nothing rather than a toast that lies.
  // No argument - the surface described the scope and the time when it sent
  // the payload, and re-deriving that sentence here would be a second place
  // for it to drift.
  if (sidebar_contents_) {
    sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
        u"window.atriumRewindRestored && window.atriumRewindRestored()",
        base::NullCallback());
  }
}

void AtriumBrowserWindow::GlideSidebarToSpace(int index) {
  if (!sidebar_contents_ || index < 0) {
    return;
  }
  // The page answers this itself - the receiver sets the settle target and
  // starts the frame loop, which is what a released swipe does. Guarded on the
  // receiver existing, like every other push here, because the surface may not
  // have finished loading.
  //
  // `index` is a SPACE. The rail counts panes and Overview holds pane 0, so the
  // page adds that offset; sending a pane index from here glides one space the
  // wrong way.
  const std::u16string script = u"window.atriumSwipeTo && atriumSwipeTo(" +
                                base::NumberToString16(index) + u")";
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

void AtriumBrowserWindow::ApplySidebarGesture(bool moved,
                                               base::OnceClosure on_saved) {
  // False means the gesture resolved to no change - a drag that landed where
  // it started, or a row the document no longer has. Nothing to write and
  // nothing to redraw.
  if (!moved) {
    return;
  }
  // The mutation above is in memory. The write blocks and fsyncs, so it goes
  // to the thread pool; the core takes its own lock, so a burst of drags
  // cannot interleave two writers. BLOCK_SHUTDOWN because the gesture most
  // likely to be lost is the last one before quit, which is the whole reason
  // this path exists.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([] { return atrium_core_save() >= 0; }),
      base::BindOnce(
          [](base::OnceClosure on_saved, bool wrote) {
            if (!wrote) {
              LOG(WARNING) << "atrium: a sidebar gesture could not be "
                              "written; it is in memory only and a relaunch "
                              "loses it";
            }
            if (on_saved) {
              std::move(on_saved).Run();
            }
          },
          std::move(on_saved)));
  PushSidebarSnapshot();
}

void AtriumBrowserWindow::PushSidebarSnapshot() {
  if (sidebar_push_pending_) {
    return;
  }
  sidebar_push_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<AtriumBrowserWindow> self) {
            if (self && self->sidebar_push_pending_) {
              self->PushSidebarSnapshotNow();
            }
          },
          weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::FlushSidebarSnapshotForTesting() {
  if (sidebar_push_pending_) {
    PushSidebarSnapshotNow();
  }
}

void AtriumBrowserWindow::PushSidebarSnapshotNow() {
  sidebar_push_pending_ = false;
  AtriumBrowserWindow* presenter =
      atrium::BrowserController::PresentedWindowForBrowser(browser_.get());
  const bool skip = DecideSidebarSnapshot(presenter == this) ==
                    SidebarSnapshotDecision::kSkip;
  // After an in-window swap the presenter's sidebar shows THIS browser's rows,
  // and its late favicons and titles land here. Before the check below: a
  // hidden window never builds a sidebar of its own.
  if (skip && presenter &&
      presenter->SidebarTargetBrowser() == browser_.get()) {
    presenter->PushSidebarSnapshot();
  }
  if (!sidebar_contents_) {
    return;  // SwiftUI arm, or the WebView has not been built yet.
  }
  // apply the stored width once. The slot is built at
  // kDefaultSidebarWidth before the core is open, so this cannot happen at
  // construction; the first push is the earliest point the core has provably
  // answered. Without it a resize persisted and was never seen again, which is
  // the same defect as never persisting it.
  //
  // Skipped while collapsed: a collapse is session state, and expanding a
  // deliberately hidden sidebar on the first push would fight the user.
  //
  // the flag is set only once a width has ACTUALLY been applied.
  // Setting it before the checks marked the job done on a first push that could
  // not do it - the core not open yet, so the read answers 0, or the sidebar
  // collapsed - and the stored width was then lost for the whole launch despite
  // having been read correctly. Retrying costs one ABI read per push, and
  // SetSidebarWidth on a width the slot already has returns early.
  if (!applied_stored_sidebar_width_) {
    const int stored = atrium_core_sidebar_width();
    if (stored > 0 && !IsSidebarCollapsed()) {
      applied_stored_sidebar_width_ = true;
      SetSidebarWidth(stored);
    }
  }
  // a hidden profile's own tab/title/favicon churn used to rebuild
  // this window's sidebar model unconditionally, even though nothing is on
  // screen to show it. SlotPresentationTable is the source of truth for who
  // is presenting; skip the rebuild here and let ActivateWindow's
  // kFirstActivation branch pay for one coalesced rebuild when this window
  // becomes the presenter (see atrium_sidebar_snapshot_gate.h).
  if (skip) {
    return;
  }
  ++sidebar_snapshot_builds_;
  const std::string model =
      atrium::BuildSidebarTabsJSON(SidebarTargetBrowser());
  RequestMissingRowFavicons(model);
  // Empty space_id() means no core was open at Show() time - nothing to
  // verify, so leave it open rather than treat "unknown" as "gone".
  if (space_icon_picker_widget_ && space_icon_picker_widget_->IsShowing() &&
      !space_icon_picker_widget_->space_id().empty() &&
      !SpaceIdStillExists(space_icon_picker_widget_->space_id())) {
    space_icon_picker_widget_->Close(/*return_focus=*/true);
  }
  // The model line above fires for every reader too. This one is the push.
  VLOG(1) << "Atrium: sidebar snapshot pushed, " << model.size() << " bytes";
  // JSON.parse rather than interpolation: tab titles are attacker-controlled
  // and this executes at browser privilege.
  const std::u16string script =
      u"window.atriumSetSidebar && window.atriumSetSidebar(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(model)) + u"))";
  CallSurface(sidebar_contents_.get(), script);
  // The Courtyard is a second, independent WebContents on the same data - a
  // gesture made there or in the docked sidebar must keep both live.
  if (courtyard_widget_ && courtyard_widget_->IsShowing()) {
    courtyard_widget_->PushSnapshot();
  }
}

void AtriumBrowserWindow::NotifySnoozedTabReturned(const std::string& tab_id,
                                                   const std::string& title,
                                                   const std::string& color) {
  if (!sidebar_contents_) {
    return;
  }
  base::DictValue tab;
  tab.Set("id", tab_id);
  tab.Set("title", title);
  tab.Set("color", color);
  std::string json;
  base::JSONWriter::Write(tab, &json);
  // JSON.parse rather than interpolation - title is attacker-controlled
  // (matches PushSidebarSnapshot's own reasoning for the same construct).
  // opts stays empty - atriumTabSnoozeReturned merges in the pushed step.
  const std::u16string script =
      u"window.atriumTabSnoozeReturned && window.atriumTabSnoozeReturned("
      u"JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"), {})";
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

void AtriumBrowserWindow::NotifyTabsArchived(
    const std::vector<std::string>& tab_ids) {
  if (!sidebar_contents_ || tab_ids.empty()) {
    return;
  }
  base::ListValue ids;
  for (const std::string& tab_id : tab_ids) {
    ids.Append(tab_id);
  }
  std::string json;
  base::JSONWriter::Write(ids, &json);
  // JSON.parse rather than interpolation - matches NotifySnoozedTabReturned's
  // reasoning for the same construct.
  const std::u16string script =
      u"window.atriumTabsArchived && window.atriumTabsArchived(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

void AtriumBrowserWindow::CatchUpSidebarForNewPresenter() {
  const bool is_presenter =
      atrium::BrowserController::PresentedWindowForBrowser(browser_.get()) ==
      this;
  if (DecideSidebarPresenterCatchUp(is_presenter) ==
      SidebarPresenterCatchUpDecision::kNothing) {
    return;
  }
  // Idempotent: a no-op if sidebar_contents_ already exists (its own
  // container->children().empty() guard, inside the posted callback).
  // Needed here because construction no longer builds this window's sidebar
  // at all - see the ctor's comment - so the first time any window (shown at
  // birth, or promoted from the hidden pool) needs one is exactly here.
  BuildHtmlSidebar();
  PushSidebarSnapshotNow();
  RestoreSpaceTheme();
}

// The ctor's RestoreSpaceTint cannot do this: the controller has not adopted
// this Browser yet at construction, and the theme lives on the controller.
void AtriumBrowserWindow::RestoreSpaceTheme() {
  if (atrium::BrowserController* controller =
          atrium::BrowserController::ControllerForBrowser(
              SidebarTargetBrowser())) {
    controller->ApplyActiveSpaceTheme(this);
  }
}

void AtriumBrowserWindow::RequestMissingRowFavicons(const std::string& model) {
  // A favourite tile, a restored tab and a pinned tab have no live WebContents,
  // so the reactive per-tab pipeline never produces their icons. The snapshot
  // is the only place the browser learns their URLs.
  Browser* target = SidebarTargetBrowser();
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(target);
  if (!controller) {
    return;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  // Incognito fetches nothing, by decision: an incognito window must not read
  // icons from regular browsing history (see RequestFaviconsForRowURLs). It
  // also shares its base name with its parent, so the loop below would reach
  // the parent's controller.
  if (target->profile()->IsOffTheRecord()) {
    return;
  }
  const base::DictValue& model_dict = parsed->GetDict();
  // Each profile asks its own database and fills its own cache, the one its
  // rail panes read. The presenter's profile goes first.
  const std::string own_id = target->profile()->GetBaseName().AsUTF8Unsafe();
  std::vector<std::string> profile_ids = {own_id};
  if (const base::ListValue* spaces = model_dict.FindList("spaces")) {
    for (const base::Value& space : *spaces) {
      const std::string* id =
          space.is_dict() ? space.GetDict().FindString("profileId") : nullptr;
      if (id && !id->empty() &&
          std::ranges::find(profile_ids, *id) == profile_ids.end()) {
        profile_ids.push_back(*id);
      }
    }
  }
  for (const std::string& id : profile_ids) {
    const bool own = id == own_id;
    Profile* profile = own ? target->profile() : atrium::LoadedProfileForId(id);
    atrium::BrowserController* fetcher =
        own ? controller
            : atrium::BrowserController::LiveControllerForProfile(controller,
                                                                  profile);
    if (!profile || !fetcher) {
      continue;
    }
    atrium::IconCache& icons = atrium::IconCache::UrlFor(profile);
    std::set<std::string> cached;
    const std::vector<std::string> missing = atrium::SelectMissingIconURLs(
        model_dict, id, own_id, [&icons, &cached](const std::string& url) {
          const bool has = icons.Find(url) != nullptr;
          if (has) {
            cached.insert(url);
          }
          return has;
        });
    // Past the cache size a refetch only evicts another visible row.
    const bool fits =
        cached.size() + missing.size() <= atrium::IconCache::kMaxEntries;
    fetcher->RequestFaviconsForRowURLs(missing, fits);
  }
}

void AtriumBrowserWindow::BuildHtmlSidebar() {
  views::View* container = root_view_->sidebar_web_container();
  if (!container) {
    return;  // SwiftUI arm.
  }
  atrium::AtriumChromeContext::PostWhenReady(base::BindOnce(
      [](base::WeakPtr<AtriumBrowserWindow> self) {
        if (!self) {
          return;
        }
        views::View* container = self->root_view_->sidebar_web_container();
        atrium::AtriumChromeContext* context =
            atrium::AtriumChromeContext::GetIfExists();
        if (!container || !context || !container->children().empty()) {
          return;
        }
        // The fragment names the surface. Every sidebar document has the same
        // URL, so devtools and any probe attaching by URL cannot otherwise
        // tell an incognito window's sidebar from the window it opened over.
        const bool is_incognito =
            atrium::IsIncognitoWindow(self->browser_.get());
        std::unique_ptr<content::WebContents> contents =
            context->CreateChromeContents(
                GURL(std::string(chrome::kChromeUIAtriumURL) + "sidebar" +
                     (is_incognito ? "#incognito" : "")));
        if (!contents) {
          return;
        }
        auto web_view =
            std::make_unique<views::WebView>(contents->GetBrowserContext());
        web_view->SetWebContents(contents.get());
        self->sidebar_contents_ = std::move(contents);
        self->AttachToContents(self->sidebar_contents_.get());
        self->sidebar_web_ = container->AddChildView(std::move(web_view));
        // Transparent: the renderer otherwise paints an opaque
        // page background over the window's own backdrop.
        if (content::RenderWidgetHostView* rwhv =
                self->sidebar_contents_->GetRenderWidgetHostView()) {
          rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
        }
        self->sidebar_contents_->SetPageBaseBackgroundColor(
            SK_ColorTRANSPARENT);
        // nothing else ever tells this contents it is on screen -
        // every other UpdateWebContentsVisibility call in this file is for a
        // TAB's contents. A WebContents that was never marked visible
        // throttles frame production, so the first snapshot rendered into a
        // tree that was never painted, and the sidebar stayed blank until some
        // later event forced a frame.
        self->sidebar_contents_->UpdateWebContentsVisibility(
            content::Visibility::VISIBLE);
        self->sidebar_paint_watch_ =
            std::make_unique<SidebarPaintWatch>(self->sidebar_contents_.get());
        container->InvalidateLayout();
      },
      weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::BuildHtmlFoyer() {
  if (!root_view_ || !root_view_->little_chrome_container()) {
    return;  // Not a little window.
  }
  atrium::AtriumChromeContext::PostWhenReady(base::BindOnce(
      [](base::WeakPtr<AtriumBrowserWindow> self) {
        if (!self || !self->root_view_) {
          return;
        }
        views::View* container = self->root_view_->little_chrome_container();
        atrium::AtriumChromeContext* context =
            atrium::AtriumChromeContext::GetIfExists();
        if (!container || !context || !container->children().empty()) {
          return;
        }
        std::unique_ptr<content::WebContents> contents =
            context->CreateChromeContents(
                GURL(std::string(chrome::kChromeUIAtriumURL) + "foyer"));
        if (!contents) {
          return;
        }
        auto web_view =
            std::make_unique<views::WebView>(contents->GetBrowserContext());
        web_view->SetWebContents(contents.get());
        self->foyer_contents_ = std::move(contents);
        self->AttachToContents(self->foyer_contents_.get());
        // The theme fan-out only runs on a tint CHANGE, so a foyer built
        // after the last one would open on the defaults.
        self->PushOverlayTheme(self->foyer_contents_.get());
        container->AddChildView(std::move(web_view));
        // Transparent for the same reason the sidebar needs it: the renderer
        // otherwise paints an opaque page background over the window's own
        // backdrop.
        if (content::RenderWidgetHostView* rwhv =
                self->foyer_contents_->GetRenderWidgetHostView()) {
          rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
        }
        self->foyer_contents_->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
        container->InvalidateLayout();
      },
      weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::PushFoyerSnapshot() {
  if (!foyer_contents_ || !browser_) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  base::DictValue snapshot;
  snapshot.Set("url", active ? active->GetVisibleURL().spec() : std::string());
  snapshot.Set("title",
               active ? base::UTF16ToUTF8(active->GetTitle()) : std::string());
  std::string json;
  base::JSONWriter::Write(snapshot, &json);
  // Logs the LENGTHS, never the text. A little window's URL and title are the
  // browsing data a log file has no business holding - the same posture the
  // command bar's query line takes. Length is enough to tell "the strip was
  // fed" from "the document loaded and got nothing", which is the distinction
  // the first surface inventory got wrong.
  VLOG(1) << "Atrium: foyer snapshot pushed, url length "
          << (active ? active->GetVisibleURL().spec().size() : 0)
          << " title length " << (active ? active->GetTitle().size() : 0);
  // JSON.parse rather than interpolation: the title and the URL come from an
  // external app, and this executes at browser privilege.
  const std::u16string script =
      u"window.atriumSetFoyer && window.atriumSetFoyer(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  CallSurface(foyer_contents_.get(), script);
}

void AtriumBrowserWindow::BuildHtmlAiPanel() {
  views::View* container = root_view_->ai_panel_container();
  if (!container) {
    return;
  }
  atrium::AtriumChromeContext::PostWhenReady(base::BindOnce(
      [](base::WeakPtr<AtriumBrowserWindow> self) {
        if (!self || !self->root_view_) {
          return;
        }
        views::View* container = self->root_view_->ai_panel_container();
        atrium::AtriumChromeContext* context =
            atrium::AtriumChromeContext::GetIfExists();
        if (!container || !context || !container->children().empty()) {
          return;
        }
        std::unique_ptr<content::WebContents> contents =
            context->CreateChromeContents(
                GURL(std::string(chrome::kChromeUIAtriumURL) + "ai-panel"));
        if (!contents) {
          return;
        }
        auto web_view =
            std::make_unique<views::WebView>(contents->GetBrowserContext());
        web_view->SetWebContents(contents.get());
        self->ai_panel_contents_ = std::move(contents);
        self->AttachToContents(self->ai_panel_contents_.get());
        self->ai_panel_web_ = container->AddChildView(std::move(web_view));
        // Transparent for the same reason the sidebar/foyer need it.
        if (content::RenderWidgetHostView* rwhv =
                self->ai_panel_contents_->GetRenderWidgetHostView()) {
          rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
        }
        self->ai_panel_contents_->SetPageBaseBackgroundColor(
            SK_ColorTRANSPARENT);
        container->InvalidateLayout();
      },
      weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::PushAiPanelOnRight() {
  const std::u16string script =
      u"window.atriumSetAiPanelOnRight && atriumSetAiPanelOnRight(" +
      base::UTF8ToUTF16(std::string(sidebar_on_right_ ? "false" : "true")) +
      u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushSidebarOnRight() {
  // Same side, unlike PushAiPanelOnRight's opposite: the Shelf drop zone
  // lives INSIDE the sidebar's own column, which already sits flush against
  // whichever edge the sidebar docks on.
  const std::u16string script =
      u"window.atriumSetSidebarOnRight && atriumSetSidebarOnRight(" +
      base::UTF8ToUTF16(std::string(sidebar_on_right_ ? "true" : "false")) +
      u")";
  CallSurface(sidebar_contents_.get(), script);
}

void AtriumBrowserWindow::PushSidebarBindings() {
  if (!sidebar_contents_) {
    return;
  }
  // Same bindings-map construction and unbound-is-empty rule
  // CommandBarWidget::PushActions/BindingFor use, so a rebind shows the same
  // glyph on both surfaces.
  std::map<std::string, std::string> glyphs_by_action;
  for (const atrium::NativeHotkeyDefault& entry :
       atrium::NativeHotkeyDefaults()) {
    const std::vector<std::string> combos =
        atrium::NativeHotkeyMap::Get()->CombosFor(entry.action);
    glyphs_by_action[std::string(entry.action_id)] =
        combos.empty() ? std::string()
                       : atrium::CommandBarShortcutGlyphs(combos.front());
  }
  // Main world, unlike the isolated-world pushes nearby: those reach the DOM,
  // which worlds share, but this calls a function sidebar.js puts on window,
  // and that is per-world. Isolated, the `&&` guard found nothing and the push
  // was a silent no-op, so no hotkey ever reached a menu item or hover hint.
  CallSurface(
      sidebar_contents_.get(),
      base::UTF8ToUTF16(atrium::SidebarBindingsScript(glyphs_by_action)));
}

void AtriumBrowserWindow::PushSidebarInk() {
  PushGlassInk(sidebar_contents_.get());
}

void AtriumBrowserWindow::PushGlassInk(content::WebContents* contents) {
  if (!contents || !contents->GetPrimaryMainFrame() || !has_backdrop_tint_) {
    return;
  }
  // The tint sits on the desktop through the glass, and a light desktop at a
  // low alpha is what drowned white ink.
  const SkColor seen = atrium::TintOverGlass(
      backdrop_tint_, SkColorGetA(backdrop_tint_), AppearanceBase());
  const SkColor ink = atrium::InkForBackdrop(seen);
  VLOG(1) << "Atrium: glass ink " << (ink == SK_ColorWHITE ? "white" : "black")
          << " on #" << std::hex << (seen & 0xFFFFFF) << std::dec;
  // The selected pill is a white wash. Under black ink it can go bright, the
  // way Arc draws it. Under white ink the same wash would drown the label.
  const char* sel_alpha = ink == SK_ColorWHITE ? ".3" : ".6";
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(base::StringPrintf(
          "document.documentElement?.style.setProperty("
          "'--atrium-glass-ink', '%d %d %d');"
          "document.documentElement?.style.setProperty("
          "'--atrium-sel-alpha', '%s')",
          SkColorGetR(ink), SkColorGetG(ink), SkColorGetB(ink), sel_alpha)),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::PushCardInset(content::WebContents* contents) {
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return;
  }
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(base::StringPrintf(
          "document.documentElement?.style.setProperty("
          "'--atrium-content-edge-inset', '%dpx')",
          ContentCardInset())),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::PushOverlayTheme(content::WebContents* contents) {
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return;
  }
  const SkColor ink = color_utils::GetColorWithMaxContrast(
      SkColorSetA(static_cast<SkColor>(opaque_surround_argb_), SK_AlphaOPAQUE));
  const bool system = overlay_theme_ == atrium::kSystemOverlayAccent;
  if (system && !system_accent_tokens_) {
    system_accent_tokens_ = atrium::SystemOverlayAccentTokens();
  }
  // The slug goes no further when it is the system one: it names no
  // atrium-theme- class, and adding one would clear the tokens' own class.
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(atrium::OverlayThemeScript(
          backdrop_tint_, has_backdrop_tint_, opaque_surround_enabled_,
          opaque_surround_argb_, ink, system ? std::string() : overlay_theme_,
          system ? system_accent_tokens_
                 : std::optional<atrium::OverlayAccentTokens>())),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

// static
void AtriumBrowserWindow::OnSystemAccentChanged() {
  const std::optional<atrium::OverlayAccentTokens> tokens =
      atrium::SystemOverlayAccentTokens();
  VLOG(1) << "Atrium: system accent now "
          << (tokens ? tokens->accent : std::string("(none)"));
  // A copy of the list, not the list - the reason SetWindowAppearance gives.
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    if (window->overlay_theme_ != atrium::kSystemOverlayAccent) {
      continue;
    }
    window->system_accent_tokens_.reset();
    window->PushConfirmQuitPanelTheme();
    window->PushAiPanelInk();
  }
  // Its own push, because the settings window is not a live window here and
  // it is the one surface showing the accent picker while this fires.
  AtriumSettingsWindow::RepaintOverlayTheme();
}

namespace {

// The system accent arrives as the script's strings: hexes, and an "R G B"
// triplet for the ink.
std::optional<atrium::OverlayThemePalette> PaletteFromOverlayTokens(
    const atrium::OverlayAccentTokens& tokens) {
  const std::optional<SkColor> accent = atrium::ParseHexColor(tokens.accent);
  const std::optional<SkColor> paper = atrium::ParseHexColor(tokens.paper);
  const std::optional<SkColor> soft = atrium::ParseHexColor(tokens.soft);
  const std::vector<std::string> ink_parts = base::SplitString(
      tokens.ink, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  int rgb[3] = {0, 0, 0};
  const bool ink_ok =
      ink_parts.size() == 3 && base::StringToInt(ink_parts[0], &rgb[0]) &&
      base::StringToInt(ink_parts[1], &rgb[1]) &&
      base::StringToInt(ink_parts[2], &rgb[2]);
  if (!accent || !paper || !soft || !ink_ok) {
    return std::nullopt;
  }
  return atrium::OverlayThemePalette{
      .accent = *accent,
      .ink = SkColorSetRGB(rgb[0] & 0xFF, rgb[1] & 0xFF, rgb[2] & 0xFF),
      .paper = *paper,
      .soft = *soft,
  };
}

}  // namespace

void AtriumBrowserWindow::PushConfirmQuitPanelTheme() {
  std::optional<atrium::OverlayThemePalette> theme;
  if (overlay_theme_ == atrium::kSystemOverlayAccent) {
    if (!system_accent_tokens_) {
      system_accent_tokens_ = atrium::SystemOverlayAccentTokens();
    }
    if (system_accent_tokens_) {
      theme = PaletteFromOverlayTokens(*system_accent_tokens_);
    }
  } else {
    theme = atrium::BrandBookThemePalette(overlay_theme_);
  }
  atrium::SetConfirmQuitPanelTheme(theme);
  if (root_view_) {
    root_view_->SetSeamAccent(theme ? theme->accent : kSidebarSeamLineAccent);
    PushDevBarFocusRing();
  }
}

void AtriumBrowserWindow::PushAiPanelInk() {
  // every tint change comes through here, and the sidebar's ink
  // follows the same backdrop.
  PushSidebarInk();
  PushSidebarFrostAppearance();
  RepaintFloatingPanel();
  if (command_bar_widget_) {
    PushOverlayTheme(command_bar_widget_->web_contents());
  }
  PushOverlayTheme(sidebar_contents_.get());
  if (PermissionSheetWidget* sheet = LivePermissionSheet()) {
    PushOverlayTheme(sheet->web_contents());
  }
  if (peek_widget_) {
    PushOverlayTheme(peek_widget_->controls_web_contents());
  }
  if (courtyard_widget_) {
    PushOverlayTheme(courtyard_widget_->web_contents());
  }
  if (toast_overlay_widget_) {
    PushOverlayTheme(toast_overlay_widget_->web_contents());
  }
  if (hover_url_widget_) {
    PushOverlayTheme(hover_url_widget_->web_contents());
  }
  if (hint_overlay_widget_) {
    PushOverlayTheme(hint_overlay_widget_->web_contents());
  }
  // The floating address bar draws its own hover hints, locally and on the
  // shared .ov-card, so without this they wear overlay.css's defaults while
  // every other surface wears the space tint.
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    if (bar->contents()) {
      PushOverlayTheme(bar->contents());
    }
  }
  if (foyer_contents_) {
    PushOverlayTheme(foyer_contents_.get());
  }
  // The panel sits on the sidebar's backdrop and reads the sidebar's tokens.
  PushGlassInk(ai_panel_contents_.get());
  PushOverlayTheme(ai_panel_contents_.get());
  // the panel's outer padding is the same gutter the card
  // keeps, so it follows the setting rather than tokens.css's static copy.
  PushCardInset(ai_panel_contents_.get());
}

void AtriumBrowserWindow::PushAiPanelTarget() {
  if (!ai_panel_contents_ || !browser_) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  // No active tab reads as no target - the JS side's sameTarget() treats
  // null the same as "nothing to attach to".
  base::DictValue target;
  if (active) {
    target.Set("tabId", static_cast<double>(TabId(active)));
    target.Set("spaceIndex", WindowSpaceIndex());
    target.Set("pageTitle", base::UTF16ToUTF8(active->GetTitle()));
  }
  std::string json;
  base::JSONWriter::Write(
      active ? base::Value(std::move(target)) : base::Value(), &json);
  // JSON.parse rather than interpolation: the title is page content.
  const std::u16string script =
      u"window.atriumSetAiPanelTarget && atriumSetAiPanelTarget(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelEnabled() {
  const std::u16string script =
      u"window.atriumSetAiPanelEnabled && atriumSetAiPanelEnabled(" +
      base::UTF8ToUTF16(std::string(CurrentAiEnabled() ? "true" : "false")) +
      u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelHotkey() {
  const std::u16string script =
      u"window.atriumSetAiPanelHotkey && atriumSetAiPanelHotkey(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(CurrentAiPanelHotkey())) +
      u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelAutoSummarize() {
  const std::u16string script =
      u"window.atriumSetAiPanelAutoSummarize && "
      u"atriumSetAiPanelAutoSummarize(" +
      base::UTF8ToUTF16(std::string(
          CurrentAiPanelAutoSummarize() ? "true" : "false")) +
      u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelTranslateLanguage() {
  const std::u16string script =
      u"window.atriumSetAiPanelTranslateLanguage && "
      u"atriumSetAiPanelTranslateLanguage(" +
      base::UTF8ToUTF16(
          base::GetQuotedJSONString(CurrentAiPanelTranslateLanguage())) +
      u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::ToggleAiPanel() {
  if (!CurrentAiEnabled() || !ai_panel_contents_) {
    return;
  }
  if (!ai_panel_open_) {
    // Consumed by SetAiPanelOpen once the container has actually widened -
    // RequestFocus() called here instead would race that width change.
    ai_panel_focus_pending_ = true;
  }
  // The panel's own onKeyDown listener never has focus while the user
  // browses, so the same keydown the browsertest fires by hand is dispatched
  // here for a mouse or a native-hotkey caller.
  const std::u16string script =
      u"document.dispatchEvent(new KeyboardEvent('keydown', {key: " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(CurrentAiPanelHotkey())) +
      u"}))";
  ai_panel_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      script, base::NullCallback());
}

void AtriumBrowserWindow::ForgetAiPanelTab(uint64_t tab_id) {
  CallSurface(ai_panel_contents_.get(),
              u"window.atriumForgetAiPanelTab && atriumForgetAiPanelTab(" +
                  base::NumberToString16(tab_id) + u")");
}

void AtriumBrowserWindow::PushSidebarUrl() {
  if (!browser_) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  std::string origin;
  if (active) {
    const url::Origin parsed =
        url::Origin::Create(active->GetLastCommittedURL());
    // Opaque (about:blank, data:, a blank new-tab page) reads as no origin -
    // the field's placeholder covers that case the same as any other.
    if (!parsed.opaque()) {
      origin = parsed.Serialize();
    }
  }
  last_pushed_sidebar_origin_ = origin;
  const std::u16string script =
      u"window.atriumSetSidebarUrl && atriumSetSidebarUrl(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(origin)) + u")";
  CallSurface(sidebar_contents_.get(), script);
}

void AtriumBrowserWindow::PushTabLoading(uint64_t tab_id, bool loading) {
  const std::u16string script =
      u"window.atriumTabLoading && atriumTabLoading(" +
      base::UTF8ToUTF16(
          base::GetQuotedJSONString(base::NumberToString(tab_id))) +
      (loading ? u", true)" : u", false)");
  CallSurface(sidebar_contents_.get(), script);
}

uint64_t AtriumBrowserWindow::ObservedLoadTabId() const {
  content::WebContents* contents =
      load_progress_observer_ ? load_progress_observer_->web_contents()
                              : nullptr;
  return contents ? atrium::TabId(contents) : 0;
}

void AtriumBrowserWindow::PushLoadPhase(uint64_t tab_id,
                                        atrium::LoadPhase phase,
                                        double fraction) {
  if (tab_id == 0) {
    return;
  }
  if (record_load_phase_pushes_for_testing_) {
    load_phase_pushes_for_testing_.emplace_back(phase, fraction);
  }
  const std::u16string script =
      atrium::BuildLoadPhaseScript(tab_id, phase, fraction);
  CallSurface(sidebar_contents_.get(), script);
  // Every bar: a split bar drops a phase for a tab that is not its own.
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->CallScript(script);
  }
}

void AtriumBrowserWindow::BuildHtmlDevBar() {
  if (!root_view_ || !root_view_->dev_bar_container() || !dev_bars_.empty()) {
    return;
  }
  dev_bars_.push_back(std::make_unique<AtriumDevBar>(
      this, root_view_->dev_bar_container(), 0));
  dev_bars_.front()->Build();
}

content::WebContents* AtriumBrowserWindow::DevBarContents() const {
  return dev_bars_.empty() ? nullptr : dev_bars_.front()->contents();
}

content::WebContents* AtriumBrowserWindow::DevBarSender() const {
  return dev_bar_sender_ ? dev_bar_sender_->contents() : DevBarContents();
}

content::WebContents* AtriumBrowserWindow::DevBarTab(size_t index) {
  if (!IsSplit()) {
    return CurrentlyDisplayedWebContents();
  }
  AtriumPaneContainerView* panes = root_view_->pane_container();
  return index < panes->pane_count()
             ? panes->PaneAt(index)->web_view()->web_contents()
             : nullptr;
}

content::WebContents* AtriumBrowserWindow::NavTargetWebContents() {
  return dev_bar_sender_ && IsSplit() ? DevBarTab(dev_bar_sender_->index())
                                      : CurrentlyDisplayedWebContents();
}

void AtriumBrowserWindow::OnDevBarSurfaceMessage(AtriumDevBar* bar,
                                                 std::string_view name,
                                                 const base::ListValue& args) {
  base::AutoReset<raw_ptr<AtriumDevBar>> sender(&dev_bar_sender_, bar);
  OnSurfaceMessage(name, args);
}

void AtriumBrowserWindow::OnDevBarTabNavigated() {
  PushDevBarState();
  PushNavAbility();
}

AtriumOverlayShadowLayer* AtriumBrowserWindow::EnsureOverlayShadowLayer() {
  if (!overlay_shadow_layer_) {
    overlay_shadow_layer_ =
        std::make_unique<AtriumOverlayShadowLayer>(widget_.get());
  }
  return overlay_shadow_layer_.get();
}

void AtriumBrowserWindow::RegisterOverlayShadow(
    const std::string& surface_id,
    const gfx::Rect& rect_in_screen,
    const AtriumOverlaySurfaceShadow& shadow) {
  EnsureOverlayShadowLayer()->Register(surface_id, rect_in_screen, shadow);
}

void AtriumBrowserWindow::UnregisterOverlayShadow(
    const std::string& surface_id) {
  if (overlay_shadow_layer_) {
    overlay_shadow_layer_->Unregister(surface_id);
  }
}

void AtriumBrowserWindow::SetOverlayShadowAway(const std::string& surface_id,
                                               bool away,
                                               const gfx::Vector2d& shift,
                                               base::TimeDelta duration) {
  // Ensure, not a null check: an arriving surface sends its shadow away BEFORE
  // registering it, and Register is what builds the layer. A null check dropped
  // that for the first shadow of the session, which then came up solid.
  EnsureOverlayShadowLayer()->SetSurfaceAway(surface_id, away, shift, duration);
}

AtriumSidebarFrostLayer* AtriumBrowserWindow::EnsureSidebarFrostLayer() {
  if (!sidebar_frost_layer_) {
    // Ensured first, every time, so the frost can always insert itself
    // directly below the shadow layer's view instead of depending on which
    // of the two happened to exist first.
    AtriumOverlayShadowLayer* shadow = EnsureOverlayShadowLayer();
    sidebar_frost_layer_ = std::make_unique<AtriumSidebarFrostLayer>(
        widget_.get(), shadow->view());
    // The layer is built on the first reveal, long after the theme landed, so
    // it has no push of its own to wait for.
    PushSidebarFrostAppearance();
  }
  return sidebar_frost_layer_.get();
}

void AtriumBrowserWindow::PushSidebarFrostAppearance() {
  if (!sidebar_frost_layer_) {
    return;
  }
  const SkColor seen =
      has_backdrop_tint_
          ? atrium::TintOverGlass(backdrop_tint_, SkColorGetA(backdrop_tint_),
                                  AppearanceBase())
          : AppearanceBase();
  sidebar_frost_layer_->SetDarkBackdrop(atrium::InkForBackdrop(seen) ==
                                        SK_ColorWHITE);
}

void AtriumBrowserWindow::SetSidebarFrostFrame(
    const gfx::Rect& rect_in_screen, float corner_radius) {
  // a kill switch, not a test hook - the frost has no user-facing
  // setting of its own to fall back on if it misbehaves on some machine.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "atrium-disable-sidebar-frost")) {
    return;
  }
  EnsureSidebarFrostLayer()->SetFrame(rect_in_screen, corner_radius);
}

void AtriumBrowserWindow::AnimateSidebarFrostHide(base::TimeDelta duration,
                                                  double slide_dx) {
  if (sidebar_frost_layer_) {
    sidebar_frost_layer_->AnimateHide(duration, slide_dx);
  }
}

void AtriumBrowserWindow::AnimateSidebarFrostShow(base::TimeDelta duration,
                                                  double slide_dx) {
  if (sidebar_frost_layer_) {
    sidebar_frost_layer_->AnimateShow(duration, slide_dx);
  }
}

bool AtriumBrowserWindow::SidebarFrostSlidingForTesting() const {
  return sidebar_frost_layer_ && sidebar_frost_layer_->sliding_for_testing();
}

double AtriumBrowserWindow::SidebarFrostSlideDxForTesting() const {
  return sidebar_frost_layer_ ? sidebar_frost_layer_->slide_dx_for_testing()
                              : 0;
}

atrium::AtriumWindowPlatform::ControlProbe
AtriumBrowserWindow::ProbeWindowControlsForTesting() const {
  return window_platform_ ? window_platform_->ProbeWindowControlsForTesting()
                          : atrium::AtriumWindowPlatform::ControlProbe();
}

void AtriumBrowserWindow::HideSidebarFrost() {
  if (sidebar_frost_layer_) {
    sidebar_frost_layer_->Hide();
  }
}

void AtriumBrowserWindow::PushDevBarShadow() {
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->PushShadow(devbar_shadow_spec_ ? &*devbar_shadow_spec_ : nullptr,
                    devbar_away_shift_, devbar_away_duration_);
  }
}

void AtriumBrowserWindow::OnViewLayoutInvalidated(views::View* observed_view) {
  if (devbar_shadow_reapply_pending_) {
    return;
  }
  devbar_shadow_reapply_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<AtriumBrowserWindow> self) {
            if (!self) {
              return;
            }
            self->devbar_shadow_reapply_pending_ = false;
            self->PushDevBarShadow();
          },
          weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::ToggleDevBarMode() {
  dev_mode_on_ = !dev_mode_on_;
  PushDevBarState();
}

namespace {

// "#rrggbbaa" - the shape atriumSetDevBarFocused's receiver expects.
std::string DevBarRingColorHex(SkColor color) {
  return base::StringPrintf("#%02x%02x%02x%02x", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color),
                            SkColorGetA(color));
}

}  // namespace

void AtriumBrowserWindow::PushDevBarFocusRing() {
  if (!root_view_) {
    return;
  }
  AtriumPaneContainerView* panes = root_view_->pane_container();
  const bool split = IsSplit();
  const std::string ring =
      split ? DevBarRingColorHex(atrium::SidebarSeamLineColor(
                  panes->ActivePaneRingAccent(), 1.f))
            : std::string();
  for (size_t i = 0; i < dev_bars_.size(); ++i) {
    const bool focused =
        split && i == static_cast<size_t>(panes->active_pane_index());
    dev_bars_[i]->PushFocused(focused, ring);
  }
}

void AtriumBrowserWindow::PushDevBarState() {
  if (!browser_ || !root_view_) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  const GURL url = active ? active->GetLastCommittedURL() : GURL();
  // Lab parity (isInsecure/devBarShownFor): localhost always gets the devbar,
  // on top of the window's own Cmd+Shift+D toggle. url.host() rather than the
  // lab's string-prefix hack, since GURL already strips the port for us.
  // A bar over no tab has no url to show and no page for its buttons to act
  // on, so Developer Mode alone is not enough to put it up.
  const bool split = IsSplit();
  // split, every pane wears a bar whatever its page or the mode.
  const bool shown =
      split || (active && (dev_mode_on_ || url.host() == "localhost"));
  // Unlike `shown`, this excludes a split-forced bar - only the user's own
  // toggle duplicates the sidebar's own address row.
  const bool own_bar_shown =
      active && (dev_mode_on_ || url.host() == "localhost");
  AtriumPaneContainerView* panes = root_view_->pane_container();
  const size_t count =
      dev_bars_.empty() || !shown ? 0 : split ? panes->pane_count() : 1;
  while (dev_bars_.size() < count) {
    dev_bars_.push_back(std::make_unique<AtriumDevBar>(
        this, panes->AddDevBarContainer(), dev_bars_.size()));
    dev_bars_.back()->Build();
  }
  const size_t was_shown_count = shown_dev_bar_count_;
  const bool was_shown = was_shown_count > 0;
  panes->SetDevBarsShown(count);
  shown_dev_bar_count_ = count;
  if (count != was_shown_count && (count > 1 || was_shown_count > 1)) {
    VLOG(1) << "Atrium: pane devbars " << count;
  }
  if (sidebar_contents_) {
    // First arg hides the sidebar's own #sbUrl (own_bar_shown only), second
    // covers the ext-strip/nav handoff, which a split-forced bar shares too.
    const std::u16string script =
        u"window.atriumSetDevBarShown && atriumSetDevBarShown(" +
        base::UTF8ToUTF16(std::string(own_bar_shown ? "true" : "false")) +
        u", " + base::UTF8ToUTF16(std::string(shown ? "true" : "false")) +
        u")";
    CallSurface(sidebar_contents_.get(), script);
  }
  if (devbar_scroll_.Reset()) {
    PushDevBarAway(false);
  }
  // The devBarReady handshake lands here, so this is where a page that has
  // just been built learns the pin it was saved with.
  ApplyDevBarPinned();
  // Only on the flip: every other push here is a navigation, and replaying the
  // descent on one would drop the bar again mid-read. PushDevBarShadow reads
  // the flag, so the entrance also survives a bar shown before its page loads.
  // Split bars are static, so only the single bar descends.
  const bool enters = shown && !was_shown && !split;
  if (enters) {
    dev_bars_.front()->SetEntrancePending();
  }
  for (size_t i = 0; i < dev_bars_.size(); ++i) {
    AtriumDevBar* bar = dev_bars_[i].get();
    if (i >= count) {
      bar->SetTab(nullptr);
      continue;
    }
    content::WebContents* tab = split ? DevBarTab(i) : active;
    bar->SetTab(split ? tab : nullptr);
    const GURL bar_url = tab ? tab->GetLastCommittedURL() : GURL();
    bar->CallScript(
        u"window.atriumSetDevBarUrl && atriumSetDevBarUrl(" +
        base::UTF8ToUTF16(base::GetQuotedJSONString(bar_url.spec())) + u")");
    // Every load phase reaches every bar. Outside a split the one bar takes
    // them all, as it always has.
    bar->CallScript(
        u"window.atriumSetDevBarTab && atriumSetDevBarTab(" +
        base::UTF8ToUTF16(base::GetQuotedJSONString(
            split && tab ? base::NumberToString(TabId(tab)) : std::string())) +
        u")");
    if (enters) {
      bar->CallScript(u"window.atriumDevBarEnter && atriumDevBarEnter()");
    }
  }
  PushDevBarFocusRing();
}

void AtriumBrowserWindow::PushAddressRowHidden(bool hidden) {
  const std::u16string script =
      u"window.atriumSetAddressRowHidden && atriumSetAddressRowHidden(" +
      base::UTF8ToUTF16(std::string(hidden ? "true" : "false")) + u")";
  CallSurface(sidebar_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelRowMark(const std::string& tab_id) {
  const std::u16string script =
      u"window.atriumSetAiPanelRow && atriumSetAiPanelRow(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(tab_id)) + u")";
  CallSurface(sidebar_contents_.get(), script);
}

void AtriumBrowserWindow::PushCommandBarNewTabPending(bool pending) {
  const std::u16string script =
      u"window.atriumSetCommandBarNewTabPending && "
      u"atriumSetCommandBarNewTabPending(" +
      base::UTF8ToUTF16(std::string(pending ? "true" : "false")) + u")";
  CallSurface(sidebar_contents_.get(), script);
}

content::WebContents* AtriumBrowserWindow::ResolveAiPanelTargetContents(
    const std::string& tab_id) {
  if (!browser_) {
    return nullptr;
  }
  TabStripModel* model = browser_->tab_strip_model();
  if (!tab_id.empty()) {
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      if (contents && base::NumberToString(atrium::TabId(contents)) == tab_id) {
        return contents;
      }
    }
  }
  return model->GetActiveWebContents();
}

void AtriumBrowserWindow::HandleAiPanelSummarizeRequested(
    const std::string& tab_id,
    int request_id,
    const std::string& question,
    const std::vector<std::string>& extra_tab_ids) {
  if (!CurrentAiEnabled()) {
    PushAiPanelSummaryError(request_id, "AI is off.");
    return;
  }
  content::WebContents* target = ResolveAiPanelTargetContents(tab_id);
  if (!target) {
    PushAiPanelSummaryError(request_id, question.empty()
                                            ? "No page to summarize."
                                            : "No page to ask about.");
    return;
  }
  // The page on screen leads, so the clamp on the joined text can only eat
  // into the travelled pages behind it and never the one being asked about.
  std::vector<std::string> pages;
  pages.reserve(extra_tab_ids.size() + 1);
  pages.push_back(tab_id);
  for (const std::string& id : extra_tab_ids) {
    if (id != tab_id) {
      pages.push_back(id);
    }
  }
  CollectAiPanelPageText(request_id, question, std::move(pages), 0,
                         std::string());
}

// Walks the pages one at a time rather than in parallel: the order they are
// joined in is what the clamp trims from, so it has to be deterministic.
void AtriumBrowserWindow::CollectAiPanelPageText(
    int request_id,
    std::string question,
    std::vector<std::string> tab_ids,
    size_t index,
    std::string collected) {
  if (index >= tab_ids.size()) {
    OnAiPanelPageTextExtracted(request_id, std::move(question),
                               base::Value(std::move(collected)));
    return;
  }
  // Re-resolved at each hop, never held as a pointer: a tab can close while
  // an earlier page is still being read.
  content::WebContents* target = ResolveAiPanelTargetContents(tab_ids[index]);
  // ResolveAiPanelTargetContents falls back to the active tab for an id it no
  // longer knows, so a travelled tab the user has since closed would hand the
  // model the page on screen a second time rather than nothing.
  const bool is_a_repeat_of_the_page_on_screen =
      index > 0 && target && target == ResolveAiPanelTargetContents(tab_ids[0]);
  if (!target || !target->GetPrimaryMainFrame() ||
      is_a_repeat_of_the_page_on_screen) {
    CollectAiPanelPageText(request_id, std::move(question), std::move(tab_ids),
                           index + 1, std::move(collected));
    return;
  }
  // ExecuteJavaScript() CHECK-crashes outside a WebUI/devtools/about:blank
  // frame (RenderFrameHostImpl::CanExecuteJavaScript) - `target` is an
  // ordinary web page, so this has to run in an isolated world instead.
  target->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.body ? document.body.innerText : ''",
      base::BindOnce(&AtriumBrowserWindow::OnAiPanelOnePageCollected,
                     weak_factory_.GetWeakPtr(), request_id,
                     std::move(question), std::move(tab_ids), index,
                     std::move(collected)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::OnAiPanelOnePageCollected(
    int request_id,
    std::string question,
    std::vector<std::string> tab_ids,
    size_t index,
    std::string collected,
    base::Value page_text) {
  const std::string page =
      page_text.is_string() ? page_text.GetString() : std::string();
  if (!page.empty()) {
    if (!collected.empty()) {
      // The separator kChatInstructions names. Without it the pages read as
      // one document and the model attributes one page's facts to another.
      collected += "\n\n---\n\n";
    }
    collected += page;
  }
  CollectAiPanelPageText(request_id, std::move(question), std::move(tab_ids),
                         index + 1, std::move(collected));
}

// static
AtriumBrowserWindow::AiRouteFacts AtriumBrowserWindow::ComputeAiRouteFacts() {
  AiRouteFacts facts;
  const std::string settings_json =
      atrium::LoadSettingsJSON(atrium::ResolveSettingsFilePath());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_dict()) {
    if (const std::string* model = parsed->GetDict().FindString("aiModel")) {
      facts.ai_model = *model;
    }
    if (const std::string* host = parsed->GetDict().FindString("ollamaHost")) {
      facts.ollama_host = *host;
    }
    if (std::optional<int> port = parsed->GetDict().FindInt("ollamaPort")) {
      facts.ollama_port = *port;
    }
    if (const std::string* model = parsed->GetDict().FindString("ollamaModel");
        model && !model->empty()) {
      facts.ollama_model = *model;
    }
    if (const std::string* url =
            parsed->GetDict().FindString("customEndpointUrl")) {
      facts.custom_endpoint_url = *url;
    }
    if (const std::string* model =
            parsed->GetDict().FindString("customEndpointModel");
        model && !model->empty()) {
      facts.custom_endpoint_model = *model;
    }
    // Zero is the off value the core already normalises a bad number to, so
    // it needs no second range check here.
    facts.custom_endpoint_limits.max_tokens =
        parsed->GetDict().FindInt("customEndpointMaxTokens").value_or(0);
    facts.custom_endpoint_limits.idle_timeout_seconds =
        parsed->GetDict().FindInt("customEndpointTimeoutSeconds").value_or(0);
    // The core already refuses a value outside its bounds, so a number that
    // reaches here is one the user chose.
    facts.page_text_cap = static_cast<size_t>(
        std::max(0, parsed->GetDict().FindInt("aiPageTextCap").value_or(0)));
  }
  if (facts.ai_model == "ollama") {
    facts.model_available =
        atrium_ai::IsOllamaConfigured(facts.ollama_host, facts.ollama_port);
  } else if (facts.ai_model == "onDevice") {
    facts.model_available = atrium_ai::IsOnDeviceSummarizeAvailable();
  } else if (facts.ai_model == "customEndpoint") {
    facts.model_available =
        atrium_ai::IsCustomEndpointConfigured(facts.custom_endpoint_url);
  }
  return facts;
}

void AtriumBrowserWindow::OnAiPanelPageTextExtracted(int request_id,
                                                     std::string question,
                                                     base::Value page_text) {
  // Clamped on the far side, in OnAiPanelAvailabilityChecked: the cap is a
  // settings row, and reading settings is what this hop is for.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
      base::BindOnce(&AtriumBrowserWindow::ComputeAiRouteFacts),
      base::BindOnce(
          &AtriumBrowserWindow::OnAiPanelAvailabilityChecked,
          weak_factory_.GetWeakPtr(), request_id,
          page_text.is_string() ? page_text.GetString() : std::string(),
          std::move(question)));
}

void AtriumBrowserWindow::OnAiPanelAvailabilityChecked(
    int request_id,
    std::string page_text,
    std::string question,
    AiRouteFacts facts) {
  const std::string clamped_page_text =
      atrium::ClampPageTextForModel(page_text, facts.page_text_cap);
  question = atrium::ClampPageTextForModel(question, facts.page_text_cap);
  const bool chat = !question.empty();
  const bool on_device = atrium_ai::SelectSummarizeBackend(facts.ai_model) ==
                         atrium_ai::RouteTarget::kOnDevice;
  switch (atrium::DecideSummarizeAction(
      CurrentAiEnabled(), facts.model_available, clamped_page_text)) {
    case atrium::SummarizeOutcome::kDisabled:
      PushAiPanelSummaryError(request_id, "AI is off.");
      return;
    case atrium::SummarizeOutcome::kUnavailable:
      PushAiPanelSummaryError(
          request_id,
          chat && on_device ? "On-device chat isn't available on this Mac."
                            : atrium_ai::UnavailableSummarizeError(
                                  facts.ai_model));
      return;
    case atrium::SummarizeOutcome::kEmptyPage:
      PushAiPanelSummaryError(request_id,
                              chat ? "Nothing on this page to ask about."
                                   : "Nothing on this page to summarize.");
      return;
    case atrium::SummarizeOutcome::kCallModel:
      break;
  }
  switch (atrium_ai::SelectSummarizeBackend(facts.ai_model)) {
    case atrium_ai::RouteTarget::kOllama:
      atrium_ai::ChatViaOllama(
          facts.ollama_host, facts.ollama_port, facts.ollama_model,
          clamped_page_text, question,
          g_browser_process->system_network_context_manager()
              ->GetURLLoaderFactory(),
          base::BindOnce(&AtriumBrowserWindow::OnAiPanelSummarizeComplete,
                         weak_factory_.GetWeakPtr(), request_id),
          base::BindRepeating(&AtriumBrowserWindow::PushAiPanelSummaryChunk,
                              weak_factory_.GetWeakPtr(), request_id));
      return;
    case atrium_ai::RouteTarget::kCustomEndpoint:
      // The key lives only in the keychain - fetch it here,
      // off the UI thread, rather than folding it into AiRouteFacts.
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
          base::BindOnce(&atrium::GetCustomEndpointApiKey),
          base::BindOnce(&AtriumBrowserWindow::OnCustomEndpointApiKeyFetched,
                         weak_factory_.GetWeakPtr(), request_id,
                         clamped_page_text, question,
                         facts.custom_endpoint_url,
                         facts.custom_endpoint_model,
                         facts.custom_endpoint_limits));
      return;
    case atrium_ai::RouteTarget::kOnDevice:
      break;
  }
  if (chat) {
    atrium_ai::AskOnDevice(
        clamped_page_text, question,
        base::BindOnce(&AtriumBrowserWindow::OnAiPanelSummarizeComplete,
                       weak_factory_.GetWeakPtr(), request_id));
    return;
  }
  atrium_ai::SummarizeOnDevice(
      clamped_page_text,
      base::BindOnce(&AtriumBrowserWindow::OnAiPanelSummarizeComplete,
                     weak_factory_.GetWeakPtr(), request_id));
}

void AtriumBrowserWindow::OnCustomEndpointApiKeyFetched(
    int request_id,
    std::string clamped_page_text,
    std::string question,
    std::string custom_endpoint_url,
    std::string custom_endpoint_model,
    atrium_ai::CustomEndpointLimits limits,
    std::optional<std::string> api_key) {
  atrium_ai::ChatViaCustomEndpoint(
      custom_endpoint_url, api_key, clamped_page_text, question,
      g_browser_process->system_network_context_manager()
          ->GetURLLoaderFactory(),
      g_browser_process->system_network_context_manager()->GetContext(),
      base::BindOnce(&AtriumBrowserWindow::OnAiPanelSummarizeComplete,
                     weak_factory_.GetWeakPtr(), request_id),
      custom_endpoint_model, limits,
      base::BindRepeating(&AtriumBrowserWindow::PushAiPanelSummaryChunk,
                          weak_factory_.GetWeakPtr(), request_id));
}

void AtriumBrowserWindow::OnAiPanelSummarizeComplete(
    int request_id,
    std::optional<std::string> summary,
    std::optional<std::string> error) {
  if (summary) {
    PushAiPanelSummary(request_id, *summary);
  } else {
    PushAiPanelSummaryError(request_id, error.value_or("Summarize failed."));
  }
}

void AtriumBrowserWindow::PushAiPanelSummary(int request_id,
                                             const std::string& summary) {
  if (!ai_panel_contents_) {
    return;
  }
  const std::u16string script =
      u"window.atriumSetAiPanelSummary && atriumSetAiPanelSummary(" +
      base::NumberToString16(request_id) + u", " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(summary)) + u")";
  CallSurface(ai_panel_contents_.get(), script);
}

// the answer so far, while it is still arriving. Its own push, not
// PushAiPanelSummary: the panel has to tell a partial answer from a finished
// one, or it clears the loader and the request on the first delta.
void AtriumBrowserWindow::PushAiPanelSummaryChunk(
    int request_id,
    const std::string& so_far,
    const std::string& reasoning_so_far) {
  if (!ai_panel_contents_) {
    return;
  }
  const std::u16string script =
      u"window.atriumSetAiPanelSummaryChunk && atriumSetAiPanelSummaryChunk(" +
      base::NumberToString16(request_id) + u", " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(so_far)) + u", " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(reasoning_so_far)) + u")";
  CallSurface(ai_panel_contents_.get(), script);
}

void AtriumBrowserWindow::PushAiPanelSummaryError(int request_id,
                                                  const std::string& reason) {
  if (!ai_panel_contents_) {
    return;
  }
  const std::u16string script =
      u"window.atriumSetAiPanelSummaryError && atriumSetAiPanelSummaryError(" +
      base::NumberToString16(request_id) + u", " +
      base::UTF8ToUTF16(base::GetQuotedJSONString(reason)) + u")";
  CallSurface(ai_panel_contents_.get(), script);
}

bool AtriumBrowserWindow::AiPanelWebViewHasFocusForTesting() const {
  return ai_panel_web_ && ai_panel_web_->HasFocus();
}

bool AtriumBrowserWindow::ActivePaneWebViewHasFocusForTesting() const {
  return ActivePane() && ActivePane()->web_view()->HasFocus();
}

void AtriumBrowserWindow::SetAiPanelOpen(bool open) {
  // One slot: the extension panel gives way to an AI panel the user opens.
  // A reopen from the page's tab memory has no pending focus, and gives way
  // to the extension panel instead.
  if (open && ExtensionPanelShowing()) {
    if (!ai_panel_focus_pending_) {
      CallSurface(ai_panel_contents_.get(),
                  u"window.atriumCloseAiPanel && atriumCloseAiPanel()");
      return;
    }
    HideExtensionPanel();
  }
  ai_panel_open_ = open;
  SetAiPanelWidth(ai_panel_width_);
  if (open) {
    if (ai_panel_focus_pending_ && ai_panel_web_) {
      ai_panel_web_->RequestFocus();
    }
    ai_panel_focus_pending_ = false;
  } else {
    // Hand focus back to the page, whatever closed the panel - Escape,
    // the close button, or an unpinned tab switch away. Not when the
    // extension panel took the slot, which owns the focus then.
    ai_panel_focus_pending_ = false;
    if (!ExtensionPanelShowing()) {
      FocusWebContentsPane();
    }
  }
}

void AtriumBrowserWindow::SetAiPanelWidth(int width) {
  // Same defensive-clamp posture as SetSidebarWidth: never trust the
  // renderer's own clampWidth() alone. The fraction is this side's ALONE:
  // that document is the panel, so it cannot see the window width.
  const int window_width = root_view_ ? root_view_->width() : 0;
  const int max_width =
      std::min(kAiPanelMaxWidth,
               static_cast<int>(window_width * kAiPanelMaxWidthFraction));
  ai_panel_width_ = std::clamp(width, kAiPanelMinWidth,
                               std::max(kAiPanelMinWidth, max_width));
  if (!root_view_ || !root_view_->ai_panel_container()) {
    return;
  }
  // Every open/close path lands here, including AI being switched off
  // wholesale, which sets ai_panel_open_ directly rather than through
  // SetAiPanelOpen.
  root_view_->pane_container()->SetAiPanelOpen(ai_panel_open_);
  root_view_->ai_panel_container()->SetPreferredSize(
      gfx::Size(ai_panel_open_ ? ai_panel_width_ : 0, 1));
  // Same forced-relayout rationale as SetSidebarWidthLive: a caller reading
  // GetWebContentBoundsInScreen() (or the pane itself painting) right after
  // this call must see the squeeze now, not at the next natural layout pass.
  root_view_->DeprecatedLayoutImmediately();
}

void AtriumBrowserWindow::ShowExtensionPanel(std::unique_ptr<views::View> view,
                                             AtriumSidePanelUI* owner,
                                             const std::string& extension_id) {
  CHECK(root_view_);
  // One slot: the AI panel gives way. Native first, so the width is right in
  // this same layout pass. The page follows through atriumCloseAiPanel.
  if (ai_panel_open_) {
    ai_panel_open_ = false;
    SetAiPanelWidth(ai_panel_width_);
    CallSurface(ai_panel_contents_.get(),
                u"window.atriumCloseAiPanel && atriumCloseAiPanel()");
  }
  // One panel per window: a view from another Browser goes back to it first.
  if (extension_panel_owner_ != owner) {
    ReturnExtensionPanelToOwner();
  }
  extension_panel_owner_ = owner;
  extension_panel_id_ = extension_id;
  last_extension_panel_id_ = extension_id;
  AtriumExtensionPanelView* panel = root_view_->extension_panel();
  panel->SetContent(std::move(view));
  panel->SetExtension(ExtensionPanelProfile(),
                      extensions::ExtensionRegistry::Get(ExtensionPanelProfile())
                          ->enabled_extensions()
                          .GetByID(extension_id),
                      extension_id);
  extension_panel_width_ = StoredExtensionPanelWidth();
  ApplyExtensionPanelLayout();
  const gfx::Point close_point =
      panel->close_button()->GetBoundsInScreen().CenterPoint();
  VLOG(1) << "Atrium: extension panel shown " << extension_id << " width "
          << extension_panel_width_ << " close " << close_point.x() << ","
          << close_point.y() << " grip "
          << panel->grip()->GetBoundsInScreen().CenterPoint().x();
}

std::unique_ptr<views::View> AtriumBrowserWindow::DetachExtensionPanel() {
  if (!ExtensionPanelShowing()) {
    return nullptr;
  }
  std::unique_ptr<views::View> view =
      root_view_->extension_panel()->TakeContent();
  extension_panel_owner_ = nullptr;
  extension_panel_id_.clear();
  ApplyExtensionPanelLayout();
  VLOG(1) << "Atrium: extension panel hidden";
  return view;
}

bool AtriumBrowserWindow::ExtensionPanelShowing() const {
  return root_view_ && root_view_->extension_panel()->content();
}

void AtriumBrowserWindow::ApplyExtensionPanelLayout() {
  AtriumExtensionPanelView* panel = root_view_->extension_panel();
  const bool open = panel->content() != nullptr;
  panel->SetPreferredSize(gfx::Size(open ? extension_panel_width_ : 0, 1));
  root_view_->pane_container()->SetExtensionPanelOpen(open);
  root_view_->ApplyExtensionCardShape();
  // Same forced relayout as SetAiPanelWidth: a caller reading the page bounds
  // right after this call must see the squeeze now.
  root_view_->DeprecatedLayoutImmediately();
}

void AtriumBrowserWindow::BuildExtensionPanelChrome() {
  AtriumExtensionPanelView* panel = root_view_->extension_panel();
  panel->SetCloseCallback(base::BindRepeating(
      &AtriumBrowserWindow::CloseExtensionPanel, weak_factory_.GetWeakPtr()));
  // The root view outlives the grip, its own descendant. The window does not,
  // so the window side binds weakly.
  panel->SetGrip(std::make_unique<AtriumSidebarGrip>(
      base::BindRepeating(&AtriumBrowserWindow::SetExtensionPanelWidthLive,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&AtriumBrowserWindow::CommitExtensionPanelWidth,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&AtriumRootView::CurrentExtensionPanelWidth,
                          base::Unretained(root_view_.get())),
      base::BindRepeating(&AtriumRootView::extension_panel_on_right,
                          base::Unretained(root_view_.get())),
      // The window clamps the maximum, which moves with its width.
      kExtensionPanelMinWidth, std::numeric_limits<int>::max(),
      kExtensionPanelDefaultWidth));
}

void AtriumBrowserWindow::CloseExtensionPanel() {
  if (!ExtensionPanelShowing()) {
    return;
  }
  HideExtensionPanel();
  FocusWebContentsPane();
}

void AtriumBrowserWindow::PresentExtensionPanelFor(Browser* browser) {
  if (extension_panel_owner_ && extension_panel_owner_->browser() != browser) {
    ReturnExtensionPanelToOwner();
  }
  if (AtriumSidePanelUI* incoming = AtriumSidePanelUI::From(browser)) {
    incoming->PresentIn(this);
  }
  // A closed panel's id may be the outgoing profile's, which the incoming one
  // has no entry for.
  last_extension_panel_id_ = extension_panel_id_;
}

void AtriumBrowserWindow::ToggleExtensionPanel() {
  if (ExtensionPanelShowing()) {
    CloseExtensionPanel();
    return;
  }
  if (last_extension_panel_id_.empty()) {
    return;
  }
  // The Browser on screen, which is another profile's after a swap. A last id
  // that profile does not have finds no entry and opens nothing.
  Browser* target =
      BrowserController::VisibleBrowserForBrowser(browser_.get());
  extensions::side_panel_util::ToggleExtensionSidePanel(
      target ? target : browser_.get(), last_extension_panel_id_);
}

bool AtriumBrowserWindow::CanToggleExtensionPanel() const {
  return ExtensionPanelShowing() || !last_extension_panel_id_.empty();
}

void AtriumBrowserWindow::ReturnExtensionPanelToOwner() {
  // Detaching clears the owner, so read it first.
  if (AtriumSidePanelUI* owner = extension_panel_owner_) {
    owner->ReclaimView(DetachExtensionPanel());
  }
}

void AtriumBrowserWindow::HideExtensionPanel() {
  if (extension_panel_owner_) {
    extension_panel_owner_->Close(SidePanelEntryHideReason::kSidePanelClosed,
                                  /*suppress_animations=*/true);
    return;
  }
  // A view a test showed with no owner.
  DetachExtensionPanel();
}

void AtriumBrowserWindow::SetExtensionPanelWidthLive(int width) {
  if (!ExtensionPanelShowing()) {
    return;
  }
  extension_panel_width_ = ClampExtensionPanelWidth(width, GetBounds().width());
  ApplyExtensionPanelLayout();
}

void AtriumBrowserWindow::CommitExtensionPanelWidth(int width) {
  SetExtensionPanelWidthLive(width);
  if (extension_panel_id_.empty()) {
    return;
  }
  extensions::ExtensionPrefs::Get(ExtensionPanelProfile())
      ->UpdateExtensionPref(extension_panel_id_, kExtensionPanelWidthPref,
                            base::Value(extension_panel_width_));
  VLOG(1) << "Atrium: extension panel width " << extension_panel_id_ << " "
          << extension_panel_width_;
}

int AtriumBrowserWindow::StoredExtensionPanelWidth() const {
  int stored = 0;
  const bool found =
      extensions::ExtensionPrefs::Get(ExtensionPanelProfile())
          ->ReadPrefAsInteger(extension_panel_id_, kExtensionPanelWidthPref,
                              &stored);
  return ResolveExtensionPanelWidth(
      found ? std::optional<int>(stored) : std::nullopt, GetBounds().width());
}

Profile* AtriumBrowserWindow::ExtensionPanelProfile() const {
  // After a swap the panel belongs to another profile's Browser, and so do its
  // extension and its stored width.
  return extension_panel_owner_
             ? extension_panel_owner_->browser()->GetProfile()
             : browser_->profile();
}

views::View* AtriumBrowserWindow::ExtensionPanelCardForTesting() {
  return root_view_->extension_panel()->card();
}

bool AtriumBrowserWindow::ExtensionPanelShadowVisibleForTesting() const {
  return root_view_->ExtensionCardShadowVisibleForTesting();
}

void AtriumBrowserWindow::SetRootImmersiveForTesting(bool immersive) {
  root_view_->SetImmersive(immersive);
}

views::View* AtriumBrowserWindow::ExtensionPanelGripForTesting() {
  return root_view_->extension_panel()->grip();
}

views::Button* AtriumBrowserWindow::ExtensionPanelCloseButtonForTesting() {
  return root_view_->extension_panel()->close_button();
}

std::u16string AtriumBrowserWindow::ExtensionPanelTitleForTesting() const {
  return root_view_->extension_panel()->title();
}

SkColor AtriumBrowserWindow::ExtensionPanelGripAccentForTesting() {
  return static_cast<AtriumSidebarGrip*>(root_view_->extension_panel()->grip())
      ->accent();
}

views::View* AtriumBrowserWindow::ExtensionPanelContentForTesting() {
  return root_view_ ? root_view_->extension_panel()->content() : nullptr;
}

gfx::Rect AtriumBrowserWindow::ExtensionPanelBoundsForTesting() const {
  return root_view_->extension_panel()->bounds();
}

std::vector<gfx::Rect> AtriumBrowserWindow::ClientRegionsForTesting() const {
  return root_view_->GetClientRegions(root_view_->GetLocalBounds());
}

bool AtriumBrowserWindow::OpenPeek(content::WebContents* contents,
                                   base::RepeatingClosure dismiss_callback,
                                   base::RepeatingClosure promote_callback,
                                   std::optional<gfx::Point> click_origin_screen) {
  if (!peek_widget_) {
    return false;
  }
  if (peek_widget_->peeked_web_contents() != contents) {
    PushPeekBadge(0, 0, 0);
  }
  peek_widget_->SetWebContents(contents);
  peek_widget_->SetDismissCallback(std::move(dismiss_callback));
  peek_widget_->Show(click_origin_screen);
  if (PeekControlsWidget* controls = peek_widget_->controls()) {
    controls->SetPromoteCallback(std::move(promote_callback));
  }
  return true;
}

void AtriumBrowserWindow::SetPeekOriginChip(const content::WebContents* peek,
                                            const std::u16string& host,
                                            bool secure) {
  if (ShowsPeek(peek)) {
    peek_widget_->SetOriginChip(host, secure);
  }
}

std::u16string AtriumBrowserWindow::PeekOriginChipHostForTesting() const {
  return peek_widget_ ? peek_widget_->OriginChipHostForTesting()
                      : std::u16string();
}

bool AtriumBrowserWindow::PeekLoadingPlaceholderVisibleForTesting() const {
  return peek_widget_ && peek_widget_->LoadingPlaceholderVisibleForTesting();
}

content::WebContents* AtriumBrowserWindow::PeekedWebContentsForTesting() const {
  return peek_widget_ ? peek_widget_->peeked_web_contents() : nullptr;
}

void AtriumBrowserWindow::SimulatePeekScrimPressForTesting() {
  if (!root_view_) {
    return;
  }
  root_view_->pane_container()->active_pane()->scrim_view()->OnMousePressed(
      ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                    ui::EventTimeForNow(), 0, 0));
}

gfx::Rect AtriumBrowserWindow::GetWebContentBoundsInScreen() const {
  // Null during teardown: ~AtriumBrowserWindow nulls root_view_ (and
  // web_view_) BEFORE resetting peek_widget_, and the peek widget observes
  // the parent widget until its own destructor runs - a bounds-changed
  // notification delivered in that gap must not dereference the cleared
  // pointer. Empty rect makes PeekWidget::ApplyParentBounds fall back to the
  // parent window's bounds. Routed through ActivePane() (rather
  // than the cached web_view_ member, which stays pinned to the PRIMARY
  // pane forever) so a peek opened while the secondary pane is active sizes
  // against the right card.
  if (!root_view_) {
    return gfx::Rect();
  }
  // Measured from the pane's parent, not the web view: View::GetBoundsInScreen
  // folds in the pane layer's transform, and under an open peek that is the
  // 0.985 step back, so a card resized then came out 1.5% small.
  AtriumPaneView* pane = ActivePane();
  gfx::Rect rect = pane->web_view()->bounds();
  rect.Offset(pane->bounds().OffsetFromOrigin());
  views::View::ConvertRectToScreen(pane->parent(), &rect);
  return rect;
}

void AtriumBrowserWindow::SetSidebarPeekBadge(const content::WebContents* peek,
                                              uint64_t tab_id,
                                              uint64_t icon_id,
                                              uint64_t rev) {
  if (ShowsPeek(peek)) {
    PushPeekBadge(tab_id, icon_id, rev);
  }
}

bool AtriumBrowserWindow::ShowsPeek(const content::WebContents* peek) const {
  return peek && peek_widget_ && peek_widget_->peeked_web_contents() == peek;
}

void AtriumBrowserWindow::PushPeekBadge(uint64_t tab_id,
                                        uint64_t icon_id,
                                        uint64_t rev) {
  if (tab_id == 0 && peek_badge_tab_id_ == 0) {
    return;
  }
  peek_badge_tab_id_ = tab_id;
  if (!sidebar_contents_) {
    return;
  }
  VLOG(1) << "Atrium: peek badge for tab " << tab_id << " rev " << rev;
  CallSurface(sidebar_contents_.get(),
              u"window.atriumSetPeekBadge && window.atriumSetPeekBadge(" +
                  base::NumberToString16(tab_id) + u", " +
                  base::NumberToString16(icon_id) + u", " +
                  base::NumberToString16(rev) + u")");
}

void AtriumBrowserWindow::ClosePeek(const content::WebContents* peek,
                                    bool animate,
                                    base::OnceClosure on_hidden) {
  if (ShowsPeek(peek)) {
    PushPeekBadge(0, 0, 0);
    peek_widget_->Close(animate, std::move(on_hidden));
    return;
  }
  if (on_hidden) {
    std::move(on_hidden).Run();
  }
}

bool AtriumBrowserWindow::IsPeekShowing() const {
  return peek_widget_ && peek_widget_->IsShowing();
}

void AtriumBrowserWindow::ParkPeek(const content::WebContents* peek) {
  if (ShowsPeek(peek)) {
    PushPeekBadge(0, 0, 0);
    peek_widget_->SetCardVisibleForActiveTab(false);
    peek_widget_->SetWebContents(nullptr);
    peek_widget_->ClearOriginChip();
  }
}

bool AtriumBrowserWindow::RestorePeek(content::WebContents* peek,
                                      base::RepeatingClosure dismiss_callback,
                                      base::RepeatingClosure promote_callback) {
  if (!peek_widget_) {
    return false;
  }
  // A card still animating out of a close is on screen, so only a fresh open
  // brings it back.
  if (peek_widget_->IsShowing()) {
    OpenPeek(peek, std::move(dismiss_callback), std::move(promote_callback),
             std::nullopt);
  } else {
    peek_widget_->SetWebContents(peek);
    peek_widget_->SetDismissCallback(std::move(dismiss_callback));
    if (PeekControlsWidget* controls = peek_widget_->controls()) {
      controls->SetPromoteCallback(std::move(promote_callback));
    }
    peek_widget_->SetCardVisibleForActiveTab(true);
  }
  // ponytail: a peek parked before its first paint, restored after another
  // peek painted, shows no placeholder until it paints.
  if (peek->CompletedFirstVisuallyNonEmptyPaint()) {
    peek_widget_->HideLoadingPlaceholder();
  }
  return true;
}

void AtriumBrowserWindow::DetachPeekWebContents(
    const content::WebContents* peek) {
  if (ShowsPeek(peek)) {
    PushPeekBadge(0, 0, 0);
    peek_widget_->SetWebContents(nullptr);
  }
}

void AtriumBrowserWindow::ReleasePeekClaim(content::WebContents* contents) {
  // See the header doc for the DCHECK this prevents. Deliberately compares
  // pointers rather than unconditionally clearing: a peek showing some OTHER
  // tab must survive a pane swap (a peek stays up across the activation that
  // promotes a DIFFERENT tab).
  DetachPeekWebContents(contents);
}

void AtriumBrowserWindow::HidePeekLoadingPlaceholder(
    const content::WebContents* peek) {
  if (ShowsPeek(peek)) {
    peek_widget_->HideLoadingPlaceholder();
  }
}

void AtriumBrowserWindow::BeginRegionCapture(
    base::OnceCallback<void(const gfx::Rect&)> callback) {
  // Re-entry closes any existing overlay first - one at a time. Resetting
  // (rather than reusing) is safe even mid-drag: AtriumCaptureOverlayWidget's
  // destructor only unregisters its parent-bounds observer, and its own
  // deferred-teardown PostTask (if one is already in flight) becomes a no-op
  // once the weak ptr it captured is invalidated by this reset.
  capture_overlay_widget_.reset();
  // The weak ptr is the deferred-teardown PostTask's receiver:
  // the posted FinishRegionCaptureIfCurrent can outlive a
  // Cmd+W'd window, and an Unretained receiver there is a use-after-free.
  capture_overlay_widget_ = std::make_unique<AtriumCaptureOverlayWidget>(
      this, weak_factory_.GetWeakPtr(), std::move(callback));
}

void AtriumBrowserWindow::FinishRegionCaptureIfCurrent(
    base::WeakPtr<AtriumCaptureOverlayWidget> which) {
  if (which.get() == capture_overlay_widget_.get()) {
    capture_overlay_widget_.reset();
  }
}

void AtriumBrowserWindow::SetPeekScrimVisible(bool visible) {
  // Same teardown-window guard as GetWebContentBoundsInScreen above - see
  // its doc.
  if (!root_view_) {
    return;
  }
  AtriumPaneView* pane = ActivePane();
  pane->scrim_view()->SetVisible(visible);
  // The page steps back a little under the card and returns on close. The
  // shadow follows, or it would show as a halo around the smaller card.
  const float scale = visible ? kPeekBackdropScale : 1.0f;
  const base::TimeDelta duration =
      visible ? kPeekBackdropDuration : kPeekBackdropReturnDuration;
  const gfx::Tween::Type tween =
      visible ? gfx::Tween::EASE_OUT_2 : gfx::Tween::EASE_IN_OUT;
  ScaleLayerAboutCenter(pane->layer(), scale, duration, tween);
  ScaleLayerAboutCenter(root_view_->pane_container()->ShadowLayerFor(pane),
                        scale, duration, tween);
  // Root paint cannot follow the spring per frame, so the glow jumps while
  // the page still covers the strip between the two card rects.
  if (visible) {
    peek_glow_restore_timer_.Stop();
    SetPeekGlowScale(kPeekBackdropScale);
    return;
  }
  peek_glow_restore_timer_.Start(
      FROM_HERE, kPeekBackdropReturnDuration,
      base::BindOnce(&AtriumBrowserWindow::SetPeekGlowScale,
                     weak_factory_.GetWeakPtr(), 1.0f));
}

void AtriumBrowserWindow::SetPeekGlowScale(float scale) {
  // root_view_ is nulled ahead of the timer in teardown.
  if (!root_view_ || peek_glow_scale_ == scale) {
    return;
  }
  peek_glow_scale_ = scale;
  root_view_->SchedulePaint();
}

void AtriumBrowserWindow::PromotePeek(const content::WebContents* peek,
                                      base::OnceClosure on_grown) {
  if (ShowsPeek(peek)) {
    peek_widget_->Promote(std::move(on_grown));
  }
}

void AtriumBrowserWindow::EnsureHintOverlayWidget() {
  if (!hint_overlay_widget_) {
    hint_overlay_widget_ = std::make_unique<HintOverlayWidget>(this);
  }
}

void AtriumBrowserWindow::EnsureToastOverlayWidget() {
  if (!toast_overlay_widget_) {
    toast_overlay_widget_ = std::make_unique<ToastOverlayWidget>(
        this, ToastOverlayWidget::Corner::kTopTrailing);
  }
}

void AtriumBrowserWindow::EnsureHoverUrlWidget() {
  if (!hover_url_widget_) {
    hover_url_widget_ = std::make_unique<ToastOverlayWidget>(
        this, ToastOverlayWidget::Corner::kBottomLeading);
  }
}

void AtriumBrowserWindow::EnsureCommandBarWidget() {
  if (!command_bar_widget_) {
    command_bar_widget_ = std::make_unique<CommandBarWidget>(this);
  }
}

void AtriumBrowserWindow::OpenCommandBar(bool is_startup_open) {
  // Cmd+T/Cmd+L and the anchored open both funnel through here, so a plain
  // open clears a previous anchor click's target - unless this call IS that
  // click's own funnel, mid-stack from the two-arg overload below.
  if (!command_bar_target_tab_pending_) {
    command_bar_target_tab_ = nullptr;
  }
  EnsureCommandBarWidget();
  command_bar_widget_->Show(is_startup_open);
}

void AtriumBrowserWindow::OpenCommandBar(CommandBarOpenReason reason,
                                         const gfx::Rect& anchor_in_window) {
  EnsureCommandBarWidget();
  // Before the controller call, which reaches Show() synchronously.
  command_bar_widget_->SetPendingAnchor(anchor_in_window);
  base::AutoReset<bool> pending(&command_bar_target_tab_pending_, true);
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->OpenCommandBarWithReason(reason);
    return;
  }
  command_bar_widget_->Show();
}

void AtriumBrowserWindow::PushCommandBarMatches(const std::string& json) {
  if (command_bar_widget_) {
    command_bar_widget_->SetMatchesJSON(json);
  }
}

void AtriumBrowserWindow::CloseCommandBar() {
  command_bar_target_tab_ = nullptr;
  if (command_bar_widget_) {
    command_bar_widget_->Close();
  }
}

bool AtriumBrowserWindow::IsCommandBarShowing() const {
  return command_bar_widget_ && command_bar_widget_->IsShowing();
}

void AtriumBrowserWindow::OpenFindBar() {
  if (!find_bar_widget_) {
    find_bar_widget_ = std::make_unique<FindBarWidget>(this);
  }
  find_bar_widget_->Show();
}

void AtriumBrowserWindow::FindNextMatch(bool forward) {
  if (!find_bar_widget_) {
    find_bar_widget_ = std::make_unique<FindBarWidget>(this);
  }
  find_bar_widget_->FindNext(forward);
}

void AtriumBrowserWindow::CloseFindBar() {
  if (find_bar_widget_) {
    find_bar_widget_->Close();
  }
}

bool AtriumBrowserWindow::IsFindBarShowing() const {
  return find_bar_widget_ && find_bar_widget_->IsShowing();
}

void AtriumBrowserWindow::ShowThemeStudio(const std::string& payload_json,
                                          int rail_index,
                                          const gfx::Rect& anchor_in_screen) {
  if (!theme_studio_widget_) {
    theme_studio_widget_ = std::make_unique<ThemeStudioWidget>(this);
  }
  theme_studio_widget_->Show(payload_json, rail_index, anchor_in_screen);
}

void AtriumBrowserWindow::CloseThemeStudio() {
  if (theme_studio_widget_) {
    theme_studio_widget_->Close();
  }
}

bool AtriumBrowserWindow::IsThemeStudioShowing() const {
  return theme_studio_widget_ && theme_studio_widget_->IsShowing();
}

int AtriumBrowserWindow::ThemeStudioSpaceIndex() const {
  return theme_studio_widget_ ? theme_studio_widget_->SpaceIndex() : -1;
}

void AtriumBrowserWindow::ToggleCommandBar() {
  // Cmd+T is a toggle (decided model): open the prompt if it is hidden,
  // close it (returning key to the web content) if it is already up - no tab
  // is created either way. A real tab is created only when the user accepts a
  // palette entry (CommandBarActionRouter). Supersedes the eager
  // create-blank-then-open behavior.
  if (IsCommandBarShowing()) {
    CloseCommandBar();
  } else {
    OpenCommandBar();
  }
}

void AtriumBrowserWindow::EnsureOverviewController() {
  if (!overview_controller_) {
    overview_controller_ = std::make_unique<OverviewController>(this);
  }
}

bool AtriumBrowserWindow::IsOverviewShowing() const {
  return sidebar_overview_open_;
}

void AtriumBrowserWindow::GlideSidebarToOverview() {
  if (!sidebar_contents_) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      u"window.atriumSwipeToOverview && atriumSwipeToOverview()",
      base::NullCallback());
}

void AtriumBrowserWindow::ToggleOverview() {
  // Never touches the active tab or the core's space index: paging the rail
  // back to the active space is what reveals the space the user was on.
  EnsureOverviewController();
  if (sidebar_overview_open_) {
    GlideSidebarToSpace(WindowSpaceIndex());
  } else {
    GlideSidebarToOverview();
  }
}

void AtriumBrowserWindow::OpenOverviewAtSection(const std::string& section) {
  // Glide rather than toggle: a toast click means "take me there", and paging
  // an already-open Overview away is the opposite of that.
  EnsureOverviewController();
  if (!sidebar_overview_open_) {
    GlideSidebarToOverview();
  }
  overview_controller_->SelectSection(section);
}

void AtriumBrowserWindow::EnsureCourtyardWidget() {
  if (!courtyard_widget_) {
    courtyard_widget_ = std::make_unique<CourtyardWidget>(this);
  }
}

void AtriumBrowserWindow::SendToCommandBarHostForTesting(
    std::string_view name) {
  EnsureCommandBarWidget();
  if (command_bar_widget_) {
    command_bar_widget_->OnSurfaceMessage(name, base::ListValue());
  }
}

void AtriumBrowserWindow::OpenCourtyard() {
  EnsureCourtyardWidget();
  courtyard_widget_->Show();
}

void AtriumBrowserWindow::CloseCourtyard() {
  if (courtyard_widget_) {
    courtyard_widget_->Close();
  }
}

bool AtriumBrowserWindow::IsCourtyardShowing() const {
  return courtyard_widget_ && courtyard_widget_->IsShowing();
}

void AtriumBrowserWindow::SetCourtyardOpen(bool open) {
  if (courtyard_open_ == open || !root_view_) {
    return;
  }
  courtyard_open_ = open;
  root_view_->SetCourtyardOpen(open);
  // SetVisible rather than a layer-opacity hide: the page really is off screen
  // here, so the renderer should hear it and stop painting and playing.
  root_view_->pane_container()->SetVisible(!open);
  if (open) {
    // Not InstallRootBackground: the latch above makes that a no-op, and this
    // is the one caller that means to clear rather than to paint.
    root_view_->SetBackground(nullptr);
    root_view_->SchedulePaint();
  } else {
    RestoreRootBackground();
  }
  root_view_->InvalidateLayout();
  // Whole lines rather than one interpolated one: a scenario gate greps the
  // sources for the pattern it waits on, and an assembled string matches none.
  if (open) {
    VLOG(1) << "Atrium: courtyard hid sidebar, pane and root background";
  } else {
    VLOG(1) << "Atrium: courtyard restored sidebar, pane and root background";
  }
}

bool AtriumBrowserWindow::CourtyardHidesContentForTesting() const {
  views::View* slot = root_view_ ? root_view_->sidebar_slot() : nullptr;
  // A window with no slot at all is not evidence that anything was hidden.
  return slot && !slot->GetVisible() &&
         !root_view_->pane_container()->GetVisible() &&
         !root_view_->GetBackground();
}

bool AtriumBrowserWindow::CourtyardContentRestoredForTesting() const {
  views::View* slot = root_view_ ? root_view_->sidebar_slot() : nullptr;
  return slot && slot->GetVisible() &&
         root_view_->pane_container()->GetVisible() &&
         root_view_->GetBackground() != nullptr;
}

bool AtriumBrowserWindow::IsCourtyardZoomAtOrAboveOne() const {
  return courtyard_widget_ && courtyard_widget_->IsShowing() &&
         courtyard_widget_->Zoom() >= 1.0;
}

void AtriumBrowserWindow::ForwardPinchToCourtyard(double delta_y) {
  if (courtyard_widget_) {
    courtyard_widget_->ForwardPinch(delta_y);
  }
}

bool AtriumBrowserWindow::IsPointOverSidebar(
    const gfx::Point& point_in_window) const {
  gfx::Point screen_point = widget_->GetClientAreaBoundsInScreen().origin();
  screen_point += point_in_window.OffsetFromOrigin();
  if (sidebar_overlay_widget_ && sidebar_overlay_widget_->IsShowing()) {
    return sidebar_overlay_widget_->BoundsForTesting().Contains(screen_point);
  }
  views::View* slot = root_view_ ? root_view_->sidebar_slot() : nullptr;
  return slot && slot->GetVisible() &&
        slot->GetBoundsInScreen().Contains(screen_point);
}

void AtriumBrowserWindow::SimulateCtrlWheelDeltaForTesting(
    double delta_y,
    bool over_sidebar) {
  const PinchGestureStep step = StepWheelAccumulator(
      pinch_wheel_accumulator_for_testing_, delta_y, IsCourtyardShowing(),
      over_sidebar, IsCourtyardZoomAtOrAboveOne());
  pinch_wheel_accumulator_for_testing_ = step.next_accumulator;
  switch (step.action) {
    case PinchGestureAction::kOpenCourtyard:
      OpenCourtyard();
      break;
    case PinchGestureAction::kCloseCourtyard:
      CloseCourtyard();
      break;
    case PinchGestureAction::kZoomCanvas:
      ForwardPinchToCourtyard(delta_y);
      break;
    case PinchGestureAction::kNone:
      break;
  }
}

content::WebContents* AtriumBrowserWindow::CourtyardContentsForTesting() const {
  return courtyard_widget_ ? courtyard_widget_->web_contents()
                           : nullptr;
}

std::string AtriumBrowserWindow::CourtyardSnapshotJSON() const {
  return atrium::BuildSidebarTabsJSON(SidebarTargetBrowser());
}

void AtriumBrowserWindow::ApplyCourtyardRoomRect(int space,
                                                 double x,
                                                 double y,
                                                 double w,
                                                 double h) {
  // The core refuses a NaN or out-of-plane value and returns false, and
  // ApplySidebarGesture then pushes nothing - so the view keeps the rect it
  // is already showing rather than snapping to a bad one.
  ApplySidebarGesture(atrium_core_set_space_canvas_rect(space, x, y, w, h));
}

namespace {

// Counts cards as canvas-model.js's sectionRows does, one per folder member.
// `item` is the core's pinned ITEM index, unset for a folder member.
struct CourtyardPinnedCard {
  raw_ptr<const base::DictValue> row = nullptr;
  std::optional<int> item;
};

CourtyardPinnedCard CourtyardPinnedCardAt(const base::ListValue& pinned,
                                          int card) {
  int seen = 0;
  for (size_t item = 0; item < pinned.size(); ++item) {
    const base::DictValue* entry = pinned[item].GetIfDict();
    const std::string* kind = entry ? entry->FindString("kind") : nullptr;
    if (kind && *kind == "folder") {
      const base::ListValue* members = entry->FindList("tabs");
      const int count = members ? static_cast<int>(members->size()) : 0;
      if (card - seen < count) {
        return {(*members)[card - seen].GetIfDict(), std::nullopt};
      }
      seen += count;
      continue;
    }
    if (card == seen) {
      return {entry, static_cast<int>(item)};
    }
    ++seen;
  }
  return {};
}

}  // namespace

void AtriumBrowserWindow::ApplyCourtyardCardX(int space,
                                              const std::string& section,
                                              int index,
                                              double x) {
  if (section == "pinned") {
    std::optional<base::Value> parsed = base::JSONReader::Read(
        CourtyardSnapshotJSON(), base::JSON_PARSE_RFC);
    const base::ListValue* spaces =
        parsed && parsed->is_dict() ? parsed->GetDict().FindList("spaces")
                                    : nullptr;
    const base::DictValue* room =
        spaces && space >= 0 && static_cast<size_t>(space) < spaces->size()
            ? (*spaces)[space].GetIfDict()
            : nullptr;
    const base::ListValue* pinned = room ? room->FindList("pinned") : nullptr;
    const std::optional<int> item =
        pinned && index >= 0 ? CourtyardPinnedCardAt(*pinned, index).item
                             : std::nullopt;
    // The core refuses a folder, so a member card has no offset to keep.
    if (!item) {
      return;
    }
    index = *item;
  }
  // Favourites are per profile and are not tabs, so they have no offset to
  // set. The core refuses any section code but 0 and 1.
  ApplySidebarGesture(atrium_core_set_tab_canvas_x(
      space, section == "pinned" ? 1 : 0, index, x));
}

void AtriumBrowserWindow::ActivateCourtyardCard(int space,
                                                const std::string& section,
                                                int index) {
  if (space < 0 || index < 0) {
    return;
  }
  if (space != WindowSpaceIndex()) {
    atrium::BrowserController* controller =
        atrium::BrowserController::ControllerForBrowser(SidebarTargetBrowser());
    // A space the document does not have is refused rather than clamped, and
    // activating a row of the space still showing would open the wrong page.
    if (!controller || !controller->SelectSpace(space)) {
      VLOG(1) << "Atrium: courtyard card refused - space " << space
              << " could not be selected";
      return;
    }
  }
  // Rebuilt AFTER the switch: only the active space's rows are resolved
  // against the live strip, so the id the page could see was empty.
  std::optional<base::Value> parsed = base::JSONReader::Read(
      CourtyardSnapshotJSON(), base::JSON_PARSE_RFC);
  const base::DictValue* root = parsed ? parsed->GetIfDict() : nullptr;
  const base::ListValue* rows =
      root ? root->FindList(section == "pinned"      ? "pinned"
                            : section == "favorites" ? "favorites"
                                                     : "tabs")
           : nullptr;
  const base::DictValue* row = nullptr;
  if (rows && section == "pinned") {
    row = CourtyardPinnedCardAt(*rows, index).row;
  } else if (rows && static_cast<size_t>(index) < rows->size()) {
    row = (*rows)[index].GetIfDict();
  }
  if (!row) {
    VLOG(1) << "Atrium: courtyard card refused - " << section << " row "
            << index << " is not in the snapshot";
    return;
  }
  const std::string* url = row->FindString("url");
  if (section == "favorites") {
    // A favourite's `id` is its URL, and `tabId` is the live tab - the same
    // pair OnSurface_SidebarFavoriteClicked hands to this.
    const std::string* tab_id = row->FindString("tabId");
    ActivateFavoriteRow(url ? *url : std::string(),
                        tab_id ? *tab_id : std::string());
    return;
  }
  const std::string* id = row->FindString("id");
  if (id && !id->empty()) {
    ActivateSidebarTab(*id);
    return;
  }
  // A cold row: in the space, with no live tab to activate.
  if (url && !url->empty()) {
    OpenSidebarColdTab(*url, /*foreground=*/true);
  }
}

content::WebContents* AtriumBrowserWindow::CurrentlyDisplayedWebContents() {
  // Null during teardown: ~AtriumBrowserWindow nulls root_view_ on a different
  // signal than browser_, so callers' own guards (e.g. CopyCurrentPageLink's
  // !browser_ check) don't cover the gap - same guard as
  // GetWebContentBoundsInScreen.
  if (!root_view_) {
    return nullptr;
  }
  return root_view_->web_view()->web_contents();
}

void AtriumBrowserWindow::FocusDisplayedPage() {
  // A key surface window leaves this widget inactive, so the check alone keeps
  // the keyboard where the surface put it.
  content::WebContents* contents = DisplayedContents();
  if (!contents || !widget_ || !widget_->IsActive()) {
    return;
  }
  // Docked DevTools the user is typing in keeps the keyboard. The page itself
  // is not exempt: its WebView can hold views focus while it is not first
  // responder, which is the state a closing surface leaves behind.
  AtriumPaneView* pane = ActivePane();
  const views::View* focused = widget_->GetFocusManager()->GetFocusedView();
  if (focused && pane->devtools_web_view()->Contains(focused)) {
    return;
  }
  pane->web_view()->RequestFocus();
  // RequestFocus is a no-op on a WebView that already holds views focus, and
  // that says nothing about whether the page is first responder.
  contents->Focus();
}

void AtriumBrowserWindow::RunDamageHealForTesting() {
  if (content::WebContents* contents = DisplayedContents()) {
    ReattachActiveTabForDamageHeal(TabId(contents));
  }
}

views::WebView* AtriumBrowserWindow::PaneWebViewForContents(
    content::WebContents* contents) {
  // Same teardown gap CurrentlyDisplayedWebContents guards: a sad tab can be
  // installed while the window is going away.
  if (!contents || !root_view_) {
    return nullptr;
  }
  AtriumPaneContainerView* container = root_view_->pane_container();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    AtriumPaneView* pane = container->PaneAt(i);
    if (pane && pane->web_view()->web_contents() == contents) {
      return pane->web_view();
    }
  }
  return nullptr;
}

void AtriumBrowserWindow::ReleaseDisplayedContents(
    content::WebContents* contents) {
  if (!contents) {
    return;
  }
  // Reachable from another window, so this no longer runs only
  // on a window the caller knows is alive.
  if (!root_view_) {
    return;
  }
  // Every pane, not just ActivePane(): only the active pane is reachable today
  // (routing freezes hidden windows), but the full scan keeps the single-embed
  // invariant safe against a later tab-active/pane-active desync.
  AtriumPaneContainerView* container = root_view_->pane_container();
  std::vector<AtriumPaneView*> panes;
  for (size_t i = 0; i < container->pane_count(); ++i) {
    panes.push_back(container->PaneAt(i));
  }
  for (AtriumPaneView* pane : panes) {
    if (!pane) {
      // secondary_pane() is null unless is_split().
      continue;
    }
    views::WebView* pane_web_view = pane->web_view();
    if (pane_web_view->web_contents() != contents) {
      // Identity guard: this pane doesn't host `contents` (already released, or
      // never held it). Detaching here would blindly detach unrelated content.
      continue;
    }
    {
      // See suppress_pane_focus_activation_'s doc. SetWebContents(nullptr)
      // doesn't focus, but keep parity with ShowTab's other SetWebContents
      // calls.
      base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                             true);
      // Clean detach WHILE this pane is still `contents`' current views host,
      // so WebContentsViewMac::views_host_ is repointed cleanly by the
      // presenter's subsequent attach instead of being double-nulled. This is
      // the whole point of releasing BEFORE the presenter attach in the swap
      // path.
      TraceClaim(pane_web_view, nullptr, ClaimTag("pane-moveout-freeze"));
      pane_web_view->SetWebContents(nullptr);
    }
    // Match ShowTab's null-contents branch: nothing is shown in this pane now,
    // so hide the WebView (don't paint its default background over root_view_'s
    // tint) and show the empty-window placeholder. A later presentation of this
    // window always re-runs ShowTab, which re-establishes the correct pane
    // state.
    pane_web_view->SetVisible(false);
    pane->empty_card()->SetVisible(true);
  }
}

void AtriumBrowserWindow::ReleaseContentsFromOtherWindows(
    content::WebContents* contents) {
  if (!contents) {
    return;
  }
  // Copied: a release repaints the losing window, which must not be able to
  // resize the list this loop walks.
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* other : windows) {
    if (other != this) {
      other->ReleaseDisplayedContents(contents);
    }
  }
}

AtriumBrowserWindow::ShowTabOutcome AtriumBrowserWindow::ShowTab(
    content::WebContents* contents) {
  // Above the freeze early-return: a swallowed ShowTab still has to end the
  // session, or the old tab travels to another window with one live and
  // Browser's kInserted branch reaches CreateFindBar's CHECK.
  if (find_bar_widget_) {
    find_bar_widget_->EndSessionAndClose();
  }
  if (suppress_showtab_this_turn_) {
    // Swallow the auto-reselect this window's OWN TabStripModel makes when
    // the tab it was displaying gets taken by another Atrium window (see
    // ArmMoveOutFreeze) - both BrowserController's direct call and Chrome's
    // BrowserWindow::OnActiveTabChanged routing land here for the SAME
    // event (ShowTab's "idempotent" design already assumed multiple calls
    // per event); neither should break the freeze or attach `contents` live.
    return ShowTabOutcome::kSwallowed;
  }
  if (is_frozen_) {
    // A REAL subsequent call - either the frozen tab coming back, or the
    // user activating a different tab in this window - always breaks the
    // freeze, whichever `contents` turns out to be.
    is_frozen_ = false;
    root_view_->snapshot_view()->SetVisible(false);
  }

  // One TabStripModel holds every space's tabs, so a close or an auto-reselect
  // can hand this pane a live page from a space the rail is not on. The empty
  // pane a rowless space already shows is the honest answer instead.
  bool refused = false;
  if (BrowserController::TabIsOutsideActiveSpace(contents)) {
    VLOG(1) << "Atrium: pane refused tab " << TabId(contents)
            << ", its space is not the active one";
    // A split's two panes are both live and attached, and the null path below
    // detaches one while covering the other with the empty card.
    if (root_view_->pane_container()->is_split()) {
      return ShowTabOutcome::kRefused;
    }
    contents = nullptr;
    refused = true;
  }

  if (contents) {
    // clear any switch-fade left on this contents' view (see
    // SetContentFade) - whatever path below shows it, it shows opaque. A zero
    // duration also cancels a fade already running on the property.
    window_platform_->FadeNativeView(contents->GetNativeView(), 1.0,
                                     base::TimeDelta());
  }

  AtriumPaneContainerView* container = root_view_->pane_container();
  if (container->is_split() && contents) {
    // Split-aware routing,
    // BEFORE the single-pane path below - a split always has two live,
    // attached tabs (the collapse paths run before any null ShowTab could
    // reach here while split), so non-null `contents` is either one of the
    // two panes (flip which is "active" - no attach work at all) or a THIRD
    // tab (Arc behavior: replaces the active pane's tab, split survives).
    const uint64_t contents_id = TabId(contents);
    if (mirror_contents_ && contents_id != split_primary_tab_id_ &&
        contents_id != split_secondary_tab_id_) {
      // a genuinely different tab - picking another row, or
      // a space switch's own ShowRow call - ends the mirror instead of
      // reaching the Arc-style swap below, meant for an ordinary split.
      EndMirror("navigation");
      return ShowTab(contents);
    }
    // A pane's own WebView names it wherever it sits now. The cached ids
    // below stand for leaves 0 and 1, and an insert past leaf 1 renumbers
    // the leaves under them, so ask the panes before the cache.
    for (size_t i = 0; i < container->pane_count(); ++i) {
      if (container->PaneAt(i)->web_view()->web_contents() == contents) {
        container->SetActivePaneIndex(static_cast<int>(i));
        return FinishShowTab(contents, ShowTabOutcome::kShown);
      }
    }
    if (contents_id == split_primary_tab_id_) {
      container->SetActivePaneIndex(0);
      return FinishShowTab(contents, ShowTabOutcome::kShown);
    }
    if (contents_id == split_secondary_tab_id_) {
      container->SetActivePaneIndex(1);
      return FinishShowTab(contents, ShowTabOutcome::kShown);
    }
    AtriumPaneView* pane = container->active_pane();
    pane->web_view()->SetVisible(true);
    // the peek card must let go before this pane claims the tab.
    ReleasePeekClaim(contents);
    ReleaseContentsFromOtherWindows(contents);
    TraceClaim(pane->web_view(), contents, ClaimTag("pane-split-active"));
    {
      // See suppress_pane_focus_activation_'s doc: SetWebContents(non-null)
      // synchronously calls Focus(), which would otherwise reach
      // OnPaneWebContentsFocused for a purely programmatic attach.
      base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                             true);
      pane->web_view()->SetWebContents(contents);
    }
    pane->empty_card()->SetVisible(false);
    if (widget_->IsVisible()) {
      contents->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
    }
    // Only leaves 0 and 1 have a cached id. A pane past the second is
    // identified by its own WebView, which the swap above just re-pointed.
    if (container->active_pane_index() == 0) {
      split_primary_tab_id_ = contents_id;
    } else if (container->active_pane_index() == 1) {
      split_secondary_tab_id_ = contents_id;
    }
    // Same damage-propagation self-heal as the single-pane path below - see
    // its comment for why.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AtriumBrowserWindow::ReattachActiveTabForDamageHeal,
                       weak_factory_.GetWeakPtr(), contents_id),
        base::Milliseconds(250));
    PushSplitState(/*active=*/true, "op");
    return FinishShowTab(contents, ShowTabOutcome::kShown);
  }

  // Single-pane path - routed through
  // ActivePane() (rather than the cached web_view_ member, which
  // stays pinned to the PRIMARY pane forever and would be wrong if the
  // secondary pane were ever active) so this always touches whichever pane
  // is currently authoritative.
  views::WebView* active_web_view = ActivePane()->web_view();
  // Captured BEFORE the swap below so the outgoing contents can be paired
  // with a HIDDEN cycle after attach - see the hide call below for why.
  content::WebContents* old_contents = active_web_view->web_contents();
  // A null `contents` (empty strip - Cmd+N with no startup tab, or the last
  // tab just went cold) must not paint WebView's own default background over
  // root_view_'s tint; hide it. Non-null re-shows it BEFORE attaching, so
  // the NativeViewHost attach machinery (ui-layer parenting, NSView
  // hierarchy) never runs against a hidden host view.
  if (contents) {
    active_web_view->SetVisible(true);
  }
  // the peek card must let go before this pane claims the tab -
  // otherwise both hold it, the peek's later detach nulls the WebContents'
  // views_host_, and the pane's NEXT swap aborts on ViewsHostableDetach's
  // DCHECK. Runs before the swap below so the release cannot be mistaken for
  // part of the outgoing contents' teardown.
  ReleasePeekClaim(contents);
  ReleaseContentsFromOtherWindows(contents);
  TraceClaim(active_web_view, contents, ClaimTag("pane-active"));
  {
    // See suppress_pane_focus_activation_'s doc.
    base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                           true);
    active_web_view->SetWebContents(contents);
  }
  if (!contents) {
    active_web_view->SetVisible(false);
  }
  // The empty-window placeholder shows exactly when nothing else does.
  root_view_->empty_card()->SetVisible(!contents);
  // Same visibility-guard class as AssertActiveTabVisible (see Show()): a
  // WebContents moved here from ANOTHER window (M2c Arc-parity takeover -
  // BrowserController::ActivateTab's DetachTabAtForInsertion/
  // InsertDetachedTabAt) went HIDDEN on detach, and nothing on the insert
  // path re-asserts VISIBLE - the page keeps its last frame, CSS animations
  // pause and timers throttle (probe-caught: the moved anim page froze while
  // its title kept ticking at the hidden-page rate). Guarded on the widget
  // actually being visible so a hidden pool window can't wrongly wake a
  // renderer.
  if (contents && widget_->IsVisible()) {
    contents->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  }
  // pair the above VISIBLE with a HIDDEN on the outgoing contents,
  // mirroring OnActiveTabChanged's cycle (see its comment) - that paired
  // WasHidden/WasShown is what forces a fresh CompositorFrame and re-wires
  // damage deterministically. Callers that route through OnActiveTabChanged
  // (same-profile switches) already get this pair from there; this covers
  // callers that invoke ShowTab directly and skip it - namely
  // BrowserController's cross-profile in-window swap (kInWindowSwap), which
  // never fires OnActiveTabChanged, so the outgoing pane's contents was never
  // told it went HIDDEN - no paired cycle, just the racy single-shot damage
  // heal above. Guarded exactly like OnActiveTabChanged: skip when
  // `old_contents` is still a live split pane (any of them).
  if (old_contents && old_contents != contents &&
      !IsSplitPaneContents(old_contents)) {
    old_contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
  }
  // Damage-propagation self-heal (M2c Arc-parity, probe-caught): an attach
  // that lands in the same run-loop turn as a detach (cross-window tab
  // takeover; switching back after a tab close) can leave the destination
  // compositor without damage wiring from the contents' surface - the
  // renderer keeps producing frames (CopyFromSurface returns live pixels)
  // but the pane freezes on the frame from the attach instant, only
  // repainting on unrelated window damage. Re-running the attach once the
  // move has settled restores the wiring; a synchronous re-attach inside
  // the same turn does not (probe-tested both ways). The heal re-resolves
  // the tab by id and no-ops if this window's shown tab changed meanwhile.
  if (contents) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AtriumBrowserWindow::ReattachActiveTabForDamageHeal,
                       weak_factory_.GetWeakPtr(), TabId(contents)),
        base::Milliseconds(250));
  }
  return FinishShowTab(
      contents, refused ? ShowTabOutcome::kRefused : ShowTabOutcome::kShown);
}

AtriumBrowserWindow::ShowTabOutcome AtriumBrowserWindow::FinishShowTab(
    content::WebContents* contents,
    ShowTabOutcome outcome) {
  // Before the devtools pass below, which may take focus back for itself. A
  // no-op on an emptied pane.
  FocusDisplayedPage();
  // re-run docked devtools for the now-active tab (attaches it if
  // `contents` already has an open DevToolsWindow docked to it; detaches a
  // stale one if the pane's previous tab had one but this one doesn't).
  UpdateDockedDevtools(contents);
  // Re-point the loading-bar observer at the now-active tab.
  RetargetLoadProgress();
  return outcome;
}

void AtriumBrowserWindow::ShowActiveTabIfPaneIsEmpty(
    uint64_t expected_tab_id) {
  // Anything already attached - including either half of a split - is left
  // alone, so this can only ever fill an empty pane, never re-target a full
  // one.
  if (!browser_ || !root_view_) {
    return;
  }
  AtriumPaneView* pane = ActivePane();
  if (!pane || pane->web_view()->web_contents()) {
    return;
  }
  content::WebContents* active = SidebarActiveTab();
  if (!active || TabId(active) != expected_tab_id) {
    return;
  }
  ShowTab(active);
  // The strip did not change, so no observer re-runs the tint for the page
  // that just came back.
  ApplyPageTint();
  StartAmbilightSampling();
}

bool AtriumBrowserWindow::PaneWebViewShownForTesting() {
  return root_view_ && root_view_->web_view()->GetVisible();
}

void AtriumBrowserWindow::ReattachActiveTabForDamageHeal(
    uint64_t expected_tab_id) {
  // Routed through the active-pane accessor - see its doc - rather than
  // browser_'s tab strip, so this resolves whatever is ACTUALLY displayed in
  // the pane (the single pane outside a split, or the active-pane
  // tracking while split). browser_ is the presenter's own Browser and never
  // reassigned (see its doc), so during a single-window cross-profile swap
  // the pane can display `incoming_contents` from the TARGET
  // profile's tab strip while browser_->tab_strip_model() still only knows
  // about the PRESENTER's own tabs - comparing against it would always miss
  // and silently disable this heal for the swap's main path. Same-profile
  // behavior is unchanged: there the pane's contents and browser_'s active
  // tab are the same WebContents.
  views::WebView* active_web_view = ActivePane()->web_view();
  content::WebContents* current = active_web_view->web_contents();
  if (!current || TabId(current) != expected_tab_id) {
    return;  // shown tab changed since scheduling - a newer heal covers it
  }
  // Direct SetWebContents calls (NOT ShowTab) so the heal never re-schedules
  // itself. See suppress_pane_focus_activation_'s doc for the guard.
  base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                         true);
  // The re-attach takes key from a peek card over the pane, which reads that
  // as a click away and closes. A tab switch that brings a peek back lands here.
  views::Widget* card = peek_widget_ ? peek_widget_->card_widget() : nullptr;
  const bool card_was_key = card && card->IsActive();
  // The detach drops the page's keyboard. Only that comes back: the sidebar
  // or docked devtools may hold it by now.
  const bool page_had_focus = active_web_view->HasFocus();
  TraceClaim(active_web_view, nullptr, ClaimTag("pane-heal-detach"));
  active_web_view->SetWebContents(nullptr);
  TraceClaim(active_web_view, current, ClaimTag("pane-heal-attach"));
  active_web_view->SetWebContents(current);
  RearmPagePointerTracking(current);
  if (widget_->IsVisible()) {
    current->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  }
  if (card_was_key) {
    card->Activate();
  } else if (page_had_focus) {
    FocusDisplayedPage();
  }
}

void AtriumBrowserWindow::ArmMoveOutFreeze(
    content::WebContents* leaving_contents,
    uint64_t tab_id,
    base::OnceClosure on_frozen) {
  // A split-pane tab moving
  // to another window collapses the split with NO freeze - the freeze pane
  // exists for a window that would otherwise show nothing, and a split
  // window still has live content in its OTHER pane. Runs BEFORE every
  // freeze/suppress check below, so ArmMoveOutFreeze never actually arms
  // while a split is active (kept single-pane and unreached).
  // Safe to call ActivateOwnTab synchronously here (unlike the close path -
  // see CollapseSplit's doc): this method runs from BrowserController::
  // ActivateTab, an ops dispatch, never from inside TabStripModelObserver
  // dispatch, and always BEFORE the actual DetachTabAtForInsertion in that
  // caller - forcing this window's active tab to the survivor now means the
  // later detach (by index, unaffected by this reselect) never even
  // triggers its own auto-reselect on the departing tab.
  const uint64_t survivor_id = ClosePaneForTab(tab_id, "moved_out");
  if (survivor_id != 0) {
    // the split panes hold whatever this window DISPLAYS, and after a
    // cross-profile in-window swap that is a different controller's tabs than
    // browser_'s own (the split ops route through PresentedWindowForBrowser, so
    // they land on the presenter's window). `survivor_id` is then an id in the
    // VISIBLE controller's strip, and re-activating it on browser_ would search
    // the wrong - foreign-profile - strip and drop the reselect with a warning.
    // Same resolution the tab-scoped commands use (VisibleBrowserForBrowser),
    // falling back to browser_ when nothing else is visible for the slot, which
    // is the no-swap case and today's behavior.
    Browser* displayed =
        BrowserController::VisibleBrowserForBrowser(browser_.get());
    if (BrowserController* controller = BrowserController::ControllerForBrowser(
            displayed ? displayed : browser_.get())) {
      controller->ActivateOwnTab(survivor_id);
    }
    std::move(on_frozen).Run();
    return;
  }

  if (ActivePane()->web_view()->web_contents() != leaving_contents) {
    // Not this window's currently displayed tab - nothing to freeze.
    std::move(on_frozen).Run();
    return;
  }
  if (pending_freeze_tab_id_ == tab_id) {
    VLOG(1) << "Atrium: move-out freeze already pending for tab " << tab_id;
    return;
  }
  // A2 review fix (finding 4): the split-collapse-no-freeze branch above
  // returns unconditionally whenever this window is split (SplitPartnerTabId
  // is only non-zero while split), so this freeze body must never run while
  // split - regressions here would silently reintroduce the "frozen
  // two-pane snapshot" bug plan section 3 explicitly rules out.
  DCHECK(!IsSplit());

  content::RenderWidgetHostView* rwhv =
      leaving_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    FreezeOn(tab_id, 1.0f, SkBitmap(), std::move(on_frozen));
    return;
  }
  // The pane stays live until the frame lands (see the header). The scale is
  // read now: an empty `output_size` returns a bitmap in DEVICE pixels.
  pending_freeze_tab_id_ = tab_id;
  const float device_scale_factor = rwhv->GetDeviceScaleFactor();
  // No base::BindPostTask wrapper: CopyFromSurface's completion callback is
  // already guaranteed to run on this (UI) sequence - see
  // TabDragController::RequestTabThumbnail for the same idiom.
  rwhv->CopyFromSurface(
      gfx::Rect(), gfx::Size(), base::TimeDelta(),
      base::BindOnce(&AtriumBrowserWindow::OnSnapshotCaptureCompleted,
                     weak_factory_.GetWeakPtr(), tab_id, device_scale_factor,
                     std::move(on_frozen)));
}

void AtriumBrowserWindow::OnSnapshotCaptureCompleted(
    uint64_t tab_id,
    float device_scale_factor,
    base::OnceClosure on_frozen,
    const content::CopyFromSurfaceResult& result) {
  pending_freeze_tab_id_ = 0;
  FreezeOn(tab_id, device_scale_factor,
           result.has_value() ? result->bitmap : SkBitmap(),
           std::move(on_frozen));
}

void AtriumBrowserWindow::FreezeOn(uint64_t tab_id,
                                   float device_scale_factor,
                                   const SkBitmap& bitmap,
                                   base::OnceClosure on_frozen) {
  is_frozen_ = true;
  frozen_tab_id_ = tab_id;
  suppress_showtab_this_turn_ = true;
  // Cleared on the NEXT run-loop turn rather than after a fixed number of
  // ShowTab calls - see the flag's doc.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AtriumBrowserWindow::ClearShowTabSuppression,
                                weak_factory_.GetWeakPtr()));

  ui::ImageModel image;
  if (!bitmap.drawsNothing()) {
    gfx::ImageSkia live =
        gfx::ImageSkia::CreateFromBitmap(bitmap, device_scale_factor);
    color_utils::HSL greyscale_shift = {-1.0, 0.0, -1.0};
    image = ui::ImageModel::FromImageSkia(
        gfx::ImageSkiaOperations::CreateHSLShiftedImage(live, greyscale_shift));
  }
  // Not split (guaranteed by the caller), so ActivePane() is the primary
  // pane. The image is set BEFORE the live view goes, so the two swap in one
  // frame.
  root_view_->snapshot_view()->SetImage(image);
  root_view_->snapshot_view()->SetVisible(true);
  TraceClaim(ActivePane()->web_view(), nullptr, ClaimTag("pane-clear"));
  ActivePane()->web_view()->SetWebContents(nullptr);
  ActivePane()->web_view()->SetVisible(false);

  std::move(on_frozen).Run();
}

bool AtriumBrowserWindow::FrozenSnapshotHasImageForTesting() const {
  return is_frozen_ && !root_view_->snapshot_view()->GetImageModel().IsEmpty();
}


void AtriumBrowserWindow::ClearShowTabSuppression() {
  suppress_showtab_this_turn_ = false;
}

void AtriumBrowserWindow::EnterSplit(content::WebContents* secondary_contents,
                                     SplitOrientation orientation,
                                     double ratio) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!secondary_contents || container->is_split()) {
    LOG(WARNING) << "Atrium: EnterSplit dropped - "
                 << (!secondary_contents ? "null secondary contents"
                                         : "already split");
    return;
  }
  content::WebContents* primary_contents =
      ActivePane()->web_view()->web_contents();
  if (!primary_contents) {
    LOG(WARNING) << "Atrium: EnterSplit dropped - no active tab to use as "
                    "the primary pane";
    return;
  }
  // A2 re-probe fix (found via VM crash repro, not one of the review's
  // numbered findings, but the same defensive-validation posture as every
  // other tab_id-taking op in this file - ResolveTab's "never trust the
  // caller alone" contract): the real Swift entry point only ever offers
  // "Open in Split View" on a row that ISN'T the active tab (plan section
  // 3), but nothing here enforced that. Attaching the SAME WebContents to
  // both panes' WebViews simultaneously corrupts WebContentsViewMac's
  // single-host invariant (views_host_) and FATALs
  // (DCHECK failed: views_host_) on the next detach of either pane -
  // reproduced via a test-hook call with a stale secondary index. No-op
  // instead of trusting the caller.
  if (secondary_contents == primary_contents) {
    LOG(WARNING) << "Atrium: EnterSplit dropped - secondary_contents is "
                    "already the active (primary) tab";
    return;
  }
  if (!container->EnterSplit(orientation, ratio)) {
    return;  // Defensive - is_split() already checked above.
  }
  split_primary_tab_id_ = TabId(primary_contents);
  split_secondary_tab_id_ = TabId(secondary_contents);

  AtriumPaneView* secondary_pane = container->secondary_pane();
  RebuildPaneFocusSubscriptions();
  secondary_pane->web_view()->SetVisible(true);
  {
    // See suppress_pane_focus_activation_'s doc.
    base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                           true);
    // same exclusive-claim rule as ShowTab's attaches.
    ReleasePeekClaim(secondary_contents);
    // same cross-window rule as ShowTab's attaches - a tab still
    // held by another window's pane aborts on DCHECK(views_host_) at that
    // pane's next detach.
    ReleaseContentsFromOtherWindows(secondary_contents);
    TraceClaim(secondary_pane->web_view(), secondary_contents,
               ClaimTag("pane-secondary"));
    secondary_pane->web_view()->SetWebContents(secondary_contents);
  }
  secondary_pane->empty_card()->SetVisible(false);
  if (widget_->IsVisible()) {
    secondary_contents->UpdateWebContentsVisibility(
        content::Visibility::VISIBLE);
  }
  // The primary pane's tab is unchanged (it was already the strip-active
  // tab, per the op's contract) - just re-assert it as the active PANE
  // index, since EnterSplit reset active_pane_index_ to 0 defensively.
  container->SetActivePaneIndex(0);
  PushSplitState(/*active=*/true, "op");
  // re-run docked devtools for both panes' tabs against the new
  // split geometry - primary_contents' pane didn't change tabs, but its
  // pane just got narrower/shorter, and secondary_contents' pane is brand
  // new (built by container->EnterSplit above) and may itself already have
  // an open DevToolsWindow docked to it from before this split existed.
  UpdateDockedDevtools(primary_contents);
  UpdateDockedDevtools(secondary_contents);
  // The active pane (primary) is unchanged, but re-sync the
  // observer/bar to the now-split arrangement.
  RetargetLoadProgress();
}

void AtriumBrowserWindow::OpenNotesBesideThisPage() {
  // Every caller of this entry point wants the active space's own pad -
  // OpenFreestandingNoteBesideThisPage sets current_note_id_ back after this
  // returns, since this runs synchronously up to the PostWhenReady hand-off.
  current_note_id_.clear();
  atrium::AtriumChromeContext::PostWhenReady(base::BindOnce(
      [](base::WeakPtr<AtriumBrowserWindow> self) {
        if (!self || !self->root_view_) {
          return;
        }
        if (self->root_view_->pane_container()->is_split()) {
          return;  // EnterSplit below would only log and decline anyway.
        }
        atrium::AtriumChromeContext* context =
            atrium::AtriumChromeContext::GetIfExists();
        if (!context) {
          return;
        }
        self->notes_contents_ = context->CreateChromeContents(
            GURL(std::string(chrome::kChromeUIAtriumURL) + "notes"));
        if (!self->notes_contents_) {
          return;
        }
        self->AttachToContents(self->notes_contents_.get());
        self->EnterSplit(self->notes_contents_.get(),
                         SplitOrientation::kSideBySide, 0.5);
      },
      weak_factory_.GetWeakPtr()));
}

namespace {

// matches the lab's favHue/ColorForHost hash exactly
// (atrium_sidebar_tabs.cc), so a profile's mirror badge and its favicon tint
// agree.
SkColor MirrorBadgeHue(const std::string& seed) {
  uint32_t hash = 2166136261u;
  for (char c : seed) {
    hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  }
  color_utils::HSL hsl{(hash % 360) / 360.0, 0.5, 0.42};
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

}  // namespace

void AtriumBrowserWindow::MirrorTabToProfile(const std::string& url,
                                             const std::string& profile_id) {
  if (!browser_ || root_view_->pane_container()->is_split()) {
    return;  // EnterSplit below would only log and decline anyway.
  }
  const GURL target(url);
  // is_valid() alone let file:, chrome: and chrome-extension: reach a
  // pane on an arbitrary profile, which the sidebar's other verbs all refuse.
  if (!atrium::IsOrdinarySidebarUrl(target)) {
    LOG(WARNING) << "Atrium: MirrorTabToProfile dropped - url '"
                 << atrium::LogSafeUrl(target)
                 << "' is not http, https or about";
    return;
  }

  Profile* target_profile = nullptr;
  // Matches mirror.js's LOGGED_OUT sentinel exactly - the leading space
  // keeps it distinct from any real profile id crossing this same string arg.
  const bool logged_out = profile_id == " logged-out";
  if (logged_out) {
    target_profile =
        browser_->profile()->GetOriginalProfile()->GetOffTheRecordProfile(
            Profile::OTRProfileID::CreateUnique("atrium-mirror"),
            /*create_if_needed=*/true);
  } else {
    target_profile =
        atrium::BrowserController::ResolveMirrorProfile(profile_id);
  }
  if (!target_profile) {
    LOG(WARNING) << "Atrium: MirrorTabToProfile dropped - profile '"
                 << profile_id << "' did not resolve";
    return;
  }

  mirror_profile_is_otr_ = logged_out;
  mirror_contents_ = content::WebContents::Create(
      content::WebContents::CreateParams(target_profile));
  mirror_contents_->GetController().LoadURL(
      target, content::Referrer(), ui::PAGE_TRANSITION_LINK, std::string());
  EnterSplit(mirror_contents_.get(), SplitOrientation::kSideBySide, 0.5);

  AtriumPaneContainerView* container = root_view_->pane_container();
  // EnterSplit declines with no page in the active pane (empty strip, or a
  // space of only cold rows - the row menu offers Mirror View either way),
  // and secondary_pane() is null until the container really splits.
  if (!container->is_split()) {
    LOG(WARNING) << "Atrium: MirrorTabToProfile dropped - no page showing to "
                    "mirror beside";
    ReleaseMirror();
    ShowTransientConfirmation(u"Open a page to mirror it", /*ok=*/false);
    return;
  }

  // The lab's neutral --mbc default (swipe-lab.html) for a side naming no
  // profile - "You" here, since the primary's profile is the space's to
  // track, not this window's.
  constexpr SkColor kNeutralBadge = SkColorSetRGB(0x5a, 0x64, 0x6e);
  container->primary_pane()->SetMirrorBadge(u"You", kNeutralBadge);
  container->secondary_pane()->SetMirrorBadge(
      logged_out ? u"Logged out" : base::UTF8ToUTF16(profile_id),
      logged_out ? kNeutralBadge : MirrorBadgeHue(profile_id));
}

void AtriumBrowserWindow::EndMirror(const char* reason) {
  if (!mirror_contents_) {
    return;
  }
  // mirror_contents_ is always EnterSplit's secondary (MirrorTabToProfile is
  // EnterSplit's only mirror caller) - keep the primary. FinishCloseSplit
  // releases the mirror itself, and no-ops when already unsplit.
  if (root_view_->pane_container()->is_split()) {
    FinishCloseSplit(/*keep_index=*/0, reason);
    return;
  }
  ReleaseMirror();
}

void AtriumBrowserWindow::ReleaseMirror() {
  if (!mirror_contents_) {
    return;
  }
  Profile* mirror_profile =
      Profile::FromBrowserContext(mirror_contents_->GetBrowserContext());
  const bool was_otr = mirror_profile_is_otr_;
  // Only the primary pane survives a collapse, and no other pane carries a
  // badge.
  root_view_->pane_container()->primary_pane()->ClearMirrorBadge();
  // FinishCloseSplit only detaches the secondary pane's WebView - the
  // WebContents itself is owned here, and the OTR profile is owned by
  // browser_->profile(), so both need their own teardown.
  mirror_contents_.reset();
  mirror_profile_is_otr_ = false;
  if (was_otr) {
    ProfileDestroyer::DestroyOTRProfileWhenAppropriate(mirror_profile);
  }
}

bool AtriumBrowserWindow::HighlightSelectionOnActivePage(
    const std::string& quote) {
  Browser* browser = SidebarTargetBrowser();
  if (!browser) {
    return false;
  }
  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return false;
  }
  const std::string url = contents->GetVisibleURL().spec();
  char* raw = atrium_core_add_marginal(WindowSpaceIndex(), url.c_str(),
                                       quote.c_str(), NotesInstantNow());
  if (!raw) {
    return false;
  }
  const std::string mark_id(raw);
  atrium_core_string_free(raw);
  // The context menu's selection is still live here
  // (nothing has touched the page since the click), so this wraps THAT
  // range rather than re-finding the quote via the re-anchor path.
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildWrapSelectionScript(mark_id), base::NullCallback(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
  // The pad may already be open, in which case EnterSplit declines and only
  // this push makes the new card appear.
  OpenNotesBesideThisPage();
  PushNoteToNotesContents();
  PushSidebarSnapshot();
  return true;
}

void AtriumBrowserWindow::OpenSpaceNoteBesideThisPage(int32_t space_index) {
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(SidebarTargetBrowser())) {
    controller->SelectSpace(space_index);
  }
  OpenNotesBesideThisPage();
}

void AtriumBrowserWindow::OpenFreestandingNoteBesideThisPage(
    const std::string& note_id) {
  OpenNotesBesideThisPage();
  current_note_id_ = note_id;
  PushNoteToNotesContents();
}

void AtriumBrowserWindow::PushNoteToNotesContents() {
  if (!notes_contents_) {
    return;
  }
  char* raw = current_note_id_.empty()
                  ? atrium_core_get_space_note(WindowSpaceIndex())
                  : atrium_core_note_get(current_note_id_.c_str());
  const std::string json = raw ? raw : "null";
  if (raw) {
    atrium_core_string_free(raw);
  }
  // JSON.parse rather than interpolation - same posture as PushFoyerSnapshot:
  // the note body is user-authored text, not a browser-privilege literal.
  const std::u16string script =
      u"window.atriumSetNote && window.atriumSetNote(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  CallSurface(notes_contents_.get(), script);
}

void AtriumBrowserWindow::OnPageScrolled(double delta_y) {
  // Split bars never slide away: several pages scroll at once.
  if (!DevBarContents() || !root_view_ ||
      !root_view_->dev_bar_container()->GetVisible() || IsSplit()) {
    return;
  }
  if (const std::optional<bool> away = devbar_scroll_.OnScroll(delta_y)) {
    PushDevBarAway(*away);
  }
}

void AtriumBrowserWindow::PushDevBarAway(bool away) {
  if (!DevBarContents()) {
    return;
  }
  // The slide is named here because it is devbar.css's, pushed over a hop
  // nothing else reads - a shadow that snaps means it never arrived.
  VLOG(1) << "Atrium: devbar " << (away ? "away" : "back") << ", shadow slides "
          << devbar_away_shift_.y() << "px over "
          << devbar_away_duration_.InMilliseconds() << "ms";
  CallSurface(DevBarContents(),
             away ? u"window.atriumSetDevBarAway && atriumSetDevBarAway(true)"
                  : u"window.atriumSetDevBarAway && atriumSetDevBarAway(false)");
  // The bar slides inside its own document, so its rect here never moves and
  // its shadow would sit on a page with no bar over it. The visibility gate
  // keeps a hide from fading the shadow back in first.
  if (overlay_shadow_layer_ && root_view_ &&
      root_view_->dev_bar_container()->GetVisible()) {
    overlay_shadow_layer_->SetSurfaceAway(dev_bars_.front()->shadow_id(), away,
                                          devbar_away_shift_,
                                          devbar_away_duration_);
  }
}

void AtriumBrowserWindow::ApplyDevBarPinned() {
  const bool pinned = DevBarPinned();
  // A bar pinned while it was away has to come back, or the pin would read as
  // switched on over a bar nobody can see.
  if (const std::optional<bool> away = devbar_scroll_.SetPinned(pinned)) {
    PushDevBarAway(*away);
  }
  if (!DevBarContents()) {
    return;
  }
  // Whole literals, not an interpolated tail: the scenario guard that checks a
  // vlog pattern exists in the source can only find them written out.
  VLOG(1) << (pinned ? "Atrium: devbar pinned" : "Atrium: devbar unpinned");
  for (const std::unique_ptr<AtriumDevBar>& bar : dev_bars_) {
    bar->CallScript(
        pinned ? u"window.atriumSetDevBarPinned && atriumSetDevBarPinned(true)"
               : u"window.atriumSetDevBarPinned && "
                 u"atriumSetDevBarPinned(false)");
  }
}

void AtriumBrowserWindow::ReanchorMarginaliaOnActiveTab() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  const std::string url = active->GetVisibleURL().spec();
  char* raw = atrium_core_marginalia_for_url(WindowSpaceIndex(), url.c_str());
  if (!raw) {
    return;
  }
  const std::string json(raw);
  atrium_core_string_free(raw);
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildReanchorScript(json), base::NullCallback(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::ResetVimPageState() {
  // Hints belong to the browser, so this one is known rather than asked for.
  vim_page_capturing_ = false;
  // A pass whose replies never all arrived would otherwise hold the count from
  // a document that is gone.
  ++vim_hint_generation_;
  vim_hint_pending_ = 0;
  vim_hint_counts_.clear();
  // The mode is asked for, not assumed: a tab being returned to may still hold
  // focus in the field it was left in, and guessing NORMAL would take the next
  // keystroke out of it.
  DrainVimMode();
}

void AtriumBrowserWindow::InstallVimOnActiveTab() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  const std::u16string script = atrium::BuildVimInstallScript();
  active->ForEachRenderFrameHost([&script](content::RenderFrameHost* frame) {
    if (!frame->IsRenderFrameLive()) {
      return;
    }
    frame->ExecuteJavaScriptInIsolatedWorld(script, base::NullCallback(),
                                            ISOLATED_WORLD_ID_CHROME_INTERNAL);
  });
  // After the install, so the drain asks the copy this document just got.
  ResetVimPageState();
}

void AtriumBrowserWindow::SendVimKey(const std::string& character) {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildVimKeyScript(character),
      base::BindOnce(&AtriumBrowserWindow::OnVimKeyRan,
                     weak_factory_.GetWeakPtr(), character),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

// False means the page was in INSERT mode and the cached mode was stale. The
// keystroke belongs in the focused element, so put it back and correct the
// cache.
void AtriumBrowserWindow::OnVimKeyRan(const std::string& character,
                                      base::Value ran) {
  // A bool still reads as V1's answer, so a pak older than this build cannot
  // turn the veto into a swallowed keystroke.
  const std::string answer =
      ran.is_string() ? ran.GetString()
                      : (ran.is_bool() && ran.GetBool() ? "handled" : "veto");
  if (answer == "handled") {
    return;
  }
  if (answer == "copy-url") {
    CopyCurrentPageLink();
    return;
  }
  if (answer == "capture" || answer == "release") {
    vim_page_capturing_ = answer == "capture";
    return;
  }
  if (RunVimBrowserVerb(answer)) {
    return;
  }
  vim_page_normal_mode_ = false;
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  const std::u16string redispatch = base::StrCat(
      {u"(function(el){ if(!el) return; ", u"el.setRangeText ? el.setRangeText(",
       base::UTF8ToUTF16(base::GetQuotedJSONString(character)),
       u", el.selectionStart, el.selectionEnd, 'end') : null; ",
       u"})(document.activeElement)"});
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      redispatch, base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::ShowVimHints() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  // A second f while the first pass is still collecting would otherwise merge
  // two sets of counts and hand two frames the same offset.
  ++vim_hint_generation_;
  vim_hint_counts_.clear();
  std::vector<content::RenderFrameHost*> frames;
  active->ForEachRenderFrameHost([&frames](content::RenderFrameHost* frame) {
    if (frame->IsRenderFrameLive()) {
      frames.push_back(frame);
    }
  });
  vim_hint_pending_ = frames.size();
  const std::u16string script = atrium::BuildVimTargetCountScript();
  for (size_t i = 0; i < frames.size(); ++i) {
    // an ad iframe that dies or navigates mid-pass has its reply
    // dropped, and one missing reply left the pass counting forever.
    frames[i]->ExecuteJavaScriptInIsolatedWorld(
        script,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&AtriumBrowserWindow::OnVimTargetCount,
                           weak_factory_.GetWeakPtr(), vim_hint_generation_,
                           frames[i]->GetGlobalId()),
            base::Value(0)),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }
}

void AtriumBrowserWindow::OnVimTargetCount(int generation,
                                           content::GlobalRenderFrameHostId id,
                                           base::Value count) {
  if (generation != vim_hint_generation_ || vim_hint_pending_ == 0) {
    return;
  }
  vim_hint_counts_.emplace_back(id, count.is_int() ? count.GetInt() : 0);
  if (--vim_hint_pending_ > 0) {
    return;
  }
  int total = 0;
  for (const auto& entry : vim_hint_counts_) {
    total += entry.second;
  }
  int offset = 0;
  for (const auto& entry : vim_hint_counts_) {
    content::RenderFrameHost* frame = content::RenderFrameHost::FromID(entry.first);
    if (frame && frame->IsRenderFrameLive() && entry.second > 0) {
      frame->ExecuteJavaScriptInIsolatedWorld(
          atrium::BuildVimPaintHintsScript(offset, total), base::NullCallback(),
          ISOLATED_WORLD_ID_CHROME_INTERNAL);
    }
    offset += entry.second;
  }
  vim_page_capturing_ = total > 0;
}

void AtriumBrowserWindow::DrainVimMode() {
  // The drain paints the indicator, so with the toggle off it is the one call
  // that would put vim's UI on a page the user never asked for it on.
  if (!CurrentVimModeEnabled()) {
    return;
  }
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildVimModeDrainScript(),
      base::BindOnce(&AtriumBrowserWindow::OnVimModeDrained,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::OnVimModeDrained(base::Value mode) {
  if (mode.is_string()) {
    vim_page_normal_mode_ = mode.GetString() == "NORMAL";
  }
}

void AtriumBrowserWindow::DrainMarginaliaMarkClicks() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildDrainClicksScript(),
      base::BindOnce(&AtriumBrowserWindow::OnMarginaliaClicksDrained,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::OnMarginaliaClicksDrained(base::Value result) {
  if (!result.is_list()) {
    return;
  }
  for (const base::Value& mark_id : result.GetList()) {
    if (mark_id.is_string()) {
      PushFlashMarginal(mark_id.GetString());
    }
  }
}

void AtriumBrowserWindow::PushFlashMarginal(const std::string& mark_id) {
  const std::u16string script =
      u"window.atriumFlashMarginal && window.atriumFlashMarginal(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(mark_id)) + u")";
  CallSurface(notes_contents_.get(), script);
}

void AtriumBrowserWindow::ScrollToMarginalMarkOnActivePage(
    const std::string& mark_id) {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildScrollToMarkScript(mark_id), base::NullCallback(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::AttachDropContentsToPane(
    AtriumPaneView* pane,
    content::WebContents* contents) {
  pane->web_view()->SetVisible(true);
  {
    // See suppress_pane_focus_activation_'s doc.
    base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                           true);
    ReleasePeekClaim(contents);
    // same cross-window rule as ShowTab's attaches - a tab still
    // held by another window's pane aborts on DCHECK(views_host_) at that
    // pane's next detach.
    ReleaseContentsFromOtherWindows(contents);
    TraceClaim(pane->web_view(), contents, ClaimTag("pane-drop"));
    pane->web_view()->SetWebContents(contents);
  }
  pane->empty_card()->SetVisible(false);
  if (widget_->IsVisible()) {
    contents->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  }
}

void AtriumBrowserWindow::InsertPaneFromDrop(content::WebContents* new_contents,
                                             atrium::SplitEdge edge) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!new_contents) {
    LOG(WARNING) << "Atrium: InsertPaneFromDrop dropped - null contents";
    return;
  }
  AtriumPaneView* target_pane = container->active_pane();
  content::WebContents* target_contents =
      target_pane->web_view()->web_contents();
  if (new_contents == target_contents) {
    LOG(WARNING) << "Atrium: InsertPaneFromDrop dropped - already the "
                    "active pane's tab";
    return;
  }

  if (edge == atrium::SplitEdge::kCenter) {
    const bool secondary_active = container->active_pane_index() == 1;
    AttachDropContentsToPane(target_pane, new_contents);
    (secondary_active ? split_secondary_tab_id_ : split_primary_tab_id_) =
        TabId(new_contents);
    UpdateDockedDevtools(new_contents);
    RetargetLoadProgress();
    return;
  }

  if (!container->is_split()) {
    // The unsplit -> split transition: same InsertAtLeaf-backed primitive
    // as the already-split branch below, plus the 2-pane bookkeeping
    // EnterSplit itself relies on (tab-id cache, focus subscription).
    AtriumPaneView* secondary = container->InsertPaneAtLeaf(target_pane, edge);
    if (!secondary) {
      LOG(WARNING) << "Atrium: InsertPaneFromDrop rejected the insert";
      return;
    }
    split_primary_tab_id_ = TabId(target_contents);
    split_secondary_tab_id_ = TabId(new_contents);
    RebuildPaneFocusSubscriptions();
    AttachDropContentsToPane(secondary, new_contents);
    container->SetActivePaneIndex(0);
    PushSplitState(/*active=*/true, "op");
    UpdateDockedDevtools(target_contents);
    UpdateDockedDevtools(new_contents);
    RetargetLoadProgress();
    return;
  }

  // Growing past two leaves. The pane's own tab identity is its WebView's
  // contents - split_primary_tab_id_/split_secondary_tab_id_ name leaf 0 and
  // leaf 1, and a left/top insert renumbers the leaves under them.
  AtriumPaneView* new_pane = container->InsertPaneAtLeaf(target_pane, edge);
  if (!new_pane) {
    LOG(WARNING) << "Atrium: InsertPaneFromDrop rejected the insert "
                    "(capacity or target not found)";
    return;
  }
  RebuildPaneFocusSubscriptions();
  AttachDropContentsToPane(new_pane, new_contents);
  // A left or top insert lands BEFORE its target, so leaves 0 and 1 can now
  // be different panes. PushSplitState and CollapseSplit read these two ids.
  for (size_t i = 0; i < 2 && i < container->pane_count(); ++i) {
    content::WebContents* leaf_contents =
        container->PaneAt(i)->web_view()->web_contents();
    (i == 0 ? split_primary_tab_id_ : split_secondary_tab_id_) =
        leaf_contents ? TabId(leaf_contents) : 0;
  }
  // the two-pane branch above already pushes on growth: a third
  // or fourth pane must reach the sidebar the same way.
  PushSplitState(/*active=*/true, "op");
  UpdateDockedDevtools(new_contents);
  RetargetLoadProgress();
}

void AtriumBrowserWindow::UpdateSplit(SplitOrientation orientation,
                                      double ratio) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!container->is_split()) {
    return;
  }
  container->UpdateSplitLayout(orientation, ratio);
}

void AtriumBrowserWindow::CloseSplit(content::WebContents* keep_contents) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!container->is_split()) {
    return;
  }
  const uint64_t keep_id = keep_contents ? TabId(keep_contents) : 0;
  // keep_id == 0, or an id matching neither pane (defensive - a stale/
  // foreign id already failed ResolveTab at the op layer, but this method
  // takes a raw WebContents* and re-checks anyway), keeps whichever pane is
  // currently active.
  const bool keep_secondary =
      keep_id != 0 && keep_id == split_secondary_tab_id_;
  const bool keep_primary = keep_id != 0 && keep_id == split_primary_tab_id_;
  const int keep_index = keep_secondary ? 1
                         : keep_primary ? 0
                                        : container->active_pane_index();
  FinishCloseSplit(keep_index, "op");
}

bool AtriumBrowserWindow::IsSplit() const {
  return root_view_->pane_container()->is_split();
}

SplitOrientation AtriumBrowserWindow::split_orientation() const {
  return root_view_->pane_container()->orientation();
}

double AtriumBrowserWindow::SplitRatioForTesting() const {
  return root_view_->pane_container()->ratio();
}

uint64_t AtriumBrowserWindow::SplitPartnerTabId(uint64_t tab_id) const {
  if (!root_view_->pane_container()->is_split() || tab_id == 0) {
    return 0;
  }
  if (tab_id == split_primary_tab_id_) {
    return split_secondary_tab_id_;
  }
  if (tab_id == split_secondary_tab_id_) {
    return split_primary_tab_id_;
  }
  return 0;
}

bool AtriumBrowserWindow::IsSplitPaneContents(
    content::WebContents* contents) const {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!contents || !container->is_split()) {
    return false;
  }
  for (size_t i = 0; i < container->pane_count(); ++i) {
    if (container->PaneAt(i)->web_view()->web_contents() == contents) {
      return true;
    }
  }
  return false;
}

void AtriumBrowserWindow::CollapseSplit(uint64_t leaving_tab_id,
                                        const char* reason) {
  const uint64_t survivor_id = SplitPartnerTabId(leaving_tab_id);
  if (survivor_id == 0) {
    return;  // Not a current split-pane tab - nothing to collapse.
  }
  const int keep_index = leaving_tab_id == split_primary_tab_id_ ? 1 : 0;
  FinishCloseSplit(keep_index, reason);
}

uint64_t AtriumBrowserWindow::ClosePaneForTab(uint64_t leaving_tab_id,
                                              const char* reason) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (leaving_tab_id == 0 || !container->is_split()) {
    return 0;
  }
  auto pane_tab_id = [container](size_t index) -> uint64_t {
    content::WebContents* contents =
        container->PaneAt(index)->web_view()->web_contents();
    return contents ? TabId(contents) : 0;
  };
  if (container->pane_count() <= 2) {
    const uint64_t survivor_id = SplitPartnerTabId(leaving_tab_id);
    if (survivor_id == 0) {
      return 0;
    }
    CollapseSplit(leaving_tab_id, reason);
    return survivor_id;
  }
  // The pane is found by its page, not by a cached id: only leaves 0 and 1
  // have one, and a left/top insert renumbers the leaves under them.
  size_t index = container->pane_count();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    if (pane_tab_id(i) == leaving_tab_id) {
      index = i;
      break;
    }
  }
  if (index == container->pane_count()) {
    return 0;
  }
  AtriumPaneView* pane = container->PaneAt(index);
  // A WebContents still attached when its WebView dies keeps the claim
  // and tears down docked devtools implicitly.
  TraceClaim(pane->web_view(), nullptr, ClaimTag("pane-removed"));
  pane->web_view()->SetWebContents(nullptr);
  pane->devtools_web_view()->SetWebContents(nullptr);
  if (!container->RemovePane(pane)) {
    return 0;
  }
  RebuildPaneFocusSubscriptions();
  split_primary_tab_id_ = pane_tab_id(0);
  split_secondary_tab_id_ = pane_tab_id(1);
  PushSplitState(/*active=*/true, reason);
  RetargetLoadProgress();
  content::WebContents* active =
      container->active_pane()->web_view()->web_contents();
  return active ? TabId(active) : 0;
}

void AtriumBrowserWindow::FinishCloseSplit(int keep_index, const char* reason) {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (!container->is_split()) {
    return;
  }
  // Any leaf, not only the second: a click selects a third or fourth pane
  // now, and CloseSplit keeps whichever one is active.
  if (keep_index < 0 ||
      keep_index >= static_cast<int>(container->pane_count())) {
    keep_index = 0;
  }
  content::WebContents* survivor =
      container->PaneAt(static_cast<size_t>(keep_index))
          ->web_view()
          ->web_contents();
  // A mirror holds no strip tab, and ReleaseMirror below frees it - re-homing
  // it onto the primary WebView would leave that WebView dangling.
  if (survivor && survivor == mirror_contents_.get()) {
    survivor = nullptr;
    keep_index = 0;
  }
  // ExitSplit destroys every pane past the primary, and a WebContents still
  // attached when its view dies is the surviving claim and the
  // implicit devtools teardown this detach exists to prevent.
  for (size_t i = 1; i < container->pane_count(); ++i) {
    AtriumPaneView* pane = container->PaneAt(i);
    TraceClaim(pane->web_view(), nullptr, ClaimTag("pane-extra-clear"));
    pane->web_view()->SetWebContents(nullptr);
    pane->devtools_web_view()->SetWebContents(nullptr);
  }
  if (keep_index != 0) {
    // The survivor was some pane past the first, whose WebView ExitSplit is
    // about to destroy - re-home it onto the primary pane's WebView (the
    // detached-then-re-attached peek-promote idiom, no reload).
    AtriumPaneView* primary = container->primary_pane();
    if (survivor) {
      primary->web_view()->SetVisible(true);
      // See suppress_pane_focus_activation_'s doc: this re-home runs from
      // inside BrowserController::OnTabStripModelChanged (a genuine close)
      // or CollapseSplit (a move-out) - either way, WebContents::Focus()'s
      // synchronous WebContentsFocused notification must not trigger a
      // reentrant ActivateTabAt while TabStripModel's own dispatch may
      // still be in progress (VM-repro'd FATAL: "Check failed:
      // !*guard_flag_" - the crash this guard exists to prevent).
      base::AutoReset<bool> pane_focus_guard(&suppress_pane_focus_activation_,
                                             true);
      // same exclusive-claim rule as ShowTab's attaches.
      ReleasePeekClaim(survivor);
      TraceClaim(primary->web_view(), survivor, ClaimTag("pane-survivor"));
      primary->web_view()->SetWebContents(survivor);
      primary->empty_card()->SetVisible(false);
    }
  }
  container->ExitSplit();
  RebuildPaneFocusSubscriptions();
  // Every close path funnels here, so the mirror cannot outlive its split and
  // strand its off-the-record profile.
  ReleaseMirror();
  split_primary_tab_id_ = survivor ? TabId(survivor) : 0;
  split_secondary_tab_id_ = 0;
  PushSplitState(/*active=*/false, reason);
  // re-run docked devtools for the surviving tab against the
  // now-collapsed single-pane geometry (it may already have had docked
  // devtools open in whichever pane it was living in).
  UpdateDockedDevtools(survivor);
  // The active pane collapsed back to the survivor - re-point
  // the loading-bar observer at it and clear the (now-destroyed) secondary
  // pane's bar state.
  RetargetLoadProgress();
}

bool AtriumBrowserWindow::PaneHasDockedDevtoolsForTesting(
    content::WebContents* contents) const {
  AtriumPaneContainerView* container = root_view_->pane_container();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    AtriumPaneView* pane = container->PaneAt(i);
    if (pane->web_view()->web_contents() == contents) {
      return pane->devtools_web_view()->web_contents() != nullptr;
    }
  }
  return false;
}

int AtriumBrowserWindow::PanesWithDockedDevtoolsForTesting() const {
  int held = 0;
  AtriumPaneContainerView* container = root_view_->pane_container();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    if (container->PaneAt(i)->devtools_web_view()->web_contents()) {
      ++held;
    }
  }
  return held;
}

void AtriumBrowserWindow::UpdateDockedDevtools(
    content::WebContents* inspected_web_contents) {
  // UpdateDockedDevtoolsForPane's own contents-match gate makes this safe to
  // call unconditionally, including the null case (ShowTab's empty branch).
  AtriumPaneContainerView* container = root_view_->pane_container();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    UpdateDockedDevtoolsForPane(container->PaneAt(i), inspected_web_contents);
  }
}

void AtriumBrowserWindow::UpdateDockedDevtoolsForPane(
    AtriumPaneView* pane,
    content::WebContents* inspected_web_contents) {
  content::WebContents* pane_contents = pane->web_view()->web_contents();
  // Gate also matches null `inspected_web_contents` against a pane that
  // just went empty (ShowTab's null-contents branch).
  if (pane_contents != inspected_web_contents) {
    return;
  }

  // Re-implements DevtoolsWebViewController::UpdateDevtools
  // (devtools_ui_controller.cc:114-176) for this pane: look up whether
  // `pane_contents` currently has an open DevToolsWindow docked to it, then
  // attach/detach/resize/reorder to match.
  DevToolsContentsResizingStrategy strategy;
  content::WebContents* devtools =
      DevToolsWindow::GetInTabWebContents(pane_contents, &strategy);
  if (!devtools) {
    // Mirrors DevtoolsUIController::UpdateDevtools's RestoreFocus call
    // (devtools_ui_controller.cc), made before detaching below - restores
    // focus to whatever view held it before devtools took it, the same
    // "closing devtools gives focus back" behavior BrowserView gets for
    // free. A no-op if this pane never had a focus tracker installed.
    pane->RestoreDevToolsFocus();
  }

  views::WebView* devtools_web_view = pane->devtools_web_view();
  if (devtools_web_view->web_contents() != devtools) {
    if (!devtools_web_view->web_contents() && devtools &&
        !pane->HasDevToolsFocusTracker()) {
      // Install the focus tracker the first time devtools shows for this
      // pane.
      pane->InstallDevToolsFocusTracker();
    }
    if (devtools_web_view->web_contents() && !devtools &&
        pane->HasDevToolsFocusTracker()) {
      // Reset it the moment devtools hides.
      pane->ClearDevToolsFocusTracker();
    }
    devtools_web_view->SetWebContents(devtools);
  }

  if (devtools) {
    devtools_web_view->SetVisible(true);
    pane->SetDevToolsContentsResizingStrategy(strategy);
  } else {
    devtools_web_view->SetVisible(false);
    pane->SetDevToolsContentsResizingStrategy(
        DevToolsContentsResizingStrategy());
  }
  pane->UpdateDevToolsZOrder(devtools && strategy.hide_inspected_contents());
  // The single point where this pane's dock state resolves,
  // so the VM probe suite has one line to assert against for dock/undock,
  // side-vs-bottom, divider drag, and detach/redock. `bounds` is the
  // frontend-owned contents rect the strategy insets to - it changes with the
  // dock side and on every divider drag, which is what makes those probes
  // assertable without pixel comparison.
  VLOG(1) << "Atrium: devtools pane " << pane
          << " docked=" << (devtools ? 1 : 0)
          << " bounds=" << strategy.bounds().ToString()
          << " hide_inspected=" << strategy.hide_inspected_contents();
  // Layout() reads devtools_web_view_'s visibility/strategy directly - no
  // separate "requires_layout" bookkeeping needed the way
  // DevtoolsWebViewController tracks it, since InvalidateLayout() is cheap
  // to call unconditionally here (this only runs from ShowTab/EnterSplit/
  // FinishCloseSplit, not per-frame).
  pane->InvalidateLayout();
}

void AtriumBrowserWindow::OnSnapshotClicked() {
  if (!is_frozen_) {
    return;
  }
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->ActivateOwnTab(frozen_tab_id_);
  }
}

void AtriumBrowserWindow::OnPeekScrimPressed() {
  if (peek_widget_) {
    peek_widget_->RunDismissCallback();
  }
}

void AtriumBrowserWindow::PushSplitState(bool active, const char* reason) {
  if (!sidebar_contents_) {
    return;
  }
  AtriumPaneContainerView* container = root_view_->pane_container();
  base::DictValue dict;
  dict.Set("active", active);
  // base::Value has no int64. TabId's monotonic counter fits doubles exactly.
  dict.Set("primaryTabID", static_cast<double>(split_primary_tab_id_));
  dict.Set("secondaryTabID", static_cast<double>(split_secondary_tab_id_));
  // Every live pane in order, so a third or fourth can be badged too -
  // primaryTabID/secondaryTabID only ever name leaves 0 and 1.
  base::ListValue pane_ids;
  for (size_t i = 0; i < container->pane_count(); ++i) {
    content::WebContents* pane_contents =
        container->PaneAt(i)->web_view()->web_contents();
    pane_ids.Append(
        static_cast<double>(pane_contents ? TabId(pane_contents) : 0));
  }
  dict.Set("paneTabIDs", std::move(pane_ids));
  dict.Set("orientation", SplitOrientationToString(container->orientation()));
  dict.Set("ratio", container->ratio());
  dict.Set("reason", reason);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  // JSON.parse rather than interpolation - title/reason strings are not
  // attacker-controlled here, but matches every other push's construct.
  const std::u16string script =
      u"window.atriumSplitChanged && window.atriumSplitChanged(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  CallSurface(sidebar_contents_.get(), script);
}

// static
void AtriumBrowserWindow::PushMediaSessionsToProfile(
    Profile* profile,
    const std::string& records_json) {
  if (!profile) {
    return;
  }
  const std::u16string script =
      u"window.atriumMediaSessionsChanged && "
      u"window.atriumMediaSessionsChanged(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(records_json)) + u"))";
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        Browser* browser = window->GetBrowserForMigrationOnly();
        if (!BrowserController::ControllerForBrowser(browser)) {
          return true;  // Not an Atrium-managed window; keep scanning.
        }
        auto* atrium_window = static_cast<AtriumBrowserWindow*>(
            browser->window());
        if (!atrium_window) {
          return true;
        }
        // browser->profile() misses a cross-profile slot swap, where this
        // window's sidebar shows another controller's tabs.
        Browser* shown = atrium_window->SidebarTargetBrowser();
        if (!shown || shown->profile() != profile) {
          return true;
        }
        CallSurface(atrium_window->sidebar_contents(), script);
        return true;
      },
      BrowserCollection::Order::kActivation);
}

void AtriumBrowserWindow::OnDividerResizeCommitted() {
  // OnDividerResize only invokes this from its own is_split() guard, but
  // PushSplitState is cheap to call defensively correct either way.
  PushSplitState(/*active=*/true, "divider");
}

void AtriumBrowserWindow::RebuildPaneFocusSubscriptions() {
  pane_focus_subscriptions_.clear();
  if (!root_view_) {
    return;
  }
  AtriumPaneContainerView* container = root_view_->pane_container();
  for (size_t i = 0; i < container->pane_count(); ++i) {
    pane_focus_subscriptions_.push_back(
        container->PaneAt(i)->web_view()->AddWebContentsFocusedCallback(
            base::BindRepeating(&AtriumBrowserWindow::OnPaneWebContentsFocused,
                                weak_factory_.GetWeakPtr())));
  }
}

void AtriumBrowserWindow::OnPaneWebContentsFocused(views::WebView* web_view) {
  if (suppress_pane_focus_activation_) {
    // This notification came from our OWN programmatic SetWebContents call,
    // not a genuine click - see the member's doc for the reentrancy FATAL
    // this prevents.
    return;
  }
  if (!IsSplit()) {
    return;  // Single-pane: nothing to activate.
  }
  content::WebContents* contents = web_view->web_contents();
  if (!contents) {
    return;
  }
  AtriumPaneContainerView* container = root_view_->pane_container();
  // Re-entrancy guard (plan section 1's "click into the secondary pane...
  // no attach/detach happens, only the active-pane highlight... flip"):
  // only the currently INACTIVE pane's focus should drive an activation -
  // matches upstream MultiContentsView::OnWebContentsFocused's own
  // GetInactiveContentsView() check. Once ActivateTabAt below lands, this
  // same pane is active, so a later focus notification for it (e.g. the
  // window regaining key status over already-focused content) fails this
  // check and does nothing - no loop.
  int focused_pane_index = 0;
  for (size_t i = 0; i < container->pane_count(); ++i) {
    if (container->PaneAt(i)->web_view() == web_view) {
      focused_pane_index = static_cast<int>(i);
      break;
    }
  }
  if (focused_pane_index == container->active_pane_index()) {
    return;
  }
  const int tab_index =
      browser_->tab_strip_model()->GetIndexOfWebContents(contents);
  if (tab_index == TabStripModel::kNoTab) {
    VLOG(1) << "Atrium: pane " << focused_pane_index
            << " holds no strip tab, not activated";
    return;
  }
  browser_->tab_strip_model()->ActivateTabAt(tab_index);
}

// Liquid-glass window (design session v3, stage 2). The NSWindow goes
// non-opaque with a clear background, and an NSVisualEffectView (behind-window
// blending, state pinned ACTIVE - the palette's grey-when-not-key lesson,
// m4-appearance-report) is inserted BELOW the bridge's compositor view, so it
// frosts the desktop wherever the compositor surface has alpha. The root
// view's tint is painted translucent (backdrop_glass_alpha_) over it: frosted
// desktop + tint wash = tinted liquid glass. The web content card and the
// frozen-snapshot pane paint fully opaque on top and are unaffected.
void AtriumBrowserWindow::InstallLiquidGlassBackdrop() {
  window_platform_->InstallBackdrop();
  UpdateWindowCornerMask();
}

// this window is non-opaque, so AppKit no longer corner-masks what
// we paint - the glass + tint would show square corners against the frame's
// rounded shadow, looking narrower than every native app. Clip the content
// view AND the behind-window glass (whose CABackdropLayer ignores an ancestor
// mask) to the radius AtriumWindowCornerRadius() reports, which is where the
// "which value is native" reasoning lives.
void AtriumBrowserWindow::UpdateWindowCornerMask() {
  // fullscreen has no rounded frame to follow - the window IS the
  // screen. Keeping the radius would round the page's own corners against
  // black. Covers the green-button path too, which never reaches
  // ExclusiveAccessContext.
  window_platform_->UpdateCornerMask(
      window_platform_->IsWindowFullScreen() ? 0 : AtriumWindowCornerRadius());
}

void AtriumBrowserWindow::SetImmersiveFullscreen(bool immersive) {
  if (immersive_fullscreen_ == immersive) {
    return;
  }
  immersive_fullscreen_ = immersive;
  if (root_view_) {
    root_view_->SetImmersive(immersive);
  }
  // The native transition is async; UpdateWindowCornerMask runs again from
  // OnWidgetBoundsChanged once the window has actually resized, when
  // NSWindowStyleMaskFullScreen finally reflects the new state.
  widget_->SetFullscreen(immersive);
  UpdateWindowCornerMask();
}

// the alpha of the tint wash painted over the glass used to be the
// file-local kGlassBackdropAlpha constant here (0xFF = the old opaque
// backdrop, no glass visible; lower = frostier - ~72%/0xB8 kept the space
// tint clearly dominant while the desktop blur reads through). It is now
// backdrop_glass_alpha_, a per-window member defaulting to that same 0xB8 -
// see the header's kDefaultGlassBackdropAlpha doc and SetGlassBackdropAlpha
// below.

// content participates in the workspace-switch animation - see
// the retired ABI header's CONTENT FADE FOR SWITCH doc block.
// NSAnimationContext (Core Animation) rather than a views/compositor construct:
// the web content's NSView composites above views siblings (the M4 lesson - an
// in-hierarchy overlay can never paint over it), and a CA animation keeps
// running while the browser main thread stalls in the settle IPC.
void AtriumBrowserWindow::SetContentFade(int32_t alpha_percent,
                                         int32_t duration_ms) {
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  if (!contents) {
    return;
  }
  window_platform_->FadeNativeView(
      contents->GetNativeView(), std::clamp(alpha_percent, 0, 100) / 100.0,
      duration_ms <= 0 ? base::TimeDelta() : base::Milliseconds(duration_ms));
}

void AtriumBrowserWindow::SetBackdropTint(uint32_t argb) {
  // Translucent tint over the glass (stage 2) - the compositor surface is
  // kTranslucent, so the alpha here is what lets InstallLiquidGlassBackdrop's
  // NSVisualEffectView show through.

  // The choosers sit here, not at the callers: backdrop_tint_ is the only
  // thing the ink, the seam, the overlay pushes and the crossfade's two ends
  // read, so one substitution here covers all of them.
  const SkColor target = SkColorSetA(
      atrium::EffectiveSpaceTint(browser_.get(), static_cast<SkColor>(argb)),
      atrium::EffectiveGlassAlpha(browser_.get(), backdrop_glass_alpha_));
  // Space switches CROSSFADE the tint instead of snapping - same
  // timer-driven pattern as the sidebar resize animation. The very first
  // tint (host registration) applies instantly: fading from the untinted
  // glass would flash gray at startup.
  if (!has_backdrop_tint_) {
    has_backdrop_tint_ = true;
    backdrop_tint_ = target;
    if (!has_theme_) {
      InstallRootBackground(views::CreateSolidBackground(target));
    }
    root_view_->SchedulePaint();
    PushAiPanelInk();
    return;
  }
  if (target == backdrop_tint_ && !backdrop_tint_timer_.IsRunning()) {
    return;
  }
  backdrop_tint_start_ = backdrop_tint_;
  backdrop_tint_target_ = target;
  backdrop_tint_animation_start_ = base::TimeTicks::Now();
  backdrop_tint_t_ = 0.0;
  backdrop_tint_timer_.Start(
      FROM_HERE, kSidebarResizeAnimationTick,
      base::BindRepeating(&AtriumBrowserWindow::StepBackdropTintAnimation,
                          weak_factory_.GetWeakPtr()));
}

// Workspace-scrub perf: snaps the tint directly - no timer, no
// easing - for the live 60-120Hz scrub stream, where SetBackdropTint's
// restart-per-call 300ms crossfade either perpetually postpones its first
// fire or produces a lagged trail that never tracks the gesture. Stops any
// in-flight crossfade first so a discrete transition interrupted by a scrub
// hands off cleanly instead of the two fighting over the painted color -
// same posture as SetSidebarWidthLive stopping sidebar_resize_timer_.
void AtriumBrowserWindow::SetBackdropTintLive(uint32_t argb) {
  // Same choosers as SetBackdropTint - a scrub is a space tint arriving at
  // 120Hz rather than once, and an incognito window refuses every frame of it.
  const SkColor target = SkColorSetA(
      atrium::EffectiveSpaceTint(browser_.get(), static_cast<SkColor>(argb)),
      atrium::EffectiveGlassAlpha(browser_.get(), backdrop_glass_alpha_));
  backdrop_tint_timer_.Stop();
  // Mirrors SetBackdropTint's has_theme_ guard: while a rasterized theme is
  // active it owns root_view_'s background (AtriumThemeBackground, installed
  // by SetBackdropTheme) - replacing it with a flat CreateSolidBackground
  // here would tear the theme's gradient and grain overlay out from under
  // it while has_theme_ stays true, leaving the two out of sync. Keep
  // backdrop_tint_ current (a later theme clear still restores the right
  // color, and PaintThemeBackdrop reads it live for its base wash) but do
  // not touch the background object.
  // A staged scrub is the one exception - PaintThemeBackdrop's
  // flat-tint-incoming branch reads backdrop_tint_ live every frame (the "no
  // bitmap staged" scrub case), so a live tint push during a themed -> flat
  // scrub must repaint or the incoming layer never tracks the finger.
  // Outside a scrub this op still updates state without repainting - same as
  // SetBackdropTint's own themed branch.
  if (has_theme_) {
    has_backdrop_tint_ = true;
    backdrop_tint_ = target;
    if (scrub_blend_weight_ >= 0.0) {
      root_view_->SchedulePaint();
    }
    PushLiveScrubInk();
    return;
  }
  has_backdrop_tint_ = true;
  backdrop_tint_ = target;
  InstallRootBackground(views::CreateSolidBackground(target));
  root_view_->SchedulePaint();
  PushLiveScrubInk();
}

// The per-frame half of PushAiPanelInk: only the sidebar sits under the
// finger, and the full push executes script in every overlay's renderer,
// which at 120 Hz is hundreds of cross-process calls per swipe.
void AtriumBrowserWindow::PushLiveScrubInk() {
  PushSidebarInk();
  PushSidebarFrostAppearance();
  RepaintFloatingPanel();
  PushOverlayTheme(sidebar_contents_.get());
}

void AtriumBrowserWindow::PushBackdropInk() {
  PushAiPanelInk();
}

// instant, no crossfade - see the retired ABI header's GLASS BACKDROP
// ALPHA doc block for why (a live settings-drag value already animated by
// the user's own input; a second fade on top would only add lag).
void AtriumBrowserWindow::SetGlassBackdropAlpha(int32_t alpha_percent) {
  const uint8_t alpha = base::ClampRound<uint8_t>(
      std::clamp(alpha_percent, 0, 100) / 100.0 * 255.0);
  if (backdrop_glass_alpha_ == alpha) {
    return;
  }
  backdrop_glass_alpha_ = alpha;
  // Before the early return below: the sidebar paints its own wash out of this
  // number and does not care whether a tint has been applied to the window yet.
  PushSidebarAppearance();
  if (!has_backdrop_tint_) {
    // No tint applied yet (pre-registration) - nothing to repaint; the next
    // SetBackdropTint call picks up the new member.
    return;
  }
  if (has_theme_) {
    // PaintThemeBackdrop reads backdrop_tint_ live every paint, so the member
    // is enough. Its crossfade cannot run here: the tint timer reinstalls a
    // solid background every tick and would tear the theme painter out.
    backdrop_tint_ = SkColorSetA(backdrop_tint_, backdrop_glass_alpha_);
  } else if (ambilight_live_ && !edge_tint_target_.IsUniform()) {
    // The glow's alpha derives from this one, so the edges retarget through
    // their own crossfade. The base wash under them snaps, for the theme's
    // reason: the flat crossfade would replace the glow painter.
    backdrop_tint_ = SkColorSetA(backdrop_tint_, backdrop_glass_alpha_);
    SetEdgeTint(edge_tint_target_.Mapped(
        [](SkColor c) { return SkColorSetA(c, SK_AlphaOPAQUE); }));
  } else {
    // crossfade to the new alpha rather than reinstalling a solid
    // background at it. The glass control commits on change, not per pointer
    // move, so there is no live drag for a fade to lag behind.
    VLOG(1) << "Atrium: glass alpha crossfading to " << static_cast<int>(alpha);
    SetBackdropTint(backdrop_tint_);
  }
  // The two branches above repaint without re-pushing ink, so the frost would
  // hold the previous alpha's light/dark answer until the next tint change.
  // The crossfade branch pushes on every tick, which makes this a no-op there.
  PushSidebarFrostAppearance();
  root_view_->SchedulePaint();
}

void AtriumBrowserWindow::StepBackdropTintAnimation() {
  // advance by tick delta, not wall clock - a main-thread stall
  // (the space switch's settle IPC) pauses the fade and it resumes
  // smoothly, instead of leaping to the end when ticks resume.
  backdrop_tint_t_ =
      std::min(1.0, backdrop_tint_t_ + kSidebarResizeAnimationTick /
                                           kBackdropTintFadeDuration);
  const double t = backdrop_tint_t_;
  const double eased = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, t);
  backdrop_tint_ = gfx::Tween::ColorValueBetween(eased, backdrop_tint_start_,
                                                 backdrop_tint_target_);
  // The theme painter reads backdrop_tint_ for its wash on every paint. A
  // solid background here would tear the theme painter out.
  if (!has_theme_) {
    InstallRootBackground(views::CreateSolidBackground(backdrop_tint_));
  }
  root_view_->SchedulePaint();
  // every tick, not just the settled end - continuous ink,
  // matching the sidebar's own crossfade.
  PushAiPanelInk();
  if (t >= 1.0) {
    backdrop_tint_timer_.Stop();
  }
}

namespace {

class AtriumSurroundFadeBackground : public views::Background {
 public:
  explicit AtriumSurroundFadeBackground(AtriumBrowserWindow* window)
      : window_(window) {}
  AtriumSurroundFadeBackground(const AtriumSurroundFadeBackground&) = delete;
  AtriumSurroundFadeBackground& operator=(const AtriumSurroundFadeBackground&) =
      delete;
  ~AtriumSurroundFadeBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    window_->PaintSurroundFade(canvas);
  }

 private:
  const raw_ptr<AtriumBrowserWindow> window_;
};

}  // namespace

// the native-app chrome mode. The fill goes on the pane container,
// not root_view_ and not backdrop_glass_alpha_ - both of those are the
// SIDEBAR's surface too, and this mode exists precisely to leave the sidebar
// on glass. Nothing here is theme-aware: Swift has already resolved
// appearance and any user override to a plain colour, and per-space themes
// keep painting root_view_ untouched underneath.
void AtriumBrowserWindow::SetOpaqueSurround(int32_t enabled, uint32_t argb) {
  // The top byte is ignored rather than trusted - a translucent "opaque"
  // surround would let the glass through and stop being a surround.
  const SkColor surround =
      SkColorSetA(static_cast<SkColor>(argb), SK_AlphaOPAQUE);
  const bool want = enabled != 0;
  const bool changed = want != opaque_surround_enabled_;
  // A disabling call carries no colour, so the one fading OUT is the one
  // already on screen.
  const SkColor on_screen =
      want ? surround
           : SkColorSetA(static_cast<SkColor>(opaque_surround_argb_),
                         SK_AlphaOPAQUE);
  opaque_surround_enabled_ = want;
  opaque_surround_argb_ = argb;
  // The root too: the slot's last kSidebarGripWidth px are covered by neither
  // the pane container nor the sidebar's web layer, and the tint showed
  // through there as a line between two otherwise white surfaces.
  // A toggle after the first paint crossfades. Startup and a
  // colour-only change snap: nothing on screen to fade from, or nothing moved.
  if (changed && has_backdrop_tint_) {
    VLOG(1) << "Atrium: surround fade " << (want ? "in" : "out") << " started";
    surround_fade_color_ = on_screen;
    surround_fade_to_on_ = want;
    surround_fade_t_ = 0.0;
    surround_fade_running_ = true;
    root_view_->pane_container()->SetOpaqueSurround(
        true, SkColorSetA(on_screen, want ? 0 : SK_AlphaOPAQUE));
    InstallSurroundRootBackground(
        std::make_unique<AtriumSurroundFadeBackground>(this));
    surround_fade_timer_.Start(
        FROM_HERE, kSidebarResizeAnimationTick,
        base::BindRepeating(&AtriumBrowserWindow::StepSurroundFade,
                            weak_factory_.GetWeakPtr()));
  } else {
    root_view_->pane_container()->SetOpaqueSurround(want, surround);
    if (want) {
      InstallSurroundRootBackground(
          std::make_unique<AtriumSurroundBackground>(surround, RootGlowPainter()));
      root_view_->SchedulePaint();
    } else {
      RestoreRootBackground();
    }
  }
  // The glow resolves differently per mode (ChromeColorForPage), so the page
  // is read again rather than left as the other mode's answer.
  if (changed) {
    last_sampled_ = atrium::EdgeTint();
    ApplyPageTint();
    if (ambilight_live_) {
      ambilight_cadence_.Reset();
      ScheduleNextAmbilightSample(ambilight_cadence_.interval_ms());
    }
  }
  // the fill above stops at the sidebar column, so the sidebar has
  // to paint the mode itself or the two halves of the chrome disagree.
  PushSidebarAppearance();
  // The other eight surfaces grade their ink against opaque_surround_argb_
  // too (PushOverlayTheme) - without this they kept the previous surround's
  // ink until their own ready handshake happened to rerun.
  PushAiPanelInk();
}

void AtriumBrowserWindow::StepSurroundFade() {
  surround_fade_t_ =
      std::min(1.0, surround_fade_t_ + kSidebarResizeAnimationTick /
                                           kBackdropTintFadeDuration);
  const double eased =
      gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, surround_fade_t_);
  const double cover = surround_fade_to_on_ ? eased : 1.0 - eased;
  root_view_->pane_container()->SetOpaqueSurround(
      true, SkColorSetA(surround_fade_color_,
                        base::ClampRound<uint8_t>(cover * SK_AlphaOPAQUE)));
  root_view_->SchedulePaint();
  if (surround_fade_t_ < 1.0) {
    return;
  }
  surround_fade_timer_.Stop();
  surround_fade_running_ = false;
  VLOG(1) << "Atrium: surround fade " << (surround_fade_to_on_ ? "in" : "out")
          << " finished";
  if (surround_fade_to_on_) {
    root_view_->pane_container()->SetOpaqueSurround(true, surround_fade_color_);
    InstallSurroundRootBackground(std::make_unique<AtriumSurroundBackground>(
        surround_fade_color_, RootGlowPainter()));
    root_view_->SchedulePaint();
  } else {
    root_view_->pane_container()->SetOpaqueSurround(false, 0);
    RestoreRootBackground();
  }
  // The start-of-toggle push in SetOpaqueSurround already carries this fade's
  // target ink, but a caller that only watches for the fade to finish rather
  // than the toggle itself needs its own push here too.
  PushAiPanelInk();
}

void AtriumBrowserWindow::PaintSurroundFade(gfx::Canvas* canvas) {
  // Whatever the surround is covering or uncovering, then the surround at the
  // eased alpha. The same three states RestoreRootBackground hands back to.
  if (has_theme_) {
    PaintThemeBackdrop(canvas);
  } else if (ambilight_live_ && !edge_tint_target_.IsUniform()) {
    PaintAmbilightBackdrop(canvas);
  } else if (has_backdrop_tint_) {
    canvas->FillRect(root_view_->GetLocalBounds(), backdrop_tint_);
  }
  const double eased =
      gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, surround_fade_t_);
  const double cover = surround_fade_to_on_ ? eased : 1.0 - eased;
  canvas->FillRect(
      root_view_->GetLocalBounds(),
      SkColorSetA(surround_fade_color_,
                  base::ClampRound<uint8_t>(cover * SK_AlphaOPAQUE)));
  // Both ends of the fade carry the glow, so it must not dip in between.
  PaintPageGlow(canvas);
}

// Stages the incoming space's theme.
// NULL stages "flat tint incoming" - no bitmap to rasterize, the painter falls
// back to backdrop_tint_ at the blend weight. Otherwise copies the caller's
// transient buffer into scrub_bitmap_ exactly once, same N32-premul
// destination / straight-alpha RGBA source conversion as SetBackdropTheme
// (see its comment for why - ImageSkiaRep CHECKs premultiplied N32). Always
// (re)starts the scrub session at weight 0 - a redundant stage (e.g. D3's
// side-flip restage) restarts the compositing from "outgoing only", never
// resumes a stale weight from a previous session.
void AtriumBrowserWindow::SetBackdropScrubTheme(const uint8_t* rgba,
                                                int32_t w,
                                                int32_t h,
                                                float grain) {
  // A new stage - the first of a gesture, or a side-flip
  // restage - must not let a previous commit's still-running ease keep
  // driving scrub_blend_weight_ out from under this fresh session.
  scrub_blend_settle_timer_.Stop();
  scrub_grain_ = grain;
  if (!rgba) {
    scrub_bitmap_.reset();
    scrub_blend_weight_ = 0.0;
    return;
  }
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(w, h));
  const SkImageInfo src_info =
      SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
  bitmap.writePixels(SkPixmap(src_info, rgba, src_info.minRowBytes()));
  bitmap.setImmutable();
  scrub_bitmap_ = bitmap;
  scrub_blend_weight_ = 0.0;
  if (!has_theme_ && !atrium::IsIncognitoWindow(browser_.get())) {
    OpenScrubThemeSession();
  }
}

// Sets the scrub blend weight:
// clamp
// + store + repaint only - no rasterization, so this is cheap enough for the
// 60-120Hz scrub stream. A no-op if no scrub is staged (mirrors the D1
// stub's contract: a stray call outside a scrub session must not resurrect
// one).
//
// MEDIUM: also a no-op while the commit-time ease
// (scrub_blend_settle_timer_) is running - the third mutator of
// scrub_blend_weight_, missed by the ease's original two timer-vs-timer
// guards (SetBackdropScrubTheme, SetBackdropScrubBlendEnd). Without this, a
// late/out-of-order push landing after the gesture's own commit already
// started the ease would stomp the eased value for one frame; the ease
// self-heals on its next tick, but the field is being driven by the timer
// now, not by live gesture input.
void AtriumBrowserWindow::SetBackdropScrubBlend(double weight) {
  if (scrub_blend_weight_ < 0.0 || scrub_blend_settle_timer_.IsRunning()) {
    return;  // no scrub staged, or the commit-time ease already owns it
  }
  scrub_blend_weight_ = std::clamp(weight, 0.0, 1.0);
  root_view_->SchedulePaint();
}

// Ends the scrub:
// commit promotes the staged layer into the real backdrop; either way the
// staged state is cleared so a redundant call (no scrub in flight) is a
// no-op past the promotion check.
//
// A fast flick releases the gesture at a low blend
// weight (e.g. 0.2) - promoting straight away used to land the remaining
// 0.8 of the blend in a single frame, a visible colour snap the lab
// reference eases through instead (see swipe-lab.html: the settle spring
// keeps animating `p`, and blendWeight(p) is recomputed every frame from
// that, so the colour glides to the endpoint alongside the pane motion
// instead of cutting to it). commit=1 with a settle window and a staged
// weight still short of 1 now eases the rest of the way first, same timer
// pattern as StepBackdropTintAnimation/StepThemeAnimation, then promotes.
void AtriumBrowserWindow::SetBackdropScrubBlendEnd(int32_t commit,
                                                   int32_t settle_ms) {
  // A redundant end call, or one that lands while a previous commit's ease
  // is still running, must not let two timers race over
  // scrub_blend_weight_ - stop first, always.
  scrub_blend_settle_timer_.Stop();

  if (!commit || scrub_blend_weight_ < 0.0) {
    // Cancel, or nothing staged: unchanged from D2 - drop and repaint.
    scrub_bitmap_.reset();
    scrub_blend_weight_ = -1.0;
    if (scrub_opened_theme_) {
      // The session SetBackdropScrubTheme opened for a flat outgoing space
      // has nothing left to paint - back to the solid wash it stood in for.
      scrub_opened_theme_ = false;
      has_theme_ = false;
      theme_timer_.Stop();
      InstallRootBackground(views::CreateSolidBackground(
          SkColorSetA(backdrop_tint_, backdrop_glass_alpha_)));
      PushSidebarFrostAppearance();
    }
    root_view_->SchedulePaint();
    return;
  }
  scrub_opened_theme_ = false;

  if (settle_ms <= 0 || scrub_blend_weight_ >= kScrubBlendSettleDoneThreshold) {
    // No settle window offered, or nothing visible left to ease: promote
    // immediately - today's (D2) behavior.
    PromoteScrubThemeToBackdrop();
    scrub_bitmap_.reset();
    scrub_blend_weight_ = -1.0;
    root_view_->SchedulePaint();
    return;
  }

  scrub_blend_settle_from_ = scrub_blend_weight_;
  scrub_blend_settle_t_ = 0.0;
  scrub_blend_settle_duration_ = base::Milliseconds(settle_ms);
  scrub_blend_settle_timer_.Start(
      FROM_HERE, kSidebarResizeAnimationTick,
      base::BindRepeating(&AtriumBrowserWindow::StepScrubBlendSettleAnimation,
                          weak_factory_.GetWeakPtr()));
}

// One tick of the commit-time ease - tick-delta progress, same
// stall-resilience rationale as StepBackdropTintAnimation/StepThemeAnimation.
// EASE_OUT (fast in, slow out) matches the settle spring's own final
// approach rather than a linear ramp. On completion promotes and clears the
// staged state exactly as SetBackdropScrubBlendEnd's immediate path does.
void AtriumBrowserWindow::StepScrubBlendSettleAnimation() {
  scrub_blend_settle_t_ =
      std::min(1.0, scrub_blend_settle_t_ + kSidebarResizeAnimationTick /
                                                scrub_blend_settle_duration_);
  const double eased =
      gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT, scrub_blend_settle_t_);
  scrub_blend_weight_ =
      scrub_blend_settle_from_ + (1.0 - scrub_blend_settle_from_) * eased;
  root_view_->SchedulePaint();
  if (scrub_blend_settle_t_ >= 1.0) {
    scrub_blend_settle_timer_.Stop();
    PromoteScrubThemeToBackdrop();
    scrub_bitmap_.reset();
    scrub_blend_weight_ = -1.0;
    root_view_->SchedulePaint();
  }
}

// Load phase -------------------------------------------

void AtriumBrowserWindow::RetargetLoadProgress() {
  // Every split seam passes here, so the per-pane bars follow the panes. First,
  // so each bar knows its tab before the phase pushes below reach it.
  if (IsSplit() || shown_dev_bar_count_ > 1) {
    PushDevBarState();
  }
  content::WebContents* active = ActivePane()->web_view()->web_contents();
  // The field shows the active tab only, so the tab being left has to stop
  // filling it. Its ring keeps running on atriumTabLoading, which the switch
  // does not touch.
  if (load_phase_live_) {
    load_phase_live_ = false;
    PushLoadPhase(ObservedLoadTabId(), atrium::LoadPhase::kDone, 1.0);
  }
  load_fraction_ = 0.0;
  load_progress_observer_->Retarget(active);
  // Every seam that retargets the load-phase observer (tab
  // switch, split-pane switch, space switch - all of ShowTab's returns, plus
  // EnterSplit/FinishCloseSplit) is exactly the set of seams where a
  // currently-shown status bubble would be showing a hover for a page that
  // is no longer on screen - hide it instantly, no fade.
  // Switched TO a tab that is already mid-load: start a fresh run so the
  // phase reflects it rather than reporting idle.
  if (active && active->ShouldShowLoadingUI()) {
    OnActiveTabStartedLoading();
  }
}

void AtriumBrowserWindow::OnActiveTabStartedLoading() {
  glow_waits_for_load_ = true;
  load_phase_live_ = true;
  load_fraction_ = 0.0;
  PushLoadPhase(ObservedLoadTabId(), atrium::LoadPhase::kWaiting, 0.0);
}

void AtriumBrowserWindow::OnActiveTabLoadProgress(double progress) {
  const double p = std::clamp(progress, 0.0, 1.0);
  if (!load_phase_live_) {
    if (p >= 1.0) {
      return;  // stray completion for an already-idle tab
    }
    // A same-document navigation reports progress here too, bypassing the
    // ShouldShowLoadingUI gate DidStartLoading applies before re-arming.
    content::WebContents* contents =
        load_progress_observer_ ? load_progress_observer_->web_contents()
                                : nullptr;
    if (!contents || !contents->ShouldShowLoadingUI()) {
      return;
    }
    OnActiveTabStartedLoading();
  }
  // FrameTreeNode::DidStartLoading fires this synchronously, before DNS,
  // connect or TLS, with the same kInitialLoadProgress value a real report
  // never reaches (ProgressTracker starts real reports at +0.1 above it).
  if (p <= blink::kInitialLoadProgress) {
    return;
  }
  load_fraction_ = std::max(load_fraction_, p);
  PushLoadPhase(ObservedLoadTabId(), atrium::LoadPhase::kReceiving,
                load_fraction_);
}

void AtriumBrowserWindow::OnActiveTabStoppedLoading() {
  if (load_phase_live_) {
    load_phase_live_ = false;
    load_fraction_ = 1.0;
    PushLoadPhase(ObservedLoadTabId(), atrium::LoadPhase::kDone, 1.0);
  }
  // The glow held through the load (ApplyPageTint and the sampler both skip
  // a loading tab), so this is where the page's colours arrive, slowly.
  if (glow_waits_for_load_) {
    glow_waits_for_load_ = false;
    edge_tint_slow_fade_ = true;
    ApplyPageTint();
    StartAmbilightSampling();
  }
}

// Link-hover status bubble -----------------------------





void AtriumBrowserWindow::ShowStatusBubbleNow() {
  if (!ActivePane()) {
    return;
  }
  status_bubble_shown_url_ = status_bubble_pending_url_;
  EnsureHoverUrlWidget();
  // The same ElideUrl the bubble used, at a width it will not reach: that
  // keeps its display formatting while leaving the truncation to the card's
  // own CSS, which knows how wide it actually is.
  hover_url_widget_->ShowHoverUrl(base::UTF16ToUTF8(url_formatter::ElideUrl(
      status_bubble_shown_url_, gfx::FontList(), /*available_pixel_width=*/1e6)));
  VLOG(1) << "Atrium: status bubble shown (host="
          << status_bubble_shown_url_.host() << ")";
}




void AtriumBrowserWindow::SetStatusBubbleURL(content::WebContents* source,
                                             const GURL& url) {
  // Mirrors stock Browser::UpdateTargetURL's own anchor check - only the
  // active pane's WebContents may drive the visible card.
  if (!ActivePane() || source != ActivePane()->web_view()->web_contents()) {
    return;
  }
  EnsureHoverUrlWidget();
  if (url.is_empty()) {
    status_bubble_show_timer_.Stop();
    hover_url_widget_->ShowHoverUrl(std::string());
    return;
  }
  status_bubble_pending_url_ = url;
  // Retarget with no re-debounce once a card is up, the Safari-style
  // link-to-link update the bubble already did.
  if (hover_url_widget_->IsShowingForTesting()) {
    status_bubble_show_timer_.Stop();
    ShowStatusBubbleNow();
    return;
  }
  if (status_bubble_show_timer_.IsRunning()) {
    return;  // reads pending_url_ when it fires
  }
  status_bubble_show_timer_.Start(
      FROM_HERE, kStatusBubbleShowDelay,
      base::BindOnce(&AtriumBrowserWindow::ShowStatusBubbleNow,
                     weak_factory_.GetWeakPtr()));
}

content::WebContents* AtriumBrowserWindow::CommandBarContentsForTesting()
    const {
  return command_bar_widget_ ? command_bar_widget_->WebContentsForTesting()
                             : nullptr;
}

content::WebContents* AtriumBrowserWindow::FindBarContentsForTesting() const {
  return find_bar_widget_ ? find_bar_widget_->web_contents() : nullptr;
}

bool AtriumBrowserWindow::FindBarPageHoldsKeyboardForTesting() const {
  return find_bar_widget_ && find_bar_widget_->PageHoldsKeyboardForTesting();
}

void AtriumBrowserWindow::ClearFindBarPageFocusForTesting() {
  if (find_bar_widget_) {
    find_bar_widget_->ClearPageFocusForTesting();
  }
}

void AtriumBrowserWindow::FindQueryChangedForTesting(
    const std::u16string& query) {
  if (find_bar_widget_) {
    find_bar_widget_->FindQueryChangedForTesting(query);
  }
}

bool AtriumBrowserWindow::FindBarKeyIsInOwnGroupForTesting() const {
  return find_bar_widget_ && find_bar_widget_->KeyIsInOwnGroupForTesting();
}

content::WebContents* AtriumBrowserWindow::ThemeStudioContentsForTesting()
    const {
  return theme_studio_widget_ ? theme_studio_widget_->web_contents() : nullptr;
}

gfx::Rect AtriumBrowserWindow::ThemeStudioBoundsForTesting() const {
  return theme_studio_widget_ ? theme_studio_widget_->BoundsInScreen()
                              : gfx::Rect();
}

void AtriumBrowserWindow::DragThemeStudioByForTesting(int dx, int dy) {
  if (theme_studio_widget_) {
    theme_studio_widget_->DragBy(dx, dy);
  }
}

bool AtriumBrowserWindow::ThemeStudioPageHoldsKeyboardForTesting() const {
  return theme_studio_widget_ &&
         theme_studio_widget_->PageHoldsKeyboardForTesting();
}

void AtriumBrowserWindow::OnSurface_SidebarSpaceSelectedForTesting(
    const base::ListValue& args) {
  OnSurface_SidebarSpaceSelected(args);
}

void AtriumBrowserWindow::OnSurface_SidebarTabSplitForTesting(
    const base::ListValue& args) {
  OnSurface_SidebarTabSplit(args);
}

bool AtriumBrowserWindow::CommandBarHoldsKeyboardForTesting() const {
  return command_bar_widget_ &&
         command_bar_widget_->PageHoldsKeyboardForTesting();
}

bool AtriumBrowserWindow::CommandBarIsActiveForTesting() const {
  return command_bar_widget_ && command_bar_widget_->IsActiveForTesting();
}

void AtriumBrowserWindow::ClearCommandBarKeyboardForTesting() {
  if (command_bar_widget_) {
    command_bar_widget_->ClearPageFocusForTesting();
  }
}

void AtriumBrowserWindow::RetryCommandBarContentsForTesting() {
  if (command_bar_widget_) {
    command_bar_widget_->RetryContentsForTesting();
  }
}

bool AtriumBrowserWindow::CommandBarFirstResponderIsPageForTesting() const {
  return command_bar_widget_ &&
         command_bar_widget_->FirstResponderIsPageForTesting();
}

bool AtriumBrowserWindow::MaybeRunCaptureVerb(std::string_view name) {
  if (name != "captureViewport" && name != "captureFullPage" &&
      name != "captureRegion") {
    return false;
  }
  // each writes a PNG of whatever is on screen to the clipboard and
  // to Downloads, and carries no argument a check could look at. A gesture in
  // the palette is the evidence, the gate sidebarUrlSubmitted already uses.
  if (!command_bar_widget_ ||
      !command_bar_widget_->HasTransientUserActivation()) {
    VLOG(1) << "Atrium: capture verb '" << name
            << "' refused - no user gesture in the palette";
    return true;
  }
  // The physically displayed contents, for the same reason CopyCurrentPageLink
  // uses them: a window presenting another profile's tab must capture what is
  // on screen, not browser_'s own active tab.
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  auto deliver = base::BindOnce(&AtriumBrowserWindow::DeliverCapture,
                                weak_factory_.GetWeakPtr());
  if (name == "captureViewport") {
    atrium::CaptureViewport(contents, std::move(deliver));
    return true;
  }
  if (name == "captureFullPage") {
    atrium::CaptureFullPage(contents, std::move(deliver));
    return true;
  }
  if (name == "captureRegion") {
    // Two hops: the overlay resolves a rect from the user's drag, and only then
    // is there anything to capture. A cancelled drag never runs the callback,
    // so the verb ends with no capture and no confirmation, which is what a
    // cancelled selection should look like.
    BeginRegionCapture(base::BindOnce(
        [](base::WeakPtr<AtriumBrowserWindow> window,
           atrium::CaptureResultCallback done, const gfx::Rect& region) {
          if (!window) {
            return;
          }
          atrium::CaptureRegion(window->CurrentlyDisplayedWebContents(), region,
                                std::move(done));
        },
        weak_factory_.GetWeakPtr(), std::move(deliver)));
    return true;
  }
  return false;
}

void AtriumBrowserWindow::DeliverCapture(
    std::optional<std::vector<uint8_t>> png) {
  if (!png || png->empty()) {
    // Said out loud. A verb that silently does nothing on failure is
    // indistinguishable from one that is not wired up at all, which is the
    // state this whole feature was in.
    ShowTransientConfirmation(u"Screenshot failed", /*ok=*/false);
    LOG(WARNING) << "Atrium: capture produced no image";
    return;
  }

  // The clipboard wants a bitmap, and the core hands back PNG bytes - it
  // encodes because saving is the other half and a file has to be encoded
  // anyway. Decoding one 16-bit-per-channel-free PNG here is cheaper than
  // teaching the core to return both.
  const SkBitmap bitmap = gfx::PNGCodec::Decode(*png);
  if (!bitmap.isNull()) {
    ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
    scw.WriteImage(bitmap);
  }

  base::Time::Exploded local;
  base::Time::Now().LocalExplode(&local);
  const base::FilePath name(atrium::CaptureFileName(local));
  const base::FilePath dir =
      DownloadPrefs::FromBrowserContext(browser_->profile())->DownloadPath();

  // Off the UI thread: a full-page capture of a long document is megabytes, and
  // the confirmation waits for the write so it cannot name a file that is not
  // there yet.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath path, std::vector<uint8_t> bytes) {
            return base::WriteFile(path, bytes) ? path : base::FilePath();
          },
          dir.Append(name), std::move(*png)),
      base::BindOnce(
          [](base::WeakPtr<AtriumBrowserWindow> window,
             base::FilePath written) {
            if (!window) {
              return;
            }
            if (written.empty()) {
              // The clipboard copy above already happened, so this is a partial
              // success and the wording says which half survived.
              window->ShowTransientConfirmation(
                  u"Screenshot copied, not saved");
              LOG(WARNING) << "Atrium: capture copied but not saved";
              return;
            }
            // Short, in the same register as "Link copied": the status bubble
            // is one line beside the address bar, and the full generated file
            // name is longer than the bubble it would have to fit.
            window->ShowTransientConfirmation(u"Screenshot copied and saved");
            // The file name goes to the log rather than the bubble, so a run
            // can prove which file it wrote.
            VLOG(1) << "Atrium: capture saved "
                    << written.BaseName().AsUTF8Unsafe();
          },
          weak_factory_.GetWeakPtr()));
}

bool AtriumBrowserWindow::CopyCurrentPageLink(LinkCopyFormat format) {
  // The physically displayed contents (not browser_'s own active tab) so a
  // window presenting another profile's tab via the cross-profile swap copies
  // the URL actually on screen - see CurrentlyDisplayedWebContents.
  content::WebContents* contents = CurrentlyDisplayedWebContents();
  if (!contents) {
    return false;
  }
  const GURL url = contents->GetVisibleURL();
  if (url.is_empty() || !url.is_valid()) {
    return false;
  }
  // Markdown/Quote need a title; an empty one degrades to an empty
  // "[]()" link rather than failing the whole copy - same permissive posture
  // as the rest of this class's clipboard writes. Both variants escape their
  // interpolated pieces so a title with `]`/`\` or a URL with `(`/`)` can't
  // break the link boundary and silently retarget the pasted link.
  //   text: backslash-escape `\`, `[`, `]`; flatten control chars/newlines
  //         (the quote form must stay one line). UTF-8 continuation bytes
  //         (>=0x80 unsigned) are left intact.
  //   url:  percent-encode the parens that would close the (destination)
  //         early; url.spec() is already canonicalized, so parens are the
  //         only literal that breaks the boundary.
  auto escape_md_text = [](const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
      switch (c) {
        case '\\':
          out += "\\\\";
          break;
        case '[':
          out += "\\[";
          break;
        case ']':
          out += "\\]";
          break;
        default:
          out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
      }
    }
    return out;
  };
  auto escape_md_url = [](const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
      if (c == '(') {
        out += "%28";
      } else if (c == ')') {
        out += "%29";
      } else {
        out += c;
      }
    }
    return out;
  };
  std::string clipboard_text;
  switch (format) {
    case LinkCopyFormat::kPlain:
      // Same write as chrome::CopyURL (browser_commands.cc): the plain URL
      // spec to the copy/paste buffer.
      clipboard_text = url.spec();
      break;
    case LinkCopyFormat::kMarkdown:
      clipboard_text = base::StringPrintf(
          "[%s](%s)",
          escape_md_text(base::UTF16ToUTF8(contents->GetTitle())).c_str(),
          escape_md_url(url.spec()).c_str());
      break;
    case LinkCopyFormat::kQuote:
      clipboard_text = base::StringPrintf(
          "> [%s](%s)",
          escape_md_text(base::UTF16ToUTF8(contents->GetTitle())).c_str(),
          escape_md_url(url.spec()).c_str());
      break;
  }
  ui::ScopedClipboardWriter scw(ui::ClipboardBuffer::kCopyPaste);
  scw.WriteText(base::UTF8ToUTF16(clipboard_text));
  ShowTransientConfirmation(u"Link copied");
  VLOG(1) << "Atrium: copied link (host=" << url.host() << ")";
  return true;
}

void AtriumBrowserWindow::ShowTransientConfirmation(
    const std::u16string& text, bool ok, const std::string& icon_name,
    const std::string& key) {
  // Its own window. Hosted on the sidebar's document the card
  // resolved top/right against that narrow column, so it appeared in the
  // sidebar's upper right and not the window's - reported twice.
  EnsureToastOverlayWidget();
  toast_overlay_widget_->ShowConfirmation(base::UTF16ToUTF8(text), ok,
                                          icon_name, key);
  VLOG(1) << "Atrium: confirmation shown (text=" << base::UTF16ToUTF8(text)
          << ")";
}

void AtriumBrowserWindow::OnZoomChanged(bool can_show_bubble) {
  if (!can_show_bubble) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  zoom::ZoomController* zoom_controller =
      active ? zoom::ZoomController::FromWebContents(active) : nullptr;
  if (!zoom_controller) {
    return;
  }
  ShowTransientConfirmation(
      base::UTF8ToUTF16(base::NumberToString(zoom_controller->GetZoomPercent()) +
                        "%"),
      /*ok=*/true, "zoom", "zoom");
}


namespace {
// Theme Studio backdrop: a views::Background that defers to the window's
// PaintThemeBackdrop so the bitmap/grain state stays owned by the window.
// Installed on root_view_ while a theme is active (SetBackdropTheme), replaced
// by a plain solid background again when the theme is cleared.
class AtriumThemeBackground : public views::Background {
 public:
  explicit AtriumThemeBackground(AtriumBrowserWindow* window);
  AtriumThemeBackground(const AtriumThemeBackground&) = delete;
  AtriumThemeBackground& operator=(const AtriumThemeBackground&) = delete;
  ~AtriumThemeBackground() override;

  void Paint(gfx::Canvas* canvas, views::View* view) const override;

 private:
  const raw_ptr<AtriumBrowserWindow> window_;
};

AtriumThemeBackground::AtriumThemeBackground(AtriumBrowserWindow* window)
    : window_(window) {}

AtriumThemeBackground::~AtriumThemeBackground() = default;

void AtriumThemeBackground::Paint(gfx::Canvas* canvas,
                                  views::View* view) const {
  window_->PaintThemeBackdrop(canvas);
}

class AtriumAmbilightBackground : public views::Background {
 public:
  explicit AtriumAmbilightBackground(AtriumBrowserWindow* window)
      : window_(window) {}
  AtriumAmbilightBackground(const AtriumAmbilightBackground&) = delete;
  AtriumAmbilightBackground& operator=(const AtriumAmbilightBackground&) =
      delete;
  ~AtriumAmbilightBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    window_->PaintAmbilightBackdrop(canvas);
  }

 private:
  const raw_ptr<AtriumBrowserWindow> window_;
};
}  // namespace

// The native surround owns the root while the mode is on, and so does the open
// Courtyard. A space switch, a theme and a glass-alpha change each install a
// background of their own, and any of them would otherwise paint back over it.
void AtriumBrowserWindow::InstallRootBackground(
    std::unique_ptr<views::Background> background) {
  if (opaque_surround_enabled_ || surround_fade_running_ || courtyard_open_) {
    return;
  }
  root_view_->SetBackground(std::move(background));
}

// Below the anonymous namespace above: AtriumThemeBackground has to be a
// complete type here.
SkColor AtriumBrowserWindow::root_background_pixel_for_testing() {
  const gfx::Rect bounds = root_view_->GetLocalBounds();
  if (bounds.IsEmpty()) {
    return SK_ColorTRANSPARENT;
  }
  gfx::Canvas canvas(bounds.size(), 1.0f, /*is_opaque=*/false);
  if (views::Background* background = root_view_->GetBackground()) {
    background->Paint(&canvas, root_view_);
  }
  return canvas.GetBitmap().getColor(bounds.width() / 2, bounds.height() / 2);
}

SkColor AtriumBrowserWindow::root_glow_pixel_for_testing() {
  const gfx::Rect bounds = root_view_->GetLocalBounds();
  const gfx::Rect card = PageCardBoundsInRoot();
  if (bounds.IsEmpty() || card.IsEmpty()) {
    return SK_ColorTRANSPARENT;
  }
  gfx::Canvas canvas(bounds.size(), 1.0f, /*is_opaque=*/false);
  if (views::Background* background = root_view_->GetBackground()) {
    background->Paint(&canvas, root_view_);
  }
  return canvas.GetBitmap().getColor(std::max(0, card.x() - 2),
                                     card.CenterPoint().y());
}

void AtriumBrowserWindow::InstallSurroundRootBackground(
    std::unique_ptr<views::Background> background) {
  if (courtyard_open_) {
    return;
  }
  root_view_->SetBackground(std::move(background));
}

void AtriumBrowserWindow::RestoreRootBackground() {
  // The surround fade ends on this, and ending it under an open Courtyard
  // would paint the glass ground shut. The close reruns it.
  if (courtyard_open_) {
    return;
  }
  // Ahead of the theme: a surround toggled while the Courtyard was up skipped
  // its own paint, so this is where the window gets it back.
  if (surround_fade_running_) {
    root_view_->SetBackground(
        std::make_unique<AtriumSurroundFadeBackground>(this));
  } else if (opaque_surround_enabled_) {
    root_view_->SetBackground(std::make_unique<AtriumSurroundBackground>(
        SkColorSetA(static_cast<SkColor>(opaque_surround_argb_),
                    SK_AlphaOPAQUE),
        RootGlowPainter()));
  } else if (has_theme_) {
    root_view_->SetBackground(std::make_unique<AtriumThemeBackground>(this));
  } else if (ambilight_live_ && !edge_tint_target_.IsUniform()) {
    // the glow was a state the surround could cover but never hand
    // back, so native view going off dropped it to the flat tint until the
    // next changed sample.
    root_view_->SetBackground(
        std::make_unique<AtriumAmbilightBackground>(this));
  } else if (has_backdrop_tint_) {
    root_view_->SetBackground(views::CreateSolidBackground(backdrop_tint_));
  } else {
    root_view_->SetBackground(nullptr);
  }
  root_view_->SchedulePaint();
}

void AtriumBrowserWindow::SetBackdropTheme(const uint8_t* rgba,
                                           int32_t w,
                                           int32_t h,
                                           float grain,
                                           int32_t transition_ms) {
  // A rasterized theme is a space's picture, and this window belongs to no
  // space. EffectiveSpaceTint cannot refuse it - the bitmap goes to the root
  // view's own painter, never through backdrop_tint_.
  if (atrium::IsIncognitoWindow(browser_.get())) {
    return;
  }
  if (!rgba) {
    if (theme_clearing_) {
      // Already fading out - a repeated clear (review MEDIUM) must let the
      // in-flight fade finish, not collapse it to an instant pop.
      return;
    }
    if (has_theme_ && transition_ms > 0 && !theme_bitmap_.isNull() &&
        has_backdrop_tint_) {
      // fade the gradient OUT over the flat tint wash instead of
      // cutting to it - a switch from a themed space to a flat-tint one
      // popped otherwise. `has_theme_` stays true until the fade completes
      // so an interleaved SetBackdropTint (the flat tint arriving right
      // after this clear) only updates the wash it fades onto, instead of
      // stomping the painter with a solid background.
      theme_clearing_ = true;
      theme_clear_from_ = theme_fade_;
      theme_fade_t_ = 0.0;
      theme_fade_duration_ = base::Milliseconds(transition_ms);
      theme_bitmap_prev_.reset();
      theme_timer_.Start(
          FROM_HERE, kSidebarResizeAnimationTick,
          base::BindRepeating(&AtriumBrowserWindow::StepThemeAnimation,
                              weak_factory_.GetWeakPtr()));
      root_view_->SchedulePaint();
      return;
    }
    // Instant clear: drop the theme and revert to the flat tint wash.
    // backdrop_tint_ already carries backdrop_glass_alpha_; re-applying it is
    // idempotent.
    has_theme_ = false;
    theme_clearing_ = false;
    theme_timer_.Stop();
    theme_bitmap_.reset();
    theme_bitmap_prev_.reset();
    InstallRootBackground(views::CreateSolidBackground(
        SkColorSetA(backdrop_tint_, backdrop_glass_alpha_)));
    // The flat wash can be light where the theme was dark, and nothing else
    // here pushes ink, so the frost would keep the theme's answer.
    PushSidebarFrostAppearance();
    root_view_->SchedulePaint();
    return;
  }

  // Copy the caller's transient buffer into our own immutable bitmap - the
  // pointer is only valid for the duration of this call (the retired ABI
  // header). The DESTINATION must be N32 premul: ImageSkiaRep CHECKs colorType
  // == kN32_SkColorType (BGRA on macOS) and expects premultiplied alpha, so a
  // kRGBA_8888 destination FATALs on the first themed paint. The SOURCE stays
  // described exactly as the ABI contract says Swift sends it (straight-alpha
  // RGBA, tightly packed row-major); writePixels performs the swizzle and the
  // premultiply during the copy (Chromium bans raw memcpy anyway).
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32Premul(w, h));
  const SkImageInfo src_info =
      SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
  bitmap.writePixels(SkPixmap(src_info, rgba, src_info.minRowBytes()));
  bitmap.setImmutable();

  const bool first = !has_theme_;
  // an ambient/night-shift frame (transition_ms == 0) arriving
  // while ANY crossfade is still in flight - ascending entry OR descending
  // clear - must not cut that fade: swap the fade's bitmap and let it keep
  // running (review HIGH: this guard must run BEFORE the clearing reset
  // below, or a mid-clear ambient frame flipped the stepper from the
  // descending to the ascending formula at the same progress = a visible
  // jump).
  if (!first && transition_ms <= 0 && theme_timer_.IsRunning() &&
      theme_fade_ < 1.f) {
    theme_bitmap_ = bitmap;
    theme_grain_ = grain;
    root_view_->SchedulePaint();
    return;
  }
  // a REAL push lands mid-CLEAR - cancel the clear and fade the
  // new gradient in over the wash from zero. Deliberately NOT via
  // theme_bitmap_prev_ (review MEDIUM: prev paints at FULL wash alpha, so
  // the mostly-faded-out old gradient would pop back to full for a frame).
  const bool was_clearing = theme_clearing_;
  theme_clearing_ = false;
  if (was_clearing) {
    theme_bitmap_prev_.reset();
  } else {
    theme_bitmap_prev_ = theme_bitmap_;  // old current becomes the fade source
  }
  theme_bitmap_ = bitmap;
  theme_grain_ = grain;
  has_theme_ = true;
  // The tint crossfade keeps running under the theme: stopped here, the wash
  // held the previous space's tint through the fade and then jumped.
  if (first) {
    InstallRootBackground(std::make_unique<AtriumThemeBackground>(this));
  }

  if (transition_ms <= 0 || (first && !has_backdrop_tint_)) {
    // Immediate swap: ambient motion frames (transition_ms == 0) must never
    // crossfade - each is a fresh push at the current animation phase - and
    // a first theme with NO stored tint has only the untinted glass to fade
    // from, which flashes gray. A first theme WITH a tint fades in over the
    // painter's flat-tint wash.
    theme_fade_ = 1.f;
    theme_fade_t_ = 1.0;
    theme_bitmap_prev_.reset();
    theme_timer_.Stop();
  } else {
    theme_fade_ = 0.f;
    theme_fade_t_ = 0.0;
    theme_fade_duration_ = base::Milliseconds(transition_ms);
    theme_animation_start_ = base::TimeTicks::Now();
    theme_timer_.Start(
        FROM_HERE, kSidebarResizeAnimationTick,
        base::BindRepeating(&AtriumBrowserWindow::StepThemeAnimation,
                            weak_factory_.GetWeakPtr()));
  }
  root_view_->SchedulePaint();
}

void AtriumBrowserWindow::StepThemeAnimation() {
  // tick-delta progress, same stall-resilience rationale as
  // StepBackdropTintAnimation.
  theme_fade_t_ = std::min(
      1.0, theme_fade_t_ + kSidebarResizeAnimationTick / theme_fade_duration_);
  const double t = theme_fade_t_;
  const double eased = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, t);
  RepaintFloatingPanel();
  if (theme_clearing_) {
    // Fading OUT: gradient alpha descends from wherever the clear started
    // toward the flat tint wash beneath it.
    theme_fade_ = theme_clear_from_ * static_cast<float>(1.0 - eased);
    root_view_->SchedulePaint();
    if (t >= 1.0) {
      theme_timer_.Stop();
      theme_clearing_ = false;
      has_theme_ = false;
      theme_bitmap_.reset();
      theme_bitmap_prev_.reset();
      InstallRootBackground(views::CreateSolidBackground(
          SkColorSetA(backdrop_tint_, backdrop_glass_alpha_)));
      PushSidebarFrostAppearance();
      root_view_->SchedulePaint();
    }
    return;
  }
  theme_fade_ = static_cast<float>(eased);
  root_view_->SchedulePaint();
  if (t >= 1.0) {
    theme_timer_.Stop();
    theme_bitmap_prev_.reset();  // fade done - the source is no longer needed
  }
}

void AtriumBrowserWindow::PaintAmbilightBackdrop(gfx::Canvas* canvas) {
  // The base wash first, so the chrome the glow never reaches keeps the space
  // tint. backdrop_tint_ already carries backdrop_glass_alpha_.
  canvas->FillRect(root_view_->GetLocalBounds(), backdrop_tint_);
  PaintPageGlow(canvas);
}

gfx::Rect AtriumBrowserWindow::PageCardBoundsInRoot() const {
  AtriumPaneContainerView* container = root_view_->pane_container();
  gfx::Rect card = container->GetLocalBounds();
  card.Inset(container->GetInsets());
  return views::View::ConvertRectToTarget(container, root_view_, card);
}

void AtriumBrowserWindow::PaintPageGlow(gfx::Canvas* canvas) {
  // A cleared tint is four transparent edges, which the band painter would
  // still walk for nothing on every theme repaint.
  if (edge_tint_ == atrium::EdgeTint()) {
    return;
  }
  atrium::PaintEdgeGlow(
      canvas, atrium::ScaledAboutCenter(PageCardBoundsInRoot(), peek_glow_scale_),
      edge_tint_, atrium::kEdgeGlowDepth,
      static_cast<int>(ContentCardCornerRadius()));
}

void AtriumBrowserWindow::PaintPageGlowIn(gfx::Canvas* canvas,
                                          views::View* view) {
  if (!root_view_) {
    return;
  }
  gfx::ScopedCanvas scoped(canvas);
  const gfx::Point origin =
      views::View::ConvertPointToTarget(view, root_view_, gfx::Point());
  canvas->Translate(-origin.OffsetFromOrigin());
  PaintPageGlow(canvas);
}

AtriumPaneContainerView::GlowPainter AtriumBrowserWindow::RootGlowPainter() {
  return base::BindRepeating(&AtriumBrowserWindow::PaintPageGlowIn,
                             weak_factory_.GetWeakPtr());
}

void AtriumBrowserWindow::SetEdgeTint(const atrium::EdgeTint& tint) {
  // A uniform tint is what every colour source before a readback produces, and
  // the flat path already crossfades it. Switching painters for a glow whose
  // four edges agree would cost a repaint and change nothing on screen.
  // a uniform tint used to flood the whole backdrop. The band is
  // the only place a page's colour lands now, whatever its shape.
  const uint8_t alpha =
      base::ClampRound<uint8_t>(SK_AlphaOPAQUE * atrium::kEdgeGlowStrength);
  // A cleared tint (an empty pane, the last tab closed) arrives transparent.
  // It fades OUT as the colour it leaves at alpha zero: stamping the band
  // alpha on transparent black painted a black glow instead.
  edge_tint_target_ = atrium::EdgeTint::Zipped(
      edge_tint_, tint, [alpha](SkColor current, SkColor next) {
        return SkColorGetA(next) == 0 ? SkColorSetA(current, 0)
                                      : SkColorSetA(next, alpha);
      });
  // Same crossfade as the flat tint, so a sampler tick that changes the colour
  // drifts there instead of snapping. A first glow starts from a transparent
  // tint and so fades in over the flat wash rather than appearing on it.
  edge_tint_start_ = edge_tint_;
  edge_tint_t_ = 0.0;
  // Theme wins, the precedence RestoreRootBackground encodes and the same
  // guard SetBackdropTint carries. Swapping the painter here tore the theme's
  // out for good: SetBackdropTheme installs it on the FIRST push only.
  if (!has_theme_) {
    InstallRootBackground(std::make_unique<AtriumAmbilightBackground>(this));
  }
  edge_tint_timer_.Start(
      FROM_HERE, kSidebarResizeAnimationTick,
      base::BindRepeating(&AtriumBrowserWindow::StepEdgeTintAnimation,
                          weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::StepEdgeTintAnimation() {
  const base::TimeDelta duration = edge_tint_slow_fade_
                                       ? kEdgeTintLoadFadeDuration
                                       : kBackdropTintFadeDuration;
  edge_tint_t_ =
      std::min(1.0, edge_tint_t_ + kSidebarResizeAnimationTick / duration);
  const double eased =
      gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT, edge_tint_t_);
  const auto between = [eased](SkColor from, SkColor to) {
    return gfx::Tween::ColorValueBetween(eased, from, to);
  };
  edge_tint_ =
      atrium::EdgeTint::Zipped(edge_tint_start_, edge_tint_target_, between);
  root_view_->SchedulePaint();
  RepaintFloatingPanel();
  if (edge_tint_t_ >= 1.0) {
    edge_tint_timer_.Stop();
    edge_tint_slow_fade_ = false;
  }
}

void AtriumBrowserWindow::PaintThemeBackdrop(gfx::Canvas* canvas,
                                             SkAlpha tint_alpha_floor) {
  const gfx::Rect bounds = root_view_->GetLocalBounds();

  // the flat tint wash sits at the very bottom - it is what a
  // theme CLEAR fades down to (pixel-identical to the solid background the
  // painter hands back to), and what a FIRST theme fades in from (fading
  // from the untinted glass flashed gray, which is why first pushes used to
  // cut instead). backdrop_tint_ already carries backdrop_glass_alpha_.
  if (has_backdrop_tint_) {
    canvas->FillRect(bounds, SkColorSetA(backdrop_tint_,
                                         std::max<SkAlpha>(SkColorGetA(backdrop_tint_),
                                                           tint_alpha_floor)));
  }

  // The glass is translucent: an outgoing picture left under the incoming
  // one at full weight showed through, then vanished at the promotion.
  const bool scrubbing = scrub_blend_weight_ >= 0.0;
  const float weight =
      scrubbing ? static_cast<float>(scrub_blend_weight_) : 0.f;
  const float outgoing = (1.f - weight) * backdrop_glass_alpha_ / 255.f;

  const auto draw_picture = [&](const SkBitmap& bitmap, float alpha,
                                SkBlendMode mode) {
    const gfx::ImageSkia image = gfx::ImageSkia::CreateFromBitmap(bitmap, 1.f);
    cc::PaintFlags flags;
    flags.setAlphaf(alpha);
    flags.setBlendMode(mode);
    canvas->DrawImageInt(image, 0, 0, image.width(), image.height(),
                         bounds.x(), bounds.y(), bounds.width(),
                         bounds.height(), /*filter=*/true, flags);
  };
  if (!scrubbing && !theme_bitmap_prev_.isNull() && !theme_bitmap_.isNull()) {
    // Two stacked translucent pictures cover about twice the glass alpha, so
    // the window darkened and then snapped back when prev was dropped. kPlus
    // keeps the mix an exact lerp where a picture has its own alpha.
    canvas->SaveLayerAlpha(backdrop_glass_alpha_);
    draw_picture(theme_bitmap_prev_, 1.f - theme_fade_, SkBlendMode::kSrcOver);
    draw_picture(theme_bitmap_, theme_fade_, SkBlendMode::kPlus);
    canvas->Restore();
  } else {
    // Steady state (theme_fade_ == 1, prev dropped) is just the current bitmap
    // at the glass wash. A scrub fades both down together for the incoming
    // layer below.
    if (!theme_bitmap_prev_.isNull() && outgoing > 0.f) {
      draw_picture(theme_bitmap_prev_, outgoing, SkBlendMode::kSrcOver);
    }
    if (!theme_bitmap_.isNull() && outgoing > 0.f) {
      draw_picture(theme_bitmap_, theme_fade_ * outgoing,
                   SkBlendMode::kSrcOver);
    }
  }

  // A flat incoming space stages no bitmap: the wash at the bottom (live via
  // SetBackdropTintLive) is its layer, the outgoing picture fades down to it.
  if (scrubbing && weight > 0.f && !scrub_bitmap_.isNull()) {
    const gfx::ImageSkia incoming =
        gfx::ImageSkia::CreateFromBitmap(scrub_bitmap_, 1.f);
    cc::PaintFlags scrub_flags;
    scrub_flags.setAlphaf(weight * backdrop_glass_alpha_ / 255.f);
    canvas->DrawImageInt(incoming, 0, 0, incoming.width(), incoming.height(),
                         bounds.x(), bounds.y(), bounds.width(),
                         bounds.height(),
                         /*filter=*/true, scrub_flags);
  }

  // Under the grain, so the glow wears the same texture as the picture.
  PaintPageGlow(canvas);

  // Film-grain overlay: a 64x64 static noise tile (built once) repeated across
  // the backdrop, blended kOverlay at grain * 8%.
  const float grain_amount = PaintedGrain();
  if (grain_amount > 0.f) {
    if (grain_tile_.isNull()) {
      grain_tile_.allocN32Pixels(64, 64);
      for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
          const U8CPU v = base::RandIntInclusive(0, 255);
          *grain_tile_.getAddr32(x, y) = SkColorSetARGB(0xFF, v, v, v);
        }
      }
      grain_tile_.setImmutable();
    }
    const gfx::ImageSkia grain =
        gfx::ImageSkia::CreateFromBitmap(grain_tile_, 1.f);
    cc::PaintFlags grain_flags;
    grain_flags.setAlphaf(grain_amount * 0.08f);
    grain_flags.setBlendMode(SkBlendMode::kOverlay);
    canvas->TileImageInt(grain, 0, 0, bounds.x(), bounds.y(), bounds.width(),
                         bounds.height(), 1.f, SkTileMode::kRepeat,
                         SkTileMode::kRepeat, &grain_flags);
  }
}

// SetBackdropScrubBlendEnd(commit=1)'s promotion step -
// installs the staged scrub layer as the real backdrop atomically. No
// crossfade here: the live blend already carried the visual transition, so
// fading again would replay motion the user already watched under their
// finger.
float AtriumBrowserWindow::PaintedGrain() const {
  if (scrub_blend_weight_ < 0.0) {
    return theme_grain_;
  }
  const float weight = static_cast<float>(scrub_blend_weight_);
  return theme_grain_ + (scrub_grain_ - theme_grain_) * weight;
}

void AtriumBrowserWindow::PromoteScrubThemeToBackdrop() {
  theme_grain_ = scrub_grain_;
  if (!scrub_bitmap_.isNull()) {
    const bool was_themed = has_theme_;
    theme_bitmap_ = scrub_bitmap_;
    theme_bitmap_prev_.reset();
    theme_fade_ = 1.f;
    theme_fade_t_ = 1.0;
    theme_timer_.Stop();
    theme_clearing_ = false;
    has_theme_ = true;
    backdrop_tint_timer_.Stop();
    if (!was_themed) {
      InstallRootBackground(std::make_unique<AtriumThemeBackground>(this));
    }
    return;
  }
  // Flat tint incoming (NULL was staged): backdrop_tint_ already holds the
  // incoming space's live value (SetBackdropTintLive kept it current through
  // the scrub - see its comment), so committing a themed -> flat scrub just
  // means dropping the theme and reverting to the flat wash, same as
  // SetBackdropTheme's own instant-clear path.
  has_theme_ = false;
  theme_clearing_ = false;
  theme_timer_.Stop();
  theme_bitmap_.reset();
  theme_bitmap_prev_.reset();
  InstallRootBackground(views::CreateSolidBackground(
      SkColorSetA(backdrop_tint_, backdrop_glass_alpha_)));
  PushSidebarFrostAppearance();
}

// A flat outgoing space has no painter to composite the staged layer into,
// so the session opens with no bitmap of its own.
void AtriumBrowserWindow::OpenScrubThemeSession() {
  has_theme_ = true;
  scrub_opened_theme_ = true;
  theme_clearing_ = false;
  theme_timer_.Stop();
  theme_bitmap_.reset();
  theme_bitmap_prev_.reset();
  theme_fade_ = 1.f;
  theme_fade_t_ = 1.0;
  backdrop_tint_timer_.Stop();
  InstallRootBackground(std::make_unique<AtriumThemeBackground>(this));
  root_view_->SchedulePaint();
}

void AtriumBrowserWindow::RevealFloatingSidebar(bool reveal) {
  // POSTED, whole. The reveal arrives from an NSTrackingArea dispatch, and
  // building the panel's widget inside one is what crashed: constructing a
  // views::WebView registers it as an AXUpdateObserver, the adapter takes a
  // weak pointer from it, and that DCHECKed !needs_adopt_ref_ with no atrium
  // frame between AddObserver and AppKit. Posting the tracker install alone
  // was not enough - the CONSTRUCTOR still ran inside the callback.
  //
  // Latest-wins rather than a queue: a pointer crossing the strip quickly
  // produces reveal/dismiss pairs faster than they can run, and replaying
  // every one of them would animate long after the pointer settled.
  sidebar_reveal_wanted_ = reveal;
  if (sidebar_reveal_posted_) {
    return;
  }
  sidebar_reveal_posted_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AtriumBrowserWindow::ApplyFloatingSidebarReveal,
                     weak_factory_.GetWeakPtr()));
}

void AtriumBrowserWindow::PaintFloatingPanelBackdrop(
    gfx::Canvas* canvas,
    const gfx::Rect& panel_bounds_in_screen) {
  // root_view_ is nulled ahead of the panel in teardown, and a paint queued
  // before that can still land in between.
  if (!root_view_) {
    return;
  }
  // .20: the frost alone leaves 64px type unreadable over a busy page, and
  // the glass slider runs down to .02.
  constexpr SkAlpha kWashFloor = 51;
  gfx::ScopedCanvas scoped(canvas);
  canvas->Translate(root_view_->GetBoundsInScreen().origin() -
                    panel_bounds_in_screen.origin());
  if (opaque_surround_enabled_) {
    // The panel's document no longer paints the surround itself, so the
    // glow can reach it - same fill and glow as the docked root.
    canvas->FillRect(root_view_->GetLocalBounds(),
                     SkColorSetA(static_cast<SkColor>(opaque_surround_argb_),
                                 SK_AlphaOPAQUE));
    PaintPageGlow(canvas);
    return;
  }
  if (has_theme_) {
    PaintThemeBackdrop(canvas, kWashFloor);
    return;
  }
  const bool dark =
      ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
      ui::NativeTheme::PreferredColorScheme::kDark;
  const SkColor wash = has_backdrop_tint_ ? backdrop_tint_
                       : dark              ? SkColorSetRGB(42, 42, 44)
                                           : SkColorSetRGB(219, 222, 229);
  const SkAlpha alpha = has_backdrop_tint_ ? SkColorGetA(backdrop_tint_)
                                           : backdrop_glass_alpha_;
  canvas->FillRect(root_view_->GetLocalBounds(),
                   SkColorSetA(wash, std::max(alpha, kWashFloor)));
}

void AtriumBrowserWindow::RepaintFloatingPanel() {
  if (sidebar_overlay_widget_) {
    sidebar_overlay_widget_->RepaintBackdrop();
  }
}

void AtriumBrowserWindow::ApplyFloatingSidebarReveal() {
  sidebar_reveal_posted_ = false;
  const bool reveal = sidebar_reveal_wanted_;
  // The panel is a child widget, which is its own surface and so
  // composites ABOVE the page - the only place a material can go. An in-window
  // float cannot have one: views share a single surface on Mac, so there is no
  // layer between the page and the sidebar to put glass in.
  if (!sidebar_web_ || !sidebar_contents_) {
    return;
  }
  if (!reveal) {
    if (sidebar_overlay_widget_) {
      sidebar_overlay_widget_->Close();
    }
    return;
  }
  const int width = ClampSidebarOverlayWidth(LastDockedSidebarWidth());
  if (width <= 0) {
    return;
  }
  if (!sidebar_overlay_widget_) {
    sidebar_overlay_widget_ =
        std::make_unique<AtriumSidebarOverlayWidget>(this);
    sidebar_overlay_widget_->SetOnRight(root_view_->sidebar_on_right());
  }
  // Already up: this is a re-entry into the edge zone, not a new reveal. Re-
  // running Show would slide a panel that is standing still. Cancel the hide
  // instead, which is what the lab's edgehot does (swipe-lab.html:5366).
  if (sidebar_overlay_widget_->IsShowing()) {
    sidebar_overlay_widget_->OnOverlayEntered();
    return;
  }
  sidebar_overlay_widget_->Show(width);
}

void AtriumBrowserWindow::ClearSidebarLayoutWidth() {
  if (!sidebar_contents_) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.documentElement?.style.removeProperty("
      u"'--atrium-docked-width')",
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::PushSidebarLayoutWidth(int width) {
  // The page holds this width rather than laying out at 100%, so a collapse
  // slides it out instead of re-flowing the favourites and the rows on every
  // tick - the lab's fixed-width sidebar (swipe-lab.html:37). Pushed by the
  // three things that genuinely change how wide the sidebar is, and by
  // nothing else: the collapse animation deliberately does not, which is the
  // whole point of holding a width at all.
  if (!sidebar_contents_ || width <= 0) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(
          base::StringPrintf("document.documentElement?.style.setProperty("
                             "'--atrium-docked-width', '%dpx')",
                             width)),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::PushSidebarRestWidth(int width) {
  // the C++ side holds the settled width, not JS - a mid-narrow
  // re-entry (a drag that turns back and re-widens) would let JS capture an
  // in-flight width instead of the one the crossing actually restores to.
  if (!sidebar_contents_ || width <= 0) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(
          base::StringPrintf("document.documentElement?.style.setProperty("
                             "'--atrium-rest-width', '%dpx')",
                             width)),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::ClearSidebarRestWidth() {
  if (!sidebar_contents_) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      u"document.documentElement?.style.removeProperty("
      u"'--atrium-rest-width')",
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::NoteFloatingSidebarEdgeExit() {
  if (sidebar_overlay_widget_ && sidebar_overlay_widget_->IsShowing()) {
    sidebar_overlay_widget_->OnOverlayExited();
    return;
  }
  RevealFloatingSidebar(false);
}

void AtriumBrowserWindow::PublishFloatingSidebarSpan(
    std::optional<gfx::Range> span) {
  // The page draws its own window buttons while the sidebar floats: the panel
  // is a child window and covers the real ones wherever they sit. The native
  // lights stay hidden the whole time the pane is collapsed, which is what the
  // collapse already did - only the page's row changes.
  if (!sidebar_contents_) {
    return;
  }
  sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(base::StringPrintf(
          "document.documentElement?.classList.toggle("
          "'atrium-sidebar-floating', %s);"
          "document.documentElement?.classList.toggle("
          "'atrium-sidebar-on-right', %s)",
          span ? "true" : "false",
          root_view_->sidebar_on_right() ? "true" : "false")),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

std::optional<SkColor> AtriumBrowserWindow::RootBackgroundColorForTesting()
    const {
  // AtriumSurroundFadeBackground paints through PaintSurroundFade and sets no
  // colour of its own, so asking it would answer views::Background's unset
  // SK_ColorRED rather than anything on screen.
  if (surround_fade_running_) {
    return std::nullopt;
  }
  const views::Background* background = root_view_->GetBackground();
  if (!background) {
    return std::nullopt;
  }
  return background->color().ResolveToSkColor(root_view_->GetColorProvider());
}

views::View* AtriumBrowserWindow::SidebarGripForTesting() {
  return root_view_ ? root_view_->sidebar_grip() : nullptr;
}

views::View* AtriumBrowserWindow::ActivePaneRingForTesting() {
  return root_view_->pane_container()->active_pane_ring_for_testing();
}

AtriumPaneView* AtriumBrowserWindow::ActivePaneForTesting() {
  return ActivePane();
}

int AtriumBrowserWindow::ActivePaneIndexForTesting() const {
  return root_view_->pane_container()->active_pane_index();
}

views::View* AtriumBrowserWindow::SplitDividerForTesting(size_t index) {
  return root_view_->pane_container()->gutter_view_for_testing(index);
}

gfx::Rect AtriumBrowserWindow::PaneBoundsForTesting(size_t index) {
  AtriumPaneView* pane = root_view_->pane_container()->PaneAt(index);
  return pane ? pane->bounds() : gfx::Rect();
}

content::WebContents* AtriumBrowserWindow::PaneContentsForTesting(
    size_t index) {
  // LeafAt dereferences a null node past the end, so "is there a pane here" has
  // to be answered before asking for one.
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (index >= container->pane_count()) {
    return nullptr;
  }
  AtriumPaneView* pane = container->PaneAt(index);
  return pane ? pane->web_view()->web_contents() : nullptr;
}

bool AtriumBrowserWindow::PaneImmersiveForTesting(size_t index) const {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (index >= container->pane_count()) {
    return false;
  }
  return container->PaneAt(index)->IsImmersiveForTesting();
}

float AtriumBrowserWindow::PaneTileCornerRadiusForTesting(size_t index) const {
  AtriumPaneContainerView* container = root_view_->pane_container();
  if (index >= container->pane_count()) {
    return 0;
  }
  return container->PaneAt(index)->TileCornerRadiusForTesting();
}

void AtriumBrowserWindow::SetPaneContainerImmersiveForTesting(
    bool immersive) {
  root_view_->pane_container()->SetImmersive(immersive);
}

size_t AtriumBrowserWindow::PaneFocusSubscriptionCountForTesting() const {
  return pane_focus_subscriptions_.size();
}

void AtriumBrowserWindow::FinishSurroundFadeForTesting() {
  if (!surround_fade_running_) {
    return;
  }
  surround_fade_t_ = 1.0;
  StepSurroundFade();
}

bool AtriumBrowserWindow::SidebarPaneHoldsContentsForTesting() const {
  return sidebar_web_ && sidebar_web_->web_contents() != nullptr;
}

void AtriumBrowserWindow::HandSidebarToPanel() {
  if (!sidebar_overlay_widget_ || !sidebar_web_ || !sidebar_contents_) {
    return;
  }
  // One WebContents, two views over it. The docked pane lets go first: a
  // WebContents shown in two WebViews at once is not a supported state.
  sidebar_web_->SetWebContents(nullptr);
  sidebar_overlay_widget_->HoldContents(sidebar_contents_.get());
}

void AtriumBrowserWindow::DockFloatingSidebar() {
  if (!sidebar_overlay_widget_ || !sidebar_web_) {
    return;
  }
  sidebar_overlay_widget_->HoldContents(nullptr);
  sidebar_web_->SetWebContents(sidebar_contents_.get());
}

void AtriumBrowserWindow::SetSidebarWidth(double width) {
  // HIGH: a little window's root view builds with no
  // sidebar slot - a sidebar op that somehow resolves a little window must
  // no-op, not null-deref (defensive cross-ABI posture; no current caller
  // targets little windows, but the deref must not be reachable).
  // the slot, not the host - the HTML arm has a slot and no host,
  // and must still resize.
  if (!root_view_->sidebar_slot()) {
    return;
  }
  // Defensive clamp (the retired ABI header: "a stale or mismatched build must
  // not be trusted alone") - independent of whatever Swift's own
  // SpacesStore.sidebarWidthRange clamp already did. A width close enough to
  // 0 to be a float-rounding artifact reads as collapse; anything else lands
  // in the real range rather than silently collapsing on a near-zero value
  // that was never actually meant as "collapsed" (a well-behaved caller only
  // ever sends exactly 0 for that).
  const int target = width <= kSidebarCollapseThreshold
                         ? kCollapsedSidebarWidth
                         : std::clamp(base::ClampRound<int>(width),
                                      kMinSidebarWidth, kMaxSidebarWidth);
  // The traffic lights hide with the sidebar (user-decided: the lights
  // belong to the sidebar band). They return the moment the sidebar expands
  // (Cmd+S or the hover overlay). The card also regains a LEFT inset while
  // collapsed: the gutter is normally the sidebar's own trailing padding,
  // which disappears with the pane - without this flip the card touches the
  // window's left edge and the tinted border vanishes (regression the first
  // gutter-ownership change fixed and the second reintroduced).
  const bool collapsed = target == kCollapsedSidebarWidth;
  root_view_->SetCollapsedContentInsets(collapsed);
  root_view_->SetSidebarCollapsed(collapsed);
  // Sidebar on right: right-docked, the lights sit over the content
  // card's reserved band (see AtriumPaneContainerView::
  // SetCollapsedContentInsets), not the sidebar - so they must stay visible
  // through a collapse instead of hiding with the pane.
  const bool hide_lights = HidesWindowControlsWhen(collapsed);
  // The lights travel the width the pane loses, so they keep their offset from
  // its closing edge instead of fading in place. Only when a
  // collapse or an expand is one end of it - a drag-resize must not move them.
  const int slot_width = root_view_->sidebar_slot()->GetPreferredSize().width();
  // light_slide_span_ covers the reversal: a collapse turned back at 150pt is
  // neither end, and its travel would otherwise be left half-finished.
  const int light_span =
      HidesWindowControlsWhen(true) &&
              (collapsed || slot_width == kCollapsedSidebarWidth ||
               light_slide_span_ > 0)
          ? std::max(slot_width, target)
          : 0;
  window_platform_->SetWindowControlsVisible(
      !hide_lights, kSidebarResizeAnimationDuration,
      hide_lights ? base::BindOnce(
                        [](base::WeakPtr<AtriumBrowserWindow> weak) {
                          return weak && weak->HidesWindowControlsWhen(
                                             weak->IsSidebarCollapsed());
                        },
                        weak_factory_.GetWeakPtr())
                  : base::OnceCallback<bool()>());
  if (light_span > 0) {
    // Seeded now rather than left to the first tick 16ms away: a reveal
    // unhides the lights at their docked place and would flash them there.
    window_platform_->StepWindowControlsSlide(slot_width - light_span);
  }
  // Remember the last non-zero (docked) width - the
  // width the hover overlay reveals at - and dock (hide) any live overlay
  // when the pane expands back in-window (Cmd+S restore, or a drag on the
  // overlay's own content). A collapse (target 0) leaves last_docked intact
  // so the next reveal uses the pre-collapse width.
  if (target > 0) {
    // The width lives in the document's appState, so an incognito window keeps
    // its own for the session and leaves the file alone.
    if (WritesAllowed()) {
      // The store, not a member: this is the one writer that used to skip it,
      // so a Cmd+S restore after a drag brought back the pre-drag width.
      atrium_core_set_sidebar_width(target);
    }
    if (sidebar_overlay_widget_) {
      sidebar_overlay_widget_->HideForDock();
    }
  }
  if (!root_view_->sidebar_slot()) {
    return;  // Little window: no sidebar to size.
  }
  // built here rather than in the constructor because it needs a
  // sidebar slot and a live NSWindow, and this is the first moment both exist -
  // a launch seeds the stored width through this same call.
  if (!sidebar_edge_reveal_) {
    sidebar_edge_reveal_ = std::make_unique<AtriumSidebarEdgeReveal>(this);
  }
  // Told the SETTLED width, not the one on screen: the pane animates, so the
  // view would answer about the frame in flight rather than the destination.
  sidebar_edge_reveal_->OnSidebarWidthChanged(collapsed);
  // Expanding from collapsed while the overview is open: the stored width
  // needs the overview minimum, and the captured pre-overview width must be
  // the user's expand target, not the collapsed-era zero.
  int effective_target = target;
  if (sidebar_overview_open_ && !collapsed &&
      slot_width == kCollapsedSidebarWidth) {
    sidebar_width_before_overview_ = target;
    effective_target =
        EffectiveSidebarWidth(true, target, kOverviewSidebarMinWidth);
  }
  AnimateSidebarSlotWidth(effective_target);
  // A width that changed nothing starts no timer, and then nothing would ever
  // settle the hide the call above asked for.
  light_slide_span_ = sidebar_resize_timer_.IsRunning() ? light_span : 0;
  if (light_slide_span_ == 0) {
    window_platform_->SettleWindowControlsSlide();
  }
}

// The animation on its own, with none of SetSidebarWidth's bookkeeping - the
// overview widen has to animate without writing the user's width to the store.
void AtriumBrowserWindow::AnimateSidebarSlotWidth(int target, bool hold) {
  // SetSidebarWidth re-arms it below. Its other callers (the overview widen)
  // must not inherit a span measured for a different animation.
  light_slide_span_ = 0;
  const int current = root_view_->sidebar_slot()->GetPreferredSize().width();
  if (target == current) {
    return;
  }
  // Animated via sidebar_slot()'s preferred size + a timer-driven relayout,
  // not a ui::Layer transform/AnimationBuilder: NativeViewHost::Attach keeps
  // the hosted SwiftUI NSView's frame in sync with THIS View's bounds, which
  // BoxLayout (the root view's layout manager) derives from preferred size -
  // a Layer-transform animation would move the Layer without moving the
  // View's actual bounds until the animation's end, so the hosted NSView
  // wouldn't visibly track a Layer-based tween at all (it would just jump).
  // Stepping preferred size + forcing relayout each tick goes through the
  // same sanctioned path BoxLayout already owns, so this can't fight the
  // layout system the way a raw bounds override competing with the next
  // Layout() pass could.
  sidebar_resize_start_width_ = current;
  sidebar_resize_target_width_ = target;
  // the page holds a width ONLY for the length of this animation.
  // Laid out at 100% it re-flows on every tick - the favourites grid re-fills
  // its columns and the rows re-wrap - which is the "everything moves" a
  // collapse reads as. Held at a fixed width it is clipped instead and slides,
  // the way the lab's fixed-width sidebar does (swipe-lab.html:37).
  // StepSidebarResizeAnimation clears it on the last tick.
  //
  // HERE, below the equal-width early return: a call that changes nothing
  // returns before this and would otherwise leave a freeze with no animation
  // to lift it. The page then believes that width over its own viewport, and
  // every later resize leaves the contents at the stale one.
  //
  // The width to hold is whichever end is not zero - the pre-collapse width on
  // the way out, the target on the way back in.
  // No hold for the overview widen: the page anchors to its trailing edge, so
  // a hold wider than the slot hangs off the left and cuts the favourites off.
  if (hold) {
    PushSidebarLayoutWidth(sidebar_resize_target_width_ > 0
                               ? sidebar_resize_target_width_
                               : sidebar_resize_start_width_);
  } else {
    ClearSidebarLayoutWidth();
  }
  sidebar_resize_animation_start_ = base::TimeTicks::Now();
  sidebar_resize_timer_.Start(
      FROM_HERE, kSidebarResizeAnimationTick,
      base::BindRepeating(&AtriumBrowserWindow::StepSidebarResizeAnimation,
                          weak_factory_.GetWeakPtr()));
}

// Drag-resize perf: see the header comment for the full contract.
void AtriumBrowserWindow::SetSidebarWidthCommit(double width) {
  // The docked half of SetSidebarOverlayWidth, and it has to record the same
  // two things: the width the next reveal opens at, and the store. Without the
  // first the panel opens at whatever the pane was before the drag; without
  // the second the resize is forgotten at the next launch.
  SetSidebarWidthLive(width);

  // A drag with the overview open is the user choosing a width, so it becomes
  // the width the close restores. Without this the release would be undone the
  // moment the rail left the overview pane.
  if (sidebar_overview_open_) {
    sidebar_width_before_overview_ = ClampSidebarOverlayWidth(width);
    // a drag inside the Overview changes the width the close
    // restores to, so the grid's cap has to move with it.
    PushSidebarRestWidth(sidebar_width_before_overview_);
  }

  // The live half above still applies, so the grip moves in an incognito
  // window - only the appState write is refused.
  if (WritesAllowed()) {
    atrium_core_set_sidebar_width(ClampSidebarOverlayWidth(width));
  }
}

void AtriumBrowserWindow::SetSidebarOverviewOpen(bool open) {
  if (sidebar_overview_open_ == open || !root_view_->sidebar_slot()) {
    return;
  }
  sidebar_overview_open_ = open;
  if (open) {
    // The settled width, not the frame in flight: a re-entry while the close
    // is still narrowing would otherwise remember a mid-animation width.
    sidebar_width_before_overview_ =
        sidebar_resize_timer_.IsRunning()
            ? sidebar_resize_target_width_
            : root_view_->sidebar_slot()->GetPreferredSize().width();
  }
  // A collapsed pane has no width to widen or give back, and re-asserting one
  // here would slide it open behind Cmd+S. The expand goes through
  // SetSidebarWidth, which reads the store this path never wrote.
  if (sidebar_width_before_overview_ <= 0 || IsSidebarCollapsed()) {
    ClearSidebarRestWidth();
    return;
  }
  // the grid caps itself to this width from the open until the
  // close has finished narrowing, instead of reflowing at the live slot width.
  // The swipe's own .ovcross ends before the 150 ms narrow does.
  PushSidebarRestWidth(sidebar_width_before_overview_);
  // AnimateSidebarSlotWidth, not SetSidebarWidth: this is a session overlay on
  // the width, and SetSidebarWidth would write it to spaces.json.
  AnimateSidebarSlotWidth(
      EffectiveSidebarWidth(open, sidebar_width_before_overview_,
                            kOverviewSidebarMinWidth),
      /*hold=*/false);
  // A close that changes no width starts no animation, so no last tick in
  // StepSidebarResizeAnimation lifts the cap, and a later wider drag keeps it.
  if (!open && !sidebar_resize_timer_.IsRunning()) {
    ClearSidebarRestWidth();
  }
}

int AtriumBrowserWindow::SidebarWidthForTesting() const {
  views::View* slot = root_view_->sidebar_slot();
  return slot ? slot->GetPreferredSize().width() : 0;
}

void AtriumBrowserWindow::SetSidebarWidthLive(double width) {
  // HIGH: a little window's root view builds with no
  // sidebar slot - a sidebar op that somehow resolves a little window must
  // no-op, not null-deref (defensive cross-ABI posture; no current caller
  // targets little windows, but the deref must not be reachable).
  // the slot, not the host - guarding on the deleted SwiftUI arm's
  // host returned early on EVERY window that ships, so the body below (which
  // already worked through the slot) was unreachable. Same first line and same
  // seam as SetSidebarWidth.
  if (!root_view_->sidebar_slot()) {
    return;
  }
  // Defensive clamp, same posture as SetSidebarWidth's non-zero branch but
  // with no collapse form: a live drag only exists on an expanded pane
  // (Swift drops ticks while collapsed), so a near-zero artifact must land
  // at the minimum width, never read as "collapse".
  const int target = std::clamp(base::ClampRound<int>(width), kMinSidebarWidth,
                                kMaxSidebarWidth);
  // A live snap and a running ease (from an interrupted discrete
  // transition, or a stale-build fallback tick) must not fight over the
  // pane's preferred size - the drag wins.
  sidebar_resize_timer_.Stop();
  // The ease it just stopped may have been carrying the lights, and nothing
  // else would ever land them.
  ReapplyWindowControlsVisibility();
  if (!root_view_->sidebar_slot() ||
      root_view_->sidebar_slot()->GetPreferredSize().width() == target) {
    return;
  }
  // unpaced, as SidePanel::SetPanelWidth is.
  // RenderWidgetHostImpl::SynchronizeVisualProperties already drops a resize
  // the renderer has not acked, so a gate here only costs the pane frames.
  root_view_->sidebar_slot()->SetPreferredSize(gfx::Size(target, 1));
  // the card side of the reflow-lag measurement. The page logs its
  // own innerWidth against the same wall clock, and this is what that is
  // subtracted from.
  VLOG(1) << "Atrium: sidebar live width " << target;
  root_view_->SchedulePaint();
  // Keep a live peek tracking the pane, same as the animated path does.
  if (peek_widget_ && peek_widget_->IsShowing()) {
    peek_widget_->ReapplyBoundsForContentChange();
  }
}

// Sidebar on right: re-docks the sidebar to the given edge - see
// the retired ABI header's SIDEBAR POSITION doc block for the full contract.
void AtriumBrowserWindow::SetSidebarPosition(bool on_right) {
  // no sidebar, no side. Same first line and same seam as
  // SetSidebarWidth - a little window has no slot, and every re-assertion
  // below (the card's gutter, the traffic lights, the hover overlay) is about
  // where the sidebar sits.
  if (!root_view_->sidebar_slot()) {
    return;
  }
  sidebar_on_right_ = on_right;
  root_view_->SetSidebarOnRight(on_right);
  // the ai-panel docks opposite the sidebar too.
  PushAiPanelOnRight();
  PushSidebarOnRight();
  const bool collapsed = IsSidebarCollapsed();
  // Refreshes the content card's gutter/traffic-light-band inset for the
  // new side immediately - SetSidebarOnRight's InvalidateLayout re-runs
  // Layout() with whatever border is already set, but the border itself
  // (SetCollapsedContentInsets) only recomputes when explicitly called.
  root_view_->SetCollapsedContentInsets(collapsed);
  // Re-assert traffic-light visibility for the new side (mirrors
  // SetSidebarWidth's collapse branch, undoing whichever fade the previous
  // side left the lights in - no animation here, this is a discrete re-dock
  // rather than a resize drag).
  ReapplyWindowControlsVisibility();
  if (sidebar_overlay_widget_) {
    sidebar_overlay_widget_->SetOnRight(on_right);
  }
  // A dock flip moves the content card without changing WINDOW bounds, so
  // OnWidgetBoundsChanged never fires - re-anchor the card-relative status
  // bubble explicitly (same phase gating).
  // An open peek is also card-relative and won't self-heal until the next
  // content event (see ReapplyBoundsForContentChange's other call site) -
  // re-anchor it immediately, same guard as that site.
  if (peek_widget_ && peek_widget_->IsShowing()) {
    peek_widget_->ReapplyBoundsForContentChange();
  }
}

// static
void AtriumBrowserWindow::SetWindowAppearance(
    const WindowAppearance& appearance) {
  // the held width survives a call that does not carry one, and the
  // other four fields are still replaced outright.
  //
  // The width has a different source from the rest - the spaces document, not
  // settings.json - so every settings write arrives with it nullopt. Replacing
  // wholesale would drop the width the launch seeded, and the next window
  // opened after any settings edit would come up at the slot's built-in 220
  // with the file still saying otherwise.
  //
  // Only the width, deliberately. A blanket "keep what nullopt does not answer"
  // would break surroundColor, where nullopt is a real stored value (AUTOMATIC)
  // rather than a missing one - carrying an old explicit colour across an edit
  // that turned automatic back on is exactly the bug this avoids for the width.
  const std::optional<double> held_width =
      CurrentWindowAppearance().sidebar_width;
  CurrentWindowAppearance() = appearance;
  if (!appearance.sidebar_width.has_value()) {
    CurrentWindowAppearance().sidebar_width = held_width;
  }
  // A copy of the list, not the list. That covers the container only - a window
  // that closed itself off the back of one of these calls would otherwise erase
  // from the vector this loop is walking. It does NOT make a copied pointer
  // safe, so an applier that could close a WINDOW OTHER than the one it is
  // applying to would need a liveness check per entry. None of the three can.
  const std::vector<AtriumBrowserWindow*> windows = LiveWindows();
  for (AtriumBrowserWindow* window : windows) {
    window->ApplyWindowAppearance(appearance);
  }
}

// static
const WindowAppearance& AtriumBrowserWindow::CurrentAppearance() {
  return CurrentWindowAppearance();
}

void AtriumBrowserWindow::ApplyWindowAppearance(
    const WindowAppearance& appearance) {
  // The alpha first. It is a no-op until a tint has been painted, so on a
  // window that already has one this ordering repaints once rather than twice,
  // and on a brand-new one it is the pre-registration case its own doc
  // describes: the member is set and the first SetBackdropTint picks it up.
  if (appearance.glass_alpha_percent.has_value()) {
    SetGlassBackdropAlpha(*appearance.glass_alpha_percent);
  }
  // Reads the global, not the incoming field, for the constructor's call: a
  // write carrying no borderWidth replaces CurrentWindowAppearance() whole,
  // so a new window would open at the default with the field nullopt.
  if (appearance.border_width.has_value()) {
    g_content_card_inset = *appearance.border_width;
  }
  if (applied_card_inset_ != ContentCardInset()) {
    applied_card_inset_ = ContentCardInset();
    root_view_->OnContentCardInsetChanged();
    PushCardInset(ai_panel_contents_.get());
  }
  // Only on an actual side change - an optimisation, and only that. A window
  // already on the asked-for side has nothing to re-dock.
  //
  // This guard once claimed that a
  // BRAND-NEW window reads as collapsed, so docking it left would hide the
  // stoplights; both arms build the slot at kDefaultSidebarWidth, so
  // IsSidebarCollapsed() answers false here and that was never true. What the
  // guard did do was skip the default "left" and let "right" through - into a
  // re-dock that crashed on every window, new or not. A guard shaped around a
  // VALUE cannot protect an applier; the applier itself has to hold.
  if (appearance.sidebar_on_right.has_value() &&
      *appearance.sidebar_on_right != sidebar_on_right_) {
    SetSidebarPosition(*appearance.sidebar_on_right);
  }
  if (appearance.opaque_surround.has_value()) {
    const bool enabled = *appearance.opaque_surround;
    // No colour resolved while the mode is off: SetOpaqueSurround clears the
    // fill and never reads the argument, and the automatic branch below asks
    // the widget's colour provider - work every window would otherwise do at
    // construction for a mode that is off by default.
    uint32_t argb = 0;
    if (enabled) {
      argb = appearance.surround_argb.value_or(AutomaticSurroundColor());
    }
    SetOpaqueSurround(enabled ? 1 : 0, argb);
  }
  if (appearance.overlay_theme.has_value()) {
    const bool accent_changed = atrium::OverlayAccentDiffers(
        *appearance.overlay_theme, overlay_theme_);
    overlay_theme_ = *appearance.overlay_theme;
    if (overlay_theme_ == atrium::kSystemOverlayAccent) {
      // Re-derived rather than kept: the accent can have moved while another
      // one was selected, and nothing invalidated this window's copy.
      system_accent_tokens_.reset();
      EnsureSystemAccentObserver();
    }
    PushConfirmQuitPanelTheme();
    if (accent_changed) {
      PushAiPanelInk();
    }
  }
  // last, so it resizes the pane on the side the block above just
  // docked it to - SetSidebarWidth re-asserts traffic-light visibility, and
  // that answer depends on which edge the sidebar is on.
  //
  // Through SetSidebarWidth rather than around it, which is what keeps Cmd+S
  // honest: that is also the path maintaining the stored docked width, so a
  // collapse and re-expand returns to the stored width instead of the 220 the
  // slot was built at. See BrowserController::ToggleSidebar.
  //
  // No brand-new-window guard, unlike the sidebar side above. The slot is built
  // at kDefaultSidebarWidth, so this is a resize from 220 rather than a push
  // onto a window that reads as collapsed, and a stored width of exactly 220 is
  // already a no-op inside SetSidebarWidth. A collapsed width never gets this
  // far - SidebarWidthFromSpaces refuses one.
  if (appearance.sidebar_width.has_value()) {
    SetSidebarWidth(*appearance.sidebar_width);
  }
}

uint32_t AtriumBrowserWindow::AutomaticSurroundColor() const {
  return static_cast<uint32_t>(
      GetColorProvider()->GetColor(ui::kColorPrimaryBackground));
}

// sidebar-polish hover-overlay resize - see the header comment and
// the retired ABI header's SIDEBAR OVERLAY WIDTH doc block for the full
// contract.
void AtriumBrowserWindow::SetSidebarOverlayWidth(double width) {
  // The overlay's width IS the docked width being adjusted while hidden:
  // record it so the next reveal reopens at it, and so a later dock/expand
  // (Cmd+S) matches what Swift persists from the same commit. Recorded even
  // when the widget is gone or already hiding - a COMMIT must not be lost
  // just because the overlay finished hiding a frame earlier. only
  // this committing op writes it; the live drag ticks go through
  // SetSidebarOverlayWidthLive instead.
  // one width for both modes. The docked grip's release writes the
  // store (AtriumRootView::OnGripReleased) and this is the same gesture on the
  // floating side, so without this the two modes drift apart across a restart
  // and the panel reopens at a width the pane never had.
  //
  // Refused for an incognito window: the same appState field, reached from the
  // floating side.
  if (WritesAllowed()) {
    atrium_core_set_sidebar_width(ClampSidebarOverlayWidth(width));
  }
  SetSidebarOverlayWidthLive(width);
}

int AtriumBrowserWindow::LastDockedSidebarWidth() const {
  const int stored = atrium_core_sidebar_width();
  return stored > 0 ? stored : kDefaultSidebarWidth;
}

// See the header comment for the full contract.
void AtriumBrowserWindow::SetSidebarOverlayWidthLive(double width) {
  // Bounds-only by design, exactly like SetSidebarWidthLive on the docked
  // side: no last-docked bookkeeping, so an overlay drag abandoned rather
  // than committed leaves the next reveal on the pre-drag width.
  if (sidebar_overlay_widget_) {
    const int target = ClampSidebarOverlayWidth(width);
    sidebar_overlay_widget_->ResizeTo(target);
  }
}

void AtriumBrowserWindow::StepSidebarResizeAnimation() {
  const double elapsed =
      (base::TimeTicks::Now() - sidebar_resize_animation_start_) /
      kSidebarResizeAnimationDuration;
  const double t = std::clamp(elapsed, 0.0, 1.0);
  const double eased = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT, t);
  const int width =
      sidebar_resize_start_width_ +
      base::ClampRound<int>(
          eased * (sidebar_resize_target_width_ - sidebar_resize_start_width_));
  if (!root_view_->sidebar_slot()) {
    return;
  }
  root_view_->sidebar_slot()->SetPreferredSize(gfx::Size(width, 1));
  if (light_slide_span_ > 0) {
    window_platform_->StepWindowControlsSlide(width - light_slide_span_);
  }
  // Forces the relayout NOW instead of waiting for the next natural paint
  // pass, so every animation tick is actually visible - PreferredSizeChanged
  // (called by SetPreferredSize above) only schedules an async relayout.
  root_view_->DeprecatedLayoutImmediately();
  root_view_->SchedulePaint();
  // Peek mode: the peek card is sized against the web-content pane
  // (GetWebContentBoundsInScreen), which just moved with the sidebar - keep
  // a live peek in sync every tick, same as OnWidgetBoundsChanged does for
  // parent WINDOW moves/resizes (there is no bounds-changed notification for
  // an internal pane resize, so this must be pushed explicitly).
  if (peek_widget_ && peek_widget_->IsShowing()) {
    peek_widget_->ReapplyBoundsForContentChange();
  }
  if (t >= 1.0) {
    sidebar_resize_timer_.Stop();
    if (light_slide_span_ > 0) {
      light_slide_span_ = 0;
      window_platform_->SettleWindowControlsSlide();
    }
    // Back to laying out at its own viewport. The held width is for the slide
    // only - see SetSidebarWidth.
    ClearSidebarLayoutWidth();
    if (!sidebar_overview_open_) {
      ClearSidebarRestWidth();
    }
  }
}

// The code about Browser's activation state is copied from
// WebUIBrowserWindow::Show() / BrowserView::Show().
void AtriumBrowserWindow::Show() {
  // the first show waits for the sidebar's rows, or for the cap.
  // it cannot wait for the first painted frame. A window macOS does
  // not count as visible gets no frame at any opacity, so the cap always won.
  // Only a window with a sidebar sends sidebarRowsDrawn, so a little window
  // has nothing to wait for.
  if (!widget_->IsVisible() && !startup_hold_.pending() &&
      root_view_->sidebar_slot()) {
    // An adopted window builds its sidebar only on first show. Build it now so
    // its rows draw under the hold and release it early.
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(browser_.get())) {
      controller->NotifyShownIfAdopted();
    }
    startup_hold_.Begin(atrium::kStartupHoldCap,
                        base::BindOnce(&AtriumBrowserWindow::ShowNow,
                                       weak_factory_.GetWeakPtr()));
    return;
  }
  if (startup_hold_.pending()) {
    return;
  }
  ShowNow("direct");
}

void AtriumBrowserWindow::ReleaseStartupHold() {
  startup_hold_.Release();
  // Every overlay calls this before its Show(), and the palette must not take
  // key back from it and close it.
  if (command_bar_widget_) {
    command_bar_widget_->EndFocusGuard();
  }
}

void AtriumBrowserWindow::ShowNow(std::string_view reason) {
  VLOG(1) << "Atrium: startup show on " << reason;
  browser_->DidBecomeActive();
  SetExcludedFromWindowCycling(false);

  if (widget_->IsVisible()) {
    widget_->Activate();
    return;
  }

  widget_->Show();
  // the existing-Browser (adopt) ctor - Dock New Window, relaunch
  // restore, promoted popups - registers this controller before its window
  // is ever shown, so it can't fire the shown notification itself; consume
  // the deferred flag on whichever transition reaches it first. Show() takes
  // it before the hold when there is a sidebar, so this is the first for a
  // sidebar-less window. Otherwise a no-op.
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->NotifyShownIfAdopted();
  }
  // AddTabAt (before Show()) drove OnActiveTabChanged while the window was
  // still hidden, so its UpdateWebContentsVisibility(HIDDEN) call was
  // dropped by WebContentsImpl's "trust the initial visibility" guard and
  // the tab's renderer never got a real WasShown() - the page stayed a
  // blank compositor surface despite the window being on screen. Re-assert
  // VISIBLE now that the window truly is, so it does. NOTE: this only covers
  // the not-yet-visible entry into Show(); window swaps ShowInactive() first
  // and take the early return above, so ActivateWindow asserts on its own
  // (see AssertActiveTabVisible's header comment).
  AssertActiveTabVisible();
  // web_contents() (not GetWebContents()): a tab-less window (Arc parity,
  // M2c Arc-parity - Cmd+N opens with zero tabs) must show its tinted backdrop
  // through the pane's WebView, not the "owned" placeholder WebContents
  // WebView::GetWebContents() silently creates and shows (blank white) when
  // none has been explicitly set - see AssertActiveTabVisible's comment on
  // the same distinction, just above. Show() only ever runs its body the
  // FIRST time a window becomes visible (the IsVisible() early return
  // above), before any split could exist, so ActivePane() here is always
  // the primary pane - routed through the accessor anyway for consistency
  // with every other site this task repointed off the frozen web_view_
  // cache.
  if (content::WebContents* contents =
          ActivePane()->web_view()->web_contents()) {
    contents->SetInitialFocus();
  }
}

void AtriumBrowserWindow::AssertActiveTabVisible() {
  // web_contents() (not GetWebContents()): the latter silently creates an
  // owned empty WebContents when none is attached, which a zero-tab hidden
  // window being swapped in must not do.
  AtriumPaneContainerView* container = root_view_->pane_container();
  // Every pane's contents is on-screen while the widget is
  // visible, not only the strip-active one.
  for (size_t i = 0; i < container->pane_count(); ++i) {
    if (content::WebContents* contents =
            container->PaneAt(i)->web_view()->web_contents()) {
      contents->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
    }
  }
}

void AtriumBrowserWindow::ShowInactive() {
  SetExcludedFromWindowCycling(false);
  widget_->ShowInactive();
  // the only caller of this method is ActivateWindow's
  // kFirstActivation branch (atrium_browser_controller.cc), always on a
  // window that was never visible before - for an adopted controller (see
  // Show()'s comment) THIS is the real hidden->visible transition,
  // not the Show() called right after (widget_ is already visible by then,
  // so Show() takes its early return and never reaches its own check). A
  // no-op for every other controller (flag never armed) or a second call
  // here (flag already consumed).
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(browser_.get())) {
    controller->NotifyShownIfAdopted();
  }
}

bool AtriumBrowserWindow::HasRealizedNativeWindow() const {
  // Guarded on widget_ here because a torn-down window may already have
  // released it, and the platform holds it by raw pointer.
  return widget_ && window_platform_->HasRealizedWindow();
}

void AtriumBrowserWindow::SetExcludedFromWindowCycling(bool excluded) {
  excluded_from_window_cycling_ = excluded;
  window_platform_->SetExcludedFromWindowCycling(excluded);
}

void AtriumBrowserWindow::Hide() {
  SetExcludedFromWindowCycling(true);
  widget_->Hide();
}

bool AtriumBrowserWindow::IsVisible() const {
  return widget_->IsVisible();
}

void AtriumBrowserWindow::SetBounds(const gfx::Rect& bounds) {
  widget_->SetBounds(bounds);
}

void AtriumBrowserWindow::Close() {
  // A show after this activates a browser that has left the browser list, a
  // CHECK in GlobalBrowserCollection::OnBrowserActivated.
  startup_hold_.Cancel();
  widget_->Close();
}

void AtriumBrowserWindow::Activate() {
  browser_->DidBecomeActive();
  widget_->Activate();
}

void AtriumBrowserWindow::Deactivate() {
  browser_->DidBecomeInactive();
  widget_->Deactivate();
}

bool AtriumBrowserWindow::IsActive() const {
  return widget_->IsActive();
}

void AtriumBrowserWindow::FlashFrame(bool flash) {}

ui::ZOrderLevel AtriumBrowserWindow::GetZOrderLevel() const {
  return ui::ZOrderLevel::kNormal;
}

void AtriumBrowserWindow::SetZOrderLevel(ui::ZOrderLevel order) {}

gfx::NativeWindow AtriumBrowserWindow::GetNativeWindow() const {
  return widget_->GetNativeWindow();
}

bool AtriumBrowserWindow::IsOnCurrentWorkspace() const {
  // Stock BrowserView returns true unconditionally on Mac - only ChromeOS and
  // Windows have a virtual-desktop notion it can answer. Returning false made
  // ProfileBrowserCollection::FindTabbedBrowser skip every Atrium window, so
  // callers that ask "is a window already open for this profile?" got no, and
  // opened a second one. A handed-over link with the
  // little path off built a whole new space window instead of a tab.
  return true;
}

bool AtriumBrowserWindow::IsVisibleOnScreen() const {
  return false;
}

void AtriumBrowserWindow::SetTopControlsShownRatio(
    content::WebContents* web_contents,
    float ratio) {}

bool AtriumBrowserWindow::DoBrowserControlsShrinkRendererSize(
    const content::WebContents* contents) const {
  return false;
}

ui::NativeTheme* AtriumBrowserWindow::GetNativeTheme() {
  return ui::NativeTheme::GetInstanceForNativeUi();
}

const ui::ThemeProvider* AtriumBrowserWindow::GetThemeProvider() const {
  return &ThemeService::GetThemeProviderForProfile(browser_->profile());
}

const ui::ColorProvider* AtriumBrowserWindow::GetColorProvider() const {
  return widget_->GetColorProvider();
}

int AtriumBrowserWindow::GetTopControlsHeight() const {
  return 0;
}

void AtriumBrowserWindow::SetTopControlsGestureScrollInProgress(
    bool in_progress) {}

std::vector<StatusBubble*> AtriumBrowserWindow::GetStatusBubbles() {
  return {};
}

void AtriumBrowserWindow::UpdateTitleBar() {
  widget_->UpdateWindowTitle();
}

void AtriumBrowserWindow::UpdateLoadingAnimations(bool is_visible) {}

void AtriumBrowserWindow::SetStarredState(bool is_starred) {}

bool AtriumBrowserWindow::IsTabModalPopupDeprecated() const {
  return false;
}

void AtriumBrowserWindow::SetIsTabModalPopupDeprecated(
    bool is_tab_modal_popup_deprecated) {}

void AtriumBrowserWindow::OnActiveTabChanged(content::WebContents* old_contents,
                                             content::WebContents* new_contents,
                                             int index,
                                             int reason) {
  // browser_'s content is NOT always shown in browser_'s own window.
  // After a cross-profile in-window swap this window can be its
  // slot's PRESENTER while a different controller is the slot's visible one -
  // this window is the one on screen, showing THAT controller's tab, and
  // browser_'s own active tab is attached to no pane at all. Chrome routes
  // every active-tab change through here, so an unconditional ShowTab hands
  // browser_'s tab to the pane the user is watching and silently evicts the tab
  // actually on screen. Single-embed, so nothing aborts; it just replaces the
  // page in front of the user.
  //
  // VM-reproduced: after a cross-profile space switch, Cmd+Y. IDC_SHOW_HISTORY
  // is not in AtriumNativeWidgetMac::CommandTarget's rewrite list, so unlike
  // Cmd+T/Cmd+W it dispatches on browser_ - the PRESENTER's own, off-screen
  // browser - and opens a foreground tab there. This callback ran first
  // (Browser registers its TabStripModelObserver before BrowserController does)
  // and repainted the on-screen pane with profile 1's History page while the
  // sidebar still showed the other space.
  //
  // Resolved through the SAME routing helper BrowserController::
  // OnTabStripModelChanged uses, so there is one decision and not two that can
  // drift apart. std::nullopt (not an Atrium-controlled Browser) keeps the
  // unconditional behavior. Deliberately narrow: this only skips the "displayed
  // nowhere" state and leaves the visible-via-a-swap case exactly as it is
  // - there this window's attach is undone by
  // OnTabStripModelChanged's release-then-attach, and widening the guard would
  // change that tested sequence on a crash-adjacent path.
  const std::optional<AtriumBrowserWindow*> displaying =
      BrowserController::DisplayingWindowForBrowser(browser_.get());
  const bool displayed_nowhere =
      displaying.has_value() && *displaying == nullptr;
  if (!displayed_nowhere) {
    ShowTab(new_contents);
  }
  // A2 re-probe fix (found via VM probe of finding 3, not one of the
  // review's numbered findings): while split, the OTHER pane's tab going
  // strip-inactive does NOT mean it stops being shown - ShowTab's
  // other-pane branch only flips which pane is active (plan section 1:
  // "no attach/detach happens... both stay attached"). Unconditionally
  // hiding `old_contents` here blanked the now-inactive-but-still-visible
  // pane (its compositor stops submitting frames on HIDDEN) - reproduced by
  // focus-clicking the secondary pane (finding 3's own fix) then clicking
  // back into the primary pane. Skip the hide when `old_contents` is still a
  // live split pane (any of them, not only the first two).
  if (old_contents && !IsSplitPaneContents(old_contents)) {
    old_contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
  }
  // `IsVisible()` asks about this WINDOW, which in the skipped state
  // above is on screen showing another controller's tab - `new_contents` is not
  // in any pane, so waking its renderer would be wrong.
  if (IsVisible() && !displayed_nowhere) {
    new_contents->UpdateWebContentsVisibility(content::Visibility::VISIBLE);
  } else {
    new_contents->UpdateWebContentsVisibility(content::Visibility::HIDDEN);
  }
  // This also fires for a space switch (SelectSpace
  // activates the new space's front tab), so this one hook re-pushes both
  // an already-open notes pane and the ai-panel's attached target.
  PushNoteToNotesContents();
  PushAiPanelTarget();
  active_tab_observer_->Retarget(new_contents);
  // Only the navigation observer injects vim-page.js, and it watched the tab
  // already in front, so a tab loaded in the background had no vim and f
  // painted nothing. The reset it tails into drops the stale mode.
  if (VimModeEnabled()) {
    InstallVimOnActiveTab();
  }
  ReanchorMarginaliaOnActiveTab();
  PushSidebarUrl();
  // an active-tab switch can change whether the devbar should show
  // (a different tab's localhost-ness) without any navigation firing.
  PushDevBarState();
  // The tint's own case of the same thing: a tab already loaded commits
  // nothing when it comes to the front, so a reload was the only gesture that
  // moved the chrome off the last page to navigate.
  ApplyPageTint();
  // Here rather than only in UpdateToolbar: Browser calls that one before this
  // hook swaps the pane, so it would read the tab that just left.
  PushNavAbility();
  // The sampler follows the tab in front, so it restarts on the new one.
  StartAmbilightSampling();
}

void AtriumBrowserWindow::OnTabDetached(content::WebContents* contents,
                                        bool was_active) {
  // A tab handed to another window must not carry a live session with it, and
  // a detach does not always reach ShowTab.
  if (find_bar_widget_) {
    find_bar_widget_->EndSessionAndClose();
  }
  // the last tab leaves no active tab behind, so no navigation and
  // no activation ever re-runs the tint. This is the only hook that sees it.
  if (was_active && browser_ &&
      !browser_->tab_strip_model()->GetActiveWebContents()) {
    // The pane still holds the closing tab here, so a read off the pane
    // would keep its colour. The strip says the page is gone.
    ClearPageTint();
  }
}

gfx::Size AtriumBrowserWindow::GetContentsSize() const {
  // Routed through ActivePane() (rather than the cached web_view_
  // member, which stays pinned to the PRIMARY pane forever) so this
  // reflects whichever pane is actually authoritative.
  return ActivePane()->web_view()->GetContentsBounds().size();
}

void AtriumBrowserWindow::SetContentsSize(const gfx::Size& size) {}

void AtriumBrowserWindow::UpdatePageActionIcon(PageActionIconType type) {}

views::BubbleAnchor AtriumBrowserWindow::GetAutofillBubbleAnchor() const {
  // Same "active content pane" anchor the status bubble uses (see
  // UpdateStatusBubbleWidget's comment) - a chromeless window has no
  // toolbar buttons, so this is the closest stand-in for
  // ToolbarButtonProvider::GetBubbleAnchor(). Falls back to an unanchored
  // bubble when there is no active pane, same fallback
  // sharing_hub_window_controller.cc uses when its ToolbarButtonProvider
  // lookup comes back null. root_view_ is checked first (rather than just
  // calling ActivePane(), which derefs root_view_ unconditionally) because
  // ~AtriumBrowserWindow nulls root_view_ before autofill_bubble_handler_ is
  // torn down - see its comment - so this callback must survive that gap.
  if (!root_view_) {
    return views::BubbleAnchor();
  }
  // the ANCHOR is a second, independent resolution from the command
  // target. A tab-scoped command now dispatches on the VISIBLE browser
  // (AtriumNativeWidgetMac::CommandTarget), and its bubble is then handled by
  // BrowserWindow::FromBrowser on that same Browser - which is the visible
  // controller's OWN window, swapped out and off screen during a
  // cross-profile in-window swap. Anchoring there would put the bubble on a
  // window the user cannot see. Resolve the presenter instead, the same
  // answer ModalDialogHost::EffectiveWindow uses, so content and anchor can
  // diverge. Falls back to this window when nothing presents the slot (the
  // no-swap case, byte-identical to the previous unconditional ActivePane).
  const AtriumBrowserWindow* host =
      BrowserController::PresentedWindowForBrowser(browser_.get());
  if (!host || !host->root_view_) {
    host = this;
  }
  AtriumPaneView* pane = host->ActivePane();
  return pane ? views::BubbleAnchor(pane) : views::BubbleAnchor();
}

autofill::AutofillBubbleHandler*
AtriumBrowserWindow::GetAutofillBubbleHandler() {
  if (!autofill_bubble_handler_) {
    autofill_bubble_handler_ =
        std::make_unique<atrium::AtriumAutofillBubbleHandler>(
            base::BindRepeating(&AtriumBrowserWindow::GetAutofillBubbleAnchor,
                                base::Unretained(this)));
  }
  return autofill_bubble_handler_.get();
}

void AtriumBrowserWindow::ExecutePageActionIconForTesting(
    PageActionIconType type) {}

LocationBar* AtriumBrowserWindow::GetLocationBar() const {
  return location_bar_.get();
}

void AtriumBrowserWindow::SetFocusToLocationBar(bool is_user_initiated) {}

void AtriumBrowserWindow::UpdateReloadStopState(bool is_loading, bool force) {}

// Atrium draws no toolbar, but this is where Browser::NavigationStateChanged
// lands for the active tab, so it is the signal that the history moved.
void AtriumBrowserWindow::UpdateToolbar(content::WebContents* contents) {
  PushNavAbility();
}

bool AtriumBrowserWindow::UpdateToolbarSecurityState() {
  return false;
}

void AtriumBrowserWindow::UpdateCustomTabBarVisibility(bool visible,
                                                       bool animate) {}

void AtriumBrowserWindow::ResetToolbarTabState(content::WebContents* contents) {}

void AtriumBrowserWindow::FocusToolbar() {}

void AtriumBrowserWindow::ToolbarSizeChanged(bool is_animating) {}

void AtriumBrowserWindow::TabDraggingStatusChanged(bool is_dragging) {}

void AtriumBrowserWindow::LinkOpeningFromGesture(
    WindowOpenDisposition disposition) {}

void AtriumBrowserWindow::FocusAppMenu() {}

void AtriumBrowserWindow::FocusWebContentsPane() {
  // Mirrors BrowserView::FocusWebContentsPane: RequestFocus() routes through
  // the focus manager, which hands focus to the web contents' native view.
  // ActivePane() rather than the cached web_view_ member so this
  // focuses whichever pane is actually authoritative.
  ActivePane()->web_view()->RequestFocus();
}

bool AtriumBrowserWindow::IsTabStripEditable() const {
  return true;
}

void AtriumBrowserWindow::DisableTabStripEditingForTesting() {}

bool AtriumBrowserWindow::IsToolbarVisible() const {
  return false;
}

bool AtriumBrowserWindow::IsToolbarShowing() const {
  return false;
}

bool AtriumBrowserWindow::IsLocationBarVisible() const {
  return true;
}

void AtriumBrowserWindow::ShowUpdateChromeDialog() {}

void AtriumBrowserWindow::ShowIntentPickerBubble(
    std::vector<apps::IntentPickerAppInfo> app_info,
    bool show_stay_in_chrome,
    bool show_remember_selection,
    apps::IntentPickerBubbleType bubble_type,
    const std::optional<url::Origin>& initiating_origin,
    IntentPickerResponse callback) {}

void AtriumBrowserWindow::ShowBookmarkBubble(const GURL& url,
                                             bool already_bookmarked) {}

#if BUILDFLAG(IS_CHROMEOS)
void AtriumBrowserWindow::ToggleMultitaskMenu() {}
#endif  // BUILDFLAG(IS_CHROMEOS)

ShowTranslateBubbleResult AtriumBrowserWindow::ShowTranslateBubble(
    content::WebContents* contents,
    translate::TranslateStep step,
    const std::string& source_language,
    const std::string& target_language,
    translate::TranslateErrors error_type,
    bool is_user_gesture) {
  return ShowTranslateBubbleResult::kBrowserWindowNotValid;
}

DownloadBubbleUIController*
AtriumBrowserWindow::GetDownloadBubbleUIController() {
  return nullptr;
}

void AtriumBrowserWindow::ConfirmBrowserCloseWithPendingDownloads(
    int download_count,
    Browser::DownloadCloseType dialog_type,
    base::OnceCallback<void(bool)> callback) {}

void AtriumBrowserWindow::ShowAppMenu() {}

void AtriumBrowserWindow::PreHandleDragUpdate(
    const content::DropData& drop_data,
    const gfx::PointF& point) {}

void AtriumBrowserWindow::PreHandleDragExit() {}

void AtriumBrowserWindow::HandleDragEnded() {}

content::KeyboardEventProcessingResult
AtriumBrowserWindow::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  atrium::VimKeyInputs in;
  in.vim_enabled = CurrentVimModeEnabled();
  in.page_in_normal_mode = vim_page_normal_mode_;
  in.is_key_down =
      event.GetType() == input::NativeWebKeyboardEvent::Type::kRawKeyDown ||
      event.GetType() == input::NativeWebKeyboardEvent::Type::kKeyDown;
  in.command = (event.GetModifiers() & blink::WebInputEvent::kMetaKey) != 0;
  in.control = (event.GetModifiers() & blink::WebInputEvent::kControlKey) != 0;
  in.option = (event.GetModifiers() & blink::WebInputEvent::kAltKey) != 0;
  in.shift = (event.GetModifiers() & blink::WebInputEvent::kShiftKey) != 0;
  in.character = VimCharacterOf(event);
  in.page_capturing = vim_page_capturing_;
  in.focus_in_child_frame = VimFocusInChildFrame(browser_);

  if (atrium::RouteVimKey(in) != atrium::VimKeyRoute::kStealForVim) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  RunVimVerb(in.character);
  return content::KeyboardEventProcessingResult::HANDLED;
}

// Everything the browser does once RouteVimKey has said to steal. Separate so
// a test drives the same path a keystroke does.
void AtriumBrowserWindow::RunVimVerb(const std::string& character) {
  // find is a browser verb. Atrium's own bar owns the query and
  // takes focus itself, so these never reach the page.
  if (character == "/") {
    OpenFindBar();
    return;
  }
  if (character == "n" || character == "N") {
    FindNextMatch(/*forward=*/character == "n");
    return;
  }
  // Hints are allocated across frames, so the browser starts the pass. Once
  // they are up the page owns every key until it releases.
  if (!vim_page_capturing_ && (character == "f" || character == "F")) {
    ShowVimHints();
    return;
  }
  SendVimKey(character);
}

// the page names a browser verb, this runs it. False means the
// answer was not a verb at all, so the caller treats it as a stale-mode veto.
bool AtriumBrowserWindow::RunVimBrowserVerb(const std::string& answer) {
  if (answer == "close-tab") {
    // Not chrome::CloseTab: a favourite parks instead of closing, and only
    // CloseSidebarTab knows that.
    content::WebContents* active =
        browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
    if (active) {
      CloseSidebarTab(base::NumberToString(atrium::TabId(active)));
    }
    return true;
  }
  const std::optional<atrium::NativeHotkeyAction> action =
      atrium::NativeActionForVimAnswer(answer);
  if (!action) {
    return false;
  }
  // True even when the action declines. A space that will not cycle is still
  // not a character to type into the page.
  if (atrium::BrowserController* controller = DisplayedController()) {
    atrium::ExecuteNativeHotkeyAction(controller, *action);
  }
  return true;
}

content::KeyboardEventProcessingResult
AtriumBrowserWindow::RouteVimKeyForTesting(const std::string& character,
                                           bool control) {
  atrium::VimKeyInputs in;
  in.vim_enabled = CurrentVimModeEnabled();
  in.page_in_normal_mode = vim_page_normal_mode_;
  in.page_capturing = vim_page_capturing_;
  in.focus_in_child_frame = VimFocusInChildFrame(browser_);
  in.is_key_down = true;
  in.control = control;
  in.character = character;
  return atrium::RouteVimKey(in) == atrium::VimKeyRoute::kStealForVim
             ? content::KeyboardEventProcessingResult::HANDLED
             : content::KeyboardEventProcessingResult::NOT_HANDLED;
}

bool AtriumBrowserWindow::HandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  // checked ahead of ShouldHandleKeyboardEvent below, which
  // exists to gate the system-menu redispatch further down and would
  // otherwise require a real OS keydown event this doesn't need.
  if (mirror_contents_ && event.windows_key_code == ui::VKEY_ESCAPE) {
    EndMirror("escape");
    return true;
  }

  if (!ShouldHandleKeyboardEvent(event, *window_platform_)) {
    VLOG(1) << "Atrium: HandleKeyboardEvent entered should_handle=0 type="
            << static_cast<int>(event.GetType())
            << " skip_if_unhandled=" << event.skip_if_unhandled;
    return false;
  }

  // On Mac keyboard shortcuts are caught by the menu entries, so shortcuts
  // the renderer declined must be redispatched to the system for the menu to
  // catch them. Mirrors WebUIBrowserWindow::HandleKeyboardEvent.
  //
  // This WebContents' delegate stays
  // `browser_` for its OWN (possibly foreign, possibly off-screen) profile
  // even while its content is being presented in-place inside a DIFFERENT
  // slot-mate's window (BrowserController::ActivateWindow's in-window swap
  // branch) - `browser_` is never reassigned by that swap, only which
  // window's pane the content is attached to. `this` (browser_->window())
  // is therefore not necessarily the window the user can actually see;
  // redispatching a reserved shortcut like Cmd+Q to an off-screen NSWindow
  // would otherwise silently swallow it. PresentedWindowForBrowser resolves
  // the slot's real on-screen presenter (falling back to `this` when
  // browser_ is unknown to BrowserController, or when this window already
  // IS the presenter - the common case, and the flag-off case).
  AtriumBrowserWindow* presented_window =
      BrowserController::PresentedWindowForBrowser(browser_);
  gfx::NativeWindow target_window = presented_window
                                        ? presented_window->GetNativeWindow()
                                        : GetNativeWindow();

  const bool redispatched =
      window_platform_->RedispatchKeyEvent(target_window, event);
  VLOG(1) << "Atrium: HandleKeyboardEvent entered should_handle=1 redispatched="
          << redispatched;
  return redispatched;
}

std::unique_ptr<FindBar> AtriumBrowserWindow::CreateFindBar() {
  // Atrium's chromeless window never registers a FindBarOwner, and Atrium's
  // own bar answers IDC_FIND / IDC_FIND_NEXT / IDC_FIND_PREVIOUS. The one
  // reach left is Browser::OnTabStripModelChanged's kInserted branch, which
  // builds a FindBarController for a tab whose find session is still live.
  // The bar closes on a tab or space switch so no tab moves with one.
  // FindBarHost's ctor dereferences find_bar_owner unconditionally
  // (find_bar_host.cc:143-145), so CHECK rather than let it null-deref.
  FindBarOwner* find_bar_owner = browser_->GetFeatures().find_bar_owner();
  CHECK(find_bar_owner);
  return std::make_unique<FindBarHost>(find_bar_owner);
}

web_modal::WebContentsModalDialogHost*
AtriumBrowserWindow::GetWebContentsModalDialogHost() {
  return modal_dialog_host_.get();
}

web_modal::WebContentsModalDialogHost*
AtriumBrowserWindow::GetWebContentsModalDialogHostFor(
    content::WebContents* web_contents) {
  return modal_dialog_host_.get();
}

void AtriumBrowserWindow::ShowAvatarBubbleFromAvatarButton(
    bool is_source_accelerator) {}

void AtriumBrowserWindow::MaybeShowProfileSwitchIPH() {}

void AtriumBrowserWindow::MaybeShowSupervisedUserProfileSignInIPH() {}

void AtriumBrowserWindow::ShowHatsDialog(
    const std::string& site_id,
    const std::optional<std::string>& hats_histogram_name,
    const std::optional<uint64_t> hats_survey_ukm_id,
    base::OnceClosure success_callback,
    base::OnceClosure failure_callback,
    const SurveyBitsData& product_specific_bits_data,
    const SurveyStringData& product_specific_string_data) {}

ExclusiveAccessContext* AtriumBrowserWindow::GetExclusiveAccessContext() {
  return exclusive_access_context_.get();
}

std::string AtriumBrowserWindow::GetWorkspace() const {
  return std::string();
}

bool AtriumBrowserWindow::IsVisibleOnAllWorkspaces() const {
  return false;
}

void AtriumBrowserWindow::ShowEmojiPanel() {}

std::unique_ptr<content::EyeDropper> AtriumBrowserWindow::OpenEyeDropper(
    content::RenderFrameHost* frame,
    content::EyeDropperListener* listener) {
  return nullptr;
}

void AtriumBrowserWindow::ShowCaretBrowsingDialog() {}

void AtriumBrowserWindow::CreateTabSearchBubble() {}

void AtriumBrowserWindow::CloseTabSearchBubble() {}

void AtriumBrowserWindow::ShowIncognitoClearBrowsingDataDialog() {}

void AtriumBrowserWindow::ShowIncognitoHistoryDisclaimerDialog() {}

bool AtriumBrowserWindow::IsUnframedModeEnabled() const {
  return false;
}

bool AtriumBrowserWindow::GetCanResize() {
  return widget_delegate_->CanResize();
}

ui::mojom::WindowShowState AtriumBrowserWindow::GetWindowShowState() const {
  if (IsMaximized()) {
    return ui::mojom::WindowShowState::kMaximized;
  } else if (IsMinimized()) {
    return ui::mojom::WindowShowState::kMinimized;
  } else if (IsFullscreen()) {
    return ui::mojom::WindowShowState::kFullscreen;
  } else {
    return ui::mojom::WindowShowState::kDefault;
  }
}

void AtriumBrowserWindow::ShowChromeLabs() {}

BrowserView* AtriumBrowserWindow::AsBrowserView() {
  return nullptr;
}

gfx::Rect AtriumBrowserWindow::GetBounds() const {
  return widget_->GetWindowBoundsInScreen();
}

bool AtriumBrowserWindow::IsMaximized() const {
  return widget_->IsMaximized();
}

bool AtriumBrowserWindow::IsMinimized() const {
  return widget_->IsMinimized();
}

bool AtriumBrowserWindow::IsFullscreen() const {
  return widget_->IsFullscreen();
}

bool AtriumBrowserWindow::HidesWindowControlsWhen(bool collapsed) const {
  return collapsed && !root_view_->sidebar_on_right() && !IsFullscreen();
}

// Four things move the answer: the pane's width, the dock side, full screen,
// and a drag that stops the ease mid-travel. Whichever lands last owns it, so
// each re-asks here rather than trusting the answer it gave on the way in.
void AtriumBrowserWindow::ReapplyWindowControlsVisibility() {
  if (!root_view_ || !root_view_->sidebar_slot()) {
    return;
  }
  light_slide_span_ = 0;
  // The style mask leads IsFullscreen() on the way in by about 60ms, and a
  // window that keeps its lights a little too long costs nothing next to one
  // in full screen with no way out by mouse.
  const bool hide = HidesWindowControlsWhen(IsSidebarCollapsed()) &&
                    !window_platform_->IsWindowFullScreen();
  window_platform_->SetWindowControlsVisible(!hide, base::TimeDelta(),
                                             base::OnceCallback<bool()>());
}

bool AtriumBrowserWindow::WindowControlsVisibleForTesting() const {
  return window_platform_->AreWindowControlsVisible();
}

gfx::Rect AtriumBrowserWindow::GetRestoredBounds() const {
  // views::Widget (unlike BrowserWidget) has no GetWindowPlacement(); its
  // public GetRestoredBounds() already reports exactly this.
  return widget_->GetRestoredBounds();
}

ui::mojom::WindowShowState AtriumBrowserWindow::GetRestoredState() const {
  // Simplified vs BrowserView's saved-placement lookup (no native persisted
  // placement to consult in this skeleton): report the show state to which
  // Restore() would return the window.
  return IsMaximized() ? ui::mojom::WindowShowState::kMaximized
                       : ui::mojom::WindowShowState::kDefault;
}

void AtriumBrowserWindow::Maximize() {
  widget_->Maximize();
}

void AtriumBrowserWindow::Minimize() {
  widget_->Minimize();
}

void AtriumBrowserWindow::Restore() {
  widget_->Restore();
}

void AtriumBrowserWindow::CloseUserInitiated() {
  // The override is one-shot - CloseWithReason MOVES it - so re-arm before
  // every close started here. Without this the close falls into
  // AtriumClientView's AppKit-only branch instead of the handler below.
  widget_->MakeCloseSynchronous(base::BindOnce(
      &AtriumBrowserWindow::OnWindowCloseRequested, base::Unretained(this)));
  // Retired at arm time, not in the handler: a re-press after a cancelled
  // prompt would otherwise clear the flag this call is about to set.
  user_initiated_close_deferred_ = false;
  // The only evidence a scenario has that Cmd+Shift+W reached the window at
  // all. Worded apart from the line OnWindowCloseRequested logs, so a count
  // of either means one thing.
  VLOG(1) << "Atrium: user-initiated window close requested";
  user_initiated_close_post_pending_ = false;
  user_initiated_close_pending_ = true;
  Close();
}

views::CloseRequestResult AtriumBrowserWindow::OnAppKitCloseRequested() {
  switch (atrium::ChooseAppKitCloseAction(expect_commit_close_,
                                          user_initiated_close_post_pending_)) {
    case atrium::AppKitCloseAction::kLetCommitThrough:
      expect_commit_close_ = false;
      return views::CloseRequestResult::kCanClose;
    case atrium::AppKitCloseAction::kAlreadyStarting:
      break;
    case atrium::AppKitCloseAction::kStartUserInitiated:
      user_initiated_close_post_pending_ = true;
      // Posted, not inline: this returns into AppKit's own close machinery,
      // which has to unwind before the browser is torn down.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&AtriumBrowserWindow::CloseUserInitiated,
                                    weak_factory_.GetWeakPtr()));
      break;
  }
  // AppKit must not close the NSWindow itself - the posted close does it
  // through the Views path, which is what tells the Browser.
  return views::CloseRequestResult::kCannotClose;
}

void AtriumBrowserWindow::OnWindowCloseRequested(
    views::Widget::ClosedReason close_reason) {
  // A close the user cancelled at a beforeunload prompt calls nothing back,
  // so an AUTOMATIC close is the first thing that can notice and retire it.
  // A re-press retires it earlier, in CloseUserInitiated.
  if (atrium::UserInitiatedCloseWasAbandoned(
          user_initiated_close_deferred_,
          browser_->IsAttemptingToCloseBrowser())) {
    user_initiated_close_deferred_ = false;
    user_initiated_close_pending_ = false;
  }

  // TODO(atrium): don't close a window during tab dragging, once dragging is
  // supported. Mirrors WebUIBrowserWindow::OnWindowCloseRequested.
  if (!browser_->HandleBeforeClose()) {
    user_initiated_close_deferred_ = user_initiated_close_pending_;
    widget_->MakeCloseSynchronous(base::BindOnce(
        &AtriumBrowserWindow::OnWindowCloseRequested, base::Unretained(this)));
    return;
  }
  user_initiated_close_deferred_ = false;

  // Last-tab-out interception: an empty
  // strip reaching here means the last tab either moved to another Atrium
  // window or was genuinely deleted - either way this window stays open
  // showing the space's cold tabs instead of disappearing. The gate is
  // ShouldKeepWindowOpen, whose inputs each carry their own reason. It must
  // be checked BEFORE OnWindowClosing(): that call against an empty strip
  // unconditionally schedules Browser destruction (browser.cc's else-branch
  // sets is_delete_scheduled_), and there is no changing course after it.
  //
  // Known residual (documented, not handled): profile deletion
  // (DeleteProfileHelper -> CloseAllBrowsersWithProfile) also reaches here
  // without IsTryingToQuit and would be suppressed; there is no signal for
  // "this close is a profile-scoped sweep" on this call stack. Atrium has
  // no profile-deletion UI today - revisit if one is added.
  //
  // Known bypasses of this funnel: ~Browser()'s !is_delete_scheduled_
  // fallback calls Browser::OnWindowClosing() directly (process teardown -
  // no window left to keep open, and IsTryingToQuit() is true for any real
  // quit by then). The unload-handler path (UnloadController) is NOT a
  // bypass: its CloseAllTabs drain empties the strip, Browser::
  // TabStripEmpty posts window_->Close(), and MakeCloseSynchronous routes
  // that back through this method. If a new direct OnWindowClosing() caller
  // ever appears, the consequence is only the emptied window closing early
  // (UX bug), never persisted-tab deletion - that is gated per-tab in the
  // controller's kRemoved handler, independent of this check.
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser_.get());
  atrium::WindowCloseInputs close_inputs;
  close_inputs.tab_strip_empty = browser_->tab_strip_model()->empty();
  close_inputs.space_window = controller && !controller->IsLittle();
  close_inputs.trying_to_quit = browser_shutdown::IsTryingToQuit();
  close_inputs.off_the_record = browser_->profile()->IsOffTheRecord();
  close_inputs.user_initiated = user_initiated_close_pending_;
  // Consumed on this pass. A close that drains tabs first comes back through
  // AtriumClientView, not here, because CloseWithReason spent the override.
  user_initiated_close_pending_ = false;
  if (atrium::ShouldKeepWindowOpen(close_inputs)) {
    // Consume unconditionally: now that BOTH empty-strip reasons keep the
    // window open, the flag only distinguishes log evidence - but it must
    // still be cleared here so it can't go stale and leak into a later,
    // unrelated close decision.
    const bool move_emptied = controller->ConsumeMoveOutEmptyFlag();
    VLOG(1) << "Atrium: suppressing auto-close, last tab "
            << (move_emptied ? "moved to another window"
                             : "deleted (window stays open showing cold "
                               "tabs)");
    widget_->MakeCloseSynchronous(base::BindOnce(
        &AtriumBrowserWindow::OnWindowCloseRequested, base::Unretained(this)));
    return;
  }
  if (close_inputs.user_initiated) {
    // One literal, not two joined: a scenario's pattern guard greps the
    // source for the line it asserts, and a split string hides from it.
    VLOG(1) << "Atrium: user-initiated close beats the last-tab-out keep-open";
  }

  // Window close = cold: reaching here means this close is
  // genuinely committing - HandleBeforeClose() and the move-out-empty check
  // above have both fallen through, so OnWindowClosing() below is about to
  // tear this Browser's tabs down for real, not defer/cancel the close or
  // keep a move-out-emptied window open. Tell Swift BEFORE that teardown
  // starts, so the AtriumOnTabClosed storm it's about to fire is treated as
  // "go cold" (persisted SpaceTab entries survive) rather than "delete".
  // Armed only when there is a strip to drain: that drain ends in
  // TabStripEmpty, which posts the second Close that AtriumClientView sees.
  expect_commit_close_ = !browser_->tab_strip_model()->empty();
  browser_->OnWindowClosing();
  if (!browser_->tab_strip_model()->empty()) {
    // The strip survived, so OnWindowClosing bailed at its own
    // HandleBeforeClose or is waiting on unload handlers - no second Close is
    // coming, and the one-shot must not outlive this pass.
    expect_commit_close_ = false;
    // Hide() (not widget_->Hide() directly) so this plain-hide path also
    // gets excluded from the Window menu / Cmd-~ cycling like every other
    // hide path.
    Hide();
  }
}

void AtriumBrowserWindow::DeleteBrowserWindow() {
  delete this;
}

// surface-message handlers that lean on this file's own helpers.
// The table that binds them lives in atrium_browser_window_surface_routes.cc.
void AtriumBrowserWindow::OnSurface_SidebarTabSnoozed(
    const base::ListValue& args) {
  if (args.size() >= 3 && args[0].is_int() &&
      (args[1].is_int() || args[1].is_double()) && args[2].is_bool() &&
      browser_) {
    const double now = NotesInstantNow();
    const double wake_at = args[1].GetDouble();
    if (wake_at <= now) {
      return;
    }
    ApplySidebarGesture(atrium_core_snooze_tab(
        WindowSpaceIndex(),
        /*section=*/0, args[0].GetInt(), wake_at, args[2].GetBool(), now));
  }
}

void AtriumBrowserWindow::OnSurface_SidebarBackHeld(
    const base::ListValue& args) {
  content::WebContents* displayed = CurrentlyDisplayedWebContents();
  if (!displayed) {
    return;
  }
  if (char* raw = atrium_core_visits_for_tab(TabId(displayed))) {
    const std::string stops(raw);
    atrium_core_string_free(raw);
    const std::string timeline =
        TimelineWithThumbnails(stops, TabId(displayed));
    VLOG(1) << "Atrium: back held, timeline is " << timeline.size()
            << " bytes";
    if (sidebar_contents_) {
      const std::u16string script =
          u"window.atriumSetVisitTimeline && atriumSetVisitTimeline(JSON."
          u"parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(timeline)) + u"))";
      sidebar_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
          script, base::NullCallback());
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTimelineCommit(
    const base::ListValue& args) {
  if (args.size() >= 2 && args[0].is_string() && args[1].is_int()) {
    content::WebContents* displayed = CurrentlyDisplayedWebContents();
    if (!displayed) {
      return;
    }
    const std::string url = args[0].GetString();
    const int index = args[1].GetInt();
    if (url.empty() || TimelineUrlAt(TabId(displayed), index) != url) {
      VLOG(1) << "Atrium: timeline commit refused - stop " << index
              << " is not that url";
      return;
    }
    const GURL target(url);
    if (!target.is_valid()) {
      return;
    }
    content::NavigationController::LoadURLParams params(target);
    // The user picked it in the browser's own chrome, not on the page.
    params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
    displayed->GetController().LoadURLWithParams(params);
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabArchived(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    const std::vector<std::string> live = LooseRowTabIds();
    const int index = args[0].GetInt();
    const bool archived =
        atrium_core_archive_tab(WindowSpaceIndex(), index, NotesInstantNow());
    ApplySidebarGesture(archived);
    if (archived && index >= 0 && static_cast<size_t>(index) < live.size()) {
      CloseSidebarTab(live[index]);
    }
    if (overview_controller_) {
      overview_controller_->PushArchivedList();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabsArchivedBelow(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_int() && browser_) {
    const std::vector<std::string> live = LooseRowTabIds();
    const int index = args[0].GetInt();
    const bool archived = atrium_core_archive_tabs_below(
        WindowSpaceIndex(), index, NotesInstantNow());
    ApplySidebarGesture(archived);
    if (archived && index >= -1) {
      for (size_t i = static_cast<size_t>(index + 1); i < live.size(); ++i) {
        CloseSidebarTab(live[i]);
      }
    }
    if (overview_controller_) {
      overview_controller_->PushArchivedList();
    }
  }
}

void AtriumBrowserWindow::OnSurface_SidebarTabsCleared(
    const base::ListValue& args) {
  if (browser_) {
    ArchiveSpaceTabs();
  }
}

bool AtriumBrowserWindow::ArchiveSpaceTabs() {
  const std::vector<std::string> live = LooseRowTabIds();
  const bool archived =
      atrium_core_archive_space_tabs(WindowSpaceIndex(), NotesInstantNow());
  ApplySidebarGesture(archived);
  if (archived) {
    for (const std::string& tab_id : live) {
      CloseSidebarTab(tab_id);
    }
  }
  if (overview_controller_) {
    overview_controller_->PushArchivedList();
  }
  return archived;
}

std::vector<std::string> AtriumBrowserWindow::LooseRowTabIds() {
  std::vector<std::string> ids;
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(SidebarTargetBrowser()), base::JSON_PARSE_RFC);
  const base::DictValue* dict = model ? model->GetIfDict() : nullptr;
  const base::ListValue* rows = dict ? dict->FindList("tabs") : nullptr;
  if (!rows) {
    return ids;
  }
  for (const base::Value& row : *rows) {
    const std::string* id = row.is_dict() ? row.GetDict().FindString("id") : nullptr;
    ids.push_back(id ? *id : std::string());
  }
  return ids;
}

void AtriumBrowserWindow::OnSurface_NotesBodyChanged(
    const base::ListValue& args) {
  if (args.size() >= 1 && args[0].is_string()) {
    if (current_note_id_.empty()) {
      atrium_core_set_space_note(
          WindowSpaceIndex(), args[0].GetString().c_str(), NotesInstantNow());
    } else {
      atrium_core_note_set_body(current_note_id_.c_str(),
                                args[0].GetString().c_str(), NotesInstantNow());
    }
  }
}

}  // namespace atrium
