// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/scoped_environment_variable_override.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace {

// A sized link, since the walk skips anything with no box to put a label on.
constexpr char kAddLink[] =
    "const a = document.createElement('a');"
    "a.href = '/x'; a.textContent = 'x';"
    "a.style.cssText = 'display:block;width:40px;height:10px';"
    "document.body.appendChild(a);";

class AtriumVimModeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
    // browser_tests serves chrome/test/data, which has no
    // cross_site_iframe_factory.html - the subframe case 404s without this.
    embedded_test_server()->ServeFilesFromSourceDirectory("content/test/data");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&AtriumVimModeBrowserTest::ServeSlowLoadingPage));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // Parses immediately, finishes loading three seconds later. No fixture in
  // either data tree has that shape.
  static std::unique_ptr<net::test_server::HttpResponse> ServeSlowLoadingPage(
      const net::test_server::HttpRequest& request) {
    if (request.GetURL().path() != "/vim-slow.html") {
      return nullptr;
    }
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_content_type("text/html");
    response->set_content("<html><body><img src=\"/slow?3\"></body></html>");
    return response;
  }

  // Reads the labels the hints actually carry, not merely how many there are:
  // two frames each painting one hint is the case that has to be told apart
  // from two frames each painting 'a'.
  std::vector<std::string> HintLabelsIn(content::RenderFrameHost* frame) {
    // ExtractList is LIFETIME_BOUND, so the result has to outlive the read.
    const content::EvalJsResult result = content::EvalJs(
        frame,
        "Array.from(document.querySelectorAll("
        "    '.atrium-vim-hint')).map((el) => el.textContent)",
        content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
    std::vector<std::string> out;
    for (const base::Value& entry : result.ExtractList()) {
      out.push_back(entry.GetString());
    }
    return out;
  }

  content::WebContents* ActiveContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  atrium::AtriumBrowserWindow* Window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // An Atrium test window opens with no tab, so the first navigation has to
  // create one - NavigateToURL would take a null active contents and crash.
  void NavigateAndWait(const std::string& host, const std::string& path) {
    const GURL url = embedded_test_server()->GetURL(host, path);
    if (ActiveContents()) {
      ASSERT_TRUE(content::NavigateToURL(ActiveContents(), url));
    } else {
      ASSERT_TRUE(chrome::AddAndReturnTabAt(browser(), url, -1,
                                            /*foreground=*/true));
    }
    // Reading a page before it commits fails as a JS TypeError and reads as a
    // product defect.
    ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  }

  // A second tab on another host, foregrounded, for the cases that need the
  // strip to have somewhere to cycle to.
  void AddSecondTab() {
    ASSERT_TRUE(chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("b.test", "/title2.html"), -1,
        /*foreground=*/true));
    ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  }

  // content/test/data has no form fixture, and the cases below only need a
  // focused text field on a page tall enough for j to have somewhere to go.
  void AddFocusedInput() {
    ASSERT_TRUE(content::ExecJs(
        ActiveContents(),
        "const el = document.createElement('input');"
        "document.body.prepend(el);"
        "el.focus();"));
  }

  // the motions glide now, so a read on the same turn answers zero
  // whether or not the key was stolen. Three frames in, a real scroll has
  // moved.
  int ScrollYAfterAFewFrames() {
    return content::EvalJs(
               ActiveContents(),
               "new Promise(r => requestAnimationFrame(() =>"
               " requestAnimationFrame(() => requestAnimationFrame(() =>"
               " r(Math.round(window.scrollY))))))")
        .ExtractInt();
  }
};

IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, InstallsInTheMainFrame) {
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(true, content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                  "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
}

// The marginalia call site this one is modelled on only reaches
// GetPrimaryMainFrame, so a cross-origin iframe would silently have no vim.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       InstallsInACrossOriginSubframe) {
  NavigateAndWait("a.test", "/cross_site_iframe_factory.html?a.test(b.test)");
  Window()->InstallVimOnActiveTabForTesting();

  content::RenderFrameHost* child =
      content::ChildFrameAt(ActiveContents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  EXPECT_NE(child->GetLastCommittedOrigin(),
            ActiveContents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  EXPECT_EQ(true, content::EvalJs(child, "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
}

// The script is injected once per document load and must not stack.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       ASecondInstallLeavesOneIndicator) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();
  // Install alone paints nothing, so the drain is what raises the indicator
  // this case counts.
  Window()->DrainVimModeForTesting();
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               ActiveContents(),
               "document.querySelectorAll('.atrium-vim-indicator').length")
               .ExtractInt() == 1;
  }));
}

