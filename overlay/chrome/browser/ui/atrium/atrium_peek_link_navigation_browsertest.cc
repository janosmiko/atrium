// Copyright 2026 The Atrium Authors

#include <optional>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/token.h"
#include "base/values.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_matches.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_new_tab_modifiers.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "content/public/test/hit_test_region_observer.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/gfx/geometry/point.h"

// a link clicked inside a peek must produce a real tab row.
namespace atrium {
namespace {

constexpr char kPeekedPagePath[] = "/atrium-peek-link-nav.html";
constexpr char kTargetPagePath[] = "/atrium-peek-link-target.html";
constexpr char kTargetIconPath[] = "/atrium-peek-link-icon.png";
constexpr char kTargetTitle[] = "Peek Link Target";
constexpr char kOnePixelPng[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGA"
    "hKmMIQAAAABJRU5ErkJggg==";

std::unique_ptr<net::test_server::HttpResponse> HandlePeekedPage(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (request.relative_url == kPeekedPagePath) {
    response->set_content_type("text/html");
    // The third anchor is the reported gesture: an icon inside an external
    // link, so the click lands on a child of the anchor and not the anchor.
    response->set_content(
        "<html><body>"
        "<a id='plainlink' href='/title2.html'>plain</a>"
        "<a id='blanklink' href='/title2.html' target='_blank'>blank</a>"
        "<a id='externallink' href='/title2.html' target='_blank' "
        "rel='noopener noreferrer'><span id='externalicon'>&#8599;</span></a>"
        "<a id='downloadlink' href='/title2.html' download>save</a>"
        "</body></html>");
    return response;
  }
  if (request.relative_url == kTargetPagePath) {
    response->set_content_type("text/html");
    response->set_content(base::StringPrintf(
        "<html><head><title>%s</title><link rel='icon' href='%s'></head>"
        "<body>target</body></html>",
        kTargetTitle, kTargetIconPath));
    return response;
  }
  if (request.relative_url == kTargetIconPath) {
    std::string png;
    CHECK(base::Base64Decode(kOnePixelPng, &png));
    response->set_content_type("image/png");
    response->set_content(png);
    return response;
  }
  return nullptr;
}

class AtriumPeekLinkNavigationBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandlePeekedPage));
    InProcessBrowserTest::SetUpOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // With the core shut, BuildSidebarTabsJSON answers with the flat fallback -
  // TabStripModel restated, so a row assertion can never disagree with the
  // strip. The start-up open is async, so a test must open its own.
  bool OpenCoreOnAFixtureDocument() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!core_dir_.CreateUniqueTempDir()) {
      return false;
    }
    // Named with a nonce so SidebarModelIsThisTestsDocument can tell this
    // document from the one the async start-up open may have loaded.
    space_name_ = "Peek " + base::Token::CreateRandom().ToString();
    const std::string json = base::StringPrintf(
        R"({"spaces":[{"id":"peek","name":"%s","emoji":"P","tint":"#111111",)"
        R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
        R"("order":0,"groups":[]}],)"
        R"("appState":{"activeSpaceID":"peek","sidebarWidth":280,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})",
        space_name_.c_str());
    const base::FilePath path = core_dir_.GetPath().AppendASCII("spaces.json");
    if (!base::WriteFile(path, json)) {
      return false;
    }
    atrium_core_close();
    return atrium_core_open(path.value().c_str(), 1.0);
  }

  bool SidebarModelIsThisTestsDocument() {
    std::optional<base::Value> parsed = base::JSONReader::Read(
        BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return false;
    }
    const base::ListValue* spaces = parsed->GetDict().FindList("spaces");
    if (!spaces) {
      return false;
    }
    for (const base::Value& chip : *spaces) {
      const base::DictValue* dict = chip.GetIfDict();
      const std::string* name = dict ? dict->FindString("name") : nullptr;
      if (name && *name == space_name_) {
        return true;
      }
    }
    return false;
  }

  // Same startup-tab workaround as the other peek browsertests. Weak, not
  // raw: a dismiss destroys the peeked contents, and every caller below reads
  // it back across message-loop pumps that can let one through.
  base::WeakPtr<content::WebContents> OpenAPeekOnTestPage() {
    if (!OpenCoreOnAFixtureDocument()) {
      return nullptr;
    }
    if (!embedded_test_server()->Start()) {
      return nullptr;
    }
    content::WebContents* source = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return nullptr;
    }
    BrowserController* controller =
        BrowserController::ControllerForBrowser(browser());
    if (!controller) {
      return nullptr;
    }
    controller->OpenPeek(embedded_test_server()->GetURL(kPeekedPagePath),
                         content::Referrer(), gfx::Point(200, 200));
    if (!base::test::RunUntil([&]() { return window()->IsPeekShowing(); })) {
      return nullptr;
    }
    content::WebContents* peeked = window()->PeekedWebContentsForTesting();
    if (!peeked || !content::WaitForLoadStop(peeked)) {
      return nullptr;
    }
    // A synthetic click is routed by hit-test data the compositor has not
    // submitted yet on a cold run, so it lands nowhere and the test reads as a
    // product failure. Only the first case in a batch was ever slow enough.
    content::WaitForHitTestData(peeked->GetPrimaryMainFrame());
    return peeked->GetWeakPtr();
  }

  // The card behind the controls cluster, the only handle a test has on it.
  PeekWidget* PeekWidgetForTest() {
    PeekControlsWidget* controls = window()->PeekControlsForTesting();
    return controls ? controls->owner_for_testing() : nullptr;
  }

  // Counts rows the sidebar model would render for THIS window's active
  // space - the same JSON PushSidebarSnapshot pushes to the HTML surface.
  int SidebarRowCount() {
    return static_cast<int>(
        TabsFromSidebarModel(BuildSidebarTabsJSON(browser())).size());
  }

  std::optional<CommandBarTab> FindSidebarRow(const std::string& tab_id) {
    for (const CommandBarTab& tab :
         TabsFromSidebarModel(BuildSidebarTabsJSON(browser()))) {
      if (tab.id == tab_id) {
        return tab;
      }
    }
    return std::nullopt;
  }

  // sidebar.js's own close gesture: `chrome.send('sidebarTabClosed', [id])`.
  void CloseThroughSidebar(const std::string& tab_id) {
    content::WebContents* sidebar = window()->SidebarContentsForTesting();
    ASSERT_TRUE(sidebar);
    ASSERT_TRUE(content::ExecJs(
        sidebar, "chrome.send('sidebarTabClosed', ['" + tab_id + "'])"));
  }

  // The row as the user sees it. Nothing puts one in the sidebar's DOM but a
  // pushed snapshot, which the model helpers above never go through.
  int RenderedRowsFor(const std::string& tab_id) {
    content::WebContents* sidebar = window()->SidebarContentsForTesting();
    if (!sidebar) {
      return -1;
    }
    return content::EvalJs(sidebar,
                           "document.querySelectorAll('.row.tabrow[data-tab-"
                           "id=\"" +
                               tab_id + "\"]').length")
        .ExtractInt();
  }

  // sidebar.js's own row click.
  void ClickThroughSidebar(const std::string& tab_id) {
    content::WebContents* sidebar = window()->SidebarContentsForTesting();
    ASSERT_TRUE(sidebar);
    ASSERT_TRUE(content::ExecJs(
        sidebar, "chrome.send('sidebarTabClicked', ['" + tab_id + "'])"));
  }

  base::ScopedTempDir core_dir_;
  // A member, not a local: ~ScopedTempDir blocks, which the test body's own
  // scope disallows.
  base::ScopedTempDir download_dir_;
  std::string space_name_;
};

