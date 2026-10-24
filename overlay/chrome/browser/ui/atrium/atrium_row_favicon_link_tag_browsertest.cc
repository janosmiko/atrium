// Copyright 2026 The Atrium Authors

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
#include "chrome/browser/ui/atrium/atrium_row_icon_lookup.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

// A site with no /favicon.ico names its icon in the page head. The fourth
// tier reads that tag, so such a favourite gets an icon with no visit.
namespace {

constexpr char kHost[] = "site.test";
constexpr char kOtherHost[] = "login.other.test";
constexpr char kEmptyIcoHost[] = "empty-ico.test";
constexpr char kDataHost[] = "data-icon.test";
constexpr char kAwayHost[] = "away.test";
constexpr char kHangHost[] = "hang.test";

std::string PngBytes() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(2, 2);
  bitmap.eraseColor(SK_ColorBLUE);
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  CHECK(png);
  return std::string(png->begin(), png->end());
}

std::unique_ptr<net::test_server::HttpResponse> Html(const std::string& body) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(body);
  return response;
}

// Tier 4 now fetches the origin root, not the row's own page path, so each
// scenario's content lives at "/", keyed by host. `requested_paths` records
// every path asked of this server, so a test can check tier 4 hit it.
std::unique_ptr<net::test_server::HttpResponse> ServeSite(
    base::Lock* requested_paths_lock,
    std::vector<std::string>* requested_paths,
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  const std::string_view path = url.path();
  {
    // The server runs this handler on its own IO thread, off the test body.
    base::AutoLock lock(*requested_paths_lock);
    requested_paths->push_back(std::string(path));
  }
  const auto host = request.headers.find("Host");
  const auto at_host = [&](const char* name) {
    return host != request.headers.end() && host->second.starts_with(name);
  };
  if (at_host(kAwayHost) && (path == "/" || path == "/away.html")) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader(
        "Location", "http://" + std::string(kOtherHost) + ":" +
                        base::NumberToString(url.EffectiveIntPort()) +
                        "/login.html");
    return response;
  }
  if (path == "/favicon.ico" && at_host(kHangHost)) {
    return std::make_unique<net::test_server::HungResponse>();
  }
  if (path == "/favicon.ico") {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    if (at_host(kEmptyIcoHost)) {
      response->set_code(net::HTTP_OK);
      response->set_content_type("image/x-icon");
      return response;
    }
    response->set_code(net::HTTP_NOT_FOUND);
    return response;
  }
  if (path == "/" || path == "/linked.html" || path == "/data.html") {
    if (at_host(kDataHost)) {
      return Html(
          "<html><head><link rel=\"icon\" href=\"data:image/png;base64," +
          base::Base64Encode(PngBytes()) + "\"></head></html>");
    }
    return Html("<html><head><link rel=\"icon\" href=\"/assets/i.png\">"
                "</head><body>x</body></html>");
  }
  if (path == "/assets/i.png") {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_OK);
    response->set_content_type("image/png");
    response->set_content(PngBytes());
    return response;
  }
  if (path == "/login.html") {
    return Html("<html><head><link rel=\"icon\" href=\"/assets/i.png\">"
                "</head></html>");
  }
  return nullptr;
}

class AtriumRowFaviconLinkTagBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &ServeSite, &requested_paths_lock_, &requested_paths_));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  std::string PageOn(const char* host, const char* path) {
    return embedded_test_server()->GetURL(host, path).spec();
  }

  bool RequestedPath(const std::string& path) {
    base::AutoLock lock(requested_paths_lock_);
    return std::find(requested_paths_.begin(), requested_paths_.end(),
                     path) != requested_paths_.end();
  }

  base::Lock requested_paths_lock_;
  std::vector<std::string> requested_paths_;

  const std::vector<uint8_t>* CachedIcon(const std::string& page) {
    return atrium::IconCache::UrlFor(browser()->profile()).Find(page);
  }

  size_t LookupsInFlight() {
    return atrium::IconBackfillQueueFor(
               reinterpret_cast<uintptr_t>(browser()->profile()))
        .in_flight();
  }

  void RequestIconFor(const std::string& page) {
    atrium::BrowserController* controller =
        atrium::BrowserController::ControllerForBrowser(browser());
    ASSERT_TRUE(controller);
    controller->RequestFaviconsForRowURLs({page});
  }
};

