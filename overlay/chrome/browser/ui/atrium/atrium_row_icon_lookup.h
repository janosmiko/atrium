// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROW_ICON_LOOKUP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROW_ICON_LOOKUP_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "url/gurl.h"

namespace atrium {

// One wall-clock budget for a row's whole lookup, database tiers included.
// The old machine gave every fetch its own 10 s, about 50 s worst case for a
// row with a dead /favicon.ico, a slow page and three candidates.
inline constexpr base::TimeDelta kRowIconDeadline = base::Seconds(15);

// Which tier a row is in, for the log lines.
enum class RowIconTier { kIco = 3, kLinkTag = 4 };

// The decisions of tiers 3 and 4 of a row's favicon lookup, with no I/O of
// its own. The delegate fetches, decodes and stores; this decides what to ask
// next and when the row is over.
class RowIconLookup {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // Anonymous GET of `icon_url`, answered later through OnIconFetched or
    // OnRedirectRefused. `budget` is the loader timeout, the row's remaining
    // time.
    virtual void FetchIcon(const std::string& page_url,
                           const GURL& icon_url,
                           base::TimeDelta budget) = 0;
    // Anonymous GET of `origin_root`, answered through OnPageFetched or
    // OnRedirectRefused.
    virtual void FetchPage(const std::string& page_url,
                           const GURL& origin_root,
                           base::TimeDelta budget) = 0;
    // Drops whatever fetch `page_url` has in flight. Its answer never comes.
    virtual void CancelFetch(const std::string& page_url) = 0;
    // Decodes `bytes` out of process, re-encodes, stores the row's icon, and
    // answers through OnIconStored. `icon_url` is what the database is told.
    virtual void DecodeAndStore(const std::string& page_url,
                                const GURL& icon_url,
                                std::string bytes) = 0;
    // The row is over, found or not. Frees its queue slot.
    virtual void FinishRow(const std::string& page_url) = 0;
    // A deadline that hit before any fetch went out - a slow database, not
    // a slow site - so a later backfill pass may ask this URL again.
    virtual void AllowRetry(const std::string& key, const GURL& url) {}
  };

  explicit RowIconLookup(Delegate* delegate);
  ~RowIconLookup();
  RowIconLookup(const RowIconLookup&) = delete;
  RowIconLookup& operator=(const RowIconLookup&) = delete;

  // The deadline starts here, before the database tiers the controller runs.
  void Begin(const std::string& page_url);
  // The database had nothing. Asks the site: tier 3, then tier 4.
  void AskSite(const std::string& page_url);
  // The database answered, so the row is over without a fetch.
  void EndEarly(const std::string& page_url);

  // Answers from the delegate.
  void OnIconFetched(const std::string& page_url,
                     const GURL& icon_url,
                     int net_error,
                     std::optional<std::string> body);
  void OnRedirectRefused(const std::string& page_url);
  void OnPageFetched(const std::string& page_url,
                     const GURL& final_url,
                     std::string_view mime_type,
                     std::optional<std::string> body);
  void OnIconStored(const std::string& page_url, bool ok);

  // Ends every row without starting anything, for a controller teardown.
  std::vector<std::string> AbandonAll();

  RowIconTier TierOf(const std::string& page_url) const;
  size_t rows_in_flight() const { return rows_.size(); }

 private:
  struct Row {
    base::TimeTicks deadline;
    RowIconTier tier = RowIconTier::kIco;
    // Present and empty once the page fetch started: a failure there ends
    // the row instead of restarting it.
    std::optional<std::vector<GURL>> candidates;
    // Whether the site was ever actually asked, for the retry decision below.
    bool fetch_issued = false;
    base::OneShotTimer timer;
  };
  base::TimeDelta Remaining(const Row& row) const;
  void FetchIconAt(const std::string& page_url, Row& row, const GURL& icon_url);
  void OnAttemptFailed(const std::string& page_url);
  void StartPageFetch(const std::string& page_url, Row& row);
  void TryNextCandidate(const std::string& page_url, Row& row);
  void OnDeadline(const std::string& page_url);
  void Finish(const std::string& page_url);
  // Finish, then AllowRetry when a fetch never went out. Shared by
  // OnDeadline and the two out-of-budget early finishes.
  void FinishAllowingRetry(const std::string& page_url, bool fetch_issued);

  const raw_ptr<Delegate> delegate_;
  std::map<std::string, Row> rows_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROW_ICON_LOOKUP_H_
