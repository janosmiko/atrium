// Copyright 2026 The Atrium Authors

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

// A window.open popup used to land in a plain top-level window: only a
// normal-type Browser gets an Atrium window. Sign-in libraries report back
// through the opener, so that relation has to survive the card.
namespace {

constexpr char kPopupPath[] = "/atrium-popup-peek.html";

// The popup reports to its opener the moment it loads, the way a sign-in
// callback page does. Served rather than written into a blank window: an
// opener that document.writes into its own popup blocks ExecJs on this build.
std::unique_ptr<net::test_server::HttpResponse> HandlePopupPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kPopupPath) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<html><body>popup<script>"
      "if (window.opener) {"
      "  window.opener.postMessage('from-the-popup', '*');"
      "}"
      "</script></body></html>");
  return response;
}

// A page that never answers, so the popup never paints.
constexpr char kHungPath[] = "/atrium-popup-hangs";

std::unique_ptr<net::test_server::HttpResponse> HandleHungPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kHungPath) {
    return nullptr;
  }
  return std::make_unique<net::test_server::HungResponse>();
}

class AtriumPopupPeekBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    // Named hosts, so the popup can move genuinely cross-origin and the chip
    // has something other than 127.0.0.1 to follow it to.
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  // A new tab, not ui_test_utils::NavigateToURL: an Atrium window starts on
  // its own about:blank tab and that helper waited forever here, hanging every
  // test on its first line. The sibling browsertests all open tabs this way.
  content::WebContents* OpenerOnATestPage() {
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandlePopupPage));
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleHungPage));
    if (!embedded_test_server()->Start()) {
      return nullptr;
    }
    content::WebContents* opener = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/true);
    if (!opener || !content::WaitForLoadStop(opener)) {
      return nullptr;
    }
    return opener;
  }

  // `name` must differ per call within one test: window.open REUSES a window
  // of the same name instead of making a second one, so a repeat would prove
  // nothing about replacing a live peek.
  std::string OpenPopupScript(const std::string& name = "peeked") {
    return OpenPopupToScript(embedded_test_server()->GetURL(kPopupPath), name);
  }

  std::string OpenPopupToScript(const GURL& url, const std::string& name) {
    return "window.__got = '';"
           "window.addEventListener('message', e => { window.__got = e.data; });"
           "localStorage.setItem('atrium-probe', 'same-partition');"
           "window.__h = window.open('" +
           url.spec() + "', '" + name + "', 'width=340,height=380');";
  }
};

atrium::AtriumBrowserWindow* AtriumWindowOf(Browser* browser) {
  return static_cast<atrium::AtriumBrowserWindow*>(browser->window());
}

bool PeekIsShowing(Browser* browser) {
  return AtriumWindowOf(browser)->IsPeekShowing();
}

size_t BrowserCount() {
  return GlobalBrowserCollection::GetInstance()->GetSize();
}

IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       APopupFromAPageBecomesAPeekThatKeepsItsOpener) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);

  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript()));

  // The renderer sends its show request after window.open returns, so the card
  // is not up yet when ExecJs comes back.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return PeekIsShowing(browser()); }))
      << "the popup never landed in the peek card";
  EXPECT_EQ(1u, BrowserCount())
      << "a second browser window opened, so the popup took the stock path";

  // The opener's own handle still works, which is the whole point: the
  // WebContents was moved into the card rather than rebuilt.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(opener, "window.__got").ExtractString() ==
           "from-the-popup";
  })) << "postMessage from the popup never reached its opener";

  EXPECT_EQ("same-partition",
            content::EvalJs(
                opener, "window.__h.localStorage.getItem('atrium-probe')"))
      << "the popup reads a different storage partition than its opener";

  // The card carries no window frame, so the chip is the only thing naming
  // whose page this is - and it has to follow the popup's redirects.
  const GURL cross =
      embedded_test_server()->GetURL("other.example.com", "/title2.html");
  ASSERT_TRUE(content::ExecJs(
      opener, "window.__h.location = '" + cross.spec() + "';"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return AtriumWindowOf(browser())->PeekOriginChipHostForTesting() ==
           u"other.example.com";
  })) << "the origin chip did not follow the popup cross-origin, it reads '"
      << base::UTF16ToUTF8(
             AtriumWindowOf(browser())->PeekOriginChipHostForTesting())
      << "'";

  EXPECT_EQ(false, content::EvalJs(opener, "window.__h.closed"));

  controller->ClosePeek();

  // A hidden card would leave window.closed false forever, and a page that
  // polls it would wait forever with it.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(opener, "window.__h.closed").ExtractBool();
  })) << "window.closed never flipped after the peek was dismissed";
}

