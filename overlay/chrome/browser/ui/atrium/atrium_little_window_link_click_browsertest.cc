// Copyright 2026 The Atrium Authors

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/hit_test_region_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point.h"

// A Cmd+Option click on a link asks for NEW_SPLIT_VIEW, whose stock handling
// NOTREACHEDs for an Atrium window. Atrium spends the chord on a little
// window instead. These tests drive the real chord.
namespace atrium {
namespace {

// Same technique atrium_little_window_find_tabbed_browser_browsertest.cc
// uses: BrowserController has no public accessor for its own Browser*.
class NewBrowserRecorder : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    opened_.push_back(browser);
  }

  std::vector<raw_ptr<BrowserWindowInterface>> opened_;
};

constexpr char kLinkPagePath[] = "/atrium-little-window-link.html";

std::unique_ptr<net::test_server::HttpResponse> HandleLinkPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kLinkPagePath) {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(
      "<html><body><a id='link' href='/title2.html'>link</a></body></html>");
  return response;
}

class AtriumLittleWindowLinkClickBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleLinkPage));
    InProcessBrowserTest::SetUpOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // The start-up tab is not a served page, so a test opens its own.
  content::WebContents* OpenLinkPage() {
    if (!embedded_test_server()->Start()) {
      return nullptr;
    }
    content::WebContents* source = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL(kLinkPagePath), -1,
        /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return nullptr;
    }
    // A synthetic click is routed by hit-test data the compositor has not
    // submitted yet on a cold run, so it lands nowhere.
    content::WaitForHitTestData(source->GetPrimaryMainFrame());
    return source;
  }

  void ClickLinkWith(content::WebContents* source, int modifiers) {
    const gfx::PointF point =
        content::GetCenterCoordinatesOfElementWithId(source, "link");
    content::SimulateMouseClickAt(
        source, modifiers, blink::WebMouseEvent::Button::kLeft,
        gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));
  }

  // Named in the failure text: without it a click that quietly opened a plain
  // tab reads the same as one that did nothing.
  std::string DescribeStrip() {
    TabStripModel* model = browser()->tab_strip_model();
    std::string out = base::StringPrintf("%d tabs:", model->count());
    for (int i = 0; i < model->count(); ++i) {
      out += " " + model->GetWebContentsAt(i)->GetLastCommittedURL().spec();
    }
    return out;
  }

  // The one tab of `opened`, or null if `opened` is not a little window at
  // all - a plain new window would otherwise read as a pass here.
  content::WebContents* LittleTabIn(BrowserWindowInterface* opened) {
    if (!opened) {
      return nullptr;
    }
    BrowserController* controller = BrowserController::ControllerForBrowser(
        opened->GetBrowserForMigrationOnly());
    if (!controller || !controller->IsLittle()) {
      return nullptr;
    }
    TabStripModel* model = opened->GetTabStripModel();
    return model->count() == 1 ? model->GetWebContentsAt(0) : nullptr;
  }
};

// The reproduction case: without the intercept this reaches
// TabStripModel::AddToNewSplit and takes the whole browser down.
IN_PROC_BROWSER_TEST_F(AtriumLittleWindowLinkClickBrowserTest,
                       CmdOptionClickOpensTheLinkInALittleWindow) {
  content::WebContents* source = OpenLinkPage();
  ASSERT_TRUE(source);
  ASSERT_FALSE(window()->IsSplit());
  const int tabs_before = browser()->tab_strip_model()->count();

  NewBrowserRecorder recorder;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation(&recorder);
  observation.Observe(GlobalBrowserCollection::GetInstance());

  ClickLinkWith(source, blink::WebInputEvent::kMetaKey |
                            blink::WebInputEvent::kAltKey);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return recorder.opened_.size() == 1u; }))
      << "the cmd+option click opened no window; " << DescribeStrip();
  observation.Reset();

  content::WebContents* landed = LittleTabIn(recorder.opened_[0]);
  ASSERT_TRUE(landed) << "the window the click opened is not a little one";
  ASSERT_TRUE(content::WaitForLoadStop(landed));
  EXPECT_EQ(landed->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title2.html"));

  // The chord seeds the window from the link, not from a bare url: the ctor's
  // typed seed would commit with an empty referrer.
  content::NavigationEntry* entry =
      landed->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->GetReferrer().url,
            embedded_test_server()->GetURL(kLinkPagePath))
      << "the little window's navigation dropped the link page's referrer";

  EXPECT_FALSE(window()->IsSplit())
      << "the cmd+option click split the source window";
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "the link also landed in the source window; " << DescribeStrip();
  EXPECT_EQ(source->GetLastCommittedURL(),
            embedded_test_server()->GetURL(kLinkPagePath))
      << "the source tab navigated instead of staying put";
}

// A little window is never reused: the second link gets one of its own, and
// neither click folds anything back into the source window.
IN_PROC_BROWSER_TEST_F(AtriumLittleWindowLinkClickBrowserTest,
                       ASecondCmdOptionClickOpensItsOwnLittleWindow) {
  content::WebContents* source = OpenLinkPage();
  ASSERT_TRUE(source);
  const int tabs_before = browser()->tab_strip_model()->count();

  NewBrowserRecorder recorder;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation(&recorder);
  observation.Observe(GlobalBrowserCollection::GetInstance());

  ClickLinkWith(source, blink::WebInputEvent::kMetaKey |
                            blink::WebInputEvent::kAltKey);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return recorder.opened_.size() == 1u; }));

  ASSERT_TRUE(content::ExecJs(
      source, "document.getElementById('link').href = '/title3.html'"));
  ClickLinkWith(source, blink::WebInputEvent::kMetaKey |
                            blink::WebInputEvent::kAltKey);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return recorder.opened_.size() == 2u; }))
      << "the second cmd+option click opened no window of its own";
  observation.Reset();

  content::WebContents* second = LittleTabIn(recorder.opened_[1]);
  ASSERT_TRUE(second) << "the second window is not a little one";
  ASSERT_TRUE(content::WaitForLoadStop(second));
  EXPECT_EQ(second->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title3.html"));

  EXPECT_FALSE(window()->IsSplit());
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "a cmd+option click left a tab in the source window; "
      << DescribeStrip();
}

// Cmd+Option+Shift falls back to a new tab, which the little-window intercept
// must not swallow.
IN_PROC_BROWSER_TEST_F(AtriumLittleWindowLinkClickBrowserTest,
                       CmdOptionShiftClickStillOpensAForegroundTab) {
  content::WebContents* source = OpenLinkPage();
  ASSERT_TRUE(source);
  const int tabs_before = browser()->tab_strip_model()->count();

  ClickLinkWith(source, blink::WebInputEvent::kMetaKey |
                            blink::WebInputEvent::kAltKey |
                            blink::WebInputEvent::kShiftKey);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() > tabs_before;
  })) << "cmd+option+shift never opened a tab";
  EXPECT_FALSE(window()->IsSplit())
      << "cmd+option+shift split the window; nothing should";
}

}  // namespace
}  // namespace atrium
