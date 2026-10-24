// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_import_history.h"

#include <memory>
#include <optional>
#include <string_view>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/cstring_view.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/test/base/testing_profile.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/url_row.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/test/browser_task_environment.h"
#include "sql/database.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

constexpr char kChromiumUrls[] =
    "CREATE TABLE urls(id INTEGER PRIMARY KEY, url TEXT, title TEXT, "
    "visit_count INTEGER, typed_count INTEGER, hidden INTEGER)";
constexpr char kChromiumVisits[] =
    "CREATE TABLE visits(id INTEGER PRIMARY KEY, url INTEGER, "
    "visit_time INTEGER, transition INTEGER)";
constexpr char kChromiumUrlRows[] =
    "INSERT INTO urls VALUES "
    "(1, 'https://a.example.test/', 'A', 2, 1, 0), "
    "(2, 'https://b.example.test/', 'B', 1, 0, 0), "
    "(3, 'javascript:void(0)', 'J', 1, 0, 0), "
    "(4, 'https://old.example.test/', 'O', 1, 0, 0), "
    "(5, 'https://frame.example.test/', 'F', 1, 0, 0), "
    "(6, 'https://mid.example.test/', 'M', 1, 0, 0), "
    "(7, 'https://hidden.example.test/', 'H', 1, 0, 1)";
// Times are days before now. 536870912 is CHAIN_END, 536870913 is CHAIN_END
// with TYPED, 536870915 is CHAIN_END with AUTO_SUBFRAME, and 1 is a TYPED
// visit in the middle of a redirect chain.
constexpr char kChromiumVisitRows[] =
    "INSERT INTO visits(url, visit_time, transition) VALUES "
    "(1, (strftime('%s','now') + 11644473600 - 86400) * 1000000, 536870913), "
    "(1, (strftime('%s','now') + 11644473600 - 259200) * 1000000, 536870912), "
    "(2, (strftime('%s','now') + 11644473600 - 172800) * 1000000, 536870912), "
    "(3, (strftime('%s','now') + 11644473600 - 432000) * 1000000, 536870912), "
    "(4, (strftime('%s','now') + 11644473600 - 17280000) * 1000000, "
    "536870912), "
    "(5, (strftime('%s','now') + 11644473600 - 86400) * 1000000, 536870915), "
    "(6, (strftime('%s','now') + 11644473600 - 86400) * 1000000, 1), "
    "(7, (strftime('%s','now') + 11644473600 - 86400) * 1000000, 536870912)";

constexpr char kMozPlaces[] =
    "CREATE TABLE moz_places(id INTEGER PRIMARY KEY, url TEXT, title TEXT, "
    "visit_count INTEGER, hidden INTEGER)";
constexpr char kMozVisits[] =
    "CREATE TABLE moz_historyvisits(id INTEGER PRIMARY KEY, place_id INTEGER, "
    "visit_date INTEGER, visit_type INTEGER)";
constexpr char kMozPlaceRows[] =
    "INSERT INTO moz_places VALUES "
    "(1, 'https://a.example.test/', 'A', 3, 0), "
    "(2, 'https://b.example.test/', NULL, 1, 0), "
    "(3, 'https://embed.example.test/', 'E', 1, 0), "
    "(4, 'https://old.example.test/', 'O', 1, 0), "
    "(5, 'https://hidden.example.test/', 'H', 1, 1)";
// visit_type 1 is LINK, 2 TYPED, 3 BOOKMARK and 4 EMBED.
constexpr char kMozVisitRows[] =
    "INSERT INTO moz_historyvisits(place_id, visit_date, visit_type) VALUES "
    "(1, (strftime('%s','now') - 86400) * 1000000, 2), "
    "(1, (strftime('%s','now') - 172800) * 1000000, 1), "
    "(1, (strftime('%s','now') - 259200) * 1000000, 2), "
    "(2, (strftime('%s','now') - 345600) * 1000000, 3), "
    "(3, (strftime('%s','now') - 86400) * 1000000, 4), "
    "(4, (strftime('%s','now') - 17280000) * 1000000, 1), "
    "(5, (strftime('%s','now') - 86400) * 1000000, 1)";

class AtriumImportHistoryTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath Write(std::string_view name,
                       base::cstring_view create_urls,
                       base::cstring_view create_visits,
                       base::cstring_view urls,
                       base::cstring_view visits) {
    const base::FilePath path = dir_.GetPath().AppendASCII(name);
    sql::Database db(sql::DatabaseOptions(), sql::Database::Tag("Test"));
    CHECK(db.Open(path));
    CHECK(db.Execute(create_urls));
    CHECK(db.Execute(create_visits));
    CHECK(db.Execute(urls));
    CHECK(db.Execute(visits));
    return path;
  }

  static bool Near(base::Time a, base::Time b) {
    return (a - b).magnitude() < base::Seconds(5);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedAllowBlockingForTesting allow_blocking_;
  base::ScopedTempDir dir_;
};

TEST_F(AtriumImportHistoryTest, BothEpochsLandOnTheSameInstant) {
  // 2026-01-01T00:00:00Z in each schema's milliseconds.
  EXPECT_EQ(MozillaVisitTime(1767225600000), ChromiumVisitTime(13411699200000));
  EXPECT_EQ(1767225600000,
            ChromiumVisitTime(13411699200000).InMillisecondsSinceUnixEpoch());
  EXPECT_EQ(base::Time::UnixEpoch(), MozillaVisitTime(0));
  EXPECT_EQ(base::Time::UnixEpoch(), ChromiumVisitTime(11644473600000));
}

TEST_F(AtriumImportHistoryTest, ARowNeedsAWebUrlAndATime) {
  const history::URLRows rows = HistoryRowsFromJson(
      R"([{"url":"https://a.example.test/","title":null,"visit_count":-3,)"
      R"("typed_count":2,"visit_ms":1767225600000},)"
      R"({"url":null,"title":"x","visit_count":1,"typed_count":0,)"
      R"("visit_ms":1767225600000},)"
      R"({"url":"https://b.example.test/","title":"B","visit_count":1,)"
      R"("typed_count":0,"visit_ms":null},)"
      R"({"url":"file:///tmp/a.html","title":"F","visit_count":1,)"
      R"("typed_count":0,"visit_ms":1767225600000},)"
      R"({"url":"chrome://settings","title":"S","visit_count":1,)"
      R"("typed_count":0,"visit_ms":1767225600000},)"
      R"({"url":"https://future.example.test/","title":"T","visit_count":1,)"
      R"("typed_count":0,"visit_ms":99999999999999},)"
      R"("not a row"])",
      HistoryDb::kMozilla);

  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ(GURL("https://a.example.test/"), rows[0].url());
  EXPECT_EQ(u"", rows[0].title());
  EXPECT_EQ(0, rows[0].visit_count()) << "a negative count is clamped";
  EXPECT_EQ(2, rows[0].typed_count());
  EXPECT_EQ(MozillaVisitTime(1767225600000), rows[0].last_visit());
  EXPECT_EQ(GURL("file:///tmp/a.html"), rows[1].url());
  EXPECT_TRUE(HistoryRowsFromJson("not json", HistoryDb::kChromium).empty());
}

TEST_F(AtriumImportHistoryTest, ChromiumRowsAreTheVisibleVisitsNewestFirst) {
  const base::FilePath file = Write("History", kChromiumUrls, kChromiumVisits,
                                    kChromiumUrlRows, kChromiumVisitRows);

  const std::optional<HistoryRead> read =
      ReadImportHistory(file, HistoryDb::kChromium, 100);

  ASSERT_TRUE(read.has_value());
  EXPECT_FALSE(read->cut);
  ASSERT_EQ(3u, read->rows.size())
      << "old, subframe, mid-chain, hidden and javascript: rows are left out";
  EXPECT_EQ(GURL("https://a.example.test/"), read->rows[0].url());
  EXPECT_EQ(GURL("https://b.example.test/"), read->rows[1].url());
  EXPECT_EQ(GURL("https://a.example.test/"), read->rows[2].url());
  EXPECT_EQ(u"A", read->rows[0].title());
  EXPECT_EQ(2, read->rows[0].visit_count());
  EXPECT_EQ(1, read->rows[0].typed_count());
  EXPECT_TRUE(
      Near(base::Time::Now() - base::Days(1), read->rows[0].last_visit()));
  EXPECT_TRUE(
      Near(base::Time::Now() - base::Days(3), read->rows[2].last_visit()));
}

