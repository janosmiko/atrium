// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_actions_json.cc
#include "chrome/browser/ui/atrium/atrium_extension_actions_json.h"

#include "base/json/json_writer.h"
#include "base/values.h"

namespace atrium {

ExtensionActionSnapshot::ExtensionActionSnapshot() = default;
ExtensionActionSnapshot::ExtensionActionSnapshot(
    const ExtensionActionSnapshot&) = default;
ExtensionActionSnapshot& ExtensionActionSnapshot::operator=(
    const ExtensionActionSnapshot&) = default;
ExtensionActionSnapshot::ExtensionActionSnapshot(ExtensionActionSnapshot&&) =
    default;
ExtensionActionSnapshot& ExtensionActionSnapshot::operator=(
    ExtensionActionSnapshot&&) = default;
ExtensionActionSnapshot::~ExtensionActionSnapshot() = default;

std::string BuildExtensionActionsJSON(
    const std::vector<ExtensionActionSnapshot>& actions) {
  base::ListValue list;
  for (const ExtensionActionSnapshot& action : actions) {
    base::DictValue entry;
    entry.Set("id", action.id);
    entry.Set("name", action.name);
    entry.Set("visible", action.visible);
    entry.Set("hasPopup", action.has_popup);
    entry.Set("pinned", action.pinned);
    entry.Set("actionTitle", action.action_title);
    entry.Set("badgeText", action.badge_text);
    // base::Value has no uint32/int64 arm; ARGB up to 0xFFFFFFFF overflows a
    // 32-bit int, so it rides across as a double. JSONWriter emits it as a
    // number Swift's ExtensionActionJSON decodes via int64Value (see the
    // header's note).
    entry.Set("badgeBg", static_cast<double>(action.badge_bg));
    entry.Set("badgeTextColor", static_cast<double>(action.badge_text_color));
    entry.Set("iconRev", action.icon_rev);
    entry.Set("iconScope", action.icon_scope);
    list.Append(std::move(entry));
  }
  std::string json;
  base::JSONWriter::Write(list, &json);
  return json;
}

ExtensionActionsPushGate::ExtensionActionsPushGate() = default;
ExtensionActionsPushGate::~ExtensionActionsPushGate() = default;

bool ExtensionActionsPushGate::ShouldPush(int tab_id, const std::string& json) {
  const bool skip = has_previous_push_ && tab_id == last_tab_id_ &&
                    json == last_json_;
  has_previous_push_ = true;
  last_tab_id_ = tab_id;
  last_json_ = json;
  return !skip;
}

}  // namespace atrium
