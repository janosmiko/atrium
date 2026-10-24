// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_elsewhere_two_windows_browsertest.cc

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

// The elsewhere mark used to join a row to another window's tab by url. Two
// rows on one url, in two spaces, both pointed at the same tab, and a click
// on the cold one moved a tab that belongs to a row in the other space. The
// join is by the row's own binding now.
namespace {

class AtriumElsewhereTwoWindowsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  // Two spaces, each with one cold row on `url`. Work is the active space.
  bool OpenCoreWithSharedRow(const GURL& url) {
    static constexpr char kTemplate[] =
        R"({"spaces":[)"
        R"({"id":"work","name":"Work","emoji":"W","tint":"#111111",)"
        R"("profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[{"url":"%s","title":"Work row"}],"order":0,"groups":[]},)"
        R"({"id":"home","name":"Home","emoji":"H","tint":"#222222",)"
        R"("profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[{"url":"%s","title":"Home row"}],"order":1,"groups":[]})"
        R"(],"favorites":[],)"
        R"("appState":{"activeSpaceID":"work","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!dir_.CreateUniqueTempDir()) {
      return false;
    }
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    const std::string json = base::StringPrintf(
        kTemplate, url.spec().c_str(), url.spec().c_str());
    if (!base::WriteFile(path, json)) {
      return false;
    }
    return atrium_core_open(path.value().c_str(), 1.0);
  }

  // The row titled `title` in the space chip named `space_name`, in
  // `browser`'s model. By title, not position: a window's own new tab gets a
  // row of its own on top of the list.
  static std::optional<base::DictValue> RowOf(Browser* browser,
                                              std::string_view space_name,
                                              std::string_view title) {
    std::optional<base::Value> parsed = base::JSONReader::Read(
        atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return std::nullopt;
    }
    const base::ListValue* spaces = parsed->GetDict().FindList("spaces");
    if (!spaces) {
      return std::nullopt;
    }
    for (const base::Value& chip : *spaces) {
      const std::string* name = chip.GetDict().FindString("name");
      if (!name || *name != space_name) {
        continue;
      }
      const base::ListValue* tabs = chip.GetDict().FindList("tabs");
      if (!tabs) {
        return std::nullopt;
      }
      for (const base::Value& row : *tabs) {
        const std::string* found = row.GetDict().FindString("title");
        if (found && *found == title) {
          return row.GetDict().Clone();
        }
      }
      return std::nullopt;
    }
    return std::nullopt;
  }

  // Every tab's id and url, for a failure message.
  static std::string StripOf(Browser* browser) {
    std::string out;
    TabStripModel* strip = browser->tab_strip_model();
    for (int i = 0; i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      out += base::NumberToString(atrium::TabId(contents)) + " " +
             contents->GetVisibleURL().spec() + "; ";
    }
    return out;
  }

  base::ScopedTempDir dir_;
};

IN_PROC_BROWSER_TEST_F(AtriumElsewhereTwoWindowsBrowserTest,
                       OnlyTheRowBoundToTheTabIsMarkedElsewhere) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("shared.test", "/title1.html");
  ASSERT_TRUE(OpenCoreWithSharedRow(url));

  // The production open path adopts Work's cold row, since Work is active.
  content::WebContents* opened =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  const std::string id = base::NumberToString(atrium::TabId(opened));

  std::optional<base::DictValue> own = RowOf(browser(), "Work", "Work row");
  ASSERT_TRUE(own);
  EXPECT_EQ(id, *own->FindString("id")) << "the open did not adopt Work's row";

  Browser* second = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second);
  const std::string strips =
      "first: " + StripOf(browser()) + "second: " + StripOf(second);

  std::optional<base::DictValue> work = RowOf(second, "Work", "Work row");
  ASSERT_TRUE(work);
  EXPECT_EQ("", *work->FindString("id")) << strips;
  EXPECT_TRUE(work->FindBool("elsewhere").value_or(false))
      << "the bound row is not marked as live in the other window";
  const std::string* carried = work->FindString("elsewhereTabId");
  ASSERT_TRUE(carried);
  EXPECT_EQ(id, *carried);

  std::optional<base::DictValue> home = RowOf(second, "Home", "Home row");
  ASSERT_TRUE(home);
  EXPECT_FALSE(home->FindBool("elsewhere").value_or(false))
      << "a cold row on the same url took the other space's tab";
  EXPECT_FALSE(home->FindString("elsewhereTabId"));
}

}  // namespace
