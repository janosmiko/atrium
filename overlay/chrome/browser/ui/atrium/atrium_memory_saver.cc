// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_memory_saver.cc
#include "chrome/browser/ui/atrium/atrium_memory_saver.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"

namespace atrium {

namespace {

using performance_manager::user_tuning::prefs::
    kDefaultMemorySaverModeTimeBeforeDiscardInMinutes;
using performance_manager::user_tuning::prefs::
    kMemorySaverModeTimeBeforeDiscardInMinutes;
using performance_manager::user_tuning::prefs::kMemorySaverModeState;
using performance_manager::user_tuning::prefs::MemorySaverModeState;

// rust/atrium-core/src/settings.rs's MEMORY_SAVER_THRESHOLDS.
std::optional<int> ThresholdMinutes(std::string_view threshold) {
  if (threshold == "fifteenMinutes") {
    return 15;
  }
  if (threshold == "thirtyMinutes") {
    return 30;
  }
  if (threshold == "oneHour") {
    return 60;
  }
  if (threshold == "twoHours") {
    return 120;
  }
  if (threshold == "fourHours") {
    return 240;
  }
  return std::nullopt;
}

// The inverse of ThresholdMinutes. An unrecognised value (including the
// default 120 the pref system already ships) falls back to "twoHours" -
// the ladder rung that number already means.
std::string LadderFromMinutes(int minutes) {
  switch (minutes) {
    case 15:
      return "fifteenMinutes";
    case 30:
      return "thirtyMinutes";
    case 60:
      return "oneHour";
    case 240:
      return "fourHours";
    default:
      return "twoHours";
  }
}

}  // namespace

std::optional<bool> MemorySaverEnabledFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("memorySaverEnabled");
}

std::optional<int> MemorySaverThresholdMinutesFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* threshold =
      parsed->GetDict().FindString("memorySaverThreshold");
  if (!threshold) {
    return std::nullopt;
  }
  return ThresholdMinutes(*threshold);
}

void SetMemorySaverEnabledPref(bool enabled) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  local_state->SetInteger(
      kMemorySaverModeState,
      static_cast<int>(enabled ? MemorySaverModeState::kEnabled
                                : MemorySaverModeState::kDisabled));
  VLOG(1) << "Atrium: memory saver is now " << (enabled ? "on" : "off");
}

void SetMemorySaverThresholdMinutesPref(int minutes) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  local_state->SetInteger(kMemorySaverModeTimeBeforeDiscardInMinutes,
                           minutes);
  VLOG(1) << "Atrium: memory saver threshold is now " << minutes
          << " minutes";
}

bool MemorySaverEnabledFromPref() {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return false;
  }
  return performance_manager::user_tuning::prefs::GetCurrentMemorySaverModeState(
             local_state) == MemorySaverModeState::kEnabled;
}

std::string MemorySaverThresholdLadderFromPref() {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return LadderFromMinutes(kDefaultMemorySaverModeTimeBeforeDiscardInMinutes);
  }
  return LadderFromMinutes(
      local_state->GetInteger(kMemorySaverModeTimeBeforeDiscardInMinutes));
}

}  // namespace atrium
