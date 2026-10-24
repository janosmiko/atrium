// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_search_engine_mirror.h"

#include "base/test/task_environment.h"
#include "chrome/browser/search_engines/template_url_service_factory_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {

class SearchEngineMirrorTest : public testing::Test {
 protected:
  void SetUp() override {
    util_ = std::make_unique<TemplateURLServiceFactoryTestUtil>(&profile_);
    util_->VerifyLoad();
  }
  TemplateURLService* service() { return util_->model(); }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
  std::unique_ptr<TemplateURLServiceFactoryTestUtil> util_;
};

TEST_F(SearchEngineMirrorTest, ParsesTheSettingsList) {
  const auto list = SearchEnginesFromSettings(
      R"({"searchEngines":[{"id":"yt","name":"YouTube","keyword":"youtube.com",)"
      R"("url":"https://www.youtube.com/results?search_query=%s","enabled":true,"builtin":true},)"
      R"({"id":"dr","name":"Docs","keyword":"docs.rs","url":"https://docs.rs/?q=%s"}]})");
  ASSERT_TRUE(list);
  ASSERT_EQ(list->size(), 2u);
  EXPECT_EQ((*list)[0].id, "yt");
  EXPECT_EQ((*list)[0].keyword, "youtube.com");
  EXPECT_TRUE((*list)[0].enabled);
  // The Rust loader's serde default reads a missing flag as false, and the two
  // sides must agree on what the pane is showing.
  EXPECT_FALSE((*list)[1].enabled);
}

TEST_F(SearchEngineMirrorTest, DropsWhatTemplateUrlDataCannotTake) {
  const auto list = SearchEnginesFromSettings(
      R"({"searchEngines":[)"
      R"({"id":"blank","name":"Blank","keyword":"  ","url":"https://a.example/?q=%s"},)"
      R"({"id":"","name":"No id","keyword":"b.example","url":"https://b.example/?q=%s"},)"
      R"({"id":"plain","name":"Plain","keyword":"c.example","url":"http://c.example/?q=%s"},)"
      R"({"id":"twice","name":"Twice","keyword":"d.example","url":"https://d.example/?q=%s&r=%s"},)"
      R"({"id":"ok","name":"Ok","keyword":" ok.example ","url":"https://ok.example/?q=%s"}]})");
  ASSERT_TRUE(list);
  ASSERT_EQ(list->size(), 1u);
  EXPECT_EQ((*list)[0].id, "ok");
  EXPECT_EQ((*list)[0].keyword, "ok.example");
}

TEST_F(SearchEngineMirrorTest, LoopbackHttpIsAllowedNothingElse) {
  const auto list = SearchEnginesFromSettings(
      R"({"searchEngines":[)"
      R"({"id":"https","name":"H","keyword":"h1","url":"https://example.com/?q=%s"},)"
      R"({"id":"loop4","name":"H","keyword":"h2","url":"http://127.0.0.1:8000/e.html?q=%s"},)"
      R"({"id":"loop6","name":"H","keyword":"h3","url":"http://[::1]:8000/e.html?q=%s"},)"
      R"({"id":"loopname","name":"H","keyword":"h4","url":"http://localhost:8000/e.html?q=%s"},)"
      R"({"id":"trap4","name":"H","keyword":"h5","url":"http://127.0.0.1.evil.com/?q=%s"},)"
      R"({"id":"trapname","name":"H","keyword":"h6","url":"http://localhost.attacker.net/?q=%s"},)"
      R"({"id":"plainhttp","name":"H","keyword":"h7","url":"http://example.com/?q=%s"},)"
      R"({"id":"file","name":"H","keyword":"h8","url":"file:///tmp/e.html?q=%s"},)"
      // GURL lower-cases a scheme, so the Rust port accepts these two.
      R"({"id":"upper","name":"H","keyword":"h9","url":"HTTPS://example.com/?q=%s"},)"
      R"({"id":"upperloop","name":"H","keyword":"h10","url":"HTTP://127.0.0.1:8000/e.html?q=%s"},)"
      // Userinfo is not the host: this one really does reach evil.com.
      R"({"id":"userinfo","name":"H","keyword":"h11","url":"http://localhost@evil.com/?q=%s"},)"
      // The reverse: the real host after the last '@' is loopback, and
      // "evil.com" is only a (fake) username, so this one is legitimate.
      R"({"id":"userinfo-loopback","name":"H","keyword":"h13","url":"http://evil.com@127.0.0.1/?q=%s"},)"
      R"({"id":"twoterms","name":"H","keyword":"h12","url":"http://127.0.0.1:8000/a?q=%s&x=%s"}]})");
  ASSERT_TRUE(list);
  std::vector<std::string> ids;
  for (const MirroredEngine& e : *list) {
    ids.push_back(e.id);
  }
  EXPECT_EQ(ids, (std::vector<std::string>{"https", "loop4", "loop6",
                                           "loopname", "upper", "upperloop",
                                           "userinfo-loopback"}));
}

TEST_F(SearchEngineMirrorTest, NoListIsNoAnswer) {
  EXPECT_FALSE(SearchEnginesFromSettings("").has_value());
  EXPECT_FALSE(SearchEnginesFromSettings("not json").has_value());
  EXPECT_FALSE(SearchEnginesFromSettings("{}").has_value());
  EXPECT_TRUE(SearchEnginesFromSettings(R"({"searchEngines":[]})").has_value());
}

TEST_F(SearchEngineMirrorTest, TwoEnginesSwapKeywordsInOnePass) {
  MirrorSearchEngines(service(),
                      {{"a", "A", "foo", "https://a.example/?q=%s", true},
                       {"b", "B", "bar", "https://b.example/?q=%s", true}});
  ASSERT_TRUE(service()->GetTemplateURLForGUID("atrium:a"));
  ASSERT_TRUE(service()->GetTemplateURLForGUID("atrium:b"));

  MirrorSearchEngines(service(),
                      {{"a", "A", "bar", "https://a.example/?q=%s", true},
                       {"b", "B", "foo", "https://b.example/?q=%s", true}});

  EXPECT_EQ(service()->GetTemplateURLForGUID("atrium:a")->keyword(), u"bar");
  EXPECT_EQ(service()->GetTemplateURLForGUID("atrium:b")->keyword(), u"foo");
}

TEST_F(SearchEngineMirrorTest, AddsUpdatesAndRemovesOwnedEntries) {
  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", true}});
  const TemplateURL* yt = service()->GetTemplateURLForGUID("atrium:yt");
  ASSERT_TRUE(yt);
  EXPECT_EQ(yt->keyword(), u"youtube.com");

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "yt.example",
                   "https://www.youtube.com/results?search_query=%s", true}});
  EXPECT_EQ(service()->GetTemplateURLForGUID("atrium:yt")->keyword(),
            u"yt.example");

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "yt.example",
                   "https://www.youtube.com/results?search_query=%s", false}});
  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:yt"));
}

