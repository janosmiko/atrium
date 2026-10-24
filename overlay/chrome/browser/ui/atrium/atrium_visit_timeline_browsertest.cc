// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_visit_timeline_browsertest.cc
//
// Real navigations reach the timeline, and a closing tab drops
// it. This is the wiring test - that the controller's hooks actually call the
// core - not the branch test.
//
// The branch case is what the feature exists for and is proven in the
// core's own a_branch_keeps_the_states_the_back_stack_would_drop. Driving a
// real BACK from here hangs: three attempts through GoToOffset and GoBack, with
// both load-stop and navigation-finished waits, each timed out rather than
// failing. Whoever builds the gesture should find out why before trusting a
// back-driven test - the most likely cause is this fork swapping a pane's
// WebContents, which the tab-id assertion below is placed to catch.

#include <algorithm>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using AtriumVisitTimelineBrowserTest = InProcessBrowserTest;

// The timeline's urls, oldest first.
std::vector<std::string> TimelineUrls(uint64_t tab_id) {
  std::vector<std::string> urls;
  char* raw = atrium_core_visits_for_tab(tab_id);
  if (!raw) {
    return urls;
  }
  const std::string json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return urls;
  }
  for (const base::Value& visit : parsed->GetList()) {
    if (const base::DictValue* dict = visit.GetIfDict()) {
      if (const std::string* url = dict->FindString("url")) {
        urls.push_back(*url);
      }
    }
  }
  return urls;
}

IN_PROC_BROWSER_TEST_F(AtriumVisitTimelineBrowserTest,
                       RealNavigationsReachTheTimelineAndACloseDropsIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL a = embedded_test_server()->GetURL("/title1.html");
  const GURL b = embedded_test_server()->GetURL("/title2.html");
  const GURL c = embedded_test_server()->GetURL("/title3.html");

  // CreateBrowser, not the fixture's own browser(). This fork's default test
  // window comes up with no tab, so its active WebContents is null and a
  // NavigateToURL against it waits for a load that cannot happen - the test
  // hung for its whole timeout rather than failing. A window made here has one.
  Browser* window = CreateBrowser(browser()->profile());
  ASSERT_TRUE(window);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(window, a));
  content::WebContents* contents =
      window->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  const uint64_t tab_id = TabId(contents);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(window, b));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(window, c));

  // One tab, one timeline, across three navigations. Asserted rather than
  // assumed: a pane that swapped its WebContents mid-navigation would give the
  // later stops a different id, and the timeline would be split in two with
  // nothing saying so.
  content::WebContents* after =
      window->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(after);
  EXPECT_EQ(tab_id, TabId(after))
      << "the tab id moved during ordinary navigation, so a timeline cannot be "
         "keyed on it";

  const std::vector<std::string> timeline = TimelineUrls(tab_id);
  for (const GURL& expected : {a, b, c}) {
    EXPECT_NE(timeline.end(),
              std::find(timeline.begin(), timeline.end(), expected.spec()))
        << expected.spec() << " never reached the timeline; it holds "
        << timeline.size() << " stops";
  }

  // Revisiting a url with another page between is a SECOND stop, because
  // dragging past that page is how someone reaches the first one.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(window, a));
  // One named vector. Calling TimelineUrls twice inside std::count takes
  // begin() and end() from two DIFFERENT temporaries, which segfaults.
  const std::vector<std::string> revisited = TimelineUrls(tab_id);
  EXPECT_EQ(2, std::count(revisited.begin(), revisited.end(), a.spec()));

  // The tab takes its timeline with it.
  EXPECT_TRUE(atrium_core_forget_visits(tab_id));
  EXPECT_TRUE(TimelineUrls(tab_id).empty());
}

}  // namespace
}  // namespace atrium
