// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_SQL_READER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_SQL_READER_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/strings/cstring_view.h"

namespace base {
class FilePath;
}  // namespace base

namespace atrium {

// Another browser's database past this size is not a real profile.
inline constexpr int64_t kMaxImportDatabaseBytes = 512LL * 1024 * 1024;

// A text value past this arrives as null. A cut value would be a broken URL
// or a wrong keyword.
inline constexpr size_t kMaxImportTextBytes = 2048;

// No budget, for a caller that never wants the stop.
inline constexpr size_t kNoImportByteBudget =
    std::numeric_limits<size_t>::max();

// Rows of `query` from a private copy of `db`, as a JSON array of objects.
// `columns` names the query's result columns in order, because //sql has no
// column-name accessor. `max_bytes` bounds the text columns' total size, and
// `budget_cut` (if given) reports whether that bound stopped the read.
// nullopt when `db` is not a regular file, is too large, keeps changing
// during the copy, or the query fails. BLOCKING.
std::optional<std::string> ReadImportRows(
    const base::FilePath& db,
    base::cstring_view query,
    base::span<const std::string_view> columns,
    size_t max_rows,
    size_t max_bytes = kNoImportByteBudget,
    bool* budget_cut = nullptr);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_SQL_READER_H_
