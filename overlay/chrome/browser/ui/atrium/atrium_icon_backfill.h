// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_BACKFILL_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_BACKFILL_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/lru_cache.h"
#include "base/functional/function_ref.h"
#include "base/values.h"
#include "url/gurl.h"

namespace atrium {

// Which sidebar rows still need an icon, and how many lookups may run at once
//. Nothing above //base, //url and //net, the same posture as
// BoundedIconStore: the bound and the memo fail silently, so a unit test
// asserts them rather than a reader.

// Every row URL in `model` for which `has_icon` answers false, in render order
// and deduplicated. A favourite has no id of its own, so its `id` IS the page
// URL, while a tab or pinned row is keyed by its `url`.

// Walks the row lists ForEachRowList walks in atrium_sidebar_tabs.cc, plus
// `favorites`. A list added there has to be added here too, or its rows come
// up cold and stay cold.
//
// Only the rows of `profile_id`'s spaces, since a rail pane reads its own
// profile's cache. The top-level rows are the active space's. A space with no
// profile id, or a model with no spaces, belongs to `unowned_id`.
std::vector<std::string> SelectMissingIconURLs(
    const base::DictValue& model,
    const std::string& profile_id,
    const std::string& unowned_id,
    base::FunctionRef<bool(const std::string&)> has_icon);

// The origin's own /favicon.ico - where to ask the site once this profile's
// database has answered nothing.

// Empty for anything but http and https: a chrome://, file:// or data: row has
// nobody to ask, and a request on its behalf is one the user never caused.
// Credentials in the URL are dropped, since this fetch is anonymous.
std::string DefaultFaviconURLFor(const std::string& page_url);

// Whether the /favicon.ico fetch may follow a 30x to `target`. Many sites
// answer /favicon.ico with a redirect (same host under /owa/, a CDN, a renamed
// domain), so refusing every redirect left their rows blank for good.

// Stops a scheme other than http or https, localhost, and an IP literal that
// is not publicly routable. A DNS name that resolves to a private address
// still passes - accepted, out of scope.
bool RedirectTargetAllowedForIcoFetch(const GURL& target);

// How many of a page's icon links a row tries before it gives up.
inline constexpr size_t kMaxIconCandidates = 3;

// Whether the anonymous page fetch answered with something whose icon links
// belong to the row. A final URL on another registrable domain is a login
// provider, not the site, and a non-HTML answer names nothing.
bool PageMayNameIcons(const GURL& page_url,
                      const GURL& final_url,
                      std::string_view mime_type);

// Whether an ico-tier failure moves on to the page or the next link tag
// candidate, rather than ending the row. A dead host would not answer the
// page either, so a network error still ends the row before tier 4 exists.
bool IcoFailureReachesLinkTagTier(int net_error, bool in_link_tag_tier);

// The icon URLs to try, from atrium_core_icon_links's JSON. Each href
// resolves against `base` when present, else the page URL.
std::vector<GURL> IconCandidatesFromLinks(const std::string& links_json,
                                          const GURL& page_url);

// How many URLs the admitted and found memos each keep. Past it the least
// recently used one is forgotten, so it may be asked for once more.
inline constexpr size_t kMaxRemembered = 4096;

// Admits each URL once and lets at most `max_in_flight` of them run at a time.
class IconBackfillQueue {
 public:
  explicit IconBackfillQueue(size_t max_in_flight);
  ~IconBackfillQueue();

  IconBackfillQueue(const IconBackfillQueue&) = delete;
  IconBackfillQueue& operator=(const IconBackfillQueue&) = delete;

  // The subset of `urls` to start now. A URL whose lookup found nothing is
  // dropped, so an iconless site is not asked on every push. A URL marked
  // found is taken again when `allow_refetch`: callers offer only URLs the
  // icon cache lacks, and pass false when the rows outnumber the cache, where
  // each refetch would evict another visible row.
  std::vector<std::string> Admit(const std::vector<std::string>& urls,
                                 bool allow_refetch = true);

  void MarkFound(const std::string& url);

  // One lookup answered, found or not. Returns the URLs the freed slot lets
  // through.
  std::vector<std::string> Finish(const std::string& url);

  // Lets one more lookup through for a page just visited: the first ask ran
  // before the visit committed, when the database had nothing.
  // A URL whose lookup has not answered yet is ignored - that one IS the retry.
  void AllowRetry(const std::string& url);

  // Frees the slots these URLs hold without starting anything, for a caller
  // whose own lookups were cancelled and will never call Finish. The memo is
  // kept: they were asked, and a teardown is not a reason to ask again.
  void Release(const std::vector<std::string>& urls);

  size_t in_flight() const { return running_.size(); }
  size_t waiting() const { return waiting_.size(); }
  size_t admitted_size() const { return admitted_.size(); }
  size_t found_size() const { return found_.size(); }

 private:
  std::vector<std::string> StartWhatFits();

  const size_t max_in_flight_;
  // Every URL ever admitted. `queued_` and `running_` are the two subsets that
  // have not answered yet, which is what AllowRetry has to leave alone.
  base::HashingLRUCacheSet<std::string> admitted_{kMaxRemembered};
  base::HashingLRUCacheSet<std::string> found_{kMaxRemembered};
  std::set<std::string> queued_;
  std::set<std::string> running_;
  std::deque<std::string> waiting_;
};

// The one queue every window of a profile shares.

// Per profile, not per window, because the cache it fills is process wide:
// five windows on one profile each fetched the same cold row separately.

// `profile_key` is an identity token, never a pointer to dereference, which
// keeps BrowserContext out of this file. An off-the-record profile is a
// different context, so it partitions here on its own.
IconBackfillQueue& IconBackfillQueueFor(uintptr_t profile_key);

// The profile's queue, or null once it is forgotten. Never creates one, so a
// late call from a dead profile does not leave an entry behind.
IconBackfillQueue* FindIconBackfillQueue(uintptr_t profile_key);

// Drops the profile's queue when the profile is destroyed.
void ForgetIconBackfillQueue(uintptr_t profile_key);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_BACKFILL_H_
