// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <vector>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/scoped_environment_variable_override.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_for_profile.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_profile_page_url.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

namespace {

using AtriumProfileRoutesBrowserTest = InProcessBrowserTest;

// One test: settingsProfileOpenPage lands the page in a tab of the NAMED
// profile's window, not of whichever window happened to be in front. Two
// profiles are live and the front one is deliberately the wrong one, so a
// route that reached for the last active window would pass every count
// assertion and still be wrong.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesBrowserTest,
                       OpenPageLandsInTheNamedProfilesWindow) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  atrium::BrowserController* other =
      atrium::BrowserController::CreateForProfile(&second, /*show=*/true,
                                                   /*startup_url=*/"about:blank");
  ASSERT_TRUE(other);

  Browser* into =
      atrium::BrowserForProfileId(second.GetBaseName().AsUTF8Unsafe());
  ASSERT_TRUE(into);
  EXPECT_EQ(&second, into->profile())
      << "the window belongs to the profile that was named, not to the "
         "front one";

  const int before = into->tab_strip_model()->count();
  const int front_before = browser()->tab_strip_model()->count();
  // Through the route, not through its ingredients. Calling BrowserForProfileId
  // and AddTabAt here instead leaves the handler itself untested: it can be made
  // unreachable and this still passes.
  if (!atrium::AtriumSettingsWindow::IsShowing()) {
    atrium::AtriumSettingsWindow::Show();
  }
  atrium::AtriumSettingsWindow* window =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append(second.GetBaseName().AsUTF8Unsafe());
  args.Append("passwords");
  window->OnSurfaceMessage("settingsProfileOpenPage", args);

  EXPECT_EQ(into->tab_strip_model()->count(), before + 1);
  EXPECT_EQ(browser()->tab_strip_model()->count(), front_before)
      << "and nothing landed in the other profile's window";
}

// The only Browser live for `profile`, found through the same public
// collection BrowserForProfileId scans - BrowserController::window() is
// private, so a test cannot go through the controller it got back.
Browser* OnlyBrowserForProfile(Profile* profile) {
  Browser* found = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        Browser* browser = window->GetBrowserForMigrationOnly();
        if (browser->profile() == profile) {
          found = browser;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return found;
}

// The hidden bootstrap placeholder every resident profile gets is realised
// but never Shown, so the page must open a real window rather than land
// silently in it.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesBrowserTest,
                       OpenPageOpensAWindowWhenTheProfileHasNone) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  atrium::BrowserController* hidden =
      atrium::BrowserController::CreateForProfile(&second, /*show=*/false,
                                                   /*startup_url=*/"");
  ASSERT_TRUE(hidden);
  Browser* hidden_browser = OnlyBrowserForProfile(&second);
  ASSERT_TRUE(hidden_browser);

  if (!atrium::AtriumSettingsWindow::IsShowing()) {
    atrium::AtriumSettingsWindow::Show();
  }
  atrium::AtriumSettingsWindow* window =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(window);

  const int front_before = browser()->tab_strip_model()->count();
  base::ListValue args;
  args.Append(second.GetBaseName().AsUTF8Unsafe());
  args.Append("passwords");
  window->OnSurfaceMessage("settingsProfileOpenPage", args);

  EXPECT_EQ(hidden_browser->tab_strip_model()->count(), 0)
      << "the invisible bootstrap placeholder must not silently take the tab";
  EXPECT_EQ(browser()->tab_strip_model()->count(), front_before)
      << "and nothing landed in the front window";

  Browser* into =
      atrium::BrowserForProfileId(second.GetBaseName().AsUTF8Unsafe());
  ASSERT_TRUE(into);
  EXPECT_NE(into, hidden_browser) << "a real window should have been opened";
  EXPECT_EQ(into->tab_strip_model()->count(), 1);
}

