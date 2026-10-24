// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_bridge.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_BRIDGE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_BRIDGE_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_extension_actions_json.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_action_icon_factory.h"

class Browser;
class Profile;

namespace extensions {
class Extension;
class ExtensionAction;
}  // namespace extensions

namespace atrium {

class AtriumBrowserWindow;
class BrowserController;

// Observes the profile's ToolbarActionsModel and streams the extension
// toolbar-action state for the owning window's ACTIVE tab to the AtriumUI
// dylib (AtriumOnExtensionActionsChanged / AtriumOnExtensionActionIcon; see
// the retired ABI header's EXTENSIONS doc block). Built at host registration for
// every non-little window; one per BrowserController, owned by it and reset in
// OnTabStripModelDestroyed (before browser_ is nulled).
//
// Badge/visible state is PER-TAB and resolved strictly here, C++-side, from
// the active WebContents' session tab id - it never crosses the ABI as a
// separate id, only as the snapshot's already-resolved per-tab values. A plain
// OnToolbarActionUpdated drops which WebContents it is about to affect, so the
// bridge always rebuilds the WHOLE snapshot rather than patching it; the
// controller also calls ResyncActiveTab() on tab activation and active-tab
// navigation for the same reason. The rebuilt snapshot IS diff-gated before
// crossing the ABI (push_gate_) - see
// atrium_extension_actions_json.h's ExtensionActionsPushGate for the
// same-tab/tab-switch semantics.
class ExtensionActionBridge : public ToolbarActionsModel::Observer,
                              public extensions::ExtensionActionIconFactory::
                                  Observer {
 public:
  ExtensionActionBridge(Browser* browser);
  ExtensionActionBridge(const ExtensionActionBridge&) = delete;
  ExtensionActionBridge& operator=(const ExtensionActionBridge&) = delete;
  ~ExtensionActionBridge() override;

  // Safe to call any time the bridge is alive. `force` pushes an unchanged
  // snapshot too: after a cross-profile in-window swap it must reach a
  // different window than the last push did, which the diff gate cannot see.
  void ResyncActiveTab(bool force = false);

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;

  // extensions::ExtensionActionIconFactory::Observer: OnIconUpdated carries no
  // id, so each factory gets its own per-extension adapter (IconEntry) that
  // forwards to PushIcon with the id. This inherited override is never the one
  // that fires in practice - it exists only because the base class is
  // abstract - so it re-pushes every icon defensively.
  void OnIconUpdated() override;

 private:
  // Owns one ExtensionActionIconFactory plus the per-extension Observer adapter
  // it needs (the factory's observer must outlive it; declaring `factory` last
  // guarantees it is destroyed first). Keyed by extension id in icon_entries_.
  struct IconEntry : public extensions::ExtensionActionIconFactory::Observer {
    IconEntry(ExtensionActionBridge* bridge, std::string id);
    ~IconEntry() override;
    void OnIconUpdated() override;

    raw_ptr<ExtensionActionBridge> bridge;
    std::string id;
    // ExtensionActionSnapshot::icon_rev for this id.
    int rev = 0;
    uint32_t last_hash = 0;
    std::unique_ptr<extensions::ExtensionActionIconFactory> factory;
  };

  // Reconciles icon_entries_ against `present_ids`: builds a factory for each
  // newly-present extension (pushing its current icon once) and drops entries
  // for extensions no longer present.
  void SyncIconFactories(const base::flat_set<std::string>& present_ids);
  // Encodes the extension's default action icon and stores it for `id`.
  // Returns true when the bytes differ from this bridge's last push.
  bool PushIcon(const std::string& id);

  // Forwards the same snapshot to the HTML surfaces. No-op
  // unless a surface is enabled and this browser is presented.
  void PushActionsToHtmlArm(const std::string& json);

 public:
  // The gate PushActionsToHtmlArm applies, for a caller with its own target
  // window. Records `controller` on `window`, see LastExtensionActionsController.
  static void PushActionsToWindow(AtriumBrowserWindow* window,
                                  BrowserController* controller,
                                  const std::string& json);

  // The current snapshot, for a surface that missed the push. A chrome-surface
  // widget is built asynchronously (the chrome context lands ~670ms after
  // startup), by which time this Browser's push has already happened and the
  // diff gate will not repeat it.
  const std::string& CurrentActionsJSON() const {
    return push_gate_.last_json();
  }

 private:

  const raw_ptr<Browser> browser_;
  const raw_ptr<Profile> profile_;
  raw_ptr<ToolbarActionsModel> model_;

  std::map<std::string, std::unique_ptr<IconEntry>> icon_entries_;

  // Skips a ResyncActiveTab() push when it would resend the same content for
  // the same active tab (see the class comment above and
  // ExtensionActionsPushGate's own comment for the tab-switch rule).
  ExtensionActionsPushGate push_gate_;

  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      model_observation_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_BRIDGE_H_
