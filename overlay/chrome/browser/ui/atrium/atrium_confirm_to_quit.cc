// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_confirm_to_quit.cc
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace atrium {

std::optional<bool> ConfirmToQuitFromSettings(std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  // FindBool rather than Find: a field carrying a string or a number is a
  // settings file this build cannot honour, and guessing at it would turn the
  // quit gate on for someone who never asked.
  return parsed->GetDict().FindBool("confirmToQuitEnabled");
}

void SetConfirmToQuitPref(bool enabled) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  local_state->SetBoolean(prefs::kConfirmToQuitEnabled, enabled);
  // Two whole lines rather than one with the word interpolated: a scenario
  // asserting on either of them has to be able to find it in the source, which
  // is what build/scenarios/lib/parse.py's pattern guard checks.
  if (enabled) {
    VLOG(1) << "Atrium: confirm-to-quit is now on";
  } else {
    VLOG(1) << "Atrium: confirm-to-quit is now off";
  }
}

namespace {

// CurrentPanelStyle reads a negative pref as AUTOMATIC. An opaque colour's
// alpha byte always sets that sign bit, so alpha is masked off here rather
// than stored - ComputeConfirmQuitPanelStyle forces it back to opaque anyway.
constexpr int kPanelSurroundColorAutomatic = -1;
constexpr uint32_t kPanelSurroundColorRgbMask = 0x00FFFFFF;

}  // namespace

void SetConfirmQuitPanelAppearance(std::optional<bool> native,
                                   std::optional<uint32_t> surround_argb) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  if (native.has_value()) {
    local_state->SetBoolean(prefs::kAtriumConfirmQuitPanelNative, *native);
  }
  local_state->SetInteger(
      prefs::kAtriumConfirmQuitPanelSurroundColor,
      surround_argb.has_value()
          ? static_cast<int>(*surround_argb & kPanelSurroundColorRgbMask)
          : kPanelSurroundColorAutomatic);
}

namespace {

int StoredColor(std::optional<OverlayThemePalette> theme,
                SkColor OverlayThemePalette::*member) {
  return theme ? static_cast<int>((*theme).*member & kPanelSurroundColorRgbMask)
               : kPanelSurroundColorAutomatic;
}

}  // namespace

void SetConfirmQuitPanelTheme(std::optional<OverlayThemePalette> theme) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  local_state->SetInteger(prefs::kAtriumConfirmQuitPanelAccentColor,
                          StoredColor(theme, &OverlayThemePalette::accent));
  local_state->SetInteger(prefs::kAtriumConfirmQuitPanelInkColor,
                          StoredColor(theme, &OverlayThemePalette::ink));
  local_state->SetInteger(prefs::kAtriumConfirmQuitPanelPaperColor,
                          StoredColor(theme, &OverlayThemePalette::paper));
  local_state->SetInteger(prefs::kAtriumConfirmQuitPanelSoftColor,
                          StoredColor(theme, &OverlayThemePalette::soft));
}

std::optional<OverlayThemePalette> ConfirmQuitPanelThemeFromPrefs(
    const PrefService& local_state) {
  const int accent =
      local_state.GetInteger(prefs::kAtriumConfirmQuitPanelAccentColor);
  const int ink = local_state.GetInteger(prefs::kAtriumConfirmQuitPanelInkColor);
  const int paper =
      local_state.GetInteger(prefs::kAtriumConfirmQuitPanelPaperColor);
  const int soft =
      local_state.GetInteger(prefs::kAtriumConfirmQuitPanelSoftColor);
  if (accent < 0 || ink < 0 || paper < 0 || soft < 0) {
    return std::nullopt;
  }
  const auto opaque = [](int stored) {
    return SkColorSetA(static_cast<SkColor>(stored), SK_AlphaOPAQUE);
  };
  return OverlayThemePalette{
      .accent = opaque(accent),
      .ink = opaque(ink),
      .paper = opaque(paper),
      .soft = opaque(soft),
  };
}

}  // namespace atrium
