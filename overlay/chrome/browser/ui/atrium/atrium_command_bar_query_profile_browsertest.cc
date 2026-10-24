// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

// A search must leave through the engine of the space on screen. Both tests
// below put the active space on a profile that owns no window, which is the
// state the old resolution answered with the WRONG profile in.
namespace {

class AtriumCommandBarQueryProfileTest : public InProcessBrowserTest {
 protected:
  // Two spaces, the second owned by a profile this fixture deliberately never
  // gives a BrowserController to.
  void OpenCoreWithTwoProfiles() {
    static constexpr char kDoc[] =
        R"({"spaces":[)"
        R"({"id":"s1","name":"A","emoji":"A","tint":"#111111",)"
        R"("profileID":"Default","pinnedTabs":[],"tabs":[],"order":0,"groups":[]},)"
        R"({"id":"s2","name":"B","emoji":"B","tint":"#222222",)"
        R"("profileID":"Spare","pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],)"
        R"("favorites":[],)"
        R"("profiles":[{"id":"Default","name":"A"},{"id":"Spare","name":"B"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(path, kDoc));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  // Resident, so LoadedProfileForId answers it - and with no controller of its
  // own, so ControllerFor cannot.
  Profile* CreateSpareProfile() {
    ProfileManager* manager = g_browser_process->profile_manager();
    if (!manager) {
      return nullptr;
    }
    return &profiles::testing::CreateProfileSync(
        manager, manager->user_data_dir().AppendASCII("Spare"));
  }

  void SetDefaultEngine(Profile* profile, const std::string& host) {
    TemplateURLService* engines =
        TemplateURLServiceFactory::GetForProfile(profile);
    ASSERT_TRUE(engines);
    search_test_utils::WaitForTemplateURLServiceToLoad(engines);
    TemplateURLData data;
    data.SetShortName(base::UTF8ToUTF16(host));
    data.SetKeyword(base::UTF8ToUTF16(host));
    data.SetURL("https://" + host + "/s?q={searchTerms}");
    data.safe_for_autoreplace = false;
    TemplateURL* added = engines->Add(std::make_unique<TemplateURL>(data));
    ASSERT_TRUE(added);
    engines->SetUserSelectedDefaultSearchProvider(added);
  }

  base::ScopedTempDir dir_;
};

// GAP 1. The space on screen belongs to Spare, which has no window - the state
// a deferred profile is in at startup, and the one a torn-down window-slot
// stays in for good (its controller keeps a null browser_ forever).
IN_PROC_BROWSER_TEST_F(AtriumCommandBarQueryProfileTest,
                       SearchUsesTheSpacesProfileWhenItOwnsNoWindow) {
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithTwoProfiles());
  Profile* spare = CreateSpareProfile();
  ASSERT_TRUE(spare);
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(browser()->profile(), "alpha.test"));
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(spare, "bravo.test"));

  ASSERT_TRUE(atrium::LoadedProfileForId("Spare"))
      << "the profile must be resident, or this measures the load and not the "
         "resolution";
  ASSERT_FALSE(atrium::BrowserController::ControllerFor(
      atrium::BrowserController::kDefaultSlot, "Spare"))
      << "a controller for Spare would let the OLD resolution pass too";

  ASSERT_TRUE(atrium_core_set_active_space_index(1));

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  controller->QueryCommandBar("puppies");
  content::RunAllTasksUntilIdle();
  controller->AcceptCommandBarMatch(0);

  ASSERT_EQ(count_before + 1, tabs->count())
      << "Enter opened nothing at all";
  EXPECT_EQ(tabs->GetActiveWebContents()->GetVisibleURL().host(), "bravo.test")
      << "the search left through the WINDOW's engine, not the engine of the "
         "space the user is looking at";
}

// GAP 2. The switch happens with the palette already open, so SetQueryProfile
// rebuilds at accept time and takes the result the row index names with it.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarQueryProfileTest,
                       AcceptAfterASwitchUsesTheNewProfilesEngine) {
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithTwoProfiles());
  Profile* spare = CreateSpareProfile();
  ASSERT_TRUE(spare);
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(browser()->profile(), "alpha.test"));
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(spare, "bravo.test"));

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  // Queried on Default's space, so the rows the page holds are Default's.
  controller->QueryCommandBar("puppies");
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(atrium_core_set_active_space_index(1));
  controller->AcceptCommandBarMatch(0);

  ASSERT_EQ(count_before + 1, tabs->count())
      << "the rebuild emptied the result and Enter resolved nothing";
  EXPECT_EQ(tabs->GetActiveWebContents()->GetVisibleURL().host(), "bravo.test")
      << "Enter after the switch did not re-ask on the space's own engine";
}

// The index is deliberately not the top row. Whatever the user had selected
// ranked in the profile they left, so a post-switch accept is answered by the
// new profile's default match rather than by that row.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarQueryProfileTest,
                       AcceptOfANonTopRowAfterASwitchUsesTheNewProfilesEngine) {
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithTwoProfiles());
  Profile* spare = CreateSpareProfile();
  ASSERT_TRUE(spare);
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(browser()->profile(), "alpha.test"));
  ASSERT_NO_FATAL_FAILURE(SetDefaultEngine(spare, "bravo.test"));

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  controller->QueryCommandBar("puppies");
  content::RunAllTasksUntilIdle();

  ASSERT_TRUE(atrium_core_set_active_space_index(1));
  controller->AcceptCommandBarMatch(3);

  ASSERT_EQ(count_before + 1, tabs->count())
      << "a non-top row after a switch resolved nothing at all";
  EXPECT_EQ(tabs->GetActiveWebContents()->GetVisibleURL().host(), "bravo.test")
      << "the accept fell back to the row's own profile instead of answering "
         "on the engine of the space now on screen";
}

}  // namespace