// A safe-looking id nothing has ever heard of - never in the launch plan,
// so EnsureProfileLoaded cannot help it either - must refuse visibly rather
// than silently doing nothing.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesBrowserTest,
                       OpenPageRefusesAProfileThatWasNeverBootstrapped) {
  const std::string unknown_id = "NeverBootstrapped";
  ASSERT_TRUE(atrium::ProfileBootstrap::IsSafeProfileId(unknown_id));
  ASSERT_FALSE(atrium::LoadedProfileForId(unknown_id));

  if (!atrium::AtriumSettingsWindow::IsShowing()) {
    atrium::AtriumSettingsWindow::Show();
  }
  atrium::AtriumSettingsWindow* window =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(window);

  const int front_before = browser()->tab_strip_model()->count();
  base::ListValue args;
  args.Append(unknown_id);
  args.Append("passwords");
  window->OnSurfaceMessage("settingsProfileOpenPage", args);

  EXPECT_EQ(browser()->tab_strip_model()->count(), front_before)
      << "nothing should have landed anywhere";
  EXPECT_FALSE(atrium::LoadedProfileForId(unknown_id))
      << "the click must not have invented a profile to satisfy itself";
}

// The Browsers BrowserForProfileId's own scan walks, most recently activated
// first.
std::vector<Browser*> BrowsersInActivationOrder() {
  std::vector<Browser*> out;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        out.push_back(window->GetBrowserForMigrationOnly());
        return true;
      },
      BrowserCollection::Order::kActivation);
  return out;
}

// An off-the-record profile's GetBaseName() equals its parent's - the alias
// atrium_browser_for_profile.cc's scan must not let it stand in for the
// parent's own window.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesBrowserTest,
                       IncognitoWindowIsNeverPickedForItsParentProfile) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());
  // Incognito is a profile property, so the window is TYPE_NORMAL and carries
  // a controller like any other - which is what puts it in the scan at all.
  ASSERT_TRUE(atrium::BrowserController::ControllerForBrowser(incognito));
  const std::vector<Browser*> order = BrowsersInActivationOrder();
  ASSERT_FALSE(order.empty());
  ASSERT_EQ(order.front(), incognito)
      << "the incognito window has to be the more recently activated one, or "
         "the parent's own window would win for the wrong reason";

  Browser* into = atrium::BrowserForProfileId(
      browser()->profile()->GetBaseName().AsUTF8Unsafe());
  ASSERT_TRUE(into);
  EXPECT_NE(into, incognito)
      << "an incognito window answers to its parent's profile_id and must "
         "never take the parent's tab";
  EXPECT_EQ(into, browser());
}

// The snapshot the page is pushed: the counts are the C++ side's job, because
// the page has no way to tell a profile with no spaces from one it has not
// been told about. Two profiles, with different space counts and residency,
// so a count or availability read off the wrong entry is caught rather than
// masked by both profiles happening to agree.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesBrowserTest,
                       TheSnapshotCarriesCountsAndPrefValues) {
  const std::string doc = R"({"spaces":[
      {"id":"A","name":"Work","profileID":"Default","tabs":[],"pinnedTabs":[]}],
      "profiles":[{"id":"Default","name":"Work"},{"id":"Spare","name":"Spare"}]})";

  std::optional<base::Value> out = base::JSONReader::Read(
      atrium::WithProfileDetail(doc), base::JSON_PARSE_RFC);
  ASSERT_TRUE(out && out->is_dict());
  const base::ListValue* profiles = out->GetDict().FindList("profiles");
  ASSERT_TRUE(profiles);
  ASSERT_EQ(profiles->size(), 2u);

  const base::DictValue& resident = (*profiles)[0].GetDict();
  ASSERT_EQ(*resident.FindString("id"), "Default");
  EXPECT_EQ(resident.FindInt("spaceCount"), 1);
  ASSERT_TRUE(resident.FindBool("available").has_value());
  EXPECT_TRUE(*resident.FindBool("available"))
      << "Default is the resident test profile";
  // Reaches atrium_profile_prefs.cc's ProfileSuggestEnabled/ProfileDownloadDir:
  // a resident profile answers both, so the keys must be present.
  EXPECT_TRUE(resident.FindBool("suggestEnabled").has_value())
      << "Default is resident - its own pref should answer";
  EXPECT_TRUE(resident.FindString("downloadDir"))
      << "Default is resident - its own pref should answer";

  const base::DictValue& spare = (*profiles)[1].GetDict();
  ASSERT_EQ(*spare.FindString("id"), "Spare");
  EXPECT_EQ(spare.FindInt("spaceCount"), 0)
      << "Spare owns no space in the document - a count read off the wrong "
         "profile would show 1 here instead";
  ASSERT_TRUE(spare.FindBool("available").has_value());
  EXPECT_FALSE(*spare.FindBool("available")) << "Spare was never loaded";
  // Skipped, not defaulted, so the page can tell "off" from "cannot say".
  EXPECT_FALSE(spare.FindBool("suggestEnabled").has_value())
      << "Spare was never loaded - the key must be skipped";
  EXPECT_FALSE(spare.FindString("downloadDir"))
      << "Spare was never loaded - the key must be skipped";
}