// An unmodified click is CURRENT_TAB and reaches no delegate at all, so this
// must stay an in-place navigation of the peek's own page.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       PlainClickNavigatesThePeekInPlaceNotANewTab) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());

  const int tabs_before = browser()->tab_strip_model()->count();
  const int rows_before = SidebarRowCount();

  content::TestNavigationObserver observer(peeked.get());
  content::SimulateMouseClickOrTapElementWithId(peeked.get(), "plainlink");
  observer.Wait();
  ASSERT_TRUE(peeked.get());

  EXPECT_EQ(peeked->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title2.html"))
      << "the peek's own page did not navigate for a plain same-window link";
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "a plain click spawned a tab; it should have stayed in the peek";
  EXPECT_EQ(rows_before, SidebarRowCount());
  EXPECT_TRUE(window()->IsPeekShowing())
      << "the peek closed on its own from an in-place navigation";
}

// blink::NavigationPolicyFromEventModifiers (navigation_policy.cc) maps a
// new-tab click to the meta key on Mac, not ctrl - a plain click's modifier
// on this platform. This routes through OpenURLFromTab, not CreateNewWindow.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       CmdClickOnAPlainLinkMustProduceASidebarRow) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());

  const int tabs_before = browser()->tab_strip_model()->count();
  const int rows_before = SidebarRowCount();
  const GURL peek_url_before = peeked->GetLastCommittedURL();

  const gfx::PointF point =
      content::GetCenterCoordinatesOfElementWithId(peeked.get(), "plainlink");
  content::SimulateMouseClickAt(
      peeked.get(), blink::WebInputEvent::kMetaKey,
      blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() > tabs_before;
  })) << "the cmd-click never inserted a tab into this window's strip";

  content::WebContents* new_tab =
      browser()->tab_strip_model()->GetWebContentsAt(
          browser()->tab_strip_model()->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));
  EXPECT_EQ(new_tab->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title2.html"));

  // Cmd-click alone is the background convention - AddNewContents preserves
  // it for target=_blank, so this route must agree.
  EXPECT_NE(browser()->tab_strip_model()->GetActiveWebContents(), new_tab);

  const std::string tab_id = base::NumberToString(atrium::TabId(new_tab));
  std::optional<CommandBarTab> row;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    row = FindSidebarRow(tab_id);
    return row.has_value();
  })) << "TabStripModel gained a tab but the sidebar model reports no row "
         "for id "
      << tab_id << " (" << SidebarRowCount() << " rows, was " << rows_before
      << ")";
  EXPECT_EQ(row->title, "Title Of Awesomeness");
  EXPECT_EQ(row->id, tab_id);

  // ASSERT, not EXPECT: a dismissed peek destroys `peeked`, so carrying on to
  // read it is a use-after-free rather than a failure.
  ASSERT_TRUE(window()->IsPeekShowing())
      << "the cmd-click dismissed the peek it was made in";
  EXPECT_EQ(peeked->GetLastCommittedURL(), peek_url_before);

  // Closable through the same path sidebar.js's close button uses.
  CloseThroughSidebar(tab_id);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() == tabs_before;
  })) << "chrome.send('sidebarTabClosed') did not close the row's tab";
}

