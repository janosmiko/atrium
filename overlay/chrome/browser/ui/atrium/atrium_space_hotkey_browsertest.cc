// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_space_hotkey_browsertest.cc
//
// goToSpace1..9, the nine NativeHotkeyActions that switch the window to a
// space by rail position, plus the two guards that stop the core's bindable
// list and the native map drifting apart again, and the key-event harness the
// dispatch arms had never had.

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/scoped_environment_variable_override.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace {

// ResetToDefaults used to CHECK-crash on an empty compiled-in combo.
// createSpace is exactly that, so every real launch exercises this guard
// whether or not the test does.
class AtriumNativeHotkeyMapSpaceActionBrowserTest
    : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyMapSpaceActionBrowserTest,
                       AnEmptyDefaultComboResetsToNoBindingNotACrash) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  EXPECT_TRUE(atrium::NativeHotkeyMap::Get()
                  ->CombosFor(atrium::NativeHotkeyAction::kCreateSpace)
                  .empty());
  EXPECT_FALSE(atrium::NativeHotkeyMap::Get()->IsDefaultComboBound(
      atrium::NativeHotkeyAction::kCreateSpace));
}

// No editable Inputs pane exists yet, so these drive NativeHotkeyMap through
// SetFromJSON directly - the same call SeedFromSettings makes from disk, but
// deterministic: SeedFromSettings itself only runs once per process, before
// the first of many TEST_F cases here, so a file staged per-test cannot reach it.
class AtriumCommandBarSpaceBindingBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Personal\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    spaces_env_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    // The core's own store cache is process-global, keyed on the path it
    // first opened - unlike NativeHotkeyMap, it reopens lazily on next use,
    // so closing it here is enough to make THIS test's file take effect.
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    spaces_env_.reset();
  }

  atrium::BrowserController* controller() {
    return atrium::BrowserController::ControllerForBrowser(browser());
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> spaces_env_;
};

IN_PROC_BROWSER_TEST_F(AtriumCommandBarSpaceBindingBrowserTest,
                       ASpaceRowShowsItsBoundChord) {
  ASSERT_TRUE(controller());
  ASSERT_FALSE(controller()->CommandBarSpaces().empty());

  atrium::NativeHotkeyMap::Get()->SetFromJSON(base::StringPrintf(
      "{\"schemaVersion\":%d,\"actions\":{\"goToSpace1\":"
      "[\"cmd+shift+1\"]}}",
      atrium::kNativeHotkeySchemaVersion));

  EXPECT_EQ(controller()->CommandBarSpaces()[0].binding, "⇧⌘1");
}

// The provable half: the row reads NativeHotkeyMap fresh on every build
// rather than a value cached from an earlier one - the mechanism a future
// live reload from Settings would ride on.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarSpaceBindingBrowserTest,
                       TheBindingIsReadFreshNotCached) {
  ASSERT_TRUE(controller());
  ASSERT_FALSE(controller()->CommandBarSpaces().empty());

  atrium::NativeHotkeyMap::Get()->SetFromJSON(base::StringPrintf(
      "{\"schemaVersion\":%d,\"actions\":{\"goToSpace1\":"
      "[\"cmd+shift+1\"]}}",
      atrium::kNativeHotkeySchemaVersion));
  ASSERT_EQ(controller()->CommandBarSpaces()[0].binding, "⇧⌘1");

  atrium::NativeHotkeyMap::Get()->SetFromJSON(base::StringPrintf(
      "{\"schemaVersion\":%d,\"actions\":{\"goToSpace1\":[]}}",
      atrium::kNativeHotkeySchemaVersion));

  EXPECT_TRUE(controller()->CommandBarSpaces()[0].binding.empty())
      << "the row kept the earlier chip after the map's live state changed, "
         "so it was reading a cache rather than the map";
}