// The verb has to run against a real page, not a stub.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, JScrollsARealPage) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  ASSERT_EQ(0, ScrollYAfterAFewFrames());
  Window()->SendVimKeyForTesting("j");

  // Rounded at the source: a glide is mid-animation most of the time it is
  // read, and ExtractInt CHECK-fails on the fraction rather than failing the
  // test.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents(), "Math.round(window.scrollY)")
               .ExtractInt() > 0;
  }));
}

// control used to block the steal outright, so none of the four
// page motions ever reached vim.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TheControlMotionsScrollARealPage) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            Window()->RouteVimKeyForTesting("ctrl-f", /*control=*/true));
  Window()->SendVimKeyForTesting("ctrl-f");
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return ScrollYAfterAFewFrames() > 0; }))
      << "ctrl+f moved nothing";

  const int after_page = ScrollYAfterAFewFrames();
  Window()->SendVimKeyForTesting("ctrl-b");
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return ScrollYAfterAFewFrames() < after_page; }))
      << "ctrl+b did not come back up from " << after_page;
}

// the one piece of the chord path no other test reaches. A control
// key's text is the 0x04 control byte, which names no verb, so the character
// has to be rebuilt from the key code before routing ever sees it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       AControlChordIsNamedFromTheKeyItsTextCannotCarry) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  input::NativeWebKeyboardEvent down(
      blink::WebInputEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kControlKey, base::TimeTicks::Now());
  down.windows_key_code = ui::VKEY_D;
  down.text[0] = 0x04;
  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            Window()->PreHandleKeyboardEvent(down));

  // Shift rides the same key code, so without a gate of its own the two are
  // one chord and every site shortcut on ctrl+shift+d breaks.
  input::NativeWebKeyboardEvent shifted(
      blink::WebInputEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kControlKey | blink::WebInputEvent::kShiftKey,
      base::TimeTicks::Now());
  shifted.windows_key_code = ui::VKEY_D;
  shifted.text[0] = 0x04;
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->PreHandleKeyboardEvent(shifted));

  // A chord with no verb is still the page's, which is what keeps ctrl+a and
  // the rest of the alphabet working.
  input::NativeWebKeyboardEvent other(
      blink::WebInputEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kControlKey, base::TimeTicks::Now());
  other.windows_key_code = ui::VKEY_A;
  other.text[0] = 0x01;
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->PreHandleKeyboardEvent(other));
}

// history had no motion at all.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HAndLWalkTheHistory) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  const GURL first = ActiveContents()->GetLastCommittedURL();
  NavigateAndWait("a.test", "/title1.html");
  const GURL second = ActiveContents()->GetLastCommittedURL();
  ASSERT_NE(first, second);
  Window()->InstallVimOnActiveTabForTesting();

  Window()->SendVimKeyForTesting("H");
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return ActiveContents()->GetLastCommittedURL() == first;
  })) << "H did not go back";

  Window()->InstallVimOnActiveTabForTesting();
  Window()->SendVimKeyForTesting("L");
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return ActiveContents()->GetLastCommittedURL() == second;
  })) << "L did not go forward";
}

// the cached mode is one static for the whole process, so a field
// left focused on the page before made the next page's first f do nothing.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       SwitchingTabsAsksTheArrivingPageWhatModeItIsIn) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();
  AddFocusedInput();
  Window()->DrainVimModeForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return Window()->RouteVimKeyForTesting("f") ==
           content::KeyboardEventProcessingResult::NOT_HANDLED;
  })) << "the focused field never reached the browser's cache";

  // Both tabs are loaded from here on, so no new document arrives and the
  // install path cannot stand in for the switch being tested.
  AddSecondTab();
  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_EQ(tabs->count(), 2);

  tabs->ActivateTabAt(0);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return Window()->RouteVimKeyForTesting("f") ==
           content::KeyboardEventProcessingResult::NOT_HANDLED;
  })) << "coming back to the field it was left in, the tab was read as NORMAL";

  tabs->ActivateTabAt(1);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return Window()->RouteVimKeyForTesting("f") ==
           content::KeyboardEventProcessingResult::HANDLED;
  })) << "the arriving tab inherited INSERT from the one left behind";
}

