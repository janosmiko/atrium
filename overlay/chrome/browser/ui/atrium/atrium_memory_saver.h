// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_memory_saver.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEMORY_SAVER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEMORY_SAVER_H_

#include <optional>
#include <string>
#include <string_view>

namespace atrium {

// settings.json's memorySaverEnabled/memorySaverThreshold reached
// only Atrium's own store. confirm_to_quit's shape: a pure reader here, the
// Chromium local-state write beside it.

// nullopt for an unparsable object and for one omitting the field, matching
// ConfirmToQuitFromSettings.
std::optional<bool> MemorySaverEnabledFromSettings(
    std::string_view settings_json);

// Converts the ladder spelling (rust/atrium-core/src/settings.rs's
// MEMORY_SAVER_THRESHOLDS) to minutes. nullopt for a missing field or a rung
// this build does not recognise.
std::optional<int> MemorySaverThresholdMinutesFromSettings(
    std::string_view settings_json);

// UI THREAD ONLY - PrefService is not thread-safe. No-op before local state
// exists, SetConfirmToQuitPref's posture.
void SetMemorySaverEnabledPref(bool enabled);
void SetMemorySaverThresholdMinutesPref(int minutes);

// the hibernation bridge reads these back at tab-add time,
// when it has no settings.json to hand - the same prefs the setters above
// write. False/the default ladder rung before local state exists.
bool MemorySaverEnabledFromPref();
std::string MemorySaverThresholdLadderFromPref();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEMORY_SAVER_H_
