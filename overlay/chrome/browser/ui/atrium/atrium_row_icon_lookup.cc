// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_row_icon_lookup.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "net/base/data_url.h"
#include "net/base/net_errors.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/url_constants.h"

namespace atrium {

// Below this, a fetch would be cancelled by the deadline almost as soon as
// it starts - the row finishes instead of sending one.
constexpr base::TimeDelta kMinRowIconFetch = base::Seconds(1);

RowIconLookup::RowIconLookup(Delegate* delegate) : delegate_(delegate) {}

RowIconLookup::~RowIconLookup() = default;

base::TimeDelta RowIconLookup::Remaining(const Row& row) const {
  // Can be under kMinRowIconFetch, even negative - callers check that.
  return row.deadline - base::TimeTicks::Now();
}

void RowIconLookup::Begin(const std::string& page_url) {
  Row& row = rows_[page_url];
  // Reset rather than trust a stale entry: a favourite can be re-admitted
  // to the backfill queue before its previous row ever finished.
  row.timer.Stop();
  row.tier = RowIconTier::kIco;
  row.candidates.reset();
  row.fetch_issued = false;
  row.deadline = base::TimeTicks::Now() + kRowIconDeadline;
  row.timer.Start(FROM_HERE, Remaining(row),
                  base::BindOnce(&RowIconLookup::OnDeadline,
                                 base::Unretained(this), page_url));
}

void RowIconLookup::AskSite(const std::string& page_url) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  const std::string icon_url = DefaultFaviconURLFor(page_url);
  if (icon_url.empty()) {
    VLOG(1) << "Atrium: favicon tier 3 has no site to ask for "
            << LogSafeUrl(GURL(page_url));
    Finish(page_url);
    return;
  }
  VLOG(1) << "Atrium: favicon tier 3 fetching " << icon_url;
  FetchIconAt(page_url, it->second, GURL(icon_url));
}

void RowIconLookup::EndEarly(const std::string& page_url) {
  if (rows_.count(page_url)) {
    Finish(page_url);
  }
}

void RowIconLookup::FetchIconAt(const std::string& page_url,
                                Row& row,
                                const GURL& icon_url) {
  if (icon_url.SchemeIs(url::kDataScheme)) {
    // Inline. The bytes still came from the page, so they take the same
    // limits as a fetched icon before the delegate decodes them.
    constexpr size_t kMaxDataIconBytes = 64 * 1024;
    std::string mime_type, charset, data;
    if (!net::DataURL::Parse(icon_url, &mime_type, &charset, &data) ||
        data.empty() || data.size() > kMaxDataIconBytes ||
        !base::StartsWith(mime_type, "image/",
                          base::CompareCase::INSENSITIVE_ASCII)) {
      VLOG(1) << "Atrium: favicon data: icon unreadable for "
              << LogSafeUrl(GURL(page_url));
      OnAttemptFailed(page_url);
      return;
    }
    row.fetch_issued = true;
    delegate_->DecodeAndStore(page_url, icon_url, std::move(data));
    return;
  }
  const base::TimeDelta budget = Remaining(row);
  if (budget < kMinRowIconFetch) {
    FinishAllowingRetry(page_url, row.fetch_issued);
    return;
  }
  row.fetch_issued = true;
  delegate_->FetchIcon(page_url, icon_url, budget);
}

void RowIconLookup::OnAttemptFailed(const std::string& page_url) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  if (!it->second.candidates) {
    StartPageFetch(page_url, it->second);
    return;
  }
  TryNextCandidate(page_url, it->second);
}

void RowIconLookup::StartPageFetch(const std::string& page_url, Row& row) {
  // Fourth tier. Nothing at /favicon.ico, so the site's origin root is asked
  // which icon it names - not the saved page, so an unauthenticated GET to a
  // saved deep path on an intranet app is never a side effect of this.
  const GURL origin_root = GURL(page_url).GetWithEmptyPath();
  if (!origin_root.is_valid() || !origin_root.SchemeIsHTTPOrHTTPS()) {
    Finish(page_url);
    return;
  }
  const base::TimeDelta budget = Remaining(row);
  if (budget < kMinRowIconFetch) {
    FinishAllowingRetry(page_url, row.fetch_issued);
    return;
  }
  VLOG(1) << "Atrium: favicon tier 4 fetching the origin root for "
          << LogSafeUrl(origin_root);
  // An empty list marks the row as past tier 3, so a failure here ends it.
  row.candidates = std::vector<GURL>{};
  row.tier = RowIconTier::kLinkTag;
  row.fetch_issued = true;
  delegate_->FetchPage(page_url, origin_root, budget);
}

void RowIconLookup::TryNextCandidate(const std::string& page_url, Row& row) {
  if (!row.candidates || row.candidates->empty()) {
    Finish(page_url);
    return;
  }
  const GURL next = row.candidates->front();
  row.candidates->erase(row.candidates->begin());
  VLOG(1) << "Atrium: favicon tier 4 fetching " << LogSafeUrl(next) << " for "
          << LogSafeUrl(GURL(page_url));
  FetchIconAt(page_url, row, next);
}

