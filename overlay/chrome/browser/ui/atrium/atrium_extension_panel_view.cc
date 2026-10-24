// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_extension_panel_view.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_window_metrics.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/vector_icons/vector_icons.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_utils.h"

namespace atrium {
namespace {

// Native code cannot read tokens.css, so these copy --atrium-row-min-height,
// --atrium-row-icon-size and --atrium-content-edge-inset.
constexpr int kHeaderHeight = 30;
constexpr int kIconSize = 17;
constexpr int kEdgeInset = 10;
constexpr int kHeaderSpacing = 8;
constexpr int kCloseIconSize = 16;
// Views fonts take whole points, so the 13.5px row font rounds down.
constexpr int kTitleFontSize = 13;

}  // namespace

AtriumExtensionPanelView::AtriumExtensionPanelView() {
  auto column = std::make_unique<views::View>();
  // The rounded layer clips the header. The page is native and needs its own
  // corners, see Layout().
  column->SetPaintToLayer();
  column->layer()->SetFillsBoundsOpaquely(false);
  column->layer()->SetIsFastRoundedCorner(true);
  // The page card's surface, so the header reads as part of the same card.
  column->SetBackground(
      views::CreateSolidBackground(kColorWebContentsBackground));
  auto* column_layout =
      column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  column_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto header = std::make_unique<views::View>();
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kEdgeInset), kHeaderSpacing));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  header_layout->set_minimum_cross_axis_size(kHeaderHeight);
  icon_ = header->AddChildView(std::make_unique<views::ImageView>());
  icon_->SetImageSize(gfx::Size(kIconSize, kIconSize));
  title_ = header->AddChildView(std::make_unique<views::Label>());
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_->SetFontList(gfx::FontList().DeriveWithSizeDelta(
      kTitleFontSize - gfx::FontList().GetFontSize()));
  header_layout->SetFlexForView(title_, 1);
  close_button_ =
      header->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
          base::BindRepeating(&AtriumExtensionPanelView::OnClosePressed,
                              base::Unretained(this)),
          vector_icons::kCloseIcon, kCloseIconSize));
  close_button_->SetTooltipText(u"Close side panel");
  column->AddChildView(std::move(header));

  slot_ = column->AddChildView(std::make_unique<views::View>());
  slot_->SetLayoutManager(std::make_unique<views::FillLayout>());
  column_layout->SetFlexForView(slot_, 1);

  column_ = AddChildView(std::move(column));

  GetViewAccessibility().SetRole(ax::mojom::Role::kComplementary);
  // Stays visible at width 0 when closed, like the AI panel container. A
  // hidden child keeps stale bounds in the root's BoxLayout.
  SetPreferredSize(gfx::Size(0, 1));
  UpdateChildVisibility();
}

AtriumExtensionPanelView::~AtriumExtensionPanelView() = default;

void AtriumExtensionPanelView::SetContent(
    std::unique_ptr<views::View> content) {
  // One panel per window. The caller detaches the old view first.
  CHECK(!content_);
  content_ = slot_->AddChildView(std::move(content));
  UpdateChildVisibility();
  InvalidateLayout();
}

std::unique_ptr<views::View> AtriumExtensionPanelView::TakeContent() {
  if (!content_) {
    return nullptr;
  }
  views::View* content = content_;
  content_ = nullptr;
  std::unique_ptr<views::View> taken = slot_->RemoveChildViewT(content);
  UpdateChildVisibility();
  return taken;
}

void AtriumExtensionPanelView::SetGrip(std::unique_ptr<views::View> grip) {
  CHECK(!grip_);
  // The last child hit-tests first, so the band still drags where a narrow
  // gutter lets it overlap the card.
  grip_ = AddChildView(std::move(grip));
  UpdateChildVisibility();
}

void AtriumExtensionPanelView::SetCloseCallback(
    base::RepeatingClosure on_close) {
  on_close_ = std::move(on_close);
}

void AtriumExtensionPanelView::SetPanelOnRight(bool on_right) {
  panel_on_right_ = on_right;
  InvalidateLayout();
}

void AtriumExtensionPanelView::SetCardShape(int inset, float corner_radius) {
  card_inset_ = inset;
  card_radius_ = corner_radius;
  column_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(corner_radius));
  InvalidateLayout();
}

void AtriumExtensionPanelView::SetImmersive(bool immersive) {
  immersive_ = immersive;
  InvalidateLayout();
}

void AtriumExtensionPanelView::Layout(PassKey) {
  // Left-docked, the panel sits under the traffic lights, so the card clears
  // the titlebar band the same way the page card does.
  const int top = panel_on_right_ || immersive_
                      ? card_inset_
                      : static_cast<int>(kTitlebarHeight);
  gfx::Rect card = GetLocalBounds();
  card.Inset(gfx::Insets::TLBR(top, card_inset_, card_inset_, card_inset_));
  column_->SetBoundsRect(card);
  // The grip stays on the edge that faces the page, in the gutter.
  if (grip_) {
    const int grip_width = grip_->GetPreferredSize().width();
    grip_->SetBounds(panel_on_right_ ? 0 : width() - grip_width, 0, grip_width,
                     height());
  }
  // A native page ignores the card's clip. The header covers its top edge,
  // so only the bottom corners round.
  if (auto* web_view = views::AsViewClass<views::WebView>(content_.get())) {
    web_view->holder()->SetCornerRadii(
        gfx::RoundedCornersF(0, 0, card_radius_, card_radius_));
  }
}

void AtriumExtensionPanelView::SetExtension(
    content::BrowserContext* context,
    const extensions::Extension* extension,
    const std::string& extension_id) {
  icon_image_.reset();
  if (!extension) {
    title_->SetText(base::UTF8ToUTF16(extension_id));
    icon_->SetImage(ui::ImageModel::FromImageSkia(
        extensions::util::GetDefaultExtensionIcon()));
  } else {
    title_->SetText(base::UTF8ToUTF16(extension->name()));
    icon_image_ = std::make_unique<extensions::IconImage>(
        context, extension, extensions::IconsInfo::GetIcons(extension),
        kIconSize, extensions::util::GetDefaultExtensionIcon(), this);
    // An IconImage loads a scale only when something asks for it.
    icon_image_->image_skia().EnsureRepsForSupportedScales();
    OnExtensionIconImageChanged(icon_image_.get());
  }
  GetViewAccessibility().SetName(title());
}

std::u16string AtriumExtensionPanelView::title() const {
  return std::u16string(title_->GetText());
}

void AtriumExtensionPanelView::OnExtensionIconImageChanged(
    extensions::IconImage* image) {
  icon_->SetImage(ui::ImageModel::FromImageSkia(image->image_skia()));
}

void AtriumExtensionPanelView::OnClosePressed() {
  if (on_close_) {
    on_close_.Run();
  }
}

void AtriumExtensionPanelView::UpdateChildVisibility() {
  const bool open = content_ != nullptr;
  column_->SetVisible(open);
  if (grip_) {
    grip_->SetVisible(open);
  }
}

BEGIN_METADATA(AtriumExtensionPanelView)
END_METADATA

}  // namespace atrium