// Cmd+Shift-click is the foreground convention on Mac (same
// navigation_policy.cc mapping as the Cmd-click test above).
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       CmdShiftClickOnAPlainLinkOpensInForeground) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());

  const int tabs_before = browser()->tab_strip_model()->count();

  const gfx::PointF point =
      content::GetCenterCoordinatesOfElementWithId(peeked.get(), "plainlink");
  content::SimulateMouseClickAt(
      peeked.get(),
      blink::WebInputEvent::kMetaKey | blink::WebInputEvent::kShiftKey,
      blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() > tabs_before;
  })) << "the cmd+shift-click never inserted a tab into this window's strip";

  content::WebContents* new_tab =
      browser()->tab_strip_model()->GetWebContentsAt(
          browser()->tab_strip_model()->count() - 1);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), new_tab);
}

// target=_blank routes through CreateNewWindow -> PeekWidget::AddNewContents.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       BlankTargetClickMustProduceASidebarRow) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());

  const int tabs_before = browser()->tab_strip_model()->count();
  const int rows_before = SidebarRowCount();

  content::SimulateMouseClickOrTapElementWithId(peeked.get(), "blanklink");
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->count() > tabs_before;
  })) << "the target=_blank click never inserted a tab into this window's "
         "strip at all";

  content::WebContents* new_tab =
      browser()->tab_strip_model()->GetWebContentsAt(
          browser()->tab_strip_model()->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));
  EXPECT_EQ(new_tab->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title2.html"));

  // The strip has it - now check the sidebar agrees.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return SidebarRowCount() > rows_before; }))
      << "TabStripModel gained a tab (now " << tabs_before + 1
      << ") but the sidebar model still reports " << SidebarRowCount()
      << " rows (was " << rows_before << ") - the tab has no row";

  // tab-b is now active, so the card hides (it belongs to tab-a),
  // but the click must not promote or dismiss it - same page, same contents.
  EXPECT_FALSE(window()->IsPeekShowing())
      << "the target=_blank click left the peek showing over the new tab";
  EXPECT_EQ(peeked->GetLastCommittedURL(),
            embedded_test_server()->GetURL(kPeekedPagePath));
}