// Drives a route through OnSurfaceMessage and checks what atrium_core_save()
// wrote to disk, so a wrong-argument or skipped-save route fails a test
// rather than passing on a silent no-op.
class AtriumProfileRoutesCoreBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    spaces_path_ = temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path_,
        R"({"spaces":[{"id":"11111111-1111-1111-1111-111111111111",)"
        R"("name":"Test","emoji":"","tint":"oceanBlue","profileID":"Default",)"
        R"("order":0,"pinnedTabs":[],"tabs":[]}],)"
        R"("profiles":[{"id":"Default","name":"Work"},)"
        R"({"id":"Spare","name":"Spare"}],)"
        R"("appState":{"schemaVersion":1,"sidebarWidth":220,)"
        R"("activeSpaceID":"11111111-1111-1111-1111-111111111111"}})"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path_.value());
    // A process-global singleton keyed on the path it first opened - without
    // closing it, a later test in this binary reads whatever this one wrote.
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

  atrium::AtriumSettingsWindow* Window() {
    if (!atrium::AtriumSettingsWindow::IsShowing()) {
      atrium::AtriumSettingsWindow::Show();
    }
    return atrium::AtriumSettingsWindow::ForTesting();
  }

  // Flushes the ThreadPool save the route posts, then returns what landed
  // on disk.
  std::string ReadSpacesFile() {
    content::RunAllTasksUntilIdle();
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(spaces_path_, &contents));
    return contents;
  }

  base::FilePath spaces_path_;

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       AddedCreatesANewProfileRecord) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  window->OnSurfaceMessage("settingsProfileAdded", args);

  const std::string on_disk = ReadSpacesFile();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(on_disk, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::ListValue* profiles = parsed->GetDict().FindList("profiles");
  ASSERT_TRUE(profiles);
  EXPECT_EQ(profiles->size(), 3u)
      << "the added profile never reached disk";
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       RenamedWritesTheNewNameForTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append("Default");
  args.Append("Renamed");
  window->OnSurfaceMessage("settingsProfileRenamed", args);

  const std::string on_disk = ReadSpacesFile();
  EXPECT_NE(on_disk.find("\"name\":\"Renamed\""), std::string::npos)
      << "Default's new name never reached disk";
  EXPECT_NE(on_disk.find("\"name\":\"Spare\""), std::string::npos)
      << "Spare's name changed though it was not the one named";
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       DeletedRemovesOnlyTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append("Spare");
  window->OnSurfaceMessage("settingsProfileDeleted", args);

  const std::string on_disk = ReadSpacesFile();
  EXPECT_EQ(on_disk.find("\"id\":\"Spare\""), std::string::npos)
      << "Spare is still on disk";
  EXPECT_NE(on_disk.find("\"id\":\"Default\""), std::string::npos)
      << "Default was removed though it was not the one named";
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       SpaceProfileChangedPointsTheSpaceAtTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append(0);
  args.Append("Spare");
  window->OnSurfaceMessage("settingsSpaceProfileChanged", args);

  const std::string on_disk = ReadSpacesFile();
  EXPECT_NE(on_disk.find("\"profileID\":\"Spare\""), std::string::npos)
      << "the space's profile never reached disk";
}

