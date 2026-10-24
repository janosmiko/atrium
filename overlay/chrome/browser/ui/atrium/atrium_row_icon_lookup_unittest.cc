// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_row_icon_lookup.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using FetchIconCall = std::tuple<std::string, GURL, base::TimeDelta>;
using FetchPageCall = std::tuple<std::string, GURL, base::TimeDelta>;
using StoreCall = std::tuple<std::string, GURL, std::string>;
using AllowRetryCall = std::tuple<std::string, GURL>;

class FakeDelegate : public RowIconLookup::Delegate {
 public:
  void FetchIcon(const std::string& page_url,
                const GURL& icon_url,
                base::TimeDelta budget) override {
    fetched_icons.emplace_back(page_url, icon_url, budget);
  }
  void FetchPage(const std::string& page_url,
                const GURL& origin_root,
                base::TimeDelta budget) override {
    fetched_pages.emplace_back(page_url, origin_root, budget);
  }
  void CancelFetch(const std::string& page_url) override {
    cancelled.push_back(page_url);
  }
  void DecodeAndStore(const std::string& page_url,
                     const GURL& icon_url,
                     std::string bytes) override {
    stored.emplace_back(page_url, icon_url, std::move(bytes));
  }
  void FinishRow(const std::string& page_url) override {
    finished.push_back(page_url);
  }
  void AllowRetry(const std::string& key, const GURL& url) override {
    allow_retry.emplace_back(key, url);
  }

  std::vector<FetchIconCall> fetched_icons;
  std::vector<FetchPageCall> fetched_pages;
  std::vector<std::string> cancelled;
  std::vector<StoreCall> stored;
  std::vector<std::string> finished;
  std::vector<AllowRetryCall> allow_retry;
};

class RowIconLookupTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  FakeDelegate delegate_;
  RowIconLookup lookup_{&delegate_};

  const std::string kUrl = "https://site.test/a/b";
};

TEST_F(RowIconLookupTest, ADatabaseHitEndsTheRowWithoutAFetch) {
  lookup_.Begin(kUrl);
  lookup_.EndEarly(kUrl);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
  EXPECT_TRUE(delegate_.fetched_icons.empty());
  EXPECT_TRUE(delegate_.fetched_pages.empty());
}

TEST_F(RowIconLookupTest, TheIcoTierAsksTheOriginsFaviconIco) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  ASSERT_EQ(delegate_.fetched_icons.size(), 1u);
  EXPECT_EQ(std::get<0>(delegate_.fetched_icons[0]), kUrl);
  EXPECT_EQ(std::get<1>(delegate_.fetched_icons[0]),
            GURL("https://site.test/favicon.ico"));
  EXPECT_EQ(std::get<2>(delegate_.fetched_icons[0]), kRowIconDeadline);
  EXPECT_EQ(lookup_.TierOf(kUrl), RowIconTier::kIco);
}

TEST_F(RowIconLookupTest, AnIcoAnswerIsDecodedAndStoredThenTheRowEnds) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  const GURL icon_url("https://site.test/favicon.ico");
  lookup_.OnIconFetched(kUrl, icon_url, net::OK, std::string("bytes"));
  ASSERT_EQ(delegate_.stored.size(), 1u);
  EXPECT_EQ(std::get<2>(delegate_.stored[0]), "bytes");
  EXPECT_TRUE(delegate_.finished.empty());
  lookup_.OnIconStored(kUrl, true);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
}

TEST_F(RowIconLookupTest, ANetworkErrorAtIcoEndsTheRowBeforeTierFour) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  const GURL icon_url("https://site.test/favicon.ico");
  lookup_.OnIconFetched(kUrl, icon_url, net::ERR_NAME_NOT_RESOLVED,
                        std::nullopt);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
  EXPECT_TRUE(delegate_.fetched_pages.empty());
}

TEST_F(RowIconLookupTest, AnHttpMissAtIcoReachesThePage) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  const GURL icon_url("https://site.test/favicon.ico");
  lookup_.OnIconFetched(kUrl, icon_url, net::OK, std::nullopt);
  ASSERT_EQ(delegate_.fetched_pages.size(), 1u);
  EXPECT_EQ(std::get<1>(delegate_.fetched_pages[0]),
            GURL("https://site.test/"));
  EXPECT_EQ(lookup_.TierOf(kUrl), RowIconTier::kLinkTag);
}