// every other case here installs vim by hand. This one must not -
// a tab that loaded in the background never met the navigation observer, so
// the switch itself is the only thing that can arm it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       HintsPaintOnATabThatLoadedInTheBackground) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");

  ASSERT_TRUE(chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html"), -1,
      /*foreground=*/false));
  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_EQ(tabs->count(), 2);
  content::WebContents* background = tabs->GetWebContentsAt(1);
  ASSERT_TRUE(content::WaitForLoadStop(background));
  ASSERT_TRUE(content::ExecJs(background, kAddLink));

  tabs->ActivateTabAt(1);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(background->GetPrimaryMainFrame(),
                           "!!window.__atriumVim",
                           content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                           ISOLATED_WORLD_ID_CHROME_INTERNAL)
        .ExtractBool();
  })) << "the switch left the arriving tab with no vim at all";

  Window()->ShowVimHintsForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(background->GetPrimaryMainFrame()).size() == 1u;
  })) << "f counted zero targets and painted nothing";
}

// an ad frame can go away between the count request and its
// reply, and the pass counted it as pending forever. It must finish on the
// frames that are left.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       AFrameThatDiesMidCountDoesNotStallThePass) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/cross_site_iframe_factory.html?a.test(b.test)");
  Window()->InstallVimOnActiveTabForTesting();
  ASSERT_TRUE(
      content::ExecJs(ActiveContents()->GetPrimaryMainFrame(), kAddLink));

  Window()->ShowVimHintsForTesting();
  ASSERT_TRUE(content::ExecJs(ActiveContents()->GetPrimaryMainFrame(),
                              "document.querySelector('iframe').remove()"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(ActiveContents()->GetPrimaryMainFrame()).size() == 1u;
  })) << "the pass never reached paint after a frame left mid-count";
}

// The clicked case.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TypingIntoAClickedFieldIsNotStolen) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  AddFocusedInput();
  Window()->DrainVimModeForTesting();
  Window()->SendVimKeyForTesting("j");

  EXPECT_EQ("veto", content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                    "window.__atriumVim.key('j')",
                                    content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                    ISOLATED_WORLD_ID_CHROME_INTERNAL));
  EXPECT_EQ(0, ScrollYAfterAFewFrames());
  // The scroll not happening is only half the claim. Without this the test
  // also passes when the key reached nothing at all.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents(),
                           "document.querySelector('input').value")
               .ExtractString() == "j";
  }));
}

// The case the drain cannot reach. A page that focuses its own field on
// load produces no input event, so the browser's cached mode stays NORMAL and
// only the page-side veto is correct.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TypingIntoAProgrammaticallyFocusedFieldIsNotStolen) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  AddFocusedInput();
  // Deliberately no DrainVimModeForTesting: the browser still believes NORMAL.
  Window()->SendVimKeyForTesting("j");

  EXPECT_EQ(0, ScrollYAfterAFewFrames());
  // The absence above passes just as well when the key reached nothing, so the
  // character has to turn up where it belongs.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents(),
                           "document.querySelector('input').value")
               .ExtractString() == "j";
  }));
}

// 1Password's inline unlock field is an iframe. The main frame sees
// only the IFRAME element and still answers NORMAL.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TypingIntoAFieldInAChildFrameIsNotStolen) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/cross_site_iframe_factory.html?a.test(b.test)");
  Window()->InstallVimOnActiveTabForTesting();
  content::RenderFrameHost* child =
      content::ChildFrameAt(ActiveContents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  // Without this, the NOT_HANDLED below also passes on a cache that never
  // said NORMAL.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return Window()->RouteVimKeyForTesting("j") ==
           content::KeyboardEventProcessingResult::HANDLED;
  }));

  ASSERT_TRUE(content::ExecJs(child,
                              "const el = document.createElement('input');"
                              "document.body.prepend(el);"
                              "el.focus();"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return ActiveContents()->GetFocusedFrame() == child; }));
  ASSERT_EQ("NORMAL", content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                      "window.__atriumVim.mode()",
                                      content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                      ISOLATED_WORLD_ID_CHROME_INTERNAL));
  Window()->DrainVimModeForTesting();

  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->RouteVimKeyForTesting("j"));
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->RouteVimKeyForTesting("f"));
}

