// Copyright 2026 The Atrium Authors

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

using AtriumSidebarTabDragReleasedBrowserTest = InProcessBrowserTest;

// OnSurfaceMessage's "sidebarTabDragReleased" handler for
// the no-window-matched case. With only browser() open, BrowserController::
// ClassifyAndRouteDroppedTab's window enumeration excludes the source
// window itself (see the header's own doc), so
// its scan finds no OTHER live controller no matter where the release point
// lands - the same kNoMatch path an empty-desktop release takes. That
// drives AdoptTabIntoLittleWindow, exercised here through the
// real chrome.send message name rather than the C++ entry point directly -
// the adopt-tab browsertest already covers that primitive in isolation.
IN_PROC_BROWSER_TEST_F(AtriumSidebarTabDragReleasedBrowserTest,
                       NoWindowMatchedTearsTabIntoNewLittleWindow) {
  // A real URL is needed so the tab actually commits - this sandboxed VM has
  // no outbound network, so a live https://example.com never resolves (same
  // lesson atrium_little_move_cross_profile_browsertest.cc already
  // documents). The embedded test server serves real content over loopback
  // instead.
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL dragged_url = embedded_test_server()->GetURL("/title1.html");
  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1, /*foreground=*/true);

  TabStripModel* source_strip = browser()->tab_strip_model();
  const int dragged_index = source_strip->count() - 1;
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(dragged_index);
  ASSERT_TRUE(dragged_contents);
  const std::string tab_id =
      base::NumberToString(atrium::TabId(dragged_contents));
  const int source_count_before = source_strip->count();
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();

  base::ListValue args;
  args.Append(tab_id);
  args.Append(500);
  args.Append(500);

  atrium::AtriumBrowserWindow* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->OnSurfaceMessage("sidebarTabDragReleased", args);

  ASSERT_EQ(browsers_before + 1,
            GlobalBrowserCollection::GetInstance()->GetSize())
      << "no new little window was created for the no-match release";
  EXPECT_EQ(source_count_before - 1, source_strip->count())
      << "the dragged tab did not leave its source window's strip";

  tabs::TabInterface* landed_tab =
      tabs::TabInterface::GetFromContents(dragged_contents);
  ASSERT_TRUE(landed_tab);
  BrowserWindowInterface* dest_window =
      landed_tab->GetBrowserWindowInterface();
  ASSERT_TRUE(dest_window);
  TabStripModel* dest_strip = dest_window->GetTabStripModel();
  ASSERT_EQ(1, dest_strip->count());
  EXPECT_EQ(dragged_contents, dest_strip->GetWebContentsAt(0));
}

}  // namespace
