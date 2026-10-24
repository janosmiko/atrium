// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDE_PANEL_UI_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDE_PANEL_UI_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui_base.h"
#include "extensions/common/extension_id.h"

class Browser;

namespace views {
class View;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// Chromium's side panel model for an Atrium Browser, after Webium's
// WebUIBrowserSidePanelUI. AtriumBrowserWindow is not a BrowserView, so this
// hands each entry's view to whichever window displays the Browser, and keeps
// the view while no window does. Only extension entries show.
class AtriumSidePanelUI : public SidePanelUIBase {
 public:
  explicit AtriumSidePanelUI(Browser* browser);
  AtriumSidePanelUI(const AtriumSidePanelUI&) = delete;
  AtriumSidePanelUI& operator=(const AtriumSidePanelUI&) = delete;
  ~AtriumSidePanelUI() override;

  // Null for a Browser that browser_window_features.cc gave no Atrium UI.
  static AtriumSidePanelUI* From(Browser* browser);

  // SidePanelUI:
  using SidePanelUIBase::Close;
  using SidePanelUIBase::Show;
  void Show(SidePanelEntry::Key entry_key,
            std::optional<SidePanelOpenTrigger> open_trigger,
            bool suppress_animations) override;
  void Close(SidePanelEntryHideReason reason,
             bool suppress_animations) override;
  void Toggle(SidePanelEntryKey key,
              SidePanelOpenTrigger open_trigger) override;
  void ShowFrom(SidePanelEntryKey entry_key,
                gfx::Rect starting_bounds_in_browser_coordinates) override;
  content::WebContents* GetWebContentsForTest(SidePanelEntryId id) override;
  void DisableAnimationsForTesting() override;
  void SetNoDelaysForTesting(bool no_delays_for_testing) override;

  // Takes the view back from a window that stops displaying this Browser.
  void ReclaimView(std::unique_ptr<views::View> view);
  // Moves the open panel into `window`, which now displays this Browser.
  void PresentIn(AtriumBrowserWindow* window);
  std::optional<extensions::ExtensionId> CurrentExtensionId() const;

 private:
  // SidePanelUIBase:
  void Show(const UniqueKey& entry,
            std::optional<SidePanelOpenTrigger> open_trigger,
            bool suppress_animations) override;
  void PopulateSidePanel(
      bool suppress_animations,
      const UniqueKey& unique_key,
      std::optional<SidePanelOpenTrigger> open_trigger,
      SidePanelEntry* entry,
      std::optional<std::unique_ptr<views::View>> content_view) override;
  void MaybeShowEntryOnTabStripModelChanged(
      SidePanelRegistry* old_contextual_registry,
      SidePanelRegistry* new_contextual_registry) override;

  void Attach(std::unique_ptr<views::View> view);
  std::unique_ptr<views::View> TakeView();

  raw_ptr<Browser> atrium_browser_;
  // Set while a window hosts the view. Otherwise held_view_ owns it.
  raw_ptr<AtriumBrowserWindow> host_ = nullptr;
  std::unique_ptr<views::View> held_view_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDE_PANEL_UI_H_
