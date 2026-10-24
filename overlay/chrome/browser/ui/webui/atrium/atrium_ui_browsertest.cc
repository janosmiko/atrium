// Copyright 2026 The Atrium Authors

#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_feature_check.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

class AtriumUIBrowserTest : public InProcessBrowserTest {
 public:
  AtriumUIBrowserTest() {
    // Upstream warms a tab-search WebUI in every BrowserView
    // (chrome/browser/ui/views/frame/browser_view.cc, kPreloadTopChromeWebUI is
    // enabled by default). In a browsertest UIBridge is not connected, so a
    // real BrowserView is constructed and that preload fires in the USER
    // profile - which would make the count below 2 for reasons that have
    // nothing to do with Atrium.
    features_.InitAndDisableFeature(features::kPreloadTopChromeWebUI);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// A user profile must not be able to reach the chrome layer: it would
// obtain a privileged WebContents in a second BrowserContext, defeating the
// single-renderer property and handing an ordinary tab chrome.send reach.
IN_PROC_BROWSER_TEST_F(AtriumUIBrowserTest, UserProfileCannotReachTheChromeUI) {
  // with the dylib deployed the chrome layer is live and the user
  // profile's navigation is a different case from the one this test names.
  if (atrium::IsAtriumBrowserWindowEnabled() ||
      atrium::AtriumChromeContext::GetIfExists()) {
    GTEST_SKIP() << "the chrome context is already started - the dylib is "
                    "deployed, so there is no dylib-free chrome layer to test";
  }
  const GURL url(chrome::kChromeUIAtriumURL);
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // The navigation is allowed to COMMIT - an error page commits at the URL it
  // was asked for, and RenderProcessFlags::kForTopChromeWebUI is set from the
  // lexical .top-chrome host, not from whether a controller was created. So
  // neither the committed URL nor the process count says anything about
  // privilege, and asserting on them measures the wrong thing.
  //
  // What matters is that no WebUI was instantiated and the frame carries no
  // bindings. WebUIConfigMap::GetConfig returns nullptr when IsWebUIEnabled is
  // false (content/public/browser/webui_config_map.cc:107-110), so there is
  // nothing to bind chrome.send to.
  std::ignore = content::NavigateToURL(contents, url);

  EXPECT_EQ(nullptr, contents->GetWebUI())
      << "a user profile obtained a WebUI controller for the chrome layer";
  EXPECT_TRUE(contents->GetPrimaryMainFrame()->GetEnabledBindings().empty())
      << "a user-profile frame carries WebUI bindings for the chrome layer";
}

// Negative half. Without the dylib connected there is no chrome layer,
// so the reserved context is never started and nothing is enabled. Asserting
// this keeps the browsertest honest: a later regression that started the
// context unconditionally would silently pass the count check below.
IN_PROC_BROWSER_TEST_F(AtriumUIBrowserTest, ContextAbsentWithoutTheDylib) {
  // the VM harness deploys the dylib, so the context starts as soon
  // as a profile is ready - which is during this test, not before it, so the
  // presence of the dylib is the thing to ask. The negative half lives in
  // AtriumChromeContextTest, in a binary nothing starts it in.
  if (atrium::IsAtriumBrowserWindowEnabled() ||
      atrium::AtriumChromeContext::GetIfExists()) {
    GTEST_SKIP() << "the chrome context is already started - the dylib is "
                    "deployed, and the assertion lives in AtriumChromeContextTest";
  }
  EXPECT_EQ(nullptr, atrium::AtriumChromeContext::GetIfExists());
}

// The two halves of the reserved context's whole point, asserted
// together because either alone is satisfiable by a broken build: a context
// that enables nothing would pass a process count of one, and a context that
// spawned a renderer per surface would still report bindings on each.
//
// Process IDENTITY is the assertion rather than a count of top-chrome-flagged
// hosts. The flag is set from the lexical .top-chrome host, so it says what a
// URL was named, not which process served it - and "all chrome surfaces share
// one renderer" is the property that actually bounds Atrium's memory.
IN_PROC_BROWSER_TEST_F(AtriumUIBrowserTest, ReservedContextEnablesOneRenderer) {
  atrium::AtriumChromeContext::Start();
  base::RunLoop ready;
  atrium::AtriumChromeContext::PostWhenReady(ready.QuitClosure());
  ready.Run();

  atrium::AtriumChromeContext* context =
      atrium::AtriumChromeContext::GetIfExists();
  ASSERT_NE(nullptr, context) << "the reserved context never became ready";

  // Distinct routes so a shared process cannot be an artifact of same-document
  // reuse - these are three separate documents.
  std::vector<std::unique_ptr<content::WebContents>> surfaces;
  for (const char* route : {"pill", "sidebar", "commandbar"}) {
    std::unique_ptr<content::WebContents> contents =
        context->CreateChromeContents(
            GURL(std::string(chrome::kChromeUIAtriumURL) + route));
    ASSERT_TRUE(contents) << "no WebContents for route " << route;
    ASSERT_TRUE(content::WaitForLoadStop(contents.get()))
        << "route " << route << " never finished loading";
    surfaces.push_back(std::move(contents));
  }

  content::RenderProcessHost* first =
      surfaces.front()->GetPrimaryMainFrame()->GetProcess();
  ASSERT_NE(nullptr, first);
  for (const std::unique_ptr<content::WebContents>& contents : surfaces) {
    content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();

    // ENABLED: the positive counterpart to UserProfileCannotReachTheChromeUI.
    EXPECT_NE(nullptr, contents->GetWebUI())
        << "no WebUI controller in the reserved context for "
        << contents->GetVisibleURL();
    EXPECT_FALSE(frame->GetEnabledBindings().empty())
        << "no WebUI bindings in the reserved context for "
        << contents->GetVisibleURL();

    // ONE renderer: every chrome surface lands in the same process.
    EXPECT_EQ(first, frame->GetProcess())
        << contents->GetVisibleURL() << " landed in a second renderer";
  }
}

// Unlike the tests above, which push into already-mounted JS, this asserts
// the module graph itself resolved - a missing grd entry once left every
// surface blank while those all stayed green.
IN_PROC_BROWSER_TEST_F(AtriumUIBrowserTest, SidebarRouteRendersNonEmptyAtriumRoot) {
  atrium::AtriumChromeContext::Start();
  base::RunLoop ready;
  atrium::AtriumChromeContext::PostWhenReady(ready.QuitClosure());
  ready.Run();

  atrium::AtriumChromeContext* context =
      atrium::AtriumChromeContext::GetIfExists();
  ASSERT_NE(nullptr, context) << "the reserved context never became ready";

  std::unique_ptr<content::WebContents> contents =
      context->CreateChromeContents(
          GURL(std::string(chrome::kChromeUIAtriumURL) + "sidebar"));
  ASSERT_TRUE(contents) << "no WebContents for route sidebar";

  content::WebContentsConsoleObserver console_observer(contents.get());

  ASSERT_TRUE(content::WaitForLoadStop(contents.get()))
      << "sidebar route never finished loading";

  for (const content::WebContentsConsoleObserver::Message& message :
       console_observer.messages()) {
    EXPECT_NE(blink::mojom::ConsoleMessageLevel::kError, message.log_level)
        << "console error while loading the sidebar route: " << message.message;
  }

  EXPECT_GT(content::EvalJs(contents.get(),
                             "document.getElementById('atrium-root')"
                             ".children.length")
                .ExtractInt(),
            0)
      << "#atrium-root has no children - main() likely never ran because "
         "the module graph failed to resolve";
}
