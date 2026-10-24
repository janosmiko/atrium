// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_import_sql_reader.h"

#include <sys/stat.h>

#include <cmath>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace atrium {
namespace {

// A checkpoint that lands between the main-file copy and the WAL copy leaves
// a pair that does not match, so a change during the copy means copy again.
constexpr int kCopyTries = 3;

// SQLite builds a new -shm on open, so it is never copied.
constexpr const char* kSideFileSuffixes[] = {"-wal", "-journal"};

struct Stamp {
  int64_t size = 0;
  base::Time modified;
  friend bool operator==(const Stamp&, const Stamp&) = default;
};

// Opening a FIFO blocks until a writer connects, so only a regular file gets
// a stamp.
std::optional<Stamp> StampOf(const base::FilePath& path) {
  base::stat_wrapper_t stat;
  if (base::File::Stat(path, &stat) != 0 || !S_ISREG(stat.st_mode)) {
    return std::nullopt;
  }
  base::File::Info info;
  info.FromStat(stat);
  return Stamp{info.size, info.last_modified};
}

bool CopyWithSideFiles(const base::FilePath& db, const base::FilePath& copy) {
  if (!base::CopyFile(db, copy)) {
    return false;
  }
  for (const char* suffix : kSideFileSuffixes) {
    const base::FilePath side(db.value() + suffix);
    const base::FilePath side_copy(copy.value() + suffix);
    // A side file left from an earlier try would replay stale pages.
    base::DeleteFile(side_copy);
    if (!base::PathExists(side)) {
      continue;
    }
    const std::optional<Stamp> stamp = StampOf(side);
    if (!stamp || stamp->size > kMaxImportDatabaseBytes ||
        !base::CopyFile(side, side_copy)) {
      return false;
    }
  }
  return true;
}

bool CopyWhileUnchanged(const base::FilePath& db, const base::FilePath& copy) {
  for (int attempt = 0; attempt < kCopyTries; ++attempt) {
    const std::optional<Stamp> before = StampOf(db);
    if (!before || before->size > kMaxImportDatabaseBytes ||
        !CopyWithSideFiles(db, copy)) {
      return false;
    }
    if (StampOf(db) == before) {
      return true;
    }
  }
  return false;
}

base::Value Cell(sql::Statement& statement, int column) {
  switch (statement.GetColumnType(column)) {
    case sql::ColumnType::kInteger: {
      const int64_t value = statement.ColumnInt64(column);
      return base::IsValueInRangeForNumericType<int>(value)
                 ? base::Value(static_cast<int>(value))
                 : base::Value(static_cast<double>(value));
    }
    case sql::ColumnType::kFloat: {
      const double value = statement.ColumnDouble(column);
      return std::isfinite(value) ? base::Value(value) : base::Value();
    }
    case sql::ColumnType::kText: {
      const std::string_view text = statement.ColumnStringView(column);
      return text.size() > kMaxImportTextBytes || !base::IsStringUTF8(text)
                 ? base::Value()
                 : base::Value(text);
    }
    case sql::ColumnType::kBlob:
    case sql::ColumnType::kNull:
      return base::Value();
  }
  NOTREACHED();
}

}  // namespace

std::optional<std::string> ReadImportRows(
    const base::FilePath& db,
    base::cstring_view query,
    base::span<const std::string_view> columns,
    size_t max_rows,
    size_t max_bytes,
    bool* budget_cut) {
  if (budget_cut) {
    *budget_cut = false;
  }
  base::ScopedTempDir temp;
  if (!temp.CreateUniqueTempDir()) {
    return std::nullopt;
  }
  const base::FilePath copy = temp.GetPath().Append("import.db");
  if (!CopyWhileUnchanged(db, copy)) {
    return std::nullopt;
  }
  // The copy is private, so it opens read and write: SQLite can replay the
  // WAL or roll back a hot journal into the copy, which a read-only open
  // refuses. Views and triggers stay off, the //sql default, so a crafted
  // file cannot stand a view in for a table. "FirefoxImporter" is the one
  // DatabaseTag for another browser's file, and a new tag would patch
  // Chromium's histograms.xml.
  sql::Database database(sql::DatabaseOptions(), "FirefoxImporter");
  // Another browser wrote this file, so a corrupt one is an expected answer,
  // and a debug build must not crash on it.
  database.set_error_callback(base::DoNothing());
  if (!database.Open(copy)) {
    return std::nullopt;
  }
  sql::Statement statement(database.GetUniqueStatement(query));
  if (!statement.is_valid() ||
      statement.ColumnCount() != static_cast<int>(columns.size())) {
    return std::nullopt;
  }
  base::ListValue rows;
  size_t text_bytes = 0;
  while (rows.size() < max_rows && statement.Step()) {
    base::DictValue row;
    size_t row_bytes = 0;
    for (size_t i = 0; i < columns.size(); ++i) {
      base::Value cell = Cell(statement, static_cast<int>(i));
      row_bytes += cell.is_string() ? cell.GetString().size() : 0;
      row.Set(columns[i], std::move(cell));
    }
    if (text_bytes + row_bytes > max_bytes) {
      if (budget_cut) {
        *budget_cut = true;
      }
      break;
    }
    text_bytes += row_bytes;
    rows.Append(std::move(row));
  }
  if (!statement.Succeeded()) {
    return std::nullopt;
  }
  return base::WriteJson(rows);
}

}  // namespace atrium
