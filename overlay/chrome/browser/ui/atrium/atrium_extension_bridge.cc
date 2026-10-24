// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_bridge.cc
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_extension_bridge.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_extension_actions_json.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace atrium {

namespace {

// The session tab id ExtensionAction keys its per-tab state by. Falls back to
// kDefaultTabId (the tab-independent default appearance) when there is no
// active tab, so a window showing only cold/no tabs still yields a valid
// default snapshot.
int ActiveTabId(Browser* browser) {
  content::WebContents* active =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!active) {
    return extensions::ExtensionAction::kDefaultTabId;
  }
  return sessions::SessionTabHelper::IdForTab(active).id();
}

bool IsFullyTransparent(const SkBitmap& bitmap) {
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      if (SkColorGetA(bitmap.getColor(x, y)) != 0) {
        return false;
      }
    }
  }
  return true;
}

// Encodes `image`'s largest pixel representation as PNG - the favicon
// pipeline's precedent (TabEventForwarder::PushFavicon): the 1x rep upscaled
// onto a Retina chip looks blurry, so pick the biggest bitmap and let the UI
// downscale. Empty on any failure.
std::optional<std::vector<uint8_t>> EncodeLargestRep(const gfx::Image& image) {
  if (image.IsEmpty()) {
    return std::nullopt;
  }
  const gfx::ImageSkia skia = image.AsImageSkia();
  const std::vector<gfx::ImageSkiaRep> reps = skia.image_reps();
  const gfx::ImageSkiaRep* best = nullptr;
  for (const gfx::ImageSkiaRep& rep : reps) {
    // An extension IconImage fills a scale that is still loading with a
    // transparent rep of full size. On a 1x+2x display the blank 2x would win
    // over a loaded 1x.
    if (IsFullyTransparent(rep.GetBitmap())) {
      continue;
    }
    if (!best || rep.pixel_width() > best->pixel_width()) {
      best = &rep;
    }
  }
  if (!best || best->pixel_width() <= 0) {
    return std::nullopt;
  }
  return gfx::PNGCodec::EncodeBGRASkBitmap(best->GetBitmap(),
                                           /*discard_transparency=*/false);
}

}  // namespace

ExtensionActionBridge::IconEntry::IconEntry(ExtensionActionBridge* bridge,
                                            std::string id)
    : bridge(bridge), id(std::move(id)) {}

ExtensionActionBridge::IconEntry::~IconEntry() = default;

void ExtensionActionBridge::IconEntry::OnIconUpdated() {
  if (bridge->PushIcon(id)) {
    bridge->ResyncActiveTab();
  }
}

ExtensionActionBridge::ExtensionActionBridge(Browser* browser)
    : browser_(browser),
      profile_(browser->profile()),
      model_(ToolbarActionsModel::Get(browser->profile())) {
  if (model_) {
    model_observation_.Observe(model_);
  }
  ResyncActiveTab();
}

ExtensionActionBridge::~ExtensionActionBridge() = default;

void ExtensionActionBridge::ResyncActiveTab(bool force) {
  const int tab_id = ActiveTabId(browser_);

  if (!model_) {
    const std::string json = BuildExtensionActionsJSON({});
    if (push_gate_.ShouldPush(tab_id, json) || force) {
      PushActionsToHtmlArm(json);
    }
    return;
  }

  extensions::ExtensionActionManager* manager =
      extensions::ExtensionActionManager::Get(profile_);
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile_);

  std::vector<ExtensionActionSnapshot> snapshots;
  base::flat_set<std::string> present_ids;
  if (manager && registry) {
    for (const ToolbarActionsModel::ActionId& id : model_->action_ids()) {
      const extensions::Extension* extension =
          registry->enabled_extensions().GetByID(id);
      if (!extension) {
        continue;
      }
      extensions::ExtensionAction* action =
          manager->GetExtensionAction(*extension);
      if (!action) {
        continue;
      }
      ExtensionActionSnapshot snapshot;
      snapshot.id = id;
      snapshot.name = base::UTF16ToUTF8(model_->GetExtensionName(id));
      snapshot.visible = action->GetIsVisible(tab_id);
      snapshot.has_popup = action->HasPopup(tab_id);
      snapshot.pinned = model_->IsActionPinned(id);
      snapshot.action_title = action->GetTitle(tab_id);
      snapshot.badge_text = action->GetDisplayBadgeText(tab_id);
      // SkColor is uint32 in 0xAARRGGBB order - the same ARGB byte order the
      // ABI documents for badgeBg/badgeTextColor.
      snapshot.badge_bg = action->GetBadgeBackgroundColor(tab_id);
      snapshot.badge_text_color = action->GetBadgeTextColor(tab_id);
      snapshots.push_back(std::move(snapshot));
      present_ids.insert(id);
    }
  }

  SyncIconFactories(present_ids);
  for (ExtensionActionSnapshot& snapshot : snapshots) {
    auto entry = icon_entries_.find(snapshot.id);
    if (entry != icon_entries_.end()) {
      snapshot.icon_rev = entry->second->rev;
    }
    snapshot.icon_scope = ExtIconScopeFor(profile_);
  }
  const std::string json = BuildExtensionActionsJSON(snapshots);
  if (push_gate_.ShouldPush(tab_id, json) || force) {
    PushActionsToHtmlArm(json);
  }
}

void ExtensionActionBridge::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  ResyncActiveTab();
}