// The pane's counts come from a re-read of spaces.json while the move's write
// fsyncs on another worker, and nothing pushes a second time - an unordered
// read leaves pre-move counts up for the rest of the session.
IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       SpaceProfileChangedPushesThePostMoveCounts) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  content::WebContents* page = window->WebContentsForTesting();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::WaitForLoadStop(page));

  // The page's own setter is replaced so the test reads the bytes a push
  // carried rather than what the pane made of them.
  ASSERT_TRUE(content::ExecJs(
      page, "window.atriumSetSpaces = doc => { window.__pushed = doc; };"));
  // Nothing is pushed at all before the page has announced itself.
  base::ListValue ready;
  window->OnSurfaceMessage("settingsReady", ready);
  content::RunAllTasksUntilIdle();
  // Null from here on means no push since the move.
  ASSERT_TRUE(content::ExecJs(page, "window.__pushed = null;"));

  base::ListValue args;
  args.Append(0);
  args.Append("Spare");
  window->OnSurfaceMessage("settingsSpaceProfileChanged", args);
  content::RunAllTasksUntilIdle();

  // A push and this read share the frame's message pipe, so a push that was
  // sent has already run by the time the read evaluates.
  const std::string pushed =
      content::EvalJs(page, "JSON.stringify(window.__pushed)").ExtractString();
  ASSERT_NE(pushed, "null") << "the move pushed nothing to the pane";
  std::optional<base::Value> parsed =
      base::JSONReader::Read(pushed, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::ListValue* profiles = parsed->GetDict().FindList("profiles");
  ASSERT_TRUE(profiles);
  ASSERT_EQ(profiles->size(), 2u);
  for (const base::Value& entry : *profiles) {
    const base::DictValue& profile = entry.GetDict();
    const std::string* id = profile.FindString("id");
    ASSERT_TRUE(id);
    if (*id == "Default") {
      EXPECT_EQ(profile.FindInt("spaceCount"), 0)
          << "the pane still counts the space under the profile it left";
    } else if (*id == "Spare") {
      EXPECT_EQ(profile.FindInt("spaceCount"), 1)
          << "the pane never got the space the move added";
    }
  }
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       SearchEngineChangedWritesTheEngineForTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append("Default");
  args.Append("duckduckgo");
  window->OnSurfaceMessage("settingsProfileSearchEngineChanged", args);

  const std::string on_disk = ReadSpacesFile();
  EXPECT_NE(on_disk.find("\"searchEngine\":\"duckduckgo\""), std::string::npos)
      << "Default's engine never reached disk";
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       HibernateChangedWritesModeAndThresholdForTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append("Default");
  args.Append("always");
  args.Append("oneHour");
  window->OnSurfaceMessage("settingsProfileHibernateChanged", args);

  const std::string on_disk = ReadSpacesFile();
  EXPECT_NE(on_disk.find("\"hibernate\":\"always\""), std::string::npos)
      << "Default's hibernate mode never reached disk";
  EXPECT_NE(on_disk.find("\"hibThreshold\":\"oneHour\""), std::string::npos)
      << "Default's hibernate threshold never reached disk";
}

IN_PROC_BROWSER_TEST_F(AtriumProfileRoutesCoreBrowserTest,
                       SuggestChangedWritesTheChromiumPrefForTheNamedProfile) {
  atrium::AtriumSettingsWindow* window = Window();
  ASSERT_TRUE(window);
  const std::optional<bool> before = atrium::ProfileSuggestEnabled("Default");
  ASSERT_TRUE(before.has_value());

  base::ListValue args;
  args.Append("Default");
  args.Append(!*before);
  window->OnSurfaceMessage("settingsProfileSuggestChanged", args);

  const std::optional<bool> after = atrium::ProfileSuggestEnabled("Default");
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(*after, !*before)
      << "the suggest pref never reached the Chromium profile";
}

