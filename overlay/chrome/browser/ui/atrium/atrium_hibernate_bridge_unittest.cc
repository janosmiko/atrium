// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hibernate_bridge_unittest.cc
#include "chrome/browser/ui/atrium/atrium_hibernate_bridge.h"

#include <map>
#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/metrics/desktop_session_duration/desktop_session_duration_tracker.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/resource_coordinator/tab_helper.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_source.h"
#include "chrome/browser/resource_coordinator/utils.h"
#include "chrome/browser/tab_contents/form_interaction_tab_helper.h"
#include "chrome/browser/ui/atrium/atrium_memory_saver.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/test_support/test_harness_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

using performance_manager::PageLiveStateDecorator;
using performance_manager::PerformanceManager;
using resource_coordinator::ResourceCoordinatorTabHelper;
using resource_coordinator::TabLifecycleUnitExternal;

// Separate profileIDs: migrate_v5_to_v6 moved "hibernate" onto the profile,
// so two spaces sharing one profile would share its hibernate policy too.
constexpr char kSpacesJson[] =
    R"({"spaces":[
{"id":"quiet","name":"Quiet","emoji":"Q","tint":"#000000","profileID":"quiet",
 "order":0,"pinnedTabs":[],"tabs":[],"groups":[],"hibernate":"never"},
{"id":"loud","name":"Loud","emoji":"L","tint":"#FFFFFF","profileID":"loud",
 "order":1,"pinnedTabs":[],"tabs":[],"groups":[]}
],"appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
"sidebarWidthMigrated":true}})";

// The fixture tab_lifecycle_unit_unittest.cc uses to get a real, discardable
// tab out of TabLifecycleUnitSource - copied because that is the only path
// to a genuine TabLifecycleUnitExternal.
class AtriumHibernateBridgeTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    TestingBrowserProcess::GetGlobal()->SetUpGlobalFeaturesForTesting(
        /*profile_manager=*/false);
    ChromeRenderViewHostTestHarness::SetUp();
    pm_helper_.SetUp();
    PerformanceManager::GetGraph()->PassToGraph(
        std::make_unique<PageLiveStateDecorator>());
    PerformanceManager::GetGraph()->PassToGraph(
        FormInteractionTabHelper::CreateGraphObserver());
    metrics::DesktopSessionDurationTracker::Initialize();
    g_browser_process->GetTabManager();  // Forces TabLifecycleUnitSource up.
    PerformanceManager::GetGraph()->PassToGraph(
        std::make_unique<
            performance_manager::policies::DiscardEligibilityPolicy>());

    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
    }
    atrium_core_close();
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));

    tab_strip_model_ =
        std::make_unique<TabStripModel>(&tab_strip_model_delegate_, profile());
    // Without this, TabLifecycleUnitSource never creates a unit for this
    // strip's tabs, so FromWebContents() stays null.
    tab_strip_model_->AddObserver(
        resource_coordinator::GetTabLifecycleUnitSource());
  }

  void TearDown() override {
    while (!tab_strip_model_->empty()) {
      tab_strip_model_->DetachAndDeleteWebContentsAt(0);
    }
    tab_strip_model_->RemoveObserver(
        resource_coordinator::GetTabLifecycleUnitSource());
    tab_strip_model_.reset();
    atrium_core_close();
    metrics::DesktopSessionDurationTracker::CleanupForTesting();
    pm_helper_.TearDown();
    ChromeRenderViewHostTestHarness::TearDown();
    TestingBrowserProcess::GetGlobal()->TearDownGlobalFeaturesForTesting();
  }

  // A real, navigated, hidden tab in `tab_strip_model_` - the shape
  // TabLifecycleUnitSource needs before FromWebContents answers anything.
  content::WebContents* AddDiscardableTab(uint64_t tab_id) {
    std::unique_ptr<content::WebContents> contents = CreateTestWebContents();
    content::WebContents* raw = contents.get();
    auto* tester = content::WebContentsTester::For(raw);
    tester->SetLastActiveTimeTicks(base::TimeTicks::Now());
    tester->SetLastActiveTime(base::Time::Now());
    ResourceCoordinatorTabHelper::CreateForWebContents(raw);
    auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
        GURL("https://example.com"), raw);
    navigation->SetKeepLoading(true);
    navigation->Commit();
    tab_strip_model_->AppendWebContents(std::move(contents), false);
    raw->WasHidden();
    tab_ids_[raw] = tab_id;
    return raw;
  }

  // Binds `contents` into the space at `space_index` (0 quiet, 1 loud) and
  // runs the bridge - the same sequence AddTab runs for a real tab.
  void OpenIntoSpaceAndApply(content::WebContents* contents,
                             int space_index) {
    const uint64_t tab_id = tab_ids_[contents];
    atrium_core_open_tab(space_index, tab_id, contents->GetVisibleURL().spec().c_str(),
                         "t", -1);
    ApplyHibernatePolicyToTab(contents, tab_id);
  }

  bool IsAutoDiscardable(content::WebContents* contents) {
    auto* unit = TabLifecycleUnitExternal::FromWebContents(contents);
    return unit && unit->IsAutoDiscardable();
  }

 private:
  base::ScopedTempDir dir_;
  TestTabStripModelDelegate tab_strip_model_delegate_;
  // Without this, TabModel::Init CHECKs on a BrowserWindowInterface the test
  // delegate never provides - tab_lifecycle_unit_unittest.cc's fixture needs
  // the same guard for the same reason.
  const tabs::TabModel::PreventFeatureInitializationForTesting prevent_;
  std::unique_ptr<TabStripModel> tab_strip_model_;
  performance_manager::PerformanceManagerTestHarnessHelper pm_helper_;
  std::map<content::WebContents*, uint64_t> tab_ids_;
};

// The property under test: a Never-space tab must not be
// eligible for Chromium's own discard sweep while a Follow-space tab (the
// general toggle on) is.
TEST_F(AtriumHibernateBridgeTest,
       ANeverSpaceTabStaysUndiscardableWhileAFollowSpaceTabDoesNot) {
  SetMemorySaverEnabledPref(true);
  SetMemorySaverThresholdMinutesPref(120);

  content::WebContents* quiet_tab = AddDiscardableTab(1);
  content::WebContents* loud_tab = AddDiscardableTab(2);

  OpenIntoSpaceAndApply(quiet_tab, 0);  // "quiet": hibernate = never
  OpenIntoSpaceAndApply(loud_tab, 1);   // "loud": Follow General, on

  EXPECT_FALSE(IsAutoDiscardable(quiet_tab))
      << "a Never-space tab must never become eligible for discard";
  EXPECT_TRUE(IsAutoDiscardable(loud_tab))
      << "a Follow-space tab with the general toggle on must stay eligible";
}

}  // namespace
}  // namespace atrium
