// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_live_tabs_cache_browsertest.cc
//
// IdsLiveInOtherWindows caches the cross-window tab list, so a
// single shared change that pushes in every window walks the windows once
// instead of once per window. A cache is only worth having if it is also
// right, and it goes stale when a tab changes window or leaves.
//
// An InProcessBrowserTest rather than a unit_test because the cache reads
// GlobalBrowserCollection, which needs real windows. This
// shape is gateable headlessly through build/scripts/vm.sh run-gtest.

#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"

#include <memory>
#include <string>
#include <vector>

#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using AtriumLiveTabsCacheBrowserTest = InProcessBrowserTest;

bool Lists(const std::vector<LiveTab>& tabs, uint64_t id) {
  for (const LiveTab& tab : tabs) {
    if (tab.id == id) {
      return true;
    }
  }
  return false;
}

IN_PROC_BROWSER_TEST_F(AtriumLiveTabsCacheBrowserTest,
                       TheCachedCrossWindowListFollowsAMoveAndAClose) {
  const GURL first("https://elsewhere.test/first");

  // A second window on the same profile, holding one known page.
  Browser* other = CreateBrowser(browser()->profile());
  ASSERT_TRUE(other);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other, first));
  const uint64_t tab_id = TabId(other->tab_strip_model()->GetActiveWebContents());

  // Seen from this window, that tab is live elsewhere, flagged so.
  std::vector<LiveTab> tabs = TabsLiveInOtherWindows(browser());
  ASSERT_TRUE(Lists(tabs, tab_id))
      << "the other window's tab must be visible to this one";
  for (const LiveTab& tab : tabs) {
    EXPECT_TRUE(tab.elsewhere);
    EXPECT_FALSE(tab.active);
  }

  // A tab in the OTHER window is never reported to itself, whatever it holds.
  EXPECT_FALSE(Lists(TabsLiveInOtherWindows(other), tab_id))
      << "a window must not see its own tab as living elsewhere";

  // STALENESS ONE: the tab changes window. The strip change is the
  // invalidation, and without it this window keeps reporting a tab it now
  // holds itself.
  std::unique_ptr<tabs::TabModel> detached =
      other->tab_strip_model()->DetachTabAtForInsertion(0);
  browser()->tab_strip_model()->InsertDetachedTabAt(
      -1, std::move(detached), AddTabTypes::ADD_ACTIVE);
  EXPECT_FALSE(Lists(TabsLiveInOtherWindows(browser()), tab_id))
      << "the cached list still reports a tab this window holds now";
  EXPECT_TRUE(Lists(TabsLiveInOtherWindows(other), tab_id))
      << "the cached list never picked up the tab the other window lost";

  // STALENESS TWO: a window closes with the tab. The tab set changed, which is
  // the other invalidation.
  CloseBrowserSynchronously(browser());
  EXPECT_FALSE(Lists(TabsLiveInOtherWindows(other), tab_id))
      << "a closed window's tab must stop being reported as live";
}

// CollectionObserver wires a BrowserController onto
// TYPE_NORMAL windows only, so a popup must never contribute a row either.
IN_PROC_BROWSER_TEST_F(AtriumLiveTabsCacheBrowserTest,
                       AnUncontrolledWindowNeverJoinsTheLiveList) {
  const GURL page("https://elsewhere.test/popup");

  Browser* popup = Browser::Create(
      Browser::CreateParams(Browser::TYPE_POPUP, browser()->profile(), true));
  AddBlankTabAndShow(popup);
  // NavigateToURL hangs on this fake host in an Atrium popup. Load it as a
  // second tab instead and settle it without asserting the load succeeded.
  content::WebContents* popup_tab =
      chrome::AddAndReturnTabAt(popup, page, -1, /*foreground=*/true);
  ASSERT_TRUE(popup_tab);
  content::WaitForLoadStopWithoutSuccessCheck(popup_tab);
  ASSERT_FALSE(BrowserController::ControllerForBrowser(popup))
      << "a popup got a BrowserController - this test no longer covers an "
         "uncontrolled window";
  const uint64_t popup_tab_id = TabId(popup_tab);

  // The popup itself never invalidates the cache (no controller means no
  // observer). Force a rebuild from this window's own, controlled strip.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  EXPECT_FALSE(Lists(TabsLiveInOtherWindows(browser()), popup_tab_id))
      << "an uncontrolled window's tab was reported as live elsewhere";

  CloseBrowserSynchronously(popup);
}

}  // namespace
}  // namespace atrium