// Spare owns a space that is neither active nor window-restored, so the real
// launch plan defers it - the shape the write-time load kick and the
// bootstrap reapply exist for, not a profile the fixture merely forgot to
// load.
class AtriumProfileEngineBootstrapBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    spaces_path_ = temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path_,
        R"({"spaces":[
            {"id":"11111111-1111-1111-1111-111111111111","name":"Work",)"
        R"("emoji":"","tint":"oceanBlue","profileID":"Default","order":0,)"
        R"("pinnedTabs":[],"tabs":[]},)"
        R"({"id":"22222222-2222-2222-2222-222222222222","name":"Away",)"
        R"("emoji":"","tint":"oceanBlue","profileID":"Spare","order":1,)"
        R"("pinnedTabs":[],"tabs":[]}],)"
        R"("profiles":[{"id":"Default","name":"Work"},)"
        R"({"id":"Spare","name":"Spare"}],)"
        R"("appState":{"schemaVersion":1,"sidebarWidth":220,)"
        R"("activeSpaceID":"11111111-1111-1111-1111-111111111111"}})"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path_.value());

    // A keyword the bundled default engine list cannot collide with - the
    // real four (google.com, duckduckgo.com, bing.com, ecosia.org) share
    // their keyword with Chromium's own prepopulated entries, so the mirror
    // refuses every one of them in a browsertest's fully-prepopulated
    // TemplateURLService, proving nothing about the reapply this test targets.
    settings_path_ = temp_dir_.GetPath().AppendASCII("settings.json");
    // The first four keys are required by the loader's FileShape - drop one
    // and the whole file is rejected for the built-in defaults, which mirror
    // fine and leave this test failing on an engine that was never there.
    ASSERT_TRUE(base::WriteFile(
        settings_path_,
        R"({"schemaVersion":1,"appIconVariant":"nord-accent",)"
        R"("animationsEnabled":true,"startupBehavior":"blankTab",)"
        R"("searchEngines":[{"id":"testengine","name":"Test Engine",)"
        R"("keyword":"test-engine.invalid",)"
        R"("url":"https://test-engine.invalid/search?q=%s",)"
        R"("enabled":true}]})"));
    settings_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SETTINGS_FILE", settings_path_.value());

    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
    settings_env_override_.reset();
  }

 private:
  base::FilePath spaces_path_;
  base::FilePath settings_path_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      settings_env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumProfileEngineBootstrapBrowserTest,
                       PickingAnEngineAppliesOnceTheDeferredProfileLoads) {
  ASSERT_FALSE(atrium::LoadedProfileForId("Spare"))
      << "Spare must still be deferred for this test to prove anything";

  if (!atrium::AtriumSettingsWindow::IsShowing()) {
    atrium::AtriumSettingsWindow::Show();
  }
  atrium::AtriumSettingsWindow* window =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(window);

  base::ListValue args;
  args.Append("Spare");
  args.Append("testengine");
  window->OnSurfaceMessage("settingsProfileSearchEngineChanged", args);

  content::RunAllTasksUntilIdle();

  Profile* spare = atrium::LoadedProfileForId("Spare");
  ASSERT_TRUE(spare) << "the write should have kicked the deferred load";
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(spare);
  ASSERT_TRUE(service);
  if (!service->loaded()) {
    search_test_utils::WaitForTemplateURLServiceToLoad(service);
  }

  // Separates the two ways this can fail: a mirror that never ran on the new
  // profile, and an apply that ran too early to find what the mirror added.
  ASSERT_TRUE(service->GetTemplateURLForGUID("atrium:testengine"))
      << "the mirror never gave this profile the fixture engine, so the "
         "apply had nothing to select";

  const TemplateURL* def = service->GetDefaultSearchProvider();
  ASSERT_TRUE(def);
  EXPECT_EQ(def->data().sync_guid, "atrium:testengine")
      << "the default provider, not merely some engine in the list, must "
         "have moved";
}