TEST_F(RowIconLookupTest, AnEmptyBodyAtIcoReachesThePage) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  const GURL icon_url("https://site.test/favicon.ico");
  lookup_.OnIconFetched(kUrl, icon_url, net::OK, std::string());
  ASSERT_EQ(delegate_.fetched_pages.size(), 1u);
  EXPECT_EQ(std::get<1>(delegate_.fetched_pages[0]),
            GURL("https://site.test/"));
  EXPECT_EQ(lookup_.TierOf(kUrl), RowIconTier::kLinkTag);
}

TEST_F(RowIconLookupTest, ThePageNamesCandidatesAndTheFirstIsFetched) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::nullopt);
  const GURL final_url("https://site.test/");
  const std::string body =
      "<html><head><link rel=\"icon\" href=\"/a.png\"><link rel=\"icon\" "
      "href=\"/b.png\"></head></html>";
  lookup_.OnPageFetched(kUrl, final_url, "text/html", body);
  ASSERT_EQ(delegate_.fetched_icons.size(), 2u);
  EXPECT_EQ(std::get<1>(delegate_.fetched_icons[1]),
            GURL("https://site.test/a.png"));

  lookup_.OnIconStored(kUrl, false);
  ASSERT_EQ(delegate_.fetched_icons.size(), 3u);
  EXPECT_EQ(std::get<1>(delegate_.fetched_icons[2]),
            GURL("https://site.test/b.png"));
  EXPECT_TRUE(delegate_.finished.empty());

  lookup_.OnIconStored(kUrl, false);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
}

TEST_F(RowIconLookupTest, NoMoreThanThreeCandidatesAreTried) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::nullopt);
  const GURL final_url("https://site.test/");
  std::string body = "<html><head>";
  for (int i = 0; i < 5; ++i) {
    body += "<link rel=\"icon\" href=\"/i" + base::NumberToString(i) +
           ".png\">";
  }
  body += "</head></html>";
  const size_t before_candidates = delegate_.fetched_icons.size();
  lookup_.OnPageFetched(kUrl, final_url, "text/html", body);
  for (int i = 0; i < 5; ++i) {
    lookup_.OnIconStored(kUrl, false);
  }
  EXPECT_EQ(delegate_.fetched_icons.size() - before_candidates, 3u);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
}

TEST_F(RowIconLookupTest, APageOnAnotherDomainNamesNothing) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::nullopt);
  const size_t icons_before = delegate_.fetched_icons.size();
  const GURL final_url("https://login.other.test/x");
  const std::string body =
      "<html><head><link rel=\"icon\" href=\"/a.png\"></head></html>";
  lookup_.OnPageFetched(kUrl, final_url, "text/html", body);
  EXPECT_EQ(delegate_.fetched_icons.size(), icons_before);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
}

TEST_F(RowIconLookupTest, ADataUrlCandidateSkipsTheNetwork) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::nullopt);
  const size_t icons_before = delegate_.fetched_icons.size();
  const GURL final_url("https://site.test/");
  const std::string body =
      "<html><head><link rel=\"icon\" "
      "href=\"data:image/png;base64,cG5n\"></head></html>";
  lookup_.OnPageFetched(kUrl, final_url, "text/html", body);
  EXPECT_EQ(delegate_.fetched_icons.size(), icons_before);
  ASSERT_EQ(delegate_.stored.size(), 1u);
  EXPECT_EQ(std::get<2>(delegate_.stored[0]), "png");
}

TEST_F(RowIconLookupTest,
      ARefusedRedirectAtIcoReachesThePageAndAtTierFourEndsTheRow) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  lookup_.OnRedirectRefused(kUrl);
  ASSERT_EQ(delegate_.fetched_pages.size(), 1u);
  EXPECT_EQ(lookup_.TierOf(kUrl), RowIconTier::kLinkTag);
  lookup_.OnRedirectRefused(kUrl);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
}

TEST_F(RowIconLookupTest, TheDeadlineEndsARowMidFetch) {
  lookup_.Begin(kUrl);
  lookup_.AskSite(kUrl);
  task_environment_.FastForwardBy(kRowIconDeadline);
  EXPECT_EQ(delegate_.cancelled, std::vector<std::string>{kUrl});
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});

  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::string("bytes"));
  EXPECT_TRUE(delegate_.stored.empty());
  EXPECT_EQ(delegate_.finished.size(), 1u);
}

