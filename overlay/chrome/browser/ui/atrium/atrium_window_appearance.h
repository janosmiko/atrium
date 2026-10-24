// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_appearance.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_APPEARANCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_APPEARANCE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace atrium {

// overlayAccent's one slug that is not a brand book theme: the live macOS
// accent, which reaches the overlays as properties rather than as a class.
inline constexpr char kSystemOverlayAccent[] = "system";

// The settings.json fields a browser WINDOW has to paint, read out of a WHOLE
// settings object - the shape LoadSettingsJSON returns and the settings
// surface sends back.
//
// Every field is optional, and nullopt means "the file did not say" rather
// than false or zero. Absent is a real answer here for the reason
// ConfirmToQuitFromSettings gives: every key past the first four is optional,
// so a settings file from another build legitimately arrives without one, and
// a window left at its own default is better than one moved to a value the
// user never chose.
//
// AtriumBrowserWindow::SetSidebarPosition, SetOpaqueSurround and
// SetGlassBackdropAlpha were all implemented and all had zero callers - Swift
// used to broadcast these three and the broadcaster went with the ABI.
// This is the half that says what to send; AtriumBrowserWindow::
// SetWindowAppearance is the half that sends it.
struct WindowAppearance {
  // sidebarPosition: true for "right", false for "left".
  std::optional<bool> sidebar_on_right;

  // opaqueSurround: the native-app chrome mode (docs/opaque-surround-design.md).
  std::optional<bool> opaque_surround;

  // surroundColor as 0xAARRGGBB with the top byte unused, which is exactly
  // what SetOpaqueSurround's `argb` takes. Absent means AUTOMATIC, which is a
  // valid stored value rather than a missing one - the window resolves an
  // appearance-adaptive neutral itself - so this being nullopt never stops
  // opaque_surround from being applied.
  std::optional<uint32_t> surround_argb;

  // glassOpacity as the 0..100 percent SetGlassBackdropAlpha takes. Clamped
  // to the store's own legal range (0.02..1.0 -> 2..100) so a wider value
  // from a later build paints as the nearest one this build can paint,
  // instead of not painting at all.
  std::optional<int> glass_alpha_percent;

  // borderWidth in points, as ContentCardInset() holds it. Clamped to the
  // store's 4..10 on glassOpacity's terms: a value from a later build paints
  // as the nearest one this build can paint.
  std::optional<int> border_width;

  // The brand book theme's slug, or kSystemOverlayAccent. Legacy "brand" and
  // the retired "space" both arrive here as "crimson", the accent "brand"
  // pinned - an existing settings.json keeps its colours unmigrated.
  std::optional<std::string> overlay_theme;

  // appState.sidebarWidth in points, as SetSidebarWidth takes it.
  //
  // The one field here that does NOT come out of settings.json: the store keeps
  // the sidebar's width in the SPACES document, so SidebarWidthFromSpaces below
  // fills it in and WindowAppearanceFromSettings always leaves it nullopt.
  //
  // It joins this struct anyway because it is a window value like the other
  // four and takes the same three steps - applied to every live window, held as
  // the current answer, re-applied by a window built later. A holder and a walk
  // of its own would be a second copy of all three.
  std::optional<double> sidebar_width;

  // Defaulted so a field added later is compared automatically:
  // a hand-rolled AppearanceValuesDiffer forgot to compare overlay_theme.
  bool operator==(const WindowAppearance& other) const = default;
};

// nullopt fields for an object that will not parse, for one that is not an
// object, and for every key it does not carry. Never answers for
// `sidebar_width` - that key is not in this document.
WindowAppearance WindowAppearanceFromSettings(std::string_view settings_json);

// appState.sidebarWidth out of a WHOLE spaces document, in the shape
// LoadSpacesJSON returns - which is the document after the core has loaded it,
// so the legacy-280 correction has already run.
//
// nullopt for a document that will not parse, for one carrying no width, and
// for a width that reads as COLLAPSED. That last case is the trap this field
// has and the other four do not. Nothing in this build writes
// appState.sidebarWidth, so a 0 there is a Swift session that quit with the
// sidebar hidden - and seeding it would open every window collapsed, with the
// traffic lights hidden too while the sidebar docks left. Cmd+S would be the
// only way back, and it would expand to 220 rather than to the width the file
// actually holds. A width is a width; "collapsed" is not one.
//
// A real width is clamped into the range the window enforces (160..400) on the
// same terms as glassOpacity: a value from a build with a wider range paints as
// the nearest one this build can paint.
std::optional<double> SidebarWidthFromSpaces(std::string_view spaces_json);

// A full settings save always carries overlayAccent, so a caller must
// compare against the live value rather than act on presence alone.
bool OverlayAccentDiffers(const std::string& new_theme,
                          const std::string& current_theme);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_APPEARANCE_H_
