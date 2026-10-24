// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_WINDOW_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_WINDOW_H_

#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"
#include "chrome/browser/ui/atrium/atrium_ambilight_cadence.h"
#include "chrome/browser/ui/atrium/atrium_devbar.h"
#include "chrome/browser/ui/atrium/atrium_devbar_scroll.h"
#include "chrome/browser/ui/atrium/atrium_edge_tint.h"
#include "chrome/browser/ui/atrium/atrium_own_window_drop.h"
#include "chrome/browser/ui/atrium/atrium_split_tree.h"
#include "chrome/browser/ui/atrium/atrium_split_tree_insertion.h"
#include "chrome/browser/ui/atrium/atrium_split_tree_layout.h"
#include "chrome/browser/ui/atrium/atrium_startup_hold.h"
#include "chrome/browser/ui/atrium/atrium_overlay_shadow_layer.h"
#include "chrome/browser/ui/atrium/atrium_rounded_hit_test_view.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"
#include "chrome/browser/ui/atrium/atrium_surface_message_router.h"

// This header is included from both Objective-C++ (.mm) and plain C++ (.cc)
// translation units. NSView is only usable as a pointer type in the latter.
#if defined(__OBJC__)
#import <Cocoa/Cocoa.h>
#else
class NSView;
class NSWindow;
#endif

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_open_reason.h"
#include "chrome/browser/ui/atrium/atrium_link_copy_format.h"
#include "chrome/browser/ui/atrium/atrium_load_phase_script.h"
#include "chrome/browser/ui/atrium/atrium_overlay_theme_script.h"
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_decision.h"
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"
#include "chrome/browser/ui/atrium/atrium_window_platform_mac.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/range/range.h"
#include "ui/views/bubble/bubble_anchor.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

namespace blink {
class WebInputEvent;
}  // namespace blink

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class Button;
class ExternalFocusTracker;
class Label;
class MenuModelAdapter;
class MenuRunner;
class NativeViewHost;
class NativeWidget;
class ViewShadow;
class WebView;
class WidgetDelegate;
}  // namespace views

namespace extensions {
class Extension;
class ExtensionContextMenuModel;
}  // namespace extensions

class Browser;
class BrowserWindowInterface;
class Profile;
class SkBitmap;

namespace atrium {

class BrowserController;
class CommandBarWidget;
class FindBarWidget;
class ThemeStudioWidget;
class PermissionSheetWidget;
class OverviewController;
class CourtyardWidget;
class AtriumSidebarOverlayWidget;
class AtriumSidebarEdgeReveal;
class AtriumSidebarFrostLayer;
class AtriumExtensionPanelView;
class AtriumSidePanelUI;
class PeekWidget;
class PeekControlsWidget;
class AtriumCaptureOverlayWidget;
class AtriumQuickMenuWidget;
class AtriumSpaceIconPickerWidget;
class HintOverlayWidget;
class ToastOverlayWidget;
class AtriumAutofillBubbleHandler;

// "Vertical split" in the
// product sense is a divider that reads LEFT-RIGHT (panes side by side);
// "horizontal split" reads TOP-BOTTOM (panes stacked). Named sideBySide/
// stacked instead, matching the retired ABI header's ABI strings exactly, to
// dodge upstream's SplitTabLayout::kVertical trap (upstream's kVertical means side-by-side, the opposite of what the name
// suggests).
enum class SplitOrientation {
  kSideBySide,
  kStacked,
};

// Parses the ABI's orientation string (see the retired ABI header's SPLIT VIEW
// doc block) - unrecognized or null defaults to kSideBySide (logged).
SplitOrientation ParseSplitOrientation(const char* orientation);

// Inverse of ParseSplitOrientation, for building the split-state payload
// AtriumBrowserWindow::PushSplitState pushes to the HTML surface.
const char* SplitOrientationToString(SplitOrientation orientation);

// One pane's content card: the empty-window placeholder, the live tab's
// WebView, and the frozen-snapshot overlay, occupying identical bounds
// (Layout() below - FillLayout-equivalent outside the docked-devtools
// case) and toggling visibility to swap which one is shown
// (AtriumBrowserWindow::ShowTab/ArmMoveOutFreeze) - exactly what
// AtriumRootView's old content_container_ held directly before the
// pane extraction.
class AtriumPaneView : public views::View {
  METADATA_HEADER(AtriumPaneView, views::View)

 public:
  explicit AtriumPaneView(Profile* profile);
  ~AtriumPaneView() override;

  views::WebView* web_view() { return web_view_; }

  // The empty-window placeholder: a subtly lighter rounded panel filling the
  // card slot while no tab is shown (Arc-style: a fresh window's page area
  // reads distinct from the sidebar instead of dissolving into one tint).
  views::View* empty_card() { return empty_card_; }

  // The frozen-snapshot overlay shown instead of `web_view_` when this
  // pane's selected tab is hosted elsewhere (M2c Arc-parity revision 2) -
  // see AtriumBrowserWindow::ArmMoveOutFreeze.
  class SnapshotOverlayView;
  SnapshotOverlayView* snapshot_view() { return snapshot_view_; }

  // A translucent black scrim painted on top of whichever of
  // empty_card_/web_view_/snapshot_view_ is currently visible, shown while a
  // peek card is open over this pane (see PeekWidget::Show/FinishHide). Its
  // own mouse press (SetPressCallback below) dismisses the peek - the card
  // and the controls cluster are each a separate NSWindow floating above
  // this pane, so a press the scrim itself receives is outside both of them.
  class PeekScrimView;
  PeekScrimView* scrim_view() { return scrim_view_; }

  // the live edge highlight while a tab dragged from another
  // window's sidebar hovers over this pane. Non-interactive, like the scrim
  // above - a drag in flight must not be swallowed by the thing previewing
  // where it would land.
  views::View* drop_preview_view() { return drop_preview_view_; }

  // Paints the band `edge` names, or hides the highlight for kCenter's swap -
  // there is no edge to draw there, and filling the whole pane would read as a
  // split rather than as the replace it is.
  void ShowDropPreview(SplitEdge edge);
  void LayoutDropPreview(const gfx::Rect& bounds);
  void ClearDropPreview();

  // Docked DevTools, split-view Phase C: the docked-devtools
  // WebView, added below scrim_view_ in child order but ABOVE
  // empty_card_/web_view_/snapshot_view_ so it paints over live content by
  // default (see UpdateDevToolsZOrder for the hide_inspected_contents flip).
  // Hidden (and unattached) whenever this pane has no docked DevTools -
  // AtriumBrowserWindow::UpdateDockedDevtools is the sole writer.
  views::WebView* devtools_web_view() { return devtools_web_view_; }

  // The strategy last applied via SetDevToolsContentsResizingStrategy -
  // Layout()'s sole input for the docked case (ApplyDevToolsContentsResizing
  // Strategy, chrome/browser/devtools/devtools_contents_resizing_strategy.h).
  const DevToolsContentsResizingStrategy& devtools_contents_resizing_strategy()
      const {
    return devtools_contents_resizing_strategy_;
  }
  void SetDevToolsContentsResizingStrategy(
      const DevToolsContentsResizingStrategy& strategy);

  // Mirrors DevtoolsUIController::DevtoolsWebViewController's own focus-
  // tracker install/reset (devtools_ui_controller.cc) - installed the first
  // time devtools_web_view_ gets a live WebContents, reset the moment it
  // goes back to none. AtriumBrowserWindow::UpdateDockedDevtools is the sole
  // caller of all three.
  bool HasDevToolsFocusTracker() const { return !!devtools_focus_tracker_; }
  void InstallDevToolsFocusTracker();
  void ClearDevToolsFocusTracker();
  void RestoreDevToolsFocus();

  // Reorders devtools_web_view_ against whichever of empty_card_/web_view_/
  // snapshot_view_ is currently visible so it ends up on top exactly when
  // `hide_inspected_contents` is true - mirrors DevtoolsWebViewController::
  // UpdateDevtools's devtools_is_on_top reorder (devtools_ui_controller.cc),
  // generalized from ContentsWebView's single content view to this pane's
  // three mutually-exclusive ones.
  void UpdateDevToolsZOrder(bool hide_inspected_contents);

  // immersive (video) fullscreen - the card fills the screen edge
  // to edge, so its rounded corners would cut into the video against black.
  // Squares them for the duration; Layout() re-pushes the normal radii on the
  // way out.
  void SetImmersive(bool immersive);

  // the three tiles below take the card radius once, at
  // construction, so a borderWidth change has to push it again. Layout()
  // handles the contents/devtools pair on its own.
  void RefreshCardCornerRadii();

  // a pill top-left naming the profile this pane's session
  // runs under - lives on the pane rather than in the WebContents' own page,
  // since a mirrored page never differs in markup from its non-mirrored self.
  void SetMirrorBadge(const std::u16string& text, SkColor color);
  void ClearMirrorBadge();

  // Empty when no badge is showing.
  std::u16string MirrorBadgeTextForTesting() const;

  // SetImmersive is latched per pane - this is the only way to
  // see whether a pane past the first two actually took the squared radius.
  bool IsImmersiveForTesting() const { return immersive_; }

  float TileCornerRadiusForTesting() const;

  // views::View:
  void Layout(PassKey) override;

 private:
  // Whichever of empty_card_/web_view_/snapshot_view_ is currently visible -
  // the pane's own invariant is that exactly one of the three is, at any
  // given time (ShowTab/ArmMoveOutFreeze toggle visibility, never leaving
  // more than one shown).
  views::View* ActiveContentView();

  // Re-points the card's rounded corners at the tiles that actually draw them.
  // A native web view ignores an ancestor's mask, so each tile carries its own
  // radii: whichever corners of the tile sit on the card's own corners get
  // rounded, and the contents/devtools seam stays square (atrium_devtools_card
  // _corners.h). Called from Layout() for both the docked and undocked cases,
  // so undocking restores all four corners. `devtools` is empty when none is
  // docked.
  void ApplyCardCornerRadii(const gfx::Rect& card,
                            const gfx::Rect& contents,
                            const gfx::Rect& devtools);

  // 0 while immersive - see SetImmersive.
  float CardCornerRadius() const;

  // true while this pane is in immersive fullscreen.
  bool immersive_ = false;

  // Last radii ApplyCardCornerRadii actually pushed, so a plain resize (which
  // relayouts per frame) doesn't rebuild the tiles' mask layers. The initial
  // all-zero value is deliberately not a reachable steady state - the first
  // Layout() always pushes.
  gfx::RoundedCornersF applied_contents_corners_;
  gfx::RoundedCornersF applied_devtools_corners_;

  raw_ptr<views::View> empty_card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<SnapshotOverlayView> snapshot_view_ = nullptr;
  raw_ptr<views::WebView> devtools_web_view_ = nullptr;
  raw_ptr<PeekScrimView> scrim_view_ = nullptr;
  raw_ptr<views::View> drop_preview_view_ = nullptr;
  SplitEdge drop_preview_edge_ = SplitEdge::kCenter;
  // Built lazily - most panes never show one.
  raw_ptr<views::Label> mirror_badge_ = nullptr;
  DevToolsContentsResizingStrategy devtools_contents_resizing_strategy_;
  // Incomplete type here (views/focus/external_focus_tracker.h is only
  // included in the .mm) - safe with unique_ptr because ~AtriumPaneView() is
  // defined out-of-line in the .mm, not defaulted inline here.
  std::unique_ptr<views::ExternalFocusTracker> devtools_focus_tracker_;
};

// Replaces AtriumRootView's old content_container_: the card drop-shadow
// anchor plus one or two AtriumPaneViews (a second pane plus an AtriumSplitResizeArea divider appear
// while split - see EnterSplit/ExitSplit below). Single-pane layout is
// FillLayout-equivalent via Layout()'s early-return branch).
class AtriumPaneContainerView : public views::View {
  METADATA_HEADER(AtriumPaneContainerView, views::View)

 public:
  explicit AtriumPaneContainerView(Profile* profile);
  ~AtriumPaneContainerView() override;

  // The pane currently authoritative for dialogs, hit-testing, and
  // damage-heal: the single pane outside a split, or active_pane_index()'s
  // pane while split - see SetActivePaneIndex's doc.
  AtriumPaneView* active_pane() {
    return PaneAt(static_cast<size_t>(active_pane_index_));
  }
  AtriumPaneView* primary_pane() { return pane_tree_.LeafAt(0); }
  // Null unless is_split().
  AtriumPaneView* secondary_pane() {
    return is_split() ? pane_tree_.LeafAt(1) : nullptr;
  }

  // The drop-shadow layer under `pane`'s card, so a transform on the pane
  // can carry its shadow along. Null for a pane past the first two.
  ui::Layer* ShadowLayerFor(AtriumPaneView* pane);

  // Every leaf, including the third and beyond that InsertPaneAtLeaf can
  // grow - primary_pane()/secondary_pane() only reach the first two.
  size_t pane_count() const { return pane_tree_.leaf_count(); }
  AtriumPaneView* PaneAt(size_t index) { return pane_tree_.LeafAt(index); }

  bool is_split() const { return pane_tree_.leaf_count() > 1; }
  // The root branch's own axis/ratio - the only gutter AtriumBrowserWindow's
  // ABI still names a single SplitOrientation for. kSideBySide outside a
  // split (no branch to read).
  SplitOrientation orientation() const;
  double ratio() const;
  int active_pane_index() const { return active_pane_index_; }

  // Builds the secondary pane + resize divider and lays out per
  // `orientation`/`ratio` (clamped - see ClampSplitRatio's doc). No-op
  // (returns false) if already split. Only builds views - the caller
  // (AtriumBrowserWindow::EnterSplit) is responsible for attaching the
  // right WebContents into primary_pane()/secondary_pane() right after this
  // returns, and for calling SetActivePaneIndex to match.
  bool EnterSplit(SplitOrientation orientation, double ratio);

  // Grows pane_tree_ via InsertAtLeaf, past two leaves if needed. Null for
  // kCenter (caller swaps in place instead) or a rejected insert.
  AtriumPaneView* InsertPaneAtLeaf(AtriumPaneView* target,
                                   atrium::SplitEdge edge);

  // Destroys the secondary pane + resize divider and forces
  // active_pane_index() back to 0. The caller (AtriumBrowserWindow::
  // CollapseSplit/CloseSplit) is responsible for having already attached
  // the surviving tab's WebContents into primary_pane() BEFORE calling this
  // if the survivor was previously the secondary pane. No-op if not split.
  void ExitSplit();

  // Applies a new orientation/ratio to an already-split layout (no
  // create/destroy). No-op if not split.
  void UpdateSplitLayout(SplitOrientation orientation, double ratio);

  // Any leaf of pane_tree_. An index past the last leaf, or a request while
  // not split, clamps to 0 - active_pane() must always resolve a live pane.
  void SetActivePaneIndex(int index);

  // Drops one leaf and its view, so its space goes back to its siblings.
  // Refuses below three leaves - ExitSplit owns the two-pane collapse, whose
  // survivor re-home the caller must do first. Detach `pane`'s tab first.
  bool RemovePane(AtriumPaneView* pane);

  // Collapsed: the card regains a LEFT inset (the gutter is normally the
  // sidebar's own trailing padding, which disappears with the pane).
  void SetCollapsedContentInsets(bool collapsed);

  // immersive (video) fullscreen - the card drops its insets,
  // corner radii, and drop shadow so the page fills the screen edge to edge.
  // Latched, because the collapsed/docked inset math runs on unrelated
  // triggers (sidebar collapse, dock side) and would otherwise reinstate the
  // gutters mid-fullscreen.
  void SetImmersive(bool immersive);

  // The insets alone are not enough: the card radius derives from
  // the gutter, so a wider border keeping the old radius pinches the corner.
  void OnContentCardInsetChanged();

  // Sidebar on right: which edge the docked sidebar's gutter (and
  // the traffic-light band reservation) hugs - see SetCollapsedContentInsets'
  // .mm for the resulting inset math. Propagated from AtriumRootView::
  // SetSidebarOnRight.
  void SetSidebarOnRight(bool on_right);

  // the card gives up its inset on the panel's side. The panel
  // clips its resize grip at its container, so the mark can only reach the
  // seam if that container starts at the card's edge.
  void SetAiPanelOpen(bool open);

  // The extension side panel holds the same gutter as the AI panel.
  void SetExtensionPanelOpen(bool open);

  // the Developer Mode URL toolbar's slot, above every pane
  // (including a split's) - built once, hidden until PushDevBarState()
  // shows it. Null never happens; a WebView lands inside it once the
  // reserved chrome context exists, same as sidebar_web_container().
  // a split adds one more per pane, AddDevBarContainer, and
  // Layout puts slot i over PaneAt(i).
  views::View* dev_bar_container() { return dev_bar_containers_.front(); }
  views::View* dev_bar_container_at(size_t index) {
    return dev_bar_containers_[index];
  }
  size_t dev_bar_container_count() const { return dev_bar_containers_.size(); }
  views::View* AddDevBarContainer();
  // The active card's stroke wears the same accent as the sidebar seam.
  void SetActivePaneRingAccent(SkColor accent);
  SkColor ActivePaneRingAccent() const;
  views::View* active_pane_ring_for_testing() { return active_pane_ring_; }
  // Defined out of line - AtriumSplitResizeArea is incomplete here.
  views::View* gutter_view_for_testing(size_t index);

  // Shows the first `count` devbar slots, hides the rest, and relayouts -
  // idempotent.
  void SetDevBarsShown(size_t count);

  // Native-app chrome mode: fills this view with an opaque `color`,
  // or clears the fill when `enabled` is false. This view is the deliberate
  // target - its bounds cover the card's gutters and the area behind the card
  // but NOT the sidebar column, so the sidebar's glass survives the mode.
  // `color` must already be opaque; AtriumBrowserWindow::SetOpaqueSurround
  // forces that.
  void SetOpaqueSurround(bool enabled, SkColor color);

  // Paints the page glow over the surround fill, in this view's coordinates.
  // Without it the fill hides the band in the gutters around the card.
  using GlowPainter = base::RepeatingCallback<void(gfx::Canvas*, views::View*)>;
  void SetSurroundGlowPainter(GlowPainter painter);

  // The client (non-HTCAPTION) regions within `available_bounds`: [primary
  // card] outside a split, [primary card, secondary card, divider strip]
  // while split - see AtriumClientView::NonClientHitTest, the sole caller
  // (through AtriumRootView's forwarding accessor).
  std::vector<gfx::Rect> GetClientRegions(
      const gfx::Rect& available_bounds) const;

  // `gutter_index` is the gutter's pre-order-over-branches position in
  // gutters_ - same clamp/snap recipe as before, now per-gutter.
  double OnGutterResize(size_t gutter_index,
                        int resize_amount,
                        bool done_resizing);

  // A2 review fix (finding 2): fires on drag-END only, for the root gutter
  // only - the ABI echoes one "divider" ratio, not one per gutter.
  void SetDividerCommittedCallback(base::RepeatingClosure callback);

  // views::View:
  void Layout(PassKey) override;

 private:
  class AtriumSplitResizeArea;
  using Direction = AtriumSplitTree<raw_ptr<AtriumPaneView>>::Direction;

  void LayoutDevBars(const gfx::Rect& content);

  // ExtentForDirection along the root branch's own axis (kRow when not
  // split, matching EnterSplit's pre-tree default).
  int SplitExtent() const;

  // The extent (width for kRow, height for kCol) available to split along
  // `dir`, in this view's local coordinates (post-border, post-collapsed-
  // inset) - generalizes SplitExtent to any branch's own axis.
  int ExtentForDirection(Direction dir) const;

  // atrium_split_tree_layout.h's SplitLayoutParams built from this
  // container's own constants (kSplitDividerWidth, kMinPaneExtent, etc).
  SplitLayoutParams LayoutParams() const;

  // Upstream's CalculateRatioWithSnapPoints (single snap point at 0.5,
  // kSnapDistance=15 - see the .mm for the exact constants).
  double SnapRatio(double ratio, int extent) const;

  // Re-points the selection at `pane` wherever it landed, falling back to
  // leaf 0. An insert or a removal renumbers the leaves under the selection,
  // so the index alone would move the ring to another pane.
  void SelectPaneByIdentity(AtriumPaneView* pane);

  // Keeps gutters_ in sync with pane_tree_'s branch count (leaf_count()-1,
  // since every branch is 2-kid - see atrium_split_tree_insertion.h).
  void RebuildGutterViews();

  // The leaves currently under the branch at `path`, in pre-order - see
  // GutterState::leaves' doc.
  std::vector<raw_ptr<AtriumPaneView>> LeavesUnderBranch(
      const SplitNodePath& path) const;

  // Paint-less anchor the primary card's drop shadow (card_shadow_) is
  // attached to - see the constructor's comment for why this can't just be
  // the pane's empty_card_/web_view_/snapshot_view_ directly.
  raw_ptr<views::View> card_shadow_host_ = nullptr;
  std::unique_ptr<views::ViewShadow> card_shadow_;
  // Only built while is_split() - the secondary pane's own drop-shadow
  // anchor (card_shadow_host_ above only ever anchored a single pane; a
  // split needs an independent shadow per card).
  raw_ptr<views::View> secondary_card_shadow_host_ = nullptr;
  std::unique_ptr<views::ViewShadow> secondary_card_shadow_;

  // See dev_bar_container()'s doc. Never empty.
  std::vector<raw_ptr<views::View>> dev_bar_containers_;

  GlowPainter surround_glow_painter_;

  // a single leaf outside a split, an N-leaf branch tree
  // while split - replaces the old primary_pane_/secondary_pane_ pair.
  AtriumSplitTree<raw_ptr<AtriumPaneView>> pane_tree_;

  // one entry per internal branch node of pane_tree_, in
  // the tree's own pre-order-over-branches - RebuildGutterViews() keeps
  // this in sync with the tree's actual branch count.
  struct GutterState {
    // Identifies the branch itself - RebuildGutterViews only grows/shrinks
    // gutters_ from the back, so an earlier insert can shift a later
    // branch's vector index. Root (gutters_[0]) is exempt, see ratio_for.
    std::vector<raw_ptr<AtriumPaneView>> leaves;
    Direction axis = Direction::kRow;
    double ratio = 0.5;
    // The branch's own extent at the start of the CURRENT resize drag -
    // upstream's initial_start_width_on_resize_. Nullopt outside a drag.
    std::optional<double> resize_initial_extent;
    // Refreshed every Layout() pass from LayoutSplitTree's result, so
    // OnGutterResize can convert a drag delta back into a ratio without
    // re-running the whole tree layout.
    int total_extent = 0;
    raw_ptr<AtriumSplitResizeArea> view = nullptr;
  };
  std::vector<GutterState> gutters_;

  // Captured by ExitSplit before RebuildGutterViews wipes gutters_, so a
  // same-session re-entry can restore it instead of defaulting to 0.5.
  std::optional<double> last_split_ratio_;

  int active_pane_index_ = 0;
  // The stroke around the active card while split. Layout and
  // SetActivePaneIndex both move it, so a ratio drag and a click agree.
  void UpdateActivePaneRing();
  raw_ptr<views::View> active_pane_ring_ = nullptr;
  // See SetSidebarOnRight's doc.
  bool sidebar_on_right_ = false;
  // Latched inputs to ApplyContentInsets - see SetImmersive's doc for why
  // the collapsed state has to be remembered rather than recomputed.
  bool collapsed_ = false;
  bool immersive_ = false;
  bool ai_panel_open_ = false;
  bool extension_panel_open_ = false;
  // The single place the card's border insets are decided, so the four
  // independent triggers (collapse, dock side, immersive, AI panel) cannot
  // each push a different answer.
  void ApplyContentInsets();
  // See SetDividerCommittedCallback's doc.
  base::RepeatingClosure divider_committed_callback_;
  // Cached so EnterSplit (called long after construction) can build a new
  // AtriumPaneView without threading Profile* through every op call site.
  raw_ptr<Profile> profile_ = nullptr;
};

// Root view for AtriumBrowserWindow: a horizontal split with a fixed-width
// full-height left pane (a WebView container the HTML sidebar attaches into)
// and a flexible right pane hosting the active pane's WebView as a
// rounded-corner card, inset so the tinted root background shows as a thin
// frame around it (Arc-style chromeless window, M2c).
class AtriumRootView : public views::View, public views::ViewTargeterDelegate {
 public:
  // Little Atrium: `little` builds this root view WITHOUT
  // a left sidebar pane - a little (chromeless, host-less) window has no
  // sidebar/spaces UI at all (BrowserController::CreateLittleWindow's ctor
  // arm never calls AttachSidebar) - and WITH a minimal top chrome-strip
  // strip instead (little_chrome_container() below), stacked ABOVE
  // pane_container_ in a vertical layout rather than beside it in the
  // regular horizontal one. There is no sidebar slot in this mode; every
  // sidebar-only accessor (sidebar_slot(), IsSidebarCollapsed(), etc.) answers
  // "no sidebar" rather than being called and dereferenced.

