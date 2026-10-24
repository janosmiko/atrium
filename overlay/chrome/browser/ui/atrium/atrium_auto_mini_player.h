// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_auto_mini_player.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTO_MINI_PLAYER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTO_MINI_PLAYER_H_

#include <string_view>

namespace base {
class CommandLine;
}

namespace content {
class WebContents;
}

namespace atrium {

// settings.json's `autoMiniPlayer`, which decides which playing videos go to
// picture-in-picture when the user switches away from their tab.
enum class AutoMiniPlayer {
  kOff,
  kSiteAsks,
  kAudible,
  kAnyVideo,
};

// The two conditions AutoPictureInPictureTabHelper owns that this setting
// moves. Everything else it checks - a safe URL, no camera or microphone in
// use, the per-site content setting - stays Chromium's and is not asked here.
enum class AutoPipCondition {
  kSiteRegistered,
  kAudible,
};

// Whether `condition` may pass, given Chromium's own verdict on it.
//
// The ONE place the helper asks, so a Chromium roll that moves either
// condition breaks here and nowhere else.
bool AutoMiniPlayerAllows(AutoPipCondition condition, bool chromium_says_yes);

// The `autoMiniPlayer` field of a WHOLE settings object, the shape
// LoadSettingsJSON returns.
//
// An object that will not parse, one without the field, and one carrying a
// spelling this build does not know all read as kAudible - the same answer the
// Rust core gives for the same file, so the gate and the stored settings
// cannot disagree.
AutoMiniPlayer AutoMiniPlayerFromSettings(std::string_view settings_json);

// BrowserInitiatedAutomaticPictureInPicture ships disabled, and without it
// nothing opens for a video the site never registered. kSiteAsks must keep it
// off: it also offers the action for unregistered videos.
void AppendAutoMiniPlayerFeature(AutoMiniPlayer mode,
                                 base::CommandLine* command_line);

// UI THREAD ONLY. Seeded at startup and rewritten when the settings surface
// saves, the same posture as SetConfirmToQuitPref: the helper is asked from a
// tab switch, where reading a file is a fatal DCHECK.
void SetAutoMiniPlayer(AutoMiniPlayer mode);

// A tab in a space the window is not showing counts as switched away from,
// even while it stays its strip's active tab. picture_in_picture cannot see
// spaces, so BrowserController registers the check. UI THREAD ONLY.
using TabOutsideActiveSpaceCheck = bool (*)(content::WebContents*);
void SetTabOutsideActiveSpaceCheck(TabOutsideActiveSpaceCheck check);

// False until a check is registered.
bool TabIsOutsideActiveSpace(content::WebContents* contents);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTO_MINI_PLAYER_H_