void ExtensionActionBridge::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  // Drop this extension's icon factory before its ExtensionAction can dangle
  // (the factory holds a raw_ptr to it); the rebuild below then re-pushes the
  // snapshot without it.
  icon_entries_.erase(id);
  ResyncActiveTab();
}

void ExtensionActionBridge::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  ResyncActiveTab();
}

void ExtensionActionBridge::OnToolbarModelInitialized() {
  ResyncActiveTab();
}

void ExtensionActionBridge::OnToolbarPinnedActionsChanged() {
  // The snapshot carries `pinned`, so a pin toggle changes it and the strip
  // has to be told.
  ResyncActiveTab();
}

void ExtensionActionBridge::OnIconUpdated() {
  bool changed = false;
  for (const auto& [id, entry] : icon_entries_) {
    changed |= PushIcon(id);
  }
  if (changed) {
    ResyncActiveTab();
  }
}

void ExtensionActionBridge::SyncIconFactories(
    const base::flat_set<std::string>& present_ids) {
  // Prune entries for extensions that vanished from the action set.
  for (auto it = icon_entries_.begin(); it != icon_entries_.end();) {
    if (!present_ids.contains(it->first)) {
      it = icon_entries_.erase(it);
    } else {
      ++it;
    }
  }

  extensions::ExtensionActionManager* manager =
      extensions::ExtensionActionManager::Get(profile_);
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile_);
  if (!manager || !registry) {
    return;
  }
  for (const std::string& id : present_ids) {
    if (icon_entries_.count(id)) {
      continue;
    }
    const extensions::Extension* extension =
        registry->enabled_extensions().GetByID(id);
    if (!extension) {
      continue;
    }
    extensions::ExtensionAction* action =
        manager->GetExtensionAction(*extension);
    if (!action) {
      continue;
    }
    auto entry = std::make_unique<IconEntry>(this, id);
    // The factory's Observer (the IconEntry) must outlive it - guaranteed by
    // IconEntry's member order (factory declared last, destroyed first).
    entry->factory = std::make_unique<extensions::ExtensionActionIconFactory>(
        extension, action, entry.get());
    icon_entries_[id] = std::move(entry);
    // Push whatever the factory already holds; the observer covers later
    // async resolves.
    PushIcon(id);
  }
}


void ExtensionActionBridge::PushActionsToHtmlArm(const std::string& json) {
  // The HTML arm gets the SAME snapshot, from the same place - no second
  // source of truth. A no-op unless this browser's own tab is on screen
  // somewhere.
  if (!browser_) {
    return;
  }
  // Several profiles' Browsers share a slot, so "is this window mine" answered
  // a cross-profile in-window swap backwards: the presenter passed it while
  // describing its own hidden tab. Displayed nowhere still means skip.
  AtriumBrowserWindow* window =
      BrowserController::DisplayingWindowForBrowser(browser_).value_or(nullptr);
  PushActionsToWindow(window, BrowserController::ControllerForBrowser(browser_),
                       json);
}

// static
void ExtensionActionBridge::PushActionsToWindow(AtriumBrowserWindow* window,
                                                 BrowserController* controller,
                                                 const std::string& json) {
  // A controller with no bridge yet hands out an empty string, and every
  // surface below runs it through JSON.parse, which throws on one.
  if (!window || json.empty()) {
    return;
  }
  window->SetLastExtensionActionsController(controller);
  // Empty for an incognito controller: its profile_id_ is its parent's
  // (BrowserController::IsOffTheRecordProfile), so keying by it would file the
  // incognito window's pins under the parent profile's strip.
  const std::string profile_id =
      controller && !controller->IsOffTheRecordProfile() ? controller->ProfileId()
                                                         : std::string();
  window->SetSidebarExtensionActionsJSON(json, profile_id);
  window->SetDevBarExtensionActionsJSON(json);
  window->SetQuickMenuActionsJSON(json);
}

bool ExtensionActionBridge::PushIcon(const std::string& id) {
  auto it = icon_entries_.find(id);
  if (it == icon_entries_.end() || !it->second->factory) {
    return false;
  }
  // Icons are keyed by extension id process-wide (see the retired ABI header) - use
  // the default (tab-independent) icon, not a per-tab setIcon override.
  const gfx::Image icon =
      it->second->factory->GetIcon(extensions::ExtensionAction::kDefaultTabId);
  // An IconImage loads a scale only when a caller asks for it, and no native
  // toolbar asks here. Without this call image_reps() stays empty and every
  // chip gets a 404. The load finishes later through OnIconUpdated.
  if (!icon.IsEmpty()) {
    icon.AsImageSkia().EnsureRepsForSupportedScales();
  }
  const std::optional<std::vector<uint8_t>> png = EncodeLargestRep(icon);
  if (!png || png->empty()) {
    return false;
  }
  // Cached here, not base64-inlined into the action snapshot, which is
  // re-pushed on every tab switch. The WebUI arm serves these bytes at
  // chrome://atrium-exticon/<scope>/<id>.png.
  // Compared with this bridge's own last push, not with the cache. Every
  // Browser of the profile shares the cache, so a second bridge would see the
  // bytes already there and never bump the rev its own surfaces read.
  const uint32_t hash = base::FastHash(*png);
  if (it->second->rev > 0 && it->second->last_hash == hash) {
    return false;
  }
  IconCache::ExtFor(profile_).Store(id, *png);
  it->second->last_hash = hash;
  ++it->second->rev;
  return true;
}

}  // namespace atrium