// Default owns the only space, so it is resident from the first frame - the
// shape a user is actually in when they pick an engine for the profile in
// front of them, not the deferred load above.
class AtriumProfileEngineApplyBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    spaces_path_ = temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path_,
        R"({"spaces":[{"id":"11111111-1111-1111-1111-111111111111",)"
        R"("name":"Work","emoji":"","tint":"oceanBlue","profileID":"Default",)"
        R"("order":0,"pinnedTabs":[],"tabs":[]}],)"
        R"("profiles":[{"id":"Default","name":"Work"}],)"
        R"("appState":{"schemaVersion":1,"sidebarWidth":220,)"
        R"("activeSpaceID":"11111111-1111-1111-1111-111111111111"}})"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path_.value());

    // A keyword no prepopulated entry owns, for the reason the fixture above
    // spells out: the four bundled engines collide and mirror nothing here.
    settings_path_ = temp_dir_.GetPath().AppendASCII("settings.json");
    ASSERT_TRUE(base::WriteFile(
        settings_path_,
        R"({"schemaVersion":1,"appIconVariant":"nord-accent",)"
        R"("animationsEnabled":true,"startupBehavior":"blankTab",)"
        R"("searchEngines":[{"id":"testengine","name":"Test Engine",)"
        R"("keyword":"test-engine.invalid",)"
        R"("url":"https://test-engine.invalid/search?q=%s",)"
        R"("enabled":true},)"
        // A second one, so alternating picks are ten real changes rather than
        // nine re-picks of the engine already in effect.
        R"({"id":"othertestengine","name":"Other Test Engine",)"
        R"("keyword":"other-test-engine.invalid",)"
        R"("url":"https://other-test-engine.invalid/search?q=%s",)"
        R"("enabled":true}]})"));
    settings_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SETTINGS_FILE", settings_path_.value());

    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
    settings_env_override_.reset();
  }

  atrium::AtriumSettingsWindow* Window() {
    if (!atrium::AtriumSettingsWindow::IsShowing()) {
      atrium::AtriumSettingsWindow::Show();
    }
    return atrium::AtriumSettingsWindow::ForTesting();
  }

  // The service loads asynchronously and the mirror runs off that load, so
  // the `atrium:` GUID the apply selects does not exist for either yet.
  TemplateURLService* MirroredServiceForDefault() {
    Profile* profile = atrium::LoadedProfileForId("Default");
    EXPECT_TRUE(profile) << "Default owns the only space and must be resident";
    if (!profile) {
      return nullptr;
    }
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(profile);
    EXPECT_TRUE(service);
    if (service && !service->loaded()) {
      search_test_utils::WaitForTemplateURLServiceToLoad(service);
    }
    content::RunAllTasksUntilIdle();
    return service;
  }

  void PickEngine(const std::string& engine_id) {
    atrium::AtriumSettingsWindow* window = Window();
    ASSERT_TRUE(window);
    base::ListValue args;
    args.Append("Default");
    args.Append(engine_id);
    window->OnSurfaceMessage("settingsProfileSearchEngineChanged", args);
    content::RunAllTasksUntilIdle();
  }

 private:
  base::FilePath spaces_path_;
  base::FilePath settings_path_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      settings_env_override_;
};

// The gesture a user makes after one apply failed: the stored value already
// names the engine, so the core answers "unchanged" and only the live default
// provider is still wrong. Re-picking must still move it.
IN_PROC_BROWSER_TEST_F(AtriumProfileEngineApplyBrowserTest,
                       PickingTheSameEngineTwiceAppliesItAgain) {
  TemplateURLService* service = MirroredServiceForDefault();
  ASSERT_TRUE(service);
  const TemplateURL* before = service->GetDefaultSearchProvider();
  ASSERT_TRUE(before);
  const std::string original_guid = before->data().sync_guid;
  ASSERT_NE(original_guid, "atrium:testengine");

  PickEngine("testengine");
  ASSERT_EQ(service->GetDefaultSearchProvider()->data().sync_guid,
            "atrium:testengine");

  TemplateURL* original = service->GetTemplateURLForGUID(original_guid);
  ASSERT_TRUE(original);
  service->SetUserSelectedDefaultSearchProvider(original);
  ASSERT_EQ(service->GetDefaultSearchProvider()->data().sync_guid,
            original_guid);

  PickEngine("testengine");

  EXPECT_EQ(service->GetDefaultSearchProvider()->data().sync_guid,
            "atrium:testengine")
      << "the second pick of the engine already on disk applied nothing, so "
         "the user cannot retry a failed switch";
}

// the report is that a switch sometimes keeps the old engine,
// so one pass proves nothing. Every pick is read back on the spot.
IN_PROC_BROWSER_TEST_F(AtriumProfileEngineApplyBrowserTest,
                       TenConsecutiveSwitchesAllLand) {
  TemplateURLService* service = MirroredServiceForDefault();
  ASSERT_TRUE(service);

  for (int i = 0; i < 10; ++i) {
    const std::string id = i % 2 == 0 ? "testengine" : "othertestengine";
    PickEngine(id);
    ASSERT_EQ(service->GetDefaultSearchProvider()->data().sync_guid,
              "atrium:" + id)
        << "switch " << i << " kept the engine the one before it set";
  }
}

// the pane's 'Saved, but could not switch' hint is raised only
// by a deferred apply that gave up. A profile that is loaded applies inline, so
// nothing is ever deferred for it and the hint cannot appear.
IN_PROC_BROWSER_TEST_F(AtriumProfileEngineApplyBrowserTest,
                       ALoadedProfileDefersNothingSoItRaisesNoHint) {
  TemplateURLService* service = MirroredServiceForDefault();
  ASSERT_TRUE(service);
  ASSERT_TRUE(atrium::LoadedProfileForId("Default"));

  PickEngine("testengine");

  EXPECT_EQ(service->GetDefaultSearchProvider()->data().sync_guid,
            "atrium:testengine");
  EXPECT_EQ(Window()->PendingEngineAppliesForTesting(), 0u)
      << "the switch went to a deferred wait, which is the only thing that "
         "can end in the pane's failure hint";
}