  // `on_live_resize` is the window's SetSidebarWidthLive. A grip drag
  // step that wrote the slot here instead would be overwritten by the next
  // resize-animation tick, because only that entry point stops the timer.
  AtriumRootView(Profile* profile,
                 base::RepeatingCallback<void(double)> on_live_resize,
                 base::RepeatingCallback<void(double)> on_commit_resize,
                 bool little = false);
  ~AtriumRootView() override;

  views::WebView* web_view() {
    return pane_container_->active_pane()->web_view();
  }
  // Null unless constructed with `little=true`. The little window's minimal
  // top strip, holding the /foyer route - see
  // AtriumBrowserWindow::BuildHtmlFoyer.
  views::View* little_chrome_container() { return little_chrome_container_; }

  // the sidebar's container, null only on a little window (which
  // has no sidebar at all). The WebView lands inside this later - the chrome
  // context is not ready when this view is built.
  views::View* sidebar_web_container() { return sidebar_web_container_; }

  // the width grip, for the window's hit test to carve out of its
  // caption region. Null on a little window. Exposed because the grip sits ON
  // the seam - at x >= the sidebar's width by construction - and the backdrop
  // test would otherwise claim its press as a window drag.
  views::View* sidebar_grip() { return sidebar_grip_; }

  // the ai-panel's container, docked opposite the sidebar.
  // Width is native-controlled (SetAiPanelOpen/SetAiPanelWidth) so it really
  // squeezes pane_container_'s BoxLayout share.
  views::View* ai_panel_container() { return ai_panel_container_; }

  // Shares the AI panel's edge. Only one of the two is ever open.
  AtriumExtensionPanelView* extension_panel() { return extension_panel_; }
  const AtriumExtensionPanelView* extension_panel() const {
    return extension_panel_;
  }
  // Showing the card turns its shadow layer back on, so a window that is in
  // fullscreen must re-hide it after every show.
  void ApplyExtensionCardShape();
  bool ExtensionCardShadowVisibleForTesting() const;

  // forwards to pane_container_'s own slot - see its doc.
  views::View* dev_bar_container() {
    return pane_container_->dev_bar_container();
  }
  // The container's own visibility, which is the state the hit test needs:
  // The nav group moves into the bar for exactly as long as it is
  // up.
  bool dev_bar_shown() {
    views::View* bar = dev_bar_container();
    return bar && bar->GetVisible();
  }

  // The sidebar's WebView container - width, collapse and dock all act on
  // this slot. Null on a little window, which has no sidebar at all.
  views::View* sidebar_slot();
  using SnapshotOverlayView = AtriumPaneView::SnapshotOverlayView;
  SnapshotOverlayView* snapshot_view() {
    return pane_container_->active_pane()->snapshot_view();
  }
  views::View* empty_card() {
    return pane_container_->active_pane()->empty_card();
  }
  // see AtriumPaneView::scrim_view()'s doc.
  using PeekScrimView = AtriumPaneView::PeekScrimView;
  PeekScrimView* peek_scrim() {
    return pane_container_->active_pane()->scrim_view();
  }

  // Collapsed: the card regains a LEFT inset (the gutter is normally the
  // sidebar's own trailing padding, which disappears with the pane).
  void SetCollapsedContentInsets(bool collapsed);

  // immersive (video) fullscreen - hides the sidebar and hands the
  // whole window to the card. Hides sidebar_slot(), so it covers the HTML arm
  // as well as the retired SwiftUI one. A little window has no slot,
  // so only the card and chrome-strip parts apply there.
  void SetImmersive(bool immersive);

  // No-op on a little window, which has no pane container.
  void OnContentCardInsetChanged();

  // The open Courtyard hides the same slot for its own reason, so the two
  // latch separately and one place reads both - leaving fullscreen under an
  // open Courtyard used to put the sidebar back behind the glass.
  void SetCourtyardOpen(bool open);

  // Sidebar on right: whether the sidebar is currently docked to
  // the right edge - false is left, the stock default.
  bool sidebar_on_right() const { return sidebar_on_right_; }
  int CurrentExtensionPanelWidth() const;
  bool extension_panel_on_right() const { return !sidebar_on_right_; }

  // The seam goes with the pane. A collapsed pane has no edge to drag, and the
  // reveal panel carries its own handle in the page.
  void SetSidebarCollapsed(bool collapsed);

  // The colour the seam's pill paints with, the overlay accent's own.
  void SetSeamAccent(SkColor accent);

  // Re-docks the sidebar to the given edge: reorders sidebar_slot() within
  // the horizontal BoxLayout (first child for left, last for right) and
  // propagates to pane_container_ (its gutter/traffic-light-band inset
  // mirrors the same way). A no-op if `on_right` already matches, and on a
  // little window, which has no slot.
  void SetSidebarOnRight(bool on_right);

  // The client (non-HTCAPTION) card/divider regions within `available_bounds`
  // (the caller's own bounds - this chromeless window's root view fills the
  // entire client area, so the caller's bounds and this view's local bounds
  // coincide) - see AtriumPaneContainerView::GetClientRegions's doc. The
  // seam AtriumClientView::NonClientHitTest goes through instead of deriving
  // rects inline (covers both panes plus the resize
  // divider while split).
  std::vector<gfx::Rect> GetClientRegions(
      const gfx::Rect& available_bounds) const;

  // Exposes the pane container so AtriumBrowserWindow can route its own
  // "active pane" accessor (ModalDialogHost, damage-heal) through one seam -
  // see AtriumPaneContainerView::active_pane()'s doc.
  AtriumPaneContainerView* pane_container() { return pane_container_; }

  // views::View:
  // reaches the width grip back over the sidebar's own trailing
  // pixels, so one handle owns the seam and the grab band has no hole in it.
  void Layout(PassKey) override;

  // views::ViewTargeterDelegate:
  views::View* TargetForRect(views::View* root, const gfx::Rect& rect) override;

 private:
  // Null unless constructed with `little=true` - see
  // little_chrome_container()'s doc.
  raw_ptr<views::View> little_chrome_container_ = nullptr;
  raw_ptr<views::View> sidebar_web_container_ = nullptr;
  raw_ptr<views::View> ai_panel_container_ = nullptr;
  raw_ptr<AtriumExtensionPanelView> extension_panel_ = nullptr;
  std::unique_ptr<views::ViewShadow> extension_card_shadow_;
  raw_ptr<AtriumPaneContainerView> pane_container_ = nullptr;
  // the width grip between the sidebar and the pane. Null on a
  // little window, which has no sidebar to resize.
  raw_ptr<views::View> sidebar_grip_ = nullptr;
  // See sidebar_on_right()/SetSidebarOnRight.
  bool sidebar_on_right_ = false;

  // See the constructor's `on_live_resize`.
  base::RepeatingCallback<void(double)> on_live_resize_;
  base::RepeatingCallback<void(double)> on_commit_resize_;

  // See SetSidebarCollapsed and SetImmersive - either hides the seam.
  bool sidebar_collapsed_ = false;
  bool immersive_ = false;
  // See SetCourtyardOpen - either of these two hides the slot.
  bool courtyard_open_ = false;

  void UpdateSidebarGripVisibility();
  void UpdateSidebarSlotVisibility();