TEST_F(RowIconLookupTest, ALaterFetchGetsOnlyTheRemainingBudget) {
  lookup_.Begin(kUrl);
  task_environment_.FastForwardBy(base::Seconds(5));
  lookup_.AskSite(kUrl);
  ASSERT_EQ(delegate_.fetched_icons.size(), 1u);
  EXPECT_EQ(std::get<2>(delegate_.fetched_icons[0]),
            kRowIconDeadline - base::Seconds(5));

  task_environment_.FastForwardBy(base::Seconds(5));
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::nullopt);
  ASSERT_EQ(delegate_.fetched_pages.size(), 1u);
  EXPECT_EQ(std::get<2>(delegate_.fetched_pages[0]),
            kRowIconDeadline - base::Seconds(10));
}

TEST_F(RowIconLookupTest, AbandonAllEndsEveryRowSilently) {
  const std::string url2 = "https://other.test/x";
  lookup_.Begin(kUrl);
  lookup_.Begin(url2);
  EXPECT_EQ(lookup_.rows_in_flight(), 2u);
  std::vector<std::string> abandoned = lookup_.AbandonAll();
  std::sort(abandoned.begin(), abandoned.end());
  std::vector<std::string> expected = {kUrl, url2};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(abandoned, expected);
  std::sort(delegate_.cancelled.begin(), delegate_.cancelled.end());
  EXPECT_EQ(delegate_.cancelled, expected);
  EXPECT_TRUE(delegate_.finished.empty());
  EXPECT_EQ(lookup_.rows_in_flight(), 0u);
}

TEST_F(RowIconLookupTest, AnAnswerForAnUnknownRowIsIgnored) {
  const std::string unknown = "https://never-begun.test/";
  lookup_.OnIconFetched(unknown, GURL("https://never-begun.test/favicon.ico"),
                        net::OK, std::string("bytes"));
  EXPECT_TRUE(delegate_.stored.empty());
  EXPECT_TRUE(delegate_.finished.empty());
}

TEST_F(RowIconLookupTest, ADeadlineDuringADecodeFinishesTheRowOnce) {
  lookup_.Begin(kUrl);
  lookup_.OnIconFetched(kUrl, GURL("https://site.test/favicon.ico"), net::OK,
                        std::string("bytes"));
  task_environment_.FastForwardBy(kRowIconDeadline);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
  EXPECT_TRUE(delegate_.allow_retry.empty());

  lookup_.OnIconStored(kUrl, true);
  EXPECT_EQ(delegate_.finished.size(), 1u);
}

TEST_F(RowIconLookupTest, ADeadlineBeforeAnyFetchAllowsARetry) {
  lookup_.Begin(kUrl);
  task_environment_.FastForwardBy(kRowIconDeadline);
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
  ASSERT_EQ(delegate_.allow_retry.size(), 1u);
  EXPECT_EQ(std::get<0>(delegate_.allow_retry[0]), kUrl);
  EXPECT_EQ(std::get<1>(delegate_.allow_retry[0]), GURL(kUrl));

  lookup_.AskSite(kUrl);
  EXPECT_TRUE(delegate_.fetched_icons.empty());
  EXPECT_TRUE(delegate_.fetched_pages.empty());
  EXPECT_EQ(delegate_.finished.size(), 1u);
}

TEST_F(RowIconLookupTest, ABudgetTooSmallForAFetchAllowsARetry) {
  lookup_.Begin(kUrl);
  task_environment_.FastForwardBy(kRowIconDeadline - base::Milliseconds(500));
  lookup_.AskSite(kUrl);
  EXPECT_TRUE(delegate_.fetched_icons.empty());
  EXPECT_TRUE(delegate_.fetched_pages.empty());
  EXPECT_EQ(delegate_.finished, std::vector<std::string>{kUrl});
  ASSERT_EQ(delegate_.allow_retry.size(), 1u);
  EXPECT_EQ(std::get<0>(delegate_.allow_retry[0]), kUrl);
  EXPECT_EQ(std::get<1>(delegate_.allow_retry[0]), GURL(kUrl));
}

}  // namespace
}  // namespace atrium
