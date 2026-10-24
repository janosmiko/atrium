// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_favicon_source.h"

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::vector<uint8_t> Icon() {
  return {1, 2, 3};
}

std::vector<uint8_t> Bytes(uint8_t marker) {
  return std::vector<uint8_t>{marker, marker, marker};
}

constexpr char kPage[] = "https://example.test/";
constexpr char kEncodedPage[] = "https%3A%2F%2Fexample.test%2F";

std::string IconPath(const std::string& scope) {
  return "urlicon/" + scope + "/" + kEncodedPage + ".png";
}

std::string TabIconPath(const std::string& scope) {
  return "favicon/" + scope + "/77.png";
}

class IconCacheTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
  content::TestBrowserContext regular_;
  content::TestBrowserContext incognito_;
};

// The whole point of the type: a window renders rows for one profile, and the
// icons another profile fetched are not its to show.
TEST_F(IconCacheTest, OneContextNeverReadsAnothersIcon) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());

  ASSERT_TRUE(IconCache::UrlFor(&regular_).Find(kPage));
  EXPECT_EQ(Icon(), *IconCache::UrlFor(&regular_).Find(kPage));
  EXPECT_FALSE(IconCache::UrlFor(&incognito_).Find(kPage));
}

TEST_F(IconCacheTest, AContextKeepsOneCacheAcrossCalls) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());
  IconCache::UrlFor(&regular_).Store("https://other.test/", {9, 9});

  ASSERT_TRUE(IconCache::UrlFor(&regular_).Find("https://other.test/"));
  EXPECT_EQ(Icon(), *IconCache::UrlFor(&regular_).Find(kPage));
}

TEST_F(IconCacheTest, EachContextGetsItsOwnScopeToken) {
  const std::string regular = IconScopeFor(&regular_);
  const std::string incognito = IconScopeFor(&incognito_);

  EXPECT_FALSE(regular.empty());
  EXPECT_NE(regular, incognito);
  EXPECT_EQ(regular, IconScopeFor(&regular_));
}

TEST_F(IconCacheTest, AScopeReachesOnlyTheContextThatMintedIt) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());

  IconCache* from_regular =
      IconCache::UrlForScope(IconScopeFor(&regular_));
  IconCache* from_incognito =
      IconCache::UrlForScope(IconScopeFor(&incognito_));
  ASSERT_TRUE(from_regular);
  ASSERT_TRUE(from_incognito);
  EXPECT_EQ(Icon(), *from_regular->Find(kPage));
  EXPECT_FALSE(from_incognito->Find(kPage));
}

// A path naming no scope, or one that was never minted, must reach no cache
// rather than fall back to whichever profile happens to hold the URL.
TEST_F(IconCacheTest, AnUnknownScopeReachesNoCache) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());

  EXPECT_FALSE(IconCache::UrlForScope(std::string()));
  EXPECT_FALSE(IconCache::UrlForScope("made-up"));
}

TEST_F(IconCacheTest, AContextTakesItsIconsWithItWhenItGoes) {
  auto scoped = std::make_unique<content::TestBrowserContext>();
  const std::string scope = IconScopeFor(scoped.get());
  IconCache::UrlFor(scoped.get()).Store(kPage, Icon());
  ASSERT_TRUE(IconCache::UrlForScope(scope));

  scoped.reset();

  EXPECT_FALSE(IconCache::UrlForScope(scope));
}

TEST_F(IconCacheTest, AnEmptyAnswerDoesNotReplaceARealIcon) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());
  IconCache::UrlFor(&regular_).Store(kPage, {});

  ASSERT_TRUE(IconCache::UrlFor(&regular_).Find(kPage));
  EXPECT_EQ(Icon(), *IconCache::UrlFor(&regular_).Find(kPage));
}

// The read path the WebUI request filter actually runs. The cache test above
// cannot catch a ServeIcon that ignores the scope it was handed.
TEST_F(IconCacheTest, ServeIconAnswersOnlyTheScopeItsPathNames) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());

  scoped_refptr<base::RefCountedMemory> served =
      ServeIcon(IconPath(IconScopeFor(&regular_)));
  ASSERT_TRUE(served);
  EXPECT_EQ(Icon(), std::vector<uint8_t>(served->begin(), served->end()));

  EXPECT_FALSE(ServeIcon(IconPath(IconScopeFor(&incognito_))));
  EXPECT_FALSE(ServeIcon(IconPath("made-up")));
  EXPECT_FALSE(ServeIcon(std::string("urlicon/") + kEncodedPage + ".png"));
}

