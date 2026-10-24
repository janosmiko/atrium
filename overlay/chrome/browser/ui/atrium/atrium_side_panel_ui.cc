// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_side_panel_ui.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_feature_check.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_waiter.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/views/view.h"

namespace atrium {

AtriumSidePanelUI::AtriumSidePanelUI(Browser* browser)
    : SidePanelUIBase(browser), atrium_browser_(browser) {}

AtriumSidePanelUI::~AtriumSidePanelUI() {
  // The window must not keep a view whose owner is gone.
  if (host_) {
    host_->DetachExtensionPanel();
  }
}

// static
AtriumSidePanelUI* AtriumSidePanelUI::From(Browser* browser) {
  // The gate browser_window_features.cc creates this under, so the cast never
  // meets a stock SidePanelCoordinator.
  if (!browser || !IsAtriumBrowserWindowEnabled() ||
      !browser->is_type_normal()) {
    return nullptr;
  }
  return static_cast<AtriumSidePanelUI*>(SidePanelUI::From(browser));
}

void AtriumSidePanelUI::Show(SidePanelEntry::Key entry_key,
                             std::optional<SidePanelOpenTrigger> open_trigger,
                             bool suppress_animations) {
  // The base class CHECKs here, and stock commands such as bookmarks ask for
  // entries this window never registers.
  std::optional<UniqueKey> unique_key = GetUniqueKeyForKey(entry_key);
  if (!unique_key) {
    VLOG(1) << "Atrium: side panel entry not registered "
            << entry_key.ToString();
    return;
  }
  Show(*unique_key, open_trigger, suppress_animations);
}

void AtriumSidePanelUI::Show(const UniqueKey& input,
                             std::optional<SidePanelOpenTrigger> open_trigger,
                             bool suppress_animations) {
  // The slot has an extension header, and a tab-scoped stock entry (Lens,
  // reading mode) can be registered here too.
  if (input.key.id() != SidePanelEntryId::kExtension) {
    VLOG(1) << "Atrium: side panel entry ignored " << input.key.ToString();
    return;
  }
  if (IsSidePanelShowing() && *current_key() == input) {
    waiter()->ResetLoadingEntryIfNecessary();
    return;
  }
  waiter()->WaitForEntry(
      GetEntryForUniqueKey(input),
      base::BindOnce(&AtriumSidePanelUI::PopulateSidePanel,
                     base::Unretained(this), suppress_animations, input,
                     open_trigger));
}

void AtriumSidePanelUI::PopulateSidePanel(
    bool suppress_animations,
    const UniqueKey& unique_key,
    std::optional<SidePanelOpenTrigger> open_trigger,
    SidePanelEntry* entry,
    std::optional<std::unique_ptr<views::View>> content_view) {
  // No controller owns this Browser: keep the view for the next open and show
  // nothing.
  if (!BrowserController::DisplayingWindowForBrowser(atrium_browser_)) {
    if (content_view) {
      entry->CacheView(std::move(*content_view));
    }
    return;
  }
  entry->set_last_open_trigger(open_trigger);

  SidePanelEntry* previous_entry =
      IsSidePanelShowing() ? GetEntryForUniqueKey(*current_key()) : nullptr;
  if (previous_entry) {
    previous_entry->OnEntryWillHide(
        open_trigger == SidePanelOpenTrigger::kTabChanged
            ? SidePanelEntryHideReason::kBackgrounded
            : SidePanelEntryHideReason::kReplaced);
    // Cached, not destroyed: destroying it fires that extension's onClosed.
    if (std::unique_ptr<views::View> previous_view = TakeView()) {
      previous_entry->CacheView(std::move(previous_view));
    }
  }

  std::unique_ptr<views::View> view =
      content_view ? std::move(*content_view) : entry->GetContent();
  if (auto* contextual_registry = GetActiveContextualRegistry()) {
    contextual_registry->ResetActiveEntry();
  }
  SetCurrentKey(unique_key);
  Attach(std::move(view));
  entry->OnEntryShown();
  if (previous_entry) {
    previous_entry->OnEntryHidden();
  }
  NotifyShownCallbacks();
}

void AtriumSidePanelUI::MaybeShowEntryOnTabStripModelChanged(
    SidePanelRegistry* old_contextual_registry,
    SidePanelRegistry* new_contextual_registry) {
  if (IsSidePanelShowing()) {
    if (std::optional<UniqueKey> unique_key = GetNewActiveKeyOnTabChanged()) {
      Show(*unique_key, SidePanelOpenTrigger::kTabChanged,
           /*suppress_animations=*/true);
      return;
    }
    // The old tab's own panel: cache it for when that tab comes back.
    std::optional<SidePanelEntry*> old_entry =
        old_contextual_registry ? old_contextual_registry->GetActiveEntry()
                                : std::nullopt;
    if (old_entry.has_value() && current_key()->tab_handle &&
        (*old_entry)->key() == current_key()->key) {
      if (std::unique_ptr<views::View> view = TakeView()) {
        (*old_entry)->CacheView(std::move(view));
      }
    }
    Close(SidePanelEntryHideReason::kBackgrounded,
          /*suppress_animations=*/true);
    return;
  }
  std::optional<SidePanelEntry*> new_entry =
      new_contextual_registry ? new_contextual_registry->GetActiveEntry()
                              : std::nullopt;
  if (new_entry.has_value()) {
    Show(UniqueKey{browser()->GetActiveTabInterface()->GetHandle(),
                   (*new_entry)->key()},
         SidePanelOpenTrigger::kTabChanged, /*suppress_animations=*/true);
  }
}

void AtriumSidePanelUI::Close(SidePanelEntryHideReason reason,
                              bool suppress_animations) {
  if (!IsSidePanelShowing()) {
    return;
  }
  SidePanelEntry* entry = GetEntryForUniqueKey(*current_key());
  if (entry) {
    entry->OnEntryWillHide(reason);
  }
  // No close animation, so the stock coordinator's second half (its
  // OnViewVisibilityChanged) runs here at once.
  std::unique_ptr<views::View> view = TakeView();
  SetCurrentKey(std::nullopt);
  if (entry) {
    entry->OnEntryHidden();
  }
  if (auto* contextual_registry = GetActiveContextualRegistry()) {
    contextual_registry->ResetActiveEntry();
  }
  SidePanelRegistry::From(browser())->ResetActiveEntry();
  // A panel this one replaced keeps its page alive in the cache. A tab whose
  // own panel is still active keeps its cached view for when it comes back.
  SidePanelRegistry::From(browser())->ClearCachedEntryViews();
  for (tabs::TabInterface* tab : *browser()->GetTabStripModel()) {
    SidePanelRegistry::From(tab)->ClearCachedEntryViews();
  }
  // Destroying the view is what fires the extension's onClosed.
  view.reset();
}

void AtriumSidePanelUI::Toggle(SidePanelEntryKey key,
                               SidePanelOpenTrigger open_trigger) {
  if (IsSidePanelEntryShowing(key)) {
    Close(SidePanelEntryHideReason::kSidePanelClosed,
          /*suppress_animations=*/true);
    return;
  }
  SidePanelEntry* entry = GetActiveContextualEntryForKey(key);
  if (!entry) {
    entry = SidePanelRegistry::From(browser())->GetEntryForKey(key);
  }
  if (entry && IsSidePanelShowing() && waiter()->loading_entry() == entry) {
    waiter()->ResetLoadingEntryIfNecessary();
    Close(SidePanelEntryHideReason::kSidePanelClosed,
          /*suppress_animations=*/true);
    return;
  }
  if (std::optional<UniqueKey> unique_key = GetUniqueKeyForKey(key)) {
    Show(*unique_key, open_trigger, /*suppress_animations=*/false);
  }
}

void AtriumSidePanelUI::ShowFrom(
    SidePanelEntryKey entry_key,
    gfx::Rect starting_bounds_in_browser_coordinates) {
  SidePanelUI::Show(entry_key);
}

content::WebContents* AtriumSidePanelUI::GetWebContentsForTest(
    SidePanelEntryId id) {
  return nullptr;
}

void AtriumSidePanelUI::DisableAnimationsForTesting() {}

void AtriumSidePanelUI::SetNoDelaysForTesting(bool no_delays_for_testing) {
  waiter()->SetNoDelaysForTesting(no_delays_for_testing);  // IN-TEST
}

void AtriumSidePanelUI::ReclaimView(std::unique_ptr<views::View> view) {
  host_ = nullptr;
  held_view_ = std::move(view);
}

void AtriumSidePanelUI::PresentIn(AtriumBrowserWindow* window) {
  if (!IsSidePanelShowing() || host_ == window) {
    return;
  }
  std::unique_ptr<views::View> view = TakeView();
  if (!view) {
    return;
  }
  host_ = window;
  window->ShowExtensionPanel(std::move(view), this,
                             CurrentExtensionId().value_or(""));
}

std::optional<extensions::ExtensionId> AtriumSidePanelUI::CurrentExtensionId()
    const {
  if (!current_key()) {
    return std::nullopt;
  }
  return current_key()->key.extension_id();
}

void AtriumSidePanelUI::Attach(std::unique_ptr<views::View> view) {
  // nullptr: this Browser presents no pane right now (another profile's tab
  // is on screen), so the view waits for the swap that shows it.
  AtriumBrowserWindow* window =
      BrowserController::DisplayingWindowForBrowser(atrium_browser_)
          .value_or(nullptr);
  if (!window) {
    held_view_ = std::move(view);
    return;
  }
  host_ = window;
  window->ShowExtensionPanel(std::move(view), this,
                             CurrentExtensionId().value_or(""));
}

std::unique_ptr<views::View> AtriumSidePanelUI::TakeView() {
  if (host_) {
    AtriumBrowserWindow* host = host_;
    host_ = nullptr;
    return host->DetachExtensionPanel();
  }
  return std::move(held_view_);
}

}  // namespace atrium