TEST_F(SearchEngineMirrorTest, SkipsRemovingTheDefaultProvider) {
  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", true}});
  TemplateURL* yt = service()->GetTemplateURLForGUID("atrium:yt");
  ASSERT_TRUE(yt);
  service()->SetUserSelectedDefaultSearchProvider(yt);

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", false}});
  EXPECT_TRUE(service()->GetTemplateURLForGUID("atrium:yt"));

  TemplateURLData foreign;
  foreign.SetShortName(u"Foreign");
  foreign.SetKeyword(u"foreign.example");
  foreign.SetURL("https://foreign.example/?q={searchTerms}");
  foreign.sync_guid = "not-ours";
  TemplateURL* foreign_engine =
      service()->Add(std::make_unique<TemplateURL>(foreign));
  ASSERT_TRUE(foreign_engine);
  service()->SetUserSelectedDefaultSearchProvider(foreign_engine);

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", false}});
  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:yt"));
}

// this used to expect the mirror to skip ANY taken keyword, which
// pinned the defect - Chromium owns google.com and duckduckgo.com in every
// region, and claims youtube.com the first time the user searches there.
TEST_F(SearchEngineMirrorTest, MirrorsOverAKeywordChromiumAlreadyOwns) {
  TemplateURLData foreign;
  foreign.SetShortName(u"Foreign");
  foreign.SetKeyword(u"youtube.com");
  foreign.SetURL("https://foreign.example/?q={searchTerms}");
  foreign.sync_guid = "not-ours";
  service()->Add(std::make_unique<TemplateURL>(foreign));

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", true}});

  const TemplateURL* mirrored = service()->GetTemplateURLForGUID("atrium:yt");
  ASSERT_TRUE(mirrored)
      << "no row means SetProfileDefaultSearchEngine has no GUID to select, "
         "so no profile can ever be switched to this engine";
  EXPECT_EQ(mirrored->keyword(), u"youtube.com");
  // Nothing is taken from the user's own engine - it keeps its keyword too.
  const TemplateURL* still_foreign =
      service()->GetTemplateURLForGUID("not-ours");
  ASSERT_TRUE(still_foreign);
  EXPECT_EQ(still_foreign->keyword(), u"youtube.com");
}

// Same setup minus the load, which is the state the pane can reach a pick in.
class LoadingSearchEngineMirrorTest : public testing::Test {
 protected:
  void SetUp() override {
    util_ = std::make_unique<TemplateURLServiceFactoryTestUtil>(&profile_);
  }
  TemplateURLService* service() { return util_->model(); }

  content::BrowserTaskEnvironment task_environment_;
  TestingProfile profile_;
  std::unique_ptr<TemplateURLServiceFactoryTestUtil> util_;
};

