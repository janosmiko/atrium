// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hibernate_bridge.cc
#include "chrome/browser/ui/atrium/atrium_hibernate_bridge.h"

#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/atrium/atrium_memory_saver.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

namespace {
constexpr double kUnknownPolicy = -2.0;
}  // namespace

void ApplyHibernatePolicyToTab(content::WebContents* contents,
                                uint64_t tab_id) {
  if (!contents) {
    return;
  }
  auto* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          contents);
  if (!unit) {
    return;
  }

  const std::string threshold = MemorySaverThresholdLadderFromPref();
  const double answer = atrium_core_hibernate_policy_for_tab(
      tab_id, MemorySaverEnabledFromPref(), threshold.c_str());
  if (answer == kUnknownPolicy) {
    return;
  }
  unit->SetAutoDiscardable(answer >= 0.0);
}

}  // namespace atrium