// With the toggle off the window must behave exactly as before.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, TheToggleOffPassesEveryKey) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(false);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->RouteVimKeyForTesting("j"));
}

// The toggle is off by default, so this is what every user who never
// turns vim on sees. An indicator on every page is not "exactly as before".
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TheToggleOffPaintsNoIndicator) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(false);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();
  Window()->DrainVimModeForTesting();

  // The script still installs, so enabling the toggle needs no reload.
  EXPECT_EQ(true, content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                  "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
  EXPECT_EQ(0, content::EvalJs(
                   ActiveContents(),
                   "document.querySelectorAll('.atrium-vim-indicator').length"));
}

// Turning the toggle off must take the indicator off pages already open, not
// wait for a reload.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TurningTheToggleOffClearsALiveIndicator) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/scrollable_page.html");
  Window()->InstallVimOnActiveTabForTesting();
  Window()->DrainVimModeForTesting();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               ActiveContents(),
               "document.querySelectorAll('.atrium-vim-indicator').length")
               .ExtractInt() == 1;
  }));

  atrium::AtriumBrowserWindow::SetVimModeEnabled(false);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               ActiveContents(),
               "document.querySelectorAll('.atrium-vim-indicator').length")
               .ExtractInt() == 0;
  }));
}

// The page parses at once but its onload waits on a slow image, so vim
// being live before "complete" is true only of the parse-time hook. Moving the
// install back to DocumentOnLoadCompletedInPrimaryMainFrame turns this red.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, VimIsLiveBeforeOnload) {
  const GURL url = embedded_test_server()->GetURL("a.test", "/vim-slow.html");
  ASSERT_TRUE(chrome::AddAndReturnTabAt(browser(), url, -1,
                                        /*foreground=*/true));
  ASSERT_TRUE(ActiveContents());

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                           "!!window.__atriumVim",
                           content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                           ISOLATED_WORLD_ID_CHROME_INTERNAL)
        .ExtractBool();
  }));
  EXPECT_NE("complete",
            content::EvalJs(ActiveContents(), "document.readyState"));

  // The absence half above needs this beside it: a document that never
  // finishes loading would satisfy it for the wrong reason.
  ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  EXPECT_EQ("complete",
            content::EvalJs(ActiveContents(), "document.readyState"));
}

// Each frame runs its own instance, so without an allocation pass both
// would start at 'a' and typing it would be ambiguous.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       HintLabelsDoNotRepeatAcrossFrames) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/cross_site_iframe_factory.html?a.test(b.test)");
  Window()->InstallVimOnActiveTabForTesting();

  content::RenderFrameHost* child =
      content::ChildFrameAt(ActiveContents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  // Both frames need a target, or "no repeat" is true for the wrong reason.
  for (content::RenderFrameHost* frame :
       {ActiveContents()->GetPrimaryMainFrame(), child}) {
    ASSERT_TRUE(content::ExecJs(frame, kAddLink));
  }

  Window()->ShowVimHintsForTesting();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(ActiveContents()->GetPrimaryMainFrame()).size() == 1u &&
           HintLabelsIn(child).size() == 1u;
  }));
  EXPECT_NE(HintLabelsIn(ActiveContents()->GetPrimaryMainFrame())[0],
            HintLabelsIn(child)[0]);
}

// The shim models open roots, but only a real renderer proves the walk
// crosses one.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintsReachAnOpenShadowRoot) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  ASSERT_TRUE(content::ExecJs(ActiveContents(),
                              "const host = document.createElement('div');"
                              "document.body.appendChild(host);"
                              "const root = host.attachShadow({mode:'open'});"
                              "const a = document.createElement('a');"
                              "a.href = '/deep'; a.textContent = 'deep';"
                              "root.appendChild(a);"));

  Window()->ShowVimHintsForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(ActiveContents()->GetPrimaryMainFrame()).size() == 1u;
  }));
}

