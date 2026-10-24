// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hotkey_reset_browsertest.cc
//
// Putting a shortcut back on the chord Atrium ships, driven as a key through
// NativeHotkeyMap: a file assertion alone would pass on a build where the
// reset never reaches the live map and the old chord keeps firing.

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// toggleDevBar ships cmd+shift+d and its effect is readable from the window,
// so a chord that fired is told apart from one that was merely consumed.
constexpr char kShipped[] = "cmd+shift+d";
constexpr char kRebound[] = "cmd+shift+u";

constexpr char kReboundFile[] =
    R"({"schemaVersion":2,"actions":{"toggleDevBar":["cmd+shift+u"]}})";

class AtriumHotkeyResetBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    path_ = temp_dir_.GetPath().AppendASCII("hotkeys.json");
    atrium::NativeHotkeyMap::Get()->ResetForTesting();
  }

  atrium::AtriumBrowserWindow* AtriumWindow() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // SaveHotkeys and OnHotkeysSaved, in their order and with their arguments,
  // minus the settings window. Re-reading the file instead would merge the
  // defaults back in and hide the bug this case exists to catch.
  void SaveAndSeed(const std::string& payload) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const std::string file = atrium::HotkeysFileAfterEdit(payload);
    ASSERT_EQ(atrium::SaveHotkeysJSON(path_, file), 1);
    atrium::NativeHotkeyMap::Get()->SetFromJSON(
        atrium::HotkeysMergedForNativeMap(file));
  }

  std::string OnDisk() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(path_, &contents));
    return contents;
  }

  // The dev bar toggles, so a case that presses more than once has to put it
  // back before the next press means anything.
  void PressAndExpectDevBar(const char* combo) {
    ASSERT_TRUE(AtriumWindow()->DeliverComboForTesting(combo))
        << combo << " was declined, so the map does not carry it";
    EXPECT_TRUE(AtriumWindow()->DevBarShownForTesting());
    ASSERT_TRUE(AtriumWindow()->DeliverComboForTesting(combo));
    EXPECT_FALSE(AtriumWindow()->DevBarShownForTesting());
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath path_;
};

IN_PROC_BROWSER_TEST_F(AtriumHotkeyResetBrowserTest,
                       ResettingOneRowGivesTheShippedChordBackWithoutARelaunch) {
  SaveAndSeed(kReboundFile);
  ASSERT_FALSE(AtriumWindow()->DeliverComboForTesting(kShipped))
      << "the rebind did not release the shipped chord, so this case cannot "
         "tell a reset from a build that never applied the rebind";
  PressAndExpectDevBar(kRebound);

  SaveAndSeed(
      R"({"schemaVersion":2,"actions":{"toggleDevBar":["cmd+shift+u"]},)"
      R"("forget":"toggleDevBar"})");

  EXPECT_EQ(OnDisk().find("toggleDevBar"), std::string::npos)
      << "the file still names the action, so a later change to what Atrium "
         "ships would never reach this profile";
  EXPECT_FALSE(AtriumWindow()->DeliverComboForTesting(kRebound));
  PressAndExpectDevBar(kShipped);
}

IN_PROC_BROWSER_TEST_F(AtriumHotkeyResetBrowserTest,
                       ResettingThePaneLeavesNoUserActionsAndRebindsTheMap) {
  SaveAndSeed(kReboundFile);
  ASSERT_FALSE(AtriumWindow()->DeliverComboForTesting(kShipped));

  SaveAndSeed(
      R"({"schemaVersion":2,"actions":{"toggleDevBar":["cmd+shift+u"]},)"
      R"("forgetAll":true})");

  EXPECT_NE(OnDisk().find(R"("actions":{})"), std::string::npos)
      << "the pane reset left user actions behind: " << OnDisk();
  EXPECT_FALSE(AtriumWindow()->DeliverComboForTesting(kRebound));
  PressAndExpectDevBar(kShipped);
}

// An ordinary rebind travels through the same call, so a payload that forgets
// nothing must still write the map it was sent.
IN_PROC_BROWSER_TEST_F(AtriumHotkeyResetBrowserTest,
                       APayloadThatForgetsNothingIsWrittenAsSent) {
  SaveAndSeed(kReboundFile);

  EXPECT_NE(OnDisk().find("cmd+shift+u"), std::string::npos);
  PressAndExpectDevBar(kRebound);
}

}  // namespace
