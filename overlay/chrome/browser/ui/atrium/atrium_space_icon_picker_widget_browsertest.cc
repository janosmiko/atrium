// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_space_icon_picker_widget_browsertest.cc

#include "chrome/browser/ui/atrium/atrium_browser_window.h"

#include <string>
#include <string_view>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

using AtriumSpaceIconPickerTest = InProcessBrowserTest;

// Two empty spaces - the default profile these other tests run against
// renders no space dots at all.
constexpr char kTwoSpaceFixture[] =
    R"({"spaces":[)"
    R"({"id":"work","name":"Work","emoji":"W","tint":"#111111",)"
    R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
    R"("order":0,"groups":[]},)"
    R"({"id":"home","name":"Home","emoji":"H","tint":"#222222",)"
    R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
    R"("order":1,"groups":[]})"
    R"(],"appState":{"activeSpaceID":"work","sidebarWidth":280,)"
    R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";

base::Value SidebarModel(Browser* browser) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  CHECK(parsed) << "BuildSidebarTabsJSON did not return valid JSON";
  return std::move(*parsed);
}

std::string FirstSpaceFirstTabTitle(Browser* browser) {
  base::Value model = SidebarModel(browser);
  const base::ListValue* spaces = model.GetDict().FindList("spaces");
  if (!spaces || spaces->empty()) {
    return std::string();
  }
  const base::ListValue* tabs = (*spaces)[0].GetDict().FindList("tabs");
  if (!tabs || tabs->empty()) {
    return std::string();
  }
  const std::string* title = (*tabs)[0].GetDict().FindString("title");
  return title ? *title : std::string();
}

std::string SpaceIconAt(Browser* browser, size_t position) {
  base::Value model = SidebarModel(browser);
  const base::ListValue* spaces = model.GetDict().FindList("spaces");
  if (!spaces || position >= spaces->size()) {
    return std::string();
  }
  const std::string* icon = (*spaces)[position].GetDict().FindString("icon");
  return icon ? *icon : std::string();
}

// The live sidebar position of the space with `space_id`, or -1.
int SpaceIndexForId(const std::string& space_id) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return -1;
  }
  const std::string slot_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(slot_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return -1;
  }
  return parsed->GetDict().FindInt("index").value_or(-1);
}

void SpinFor(base::TimeDelta delay) {
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), delay);
  loop.Run();
}

void OpenFixture(base::ScopedTempDir& dir, std::string_view json) {
  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  CHECK(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  CHECK(base::WriteFile(spaces_path, json));
  CHECK(atrium_core_open(spaces_path.value().c_str(), 1.0));
}

void OpenTwoSpaceFixture(base::ScopedTempDir& dir) {
  OpenFixture(dir, kTwoSpaceFixture);
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest, OpenShowsThePicker) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsSpaceIconPickerShowing());

  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  })) << "ShowSpaceIconPicker did not bring the widget up";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       EscapeHidesAndTheSidebarHoldsFocus) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));
  content::WebContents* picker = window->SpaceIconPickerContentsForTesting();
  ASSERT_TRUE(picker);
  ASSERT_TRUE(content::WaitForLoadStop(picker));

  EXPECT_TRUE(window->SendSpaceIconPickerEscapeForTesting());
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsSpaceIconPickerShowing();
  })) << "Escape did not close the picker";

  content::WebContents* sidebar = window->sidebar_contents();
  ASSERT_TRUE(sidebar);
  // The default test profile renders no space dots (atriumFocusSpaceDot has
  // nothing to select), so this checks the weaker, always-available half of
  // Close()'s contract: the sidebar page itself reclaimed focus.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "document.hasFocus()").ExtractBool();
  })) << "focus did not return to the sidebar";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       SnapshotPushWithoutASpacesChangeStaysOpen) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));

  // A favicon/title-only rebuild reaches the same code path a real spaces
  // reorder does - only the "spaces" list's own signature tells them apart.
  window->PushSidebarSnapshot();
  window->FlushSidebarSnapshotForTesting();

  EXPECT_TRUE(window->IsSpaceIconPickerShowing())
      << "a snapshot push with no spaces-list change closed the picker";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       StaleSidebarSpaceIconSetIsIgnored) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));
  content::WebContents* picker = window->SpaceIconPickerContentsForTesting();
  ASSERT_TRUE(picker);
  ASSERT_TRUE(content::WaitForLoadStop(picker));

  // A message for a different space's index must not be treated as this
  // picker's own pick - reachable if a second dot's picker is still landing
  // its own close.
  ASSERT_TRUE(content::ExecJs(picker, "chrome.send('sidebarSpaceIconSet', "
                                     "[1, 'ph:star'])"));
  // A value the grid never renders (too long to be a glyph or a ph: name)
  // must not reach the sidebar gate either.
  ASSERT_TRUE(content::ExecJs(
      picker,
      "chrome.send('sidebarSpaceIconSet', [0, "
      "'ph:not-a-real-icon-name-that-goes-on-forever'])"));

  EXPECT_TRUE(window->IsSpaceIconPickerShowing())
      << "a stale or invalid sidebarSpaceIconSet closed the picker";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       ALiveTabMutationInTheOpenSpaceStaysOpen) {
  ASSERT_TRUE(embedded_test_server()->Start());
  // Outlives `dir` below, so its temp-directory cleanup on scope exit stays
  // inside the allowance rather than crashing the DCHECK at teardown.
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);

  // Lands in "work" (activeSpaceID), the only live row either space has.
  content::WebContents* tab = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::WaitForLoadStop(tab));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));

  ASSERT_TRUE(content::ExecJs(tab, "document.title = 'Changed'"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return FirstSpaceFirstTabTitle(browser()) == "Changed";
  })) << "the title change never reached the sidebar model";

  window->PushSidebarSnapshot();
  window->FlushSidebarSnapshotForTesting();

  EXPECT_TRUE(window->IsSpaceIconPickerShowing())
      << "a live tab title change inside the open space closed the picker";
}