  // The grip's three callbacks. A drag step resizes without animating, a
  // release animates to the settled width and persists it, and the third
  // reports the width a press starts from. Methods rather than lambdas so the
  // header records what the grip is allowed to do.
  void OnGripDragged(int width);
  void OnGripReleased(int width);
  int CurrentSidebarWidth() const;
};

// Atrium's BrowserWindow: hosts the SwiftUI sidebar and web contents in a
// views::Widget. See WebUIBrowserWindow for the stub-strategy precedent.
class AtriumBrowserWindow : public BrowserWindow,
                            public views::WidgetObserver,
                            public views::ViewObserver,
                            public atrium::SurfaceHost {
 public:
  // Little Atrium: `little` forwards to AtriumRootView's
  // own `little` param (see its doc) - read by browser_window_factory.cc via
  // atrium::CreatingLittleBrowser() (atrium_little_marker.h), the same
  // bracket-around-Browser::Create() flag BrowserController::
  // CreateLittleWindow's ctor arm sets, since Browser itself has nowhere
  // cheap to stamp a per-instance marker before this constructor runs.
  explicit AtriumBrowserWindow(Browser* browser, bool little = false);
  ~AtriumBrowserWindow() override;

  Browser* browser() { return browser_.get(); }
  views::Widget* widget() { return widget_.get(); }

  // Set by PermissionSheetWidget's own constructor/destructor - this window
  // does not own the widget, AtriumPermissionPrompt does.
  void SetLivePermissionSheet(PermissionSheetWidget* sheet) {
    live_permission_sheet_ = sheet;
  }

  // A tab switch destroys the outgoing sheet AFTER the incoming one registered
  // (kDestroyPromptButKeepRequestPending), so an unconditional null here would
  // hide the live sheet from every later push.
  void ClearLivePermissionSheet(PermissionSheetWidget* sheet) {
    if (live_permission_sheet_ == sheet) {
      live_permission_sheet_ = nullptr;
    }
  }

  // slides the hidden sidebar in or out as a floating panel over the
  // page. The edge reveal drives this instead of re-expanding the docked
  // column, which is what made the reveal read as a plain expand.
  void RevealFloatingSidebar(bool reveal);

  // The posted half of RevealFloatingSidebar. Applies whichever state was
  // asked for last, so a pointer crossing the strip quickly settles once
  // rather than replaying every crossing.
  void ApplyFloatingSidebarReveal();

  // The panel is a child window: nothing native samples the page through it,
  // and a page-drawn wash carries only the flat tint, never the picture.
  void PaintFloatingPanelBackdrop(gfx::Canvas* canvas,
                                  const gfx::Rect& panel_bounds_in_screen);
  void RepaintFloatingPanel();

  // The reveal panel, once a reveal has built it. Null before that.
  AtriumSidebarOverlayWidget* SidebarOverlayWidgetForTesting() {
    return sidebar_overlay_widget_.get();
  }

  // True while the DOCKED pane holds the sidebar's WebContents. False once the
  // panel has taken it, which is what a working reveal does.
  bool SidebarPaneHoldsContentsForTesting() const;

  // The width grip between the sidebar and the pane. Null on a little window.
  views::View* SidebarGripForTesting();
  views::View* ActivePaneRingForTesting();
  AtriumPaneView* ActivePaneForTesting();
  int ActivePaneIndexForTesting() const;
  gfx::Rect PaneBoundsForTesting(size_t index);

  // The colour root_view_ currently paints, or nullopt when it paints nothing.
  // The seam between the sidebar and the card shows this and nothing else, and
  // no pixel test in this tree can grade a 6px column.
  // Also nullopt mid-crossfade: the fade's Background sets no colour, and
  // views::Background answers SK_ColorRED when unset, so a caller reading
  // through a fade got opaque red and believed it.
  std::optional<SkColor> RootBackgroundColorForTesting() const;

  // Runs the surround crossfade straight to its end, so a test can assert the
  // colour it lands on without waiting out kBackdropTintFadeDuration.
  void FinishSurroundFadeForTesting();

  // A pushed edge tint crossfades in, so the glow is faint for its first ticks.
  bool EdgeTintAnimatingForTesting() const {
    return edge_tint_timer_.IsRunning();
  }

  // Freezes the sidebar page at `width` for the length of a pane animation, so
  // the slide clips it instead of re-flowing it. See the definition.
  void PushSidebarLayoutWidth(int width);

  // Hands the page back to its own viewport once the animation lands.
  void ClearSidebarLayoutWidth();

  // publishes the width the Overview crossing will restore, so the
  // page can cap the favourites grid to it instead of reflowing at the live
  // (animating) sidebar width. See the definition.
  void PushSidebarRestWidth(int width);
  void ClearSidebarRestWidth();

  // The pointer left the edge zone. Starts the panel's own hide rule if a
  // panel is up, and dismisses outright if the reveal never built one.
  void NoteFloatingSidebarEdgeExit();

  // The panel's horizontal span in window coordinates while one is up, so the
  // width grip can park on its edge. Nullopt clears it.
  void PublishFloatingSidebarSpan(std::optional<gfx::Range> span);

  // Hands the sidebar's WebContents to the panel. Called by the panel's own
  // Show, so every reveal path takes the contents whether it remembered to or
  // not.
  void HandSidebarToPanel();

  // Hands the sidebar's WebContents back to the docked pane. Called when the
  // panel has finished hiding, and when the pane docks for real.
  void DockFloatingSidebar();

  // fills sidebar_web_container() with a WebView on
  // chrome://atrium.top-chrome/sidebar. Deferred through
  // AtriumChromeContext::PostWhenReady - the reserved context lands ~670ms
  // after startup, well after this window exists (a lesson learned
  // the hard way, where deciding at construction pinned a surface to the
  // SwiftUI arm forever).
  void BuildHtmlSidebar();

  // rebuilds the tab list from TabStripModel and hands it to the
  // sidebar renderer. Safe to call at any time - a no-op unless the HTML arm
  // is up. Called on every tab-strip change, so it must stay cheap.
  // `moved` is what the mutation returned - false skips the save and
  // `on_saved`. `on_saved` runs once the write lands, for a push that must
  // not read the file early.
  void ApplySidebarGesture(bool moved,
                           base::OnceClosure on_saved = base::OnceClosure());

  // asks the sidebar page to GLIDE the rail to `index` rather
  // than appear there. window.atriumSwipeTo drives the same settle machinery a
  // released swipe uses, so the keyboard route animates the way the gesture
  // does instead of landing with no transition.
  //
  // Call it BEFORE the model rebuild. A rebuild draws the destination, and a
  // glide told to travel somewhere it is already showing has nowhere to go.
  void GlideSidebarToSpace(int index);
  // Opens a row whose tab is not live. Public because the cycle key reaches
  // a cold row too, and this is the funnel that holds the scheme
  // check and the realised-window check both paths need. `foreground` is
  // explicit rather than defaulted: a restore reopens many rows
  // at once and only the moment's selected one may take the selection.
  // Returns the tab it opened, or null when the open was refused. The
  // favourite claim needs the tab itself, not the active one read back
  // afterwards.
  content::WebContents* OpenSidebarColdTab(const std::string& url,
                                           bool foreground);

  // the Shelf's Open action. Activates the live tab showing
  // `url` if the strip still has one, or reopens it cold otherwise - the
  // Shelf entry is never touched either way.
  void OpenOrActivateUrl(const std::string& url);

  // ActivateFavoriteRow's marker match, not OpenOrActivateUrl's exact string
  // one: a favourite's tab has usually moved past its stored URL.
  void OpenOrActivateFavorite(const std::string& favorite_url);

  // The shelf confirmation toast, dispatched to the sidebar surface.
  void ShowShelfToast(const std::string& label);

  // the Skylight state the backdrop paints with.
  void PushSkylightToSidebar();
  static void SetSkylight(const std::string& weather, bool night_shift);
  static std::optional<std::pair<std::string, bool>> SkylightFromSettings(
      std::string_view settings_json);

  // fires the toast+glow path
  // (window.atriumTabSnoozeReturned) but nothing called - the wake timer's
  // caller, once it has reopened `tab_id`.
  void NotifySnoozedTabReturned(const std::string& tab_id,
                                const std::string& title,
                                const std::string& color);

  // same seam as NotifySnoozedTabReturned, for the archive
  // sweep - archive-fly.js's clone needs the row still in the sidebar DOM,
  // so callers must fire this before PushSidebarSnapshot rebuilds it away.
  void NotifyTabsArchived(const std::vector<std::string>& tab_ids);

  // Schedules one PushSidebarSnapshotNow for the end of this run-loop turn.
  void PushSidebarSnapshot();
  void PushSidebarSnapshotNow();
  int SidebarSnapshotBuildsForTesting() const {
    return sidebar_snapshot_builds_;
  }
  void FlushSidebarSnapshotForTesting();

  // the sidebarReady catch-up for the current extension-action
  // snapshot, which PushSidebarSnapshot doesn't carry.
  void PushSidebarExtensionsForNewSurface();

  // same catch-up, for the notification timeout step.
  void PushNotificationTimeoutToSidebar();

  // Same catch-up for the confirmation toast's own document - the
  // notification timeout setting reached only the sidebar until now, so a
  // confirmation always used toast-stack.js's CONFIRM_MS fallback.
  void PushNotificationTimeoutToToastOverlay();

  // same catch-up, for the swipe presets.
  void PushSwipeTuningToSidebar();

  // Same catch-up, for the window's own appearance - the glass
  // alpha the floating panel paints itself with, and the native-app chrome
  // mode, which stops at the sidebar column unless the page carries it across.
  void PushSidebarAppearance();

  // the one funnel every "this window just became its slot's
  // presenter" call site should use, rather than each calling
  // BuildHtmlSidebar() and PushSidebarSnapshot() by hand - see
  // atrium_sidebar_snapshot_gate.h's DecideSidebarPresenterCatchUp doc for
  // why. A no-op if this window is not (or is no longer, by the time this
  // runs) its slot's presenter.
  void CatchUpSidebarForNewPresenter();

  // Asks for an icon for every row in `model` that has none
  // yet. Driven from the push rather than from a surface message: the snapshot
  // is the only place that knows which rows exist.
  void RequestMissingRowFavicons(const std::string& model);

  // atrium::SurfaceHost - chrome.send from the sidebar route. The window is
  // the host because it owns both the WebContents and the Browser the
  // messages act on.
  // the live edge highlight, driven from the DRAGGING window's
  // controller while a sidebar drag is in flight over this one.
  void ShowContentDropPreview(SplitEdge edge);
  void ClearContentDropPreview();

  // ChooseDropTarget never sees the drag's own window. True when
  // the point is in it, and the pane showing `tab` is never a target.
  bool PreviewOwnWindowDrop(gfx::Point screen_point,
                            content::WebContents* tab = nullptr);
  // Rearranges the split, or leaves the drop to the sidebar's own handling.
  // A pane dropped on the sidebar leaves the split.
  bool DropTabOnOwnWindow(content::WebContents* tab, gfx::Point screen_point);

  // Two accessors rather than one, because the pair is what a test needs: a
  // preview that only ever appears would satisfy a visibility check while
  // pointing at the wrong edge the whole time. Empty when nothing is shown.
  bool IsContentDropPreviewVisibleForTesting() const;
  gfx::Rect ContentDropPreviewBoundsForTesting() const;

  // One row of the surface-message route table: a name and the member that
  // answers it.
  struct SurfaceRoute {
    const char* name;
    void (AtriumBrowserWindow::*handler)(const base::ListValue& args);
  };

  // the surface-message route table, one handler per name. Data,
  // so a test can check it against kSurfaceMessages without a window.
  static base::span<const SurfaceRoute> SurfaceRoutes();

  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

  // Whether `browser` has a native window content can be attached to.
  //
  // Public, and the only guard anything should use before adding a tab: a tab
  // added to a Browser with no realised window aborts the process,
  // and that trap is not this class's alone - the settings window
  // opens the licences page and has to clear the same bar. One check rather
  // than a copy per caller.
  static bool HasRealisedWindow(Browser* browser);

  // AtriumMediaHub's push: `records_json` reaches the sidebar of every window
  // SHOWING `profile`, the same resolution the media commands use.
  static void PushMediaSessionsToProfile(Profile* profile,
                                         const std::string& records_json);

 private:
  // The Browser whose tabs the sidebar's rows actually describe, which is not
  // always browser_ - see the .mm for why.
  Browser* SidebarTargetBrowser() const;
  // The rail position of the space this window shows, for every core call
  // that takes an active_space_index.
  int WindowSpaceIndex() const;
  // Whether this window's gestures may reach the persisted document. Both
  // browsers, because a slot can show another profile's rows: an off-the-record
  // one at either end means the write is somebody's private browsing.
  bool WritesAllowed() const;
  // Whether a tab carrying `tab_id` was found in the target strip and
  // activated. A favourite click needs the answer, not just the attempt, so it
  // can fall back to opening the page.
  bool ActivateTabWithId(const std::string& tab_id);
  // Whether the page opened for `favorite_url` is still in the target strip and
  // was activated. Found by the key the tab carries, not by its URL, so a
  // favourite that redirected is still its own tab.
  bool ActivateFavoriteTab(const std::string& favorite_url);
  bool ActivateTabWithUrl(const std::string& url);
  void OpenFavoriteTab(const std::string& favorite_url);
  // The four ways a favourite finds its page, in the order a tile click needs
  // them. Shared with the Courtyard, whose favourite cards are the same rows.
  void ActivateFavoriteRow(const std::string& favorite_url,
                           const std::string& tab_id);
  void ActivateSidebarTab(const std::string& tab_id);
  void CloseSidebarTab(const std::string& tab_id);
  void DuplicateSidebarTab(const std::string& tab_id);
  // Put on Shelf, from the row context menu or the edge drop
  // zone. Resolves the tab server-side rather than trusting a client-supplied
  // title, same posture as ActivateSidebarTab/CloseSidebarTab above.
  void ShelveSidebarTab(const std::string& tab_id);
  // Put on Shelf from a favourite tile. A favourite has no live
  // tab to resolve a title from, so the client supplies both the url and the
  // title it already renders (see buildFavoriteMenuItems).
  void ShelveFavorite(const std::string& url, const std::string& title);
  // hands Rewind's payload to the core, then does the half only
  // the browser can - closing the tabs the moment did not hold, reopening the
  // rows whose tabs are gone, and landing on the tab it was showing.
  void RestoreSidebarSlice(const std::string& payload);

 public:
  // brings the space's first remaining row back to life after the
  // strip empties, so closing a tab does not leave the window blank with a
  // perfectly good row one click away. Public because BrowserController's
  // tab-strip observer is what notices the strip emptied - same shape as
  // ActivateOwnTab crossing the other way. Safe to call when there is nothing
  // to promote: it no-ops when the strip refilled meanwhile or the space has
  // no other row.
  void PromoteSurvivingSidebarRow();

  // Shows the command-bar widget, building it on first use. Reached from every
  // trigger there is - the C++ IDC_NEW_TAB intercept
  // (AtriumNativeWidgetMac::ExecuteCommand, via BrowserController's public
  // OpenCommandBar()), the hotkey map, and the startup bootstrap.
  // `is_startup_open` forwards to CommandBarWidget::Show() - see its doc.
  void OpenCommandBar(bool is_startup_open = false);

  // The anchored open. `anchor_in_window` is the clicked row's rect in this
  // window's own coordinates; the widget clamps it and hands the box to the
  // page. Routed through the controller so the reason latch is set the one
  // way every other reason is.
  void OpenCommandBar(CommandBarOpenReason reason,
                      const gfx::Rect& anchor_in_window);

  // Opens the URL of an accepted palette match in a foreground tab. Declines
  // an invalid or javascript: target, and a window with no native host.
  void OpenCommandBarMatch(const GURL& target);

  // For the palette's results callback, which is held by a bridge this window
  // outlives in the ordinary case but not during teardown.
  base::WeakPtr<AtriumBrowserWindow> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // builds the little window's top strip on the /foyer route, and
  // hands it the active tab's title and URL. Both are no-ops on a regular
  // window, which has no foyer slot.
  void BuildHtmlFoyer();
  void PushFoyerSnapshot();

  // builds the Developer Mode URL toolbar on the /devbar route,
  // hidden until PushDevBarState() shows it. Cmd+Shift+D toggles dev_mode_on_
  // for the window; a localhost origin shows it regardless (lab parity).
  void BuildHtmlDevBar();
  void ToggleDevBarMode();
  void PushDevBarState();
  // the focused pane's bar wears ActivePaneRingView's own
  // accent. Off (both args false/empty) outside a split.
  void PushDevBarFocusRing();

  // CommandBarWidget::Close() calls this on every close so the row
  // an anchored open's click hid (sidebar.js) reappears again.
  void PushAddressRowHidden(bool hidden);

  // the panel and the sidebar are separate WebContents, so the row
  // marker travels through here. Empty marks no row.
  void PushAiPanelRowMark(const std::string& tab_id);

  // A Cmd+T open has no tab yet, so the sidebar moves the selection onto its
  // New Tab button until the bar closes.
  void PushCommandBarNewTabPending(bool pending);

  // Hands the palette a fresh result set (BuildCommandBarMatchesJSON's shape).
  // A no-op before the first open, which is correct: no palette is on screen
  // to show them, and Show() asks for a fresh pass anyway.
  void PushCommandBarMatches(const std::string& json);

  // Cmd+T toggle support: whether the palette is currently visible, and a
  // show/hide toggle that creates no tab (see ToggleCommandBar impl).
  bool IsCommandBarShowing() const;
  void ToggleCommandBar();

  // Dispatches a real keydown for the configured hotkey into
  // ai_panel_contents_. No-op while the AI kill switch is off.
  void ToggleAiPanel();

  // Extension side panels. `owner` keeps the panel's state, this window only
  // hosts the view. A null `owner` is for tests.
  void ShowExtensionPanel(std::unique_ptr<views::View> view,
                          AtriumSidePanelUI* owner,
                          const std::string& extension_id);
  // Hands the view back without destroying it. Destroying it fires the
  // extension's onClosed, which a swap must not do.
  std::unique_ptr<views::View> DetachExtensionPanel();
  bool ExtensionPanelShowing() const;
  // The user's close: the header button, the verb and the menu. Focus goes
  // back to the page.
  void CloseExtensionPanel();
  // A cross-profile swap: the outgoing Browser takes its view back, and the
  // incoming Browser's open panel moves in.
  void PresentExtensionPanelFor(Browser* browser);
  // The verb and the View menu: close the panel, or reopen the last one this
  // window showed.
  void ToggleExtensionPanel();
  bool CanToggleExtensionPanel() const;

  // Drops the panel's per-tab conversation for a tab that left this window.
  void ForgetAiPanelTab(uint64_t tab_id);

  // Overview is a pane of the sidebar's rail, so "showing" means
  // the rail is parked on it and toggling pages the rail, not a window.
  bool IsOverviewShowing() const;
  void ToggleOverview();

  // Pages the rail to the Overview pane. GlideSidebarToSpace cannot: its
  // index is a SPACE, and Overview is a pane that is not one.
  void GlideSidebarToOverview();

  // Cmd+D: pins the shown tab, or unpins it when it is already
  // pinned. False when nothing moved, so the key falls through to Bookmark
  // This Tab rather than being swallowed on a window with no row to move.
  bool TogglePinShownTab();

  // Cmd+1..9: opens the favourite tile in that slot, exactly as a
  // click on it does. False when the space has no tile there, so the key falls
  // through instead of being swallowed. kLastFavorite asks for the last tile.
  bool ActivateFavoriteInSlot(int ordinal);

  // Shows Overview and selects `section` - a toast click means take me there,
  // so it never toggles an open surface shut.
  void OpenOverviewAtSection(const std::string& section);

  // Archives the active space's ungrouped tabs: the sidebar's Clear icon and
  // the clearTabs hotkey. False when the space had none.
  bool ArchiveSpaceTabs();
  // Live tab ids of the active space's loose rows, in row order, empty for a
  // cold row. An archive takes the rows, so the pages they held close too.
  std::vector<std::string> LooseRowTabIds();

  // the Courtyard (own CourtyardWidget, own /courtyard route).
  // Reached from the sidebar's "Courtyard" menu entry.
  void OpenCourtyard();
  // a wheel or gesture delta on the active page, from the url bar
  // observer. Positive means the content moves down. Slides the devbar.
  void OnPageScrolled(double delta_y);
  void CloseCourtyard();
  bool IsCourtyardShowing() const;
  // Whether the open plane last reported a zoom at or above 1.0. The close is
  // this gate's, not the pinch threshold's - see StepWheelAccumulator.
  bool IsCourtyardZoomAtOrAboveOne() const;
  // Hands one pinch sample to the open plane.
  void ForwardPinchToCourtyard(double delta_y);
  // True over the docked sidebar's bounds, or the floating panel's while it is
  // up - the pinch monitor's gate for claiming a gesture while closed.
  bool IsPointOverSidebar(const gfx::Point& point_in_window) const;
  content::WebContents* CourtyardContentsForTesting() const;

  // Everything between the window's NSVisualEffectView backdrop and the
  // Courtyard's glass ground has to go while it is open. Driven by the
  // widget's visibility, so a hide this window never asked for still restores.
  void SetCourtyardOpen(bool open);
  // Two conjunctions rather than one negated: `!hides` is satisfied by any one
  // of the three coming back, which is not what a close has to do.
  bool CourtyardHidesContentForTesting() const;
  bool CourtyardContentRestoredForTesting() const;

  // Delivers `name` the way the palette's own renderer does - through
  // CommandBarWidget, whose SurfaceHost is the only one the palette's
  // WebContents has. Sending straight to this window instead would skip the
  // forward that makes a palette verb reach a handler at all, which is the
  // exact step that was missing. Null-safe: no palette open, no delivery.
  void SendToCommandBarHostForTesting(std::string_view name);

  // Exercises the same StepWheelAccumulator path a real ctrl+wheel NSEvent
  // feeds - routing an actual synthetic NSEvent through this window's real
  // event loop risks the same hang already seen from DOM assertions here.
  void SimulateCtrlWheelDeltaForTesting(
      double delta_y,
      bool over_sidebar = true);  // true keeps every pre-existing caller's meaning

  // Synthesizes a Cmd+Q key equivalent against the Courtyard's own
  // CommandDispatcher delegate and reports whether it routed to the main
  // menu. False if the Courtyard isn't open or has no
  // delegate installed.
  bool CourtyardCmdQRoutesToMainMenuForTesting();

  // Presses `combo` ("cmd+shift+1") as a synthetic NSEvent through this
  // window's real performKeyEquivalent path, and reports whether it was
  // handled. False for a combo the hotkey map would refuse to bind.
  bool DeliverComboForTesting(std::string_view combo);

  // The sidebar snapshot JSON, same shape PushSidebarSnapshot pushes to the
  // docked sidebar - CourtyardWidget re-pushes it to its own WebContents.
  std::string CourtyardSnapshotJSON() const;

  // the canvas positions. `space` is a position in sidebar
  // order, never the active space - the Courtyard shows every room at once.
  void ApplyCourtyardRoomRect(int space,
                              double x,
                              double y,
                              double w,
                              double h);
  void ApplyCourtyardCardX(int space,
                           const std::string& section,
                           int index,
                           double x);

  // a Courtyard card click. Switches to `space` first, because
  // the snapshot resolves live tab ids for the active space alone, so `index`
  // can only be turned into a tab once its room is the active one.
  void ActivateCourtyardCard(int space,
                             const std::string& section,
                             int index);

  // writes the physically-displayed tab's visible URL (the one the
  // user sees, respecting the cross-profile presenter swap - see
  // CurrentlyDisplayedWebContents) to the system clipboard and flashes a brief
  // themed confirmation in the status bubble. Returns false when no page is on
  // screen (empty/sidebar-only window), letting the Cmd+Shift+C intercept fall
  // through to its stock meaning.
  //
  // `format` selects a plain-URL, Markdown-link, or blockquoted-
  // Markdown-link write - same failure/confirmation behavior for all three,
  // just a different clipboard string. Defaulted so every pre-existing
  // Cmd+Shift+C call site is unaffected.
  bool CopyCurrentPageLink(LinkCopyFormat format = LinkCopyFormat::kPlain);

  // Hides the command-bar widget (Escape/click-outside via the
  // command_bar_dismiss op). A no-op if there is no widget.
  void CloseCommandBar();

  // Opens the find bar over the tab this window is showing, or refocuses and
  // selects the field when it is already up. Never opens a second bar.
  void OpenFindBar();

  // Walks the matches. Opens the bar first when it is closed, on the tab's own
  // last search - Chrome's behaviour for Cmd+G with no bar up.
  void FindNextMatch(bool forward);

  void CloseFindBar();
  bool IsFindBarShowing() const;

  // Opens the theme studio window on `payload_json` (the ThemeStudioPayload
  // shape: spaceId, spaceName, accent, spec), placed above `anchor_in_screen`.
  void ShowThemeStudio(const std::string& payload_json,
                       int rail_index,
                       const gfx::Rect& anchor_in_screen);
  void CloseThemeStudio();
  bool IsThemeStudioShowing() const;
  // The rail index the open studio edits, or -1.
  int ThemeStudioSpaceIndex() const;

  // The studio's half of settingsThemePreview / settingsThemeChanged: the same
  // BrowserController entry points the settings window drives, so an edit made
  // in the sidebar and one made in the Spaces pane cannot diverge.
  void ApplySidebarThemeEdit(std::string_view name,
                             const base::ListValue& args);

  // What ShowTab did. The two non-kShown answers both leave `contents`
  // unshown and must not be conflated: one wants a recovery, the other is the
  // freeze deliberately holding the pane still.
  enum class ShowTabOutcome {
    kShown,
    // `contents` belongs to a space the rail is not on. The pane was emptied,
    // or left alone when split, because neither of its tabs may be detached.
    kRefused,
    // Swallowed by an in-flight freeze, WebView untouched - ArmMoveOutFreeze.
    kSwallowed,
  };

  // Hosts `contents` (may be null) in the right-pane WebView. Idempotent:
  // WebView::SetWebContents early-returns on the current contents.
  ShowTabOutcome ShowTab(content::WebContents* contents);

  // activating a tab the strip ALREADY holds active notifies no
  // observer, so ShowTab never runs and a pane emptied on the way out of a
  // space has no path back to the page. A no-op while the pane holds
  // contents, or while the strip's active tab is not `expected_tab_id`.
  void ShowActiveTabIfPaneIsEmpty(uint64_t expected_tab_id);

  // The active tab in this window's presentation target (SidebarTargetBrowser
  // - see its doc), or null if there is none. Public so a row activation can
  // confirm it actually landed there rather than being dropped.
  content::WebContents* SidebarActiveTab() const;

  // animates the currently-displayed WebContents NSView's
  // alphaValue to alpha_percent/100 over duration_ms via NSAnimationContext
  // (CA-driven - keeps running through main-thread stalls). ShowTab
  // unconditionally restores alpha 1 on whatever it shows, so a fade can
  // never strand content invisible. No-op when no contents is displayed.
  void SetContentFade(int32_t alpha_percent, int32_t duration_ms);

  // whatever WebContents is currently attached to the visible
  // pane's WebView - NOT necessarily browser()'s own active tab, since a
  // cross-profile in-window swap can leave this window
  // presenting a DIFFERENT controller's tab. Used to capture what's about
  // to be hidden right before ActivateWindow's kInWindowSwap branch calls
  // ShowTab with the incoming contents. Out-of-line (like ShowTab) since
  // views::WebView is only forward-declared here.
  content::WebContents* CurrentlyDisplayedWebContents();

  // Gives the page on screen the keyboard, unless another app or an Atrium
  // surface window (command bar, find bar, peek card, quick menu) holds key.
  void FocusDisplayedPage();

  // whichever pane's WebView hosts `contents`, or null. Scans every
  // pane, not just ActivePane() - a background split pane can hold the tab
  // whose renderer died. Feeds atrium_sad_tab_hook.h.
  views::WebView* PaneWebViewForContents(content::WebContents* contents);

  // Writes the space tint and the native chrome mode into one overlay
  // document. The sidebar has its own push, so this serves the others: the
  // command bar today, the permission sheet and peek controls later.
  void PushOverlayTheme(content::WebContents* contents);

  // Every overlay calls this before its widget's Show(). macOS does not show a
  // child window while the startup hold keeps this window off screen.
  void ReleaseStartupHold();
  bool StartupHoldPendingForTesting() const { return startup_hold_.pending(); }

  // Subscribes once, for the process, to the AppKit notifications that fire
  // when the accent changes in System Settings.
  static void EnsureSystemAccentObserver();

  // Re-derives and re-pushes on every window wearing the system accent.
  static void OnSystemAccentChanged();

  // Registers (or replaces) one floating surface's drop
  // shadow with this window's AtriumOverlayShadowLayer. `rect_in_screen`
  // uses Widget::GetWindowBoundsInScreen's coordinates.
  void RegisterOverlayShadow(const std::string& surface_id,
                             const gfx::Rect& rect_in_screen,
                             const AtriumOverlaySurfaceShadow& shadow);
  void UnregisterOverlayShadow(const std::string& surface_id);
  // Zero before any surface registers one, so a test that expects a release
  // has to raise a shadow first for the reading to mean anything.
  size_t OverlayShadowCountForTesting() const {
    return overlay_shadow_layer_ ? overlay_shadow_layer_->ShadowCountForTesting()
                                 : 0u;
  }
  bool OverlayShadowStartedAwayForTesting(const std::string& surface_id) const {
    return overlay_shadow_layer_ &&
           overlay_shadow_layer_->ShadowStartedAwayForTesting(surface_id);
  }
  size_t OverlayShadowBuildCountForTesting(
      const std::string& surface_id) const {
    return overlay_shadow_layer_
               ? overlay_shadow_layer_->ShadowBuildCountForTesting(surface_id)
               : 0u;
  }
  // Fades and slides a registered shadow out with the surface it belongs to,
  // for a surface that leaves by animating rather than by disappearing.
  void SetOverlayShadowAway(const std::string& surface_id,
                            bool away,
                            const gfx::Vector2d& shift,
                            base::TimeDelta duration);

  // Shows (or moves) the floating sidebar's frost at `rect_in_screen`.
  // `corner_radius` must match the panel's own OverlayCornerRadius(), or the
  // frost's square corners show behind the panel's rounded ones.
  void SetSidebarFrostFrame(const gfx::Rect& rect_in_screen,
                            float corner_radius);
  // Slides the frost out over `duration`, in step with the panel's own
  // dismiss slide. Strength never ramps.
  void AnimateSidebarFrostHide(base::TimeDelta duration, double slide_dx);
  // Slides the frost in over `duration`, in step with the panel's own
  // reveal slide. Strength never ramps.
  void AnimateSidebarFrostShow(base::TimeDelta duration, double slide_dx);
  void HideSidebarFrost();
  bool SidebarFrostSlidingForTesting() const;
  // The frost's live sideways offset. It has to match the panel's own, or the
  // two show different bands of page mid-slide.
  double SidebarFrostSlideDxForTesting() const;
  atrium::AtriumWindowPlatform::ControlProbe ProbeWindowControlsForTesting()
      const;

  // Single-embed invariant: detach `contents` from this window's
  // active pane, but ONLY if that pane currently displays it (identity guard).
  // Called by the cross-profile in-window swap right before the incoming
  // contents is attached into the PRESENTER's pane, so the one WebContents is
  // never embedded in two panes' NativeViewHosts at once (the double-embed that
  // aborts a later detach at DCHECK(views_host_) - the cold/pinned-tab-select
  // crash). A no-op when this pane doesn't host `contents`.
  void ReleaseDisplayedContents(content::WebContents* contents);

  // ShowTab's two claims only. The split, drop and survivor claims
  // run while the source window still holds the page on purpose, and evicting
  // there crashes the split-drop tests. Skips this window's own panes.
  void ReleaseContentsFromOtherWindows(content::WebContents* contents);

  // Link-hover status bubble. Called from the atrium::
  // MaybeUpdateStatusBubbleURL hook in Browser::UpdateTargetURL (browser.cc)
  // whenever ANY WebContents' hovered-link target URL changes. `source` is
  // compared against the active pane's WebContents (mirrors stock
  // Browser::UpdateTargetURL's own anchor check) so a hover reported for a
  // tab that isn't the one currently on screen can't drive the visible
  // bubble. `url.is_empty()` means the mouse left the link - see
  // atrium_status_bubble_hook.h for why this isn't fed through
  // GetStatusBubbles() instead.
  void SetStatusBubbleURL(content::WebContents* source, const GURL& url);

  // Freezes this window's content pane on `leaving_contents`'s last-rendered
  // frame (desaturated, Arc-parity revision 2) instead of live content - call
  // right BEFORE detaching a tab from this window's own TabStripModel to move
  // it to another Atrium window (BrowserController::ActivateTab). A no-op if
  // `leaving_contents` isn't what this window is currently displaying (the
  // move doesn't affect what this window shows or has selected).
  //
  // Needed because detaching the window's ACTIVE tab makes TabStripModel
  // auto-reselect another tab in the SAME window's own strip when one is
  // left behind - both this window's own BrowserWindow::OnActiveTabChanged
  // routing and BrowserController's direct TabStripModelObserver callback
  // would otherwise call ShowTab for that auto-reselected tab, silently
  // switching this window's pane (and, via the caller's ShowTab-return-value
  // check, its recorded per-slot selection) away from the tab that just left
  // - exactly the "per-slot last-active fallback activation on move-out" bug
  // the Arc-parity revision 2 spec requires killed. `tab_id` is cached so a
  // later click on the snapshot (or the sidebar row, in Swift) can take the
  // tab back via BrowserController::ActivateOwnTab.
  //
  // `on_frozen` runs once the pane shows the snapshot, or at once when there
  // is nothing to freeze. The caller detaches the tab from there, so the
  // pane never paints a blank frame between the live page and the snapshot:
  // the capture needs the live surface, and clearing the pane before the
  // frame landed was the blink.
  void ArmMoveOutFreeze(content::WebContents* leaving_contents,
                        uint64_t tab_id,
                        base::OnceClosure on_frozen);

  // Whether this window is currently showing a frozen snapshot rather than
  // live content - true from ArmMoveOutFreeze until the next real ShowTab
  // call (the tab returning, or a different tab activating) clears it.
  bool IsFrozen() const { return is_frozen_; }
  uint64_t FrozenTabId() const { return is_frozen_ ? frozen_tab_id_ : 0; }
  // Whether the frozen pane holds a captured frame rather than the empty
  // fallback a failed capture leaves.
  bool FrozenSnapshotHasImageForTesting() const;

  // Enters split on this
  // window: `secondary_contents` becomes the secondary pane, this window's
  // CURRENT active-pane tab becomes primary. No-op (logged) if `secondary_
  // contents` is null or this window is already split (use UpdateSplit for
  // orientation/ratio changes on an existing split). `ratio` is clamped
  // defensively (the SetSidebarWidth posture: never trust a cross-ABI value
  // alone) via AtriumPaneContainerView's own min-extent rule. Pushes split
  // state (reason "op") on success.
  void EnterSplit(content::WebContents* secondary_contents,
                  SplitOrientation orientation,
                  double ratio);

  // always a fresh chrome://atrium.top-chrome/notes
  // WebContents, never reused, so a reopen re-fetches the active space's
  // note instead of showing a stale pad.
  void OpenNotesBesideThisPage();

  // stores `quote` as a highlight on the active tab's URL and
  // opens the pad so the new comment card is visible. False when the quote is
  // under three characters or nothing is open, which is what the context-menu
  // item's enabled state is derived from.
  bool HighlightSelectionOnActivePage(const std::string& quote);

  // Switches to `space_index` (sidebarSpaceSelected's convention) then opens
  // its pad - OpenNotesBesideThisPage always shows whichever space is active.
  void OpenSpaceNoteBesideThisPage(int32_t space_index);

  // Opens beside whatever space is active and shows `note_id` instead of that
  // space's own pad - a freestanding note belongs to no space to switch to.
  void OpenFreestandingNoteBesideThisPage(const std::string& note_id);

  // Mirror View's open verb. `profile_id` not found among
  // bootstrapped spaces fails cleanly (logged, no split); mirror.js's
  // LOGGED_OUT sentinel (" logged-out") gets a fresh OTR profile instead.
  void MirrorTabToProfile(const std::string& url,
                          const std::string& profile_id);

  // RouteContentDrop's real entry point, called once `new_contents`
  // already belongs to this window's own TabStripModel. kCenter swaps it
  // into the active pane in place, any other edge grows the tree.
  void InsertPaneFromDrop(content::WebContents* new_contents,
                          atrium::SplitEdge edge);

  // Changes orientation and/or ratio of an already-split window. No-op
  // (logged, no push) if not currently split.
  //
  // unlike EnterSplit/CloseSplit, this path is not one of the
  // four report points, so it does not push split state - only ratio/
  // orientation change here, not enter/collapse/divider-commit/tab-swap.
  void UpdateSplit(SplitOrientation orientation, double ratio);

  // Dissolves the split, leaving `keep_contents` as this window's single
  // shown tab (null keeps whichever pane is currently active - the
  // close_split op's keep_tab_id==0 contract, resolved by the caller before
  // this is reached). No-op (logged, no push) if not currently split.
  // Pushes split state (reason "op", active=false) on success.
  void CloseSplit(content::WebContents* keep_contents);

  // Docked DevTools, split-view Phase C: re-implements
  // DevtoolsUIController::UpdateDevtools (devtools_ui_controller.cc) for
  // this chromeless window, which has no ContentsContainerView/
  // DevtoolsUIController of its own - see atrium_devtools_bridge.h's
  // MaybeUpdateAtriumDockedDevtools, the sole caller from outside this
  // class. Re-runs the pane-selection + strategy lookup against EVERY pane
  // this window currently has (one outside a split, up to kMaxSplitLeaves
  // while split); a pane not currently showing `inspected_web_contents` is
  // left untouched. `inspected_web_contents` may be null, which hides
  // docked devtools wherever it's currently shown. Re-run from ShowTab/
  // EnterSplit/FinishCloseSplit, next to the existing RetargetLoadProgress
  // call sites - deliberately NOT from ReattachActiveTabForDamageHeal (a
  // same-tab heal re-running this every 250ms would flicker the docked
  // layout for no reason).
  void UpdateDockedDevtools(content::WebContents* inspected_web_contents);

  bool IsSplit() const;

  // the split's orientation - only meaningful while IsSplit() is
  // true (kSideBySide, the pane_container's default, otherwise). Lets
  // callers outside this class (the split-view link menu's label/icon) read
  // it without their own AtriumPaneContainerView handle.
  SplitOrientation split_orientation() const;

  // The root gutter's current ratio (0.5 while unsplit) - lets tests verify
  // it survives ExitSplit/re-entry without a raw pane_container() handle.
  double SplitRatioForTesting() const;

  // The divider strip for the gutter at `index` (0 is the root one), so a
  // test can drag a nested divider with real mouse events. Null past the
  // last gutter.
  views::View* SplitDividerForTesting(size_t index);

  // How many holders the pane-claim ledger records for `contents`. More than
  // one is the double claim that aborts later, in the other holder.
  static size_t ClaimHoldersForTesting(content::WebContents* contents);

  // whether the pane currently showing `contents` holds a docked
  // DevTools view, and how many panes hold one at all.
  //
  // The per-pane claim needs a per-pane question, and a split is the only shape
  // where a wrong answer exists - a single-pane window cannot distinguish "the
  // inspected pane took it" from "a pane took it". Returns false when no pane
  // is showing `contents`.
  bool PaneHasDockedDevtoolsForTesting(content::WebContents* contents) const;
  int PanesWithDockedDevtoolsForTesting() const;

  // A real divider drag is a synthetic mouse sequence on
  // AtriumPaneContainerView::AtriumSplitResizeArea - this calls the
  // drag-end callback directly so a test can exercise it without one.
  void OnDividerResizeCommittedForTesting() { OnDividerResizeCommitted(); }

  // Null before OpenNotesBesideThisPage's first successful call.
  content::WebContents* NotesContentsForTesting() const {
    return notes_contents_.get();
  }

  // Null before MirrorTabToProfile's first successful call.
  content::WebContents* MirrorContentsForTesting() const {
    return mirror_contents_.get();
  }

  // Empty when the given pane has no badge showing, or (secondary) no split.
  std::u16string MirrorBadgeTextForTesting(bool secondary) const {
    AtriumPaneContainerView* container = root_view_->pane_container();
    AtriumPaneView* pane =
        secondary ? container->secondary_pane() : container->primary_pane();
    return pane ? pane->MirrorBadgeTextForTesting() : std::u16string();
  }

  // The page the pane at `index` in the split tree is showing. Leaf order is
  // layout order, so this is what says which SIDE a pane ended up on.
  content::WebContents* PaneContentsForTesting(size_t index);

  // whether the pane at `index` took immersive's squared radius.
  // False for an out-of-range index rather than a crash - PaneContentsForTesting's
  // convention.
  bool PaneImmersiveForTesting(size_t index) const;

  // 0 for an out-of-range index, same convention as PaneImmersiveForTesting.
  float PaneTileCornerRadiusForTesting(size_t index) const;

  // Drives AtriumPaneContainerView::SetImmersive directly, skipping the native
  // NSWindow fullscreen transition SetImmersiveFullscreen would otherwise
  // trigger - a browser test only needs the pane-radius side effect.
  void SetPaneContainerImmersiveForTesting(bool immersive);

  // Panes whose focus subscription is still live. A pane without one takes
  // no activation from a click in its page.
  size_t PaneFocusSubscriptionCountForTesting() const;

  // Null until BuildHtmlAiPanel's PostWhenReady callback runs.
  content::WebContents* AiPanelContentsForTesting() const {
    return ai_panel_contents_.get();
  }

  // ai-panel.js's own open/closed state, as last reported through
  // aiPanelToggled.
  bool AiPanelOpenForTesting() const { return ai_panel_open_; }
  views::View* ExtensionPanelContentForTesting();
  gfx::Rect ExtensionPanelBoundsForTesting() const;
  views::View* ExtensionPanelCardForTesting();
  bool ExtensionPanelShadowVisibleForTesting() const;
  void SetRootImmersiveForTesting(bool immersive);
  views::View* ExtensionPanelGripForTesting();
  views::Button* ExtensionPanelCloseButtonForTesting();
  std::u16string ExtensionPanelTitleForTesting() const;
  SkColor ExtensionPanelGripAccentForTesting();
  std::vector<gfx::Rect> ClientRegionsForTesting() const;

  // Views-level focus, independent of ai-panel.js's own document.activeElement
  // - the browser test needs both proven separately. views::WebView is only
  // forward-declared here, so these are defined in the .cc.
  bool AiPanelWebViewHasFocusForTesting() const;

  // Same view FocusWebContentsPane hands focus to, so a test can confirm
  // closing the ai-panel actually returned focus to the page.
  bool ActivePaneWebViewHasFocusForTesting() const;

  // Runs the re-attach ShowTab schedules 250ms out, now.
  void RunDamageHealForTesting();

  // AtriumRootView-local bounds of the native ai_panel_container_, the same
  // space GetClientRegions and AtriumClientView::NonClientHitTest use.
  gfx::Rect AiPanelContainerBoundsForTesting() const {
    return root_view_->ai_panel_container()->bounds();
  }

  // proves which tab summarize would act on without
  // depending on on-device model availability in the test environment.
  content::WebContents* ResolveAiPanelTargetContentsForTesting(
      const std::string& tab_id) {
    return ResolveAiPanelTargetContents(tab_id);
  }

  // Null until BuildHtmlSidebar() has run - see NotifySnoozedTabReturned.
  content::WebContents* sidebar_contents() const {
    return sidebar_contents_.get();
  }
  content::WebContents* SidebarContentsForTesting() const {
    return sidebar_contents();
  }

  // The palette's own contents, so a test can give its frame a real gesture
  // rather than fake the activation the capture verbs are gated on.
  content::WebContents* CommandBarContentsForTesting() const;

  // Null until the first OpenFindBar builds the bar's document.
  content::WebContents* FindBarContentsForTesting() const;

  // Whether the find bar's page holds its widget's keyboard focus.
  bool FindBarPageHoldsKeyboardForTesting() const;

  // Null until the first ShowThemeStudio builds the page.
  content::WebContents* ThemeStudioContentsForTesting() const;
  gfx::Rect ThemeStudioBoundsForTesting() const;
  void DragThemeStudioByForTesting(int dx, int dy);
  bool ThemeStudioPageHoldsKeyboardForTesting() const;

  // Drives one sidebarSpaceSelected without a renderer to send it.
  void OnSurface_SidebarSpaceSelectedForTesting(const base::ListValue& args);

  // Drives one sidebarTabSplit without a renderer to send it.
  void OnSurface_SidebarTabSplitForTesting(const base::ListValue& args);

  // Drops that focus, standing in for a click that takes key elsewhere.
  void ClearFindBarPageFocusForTesting();

  // Drives one findQueryChanged without a renderer to send it.
  void FindQueryChangedForTesting(const std::u16string& query);

  // Whether the live key window is the find bar's own window or its parent's
  // - the check MaybeReclaimKey's decision to reclaim key turns on.
  bool FindBarKeyIsInOwnGroupForTesting() const;

  // Whether the palette's page holds its widget's keyboard focus.
  bool CommandBarHoldsKeyboardForTesting() const;

  // Whether the palette's own widget currently holds key.
  bool CommandBarIsActiveForTesting() const;

  // Drops that focus, and replays the deferred document build - together they
  // reproduce the launch open, whose document arrives after the open ran.
  void ClearCommandBarKeyboardForTesting();
  void RetryCommandBarContentsForTesting();

  // Whether the page's NSView is the palette window's first responder - the
  // AppKit half a mouse click was supplying by hand.
  bool CommandBarFirstResponderIsPageForTesting() const;

  // Whether a region selection is on screen. The observable half of
  // captureRegion, which opens the overlay before anything is captured.
  bool RegionCaptureOverlayOpenForTesting() const {
    return static_cast<bool>(capture_overlay_widget_);
  }

  // The origin PushSidebarUrl last sent, without executing JS against
  // sidebar_contents_ to read it back.
  const std::string& LastPushedSidebarOriginForTesting() const {
    return last_pushed_sidebar_origin_;
  }

  // The floating bar's page. Only for the overlay theme, whose tokens live in
  // its document and have no native counterpart to read.
  content::WebContents* DevBarContentsForTesting(size_t index = 0) const {
    return index < dev_bars_.size() ? dev_bars_[index]->contents() : nullptr;
  }

  // Whether the devbar container is currently visible, read natively rather
  // than by asserting against the bar's DOM (RULE ZERO).
  bool DevBarShownForTesting() const {
    return root_view_ && root_view_->dev_bar_container() &&
           root_view_->dev_bar_container()->GetVisible();
  }
  // How many bars are up, and where bar `index`'s slot sits in the pane
  // container's coordinates.
  size_t DevBarsShownForTesting() const { return shown_dev_bar_count_; }
  gfx::Rect DevBarBoundsForTesting(size_t index) const {
    return index < dev_bars_.size() && dev_bars_[index]->container()
               ? dev_bars_[index]->container()->bounds()
               : gfx::Rect();
  }
  // Native truth, not the page's own .focused class (RULE ZERO) -
  // what PushDevBarFocusRing actually pushed to bar `index`, not a
  // recomputation from pane state that stays right even if the push breaks.
  bool DevBarFocusedForTesting(size_t index) const {
    return index < dev_bars_.size() && dev_bars_[index]->FocusedForTesting();
  }
  std::string DevBarFocusRingForTesting(size_t index) const {
    return index < dev_bars_.size() ? dev_bars_[index]->FocusRingForTesting()
                                    : std::string();
  }

  // Whether the active pane's WebView is on screen. Read alongside
  // CurrentlyDisplayedWebContents to tell "showing the tab" from "hidden with
  // the empty card up", which ShowTab(nullptr) leaves behind.
  bool PaneWebViewShownForTesting();

  // Runs the same re-anchor a real navigation/tab-switch triggers, without
  // waiting on either.
  void ReanchorMarginaliaOnActiveTabForTesting() {
    ReanchorMarginaliaOnActiveTab();
  }

  // Runs the same click-queue drain DidGetUserInteraction triggers, without
  // depending on that observer callback firing during the test.
  void DrainMarginaliaMarkClicksForTesting() { DrainMarginaliaMarkClicks(); }

  void InstallVimOnActiveTabForTesting() { InstallVimOnActiveTab(); }
  void SendVimKeyForTesting(const std::string& character) {
    SendVimKey(character);
  }
  void DrainVimModeForTesting() { DrainVimMode(); }
  void ShowVimHintsForTesting() { ShowVimHints(); }
  bool RunVimBrowserVerbForTesting(const std::string& answer) {
    return RunVimBrowserVerb(answer);
  }
  void RunVimVerbForTesting(const std::string& character) {
    RunVimVerb(character);
  }
  content::KeyboardEventProcessingResult RouteVimKeyForTesting(
      const std::string& character,
      bool control = false);

  // Lets a test replay content::WebContentsObserver's exact call order
  // without driving a real navigation.
  void OnActiveTabStartedLoadingForTesting() { OnActiveTabStartedLoading(); }
  void OnActiveTabLoadProgressForTesting(double progress) {
    OnActiveTabLoadProgress(progress);
  }
  void OnActiveTabStoppedLoadingForTesting() { OnActiveTabStoppedLoading(); }

  // Off by default: PushLoadPhase runs for the life of every browser window,
  // and an unbounded log would grow for the whole session. A test turns this
  // on to pin the exact phase/fraction sequence a run pushes.
  void SetLoadPhasePushRecordingForTesting(bool record) {
    record_load_phase_pushes_for_testing_ = record;
    load_phase_pushes_for_testing_.clear();
  }
  const std::vector<std::pair<atrium::LoadPhase, double>>&
  LoadPhasePushesForTesting() const {
    return load_phase_pushes_for_testing_;
  }

  // The OTHER split pane's tab id if `tab_id` names either of this window's
  // two current split panes, else 0 (never a real tab id - see
  // the retired ABI header's SPLIT VIEW doc block). Used by BrowserController
  // to resolve the deterministic collapse survivor on close/move-out (plan
  // section 3) without this window exposing its raw tab-id bookkeeping.
  uint64_t SplitPartnerTabId(uint64_t tab_id) const;

  // Implicit dissolution: collapses
  // the split because `leaving_tab_id` (one of the two panes) is departing
  // this window for good - a genuine close or a cross-window move-out.
  // Purely visual/echo (no TabStripModel mutation) - callers separately
  // re-select the survivor: synchronously via BrowserController::
  // ActivateOwnTab for a move-out (ArmMoveOutFreeze runs from an ops
  // dispatch, not from inside TabStripModelObserver, so a synchronous
  // ActivateTabAt is safe there), or deferred one run-loop turn via
  // PostTask for a close (BrowserController::OnTabStripModelChanged IS
  // inside TabStripModelObserver dispatch when a close is detected, so
  // mutating the strip's selection synchronously there would re-enter
  // TabStripModel's own notification machinery - see that call site's
  // comment). A no-op if `leaving_tab_id` isn't a current split pane.
  // Pushes split state with `reason` ("tab_closed" or "moved_out").
  void CollapseSplit(uint64_t leaving_tab_id, const char* reason);

  // The close route for a tab leaving ANY pane, returning the tab to
  // re-select (0 when nothing moved). CollapseSplit reads only the two
  // cached ids, so a third pane's close used to leave a ghost pane behind.
  uint64_t ClosePaneForTab(uint64_t leaving_tab_id, const char* reason);

  // Tints the root view's background. `argb` is 0xAARRGGBB (alpha in the
  // top byte), matching AtriumHostOps::set_backdrop_tint's contract exactly:
  // SkColor uses the same ARGB byte order, so no channel reordering.
  void SetBackdropTint(uint32_t argb);

  // Workspace-scrub perf: snaps the tint directly to `argb` -
  // stops backdrop_tint_timer_ first, then applies with no crossfade - for
  // the live 60-120Hz scrub stream, where SetBackdropTint's restart-per-call
  // 300ms crossfade either perpetually postpones its first fire or produces
  // a lagged trail that never tracks the gesture. Same argb encoding as
  // SetBackdropTint. Mirrors SetBackdropTint's has_theme_ guard - see the
  // .mm for why. See the retired ABI header's set_backdrop_tint_live doc.
  void SetBackdropTintLive(uint32_t argb);
  // The full ink push SetBackdropTintLive withholds per frame - every
  // overlay's renderer hears the tint. Once, when a scrub ends.
  void PushBackdropInk();

  // drift the backdrop toward the active page's colour, and put it
  // back. Both no-op without an active space.
  void ApplyPageTint();
  // Fades the band out and puts the space tint back: no page is shown.
  void ClearPageTint();
  // One tab's load began or ended, for the ring on its row and tile.
  void PushTabLoading(uint64_t tab_id, bool loading);
  void RestoreSpaceTint();
  // The theme half of the same restore, deferred to the presenter catch-up
  // because construction runs before the controller adopts this Browser.
  void RestoreSpaceTheme();
  // The favicon arm of the same sampler. A background tab resolving its icon
  // must not spend a sample on the page in front, so this declines any
  // `contents` that is not the active one.
  void ApplyPageTintFor(content::WebContents* contents);
  static void SetChromePageTint(bool enabled);
  static std::optional<bool> ChromePageTintFromSettings(
      std::string_view settings_json);
  static bool& ChromePageTintEnabled();

  // The floating address bar's pin. Static like the page tint - one settings
  // field, and every window's bar answers to it.
  static void SetDevBarPinned(bool pinned);
  static std::optional<bool> AddressBarPinnedFromSettings(
      std::string_view settings_json);
  static bool& DevBarPinned();

  // Pushed from the delegate's flagsChanged monitor: the sidebar sees key
  // events only while it holds the keyboard, which it does not while someone
  // reads a page. Static because that monitor is process-wide.
  static void SetSidebarNavModifiers(bool shift, bool cmd, bool option);

  // The delegate's mouse-down monitor, for the same reason: a press on the
  // page or on native chrome never reaches the sidebar document, where its
  // menus listen for one. `screen_point` is in screen DIPs.
  static void DismissSidebarMenusForMouseDown(const gfx::Point& screen_point);
  // Leaving the app takes every menu with it - nothing will be clicked.
  static void DismissSidebarMenus();

  // No window is involved. It lives here so startup and the settings save
  // reach the Dock icon the same way they reach every other settings reader.
  static void SetAppIconVariant(const std::string& slug);

  // The value becomes a FILE NAME, so a spelling outside the slug alphabet -
  // a newer build's, or a hand-edited file's - is refused rather than passed.
  static std::optional<std::string> AppIconVariantFromSettings(
      std::string_view settings_json);

  // `alpha_percent` is 0..100 (clamped), matching
  // AtriumHostOps::set_glass_backdrop_alpha's contract exactly - same
  // percent-not-fraction convention as SetContentFade. Sets
  // backdrop_glass_alpha_ (default kDefaultGlassBackdropAlpha, 0xB8 - the
  // value every window painted before this existed) and repaints instantly -
  // see the retired ABI header's GLASS BACKDROP ALPHA doc block. A no-op if the
  // resulting byte already matches the current value.
  void SetGlassBackdropAlpha(int32_t alpha_percent);

  // the native-app chrome mode - `enabled` 0/1, `argb` 0xAARRGGBB
  // with the top byte ignored, matching AtriumHostOps::set_opaque_surround's
  // contract exactly. Forwards to the pane container's own fill; explicitly
  // NOT backdrop_glass_alpha_ and NOT root_view_'s background, both of which
  // are the sidebar's own surface. See the retired ABI header's
  // set_opaque_surround doc block.
  void SetOpaqueSurround(int32_t enabled, uint32_t argb);

  // See InstallRootBackground's doc. Every install of root_view_'s background
  // goes through these two, so the native-app surround cannot be repainted
  // over by a space switch or a theme.
  void InstallRootBackground(std::unique_ptr<views::Background> background);
  void RestoreRootBackground();
  // The surround owns the root, so it cannot go through InstallRootBackground,
  // whose latch is the surround itself. The Courtyard's latch still applies.
  void InstallSurroundRootBackground(
      std::unique_ptr<views::Background> background);

  // Stages the incoming space's theme for a live scrub
  // cross-fade - see the retired ABI header's set_backdrop_scrub_theme doc.
  // Same wire format and copy-before-return contract as SetBackdropTheme (no
  // ThemeSpec parser exists in C++); rasterizes into scrub_bitmap_ exactly
  // once here, so the per-frame SetBackdropScrubBlend call below is a pure
  // opacity composite. NULL rgba stages "flat tint incoming" (an empty
  // scrub_bitmap_ - the outgoing picture fades out to backdrop_tint_).
  // `grain` is the staged theme's film grain, blended by the weight too.
  void SetBackdropScrubTheme(const uint8_t* rgba,
                             int32_t w,
                             int32_t h,
                             float grain);

  // Per-frame blend weight in [0,1] for the staged scrub -
  // see the retired ABI header's set_backdrop_scrub_blend doc. Clamp + store +
  // SchedulePaint only, no rasterization - cheap enough for scrub cadence.
  void SetBackdropScrubBlend(double weight);

  // Ends the scrub compositing session - see
  // the retired ABI header's set_backdrop_scrub_blend_end doc. commit != 0
  // promotes the staged layer (scrub_bitmap_, or the live flat tint if none was
  // staged) into the real backdrop; commit == 0 just drops the staged
  // layer. Flick release: `settle_ms` > 0 with the staged weight
  // still short of full eases scrub_blend_weight_ the rest of the way over
  // that window (StepScrubBlendSettleAnimation) before promoting, instead of
  // snapping the remaining blend in one frame - see the amended ABI doc.
  void SetBackdropScrubBlendEnd(int32_t commit, int32_t settle_ms);

  // Liquid-glass window (stage 2): makes the NSWindow non-opaque and inserts
  // an NSVisualEffectView behind the compositor view so the translucent
  // backdrop tint frosts the desktop. Called once, right after widget Init.
  void InstallLiquidGlassBackdrop();

  // (re)applies the window's corner mask. The radius goes to 0 in
  // fullscreen - the window fills the screen, so rounding what we paint would
  // cut the page's own corners against black instead of following a rounded
  // frame that is no longer there. Re-applied on bounds changes because a
  // fullscreen transition resizes the window, and macOS can enter fullscreen
  // without going through ExclusiveAccessContext at all (the green button).
  void UpdateWindowCornerMask();

  // HTML5/element fullscreen (the video player's own button), as
  // opposed to macOS window fullscreen. Drives the immersive layout.
  bool immersive_fullscreen_ = false;

  // Enters/leaves immersive fullscreen: the native fullscreen transition plus
  // the edge-to-edge layout.
  void SetImmersiveFullscreen(bool immersive);

  // Resizes or collapses the sidebar pane, animating
  // from its current width to `width` (device-independent points; <= 0
  // means collapsed - see the retired ABI header's SIDEBAR WIDTH doc block for
  // the full contract). Clamps defensively into {0} u [kMinSidebarWidth,
  // kMaxSidebarWidth] regardless of what the Swift caller already clamped.
  // A no-op if the target already matches the pane's current width (e.g. a
  // redundant apply-on-registration call for a window that's already at the
  // right width).
  void SetSidebarWidth(double width);

  // Drag-resize perf: snaps the sidebar pane directly to `width`
  // - one relayout, no timer, no easing - for the seam grip's live drag,
  // where SetSidebarWidth's restart-per-tick 150ms animation perpetually
  // trailed the cursor. Clamps into [kMinSidebarWidth, kMaxSidebarWidth]
  // (no collapse form - a live drag only exists on an expanded pane).
  // Bounds-only: traffic lights, content insets, overlay docking and
  // last-docked bookkeeping stay with the gesture-end SetSidebarWidth
  // commit. Stops any in-flight resize animation so the two paths never
  // fight over preferred size. See the retired ABI header's SIDEBAR WIDTH LIVE
  // doc block.
  // What the width grip does on release. Records the width the reveal opens
  // at and writes the store, which the live setter deliberately does not
  //.
  void SetSidebarWidthCommit(double width);

  void SetSidebarWidthLive(double width);

  // Takes kOverviewSidebarMinWidth while the rail parks on the overview pane,
  // which is unreadable at an ordinary sidebar width. A SESSION overlay: the
  // user's width comes back on close and never reaches spaces.json.
  void SetSidebarOverviewOpen(bool open);

  int SidebarWidthForTesting() const;

  // Sidebar on right: re-docks the sidebar to the given window
  // edge - `on_right` false is "left", the stock default - and mirrors the
  // content card's gutter/traffic-light band, the hover-overlay reveal edge,
  // and the Cmd+S slide direction to match. Live-relayout, no relaunch
  // needed. A no-op on a window with no sidebar slot, same first line and
  // same reason as SetSidebarWidth. See the retired ABI header's
  // SIDEBAR POSITION doc block.
  void SetSidebarPosition(bool on_right);

  // puts settings.json's three window-appearance answers into force -
  // SetSidebarPosition, SetOpaqueSurround and SetGlassBackdropAlpha above,
  // which were all implemented with zero callers removed the
  // Swift broadcaster.
  //
  // Does BOTH halves that broadcaster did, and both are needed. It applies
  // `appearance` to every window open right now, which is what makes an edit
  // take effect without a relaunch; and it keeps the value as the current one,
  // which is what a window opened later seeds itself from in the constructor.
  // A field left nullopt is one the file did not answer - the window keeps
  // whatever it has rather than being moved to a default invented here.
  //
  // UI THREAD ONLY - it walks live windows and repaints them.
  static void SetWindowAppearance(const WindowAppearance& appearance);

  // For a surface that is not one of these windows but still wears
  // overlay.css (AtriumSettingsWindow) and needs the live values outside a
  // settings edit, e.g. on its own ready handshake.
  static const WindowAppearance& CurrentAppearance();

  // settings.json's archiveThreshold rung, the one the archive sweep reads.
  static std::optional<std::string> ArchiveThresholdFromSettings(
      std::string_view settings_json);

  // same cache-and-broadcast, for the notification timeout
  // step - the setting that PushNotificationTimeoutToSidebar reads.
  static void SetNotificationTimeoutStep(const std::string& step);

  // nullopt for an unparsable object or a missing field.
  static std::optional<std::string> NotificationTimeoutFromSettings(
      std::string_view settings_json);

  // same cache-and-broadcast, for the swipe presets. The dict
  // carries whichever of the eight settings.rs fields were present - a
  // partial preset (an older file, say) still updates the ones it has.
  static void SetSwipeTuning(base::DictValue tuning);

  // The eight swipe fields the file carries, or an empty dict if none of them
  // parsed - never nullopt, since a subset is still worth pushing.
  static base::DictValue SwipeTuningFromSettings(
      std::string_view settings_json);

  // mirrors SetWindowAppearance's cache-and-broadcast, for the
  // Settings > AI kill switch.
  static void SetAiEnabled(bool enabled);

  // nullopt for an unparsable object or a missing field.
  static std::optional<bool> AiEnabledFromSettings(
      std::string_view settings_json);

  // the live kill-switch state, for a native handler that must
  // refuse an AI-gated action even if the surface that should have hidden it
  // did not.
  static bool AiEnabled();

  // Kept so AutocompleteBridge::OnTemplateURLServiceChanged can finish
  // mirroring engines once the async-loading service catches up.
  static const std::string& LastSettingsJson();

  // A save has to refresh it: a catch-up mirror off a stale copy re-applies
  // the list the user just edited away.
  static void SetLastSettingsJson(std::string json);

  // Reply to the constructor's SettingsJsonCache read: fills the cache above
  // and mirrors this profile's engines.
  void OnSettingsJsonLoaded(const std::string& json);

  // mirrors SetAiEnabled's cache-and-broadcast, for the remap the
  // JS side's backtick toggle expects but nothing ever pushed.
  static void SetAiPanelHotkey(const std::string& key);

  // nullopt for an unparsable object or a missing field - the JS default
  // ('`') stays in effect when nothing is configured.
  static std::optional<std::string> AiPanelHotkeyFromSettings(
      std::string_view settings_json);

  // mirrors SetAiEnabled's cache-and-broadcast, for the Auto
  // summarize switch - only the panel's NEXT open reads the new value.
  static void SetAiPanelAutoSummarize(bool enabled);

  // nullopt for an unparsable object or a missing field.
  static std::optional<bool> AutoSummarizeFromSettings(
      std::string_view settings_json);

  // whether the window steals a key for vim mode. Cached in the
  // browser process because PreHandleKeyboardEvent runs per keystroke and
  // cannot afford to parse the settings JSON.
  static void SetVimModeEnabled(bool enabled);
  static bool VimModeEnabled();

  // nullopt for an unparsable object or a missing field.
  static std::optional<bool> VimModeEnabledFromSettings(
      std::string_view settings_json);

  // the language Translate and summarize answers in, as a BCP-47
  // tag. Empty means the panel follows its own locale.
  static void SetAiPanelTranslateLanguage(std::string tag);

  static std::optional<std::string> TranslateLanguageFromSettings(
      std::string_view settings_json);

  // One tick of the backdrop-tint crossfade (SetBackdropTint
  // animates space switches instead of snapping).
  void StepBackdropTintAnimation();

  // Theme Studio (set_backdrop_theme op): swaps the flat backdrop_tint_ wash
  // for a rasterized gradient bitmap (`rgba` is a `w`x`h` straight-alpha sRGB
  // buffer, row-major; the pointer is only valid for this call, so it is
  // copied into theme_bitmap_). `grain` (0..1) drives a film-grain overlay.
  // `transition_ms` <= 0 (or the first theme) swaps immediately; otherwise the
  // old bitmap crossfades to the new one over that duration. `rgba == nullptr`
  // clears the theme and reverts to the last backdrop_tint_ color.
  void SetBackdropTheme(const uint8_t* rgba,
                        int32_t w,
                        int32_t h,
                        float grain,
                        int32_t transition_ms);

  // Test-only: whether a Theme Studio backdrop is installed.
  bool has_theme_for_testing() const { return has_theme_; }
  // Test-only: the staged scrub's weight, -1 with no scrub in flight.
  double scrub_blend_weight_for_testing() const { return scrub_blend_weight_; }
  // Test-only: the grain the next paint overlays, scrub blend included.
  float painted_grain_for_testing() const { return PaintedGrain(); }
  // Test-only: the flat wash the painters read, alpha included.
  SkColor backdrop_tint_for_testing() const { return backdrop_tint_; }
  // Test-only: the current (post-crossfade) theme bitmap.
  SkBitmap theme_bitmap_for_testing() const { return theme_bitmap_; }
  // Test-only: what the root's INSTALLED background actually paints. A theme
  // push updates the bitmap above even when its painter is off the root.
  SkColor root_background_pixel_for_testing();
  // Test-only: the same paint, read just outside the page card's left edge,
  // where the glow lands.
  SkColor root_glow_pixel_for_testing();
  // Test-only: null until the floating panel's frost is first framed.
  AtriumSidebarFrostLayer* sidebar_frost_layer_for_testing() {
    return sidebar_frost_layer_.get();
  }

  // Runs on every OnWidgetVisibilityChanged - BrowserController's theme
  // animator pauses while this window is hidden.
  void SetThemeVisibilityCallback(base::RepeatingCallback<void(bool)> callback);

  // One tick of the theme crossfade - mirrors StepBackdropTintAnimation.
  void StepThemeAnimation();

  // Paints the active theme backdrop (prev+current bitmaps at the crossfade
  // alpha, then the grain overlay). Called from AtriumThemeBackground::Paint,
  // the custom background SetBackdropTheme installs on root_view_.
  // `tint_alpha_floor` lifts the flat wash under the picture, for the floating
  // panel whose frost needs some tint on it to stay legible.
  void PaintThemeBackdrop(gfx::Canvas* canvas, SkAlpha tint_alpha_floor = 0);
  // theme_grain_ blended toward scrub_grain_ while a scrub is in flight.
  float PaintedGrain() const;
  void PaintAmbilightBackdrop(gfx::Canvas* canvas);
  // The page card in root coordinates: the pane container minus its margins.
  gfx::Rect PageCardBoundsInRoot() const;
  // edge_tint_ thrown on the chrome around the card. Both root painters call
  // it, so a page's colour shows on a themed space as it does on a flat one.
  void PaintPageGlow(gfx::Canvas* canvas);
  // The same glow for a painter that runs in a descendant of the root.
  void PaintPageGlowIn(gfx::Canvas* canvas, views::View* view);
  AtriumPaneContainerView::GlowPainter RootGlowPainter();
  // The card the glow hugs is the page card scaled by this about its centre:
  // the step back under a peek, 1 otherwise. See SetPeekScrimVisible.
  void SetPeekGlowScale(float scale);
  float peek_glow_scale_ = 1.0f;
  base::OneShotTimer peek_glow_restore_timer_;

  // A uniform tint goes down the existing flat path, which is every colour
  // source that exists before a readback does. Only a tint whose edges differ
  // needs the glow painter, so nothing switches painters until one arrives.
  void SetEdgeTint(const atrium::EdgeTint& tint);
  void StepEdgeTintAnimation();

  // the native-app surround crossfading in or out over whatever
  // sat under it. Startup snaps, because there is nothing on screen to fade
  // from.
  void PaintSurroundFade(gfx::Canvas* canvas);
  void StepSurroundFade();

  // The ambilight sampler. Start is idempotent and declines while the page
  // tint setting is off, so every caller can just say "start" without first
  // asking whether it should.
  void StartAmbilightSampling();
  void StopAmbilightSampling();

  // SetBackdropScrubBlendEnd(commit=1)'s promotion step -
  // installs the staged scrub layer (scrub_bitmap_, or the live flat tint if
  // none was staged) as the real backdrop atomically. No crossfade: the live
  // blend already carried the visual transition.
  void PromoteScrubThemeToBackdrop();
  // the theme session a scrub opens over a flat outgoing space.
  void OpenScrubThemeSession();

  // One tick of the commit-time scrub-blend ease - mirrors
  // StepBackdropTintAnimation/StepThemeAnimation. Eases scrub_blend_weight_
  // from wherever the gesture released it up to 1.0, then calls
  // PromoteScrubThemeToBackdrop() and clears the staged state exactly as
  // SetBackdropScrubBlendEnd's immediate path does.
  void StepScrubBlendSettleAnimation();

  // sidebar-polish hover-overlay resize: live-resizes the hover-reveal
  // overlay widget and records `width` as the last-docked width (the width
  // the next reveal AND the next dock/expand open at). Never touches the
  // collapsed pane itself - see the retired ABI header's SIDEBAR OVERLAY WIDTH
  // doc block for why this is a separate op from SetSidebarWidth. Clamps
  // defensively into [kMinSidebarWidth, kMaxSidebarWidth] (no collapse
  // semantic - the overlay has no width-0 form).
  void SetSidebarOverlayWidth(double width);

  // the overlay's live drag-tick channel - resizes the overlay
  // widget exactly as SetSidebarOverlayWidth does but WITHOUT recording
  // last_docked_sidebar_width_, mirroring the docked split where
  // SetSidebarWidthLive leaves that bookkeeping to the gesture-end
  // SetSidebarWidth commit. An overlay drag abandoned rather than committed
  // (the overlay auto-hides mid-drag) must leave the last-docked width at
  // its pre-drag value, or C++ reopens at the dragged width while Swift's
  // drag base is still the older one. See the retired ABI header's SIDEBAR
  // OVERLAY WIDTH LIVE doc block.
  void SetSidebarOverlayWidthLive(double width);

  // the same snapshot, to the sidebar's extension strip chips.
  // No-op before sidebar_contents_ exists.
  // `profile_id` is the pushing controller's own profile: the sidebar keeps a
  // strip per profile so a rail pane of another one can draw its pins.
  void SetSidebarExtensionActionsJSON(const std::string& json,
                                      const std::string& profile_id);

  // The snapshot SetSidebarExtensionActionsJSON last sent, for a test that
  // has to tell WHICH controller's actions reached this window after a
  // cross-profile swap.
  const std::string& SidebarExtensionActionsJSONForTesting() const;

  // The snapshot this window still holds for one profile. Empty for a profile
  // that has never pushed.
  const std::string& SidebarExtensionActionsJSONForProfileForTesting(
      const std::string& profile_id) const;

  // The controller whose extension_bridge_ produced the JSON now on this
  // window's strip - a store extension's id repeats across profiles, so a
  // pin toggle needs to know which one it actually came from.
  void SetLastExtensionActionsController(BrowserController* controller);
  BrowserController* LastExtensionActionsController() const;

  // The floating address bar's own copy of the strip, from the same snapshot.
  void SetDevBarExtensionActionsJSON(const std::string& json);

  // Routes a pin toggle to the controller that owns the tab on screen, the
  // same displayed-browser rule OnSurface_PillActionClicked follows.
  void ToggleExtensionPin(const std::string& extension_id);

  // Opens the quick menu under `anchor_in_window`, or closes it when it is
  // already up for that same button.
  void ToggleQuickMenu(const gfx::Rect& anchor_in_window);
  bool IsQuickMenuShowing() const;
  content::WebContents* QuickMenuContentsForTesting() const;
  void SetQuickMenuActionsJSON(const std::string& json);

  // {"host": ..., "secure": ..., "shareable": ...} for the tab on screen.
  std::string CurrentQuickMenuSiteJSON();

  // Opens the space icon picker next to `anchor_in_window` for `space_index`,
  // starting on `value`'s tab (icon vs emoji).
  void ShowSpaceIconPicker(int space_index, const std::string& value,
                           const gfx::Rect& anchor_in_window);
  bool IsSpaceIconPickerShowing() const;
  content::WebContents* SpaceIconPickerContentsForTesting() const;
  // Forwards a synthetic Escape to the picker widget's own
  // HandleKeyboardEvent - the same direct-call pattern this window's own
  // MirrorEndsOnEscape test uses for its accelerator path.
  bool SendSpaceIconPickerEscapeForTesting();

  // Chromium's own page info bubble, anchored on `anchor_in_window`.
  void ShowPageInfoForQuickMenu(const gfx::Rect& anchor_in_window);
  void OpenExtensionsPage();

  // The Chrome Web Store's extensions category, in a new tab.
  void OpenExtensionStorePage();
  void CopyCurrentPageUrl();

  // The click path the strip chips and the menu tiles share.
  void ClickExtensionActionFromSurface(const std::string& extension_id);

  // Chromium's own extension context menu, anchored at `screen_rect`. The
  // strip chips, the floating bar chips and the quick menu tiles all land here.
  void ShowExtensionContextMenuFromSurface(const std::string& extension_id,
                                           const gfx::Rect& screen_rect);
  bool IsExtensionContextMenuShowingForTesting() const;
  bool ExtensionContextMenuHasSidePanelEntryForTesting() const;
  // Runs the menu's own command, the same path a click on the item takes.
  void ExecuteExtensionContextMenuSidePanelEntryForTesting();
  std::u16string ExtensionContextMenuPinLabelForTesting() const;
  // RunMenuAt does not block here, so a test polls
  // IsExtensionContextMenuShowingForTesting to see the close land.
  void CloseExtensionContextMenuForTesting();

  // Where the clicked icon is, in screen coordinates. An empty rect goes back
  // to the surface-wide fallback below, which is what a caller that cannot
  // report an icon gets.
  void SetExtensionPopupAnchorRect(const gfx::Rect& screen_rect);

  // The probe placed by SetExtensionPopupAnchorRect while it holds a rect.
  // Otherwise the surface view that owns the button the popup was opened
  // from: the floating bar's container while it is up, else the sidebar slot.
  views::View* ExtensionPopupAnchorView();

  // ExtensionPopupAnchorView as a views::BubbleAnchor, unanchored during
  // teardown when there is no root view left to point at.
  views::BubbleAnchor GetExtensionPopupAnchor();

  // Peek mode: builds this
  // window's peek child widget - called once, at BrowserController
  // construction, alongside AttachSidebar/AttachCommandBar. A no-op if
  // already built (never rebuilt - see PeekWidget's class doc). Unlike
  // AttachCommandBar, there is no NSView to hand over and no availability
  // gate on the widget's existence itself - browser.cc's shift+click
  // intercept is what's gated (UIBridge::peek_connected(), see the retired
  // ABI loader), not whether this window owns a widget.
  void EnsurePeekWidget();

  // Built lazily, on the first sidebar hint that needs to draw above the
  // content card - same deferred-build reason as EnsureCommandBarWidget.
  void EnsureHintOverlayWidget();
  void EnsureToastOverlayWidget();
  void EnsureHoverUrlWidget();

  // builds the palette's widget on the first open. Deferred rather
  // than built with the window because the WebContents it hosts needs the
  // reserved chrome context, which lands ~670ms after startup.
  void EnsureCommandBarWidget();

  // same deferred-build reason as EnsureCommandBarWidget.
  void EnsureOverviewController();

  // same deferred-build reason as EnsureCommandBarWidget.
  void EnsureCourtyardWidget();

  // The peek card's control cluster, or null before EnsurePeekWidget ran.
  PeekControlsWidget* PeekControlsForTesting();
  int PeekShowGenerationForTesting() const;
  bool PeekPromotingForTesting() const;

  // Null until the first sidebar hint that needed to draw above the card.
  HintOverlayWidget* HintOverlayWidgetForTesting() const {
    return hint_overlay_widget_.get();
  }

  // Null until the first hover target, built by EnsureHoverUrlWidget via
  // SetStatusBubbleURL.
  ToastOverlayWidget* HoverUrlWidgetForTesting() const {
    return hover_url_widget_.get();
  }

  // The other corner, so a test that asserts what the hover corner declines can
  // assert beside it what this one still takes.
  ToastOverlayWidget* ConfirmationWidgetForTesting() const {
    return toast_overlay_widget_.get();
  }

  // The confirmation a copy raises. Going through CopyCurrentPageLink needs a
  // page in the pane, and a browser test with no space fixture has none.
  void ShowTransientConfirmationForTesting(const std::u16string& text) {
    ShowTransientConfirmation(text);
  }

  // Null except in a little window (BuildHtmlFoyer no-ops without
  // little_chrome_container()).
  content::WebContents* FoyerContentsForTesting() const {
    return foyer_contents_.get();
  }

  // Shows the peek widget bound to `contents`, with `dismiss_callback`
  // wired as the widget's own-detected-dismiss path (Escape / the
  // focus-bounce guard - see PeekWidget::SetDismissCallback). A no-op if
  // EnsurePeekWidget was never called. `click_origin_screen`
  // forwards to PeekWidget::Show - see its doc.
  // False when this window has no peek widget, so a caller that already gave
  // up ownership of `contents` can take it back instead of stranding it.
  // `promote_callback` is the controls cluster's popout button.
  bool OpenPeek(content::WebContents* contents,
                base::RepeatingClosure dismiss_callback,
                base::RepeatingClosure promote_callback,
                std::optional<gfx::Point> click_origin_screen);

  // The card's origin chip - see PeekRootView::SetOriginChip. Every
  // controller in a slot shares this window, so each peek method below that
  // takes a `peek` does nothing unless the card holds it.
  void SetPeekOriginChip(const content::WebContents* peek,
                         const std::u16string& host,
                         bool secure);
  std::u16string PeekOriginChipHostForTesting() const;
  content::WebContents* PeekedWebContentsForTesting() const;
  bool PeekLoadingPlaceholderVisibleForTesting() const;

  // Delivers a mouse press to the active pane's scrim, the same event a
  // click on the dimmed page outside the card would produce.
  void SimulatePeekScrimPressForTesting();

  // Peek mode: the web-content pane's
  // current bounds in screen coordinates - the sidebar strip is NOT
  // included (this is web_view_'s own laid-out bounds, which BoxLayout
  // already places to the right of sidebar_slot()). PeekWidget sizes its
  // near-full card against this, inset by a small margin, so the card never
  // covers the sidebar and tracks sidebar collapse/expand for free on the
  // next query. Unlike the parent WINDOW's bounds (which WidgetObserver
  // reports via OnWidgetBoundsChanged), a sidebar pane resize is an
  // internal layout change with no bounds-changed notification of its own -
  // see StepSidebarResizeAnimation's PeekWidget::ReapplyBoundsForContentChange
  // call for how a live peek stays in sync with it anyway.
  gfx::Rect GetWebContentBoundsInScreen() const;

  // Where each overlay child window sits, bottom to top. Nothing used to order
  // them, so the last one to show won: a hover card raised itself over the
  // panel the user had just summoned.
  enum class OverlayLayer {
    kHoverUrlCard,
    kSidebarPanel,
    kToastStack,
    kCaptureOverlay,
    kHintOverlay,
  };

  // Call after showing `child`. Places it among the siblings that have a
  // layer, and leaves any other child window where it is.
  void OrderOverlayChild(views::Widget* child, OverlayLayer layer);

  // Where `child` sits in the app's live z-order, or -1 when it is not in it.
  // Higher draws above.
  int OverlayChildOrderForTesting(views::Widget* child) const;

  // Gives a borderless window a real titlebar with traffic lights. Returns
  // the titlebar height in DIPs, which the caller must reserve above its
  // content.
  static int AddTrafficLights(views::Widget* window);
  static bool HasTrafficLightsForTesting(views::Widget* widget);
  static void ClickCloseLightForTesting(views::Widget* widget);

  // Hides the peek widget. Does not touch `contents` - see
  // PeekWidget::Close()'s doc; the caller (BrowserController::ClosePeek)
  // destroys it separately. A no-op if there is no widget or it isn't
  // showing. `animate` and `on_hidden` forward to PeekWidget::Close() - see
  // its doc (true for every dismiss trigger, false for PromotePeek's
  // instant close). Every controller in a slot shares this window, so it
  // closes only when the card shows `peek`. `on_hidden` runs either way.
  void ClosePeek(const content::WebContents* peek,
                 bool animate = true,
                 base::OnceClosure on_hidden = base::OnceClosure());

  // `rev` rises with each new icon so the page fetches it again. Ignored
  // unless the card shows `peek`, and cleared when the card lets it go.
  void SetSidebarPeekBadge(const content::WebContents* peek,
                           uint64_t tab_id,
                           uint64_t icon_id,
                           uint64_t rev);
  uint64_t PeekBadgeTabIdForTesting() const { return peek_badge_tab_id_; }

  // Grows the peek card out to the pane with a spring, then runs `on_grown`
  // - see PeekWidget::Promote. Runs it at once when the card is hidden, and
  // never unless the card holds `peek`.
  void PromotePeek(const content::WebContents* peek,
                   base::OnceClosure on_grown);
  bool ShowsPeek(const content::WebContents* peek) const;

  bool IsPeekShowing() const;

  // Hides the card and unbinds `peek` without closing it: its owner keeps the
  // page, scroll and all, for RestorePeek to put back.
  void ParkPeek(const content::WebContents* peek);
  // Binds a parked `peek` and unhides the card as it was, with no open spring.
  // False when this window has no card.
  bool RestorePeek(content::WebContents* peek,
                   base::RepeatingClosure dismiss_callback,
                   base::RepeatingClosure promote_callback);

  // Peek mode (promote): unbinds the current WebContents from the
  // peek widget's WebView (PeekWidget::SetWebContents(nullptr)) without
  // hiding or destroying anything - the promote op's first step (Global
  // Constraints: "detach WebView, then InsertWebContentsAt") before
  // BrowserController moves ownership into the tab strip. A no-op unless the
  // card shows `peek`.
  void DetachPeekWebContents(const content::WebContents* peek);

  // Crash: releases `contents` from the peek widget if the peek
  // card is currently holding it, so a pane can claim it exclusively.
  //
  // A WebContents can only be hosted by ONE views::WebView at a time:
  // WebContentsViewMac::ViewsHostableAttach overwrites views_host_ with no
  // guard, while ViewsHostableDetach DCHECKs it non-null.
  //
  // A peek-promote overlap was blamed for that abort and then DISPROVED:
  // PromotePeek already calls DetachPeekWebContents()
  // before InsertWebContentsAt, and peek_contents_ is a WebContents of its
  // own rather than an existing tab's, so two holders are never live on that
  // path. This is an unproven invariant kept as defence in depth, NOT a known
  // fix - the abort was root-caused to the display-routing flip instead
  // (atrium_browser_controller.cc's PresenterController handoff).
  //
  // Called before every pane attach in ShowTab, so the peek releases first
  // and the two holders are never live at once.
  void ReleasePeekClaim(content::WebContents* contents);

  // Pane-claim ledger. Records which holder claims each tab
  // WebContents and logs the moment two holders overlap, since the DCHECK that
  // actually aborts fires later, in the OTHER holder, and never names the code
  // that created the double claim. Call immediately BEFORE each
  // views::WebView::SetWebContents, passing the view's next contents.
  //
  // Kept permanently, with a
  // destruction hook - see ClaimLedger's comment in the .mm for the argument
  // and for the condition under which it may finally be retired. The short
  // version: the wrong-pane bug class is SILENT, so an abort-time tripwire
  // cannot see it and a claim-time record is the only signal there is.
  static void TraceClaim(views::WebView* web_view,
                         content::WebContents* next,
                         const std::string& holder);
  std::string ClaimTag(const char* what) const;

  // Shared attach steps InsertPaneFromDrop uses for every pane it fills
  // (new or swapped-in-place): claim, visibility, empty-card hide.
  void AttachDropContentsToPane(AtriumPaneView* pane,
                                content::WebContents* contents);

  // whether this window still has a live
  // NativeWidgetMacNSWindowHost - i.e. whether Show() can safely run, since
  // it reaches SetExcludedFromWindowCycling's CHECK(host) as its very first
  // statement.
  //
  // Needed because a closed window's NSWindow is destroyed IMMEDIATELY while
  // its controller's teardown (which clears the slot's presenter entry, see
  // BrowserController's OnTeardown) runs later. In that interval the
  // presentation table still names a controller whose window is already
  // unrealized, and anything that trusts a presenter lookup to imply a
  // showable window will abort. Callers must consult this before Show()ing a
  // window they resolved indirectly rather than just created.
  bool HasRealizedNativeWindow() const;

  // Hides the peek widget's loading placeholder - see
  // PeekWidget::HideLoadingPlaceholder's doc.
  void HidePeekLoadingPlaceholder(const content::WebContents* peek);

  // Peek pop-out polish: shows/hides the scrim dimming this
  // window's active pane while a peek card is open over it - see
  // AtriumPaneView::scrim_view()'s doc. Routed through ActivePane() (same
  // as GetWebContentBoundsInScreen above) so the dimmed pane always matches
  // the one the peek is actually sized against. A no-op during the same
  // teardown window GetWebContentBoundsInScreen's doc describes (root_view_
  // already null).
  void SetPeekScrimVisible(bool visible);

  // opens the region-select capture overlay over this window's
  // content pane (see AtriumCaptureOverlayWidget's class doc). `callback`
  // runs at most once, on a successful drag, with the selected region in
  // content DIP coordinates - ready to pass straight to
  // atrium::CaptureRegion (atrium_capture.h). Re-entry (calling
  // this again while an overlay is already up) closes the existing one
  // first - one overlay at a time, dropping its callback unrun.
  void BeginRegionCapture(base::OnceCallback<void(const gfx::Rect&)> callback);

  // AtriumCaptureOverlayWidget's deferred-teardown callback
  // (see its OnCommit/OnCancel comments) - resets capture_overlay_widget_
  // ONLY if it still points at `which` (a re-entrant BeginRegionCapture call
  // in the meantime may already have replaced it with a newer instance,
  // whose own posted teardown must not be pre-empted by a stale one).
  // Public because the overlay widget (a separate class) invokes it from
  // its posted teardown.
  void FinishRegionCaptureIfCurrent(
      base::WeakPtr<AtriumCaptureOverlayWidget> which);

  // runs one of the palette's three capture verbs against the active
  // tab - captureViewport, captureRegion, captureFullPage. Returns false when
  // `name` is not one of them, so the caller can carry on matching.
  //
  // The capture core (atrium_capture.h) and the region overlay shipped without
  // a caller: the command bar that drove them was Swift, and the HTML palette
  // that replaced it never got the verbs. This is that caller.
  bool MaybeRunCaptureVerb(std::string_view name);

  // Whether the sidebar pane is currently collapsed
  // (width 0). The hover strip only reveals the overlay while this is true.
  // Whether this window has a sidebar slot at all. IsSidebarCollapsed()
  // cannot answer this - it reports "not collapsed" for a window with no
  // sidebar, which is right for the inset and traffic-light logic and wrong
  // for deciding whether a toggle has anything to act on.
  bool HasSidebar() const;
  bool IsSidebarCollapsed() const;

  // The traffic lights hide with a left-docked collapsed sidebar. Not in full
  // screen: there they live on the menu bar reveal, and a hidden set leaves
  // no way out by mouse.
  bool HidesWindowControlsWhen(bool collapsed) const;
  bool WindowControlsVisibleForTesting() const;

  // Settles the traffic lights on the rule as it stands now, abandoning any
  // travel in flight. For the paths that change the answer without owning the
  // pane's animation: full screen, the dock side, a drag that stops the ease.
  void ReapplyWindowControlsVisibility();

  // Which edge the pane is docked to, for callers outside AtriumRootView -
  // the edge-reveal zone has to sit on the same side the pane will appear on.
  bool IsSidebarOnRight() const;

  // The pointer-at-the-edge reveal, or null before the first width lands.
  // Cmd+S asks it whether the pane standing open is a reveal of its own.
  AtriumSidebarEdgeReveal* sidebar_edge_reveal() {
    return sidebar_edge_reveal_.get();
  }

  // The last non-zero width the pane docked at (Cmd+S
  // hide/collapse preserves it) - the width the overlay reveals at. Defaults
  // to kDefaultSidebarWidth before the first real width arrives.
  // read from the core, which already persists it. A cached copy
  // here drifted from the store and from the page three separate times.
  int LastDockedSidebarWidth() const;

  // Returns the live in-process NSWindow backing this widget, for direct
  // alpha animation during a window-swap cross-fade. CHECKs non-null: Atrium
  // windows are always in-process, never app-shim-hosted (see
  // AtriumNativeWidgetMac::OnWindowInitialized).
  NSWindow* GetNSWindowForAnimation();

  // Excludes (or re-includes) this window from the macOS Window menu, Cmd-~
  // cycling, and Mission Control / App Exposé. Show()/ShowInactive() call this
  // with `false` and Hide() calls it with `true`, so the invariant holds
  // automatically for
  // every path that changes visibility; callers only need it directly for a
  // window that is constructed hidden and never goes through Hide() first
  // (see BrowserController's show=false hidden-window-pool members).
  void SetExcludedFromWindowCycling(bool excluded);
  bool IsExcludedFromWindowCycling() const {
    return excluded_from_window_cycling_;
  }

  // views::WidgetObserver:
  //
  // M2c Rev3 (bug 3): the frozen/live snapshot swap must follow window
  // FOCUS, not only clicks inside the web content or a sidebar row -
  // switching the key window via a sidebar click elsewhere in macOS UI, or
  // Cmd+Tab, previously left the wrong window showing live content. Widget
  // activation (backed by the NSWindow's key/main state on macOS, via
  // NativeWidgetMac) is the Chromium-side hook for "this window became the
  // active one" - forwards to BrowserController::NotifyWindowActivated,
  // which resolves this window's controller/slot and tells Swift
  // (UIBridge::OnWindowActivatedInSlot). Swift's SpacesViewModel.
  // windowDidActivate makes the actual takeover decision (has this slot's
  // selected tab moved elsewhere?) and, if so, drives the takeover through
  // the SAME activate_tab op a sidebar-row click uses - no new C++ move/
  // freeze logic, this only supplies the trigger. Only `active == true` is
  // forwarded: a window resigning key has nothing to take over.
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // Re-anchors the child overlay widgets (status bubble, loading bar) when
  // the window moves or resizes. The animation ticks re-apply bounds while
  // they run, but a bubble in its steady kVisible phase has no tick - and
  // tiling window managers resize windows without any mouse travel that
  // would otherwise clear the hover first.
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // Forwards every visibility flip to whoever set
  // SetThemeVisibilityCallback - the theme animator pauses while this
  // window is off screen.
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  // views::ViewObserver, on root_view_: any layout pass that could move the
  // devbar (window resize, sidebar width, a split change) re-derives its
  // shadow rect the same way ToastOverlayWidget re-derives its own bounds.
  void OnViewLayoutInvalidated(views::View* observed_view) override;

  // Re-asserts VISIBLE on the active tab's WebContents (no-op with no tab).
  // A tab attached while this window was hidden never gets its first real
  // WasShown() - OnActiveTabChanged's HIDDEN is dropped by WebContentsImpl's
  // "trust the initial visibility" guard, and nothing revisits it - so every
  // path that puts this window on screen must call this once the window is
  // genuinely visible. Show() does it for the not-yet-visible case; window
  // swaps (BrowserController::ActivateWindow) go through ShowInactive()
  // first, which makes Show() early-return, so they must call this directly.
  void AssertActiveTabVisible();

  // BrowserWindow:
  bool IsOnCurrentWorkspace() const override;
  bool IsVisibleOnScreen() const override;
  void SetTopControlsShownRatio(content::WebContents* web_contents,
                                float ratio) override;
  bool DoBrowserControlsShrinkRendererSize(
      const content::WebContents* contents) const override;
  ui::NativeTheme* GetNativeTheme() override;
  const ui::ThemeProvider* GetThemeProvider() const override;
  const ui::ColorProvider* GetColorProvider() const override;
  int GetTopControlsHeight() const override;
  void SetTopControlsGestureScrollInProgress(bool in_progress) override;
  std::vector<StatusBubble*> GetStatusBubbles() override;
  void UpdateTitleBar() override;
  void UpdateLoadingAnimations(bool is_visible) override;
  void SetStarredState(bool is_starred) override;
  bool IsTabModalPopupDeprecated() const override;
  void SetIsTabModalPopupDeprecated(
      bool is_tab_modal_popup_deprecated) override;
  void OnActiveTabChanged(content::WebContents* old_contents,
                          content::WebContents* new_contents,
                          int index,
                          int reason) override;
  void OnTabDetached(content::WebContents* contents, bool was_active) override;
  gfx::Size GetContentsSize() const override;
  void SetContentsSize(const gfx::Size& size) override;
  void UpdatePageActionIcon(PageActionIconType type) override;
  autofill::AutofillBubbleHandler* GetAutofillBubbleHandler() override;
  // The anchor AtriumAutofillBubbleHandler resolves for every bubble it
  // shows: the active content pane, or an unanchored views::BubbleAnchor()
  // if there is none (see ActivePane()'s doc for when that happens). Public
  // so it can be bound into the handler's AnchorProvider callback without
  // making ActivePane() itself public.
  views::BubbleAnchor GetAutofillBubbleAnchor() const;
  void ExecutePageActionIconForTesting(PageActionIconType type) override;
  LocationBar* GetLocationBar() const override;
  void SetFocusToLocationBar(bool is_user_initiated) override;
  void UpdateReloadStopState(bool is_loading, bool force) override;
  void UpdateToolbar(content::WebContents* contents) override;
  bool UpdateToolbarSecurityState() override;
  void UpdateCustomTabBarVisibility(bool visible, bool animate) override;
  void ResetToolbarTabState(content::WebContents* contents) override;
  void FocusToolbar() override;
  void ToolbarSizeChanged(bool is_animating) override;
  void TabDraggingStatusChanged(bool is_dragging) override;
  void LinkOpeningFromGesture(WindowOpenDisposition disposition) override;
  void FocusAppMenu() override;
  // FocusWebContentsPane is no longer part of the BrowserWindow interface in
  // 151 (moved to BrowserFocusController); kept as a plain method since
  // AtriumBrowserController calls it directly on the window.
  void FocusWebContentsPane();
  bool IsTabStripEditable() const override;
  void DisableTabStripEditingForTesting() override;
  bool IsToolbarVisible() const override;
  bool IsToolbarShowing() const override;
  bool IsLocationBarVisible() const override;
  void ShowUpdateChromeDialog() override;
  void ShowIntentPickerBubble(
      std::vector<apps::IntentPickerAppInfo> app_info,
      bool show_stay_in_chrome,
      bool show_remember_selection,
      apps::IntentPickerBubbleType bubble_type,
      const std::optional<url::Origin>& initiating_origin,
      IntentPickerResponse callback) override;
  void ShowBookmarkBubble(const GURL& url, bool already_bookmarked) override;
#if BUILDFLAG(IS_CHROMEOS)
  void ToggleMultitaskMenu() override;
#endif  // BUILDFLAG(IS_CHROMEOS)
  ShowTranslateBubbleResult ShowTranslateBubble(
      content::WebContents* contents,
      translate::TranslateStep step,
      const std::string& source_language,
      const std::string& target_language,
      translate::TranslateErrors error_type,
      bool is_user_gesture) override;
  // StartPartialTranslate moved off BrowserWindow onto TranslateController in
  // 151 (registered unconditionally per-Browser); no Atrium override needed.
  DownloadBubbleUIController* GetDownloadBubbleUIController() override;
  void ConfirmBrowserCloseWithPendingDownloads(
      int download_count,
      Browser::DownloadCloseType dialog_type,
      base::OnceCallback<void(bool)> callback) override;
  void ShowAppMenu() override;
  void PreHandleDragUpdate(const content::DropData& drop_data,
                           const gfx::PointF& point) override;
  void PreHandleDragExit() override;
  void HandleDragEnded() override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(const input::NativeWebKeyboardEvent& event) override;
  std::unique_ptr<FindBar> CreateFindBar() override;
  web_modal::WebContentsModalDialogHost* GetWebContentsModalDialogHost()
      override;
  web_modal::WebContentsModalDialogHost* GetWebContentsModalDialogHostFor(
      content::WebContents* web_contents) override;
  void ShowAvatarBubbleFromAvatarButton(bool is_source_accelerator) override;
  void MaybeShowProfileSwitchIPH() override;
  void MaybeShowSupervisedUserProfileSignInIPH() override;
  void ShowHatsDialog(
      const std::string& site_id,
      const std::optional<std::string>& hats_histogram_name,
      const std::optional<uint64_t> hats_survey_ukm_id,
      base::OnceClosure success_callback,
      base::OnceClosure failure_callback,
      const SurveyBitsData& product_specific_bits_data,
      const SurveyStringData& product_specific_string_data) override;
  ExclusiveAccessContext* GetExclusiveAccessContext() override;
  std::string GetWorkspace() const override;
  bool IsVisibleOnAllWorkspaces() const override;
  void ShowEmojiPanel() override;
  std::unique_ptr<content::EyeDropper> OpenEyeDropper(
      content::RenderFrameHost* frame,
      content::EyeDropperListener* listener) override;
  void ShowCaretBrowsingDialog() override;
  void CreateTabSearchBubble() override;
  void CloseTabSearchBubble() override;
  void ShowIncognitoClearBrowsingDataDialog() override;
  void ShowIncognitoHistoryDisclaimerDialog() override;
  bool IsUnframedModeEnabled() const override;
  bool GetCanResize() override;
  ui::mojom::WindowShowState GetWindowShowState() const override;
  void ShowChromeLabs() override;
  BrowserView* AsBrowserView() override;

  // ui::BaseWindow:
  void Show() override;
  // the body of Show, run once the startup hold lets go.
  void ShowNow(std::string_view reason);
  void ShowInactive() override;
  void Hide() override;
  bool IsVisible() const override;
  void SetBounds(const gfx::Rect& bounds) override;
  void Close() override;
  void Activate() override;
  void Deactivate() override;
  bool IsActive() const override;
  gfx::NativeWindow GetNativeWindow() const override;
  gfx::Rect GetBounds() const override;
  bool IsMaximized() const override;
  bool IsMinimized() const override;
  bool IsFullscreen() const override;
  gfx::Rect GetRestoredBounds() const override;
  ui::mojom::WindowShowState GetRestoredState() const override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  void FlashFrame(bool flash) override;
  ui::ZOrderLevel GetZOrderLevel() const override;
  void SetZOrderLevel(ui::ZOrderLevel order) override;

  // Cmd+Shift+W. Marks the upcoming close as user-initiated so
  // OnWindowCloseRequested's empty-strip suppression yields to it, then
  // closes exactly like Close().
  void CloseUserInitiated();

  // What windowShouldClose: should answer, and any close it has to start.
  // AtriumClientView owns no state of its own - this window does.
  views::CloseRequestResult OnAppKitCloseRequested();

 protected:
  // BrowserWindow:
  void DeleteBrowserWindow() override;

 private:
  friend class AtriumDevBar;
  class StubLocationBar;
  class ModalDialogHost;
  class ExclusiveAccessContextImpl;
  // WebContentsObserver on this window's ACTIVE tab.
  // Retargeted by RetargetLoadProgress and OnActiveTabChanged on every
  // active-tab / active-pane change.
  class ActiveTabObserver;

  // logs the sidebar's first painted frame. The startup hold cannot
  // wait for it, see the class in the .cc.
  class SidebarPaintWatch;

  // Creates and returns the native widget. Has a macOS implementation in
  // atrium_browser_window.cc (this file is macOS-only, unlike
  // WebUIBrowserWindow which splits platform code into a _mac.mm file).
  views::NativeWidget* CreateNativeWidget();

  // The controller of the tab the user can SEE. After a cross-profile
  // in-window swap this window's own describes its hidden tab instead.
  BrowserController* DisplayedController() const;

  // `appearance` applied to THIS window. The per-window half of
  // SetWindowAppearance; also run once at the end of the constructor, so a
  // window opened after an edit does not paint the old look for a frame.
  void ApplyWindowAppearance(const WindowAppearance& appearance);

  // The surround colour to use when settings.json holds no explicit one, which
  // is the default. docs/opaque-surround-design.md section 4 settled on an
  // appearance-adaptive neutral - white in light, near-black in dark - and its
  // section 9 records that no such constant was found on the Swift side.
  // Chromium's own window background is that neutral and already tracks the
  // system appearance, so it is read rather than a pair of literals invented
  // here.
  uint32_t AutomaticSurroundColor() const;

  // The pane currently authoritative for dialog anchoring and damage-heal:
  // root_view_->pane_container()->active_pane() - the single pane outside a
  // split, or whichever pane SetActivePaneIndex last named while split. See
  // AtriumPaneContainerView::active_pane()'s doc.
  AtriumPaneView* ActivePane() const;
  // The page in the active pane, null while the pane shows the empty card.
  // The tint keys off this, not the strip's active tab: a space with nothing
  // to open empties the pane and leaves the strip where it was.
  content::WebContents* DisplayedContents() const;

  // UpdateDockedDevtools's per-pane worker - re-implements
  // DevtoolsUIController::DevtoolsWebViewController::UpdateDevtools
  // (devtools_ui_controller.cc) for a single AtriumPaneView. No-op if
  // `pane` isn't currently showing `inspected_web_contents`.
  void UpdateDockedDevtoolsForPane(
      AtriumPaneView* pane,
      content::WebContents* inspected_web_contents);

  // Load phase. Points load_progress_observer_ at the
  // current active tab's WebContents and resets phase tracking to idle,
  // then - if the freshly-active tab is already loading - starts a fresh
  // run. Called from every ShowTab return and after each split
  // enter/collapse (the seams where the active tab or active pane changes).
  void RetargetLoadProgress();

  // Runs BuildLoadPhaseScript in the sidebar document and the devbar
  // document. Both draw the same two phase indicator, so one push feeds
  // them rather than each surface growing its own path.
  void PushLoadPhase(uint64_t tab_id, atrium::LoadPhase phase,
                     double fraction);

  // The tab load_progress_observer_ currently watches, or 0 before the
  // first Retarget. The push carries it so a row and a tile can key on it
  // the way atriumTabLoading does.
  uint64_t ObservedLoadTabId() const;

  // ActiveTabObserver's loading callbacks (active tab only): StartedLoading
  // begins a phase-push run, LoadProgress advances the monotonic max-so-far
  // fraction, StoppedLoading marks the run done.
  void OnActiveTabStartedLoading();
  void OnActiveTabLoadProgress(double progress);
  void OnActiveTabStoppedLoading();

  // Starts the kStatusBubbleShowDelay debounce, then hands the URL to
  // hover_url_widget_ as a card. A hover target change while a card is up
  // retargets it in place, with no re-debounce.
  void ShowStatusBubbleNow();

  // reuses the status-bubble surface to flash `text` (e.g. "Link
  // copied") at full opacity, then fades it out after
  // kStatusBubbleConfirmationDuration. Distinct from the hover path: it is
  // self-timed rather than mouse-driven, and paints `text` verbatim instead of
  // an elided URL (see status_bubble_override_text_).
  // `ok` false drops the tick, for a confirmation that reports a failure.
  // `icon_name` overrides the ok/close default (icons.js names); `key`
  // replaces an already-showing card with the same key instead of stacking.
  void ShowTransientConfirmation(const std::u16string& text, bool ok = true,
                                 const std::string& icon_name = "",
                                 const std::string& key = "");

  // Writes `url` and flashes the confirmation. The floating bar's Copy and
  // the quick menu's share one path.
  void CopyUrlToClipboard(const std::string& url);

  // The confirmation's auto-dismiss callback. A no-op once a hover has taken
  // over the bubble (override text cleared), so an in-flight dismiss can't
  // fade out a legitimate hover URL that replaced the confirmation.

  void OnWindowCloseRequested(views::Widget::ClosedReason close_reason);

  // Distinct from BrowserController's pending_move_out_empty_: that flag
  // names an automatic close's cause, this one overrides the suppression.
  // Cleared only once the strip is empty - see OnWindowCloseRequested.
  bool user_initiated_close_pending_ = false;

  // The above, held at a beforeunload prompt. A "stay" answer ends the close
  // silently, and this is what lets the next request notice and retire it.
  bool user_initiated_close_deferred_ = false;

  // Armed for ONE pass, immediately before the drain that makes TabStripEmpty
  // post a second Close. That Close arrives through AtriumClientView, which
  // consumes this rather than starting another close.
  bool expect_commit_close_ = false;

  // A posted CloseUserInitiated that has not run yet.
  bool user_initiated_close_post_pending_ = false;

  // Pushes split state to the HTML surface (same wire shape as the old
  // EchoSplitChanged wire shape). `active` is a param, not read off
  // pane_container()->is_split(), because FinishCloseSplit calls this AFTER
  // ExitSplit() so it can still report the surviving tab.
  void PushSplitState(bool active, const char* reason);

  // True while `contents` is attached to ANY live pane of the split, not only
  // the first two SplitPartnerTabId knows about.
  bool IsSplitPaneContents(content::WebContents* contents) const;

  // Shared dissolution body for CloseSplit (explicit op) and CollapseSplit
  // (implicit, tab-closed/moved-out): re-homes the surviving pane's
  // WebContents onto the primary pane's WebView if it currently lives in
  // the secondary pane (about to be destroyed), calls pane_container()->
  // ExitSplit(), updates split_primary_tab_id_/split_secondary_tab_id_, and
  // pushes `reason`. `keep_index` is 0 (primary survives) or 1 (secondary
  // survives).
  void FinishCloseSplit(int keep_index, const char* reason);

  // ends an active Mirror View split - Escape, navigating away
  // from the mirrored tab, and a space switch all route here.
  void EndMirror(const char* reason);

  // Drops the mirror badge, the mirror WebContents and a logged-out mirror's
  // off-the-record profile. Called from FinishCloseSplit, so EVERY split-close
  // path ends the mirror, not only EndMirror's.
  void ReleaseMirror();

  // also the shared tab/space-switch re-push, driven from
  // OnActiveTabChanged.
  void PushNoteToNotesContents();

  void PushDevBarAway(bool away);
  // Hands this window's bar the pin state and stops it moving on scroll.
  void ApplyDevBarPinned();

  // Fetches the active tab's marginalia (atrium_core_marginalia_for_url) and
  // runs the naive exact-substring re-anchor against its current DOM.
  void ReanchorMarginaliaOnActiveTab();

  // injects vim-page.js into every frame of the active tab. Every
  // frame, not just the primary main frame the marginalia call site uses - a
  // cross-origin iframe needs its own copy.
  void InstallVimOnActiveTab();

  // Forwards one character to vim-page.js, which runs the verb or refuses.
  void SendVimKey(const std::string& character);
  void OnVimKeyRan(const std::string& character, base::Value ran);

  // The page owns the mode, so the browser's copy is only ever a cache.
  void DrainVimMode();
  void OnVimModeDrained(base::Value mode);

  // a tab or a document left behind while a field had focus made
  // the next page's first f do nothing. Forgets what the old document said
  // and asks the new one.
  void ResetVimPageState();

  void HideVimOnActiveTab();

  // Two phases: ask every live frame for its target count, then hand each one
  // a disjoint slice of the label sequence.
  void ShowVimHints();
  void OnVimTargetCount(int generation,
                        content::GlobalRenderFrameHostId id,
                        base::Value count);

  void RunVimVerb(const std::string& character);
  bool RunVimBrowserVerb(const std::string& answer);
  void PushVimModeEnabled();

  // Mark -> card: drains the click queue an isolated-world listener
  // fills, then flashes each mark's card via notes_contents_'s existing
  // atriumFlashMarginal.
  void DrainMarginaliaMarkClicks();
  void OnMarginaliaClicksDrained(base::Value result);
  void PushFlashMarginal(const std::string& mark_id);

  // Card -> mark: scrolls `mark_id` into view on the active tab and
  // flashes it. Driven by the notes route's marginalMarkJumpRequested.
  void ScrollToMarginalMarkOnActivePage(const std::string& mark_id);

  // builds the ai-panel WebContents the same lazy,
  // idempotent way BuildHtmlSidebar/BuildHtmlFoyer do.
  void BuildHtmlAiPanel();

  // The panel docks opposite the sidebar, so this pushes !sidebar_on_right_.
  // Called on a real edge flip and from the aiPanelReady catch-up.
  void PushAiPanelOnRight();

  // tells the sidebar which edge it docks on, so its own Shelf
  // drop zone hugs the same one. Called on a real edge flip and from the
  // sidebarReady catch-up.
  void PushSidebarOnRight();

  // Pushes every native hotkey's glyph, keyed by action id, so the sidebar's
  // own hint bubbles can show the live binding. Called from the sidebarReady
  // catch-up, same as PushSidebarOnRight.
  void PushSidebarBindings();

  // Null when no permission sheet is currently on screen.
  PermissionSheetWidget* LivePermissionSheet() const {
    return live_permission_sheet_;
  }

  // Repaints every chrome surface after a tint, theme or native mode change.
  void PushAiPanelInk();
  // The quit HUD has no WebContents, so PushOverlayTheme never reaches it. It
  // reads the chosen theme's palette back from local state instead.
  void PushConfirmQuitPanelTheme();
  // The sidebar-only slice of PushAiPanelInk, for the 120 Hz scrub stream.
  void PushLiveScrubInk();
  void PushSidebarInk();
  // The ink graded against the backdrop, for a document that sits on it.
  void PushGlassInk(content::WebContents* contents);
  // the borderWidth setting as --atrium-content-edge-inset, which
  // ai-panel.css pads by. Overrides tokens.css's generated 10, so a surface
  // that never gets this push sits at the old fixed gutter.
  void PushCardInset(content::WebContents* contents);

  // Pushes {tabId, spaceIndex, pageTitle} for the active tab - the same
  // OnActiveTabChanged site PushNoteToNotesContents shares (one hook serves
  // both, since a space switch resolves to activating its front tab).
  void PushAiPanelTarget();

  // pushes the kill switch's current state. Called on a real
  // Settings edit (SetAiEnabled) and from the aiPanelReady catch-up.
  void PushAiPanelEnabled();

  // pushes the configured backtick remap. Called on a real
  // Settings edit (SetAiPanelHotkey) and from the aiPanelReady catch-up.
  void PushAiPanelHotkey();

  // Settings edit (SetAiPanelAutoSummarize) and from the aiPanelReady
  // catch-up.
  void PushAiPanelAutoSummarize();

  void PushAiPanelTranslateLanguage();

  // pushes the active tab's origin (empty for an opaque origin) to
  // atriumSetSidebarUrl. Called from OnActiveTabChanged and from
  // ActiveTabObserver's DidFinishNavigation.
  void PushSidebarUrl();

  // `tab_id` is the panel's attached tab, not necessarily
  // the active one. `request_id` rides every reply, so the JS side can match
  // it back to the summarize() call that sent it even out of order. A
  // non-empty `question` asks the on-device model about the page instead.
  // `extra_tab_ids` are the other pages a pinned panel has
  // travelled, empty for an unpinned one.
  void HandleAiPanelSummarizeRequested(
      const std::string& tab_id,
      int request_id,
      const std::string& question,
      const std::vector<std::string>& extra_tab_ids);

  // Falls back to the active tab when `tab_id` is empty or no longer in
  // this window's strip.
  content::WebContents* ResolveAiPanelTargetContents(const std::string& tab_id);

  // Reads `tab_ids` from `index` on, one page per hop, appending
  // each to `collected` before handing the join to the extraction callback.
  void CollectAiPanelPageText(int request_id,
                              std::string question,
                              std::vector<std::string> tab_ids,
                              size_t index,
                              std::string collected);
  void OnAiPanelOnePageCollected(int request_id,
                                 std::string question,
                                 std::vector<std::string> tab_ids,
                                 size_t index,
                                 std::string collected,
                                 base::Value page_text);

  // ExecuteJavaScript result callback for the innerText extraction above.
  void OnAiPanelPageTextExtracted(int request_id,
                                  std::string question,
                                  base::Value page_text);

  struct AiRouteFacts {
    std::string ai_model = "onDevice";
    std::string ollama_host;
    int ollama_port = 0;
    std::string ollama_model = "llama3";
    std::string custom_endpoint_url;
    std::string custom_endpoint_model = "gpt-4o-mini";
    atrium_ai::CustomEndpointLimits custom_endpoint_limits;
    // Zero means the settings read found no row, and the clamp falls back.
    size_t page_text_cap = 0;
    bool model_available = false;
  };

  // Blocks - settings.json I/O, plus a helper-process spawn for onDevice.
  // Static (no instance state) so it can be posted to the ThreadPool as a
  // plain function pointer.
  static AiRouteFacts ComputeAiRouteFacts();

  // Takes the page text unclamped: the cap is a setting, and `facts` is what
  // carries the settings read back to the UI thread.
  void OnAiPanelAvailabilityChecked(int request_id,
                                    std::string page_text,
                                    std::string question,
                                    AiRouteFacts facts);

  // Keychain read (blocks) before the custom-endpoint request can be built -
  // the key never lives in AiRouteFacts or settings.json.
  void OnCustomEndpointApiKeyFetched(int request_id,
                                     std::string clamped_page_text,
                                     std::string question,
                                     std::string custom_endpoint_url,
                                     std::string custom_endpoint_model,
                                     atrium_ai::CustomEndpointLimits limits,
                                     std::optional<std::string> api_key);

  // atrium_ai::SummarizeOnDevice's completion callback.
  void OnAiPanelSummarizeComplete(int request_id,
                                  std::optional<std::string> summary,
                                  std::optional<std::string> error);

  // Pushes a completed summary as plain text - the JS side renders it via
  // textContent, never innerHTML.
  void PushAiPanelSummary(int request_id, const std::string& summary);

  // the answer so far, while it is still arriving, and the
  // thinking beside it - two fields, so the panel can fold one away.
  void PushAiPanelSummaryChunk(int request_id,
                               const std::string& so_far,
                               const std::string& reasoning_so_far);

  // Pushes an explanation in place of a summary - unavailable model, empty
  // page, or a failed model call all show one instead of nothing.
  void PushAiPanelSummaryError(int request_id, const std::string& reason);

  // Echoes the JS-side open/close state so the container can grow/shrink -
  // the real content squeeze.
  void SetAiPanelOpen(bool open);

  // The settled drag width, clamped defensively (SetSidebarWidth's posture:
  // never trust a caller's own clamp alone). Stored even while closed, so a
  // later open uses the last width.
  void SetAiPanelWidth(int width);
  void ApplyExtensionPanelLayout();
  void BuildExtensionPanelChrome();
  // Hides without moving focus. The AI panel opening uses this.
  void HideExtensionPanel();
  // Gives the view back to its owner, which keeps the panel open.
  void ReturnExtensionPanelToOwner();
  void SetExtensionPanelWidthLive(int width);
  void CommitExtensionPanelWidth(int width);
  int StoredExtensionPanelWidth() const;
  Profile* ExtensionPanelProfile() const;

  // Delayed self-heal for ShowTab's attach: re-runs the WebView attach for
  // the tab identified by `expected_tab_id` (the TabId pointer-token; never
  // dereferenced before re-resolving) if it is still this window's shown
  // active tab. See ShowTab's comment for the damage-propagation bug this
  // works around.
  void ReattachActiveTabForDamageHeal(uint64_t expected_tab_id);

  // A page view put back in the window under a still pointer gets no mouse
  // moves until the pointer leaves it and comes back. AppKit only resets that
  // for a new tracking area, not for the one the view already had.
  void RearmPagePointerTracking(content::WebContents* contents);

  // The tail every ShowTab exit that reaches a pane runs through.
  ShowTabOutcome FinishShowTab(content::WebContents* contents,
                               ShowTabOutcome outcome);

  // One-shot clear for `suppress_showtab_this_turn_`, posted (not delayed)
  // from ArmMoveOutFreeze so it survives every ShowTab call driven by the
  // SAME synchronous detach event, however many of those calls there turn
  // out to be, and is ready again for the next, genuinely later one.
  void ClearShowTabSuppression();

  // CopyFromSurface's completion for ArmMoveOutFreeze. Bound rather than a
  // capturing lambda, which base::BindOnce refuses.
  //
  // `device_scale_factor` is read BEFORE the capture: an empty `output_size`
  // makes CopyFromSurface return a bitmap in DEVICE pixels, and the ImageSkia
  // must be built at that scale (gfx::ImageSkia::CreateFromBitmap, never
  // CreateFrom1xBitmap) or a Retina display draws the frame at twice its
  // size. Same idiom as TabDragController::OnTabThumbnailAvailable.
  void OnSnapshotCaptureCompleted(uint64_t tab_id,
                                  float device_scale_factor,
                                  base::OnceClosure on_frozen,
                                  const content::CopyFromSurfaceResult& result);

  // Swaps the pane from the live page to the snapshot and runs `on_frozen`.
  // An empty `bitmap` (a failed capture) still freezes, on the wash alone.
  void FreezeOn(uint64_t tab_id,
                float device_scale_factor,
                const SkBitmap& bitmap,
                base::OnceClosure on_frozen);

  // Snapshot-overlay click handler (Arc-parity revision 2: clicking the
  // frozen pane takes the tab over, same as clicking its sidebar row).
  void OnSnapshotClicked();

  // Peek scrim press handler: a click on the dimmed page outside the card
  // dismisses it, the same funnel Escape and the controls' close button use.
  void OnPeekScrimPressed();

  // AtriumPaneContainerView::SetDividerCommittedCallback's target is
  // OnDividerResizeCommitted, which fires on drag-end only.
  void OnDividerResizeCommitted();

  // A2 review fix (finding 3, plan section 1): "Activating the other pane
  // (click inside its web content...) = plain ActivateTabAt on the strip".
  // Subscribed to every pane's WebView via AddWebContentsFocusedCallback (the
  // upstream multi_contents_view.cc contents_focused_subscriptions_/
  // OnWebContentsFocused pattern - see RebuildPaneFocusSubscriptions). A no-op unless split AND `web_view` is the
  // currently INACTIVE pane's WebView - that second half of the guard is
  // also the loop guard: ActivateTabAt below flows through ShowTab's
  // other-pane branch, which only flips SetActivePaneIndex (no WebView
  // reattach, no focus change of its own), so once this pane becomes
  // active a repeat focus notification for the SAME pane fails the
  // "inactive" check and does not loop.
  //
  // Also a no-op while suppress_pane_focus_activation_ is set - see that
  // member's doc for the reentrancy crash this guards (VM-repro'd during
  // the re-probe: WebView::SetWebContents(non-null) unconditionally calls
  // WebContents::Focus() internally, which synchronously fires THIS
  // callback even for a purely programmatic attach; if that attach is
  // itself running from inside TabStripModel's own change dispatch (e.g.
  // FinishCloseSplit's survivor re-home, called from
  // BrowserController::OnTabStripModelChanged), calling ActivateTabAt here
  // re-enters TabStripModel while its internal reentrancy guard is armed -
  // FATAL: "Check failed: !*guard_flag_").
  void OnPaneWebContentsFocused(views::WebView* web_view);
  // Called after any change to the pane set. Dropping every subscription
  // first is what unhooks the pane that just went away.
  void RebuildPaneFocusSubscriptions();

  // can_show_bubble is false for a bare active-tab switch, not an actual zoom.
  void OnZoomChanged(bool can_show_bubble);

  // Advances the in-flight sidebar-resize animation
  // one tick (~60Hz, driven by sidebar_resize_timer_) - eases from
  // sidebar_resize_start_width_ to sidebar_resize_target_width_ over
  // kSidebarResizeAnimationDuration, via sidebar_slot()'s preferred size
  // (BoxLayout's own sizing mechanism, not a Layer transform - see
  // SetSidebarWidth's .mm comment for why). Stops the timer once the
  // animation reaches its target.
  void StepSidebarResizeAnimation();

  // SetSidebarWidth's easing without its store write, last-docked bookkeeping
  // or traffic-light re-assertion. `hold` keeps the page at a fixed width for
  // the slide; the overview widen passes false and lets it reflow.
  void AnimateSidebarSlotWidth(int target, bool hold = true);

  const raw_ptr<Browser> browser_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  // M4: the command-bar child widget (see CommandBarWidget's class doc).
  // Declared after `widget_` so it is destroyed FIRST on implicit teardown
  // (reverse declaration order) - the destructor below also resets it
  // explicitly, ahead of widget_.reset(), for the same reason: a child
  // widget must not outlive the parent it's parented into.
  std::unique_ptr<CommandBarWidget> command_bar_widget_;
  // Same child-widget lifetime reasoning as command_bar_widget_ above.
  std::unique_ptr<FindBarWidget> find_bar_widget_;
  // Same child-widget lifetime reasoning as command_bar_widget_ above.
  std::unique_ptr<ThemeStudioWidget> theme_studio_widget_;
  // Non-owning: AtriumPermissionPrompt owns the widget, self-registered here
  // through SetLivePermissionSheet so PushAiPanelInk can reach it.
  raw_ptr<PermissionSheetWidget> live_permission_sheet_ = nullptr;
  // a bubble anchors to a View, and the extension strip is HTML, so
  // this stands in for the clicked icon. Owned by the root view, ignored by
  // its layout, empty whenever no icon rect has been reported.
  raw_ptr<views::View> extension_popup_anchor_ = nullptr;
  // Holds a DownloadManager observer, so it is reset explicitly in the
  // destructor rather than left to member order.
  std::unique_ptr<OverviewController> overview_controller_;
  // same child-widget lifetime reasoning as command_bar_widget_
  // above - declared here and reset explicitly in the destructor ahead of
  // widget_.reset().
  std::unique_ptr<CourtyardWidget> courtyard_widget_;
  // See SetCourtyardOpen. Latched here because a space switch or a theme push
  // arriving while the Courtyard is up would otherwise repaint root_view_.
  bool courtyard_open_ = false;
  double pinch_wheel_accumulator_for_testing_ = 0;
  // The hover-reveal overlay child widget. Declared
  // (and destroyed) alongside command_bar_widget_ - a child widget must not
  // outlive the parent it's parented into (the destructor resets it
  // explicitly, ahead of widget_.reset()).
  std::unique_ptr<AtriumSidebarOverlayWidget> sidebar_overlay_widget_;
  // the pointer-at-the-edge reveal. Constructed with the window
  // rather than lazily: it holds one hidden view and one tracking area, and a
  // lazy build would need a trigger, which is the thing it exists to provide.
  std::unique_ptr<AtriumSidebarEdgeReveal> sidebar_edge_reveal_;

  // whether the stored sidebar width has been applied yet. The
  // slot is built at kDefaultSidebarWidth before the core is open - the store
  // is read on a thread that may block - so the stored width cannot be applied
  // at construction. It is applied on the FIRST push instead, which is the
  // earliest moment the core has provably answered.
  bool applied_stored_sidebar_width_ = false;
  // the window chrome only a platform can draw. Created with the
  // widget and used instead of reaching for AppKit from this class.
  std::unique_ptr<atrium::AtriumWindowPlatform> window_platform_;
  // Peek mode: the peek child widget (see PeekWidget's class doc).
  // Declared (and destroyed) alongside command_bar_widget_/
  // sidebar_overlay_widget_ - a child widget must not outlive the parent
  // it's parented into (the destructor resets it explicitly, ahead of
  // widget_.reset()).
  std::unique_ptr<PeekWidget> peek_widget_;
  void PushPeekBadge(uint64_t tab_id, uint64_t icon_id, uint64_t rev);
  uint64_t peek_badge_tab_id_ = 0;
  // the region-select capture overlay child widget (see
  // AtriumCaptureOverlayWidget's class doc) - unlike the widgets above, NOT
  // build-once: BeginRegionCapture creates a fresh instance per capture and
  // replaces (destroying) any prior one. Declared (and destroyed) alongside
  // command_bar_widget_/sidebar_overlay_widget_/peek_widget_ - a child
  // widget must not outlive the parent it's parented into (the destructor
  // resets it explicitly, ahead of widget_.reset()).
  std::unique_ptr<AtriumCaptureOverlayWidget> capture_overlay_widget_;

  // where a finished capture goes - the user's Downloads directory
  // AND the clipboard, then a themed confirmation naming the file. Both,
  // because a screenshot is as often pasted straight into a message as kept.
  // `png` empty means the capture failed; the confirmation says so rather than
  // leaving the verb looking like it did nothing.
  void DeliverCapture(std::optional<std::vector<uint8_t>> png);
  // Built on the first ToggleQuickMenu and reused for the window's lifetime.
  std::unique_ptr<AtriumQuickMenuWidget> quick_menu_widget_;
  // Built on the first ShowSpaceIconPicker and reused for the window's
  // lifetime.
  std::unique_ptr<AtriumSpaceIconPickerWidget> space_icon_picker_widget_;
  // The extension right-click menu - a plain MenuRunner, not a widget: there
  // is no bubble chrome to theme, only the native menu.
  std::unique_ptr<extensions::ExtensionContextMenuModel> extension_menu_model_;
  std::unique_ptr<views::MenuModelAdapter> extension_menu_adapter_;
  std::unique_ptr<views::MenuRunner> extension_menu_runner_;
  base::CallbackListSubscription extension_menu_owner_closed_subscription_;
  // The sidebar hint bubble's own overlay window - see HintOverlayWidget's
  // class doc. Declared and destroyed alongside the widgets above, for the
  // same child-must-not-outlive-widget_ reason.
  std::unique_ptr<HintOverlayWidget> hint_overlay_widget_;
  // The toast stack's own window, owned and destroyed the same way.
  std::unique_ptr<ToastOverlayWidget> toast_overlay_widget_;
  // The hovered link's URL, at the bottom leading corner. Its own instance:
  // one widget reaching both corners would cover the page between them.
  std::unique_ptr<ToastOverlayWidget> hover_url_widget_;

  // Null until the first surface registers a shadow.
  std::unique_ptr<AtriumOverlayShadowLayer> overlay_shadow_layer_;
  AtriumOverlayShadowLayer* EnsureOverlayShadowLayer();
  // The devbar's own resolved shadow spec, pushed by devbar.js - C++ already
  // knows the bar's rect (BubbleBounds) but not overlay.css's colour, blur
  // and offset, which vary by theme.
  std::optional<AtriumOverlaySurfaceShadow> devbar_shadow_spec_;
  // devbar.css's own away transform and transition, pushed with the spec so
  // the shadow leaves on the same curve rather than on a second copy of it.
  gfx::Vector2d devbar_away_shift_;
  base::TimeDelta devbar_away_duration_;
  // Combines devbar_shadow_spec_ with each bar's current bounds and
  // (un)registers its shadow. A no-op with no spec yet or a hidden bar.
  void PushDevBarShadow();
  // Coalesces OnViewLayoutInvalidated bursts into one posted PushDevBarShadow
  // - ToastOverlayWidget::ReapplyBoundsForPaneLayout debounces the same way.
  bool devbar_shadow_reapply_pending_ = false;
  bool sidebar_push_pending_ = false;
  int sidebar_snapshot_builds_ = 0;

  // null until the floating sidebar panel first shows.
  std::unique_ptr<AtriumSidebarFrostLayer> sidebar_frost_layer_;
  AtriumSidebarFrostLayer* EnsureSidebarFrostLayer();
  // Hands the frost the same light/dark decision PushSidebarInk makes for the
  // rows, so the material matches the wash the panel paints over it.
  void PushSidebarFrostAppearance();
  // lazily constructed on the first GetAutofillBubbleHandler()
  // call. Unlike the child widgets above this holds no pointer into
  // widget_'s view tree (it only calls GetAutofillBubbleAnchor() on demand),
  // so its declaration order relative to widget_ doesn't matter for
  // teardown safety - grouped here anyway, alongside the other lazily
  // constructed per-window UI helpers.
  std::unique_ptr<AtriumAutofillBubbleHandler> autofill_bubble_handler_;
  raw_ptr<AtriumRootView> root_view_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  std::unique_ptr<StubLocationBar> location_bar_;
  std::unique_ptr<ModalDialogHost> modal_dialog_host_;
  std::unique_ptr<ExclusiveAccessContextImpl> exclusive_access_context_;

  // See IsFrozen().
  bool is_frozen_ = false;
  // One-shot suppression for the ShowTab call(s) driven by the SAME
  // synchronous move-out event that just armed the freeze - see
  // ArmMoveOutFreeze's comment for why this can't be a simple per-call
  // consumed flag (ShowTab is called twice per event, via two independent
  // routing paths, in an order this class doesn't control).
  bool suppress_showtab_this_turn_ = false;
  // The tab id this window is frozen on - see IsFrozen()/ArmMoveOutFreeze.
  // Meaningless (stale) while `is_frozen_` is false.
  uint64_t frozen_tab_id_ = 0;
  // The tab a capture is in flight for. A second move of the same tab while
  // it is pending is dropped: the first completes the move.
  uint64_t pending_freeze_tab_id_ = 0;

  // A2 review fix (finding 3, re-probe): true for the duration of every
  // programmatic `views::WebView::SetWebContents(non-null)` call this class
  // makes - ShowTab's two attach branches, EnterSplit, FinishCloseSplit's
  // survivor re-home, ReattachActiveTabForDamageHeal's detach+reattach (set
  // via base::AutoReset at each call site). WebView::SetWebContents calls
  // WebContents::Focus() internally regardless of whether the attach was
  // user-driven or code-driven, which synchronously fires
  // OnPaneWebContentsFocused - see that method's doc for the reentrancy
  // FATAL this prevents.
  bool suppress_pane_focus_activation_ = false;

  // Tab identity for the current
  // split, meaningless unless root_view_->pane_container()->is_split() is
  // also true - the container (a views::View) is the single source of
  // truth for "is a split visually active"; these ids are its accompanying
  // WebContents identity, which the container has no business tracking
  // itself (see SplitPartnerTabId).
  uint64_t split_primary_tab_id_ = 0;
  uint64_t split_secondary_tab_id_ = 0;
  // One per live pane, for focus-driven activation (OnPaneWebContentsFocused).
  // A list rebuilt with the pane set, not a member per pane: a left or top
  // drop moves a pane's leaf index, and any pane can be the one that closes.
  std::vector<base::CallbackListSubscription> pane_focus_subscriptions_;
  base::CallbackListSubscription zoom_changed_subscription_;

  // In-flight resize-animation state - see
  // SetSidebarWidth/StepSidebarResizeAnimation. Meaningless while
  // sidebar_resize_timer_ isn't running.
  int sidebar_resize_start_width_ = 0;
  int sidebar_resize_target_width_ = 0;
  // The docked width the traffic lights travel against, 0 when this resize
  // does not move them.
  int light_slide_span_ = 0;
  base::TimeTicks sidebar_resize_animation_start_;
  bool sidebar_reveal_wanted_ = false;
  bool sidebar_reveal_posted_ = false;
  base::RepeatingTimer sidebar_resize_timer_;

  // replies from an earlier hint pass are dropped rather than
  // merged, so a second f cannot give two frames the same offset.
  int vim_hint_generation_ = 0;
  size_t vim_hint_pending_ = 0;
  std::vector<std::pair<content::GlobalRenderFrameHostId, int>>
      vim_hint_counts_;

  // the page's last reported mode, which vim-page.js vetoes a steal
  // made on a stale value of. per window. Shared as one static, a
  // second window's tab switch answered for the window being typed into.
  bool vim_page_normal_mode_ = true;

  // hints are up, so every key is a label. The page sets and clears
  // it, because the browser cannot see what the page is showing.
  bool vim_page_capturing_ = false;

  // The width to put back when the overview pane closes. Re-read on every
  // open, and re-taken by a grip release, so a drag under the open overview
  // is not undone by the close.
  bool sidebar_overview_open_ = false;
  int sidebar_width_before_overview_ = 0;

  // Sidebar on right: which edge the sidebar is currently docked
  // to - false is left, the stock default. Set by SetSidebarPosition; read
  // when a hover-overlay widget is created lazily so it picks up the
  // current side.
  bool sidebar_on_right_ = false;

  // default alpha baked into the glass backdrop wash - a named
  // constant rather than a bare literal on the member below, so a reader (or
  // the GLASS BACKDROP ALPHA doc block in the retired ABI header) can reference
  // it by name. Mirrors the old kGlassBackdropAlpha file-local
  // constant exactly (0xB8, ~72% - keeps the tint clearly dominant while the
  // desktop blur reads through).
  static constexpr uint8_t kDefaultGlassBackdropAlpha = 0xB8;
  // the CURRENT alpha SetBackdropTint/PaintThemeBackdrop/
  // StatusBubbleBackgroundColor bake into the glass wash - user-adjustable via
  // SetGlassBackdropAlpha (set_glass_backdrop_alpha op), defaulting to
  // kDefaultGlassBackdropAlpha so an unadjusted window looks identical to
  // every earlier build.
  uint8_t backdrop_glass_alpha_ = kDefaultGlassBackdropAlpha;

  // the native-app chrome mode as SetOpaqueSurround last received
  // it. The pane container holds the fill, and a views::Background answers
  // neither which colour it is nor whether one is set - so a sidebar surface
  // built after the mode was turned on has nothing to ask. This is what
  // PushSidebarAppearance reads.
  bool opaque_surround_enabled_ = false;
  uint32_t opaque_surround_argb_ = 0;

  // Relayouting on a save that only moved the accent would rebuild
  // the tiles' mask layers for nothing. 0, not the store's default, which
  // here would be a second copy free to drift from the one in the .cc.
  int applied_card_inset_ = 0;

  // The brand book theme's slug, read by PushOverlayTheme. Defaults to
  // crimson, "brand"'s own accent, matching WindowAppearance's default.
  std::string overlay_theme_ = "crimson";

  // The derived palette for the live macOS accent, held so one settings push
  // to nine surfaces derives it once. Dropped when the system accent changes.
  std::optional<OverlayAccentTokens> system_accent_tokens_;

  // Backdrop-tint crossfade: current/animated tint state. The
  // first SetBackdropTint applies instantly (nothing to fade from); every
  // later one lerps current -> target over kBackdropTintFadeDuration.
  bool has_backdrop_tint_ = false;

  std::string ChromeInkHex();
  std::optional<SkColor> ChromeColorForPage(const std::string& page_hex,
                                            const std::string& ink_hex);
  void AdvanceAmbilightCadence(bool changed);
  void ScheduleNextAmbilightSample(int interval_ms);
  void SampleAmbilightFrame();
  void OnAmbilightFrame(int generation, const SkBitmap& frame);

  // One-shot rather than repeating: the cadence picks a new interval after
  // every read, and a RepeatingTimer is fixed at the one it started with.
  base::OneShotTimer ambilight_timer_;
  // Whether the sampler is live, which the timer cannot say: a one-shot is
  // not running between firing and the surface copy coming back, and on a
  // slow GPU that gap is most of every cycle.
  bool ambilight_live_ = false;
  // Bumped on every start and stop. A copy still in flight from the page a
  // space switch left must not paint its colour on the new space.
  int ambilight_generation_ = 0;
  atrium::AmbilightCadence ambilight_cadence_;
  atrium::StartupHold startup_hold_;
  atrium::DevBarScrollAway devbar_scroll_;
  bool sidebar_drawn_ = false;
  std::unique_ptr<SidebarPaintWatch> sidebar_paint_watch_;
  // Mirrors the last SetExcludedFromWindowCycling call - true from
  // construction for a show=false hidden-pool member, since that path is the
  // one caller that sets it before any Show()/Hide() ever runs.
  bool excluded_from_window_cycling_ = false;
  // The last tint the sampler resolved, before the glass alpha. Compared
  // rather than painted, so an unchanged page neither logs nor repaints.
  atrium::EdgeTint last_sampled_;
  // Carries the glow alpha on every edge, the way backdrop_tint_ carries the
  // glass alpha. The start/target pair and the timer are its crossfade, on
  // the same tick and duration as the flat tint's.
  void RegisterSurfaceRoutes();
  void OnSurface_FoyerReady(const base::ListValue& args);
  void OnSurface_FoyerMoveToSpace(const base::ListValue& args);
  void OnSurface_DevBarReady(const base::ListValue& args);
  void OnSurface_DevBarCopyUrl(const base::ListValue& args);
  void OnSurface_DevBarPinToggled(const base::ListValue& args);
  void OnSurface_DevBarShadowSpec(const base::ListValue& args);
  // A press anywhere on a split bar makes its pane the active one first, so
  // the url edit, the nav buttons and copy act on that pane.
  void OnSurface_DevBarPointerDown(const base::ListValue& args);
  // the bar's grip drags its tab like a sidebar row.
  void OnSurface_DevBarTabDragStarted(const base::ListValue& args);
  void OnSurface_DevBarTabDragMoved(const base::ListValue& args);
  void OnSurface_DevBarTabDragReleased(const base::ListValue& args);
  void OnSidebarExternalDropEnded(uint64_t tab_id, base::Value placed);
  // The own-window drop target under `screen_point`, and the pane index that
  // shows `tab` or -1.
  atrium::OwnWindowDrop OwnWindowDropAt(const gfx::Point& screen_point);
  int PaneIndexShowing(content::WebContents* tab);
  void ClearOwnPaneDropPreviews();
  // Bar 0's page, or null before it is built.
  content::WebContents* DevBarContents() const;
  // The sending bar's page, or bar 0's outside a bar's message.
  content::WebContents* DevBarSender() const;
  // The tab bar `index` shows: its own pane's while split, else the one the
  // panes show.
  content::WebContents* DevBarTab(size_t index);
  // The tab a back, forward or reload button acts on: a split bar's own pane,
  // which its press cannot make active when that pane is not a strip tab.
  content::WebContents* NavTargetWebContents();
  void OnDevBarSurfaceMessage(AtriumDevBar* bar,
                              std::string_view name,
                              const base::ListValue& args);
  void OnDevBarTabNavigated();
  void OnSurface_SidebarOverviewPaneRendered(const base::ListValue& args);
  void OnSurface_SidebarOverviewPaneActive(const base::ListValue& args);
  void OnSurface_SidebarRowsDrawn(const base::ListValue& args);
  void OnSurface_SidebarReady(const base::ListValue& args);
  void OnSurface_PillActionClicked(const base::ListValue& args);
  void OnSurface_ExtensionChipContextMenu(const base::ListValue& args);
  void OnSurface_ExtensionPinToggled(const base::ListValue& args);
  // Runner, then adapter, then model, then the owner-close subscription.
  void ResetExtensionMenuState();
  void OnExtensionContextMenuClosed();
  // The owner browser can differ from this window's own (see
  // ControllerOwningExtension) - if it closes while the menu is up, the
  // model's raw BrowserWindowInterface* would dangle without this.
  void OnExtensionContextMenuOwnerClosed(BrowserWindowInterface*);
  void OnSurface_QuickMenuToggled(const base::ListValue& args);
  void OnSurface_SidebarTabClicked(const base::ListValue& args);
  void OnSurface_SidebarColdTabClicked(const base::ListValue& args);
  void OnSurface_SidebarTabClosed(const base::ListValue& args);
  void OnSurface_MediaPlayPause(const base::ListValue& args);
  void OnSurface_MediaPrev(const base::ListValue& args);
  void OnSurface_MediaNext(const base::ListValue& args);
  void OnSurface_MediaMuteToggled(const base::ListValue& args);
  void OnSurface_MediaBackToTab(const base::ListValue& args);
  void OnSurface_MediaClosed(const base::ListValue& args);
  void OnSurface_MediaPictureInPicture(const base::ListValue& args);
  void OnSurface_MediaVolume(const base::ListValue& args);
  void OnSurface_VimVerb(const base::ListValue& args);
  void OnSurface_SidebarRestoreSlice(const base::ListValue& args);
  void OnSurface_SidebarTabReordered(const base::ListValue& args);
  void OnSurface_SidebarPinnedReordered(const base::ListValue& args);
  void OnSurface_SidebarTabPinned(const base::ListValue& args);
  void OnSurface_SidebarTabUnpinned(const base::ListValue& args);
  void OnSurface_SidebarPinnedTabRemoved(const base::ListValue& args);
  void OnSurface_SidebarColdTabRemoved(const base::ListValue& args);
  void OnSurface_SidebarTabSnoozed(const base::ListValue& args);
  void OnSurface_SidebarTabShelved(const base::ListValue& args);
  void OnSurface_SidebarFavoriteShelved(const base::ListValue& args);
  void OnSurface_SidebarFavoriteRemoved(const base::ListValue& args);
  void OnSurface_SidebarFavoriteMovedOut(const base::ListValue& args);
  void OnSurface_SidebarTabAddedToDock(const base::ListValue& args);
  void OnSurface_SidebarRowFavorited(const base::ListValue& args);
  void OnSurface_SidebarFavoriteGrouped(const base::ListValue& args);
  void OnSurface_SidebarTabGrouped(const base::ListValue& args);
  void OnSurface_SidebarTabUngrouped(const base::ListValue& args);
  void OnSurface_SidebarTabSplit(const base::ListValue& args);
  void OnSurface_SidebarFolderMemberReordered(const base::ListValue& args);
  void OnSurface_SidebarFolderPinned(const base::ListValue& args);
  void OnSurface_SidebarFolderCreated(const base::ListValue& args);
  void OnSurface_SidebarFolderRenamed(const base::ListValue& args);
  void OnSurface_SidebarFolderDeleted(const base::ListValue& args);
  void OnSurface_SidebarFolderIconSet(const base::ListValue& args);
  void OnSurface_SidebarFolderReordered(const base::ListValue& args);
  void OnSurface_SidebarFavoriteReordered(const base::ListValue& args);
  void OnSurface_SidebarTabMovedToSpace(const base::ListValue& args);
  void OnSurface_SidebarRunMoved(const base::ListValue& args);
  void OnSurface_SidebarTabMirrored(const base::ListValue& args);
  void OnSurface_SidebarTabDragMoved(const base::ListValue& args);
  void OnSurface_SidebarTabDragReleased(const base::ListValue& args);
  void OnSurface_SidebarPinnedToggled(const base::ListValue& args);
  void OnSurface_SidebarTextFocusChanged(const base::ListValue& args);
  void OnSurface_SidebarNewTab(const base::ListValue& args);
  // sidebarAddressRowClicked and devBarUrlClicked differ only in which
  // surface the click's rect is relative to.
  void HandleAddressRowClicked(content::WebContents* surface,
                               const base::ListValue& args);
  void OnSurface_SidebarAddressRowClicked(const base::ListValue& args);
  void OnSurface_DevBarUrlClicked(const base::ListValue& args);
  // The click's rect arrives in the surface document's client coordinates.
  // Anchoring needs it in the window's, and the two differ by wherever that
  // surface's render widget currently sits.
  gfx::Rect SurfaceRectToWindow(content::WebContents* surface,
                                const gfx::Rect& client_rect) const;
  void HandleHintShow(content::WebContents* surface,
                      const base::ListValue& args);
  void OnSurface_SidebarHintShow(const base::ListValue& args);
  void OnSurface_DevBarHintShow(const base::ListValue& args);
  void OnSurface_SurfaceHintHide(const base::ListValue& args);
  void OnSurface_SidebarBackPressed(const base::ListValue& args);
  void OnSurface_SidebarForwardPressed(const base::ListValue& args);
  void OnSurface_SidebarReloadPressed(const base::ListValue& args);
  // the same three controls with Option or Cmd+Shift held. One
  // handler per name, each covering one button's one modifier.
  void OnSurface_SidebarBackInNewTab(const base::ListValue& args);
  void OnSurface_SidebarBackToStart(const base::ListValue& args);
  void OnSurface_SidebarForwardInNewTab(const base::ListValue& args);
  void OnSurface_SidebarForwardToEnd(const base::ListValue& args);
  void PushNavModifiers(bool shift, bool cmd, bool option);
  // Whether the displayed tab can go back and forward, so the buttons that
  // cannot move it grey out. Pushed on navigation, on a tab switch, and when
  // the window becomes key.
  void PushNavAbility();
  void OnSurface_SidebarReloadHard(const base::ListValue& args);
  void OnSurface_SidebarReloadClearingSite(const base::ListValue& args);
  void OnSurface_SidebarReloadClearingCache(const base::ListValue& args);
  // sidebarMenuOpened and sidebarMenuClosed just set opposite values of the
  // flag that keeps the dismiss off the path of an ordinary click.
  void OnSurface_SidebarMenuOpened(const base::ListValue& args);
  void OnSurface_SidebarMenuClosed(const base::ListValue& args);
  void PushDismissMenus();
  bool sidebar_menu_open_ = false;
  void OnSurface_SidebarToggleRequested(const base::ListValue& args);
  void OnSurface_SidebarOverlayWidthLive(const base::ListValue& args);
  void OnSurface_SidebarOverlayWidthCommit(const base::ListValue& args);
  // The panel-dismiss prologue every window control shares.
  void HandleWindowControlDismiss();
  void OnSurface_SidebarWindowClose(const base::ListValue& args);
  void OnSurface_SidebarWindowMinimize(const base::ListValue& args);
  void OnSurface_SidebarWindowZoom(const base::ListValue& args);
  void OnSurface_SidebarBackHeld(const base::ListValue& args);
  void OnSurface_SidebarTimelineCommit(const base::ListValue& args);
  void OnSurface_SidebarFolderToggled(const base::ListValue& args);
  void OnSurface_SidebarFavoriteClicked(const base::ListValue& args);
  void OnSurface_SidebarSpaceSelected(const base::ListValue& args);
  void OnSurface_SidebarSpaceScrubBegan(const base::ListValue& args);
  void OnSurface_SidebarSpaceScrubBlend(const base::ListValue& args);
  void OnSurface_SidebarSpaceScrubEnded(const base::ListValue& args);
  void OnSurface_SidebarSwipeHaptic(const base::ListValue& args);
  void OnSurface_SidebarSpaceThemeRequested(const base::ListValue& args);
  void OnSurface_SettingsThemePreview(const base::ListValue& args);
  void OnSurface_SettingsThemeChanged(const base::ListValue& args);
  void OnSurface_SidebarTabArchived(const base::ListValue& args);
  void OnSurface_SidebarTabsArchivedBelow(const base::ListValue& args);
  void OnSurface_SidebarTabDuplicated(const base::ListValue& args);
  void OnSurface_SidebarTabsCleared(const base::ListValue& args);
  void OnSurface_SidebarSpaceReordered(const base::ListValue& args);
  void OnSurface_SidebarTabRenamed(const base::ListValue& args);
  void OnSurface_SidebarSpaceCreated(const base::ListValue& args);
  void OnSurface_SidebarShelfToastClicked(const base::ListValue& args);
  void OnSurface_SidebarOverviewSectionOpened(const base::ListValue& args);
  void OnSurface_SidebarSpaceRenamed(const base::ListValue& args);
  void OnSurface_SidebarSpaceIconSet(const base::ListValue& args);
  void OnSurface_SidebarSpaceIconPickerOpened(const base::ListValue& args);
  void OnSurface_SidebarOverviewToggled(const base::ListValue& args);
  void OnSurface_SidebarCourtyardOpened(const base::ListValue& args);
  void OnSurface_OpenSettingsPane(const base::ListValue& args);
  void OnSurface_NotesReady(const base::ListValue& args);
  void OnSurface_NotesBodyChanged(const base::ListValue& args);
  void OnSurface_MarginaliaNoteChanged(const base::ListValue& args);
  void OnSurface_MarginalMarkJumpRequested(const base::ListValue& args);
  void OnSurface_AiPanelReady(const base::ListValue& args);
  void OnSurface_AiPanelToggled(const base::ListValue& args);
  void OnSurface_AiPanelWidthChanged(const base::ListValue& args);
  void OnSurface_AiPanelRowMark(const base::ListValue& args);
  void OnSurface_AiPanelSummarizeRequested(const base::ListValue& args);
  void OnSurface_AiPanelChatRequested(const base::ListValue& args);
  void OnSurface_AiPanelToggleRequested(const base::ListValue& args);
  void OnSurface_ExtensionSidePanelToggleRequested(const base::ListValue& args);
  // One handler per name routed to the overview widget, so the row it
  // forwards is a compile-time literal rather than the incoming name.
  void OnSurface_NotesListReady(const base::ListValue& args);
  void OnSurface_SpacesListReady(const base::ListValue& args);
  void OnSurface_ShelfListReady(const base::ListValue& args);
  void OnSurface_ArchivedListReady(const base::ListValue& args);
  void OnSurface_SnoozedListReady(const base::ListValue& args);
  void OnSurface_DownloadsShowInFinder(const base::ListValue& args);
  void OnSurface_DownloadsRemove(const base::ListValue& args);
  void OnSurface_DownloadsDeleteFromDisk(const base::ListValue& args);
  void OnSurface_ShelfListAdd(const base::ListValue& args);
  void OnSurface_ShelfListRemove(const base::ListValue& args);
  void OnSurface_ShelfListOpen(const base::ListValue& args);
  void OnSurface_NotesListCreate(const base::ListValue& args);
  void OnSurface_NotesListDismiss(const base::ListValue& args);
  void OnSurface_NotesListDelete(const base::ListValue& args);
  void OnSurface_NotesListOpen(const base::ListValue& args);
  void OnSurface_ArchivedListRestore(const base::ListValue& args);
  void OnSurface_ArchivedListDelete(const base::ListValue& args);
  void OnSurface_SnoozedListWake(const base::ListValue& args);
  void OnSurface_SnoozedListDelete(const base::ListValue& args);
  atrium::SurfaceMessageRouter surface_router_;
  atrium::EdgeTint edge_tint_;
  base::RepeatingTimer surround_fade_timer_;
  double surround_fade_t_ = 1.0;
  bool surround_fade_to_on_ = false;
  bool surround_fade_running_ = false;
  // The surround on screen during the fade - the new colour fading in, or the
  // old one fading out, which the disabling call no longer carries.
  SkColor surround_fade_color_ = 0;
  atrium::EdgeTint edge_tint_start_;
  atrium::EdgeTint edge_tint_target_;
  double edge_tint_t_ = 1.0;
  base::RepeatingTimer edge_tint_timer_;
  // The first glow after a page load takes a slow fade, later ticks the
  // usual one. Set when a load stops, cleared when that fade ends.
  bool edge_tint_slow_fade_ = false;
  // A load began on the active tab and its glow is still to come.
  bool glow_waits_for_load_ = false;
  // The active page's brand colour - its favicon first, its theme-color
  // second - refreshed on the event path and read by every sampler tick.
  std::optional<SkColor> ambilight_brand_;
  SkColor backdrop_tint_ = 0;
  SkColor backdrop_tint_start_ = 0;
  SkColor backdrop_tint_target_ = 0;
  base::TimeTicks backdrop_tint_animation_start_;
  // tick-accumulated linear progress (0..1) of the tint
  // crossfade. Progress used to derive from wall-clock elapsed time, so a
  // main-thread stall (the space switch's settle IPC) skipped straight to
  // the end when ticks resumed - the "fades smoothly then jumps the last
  // few shades" glitch. Accumulating per tick pauses under a stall and
  // resumes smoothly instead.
  double backdrop_tint_t_ = 1.0;
  base::RepeatingTimer backdrop_tint_timer_;

  // Theme Studio gradient backdrop. When has_theme_ is true, theme_bitmap_
  // wins over the flat backdrop_tint_ wash (an AtriumThemeBackground is
  // installed on root_view_); clearing (rgba == nullptr) reverts to the tint.
  SkBitmap theme_bitmap_;       // current
  SkBitmap theme_bitmap_prev_;  // crossfade source, dropped when the fade ends
  bool has_theme_ = false;
  float theme_grain_ = 0.f;
  base::RepeatingCallback<void(bool)> theme_visibility_callback_;
  // Eased crossfade progress: 0 shows only theme_bitmap_prev_, 1 only
  // theme_bitmap_. Held at 1 while no fade is running (the tint's
  // backdrop_tint_ analog - the animated value the painter reads).
  float theme_fade_ = 1.f;
  base::TimeTicks theme_animation_start_;
  base::TimeDelta theme_fade_duration_;
  // tick-accumulated linear progress of the theme crossfade -
  // same stall-resilience rationale as backdrop_tint_t_ above.
  double theme_fade_t_ = 1.0;
  // a theme CLEAR in flight - the gradient fades OUT over the
  // flat tint wash (from `theme_clear_from_` down to 0) instead of cutting
  // to it; on completion the painter is uninstalled and the solid tint
  // background takes over, pixel-identical to the wash it fades onto.
  bool theme_clearing_ = false;
  float theme_clear_from_ = 1.f;
  base::RepeatingTimer theme_timer_;
  SkBitmap grain_tile_;  // 64x64 static noise, built lazily on first paint

  // Staged INCOMING theme for a live scrub cross-fade (the
  // lab's ScrubThemeBlendLayer pattern). scrub_blend_weight_ < 0 means "no
  // scrub in flight" (the D1 stub's sentinel, now load-bearing); an empty
  // scrub_bitmap_ while a scrub IS in flight means "flat tint incoming" - the
  // painter composites backdrop_tint_ (kept live by SetBackdropTintLive even
  // while has_theme_ is true - see its comment) instead of a second bitmap.
  SkBitmap scrub_bitmap_;
  double scrub_blend_weight_ = -1.0;
  float scrub_grain_ = 0.f;
  // the scrub opened the theme session itself (flat outgoing
  // space), so a cancel has to close it again.
  bool scrub_opened_theme_ = false;
  // Commit-time ease state for the case above - a fast flick's
  // staged weight is still low when the gesture ends, so SetBackdropScrub-
  // BlendEnd(commit=1) eases it to 1.0 over the caller's settle window
  // instead of snapping. `scrub_blend_settle_from_` is the weight the ease
  // started at (StepScrubBlendSettleAnimation lerps from there to 1.0);
  // `scrub_blend_settle_t_` is tick-accumulated progress, same stall-
  // resilience rationale as backdrop_tint_t_/theme_fade_t_ above.
  base::RepeatingTimer scrub_blend_settle_timer_;
  double scrub_blend_settle_from_ = 0.0;
  double scrub_blend_settle_t_ = 1.0;
  base::TimeDelta scrub_blend_settle_duration_;

  // kLoadProgress: retargeted onto the active PANE's
  // WebContents from RetargetLoadProgress, which can differ from the tab
  // strip's active one while split.
  std::unique_ptr<ActiveTabObserver> load_progress_observer_;
  // kOther: retargeted onto the tab strip's active WebContents from
  // OnActiveTabChanged.
  std::unique_ptr<ActiveTabObserver> active_tab_observer_;
  // mirrors the value PushSidebarUrl last pushed - see
  // LastPushedSidebarOriginForTesting().
  std::string last_pushed_sidebar_origin_;
  // Whether a run is in progress, and its monotonic max fraction. A mid-load
  // navigation resets Chromium's own progress to a low value, and the
  // fraction must never travel backward.
  bool load_phase_live_ = false;
  double load_fraction_ = 0.0;
  // See SetLoadPhasePushRecordingForTesting.
  bool record_load_phase_pushes_for_testing_ = false;
  std::vector<std::pair<atrium::LoadPhase, double>>
      load_phase_pushes_for_testing_;

  // Link-hover status bubble state. kHidden -> kPendingShow
  // kStatusBubbleShowDelay lets a quick mouse pass over a link raise nothing.
  // re-debounce (Safari-style live retarget link-to-link).
  // The hovered URL's debounce and the URL it is showing. The card is drawn by
  // hover_url_widget_, so nothing here paints or fades any more.
  GURL status_bubble_pending_url_;
  GURL status_bubble_shown_url_;
  base::OneShotTimer status_bubble_show_timer_;
  // when non-empty, the bubble paints this text verbatim (a transient
  // confirmation) instead of the elided hover URL. Cleared whenever the hover
  // path shows a URL or the bubble is force-hidden / fully faded.

  // HTML sidebar arm. The WebContents is owned here; sidebar_web_
  // is the view that displays it and is owned by the container.
  std::unique_ptr<content::WebContents> sidebar_contents_;
  // The last JSON SetSidebarExtensionActionsJSON received, for
  // SidebarExtensionActionsJSONForTesting.
  std::string last_sidebar_extension_actions_json_;
  // One entry per profile that has pushed, replayed to a rebuilt sidebar so its
  // rail panes draw the pins of the profiles they belong to. Nothing erases an
  // entry - this window observes no profile removal.
  std::map<std::string, std::string> sidebar_extension_actions_by_profile_;
  // See LastExtensionActionsController's own doc.
  raw_ptr<BrowserController> last_extension_actions_controller_ = nullptr;

  // the /foyer route's WebContents, on a little window only. Owned
  // here for the same reason sidebar_contents_ is: the slot holds the WebView,
  // and a WebView does not own the contents it shows.
  std::unique_ptr<content::WebContents> foyer_contents_;
  raw_ptr<views::WebView> sidebar_web_ = nullptr;

  // A pool that never shrinks: a bar past the pane count only hides.
  std::vector<std::unique_ptr<AtriumDevBar>> dev_bars_;
  // The Cmd+Shift+D toggle this window remembers between navigations.
  bool dev_mode_on_ = false;
  size_t shown_dev_bar_count_ = 0;
  raw_ptr<AtriumDevBar> dev_bar_sender_ = nullptr;
  // The pane an edit-at-anchor open was clicked from, since dev_bar_sender_'s
  // AutoReset ends before the user types. A destroyed pane tab clears itself.
  base::WeakPtr<content::WebContents> command_bar_target_tab_;
  // True for the span of an edit-at-anchor OpenCommandBar call, so the
  // one-arg overload it funnels through (the same one Cmd+T/Cmd+L use) knows
  // not to wipe the target HandleAddressRowClicked just set.
  bool command_bar_target_tab_pending_ = false;

  // the notes split pane's WebContents, owned here for the same
  // reason sidebar_contents_ is - see OpenNotesBesideThisPage's doc for why
  // this is rebuilt (not reused) on every open.
  std::unique_ptr<content::WebContents> notes_contents_;

  // Mirror View's split secondary - not tab-strip owned, since
  // its profile can differ from this window's own TabStripModel.
  std::unique_ptr<content::WebContents> mirror_contents_;

  // true only for the fresh OTR profile Logged out creates -
  // EndMirror destroys that one but never a named space's own profile.
  bool mirror_profile_is_otr_ = false;

  // Empty shows the active space's own pad. Non-empty names a freestanding
  // note shown in its place instead. OpenNotesBesideThisPage clears it.
  std::string current_note_id_;

  // the ai-panel route's WebContents, owned here for the same
  // reason sidebar_contents_ is.
  std::unique_ptr<content::WebContents> ai_panel_contents_;
  raw_ptr<views::WebView> ai_panel_web_ = nullptr;
  // True once the JS side has opened the panel (aiPanelToggled). Gates
  // whether ai_panel_width_ is currently applied to the container.
  bool ai_panel_open_ = false;
  // Set by ToggleAiPanel's opening branch, consumed by SetAiPanelOpen once
  // the container has actually widened - RequestFocus() before that raced
  // the width change. Never set by an implicit (tab-switch) reopen.
  bool ai_panel_focus_pending_ = false;
  // The last width the JS side reported, applied to the container while
  // ai_panel_open_ and remembered across a close/reopen. 280 matches
  // ai-panel.js's own state.width default.
  int ai_panel_width_ = 280;
  raw_ptr<AtriumSidePanelUI> extension_panel_owner_ = nullptr;
  std::string extension_panel_id_;
  // Survives a close, so the verb and the menu can reopen it.
  std::string last_extension_panel_id_;
  int extension_panel_width_ = 0;

  base::WeakPtrFactory<AtriumBrowserWindow> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_WINDOW_H_