void RowIconLookup::OnIconFetched(const std::string& page_url,
                                  const GURL& icon_url,
                                  int net_error,
                                  std::optional<std::string> body) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  Row& row = it->second;
  // Only OnDeadline reads this, and only when this row is still tracked -
  // an answer means a fetch clearly went out, however it got here.
  row.fetch_issued = true;
  if (net_error != net::OK || !body || body->empty()) {
    const bool in_link_tag_tier = row.tier == RowIconTier::kLinkTag;
    VLOG(1) << "Atrium: favicon tier " << (in_link_tag_tier ? 4 : 3)
            << " fetch failed for " << LogSafeUrl(GURL(page_url))
            << " net_error=" << net_error
            << " bytes=" << (body ? body->size() : 0);
    // A dead host would not answer the page either, but a tier 4 candidate's
    // own network error just tries the next one.
    if (!IcoFailureReachesLinkTagTier(net_error, in_link_tag_tier)) {
      Finish(page_url);
      return;
    }
    OnAttemptFailed(page_url);
    return;
  }
  delegate_->DecodeAndStore(page_url, icon_url, std::move(*body));
}

void RowIconLookup::OnRedirectRefused(const std::string& page_url) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  const bool in_link_tag_tier = it->second.tier == RowIconTier::kLinkTag;
  VLOG(1) << "Atrium: favicon tier " << (in_link_tag_tier ? 4 : 3)
          << " refused a redirect for " << LogSafeUrl(GURL(page_url));
  OnAttemptFailed(page_url);
}

void RowIconLookup::OnPageFetched(const std::string& page_url,
                                  const GURL& final_url,
                                  std::string_view mime_type,
                                  std::optional<std::string> body) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  if (!body || body->empty() ||
      !PageMayNameIcons(GURL(page_url), final_url, mime_type)) {
    // The reason is a fixed word, not mime_type: that string is the remote
    // host's own, and this line must not echo it back into the log.
    const bool wrong_type = !mime_type.empty() && mime_type != "text/html" &&
                            mime_type != "application/xhtml+xml";
    VLOG(1) << "Atrium: favicon tier 4 page unusable for "
            << LogSafeUrl(GURL(page_url)) << " final=" << LogSafeUrl(final_url)
            << " reason=" << (wrong_type ? "type" : "domain")
            << " bytes=" << (body ? body->size() : 0);
    Finish(page_url);
    return;
  }
  // Rust, not C++: the bytes are a remote host's, and the browser process is
  // the last place to hand them to a hand-written scanner.
  char* raw = atrium_core_icon_links(body->data(), body->size());
  const std::string links_json = raw ? raw : "";
  atrium_core_string_free(raw);
  // Already capped at kMaxIconCandidates inside IconCandidatesFromLinks.
  std::vector<GURL> candidates = IconCandidatesFromLinks(links_json, final_url);
  VLOG(1) << "Atrium: favicon tier 4 found " << candidates.size()
          << " candidate(s) for " << LogSafeUrl(GURL(page_url));
  it->second.candidates = std::move(candidates);
  TryNextCandidate(page_url, it->second);
}

void RowIconLookup::OnIconStored(const std::string& page_url, bool ok) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  if (ok) {
    Finish(page_url);
    return;
  }
  OnAttemptFailed(page_url);
}

void RowIconLookup::OnDeadline(const std::string& page_url) {
  auto it = rows_.find(page_url);
  if (it == rows_.end()) {
    return;
  }
  const bool fetch_issued = it->second.fetch_issued;
  VLOG(1) << "Atrium: favicon row deadline for " << LogSafeUrl(GURL(page_url));
  delegate_->CancelFetch(page_url);
  FinishAllowingRetry(page_url, fetch_issued);
}

void RowIconLookup::Finish(const std::string& page_url) {
  rows_.erase(page_url);
  delegate_->FinishRow(page_url);
}

void RowIconLookup::FinishAllowingRetry(const std::string& page_url,
                                        bool fetch_issued) {
  Finish(page_url);
  if (!fetch_issued) {
    // A slow database, not a slow site: nothing about this URL was learned,
    // so a later backfill pass gets to ask it again.
    delegate_->AllowRetry(page_url, GURL(page_url));
  }
}

std::vector<std::string> RowIconLookup::AbandonAll() {
  std::vector<std::string> urls;
  urls.reserve(rows_.size());
  for (const auto& [url, row] : rows_) {
    urls.push_back(url);
  }
  rows_.clear();
  // After rows_.clear(): a re-entrant delegate must not find its own row
  // still tracked, or invalidate this loop's iterator.
  for (const std::string& url : urls) {
    delegate_->CancelFetch(url);
  }
  return urls;
}

RowIconTier RowIconLookup::TierOf(const std::string& page_url) const {
  auto it = rows_.find(page_url);
  return it != rows_.end() ? it->second.tier : RowIconTier::kIco;
}

}  // namespace atrium