// A content script writes into the main world's DOM. The walk runs in an
// isolated world, and the two share the tree - this is the proof, since the
// walk itself only assumes it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintsReachScriptInjectedDom) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  // The main world, the way an extension content script would append it.
  ASSERT_TRUE(content::ExecJs(ActiveContents(), kAddLink));

  Window()->ShowVimHintsForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(ActiveContents()->GetPrimaryMainFrame()).size() == 1u;
  }));
}

// Atrium's own bar, never Chrome's: CreateFindBar CHECKs on a chromeless
// window, so routing to the wrong one would crash rather than misbehave.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, SlashOpensTheAtriumFindBar) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");

  ASSERT_FALSE(Window()->IsFindBarShowing());
  Window()->RunVimVerbForTesting("/");
  EXPECT_TRUE(Window()->IsFindBarShowing());
}

// N steps the session rather than reopening the bar, so a second n must
// leave exactly one bar up.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, NStepsWithoutReopening) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");

  Window()->RunVimVerbForTesting("/");
  ASSERT_TRUE(Window()->IsFindBarShowing());
  Window()->RunVimVerbForTesting("n");
  Window()->RunVimVerbForTesting("N");
  EXPECT_TRUE(Window()->IsFindBarShowing());
}

// A find field is a text field. The bar is its own WebContents, so which
// delegate sees those keystrokes is the part worth asserting rather than
// reasoning about.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TheFindBarKeepsWorkingWithVimOn) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");

  Window()->RunVimVerbForTesting("/");
  ASSERT_TRUE(Window()->IsFindBarShowing());
  // The page never entered INSERT, so vim still believes it owns j. The bar
  // must not be closed or bypassed by that.
  Window()->RunVimVerbForTesting("n");
  EXPECT_TRUE(Window()->IsFindBarShowing());

  Window()->CloseFindBar();
  EXPECT_FALSE(Window()->IsFindBarShowing());
}

// The node suite proves the page asks for the copy. This proves the
// browser answers, which is the half a clipboard write can silently skip.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, YYCopiesThePageUrl) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();
  Window()->RunVimVerbForTesting("y");
  Window()->RunVimVerbForTesting("y");

  const std::string expected =
      ActiveContents()->GetLastCommittedURL().spec();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    std::string got;
    clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste,
                        /*data_dst=*/std::nullopt,
                        base::BindOnce(
                            [](std::string* out, std::u16string text) {
                              *out = base::UTF16ToUTF8(text);
                            },
                            &got));
    return got == expected;
  }));
  ui::Clipboard::DestroyClipboardForCurrentThread();
}

// gt and gT ride the same native action the cmd-chord does, so
// rebinding that action moves the vim pair with it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, GTAndGCapitalTCycleTabs) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  AddSecondTab();
  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->ActivateTabAt(0);
  Window()->InstallVimOnActiveTabForTesting();

  Window()->RunVimVerbForTesting("g");
  Window()->RunVimVerbForTesting("t");
  EXPECT_TRUE(base::test::RunUntil([&]() { return tabs->active_index() == 1; }));

  Window()->InstallVimOnActiveTabForTesting();
  Window()->RunVimVerbForTesting("g");
  Window()->RunVimVerbForTesting("T");
  EXPECT_TRUE(base::test::RunUntil([&]() { return tabs->active_index() == 0; }));
}

// X closes through the sidebar's own route. chrome::CloseTab would look
// identical here and destroy a favourite the sidebar means to park.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, XClosesTheActiveTab) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  AddSecondTab();
  Window()->InstallVimOnActiveTabForTesting();

  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_EQ(2, tabs->count());
  // The sidebar route finds its tab by id. A count alone would pass just as
  // well if the wrong row went, so both ends are named.
  content::WebContents* doomed = ActiveContents();
  content::WebContents* keep = tabs->GetWebContentsAt(0) == doomed
                                   ? tabs->GetWebContentsAt(1)
                                   : tabs->GetWebContentsAt(0);
  Window()->RunVimVerbForTesting("x");
  EXPECT_TRUE(base::test::RunUntil([&]() { return tabs->count() == 1; }));
  EXPECT_EQ(keep, tabs->GetWebContentsAt(0));
}