TEST_F(AtriumImportHistoryTest, TheCapKeepsTheNewestAndSaysSo) {
  const base::FilePath file = Write("History", kChromiumUrls, kChromiumVisits,
                                    kChromiumUrlRows, kChromiumVisitRows);

  const std::optional<HistoryRead> capped =
      ReadImportHistory(file, HistoryDb::kChromium, 2);
  ASSERT_TRUE(capped.has_value());
  EXPECT_TRUE(capped->cut);
  ASSERT_EQ(2u, capped->rows.size());
  EXPECT_EQ(GURL("https://b.example.test/"), capped->rows[1].url());

  // Four rows pass the query, the javascript: one among them.
  const std::optional<HistoryRead> exact =
      ReadImportHistory(file, HistoryDb::kChromium, 4);
  ASSERT_TRUE(exact.has_value());
  EXPECT_FALSE(exact->cut) << "a file that fits the cap exactly is not cut";
  EXPECT_EQ(3u, exact->rows.size());
}

TEST_F(AtriumImportHistoryTest, MozillaRowsCountTypedVisits) {
  const base::FilePath file = Write("places.sqlite", kMozPlaces, kMozVisits,
                                    kMozPlaceRows, kMozVisitRows);

  const std::optional<HistoryRead> read =
      ReadImportHistory(file, HistoryDb::kMozilla, 100);

  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(4u, read->rows.size())
      << "embed, old and hidden visits are left out";
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(GURL("https://a.example.test/"), read->rows[i].url()) << i;
    EXPECT_EQ(2, read->rows[i].typed_count()) << i;
    EXPECT_EQ(3, read->rows[i].visit_count()) << i;
  }
  EXPECT_EQ(GURL("https://b.example.test/"), read->rows[3].url());
  EXPECT_EQ(u"", read->rows[3].title());
  EXPECT_EQ(0, read->rows[3].typed_count());
  EXPECT_TRUE(
      Near(base::Time::Now() - base::Days(4), read->rows[3].last_visit()));
}

TEST_F(AtriumImportHistoryTest, AMissingOrDamagedFileIsUnreadable) {
  EXPECT_FALSE(ReadImportHistory(dir_.GetPath().AppendASCII("absent"),
                                 HistoryDb::kChromium, 10));
  const base::FilePath junk = dir_.GetPath().AppendASCII("History");
  ASSERT_TRUE(base::WriteFile(junk, "not a database"));
  EXPECT_FALSE(ReadImportHistory(junk, HistoryDb::kChromium, 10));
}

history::URLRow Visit(std::string_view url, base::Time at) {
  history::URLRow row{GURL(url)};
  row.set_title(u"Quokka field notes");
  row.set_visit_count(3);
  row.set_typed_count(1);
  row.set_last_visit(at);
  return row;
}

class AtriumImportHistoryWriteTest : public testing::Test {
 protected:
  void SetUp() override {
    TestingProfile::Builder builder;
    builder.AddTestingFactory(HistoryServiceFactory::GetInstance(),
                              HistoryServiceFactory::GetDefaultFactory());
    profile_ = builder.Build();
    service_ = HistoryServiceFactory::GetForProfile(
        profile_.get(), ServiceAccessType::EXPLICIT_ACCESS);
    ASSERT_TRUE(service_);
  }

  size_t Add(const history::URLRows& rows, history::VisitSource source) {
    base::test::TestFuture<size_t> added;
    AddImportedHistory(service_, rows, source, added.GetCallback());
    return added.Get();
  }

  size_t Visits(const GURL& url) {
    base::CancelableTaskTracker tracker;
    base::test::TestFuture<history::QueryURLAndVisitsResult> result;
    service_->QueryURLAndVisits(
        url, history::VisitQuery404sPolicy::kInclude404s,
        result.GetCallback(), &tracker);
    return result.Get().visits.size();
  }