// The reported gesture, end to end: a plain click on an external link icon
// inside a peek, checked against the core-backed model the sidebar renders
// rather than the flat fallback, and against every property the feature promises.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       PlainClickOnAnExternalBlankLinkGetsAUsableRow) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());
  ASSERT_TRUE(SidebarModelIsThisTestsDocument())
      << "the sidebar model is not this test's core document, so the row "
         "assertions below would only restate TabStripModel: "
      << BuildSidebarTabsJSON(browser());

  TabStripModel* strip = browser()->tab_strip_model();
  const int tabs_before = strip->count();
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const GURL target =
      embedded_test_server()->GetURL("external.test", kTargetPagePath);
  ASSERT_TRUE(content::ExecJs(
      peeked.get(),
      content::JsReplace("document.getElementById('externallink').href = $1",
                         target)));

  content::SimulateMouseClickOrTapElementWithId(peeked.get(), "externalicon");

  ASSERT_TRUE(base::test::RunUntil([&]() { return strip->count() > tabs_before; }))
      << "no tab reached this window's strip: " << strip->count()
      << " tabs (was " << tabs_before << "), "
      << GlobalBrowserCollection::GetInstance()->GetSize() << " browsers (was "
      << browsers_before << ")";

  content::WebContents* new_tab = strip->GetWebContentsAt(strip->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));
  EXPECT_EQ(new_tab->GetLastCommittedURL(), target);

  const std::string tab_id = base::NumberToString(atrium::TabId(new_tab));
  std::optional<CommandBarTab> row;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    row = FindSidebarRow(tab_id);
    return row.has_value();
  })) << "the strip has the tab but the sidebar model carries no row for id "
      << tab_id << ": " << BuildSidebarTabsJSON(browser());
  EXPECT_EQ(row->title, kTargetTitle);
  EXPECT_EQ(row->url, target.spec());

  EXPECT_TRUE(
      base::test::RunUntil([&]() { return RenderedRowsFor(tab_id) == 1; }))
      << "the model carries the row but the sidebar renders no row for it - "
         "the user's own report";

  // The icon the row draws comes from this tab-keyed cache, not from the model.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return FaviconCache::For(browser()->profile())
               .Find(atrium::TabId(new_tab)) != nullptr;
  })) << "no favicon for the new row reached the sidebar's icon cache";

  // the new tab is now active, so the card hides - it belongs to
  // the tab it was opened from, not the tab this click just activated.
  EXPECT_FALSE(window()->IsPeekShowing())
      << "the external link click left the peek showing over the new tab";
  EXPECT_EQ(peeked->GetLastCommittedURL(),
            embedded_test_server()->GetURL(kPeekedPagePath));

  ClickThroughSidebar(tab_id);
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return strip->GetActiveWebContents() == new_tab; }))
      << "clicking the row did not activate its tab";
  row = FindSidebarRow(tab_id);
  ASSERT_TRUE(row.has_value());
  EXPECT_TRUE(row->selected) << "the row does not draw as the selected one";

  CloseThroughSidebar(tab_id);
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return strip->count() == tabs_before; }))
      << "closing the row did not close its tab";
}

