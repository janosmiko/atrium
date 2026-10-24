// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_HISTORY_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_HISTORY_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"

namespace base {
class FilePath;
}  // namespace base

namespace history {
class HistoryService;
}  // namespace history

namespace atrium {

// Chromium's History file, or Mozilla's places.sqlite.
enum class HistoryDb { kChromium, kMozilla };

// The newest visits kept for one source profile, and for one whole import.
inline constexpr size_t kMaxImportHistoryVisits = 20000;
inline constexpr size_t kMaxImportHistoryVisitsTotal = 50000;

// Chromium counts from 1601 and Mozilla from 1970. Both queries hand over
// milliseconds, because microseconds since 1601 pass 2^53 and a JSON double
// would round them.
base::Time ChromiumVisitTime(double ms);
base::Time MozillaVisitTime(double ms);

// One row per visit in ReadImportRows's JSON. A row whose URL is not http,
// https or file, or whose time is missing or in the future, is left out.
history::URLRows HistoryRowsFromJson(std::string_view json, HistoryDb db);

struct HistoryRead {
  history::URLRows rows;
  // The file held more than `max_rows` visits, so the older ones were left
  // out.
  bool cut = false;
};

// The newest `max_rows` visible visits of the last 90 days in `file`, newest
// first. nullopt when the file cannot be read. BLOCKING.
std::optional<HistoryRead> ReadImportHistory(const base::FilePath& file,
                                             HistoryDb db,
                                             size_t max_rows);

// One source profile's visits for one Atrium profile.
struct ImportedHistory {
  std::string profile_id;
  history::VisitSource visit_source = history::SOURCE_BROWSED;
  history::URLRows rows;
};

// Adds `rows` to `service` and calls `done` with the number of visits added.
// A visit lands only when it is later than every visit `service` already has
// for its URL, so a second import of the same file adds nothing. UI thread.
void AddImportedHistory(history::HistoryService* service,
                        history::URLRows rows,
                        history::VisitSource source,
                        base::OnceCallback<void(size_t)> done);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_IMPORT_HISTORY_H_