// Tab ids are a small per-process counter shared by every profile, so an
// unscoped path could be walked to read the open tabs of all of them.
TEST_F(IconCacheTest, OneContextNeverReadsAnothersTabIcon) {
  FaviconCache::For(&regular_).Store(77, Icon());

  ASSERT_TRUE(FaviconCache::For(&regular_).Find(77));
  EXPECT_EQ(Icon(), *FaviconCache::For(&regular_).Find(77));
  EXPECT_FALSE(FaviconCache::For(&incognito_).Find(77));
}

// THE point of the type: tab ids are minted forever, so eviction past
// capacity is what stops the cache growing for the life of the process.
TEST_F(IconCacheTest, TabIconIsEvictedPastCapacity) {
  constexpr uint64_t kCapacity = 256;  // FaviconCache::kMaxEntries
  for (uint64_t id = 0; id < kCapacity + 1; ++id) {
    FaviconCache::For(&regular_).Store(id, Bytes(static_cast<uint8_t>(id)));
  }
  EXPECT_FALSE(FaviconCache::For(&regular_).Find(0))
      << "the oldest should have gone";
  EXPECT_TRUE(FaviconCache::For(&regular_).Find(kCapacity));
}

TEST_F(IconCacheTest, UrlIconIsEvictedPastCapacity) {
  constexpr int kCapacity = 1024;  // IconCache::kMaxEntries
  IconCache& cache = IconCache::UrlFor(&regular_);
  for (int i = 0; i < kCapacity + 10; ++i) {
    cache.Store("https://site.test/" + base::NumberToString(i),
                Bytes(static_cast<uint8_t>(i)));
  }
  EXPECT_FALSE(cache.Find("https://site.test/0"));
  EXPECT_FALSE(cache.Find("https://site.test/9"));
  EXPECT_TRUE(cache.Find("https://site.test/10"));
  EXPECT_TRUE(cache.Find("https://site.test/" +
                         base::NumberToString(kCapacity + 9)));
}

TEST_F(IconCacheTest, ReadingAUrlIconKeepsItFromEviction) {
  constexpr int kCapacity = 1024;  // IconCache::kMaxEntries
  IconCache& cache = IconCache::UrlFor(&regular_);
  for (int i = 0; i < kCapacity; ++i) {
    cache.Store("https://site.test/" + base::NumberToString(i), Icon());
  }
  ASSERT_TRUE(cache.Find("https://site.test/0"));
  cache.Store("https://site.test/new", Icon());

  EXPECT_TRUE(cache.Find("https://site.test/0"));
  EXPECT_FALSE(cache.Find("https://site.test/1"));
}

TEST_F(IconCacheTest, ReStoringATabIconReplacesTheBytes) {
  FaviconCache::For(&regular_).Store(77, Icon());
  FaviconCache::For(&regular_).Store(77, Bytes(0x99));

  const std::vector<uint8_t>* found = FaviconCache::For(&regular_).Find(77);
  ASSERT_TRUE(found);
  EXPECT_EQ(Bytes(0x99), *found)
      << "a navigated tab must not show a stale icon";
}

// A tab with no icon must not evict one that has an icon.
TEST_F(IconCacheTest, EmptyTabIconBytesAreIgnored) {
  FaviconCache::For(&regular_).Store(1, Icon());
  FaviconCache::For(&regular_).Store(2, {});

  EXPECT_FALSE(FaviconCache::For(&regular_).Find(2));
  EXPECT_TRUE(FaviconCache::For(&regular_).Find(1))
      << "the real icon should have survived";
}

TEST_F(IconCacheTest, ServeIconAnswersATabIconOnlyForItsOwnScope) {
  FaviconCache::For(&regular_).Store(77, Icon());

  scoped_refptr<base::RefCountedMemory> served =
      ServeIcon(TabIconPath(IconScopeFor(&regular_)));
  ASSERT_TRUE(served);
  EXPECT_EQ(Icon(), std::vector<uint8_t>(served->begin(), served->end()));

  EXPECT_FALSE(ServeIcon(TabIconPath(IconScopeFor(&incognito_))));
  EXPECT_FALSE(ServeIcon(TabIconPath("made-up")));
  EXPECT_FALSE(ServeIcon("favicon/77.png"));
}

