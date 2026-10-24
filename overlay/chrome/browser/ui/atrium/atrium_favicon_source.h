// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVICON_SOURCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVICON_SOURCE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/containers/lru_cache.h"
#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/ui/atrium/atrium_icon_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"

namespace content {
class BrowserContext;
}

namespace atrium {

class IconScopeHolder;

// The token a request path must carry to reach `context`'s icon caches. Every
// chrome surface renders in ONE shared context, so nothing else about a
// request says which profile is asking.
const std::string& IconScopeFor(content::BrowserContext* context);

// A second token that reaches only `context`'s extension icons. It rides on
// every extension action push, which also reaches surfaces of other profiles,
// so it must not open the tab or url icons that IconScopeFor guards.
const std::string& ExtIconScopeFor(content::BrowserContext* context);

// Holds tab favicon bytes for the HTML sidebar.
//
// A cache only. The bytes are served by the atrium.top-chrome WebUI's own
// request filter (atrium_ui.cc); there was a URLDataSource on a host of its
// own here, and every request to it failed before the source ever ran.
//
// Same reasoning as IconCache below: the HTML sidebar must not fetch icons
// itself. A request issued from the chrome context would run on that profile
// and contaminate its cache and cookies, so the browser serves bytes it
// already holds - TabEventForwarder::PushFavicon encodes this exact PNG for
// the Swift arm and would otherwise drop it.
//
// Bounded. Tab ids are minted forever, so an unbounded map here
// is a leak that grows for the life of the process. Least-recently-used
// entries are evicted past kMaxEntries. A miss is a 404 and the row falls
// back to its tint.
class FaviconCache {
 public:
  // This context's own icons, created on first use and destroyed with it.
  static FaviconCache& For(content::BrowserContext* context);

  // Null for a token no live context owns. Tab ids are a small per-process
  // counter, so an unscoped path could be walked to read the tabs of every
  // profile in the window list.
  static FaviconCache* ForScope(const std::string& scope);

  FaviconCache(const FaviconCache&) = delete;
  FaviconCache& operator=(const FaviconCache&) = delete;

  void Store(uint64_t tab_id, std::vector<uint8_t> png);
  const std::vector<uint8_t>* Find(uint64_t tab_id) const;

 private:
  friend class IconScopeHolder;

  FaviconCache();
  ~FaviconCache();

  // Generous enough that a realistic session never evicts a visible tab, small
  // enough to bound the memory at roughly a few MB of PNG.
  static constexpr size_t kMaxEntries = 256;

  base::HashingLRUCache<uint64_t, std::vector<uint8_t>> icons_{kMaxEntries};
};

// One LRU, one Store/Find, for both url and extension icons - UrlFor/ExtFor
// say which IconScopeHolder member a call reaches.
class IconCache {
 public:
  // This context's own url icons, created on first use and destroyed with it.
  static IconCache& UrlFor(content::BrowserContext* context);

  // Null for a token no live context owns - a made-up one, or a profile that
  // has gone.
  static IconCache* UrlForScope(const std::string& scope);

  // This context's own extension icons, created on first use and destroyed
  // with it.
  static IconCache& ExtFor(content::BrowserContext* context);

  // Null for a token no live context owns.
  static IconCache* ExtForScope(const std::string& scope);

  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;

  void Store(const std::string& key, std::vector<uint8_t> png);

  // Null when unknown - the source answers 404 and the caller keeps whatever
  // it showed before any icon resolved.
  const std::vector<uint8_t>* Find(const std::string& key) const;

  // Keyed by page URL, not host: docs.google.com serves a different icon per
  // path. Callers compare their row count to this (see Admit).
  static constexpr size_t kMaxEntries = 1024;

 private:
  friend class IconScopeHolder;

  IconCache();
  ~IconCache();

  // Bytes only - which URLs have been asked for is IconBackfillQueue's
  //. Mutable because a read refreshes recency.
  mutable base::HashingLRUCache<std::string, std::vector<uint8_t>> icons_{
      kMaxEntries};
};

// The bytes for `path`, or null - which the WebUI data source turns into a
// 404, leaving the row's tint chip showing rather than a broken image.
scoped_refptr<base::RefCountedMemory> ServeIcon(const std::string& path);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVICON_SOURCE_H_
