// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_autocomplete_bridge.h"

#include <memory>
#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/search_engines/template_url_service_factory_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {

namespace {

TemplateURL* AddEngine(TemplateURLService* service,
                       const std::string& guid,
                       const std::u16string& keyword) {
  TemplateURLData data;
  data.SetShortName(keyword);
  data.SetKeyword(keyword);
  data.SetURL("https://example.test/?q={searchTerms}");
  data.sync_guid = guid;
  data.safe_for_autoreplace = false;
  return service->Add(std::make_unique<TemplateURL>(data));
}

TemplateURL* AddDefaultEngine(TemplateURLService* service,
                              const std::u16string& keyword,
                              const std::string& url) {
  TemplateURLData data;
  data.SetShortName(keyword);
  data.SetKeyword(keyword);
  data.SetURL(url);
  data.sync_guid = base::UTF16ToUTF8(keyword);
  data.safe_for_autoreplace = false;
  TemplateURL* added = service->Add(std::make_unique<TemplateURL>(data));
  service->SetUserSelectedDefaultSearchProvider(added);
  return added;
}

}  // namespace

constexpr char kOneEngine[] =
    R"({"searchEngines":[{"id":"yt","name":"YouTube","keyword":"youtube.com",)"
    R"("url":"https://www.youtube.com/results?search_query=%s","enabled":true}]})";

class AutocompleteBridgeMirrorTest : public testing::Test {
 protected:
  void SetUp() override {
    util_ = std::make_unique<TemplateURLServiceFactoryTestUtil>(&profile_);
    AtriumBrowserWindow::SetLastSettingsJson(std::string());
  }

  TemplateURLService* service() { return util_->model(); }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
  std::unique_ptr<TemplateURLServiceFactoryTestUtil> util_;
};

// A mirror running inside the notification re-enters TemplateURLService's
// observer loop through its own Add, and base/observer_list.h aborts on that.
TEST_F(AutocompleteBridgeMirrorTest, MirrorsAfterTheNotificationReturns) {
  AtriumBrowserWindow::SetLastSettingsJson(kOneEngine);

  AutocompleteBridge bridge(&profile_);
  util_->VerifyLoad();
  task_environment_.RunUntilIdle();

  const TemplateURL* yt = service()->GetTemplateURLForGUID("atrium:yt");
  ASSERT_TRUE(yt);
  EXPECT_EQ(yt->keyword(), u"youtube.com");
}

// The catch-up belongs to the load alone. A bridge built after it has nothing
// to catch up on, and a later change notification is somebody's edit - mirroring
// the cached list on top of it would undo exactly that edit.
TEST_F(AutocompleteBridgeMirrorTest, SkipsTheCatchUpWhenBuiltAfterTheLoad) {
  util_->VerifyLoad();
  ASSERT_TRUE(AddEngine(service(), "atrium:stale", u"stale.example"));
  AtriumBrowserWindow::SetLastSettingsJson(kOneEngine);

  AutocompleteBridge bridge(&profile_);

  // Any model mutation ends in the notification the bridge observes.
  ASSERT_TRUE(AddEngine(service(), "not-ours", u"foreign.example"));
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(service()->GetTemplateURLForGUID("atrium:stale"));
  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:yt"));
}

// No settings have been read yet, which is not the same as a user with no
// engines - mirroring an empty list would delete the lot.
TEST_F(AutocompleteBridgeMirrorTest, MirrorsNothingWithoutSettings) {
  ASSERT_TRUE(AddEngine(service(), "atrium:kept", u"kept.example"));

  AutocompleteBridge bridge(&profile_);
  util_->VerifyLoad();
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(service()->GetTemplateURLForGUID("atrium:kept"));
}

class AutocompleteBridgeQueryProfileTest : public testing::Test {
 protected:
  void SetUp() override {
    AtriumBrowserWindow::SetLastSettingsJson(std::string());
    alpha_util_ = std::make_unique<TemplateURLServiceFactoryTestUtil>(&alpha_);
    bravo_util_ = std::make_unique<TemplateURLServiceFactoryTestUtil>(&bravo_);
    alpha_util_->VerifyLoad();
    bravo_util_->VerifyLoad();
    AddDefaultEngine(alpha_util_->model(), u"alpha.test",
                     "https://alpha.test/s?q={searchTerms}");
    AddDefaultEngine(bravo_util_->model(), u"bravo.test",
                     "https://bravo.test/s?q={searchTerms}");
  }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile alpha_;
  TestingProfile bravo_;
  std::unique_ptr<TemplateURLServiceFactoryTestUtil> alpha_util_;
  std::unique_ptr<TemplateURLServiceFactoryTestUtil> bravo_util_;
};

// The caller has to know, because the rebuild takes the result an accept's
// index names with it.
TEST_F(AutocompleteBridgeQueryProfileTest, ReportsWhetherItRebuilt) {
  AutocompleteBridge bridge(&alpha_);

  EXPECT_FALSE(bridge.SetQueryProfile(nullptr));
  EXPECT_FALSE(bridge.SetQueryProfile(&alpha_));
  EXPECT_TRUE(bridge.SetQueryProfile(&bravo_));
  EXPECT_FALSE(bridge.SetQueryProfile(&bravo_));
}

// The whole of the second gap: after the repoint the page's index resolves to
// nothing, and re-asking is what turns that back into a real destination -
// on the engine of the profile just switched to, not the one queried under.
TEST_F(AutocompleteBridgeQueryProfileTest, ReQueryAnswersOnTheNewProfile) {
  AutocompleteBridge bridge(&alpha_);
  bridge.Query("puppies");
  task_environment_.RunUntilIdle();
  ASSERT_EQ(bridge.DestinationAt(0), GURL("https://alpha.test/s?q=puppies"));

  ASSERT_TRUE(bridge.SetQueryProfile(&bravo_));
  EXPECT_TRUE(bridge.DestinationAt(0).is_empty());

  EXPECT_EQ(bridge.ReQueryDefaultDestination(),
            GURL("https://bravo.test/s?q=puppies"));
}

// Nothing was typed, so there is nothing to re-ask - and an empty GURL is what
// the window already declines.
TEST_F(AutocompleteBridgeQueryProfileTest, ReQueryWithoutAQueryAnswersNothing) {
  AutocompleteBridge bridge(&alpha_);

  EXPECT_TRUE(bridge.ReQueryDefaultDestination().is_empty());
}

}  // namespace atrium
