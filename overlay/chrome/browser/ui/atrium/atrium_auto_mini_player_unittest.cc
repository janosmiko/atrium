// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_auto_mini_player_unittest.cc
#include "chrome/browser/ui/atrium/atrium_auto_mini_player.h"

#include <string>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::string EnabledFeatures(const base::CommandLine& command_line) {
  return command_line.GetSwitchValueASCII(switches::kEnableFeatures);
}

class AtriumAutoMiniPlayerTest : public ::testing::Test {
 protected:
  void TearDown() override {
    SetAutoMiniPlayer(AutoMiniPlayer::kAudible);
    SetTabOutsideActiveSpaceCheck(nullptr);
  }
};

TEST_F(AtriumAutoMiniPlayerTest, OffRefusesWhatChromiumWouldTake) {
  SetAutoMiniPlayer(AutoMiniPlayer::kOff);
  EXPECT_FALSE(AutoMiniPlayerAllows(AutoPipCondition::kSiteRegistered, true));
  EXPECT_FALSE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, true));
}

TEST_F(AtriumAutoMiniPlayerTest, SiteAsksPassesChromiumsOwnVerdictThrough) {
  SetAutoMiniPlayer(AutoMiniPlayer::kSiteAsks);
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kSiteRegistered, true));
  EXPECT_FALSE(AutoMiniPlayerAllows(AutoPipCondition::kSiteRegistered, false));
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, true));
  EXPECT_FALSE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, false));
}

TEST_F(AtriumAutoMiniPlayerTest, AudibleDropsTheRegistrationButKeepsTheSound) {
  SetAutoMiniPlayer(AutoMiniPlayer::kAudible);
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kSiteRegistered, false));
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, true));
  EXPECT_FALSE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, false));
}

TEST_F(AtriumAutoMiniPlayerTest, AnyVideoDropsBoth) {
  SetAutoMiniPlayer(AutoMiniPlayer::kAnyVideo);
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kSiteRegistered, false));
  EXPECT_TRUE(AutoMiniPlayerAllows(AutoPipCondition::kAudible, false));
}

TEST_F(AtriumAutoMiniPlayerTest, EveryStoredSpellingIsRead) {
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"autoMiniPlayer":"off"})"),
            AutoMiniPlayer::kOff);
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"autoMiniPlayer":"siteAsks"})"),
            AutoMiniPlayer::kSiteAsks);
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"autoMiniPlayer":"audible"})"),
            AutoMiniPlayer::kAudible);
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"autoMiniPlayer":"anyVideo"})"),
            AutoMiniPlayer::kAnyVideo);
}

// A settings file from a build that predates the key, and one hand-edited to a
// spelling neither side knows. The core answers both with the default, so this
// reader has to agree or the gate and the stored file disagree.
TEST_F(AtriumAutoMiniPlayerTest, AnythingElseReadsAsAudible) {
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"glassOpacity":0.55})"),
            AutoMiniPlayer::kAudible);
  EXPECT_EQ(AutoMiniPlayerFromSettings(R"({"autoMiniPlayer":"sometimes"})"),
            AutoMiniPlayer::kAudible);
  EXPECT_EQ(AutoMiniPlayerFromSettings(""), AutoMiniPlayer::kAudible);
}

// The two modes that exist for videos the site never registered. Without the
// feature Chromium opens nothing for those, so the modes are inert.
TEST_F(AtriumAutoMiniPlayerTest, UnregisteredModesTurnTheFeatureOn) {
  for (AutoMiniPlayer mode :
       {AutoMiniPlayer::kAudible, AutoMiniPlayer::kAnyVideo}) {
    base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
    AppendAutoMiniPlayerFeature(mode, &command_line);
    EXPECT_EQ(EnabledFeatures(command_line),
              "BrowserInitiatedAutomaticPictureInPicture");
  }
}

// siteAsks means what it says only while the feature is off. With it on,
// Chromium offers the action for an unregistered video too.
TEST_F(AtriumAutoMiniPlayerTest, RegisteredOnlyModesLeaveTheFeatureAlone) {
  for (AutoMiniPlayer mode :
       {AutoMiniPlayer::kOff, AutoMiniPlayer::kSiteAsks}) {
    base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
    AppendAutoMiniPlayerFeature(mode, &command_line);
    EXPECT_FALSE(command_line.HasSwitch(switches::kEnableFeatures));
  }
}

TEST_F(AtriumAutoMiniPlayerTest, UserSuppliedFeatureListSurvives) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(switches::kEnableFeatures, "SomeOtherThing");
  AppendAutoMiniPlayerFeature(AutoMiniPlayer::kAudible, &command_line);
  EXPECT_EQ(EnabledFeatures(command_line),
            "SomeOtherThing,BrowserInitiatedAutomaticPictureInPicture");
}

// A relaunch inherits the parent's already-appended switch, so the second
// pass must not write the name twice.
TEST_F(AtriumAutoMiniPlayerTest, AlreadyEnabledIsNotAppendedAgain) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  AppendAutoMiniPlayerFeature(AutoMiniPlayer::kAudible, &command_line);
  AppendAutoMiniPlayerFeature(AutoMiniPlayer::kAudible, &command_line);
  EXPECT_EQ(EnabledFeatures(command_line),
            "BrowserInitiatedAutomaticPictureInPicture");
}

TEST_F(AtriumAutoMiniPlayerTest, NoSpaceCheckKeepsEveryTabInside) {
  EXPECT_FALSE(TabIsOutsideActiveSpace(nullptr));
}

TEST_F(AtriumAutoMiniPlayerTest, RegisteredSpaceCheckDecides) {
  SetTabOutsideActiveSpaceCheck([](content::WebContents*) { return true; });
  EXPECT_TRUE(TabIsOutsideActiveSpace(nullptr));
  SetTabOutsideActiveSpaceCheck([](content::WebContents*) { return false; });
  EXPECT_FALSE(TabIsOutsideActiveSpace(nullptr));
}

}  // namespace
}  // namespace atrium