// The control. A card is drawn over whatever the window is showing, so a popup
// from a tab the user is not looking at has to stay a window of its own -
// otherwise this test passes for a hook that fires unconditionally.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       APopupFromABackgroundTabKeepsItsOwnWindow) {
  content::WebContents* background = OpenerOnATestPage();
  ASSERT_TRUE(background);
  ASSERT_TRUE(content::WaitForLoadStop(chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title2.html"), -1,
      /*foreground=*/true)));
  ASSERT_NE(background, browser()->tab_strip_model()->GetActiveWebContents());

  ASSERT_TRUE(content::ExecJs(background, OpenPopupScript()));

  ASSERT_TRUE(base::test::RunUntil([&]() { return BrowserCount() == 2u; }))
      << "the popup got no window at all";
  EXPECT_FALSE(PeekIsShowing(browser()))
      << "a background tab's popup drew a card over the tab in front of it";
}

// A sign-in popup closes itself the moment it has reported back. Without this
// the card sits there holding a dead page and the flow looks hung.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       WindowCloseFromInsideTheCardDismissesIt) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript()));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return PeekIsShowing(browser()); }));

  content::WebContents* peeked =
      AtriumWindowOf(browser())->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked);
  // Async: window.close() tears the frame down, so a script that waits for its
  // own result has nothing left to answer through.
  content::ExecuteScriptAsync(peeked, "window.close();");

  ASSERT_TRUE(base::test::RunUntil([&]() { return !PeekIsShowing(browser()); }))
      << "the card outlived the page's own window.close()";
  EXPECT_TRUE(content::EvalJs(opener, "window.__h.closed").ExtractBool());
}

// No stacking: a second popup takes the card, and the first must really die
// rather than linger invisibly with window.closed still false.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       ASecondPopupReplacesTheLivePeek) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript()));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return PeekIsShowing(browser()); }));
  ASSERT_TRUE(content::ExecJs(opener, "window.__first = window.__h;"));

  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript("second")));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(opener, "window.__first.closed").ExtractBool();
  })) << "the replaced popup is still open as far as the page can tell";
  EXPECT_TRUE(PeekIsShowing(browser()));
  EXPECT_EQ(1u, BrowserCount())
      << "the second popup opened a window instead of taking the card";
}

// A nested window.open from the card gets a real window. Adopting it would
// destroy the page that opened it, which is the opener the nested popup
// reports back through - so the chain has to be kept, not the card.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       ANestedPopupFromTheCardGetsItsOwnWindow) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript()));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return PeekIsShowing(browser()); }));

  content::WebContents* peeked =
      AtriumWindowOf(browser())->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked);
  const GURL nested = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(content::ExecJs(
      peeked, "window.__n = window.open('" + nested.spec() +
                  "', 'nested', 'width=320,height=320');"));

  ASSERT_TRUE(base::test::RunUntil([&]() { return BrowserCount() == 2u; }))
      << "the nested popup was dropped instead of opening a window";
  EXPECT_TRUE(PeekIsShowing(browser()))
      << "the nested popup took the card from the page that opened it";
  EXPECT_EQ(false, content::EvalJs(peeked, "window.__n.closed"));
}

// The card is a window of its own and only the popup's page paints into it.
// Until that page paints, the placeholder is all that keeps the card from
// being a see-through hole in the dimmed page beneath it.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       APopupStillLoadingKeepsThePlaceholderUntilItPaints) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  ASSERT_TRUE(content::ExecJs(
      opener, OpenPopupToScript(embedded_test_server()->GetURL(
                                    "slow.example.com", kHungPath),
                                "hung")));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return PeekIsShowing(browser()); }));

  EXPECT_TRUE(AtriumWindowOf(browser())->PeekLoadingPlaceholderVisibleForTesting())
      << "the card dropped its placeholder before the popup painted anything";
  // Nothing has committed, so the chip can only name the target.
  EXPECT_EQ(AtriumWindowOf(browser())->PeekOriginChipHostForTesting(),
            u"slow.example.com")
      << "the chip does not name the host the card is waiting on";

  // The control: a page that paints takes the placeholder down.
  ASSERT_TRUE(content::ExecJs(opener, OpenPopupScript("painted")));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !AtriumWindowOf(browser())
                ->PeekLoadingPlaceholderVisibleForTesting();
  })) << "the placeholder outlived the popup's first paint";
}

// A blank popup a page writes into never reports a first paint, so its card
// cannot wait for one.
IN_PROC_BROWSER_TEST_F(AtriumPopupPeekBrowserTest,
                       ABlankPopupDropsThePlaceholderAtOnce) {
  content::WebContents* opener = OpenerOnATestPage();
  ASSERT_TRUE(opener);
  ASSERT_TRUE(content::ExecJs(
      opener, "window.__h = window.open('', 'blank', 'width=340,height=380');"));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return PeekIsShowing(browser()); }));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !AtriumWindowOf(browser())
                ->PeekLoadingPlaceholderVisibleForTesting();
  })) << "a blank popup's card kept a placeholder nothing will ever replace";
}

}  // namespace
