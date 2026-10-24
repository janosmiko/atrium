// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_import_sql_reader.h"

#include <sys/stat.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_restrictions.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr std::string_view kColumns[] = {"n", "name"};
constexpr char kQuery[] = "SELECT n, name FROM t ORDER BY rowid";

class AtriumImportSqlReaderTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath Path(std::string_view name) const {
    return dir_.GetPath().AppendASCII(name);
  }

  // A database at `path` with table t(n, name). The caller keeps it open.
  static std::unique_ptr<sql::Database> OpenTable(const base::FilePath& path,
                                                  bool wal) {
    auto db = std::make_unique<sql::Database>(
        sql::DatabaseOptions().set_wal_mode(wal), sql::Database::Tag("Test"));
    CHECK(db->Open(path));
    CHECK(db->Execute("CREATE TABLE t(n INTEGER, name TEXT)"));
    return db;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedAllowBlockingForTesting allow_blocking_;
  base::ScopedTempDir dir_;
};

TEST_F(AtriumImportSqlReaderTest, AWalDatabaseLeftOpenReadsItsNewestRows) {
  const base::FilePath db_path = Path("places.sqlite");
  std::unique_ptr<sql::Database> writer = OpenTable(db_path, /*wal=*/true);
  ASSERT_TRUE(writer->Execute("INSERT INTO t VALUES (1, 'a'), (NULL, 'b')"));
  const base::FilePath wal(db_path.value() + "-wal");
  ASSERT_GT(base::GetFileSize(wal).value_or(0), 0)
      << "the rows must still sit in the WAL, or this test proves nothing";
  std::string main_before, wal_before;
  ASSERT_TRUE(base::ReadFileToString(db_path, &main_before));
  ASSERT_TRUE(base::ReadFileToString(wal, &wal_before));

  const std::optional<std::string> rows =
      ReadImportRows(db_path, kQuery, kColumns, 10);

  ASSERT_TRUE(rows.has_value());
  EXPECT_EQ(R"([{"n":1,"name":"a"},{"n":null,"name":"b"}])", *rows);
  std::string main_after, wal_after;
  ASSERT_TRUE(base::ReadFileToString(db_path, &main_after));
  ASSERT_TRUE(base::ReadFileToString(wal, &wal_after));
  EXPECT_EQ(main_before, main_after) << "the reader never writes the source";
  EXPECT_EQ(wal_before, wal_after);
}

TEST_F(AtriumImportSqlReaderTest, TheRowCapStopsTheRead) {
  std::unique_ptr<sql::Database> writer =
      OpenTable(Path("capped.sqlite"), /*wal=*/false);
  ASSERT_TRUE(writer->Execute("INSERT INTO t VALUES (1, 'a'), (2, 'b'), "
                              "(3, 'c')"));
  writer.reset();

  const std::optional<std::string> rows =
      ReadImportRows(Path("capped.sqlite"), kQuery, kColumns, 2);

  ASSERT_TRUE(rows.has_value());
  EXPECT_EQ(R"([{"n":1,"name":"a"},{"n":2,"name":"b"}])", *rows);
}

TEST_F(AtriumImportSqlReaderTest, TextPastTheCapArrivesAsNullNotCut) {
  std::unique_ptr<sql::Database> writer =
      OpenTable(Path("text.sqlite"), /*wal=*/false);
  sql::Statement insert(
      writer->GetUniqueStatement("INSERT INTO t VALUES (?, ?)"));
  insert.BindInt(0, 1);
  insert.BindString(1, std::string(kMaxImportTextBytes + 1, 'x'));
  ASSERT_TRUE(insert.Run());
  insert.Reset(/*clear_bound_vars=*/true);
  insert.BindInt(0, 2);
  insert.BindString(1, std::string(kMaxImportTextBytes, 'y'));
  ASSERT_TRUE(insert.Run());
  // Closing writer's database DCHECKs on an outstanding open statement.
  insert.Clear();
  writer.reset();

  const std::optional<std::string> rows =
      ReadImportRows(Path("text.sqlite"), kQuery, kColumns, 10);

  ASSERT_TRUE(rows.has_value());
  EXPECT_EQ(base::StrCat({R"([{"n":1,"name":null},{"n":2,"name":")",
                          std::string(kMaxImportTextBytes, 'y'), R"("}])"}),
            *rows);
}

