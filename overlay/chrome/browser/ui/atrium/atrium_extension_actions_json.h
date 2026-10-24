// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_actions_json.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_ACTIONS_JSON_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_ACTIONS_JSON_H_

#include <cstdint>
#include <string>
#include <vector>

namespace atrium {

// One extension's already-resolved, per-tab toolbar-action state. The bridge
// (atrium_extension_bridge.cc) reads these values off the live Chromium
// ExtensionAction for the active tab; this struct is the Browser-free,
// views-free hand-off so the JSON serialization stays a pure unit
// (slot_presentation_table's posture - see BUILD.gn).
struct ExtensionActionSnapshot {
  ExtensionActionSnapshot();
  ExtensionActionSnapshot(const ExtensionActionSnapshot&);
  ExtensionActionSnapshot& operator=(const ExtensionActionSnapshot&);
  ExtensionActionSnapshot(ExtensionActionSnapshot&&);
  ExtensionActionSnapshot& operator=(ExtensionActionSnapshot&&);
  ~ExtensionActionSnapshot();

  std::string id;
  std::string name;
  bool visible = false;
  bool has_popup = false;
  // ToolbarActionsModel::IsActionPinned. The strip shows exactly these, and
  // the quick menu's tile dot follows it.
  bool pinned = false;
  std::string action_title;
  std::string badge_text;
  // ARGB, 0xAARRGGBB byte order (same as set_backdrop_tint's argument).
  uint32_t badge_bg = 0;
  uint32_t badge_text_color = 0;
  // Goes up each time the browser stores new icon bytes for this id, so the
  // page asks for a new URL. 0 means no icon is stored yet.
  int icon_rev = 0;
  // ExtIconScopeFor the profile whose cache holds the icon bytes. The page's
  // own scope follows the window's target browser, which can be another one.
  std::string icon_scope;
};

// Serializes `actions` into AtriumOnExtensionActionsChanged's JSON array shape
// (see the retired ABI header's EXTENSIONS doc block). ARGB colors are emitted as
// numbers up to 0xFFFFFFFF, matching ExtensionActionJSON's int64Value/
// truncatingIfNeeded decode on the Swift side. An empty `actions` yields "[]",
// a real empty snapshot Swift full-replaces its cache from.
std::string BuildExtensionActionsJSON(
    const std::vector<ExtensionActionSnapshot>& actions);

// Diff-gates repeated AtriumOnExtensionActionsChanged pushes for the same
// active tab (ResyncActiveTab fires on every ToolbarActionsModel
// observer event, including chatty background badge updates, with no diff
// against what was last sent). One instance lives per ExtensionActionBridge
// (per-window), never shared across tabs/windows.
//
// A tab switch (the tab_id passed to ShouldPush differs from the last call's)
// ALWAYS returns true, even if the newly-built json happens to be
// byte-identical to the old tab's last-pushed json. Swift's handler is a
// full per-host replace, so re-sending identical content on a switch is
// redundant, not wrong - but this gate deliberately does not try to skip
// that case: scoping "skip" strictly to "same tab, same content" keeps the
// gate's contract simple (one redundant push on activation, which already
// happens today) and safe against a future change to per-tab resolution
// silently going stale if two different tabs' snapshots ever collided.
class ExtensionActionsPushGate {
 public:
  ExtensionActionsPushGate();
  ~ExtensionActionsPushGate();

  // Returns false (skip the push) only when `tab_id` matches the previous
  // call's tab_id AND `json` is byte-identical to that call's json. Always
  // records `tab_id`/`json` as the new baseline, regardless of the result.
  bool ShouldPush(int tab_id, const std::string& json);

  // The snapshot last handed to ShouldPush - empty before the first call.
  // Exposed so a surface that comes up AFTER the push can pull the current
  // state instead of waiting for the next change, which may never come.
  const std::string& last_json() const { return last_json_; }

 private:
  // false until the first ShouldPush() call - guarantees that call always
  // pushes (there is nothing yet to consider identical to). A plain sentinel
  // tab id would not work here: ExtensionAction::kDefaultTabId is -1, a
  // legitimate value ShouldPush can be called with.
  bool has_previous_push_ = false;
  int last_tab_id_ = 0;
  std::string last_json_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_ACTIONS_JSON_H_
