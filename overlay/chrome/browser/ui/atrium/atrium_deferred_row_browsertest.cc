// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_deferred_row_browsertest.cc

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

// A tab the renderer creates enters the strip before it has a url, so its
// row waited for the first commit: one network round trip in which the page
// was on screen and the sidebar showed nothing for it.
namespace {

constexpr char kHeldPath[] = "/held.html";

bool AnyRowHasId(Browser* browser, const std::string& tab_id) {
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  if (!model) {
    return false;
  }
  bool found = false;
  auto scan = [&found, &tab_id](const base::ListValue* rows) {
    if (!rows) {
      return;
    }
    for (const base::Value& row : *rows) {
      if (const std::string* id = row.GetDict().FindString("id");
          id && *id == tab_id) {
        found = true;
      }
    }
  };
  scan(model->GetDict().FindList("tabs"));
  if (const base::ListValue* spaces = model->GetDict().FindList("spaces")) {
    for (const base::Value& space : *spaces) {
      scan(space.GetDict().FindList("tabs"));
    }
  }
  return found;
}

class AtriumDeferredRowBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    held_ = std::make_unique<net::test_server::ControllableHttpResponse>(
        embedded_test_server(), kHeldPath);
    InProcessBrowserTest::SetUpOnMainThread();
  }

  bool OpenEmptyCore() {
    static constexpr char kDocument[] =
        R"({"spaces":[{"id":"s1","name":"Test","emoji":"T",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":0,"groups":[]}],"favorites":[],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!dir_.CreateUniqueTempDir()) {
      return false;
    }
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    return base::WriteFile(path, kDocument) &&
           atrium_core_open(path.value().c_str(), 1.0);
  }

  std::unique_ptr<net::test_server::ControllableHttpResponse> held_;
  base::ScopedTempDir dir_;
};

IN_PROC_BROWSER_TEST_F(AtriumDeferredRowBrowserTest,
                       ARendererCreatedTabGetsItsRowWhenItsNavigationStarts) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(OpenEmptyCore());

  // Inserted with no url, the way a target=_blank tab arrives.
  std::unique_ptr<content::WebContents> created = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* tab = created.get();
  browser()->tab_strip_model()->AddWebContents(
      std::move(created), -1, ui::PAGE_TRANSITION_LINK, AddTabTypes::ADD_ACTIVE);
  const std::string id = base::NumberToString(atrium::TabId(tab));
  EXPECT_FALSE(AnyRowHasId(browser(), id)) << "a row with no url got in";

  tab->GetController().LoadURL(embedded_test_server()->GetURL(kHeldPath),
                               content::Referrer(), ui::PAGE_TRANSITION_LINK,
                               std::string());
  // The request is out and unanswered, so nothing has committed.
  held_->WaitForRequest();
  EXPECT_TRUE(AnyRowHasId(browser(), id))
      << "the row waited for the commit instead of the navigation start";

  held_->Send(net::HTTP_OK, "text/html", "<p>held</p>");
  held_->Done();
  EXPECT_TRUE(content::WaitForLoadStop(tab));
  EXPECT_TRUE(AnyRowHasId(browser(), id)) << "the commit lost the row";
}

}  // namespace