// The combo strings the core ships as defaults for `action_id`, or nullopt
// when it ships none - the ordinary answer for createSpace.
std::optional<std::vector<std::string>> CoreDefaultCombosFor(
    std::string_view action_id) {
  char* raw = atrium_core_query("hotkeys", "", /*migration_instant=*/0.0);
  if (!raw) {
    return std::nullopt;
  }
  const std::string out(raw);
  atrium_core_string_free(raw);
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(out, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue* actions = parsed->GetDict().FindDict("actions");
  const base::ListValue* combos =
      actions ? actions->FindList(action_id) : nullptr;
  if (!combos) {
    return std::nullopt;
  }
  std::vector<std::string> out_combos;
  for (const base::Value& combo : *combos) {
    out_combos.push_back(combo.GetString());
  }
  return out_combos;
}

using AtriumNativeHotkeyMapComboGuardBrowserTest = InProcessBrowserTest;

// Against the map's real ParseCombo, not a copy of its rules. A core default
// it refuses is logged and dropped on every launch while the Inputs pane goes
// on listing it, which is how opt+tab shipped dead.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyMapComboGuardBrowserTest,
                       EveryCoreDefaultComboSurvivesParseCombo) {
  size_t checked = 0;
  for (const atrium::NativeHotkeyDefault& entry :
       atrium::NativeHotkeyDefaults()) {
    const std::optional<std::vector<std::string>> combos =
        CoreDefaultCombosFor(entry.action_id);
    if (!combos) {
      continue;
    }
    for (const std::string& combo : *combos) {
      ++checked;
      EXPECT_TRUE(
          atrium::NativeHotkeyMap::ParseComboForTesting(combo).has_value())
          << entry.action_id << "'s default combo '" << combo
          << "' does not survive ParseCombo";
    }
  }
  EXPECT_GT(checked, 0u) << "no core default combo was read at all";
}

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyMapComboGuardBrowserTest,
                       OptTabIsBoundToSpaceCycling) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  atrium::NativeHotkeyMap::Get()->SetFromJSON(
      base::StringPrintf("{\"schemaVersion\":%d,\"actions\":{\"nextSpace\":"
                         "[\"opt+tab\"],\"previousSpace\":"
                         "[\"shift+opt+tab\"]}}",
                         atrium::kNativeHotkeySchemaVersion));
  EXPECT_THAT(atrium::NativeHotkeyMap::Get()->CombosFor(
                  atrium::NativeHotkeyAction::kNextSpace),
              testing::Contains("opt+tab"));
  EXPECT_THAT(atrium::NativeHotkeyMap::Get()->CombosFor(
                  atrium::NativeHotkeyAction::kPreviousSpace),
              testing::Contains("shift+opt+tab"));
}

// The half of the rule that keeps a text edit. ComboEditsText gates arrows
// only when cmd is held, so anything admitted here without cmd or ctrl has no
// gate behind it and would swallow the edit outright.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyMapComboGuardBrowserTest,
                       AnUnmodifiedOrTextEditingComboIsStillRefused) {
  for (const char* combo : {"opt+a", "shift+opt+a", "tab", "escape"}) {
    EXPECT_FALSE(atrium::NativeHotkeyMap::ParseComboForTesting(combo)
                     .has_value())
        << combo << " bound with no modifier or as a character chord";
  }
  // Word movement, word selection and selection extension - every arrow form
  // reachable without cmd or ctrl, plus space, which scrolls a page.
  for (const char* combo : {"opt+left", "opt+right", "shift+opt+left",
                            "shift+opt+right", "shift+left", "shift+right",
                            "shift+up", "shift+down", "opt+up", "opt+down",
                            "shift+space", "opt+space"}) {
    EXPECT_FALSE(atrium::NativeHotkeyMap::ParseComboForTesting(combo)
                     .has_value())
        << combo << " is an AppKit text-editing chord and must stay with the "
        << "field - ComboEditsText does not gate it";
  }
  // The two that ARE claimed, so the rule above is narrow rather than total.
  EXPECT_TRUE(
      atrium::NativeHotkeyMap::ParseComboForTesting("opt+tab").has_value());
  EXPECT_TRUE(atrium::NativeHotkeyMap::ParseComboForTesting("shift+opt+tab")
                  .has_value());
}