// a pick made while the service is still reading its database
// mirrors nothing, so the row the switch needs does not exist yet. The retry
// on load is what has to land, and DeferredProfileEngineApply exists for it.
TEST_F(LoadingSearchEngineMirrorTest, TheSwitchLandsOnTheRetryAfterLoad) {
  const std::vector<MirroredEngine> engines = {
      {"yt", "YouTube", "youtube.com",
       "https://www.youtube.com/results?search_query=%s", true}};
  ASSERT_FALSE(service()->loaded());

  MirrorSearchEngines(service(), engines);
  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:yt"))
      << "a mirror pass mid-load must not half-write the list";

  util_->VerifyLoad();
  MirrorSearchEngines(service(), engines);
  EXPECT_TRUE(service()->GetTemplateURLForGUID("atrium:yt"))
      << "the retry never produced the GUID the switch selects by";
}

TEST_F(SearchEngineMirrorTest, AnotherMirroredRowStillBlocksTheKeyword) {
  MirrorSearchEngines(service(),
                      {{"a", "A", "foo", "https://a.example/?q=%s", true}});
  ASSERT_TRUE(service()->GetTemplateURLForGUID("atrium:a"));

  // Two settings rows on one keyword, both in the same pass. Without the block
  // they would fight, each pass leaving a different one holding it.
  MirrorSearchEngines(service(),
                      {{"a", "A", "foo", "https://a.example/?q=%s", true},
                       {"b", "B", "foo", "https://b.example/?q=%s", true}});

  EXPECT_TRUE(service()->GetTemplateURLForGUID("atrium:a"));
  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:b"));
}

TEST_F(SearchEngineMirrorTest, FreesAStaleKeywordInTheSamePass) {
  MirrorSearchEngines(service(),
                      {{"a", "A", "foo", "https://a.example/?q=%s", true}});
  ASSERT_TRUE(service()->GetTemplateURLForGUID("atrium:a"));

  MirrorSearchEngines(service(),
                      {{"b", "B", "foo", "https://b.example/?q=%s", true}});

  EXPECT_FALSE(service()->GetTemplateURLForGUID("atrium:a"));
  const TemplateURL* b = service()->GetTemplateURLForGUID("atrium:b");
  ASSERT_TRUE(b);
  EXPECT_EQ(b->keyword(), u"foo");
}

// the update path carried the same refusal as the add path, so an
// engine whose settings keyword moved onto a prepopulated one stayed put and
// the pane kept offering a keyword the service did not have.
TEST_F(SearchEngineMirrorTest, MovesOntoAKeywordChromiumAlreadyOwns) {
  MirrorSearchEngines(service(),
                      {{"a", "A", "foo", "https://a.example/?q=%s", true}});
  ASSERT_TRUE(service()->GetTemplateURLForGUID("atrium:a"));

  TemplateURLData foreign;
  foreign.SetShortName(u"Foreign");
  foreign.SetKeyword(u"bar");
  foreign.SetURL("https://foreign.example/?q={searchTerms}");
  foreign.sync_guid = "not-ours";
  service()->Add(std::make_unique<TemplateURL>(foreign));

  MirrorSearchEngines(service(),
                      {{"a", "A", "bar", "https://a.example/?q=%s", true}});

  const TemplateURL* a = service()->GetTemplateURLForGUID("atrium:a");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->keyword(), u"bar");
  const TemplateURL* still_foreign =
      service()->GetTemplateURLForGUID("not-ours");
  ASSERT_TRUE(still_foreign);
  EXPECT_EQ(still_foreign->keyword(), u"bar");
}

TEST_F(SearchEngineMirrorTest, MirroredEnginesAreActive) {
  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", true}});
  TemplateURL* yt = service()->GetTemplateURLForGUID("atrium:yt");
  ASSERT_TRUE(yt);
  EXPECT_EQ(yt->is_active(), TemplateURLData::ActiveStatus::kTrue);

  // Simulate an entry mirrored before this field existed - the update pass
  // must reactivate it even though nothing else about it changed.
  service()->SetIsActiveTemplateURL(yt, false);
  ASSERT_NE(service()->GetTemplateURLForGUID("atrium:yt")->is_active(),
            TemplateURLData::ActiveStatus::kTrue);

  MirrorSearchEngines(
      service(), {{"yt", "YouTube", "youtube.com",
                   "https://www.youtube.com/results?search_query=%s", true}});
  EXPECT_EQ(service()->GetTemplateURLForGUID("atrium:yt")->is_active(),
            TemplateURLData::ActiveStatus::kTrue);
}

TEST_F(SearchEngineMirrorTest, LeavesForeignEntriesAlone) {
  TemplateURLData foreign;
  foreign.SetShortName(u"Foreign");
  foreign.SetKeyword(u"foreign.example");
  foreign.SetURL("https://foreign.example/?q={searchTerms}");
  foreign.sync_guid = "not-ours";
  service()->Add(std::make_unique<TemplateURL>(foreign));

  MirrorSearchEngines(service(), {});
  EXPECT_TRUE(service()->GetTemplateURLForGUID("not-ours"));
}

}  // namespace atrium