TEST_F(AtriumImportSqlReaderTest, AFileOverTheSizeCapIsRefused) {
  base::File file(Path("big.sqlite"),
                  base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.SetLength(kMaxImportDatabaseBytes + 1));
  file.Close();
  EXPECT_FALSE(ReadImportRows(Path("big.sqlite"), kQuery, kColumns, 10));
}

TEST_F(AtriumImportSqlReaderTest, AFileThatIsNotADatabaseIsRefused) {
  ASSERT_TRUE(base::WriteFile(Path("junk.sqlite"), "not a database at all"));
  EXPECT_FALSE(ReadImportRows(Path("junk.sqlite"), kQuery, kColumns, 10));
}

TEST_F(AtriumImportSqlReaderTest, AFifoIsRefusedWithoutBlocking) {
  ASSERT_EQ(0, mkfifo(Path("fifo.sqlite").value().c_str(), 0644));
  // A copy that did not check the file type would block here forever,
  // because nothing ever connects as a writer.
  EXPECT_FALSE(ReadImportRows(Path("fifo.sqlite"), kQuery, kColumns, 10));
}

TEST_F(AtriumImportSqlReaderTest, AMissingFileIsRefused) {
  EXPECT_FALSE(ReadImportRows(Path("absent.sqlite"), kQuery, kColumns, 10));
}

TEST_F(AtriumImportSqlReaderTest, AColumnListThatDoesNotMatchIsRefused) {
  OpenTable(Path("columns.sqlite"), /*wal=*/false).reset();
  constexpr std::string_view kThree[] = {"n", "name", "extra"};
  EXPECT_FALSE(ReadImportRows(Path("columns.sqlite"), kQuery, kThree, 10));
}

TEST_F(AtriumImportSqlReaderTest, TheByteBudgetStopsTheReadAndReportsIt) {
  std::unique_ptr<sql::Database> writer =
      OpenTable(Path("budget.sqlite"), /*wal=*/false);
  ASSERT_TRUE(writer->Execute("INSERT INTO t VALUES (1, 'aaaa'), (2, 'bbbb'), "
                              "(3, 'cccc')"));
  writer.reset();

  bool cut = false;
  const std::optional<std::string> rows = ReadImportRows(
      Path("budget.sqlite"), kQuery, kColumns, 10, /*max_bytes=*/4, &cut);

  ASSERT_TRUE(rows.has_value());
  EXPECT_TRUE(cut);
  EXPECT_EQ(R"([{"n":1,"name":"aaaa"}])", *rows);
}

TEST_F(AtriumImportSqlReaderTest, ATableUnderTheByteBudgetReadsEveryRow) {
  std::unique_ptr<sql::Database> writer =
      OpenTable(Path("under_budget.sqlite"), /*wal=*/false);
  ASSERT_TRUE(writer->Execute("INSERT INTO t VALUES (1, 'a'), (2, 'b')"));
  writer.reset();

  bool cut = true;
  const std::optional<std::string> rows =
      ReadImportRows(Path("under_budget.sqlite"), kQuery, kColumns, 10,
                     /*max_bytes=*/1024, &cut);

  ASSERT_TRUE(rows.has_value());
  EXPECT_FALSE(cut);
  EXPECT_EQ(R"([{"n":1,"name":"a"},{"n":2,"name":"b"}])", *rows);
}

TEST_F(AtriumImportSqlReaderTest, AViewCannotStandInForATable) {
  {
    sql::Database writer(
        sql::DatabaseOptions().set_enable_views_discouraged(true), "Test");
    ASSERT_TRUE(writer.Open(Path("view.sqlite")));
    ASSERT_TRUE(writer.Execute("CREATE TABLE real(n INTEGER, name TEXT)"));
    ASSERT_TRUE(writer.Execute("INSERT INTO real VALUES (1, 'a')"));
    ASSERT_TRUE(writer.Execute("CREATE VIEW t AS SELECT n, name FROM real"));
  }
  EXPECT_FALSE(ReadImportRows(Path("view.sqlite"), kQuery, kColumns, 10));
}

}  // namespace
}  // namespace atrium
