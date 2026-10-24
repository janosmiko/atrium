// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_auto_mini_player.cc
#include "chrome/browser/ui/atrium/atrium_auto_mini_player.h"

#include <optional>
#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/values.h"

namespace atrium {

namespace {

constexpr char kBrowserInitiatedPip[] =
    "BrowserInitiatedAutomaticPictureInPicture";

AutoMiniPlayer& CurrentMode() {
  static AutoMiniPlayer mode = AutoMiniPlayer::kAudible;
  return mode;
}

TabOutsideActiveSpaceCheck& CurrentSpaceCheck() {
  static TabOutsideActiveSpaceCheck check = nullptr;
  return check;
}

}  // namespace

bool AutoMiniPlayerAllows(AutoPipCondition condition, bool chromium_says_yes) {
  switch (CurrentMode()) {
    case AutoMiniPlayer::kOff:
      return false;
    case AutoMiniPlayer::kSiteAsks:
      return chromium_says_yes;
    case AutoMiniPlayer::kAudible:
      return condition == AutoPipCondition::kSiteRegistered ? true
                                                            : chromium_says_yes;
    case AutoMiniPlayer::kAnyVideo:
      return true;
  }
}

AutoMiniPlayer AutoMiniPlayerFromSettings(std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return AutoMiniPlayer::kAudible;
  }
  const std::string* mode = parsed->GetDict().FindString("autoMiniPlayer");
  if (!mode) {
    return AutoMiniPlayer::kAudible;
  }
  if (*mode == "off") {
    return AutoMiniPlayer::kOff;
  }
  if (*mode == "siteAsks") {
    return AutoMiniPlayer::kSiteAsks;
  }
  if (*mode == "anyVideo") {
    return AutoMiniPlayer::kAnyVideo;
  }
  return AutoMiniPlayer::kAudible;
}

void AppendAutoMiniPlayerFeature(AutoMiniPlayer mode,
                                 base::CommandLine* command_line) {
  if (mode != AutoMiniPlayer::kAudible && mode != AutoMiniPlayer::kAnyVideo) {
    return;
  }
  std::string enabled =
      command_line->GetSwitchValueASCII(switches::kEnableFeatures);
  // A relaunch inherits the switch this already wrote.
  if (enabled.find(kBrowserInitiatedPip) != std::string::npos) {
    return;
  }
  if (!enabled.empty()) {
    enabled += ",";
  }
  enabled += kBrowserInitiatedPip;
  command_line->AppendSwitchASCII(switches::kEnableFeatures, enabled);
}

void SetAutoMiniPlayer(AutoMiniPlayer mode) {
  CurrentMode() = mode;
}

void SetTabOutsideActiveSpaceCheck(TabOutsideActiveSpaceCheck check) {
  CurrentSpaceCheck() = check;
}

bool TabIsOutsideActiveSpace(content::WebContents* contents) {
  const TabOutsideActiveSpaceCheck check = CurrentSpaceCheck();
  return check && check(contents);
}

}  // namespace atrium
