// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pip_always_on_top.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIP_ALWAYS_ON_TOP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIP_ALWAYS_ON_TOP_H_

#include <optional>
#include <string_view>

namespace atrium {

// The pipAlwaysOnTop field of a WHOLE settings object, the shape
// LoadSettingsJSON returns and the settings surface sends back.
//
// nullopt for an object that will not parse and for one that does not carry
// the field - a settings file from another build legitimately arrives without
// it, and the caller leaves PipAlwaysOnTop() at its default rather than
// inventing a value the user never chose.
std::optional<bool> PipAlwaysOnTopFromSettings(std::string_view settings_json);

// The z-order the next mini player should open with. Defaults to true
// (`ui::ZOrderLevel::kFloatingWindow`, the behaviour before this setting
// existed) until LoadPipAlwaysOnTopFromSettings or the button's own toggle
// sets it.
//
// A plain in-memory mirror rather than a PrefService entry: the mini player
// is a content::VideoOverlayWindow living in chrome/browser/ui/views/overlay,
// not an AtriumBrowserWindow, so it has no PrefService to read at
// views::Widget::InitParams time. Mirrors AtriumBrowserWindow::DevBarPinned()
// for the same reason.
bool& PipAlwaysOnTop();

// Seeds PipAlwaysOnTop() from a whole settings document read at startup
// (AtriumMainExtraParts::SeedFromSettings). A no-op when the field is absent
// or the document does not parse - PipAlwaysOnTop() keeps its default.
void LoadPipAlwaysOnTopFromSettings(std::string_view settings_json);

// Writes `enabled` to settings.json, whole-document read-modify-write like
// AtriumBrowserWindow::SaveAddressBarPinned - the toggle starts outside
// AtriumSettingsWindow, same as that one. Posts async, logs on failure only.
void SavePipAlwaysOnTop(bool enabled);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIP_ALWAYS_ON_TOP_H_
