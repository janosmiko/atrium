// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_VIEW_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "extensions/browser/extension_icon_image.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace extensions {
class Extension;
}  // namespace extensions

namespace views {
class Button;
class ImageView;
class Label;
}  // namespace views

namespace atrium {

// The extension side panel's column in the root view: the resize grip on the
// edge that faces the page, then a card with a header over the content. The
// content view belongs to the Browser's AtriumSidePanelUI, this view only
// hosts it.
class AtriumExtensionPanelView : public views::View,
                                 public extensions::IconImage::Observer {
  METADATA_HEADER(AtriumExtensionPanelView, views::View)

 public:
  AtriumExtensionPanelView();
  AtriumExtensionPanelView(const AtriumExtensionPanelView&) = delete;
  AtriumExtensionPanelView& operator=(const AtriumExtensionPanelView&) =
      delete;
  ~AtriumExtensionPanelView() override;

  void SetContent(std::unique_ptr<views::View> content);
  std::unique_ptr<views::View> TakeContent();
  views::View* content() { return content_; }
  const views::View* content() const { return content_; }

  void SetGrip(std::unique_ptr<views::View> grip);
  void SetCloseCallback(base::RepeatingClosure on_close);
  void SetPanelOnRight(bool on_right);
  // The page card's gutter and corner radius. The window owns both values.
  void SetCardShape(int inset, float corner_radius);
  // Fullscreen hides the traffic lights, so the card needs no band under them.
  void SetImmersive(bool immersive);
  // A null `extension` shows the raw id and the default icon.
  void SetExtension(content::BrowserContext* context,
                    const extensions::Extension* extension,
                    const std::string& extension_id);

  views::View* card() { return column_; }
  views::View* grip() { return grip_; }
  views::Button* close_button() { return close_button_; }
  std::u16string title() const;

 private:
  // views::View:
  void Layout(PassKey) override;

  // extensions::IconImage::Observer:
  void OnExtensionIconImageChanged(extensions::IconImage* image) override;

  void OnClosePressed();
  // The panel stays visible at width 0, so its children hide instead.
  void UpdateChildVisibility();

  raw_ptr<views::View> column_ = nullptr;
  raw_ptr<views::View> grip_ = nullptr;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Button> close_button_ = nullptr;
  raw_ptr<views::View> slot_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  bool panel_on_right_ = true;
  bool immersive_ = false;
  int card_inset_ = 0;
  float card_radius_ = 0;
  base::RepeatingClosure on_close_;
  std::unique_ptr<extensions::IconImage> icon_image_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_VIEW_H_