// Spare is deferred, so a pick for it cannot apply inline and has to wait out
// the load. Two picks then have two waits in flight at once.
class AtriumProfileEngineSupersedeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    spaces_path_ = temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path_,
        R"({"spaces":[
            {"id":"11111111-1111-1111-1111-111111111111","name":"Work",)"
        R"("emoji":"","tint":"oceanBlue","profileID":"Default","order":0,)"
        R"("pinnedTabs":[],"tabs":[]},)"
        R"({"id":"22222222-2222-2222-2222-222222222222","name":"Away",)"
        R"("emoji":"","tint":"oceanBlue","profileID":"Spare","order":1,)"
        R"("pinnedTabs":[],"tabs":[]}],)"
        R"("profiles":[{"id":"Default","name":"Work"},)"
        R"({"id":"Spare","name":"Spare"}],)"
        R"("appState":{"schemaVersion":1,"sidebarWidth":220,)"
        R"("activeSpaceID":"11111111-1111-1111-1111-111111111111"}})"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path_.value());

    // Two engines, both on keywords no prepopulated entry owns - the bundled
    // four collide and mirror nothing in a browsertest.
    settings_path_ = temp_dir_.GetPath().AppendASCII("settings.json");
    ASSERT_TRUE(base::WriteFile(
        settings_path_,
        R"({"schemaVersion":1,"appIconVariant":"nord-accent",)"
        R"("animationsEnabled":true,"startupBehavior":"blankTab",)"
        R"("searchEngines":[{"id":"firstengine","name":"First Engine",)"
        R"("keyword":"first-engine.invalid",)"
        R"("url":"https://first-engine.invalid/search?q=%s",)"
        R"("enabled":true},)"
        R"({"id":"secondengine","name":"Second Engine",)"
        R"("keyword":"second-engine.invalid",)"
        R"("url":"https://second-engine.invalid/search?q=%s",)"
        R"("enabled":true}]})"));
    settings_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SETTINGS_FILE", settings_path_.value());

    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
    settings_env_override_.reset();
  }

 private:
  base::FilePath spaces_path_;
  base::FilePath settings_path_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      settings_env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumProfileEngineSupersedeBrowserTest,
                       ASecondPickSupersedesTheFirstStillWaitingOne) {
  ASSERT_FALSE(atrium::LoadedProfileForId("Spare"))
      << "Spare must still be deferred, or neither pick has to wait";

  if (!atrium::AtriumSettingsWindow::IsShowing()) {
    atrium::AtriumSettingsWindow::Show();
  }
  atrium::AtriumSettingsWindow* window =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(window);

  base::ListValue first;
  first.Append("Spare");
  first.Append("firstengine");
  window->OnSurfaceMessage("settingsProfileSearchEngineChanged", first);
  ASSERT_EQ(window->PendingEngineAppliesForTesting(), 1u)
      << "a pick for a deferred profile cannot apply inline, so it must be "
         "waiting";

  base::ListValue second;
  second.Append("Spare");
  second.Append("secondengine");
  window->OnSurfaceMessage("settingsProfileSearchEngineChanged", second);
  EXPECT_EQ(window->PendingEngineAppliesForTesting(), 1u)
      << "the newer pick left the older wait alive, so both will apply and "
         "the later one to resolve wins";

  content::RunAllTasksUntilIdle();

  Profile* spare = atrium::LoadedProfileForId("Spare");
  ASSERT_TRUE(spare);
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(spare);
  ASSERT_TRUE(service);
  if (!service->loaded()) {
    search_test_utils::WaitForTemplateURLServiceToLoad(service);
  }
  content::RunAllTasksUntilIdle();

  const TemplateURL* def = service->GetDefaultSearchProvider();
  ASSERT_TRUE(def);
  EXPECT_EQ(def->data().sync_guid, "atrium:secondengine")
      << "the superseded pick landed its engine on top of the newer one";
}

}  // namespace
