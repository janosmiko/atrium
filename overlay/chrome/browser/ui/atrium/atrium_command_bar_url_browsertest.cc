// Copyright 2026 The Atrium Authors

#include <string>

#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/bind.h"
#include "base/values.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_autocomplete_bridge.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/search_engines/template_url_service.h"
#include "chrome/test/base/search_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

// Typing a url into the palette offered no row, so Cmd+T could not
// open a page. Drives the bridge directly: synthetic keys into the bar are
// unreliable.
namespace {

class AtriumCommandBarUrlBrowserTest : public InProcessBrowserTest {};

// The kinds the snapshot carries for the query, in order.
std::vector<std::string> KindsIn(const std::string& json) {
  std::vector<std::string> kinds;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return kinds;
  }
  const base::ListValue* matches = parsed->GetDict().FindList("matches");
  if (!matches) {
    return kinds;
  }
  for (const base::Value& match : *matches) {
    if (const std::string* kind = match.GetDict().FindString("kind")) {
      kinds.push_back(*kind);
    }
  }
  return kinds;
}

IN_PROC_BROWSER_TEST_F(AtriumCommandBarUrlBrowserTest,
                       ATypedHostIsOfferedAsSomethingToOpen) {
  // The providers need their engines, and the load is async. Without this the
  // first pass lands against an unloaded service and answers nothing, which is
  // the shape this test exists to tell apart from a broken bridge.
  TemplateURLService* engines =
      TemplateURLServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(engines);

  // Declared BEFORE the bridge, so the bridge dies first. A controller that
  // pushes one more pass during teardown would otherwise call back into a
  // RunLoop that is already gone.
  std::string last;
  base::RunLoop loop;
  atrium::AutocompleteBridge bridge(browser()->profile());
  bridge.SetMatchesCallback(
      base::BindLambdaForTesting([&last, &loop](const std::string& json) {
        last = json;
        if (!KindsIn(json).empty()) {
          loop.Quit();
        }
      }));

  bridge.Query("google.com");
  loop.Run();

  const std::vector<std::string> kinds = KindsIn(last);
  ASSERT_FALSE(kinds.empty())
      << "the palette offered nothing for a typed host, so Cmd+T cannot open "
         "a page: "
      << last;
  EXPECT_NE(std::find(kinds.begin(), kinds.end(), "url"), kinds.end())
      << "no row offers to NAVIGATE to what was typed; kinds were "
      << last;
}

// The half the test above could not see. A pass is not
// final until every provider finishes, and one that never does is ended by
// AutocompleteController's stop timer - which requests a notification and then
// cancels it (autocomplete_controller.cc, Stop). So a bridge that pushes only
// final passes pushes nothing at all for that query, and the palette holds an
// empty list over a result that has rows in it.
//
// The synchronous pass is the one every query gets. It lands inside Query,
// before any provider can have finished, so the assert needs no RunLoop and no
// network: if the page has no rows the moment Query returns, the palette is
// waiting on a pass that may never come.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarUrlBrowserTest,
                       APassThatIsNotFinalStillReachesThePage) {
  TemplateURLService* engines =
      TemplateURLServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(engines);

  std::string last;
  atrium::AutocompleteBridge bridge(browser()->profile());
  bridge.SetMatchesCallback(
      base::BindLambdaForTesting([&last](const std::string& json) {
        last = json;
      }));

  bridge.Query("google.com");

  EXPECT_FALSE(KindsIn(last).empty())
      << "the synchronous pass did not reach the page, so the palette shows "
         "nothing until a final pass that an unfinished provider can withhold "
         "forever: "
      << last;
}

// The reserved chrome context lands ~670ms AFTER the launch auto-open, so that
// Show() ran with no WebView and its focus hand-off was a no-op. Cmd+T on a
// running app has its document already and was never affected.
class AtriumCommandBarFocusBrowserTest : public InProcessBrowserTest {};

// Puts the widget in the state the launch open leaves it in, then drives the
// deferred build. The clear stands in for the open that had nothing to focus.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarFocusBrowserTest,
                       ADocumentArrivingAfterTheOpenStillTakesTheKeyboard) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCommandBar(/*is_startup_open=*/true);
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette) << "no palette document to focus";
  ASSERT_TRUE(content::WaitForLoadStop(palette));

  // The launch shape, both halves: the open had no document to focus, and the
  // parent window took key back during the ~670ms before one arrived.
  window->ClearCommandBarKeyboardForTesting();
  window->Activate();
  ASSERT_FALSE(window->CommandBarHoldsKeyboardForTesting())
      << "precondition: the palette must start without the keyboard, or this "
         "test cannot tell the fix from the open's own hand-off";

  window->RetryCommandBarContentsForTesting();

  EXPECT_TRUE(window->CommandBarHoldsKeyboardForTesting())
      << "the deferred document build left the palette visible but deaf: every "
         "keystroke stops at the widget's root view instead of the query field";

  // Not key status: the app is not frontmost on a test shard, and the reclaim
  // declines to activate an inactive app. The VM scenario types real keys with
  // no click, which is where key status is proven.
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return window->CommandBarFirstResponderIsPageForTesting(); }))
      << "the page's NSView is not the window's first responder, so "
         "keystrokes reach the window but never the document";
}

// The second half of the same hand-off. At the deferred build the renderer has
// no widget yet, so focusing the contents there cannot reach one - the page
// announcing itself is the first moment it can.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarFocusBrowserTest,
                       APageAnnouncingItselfTakesTheKeyboard) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCommandBar(/*is_startup_open=*/true);
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette);
  ASSERT_TRUE(content::WaitForLoadStop(palette));

  window->ClearCommandBarKeyboardForTesting();
  ASSERT_FALSE(window->CommandBarHoldsKeyboardForTesting());

  window->SendToCommandBarHostForTesting("commandBarReady");

  EXPECT_TRUE(window->CommandBarHoldsKeyboardForTesting())
      << "the palette's document came up without the keyboard, so the first "
         "thing typed at launch is lost";
}

}  // namespace
