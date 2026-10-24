// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_import_history.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_import_sql_reader.h"
#include "components/history/core/browser/history_backend.h"
#include "components/history/core/browser/history_service.h"
#include "url/gurl.h"

namespace atrium {
namespace {

constexpr std::string_view kHistoryColumns[] = {
    "url", "title", "visit_count", "typed_count", "visit_ms"};

// A hostile profile folder can hold 50000 rows of a few KB of title and URL
// text each. This bounds that total, independent of the row cap.
constexpr size_t kMaxHistoryBytes = 32 * 1024 * 1024;

// HistoryBackend drops a visit older than 90 days, so none is read. The
// transition rule is the history page's own: the end of a redirect chain,
// and no subframe or keyword-generated visit.
constexpr char kChromiumHistoryQuery[] =
    "SELECT u.url, u.title, u.visit_count, u.typed_count, "
    "v.visit_time / 1000 FROM visits v JOIN urls u ON u.id = v.url "
    "WHERE u.hidden = 0 AND (v.transition & 536870912) != 0 "
    "AND (v.transition & 255) NOT IN (3, 4, 10) "
    "AND v.visit_time > "
    "(strftime('%s', 'now') + 11644473600 - 7776000) * 1000000 "
    "ORDER BY v.visit_time DESC";

// Link, typed and bookmark visits. The rest are redirects, embeds, downloads
// and reloads. moz_places keeps only a typed flag, and the omnibox ranks by
// the typed count, so the typed visits are counted.
constexpr char kMozillaHistoryQuery[] =
    "SELECT p.url, p.title, p.visit_count, "
    "(SELECT count(*) FROM moz_historyvisits t "
    "WHERE t.place_id = p.id AND t.visit_type = 2), "
    "v.visit_date / 1000 FROM moz_historyvisits v "
    "JOIN moz_places p ON p.id = v.place_id "
    "WHERE p.hidden = 0 AND v.visit_type IN (1, 2, 3) "
    "AND v.visit_date > (strftime('%s', 'now') - 7776000) * 1000000 "
    "ORDER BY v.visit_date DESC";

int Count(const base::DictValue& row, std::string_view key) {
  return std::max(0, row.FindInt(key).value_or(0));
}

history::URLRows RowsFromList(const base::ListValue& list, HistoryDb db) {
  history::URLRows rows;
  // A future visit would stay at the top of the history page for good.
  const base::Time now = base::Time::Now();
  for (size_t i = 0; i < list.size(); ++i) {
    const base::DictValue* row = list[i].GetIfDict();
    const std::string* url = row ? row->FindString("url") : nullptr;
    const std::optional<double> ms =
        row ? row->FindDouble("visit_ms") : std::nullopt;
    if (!url || !ms) {
      continue;
    }
    const GURL gurl(*url);
    if (!gurl.is_valid() ||
        !(gurl.SchemeIsHTTPOrHTTPS() || gurl.SchemeIsFile())) {
      continue;
    }
    const base::Time at = db == HistoryDb::kChromium ? ChromiumVisitTime(*ms)
                                                     : MozillaVisitTime(*ms);
    if (at.is_null() || at > now) {
      continue;
    }
    history::URLRow out(gurl);
    const std::string* title = row->FindString("title");
    out.set_title(base::UTF8ToUTF16(title ? *title : std::string()));
    out.set_visit_count(Count(*row, "visit_count"));
    out.set_typed_count(Count(*row, "typed_count"));
    out.set_last_visit(at);
    rows.push_back(std::move(out));
  }
  return rows;
}

// AddPagesWithDetails never moves an existing URL row's last visit, so the
// row alone would let a second import add the same visits again. The newest
// visit is asked, once per URL and before any add, so every older visit of
// one URL still lands on the first import.
size_t AddNewerVisits(history::HistoryBackend* backend,
                      history::URLRows rows,
                      history::VisitSource source) {
  std::map<GURL, base::Time> newest;
  for (const history::URLRow& row : rows) {
    const auto [it, inserted] = newest.try_emplace(row.url());
    if (!inserted) {
      continue;
    }
    const history::QueryURLAndVisitsResult recent =
        backend->GetMostRecentVisitsForGurl(
            row.url(), 1, history::VisitQuery404sPolicy::kInclude404s);
    it->second =
        recent.visits.empty() ? base::Time() : recent.visits[0].visit_time;
  }
  history::URLRows fresh;
  for (history::URLRow& row : rows) {
    if (row.last_visit() > newest[row.url()]) {
      fresh.push_back(std::move(row));
    }
  }
  backend->AddPagesWithDetails(fresh, source);
  // AddPagesWithDetails can drop a row, so fresh.size() can overstate what
  // landed. The surviving newest visit is asked once per URL, not per row.
  std::map<GURL, base::Time> written;
  for (const history::URLRow& row : fresh) {
    const auto [it, inserted] = written.try_emplace(row.url());
    if (!inserted) {
      continue;
    }
    const history::QueryURLAndVisitsResult recent =
        backend->GetMostRecentVisitsForGurl(
            row.url(), 1, history::VisitQuery404sPolicy::kInclude404s);
    it->second =
        recent.visits.empty() ? base::Time() : recent.visits[0].visit_time;
  }
  size_t added = 0;
  for (const history::URLRow& row : fresh) {
    if (row.last_visit() <= written[row.url()]) {
      ++added;
    }
  }
  return added;
}

}  // namespace

base::Time ChromiumVisitTime(double ms) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(ms));
}

base::Time MozillaVisitTime(double ms) {
  return base::Time::FromMillisecondsSinceUnixEpoch(ms);
}

history::URLRows HistoryRowsFromJson(std::string_view json, HistoryDb db) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return {};
  }
  return RowsFromList(parsed->GetList(), db);
}

std::optional<HistoryRead> ReadImportHistory(const base::FilePath& file,
                                             HistoryDb db,
                                             size_t max_rows) {
  bool budget_cut = false;
  // One row past the cap tells a cut file from one that fits exactly.
  const std::optional<std::string> json =
      db == HistoryDb::kChromium
          ? ReadImportRows(file, kChromiumHistoryQuery, kHistoryColumns,
                           max_rows + 1, kMaxHistoryBytes, &budget_cut)
          : ReadImportRows(file, kMozillaHistoryQuery, kHistoryColumns,
                           max_rows + 1, kMaxHistoryBytes, &budget_cut);
  if (!json) {
    return std::nullopt;
  }
  HistoryRead read;
  read.rows = HistoryRowsFromJson(*json, db);
  read.cut = budget_cut || read.rows.size() > max_rows;
  if (read.rows.size() > max_rows) {
    read.rows.resize(max_rows);
  }
  return read;
}

void AddImportedHistory(history::HistoryService* service,
                        history::URLRows rows,
                        history::VisitSource source,
                        base::OnceCallback<void(size_t)> done) {
  // The check and the write are one task on the history sequence, so a second
  // import that starts before the first one lands still sees its visits.
  service->ScheduleDBTaskForUI(base::BindOnce(
      [](history::URLRows rows, history::VisitSource source,
         scoped_refptr<base::SequencedTaskRunner> reply,
         base::OnceCallback<void(size_t)> done,
         history::HistoryBackend* backend, history::URLDatabase*) {
        const size_t added = AddNewerVisits(backend, std::move(rows), source);
        reply->PostTask(FROM_HERE, base::BindOnce(std::move(done), added));
      },
      std::move(rows), source, base::SequencedTaskRunner::GetCurrentDefault(),
      std::move(done)));
}

}  // namespace atrium