// a peek belongs to the tab it was opened from - it hides (not
// dismisses) once a link on its own page activates a different tab, and
// shows the same card again once its own tab is active again.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       PeekHidesOnTabSwitchAwayAndReshowsOnReturn) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab_a = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL(kPeekedPagePath), -1,
      /*foreground=*/true);
  ASSERT_TRUE(tab_a);
  ASSERT_TRUE(content::WaitForLoadStop(tab_a));
  content::WaitForHitTestData(tab_a->GetPrimaryMainFrame());

  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->OpenPeek(embedded_test_server()->GetURL(kPeekedPagePath),
                       content::Referrer(), gfx::Point(200, 200));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));
  content::WebContents* peeked_contents = window()->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked_contents);
  base::WeakPtr<content::WebContents> peeked = peeked_contents->GetWeakPtr();
  ASSERT_TRUE(content::WaitForLoadStop(peeked.get()));
  const GURL peek_url = peeked->GetLastCommittedURL();

  TabStripModel* strip = browser()->tab_strip_model();
  const int tab_a_index = strip->GetIndexOfWebContents(tab_a);
  ASSERT_NE(tab_a_index, TabStripModel::kNoTab);
  const int tabs_before = strip->count();

  ASSERT_TRUE(content::ExecJs(
      peeked.get(), "document.getElementById('blanklink').click()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return strip->count() > tabs_before; }))
      << "the peek's own link never inserted tab-b into the strip";
  content::WebContents* tab_b = strip->GetWebContentsAt(strip->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(tab_b));
  ASSERT_EQ(strip->GetActiveWebContents(), tab_b)
      << "tab-b never became the active tab";

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window()->IsPeekShowing(); }))
      << "the peek stayed visible over tab-b";
  ASSERT_TRUE(peeked.get())
      << "the peek was dismissed instead of merely hidden";
  EXPECT_EQ(peeked->GetLastCommittedURL(), peek_url);

  // The card's own focus manager must let go of the keyboard along with the
  // window, or tab-b reads as active while the peek's page still types.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(tab_b, "document.hasFocus()").ExtractBool();
  })) << "tab-b never gained focus once the peek over it hid";

  strip->ActivateTabAt(tab_a_index);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->IsPeekShowing(); }))
      << "the peek did not reappear once tab-a was active again";
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peeked.get());
  EXPECT_EQ(peeked->GetLastCommittedURL(), peek_url);

  // A second round trip still keys the card to tab-a, not tab-b.
  strip->ActivateTabAt(strip->GetIndexOfWebContents(tab_b));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !window()->IsPeekShowing(); }));
  strip->ActivateTabAt(tab_a_index);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peeked.get());
}

// A peek with no tab left to belong to must close
// outright rather than sit hidden forever.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       ClosingTheSourceTabClosesItsHiddenPeek) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab_a = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL(kPeekedPagePath), -1,
      /*foreground=*/true);
  ASSERT_TRUE(tab_a);
  ASSERT_TRUE(content::WaitForLoadStop(tab_a));
  content::WaitForHitTestData(tab_a->GetPrimaryMainFrame());

  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->OpenPeek(embedded_test_server()->GetURL(kPeekedPagePath),
                       content::Referrer(), gfx::Point(200, 200));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));
  content::WebContents* peeked_contents = window()->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked_contents);
  ASSERT_TRUE(content::WaitForLoadStop(peeked_contents));

  TabStripModel* strip = browser()->tab_strip_model();
  const int tab_a_index = strip->GetIndexOfWebContents(tab_a);
  ASSERT_NE(tab_a_index, TabStripModel::kNoTab);
  const int tabs_before = strip->count();

  ASSERT_TRUE(content::ExecJs(
      peeked_contents, "document.getElementById('blanklink').click()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return strip->count() > tabs_before; }))
      << "the peek's own link never inserted tab-b into the strip";
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window()->IsPeekShowing(); }))
      << "the peek never hid over tab-b, so this test cannot cover the "
         "hidden-peek close path";

  strip->CloseWebContentsAt(tab_a_index, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->PeekedWebContentsForTesting() == nullptr; }))
      << "the peek's WebContents outlived the tab it was opened from";
  EXPECT_FALSE(window()->IsPeekShowing());
}

// A move (not a close) also leaves the source strip
// with no tab for the peek to belong to - the DetachTabForMove branch must
// close it too, not just the WillDeleteWebContents one.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       DetachingTheSourceTabIntoAnotherWindowClosesItsPeek) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab_a = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL(kPeekedPagePath), -1,
      /*foreground=*/true);
  ASSERT_TRUE(tab_a);
  ASSERT_TRUE(content::WaitForLoadStop(tab_a));

  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->OpenPeek(embedded_test_server()->GetURL(kPeekedPagePath),
                       content::Referrer(), gfx::Point(200, 200));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));
  ASSERT_TRUE(window()->PeekedWebContentsForTesting());

  BrowserController* little =
      BrowserController::AdoptTabIntoLittleWindow(tab_a, std::nullopt);
  ASSERT_TRUE(little) << "the move-to-little-window helper itself failed";

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window()->PeekedWebContentsForTesting() == nullptr; }))
      << "the peek's WebContents outlived tab-a's move to another window";
  EXPECT_FALSE(window()->IsPeekShowing());
}