  history::VisitSource NewestSource(const GURL& url) {
    base::test::TestFuture<history::VisitSource> source;
    service_->ScheduleDBTaskForUI(base::BindOnce(
        [](GURL url, scoped_refptr<base::SequencedTaskRunner> reply,
           base::OnceCallback<void(history::VisitSource)> done,
           history::HistoryBackend* backend, history::URLDatabase*) {
          history::VisitSource found = history::SOURCE_SYNCED;
          const history::QueryURLAndVisitsResult result =
              backend->QueryURLAndVisits(
                  url, history::VisitQuery404sPolicy::kInclude404s);
          if (!result.visits.empty()) {
            backend->GetVisitSource(result.visits.back().visit_id, &found);
          }
          reply->PostTask(FROM_HERE, base::BindOnce(std::move(done), found));
        },
        url, base::SequencedTaskRunner::GetCurrentDefault(),
        source.GetCallback()));
    return source.Get();
  }

  content::BrowserTaskEnvironment task_environment_;
  std::unique_ptr<TestingProfile> profile_;
  raw_ptr<history::HistoryService> service_ = nullptr;
};

TEST_F(AtriumImportHistoryWriteTest, ARepeatedImportAddsNoVisit) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");
  const GURL b("https://b.example.test/");
  const history::URLRows rows = {Visit(a.spec(), now - base::Days(1)),
                                 Visit(b.spec(), now - base::Days(1)),
                                 Visit(a.spec(), now - base::Days(2))};

  EXPECT_EQ(3u, Add(rows, history::SOURCE_OS_MIGRATION_IMPORTED));
  EXPECT_EQ(2u, Visits(a));
  EXPECT_EQ(1u, Visits(b));

  EXPECT_EQ(0u, Add(rows, history::SOURCE_OS_MIGRATION_IMPORTED));
  EXPECT_EQ(2u, Visits(a));
  EXPECT_EQ(1u, Visits(b));
}

TEST_F(AtriumImportHistoryWriteTest, OnlyVisitsNewerThanAtriumsLastOneLand) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");
  EXPECT_EQ(1u, Add({Visit(a.spec(), now - base::Days(3))},
                    history::SOURCE_FIREFOX_IMPORTED));

  EXPECT_EQ(1u, Add({Visit(a.spec(), now - base::Days(1)),
                     Visit(a.spec(), now - base::Days(3)),
                     Visit(a.spec(), now - base::Days(5))},
                    history::SOURCE_FIREFOX_IMPORTED));
  EXPECT_EQ(2u, Visits(a));
}

TEST_F(AtriumImportHistoryWriteTest, AUrlVisitedInAtriumSinceIsSkipped) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");
  service_->AddPage(a, now - base::Hours(1), history::SOURCE_BROWSED);

  EXPECT_EQ(0u, Add({Visit(a.spec(), now - base::Days(1))},
                    history::SOURCE_OS_MIGRATION_IMPORTED));
  EXPECT_EQ(1u, Visits(a));
}

TEST_F(AtriumImportHistoryWriteTest, AnExpiredVisitIsNotCounted) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");

  // HistoryBackend drops a visit past its 90-day expiration threshold, so
  // AddPagesWithDetails writes nothing for this row.
  EXPECT_EQ(0u, Add({Visit(a.spec(), now - base::Days(200))},
                    history::SOURCE_OS_MIGRATION_IMPORTED));
  EXPECT_EQ(0u, Visits(a));
}

TEST_F(AtriumImportHistoryWriteTest, TwoFreshVisitsOfOneUrlBothCount) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");

  EXPECT_EQ(2u, Add({Visit(a.spec(), now - base::Days(1)),
                     Visit(a.spec(), now - base::Days(2))},
                    history::SOURCE_OS_MIGRATION_IMPORTED));
  EXPECT_EQ(2u, Visits(a));
}

TEST_F(AtriumImportHistoryWriteTest, TheImportedVisitCarriesItsSource) {
  const base::Time now = base::Time::Now();
  const GURL a("https://a.example.test/");
  const GURL b("https://b.example.test/");
  Add({Visit(a.spec(), now - base::Days(1))},
      history::SOURCE_OS_MIGRATION_IMPORTED);
  Add({Visit(b.spec(), now - base::Days(1))}, history::SOURCE_FIREFOX_IMPORTED);

  EXPECT_EQ(history::SOURCE_OS_MIGRATION_IMPORTED, NewestSource(a));
  EXPECT_EQ(history::SOURCE_FIREFOX_IMPORTED, NewestSource(b));
}

}  // namespace
}  // namespace atrium
