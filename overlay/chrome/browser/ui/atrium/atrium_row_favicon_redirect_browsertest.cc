// Copyright 2026 The Atrium Authors

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"
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

// A favourite added by URL and never opened has no database icon, so its row
// asks the site for /favicon.ico. Many sites answer that with a 30x, and the
// fetch refused every redirect, which left those tiles blank for good.
namespace {

constexpr char kHost[] = "icon.test";

std::string PngBytes() {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(2, 2);
  bitmap.eraseColor(SK_ColorRED);
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  CHECK(png);
  return std::string(png->begin(), png->end());
}

// /favicon.ico answers a 302 to `location`. /icon.png is the only path that
// serves an image, so an icon in the cache can only have come through the
// redirect.
std::unique_ptr<net::test_server::HttpResponse> ServeRedirectedIcon(
    const std::string& location,
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  const std::string_view path = url.path();
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (path == "/favicon.ico") {
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader("Location", location);
    return response;
  }
  if (path == "/icon.png") {
    response->set_code(net::HTTP_OK);
    response->set_content_type("image/png");
    response->set_content(PngBytes());
    return response;
  }
  return nullptr;
}

class AtriumRowFaviconRedirectBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    // A named host, so the page origin is not itself the loopback literal the
    // refusal test redirects to.
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  void StartServerRedirectingIconTo(const std::string& location) {
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeRedirectedIcon, location));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

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

IN_PROC_BROWSER_TEST_F(AtriumRowFaviconRedirectBrowserTest,
                       ARowIconFollowsTheSiteRedirect) {
  StartServerRedirectingIconTo("/icon.png");
  const std::string page =
      embedded_test_server()->GetURL(kHost, "/followed.html").spec();

  RequestIconFor(page);

  EXPECT_TRUE(base::test::RunUntil([&]() { return CachedIcon(page); }))
      << "the row never got an icon, so the /favicon.ico redirect was not "
         "followed";
  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }))
      << "the lookup kept its backfill slot after it answered";
}

// The fetch is anonymous and the user never named the redirect target. A site
// that points /favicon.ico at a loopback or private address gets nothing, and
// the row's queue slot still comes back so the rows behind it can run.
IN_PROC_BROWSER_TEST_F(AtriumRowFaviconRedirectBrowserTest,
                       ARedirectToALocalAddressIsRefused) {
  StartServerRedirectingIconTo(
      "http://127.0.0.1:" +
      base::NumberToString(embedded_test_server()->port()) + "/icon.png");
  const std::string page =
      embedded_test_server()->GetURL(kHost, "/refused.html").spec();

  RequestIconFor(page);

  EXPECT_TRUE(base::test::RunUntil([&]() { return LookupsInFlight() == 0; }))
      << "the refused lookup never freed its backfill slot";
  EXPECT_FALSE(CachedIcon(page))
      << "an icon reached the cache through a redirect to a local address";
}

}  // namespace