// Both caches live on one holder, so a surface is handed a single token.
TEST_F(IconCacheTest, TheTwoCachesShareOneTokenPerContext) {
  IconCache::UrlFor(&regular_).Store(kPage, Icon());
  FaviconCache::For(&regular_).Store(77, Icon());

  const std::string scope = IconScopeFor(&regular_);
  ASSERT_TRUE(IconCache::UrlForScope(scope));
  ASSERT_TRUE(FaviconCache::ForScope(scope));
  EXPECT_TRUE(IconCache::UrlForScope(scope)->Find(kPage));
  EXPECT_TRUE(FaviconCache::ForScope(scope)->Find(77));
}

constexpr char kExtensionId[] = "abcdefghijklmnopabcdefghijklmnop";

std::string ExtIconPath(const std::string& scope, const std::string& id) {
  return "exticon/" + scope + "/" + id + ".png";
}

// The whole point of the type: an extension id one profile installed and
// cached is not another profile's to read, even when the other profile
// already knows the id.
TEST_F(IconCacheTest, OneContextNeverReadsAnothersExtensionIcon) {
  IconCache::ExtFor(&regular_).Store(kExtensionId, Icon());

  ASSERT_TRUE(IconCache::ExtFor(&regular_).Find(kExtensionId));
  EXPECT_EQ(Icon(), *IconCache::ExtFor(&regular_).Find(kExtensionId));
  EXPECT_FALSE(IconCache::ExtFor(&incognito_).Find(kExtensionId));
}

TEST_F(IconCacheTest, AnExtensionScopeReachesOnlyTheContextThatMintedIt) {
  IconCache::ExtFor(&regular_).Store(kExtensionId, Icon());

  IconCache* from_regular =
      IconCache::ExtForScope(ExtIconScopeFor(&regular_));
  IconCache* from_incognito =
      IconCache::ExtForScope(ExtIconScopeFor(&incognito_));
  ASSERT_TRUE(from_regular);
  ASSERT_TRUE(from_incognito);
  EXPECT_EQ(Icon(), *from_regular->Find(kExtensionId));
  EXPECT_FALSE(from_incognito->Find(kExtensionId));
}

TEST_F(IconCacheTest, AContextTakesItsExtensionIconsWithItWhenItGoes) {
  auto scoped = std::make_unique<content::TestBrowserContext>();
  const std::string scope = ExtIconScopeFor(scoped.get());
  IconCache::ExtFor(scoped.get()).Store(kExtensionId, Icon());
  ASSERT_TRUE(IconCache::ExtForScope(scope));

  scoped.reset();

  EXPECT_FALSE(IconCache::ExtForScope(scope));
}

// The read path the WebUI request filter actually runs. The cache test above
// cannot catch a ServeIcon that ignores the scope it was handed.
TEST_F(IconCacheTest, ServeIconAnswersAnExtensionIconOnlyForItsOwnScope) {
  IconCache::ExtFor(&regular_).Store(kExtensionId, Icon());

  scoped_refptr<base::RefCountedMemory> served =
      ServeIcon(ExtIconPath(ExtIconScopeFor(&regular_), kExtensionId));
  ASSERT_TRUE(served);
  EXPECT_EQ(Icon(), std::vector<uint8_t>(served->begin(), served->end()));

  EXPECT_FALSE(
      ServeIcon(ExtIconPath(ExtIconScopeFor(&incognito_), kExtensionId)));
  EXPECT_FALSE(ServeIcon(ExtIconPath("made-up", kExtensionId)));
  EXPECT_FALSE(ServeIcon(std::string("exticon/") + kExtensionId + ".png"));
}

// The extension token travels with every action push, and an incognito
// bridge's push reaches the shared chrome surfaces. So it must open nothing
// but extension icons: not the tab favicons, not the url icons.
TEST_F(IconCacheTest, AnExtensionTokenOpensNoOtherIconCache) {
  IconCache::UrlFor(&incognito_).Store(kPage, Icon());
  FaviconCache::For(&incognito_).Store(77, Icon());
  const std::string ext_scope = ExtIconScopeFor(&incognito_);

  EXPECT_NE(ext_scope, IconScopeFor(&incognito_));
  EXPECT_FALSE(ServeIcon(IconPath(ext_scope)));
  EXPECT_FALSE(ServeIcon(TabIconPath(ext_scope)));
}

TEST_F(IconCacheTest, ThePageTokenOpensNoExtensionIcon) {
  IconCache::ExtFor(&regular_).Store(kExtensionId, Icon());

  EXPECT_FALSE(ServeIcon(ExtIconPath(IconScopeFor(&regular_), kExtensionId)));
}

}  // namespace
}  // namespace atrium
