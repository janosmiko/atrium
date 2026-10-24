// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_favicon_source.h"

#include <map>
#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/string_number_conversions.h"
#include "base/unguessable_token.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "url/gurl.h"

namespace atrium {

namespace {

const void* const kIconScopeKey = &kIconScopeKey;

// Every live context's caches, so a served path can find the ones its token
// names. The holder below adds and removes its own entry, so a profile that
// goes takes its icons with it.
std::map<std::string, IconScopeHolder*>& ScopeRegistry() {
  static base::NoDestructor<std::map<std::string, IconScopeHolder*>> registry;
  return *registry;
}

std::map<std::string, IconScopeHolder*>& ExtScopeRegistry() {
  static base::NoDestructor<std::map<std::string, IconScopeHolder*>> registry;
  return *registry;
}

}  // namespace

// Owned by the BrowserContext, which is what ties an icon's lifetime to the
// profile that fetched it.
class IconScopeHolder : public base::SupportsUserData::Data {
 public:
  explicit IconScopeHolder(uintptr_t backfill_key)
      : backfill_key_(backfill_key),
        scope_(base::UnguessableToken::Create().ToString()),
        ext_scope_(base::UnguessableToken::Create().ToString()) {
    ScopeRegistry()[scope_] = this;
    ExtScopeRegistry()[ext_scope_] = this;
  }

  ~IconScopeHolder() override {
    ForgetIconBackfillQueue(backfill_key_);
    ScopeRegistry().erase(scope_);
    ExtScopeRegistry().erase(ext_scope_);
  }

  const std::string& scope() const { return scope_; }
  const std::string& ext_scope() const { return ext_scope_; }
  FaviconCache& tab_icons() { return tab_icons_; }
  IconCache& url_icons() { return url_icons_; }
  IconCache& ext_icons() { return ext_icons_; }

 private:
  const uintptr_t backfill_key_;
  const std::string scope_;
  const std::string ext_scope_;
  FaviconCache tab_icons_;
  IconCache url_icons_;
  IconCache ext_icons_;
};

namespace {

IconScopeHolder& HolderFor(content::BrowserContext* context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(context);
  auto* holder =
      static_cast<IconScopeHolder*>(context->GetUserData(kIconScopeKey));
  if (!holder) {
    auto owned = std::make_unique<IconScopeHolder>(
        reinterpret_cast<uintptr_t>(context));
    holder = owned.get();
    context->SetUserData(kIconScopeKey, std::move(owned));
  }
  return *holder;
}

IconScopeHolder* HolderForScope(const std::string& scope) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto it = ScopeRegistry().find(scope);
  return it == ScopeRegistry().end() ? nullptr : it->second;
}

IconScopeHolder* HolderForExtScope(const std::string& scope) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto it = ExtScopeRegistry().find(scope);
  return it == ExtScopeRegistry().end() ? nullptr : it->second;
}

}  // namespace

const std::string& IconScopeFor(content::BrowserContext* context) {
  if (!context) {
    static base::NoDestructor<std::string> none;
    return *none;
  }
  return HolderFor(context).scope();
}

const std::string& ExtIconScopeFor(content::BrowserContext* context) {
  if (!context) {
    static base::NoDestructor<std::string> none;
    return *none;
  }
  return HolderFor(context).ext_scope();
}

scoped_refptr<base::RefCountedMemory> ServeIcon(const std::string& path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const IconRequest request = ParseIconPath(path);
  const std::vector<uint8_t>* png = nullptr;
  switch (request.kind) {
    case IconKind::kFavicon: {
      uint64_t tab_id = 0;
      // The renderer supplies the id from the DOM, so it is not trusted. A
      // non-numeric one simply finds nothing (the placeholder folder's
      // stand-in row lands here, by design).
      FaviconCache* cache = FaviconCache::ForScope(request.scope);
      png = cache && base::StringToUint64(request.id, &tab_id)
                ? cache->Find(tab_id)
                : nullptr;
      break;
    }
    case IconKind::kExtension: {
      IconCache* cache = IconCache::ExtForScope(request.scope);
      png = cache ? cache->Find(request.id) : nullptr;
      break;
    }
    case IconKind::kUrlIcon: {
      // A path naming no scope, or one whose profile has gone, reaches no
      // cache at all rather than falling back to some other profile's.
      IconCache* cache = IconCache::UrlForScope(request.scope);
      png = cache ? cache->Find(request.id) : nullptr;
      break;
    }
    case IconKind::kAppIcon:
      // Files, not bytes the browser holds, so they cannot be answered
      // synchronously. The request filter routes them to ServeAppIcon before
      // reaching here.
      break;
    case IconKind::kNone:
      break;
  }
  if (!png) {
    return nullptr;
  }
  return base::MakeRefCounted<base::RefCountedBytes>(*png);
}

FaviconCache::FaviconCache() = default;
FaviconCache::~FaviconCache() = default;

// static
FaviconCache& FaviconCache::For(content::BrowserContext* context) {
  return HolderFor(context).tab_icons();
}

// static
FaviconCache* FaviconCache::ForScope(const std::string& scope) {
  IconScopeHolder* holder = HolderForScope(scope);
  return holder ? &holder->tab_icons() : nullptr;
}

void FaviconCache::Store(uint64_t tab_id, std::vector<uint8_t> png) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (png.empty()) {
    // An empty answer must not evict a tab that already has a real icon.
    return;
  }
  icons_.Put(tab_id, std::move(png));
}

const std::vector<uint8_t>* FaviconCache::Find(uint64_t tab_id) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = icons_.Peek(tab_id);
  return it == icons_.end() ? nullptr : &it->second;
}

IconCache::IconCache() = default;
IconCache::~IconCache() = default;

// static
IconCache& IconCache::UrlFor(content::BrowserContext* context) {
  return HolderFor(context).url_icons();
}

// static
IconCache* IconCache::UrlForScope(const std::string& scope) {
  IconScopeHolder* holder = HolderForScope(scope);
  return holder ? &holder->url_icons() : nullptr;
}

// static
IconCache& IconCache::ExtFor(content::BrowserContext* context) {
  return HolderFor(context).ext_icons();
}

// static
IconCache* IconCache::ExtForScope(const std::string& scope) {
  IconScopeHolder* holder = HolderForExtScope(scope);
  return holder ? &holder->ext_icons() : nullptr;
}

void IconCache::Store(const std::string& key, std::vector<uint8_t> png) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (key.empty() || png.empty()) {
    // An empty answer must not replace a real icon.
    return;
  }
  icons_.Put(key, std::move(png));
}

const std::vector<uint8_t>* IconCache::Find(const std::string& key) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = icons_.Get(key);
  return it == icons_.end() ? nullptr : &it->second;
}

}  // namespace atrium