// Two spaces with the same name/color/icon - the surface alone cannot tell
// them apart, only the native id the picker captured at open time can.
constexpr char kIdenticalSpacesFixture[] =
    R"({"spaces":[)"
    R"({"id":"alpha","name":"New Space","emoji":"N","tint":"#333333",)"
    R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
    R"("order":0,"groups":[]},)"
    R"({"id":"beta","name":"New Space","emoji":"N","tint":"#333333",)"
    R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
    R"("order":1,"groups":[]})"
    R"(],"appState":{"activeSpaceID":"alpha","sidebarWidth":280,)"
    R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       APickAfterASwapChangesTheRightSpace) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenFixture(dir, kIdenticalSpacesFixture);

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));
  content::WebContents* picker = window->SpaceIconPickerContentsForTesting();
  ASSERT_TRUE(picker);
  ASSERT_TRUE(content::WaitForLoadStop(picker));

  // "alpha" moves from 0 to 1 while the picker still thinks it opened on 0.
  ASSERT_TRUE(atrium_core_reorder_space(0, 1));
  ASSERT_EQ(1, SpaceIndexForId("alpha"));

  ASSERT_TRUE(content::ExecJs(
      picker, "chrome.send('sidebarSpaceIconSet', [0, 'ph:star'])"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return SpaceIconAt(browser(), 1) == "ph:star";
  })) << "the write did not land on alpha, the space the picker actually "
         "opened on";
  EXPECT_EQ("N", SpaceIconAt(browser(), 0))
      << "beta picked up a write meant for alpha";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       EscapeAfterASwapFocusesTheSpacesNewDot) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenFixture(dir, kIdenticalSpacesFixture);

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->sidebar_contents();
  ASSERT_TRUE(sidebar);

  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));
  content::WebContents* picker = window->SpaceIconPickerContentsForTesting();
  ASSERT_TRUE(picker);
  ASSERT_TRUE(content::WaitForLoadStop(picker));

  ASSERT_TRUE(atrium_core_reorder_space(0, 1));
  ASSERT_EQ(1, SpaceIndexForId("alpha"));

  // Installed last: a sidebar rebuild from the reorder would replace it.
  ASSERT_TRUE(content::ExecJs(
      sidebar,
      "window.__dotCalls = [];"
      "window.atriumFocusSpaceDot = (i) => { window.__dotCalls.push(i); };"));
  ASSERT_TRUE(window->SendSpaceIconPickerEscapeForTesting());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !window->IsSpaceIconPickerShowing();
  }));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "window.__dotCalls.length")
               .ExtractInt() > 0;
  })) << "Escape never asked the sidebar to focus a dot";
  EXPECT_EQ("[1]", content::EvalJs(sidebar, "JSON.stringify(window.__dotCalls)")
                       .ExtractString())
      << "focus went to the stale index, not alpha's new position";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest,
                       OpeningWithoutACoreSpaceStaysClosed) {
  atrium_core_close();
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));

  SpinFor(base::Milliseconds(500));
  EXPECT_FALSE(window->IsSpaceIconPickerShowing())
      << "the picker opened with no space id, so every pick would be dropped";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceIconPickerTest, PickingACellSetsTheIcon) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  OpenTwoSpaceFixture(dir);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowSpaceIconPicker(0, "", gfx::Rect(40, 700, 8, 8));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->IsSpaceIconPickerShowing();
  }));
  content::WebContents* picker = window->SpaceIconPickerContentsForTesting();
  ASSERT_TRUE(picker);
  ASSERT_TRUE(content::WaitForLoadStop(picker));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(picker,
                           "document.querySelector('.spicon-cell') !== null")
        .ExtractBool();
  })) << "the grid never rendered";

  // A simulated native click drops the gesture in this VM's browsertests -
  // a real DOM event exercises the same listener the mouse would.
  ASSERT_TRUE(content::ExecJs(
      picker,
      "document.querySelector('.spicon-cell').click()"));

  // sidebarSpaceIconSet forwards through parent_->OnSurfaceMessage, which
  // closes the picker as its first act - proof the write reached the router.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsSpaceIconPickerShowing();
  })) << "picking a cell did not close the picker";
}

}  // namespace
}  // namespace atrium