// Nothing anywhere pressed a key through MatchAction or
// ExecuteNativeHotkeyAction before this fixture. Three spaces, active on the
// second, so firing goToSpace1 has an observable move to prove against, and a
// next-space step from the Overview cannot reach the first space by wrapping.
class AtriumNativeHotkeyDispatchBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":["
        "{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"First\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[]},"
        "{\"id\":\"22222222-2222-2222-2222-222222222222\","
        "\"name\":\"Second\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":1,"
        "\"pinnedTabs\":[],\"tabs\":[]},"
        "{\"id\":\"33333333-3333-3333-3333-333333333333\","
        "\"name\":\"Third\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":2,"
        "\"pinnedTabs\":[],\"tabs\":[]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"22222222-2222-2222-2222-222222222222\"}}"));
    spaces_env_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    spaces_env_.reset();
  }

  // The message the page sends when the rail rests on the Overview pane.
  static void ParkOnOverview(atrium::AtriumBrowserWindow* window) {
    base::ListValue parked;
    parked.Append(true);
    window->OnSurfaceMessage("sidebarOverviewPaneActive", parked);
  }

  // Cmd+L ships on editCurrentURL, so it is emptied the way the Arc preset
  // does. Left bound, it wins the chord and the space never moves.
  static void Bind(const char* action, const char* combo) {
    atrium::NativeHotkeyMap::Get()->ResetForTesting();
    atrium::NativeHotkeyMap::Get()->SetFromJSON(base::StringPrintf(
        "{\"schemaVersion\":%d,\"actions\":{\"editCurrentURL\":[],"
        "\"%s\":[\"%s\"]}}",
        atrium::kNativeHotkeySchemaVersion, action, combo));
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> spaces_env_;
};

// The Overview sits in front of the first space, so the next space from it is
// the first one, not the one after the space it opened from.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       NextSpaceFromTheOverviewLandsOnTheFirstSpace) {
  ASSERT_EQ(atrium_core_active_space_index(), 1);
  Bind("nextSpace", "cmd+l");
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ParkOnOverview(window);
  ASSERT_TRUE(window->IsOverviewShowing());

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+l")) << "cmd+l not handled";
  EXPECT_EQ(atrium_core_active_space_index(), 0);
}

// Opened from the first space, the step lands on the space the core already
// has. The chord must still be consumed, or it reaches its stock meaning.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       NextSpaceFromAnOverviewOpenedOnTheFirstSpaceStaysThere) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->DeliverComboForTesting("cmd+1"));
  ASSERT_EQ(atrium_core_active_space_index(), 0);
  Bind("nextSpace", "cmd+l");
  ParkOnOverview(window);

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+l"))
      << "cmd+l fell through to its stock meaning";
  EXPECT_EQ(atrium_core_active_space_index(), 0);
}

// The backward step from the Overview wraps to the last space, the same way a
// backward step from the first space does.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       PreviousSpaceFromTheOverviewLandsOnTheLastSpace) {
  ASSERT_EQ(atrium_core_active_space_index(), 1);
  Bind("previousSpace", "cmd+h");
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ParkOnOverview(window);

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+h")) << "cmd+h not handled";
  EXPECT_EQ(atrium_core_active_space_index(), 2);
}

// The chord goes through prePerformKeyEquivalent rather than into MatchAction,
// so the dispatch arm a mutation once found unexercised is what runs.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       APressedDigitFiresGoToSpace1) {
  ASSERT_EQ(atrium_core_active_space_index(), 1);
  atrium::NativeHotkeyMap::Get()->ResetForTesting();

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  EXPECT_TRUE(window->DeliverComboForTesting("cmd+1")) << "cmd+1 not handled";
  EXPECT_EQ(atrium_core_active_space_index(), 0);
}

// The rebound case: a chord the compiled-in table does not carry still fires
// its action once hotkeys.json names it.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       AReboundChordFiresGoToSpace1) {
  ASSERT_EQ(atrium_core_active_space_index(), 1);
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  atrium::NativeHotkeyMap::Get()->SetFromJSON(
      base::StringPrintf("{\"schemaVersion\":%d,\"actions\":{\"goToSpace1\":"
                         "[\"cmd+shift+1\"]}}",
                         atrium::kNativeHotkeySchemaVersion));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  EXPECT_TRUE(window->DeliverComboForTesting("cmd+shift+1"))
      << "cmd+shift+1 not handled";
  EXPECT_EQ(atrium_core_active_space_index(), 0);
}

// A digit past the space count moves nothing AND is still consumed. Declining
// would hand cmd+9 to stock select-tab-N, which activates a tab of the shared
// strip and so moves the window to that tab's space.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchBrowserTest,
                       ADigitPastTheLastSpaceIsSwallowedAndMovesNothing) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const int before = atrium_core_active_space_index();

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+9"))
      << "cmd+9 fell through to its stock meaning on a three-space profile";
  EXPECT_EQ(atrium_core_active_space_index(), before);
}

