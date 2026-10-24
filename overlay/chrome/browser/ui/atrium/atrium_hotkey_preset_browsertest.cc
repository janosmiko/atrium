// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hotkey_preset_browsertest.cc
//
// Picking a keyboard preset, driven as keys through NativeHotkeyMap. A file
// assertion alone would pass on a build where the pick never reaches the live
// map and the previous preset's chords keep firing.

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace {

// Arc leaves both rows unassigned, which its table spells as an EMPTY list -
// the one shape that releases a key the build compiles in.

// Both sit on the reload ladder, where stock Chromium binds no chord. A
// RELEASED Cmd+T still reports as handled, by IDC_NEW_TAB, so a chord
// Chromium claims too cannot tell Atrium's binding from Chromium's.
constexpr char kAtriumOnlyReload[] = "cmd+opt+r";
constexpr char kAtriumOnlySiteData[] = "cmd+shift+opt+r";
// Both presets bind the sidebar toggle to it, so a run where every chord went
// dark is told apart from one where Arc's table landed.
constexpr char kSharedSidebar[] = "cmd+s";

constexpr char kAtriumFile[] = R"({"schemaVersion":2,"actions":{}})";
constexpr char kPickArc[] =
    R"({"schemaVersion":2,"preset":"arc","actions":{},"forgetAll":true})";
constexpr char kPickAtrium[] =
    R"({"schemaVersion":2,"preset":"atrium","actions":{},"forgetAll":true})";

class AtriumHotkeyPresetBrowserTest : public InProcessBrowserTest {
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
  // minus the settings window.
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

  bool Carries(const char* combo) {
    return AtriumWindow()->DeliverComboForTesting(combo);
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath path_;
};

IN_PROC_BROWSER_TEST_F(AtriumHotkeyPresetBrowserTest,
                       PickingArcRebindsTheLiveMapWithoutARelaunch) {
  SaveAndSeed(kAtriumFile);
  ASSERT_TRUE(Carries(kAtriumOnlyReload))
      << "Atrium's own chord was not live, so this case cannot tell a preset "
         "switch from a map that never carried it";
  ASSERT_TRUE(Carries(kAtriumOnlySiteData));

  SaveAndSeed(kPickArc);

  EXPECT_NE(OnDisk().find(R"("preset":"arc")"), std::string::npos)
      << "the file does not name the preset, so the pick dies at the relaunch: "
      << OnDisk();
  EXPECT_NE(OnDisk().find(R"("actions":{})"), std::string::npos)
      << "the pick wrote the chords out as user values, which pins them: "
      << OnDisk();
  EXPECT_TRUE(Carries(kSharedSidebar))
      << "Arc's own table did not land, so the assertions below would pass on "
         "a map that was merely emptied";
  EXPECT_FALSE(Carries(kAtriumOnlyReload));
  EXPECT_FALSE(Carries(kAtriumOnlySiteData))
      << "an unassigned row left the compiled-in chord bound, so the preset's "
         "empty list did not reach the map as an unbind";
}

IN_PROC_BROWSER_TEST_F(AtriumHotkeyPresetBrowserTest,
                       PickingAtriumBackRestoresTheChordsArcReleased) {
  SaveAndSeed(kPickArc);
  ASSERT_FALSE(Carries(kAtriumOnlyReload));

  SaveAndSeed(kPickAtrium);

  EXPECT_TRUE(Carries(kAtriumOnlyReload));
  EXPECT_TRUE(Carries(kAtriumOnlySiteData));
  EXPECT_TRUE(Carries(kSharedSidebar))
      << "the chord both presets share must survive the round trip";
}

// The whole reason the file records a NAME: the user's own row is merged over
// the preset, so putting the row back lands on the PRESET's chord. Arc leaves
// this row unassigned, so landing on Atrium's cmd+opt+r would be the bug.
IN_PROC_BROWSER_TEST_F(AtriumHotkeyPresetBrowserTest,
                       AResetUnderAPresetReturnsTheRowToThatPresetsChord) {
  SaveAndSeed(R"({"schemaVersion":2,"preset":"arc",)"
              R"("actions":{"reloadClearingCache":["cmd+y"]}})");
  ASSERT_TRUE(Carries("cmd+y"));
  ASSERT_FALSE(Carries(kAtriumOnlyReload));

  SaveAndSeed(R"({"schemaVersion":2,"preset":"arc",)"
              R"("actions":{"reloadClearingCache":["cmd+y"]},)"
              R"("forget":"reloadClearingCache"})");

  EXPECT_EQ(OnDisk().find("reloadClearingCache"), std::string::npos)
      << "the file still names the action, so a later change to Arc's table "
         "would never reach this profile";
  EXPECT_NE(OnDisk().find(R"("preset":"arc")"), std::string::npos)
      << "the reset took the preset with it: " << OnDisk();
  EXPECT_TRUE(Carries(kSharedSidebar)) << "the map is still live at all";
  EXPECT_FALSE(Carries("cmd+y"));
  EXPECT_FALSE(Carries(kAtriumOnlyReload))
      << "the row went back to Atrium's chord rather than to Arc's, which "
         "leaves it unassigned";
}

// The cross-table guard, one preset wider than the defaults unittest: a table
// may only name actions this build can run, on chords ParseCombo accepts.
IN_PROC_BROWSER_TEST_F(AtriumHotkeyPresetBrowserTest,
                       EveryPresetChordIsOneTheNativeMapCanRun) {
  const std::string raw = atrium::HotkeyPresetTables();
  const std::optional<base::DictValue> presets =
      base::JSONReader::ReadDict(raw, base::JSON_PARSE_RFC);
  ASSERT_TRUE(presets) << raw;
  EXPECT_FALSE(presets->empty()) << "the core published no preset at all";

  for (const auto [preset, tables] : *presets) {
    const base::DictValue* actions = tables.GetIfDict();
    ASSERT_TRUE(actions) << preset;
    EXPECT_FALSE(actions->empty()) << preset << " is an empty table";
    for (const auto [action, value] : *actions) {
      EXPECT_TRUE(atrium::NativeHotkeyActionForId(action).has_value())
          << preset << " binds " << action << ", which runs nothing";
      const base::ListValue* combos = value.GetIfList();
      ASSERT_TRUE(combos) << preset << " " << action;
      for (const base::Value& combo : *combos) {
        const std::string* spelling = combo.GetIfString();
        ASSERT_TRUE(spelling) << preset << " " << action;
        EXPECT_TRUE(atrium::NativeHotkeyMap::ParseComboForTesting(*spelling)
                        .has_value())
            << preset << " binds " << action << " to " << *spelling
            << ", which the map drops with a log";
      }
    }
  }
}

}  // namespace