// O and t both open the bar. The page only names the verb, so this is the
// half that proves a name with no branch behind it would not silently do
// nothing.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, OOpensTheCommandBar) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  // The window opens with the bar up, so it has to go first - otherwise an o
  // that did nothing at all would pass this.
  Window()->CloseCommandBar();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !Window()->IsCommandBarShowing(); }));
  Window()->RunVimVerbForTesting("o");
  EXPECT_TRUE(
      base::test::RunUntil([&]() { return Window()->IsCommandBarShowing(); }));

  // The page never entered INSERT, so vim still believes it owns j. A bar the
  // user just opened by keystroke must not be closed or bypassed by that.
  Window()->RunVimVerbForTesting("j");
  EXPECT_TRUE(Window()->IsCommandBarShowing());
}

// playbackRate rather than play(): a source-less element still
// carries the rate, so this asserts the verb reached the right video without
// needing a decodable file in the test data tree.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, RateVerbsReachThePrimaryVideo) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  ASSERT_TRUE(content::ExecJs(
      ActiveContents(),
      "const small = document.createElement('video');"
      "small.style.cssText = 'display:block;width:80px;height:45px';"
      "document.body.appendChild(small);"
      "const big = document.createElement('video');"
      "big.id = 'big';"
      "big.style.cssText = 'display:block;width:640px;height:360px';"
      "document.body.appendChild(big);"));
  Window()->InstallVimOnActiveTabForTesting();

  Window()->RunVimVerbForTesting(">");
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents(),
                           "document.getElementById('big').playbackRate",
                           content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                           ISOLATED_WORLD_ID_CHROME_INTERNAL)
               .ExtractDouble() == 2.0;
  }));
  // The small one is the control: a verb that hit every video would pass the
  // assertion above and still be wrong.
  EXPECT_EQ(1.0, content::EvalJs(ActiveContents(),
                                 "document.querySelector('video').playbackRate",
                                 content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                 ISOLATED_WORLD_ID_CHROME_INTERNAL)
                     .ExtractDouble());
}

// A custom player draws its controls as divs with a button role, which
// the V2 walk already matches - so this pins behaviour rather than adding it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintsReachACustomPlayerButton) {
  atrium::AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/title1.html");
  ASSERT_TRUE(content::ExecJs(
      ActiveContents(),
      "const video = document.createElement('video');"
      "video.style.cssText = 'display:block;width:320px;height:180px';"
      "document.body.appendChild(video);"
      "const play = document.createElement('div');"
      "play.setAttribute('role', 'button');"
      "play.textContent = 'Play';"
      "play.style.cssText = 'display:block;width:40px;height:20px';"
      "document.body.appendChild(play);"));
  Window()->InstallVimOnActiveTabForTesting();

  Window()->ShowVimHintsForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return HintLabelsIn(ActiveContents()->GetPrimaryMainFrame()).size() == 1u;
  })) << "the player's own control got no label";
}

// The bracket pairs need two spaces to have anywhere to go, and a space
// is set up before the browser starts rather than through a gesture.
class AtriumVimSpaceVerbBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    // A tab in each space, so the window shows a different one after a cycle.
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"55555555-5555-5555-5555-555555555555\","
        "\"name\":\"First\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://one.test/\",\"title\":\"One\"}]},"
        "{\"id\":\"66666666-6666-6666-6666-666666666666\","
        "\"name\":\"Second\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":1,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://two.test/\",\"title\":\"Two\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"55555555-5555-5555-5555-555555555555\"}}"));
    spaces_env_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    spaces_env_.reset();
  }

  atrium::AtriumBrowserWindow* Window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // The WebContents rather than its URL: each space restores its own row, so
  // the shown object changes without any navigation having to commit.
  void ExpectAnswerMovesTheShownTab(const std::string& answer) {
    content::WebContents* before =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(before);
    EXPECT_TRUE(Window()->RunVimBrowserVerbForTesting(answer))
        << answer << " was not recognised as a browser verb at all";
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return browser()->tab_strip_model()->GetActiveWebContents() != before;
    })) << answer << " was handled but the window still shows the same tab";
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> spaces_env_;
};

IN_PROC_BROWSER_TEST_F(AtriumVimSpaceVerbBrowserTest, NextSpaceCyclesForward) {
  ExpectAnswerMovesTheShownTab("next-space");
}

IN_PROC_BROWSER_TEST_F(AtriumVimSpaceVerbBrowserTest, PreviousSpaceCyclesBack) {
  ExpectAnswerMovesTheShownTab("previous-space");
}

}  // namespace