// One PeekWidget serves every peek on a window, so the guard that keeps a
// card up while it opens a tab must not outlive the card that armed it.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       ASecondPeekDoesNotInheritTheFirstsTabOpenGuard) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());
  ASSERT_TRUE(PeekWidgetForTest());
  ASSERT_FALSE(PeekWidgetForTest()->SelfOpenedTabGuardArmedForTesting())
      << "a peek that has opened nothing starts with the guard armed";

  content::SimulateMouseClickOrTapElementWithId(peeked.get(), "blanklink");
  ASSERT_TRUE(base::test::RunUntil([&]() {
    PeekWidget* peek = PeekWidgetForTest();
    return peek && peek->SelfOpenedTabGuardArmedForTesting();
  })) << "the tab the card opened never armed the guard";

  // OpenPeek closes the first card and shows a second one - the no-stacking
  // path a user takes by shift+clicking a second link.
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->OpenPeek(embedded_test_server()->GetURL(kPeekedPagePath),
                       content::Referrer(), gfx::Point(200, 200));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));

  ASSERT_TRUE(PeekWidgetForTest());
  EXPECT_FALSE(PeekWidgetForTest()->SelfOpenedTabGuardArmedForTesting())
      << "the second card inherited the first's tab-open guard, so a genuine "
         "click-away would not dismiss it";
}

// window.open() inserts its tab with no url, the same as a target=_blank one,
// so the row can only come from the deferred open once a real url commits.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       AWindowOpenTabGetsItsRowFromTheLaterRealUrl) {
  base::WeakPtr<content::WebContents> peeked = OpenAPeekOnTestPage();
  ASSERT_TRUE(peeked.get());
  ASSERT_TRUE(SidebarModelIsThisTestsDocument())
      << BuildSidebarTabsJSON(browser());

  TabStripModel* strip = browser()->tab_strip_model();
  const int tabs_before = strip->count();
  const GURL target =
      embedded_test_server()->GetURL("external.test", kTargetPagePath);

  ASSERT_TRUE(
      content::ExecJs(peeked.get(),
                      "window.opened = window.open('', '_blank')"));
  ASSERT_TRUE(base::test::RunUntil([&]() { return strip->count() > tabs_before; }))
      << "window.open never inserted a tab into this window's strip";
  content::WebContents* new_tab = strip->GetWebContentsAt(strip->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));

  ASSERT_TRUE(content::ExecJs(
      peeked.get(),
      content::JsReplace("window.opened.location.href = $1", target)));
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));
  EXPECT_EQ(new_tab->GetLastCommittedURL(), target);

  const std::string tab_id = base::NumberToString(atrium::TabId(new_tab));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return FindSidebarRow(tab_id).has_value(); }))
      << "the real url committed but no row was opened for the tab: "
      << BuildSidebarTabsJSON(browser());
}

// Option alone reaches the browser as NEW_WINDOW (navigation_policy.cc's mac
// branch), the disposition the peek intercept takes. It used to be
// SAVE_TO_DISK, so the chord silently downloaded the link.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       OptionClickOnAnOrdinaryTabOpensAPeek) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL(kPeekedPagePath), -1,
      /*foreground=*/true);
  ASSERT_TRUE(source);
  ASSERT_TRUE(content::WaitForLoadStop(source));
  content::WaitForHitTestData(source->GetPrimaryMainFrame());
  ASSERT_FALSE(window()->IsPeekShowing());

  TabStripModel* strip = browser()->tab_strip_model();
  const int tabs_before = strip->count();

  const gfx::PointF point =
      content::GetCenterCoordinatesOfElementWithId(source, "plainlink");
  content::SimulateMouseClickAt(
      source, blink::WebInputEvent::kAltKey,
      blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));

  ASSERT_TRUE(base::test::RunUntil([&]() { return window()->IsPeekShowing(); }))
      << "the option-click opened no peek; the strip holds " << strip->count()
      << " tabs (was " << tabs_before << ")";
  content::WebContents* peeked = window()->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked);
  ASSERT_TRUE(content::WaitForLoadStop(peeked));
  EXPECT_EQ(peeked->GetLastCommittedURL(),
            embedded_test_server()->GetURL("/title2.html"));

  EXPECT_EQ(tabs_before, strip->count())
      << "the option-click opened a tab as well as the peek";
  EXPECT_EQ(source->GetLastCommittedURL(),
            embedded_test_server()->GetURL(kPeekedPagePath))
      << "the source tab navigated instead of staying put";
}