// A second action costs one case, not a new fixture or harness method -
// toggleDevBar ships cmd+shift+d as a default, so this needs no SetFromJSON.
using AtriumNativeHotkeyDispatchDefaultComboBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchDefaultComboBrowserTest,
                       APressedChordFiresToggleDevBar) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->DevBarShownForTesting());

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+shift+d"))
      << "cmd+shift+d not handled";
  EXPECT_TRUE(window->DevBarShownForTesting());
}

// The plain fixture launches tab-less, and EnterSplit needs ActivePane() to
// hold a real tab. Same spaces.json shape AtriumBrowserWindowBrowserTest
// uses for the same reason.
class AtriumNativeHotkeyDispatchWithTabBrowserTest
    : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Test\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"Test\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchWithTabBrowserTest,
                       APressedChordFiresOpenArchive) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsOverviewShowing());

  EXPECT_TRUE(window->DeliverComboForTesting("cmd+shift+a"))
      << "cmd+shift+a not handled";
  EXPECT_TRUE(window->IsOverviewShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchWithTabBrowserTest,
                       APressedChordFiresCloseSplitPane) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());

  EXPECT_TRUE(window->DeliverComboForTesting("shift+ctrl+-"))
      << "shift+ctrl+- not handled";
  EXPECT_FALSE(window->IsSplit());
}

IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchWithTabBrowserTest,
                       APressedChordFiresToggleSplitOrientation) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  ASSERT_EQ(window->split_orientation(), atrium::SplitOrientation::kSideBySide);

  EXPECT_TRUE(window->DeliverComboForTesting("shift+ctrl+\\"))
      << "shift+ctrl+\\ not handled";
  EXPECT_EQ(window->split_orientation(), atrium::SplitOrientation::kStacked);
}

// Distinct from toggleDevBar above, which flips the URL toolbar instead.
IN_PROC_BROWSER_TEST_F(AtriumNativeHotkeyDispatchDefaultComboBrowserTest,
                       APressedChordFiresToggleDeveloperMode) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  PrefService* prefs = browser()->profile()->GetPrefs();
  const bool before = prefs->GetBoolean(prefs::kExtensionsUIDeveloperMode);

  EXPECT_TRUE(window->DeliverComboForTesting("opt+ctrl+d"))
      << "opt+ctrl+d not handled";
  EXPECT_NE(before, prefs->GetBoolean(prefs::kExtensionsUIDeveloperMode));
}

// nextTab and previousTab, one case per chord. Only cmd+shift+] had ever been
// pressed by anything, and the report was that the keys do nothing.
class AtriumTabCycleHotkeyBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(spaces_path, SpacesJSON()));
    spaces_env_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    spaces_env_.reset();
  }

  virtual std::string SpacesJSON() {
    return "{\"spaces\":[{\"id\":\"33333333-3333-3333-3333-333333333333\","
           "\"name\":\"Cycle\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
           "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
           "\"pinnedTabs\":[],\"tabs\":["
           "{\"lastActiveAt\":3.0,\"url\":\"https://one.test/\","
           "\"title\":\"One\"},"
           "{\"lastActiveAt\":2.0,\"url\":\"https://two.test/\","
           "\"title\":\"Two\"},"
           "{\"lastActiveAt\":1.0,\"url\":\"https://three.test/\","
           "\"title\":\"Three\"}]}],"
           "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
           "\"activeSpaceID\":\"33333333-3333-3333-3333-333333333333\"}}";
  }

  // The compiled-in table carries one chord per action. The other two reach a
  // real launch through the hotkeys.json seed, which no browsertest runs.
  void BindEveryCycleChord() {
    atrium::NativeHotkeyMap::Get()->ResetForTesting();
    atrium::NativeHotkeyMap::Get()->SetFromJSON(base::StringPrintf(
        "{\"schemaVersion\":%d,\"actions\":{"
        "\"nextTab\":[\"cmd+opt+down\",\"cmd+j\",\"cmd+shift+]\"],"
        "\"previousTab\":[\"cmd+opt+up\",\"cmd+k\",\"cmd+shift+[\"]}}",
        atrium::kNativeHotkeySchemaVersion));
  }

  // The WebContents, not its URL: the cycle reaches a cold row by REOPENING
  // it, so the tab the window shows afterwards is a different object whether
  // the target was live or cold, and no navigation has to commit first.
  void ExpectChordMovesTheShownTab(std::string_view combo) {
    BindEveryCycleChord();
    auto* window =
        static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
    ASSERT_TRUE(window);
    content::WebContents* before =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(before);

    EXPECT_TRUE(window->DeliverComboForTesting(combo))
        << combo << " was declined, so it fell through to its stock meaning";
    EXPECT_NE(browser()->tab_strip_model()->GetActiveWebContents(), before)
        << combo << " was handled but the window still shows the same tab";
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> spaces_env_;
};

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest, CmdOptDownMovesNext) {
  ExpectChordMovesTheShownTab("cmd+opt+down");
}

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest, CmdJMovesNext) {
  ExpectChordMovesTheShownTab("cmd+j");
}

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest,
                       CmdShiftRightBracketMovesNext) {
  ExpectChordMovesTheShownTab("cmd+shift+]");
}

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest, CmdOptUpMovesPrevious) {
  ExpectChordMovesTheShownTab("cmd+opt+up");
}

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest, CmdKMovesPrevious) {
  ExpectChordMovesTheShownTab("cmd+k");
}