// A rail pane reads only its own profile's cache, so an icon for another
// profile's favourite is useless in the presenter's.
IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       AnotherProfilesRowFillsThatProfilesCache) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);
  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-backfill-other"));
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &other, /*show=*/false, /*startup_url=*/std::string(),
      atrium::BrowserController::kDefaultSlot));

  const std::string page = PageOn(kHost, "/linked.html");
  const std::string own = browser()->profile()->GetBaseName().AsUTF8Unsafe();
  static_cast<atrium::AtriumBrowserWindow*>(browser()->window())
      ->RequestMissingRowFavicons(
          R"({"activeSpace":0,"spaces":[{"profileId":")" + own +
          R"("},{"profileId":"atrium-backfill-other",)"
          R"("favorites":[{"id":")" +
          page + R"("}]}]})");
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::IconCache::UrlFor(&other).Find(page) != nullptr;
  })) << "the other profile's favourite never reached its own cache";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::IconBackfillQueueFor(reinterpret_cast<uintptr_t>(&other))
               .in_flight() == 0;
  }));
  EXPECT_FALSE(CachedIcon(page))
      << "the presenter's cache took an icon none of its panes reads";
}

IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       ARowIconComesFromThePagesLinkTag) {
  const std::string page = PageOn(kHost, "/linked.html");
  RequestIconFor(page);
  EXPECT_TRUE(base::test::RunUntil([&]() { return CachedIcon(page); }))
      << "the row never got the icon its page names";
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }));
  EXPECT_TRUE(RequestedPath("/"))
      << "tier 4 fetched the saved page path instead of the origin root";
}

IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       ADataUrlIconResolves) {
  const std::string page = PageOn(kDataHost, "/data.html");
  RequestIconFor(page);
  EXPECT_TRUE(base::test::RunUntil([&]() { return CachedIcon(page); }))
      << "the inline data: icon never reached the cache";
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }));
}

// A 200 with an empty body is not a fetch failure, so it must not be
// mistaken for the dead-host case that ends a row before tier 4.
IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       AnEmptyIcoBodyStillReachesTheLinkTag) {
  const std::string page = PageOn(kEmptyIcoHost, "/linked.html");
  RequestIconFor(page);
  EXPECT_TRUE(base::test::RunUntil([&]() { return CachedIcon(page); }))
      << "an empty /favicon.ico body never fell through to tier 4";
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }));
}

// The anonymous GET landed on another domain, so its icon is the login
// provider's, not the site's. The row stays blank and frees its slot.
IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       ALoginRedirectStoresNothing) {
  const std::string page = PageOn(kAwayHost, "/away.html");
  RequestIconFor(page);
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }))
      << "the row never freed its backfill slot";
  EXPECT_FALSE(CachedIcon(page))
      << "the login page's icon landed on the favourite";
}

// Pins the slot freeing itself, not the timeout that does it - the loader's
// own before the rewiring, kRowIconDeadline after.
IN_PROC_BROWSER_TEST_F(AtriumRowFaviconLinkTagBrowserTest,
                       ARowDeadlineFreesTheSlot) {
  const std::string page = PageOn(kHangHost, "/");
  RequestIconFor(page);
  base::test::ScopedRunLoopTimeout run_timeout(
      FROM_HERE, atrium::kRowIconDeadline + base::Seconds(15));
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }))
      << "a hung /favicon.ico never freed the row's backfill slot";
  EXPECT_FALSE(CachedIcon(page));
}

}  // namespace
