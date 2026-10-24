// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_activate_tab_otr_guard_browsertest.cc

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using AtriumActivateTabOtrGuardBrowserTest = InProcessBrowserTest;

// an incognito profile's GetBaseName() answers its parent's path,
// so a renderer-supplied incognito tab id used to read as "same profile" and
// ActivateTab moved the OTR WebContents into the regular window.
IN_PROC_BROWSER_TEST_F(AtriumActivateTabOtrGuardBrowserTest,
                       IncognitoTabIdIsRefused) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());

  content::WebContents* otr_tab = chrome::AddAndReturnTabAt(
      incognito, embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(otr_tab);
  ASSERT_TRUE(content::WaitForLoadStop(otr_tab));
  const uint64_t otr_tab_id = atrium::TabId(otr_tab);

  atrium::BrowserController* host =
      atrium::BrowserController::ControllerForBrowser(browser());
  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(incognito);
  ASSERT_TRUE(host);
  ASSERT_TRUE(source);

  const int regular_count_before = browser()->tab_strip_model()->count();
  const int otr_count_before = incognito->tab_strip_model()->count();

  atrium::BrowserController::ActivateTab(host, otr_tab_id);

  EXPECT_EQ(regular_count_before, browser()->tab_strip_model()->count())
      << "an incognito tab id landed in the regular window";
  EXPECT_EQ(otr_count_before, incognito->tab_strip_model()->count())
      << "the incognito window lost the tab it was refused for";
  EXPECT_NE(TabStripModel::kNoTab,
            incognito->tab_strip_model()->GetIndexOfWebContents(otr_tab))
      << "the incognito tab left its own strip";
}