// The renderer cannot save a CROSS-ORIGIN <a download> itself
// (html_anchor_element.cc's CanReadContent gate), so kNavigationPolicyDownload
// is the browser's only signal that this is a file.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       OptionClickOnACrossOriginDownloadLinkStillDownloads) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("app.test", kPeekedPagePath),
      -1, /*foreground=*/true);
  ASSERT_TRUE(source);
  ASSERT_TRUE(content::WaitForLoadStop(source));

  // Plain text/html, no Content-Disposition: only the policy can turn this
  // into a download, which is what the test measures.
  const GURL target =
      embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_TRUE(content::ExecJs(
      source,
      content::JsReplace("document.getElementById('downloadlink').href = $1",
                         target)));
  content::WaitForHitTestData(source->GetPrimaryMainFrame());

  content::DownloadManager* manager =
      browser()->profile()->GetDownloadManager();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(download_dir_.CreateUniqueTempDir());
    DownloadPrefs::FromDownloadManager(manager)->SetDownloadPath(
        download_dir_.GetPath());
  }

  content::DownloadTestObserverInProgress observer(manager, /*wait_count=*/1);
  const gfx::PointF point =
      content::GetCenterCoordinatesOfElementWithId(source, "downloadlink");
  content::SimulateMouseClickAt(
      source, blink::WebInputEvent::kAltKey,
      blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));
  observer.WaitForFinished();

  EXPECT_EQ(1u, observer.NumDownloadsSeenInState(
                    download::DownloadItem::IN_PROGRESS))
      << "the option-click on a cross-origin <a download> started no download";
  EXPECT_FALSE(window()->IsPeekShowing())
      << "the option-click peeked the file instead of saving it";
}

// A popup has no Atrium controller, and Option alone reaches
// Browser::OpenURLFromTab as NEW_WINDOW, the same as a real Shift+click.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       OptionClickInAWindowWithNoControllerDownloadsInstead) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* opener = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(opener);
  content::WaitForLoadStopWithoutSuccessCheck(opener);
  // A second foreground tab so the popup's opener is not the active one -
  // ShouldPeekPopup then refuses it a card and it gets a real Browser.
  content::WebContents* second_tab = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title2.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(second_tab);
  content::WaitForLoadStopWithoutSuccessCheck(second_tab);

  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  ASSERT_TRUE(content::ExecJs(
      opener, "window.open('" +
                  embedded_test_server()->GetURL(kPeekedPagePath).spec() +
                  "', 'popup', 'width=340,height=380')"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() >
           browsers_before;
  })) << "the popup never got its own window";

  Browser* popup_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* candidate_browser = candidate->GetBrowserForMigrationOnly();
        if (candidate_browser != browser()) {
          popup_browser = candidate_browser;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(popup_browser);
  ASSERT_FALSE(BrowserController::ControllerForBrowser(popup_browser))
      << "the popup got an Atrium controller, so this test proves nothing";

  content::WebContents* popup_contents =
      popup_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(popup_contents);
  content::WaitForLoadStopWithoutSuccessCheck(popup_contents);
  content::WaitForHitTestData(popup_contents->GetPrimaryMainFrame());

  content::DownloadManager* manager =
      browser()->profile()->GetDownloadManager();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(download_dir_.CreateUniqueTempDir());
    DownloadPrefs::FromDownloadManager(manager)->SetDownloadPath(
        download_dir_.GetPath());
  }

  content::DownloadTestObserverInProgress observer(manager, /*wait_count=*/1);
  // Left running past the assertions below - QuitBrowsers() shows a native
  // "downloads in progress" alert (which never resolves in a browser test)
  // if this popup's window closes with the download still unfinished.
  content::DownloadTestObserverTerminal terminal_observer(
      manager, /*wait_count=*/1,
      content::DownloadTestObserverTerminal::ON_DANGEROUS_DOWNLOAD_ACCEPT);
  const gfx::PointF point = content::GetCenterCoordinatesOfElementWithId(
      popup_contents, "plainlink");
  SetOptionOnlyModifierHeldForTesting(true);
  content::SimulateMouseClickAt(
      popup_contents, blink::WebInputEvent::kAltKey,
      blink::WebMouseEvent::Button::kLeft,
      gfx::Point(static_cast<int>(point.x()), static_cast<int>(point.y())));
  observer.WaitForFinished();
  SetOptionOnlyModifierHeldForTesting(std::nullopt);

  EXPECT_EQ(1u, observer.NumDownloadsSeenInState(
                    download::DownloadItem::IN_PROGRESS))
      << "the option-click in a controller-less window started no download";
  EXPECT_EQ(browsers_before + 1,
            GlobalBrowserCollection::GetInstance()->GetSize())
      << "the option-click opened a second new window instead of downloading";
  EXPECT_EQ(popup_contents->GetLastCommittedURL(),
            embedded_test_server()->GetURL(kPeekedPagePath))
      << "the popup navigated instead of staying put";
  terminal_observer.WaitForFinished();
}

