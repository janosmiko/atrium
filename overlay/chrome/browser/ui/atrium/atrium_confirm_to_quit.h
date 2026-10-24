// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_confirm_to_quit.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_TO_QUIT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_TO_QUIT_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"

class PrefService;

namespace atrium {

// The confirmToQuitEnabled field of a WHOLE settings object, the shape
// LoadSettingsJSON returns and the settings surface sends back.
//
// nullopt for an object that will not parse and for one that does not carry
// the field. Absent is a real answer rather than an error: every key past the
// first four is optional, so a settings file from another build legitimately
// arrives without it, and the caller leaves the pref alone rather than
// inventing a value the user never chose.
std::optional<bool> ConfirmToQuitFromSettings(std::string_view settings_json);

// Puts `enabled` onto prefs::kConfirmToQuitEnabled ("browser.confirm_to_quit")
// in Chromium's app-wide LOCAL STATE - the one thing that decides whether
// Cmd+Q asks first. confirm_quit::RegisterLocalState registers it (opt-in,
// unlike stock Chrome), BrowserProcessPlatformPart::AttemptExit and
// -[AppController tryToTerminateApplication] both consult it, and the app
// menu's own "Confirm to Quit" item writes it.
//
// the settings row for this stored a byte nothing read, so the
// toggle rendered, persisted and changed nothing. This is the seam that was
// missing; it went with the ABI (set_confirm_to_quit).
//
// UI THREAD ONLY - PrefService is not thread-safe. A no-op before local state
// exists, which is the same fail-soft posture the rest of this layer takes.
void SetConfirmToQuitPref(bool enabled);

// Mirrors opaque_surround/surround_argb onto the panel's prefs (no
// WebContents to receive PushOverlayTheme). `surround_argb` always
// overwrites - nullopt means AUTOMATIC, unlike `native`'s own nullopt.
void SetConfirmQuitPanelAppearance(std::optional<bool> native,
                                   std::optional<uint32_t> surround_argb);

// The chosen overlay theme's palette, mirrored for the same reason. nullopt
// means the brand fallback.
void SetConfirmQuitPanelTheme(std::optional<OverlayThemePalette> theme);

// The palette SetConfirmQuitPanelTheme stored, or nullopt for the fallback.
std::optional<OverlayThemePalette> ConfirmQuitPanelThemeFromPrefs(
    const PrefService& local_state);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_TO_QUIT_H_