IN_PROC_BROWSER_TEST_F(AtriumTabCycleHotkeyBrowserTest,
                       CmdShiftLeftBracketMovesPrevious) {
  ExpectChordMovesTheShownTab("cmd+shift+[");
}

// Two space tabs, one of them held by a favourite tile. The tile hides that
// tab's row, so the space draws ONE row and the walk used to refuse - the
// reported "the keys do nothing", on a space whose tabs are mostly favourites.
class AtriumTabCycleFavoriteHotkeyBrowserTest
    : public AtriumTabCycleHotkeyBrowserTest {
 protected:
  std::string SpacesJSON() override {
    return "{\"spaces\":[{\"id\":\"44444444-4444-4444-4444-444444444444\","
           "\"name\":\"Favs\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
           "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
           "\"pinnedTabs\":[],\"tabs\":["
           "{\"lastActiveAt\":2.0,\"url\":\"https://claimed.test/\","
           "\"title\":\"Claimed\"},"
           "{\"lastActiveAt\":1.0,\"url\":\"https://plain.test/\","
           "\"title\":\"Plain\"}]}],"
           "\"favorites\":[{\"url\":\"https://claimed.test/\","
           "\"title\":\"Fav\",\"profileID\":\"Default\"}],"
           "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
           "\"activeSpaceID\":\"44444444-4444-4444-4444-444444444444\"}}";
  }

  // Whichever tab the window restored: the claim is what binds the tile and
  // hides the row, and which of the two is live does not change that.
  void ClaimTheShownTabForTheTile() {
    content::WebContents* shown =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(shown);
    atrium::SetFavoriteKeyForTab(shown, "https://claimed.test/");
  }
};

IN_PROC_BROWSER_TEST_F(AtriumTabCycleFavoriteHotkeyBrowserTest,
                       ATileHeldTabIsStillPartOfTheCycle) {
  ClaimTheShownTabForTheTile();
  ExpectChordMovesTheShownTab("cmd+opt+down");
}

// The Inputs pane's write end to end: OnSurfaceMessage through OnHotkeysSaved
// to SetFromJSON, the same path a real chrome.send takes - not SetFromJSON
// called directly, which the fixtures above already cover.
class AtriumSettingsHotkeysSaveBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    hotkeys_path_ = temp_dir_.GetPath().AppendASCII("hotkeys.json");
    hotkeys_env_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_HOTKEYS_FILE", hotkeys_path_.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    hotkeys_env_.reset();
  }

  void TearDownOnMainThread() override {
    atrium::AtriumSettingsWindow::CloseForTesting();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath hotkeys_path_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> hotkeys_env_;
};

IN_PROC_BROWSER_TEST_F(AtriumSettingsHotkeysSaveBrowserTest,
                       ASavedRebindReachesTheNativeMapWithoutARelaunch) {
  atrium::NativeHotkeyMap::Get()->ResetForTesting();
  atrium::AtriumSettingsWindow::Show();
  atrium::AtriumSettingsWindow* settings =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::WaitForLoadStop(page));

  base::ListValue args;
  args.Append(
      R"({"schemaVersion":2,"actions":{"toggleSidebar":["cmd+shift+j"]}})");
  settings->OnSurfaceMessage("settingsHotkeysChanged", args);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::NativeHotkeyMap::Get()
               ->CombosFor(atrium::NativeHotkeyAction::kToggleSidebar) ==
           std::vector<std::string>{"cmd+shift+j"};
  })) << "the write never reached NativeHotkeyMap::SetFromJSON";
}

}  // namespace