// For a PLAIN click the download attribute on a cross-origin link is inert,
// so this navigates in place.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       PlainClickOnACrossOriginDownloadLinkNavigatesInPlace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("app.test", kPeekedPagePath),
      -1, /*foreground=*/true);
  ASSERT_TRUE(source);
  ASSERT_TRUE(content::WaitForLoadStop(source));

  const GURL target =
      embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_TRUE(content::ExecJs(
      source,
      content::JsReplace("document.getElementById('downloadlink').href = $1",
                         target)));

  content::TestNavigationObserver observer(source);
  ASSERT_TRUE(content::ExecJs(
      source, "document.getElementById('downloadlink').click()"));
  observer.Wait();

  EXPECT_EQ(source->GetLastCommittedURL(), target);
  EXPECT_FALSE(window()->IsPeekShowing());
}

// A tab the renderer creates for target=_blank enters the strip with no url
// whichever page opened it, so the row is owed outside a peek too. Pinned here
// so a fix that only covered the peek route reads as a failure.
IN_PROC_BROWSER_TEST_F(AtriumPeekLinkNavigationBrowserTest,
                       BlankTargetClickFromAnOrdinaryTabAlsoGetsARow) {
  ASSERT_TRUE(OpenCoreOnAFixtureDocument());
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL(kPeekedPagePath), -1,
      /*foreground=*/true);
  ASSERT_TRUE(source);
  ASSERT_TRUE(content::WaitForLoadStop(source));
  ASSERT_TRUE(SidebarModelIsThisTestsDocument())
      << BuildSidebarTabsJSON(browser());

  TabStripModel* strip = browser()->tab_strip_model();
  const int tabs_before = strip->count();
  // Dispatched on the element rather than aimed at its coordinates: a
  // synthetic click on an ordinary tab misses often enough in the probe VM to
  // read as a product failure. The peek test above keeps the real click.
  ASSERT_TRUE(
      content::ExecJs(source, "document.getElementById('blanklink').click()"));
  ASSERT_TRUE(base::test::RunUntil([&]() { return strip->count() > tabs_before; }))
      << "the target=_blank click never inserted a tab into the strip";

  content::WebContents* new_tab = strip->GetWebContentsAt(strip->count() - 1);
  ASSERT_TRUE(content::WaitForLoadStop(new_tab));
  const std::string tab_id = base::NumberToString(atrium::TabId(new_tab));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return FindSidebarRow(tab_id).has_value(); }))
      << "no sidebar row for a target=_blank tab opened outside any peek: "
      << BuildSidebarTabsJSON(browser());
}

}  // namespace
}  // namespace atrium
