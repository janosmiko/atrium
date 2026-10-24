// Copyright 2026 The Atrium Authors

#include <set>

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_environment_variable_override.h"
#include "base/scoped_observation.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/input/native_web_keyboard_event.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overview_width.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_seam_line.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/cpp/simple_host_resolver.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/address_list.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_anonymization_key.h"
#include "net/dns/public/host_resolver_results.h"
#include "net/dns/public/resolve_error_info.h"
#include "net/http/http_request_headers.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/ui/atrium/atrium_devtools_bridge.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_extension_bridge.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"
#include "chrome/browser/ui/atrium/atrium_main_extra_parts.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/atrium/atrium_tab_cycle.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "extensions/browser/extension_host_registry.h"
#include "extensions/common/extension.h"
#include "extensions/test/test_extension_dir.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "net/base/host_port_pair.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/common/loader/loader_constants.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/base/hit_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/gfx/geometry/point.h"
#include "url/gurl.h"

// AtriumBrowserWindow::IsOnCurrentWorkspace was a
// not-implemented stub returning false (chrome/browser/ui/atrium/
// atrium_browser_window.cc). Stock BrowserView
// returns true unconditionally on Mac - there is no virtual-desktop notion
// the platform API can answer - and answering false made every caller of
// ProfileBrowserCollection::FindTabbedBrowser() believe no tabbed browser was
// ever open, so each one created a spare window instead of reusing the one
// on screen.
//
// This is an InProcessBrowserTest, not a unit_test: AtriumBrowserWindow's
// constructor builds a real views::Widget and native window (see its ctor in
// atrium_browser_window.cc), which is exactly the "Browser and BrowserWindow
// together" combination chrome/browser/ui/views/frame/test_with_browser_view.h
// warns unit tests away from - "If you want to write a test that has both a
// Browser and a BrowserView, create a browser_test." A normal browser_test
// launch already constructs the real AtriumBrowserWindow via
// BrowserWindow::CreateBrowserWindow (browser_window_factory.cc), so no
// bespoke construction is needed here.
class AtriumBrowserWindowBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    // Core::open() reads the file rather than creating it, so a path with
    // nothing there yet fails to open and the sidebar falls back silently.
    // One space with one real-URL tab: add_marginal needs a space to resolve
    // against, and an empty tab list makes atrium_profile_bootstrap open the
    // window tab-less (its "nothing to restore" rule), leaving
    // GetActiveWebContents() null for every test in this fixture.
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Test\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"Test\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    // atrium_core's cache is a process-global singleton keyed on the path it
    // first opened. Without closing it here, a second test in this binary
    // would keep reading whatever the previous test opened.
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

namespace {

// Serves the query string back as the whole page body - lets a re-anchor
// test put a nonce quote where innerText will actually see it, unlike
// EmbeddedTestServer's built-in /echoall (which echoes the request body).
std::unique_ptr<net::test_server::HttpResponse> ServeMarginaliaTestPage(
    const net::test_server::HttpRequest& request) {
  if (request.GetURL().path() != "/marginalia-test") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(
      "<p>" +
      base::UnescapeURLComponent(
          request.GetURL().query(),
          base::UnescapeRule::NORMAL | base::UnescapeRule::SPACES) +
      "</p>");
  return response;
}

}  // namespace

// the route table is the only place a surface message is answered,
// and the registry is the only place one may be sent from. The two have to
// agree in both directions, and a name in the table twice would make the
// second row silently win.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EverySurfaceRouteIsARegisteredMessageAndRoutedOnce) {
  std::set<std::string_view> registered(std::begin(atrium::kSurfaceMessages),
                                        std::end(atrium::kSurfaceMessages));
  std::set<std::string_view> routed;
  for (const auto& route : atrium::AtriumBrowserWindow::SurfaceRoutes()) {
    EXPECT_TRUE(registered.contains(route.name))
        << route.name << " is routed but not in kSurfaceMessages";
    EXPECT_TRUE(routed.insert(route.name).second)
        << route.name << " is in the route table twice";
  }
  EXPECT_GT(routed.size(), 80u) << "the table lost most of its rows";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       IsOnCurrentWorkspaceReturnsTrue) {
  ASSERT_TRUE(browser()->window())
      << "no BrowserWindow was created for the test browser";

  // Mutation this pins: IsOnCurrentWorkspace reverting to `return false;`
  // (its old body). That mutation flips this EXPECT_TRUE to a
  // failure directly - the method takes no arguments and reads no state, so
  // there is nothing else the assertion could be measuring.
  EXPECT_TRUE(browser()->window()->IsOnCurrentWorkspace())
      << "AtriumBrowserWindow::IsOnCurrentWorkspace returned false - "
      << "ProfileBrowserCollection::FindTabbedBrowser will skip every "
      << "Atrium window again";
}

// No VM UI scenario can cover this:
// it would have to drive Settings, History, and Navigate() with no browser named. No
// verified, scriptable UI trigger reaches any of these three call sites.
// Guessing click coordinates would be exactly the vacuous
// test. These three tests call the same production
// entry points directly, single-window, in-process: cheap, real, and not
// vacuous, at the cost of not proving a human can trigger them from the UI.
//
// All three share one setup: InProcessBrowserTest already has one tabbed
// browser open (`browser()`) before the test body runs, so "no spare window
// created" is "BrowserWindowInterface count did not change" and "the browser
// reused" is "the same Browser* handled the call".

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ChromePagesReusesTabbedBrowser) {
  // chrome/browser/ui/chrome_pages.cc:311 GetOrCreateBrowserForProfile.
  // ShowSettingsSubPageForProfile's GetOrCreateBrowserForProfile call is
  // BUILDFLAG(IS_CHROMEOS)-only, so it never reaches this path on Mac.
  // chrome::ShowSharedTabGroupActivity is the Mac entry point that still
  // goes through GetOrCreateBrowserForProfile.
  size_t browsers_before = GlobalBrowserCollection::GetInstance()->GetSize();

  chrome::ShowSharedTabGroupActivity(browser()->profile());

  // Mutation this pins: if IsOnCurrentWorkspace regresses to `return false;`,
  // FindTabbedBrowser() sees no tabbed browser and GetOrCreateBrowserForProfile
  // falls through to Browser::Create, growing the collection by one.
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before)
      << "chrome_pages.cc's GetOrCreateBrowserForProfile created a spare "
      << "browser instead of reusing the one already open";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ScopedTabbedBrowserDisplayerReusesTabbedBrowser) {
  // chrome/browser/ui/scoped_tabbed_browser_displayer.cc:16, the generic
  // "show me a tabbed window" helper used by History, Bookmark Manager, and
  // other menu-triggered surfaces with no browser of their own to name.
  size_t browsers_before = GlobalBrowserCollection::GetInstance()->GetSize();

  chrome::ScopedTabbedBrowserDisplayer displayer(browser()->profile());

  // Mutation this pins: with IsOnCurrentWorkspace reverted, the displayer's
  // constructor sees FindTabbedBrowser() return null and creates a new
  // Browser via Browser::Create, so `displayer.browser()` would be a
  // different Browser* and the collection size would grow.
  EXPECT_EQ(displayer.browser(), browser())
      << "ScopedTabbedBrowserDisplayer created a new browser instead of "
      << "reusing the existing tabbed one";
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       NavigateWithNoBrowserReusesTabbedBrowser) {
  // chrome/browser/ui/navigator/browser_navigator.cc:137 GetOrCreateBrowser,
  // reached from Navigate() whenever params.browser is left null - the
  // general case for "open this URL, I don't have a window handle".
  size_t browsers_before = GlobalBrowserCollection::GetInstance()->GetSize();

  NavigateParams params(browser()->profile(), GURL("chrome://version"),
                         ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  // params.browser is intentionally left null: naming browser() here would
  // bypass GetOrCreateBrowser entirely and test nothing.

  Navigate(&params);

  // Mutation this pins: with IsOnCurrentWorkspace reverted, GetOrCreateBrowser
  // sees FindTabbedBrowser() return null and calls Browser::Create, so
  // params.browser ends up pointing at a brand-new Browser, not browser().
  EXPECT_EQ(params.browser, browser())
      << "Navigate() with no browser named opened a new window instead of "
      << "reusing the existing tabbed one";
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before);
}

// OpenNotesBesideThisPage, the OnSurfaceMessage notes handlers,
// and the /note command-bar action.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OpenNotesBesideThisPageEntersSplitOnTheNotesRoute) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsSplit());

  window->OpenNotesBesideThisPage();

  // Mutation this pins: an OpenNotesBesideThisPage that fails to build the
  // notes WebContents (or never calls EnterSplit) leaves IsSplit() false.
  ASSERT_TRUE(window->IsSplit())
      << "OpenNotesBesideThisPage did not enter a split";
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes) << "no notes WebContents was attached to the window";
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  EXPECT_EQ(notes->GetLastCommittedURL().path(), "/notes")
      << "the split's secondary pane did not navigate to the notes route";
}

// a capture verb carries no argument, so the gesture is the only
// thing separating a palette click from a message a compromised renderer made
// up - and a capture writes the screen to the clipboard and to Downloads.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ACaptureVerbWithoutAGestureIsDeclined) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->RegionCaptureOverlayOpenForTesting());

  window->SendToCommandBarHostForTesting("captureRegion");
  EXPECT_FALSE(window->RegionCaptureOverlayOpenForTesting())
      << "a capture verb with no user gesture opened the selection overlay";

  // The other direction, so this cannot pass by capture being broken outright:
  // a real press in the palette leaves transient activation on its frame, and
  // ExecJs delivers one the same way.
  window->OpenCommandBar();
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette);
  ASSERT_TRUE(content::WaitForLoadStop(palette));
  ASSERT_TRUE(content::ExecJs(palette->GetPrimaryMainFrame(), "0"));

  window->SendToCommandBarHostForTesting("captureRegion");
  EXPECT_TRUE(window->RegionCaptureOverlayOpenForTesting())
      << "the gate refused a capture the user did ask for";
}

// inspecting an Atrium chrome surface must not kill the browser.
//
// Every chrome surface - the notes pad, the sidebar, the palette - is built by
// AtriumChromeContext in the OTR System Profile. DevToolsUIBindings' constructor
// ends with ThemeServiceFactory::GetForProfile(...)->AddObserver(this), and that
// factory's ProfileSelections names WithRegular, WithGuest and WithAshInternals
// with no WithSystem entry - so the System Profile resolves to kNone,
// GetForProfile returns null, and the call runs on it. Its destructor calls
// RemoveObserver the same way, so teardown crashes too.
//
// Guarding those services one at a time is a losing game - fixing the ThemeService
// deref surfaced a CookieSettings one behind it - so the FRONTEND is refused for a
// surface instead. CDP attachment is untouched, which is what the scenario harness
// and every remote-debugging session actually use.
//
// The surfaces are inspectable targets, so this is reachable: they show up in the
// target list a user can open from chrome://inspect.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       InspectingAChromeSurfaceDoesNotCrash) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->OpenNotesBesideThisPage();
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes) << "no notes contents to inspect";
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  // The precondition that makes this test the one it claims to be: a regular
  // profile would not exercise the missing service at all.
  ASSERT_TRUE(notes->GetBrowserContext()->IsOffTheRecord())
      << "the notes surface is no longer off the record, so this no longer covers "
         "the System Profile path";

  // Asking is the whole test: it used to segfault inside the bindings constructor
  // before any assertion could run. Not the ...Sync helper, which waits for a
  // frontend that is now deliberately never built.
  DevToolsWindow::OpenDevToolsWindow(notes, DevToolsToggleAction::Show(),
                                     DevToolsOpenedByAction::kUnknown);

  EXPECT_FALSE(DevToolsWindow::GetInstanceForInspectedWebContents(notes))
      << "a devtools window was built for a chrome surface, and the profile "
         "behind it has no services for one";
  // Alive, which is the point. A crash would have taken the test process with it.
  EXPECT_TRUE(browser()->tab_strip_model()->GetActiveWebContents());
}

// docked DevTools follows the window PRESENTING the inspected tab.
//
// MaybeUpdateAtriumDockedDevtools resolves through PresentedWindowForBrowser
// rather than browser->window(). The two differ only after an in-window swap: the
// slot keeps its physical presenter window and another controller's tab is shown
// inside it, so that controller's OWN window is not on screen. Docking there would
// leave the user's page unchanged and DevTools nowhere.
//
// Reaching kInWindowSwap took reading ActivateWindow's branch table. It is chosen
// only when the slot already has a LIVE presenter, and this test's own browser is
// not in the presentation table until it is activated - so activating the second
// controller alone gave kFirstActivation, where the target's window becomes both
// the visible content and the presenter and nothing diverges. Both activations are
// needed, in order. The ASSERT_NE below is what proves the state was reached.
//
// WHAT A MUTATION SHOWS, recorded because it is not what I expected. Replacing
// PresentedWindowForBrowser with a plain static_cast of browser->window() leaves
// this passing. The dock is not established by the explicit call below - the stock
// DevTools seam already did it during OpenDevToolsWindowSync, and it passes the
// browser whose window IS the presenter, so both resolutions agree on that call.
// So this test pins the BEHAVIOUR - devtools ends up in the presenting window
// after a real swap - and not the resolution choice, which looks defensive rather
// than load-bearing on the path DevTools actually takes.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DockedDevtoolsFollowsThePresentingWindow) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  // Registers this slot's presenter, which is what makes the NEXT activation a
  // swap rather than a first activation.
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-presenter"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);
  // A switch to that profile's space is what swaps the window. A bare swap
  // leaves the Default space on, and the pane refuses a tab from another space.
  ASSERT_TRUE(first->SelectSpace(
      atrium_core_space_for_profile(-1, "atrium-presenter")));

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  auto* other_own_window =
      static_cast<atrium::AtriumBrowserWindow*>(other_browser->window());
  ASSERT_TRUE(other_own_window);
  // show=false creates the window with no tabs, so give it one to inspect.
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);
  content::WebContents* other_tab =
      other_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(other_tab);
  ASSERT_TRUE(content::WaitForLoadStop(other_tab));

  atrium::AtriumBrowserWindow* presenting =
      atrium::BrowserController::PresentedWindowForBrowser(other_browser);
  ASSERT_TRUE(presenting) << "the slot has no presenter, so there was no swap";
  ASSERT_NE(presenting, other_own_window)
      << "the presenter IS this browser's own window, so nothing diverged and "
         "the two resolutions cannot be told apart";

  ASSERT_TRUE(DevToolsWindowTesting::OpenDevToolsWindowSync(other_tab,
                                                            /*is_docked=*/true));
  atrium::MaybeUpdateAtriumDockedDevtools(other_browser, other_tab);

  EXPECT_EQ(presenting->PanesWithDockedDevtoolsForTesting(), 1)
      << "the presenting window did not take devtools";
  EXPECT_EQ(other_own_window->PanesWithDockedDevtoolsForTesting(), 0)
      << "devtools went to the inspected tab's OWN window, which is off screen";
}

// After a cross-profile in-window swap the presenter's sidebar shows the other
// profile's rows, but that profile's late favicons and titles push through its
// own hidden window. That push was dropped, so a row icon never resolved.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AHiddenWindowPushReachesThePresenterShowingIt) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  // A bare swap with no space switch, because this reads only the sidebar
  // push. A test that needs the tab in a pane must switch space instead.
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-hidden-push"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);
  atrium::BrowserController::ActivateWindow(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  auto* hidden =
      static_cast<atrium::AtriumBrowserWindow*>(other_browser->window());
  ASSERT_TRUE(hidden);
  atrium::AtriumBrowserWindow* presenter =
      atrium::BrowserController::PresentedWindowForBrowser(other_browser);
  ASSERT_TRUE(presenter);
  ASSERT_NE(presenter, hidden) << "no swap, so no hidden window to push from";
  ASSERT_EQ(other_browser,
            atrium::BrowserController::VisibleBrowserForBrowser(browser()))
      << "the slot does not show the other profile";
  ASSERT_TRUE(atrium::AtriumBrowserWindow::HasRealisedWindow(other_browser))
      << "an unrealised target falls back to the presenter's own rows";

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return presenter->SidebarContentsForTesting() != nullptr; }));
  content::WebContents* sidebar = presenter->SidebarContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  auto apply_count = [&]() {
    return content::EvalJs(sidebar, "window.__atriumApplyCount || 0")
        .ExtractInt();
  };
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > 0; }))
      << "the presenter's sidebar never applied a snapshot";
  // The control: a push on the presenter itself lands, so a missing one below
  // is the forward's fault and not a parked apply.
  const int direct = apply_count();
  presenter->PushSidebarSnapshot();
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > direct; }))
      << "even a direct push never applied";
  base::RunLoop().RunUntilIdle();
  const int before = apply_count();

  hidden->PushSidebarSnapshot();

  EXPECT_TRUE(base::test::RunUntil([&]() { return apply_count() > before; }))
      << "the hidden window's push never reached the sidebar on screen";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarSnapshotPushesCoalesceIntoOneBuild) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarContentsForTesting() != nullptr; }));
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  auto apply_count = [&]() {
    return content::EvalJs(sidebar, "window.__atriumApplyCount || 0")
        .ExtractInt();
  };
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > 0; }));
  base::RunLoop().RunUntilIdle();

  window->FlushSidebarSnapshotForTesting();
  const int builds = window->SidebarSnapshotBuildsForTesting();
  const int applies = apply_count();
  for (int i = 0; i < 5; ++i) {
    window->PushSidebarSnapshot();
  }
  EXPECT_EQ(builds, window->SidebarSnapshotBuildsForTesting())
      << "a push built a snapshot inline";
  window->FlushSidebarSnapshotForTesting();
  EXPECT_EQ(builds + 1, window->SidebarSnapshotBuildsForTesting())
      << "five pushes in one turn built more than one snapshot";
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > applies; }));
  EXPECT_EQ(applies + 1, apply_count());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AddingATabBuildsOneSidebarSnapshotAfterTheTurn) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarContentsForTesting() != nullptr; }));
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  auto apply_count = [&]() {
    return content::EvalJs(sidebar, "window.__atriumApplyCount || 0")
        .ExtractInt();
  };
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > 0; }));
  base::RunLoop().RunUntilIdle();
  window->FlushSidebarSnapshotForTesting();
  const int builds = window->SidebarSnapshotBuildsForTesting();
  const int applies = apply_count();

  content::WebContents* tab = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  EXPECT_EQ(builds, window->SidebarSnapshotBuildsForTesting())
      << "adding a tab built a snapshot inline";
  ASSERT_TRUE(base::test::RunUntil([&]() { return apply_count() > applies; }));
  window->FlushSidebarSnapshotForTesting();
  EXPECT_EQ(builds + 1, window->SidebarSnapshotBuildsForTesting())
      << "adding a tab built more than one snapshot";

  ASSERT_TRUE(content::WaitForLoadStop(tab));
  ASSERT_TRUE(content::ExecJs(tab, "document.title = 'Same'"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    window->FlushSidebarSnapshotForTesting();
    return window->SidebarSnapshotBuildsForTesting() > builds + 1;
  }));
  const int after_title = window->SidebarSnapshotBuildsForTesting();
  // Blanking the entry makes UpdateTitleForEntry fire TitleWasSet again while
  // the row already holds "Same".
  content::NavigationEntry* entry = tab->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  entry->SetTitle(std::u16string());
  tab->UpdateTitleForEntry(entry, u"Same");
  base::RunLoop().RunUntilIdle();
  window->FlushSidebarSnapshotForTesting();
  EXPECT_EQ(after_title, window->SidebarSnapshotBuildsForTesting())
      << "the same title twice built a second snapshot";
}

// a browser Atrium does not manage cannot dock devtools.
//
// AtriumCanDockDevtools gates on BrowserController::ControllerForBrowser, a
// registry lookup, so a Browser outside the registry answers false and
// DevToolsWindow keeps its stock undocked behaviour.
//
// WHAT THIS COVERS, stated because a mutation showed the difference: the POSITIVE
// direction, which a gate stuck at false would fail, and the null-pointer guard.
// It does NOT reach the registry gate itself - every Browser a browsertest can
// build is registered, so replacing the whole gate with `return true` still
// passes here. The degraded configuration that does exist is a non-mac build, and
// build/scripts/devtools-degraded-test.mjs asserts those stubs are
// unconditionally false.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OnlyAnAtriumManagedBrowserCanDockDevtools) {
  EXPECT_TRUE(atrium::AtriumCanDockDevtools(browser()))
      << "an Atrium browser cannot dock, so docking is off everywhere";
  EXPECT_FALSE(atrium::AtriumCanDockDevtools(nullptr))
      << "a null browser interface must not claim it can dock";

  // And the update verb is inert for one it cannot dock: no pane is touched, so
  // there is nothing for a stock DevToolsWindow to collide with.
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  atrium::MaybeUpdateAtriumDockedDevtools(
      nullptr, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(window->PanesWithDockedDevtoolsForTesting(), 0)
      << "an ungated update docked devtools into a pane anyway";
}

// docked DevTools composes per-pane with the split view.
//
// The claim is per-PANE, and it only has a wrong answer available in a split:
// one pane must take DevTools and the other must not. A single-pane window
// cannot tell "the inspected pane took it" from "a pane took it".
//
// The INSPECTED page is the ordinary tab, not the notes pad beside it. Notes is
// an Atrium chrome-layer WebContents, which lives in the OTR System Profile -
// and ThemeServiceFactory's ProfileSelections has no WithSystem entry, so
// GetForProfile returns null and DevToolsUIBindings' constructor dereferences it.
// Inspecting notes segfaulted the whole test before it reached an assertion.
//
// Driven through the window rather than through the palette because the assertion
// is about panes, not about how the split was opened. A user CAN open this one:
// typing /note runs QueryCommandBar's interception, which calls
// OpenNotesBesideThisPage.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DockedDevtoolsLandsInTheInspectedPaneOfASplit) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsSplit());

  content::WebContents* inspected =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(inspected);

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit()) << "no split, so there is only one pane to ask";
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  ASSERT_NE(inspected, notes) << "both panes show the same contents";
  ASSERT_EQ(window->PanesWithDockedDevtoolsForTesting(), 0)
      << "a pane is holding devtools before anything asked it to";

  // A real docked DevToolsWindow: UpdateDockedDevtoolsForPane asks
  // DevToolsWindow::GetInTabWebContents for one and does nothing without it, so
  // calling UpdateDockedDevtools alone would prove nothing and read as a bug.
  DevToolsWindow* devtools =
      DevToolsWindowTesting::OpenDevToolsWindowSync(inspected,
                                                    /*is_docked=*/true);
  ASSERT_TRUE(devtools);
  window->UpdateDockedDevtools(inspected);

  EXPECT_TRUE(window->PaneHasDockedDevtoolsForTesting(inspected))
      << "the pane showing the inspected contents did not take devtools";
  EXPECT_EQ(window->PanesWithDockedDevtoolsForTesting(), 1)
      << "another pane took devtools as well, so the dock is not per-pane";
  EXPECT_FALSE(window->PaneHasDockedDevtoolsForTesting(notes))
      << "the pane beside it took devtools too";

  // And the slot empties when DevTools actually closes. Closing it is what makes
  // GetInTabWebContents answer null; passing nullptr to UpdateDockedDevtools
  // would NOT do it, and that is deliberate - null there means "this pane went
  // empty" (ShowTab's null-contents branch), so it only clears a pane whose own
  // contents is null too. An earlier version of this test asserted the wrong
  // thing and read as a product defect.
  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
  window->UpdateDockedDevtools(inspected);
  EXPECT_EQ(window->PanesWithDockedDevtoolsForTesting(), 0)
      << "the pane kept the devtools view after devtools closed";
}

// UpdateDockedDevtools only ever looked at the primary and
// secondary panes, so a third leaf could never take docked devtools.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DockedDevtoolsLandsInTheThirdPaneOfASplit) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_TRUE(content::WaitForLoadStop(third));

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  content::WebContents* third_pane_contents =
      window->PaneContentsForTesting(2);
  ASSERT_TRUE(third_pane_contents) << "the split grew no third leaf";

  DevToolsWindow* devtools = DevToolsWindowTesting::OpenDevToolsWindowSync(
      third_pane_contents, /*is_docked=*/true);
  ASSERT_TRUE(devtools);
  window->UpdateDockedDevtools(third_pane_contents);

  EXPECT_TRUE(window->PaneHasDockedDevtoolsForTesting(third_pane_contents))
      << "the third pane did not take devtools";
  EXPECT_EQ(window->PanesWithDockedDevtoolsForTesting(), 1)
      << "some other pane took devtools instead of the third";
  EXPECT_FALSE(window->PaneHasDockedDevtoolsForTesting(first))
      << "the first pane took devtools meant for the third";

  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
  window->UpdateDockedDevtools(third_pane_contents);
  EXPECT_EQ(window->PanesWithDockedDevtoolsForTesting(), 0)
      << "the third pane kept the devtools view after devtools closed";
}

// ~AtriumBrowserWindow already walks every pane when it
// tears down docked devtools, but nothing could ever get devtools attached
// past the second pane to exercise that loop.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingAWindowWithDevtoolsInTheThirdPaneDoesNotCrash) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(second && third);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_TRUE(content::WaitForLoadStop(third));

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  content::WebContents* third_pane_contents =
      window->PaneContentsForTesting(2);
  ASSERT_TRUE(third_pane_contents) << "the split grew no third leaf";

  DevToolsWindow* devtools = DevToolsWindowTesting::OpenDevToolsWindowSync(
      third_pane_contents, /*is_docked=*/true);
  ASSERT_TRUE(devtools);
  window->UpdateDockedDevtools(third_pane_contents);
  ASSERT_TRUE(window->PaneHasDockedDevtoolsForTesting(third_pane_contents))
      << "the setup did not actually dock devtools in the third pane";

  // A second window, so closing the first does not end the browser process
  // before this test can observe it finish.
  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  CloseBrowserSynchronously(browser());

  // Alive, which is the point. A crash in the destructor's per-pane devtools
  // cleanup would have taken the test process with it.
  EXPECT_TRUE(second_browser->tab_strip_model()->GetActiveWebContents());
}

// split state pushed to window.atriumSplitChanged.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EnterSplitPushesActiveSplitStateToSidebar) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar WebContents to push split state to";
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));

  content::WebContents* primary =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(primary);
  const double primary_id = static_cast<double>(atrium::TabId(primary));

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  const double secondary_id = static_cast<double>(atrium::TabId(notes));

  // Mutation this pins: dropping EnterSplit's PushSplitState call leaves
  // window.__split undefined forever.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "!!window.__split").ExtractBool();
  })) << "EnterSplit never pushed split state to the sidebar";

  EXPECT_TRUE(content::EvalJs(sidebar, "window.__split.active").ExtractBool());
  EXPECT_EQ(primary_id,
            content::EvalJs(sidebar, "window.__split.primaryTabID")
                .ExtractDouble());
  EXPECT_EQ(secondary_id,
            content::EvalJs(sidebar, "window.__split.secondaryTabID")
                .ExtractDouble());
  EXPECT_EQ("sideBySide", content::EvalJs(sidebar, "window.__split.orientation")
                              .ExtractString());
  EXPECT_EQ(0.5,
            content::EvalJs(sidebar, "window.__split.ratio").ExtractDouble());
  EXPECT_EQ("op",
            content::EvalJs(sidebar, "window.__split.reason").ExtractString());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DividerCommitPushesDividerReason) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  // kStacked, not the sideBySide every other test here uses - exercises
  // SplitOrientationToString's other branch.
  window->UpdateSplit(atrium::SplitOrientation::kStacked, 0.5);

  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));
  window->OnDividerResizeCommittedForTesting();

  // Mutation this pins: dropping the constructor's
  // SetDividerCommittedCallback registration (or OnDividerResizeCommitted's
  // body) leaves window.__split unset after a committed drag.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "!!window.__split").ExtractBool();
  })) << "a committed divider drag never pushed split state to the sidebar";
  EXPECT_TRUE(content::EvalJs(sidebar, "window.__split.active").ExtractBool());
  EXPECT_EQ("divider",
            content::EvalJs(sidebar, "window.__split.reason").ExtractString());
  EXPECT_EQ("stacked", content::EvalJs(sidebar, "window.__split.orientation")
                           .ExtractString());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       CollapseSplitPushesInactiveSplitState) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  content::WebContents* primary =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(primary);
  const double primary_id = static_cast<double>(atrium::TabId(primary));

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  const uint64_t secondary_tab_id = atrium::TabId(notes);

  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));
  window->CollapseSplit(secondary_tab_id, "tab_closed");

  // Mutation this pins: dropping FinishCloseSplit's PushSplitState call
  // leaves window.__split unset after a collapse.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "!!window.__split").ExtractBool();
  })) << "CollapseSplit never pushed split state to the sidebar";
  EXPECT_FALSE(content::EvalJs(sidebar, "window.__split.active").ExtractBool());
  EXPECT_EQ(primary_id,
            content::EvalJs(sidebar, "window.__split.primaryTabID")
                .ExtractDouble());
  EXPECT_EQ(0.0, content::EvalJs(sidebar, "window.__split.secondaryTabID")
                     .ExtractDouble());
  EXPECT_EQ("tab_closed",
            content::EvalJs(sidebar, "window.__split.reason").ExtractString());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TabSwapWhileSplitPushesOpReason) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());

  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));

  // A third tab, opened in the same window while split - the Arc-style
  // "replaces the active pane's tab, split survives" path in ShowTab.
  NavigateParams params(browser(),
                        embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));
  ASSERT_TRUE(window->IsSplit()) << "the split did not survive the tab swap";

  // Mutation this pins: dropping ShowTab's split-branch PushSplitState call
  // leaves window.__split unset after swapping a pane's tab.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "!!window.__split").ExtractBool();
  })) << "the tab-swap path never pushed split state to the sidebar";
  EXPECT_TRUE(content::EvalJs(sidebar, "window.__split.active").ExtractBool());
  EXPECT_EQ("op",
            content::EvalJs(sidebar, "window.__split.reason").ExtractString());
}

// a third pane must reach the sidebar in pane order, not
// only the cached primary/secondary pair.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       PushSplitStateListsEveryPaneInOrder) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), third);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  // Mutation this pins: dropping the growth branch's PushSplitState call, or
  // dropping paneTabIDs from the payload, leaves the sidebar with no way to
  // badge the third pane.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, "!!window.__split.paneTabIDs")
        .ExtractBool();
  })) << "the drop that opened a third pane never pushed a paneTabIDs list";
  EXPECT_EQ(3, content::EvalJs(sidebar, "window.__split.paneTabIDs.length")
                   .ExtractInt());
  EXPECT_EQ(static_cast<double>(atrium::TabId(first)),
            content::EvalJs(sidebar, "window.__split.paneTabIDs[0]")
                .ExtractDouble());
  EXPECT_EQ(static_cast<double>(atrium::TabId(third)),
            content::EvalJs(sidebar, "window.__split.paneTabIDs[1]")
                .ExtractDouble());
  EXPECT_EQ(static_cast<double>(atrium::TabId(second)),
            content::EvalJs(sidebar, "window.__split.paneTabIDs[2]")
                .ExtractDouble());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OpenNotesBesideThisPageRendersSavedNoteBody) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const std::string body = "rendered-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  ASSERT_TRUE(atrium_core_set_space_note(-1, body.c_str(), now))
      << "seeding the space note failed - no active space in this profile";

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));

  // Mutation this pins: dropping OnSurfaceMessage's "notesReady" branch (or
  // PushNoteToNotesContents itself) leaves the pad on notes.js's own empty
  // default - atriumSetNote is never called with the seeded body.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(body) != std::string::npos;
  })) << "the notes pad never rendered the seeded note body";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SlashNoteCommandOpensTheNotesSplit) {
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsSplit());

  controller->QueryCommandBar("/note");

  // Mutation this pins: dropping the "/note" interception routes the text to
  // command_bar_bridge_ instead, and IsSplit() stays false.
  EXPECT_TRUE(window->IsSplit())
      << "QueryCommandBar(\"/note\") did not open the notes split";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       NotesBodyChangedSurfaceMessagePersistsToSpaceNote) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process - "
         "OpenAtriumCoreForProfile did not run for the test profile";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // A nonce, not a literal: the spaces store persists across separate runs
  // of this binary, so a fixed literal can pass on a stale on-disk write from
  // an earlier run even with the handler disabled.
  const std::string body = "mutation-proof-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  base::ListValue args;
  args.Append(body);
  window->OnSurfaceMessage("notesBodyChanged", args);

  // Mutation this pins: removing the notesBodyChanged branch in
  // OnSurfaceMessage (or its atrium_core_set_space_note call) leaves the
  // active space's note whatever it was before this call, never `body`.
  char* raw = atrium_core_get_space_note(-1);
  ASSERT_TRUE(raw) << "atrium_core_get_space_note(-1) returned null - no "
                      "active space, or the write above never landed";
  const std::string json(raw);
  atrium_core_string_free(raw);
  EXPECT_NE(json.find(body), std::string::npos)
      << "space note body was not " << body << ": " << json;
}

// no prior test could reach a second space to switch to - this one
// makes it with atrium_core_create_space instead of reasoning about the call
// chain.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SwitchingToACreatedSpaceScopesTheSpaceNote) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process - "
         "OpenAtriumCoreForProfile did not run for the test profile";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_EQ(atrium_core_active_space_index(), 0)
      << "creating a space must not itself move the active one";

  // A nonce, not a literal - see NotesBodyChangedSurfaceMessagePersistsToSpaceNote.
  const std::string body = "mutation-proof-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  base::ListValue args;
  args.Append(body);
  window->OnSurfaceMessage("notesBodyChanged", args);

  // Mutation this pins: SelectSpace returning early (e.g. the index-set call
  // failing) leaves the active index at 0 and the note read below at `body`.
  ASSERT_TRUE(controller->SelectSpace(1))
      << "SelectSpace did not move to the space atrium_core_create_space made";
  EXPECT_EQ(atrium_core_active_space_index(), 1);

  char* raw_note = atrium_core_get_space_note(-1);
  ASSERT_TRUE(raw_note);
  const std::string json(raw_note);
  atrium_core_string_free(raw_note);
  EXPECT_EQ(json.find(body), std::string::npos)
      << "the created space read the previous space's note after a real "
         "switch: " << json;
}

// PushNoteToNotesContents() re-firing on a real tab switch,
// not just once at notesReady.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TabSwitchRePushesNoteIntoOpenPane) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const std::string body_before = "before-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  ASSERT_TRUE(atrium_core_set_space_note(-1, body_before.c_str(), now))
      << "seeding the space note failed - no active space in this profile";

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(body_before) != std::string::npos;
  })) << "the notes pad never rendered the note it opened with";

  // A nonce, not a literal - a stale on-disk write from an earlier run can
  // pass a fixed-literal check even with the re-push disabled.
  const std::string body_after = "after-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  ASSERT_TRUE(atrium_core_set_space_note(-1, body_after.c_str(), now));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                         ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  browser()->tab_strip_model()->ActivateTabAt(1);

  // Mutation this pins: dropping the OnActiveTabChanged -> PushNoteToNotesContents
  // call (or the call itself) leaves the pad on body_before, the note it
  // opened with, since nothing else re-reads the store into the pane.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(body_after) != std::string::npos;
  })) << "switching tabs never re-pushed the updated note into the open pane";
}

// Unlike TabSwitchRePushesNoteIntoOpenPane
// above, active_space_index actually changes here (via create_space).
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SpaceSwitchRePushesNoteAndReturnsIntact) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  const std::string note_space0 = "space0-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  ASSERT_TRUE(atrium_core_set_space_note(0, note_space0.c_str(), now));

  window->OpenNotesBesideThisPage();
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(note_space0) != std::string::npos;
  })) << "the notes pad never rendered space 0's own note";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  const std::string note_space1 = "space1-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  ASSERT_TRUE(atrium_core_set_space_note(1, note_space1.c_str(), now));

  ASSERT_TRUE(controller->SelectSpace(1));
  ASSERT_EQ(atrium_core_active_space_index(), 1);

  // A space with no tabs never fires OnActiveTabChanged on its own (ShowRow
  // has nothing to open); opening a page is the real trigger.
  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  // Mutation this pins: dropping the OnActiveTabChanged -> PushNoteToNotesContents
  // call leaves the pad on space 0's note after a real space switch.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(note_space1) != std::string::npos;
  })) << "switching to a real second space never pushed its own note into "
         "the open pane";
  EXPECT_EQ(content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
                .ExtractString()
                .find(note_space0),
            std::string::npos)
      << "the new space's pane still showed the previous space's note";

  // Switch back: space 0's one seeded tab (from SetUp) is already a live row,
  // so SelectSpace's own ShowRow activates it directly - a second, distinct
  // real trigger, not a repeat of the one above.
  ASSERT_TRUE(controller->SelectSpace(0));
  ASSERT_EQ(atrium_core_active_space_index(), 0);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(note_space0) != std::string::npos;
  })) << "switching back to space 0 did not return its own note to the pane";
}

// the blank card after a space switch and return. Space 0's seeded
// tab is already the strip's ACTIVE tab, so SelectSpace(0)'s ShowRow activates
// something already active - a notification nobody sends.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ReturningToASpaceReattachesItsActiveTab) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab) << "the fixture's seeded tab never restored";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "the pane was not showing the seeded tab before the switch";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);

  // A space with no tabs and no tiles: the branch that empties the pane.
  ASSERT_TRUE(controller->SelectSpace(1));
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), nullptr)
      << "a rowless space left the previous space's page on screen";

  // Mutation this pins: dropping SelectSpace's ShowActiveTabIfPaneIsEmpty call
  // leaves the pane empty here, since the strip's active tab never changed.
  ASSERT_TRUE(controller->SelectSpace(0));
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "returning to the space never put its active tab back in the pane";
  EXPECT_TRUE(window->PaneWebViewShownForTesting())
      << "the pane holds the tab but its WebView is still hidden";
}

// The second trigger: a tab whose URL is a favourite is drawn as a
// TILE and filtered out of the rows, so its own space looks rowless.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ASpaceWhoseOnlyTabIsAFavouriteKeepsShowingIt) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* seeded = tabs->GetActiveWebContents();
  ASSERT_TRUE(seeded) << "the fixture's seeded tab never restored";
  const std::string favorite_url = seeded->GetVisibleURL().spec();

  // The same call the sidebar's "Add to Favorites" gesture makes.
  ASSERT_TRUE(
      atrium_core_favorite_row(-1, /*index=*/0, /*section=*/0, /*slot=*/-1))
      << "favouriting the fixture's seeded tab failed";

  // Favouriting a row does not hide it: the filter reads the CLAIM, and only a
  // click through the tile makes one.
  base::ListValue click;
  click.Append(favorite_url);
  click.Append("");
  const int before_click = tabs->count();
  window->OnSurfaceMessage("sidebarFavoriteClicked", click);
  ASSERT_EQ(before_click + 1, tabs->count())
      << "the tile's click opened no tab, so nothing here is claimed";
  content::WebContents* tab = tabs->GetActiveWebContents();
  ASSERT_TRUE(tab);
  ASSERT_NE(seeded, tab) << "the click adopted the unclaimed original";

  const int seeded_at = tabs->GetIndexOfWebContents(seeded);
  ASSERT_NE(TabStripModel::kNoTab, seeded_at);
  tabs->CloseWebContentsAt(seeded_at, TabCloseTypes::CLOSE_NONE);
  ASSERT_EQ(before_click, tabs->count());

  const std::optional<base::Value> snapshot = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* rows = snapshot->GetDict().FindList("tabs");
  ASSERT_TRUE(rows && rows->empty())
      << "the favourited tab still has a row, so this space is not rowless "
         "and the test is not exercising the tile branch";
  const base::ListValue* tiles = snapshot->GetDict().FindList("favorites");
  ASSERT_TRUE(tiles && tiles->size() == 1u);
  const base::DictValue* tile = (*tiles)[0].GetIfDict();
  const std::string* bound = tile ? tile->FindString("tabId") : nullptr;
  ASSERT_TRUE(bound && !bound->empty())
      << "the tile took no live tab, so the branch under test has nothing to "
         "recognise - a join miss, not the defect";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));

  // Planted, not driven: favourites follow the PROFILE, so the second space
  // draws the same tile and keeps the page too. A space that empties the pane
  // on the way out lives in another profile, which this fixture has none of.
  window->ShowTab(nullptr);
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), nullptr);

  // Mutation this pins: dropping SelectSpace's FavoriteTileHoldsTab branch
  // sends this space down the ShowTab(nullptr) path with its own tab active.
  ASSERT_TRUE(controller->SelectSpace(0));
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "the space drew a tile bound to this tab and still blanked the pane";
  EXPECT_TRUE(window->PaneWebViewShownForTesting())
      << "the pane holds the tab but its WebView is still hidden";
}

// A row that neither activates nor reopens must clear the pane, not leave the
// page it showed before the switch.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest, FailedRowClearsThePane) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab) << "the fixture's seeded tab never restored";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "the pane was not showing the seeded tab before the switch";

  base::ListValue rows;
  base::DictValue row;
  row.Set("id", "G");
  rows.Append(std::move(row));
  base::DictValue root;
  root.Set("tabs", std::move(rows));

  controller->ShowActiveSpaceInPane(window, root,
                                    atrium_core_active_space_index(),
                                    /*allow_cold_open=*/true);
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), nullptr)
      << "a row ShowRow could not open left the previous page on screen";
}

// A row whose tab id is not in the strip must not fall back to the strip's
// active tab.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowTabNotInStripLeavesPaneEmpty) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab) << "the fixture's seeded tab never restored";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "the pane was not showing the seeded tab before the switch";

  base::ListValue rows;
  base::DictValue row;
  row.Set("id", "999999999999");
  rows.Append(std::move(row));
  base::DictValue root;
  root.Set("tabs", std::move(rows));

  controller->ShowActiveSpaceInPane(window, root,
                                    atrium_core_active_space_index(),
                                    /*allow_cold_open=*/true);
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), nullptr)
      << "the pane showed the strip's active tab though it was not the row's";
}

// A cold row whose url OpenSidebarColdTab refuses must clear the pane too.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RefusedColdOpenClearsThePane) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab) << "the fixture's seeded tab never restored";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), tab)
      << "the pane was not showing the seeded tab before the switch";

  base::ListValue rows;
  base::DictValue row;
  row.Set("url", "javascript:alert(1)");
  rows.Append(std::move(row));
  base::DictValue root;
  root.Set("tabs", std::move(rows));

  controller->ShowActiveSpaceInPane(window, root,
                                    atrium_core_active_space_index(),
                                    /*allow_cold_open=*/true);
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), nullptr)
      << "a refused cold open left the previous page on screen";
}

namespace {

// The id of the space at rail position `index`, empty past the rail's end.
std::string SpaceIdAt(int index) {
  char* raw = atrium_core_space_id_at(index);
  if (!raw) {
    return std::string();
  }
  const std::string id(raw);
  atrium_core_string_free(raw);
  return id;
}

// The id of the space holding this tab's row, empty when it has none.
std::string SpaceIdOfTab(content::WebContents* contents) {
  char* raw = atrium_core_space_id_for_tab(atrium::TabId(contents));
  if (!raw) {
    return std::string();
  }
  const std::string id(raw);
  atrium_core_string_free(raw);
  return id;
}

// Whether the active space still draws a row a live tab is showing.
bool ActiveSpaceHasALiveRow(Browser* browser) {
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  if (!model || !model->is_dict()) {
    return false;
  }
  for (const atrium::CycleRow& row :
       atrium::DrawnOrderRows(model->GetDict())) {
    if (!row.tab_id.empty() && row.tab_id != "0") {
      return true;
    }
  }
  return false;
}

// The invariant every space path owes the user, both ways: the pane shows a
// tab of the space the rail is on, and stands empty only while that space has
// nothing live. A tab with no row belongs to no space and proves neither.
void ExpectPaneMatchesActiveSpace(atrium::AtriumBrowserWindow* window,
                                  Browser* browser,
                                  std::string_view step) {
  content::WebContents* shown = window->CurrentlyDisplayedWebContents();
  if (!shown) {
    EXPECT_FALSE(ActiveSpaceHasALiveRow(browser))
        << step << ": the pane is empty while this space has a live row";
    return;
  }
  const std::string tab_space = SpaceIdOfTab(shown);
  if (tab_space.empty()) {
    return;
  }
  EXPECT_EQ(tab_space, SpaceIdAt(atrium_core_active_space_index()))
      << step << ": the pane is showing a page from another space";
}

}  // namespace

// The reported repro: browse a second space, then close its tabs. The strip
// picks the next tab by index - the first space's - and the pane painted it.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingASpacesLastTabDoesNotShowAnotherSpacesPage) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();

  content::WebContents* first = tabs->GetActiveWebContents();
  ASSERT_TRUE(first) << "the fixture's seeded tab never restored";
  ASSERT_EQ(SpaceIdOfTab(first), SpaceIdAt(0));

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));
  ExpectPaneMatchesActiveSpace(window, browser(), "on the empty second space");

  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.org/"), -1, /*foreground=*/true);
  ASSERT_TRUE(second);
  ASSERT_EQ(SpaceIdOfTab(second), SpaceIdAt(1))
      << "the new tab joined no row of the space it was opened in, so the "
         "close below cannot reproduce the drift";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), second);

  const int at = tabs->GetIndexOfWebContents(second);
  ASSERT_NE(TabStripModel::kNoTab, at);
  tabs->CloseWebContentsAt(at, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(first, tabs->GetActiveWebContents())
      << "the strip did not fall back to the other space's tab, so this space "
         "never reached the state under test";

  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), nullptr)
      << "closing the space's last tab left another space's page on screen";
  ExpectPaneMatchesActiveSpace(window, browser(),
                               "after the space's last tab closed");
}

// The strip appends, so a session's tabs interleave across spaces. Refusing
// the foreign neighbour must not blank a space that still has a tab of its own.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingATabFallsBackToItsOwnSpacesOtherTab) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();

  content::WebContents* first = tabs->GetActiveWebContents();
  ASSERT_TRUE(first) << "the fixture's seeded tab never restored";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);

  // The second space's tab lands BETWEEN the first space's two, which is what
  // makes the strip's next-by-index pick the wrong space.
  ASSERT_TRUE(controller->SelectSpace(1));
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.org/"), -1, /*foreground=*/true);
  ASSERT_TRUE(second);
  ASSERT_TRUE(controller->SelectSpace(0));
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.net/"), -1, /*foreground=*/true);
  ASSERT_TRUE(third);
  ASSERT_EQ(SpaceIdOfTab(third), SpaceIdAt(0));
  ASSERT_EQ(3, tabs->count());
  ExpectPaneMatchesActiveSpace(window, browser(), "back on the first space");

  // Without this the strip hands the close back to the opener, which is this
  // space's own tab - the drift under test never happens and the test passes
  // against any tree.
  tabs->ForgetAllOpeners();
  const int at = tabs->GetIndexOfWebContents(third);
  ASSERT_NE(TabStripModel::kNoTab, at);
  tabs->CloseWebContentsAt(at, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(second, tabs->GetActiveWebContents())
      << "the strip did not land on the other space's tab, so the fallback "
         "below is not being exercised";

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->CurrentlyDisplayedWebContents() == first;
  })) << "the close left the space blank instead of falling back to its own "
         "remaining tab";
  ExpectPaneMatchesActiveSpace(window, browser(), "after the interleaved close");
}

// A pinned row survives the close of its tab (mutate::close_tab keeps it
// cold), so a recovery that reopens the space's first row reopens the page
// the user just shut.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingAPinnedTabDoesNotReopenItsColdRow) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();

  content::WebContents* first = tabs->GetActiveWebContents();
  ASSERT_TRUE(first) << "the fixture's seeded tab never restored";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));

  content::WebContents* pinned = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.org/"), -1, /*foreground=*/true);
  ASSERT_TRUE(pinned);
  ASSERT_EQ(SpaceIdOfTab(pinned), SpaceIdAt(1));
  ASSERT_TRUE(atrium_core_pin_tab_by_tab_id(atrium::TabId(pinned), 0))
      << "the core refused the pin, so nothing here is a kept-cold row";
  const int pinned_tabs = tabs->count();

  const int at = tabs->GetIndexOfWebContents(pinned);
  ASSERT_NE(TabStripModel::kNoTab, at);
  tabs->CloseWebContentsAt(at, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(first, tabs->GetActiveWebContents());

  // The recovery is posted, so the reopen would land on this flush.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(ActiveSpaceHasALiveRow(browser()))
      << "the pinned row kept a live tab, so the close never emptied it";
  EXPECT_EQ(pinned_tabs - 1, tabs->count())
      << "closing the pinned tab reopened its cold row";
  ExpectPaneMatchesActiveSpace(window, browser(), "after the pinned close");
}

// Both panes of a split are live and attached, so refusing a drifted tab must
// not take the single-pane null path and blank the partner.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ADriftedActivationLeavesASplitIntact) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();

  content::WebContents* first = tabs->GetActiveWebContents();
  ASSERT_TRUE(first) << "the fixture's seeded tab never restored";
  content::WebContents* partner = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.net/"), -1, /*foreground=*/true);
  ASSERT_TRUE(partner);
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(first));
  window->EnterSplit(partner, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit()) << "the split never opened";

  // The row moves rather than the rail: a space switch would blank the panes
  // through ShowTab(nullptr) before the drifted tab under test arrives.
  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(atrium_core_move_tab_id_to_space(atrium::TabId(partner),
                                               SpaceIdAt(1).c_str()));
  ASSERT_TRUE(atrium::BrowserController::TabIsOutsideActiveSpace(partner))
      << "the split's tabs still belong to the active space, so nothing here "
         "drifts";

  ASSERT_TRUE(window->PaneWebViewForContents(first));
  ASSERT_TRUE(window->PaneWebViewForContents(partner));

  window->ShowTab(partner);
  EXPECT_TRUE(window->IsSplit())
      << "refusing a drifted tab collapsed the split";
  EXPECT_EQ(window->SplitPartnerTabId(atrium::TabId(first)),
            atrium::TabId(partner))
      << "the split lost its partner pane";
  // Either pane can be the active one, so both are checked: the null path
  // detaches whichever it is and covers the other with the empty card.
  EXPECT_TRUE(window->PaneWebViewForContents(first))
      << "refusing a drifted tab detached the split's primary pane";
  EXPECT_TRUE(window->PaneWebViewForContents(partner))
      << "refusing a drifted tab detached the split's secondary pane";
  EXPECT_TRUE(window->PaneWebViewShownForTesting())
      << "refusing a drifted tab hid the split's pane view";
}

// A favourite tile is a tab's row in another shape. The drag used to add a
// tile and leave the row, so the page showed twice in the sidebar.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       FavouritingARowMakesItsTabTheTile) {
  ASSERT_TRUE(atrium_core_is_open());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* seeded = tabs->GetActiveWebContents();
  ASSERT_TRUE(seeded);
  const int before = tabs->count();

  base::ListValue drop;
  drop.Append(0);
  drop.Append("tabs");
  window->OnSurfaceMessage("sidebarRowFavorited", drop);

  EXPECT_EQ("https://example.com/", atrium::FavoriteKeyForTab(seeded))
      << "the new tile did not claim the row's tab";
  EXPECT_EQ(before, tabs->count()) << "the drop opened or closed a tab";
  const std::optional<base::Value> snapshot = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* rows = snapshot->GetDict().FindList("tabs");
  EXPECT_TRUE(rows && rows->empty()) << "the favourited tab kept its row";
  const base::ListValue* tiles = snapshot->GetDict().FindList("favorites");
  ASSERT_TRUE(tiles && tiles->size() == 1u);
  const base::DictValue* tile = (*tiles)[0].GetIfDict();
  const std::string* bound = tile ? tile->FindString("tabId") : nullptr;
  ASSERT_TRUE(bound);
  EXPECT_EQ(base::NumberToString(atrium::TabId(seeded)), *bound)
      << "the tile is bound to a tab other than the row's";

  // The tile goes, and the page it stood for goes with it: a tab with no
  // tile and no row was unreachable from the sidebar.
  base::ListValue remove;
  remove.Append("https://example.com/");
  window->OnSurfaceMessage("sidebarFavoriteRemoved", remove);
  EXPECT_TRUE(base::test::RunUntil([&]() { return tabs->count() == before - 1; }))
      << "removing the favourite left its claimed tab open";
  const std::optional<base::Value> after = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(after && after->is_dict());
  const base::ListValue* left = after->GetDict().FindList("favorites");
  EXPECT_TRUE(left && left->empty()) << "the tile is still drawn";
}

// The slot rides in argument 3, behind the space argument
// that has been optional since it landed - a route that read it one place
// early would take the space for a grid position.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AFavouritedRowLandsOnTheGridSlotItWasDroppedOn) {
  ASSERT_TRUE(atrium_core_is_open());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  // chrome:// pages, because the VM has no network and AddTabAtIndex waits for
  // a load that an unreachable host never finishes.
  ASSERT_TRUE(
      AddTabAtIndex(1, GURL("chrome://version"), ui::PAGE_TRANSITION_TYPED));
  ASSERT_TRUE(
      AddTabAtIndex(2, GURL("chrome://credits"), ui::PAGE_TRANSITION_TYPED));

  for (int row = 0; row < 2; ++row) {
    base::ListValue appended;
    appended.Append(0);
    appended.Append("tabs");
    window->OnSurfaceMessage("sidebarRowFavorited", appended);
  }
  // Read back rather than assumed: which tab sits in row 0 follows the order
  // new tabs join the list.
  const auto tile_urls = [&]() {
    std::vector<std::string> urls;
    const std::optional<base::Value> snapshot = base::JSONReader::Read(
        atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
    if (!snapshot || !snapshot->is_dict()) {
      return urls;
    }
    if (const base::ListValue* tiles =
            snapshot->GetDict().FindList("favorites")) {
      for (const base::Value& tile : *tiles) {
        const std::string* url =
            tile.is_dict() ? tile.GetDict().FindString("url") : nullptr;
        urls.push_back(url ? *url : std::string());
      }
    }
    return urls;
  };
  const std::vector<std::string> before = tile_urls();
  ASSERT_EQ(before.size(), 2u);

  base::ListValue dropped;
  dropped.Append(0);
  dropped.Append("tabs");
  dropped.Append(-1);
  dropped.Append(1);
  window->OnSurfaceMessage("sidebarRowFavorited", dropped);

  const std::vector<std::string> after = tile_urls();
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0], before[0]);
  EXPECT_EQ(after[2], before[1])
      << "the tile appended instead of landing on the slot it was dropped on";
  EXPECT_NE(after[1], before[0]);
  EXPECT_NE(after[1], before[1]);
}

// A favourite dropped on a folder used to resolve to nothing
// at all, so the drag looked like it had gone into a hole. Everything below
// the decision is new: the route, and a core call that has to pin the tab on
// the way, because a folder's members live in the pinned run.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AFavouriteDroppedInAFolderBecomesAPinnedMember) {
  ASSERT_TRUE(atrium_core_is_open());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* seeded = tabs->GetActiveWebContents();
  ASSERT_TRUE(seeded);

  base::ListValue made;
  window->OnSurfaceMessage("sidebarFolderCreated", made);
  std::optional<base::Value> snapshot = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* folders = snapshot->GetDict().FindList("folders");
  ASSERT_TRUE(folders && folders->size() == 1u);
  const base::DictValue* made_folder = (*folders)[0].GetIfDict();
  const std::string* group = made_folder ? made_folder->FindString("id") : nullptr;
  ASSERT_TRUE(group && !group->empty());
  const std::string group_id = *group;

  base::ListValue favourited;
  favourited.Append(0);
  favourited.Append("tabs");
  window->OnSurfaceMessage("sidebarRowFavorited", favourited);
  ASSERT_EQ("https://example.com/", atrium::FavoriteKeyForTab(seeded));

  base::ListValue joined;
  joined.Append("https://example.com/");
  joined.Append(group_id);
  joined.Append(-1);
  window->OnSurfaceMessage("sidebarFavoriteGrouped", joined);

  snapshot = base::JSONReader::Read(atrium::BuildSidebarTabsJSON(browser()),
                                    base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* tiles = snapshot->GetDict().FindList("favorites");
  EXPECT_TRUE(tiles && tiles->empty())
      << "the page joined the folder and stayed a tile as well";
  const base::ListValue* pinned = snapshot->GetDict().FindList("pinned");
  ASSERT_TRUE(pinned && pinned->size() == 1u)
      << "the folder is not drawn in the pinned run, so it has no member";
  const base::DictValue* item = (*pinned)[0].GetIfDict();
  ASSERT_TRUE(item);
  const std::string* kind = item->FindString("kind");
  ASSERT_TRUE(kind);
  EXPECT_EQ("folder", *kind);
  const base::ListValue* members = item->FindList("tabs");
  ASSERT_TRUE(members && members->size() == 1u)
      << "the favourite did not land inside the folder";
  const base::DictValue* member = (*members)[0].GetIfDict();
  const std::string* url = member ? member->FindString("url") : nullptr;
  ASSERT_TRUE(url);
  EXPECT_EQ("https://example.com/", *url);
}

// Atrium never pins a tab in the strip, so Add to Dock must find the row's tab
// by id. A folder above the row shifts the pinned item index off the strip.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AddToDockOpensTheDoorwayForARowBelowAFolder) {
  ASSERT_TRUE(atrium_core_is_open());
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  base::ListValue made;
  window->OnSurfaceMessage("sidebarFolderCreated", made);
  std::optional<base::Value> snapshot = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* folders = snapshot->GetDict().FindList("folders");
  ASSERT_TRUE(folders && folders->size() == 1u);
  const std::string* group = (*folders)[0].GetDict().FindString("id");
  ASSERT_TRUE(group && !group->empty());

  base::ListValue favourited;
  favourited.Append(0);
  favourited.Append("tabs");
  window->OnSurfaceMessage("sidebarRowFavorited", favourited);
  base::ListValue joined;
  joined.Append("https://example.com/");
  joined.Append(*group);
  joined.Append(-1);
  window->OnSurfaceMessage("sidebarFavoriteGrouped", joined);

  auto open_tab = [&](const char* path) {
    content::WebContents* tab = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL(path), -1,
        /*foreground=*/true);
    EXPECT_TRUE(tab && content::WaitForLoadStop(tab));
    return tab;
  };
  // Strip: folder member, first row, unpinned filler, second row. The second
  // row is pinned item 2 but strip index 3, and strip index 2 is the filler.
  content::WebContents* first_row = open_tab("/title1.html");
  content::WebContents* filler = open_tab("/title3.html");
  content::WebContents* row = open_tab("/simple.html");
  ASSERT_TRUE(first_row && filler && row);
  ASSERT_TRUE(atrium_core_pin_tab_by_tab_id(atrium::TabId(first_row), 1));
  ASSERT_TRUE(atrium_core_pin_tab_by_tab_id(atrium::TabId(row), 2));
  const GURL row_url = row->GetLastCommittedURL();
  ASSERT_NE(row_url, filler->GetLastCommittedURL());
  ASSERT_EQ(3, browser()->tab_strip_model()->GetIndexOfWebContents(row));

  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  base::ListValue added;
  added.Append(2);
  window->OnSurfaceMessage("sidebarTabAddedToDock", added);

  ASSERT_EQ(browsers_before + 1,
            GlobalBrowserCollection::GetInstance()->GetSize())
      << "Add to Dock opened no doorway window";
  Browser* doorway = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetBrowserForMigrationOnly() != browser()) {
          doorway = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(doorway);
  content::WebContents* doorway_tab =
      doorway->tab_strip_model()->GetWebContentsAt(0);
  ASSERT_TRUE(doorway_tab);
  EXPECT_EQ(row_url, doorway_tab->GetVisibleURL());
}

// notesListOpen used
// to no-op for a freestanding note (space_index < 0).
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OverviewRailOpensAFreestandingNoteForReal) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsSplit());

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  char* raw = atrium_core_note_create(now);
  ASSERT_TRUE(raw) << "atrium_core_note_create returned null";
  const std::string created(raw);
  atrium_core_string_free(raw);
  const auto parsed = base::JSONReader::Read(created, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const std::string* id = parsed->GetDict().FindString("id");
  ASSERT_TRUE(id) << "the created note's JSON had no id field";

  const std::string body = "freestanding-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  ASSERT_TRUE(atrium_core_note_set_body(id->c_str(), body.c_str(), now));

  // the Overview pane is part of the sidebar's document, so its
  // sections mount on the click below without a surface being opened first.
  content::WebContents* overview = window->SidebarContentsForTesting();
  ASSERT_TRUE(overview);
  ASSERT_TRUE(content::WaitForLoadStop(overview));

  ASSERT_TRUE(content::ExecJs(
      overview, "document.querySelector('[data-ovsec=\"notes\"]').click()"));

  const std::string entry_selector = base::StringPrintf(
      "document.querySelector('[data-notes-open=\"%s\"]')", id->c_str());
  // The row only exists once the section's own notesListReady catch-up push
  // lands - poll rather than assume it beat the section-switch click above.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(overview, "!!" + entry_selector).ExtractBool();
  })) << "the freestanding note never appeared in the Notes rail";
  ASSERT_TRUE(content::ExecJs(overview, entry_selector + ".click()"));

  // Mutation this pins: reverting notesListOpen's space_index<0 branch to a
  // no-op leaves IsSplit() false and the pad on notes.js's own empty default.
  EXPECT_TRUE(base::test::RunUntil([&]() { return window->IsSplit(); }))
      << "clicking the freestanding note's Open button never opened a split";
  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(notes,
                           "document.querySelector('.notes-pad').textContent")
               .ExtractString()
               .find(body) != std::string::npos;
  })) << "the notes pad never rendered the freestanding note's body";
}

// Shelf, Archived, and Snoozed had no catch-up handshake, so
// a mid-session rail click never saw data pushed before the section mounted.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OverviewRailShowsShelfEntryPushedBeforeMount) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  const std::string title = "shelf-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  char* raw = atrium_core_shelf_add("page", title.c_str(),
                                    "https://example.com/", nullptr, now);
  ASSERT_TRUE(raw) << "atrium_core_shelf_add returned null";
  atrium_core_string_free(raw);

  // The entry lands in the store before any Shelf section exists, so only the
  // section's own catch-up push can put the row on screen.
  // the Overview pane is part of the sidebar's document.
  content::WebContents* overview = window->SidebarContentsForTesting();
  ASSERT_TRUE(overview);
  ASSERT_TRUE(content::WaitForLoadStop(overview));

  ASSERT_TRUE(content::ExecJs(
      overview, "document.querySelector('[data-ovsec=\"shelf\"]').click()"));

  // Mutation this pins: dropping shelfListReady's OnSurfaceMessage branch
  // leaves this row missing forever.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(overview, "document.body.textContent")
               .ExtractString()
               .find(title) != std::string::npos;
  })) << "the shelf entry added before Overview opened never reached the "
         "Shelf section after a mid-session rail click";
}

// The fixture's seeded tab is live, and the sweep leaves a live tab's row
// alone, so the row is archived by hand, the way the row menu does it.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OverviewRailShowsArchivedEntryPushedBeforeMount) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  ASSERT_TRUE(atrium_core_archive_tab(-1, /*index=*/0, now))
      << "archiving the fixture's seeded tab failed";

  // the Overview pane is part of the sidebar's document, so its
  // sections mount on the click below without a surface being opened first.
  content::WebContents* overview = window->SidebarContentsForTesting();
  ASSERT_TRUE(overview);
  ASSERT_TRUE(content::WaitForLoadStop(overview));

  ASSERT_TRUE(content::ExecJs(
      overview,
      "document.querySelector('[data-ovsec=\"archived\"]').click()"));

  // "Test" is SetUp's fixture tab title, on a fresh per-test temp file - no
  // stale row can false-pass this.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(overview, "document.body.textContent")
               .ExtractString()
               .find("Test") != std::string::npos;
  })) << "the tab archived before Overview opened never reached the "
         "Archived section after a mid-session rail click";
}

// The archive sweep runs on a timer with no view of the screen. A row bound
// to a live tab must survive it, or the page stays on screen with no row
// and no way back to one.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheArchiveSweepLeavesTheLiveTabsRow) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab) << "the fixture's seeded tab never restored";
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), tab);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  // Far past any threshold, so the row is due on age alone.
  EXPECT_FALSE(atrium_core_sweep_archive("twelveHours", now + 13.0 * 3600.0))
      << "the sweep moved a row while its tab was live and on screen";

  const std::optional<base::Value> snapshot = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(snapshot && snapshot->is_dict());
  const base::ListValue* rows = snapshot->GetDict().FindList("tabs");
  ASSERT_TRUE(rows && rows->size() == 1u)
      << "the live tab lost its row to the sweep";
  EXPECT_EQ(window->CurrentlyDisplayedWebContents(), tab);
}

// same lazy-mount gap as the Shelf test above, for Snoozed.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OverviewRailShowsSnoozedEntryPushedBeforeMount) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  // Snoozes SetUp's one regular-section tab at index 0 in space 0.
  ASSERT_TRUE(atrium_core_snooze_tab(0, 0, 0, now + 3600.0, false, now))
      << "snoozing the fixture's seeded tab failed";

  // the Overview pane is part of the sidebar's document, so its
  // sections mount on the click below without a surface being opened first.
  content::WebContents* overview = window->SidebarContentsForTesting();
  ASSERT_TRUE(overview);
  ASSERT_TRUE(content::WaitForLoadStop(overview));

  ASSERT_TRUE(content::ExecJs(
      overview, "document.querySelector('[data-ovsec=\"snoozed\"]').click()"));

  // Same fixed-title rationale as the Archived test above.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(overview, "document.body.textContent")
               .ExtractString()
               .find("Test") != std::string::npos;
  })) << "the tab snoozed before Overview opened never reached the "
         "Snoozed section after a mid-session rail click";
}

// the wake timer's own path - RunArchiveSweep's shape, but for
// snoozed tabs. A due row reopens into its space and fires the toast+glow,
// and cannot fire twice for the same row.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       WakeDueSnoozedTabReopensAndFiresOnce) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  // wake_at in the past, alert on - due the instant the timer looks. Seeded
  // past rather than slept past, matching the archive sweep tests' own
  // "seed the clock, don't sleep for it" pattern.
  ASSERT_TRUE(atrium_core_snooze_tab(0, 0, 0, now - 60.0, /*alert=*/true, now))
      << "snoozing the fixture's seeded tab failed";

  ASSERT_TRUE(atrium::WakeDueSnoozedTabs(now))
      << "a due snoozed tab was not woken";

  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  EXPECT_EQ(active->GetVisibleURL().spec(), "https://example.com/")
      << "the woken tab did not land on the snoozed row's url";

  // The row is gone from doc.snoozed - a live tab now, not a snoozed one.
  char* raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(raw);
  const std::string overview_after(raw);
  atrium_core_string_free(raw);
  EXPECT_EQ(overview_after, "[]")
      << "the woken row is still listed as snoozed";

  // Mutation this pins: dropping WakeDueSnoozedTabs' snoozed_remove call (or
  // any other exactly-once guard) lets a second tick find the same row due
  // again.
  EXPECT_FALSE(atrium::WakeDueSnoozedTabs(now))
      << "a second tick rewoke a tab that had already come back";
}

// sidebarTabSnoozed used to discard args[1]/args[2] and
// hardcode now+3600.0/false - every snooze landed an hour out with no alert
// regardless of what the popup's four-duration menu and checkbox picked.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarTabSnoozedRoundTripsChosenDurationAndAlert) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  // Six hours out, alert on - neither is the hardcoded now+3600.0/false the
  // handler used to send no matter what args[1]/args[2] carried.
  const double chosen_wake_at = now + 6.0 * 3600.0;

  base::ListValue args;
  args.Append(0);  // SetUp's one regular-section tab, index 0.
  args.Append(chosen_wake_at);
  args.Append(true);
  window->OnSurfaceMessage("sidebarTabSnoozed", args);

  char* raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(raw);
  const std::string overview_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> overview =
      base::JSONReader::Read(overview_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(overview) << "atrium_core_snoozed_overview did not return valid JSON";
  ASSERT_TRUE(overview->is_list());
  ASSERT_EQ(1u, overview->GetList().size())
      << "the snoozed row never reached the core";
  const base::DictValue& row = overview->GetList()[0].GetDict();

  // Mutation this pins: a handler that still hardcodes now+3600.0/false would
  // pass a looser "a snooze happened" check just as well - this checks the
  // SPECIFIC value the popup sent.
  const std::optional<double> stored_wake_at = row.FindDouble("wakeAt");
  ASSERT_TRUE(stored_wake_at);
  EXPECT_NEAR(*stored_wake_at, chosen_wake_at, 0.001)
      << "the stored wake_at is not the duration the popup sent";
  EXPECT_NE(*stored_wake_at, now + 3600.0)
      << "the stored wake_at is the old hardcoded one-hour default";
  const std::optional<bool> stored_alert = row.FindBool("alert");
  ASSERT_TRUE(stored_alert);
  EXPECT_TRUE(*stored_alert) << "the alert flag never reached the core";
}

// wakeAt is renderer wall-clock math, so the core is the
// validation boundary against a stale or clock-skewed value.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarTabSnoozedRejectsAWakeAtInThePast) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;

  base::ListValue args;
  args.Append(0);
  args.Append(now - 60.0);
  args.Append(false);
  window->OnSurfaceMessage("sidebarTabSnoozed", args);

  char* raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(raw);
  const std::string overview_json(raw);
  atrium_core_string_free(raw);
  EXPECT_EQ(overview_json, "[]")
      << "a wakeAt in the past was accepted instead of rejected";
}

// snoozedListWake used to carry only id, url and spaceIndex.
// route-test.mjs already proves the entrypoint renders a toast and glow
// given a title and color - this proves the manual wake reaches it at all.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ManualSnoozedWakeNotifiesTheSidebarWhenAlertIsSet) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const double now =
      base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
  ASSERT_TRUE(atrium_core_snooze_tab(0, 0, 0, now + 3600.0, /*alert=*/true, now))
      << "snoozing the fixture's seeded tab failed";

  char* raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(raw);
  const std::string overview_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> overview =
      base::JSONReader::Read(overview_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(overview && overview->is_list());
  ASSERT_EQ(1u, overview->GetList().size());
  const base::DictValue& row = overview->GetList()[0].GetDict();
  const std::string* id = row.FindString("id");
  const std::string* url = row.FindString("url");
  const std::string* title = row.FindString("title");
  ASSERT_TRUE(id && url && title);
  // The fixture's own tab title/url (SetUp) - neither carries a quote or
  // backslash, so building the chrome.send() call below by substitution is
  // safe without a JSON-escaping helper.
  ASSERT_EQ(std::string::npos, id->find('\''));
  ASSERT_EQ(std::string::npos, url->find('\''));
  ASSERT_EQ(std::string::npos, title->find('\''));

  // A recorder, not the real entrypoint - avoids driving toast-stack.js and
  // edge-glow.js's real rendering from this test, which is what the JS suite
  // already covers.
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar WebContents to notify";
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  ASSERT_TRUE(content::ExecJs(
      sidebar,
      "window.atriumTabSnoozeReturned = (tab) => { window.__wakeArgs = tab; "
      "};"));

  // the Overview pane is part of the sidebar's document.
  content::WebContents* overview_contents = sidebar;

  const std::string send_script = base::StringPrintf(
      "chrome.send('snoozedListWake', ['%s', '%s', 0, '%s', true])",
      id->c_str(), url->c_str(), title->c_str());
  ASSERT_TRUE(content::ExecJs(overview_contents, send_script));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               sidebar,
               "window.__wakeArgs ? window.__wakeArgs.title : ''")
               .ExtractString() == *title;
  })) << "the manual wake never reached window.atriumTabSnoozeReturned with "
         "the row's title";
}

// The ABI setter atrium_settings_window.cc's
// "settingsSpaceArchiveTtlChanged" handler calls, proven end to end - the
// write reaches doc.spaces and a save persists it to spaces.json.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SetSpaceArchiveTtlPersistsToSpacesFile) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";

  ASSERT_TRUE(atrium_core_set_space_archive_ttl(0, "thirtyDays"))
      << "setting the fixture's one space's TTL failed";
  ASSERT_GE(atrium_core_save(), 0) << "the write did not reach disk";

  const base::FilePath spaces_path(
      base::Environment::Create()->GetVar("ATRIUM_SPACES_FILE").value());
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string on_disk;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &on_disk));
  // Mutation this pins: reverting set_space_archive_ttl to a no-op, or
  // dropping the save call above, leaves this substring absent.
  EXPECT_NE(on_disk.find("\"archiveTtl\":\"thirtyDays\""), std::string::npos)
      << "spaces.json does not carry the space's saved TTL";

  // Clearing back to Follow General round-trips too - null clears rather
  // than storing a literal "null" or leaving the old rung in place.
  ASSERT_TRUE(atrium_core_set_space_archive_ttl(0, nullptr));
  ASSERT_GE(atrium_core_save(), 0);
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &on_disk));
  EXPECT_EQ(on_disk.find("archiveTtl"), std::string::npos)
      << "clearing the TTL left the old rung on disk";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarOverviewToggledSurfaceMessageTogglesOverview) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsOverviewShowing());

  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  base::ListValue args;
  window->OnSurfaceMessage("sidebarOverviewToggled", args);
  // Polled: the Overview pages the rail, so the page reports the settle back.
  // Mutation this pins: dropping OnSurfaceMessage's "sidebarOverviewToggled"
  // branch leaves IsOverviewShowing() false forever.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsOverviewShowing();
  })) << "the leftmost space dot's message did not open Overview";

  window->OnSurfaceMessage("sidebarOverviewToggled", args);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsOverviewShowing();
  })) << "a second press did not toggle Overview back closed";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheOverviewPaneWidensTheSidebarAndHandsTheWidthBack) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->SetSidebarWidth(280);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 280; }))
      << "the sidebar never reached the width this test starts from";
  const int32_t saved = atrium_core_sidebar_width();
  ASSERT_EQ(saved, 280);

  base::ListValue parked;
  parked.Append(true);
  window->OnSurfaceMessage("sidebarOverviewPaneActive", parked);
  // Mutation this pins: dropping SetSidebarOverviewOpen's animate call, or the
  // route table entry that reaches it, leaves the pane at 280.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->SidebarWidthForTesting() == atrium::kOverviewSidebarMinWidth;
  })) << "the overview pane never widened the sidebar, it is at "
      << window->SidebarWidthForTesting();

  base::ListValue left;
  left.Append(false);
  window->OnSurfaceMessage("sidebarOverviewPaneActive", left);
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 280; }))
      << "leaving the overview pane did not give the user's width back, the "
         "sidebar is at "
      << window->SidebarWidthForTesting();

  // The half a scenario cannot see until the file is written: routing the
  // widen through SetSidebarWidth instead of the live path would have moved
  // this to the overview minimum.
  EXPECT_EQ(atrium_core_sidebar_width(), saved)
      << "the widen overwrote the user's saved sidebar width";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ReenteringTheOverviewMidCloseKeepsThePreOverviewWidth) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->SetSidebarWidth(200);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 200; }));

  base::ListValue parked;
  parked.Append(true);
  base::ListValue left;
  left.Append(false);
  window->OnSurfaceMessage("sidebarOverviewPaneActive", parked);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->SidebarWidthForTesting() == atrium::kOverviewSidebarMinWidth;
  }));

  // Leave and re-enter before the close animation has landed. The rail does
  // this on every swipe out of the Overview: the settle's first frame reports
  // the space, the next paint still shows an Overview share.
  window->OnSurfaceMessage("sidebarOverviewPaneActive", left);
  window->OnSurfaceMessage("sidebarOverviewPaneActive", parked);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->SidebarWidthForTesting() == atrium::kOverviewSidebarMinWidth;
  }));

  // Mutation this pins: capturing the slot's in-flight width on the re-entry
  // lands the close somewhere between 380 and 200.
  window->OnSurfaceMessage("sidebarOverviewPaneActive", left);
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 200; }))
      << "the second close forgot the pre-overview width, the sidebar is at "
      << window->SidebarWidthForTesting();
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ADoubleClickOnTheGripResetsTheSidebarWidth) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* grip = window->SidebarGripForTesting();
  ASSERT_TRUE(grip);
  constexpr int kDefault = 220;  // kDefaultSidebarWidth, private to the .cc

  window->SetSidebarWidth(280);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 280; }));

  // Views hands a double-click to the view as a second press with the flag
  // set, and every press gets its own release.
  const gfx::Point at = grip->GetLocalBounds().CenterPoint();
  const gfx::Point root = grip->GetBoundsInScreen().CenterPoint();
  auto send = [&](ui::EventType type, int flags) {
    ui::MouseEvent event(type, at, root, ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON | flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    if (type == ui::EventType::kMousePressed) {
      grip->OnMousePressed(event);
    } else {
      grip->OnMouseReleased(event);
    }
  };
  send(ui::EventType::kMousePressed, 0);
  send(ui::EventType::kMouseReleased, 0);
  send(ui::EventType::kMousePressed, ui::EF_IS_DOUBLE_CLICK);
  send(ui::EventType::kMouseReleased, 0);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->SidebarWidthForTesting() == kDefault;
  })) << "the double-click did not reset the sidebar, it is at "
      << window->SidebarWidthForTesting();

  // Mutation this pins: the second release committing the first click's
  // press width would animate the sidebar back to 280 within
  // kSidebarResizeAnimationDuration (150ms).
  base::RunLoop settled;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, settled.QuitClosure(), base::Milliseconds(600));
  settled.Run();
  EXPECT_EQ(window->SidebarWidthForTesting(), kDefault)
      << "the release after the double-click took the width back";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DraggingAfterADoubleClickIsIgnoredUntilRelease) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* grip = window->SidebarGripForTesting();
  ASSERT_TRUE(grip);
  constexpr int kDefault = 220;  // kDefaultSidebarWidth, private to the .cc

  window->SetSidebarWidth(280);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == 280; }));

  const gfx::Point at = grip->GetLocalBounds().CenterPoint();
  const gfx::Point root = grip->GetBoundsInScreen().CenterPoint();
  auto press_or_release = [&](ui::EventType type, int flags) {
    ui::MouseEvent event(type, at, root, ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON | flags,
                         ui::EF_LEFT_MOUSE_BUTTON);
    if (type == ui::EventType::kMousePressed) {
      grip->OnMousePressed(event);
    } else {
      grip->OnMouseReleased(event);
    }
  };
  press_or_release(ui::EventType::kMousePressed, 0);
  press_or_release(ui::EventType::kMouseReleased, 0);
  press_or_release(ui::EventType::kMousePressed, ui::EF_IS_DOUBLE_CLICK);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->SidebarWidthForTesting() == kDefault;
  })) << "the double-click did not reset the sidebar before the drag, it is "
         "at "
      << window->SidebarWidthForTesting();

  ui::MouseEvent drag(ui::EventType::kMouseDragged, at + gfx::Vector2d(100, 0),
                      root + gfx::Vector2d(100, 0), ui::EventTimeForNow(),
                      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  grip->OnMouseDragged(drag);
  EXPECT_EQ(window->SidebarWidthForTesting(), kDefault)
      << "the drag after a double-click walked the sidebar off the reset "
         "width, it is at "
      << window->SidebarWidthForTesting();

  ui::MouseEvent release(ui::EventType::kMouseReleased,
                         at + gfx::Vector2d(100, 0),
                         root + gfx::Vector2d(100, 0), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  grip->OnMouseReleased(release);

  base::RunLoop settled;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, settled.QuitClosure(), base::Milliseconds(600));
  settled.Run();
  EXPECT_EQ(window->SidebarWidthForTesting(), kDefault)
      << "the release after the ignored drag did not stay at the reset "
         "width, it landed at "
      << window->SidebarWidthForTesting();
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EveryGripDragStepMovesThePane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* grip = window->SidebarGripForTesting();
  ASSERT_TRUE(grip);

  const gfx::Point at = grip->GetLocalBounds().CenterPoint();
  const gfx::Point root = grip->GetBoundsInScreen().CenterPoint();
  auto event = [&](ui::EventType type, int dx) {
    return ui::MouseEvent(type, at + gfx::Vector2d(dx, 0),
                          root + gfx::Vector2d(dx, 0), ui::EventTimeForNow(),
                          ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  };
  grip->OnMousePressed(event(ui::EventType::kMousePressed, 0));

  // back to back with no run loop between them, which is what a
  // real drag delivers. Any pacing on the tick - an ack gate, a timer - drops
  // all but the first, and the pane steps behind the pointer.
  std::set<int> widths;
  for (int dx = 4; dx <= 40; dx += 4) {
    grip->OnMouseDragged(event(ui::EventType::kMouseDragged, dx));
    widths.insert(window->SidebarWidthForTesting());
  }
  grip->OnMouseReleased(event(ui::EventType::kMouseReleased, 40));

  EXPECT_EQ(widths.size(), 10u)
      << "the pane took " << widths.size()
      << " of 10 drag steps - something paces the live tick again";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheSelectedSplitPaneWearsTheRingAndCmdWClosesOnlyIt) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* ring = window->ActivePaneRingForTesting();
  ASSERT_TRUE(ring);
  EXPECT_FALSE(ring->GetVisible()) << "a single pane wears no ring";

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second);
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_EQ(window->ActivePaneIndexForTesting(), 0);
  views::test::RunScheduledLayout(ring->parent());
  const gfx::Rect primary_bounds = window->PaneBoundsForTesting(0);
  const gfx::Rect secondary_bounds = window->PaneBoundsForTesting(1);
  ASSERT_FALSE(primary_bounds.IsEmpty());
  ASSERT_FALSE(secondary_bounds.IsEmpty());
  ASSERT_NE(primary_bounds, secondary_bounds);
  // Mutation this pins: dropping UpdateActivePaneRing from Layout leaves the
  // ring hidden or at the wrong card.
  EXPECT_TRUE(ring->GetVisible());
  EXPECT_EQ(ring->bounds(), primary_bounds)
      << "the ring is not on the primary card";

  // A click in the other page gives it focus, and focus is what selects.
  second->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneIndexForTesting() == 1; }))
      << "focus in the secondary pane did not select it";
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), second);
  EXPECT_EQ(ring->bounds(), secondary_bounds)
      << "the ring did not move to the selected card";
  // every pane a split adds is a later child than the ring, so a
  // ring that is not last paints under the card it marks.
  EXPECT_EQ(ring->parent()->children().back(), ring)
      << "a pane paints over the ring, so the selected card shows no stroke";

  chrome::CloseTab(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() { return !window->IsSplit(); }))
      << "Cmd+W did not collapse the split";
  EXPECT_EQ(browser()->tab_strip_model()->count(), 1);
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first)
      << "Cmd+W closed the card that was not selected";
  EXPECT_FALSE(ring->GetVisible()) << "the ring outlived the split";
}

// SplitPartnerTabId only recognizes the first two panes, so
// activating away from a third or fourth used to hide it (still on screen,
// per plan section 1 - both stay attached while split).
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       LeavingAThirdPaneKeepsItVisible) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);
  // The window stays hidden until its sidebar draws, and an active-tab change
  // in a hidden window hides the new tab, which is not what this measures.
  ASSERT_TRUE(base::test::RunUntil([&]() { return window->IsVisible(); }));

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), third);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  // Activate pane 2 (`second`), then back to pane 0 (`first`) - `second` is
  // still a live split pane throughout and must stay VISIBLE.
  second->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneIndexForTesting() == 2; }))
      << "focus in the third pane did not select it";
  first->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneIndexForTesting() == 0; }))
      << "focus in the primary pane did not select it";

  // Mutation this pins: reverting the HIDDEN-skip guard to SplitPartnerTabId
  // blanks `second` the moment it stops being the active pane.
  EXPECT_EQ(content::Visibility::VISIBLE, second->GetVisibility())
      << "leaving the third pane hid a page still on screen in the split";
}

// SetImmersive used to walk primary_pane()/secondary_pane()
// only - leaf 0 and leaf 1. A leaf PAST the second (here index 2) kept its
// windowed (non-zero) corner radius in immersive fullscreen.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ImmersiveAppliesToAThirdPane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  // Leaf 1 is `third` - still secondary_pane(), so the old code already
  // reached it. Leaf 2 (`second`) is the one past secondary_pane()'s reach.
  ASSERT_EQ(window->PaneContentsForTesting(2), second);
  ASSERT_FALSE(window->PaneImmersiveForTesting(2));

  // Mutation this pins: reverting SetImmersive's loop to primary_pane()/
  // secondary_pane() leaves leaf 2 out of the squared radius.
  window->SetPaneContainerImmersiveForTesting(true);
  EXPECT_TRUE(window->PaneImmersiveForTesting(0));
  EXPECT_TRUE(window->PaneImmersiveForTesting(1));
  EXPECT_TRUE(window->PaneImmersiveForTesting(2))
      << "the pane past secondary_pane() kept its windowed radius";

  window->SetPaneContainerImmersiveForTesting(false);
  EXPECT_FALSE(window->PaneImmersiveForTesting(2));
}

// OnContentCardInsetChanged used to re-derive the corner radius
// for primary_pane()/secondary_pane() only, leaving a third pane at its old
// radius after a border-width change.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AnInsetChangeReachesAThirdPane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  atrium::WindowAppearance appearance;
  appearance.border_width = 18;
  atrium::AtriumBrowserWindow::SetWindowAppearance(appearance);

  // Mutation this pins: reverting the loop to primary_pane()/secondary_pane()
  // leaves leaf 2 at the pre-change radius.
  EXPECT_EQ(window->PaneTileCornerRadiusForTesting(2),
            window->PaneTileCornerRadiusForTesting(0))
      << "the pane past secondary_pane() kept its old corner radius";

  appearance.border_width = 10;
  atrium::AtriumBrowserWindow::SetWindowAppearance(appearance);
}

// AssertActiveTabVisible used to assert VISIBLE on
// primary_pane()/secondary_pane() only, leaving a third pane's WebContents
// at whatever visibility it already had.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AssertVisibleReachesAThirdPane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  // Force leaf 2 HIDDEN so the assertion below has something to flip.
  second->WasHidden();
  ASSERT_EQ(content::Visibility::HIDDEN, second->GetVisibility());

  window->AssertActiveTabVisible();

  // Mutation this pins: reverting to the primary/secondary-only branch
  // leaves leaf 2 (`second`) never asserted VISIBLE.
  EXPECT_EQ(content::Visibility::VISIBLE, second->GetVisibility())
      << "the pane past secondary_pane() was never asserted VISIBLE";
}

namespace {

// A pane dropped from the split tree but left as a child view still paints,
// so the tree's own leaf count cannot answer whether a split really closed.
size_t PaneViewCount(views::View* container) {
  size_t count = 0;
  for (views::View* child : container->children()) {
    if (views::AsViewClass<atrium::AtriumPaneView>(child)) {
      ++count;
    }
  }
  return count;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ANestedDividerDragsAndAThreePaneSplitClosesToOnePane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* ring = window->ActivePaneRingForTesting();
  ASSERT_TRUE(ring);
  views::View* container = ring->parent();
  ASSERT_TRUE(container);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  // The drop lands on the active (primary) pane, so the third page joins a
  // NESTED branch under it - the shape the root gutter's ratio cannot
  // describe and ExitSplit used to drop without destroying.
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(PaneViewCount(container), 3u) << "the drop opened no third pane";
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), third)
      << "the dropped page is not under the pane it was dropped on";
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  views::View* nested_divider = window->SplitDividerForTesting(1);
  ASSERT_TRUE(nested_divider) << "the nested branch grew no divider";
  const gfx::Rect top_before = window->PaneBoundsForTesting(0);
  ASSERT_FALSE(top_before.IsEmpty());

  // Past kSplitSnapDistance, or the release snaps the ratio back to centre
  // and proves nothing.
  constexpr int kDragDy = 50;
  const gfx::Point at = nested_divider->GetLocalBounds().CenterPoint();
  const gfx::Point in_screen = nested_divider->GetBoundsInScreen().CenterPoint();
  auto drag_event = [&](ui::EventType type, int dy) {
    return ui::MouseEvent(type, at + gfx::Vector2d(0, dy),
                          in_screen + gfx::Vector2d(0, dy),
                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                          ui::EF_LEFT_MOUSE_BUTTON);
  };
  nested_divider->OnMousePressed(drag_event(ui::EventType::kMousePressed, 0));
  nested_divider->OnMouseDragged(
      drag_event(ui::EventType::kMouseDragged, kDragDy));
  nested_divider->OnMouseReleased(
      drag_event(ui::EventType::kMouseReleased, kDragDy));
  views::test::RunScheduledLayout(container);
  // Mutation this pins: answering a literal 0.5 for a non-root branch throws
  // away the ratio the drag just stored, on the relayout the drag triggers.
  EXPECT_GT(window->PaneBoundsForTesting(0).height(), top_before.height())
      << "the nested divider drag did not move the panes it separates";

  window->CloseSplit(first);
  ASSERT_FALSE(window->IsSplit());
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(PaneViewCount(container), 1u)
      << "a pane view outlived the split it was dropped from";
  EXPECT_EQ(window->PaneContentsForTesting(0), first);
  // a destroyed pane that never detached keeps its claim, and the
  // next holder of that page is then a double claim.
  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(third), 0u)
      << "the third pane's page is still claimed";
  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(second), 0u)
      << "the second pane's page is still claimed";
  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(first), 1u)
      << "the surviving page is claimed by more than one holder";
}

// WebContentsViewMac keeps ONE views_host_ and ViewsHostableAttach
// overwrites it unguarded, so the losing pane aborts on DCHECK(views_host_) at
// its next detach - in the other window, minutes later, naming nothing.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ASecondWindowClaimingATabReleasesTheFirstsPane) {
  auto* first_window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(first_window);
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  ASSERT_EQ(first_window->PaneContentsForTesting(0), tab);

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  auto* second_window =
      static_cast<atrium::AtriumBrowserWindow*>(second_browser->window());
  ASSERT_TRUE(second_window);
  ASSERT_NE(second_window, first_window);

  second_window->ShowTab(tab);
  ASSERT_EQ(second_window->PaneContentsForTesting(0), tab)
      << "the second window refused the tab, so nothing was claimed";

  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(tab), 1u)
      << "two panes hold one page";
  EXPECT_NE(first_window->PaneContentsForTesting(0), tab)
      << "the first window's pane still holds the page the second one took";
}

// EnterSplit's secondary pane never released the tab from another
// window's pane. The final ShowTab(nullptr) is the real proof - it detaches
// the stale pane and aborts on DCHECK(views_host_) if the host was stolen.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EnterSplitReleasesTheOtherWindowsPane) {
  auto* first_window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(first_window);
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  ASSERT_EQ(first_window->PaneContentsForTesting(0), tab);

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  auto* second_window =
      static_cast<atrium::AtriumBrowserWindow*>(second_browser->window());
  ASSERT_TRUE(second_window);
  ASSERT_NE(second_window, first_window);
  content::WebContents* primary = chrome::AddAndReturnTabAt(
      second_browser, GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(primary);
  ASSERT_EQ(second_window->PaneContentsForTesting(0), primary);

  second_window->EnterSplit(tab, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(second_window->IsSplit());
  ASSERT_EQ(second_window->PaneContentsForTesting(1), tab)
      << "the split never claimed the tab, so this test proves nothing";

  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(tab), 1u)
      << "two panes hold one page";
  EXPECT_NE(first_window->PaneContentsForTesting(0), tab)
      << "the first window's pane still holds the page the split just took";

  first_window->ShowTab(nullptr);
}

// same invariant as EnterSplitReleasesTheOtherWindowsPane, reached
// through a cross-window content drop instead of a split.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       InsertPaneFromDropReleasesTheOtherWindowsPane) {
  auto* first_window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(first_window);
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);
  ASSERT_EQ(first_window->PaneContentsForTesting(0), tab);

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(second_browser);
  auto* second_window =
      static_cast<atrium::AtriumBrowserWindow*>(second_browser->window());
  ASSERT_TRUE(second_window);
  ASSERT_NE(second_window, first_window);
  content::WebContents* target = chrome::AddAndReturnTabAt(
      second_browser, GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(target);
  ASSERT_EQ(second_window->PaneContentsForTesting(0), target);

  second_window->InsertPaneFromDrop(tab, atrium::SplitEdge::kBottom);
  ASSERT_TRUE(second_window->IsSplit());
  ASSERT_EQ(second_window->PaneContentsForTesting(1), tab)
      << "the drop never claimed the tab, so this test proves nothing";

  EXPECT_EQ(atrium::AtriumBrowserWindow::ClaimHoldersForTesting(tab), 1u)
      << "two panes hold one page";
  EXPECT_NE(first_window->PaneContentsForTesting(0), tab)
      << "the first window's pane still holds the page the drop just took";

  first_window->ShowTab(nullptr);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       InsertIntoTheFirstLeafDoesNotMoveOtherDividers) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* ring = window->ActivePaneRingForTesting();
  ASSERT_TRUE(ring);
  views::View* container = ring->parent();
  ASSERT_TRUE(container);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* fourth = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third && fourth);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  // Drop lands on the active (primary) pane - first grows a nested branch
  // under it, same shape as ANestedDividerDrag...'s three-pane setup.
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), third);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  views::View* nested_divider = window->SplitDividerForTesting(1);
  ASSERT_TRUE(nested_divider) << "the nested branch grew no divider";
  constexpr int kDragDy = 60;
  const gfx::Point at = nested_divider->GetLocalBounds().CenterPoint();
  const gfx::Point in_screen = nested_divider->GetBoundsInScreen().CenterPoint();
  auto drag_event = [&](ui::EventType type, int dy) {
    return ui::MouseEvent(type, at + gfx::Vector2d(0, dy),
                          in_screen + gfx::Vector2d(0, dy),
                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                          ui::EF_LEFT_MOUSE_BUTTON);
  };
  nested_divider->OnMousePressed(drag_event(ui::EventType::kMousePressed, 0));
  nested_divider->OnMouseDragged(
      drag_event(ui::EventType::kMouseDragged, kDragDy));
  nested_divider->OnMouseReleased(
      drag_event(ui::EventType::kMouseReleased, kDragDy));
  views::test::RunScheduledLayout(container);
  ASSERT_NE(window->PaneBoundsForTesting(0).height(),
           window->PaneBoundsForTesting(1).height())
      << "the drag itself never moved the ratio, so this test proves nothing";

  // Still the active (primary) pane - InsertAtLeaf's kMaxSplitLeaves==4
  // override rebuilds the whole tree into a canonical grid, so the
  // first/third branch just dragged no longer exists as such.
  window->InsertPaneFromDrop(fourth, atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), fourth);
  ASSERT_EQ(window->PaneContentsForTesting(2), third);
  ASSERT_EQ(window->PaneContentsForTesting(3), second);

  // The first/fourth divider is a branch that never existed before this
  // insert - it must start at an even split, not at the ratio dragged above.
  EXPECT_NEAR(window->PaneBoundsForTesting(0).height(),
             window->PaneBoundsForTesting(1).height(), 2)
      << "the dragged ratio leaked onto the new first/fourth divider";

  // The third/second divider is also new and must be unaffected either way.
  EXPECT_NEAR(window->PaneBoundsForTesting(2).height(),
             window->PaneBoundsForTesting(3).height(), 2)
      << "an unrelated divider moved because of the insert";
}

// the route refused every drop onto an existing split, so a
// sidebar drag could never open a third pane.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ASidebarDropOnASplitOpensAThirdAndAFourthPane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* ring = window->ActivePaneRingForTesting();
  ASSERT_TRUE(ring);
  views::View* container = ring->parent();
  ASSERT_TRUE(container);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* fourth = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* fifth = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third && fourth && fifth);

  const std::string first_id = base::NumberToString(atrium::TabId(first));
  // Every drop lands on the pane showing `first`, which stays the active one.
  auto drop_on_first = [&](content::WebContents* dragged) {
    base::ListValue args;
    args.Append(base::NumberToString(atrium::TabId(dragged)));
    args.Append(first_id);
    args.Append("bottom");
    window->OnSurface_SidebarTabSplitForTesting(args);
    views::test::RunScheduledLayout(container);
  };

  drop_on_first(second);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_EQ(PaneViewCount(container), 2u) << "the first drop opened no split";

  drop_on_first(third);
  ASSERT_EQ(PaneViewCount(container), 3u)
      << "the drop onto the split opened no third pane";
  EXPECT_EQ(window->PaneContentsForTesting(1), third)
      << "the third page is not under the pane it was dropped on";
  EXPECT_EQ(window->PaneFocusSubscriptionCountForTesting(), 3u)
      << "the third pane takes no activation from a click in its page";

  drop_on_first(fourth);
  ASSERT_EQ(PaneViewCount(container), 4u)
      << "the drop onto the three-pane split opened no fourth pane";
  EXPECT_EQ(window->PaneContentsForTesting(1), fourth);
  EXPECT_EQ(window->PaneFocusSubscriptionCountForTesting(), 4u)
      << "the fourth pane takes no activation from a click in its page";

  drop_on_first(fifth);
  EXPECT_EQ(PaneViewCount(container), 4u)
      << "a fifth pane grew past kMaxSplitLeaves";
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_NE(window->PaneContentsForTesting(i), fifth)
        << "the refused drop still moved its page into pane " << i;
  }

  // Turning a four-pane split rebuilt it from leaves 0 and 1, which orphaned
  // the other two panes and left one gutter against four panes' worth of
  // state - a layout CHECK away from killing the browser.
  window->UpdateSplit(atrium::SplitOrientation::kStacked, 0.5);
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(PaneViewCount(container), 4u)
      << "changing the orientation dropped panes";
  EXPECT_EQ(window->PaneContentsForTesting(0), first)
      << "the transpose lost the leaf order";
}

// a third pane could never be the selected one (the 0/1 clamp),
// and its close left the pane behind, empty - CollapseSplit reads only the
// two cached ids.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       CmdWClosesOnlyTheSelectedPaneOfAThreePaneSplit) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* ring = window->ActivePaneRingForTesting();
  ASSERT_TRUE(ring);
  views::View* container = ring->parent();
  ASSERT_TRUE(container);

  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);
  // The window stays hidden until its sidebar draws, and the show resizes the
  // panes, so a union taken before it never matches one taken after.
  ASSERT_TRUE(base::test::RunUntil([&]() { return window->IsVisible(); }));

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(PaneViewCount(container), 3u) << "the drop opened no third pane";
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), third);
  ASSERT_EQ(window->PaneContentsForTesting(2), second);

  gfx::Rect filled = window->PaneBoundsForTesting(0);
  filled.Union(window->PaneBoundsForTesting(1));
  filled.Union(window->PaneBoundsForTesting(2));
  ASSERT_FALSE(filled.IsEmpty());

  // A click in the third pane's page gives it focus, and focus is what
  // selects. Mutation this pins: the old 0/1 clamp answered 0 here.
  second->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneIndexForTesting() == 2; }))
      << "focus in the third pane did not select it";
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(ring->bounds(), window->PaneBoundsForTesting(2))
      << "the ring is not on the selected card";
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), second);

  chrome::CloseTab(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return PaneViewCount(container) == 2u; }))
      << "Cmd+W left the closed pane behind";
  views::test::RunScheduledLayout(container);
  EXPECT_TRUE(window->IsSplit()) << "closing one of three panes killed the "
                                    "whole split";
  EXPECT_EQ(window->PaneFocusSubscriptionCountForTesting(), 2u)
      << "the closed pane left its focus subscription behind, so a split "
         "opened and closed in a loop grows the vector forever";
  EXPECT_EQ(browser()->tab_strip_model()->count(), 2);
  EXPECT_EQ(window->PaneContentsForTesting(0), first);
  EXPECT_EQ(window->PaneContentsForTesting(1), third);

  gfx::Rect refilled = window->PaneBoundsForTesting(0);
  refilled.Union(window->PaneBoundsForTesting(1));
  EXPECT_EQ(refilled, filled)
      << "the closed pane's space never went back to its siblings";
  EXPECT_FALSE(window->PaneBoundsForTesting(0).Intersects(
      window->PaneBoundsForTesting(1)))
      << "the two surviving cards overlap";
  EXPECT_TRUE(ring->GetVisible());

  // Down to the last two panes, which collapse to the survivor as ever.
  chrome::CloseTab(browser());
  ASSERT_TRUE(base::test::RunUntil([&]() { return !window->IsSplit(); }))
      << "Cmd+W did not collapse the last two panes";
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(PaneViewCount(container), 1u) << "a pane outlived the split";
  EXPECT_EQ(browser()->tab_strip_model()->count(), 1);
  EXPECT_FALSE(ring->GetVisible()) << "the ring outlived the split";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       LosingCaptureMidDragRestoresThePrePressRatio) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(second);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_DOUBLE_EQ(window->SplitRatioForTesting(), 0.5);

  views::View* divider = window->SplitDividerForTesting(0);
  ASSERT_TRUE(divider);
  views::test::RunScheduledLayout(divider->parent());
  const gfx::Point at = divider->GetLocalBounds().CenterPoint();
  const gfx::Point in_screen = divider->GetBoundsInScreen().CenterPoint();
  auto drag_event = [&](ui::EventType type, int dx) {
    return ui::MouseEvent(type, at + gfx::Vector2d(dx, 0),
                          in_screen + gfx::Vector2d(dx, 0),
                          ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                          ui::EF_LEFT_MOUSE_BUTTON);
  };
  divider->OnMousePressed(drag_event(ui::EventType::kMousePressed, 0));
  divider->OnMouseDragged(drag_event(ui::EventType::kMouseDragged, 100));
  ASSERT_NE(window->SplitRatioForTesting(), 0.5)
      << "the drag itself never moved the ratio, so a capture loss proves "
         "nothing";

  // No OnMouseReleased - a deactivation or a menu during the press steals
  // capture instead.
  divider->OnMouseCaptureLost();
  EXPECT_DOUBLE_EQ(window->SplitRatioForTesting(), 0.5)
      << "losing capture mid-drag did not restore the pre-press ratio, it "
         "landed at "
      << window->SplitRatioForTesting();
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ARowDroppedOnAnEdgeSplitsOnThatSide) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  // Foreground, so the pane shows the dragged row's page and the drop has to
  // bring the row it landed on up as the pane it splits against.
  content::WebContents* dragged = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(target && dragged);
  const std::string target_id = base::NumberToString(atrium::TabId(target));
  const std::string dragged_id = base::NumberToString(atrium::TabId(dragged));

  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  ASSERT_TRUE(content::ExecJs(
      sidebar, "window.atriumSplitChanged = (s) => { window.__split = s; };"));

  struct Case {
    const char* side;
    size_t dragged_pane;  // Leaf index the dropped page must land at.
    atrium::SplitOrientation orientation;
  };
  using Orientation = atrium::SplitOrientation;
  for (const Case& c : {Case{"left", 0, Orientation::kSideBySide},
                        Case{"right", 1, Orientation::kSideBySide},
                        Case{"top", 0, Orientation::kStacked},
                        Case{"bottom", 1, Orientation::kStacked}}) {
    base::ListValue drop;
    drop.Append(dragged_id);
    drop.Append(target_id);
    drop.Append(c.side);
    window->OnSurfaceMessage("sidebarTabSplit", drop);

    ASSERT_TRUE(window->IsSplit()) << c.side << " opened no split";
    EXPECT_EQ(window->split_orientation(), c.orientation) << c.side;
    EXPECT_EQ(window->PaneContentsForTesting(c.dragged_pane), dragged)
        << "the dropped page is on the wrong side for " << c.side;
    EXPECT_EQ(window->PaneContentsForTesting(c.dragged_pane == 0 ? 1 : 0),
              target)
        << "the row dropped on is on the wrong side for " << c.side;

    // Without this the sidebar never learns a drop opened a split, keeps
    // offering the edge quarters, and the next drop is refused in silence.
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return content::EvalJs(sidebar,
                             "!!(window.__split && window.__split.active)")
          .ExtractBool();
    })) << "the drop never pushed split state to the sidebar for " << c.side;
    EXPECT_EQ(static_cast<double>(atrium::TabId(target)),
              content::EvalJs(sidebar, "window.__split.primaryTabID")
                  .ExtractDouble());
    EXPECT_EQ(static_cast<double>(atrium::TabId(dragged)),
              content::EvalJs(sidebar, "window.__split.secondaryTabID")
                  .ExtractDouble());

    window->CloseSplit(target);
    ASSERT_FALSE(window->IsSplit());
  }
}

// The ids come from the renderer, which is free to name a tab the rows it drew
// never contained.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ARowDroppedOnAnEdgeRefusesATabFromAnotherSpace) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* first = tabs->GetActiveWebContents();
  ASSERT_TRUE(first) << "the fixture's seeded tab never restored";
  ASSERT_EQ(SpaceIdOfTab(first), SpaceIdAt(0));

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("https://example.org/"), -1, /*foreground=*/true);
  ASSERT_TRUE(second);
  ASSERT_EQ(SpaceIdOfTab(second), SpaceIdAt(1))
      << "the new tab joined no row of the space it was opened in";
  ASSERT_EQ(window->PaneContentsForTesting(0), second);

  base::ListValue drop;
  drop.Append(base::NumberToString(atrium::TabId(second)));
  drop.Append(base::NumberToString(atrium::TabId(first)));
  drop.Append("right");
  window->OnSurfaceMessage("sidebarTabSplit", drop);

  EXPECT_FALSE(window->IsSplit())
      << "a target tab from another space opened a split";
  EXPECT_EQ(window->PaneContentsForTesting(0), second)
      << "the pane stopped showing the active space's own tab";
}

// The left and top edges splice the new pane BEFORE its target, so leaves 0
// and 1 stop meaning the panes the two cached tab ids name. Every other test
// here drops on a bottom or right edge, where the renumbering never happens.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ARowDroppedOnTheLeftEdgeOfASplitTakesTheFirstLeaf) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* first = tabs->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  ASSERT_EQ(window->PaneContentsForTesting(1), second);

  base::ListValue drop;
  drop.Append(base::NumberToString(atrium::TabId(third)));
  drop.Append(base::NumberToString(atrium::TabId(first)));
  drop.Append("left");
  window->OnSurfaceMessage("sidebarTabSplit", drop);

  EXPECT_EQ(window->PaneContentsForTesting(0), third)
      << "a left drop landed after its target instead of before it";
  EXPECT_EQ(window->PaneContentsForTesting(1), first);
  EXPECT_EQ(window->PaneContentsForTesting(2), second);
  EXPECT_EQ(window->PaneFocusSubscriptionCountForTesting(), 3u);

  // Focus is what selects, and it reaches the pane through the same cached
  // ids the insert just renumbered.
  second->Focus();
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneIndexForTesting() == 2; }))
      << "the cached ids still name the panes the drop moved";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ASidebarWiderThanTheOverviewMinimumDoesNotMove) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  constexpr int kWide = 396;  // Over the overview minimum, under the 400 clamp.
  static_assert(kWide > atrium::kOverviewSidebarMinWidth);
  window->SetSidebarWidth(kWide);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->SidebarWidthForTesting() == kWide; }));

  base::ListValue parked;
  parked.Append(true);
  window->OnSurfaceMessage("sidebarOverviewPaneActive", parked);

  // A width that did change would be gone within kSidebarResizeAnimationDuration
  // (150ms), so this outlasts the animation the assertion is about.
  base::RunLoop settled;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, settled.QuitClosure(), base::Milliseconds(600));
  settled.Run();

  EXPECT_EQ(window->SidebarWidthForTesting(), kWide)
      << "opening the overview narrowed a sidebar that was already wide enough";
}

// the ai-panel's native push surface - PushAiPanelOnRight,
// PushAiPanelInk, SetAiPanelOpen/SetAiPanelWidth, and PushAiPanelTarget
// (shared with PushNoteToNotesContents via OnActiveTabChanged, above).

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarPositionPushesOppositeEdgeToAiPanel) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel) << "BuildHtmlAiPanel never built the ai-panel WebContents";
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  window->SetSidebarPosition(/*on_right=*/true);

  // Mutation this pins: dropping PushAiPanelOnRight from SetSidebarPosition
  // leaves the panel on its install-time default (aionleft), so this never
  // flips to aionright when the sidebar docks right.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aionleft')")
               .ExtractBool();
  })) << "sidebar docked right, but the ai-panel did not flip to the left "
         "edge (aionleft)";
}

// the panel takes the window's LEFT edge when the sidebar moves
// right, and the native traffic lights sit over that corner. The header's
// close button ended up behind the green one and neither took a click.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheAiPanelHeaderClearsTheTrafficLightsOnTheLeftEdge) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  window->ToggleAiPanel();

  // The header only has a box once the panel is open - .aipanel is display:none
  // until then, and a closed panel reports 0 for every edge.
  constexpr char kHeaderTop[] =
      "(() => { const h = document.querySelector('.aiphead');"
      " return h ? Math.round(h.getBoundingClientRect().top) : -1; })()";
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kHeaderTop).ExtractInt() > 0;
  })) << "the ai panel never opened, so there is no header to measure";

  // The sidebar starts on the left, so the panel is on the right where
  // nothing overlaps. A constant reserve would show up as a dead band here.
  const int right_docked_top = content::EvalJs(panel, kHeaderTop).ExtractInt();
  EXPECT_LT(right_docked_top, 40)
      << "the right-docked panel gained a traffic-light reserve it does not "
         "need - header top " << right_docked_top;

  window->SetSidebarPosition(/*on_right=*/true);

  // The reserve appears on the flip itself, with no reload.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kHeaderTop).ExtractInt() >= 40;
  })) << "the ai panel moved to the left edge and its header still sits in "
         "the traffic-light band - header top "
      << content::EvalJs(panel, kHeaderTop).ExtractInt();
}

// the two grips have to read as one control. Nothing ever defined
// --accent, so the page-drawn hairline took its white fallback while the
// native seam pill beside it wore the theme's colour.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheAiPanelGripWearsTheSeamAccentBesideTheCard) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  window->ToggleAiPanel();

  constexpr char kGripWidth[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " return g ? Math.round(g.getBoundingClientRect().width) : 0; })()";
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return content::EvalJs(panel, kGripWidth).ExtractInt() > 0; }))
      << "the ai panel never opened, so the grip has no box to measure";

  // The hover colour, read off the ::after that actually paints it. A
  // theme accent is opaque, and the old fallback was rgba(255,255,255,.55) -
  // the alpha alone separates them, whichever theme is active.
  constexpr char kHairlineColor[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " if (!g) { return 'no grip'; }"
      " return getComputedStyle(g, '::after').backgroundColor; })()";
  const std::string at_rest =
      content::EvalJs(panel, kHairlineColor).ExtractString();
  EXPECT_NE(at_rest.find("rgba(0, 0, 0, 0)"), std::string::npos)
      << "the grip paints at rest, where the native seam shows nothing - got "
      << at_rest;

  // .on is the class the drag adds, and it shares the hover rule. Waited for,
  // not read at once: the hairline eases in over .15s and a read on the same
  // turn still answers the transparent it started from.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.querySelector('.aipresize').classList.add('on')"));
  // Settled means opaque: a theme accent has no alpha, so an rgba() reading is
  // either the transition still easing in or the old translucent fallback.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kHairlineColor)
               .ExtractString()
               .rfind("rgb(", 0) == 0;
  })) << "the drag never settled on an opaque colour - got "
      << content::EvalJs(panel, kHairlineColor).ExtractString();
  const std::string dragging =
      content::EvalJs(panel, kHairlineColor).ExtractString();
  EXPECT_EQ(dragging.find("255, 255, 255"), std::string::npos)
      << "the dragging grip is still the white --accent fallback rather than "
         "the seam accent - got "
      << dragging;

  // The mark, not the band: the band deliberately reaches back over the
  // card for grab area, the way kSidebarGripReach reaches over the sidebar.
  // Flush, the card ran the full width of the panel and this measured -10.
  constexpr char kCardClearsTheGrip[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " const c = document.querySelector('.aipcard');"
      " if (!g || !c) { return -1; }"
      " const s = getComputedStyle(g, '::after');"
      " const gr = g.getBoundingClientRect(); const cr = c.getBoundingClientRect();"
      " const w = parseFloat(s.width); const l = parseFloat(s.left);"
      // Only one of the two offsets is set, and a pseudo-element can answer
      // the other as "auto" rather than resolving it.
      " const lead = Number.isNaN(l) ? gr.width - parseFloat(s.right) - w : l;"
      " const left = gr.left + lead;"
      " return Math.round(left >= cr.right ? left - cr.right"
      "                                    : cr.left - (left + w)); })()";
  EXPECT_GE(content::EvalJs(panel, kCardClearsTheGrip).ExtractInt(), 0)
      << "the grip overlaps the ai card instead of sitting in a gap beside it";

  // The grip and the gap swap edges with the dock, and a rule written
  // for one side only would put the card back under the line on the other.
  window->SetSidebarPosition(/*on_right=*/true);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kCardClearsTheGrip).ExtractInt() >= 0;
  })) << "docked left, the grip overlaps the card again - gap "
      << content::EvalJs(panel, kCardClearsTheGrip).ExtractInt();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kHairlineColor).ExtractString() == dragging;
  })) << "the accent changed with the side, so the two docks do not match - "
      << "got " << content::EvalJs(panel, kHairlineColor).ExtractString()
      << " against " << dragging;
}

// The page grip drew a 2px line the full height of the pane
// where the native seam draws a 4px pill inset from both ends, so beside each
// other the two read as different controls.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheAiPanelGripMarkCopiesTheNativeSeamPill) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  window->ToggleAiPanel();

  constexpr char kMarkWidth[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " if (!g || !g.getBoundingClientRect().width) { return 0; }"
      " return Math.round("
      "     parseFloat(getComputedStyle(g, '::after').width)); })()";
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return content::EvalJs(panel, kMarkWidth).ExtractInt() > 0; }))
      << "the ai panel never opened, so the grip has no mark to measure";

  // Read against the native constants rather than a copy of them, so moving
  // the seam moves both marks or fails here.
  EXPECT_EQ(content::EvalJs(panel, kMarkWidth).ExtractInt(),
            atrium::kSidebarSeamLineThickness);

  constexpr char kMarkInset[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " return Math.round("
      "     parseFloat(getComputedStyle(g, '::after').top)); })()";
  EXPECT_EQ(content::EvalJs(panel, kMarkInset).ExtractInt(),
            atrium::kSidebarSeamLineInset)
      << "the mark runs the full height instead of stopping where a card's "
         "corner turns, the way the seam pill does";

  // Two readings, because either alone passes while the mark sits in the
  // wrong place. This one is the gutter the mark keeps from the panel's own
  // seam edge, against the pill's own gutter in kSidebarGripWidth.
  constexpr int kSeamGutterGap =
      (atrium::kSidebarGripWidth - atrium::kSidebarSeamLineThickness) / 2;
  constexpr char kMarkSeamGap[] =
      "(() => { const g = document.querySelector('.aipresize');"
      " const s = getComputedStyle(g, '::after');"
      " const onRight = document.getElementById('atrium-root')"
      "     .classList.contains('aionright');"
      " const band = g.getBoundingClientRect().width;"
      " const w = parseFloat(s.width); const l = parseFloat(s.left);"
      " const lead = Number.isNaN(l) ? band - parseFloat(s.right) - w : l;"
      " return Math.round(onRight ? lead : band - lead - w); })()";
  EXPECT_EQ(content::EvalJs(panel, kMarkSeamGap).ExtractInt(), kSeamGutterGap)
      << "the mark is centred in the grab band rather than in the gutter, so "
         "it hugs the ai card";

  // And this one is the half the page cannot see: the panel's seam edge has to
  // BE the web card's edge. The pane container used to hold a gutter of its
  // own out here, which put the mark 13px into the gap and against the card.
  auto seam_gap = [&]() {
    const gfx::Rect card = window->GetWebContentBoundsInScreen();
    const gfx::Rect box = panel->GetContainerBounds();
    return box.x() >= card.right() ? box.x() - card.right()
                                   : card.x() - box.right();
  };
  EXPECT_EQ(seam_gap(), 0)
      << "the pane container still insets the card away from the ai panel, so "
         "the mark cannot reach the seam";

  // Both halves again on the other dock - the inset and the offset are
  // each written per side, so one of them can flip and the other not.
  window->SetSidebarPosition(/*on_right=*/true);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kMarkSeamGap).ExtractInt() == kSeamGutterGap;
  })) << "docked left, the mark left the gutter - gap "
      << content::EvalJs(panel, kMarkSeamGap).ExtractInt();
  EXPECT_TRUE(base::test::RunUntil([&]() { return seam_gap() == 0; }))
      << "docked left, the card keeps an inset from the panel - gap "
      << seam_gap();
}

// every sidebar surface is app chrome. The defaults used to hang
// off .sidebar, which the Overview rail pane is not inside, so its cards and
// rows took the hand and the archived rows the caret.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       NoSidebarSurfaceShowsTheHandOrTheCaret) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  // Names the offender rather than counting them, so a failure says which rule
  // came back.
  constexpr char kFirstHand[] =
      "(() => { for (const el of"
      "     document.querySelectorAll('#atrium-root *')) {"
      "   if (getComputedStyle(el).cursor === 'pointer') {"
      "     return el.className || el.tagName; } }"
      " return ''; })()";
  EXPECT_EQ(content::EvalJs(sidebar, kFirstHand).ExtractString(), "");

  // A field is the one place the caret belongs, so it is excluded by tag
  // rather than by the rule under test.
  constexpr char kFirstSelectable[] =
      "(() => { for (const el of"
      "     document.querySelectorAll('#atrium-root *')) {"
      "   if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' ||"
      "       el.isContentEditable) { continue; }"
      "   if (getComputedStyle(el).userSelect === 'text') {"
      "     return el.className || el.tagName; } }"
      " return ''; })()";
  EXPECT_EQ(content::EvalJs(sidebar, kFirstSelectable).ExtractString(), "");
}

// each Overview section set its own left padding and most set none,
// so the chips, cards and rows started against the icon rail's border.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TheOverviewInsetsItsContentFromTheRail) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  constexpr char kInset[] =
      "(() => { const rail = document.querySelector('.ovrail');"
      " const head = document.querySelector('.ovhead');"
      " if (!rail || !head) { return -1; }"
      " return Math.round(head.getBoundingClientRect().left -"
      "                   rail.getBoundingClientRect().right); })()";
  EXPECT_GE(content::EvalJs(sidebar, kInset).ExtractInt(), 8)
      << "the Overview's own column starts against the rail's border";

  // Every section mounts into .ovpane, so a padding on .ovhead alone would
  // pass above and leave the report's own rows where they were.
  constexpr char kPaneInset[] =
      "(() => { const rail = document.querySelector('.ovrail');"
      " const pane = document.querySelector('.ovpane');"
      " if (!rail || !pane) { return -1; }"
      " return Math.round(pane.getBoundingClientRect().left -"
      "                   rail.getBoundingClientRect().right); })()";
  EXPECT_GE(content::EvalJs(sidebar, kPaneInset).ExtractInt(), 8)
      << "the section content starts against the rail's border";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       BackdropTintPushesInkToAiPanel) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  // 0xFF3366CC, opaque - a value distinct from any default backdrop.
  window->SetBackdropTint(0xFF3366CCu);

  // Mutation this pins: dropping PushAiPanelInk from SetBackdropTint's
  // instant-apply branch leaves the panel without the theme push.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               panel,
               "document.documentElement.style"
               ".getPropertyValue('--atrium-space-tint')")
               .ExtractString() == "51 102 204";
  })) << "SetBackdropTint(0xFF3366CC) never reached the ai-panel's "
         "--atrium-space-tint";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AiPanelWidthChangedNarrowsWebContent) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->AiPanelContentsForTesting());
  const int width_before = window->GetWebContentBoundsInScreen().width();

  base::ListValue toggled_args;
  toggled_args.Append(true);
  window->OnSurfaceMessage("aiPanelToggled", toggled_args);
  base::ListValue width_args;
  width_args.Append(320);
  window->OnSurfaceMessage("aiPanelWidthChanged", width_args);

  // Mutation this pins: dropping SetAiPanelWidth's SetPreferredSize/
  // InvalidateLayout calls (or leaving ai_panel_container_ at width 0) means
  // the web content pane never loses any width - the real squeeze.
  const int width_after = window->GetWebContentBoundsInScreen().width();
  EXPECT_LT(width_after, width_before)
      << "opening the ai-panel to 320px did not narrow the web content pane "
      << "(before=" << width_before << " after=" << width_after << ")";
}

// the container is the width, so the document must fill it.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AiPanelLaysOutAtItsContainerWidth) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window->AiPanelContainerBoundsForTesting().IsEmpty(); }))
      << "the ai-panel never opened on the hotkey";

  // Mutation this pins: a .aipanel width that comes from anything but the
  // container - a fixed 280px, or clampWidth() reading this document's own
  // innerWidth for the window width, which returned 45% of the panel itself.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "Math.abs(document.querySelector('.aipanel')"
                           ".getBoundingClientRect().width -"
                           " document.documentElement.clientWidth) <= 1")
        .ExtractBool();
  })) << "the panel is not as wide as the container the browser gave it: "
      << content::EvalJs(panel,
                         "document.querySelector('.aipanel')"
                         ".getBoundingClientRect().width + ' of ' +"
                         " document.documentElement.clientWidth")
             .ExtractString();

  // The grip straddles the seam by half its width, so only the inner half is
  // ever inside the panel - which side that is depends on the dock edge.
  ASSERT_TRUE(content::EvalJs(panel,
                              "document.getElementById('atrium-root')"
                              ".classList.contains('aionright')")
                  .ExtractBool())
      << "this window docks its sidebar left, so the panel must be aionright";
  EXPECT_TRUE(content::EvalJs(panel,
                              "document.querySelector('.aipresize')"
                              ".getBoundingClientRect().right > 0 &&"
                              " document.querySelector('.aipresize')"
                              ".getBoundingClientRect().right <="
                              " document.documentElement.clientWidth")
                  .ExtractBool())
      << "the resize grip has no reachable half inside the panel";

  EXPECT_TRUE(content::EvalJs(panel,
                              "document.querySelector('.aiphead')"
                              ".lastElementChild.getBoundingClientRect()"
                              ".right <= document.documentElement.clientWidth")
                  .ExtractBool())
      << "the header's Close button is clipped by the panel's own edge";
  EXPECT_TRUE(content::EvalJs(panel,
                              "document.querySelector('.aichatrow')"
                              ".getBoundingClientRect().right <="
                              " document.documentElement.clientWidth")
                  .ExtractBool())
      << "the prompt row is clipped by the panel's own edge";

  // the header/body/prompt sit on a raised card, not straight on
  // the transparent backdrop.
  EXPECT_TRUE(content::EvalJs(panel,
                              "getComputedStyle(document.querySelector("
                              "'.aipcard')).backgroundColor !=="
                              " 'rgba(0, 0, 0, 0)'")
                  .ExtractBool())
      << "the ai-panel card must paint a non-transparent surface";
  EXPECT_TRUE(content::EvalJs(panel,
                              "getComputedStyle(document.querySelector("
                              "'.aipcard')).borderRadius !== '0px'")
                  .ExtractBool())
      << "the ai-panel card must be rounded";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TabSwitchPushesTargetToAiPanel) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  // Loaded and titled BEFORE it's ever made active (background tab), so the
  // later ActivateTabAt below - the one this test measures - can't race the
  // title against GetTitle().
  const GURL titled_url = embedded_test_server()->GetURL("/title2.html");
  NavigateParams params(browser(), titled_url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  // Pin it - unpinned, the close-on-mismatch would close the
  // panel on this test's own tab switch instead of re-targeting it.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(content::ExecJs(panel, "document.querySelector('.aippin').click()"));

  browser()->tab_strip_model()->ActivateTabAt(1);

  // Mutation this pins: dropping PushAiPanelTarget from OnActiveTabChanged
  // (or the call itself) leaves .aictx on whatever tab the panel opened
  // with, never the new foreground tab's title.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "document.querySelector('.aictx').textContent")
               .ExtractString()
               .find("Title Of Awesomeness") != std::string::npos;
  })) << "switching to the new tab never re-targeted the ai-panel's context "
         "line to its title";
}

// nothing ever called atriumSetAiPanelHotkey, so the "remappable"
// half was never proven end to end.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ConfiguredHotkeyPushesRemapToAiPanel) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  atrium::AtriumBrowserWindow::SetAiPanelHotkey("'");

  // Mutation this pins: dropping PushAiPanelHotkey's call from
  // SetAiPanelHotkey leaves ai-panel.js on its install-time default ('`'),
  // so a real keydown for the newly configured key never opens it.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: \"'\"}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool();
  })) << "SetAiPanelHotkey(\"'\") never reached ai-panel.js - a real keydown "
         "for the new key did not open the panel";
}

// ToggleAiPanel gives the command bar's "Toggle AI panel" verb a way to
// reach the panel's own keydown listener, which never has focus otherwise.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ToggleAiPanelOpensThePanelWhenAiIsEnabled) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aipanel')")
                  .ExtractBool());

  window->ToggleAiPanel();

  // Mutation this pins: dropping ToggleAiPanel's ExecuteJavaScript call (or
  // its own gate) leaves the panel closed with no entry point having fired.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool();
  })) << "ToggleAiPanel() never opened the panel while AI is enabled";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ToggleAiPanelDoesNothingWhenAiIsDisabled) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  atrium::AtriumBrowserWindow::SetAiEnabled(false);

  // Mutation this pins: dropping the kill-switch check from ToggleAiPanel
  // would dispatch the configured key's keydown regardless of aiEnabled.
  window->ToggleAiPanel();

  EXPECT_FALSE(window->AiPanelOpenForTesting())
      << "ToggleAiPanel() opened the panel while AI is disabled";

  // Restores the default so this test does not poison a later one sharing
  // the same test-launcher process.
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
}

// The native kToggleAiPanel hotkey action runs through BrowserController,
// not straight to the window - proving the wiring a real keypress uses,
// distinct from the ToggleAiPanel() tests above.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       NativeToggleAiPanelOpensThePanelWhenAiIsEnabled) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  auto* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);

  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aipanel')")
                  .ExtractBool());

  controller->ToggleAiPanel();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool();
  })) << "the native kToggleAiPanel action never opened the panel while AI "
         "is enabled";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       NativeToggleAiPanelDoesNothingWhenAiIsDisabled) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  auto* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);

  atrium::AtriumBrowserWindow::SetAiEnabled(false);

  controller->ToggleAiPanel();

  EXPECT_FALSE(window->AiPanelOpenForTesting())
      << "the native kToggleAiPanel action opened the panel while AI is "
         "disabled";

  atrium::AtriumBrowserWindow::SetAiEnabled(true);
}

// The user report: opening the AI panel should focus its prompt, with no
// click needed - ToggleAiPanel is the explicit-open path both the hotkey and
// the command bar's "Toggle AI panel" verb route through.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ToggleAiPanelFocusesThePromptAndTheWebView) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  window->ToggleAiPanel();

  // Mutation this pins: dropping the RequestFocus() call from ToggleAiPanel's
  // opening branch leaves the WebView unfocused, so a real keypress would
  // never reach the prompt despite document.activeElement being right.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->AiPanelWebViewHasFocusForTesting(); }))
      << "opening the ai-panel never gave its WebView native focus";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "document.activeElement === "
                                   "document.querySelector('.aichatinput')")
               .ExtractBool();
  })) << "opening the ai-panel never focused its prompt input";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingAiPanelReturnsFocusToWebContents) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  window->ToggleAiPanel();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->AiPanelWebViewHasFocusForTesting(); }));

  window->ToggleAiPanel();  // close

  // Mutation this pins: dropping SetAiPanelOpen's FocusWebContentsPane() call
  // strands focus in the now-hidden panel instead of handing it back.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneWebViewHasFocusForTesting(); }))
      << "closing the ai-panel never returned focus to the web contents";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TurningAiOffWithFocusInThePanelReturnsFocusToWebContents) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  window->ToggleAiPanel();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->AiPanelWebViewHasFocusForTesting(); }));

  atrium::AtriumBrowserWindow::SetAiEnabled(false);

  // Mutation this pins: dropping SetAiEnabled's focus hand-back strands focus
  // in the panel's now-torn-down DOM instead of returning it to the page.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneWebViewHasFocusForTesting(); }))
      << "turning AI off while the prompt had focus never returned focus to "
         "the web contents";

  atrium::AtriumBrowserWindow::SetAiEnabled(true);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AiOffBeforeAiPanelToggleLandsClearsPendingFocus) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  window->FocusWebContentsPane();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->ActivePaneWebViewHasFocusForTesting(); }));

  // ToggleAiPanel's opening branch sets ai_panel_focus_pending_ before the
  // JS side's aiPanelToggled(true) reaches SetAiPanelOpen - turning AI off
  // right here, before that message lands, must not leave the flag alive.
  window->ToggleAiPanel();
  atrium::AtriumBrowserWindow::SetAiEnabled(false);
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  // The reshow that would otherwise arrive through aiPanelToggled(true).
  base::ListValue reshow_args;
  reshow_args.Append(true);
  window->OnSurfaceMessage("aiPanelToggled", reshow_args);
  base::RunLoop().RunUntilIdle();

  // Mutation this pins: SetAiEnabled(false) not clearing
  // ai_panel_focus_pending_ leaves it set for this reshow to steal focus.
  EXPECT_FALSE(window->AiPanelWebViewHasFocusForTesting())
      << "an implicit reshow after an AI off/on cycle stole focus into the "
         "panel";
  EXPECT_TRUE(window->ActivePaneWebViewHasFocusForTesting())
      << "an implicit reshow after an AI off/on cycle left the page "
         "unfocused";
}

// PushAiPanelTarget fires once at activation and was never
// re-pushed when the attached tab's title changed afterwards.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TitleChangeOnAttachedTabUpdatesAiPanelContextLine) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      active, embedded_test_server()->GetURL("/title2.html")));

  // Pinned, so the title change below can't be masked by an unpinned close.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(content::ExecJs(panel, "document.querySelector('.aippin').click()"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "document.querySelector('.aictx').textContent")
               .ExtractString()
               .find("Title Of Awesomeness") != std::string::npos;
  })) << "the panel never attached to the tab's initial title";

  ASSERT_TRUE(content::ExecJs(active, "document.title = 'Retitled Later'"));

  // Mutation this pins: dropping AiPanelTargetObserver's TitleWasSet hook (or
  // its PushAiPanelTarget call) leaves .aictx on the load-time title forever -
  // no OnActiveTabChanged fires here, since the tab itself never changes.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "document.querySelector('.aictx').textContent")
               .ExtractString()
               .find("Retitled Later") != std::string::npos;
  })) << "a title change on the attached tab never reached the ai-panel's "
         "context line";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       TitleChangeOnUnattachedTabDoesNotRetargetAiPanel) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      active, embedded_test_server()->GetURL("/title2.html")));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title1.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  content::WebContents* background = params.navigated_or_inserted_contents;
  ASSERT_TRUE(content::WaitForLoadStop(background));

  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(content::ExecJs(panel, "document.querySelector('.aippin').click()"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "document.querySelector('.aictx').textContent")
               .ExtractString()
               .find("Title Of Awesomeness") != std::string::npos;
  })) << "the panel never attached to the foreground tab";

  ASSERT_TRUE(content::ExecJs(background, "document.title = 'Should Not Show'"));
  // GetTitle(), not the renderer's document.title - it only updates AFTER
  // TitleWasSet already fired for this WebContents, so any wrongly-scoped
  // observer reaction would already be in flight by the time this is true.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return background->GetTitle() == u"Should Not Show"; }))
      << "the background tab's title never actually changed";

  // Mutation this pins: retargeting AiPanelTargetObserver at every tab
  // instead of just the active one would pull this stray title into .aictx.
  EXPECT_EQ(content::EvalJs(panel, "document.querySelector('.aictx').textContent")
                .ExtractString()
                .find("Should Not Show"),
            std::string::npos);
}

// the missing native call from a Settings edit to a running
// window's ai-panel.js - AtriumBrowserWindow::SetAiEnabled.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AiEnabledPushTearsDownRebuildsAndCatchesUpOnReload) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aipanel')")
                  .ExtractBool())
      << "the panel did not start built";

  // The toggle reaches an OPEN panel and tears it down.
  atrium::AtriumBrowserWindow::SetAiEnabled(false);
  // Mutation this pins: dropping PushAiPanelEnabled's ExecuteJavaScript (or
  // its call from SetAiEnabled) leaves .aipanel on screen forever.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "!document.querySelector('.aipanel')")
        .ExtractBool();
  })) << "SetAiEnabled(false) never reached the open ai-panel";

  // A panel opened LATER (a fresh document, modeled here with a
  // reload) catches up from the cache rather than starting on again.
  panel->GetController().Reload(content::ReloadType::NORMAL,
                                /*check_for_repost=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  // Mutation this pins: dropping PushAiPanelEnabled from the aiPanelReady
  // catch-up (or CurrentAiEnabled's cache write in SetAiEnabled) leaves a
  // freshly loaded document on ai-panel.js's own install-time default (on).
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "!document.querySelector('.aipanel')")
        .ExtractBool();
  })) << "a freshly loaded ai-panel document did not catch up on the off state";

  // Restores the default so this test does not poison a later one sharing
  // the same test-launcher process.
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "!!document.querySelector('.aipanel')")
        .ExtractBool();
  })) << "SetAiEnabled(true) never rebuilt the ai-panel";

  // Mutation this pins: buildAiSurface() appending onto the stale off-state
  // div instead of replacing it leaves both nodes in #atrium-root, sharing
  // the panel's flex row.
  EXPECT_EQ(content::EvalJs(panel,
                            "document.querySelectorAll('#atrium-root > *')"
                            ".length")
                .ExtractInt(),
            1)
      << "the off-state div was not cleared before the panel rebuilt";
  EXPECT_FALSE(content::EvalJs(panel, "!!document.querySelector('.aipaneloff')")
                   .ExtractBool())
      << "the off-state div is still in the rebuilt document";

  // AtriumClientView::NonClientHitTest must not claim the open panel as a
  // drag region (GetClientRegions carve, independent of the DOM defect above).
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->AiPanelContainerBoundsForTesting().IsEmpty(); }))
      << "the ai-panel never opened on the hotkey";
  const gfx::Point panel_point =
      window->AiPanelContainerBoundsForTesting().CenterPoint();
  EXPECT_NE(window->widget()->non_client_view()->client_view()->NonClientHitTest(
                panel_point),
            HTCAPTION)
      << "a press inside the open ai-panel dragged the window instead of "
         "reaching the panel";
}

// the JS attach lifecycle (ai-panel.js), driven for real
// through the same OnActiveTabChanged path TabSwitchPushesTargetToAiPanel
// exercises above.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       UnpinnedTabSwitchClosesAiPanel) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  // Opened, left unpinned - the default state.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool();
  })) << "the ai-panel never opened on the hotkey";

  browser()->tab_strip_model()->ActivateTabAt(1);

  // Mutation this pins: dropping atriumSetAiPanelTarget's unpinned
  // "setOpen(false)" branch leaves the panel open on the tab it opened with.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !content::EvalJs(panel,
                            "document.getElementById('atrium-root')"
                            ".classList.contains('aipopen')")
                .ExtractBool();
  })) << "switching tabs did not close the unpinned ai-panel";
}

// The panel belongs to its tab: hidden on another tab, back with the same
// conversation on the switch back, and never showing another tab's history.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       UnpinnedAiPanelFollowsItsTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  auto panel_open = [&]() {
    return content::EvalJs(panel, "document.getElementById('atrium-root')"
                                  ".classList.contains('aipopen')")
               .ExtractBool();
  };
  auto chat_text = [&]() {
    return content::EvalJs(panel,
                           "[...document.querySelectorAll('.aichatlog "
                           ".aibubble-me')].map((b) => b.textContent).join('|')")
               .ExtractString();
  };

  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(base::test::RunUntil(panel_open))
      << "the ai-panel never opened on the hotkey";
  ASSERT_TRUE(content::ExecJs(
      panel, "document.querySelector('.aichatinput').value = 'question on A'; "
             "document.querySelector('.aichatsend').click()"));
  ASSERT_EQ(chat_text(), "question on A");

  // Another tab hides the panel, down to the native container.
  browser()->tab_strip_model()->ActivateTabAt(1);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !panel_open() && !window->AiPanelOpenForTesting();
  })) << "switching to tab B did not hide the unpinned ai-panel";

  // Opening it on tab B shows none of tab A's history.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(base::test::RunUntil(panel_open));
  EXPECT_EQ(chat_text(), "") << "tab B's panel shows tab A's conversation";
  ASSERT_TRUE(content::ExecJs(
      panel, "document.querySelector('.aichatinput').value = 'question on B'; "
             "document.querySelector('.aichatsend').click()"));

  // Back on tab A the panel shows again with tab A's conversation only.
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return panel_open() && window->AiPanelOpenForTesting() &&
           chat_text() == "question on A";
  })) << "switching back to tab A did not restore its panel and conversation";
}

// Closing a tab must drop its panel state, or the map grows with every tab
// the panel was ever opened on.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClosingATabForgetsItsAiPanelState) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  NavigateParams params(browser(), GURL("about:blank"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  const double closed_id =
      static_cast<double>(atrium::TabId(params.navigated_or_inserted_contents));

  ASSERT_TRUE(content::ExecJs(
      panel, "window.forgotten = []; window.atriumForgetAiPanelTab = "
             "(id) => window.forgotten.push(id);"));
  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->CloseWebContentsAt(
      tabs->GetIndexOfWebContents(params.navigated_or_inserted_contents),
      TabCloseTypes::CLOSE_USER_GESTURE);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, "window.forgotten.join(',')")
               .ExtractString() == base::NumberToString(closed_id);
  })) << "closing a tab never told the ai-panel to forget it";
}

// Opens the unpinned panel on the active tab and leaves one chat message in
// it, with a titled background tab at index 1 to move to.
class AtriumAiPanelPerTabBrowserTest : public AtriumBrowserWindowBrowserTest {
 protected:
  void SetUpPanelWithChatOnFirstTab() {
    ASSERT_TRUE(embedded_test_server()->Start());
    window_ = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
    ASSERT_TRUE(window_);
    panel_ = window_->AiPanelContentsForTesting();
    ASSERT_TRUE(panel_);
    ASSERT_TRUE(content::WaitForLoadStop(panel_));

    NavigateParams params(browser(),
                          embedded_test_server()->GetURL("/title2.html"),
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
    Navigate(&params);
    ASSERT_TRUE(
        content::WaitForLoadStop(params.navigated_or_inserted_contents));

    ASSERT_TRUE(content::ExecJs(
        panel_, "document.dispatchEvent(new KeyboardEvent('keydown', "
                "{key: '`'}))"));
    ASSERT_TRUE(base::test::RunUntil([&]() { return PanelOpen(); }));
    ASSERT_TRUE(content::ExecJs(
        panel_, "document.querySelector('.aichatinput').value = 'on A'; "
                "document.querySelector('.aichatsend').click()"));
    ASSERT_EQ(ChatCount(), 1);
  }

  bool PanelOpen() {
    return content::EvalJs(panel_, "document.getElementById('atrium-root')"
                                   ".classList.contains('aipopen')")
               .ExtractBool();
  }

  int ChatCount() {
    return content::EvalJs(
               panel_,
               "document.querySelectorAll('.aichatlog .aibubble-me').length")
        .ExtractInt();
  }

  bool ContextNamesSecondTab() {
    return content::EvalJs(panel_,
                           "document.querySelector('.aictx')?.textContent ?? ''")
               .ExtractString()
               .find("Title Of Awesomeness") != std::string::npos;
  }

  raw_ptr<atrium::AtriumBrowserWindow> window_ = nullptr;
  raw_ptr<content::WebContents> panel_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AtriumAiPanelPerTabBrowserTest,
                       ClosingTheActiveTabHidesTheUnpinnedAiPanel) {
  ASSERT_NO_FATAL_FAILURE(SetUpPanelWithChatOnFirstTab());

  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return ContextNamesSecondTab() && !PanelOpen() &&
           !window_->AiPanelOpenForTesting();
  })) << "closing the active tab left the unpinned ai-panel open";
  EXPECT_EQ(ChatCount(), 0) << "the closed tab's chat is still on screen";

  ASSERT_TRUE(content::ExecJs(
      panel_, "document.dispatchEvent(new KeyboardEvent('keydown', "
              "{key: '`'}))"));
  ASSERT_TRUE(base::test::RunUntil([&]() { return PanelOpen(); }));
  EXPECT_EQ(ChatCount(), 0)
      << "the new active tab's panel shows the closed tab's chat";
}

// The panel showing again because the user switched back to its tab
// must not steal focus - only an explicit open (hotkey, command bar) may.
IN_PROC_BROWSER_TEST_F(AtriumAiPanelPerTabBrowserTest,
                       ReturningToATabWithAnOpenPanelDoesNotStealFocus) {
  ASSERT_NO_FATAL_FAILURE(SetUpPanelWithChatOnFirstTab());

  TabStripModel* tabs = browser()->tab_strip_model();
  tabs->ActivateTabAt(1);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !PanelOpen(); }));
  window_->FocusWebContentsPane();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window_->ActivePaneWebViewHasFocusForTesting(); }));

  tabs->ActivateTabAt(0);

  // Mutation this pins: an unconditional RequestFocus() in SetAiPanelOpen's
  // open branch would pull focus into the panel on this implicit reopen too.
  ASSERT_TRUE(base::test::RunUntil([&]() { return PanelOpen(); }));
  EXPECT_FALSE(window_->AiPanelWebViewHasFocusForTesting())
      << "switching back to a tab with an open panel stole focus into it";
  EXPECT_TRUE(window_->ActivePaneWebViewHasFocusForTesting())
      << "switching back to a tab with an open panel left the page unfocused";
}

IN_PROC_BROWSER_TEST_F(AtriumAiPanelPerTabBrowserTest,
                       AiOffThenOnClearsEveryTabsAiPanelState) {
  ASSERT_NO_FATAL_FAILURE(SetUpPanelWithChatOnFirstTab());

  // Tab A's conversation is now saved, not on screen.
  browser()->tab_strip_model()->ActivateTabAt(1);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return ContextNamesSecondTab() && !PanelOpen(); }));

  atrium::AtriumBrowserWindow::SetAiEnabled(false);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel_, "!document.querySelector('.aipanel')")
        .ExtractBool();
  }));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel_, "!!document.querySelector('.aipanel')")
        .ExtractBool();
  }));

  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_TRUE(base::test::RunUntil([&]() { return !ContextNamesSecondTab(); }))
      << "switching back to tab A never reached the ai-panel";
  EXPECT_FALSE(PanelOpen()) << "AI off/on kept tab A's open state";
  EXPECT_FALSE(window_->AiPanelOpenForTesting());
  EXPECT_EQ(ChatCount(), 0) << "AI off/on kept tab A's conversation";
}

IN_PROC_BROWSER_TEST_F(
    AtriumBrowserWindowBrowserTest,
    PinnedTabSwitchRetargetsBodyAndActsOnAttachedTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  content::WebContents* second_tab = params.navigated_or_inserted_contents;
  ASSERT_TRUE(content::WaitForLoadStop(second_tab));
  const std::string second_tab_id =
      base::NumberToString(atrium::TabId(second_tab));

  // Opening auto-summarizes the first tab. Leave a chat entry too, so both
  // .aisum and .aichatlog carry this tab's content before the switch.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelector('.aisum')?.textContent ?? ''")
               .ExtractString()
               .size() > 0;
  })) << "opening the panel never produced a summary for the first tab";

  ASSERT_TRUE(content::ExecJs(
      panel, "document.querySelector('.aichatinput').value = 'q'; "
             "document.querySelector('.aichatsend').click()"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelectorAll('.aichatlog *').length")
               .ExtractInt() >= 2;
  })) << "the first tab's chat never got a reply";

  ASSERT_TRUE(content::ExecJs(panel, "document.querySelector('.aippin').click()"));

  browser()->tab_strip_model()->ActivateTabAt(1);

  // Mutation this pins: dropping restoreBodyFor's replaceChildren calls
  // leaves the first tab's summary and chat bubbles on screen after the
  // re-attach, so only .aictx would differ.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->AiPanelOpenForTesting() &&
           content::EvalJs(panel,
                           "document.querySelector('.aictx')?.textContent ?? ''")
                   .ExtractString()
                   .find("Title Of Awesomeness") != std::string::npos &&
           content::EvalJs(panel,
                           "document.querySelector('.aisum')?.childElementCount"
                           " ?? -1")
                   .ExtractInt() == 0 &&
           content::EvalJs(
               panel, "document.querySelector('.aichatlog')?.childElementCount"
                      " ?? -1")
                   .ExtractInt() == 0;
  })) << "pinned re-attachment closed the panel, left stale summary/chat "
         "content from the previous tab, or never updated .aictx";

  // No model is reachable in a test, so every reply is an error note and the
  // answer text can say nothing about which tab was asked. What the panel
  // decided is in the request: a chat carries the tab id its context gave it.
  ASSERT_TRUE(content::ExecJs(panel, R"(
      window.__aiSent = [];
      window.__aiSendOrig = chrome.send.bind(chrome);
      chrome.send = (name, args) => {
        window.__aiSent.push([name, args]);
        window.__aiSendOrig(name, args);
      };
      true;
  )"));

  // Summarize/chat must act on the newly attached tab.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.querySelector('.aichatinput').value = 'q2'; "
             "document.querySelector('.aichatsend').click()"));
  // Mutation this pins: reverting sendChat's currentContext() call to
  // getContext() sends the panel's install-time tab id, which is the tab the
  // switch just moved away from.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               panel,
               "(window.__aiSent.find(s => s[0] === 'aiPanelChatRequested')"
               " ?? ['', ['']])[1][0]")
               .ExtractString() == second_tab_id;
  })) << "chat after the switch asked about the wrong (previous) tab";
}

// attached diverges from active only in the async gap
// before PushAiPanelTarget lands, so the attached id is passed explicitly.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SummarizeResolvesAttachedTabNotActiveTab) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  content::WebContents* attached_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      attached_tab, GURL("data:text/html,<body>NONCE-ATTACHED-TAB</body>")));
  const std::string attached_tab_id =
      base::NumberToString(atrium::TabId(attached_tab));

  NavigateParams params(
      browser(), GURL("data:text/html,<body>NONCE-ACTIVE-TAB</body>"),
      ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  content::WebContents* other_tab = params.navigated_or_inserted_contents;
  ASSERT_TRUE(content::WaitForLoadStop(other_tab));

  // Now looking at other_tab, as if the panel is pinned to attached_tab from
  // before this switch.
  browser()->tab_strip_model()->ActivateTabAt(1);
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), other_tab);

  // Mutation this pins: reverting ResolveAiPanelTargetContents to
  // unconditionally return GetActiveWebContents() resolves other_tab here
  // instead of attached_tab.
  EXPECT_EQ(window->ResolveAiPanelTargetContentsForTesting(attached_tab_id),
            attached_tab)
      << "summarize resolved the active tab instead of the attached one";

  // An empty or unknown tab id (unpinned, or never attached) still falls
  // back to the active tab - the pre-existing behavior this fix must not
  // break.
  EXPECT_EQ(window->ResolveAiPanelTargetContentsForTesting(""), other_tab);
  EXPECT_EQ(window->ResolveAiPanelTargetContentsForTesting("999999999"),
            other_tab);
}

// The space-switch half, via create_space rather than
// the call-chain reasoning previously on record.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       UnpinnedSpaceSwitchClosesAiPanel) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool();
  })) << "the ai-panel never opened on the hotkey";

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  // Mutation this pins: dropping atriumSetAiPanelTarget's unpinned
  // "setOpen(false)" branch leaves the panel open after a real space switch.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !content::EvalJs(panel,
                            "document.getElementById('atrium-root')"
                            ".classList.contains('aipopen')")
                .ExtractBool();
  })) << "switching to a real second space did not close the unpinned ai-panel";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       PinnedSpaceSwitchRetargetsAiPanel) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));
  ASSERT_TRUE(content::ExecJs(panel, "document.querySelector('.aippin').click()"));

  char* raw_id = atrium_core_create_space("Second", "Default");
  ASSERT_TRUE(raw_id) << "atrium_core_create_space returned null";
  atrium_core_string_free(raw_id);
  ASSERT_TRUE(controller->SelectSpace(1));

  // Loaded in the background first, same as TabSwitchPushesTargetToAiPanel -
  // PushAiPanelTarget fires once, at activation, so the title must already
  // be final before ActivateTabAt below or this reads the pre-load title.
  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));
  browser()->tab_strip_model()->ActivateTabAt(1);

  // Mutation this pins: dropping PushAiPanelTarget from OnActiveTabChanged
  // leaves .aictx stale and would also let an unpinned-only close fire here.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool() &&
           content::EvalJs(panel, "document.querySelector('.aictx').textContent")
               .ExtractString()
               .find("Title Of Awesomeness") != std::string::npos;
  })) << "a pinned ai-panel did not stay open and re-target after a real "
         "space switch";
}

// Marginalia store, pad and reaching path. The tests
// below cover the page-side half these left unbuilt: the wrap, the re-anchor
// and click-to-jump.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       HighlightSelectionRefusesATooShortSelection) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // Mutation this pins: dropping mutate::add_marginal's length check makes
  // this return true and stores a two-character quote.
  EXPECT_FALSE(window->HighlightSelectionOnActivePage("ab"));
  EXPECT_FALSE(window->IsSplit()) << "a refused highlight must not open a pad";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       HighlightSelectionStoresTheQuoteAndOpensTheNotesPad) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // A nonce, not a literal: the spaces store persists across separate runs of
  // this binary, so a fixed quote can pass on a stale on-disk write even with
  // the handler disabled.
  const std::string quote = "marginalia-nonce-" +
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());

  // Mutation this pins: removing the atrium_core_add_marginal call, or the
  // OpenNotesBesideThisPage call, from HighlightSelectionOnActivePage.
  ASSERT_TRUE(window->HighlightSelectionOnActivePage(quote));
  ASSERT_TRUE(window->IsSplit()) << "the highlight did not open the notes pad";

  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));

  // The quote arrives as a card with an empty note field, through the
  // marginalia array atrium_core_get_space_note now carries.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               notes,
               "document.querySelector('.notes-cards')?.textContent ?? ''")
               .ExtractString()
               .find(quote) != std::string::npos;
  })) << "the notes pad never rendered a comment card for the highlight";
  // Scoped to THIS run's card: the spaces store persists across runs of this
  // binary, so the first card in the pad is very likely an older mark that
  // already carries a note.
  EXPECT_EQ(content::EvalJs(
                notes,
                "[...document.querySelectorAll('.mcard')]"
                "  .find(c => c.querySelector('.mquote').textContent"
                "               .includes('" + quote + "'))"
                "  ?.querySelector('.mnote').textContent ?? 'NO SUCH CARD'")
                .ExtractString(),
            "")
      << "a fresh highlight's note field must start empty";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       MarginaliaNoteChangedSurfaceMessagePersistsToTheMark) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const std::string stamp =
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  ASSERT_TRUE(window->HighlightSelectionOnActivePage("quote-" + stamp));

  char* pad_raw = atrium_core_get_space_note(-1);
  ASSERT_TRUE(pad_raw);
  const std::string pad_json(pad_raw);
  atrium_core_string_free(pad_raw);
  const size_t id_at = pad_json.rfind("\"id\":\"mark-");
  ASSERT_NE(id_at, std::string::npos) << pad_json;
  const size_t id_start = id_at + 6;
  const std::string mark_id =
      pad_json.substr(id_start, pad_json.find('"', id_start) - id_start);

  const std::string note = "commentary-" + stamp;
  base::ListValue args;
  args.Append(mark_id);
  args.Append(note);
  window->OnSurfaceMessage("marginaliaNoteChanged", args);

  // Mutation this pins: removing the marginaliaNoteChanged branch from
  // OnSurfaceMessage leaves the mark's note empty forever.
  char* after_raw = atrium_core_get_space_note(-1);
  ASSERT_TRUE(after_raw);
  const std::string after(after_raw);
  atrium_core_string_free(after_raw);
  EXPECT_NE(after.find(note), std::string::npos)
      << "the card's note never reached the stored highlight: " << after;
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       WrapSelectionMarksEachElementItCrosses) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      active, GURL("data:text/html,<p id=p>before <b id=b>bold</b> "
                   "after</p>")));

  // Selects "ore " (the tail of the plain text node) through "bo" (the head
  // of <b>'s text node) - one range, two Text nodes, one element boundary.
  const std::string quote =
      content::EvalJs(active,
                      "(function(){"
                      "var range = document.createRange();"
                      "range.setStart(document.getElementById('p')."
                      "firstChild, 3);"
                      "range.setEnd(document.getElementById('b')."
                      "firstChild, 2);"
                      "var sel = window.getSelection();"
                      "sel.removeAllRanges();"
                      "sel.addRange(range);"
                      "return range.toString();"
                      "})()")
          .ExtractString();
  ASSERT_EQ(quote, "ore bo");

  ASSERT_TRUE(window->HighlightSelectionOnActivePage(quote));

  // Mutation this pins: wrapRange using range.surroundContents (which throws
  // on a range crossing element boundaries) instead of one <mark> per
  // intersected Text node.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               active,
               "document.querySelectorAll('mark[data-atrium-mark-id]')"
               ".length")
               .ExtractInt() == 2;
  })) << "a selection crossing an element boundary must produce one <mark> "
         "per Text node, not one that throws or spans the boundary";

  EXPECT_EQ(content::EvalJs(
                active,
                "new Set([...document.querySelectorAll("
                "'mark[data-atrium-mark-id]')].map("
                "m => m.getAttribute('data-atrium-mark-id'))).size")
                .ExtractInt(),
            1)
      << "both marks must share the one highlight's id";
  EXPECT_EQ(content::EvalJs(
                active,
                "[...document.querySelectorAll("
                "'mark[data-atrium-mark-id]')].map(m => m.textContent)"
                ".join('')")
                .ExtractString(),
            quote);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ReanchorFindsTheQuoteAgainAfterANavigation) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());

  const std::string stamp =
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  const std::string quote = "marginalia-revisit-" + stamp;
  const GURL url = embedded_test_server()->GetURL(
      "/marginalia-test?" + base::EscapeQueryParamValue(quote, false));

  char* id_raw = atrium_core_add_marginal(-1, url.spec().c_str(),
                                          quote.c_str(), 0.0);
  ASSERT_TRUE(id_raw);
  atrium_core_string_free(id_raw);

  // A same-tab navigation - the observer marginalia_observer_ is retargeted
  // onto by OnActiveTabChanged already covers this tab, so DidFinishNavigation
  // alone drives the re-anchor.
  ASSERT_TRUE(content::NavigateToURL(active, url));

  // Mutation this pins: MarginaliaObserver::DidFinishNavigation not calling
  // ReanchorMarginaliaOnActiveTab, or reanchor() not calling wrapRange.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               active, "document.querySelector('mark[data-atrium-mark-id]')"
                       "?.textContent ?? ''")
               .ExtractString() == quote;
  })) << "revisiting a URL with a stored highlight must re-wrap its quote";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ReanchorDropsSilentlyWhenTheTextChanged) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(embedded_test_server()->Start());

  const std::string stamp =
      base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  char* id_raw = atrium_core_add_marginal(
      -1, url.spec().c_str(), ("gone-" + stamp).c_str(), 0.0);
  ASSERT_TRUE(id_raw);
  atrium_core_string_free(id_raw);

  ASSERT_TRUE(content::NavigateToURL(active, url));
  ASSERT_EQ(content::EvalJs(active, "document.body.innerText")
                .ExtractString()
                .find("gone-"),
            std::string::npos)
      << "title1.html must not happen to contain the quote";

  // The naive exact-substring match finds nothing on a page
  // whose text changed since the highlight was made, and drops it silently
  // rather than guessing at a new location.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(active, "document.readyState").ExtractString() ==
        "complete";
  }));
  window->ReanchorMarginaliaOnActiveTabForTesting();
  EXPECT_EQ(content::EvalJs(
                active, "document.querySelectorAll('mark[data-atrium-mark-id]')"
                       ".length")
                .ExtractInt(),
            0);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClickToJumpWorksBothWaysBetweenAMarkAndItsCard) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      active, GURL("data:text/html,<p id=p>a passage worth marking</p>")));

  ASSERT_TRUE(content::EvalJs(
                  active,
                  "(function(){"
                  "var range = document.createRange();"
                  "range.selectNodeContents(document.getElementById('p'));"
                  "var sel = window.getSelection();"
                  "sel.removeAllRanges();"
                  "sel.addRange(range);"
                  "return true;"
                  "})()")
                  .ExtractBool());
  ASSERT_TRUE(
      window->HighlightSelectionOnActivePage("a passage worth marking"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               active,
               "document.querySelector('mark[data-atrium-mark-id]') !== null")
        .ExtractBool();
  }));
  const std::string mark_id =
      content::EvalJs(
          active,
          "document.querySelector('mark[data-atrium-mark-id]')"
          ".getAttribute('data-atrium-mark-id')")
          .ExtractString();
  ASSERT_FALSE(mark_id.empty());

  content::WebContents* notes = window->NotesContentsForTesting();
  ASSERT_TRUE(notes);
  ASSERT_TRUE(content::WaitForLoadStop(notes));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               notes, "document.querySelector('.mcard[data-mark-id=\"" +
                          mark_id + "\"]') !== null")
        .ExtractBool();
  }));

  // Mark -> card: a real click on the mark, routed through
  // MarginaliaObserver::DidGetUserInteraction draining the isolated world's
  // pending-click queue.
  const gfx::Point center(
      content::EvalJs(active,
                      "Math.round(document.querySelector("
                      "'mark[data-atrium-mark-id]').getBoundingClientRect()"
                      ".left + 5)")
          .ExtractInt(),
      content::EvalJs(active,
                      "Math.round(document.querySelector("
                      "'mark[data-atrium-mark-id]').getBoundingClientRect()"
                      ".top + 5)")
          .ExtractInt());
  content::SimulateMouseClickAt(active, 0,
                                blink::WebMouseEvent::Button::kLeft, center);

  // Diagnostic: confirm the click reached the isolated world's listener
  // before asserting on the drain that consumes it.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               active,
               "window.__atriumMarginaliaPendingClicks &&"
               "window.__atriumMarginaliaPendingClicks.length > 0",
               content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
               ISOLATED_WORLD_ID_CHROME_INTERNAL)
        .ExtractBool();
  })) << "the real click on the mark never reached the isolated world's "
         "click delegate";

  window->DrainMarginaliaMarkClicksForTesting();

  // Mutation this pins: the click delegate not pushing onto the pending
  // queue, or DrainMarginaliaMarkClicks not calling atriumFlashMarginal.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               notes, "document.querySelector('.mcard[data-mark-id=\"" +
                          mark_id + "\"]').classList.contains('mflash')")
        .ExtractBool();
  })) << "clicking a mark on the page never flashed its card";

  // Card -> mark: the surface message a click on the card's quote sends.
  base::ListValue args;
  args.Append(mark_id);
  window->OnSurfaceMessage("marginalMarkJumpRequested", args);

  // Mutation this pins: ScrollToMarginalMarkOnActivePage not calling
  // BuildScrollToMarkScript, or scrollToMark not adding the flash class.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               active,
               "document.querySelector('mark[data-atrium-mark-id]')"
               ".classList.contains('atrium-marginalia-flash')")
        .ExtractBool();
  })) << "clicking a card's quote never flashed its mark on the page";
}

// These tests need a second space with its own tint, so it gets its own
// fixture rather than AtriumBrowserWindowBrowserTest's single-space one -
// same temp-dir-plus-core-close isolation pattern as that fixture's SetUp.
class AtriumSpaceSwitchBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    // A per-run value, not a fixed literal - so a pass proves THIS run's
    // switch pushed the tint, rather than coincidentally matching whatever a
    // previous run (or a stale default) happened to leave behind.
    nonce_rgb_ = static_cast<uint32_t>(
                     base::Time::Now().since_origin().InMicroseconds()) &
                 0xFFFFFEu;
    nonce_rgb_ |= 1u;  // never zero, so it can't read as "no tint pushed".
    const std::string nonce_hex = base::StringPrintf("#%06X", nonce_rgb_);
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":["
        "{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"First\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"Test\"}]},"
        "{\"id\":\"22222222-2222-2222-2222-222222222222\","
        "\"name\":\"Second\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":\"" +
            nonce_hex +
            "\",\"profileID\":\"Default\",\"order\":1,"
            "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
            "\"url\":\"https://example.com/\",\"title\":\"Test2\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

  uint32_t nonce_rgb() const { return nonce_rgb_; }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
  uint32_t nonce_rgb_ = 0;
};

IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SpaceSwitchPushesTargetSpaceTintToAiPanel) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel) << "BuildHtmlAiPanel never built the ai-panel WebContents";
  ASSERT_TRUE(content::WaitForLoadStop(panel));

  // The real path: the sidebar's space-switcher message, not a direct
  // SetBackdropTint call - this test exists precisely because nothing else reaches
  // SetBackdropTint from a real space switch.
  base::ListValue args;
  args.Append(1);
  window->OnSurfaceMessage("sidebarSpaceSelected", args);

  const std::string expected = base::StringPrintf(
      "%d %d %d", (nonce_rgb() >> 16) & 0xFF, (nonce_rgb() >> 8) & 0xFF,
      nonce_rgb() & 0xFF);

  // Mutation this pins: SelectSpace not reading/pushing the rebuilt
  // snapshot's spaceTint leaves the panel on the first space's tint.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               panel,
               "document.documentElement.style"
               ".getPropertyValue('--atrium-space-tint')")
               .ExtractString() == expected;
  })) << "switching to the second space never pushed its tint (" << expected
      << ") to the ai-panel's --atrium-space-tint";
}

// the Courtyard, driven as the real /courtyard WebContents
// against the two-space fixture AtriumSpaceSwitchBrowserTest already builds.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardClickEntersRoom) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsCourtyardShowing());

  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard,
                           "document.querySelectorAll('.room').length")
               .ExtractInt() == 2;
  })) << "both spaces never rendered as rooms";

  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "document.querySelector('.room[data-room=\"1\"] > .rhead').click()"));

  // Mutation this pins: dropping enterRoom's effect (or the sidebarSpaceSelected
  // send it produces) leaves the active space at 0 and the Courtyard open.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }))
      << "clicking room 1 never entered space 1";
  EXPECT_FALSE(window->IsCourtyardShowing())
      << "entering a room did not close the Courtyard";
}

// a card in a room that is NOT the active one. Its row carries no
// live tab id - the snapshot resolves those for the active space alone - so the
// click has to name the room, the section and the visible index instead.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardCardClickEntersTheRoomItIsIn) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_EQ(atrium_core_active_space_index(), 0);

  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               courtyard,
               "document.querySelectorAll("
               "'.room[data-room=\"1\"] .rlane[data-section=\"tabs\"] .rtab')"
               ".length")
               .ExtractInt() == 1;
  })) << "room 1 never drew its own row";

  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "document.querySelector('.room[data-room=\"1\"] "
      ".rlane[data-section=\"tabs\"] .rtab').click()"));

  // Mutation this pins: sending sidebarTabClicked with the card's dataset id
  // sends an empty string here, so the space never changes.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }))
      << "clicking a card in room 1 never entered space 1";
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->IsCourtyardShowing(); }))
      << "a card click did not close the Courtyard";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardDragMovesTabBetweenRooms) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard,
                           "document.querySelectorAll('.room').length")
               .ExtractInt() == 2;
  }));

  // A pointer-capture drag, driven the way the real gesture arrives: the plane
  // reads a PLANE coordinate off the pointer event, which is why the view
  // stopped using HTML5 drag and drop at all.
  static constexpr char kDragScript[] = R"(
    const card = document.querySelector(
        '.room[data-room="0"] .rlane[data-section="tabs"] .rtab');
    const from = card.getBoundingClientRect();
    const to = document.querySelector('.room[data-room="1"]');
    const box = to.getBoundingClientRect();
    const send = (type, x, y) => card.dispatchEvent(new PointerEvent(
        type, {bubbles: true, pointerId: 1, clientX: x, clientY: y}));
    send('pointerdown', from.left + 8, from.top + 8);
    send('pointermove', box.left + 20, box.top + 40);
    send('pointerup', box.left + 20, box.top + 40);
  )";
  ASSERT_TRUE(content::ExecJs(courtyard, kDragScript));

  // Mutation this pins: dropping dropDecision's cross-room branch, or the
  // sidebarTabMovedToSpace forward through CourtyardWidget, leaves room 0 with
  // its card and room 1 without the moved one.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               courtyard,
               "document.querySelectorAll("
               "'.room[data-room=\"1\"] .rtab').length")
               .ExtractInt() == 2;
  })) << "the dragged card never landed in room 1";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               courtyard,
               "document.querySelectorAll("
               "'.room[data-room=\"0\"] .rtab').length")
               .ExtractInt() == 0;
  })) << "room 0 still shows the card that was dragged away";
}

// a canvas reorder has to reach the SIDEBAR model, not just
// redraw the plane. The Courtyard's own re-render would pass a DOM-only check
// even if no message ever left the page.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardCanvasDragReordersTheSidebar) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // Two rows in room 0, so there is an order to change at all.
  ASSERT_TRUE(chrome::AddAndReturnTabAt(browser(), GURL("https://second.test/"),
                                        1, /*foreground=*/true));

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               courtyard,
               "document.querySelectorAll("
               "'.room[data-room=\"0\"] .rlane[data-section=\"tabs\"] .rtab')"
               ".length")
               .ExtractInt() == 2;
  })) << "room 0 never drew both rows";

  const std::string before = window->CourtyardSnapshotJSON();

  static constexpr char kReorderScript[] = R"(
    const lane = document.querySelector(
        '.room[data-room="0"] .rlane[data-section="tabs"]');
    const card = lane.querySelector('.rtab[data-index="0"]');
    const box = lane.getBoundingClientRect();
    const send = (type, x, y) => card.dispatchEvent(new PointerEvent(
        type, {bubbles: true, pointerId: 1, clientX: x, clientY: y}));
    send('pointerdown', box.left + 8, box.top + 34);
    send('pointermove', box.left + 8, box.top + 34 + 112);
    send('pointerup', box.left + 8, box.top + 34 + 112);
  )";
  ASSERT_TRUE(content::ExecJs(courtyard, kReorderScript));

  // Mutation this pins: dropping the sidebarTabReordered forward in
  // CourtyardWidget, or the trailing space index in the handler, leaves the
  // sidebar model exactly as it was while the plane still looks reordered.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->CourtyardSnapshotJSON() != before; }))
      << "a canvas reorder never changed the sidebar snapshot";
}

// the Courtyard is an activatable widget with a real WebView
// (same shape as CommandBarWidget and the peek card), so the WebView can claim
// Cmd+Q before it ever reaches the app main menu. Drives the real
// CommandDispatcher delegate installed on the Courtyard's own window rather
// than testing the routing table in isolation - the mutation this pins is the
// delegate never being installed at all.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardCmdQReachesMainMenu) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  EXPECT_TRUE(window->CourtyardCmdQRoutesToMainMenuForTesting())
      << "Cmd+Q was not routed to the main menu while the Courtyard held key "
         "focus";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardEscapeCloses) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "document.querySelector('.courtwrap').dispatchEvent("
      "new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}))"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->IsCourtyardShowing(); }))
      << "Escape never closed the Courtyard";
}

// pinch-in opens the Courtyard directly (the canvas rung
// is cancelled). Drives SimulateCtrlWheelDeltaForTesting rather than a
// synthetic NSEvent - see that method's comment for why.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CtrlWheelPastThresholdOpensCourtyard) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsCourtyardShowing());
  const int space_before = atrium_core_active_space_index();

  // 40 then 31 crosses the 70 threshold on the second sample, the same
  // split StepWheelAccumulator's unit test uses.
  window->SimulateCtrlWheelDeltaForTesting(40);
  EXPECT_FALSE(window->IsCourtyardShowing());
  window->SimulateCtrlWheelDeltaForTesting(31);

  // Mutation this pins: StepWheelAccumulator or the OpenCourtyard call this
  // wires it to being dropped leaves the Courtyard closed.
  EXPECT_TRUE(window->IsCourtyardShowing())
      << "ctrl+wheel past the threshold never opened the Courtyard";
  EXPECT_EQ(atrium_core_active_space_index(), space_before)
      << "the pinch changed the active space - it must only open the "
         "Courtyard, not also drive swipe.js's space switch";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CtrlWheelPinchOutClosesCourtyard) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());

  window->SimulateCtrlWheelDeltaForTesting(-40);
  EXPECT_TRUE(window->IsCourtyardShowing());
  window->SimulateCtrlWheelDeltaForTesting(-31);

  // Mutation this pins: dropping the courtyard_showing-gated close direction
  // leaves the Courtyard open after a full pinch-out.
  EXPECT_FALSE(window->IsCourtyardShowing())
      << "pinch-out past the threshold never closed the Courtyard";
}

// A native-initiated close has to tell the page, or the next open refuses
// its own `open` step and the plane comes back at the old pan and zoom.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CourtyardCloseNotifiesThePage) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCourtyard();
  ASSERT_TRUE(window->IsCourtyardShowing());
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  // Rendered rooms mean renderer_ready_ is set, the same flag
  // NotifyPageClosed checks.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard,
                           "document.querySelectorAll('.room').length")
               .ExtractInt() == 2;
  })) << "both spaces never rendered as rooms";

  ASSERT_TRUE(content::ExecJs(
      courtyard,
      "window.__atriumCourtyardClosedCalled = false;"
      "const real = window.atriumCourtyardClosed;"
      "window.atriumCourtyardClosed = () => {"
      "  window.__atriumCourtyardClosedCalled = true;"
      "  real();"
      "};"));

  window->CloseCourtyard();
  ASSERT_FALSE(window->IsCourtyardShowing());

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard, "window.__atriumCourtyardClosedCalled")
        .ExtractBool();
  })) << "CloseCourtyard() hid the widget but never called "
         "window.atriumCourtyardClosed on the page";
}

namespace {

void SendScrub(atrium::AtriumBrowserWindow* window, const char* name,
               base::Value arg) {
  base::ListValue args;
  args.Append(std::move(arg));
  window->OnSurfaceMessage(name, args);
}

// The wash without the glass alpha the window adds.
SkColor OpaqueTint(atrium::AtriumBrowserWindow* window) {
  return SkColorSetA(window->backdrop_tint_for_testing(), 0xFF);
}

constexpr char kScrubOriginId[] = "11111111-1111-1111-1111-111111111111";
constexpr char kScrubTargetId[] = "22222222-2222-2222-2222-222222222222";

// A single-stop theme spec with the given film grain.
std::string ThemeWithGrain(const char* tint, double grain) {
  char* raw = atrium_core_single_stop_theme(tint);
  std::optional<base::Value> spec =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  atrium_core_string_free(raw);
  spec->GetDict().SetByDottedPath("effects.grain", grain);
  std::string json;
  base::JSONWriter::Write(*spec, &json);
  return json;
}

}  // namespace

// both spaces are flat, so the scrub is the tint lerp alone.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubMovesTheTintWithTheFingerAndDropsOnCancel) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  auto* controller = atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  const SkColor from = OpaqueTint(window);
  const SkColor to = SkColorSetRGB((nonce_rgb() >> 16) & 0xFF,
                                   (nonce_rgb() >> 8) & 0xFF, nonce_rgb() & 0xFF);
  ASSERT_NE(from, to) << "the fixture's two tints must differ";

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  ASSERT_TRUE(controller->space_scrub_in_flight_for_testing());
  EXPECT_EQ(window->scrub_blend_weight_for_testing(), 0.0);

  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  EXPECT_EQ(OpaqueTint(window), to)
      << "at full weight the wash is the neighbour's tint";

  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(0.5));
  const SkColor mid = OpaqueTint(window);
  EXPECT_NE(mid, from);
  EXPECT_NE(mid, to);
  // Mutation this pins: a blend that ignores the weight leaves the wash at
  // one end or the other.
  const int expected_r = (static_cast<int>(SkColorGetR(from)) +
                          static_cast<int>(SkColorGetR(to))) / 2;
  EXPECT_NEAR(static_cast<int>(SkColorGetR(mid)), expected_r, 1);

  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(false));
  EXPECT_FALSE(controller->space_scrub_in_flight_for_testing());
  EXPECT_EQ(window->scrub_blend_weight_for_testing(), -1.0);
  EXPECT_EQ(OpaqueTint(window), from) << "a cancel puts the origin tint back";
  EXPECT_EQ(atrium_core_active_space_index(), 0) << "a cancel switches nothing";
}

// the switch used to start its own 300 ms cross-fade, which is the
// colour arriving after the swipe.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubCommitLandsTheSpaceWithoutASecondFade) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  auto* controller = atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  const SkColor to = SkColorSetRGB((nonce_rgb() >> 16) & 0xFF,
                                   (nonce_rgb() >> 8) & 0xFF, nonce_rgb() & 0xFF);

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  // The order swipe.js sends them in: the switch lands while the scrub still
  // owns the backdrop.
  SendScrub(window, "sidebarSpaceSelected", base::Value(1));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }));
  EXPECT_TRUE(controller->space_scrub_in_flight_for_testing())
      << "the switch must not tear down the scrub it rides on";
  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(true));
  EXPECT_FALSE(controller->space_scrub_in_flight_for_testing());
  // Mutation this pins: SelectSpace still calling SetBackdropTint for the
  // scrubbed space restarts a cross-fade, and its first frames read as the
  // origin colour again.
  EXPECT_EQ(OpaqueTint(window), to);
}

// a flat origin has no painter for the staged layer, so the scrub
// opens the theme session itself and a cancel must close it again.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubStagesAThemedNeighbourOverAFlatOrigin) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  auto* controller = atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_FALSE(window->has_theme_for_testing());

  char* spec = atrium_core_single_stop_theme("#3366AA");
  ASSERT_TRUE(spec);
  ASSERT_TRUE(atrium_core_set_space_theme(
      "22222222-2222-2222-2222-222222222222", spec));
  atrium_core_string_free(spec);

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  ASSERT_TRUE(controller->space_scrub_in_flight_for_testing());
  EXPECT_TRUE(window->has_theme_for_testing())
      << "a flat origin needs the theme painter up to composite the layer";
  EXPECT_TRUE(window->theme_bitmap_for_testing().isNull())
      << "the origin has no picture of its own";

  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(false));
  EXPECT_FALSE(window->has_theme_for_testing())
      << "a cancel closes the session the scrub opened";

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  SendScrub(window, "sidebarSpaceSelected", base::Value(1));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }));
  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(true));
  EXPECT_TRUE(window->has_theme_for_testing());
  EXPECT_FALSE(window->theme_bitmap_for_testing().isNull())
      << "the commit promoted the staged picture";
  EXPECT_EQ(window->scrub_blend_weight_for_testing(), -1.0);
}

// the outgoing picture fades out under the incoming one, so the
// last scrub frame and the promoted frame are the same pixels.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubLastFrameMatchesThePromotedFrame) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  auto* controller = atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  const std::string origin = ThemeWithGrain("#AA3333", 0.0);
  const std::string target = ThemeWithGrain("#3366AA", 0.6);
  ASSERT_TRUE(atrium_core_set_space_theme(kScrubOriginId, origin.c_str()));
  ASSERT_TRUE(atrium_core_set_space_theme(kScrubTargetId, target.c_str()));
  controller->ApplySpaceThemeForTesting(window, kScrubOriginId, 0);
  ASSERT_FALSE(window->theme_bitmap_for_testing().isNull());
  const SkColor before = window->root_background_pixel_for_testing();

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(0.5));
  const SkColor mid = window->root_background_pixel_for_testing();
  EXPECT_NE(mid, before) << "half way, the incoming picture shows";
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  const SkColor last = window->root_background_pixel_for_testing();
  EXPECT_NE(last, mid);

  SendScrub(window, "sidebarSpaceSelected", base::Value(1));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }));
  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(true));
  EXPECT_EQ(window->scrub_blend_weight_for_testing(), -1.0);
  EXPECT_EQ(window->root_background_pixel_for_testing(), last)
      << "the promotion must not change the picture";
}

// toward a flat space the origin's picture and grain fade out to
// the live wash, and nothing sits on top of it at full weight.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubFadesAThemedOriginOutToTheFlatWash) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  auto* controller = atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  const std::string origin = ThemeWithGrain("#AA3333", 0.4);
  ASSERT_TRUE(atrium_core_set_space_theme(kScrubOriginId, origin.c_str()));
  controller->ApplySpaceThemeForTesting(window, kScrubOriginId, 0);
  ASSERT_TRUE(window->has_theme_for_testing());

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  EXPECT_TRUE(window->has_theme_for_testing())
      << "the painter stays up until the commit";
  EXPECT_FLOAT_EQ(window->painted_grain_for_testing(), 0.f);
  const SkColor last = window->root_background_pixel_for_testing();

  SendScrub(window, "sidebarSpaceSelected", base::Value(1));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }));
  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(true));
  EXPECT_FALSE(window->has_theme_for_testing());
  EXPECT_EQ(window->root_background_pixel_for_testing(), last)
      << "the flat wash was already the whole frame";
}

// the grain follows the finger like the picture does.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       SwipeScrubBlendsTheGrainWithTheFinger) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const std::string target = ThemeWithGrain("#3366AA", 0.6);
  ASSERT_TRUE(atrium_core_set_space_theme(kScrubTargetId, target.c_str()));

  SendScrub(window, "sidebarSpaceScrubBegan", base::Value(1));
  EXPECT_FLOAT_EQ(window->painted_grain_for_testing(), 0.f);
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(0.5));
  EXPECT_FLOAT_EQ(window->painted_grain_for_testing(), 0.3f);
  SendScrub(window, "sidebarSpaceScrubBlend", base::Value(1.0));
  EXPECT_FLOAT_EQ(window->painted_grain_for_testing(), 0.6f);

  SendScrub(window, "sidebarSpaceSelected", base::Value(1));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }));
  SendScrub(window, "sidebarSpaceScrubEnded", base::Value(true));
  EXPECT_FLOAT_EQ(window->painted_grain_for_testing(), 0.6f)
      << "the promotion keeps the grain it blended toward";
}

// atrium_core_favorite_row reads title/url off the persisted
// document, never the live WebContents, so a nonce baked into the seeded
// tab's title survives even if the tab navigates for real.
class AtriumFavoriteShelfBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    nonce_title_ = "Favourite-" +
        base::NumberToString(base::Time::Now().since_origin().InMicroseconds());
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Test\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"" +
            nonce_title_ +
            "\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

  const std::string& nonce_title() const { return nonce_title_; }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
  std::string nonce_title_;
};

IN_PROC_BROWSER_TEST_F(AtriumFavoriteShelfBrowserTest,
                       SidebarFavoriteShelvedCarriesTheFavouritesUrlAndTitle) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // Favourite the fixture's one seeded tab for real, same call the sidebar's
  // own "Add to Favorites" gesture makes (sidebarRowFavorited).
  ASSERT_TRUE(
      atrium_core_favorite_row(-1, /*index=*/0, /*section=*/0, /*slot=*/-1))
      << "favouriting the fixture's seeded tab failed";

  // What buildFavoriteMenuItems' Put on Shelf action sends: the tile's
  // dataset.favId (the url) and its title, read off the favourite - not a
  // tab id, which a favourite has none of.
  base::ListValue args;
  args.Append("https://example.com/");
  args.Append(nonce_title());
  window->OnSurfaceMessage("sidebarFavoriteShelved", args);

  // Mutation this pins: dropping the sidebarFavoriteShelved branch (or
  // ShelveFavorite's atrium_core_shelf_add call) leaves the shelf without
  // this entry entirely.
  char* raw = atrium_core_shelf_overview();
  ASSERT_TRUE(raw) << "atrium_core_shelf_overview returned null";
  const std::string overview(raw);
  atrium_core_string_free(raw);
  EXPECT_NE(overview.find("https://example.com/"), std::string::npos)
      << overview;
  EXPECT_NE(overview.find(nonce_title()), std::string::npos) << overview;
}

// both ai_model routes share one completion callback, so
// each test below asserts text only the true route produces.
class AtriumAiRouteBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":[{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Test\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        // about:blank - https://example.com/ (other fixtures' seed) is
        // unreachable here and crash-loops the renderer these tests need.
        "\"url\":\"about:blank\",\"title\":\"Test\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    settings_path_ = temp_dir_.GetPath().AppendASCII("settings.json");
    spaces_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SPACES_FILE", spaces_path.value());
    settings_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SETTINGS_FILE", settings_path_.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  // Must land before InProcessBrowserTest's own SetUpOnMainThread - the mock
  // host resolver locks its rule set once startup's first navigation runs.
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    settings_env_override_.reset();
    spaces_env_override_.reset();
  }

  // Written fresh per test, after the fixture's browser and profile already
  // exist - ComputeAiRouteFacts reads this file at request time, not at
  // startup, so there is no earlier point that would need it.
  void WriteSettings(std::string_view json) {
    // Called from the test body, after BrowserTestBase::SetUp() has already
    // disallowed blocking on the main thread - unlike the fixture's own
    // SetUp() above, which still runs before that.
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(settings_path_, json));
  }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath settings_path_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      spaces_env_override_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      settings_env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       OllamaSelectionReachesTheOllamaPath) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-route-probe-322-9-12"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  ASSERT_NE(content::EvalJs(active, "document.body.innerText")
                .ExtractString()
                .find("ai-route-probe-322-9-12"),
            std::string::npos)
      << "active tab: " << active->GetLastCommittedURL()
      << " innerText: "
      << content::EvalJs(active, "document.body.innerText").ExtractString();
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  // A closed port did not refuse fast/deterministically in this VM - point
  // at embedded_test_server instead: a real listener with no /api/chat
  // handler, so the backend reports the 404 it got rather than a dead host.
  const net::HostPortPair& ollama_host_port =
      embedded_test_server()->host_port_pair();
  // settings.rs's FileShape requires these four - an object missing any of
  // them fails to deserialize and load() falls back to Settings::default()
  // for every field, not just the missing ones.
  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"ollama\","
      "\"ollamaHost\":\"%s\",\"ollamaPort\":%d}",
      ollama_host_port.host().c_str(), ollama_host_port.port()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(atrium::AtriumBrowserWindow::AiEnabled())
      << "CurrentAiEnabled() did not read back true after SetAiEnabled(true)";
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aisum')")
                  .ExtractBool())
      << "the ai panel document has no .aisum element to render into";
  // The real hotkey gesture - summarize()'s pendingRender closure is what
  // actually writes .aisum, and a direct OnSurfaceMessage call skips it.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelector('.aisum').textContent")
               .ExtractString()
               .find("Ollama answered 404.") != std::string::npos;
  })) << "an ollama ai_model selection never produced the Ollama-specific "
         "network error - actual text: "
      << content::EvalJs(panel,
                         "document.querySelector('.aisum').textContent")
             .ExtractString();
}

IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       OnDeviceSelectionNeverRoutesToOllama) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-route-probe-322-9-12"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  // Same real, reachable ollama_host/port as the Ollama test above - if
  // routing ignored ai_model and always called Ollama, this would show the
  // Ollama parse error instead of the on-device one below.
  const net::HostPortPair& ollama_host_port =
      embedded_test_server()->host_port_pair();
  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"onDevice\","
      "\"ollamaHost\":\"%s\",\"ollamaPort\":%d}",
      ollama_host_port.host().c_str(), ollama_host_port.port()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  // The real hotkey gesture - summarize()'s pendingRender closure is what
  // actually writes .aisum, and a direct OnSurfaceMessage call skips it.
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  // The VM this runs in has no Foundation Models available, so on-device
  // reports kUnavailable - its own message, never the Ollama one.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    const std::string text =
        content::EvalJs(panel, "document.querySelector('.aisum').textContent")
            .ExtractString();
    return text == "On-device summarize isn't available on this Mac.";
  })) << "an onDevice ai_model selection did not show the on-device "
         "unavailable message - actual: "
      << content::EvalJs(panel,
                         "document.querySelector('.aisum').textContent")
             .ExtractString();
  EXPECT_EQ(content::EvalJs(panel,
                            "document.querySelector('.aisum').textContent")
                .ExtractString()
                .find("Ollama"),
            std::string::npos)
      << "an onDevice selection must never surface Ollama-specific wording";
  EXPECT_EQ(content::EvalJs(panel,
                            "document.querySelector('.aisum').textContent")
                .ExtractString()
                .find("reachable from here"),
            std::string::npos)
      << "an onDevice selection must never route into the custom-endpoint "
         "SSRF-guard message";
}

// settings.rs rejects a literal "127.0.0.1" at load time, so "a.test" plus a
// host_resolver rule stands in - resolves to loopback without looking local.
IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       CustomEndpointSelectionReachesTheGuardedCustomPath) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-route-probe-322-9-13"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(https_server.Start());
  const std::string endpoint_url =
      https_server.GetURL("a.test", "/v1/chat/completions").spec();

  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"customEndpoint\","
      "\"customEndpointUrl\":\"%s\"}",
      endpoint_url.c_str()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aisum')")
                  .ExtractBool())
      << "the ai panel document has no .aisum element to render into";
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelector('.aisum').textContent")
               .ExtractString()
               .find("That address is not reachable from here.") !=
           std::string::npos;
  })) << "a customEndpoint ai_model selection never reached the guarded "
         "custom path - actual text: "
      << content::EvalJs(panel,
                         "document.querySelector('.aisum').textContent")
             .ExtractString();
}

// The rule has to land before the fixture's own "*", because the first
// matching rule wins - a host that resolves PUBLIC is what gets the request
// past the pre-resolve guard and into the loader this test is about.
class AtriumCustomEndpointRebindBrowserTest : public AtriumAiRouteBrowserTest {
 public:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("public-endpoint.test", "93.184.216.34");
    AtriumAiRouteBrowserTest::SetUpOnMainThread();
  }
};

// The premise of the test below, which is silent when it breaks: a wildcard
// winning here would send every request into the pre-resolve refusal, and the
// guard test would fail for a reason that has nothing to do with the guard.
IN_PROC_BROWSER_TEST_F(AtriumCustomEndpointRebindBrowserTest,
                       ThePublicEndpointRuleOutranksTheFixtureWildcard) {
  std::unique_ptr<network::SimpleHostResolver> resolver =
      network::SimpleHostResolver::Create(
          g_browser_process->system_network_context_manager()->GetContext());

  base::RunLoop resolved;
  int result = net::ERR_IO_PENDING;
  std::vector<std::string> addresses;
  resolver->ResolveHost(
      network::mojom::HostResolverHost::NewHostPortPair(
          net::HostPortPair("public-endpoint.test", 443)),
      net::NetworkAnonymizationKey::CreateTransient(),
      network::mojom::ResolveHostParameters::New(),
      base::BindLambdaForTesting(
          [&](int net_error, const net::ResolveErrorInfo& resolve_error_info,
              const net::AddressList& resolved_addresses,
              const net::HostResolverEndpointResults& alternative_endpoints) {
            result = net_error;
            for (const net::IPEndPoint& endpoint :
                 resolved_addresses.endpoints()) {
              addresses.push_back(endpoint.address().ToString());
            }
            resolved.Quit();
          }));
  resolved.Run();

  ASSERT_EQ(result, net::OK);
  ASSERT_EQ(addresses.size(), 1u);
  EXPECT_EQ(addresses[0], "93.184.216.34");
}

// the guard resolves under a transient key and the loader resolves
// again under its own, so a short-TTL host answers the two differently. This
// is the second line, refusing the address the request really reached.
IN_PROC_BROWSER_TEST_F(AtriumCustomEndpointRebindBrowserTest,
                       ACustomEndpointRequestCarriesTheConnectTimeLocalBlock) {
  const std::string kEndpointUrl =
      "https://public-endpoint.test/v1/chat/completions";
  network::TestURLLoaderFactory factory;
  std::optional<std::string> refusal;
  bool answered = false;
  // Woken by the event, not by an idle poll: base::test::RunUntil re-tests its
  // predicate only when the thread goes idle, and here it burned the whole 30s
  // timeout without ever seeing the request it was waiting for.
  base::RunLoop settled;
  base::RunLoop finished;
  factory.SetInterceptor(base::BindLambdaForTesting(
      [&](const network::ResourceRequest& request) { settled.Quit(); }));
  atrium_ai::ChatViaCustomEndpoint(
      kEndpointUrl, std::optional<std::string>("secret-key"), "page text", "",
      &factory, g_browser_process->system_network_context_manager()->GetContext(),
      base::BindLambdaForTesting([&](std::optional<std::string> summary,
                                     std::optional<std::string> error) {
        refusal = std::move(error);
        answered = true;
        settled.Quit();
        finished.Quit();
      }),
      "gpt-4o-mini");
  settled.Run();

  ASSERT_FALSE(answered) << "refused before creating any request: "
                         << refusal.value_or("(no message)");
  ASSERT_EQ(factory.NumPending(), 1);

  const network::TestURLLoaderFactory::PendingRequest* pending =
      factory.GetPendingRequest(0);
  EXPECT_EQ(pending->request.headers.GetHeader(
                net::HttpRequestHeaders::kAuthorization),
            "Bearer secret-key");
  EXPECT_NE(network::GetUploadData(pending->request).find("page text"),
            std::string::npos);
  EXPECT_TRUE(pending->options &
              network::mojom::kURLLoadOptionBlockLocalRequest)
      << "a host that rebinds to 127.0.0.1 between the two resolves would be "
         "connected to and handed the bearer key";

  // Answered here so the backend finishes and deletes itself while this frame
  // is alive. Left pending, it outlives the test, and the pipe error from the
  // destroyed factory then runs its callback over a dead stack.
  factory.AddResponse(kEndpointUrl,
                      R"({"choices":[{"message":{"content":"ok"}}]})");
  finished.Run();
}

// the stored ollamaModel, not the pre-task hardcoded
// "llama3", must reach the live /api/chat request body.
IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       OllamaSelectionThreadsTheChosenModelIntoTheRequest) {
  base::Lock request_lock;
  std::string captured_request_body;  // guarded by request_lock
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  embedded_test_server()->RegisterRequestHandler(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/api/chat") {
          return nullptr;
        }
        {
          base::AutoLock lock(request_lock);
          captured_request_body = request.content;
        }
        auto response =
            std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("application/json");
        // NDJSON, so the trailing newline is what makes this a complete line
        // the stream consumer will parse at all.
        response->set_content(
            R"({"message":{"content":"ollama-thread-probe-summary"}})"
            "\n");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-route-probe-322-9-15"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  const net::HostPortPair& ollama_host_port =
      embedded_test_server()->host_port_pair();
  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"ollama\","
      "\"ollamaHost\":\"%s\",\"ollamaPort\":%d,"
      "\"ollamaModel\":\"llama3:70b-thread-probe\"}",
      ollama_host_port.host().c_str(), ollama_host_port.port()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aisum')")
                  .ExtractBool())
      << "the ai panel document has no .aisum element to render into";
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelector('.aisum').textContent")
               .ExtractString() == "ollama-thread-probe-summary";
  })) << "the ollama round trip never completed - actual text: "
      << content::EvalJs(panel,
                         "document.querySelector('.aisum').textContent")
             .ExtractString();

  std::string captured;
  {
    base::AutoLock lock(request_lock);
    captured = captured_request_body;
  }
  EXPECT_NE(captured.find("\"model\":\"llama3:70b-thread-probe\""),
            std::string::npos)
      << "the chosen ollamaModel never reached the request body - actual "
         "body: "
      << captured;
}

// the transport streamed already, but the panel waited for the last
// byte. The proof has to be a reading taken while the connection is still open
// - a two-line response delivered at once would render once and look the same.
IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       AStreamedAnswerReachesThePanelBeforeItCompletes) {
  auto stream = std::make_unique<net::test_server::ControllableHttpResponse>(
      embedded_test_server(), "/api/chat");
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-stream-probe-1132"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  const net::HostPortPair& ollama_host_port =
      embedded_test_server()->host_port_pair();
  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"ollama\","
      "\"ollamaHost\":\"%s\",\"ollamaPort\":%d,\"ollamaModel\":\"llama3\"}",
      ollama_host_port.host().c_str(), ollama_host_port.port()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  constexpr char kSummaryText[] =
      "document.querySelector('.aisum') ?"
      " document.querySelector('.aisum').textContent : ''";
  stream->WaitForRequest();
  stream->Send(
      "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n\r\n");
  stream->Send(R"({"message":{"content":"the first half "}})"
               "\n");

  // The measurement: text on screen while the response is still open. Before
  // this task the pane held a loader until Done() below.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kSummaryText)
               .ExtractString()
               .find("the first half") != std::string::npos;
  })) << "nothing rendered while the stream was open - actual text: "
      << content::EvalJs(panel, kSummaryText).ExtractString();

  stream->Send(R"({"message":{"content":"and the second"}})"
               "\n");
  stream->Done();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kSummaryText).ExtractString() ==
           "the first half and the second";
  })) << "the completed answer never replaced the partial one - actual text: "
      << content::EvalJs(panel, kSummaryText).ExtractString();
}

// the cap is a settings row, and the only proof it reaches the
// clamp is a request body cut at the number the file names. A unit test cannot
// see ComputeAiRouteFacts, which is the hop that reads it.
IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       TheConfiguredPageTextCapCutsTheLiveRequestBody) {
  constexpr char kTail[] = "cap-probe-tail-marker";
  base::Lock request_lock;
  std::string captured_request_body;  // guarded by request_lock
  embedded_test_server()->RegisterRequestHandler(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.GetURL().path() != "/cap-probe") {
          return nullptr;
        }
        auto response =
            std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("text/html");
        response->set_content("<p>" + std::string(5000, 'x') + kTail + "</p>");
        return response;
      }));
  embedded_test_server()->RegisterRequestHandler(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/api/chat") {
          return nullptr;
        }
        {
          base::AutoLock lock(request_lock);
          captured_request_body = request.content;
        }
        auto response =
            std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("application/json");
        response->set_content(R"({"message":{"content":"cap-probe-summary"}})"
                              "\n");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(browser(),
                            embedded_test_server()->GetURL("/cap-probe"),
                            ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);
  const net::HostPortPair& ollama_host_port =
      embedded_test_server()->host_port_pair();
  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"ollama\","
      "\"ollamaHost\":\"%s\",\"ollamaPort\":%d,"
      "\"ollamaModel\":\"llama3\",\"aiPageTextCap\":1000}",
      ollama_host_port.host().c_str(), ollama_host_port.port()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    base::AutoLock lock(request_lock);
    return !captured_request_body.empty();
  })) << "the ollama request never arrived, so there is no body to measure";

  std::string captured;
  {
    base::AutoLock lock(request_lock);
    captured = captured_request_body;
  }
  EXPECT_EQ(captured.find(kTail), std::string::npos)
      << "the page tail survived a 1000 character cap, so the row was ignored";
  EXPECT_NE(captured.find("xxxx"), std::string::npos)
      << "nothing of the page reached the model at all";
  EXPECT_NE(captured.find("CUT SHORT"), std::string::npos)
      << "the cut page was handed over with no sign it was cut";
  EXPECT_EQ(captured.find(atrium::kTruncatedMarker), std::string::npos)
      << "the internal marker leaked into the request body";
}

// any reachable test server resolves local, so the SSRF
// guard always refuses before a live round trip - observed one hop earlier.
IN_PROC_BROWSER_TEST_F(AtriumAiRouteBrowserTest,
                       CustomEndpointSelectionThreadsTheChosenModelIntoTheRequest) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeMarginaliaTestPage));
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigateParams nav_params(
      browser(),
      embedded_test_server()->GetURL("/marginalia-test?ai-route-probe-322-9-15b"),
      ui::PAGE_TRANSITION_TYPED);
  nav_params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&nav_params);
  ASSERT_TRUE(
      content::WaitForLoadStop(nav_params.navigated_or_inserted_contents));
  atrium::AtriumBrowserWindow::SetAiEnabled(true);

  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(https_server.Start());
  const std::string endpoint_url =
      https_server.GetURL("a.test", "/v1/chat/completions").spec();

  base::Lock model_lock;
  std::string captured_model;  // guarded by model_lock
  atrium_ai::SetCustomEndpointModelObserverForTesting(
      base::BindLambdaForTesting([&](const std::string& model) {
        base::AutoLock lock(model_lock);
        captured_model = model;
      }));

  WriteSettings(base::StringPrintf(
      "{\"schemaVersion\":1,\"appIconVariant\":\"solarized-ink\","
      "\"animationsEnabled\":true,\"startupBehavior\":\"restoreLastActive\","
      "\"aiEnabled\":true,\"aiModel\":\"customEndpoint\","
      "\"customEndpointUrl\":\"%s\","
      "\"customEndpointModel\":\"claude-3-5-sonnet-thread-probe\"}",
      endpoint_url.c_str()));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* panel = window->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  ASSERT_TRUE(content::EvalJs(panel, "!!document.querySelector('.aisum')")
                  .ExtractBool())
      << "the ai panel document has no .aisum element to render into";
  ASSERT_TRUE(content::ExecJs(
      panel, "document.dispatchEvent(new KeyboardEvent('keydown', "
             "{key: '`'}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel,
                           "document.querySelector('.aisum').textContent")
               .ExtractString()
               .find("That address is not reachable from here.") !=
           std::string::npos;
  })) << "a customEndpoint ai_model selection never reached the guarded "
         "custom path - actual text: "
      << content::EvalJs(panel,
                         "document.querySelector('.aisum').textContent")
             .ExtractString();

  std::string captured;
  {
    base::AutoLock lock(model_lock);
    captured = captured_model;
  }
  EXPECT_EQ(captured, "claude-3-5-sonnet-thread-probe")
      << "the chosen customEndpointModel never reached "
         "ChatViaCustomEndpoint";
  atrium_ai::SetCustomEndpointModelObserverForTesting(base::NullCallback());
}

// the sidebar's address/search field. No DOM assertion against
// sidebar_contents_ here (RULE ZERO) - LastPushedSidebarOriginForTesting()
// proves the same thing from the native side instead.

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       BackgroundTabNavigationDoesNotChangeTheSidebarUrl) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);

  const GURL active_url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(content::NavigateToURL(active, active_url));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->LastPushedSidebarOriginForTesting() ==
           url::Origin::Create(active_url).Serialize();
  })) << "the active tab's own navigation never reached PushSidebarUrl";

  NavigateParams params(browser(),
                        embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  content::WebContents* background = params.navigated_or_inserted_contents;
  ASSERT_TRUE(background);
  ASSERT_TRUE(content::WaitForLoadStop(background));

  // Mutation this pins: retargeting UrlBarObserver at every tab instead of
  // just the active one would flip this to the background tab's origin.
  EXPECT_EQ(window->LastPushedSidebarOriginForTesting(),
            url::Origin::Create(active_url).Serialize())
      << "a background tab's navigation changed the sidebar's tracked origin";
}

// the Developer Mode URL toolbar. DevBarShownForTesting() reads
// native visibility rather than asserting against dev_bar_contents_'s DOM
// (RULE ZERO).

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ToggleDevBarModeShowsAndHidesTheDevBarContainer) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(),
      embedded_test_server()->GetURL("/title1.html")));
  ASSERT_FALSE(window->DevBarShownForTesting());

  window->ToggleDevBarMode();
  EXPECT_TRUE(window->DevBarShownForTesting());

  // Mutation this pins: a toggle that only shows (never flips back) would
  // leave the sidebar's own address field permanently hidden.
  window->ToggleDevBarMode();
  EXPECT_FALSE(window->DevBarShownForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       LocalhostOriginShowsTheDevBarWithoutToggling) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL localhost_url(base::StringPrintf(
      "http://localhost:%d/title1.html", embedded_test_server()->port()));
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(), localhost_url));

  // Mutation this pins: dropping the localhost auto-show (lab parity) would
  // leave this false until Cmd+Shift+D, unlike every real browser's omnibox.
  EXPECT_TRUE(window->DevBarShownForTesting())
      << "a localhost origin did not auto-show the devbar";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarStaysDownWhenTheWindowHasNoTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(),
      embedded_test_server()->GetURL("/title1.html")));
  window->ToggleDevBarMode();
  ASSERT_TRUE(window->DevBarShownForTesting());

  browser()->tab_strip_model()->CloseAllTabs();

  // Mutation this pins: a `shown` that reads only dev_mode_on_ leaves the bar
  // up over an empty window, where it has no url and its buttons do nothing.
  EXPECT_FALSE(window->DevBarShownForTesting())
      << "the devbar stayed up over a window with no tab";
}

// AppKit hit-tests the devbar WebView's own NSView before Views's
// ViewTargeter runs, so a WebView still sized to the padded widget (bar +
// kDevBarShadowPadTop/X/Bottom) ate page clicks in that padding whatever
// DoesIntersectRect said. GetContainerBounds() reads the NSView's actual
// size, pinning the fix at the layer the bug lived in.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarWebViewShrinksToTheBarNotThePaddedWidget) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(),
      embedded_test_server()->GetURL("/title1.html")));

  window->ToggleDevBarMode();
  ASSERT_TRUE(window->DevBarShownForTesting());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->DevBarContentsForTesting() != nullptr; }));
  content::WebContents* contents = window->DevBarContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !contents->GetContainerBounds().IsEmpty(); }));

  // kDevBarHeight is 34. The padded widget this replaces was 64 (34 + 10 +
  // 20) - a regression back to FillLayout reads 64 here.
  EXPECT_EQ(contents->GetContainerBounds().height(), 34)
      << "the devbar's WebView still spans the shadow padding above/below it";
}

// while split, every pane wears its own floating bar.
namespace {

// Waits for bar `index` to be built, loaded and told its url.
content::WebContents* WaitForDevBar(atrium::AtriumBrowserWindow* window,
                                    size_t index,
                                    const GURL& url) {
  if (!base::test::RunUntil([&]() {
        return window->DevBarContentsForTesting(index) != nullptr;
      })) {
    return nullptr;
  }
  content::WebContents* bar = window->DevBarContentsForTesting(index);
  if (!content::WaitForLoadStop(bar)) {
    return nullptr;
  }
  const bool told = base::test::RunUntil([&]() {
    return content::EvalJs(bar, "document.querySelector('.dvurl').textContent")
               .ExtractString() == url.spec();
  });
  return told ? bar : nullptr;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SplitShowsOneDevBarPerPane) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* container = window->ActivePaneRingForTesting()->parent();
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  content::WebContents* third = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(first && second && third);
  ASSERT_EQ(window->DevBarsShownForTesting(), 0u)
      << "the control: no bar before the split, dev mode is off";

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  views::test::RunScheduledLayout(container);
  // Mutation this pins: the old single bar spans the container, centred on
  // the gutter, over neither pane.
  ASSERT_EQ(window->DevBarsShownForTesting(), 2u);
  for (size_t i = 0; i < 2; ++i) {
    const gfx::Rect pane = window->PaneBoundsForTesting(i);
    const gfx::Rect bar = window->DevBarBoundsForTesting(i);
    EXPECT_NEAR(bar.CenterPoint().x(), pane.CenterPoint().x(), 1) << i;
    EXPECT_EQ(bar.y(), pane.y()) << i;
    EXPECT_TRUE(pane.Contains(gfx::Rect(bar.x(), pane.y(), bar.width(), 1)))
        << "bar " << i << " spills out of its pane";
  }

  // The drop opens a column under the active pane: its bar sits at that
  // pane's own top, not the window's.
  window->InsertPaneFromDrop(third, atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(window->DevBarsShownForTesting(), 3u);
  const gfx::Rect lower = window->PaneBoundsForTesting(1);
  ASSERT_GT(lower.y(), window->PaneBoundsForTesting(0).y());
  EXPECT_EQ(window->DevBarBoundsForTesting(1).y(), lower.y());

  // Back to one pane with dev mode off and no localhost: today's rules put
  // no bar up.
  window->CloseSplit(first);
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(window->DevBarsShownForTesting(), 0u);
  EXPECT_FALSE(window->DevBarShownForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EachDevBarShowsItsOwnPaneUrl) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  const GURL url3 = embedded_test_server()->GetURL("/title3.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_TRUE(WaitForDevBar(window, 0, url1)) << "bar 0 never showed pane 0";
  // Mutation this pins: pushing the active tab's url to every bar.
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1) << "bar 1 never showed pane 1's url";

  // The inactive pane navigates on its own, and only its bar follows.
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first);
  ASSERT_TRUE(content::NavigateToURL(second, url3));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(bar1,
                           "document.querySelector('.dvurl').textContent")
               .ExtractString() == url3.spec();
  })) << "an inactive pane's navigation never reached its bar";
  EXPECT_EQ(content::EvalJs(window->DevBarContentsForTesting(0),
                            "document.querySelector('.dvurl').textContent")
                .ExtractString(),
            url1.spec());
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SplitDevBarsIgnoreScrollAndDevMode) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/false);
  const auto away = [&]() {
    return content::EvalJs(
               window->DevBarContentsForTesting(0),
               "document.querySelector('.devbar').classList.contains('away')")
        .ExtractBool();
  };

  // The control: the single bar in dev mode does slide away on this scroll,
  // so a false below is the split's doing, not a scroll that never landed.
  window->ToggleDevBarMode();
  ASSERT_TRUE(WaitForDevBar(window, 0, url1));
  for (int i = 0; i < 10; ++i) {
    window->OnPageScrolled(-40);
  }
  ASSERT_TRUE(base::test::RunUntil([&]() { return away(); }));
  window->ToggleDevBarMode();
  ASSERT_FALSE(window->DevBarShownForTesting());

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  // Mutation this pins: keeping the dev-mode/localhost gate while split.
  EXPECT_EQ(window->DevBarsShownForTesting(), 2u)
      << "split bars waited for dev mode or a localhost page";
  ASSERT_FALSE(away()) << "entering the split left the bar away";
  for (int i = 0; i < 10; ++i) {
    window->OnPageScrolled(-40);
  }
  // A round trip through the bar's document, so any away push has landed.
  ASSERT_TRUE(content::ExecJs(window->DevBarContentsForTesting(0), "0"));
  EXPECT_FALSE(away()) << "a split bar slid away on scroll";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarClickActivatesItsPane) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first);

  // Mutation this pins: a bar press that leaves the other pane active, so the
  // url edit it opens navigates the wrong page.
  ASSERT_TRUE(content::ExecJs(
      bar1,
      "document.querySelector('.dvurl').dispatchEvent("
      "new PointerEvent('pointerdown', {bubbles: true}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->GetActiveWebContents() == second;
  })) << "pressing pane 1's bar did not make pane 1 active";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SplitDevBarBackActsOnItsOwnPane) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  const GURL url3 = embedded_test_server()->GetURL("/title3.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  ASSERT_TRUE(content::NavigateToURL(first, url2));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url1, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_TRUE(content::NavigateToURL(second, url3));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url3);
  ASSERT_TRUE(bar1);
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first);

  // The message alone, with no press first: a pane that cannot be made active
  // (the notes pad) leaves pane 0 active when its bar's button fires.
  // Mutation this pins: a bar's back acting on the active pane, not its own.
  ASSERT_TRUE(content::ExecJs(bar1, "chrome.send('sidebarBackPressed', [])"));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return second->GetLastCommittedURL() == url1; }))
      << "pane 1's back never took pane 1 back";
  EXPECT_EQ(first->GetLastCommittedURL(), url2)
      << "pane 1's back navigated pane 0";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SplitDevBarUrlEditCommitsToItsOwnPane) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  const GURL url3 = embedded_test_server()->GetURL("/title3.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  // Only pane 1 is a favourite, so its in-place commit is observable - an
  // ordinary tab's edit always opens a new tab (OpenCommandBarMatch's
  // favourite gate) whichever pane sent it.
  atrium::SetFavoriteKeyForTab(second, url2.spec());

  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first);

  // The message alone, with no press first, the same gap
  // SplitDevBarBackActsOnItsOwnPane pins: pane 0 stays active while the
  // palette opens from pane 1's bar.
  ASSERT_TRUE(content::ExecJs(
      bar1, "chrome.send('devBarUrlClicked', [0, 0, 100, 20])"));
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), first)
      << "the click alone must not have activated pane 1";

  // Mutation this pins: the commit resolving through the active pane rather
  // than the bar that opened it.
  window->OpenCommandBarMatch(url3);

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return second->GetLastCommittedURL() == url3; }))
      << "pane 1's edit never landed on pane 1";
  EXPECT_EQ(first->GetLastCommittedURL(), url1)
      << "pane 1's edit navigated pane 0 instead";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       OnlyTheFocusedPanesDevBarIsRinged) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);

  EXPECT_TRUE(window->DevBarFocusedForTesting(0))
      << "pane 0 starts active on entering a split";
  EXPECT_FALSE(window->DevBarFocusedForTesting(1));
  const std::string ring = window->DevBarFocusRingForTesting(0);
  ASSERT_FALSE(ring.empty()) << "no ring color reached a split bar";
  EXPECT_EQ(ring[0], '#');
  EXPECT_EQ(ring, window->DevBarFocusRingForTesting(1))
      << "both bars carry the same ring color, only .focused differs";

  // Mutation this pins: a ring left on pane 0 after the click never follows
  // focus to pane 1.
  ASSERT_TRUE(content::ExecJs(
      bar1,
      "document.querySelector('.dvurl').dispatchEvent("
      "new PointerEvent('pointerdown', {bubbles: true}))"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return browser()->tab_strip_model()->GetActiveWebContents() == second;
  })) << "pressing pane 1's bar did not make pane 1 active";

  EXPECT_FALSE(window->DevBarFocusedForTesting(0));
  EXPECT_TRUE(window->DevBarFocusedForTesting(1));
}

// a devbar grip drag ends in these messages, with screen points.
namespace {

bool SendDevBarDrag(content::WebContents* bar,
                    const char* name,
                    const gfx::Point& screen_point) {
  return content::ExecJs(
      bar, base::StringPrintf("chrome.send('%s', [%d, %d])", name,
                              screen_point.x(), screen_point.y()));
}

gfx::Point LeftBandOfContents(content::WebContents* contents) {
  const gfx::Rect bounds = contents->GetContainerBounds();
  return gfx::Point(bounds.x() + bounds.width() / 20,
                    bounds.CenterPoint().y());
}

}  // namespace

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarReleaseOnOwnPaneEdgeRearranges) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  const gfx::Point left_of_first = LeftBandOfContents(first);

  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragMoved", left_of_first));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->IsContentDropPreviewVisibleForTesting(); }))
      << "hovering pane 0's left edge never showed its preview";

  // Mutation this pins: an own-window release routed to the other-window
  // path, which finds no window and tears the tab off.
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragReleased", left_of_first));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->PaneContentsForTesting(0) == second;
  })) << "pane 1 never moved to pane 0's left edge";
  EXPECT_TRUE(window->IsSplit());
  EXPECT_EQ(window->PaneContentsForTesting(1), first);
  EXPECT_FALSE(window->IsContentDropPreviewVisibleForTesting());
}

namespace {

// -[NSView accessibilityWindowAttribute] follows accessibilityParent with no
// bound, so a loop kills the process on the stack guard page.
constexpr size_t kMaxAccessibilityParentHops = 50;

void CollectWebViews(views::View* root, std::vector<views::WebView*>* out) {
  if (views::WebView* web_view = views::AsViewClass<views::WebView>(root)) {
    out->push_back(web_view);
  }
  for (views::View* child : root->children()) {
    CollectWebViews(child, out);
  }
}

std::string DescribeWebView(views::WebView* web_view) {
  content::WebContents* contents = web_view->web_contents();
  return contents ? contents->GetLastCommittedURL().possibly_invalid_spec()
                  : std::string("<no contents>");
}

// Empty when the chain terminates, otherwise why it did not. GetParentAccessible
// and GetNativeViewAccessible name the same elements, so they chain into a walk.
std::string AccessibilityParentChainFailure(
    views::WebView* start,
    const std::vector<views::WebView*>& all) {
  std::set<views::WebView*> seen;
  views::WebView* current = start;
  for (size_t hop = 0; hop < kMaxAccessibilityParentHops; ++hop) {
    if (!seen.insert(current).second) {
      return base::StringPrintf(
          "accessibility parent chain from %s loops back to %s after %zu hops",
          DescribeWebView(start).c_str(), DescribeWebView(current).c_str(),
          hop);
    }
    gfx::NativeViewAccessible parent = current->holder()->GetParentAccessible();
    if (!parent) {
      return std::string();
    }
    views::WebView* next = nullptr;
    for (views::WebView* candidate : all) {
      gfx::NativeViewAccessible element =
          candidate->holder()->GetNativeViewAccessible();
      if (element && element == parent) {
        next = candidate;
        break;
      }
    }
    // The parent is an ordinary Views node, so the rest of the walk is the
    // Views tree and it ends at the window.
    if (!next) {
      return std::string();
    }
    current = next;
  }
  return base::StringPrintf(
      "accessibility parent chain from %s did not terminate in %zu hops",
      DescribeWebView(start).c_str(), kMaxAccessibilityParentHops);
}

}  // namespace

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarWrapDoesNotMakeTheBarItsOwnAccessibilityParent) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  window->ToggleDevBarMode();
  content::WebContents* bar = WaitForDevBar(window, 0, url);
  ASSERT_TRUE(bar);

  std::vector<views::WebView*> web_views;
  CollectWebViews(window->widget()->GetRootView(), &web_views);
  views::WebView* bar_web_view = nullptr;
  for (views::WebView* candidate : web_views) {
    if (candidate->web_contents() == bar) {
      bar_web_view = candidate;
      break;
    }
  }
  ASSERT_TRUE(bar_web_view) << "the bar's page is in no WebView";

  views::NativeViewHost* holder = bar_web_view->holder();
  const gfx::NativeViewAccessible parent = holder->GetParentAccessible();
  EXPECT_TRUE(parent) << "the wrapped bar has no accessibility parent at all";
  // Mutation this pins: AtriumDevBar::Wrap attaching without repairing the
  // parent NativeViewHostMac takes from the WebView, which is the page itself.
  EXPECT_FALSE(parent == holder->GetNativeViewAccessible())
      << "the bar's page is its own accessibility parent";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarGripDropLeavesNoAccessibilityParentCycle) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  ASSERT_EQ(window->PaneContentsForTesting(0), first);
  const gfx::Point left_of_first = LeftBandOfContents(first);

  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragStarted", gfx::Point()));
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragMoved", left_of_first));
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragReleased", left_of_first));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->PaneContentsForTesting(0) == second;
  })) << "the grip drop never rearranged the split";
  ASSERT_TRUE(WaitForDevBar(window, 0, url2));
  ASSERT_TRUE(WaitForDevBar(window, 1, url1));

  std::vector<views::WebView*> web_views;
  CollectWebViews(window->widget()->GetRootView(), &web_views);
  ASSERT_GE(web_views.size(), 2u)
      << "the window kept no pane WebView to walk from";

  // Mutation this pins: a drop that leaves a pane's hosted view as its own
  // accessibility parent, which the crash reports reach through AXWindow.
  for (views::WebView* start : web_views) {
    const std::string failure =
        AccessibilityParentChainFailure(start, web_views);
    EXPECT_TRUE(failure.empty()) << failure;
  }
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarReleaseOnOwnSidebarDoesNothing) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  const int tabs_before = browser()->tab_strip_model()->count();
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const gfx::Point on_sidebar =
      window->SidebarContentsForTesting()->GetContainerBounds().CenterPoint();

  // No drop event reaches the sidebar here, the same as a release on its
  // search box or on a row that refuses the tab.
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragReleased", on_sidebar));
  // Ordered after the release on the same bar, and a preview proves it ran.
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragMoved",
                             LeftBandOfContents(first)));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->IsContentDropPreviewVisibleForTesting(); }));
  ASSERT_TRUE(content::ExecJs(window->SidebarContentsForTesting(), "0"));
  base::RunLoop().RunUntilIdle();
  // Mutation this pins: any release over the sidebar pulling the pane out.
  EXPECT_TRUE(window->IsSplit())
      << "a release the sidebar did not place pulled the pane out";
  EXPECT_EQ(window->PaneContentsForTesting(1), second);
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before)
      << "a release on the own sidebar opened a window";
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarPaneDroppedOnASidebarRowLeavesTheSplit) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url1 = embedded_test_server()->GetURL("/title1.html");
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  const GURL url3 = embedded_test_server()->GetURL("/title3.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(first, url1));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  content::WebContents* third =
      chrome::AddAndReturnTabAt(browser(), url3, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(third));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* bar1 = WaitForDevBar(window, 1, url2);
  ASSERT_TRUE(bar1);
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  const std::string third_row = base::StringPrintf(
      "document.querySelector('.railpane:not(.ovrailpane) "
      "[data-dropzone=\"tabs\"] .tabrow[data-tab-id=\"%s\"]')",
      base::NumberToString(atrium::TabId(third)).c_str());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return content::EvalJs(sidebar, "!!" + third_row).ExtractBool(); }))
      << "the third tab never got a sidebar row";

  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragStarted", gfx::Point()));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar,
                           "document.querySelector('.dragactive') !== null")
        .ExtractBool();
  })) << "the sidebar never took the bar's drag";
  // The top edge of the third row, where the reorder line goes above it.
  ASSERT_TRUE(content::ExecJs(
      sidebar, "(() => { const r = " + third_row +
                   "; const b = r.getBoundingClientRect(); "
                   "r.dispatchEvent(new DragEvent('drop', {bubbles: true, "
                   "cancelable: true, clientX: b.left + b.width / 2, "
                   "clientY: b.top + 2})); })()"));
  const gfx::Point on_sidebar = sidebar->GetContainerBounds().CenterPoint();
  ASSERT_TRUE(SendDevBarDrag(bar1, "devBarTabDragReleased", on_sidebar));
  EXPECT_TRUE(base::test::RunUntil([&]() { return !window->IsSplit(); }))
      << "a pane the sidebar placed stayed in the split";
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(second),
            TabStripModel::kNoTab)
      << "the placed tab left its window";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarReleaseOffEveryWindowTearsOff) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  content::WebContents* dragged =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(dragged));
  window->ToggleDevBarMode();
  content::WebContents* bar = WaitForDevBar(window, 0, url);
  ASSERT_TRUE(bar);
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const gfx::Rect bounds = window->GetBounds();
  const gfx::Point outside(bounds.right() + 200, bounds.bottom() + 200);

  ASSERT_TRUE(SendDevBarDrag(bar, "devBarTabDragReleased", outside));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() ==
           browsers_before + 1;
  })) << "a release off every window never tore the tab off";
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(dragged),
            TabStripModel::kNoTab)
      << "the torn-off tab is still in its old window";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       DevBarReleaseOnOtherWindowMoves) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  content::WebContents* dragged =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(dragged));
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));
  atrium::AtriumBrowserWindow* other = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* as_browser = candidate->GetBrowserForMigrationOnly();
        if (as_browser && as_browser != browser()) {
          other = atrium::BrowserController::PresentedWindowForBrowser(
              as_browser);
        }
        return !other;
      });
  ASSERT_TRUE(other);
  // A startup hold still pending at quit shows a browser already gone.
  ASSERT_TRUE(base::test::RunUntil([&]() { return other->IsVisible(); }));
  // Apart on purpose, so the point is in the other window and not this one.
  window->SetBounds(gfx::Rect(0, 0, 800, 600));
  other->SetBounds(gfx::Rect(900, 0, 800, 600));
  window->ToggleDevBarMode();
  content::WebContents* bar = WaitForDevBar(window, 0, url);
  ASSERT_TRUE(bar);

  // Mutation this pins: an own-window check that swallows every point.
  ASSERT_TRUE(SendDevBarDrag(bar, "devBarTabDragReleased",
                             other->GetBounds().CenterPoint()));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return other->browser()->tab_strip_model()->GetIndexOfWebContents(
               dragged) != TabStripModel::kNoTab;
  })) << "a release over the other window never moved the tab there";
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(dragged),
            TabStripModel::kNoTab);
}

// A sidebar row drag ends the same way as the grip's, from the sidebar route.
namespace {

void SendRowRelease(atrium::AtriumBrowserWindow* window,
                    content::WebContents* tab,
                    const gfx::Point& screen_point) {
  base::ListValue args;
  args.Append(base::NumberToString(atrium::TabId(tab)));
  args.Append(screen_point.x());
  args.Append(screen_point.y());
  window->OnSurfaceMessage("sidebarTabDragReleased", args);
}

}  // namespace

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowReleasedOnOwnPaneEdgeSplits) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title2.html"), -1,
      /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ASSERT_FALSE(window->IsSplit());
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();

  // Mutation this pins: an own-window release sent to the other-window path,
  // which finds no window and tears the tab off.
  SendRowRelease(window, second, LeftBandOfContents(first));
  EXPECT_TRUE(base::test::RunUntil([&]() { return window->IsSplit(); }))
      << "a row released on its own pane's edge opened no split";
  EXPECT_EQ(window->PaneContentsForTesting(0), second);
  EXPECT_EQ(window->PaneContentsForTesting(1), first);
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before)
      << "the row tore off into a new window";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowReleasedOnOwnSplitPaneRearranges) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL url2 = embedded_test_server()->GetURL("/title2.html");
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), url2, -1, /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  ASSERT_TRUE(WaitForDevBar(window, 1, url2));
  ASSERT_EQ(window->PaneContentsForTesting(0), first);

  SendRowRelease(window, second, LeftBandOfContents(first));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->PaneContentsForTesting(0) == second;
  })) << "the pane's row never moved it to pane 0's left edge";
  EXPECT_TRUE(window->IsSplit());
  EXPECT_EQ(window->PaneContentsForTesting(1), first);
  EXPECT_EQ(window->PaneContentsForTesting(2), nullptr)
      << "a rearrange grew the split";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowReleasedOnOwnPaneAtFourPanesDoesNothing) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::View* container = window->ActivePaneRingForTesting()->parent();
  ASSERT_TRUE(container);
  content::WebContents* first =
      browser()->tab_strip_model()->GetActiveWebContents();
  std::vector<content::WebContents*> others;
  for (int i = 0; i < 4; ++i) {
    others.push_back(chrome::AddAndReturnTabAt(
        browser(), GURL("about:blank"), -1, /*foreground=*/false));
  }
  window->EnterSplit(others[0], atrium::SplitOrientation::kSideBySide, 0.5);
  window->InsertPaneFromDrop(others[1], atrium::SplitEdge::kBottom);
  window->InsertPaneFromDrop(others[2], atrium::SplitEdge::kBottom);
  views::test::RunScheduledLayout(container);
  ASSERT_EQ(PaneViewCount(container), 4u);
  content::WebContents* fifth = others[3];
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const int tabs_before = browser()->tab_strip_model()->count();

  SendRowRelease(window, fifth, LeftBandOfContents(first));
  base::RunLoop().RunUntilIdle();
  views::test::RunScheduledLayout(container);
  EXPECT_EQ(PaneViewCount(container), 4u) << "a fifth pane opened";
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_NE(window->PaneContentsForTesting(i), fifth)
        << "the refused row still moved into pane " << i;
  }
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before)
      << "a refused own-window drop tore the tab off";
  EXPECT_EQ(browser()->tab_strip_model()->count(), tabs_before);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowReleasedOnOwnPaneInAnOffTheRecordWindowDoesNothing) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(incognito->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());
  content::WebContents* first =
      incognito->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(first);
  content::WebContents* second = chrome::AddAndReturnTabAt(
      incognito, GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(second);
  views::View* container = window->ActivePaneRingForTesting()->parent();
  views::test::RunScheduledLayout(container);
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();

  // Mutation this pins: the own-window path committing a split with no
  // WritesAllowed gate, which the devbar release already has.
  SendRowRelease(window, second, LeftBandOfContents(first));
  base::RunLoop().RunUntilIdle();
  views::test::RunScheduledLayout(container);
  EXPECT_FALSE(window->IsSplit()) << "an off-the-record window split";
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(), browsers_before)
      << "a refused own-window drop tore the tab off";
  EXPECT_NE(incognito->tab_strip_model()->GetIndexOfWebContents(second),
            TabStripModel::kNoTab);
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       RowReleasedOffEveryWindowTearsOff) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* dragged = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/false);
  ASSERT_TRUE(content::WaitForLoadStop(dragged));
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const gfx::Rect bounds = window->GetBounds();

  SendRowRelease(window, dragged,
                 gfx::Point(bounds.right() + 200, bounds.bottom() + 200));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return GlobalBrowserCollection::GetInstance()->GetSize() ==
           browsers_before + 1;
  })) << "a row released off every window never tore the tab off";
  EXPECT_EQ(browser()->tab_strip_model()->GetIndexOfWebContents(dragged),
            TabStripModel::kNoTab);
}

// Load phase fix: pins OnActiveTabLoadProgress's gate on
// the synthetic kInitialLoadProgress value FrameTreeNode::DidStartLoading
// fires in the same stack that pushes kWaiting.

IN_PROC_BROWSER_TEST_F(
    AtriumBrowserWindowBrowserTest,
    ReceivingIsNeverPushedForTheSyntheticInitialProgress) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(browser()->tab_strip_model()->GetActiveWebContents())
      << "the fixture's initial tab never loaded";

  window->SetLoadPhasePushRecordingForTesting(true);

  // Replays content::WebContentsObserver's real first two calls for a fresh
  // navigation: DidStartLoading, then LoadProgressChanged with the synthetic
  // value, both from the same stack.
  window->OnActiveTabStartedLoadingForTesting();
  window->OnActiveTabLoadProgressForTesting(blink::kInitialLoadProgress);

  // Mutation this pins: dropping the kInitialLoadProgress gate pushes
  // kReceiving right behind kWaiting, and phase 1 is never seen.
  const auto& pushes = window->LoadPhasePushesForTesting();
  ASSERT_EQ(pushes.size(), 1u)
      << "the synthetic initial progress call pushed a second phase";
  EXPECT_EQ(pushes[0].first, atrium::LoadPhase::kWaiting);

  window->OnActiveTabLoadProgressForTesting(0.4);
  ASSERT_EQ(pushes.size(), 2u)
      << "real progress past the initial value never reached the sidebar";
  EXPECT_EQ(pushes[1].first, atrium::LoadPhase::kReceiving);
  EXPECT_DOUBLE_EQ(pushes[1].second, 0.4)
      << "the fraction the address-bar fill reads did not arrive";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       FastFinishingLoadEndsInTheDonePhase) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(browser()->tab_strip_model()->GetActiveWebContents());

  window->SetLoadPhasePushRecordingForTesting(true);

  // FrameTreeNode::DidStopLoading fires LoadProgressChanged(kFinalLoadProgress)
  // before the WebContentsObserver::DidStopLoading that ends the run - the
  // same order a load with no intermediate renderer progress report sees.
  window->OnActiveTabStartedLoadingForTesting();
  window->OnActiveTabLoadProgressForTesting(blink::kInitialLoadProgress);
  window->OnActiveTabLoadProgressForTesting(1.0);
  window->OnActiveTabStoppedLoadingForTesting();

  const auto& pushes = window->LoadPhasePushesForTesting();
  ASSERT_FALSE(pushes.empty());
  // Mutation this pins: a phase-2 gate that also swallowed the final 1.0
  // report would leave the last push at kWaiting instead of kDone.
  EXPECT_EQ(pushes.back().first, atrium::LoadPhase::kDone);
  EXPECT_DOUBLE_EQ(pushes.back().second, 1.0);
}

IN_PROC_BROWSER_TEST_F(
    AtriumBrowserWindowBrowserTest,
    RealNavigationNeverPushesReceivingAtTheInitialProgress) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  // The fixture's initial tab is a real https://example.com/ this sandboxed
  // test never reaches, so WaitForLoadStop's page-type check would fail here.
  content::WaitForLoadStopWithoutSuccessCheck(active);

  window->SetLoadPhasePushRecordingForTesting(true);
  ASSERT_TRUE(content::NavigateToURL(
      active, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::WaitForLoadStop(active));

  const auto& pushes = window->LoadPhasePushesForTesting();
  ASSERT_FALSE(pushes.empty()) << "a real navigation pushed no phase at all";
  EXPECT_EQ(pushes.front().first, atrium::LoadPhase::kWaiting)
      << "the first push for a real navigation was not kWaiting";

  // Mutation this pins: the actual defect this task fixes - Chromium's real
  // observer call stack, not a hand-simulated one, must never surface the
  // synthetic kInitialLoadProgress call as kReceiving.
  for (const auto& push : pushes) {
    EXPECT_FALSE(push.first == atrium::LoadPhase::kReceiving &&
                 push.second <= blink::kInitialLoadProgress)
        << "kReceiving was pushed at or below the synthetic initial progress";
  }
  EXPECT_EQ(pushes.back().first, atrium::LoadPhase::kDone)
      << "the ring did not end in kDone after the load finished";
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SameDocumentNavigationDoesNotRestartTheLoadPhase) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  // The fixture's initial tab is a real https://example.com/ this sandboxed
  // test never reaches, so WaitForLoadStop's page-type check would fail here.
  content::WaitForLoadStopWithoutSuccessCheck(active);
  ASSERT_TRUE(content::NavigateToURL(
      active, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::WaitForLoadStop(active));

  window->SetLoadPhasePushRecordingForTesting(true);

  // pushState and a fragment jump raise the raw loading state
  // (LOADING_WITHOUT_UI), which reached DidStartLoading and replayed
  // waiting->done after the page had visibly finished.
  ASSERT_TRUE(content::ExecJs(active, "history.pushState({}, '', '#frag')"));
  ASSERT_TRUE(content::ExecJs(active, "history.replaceState({}, '', '#two')"));
  ASSERT_TRUE(content::WaitForLoadStop(active));

  // Mutation this pins: dropping either the ShouldShowLoadingUI gate in
  // LoadProgressObserver::DidStartLoading, or the matching gate in
  // OnActiveTabLoadProgress's re-arm, pushes a fresh kWaiting here.
  for (const auto& push : window->LoadPhasePushesForTesting()) {
    EXPECT_NE(push.first, atrium::LoadPhase::kWaiting)
        << "a same-document navigation restarted the load fill";
  }
}

IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       CancelledNavigationClearsTheLoadPhase) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  content::WebContents* active =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active);
  // The fixture's initial tab is a real https://example.com/ this sandboxed
  // test never reaches, so WaitForLoadStop's page-type check would fail here.
  content::WaitForLoadStopWithoutSuccessCheck(active);

  window->SetLoadPhasePushRecordingForTesting(true);
  const GURL url = embedded_test_server()->GetURL("/title2.html");
  content::TestNavigationManager manager(active, url);
  active->GetController().LoadURL(url, content::Referrer(),
                                  ui::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_TRUE(manager.WaitForRequestStart());

  active->Stop();

  // Mutation this pins: OnActiveTabStoppedLoading firing only for a
  // completed load would leave a cancelled run's ring pulsing kWaiting
  // forever.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    const auto& pushes = window->LoadPhasePushesForTesting();
    return !pushes.empty() && pushes.back().first == atrium::LoadPhase::kDone;
  })) << "cancelling the navigation never pushed kDone";
}

// Mirror View's open verb. Two spaces with DIFFERENT
// profileIDs, unlike AtriumSpaceSwitchBrowserTest's shared "Default" - a
// second, distinct profile is the whole point of these tests.
class AtriumMirrorProfileBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        spaces_path,
        "{\"spaces\":["
        "{\"id\":\"11111111-1111-1111-1111-111111111111\","
        "\"name\":\"Home\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"oceanBlue\",\"profileID\":\"Default\",\"order\":0,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"Home\"}]},"
        "{\"id\":\"22222222-2222-2222-2222-222222222222\","
        "\"name\":\"Vault\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"forestGreen\",\"profileID\":\"vault\",\"order\":1,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/\",\"title\":\"Vault\"}]},"
        "{\"id\":\"33333333-3333-3333-3333-333333333333\","
        "\"name\":\"Overflow\",\"emoji\":\"\xF0\x9F\xA7\xAA\",\"tint\":"
        "\"sunYellow\",\"profileID\":\"Default\",\"order\":2,"
        "\"pinnedTabs\":[],\"tabs\":[{\"lastActiveAt\":1.0,"
        "\"url\":\"https://example.com/overflow\",\"title\":\"Overflow\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

// Asserting only IsSplit() would still pass with both panes on the
// same profile - the silent survivor this feature exists to rule out.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorOpensSecondPaneUnderTheRequestedProfile) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // ProfileBootstrap's CreateProfileAsync for 'vault' races this test's
  // start.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::BrowserController::ResolveMirrorProfile("vault") !=
           nullptr;
  })) << "the 'vault' profile never bootstrapped";

  const GURL target = embedded_test_server()->GetURL("/title1.html");
  base::ListValue args;
  args.Append(target.spec());
  args.Append("vault");
  window->OnSurfaceMessage("sidebarTabMirrored", args);

  ASSERT_TRUE(window->IsSplit());
  content::WebContents* mirrored = window->MirrorContentsForTesting();
  ASSERT_TRUE(mirrored);
  ASSERT_TRUE(content::WaitForLoadStop(mirrored));

  Profile* vault_profile =
      atrium::BrowserController::ResolveMirrorProfile("vault");
  ASSERT_TRUE(vault_profile);
  EXPECT_EQ(mirrored->GetBrowserContext(), vault_profile);
  EXPECT_NE(mirrored->GetBrowserContext(), browser()->profile())
      << "the mirrored pane ran under the active profile, not the requested "
         "one";
}

IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorUnderLoggedOutUsesAFreshOffTheRecordProfile) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  base::ListValue args;
  args.Append(std::string("https://example.com/"));
  args.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);

  ASSERT_TRUE(window->IsSplit());
  content::WebContents* mirrored = window->MirrorContentsForTesting();
  ASSERT_TRUE(mirrored);
  EXPECT_TRUE(mirrored->GetBrowserContext()->IsOffTheRecord());
  EXPECT_NE(mirrored->GetBrowserContext(), browser()->profile());
}

// A profile id no space names must not crash the browser or open
// anything - the renderer's string is untrusted here by construction.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorWithAnUnknownProfileFailsCleanly) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  base::ListValue args;
  args.Append(std::string("https://example.com/"));
  args.Append(std::string("no-such-profile"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);

  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting());
}

// a mirror loads a renderer-supplied URL into a pane on ANOTHER
// profile, so it is held to the schemes the sidebar's other verbs allow.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorRefusesAPrivilegedScheme) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  for (const char* refused :
       {"file:///etc/passwd", "chrome://settings/",
        "chrome-extension://abcdefghijklmnopabcdefghijklmnop/x.html"}) {
    base::ListValue args;
    args.Append(std::string(refused));
    args.Append(std::string(" logged-out"));
    window->OnSurfaceMessage("sidebarTabMirrored", args);
    EXPECT_FALSE(window->IsSplit()) << refused << " opened a mirror pane";
    EXPECT_FALSE(window->MirrorContentsForTesting()) << refused;
  }

  // The other direction, so the guard cannot pass by mirroring being broken.
  base::ListValue ordinary;
  ordinary.Append(std::string("https://example.com/"));
  ordinary.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", ordinary);
  EXPECT_TRUE(window->IsSplit());
  EXPECT_TRUE(window->MirrorContentsForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorShowsProfileBadgesOnBothPanes) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  base::ListValue args;
  args.Append(std::string("https://example.com/"));
  args.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());

  // Mutation this pins: dropping either SetMirrorBadge call in
  // MirrorTabToProfile leaves that pane's badge text empty.
  EXPECT_EQ(window->MirrorBadgeTextForTesting(/*secondary=*/false), u"You");
  EXPECT_EQ(window->MirrorBadgeTextForTesting(/*secondary=*/true),
           u"Logged out");
}

IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest, MirrorEndsOnEscape) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::BrowserController::ResolveMirrorProfile("vault") !=
           nullptr;
  }));

  base::ListValue args;
  args.Append(embedded_test_server()->GetURL("/title1.html").spec());
  args.Append(std::string("vault"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());

  input::NativeWebKeyboardEvent escape(
      blink::WebKeyboardEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now());
  escape.windows_key_code = ui::VKEY_ESCAPE;

  // Mutation this pins: removing the Escape branch in HandleKeyboardEvent
  // leaves the split (and mirror_contents_) standing.
  EXPECT_TRUE(window->HandleKeyboardEvent(escape));
  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting())
      << "the mirrored pane survived Escape";
}

// picking a different tab is this window's own definition of
// "navigating" - it ends the mirror rather than silently replacing one side
// of it (ShowTab's Arc-parity swap, meant for an ordinary split).
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorEndsOnNavigatingToADifferentTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::BrowserController::ResolveMirrorProfile("vault") !=
           nullptr;
  }));

  base::ListValue args;
  args.Append(embedded_test_server()->GetURL("/title1.html").spec());
  args.Append(std::string("vault"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());

  NavigateParams params(browser(), embedded_test_server()->GetURL("/title2.html"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
  Navigate(&params);
  ASSERT_TRUE(content::WaitForLoadStop(params.navigated_or_inserted_contents));

  // Mutation this pins: dropping ShowTab's mirror_contents_ guard falls
  // straight into the Arc-style swap, leaving IsSplit() true.
  browser()->tab_strip_model()->ActivateTabAt(1);
  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting())
      << "the mirrored pane survived a tab switch";
}

// the row menu offers Mirror View with no live-tab
// requirement, and EnterSplit declines with no page in the active pane - a
// space of only cold rows, or an empty strip.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorDeclinesWhenNoPageIsShowing) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  Profile* original = browser()->profile()->GetOriginalProfile();
  const size_t otr_before = original->GetAllOffTheRecordProfiles().size();

  window->ShowTab(nullptr);
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), nullptr);

  base::ListValue args;
  args.Append(std::string("https://example.com/"));
  args.Append(std::string(" logged-out"));
  // Mutation this pins: without MirrorTabToProfile's is_split() check this
  // badges a null secondary_pane() and the browser dies here.
  window->OnSurfaceMessage("sidebarTabMirrored", args);

  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting());
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return original->GetAllOffTheRecordProfiles().size() == otr_before;
  })) << "the declined mirror kept its off-the-record profile";
}

// the closeSplitPane hotkey collapses through FinishCloseSplit,
// which never touched mirror_contents_ - Escape afterwards ran EndMirror on an
// unsplit container, and until that keypress the mirror profile stayed alive.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       ClosingAMirroredSplitEndsTheMirrorAndItsProfile) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  Profile* original = browser()->profile()->GetOriginalProfile();
  const size_t otr_before = original->GetAllOffTheRecordProfiles().size();

  base::ListValue args;
  args.Append(embedded_test_server()->GetURL("/title1.html").spec());
  args.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* mirrored = window->MirrorContentsForTesting();
  ASSERT_TRUE(mirrored);
  ASSERT_TRUE(mirrored->GetBrowserContext()->IsOffTheRecord());
  content::WaitForLoadStopWithoutSuccessCheck(mirrored);

  // BrowserController::CloseSplitPane's own body - the bound hotkey.
  window->CloseSplit(nullptr);

  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting())
      << "the split collapsed but the mirror stayed owned";
  EXPECT_EQ(window->MirrorBadgeTextForTesting(/*secondary=*/false),
            std::u16string())
      << "the surviving pane kept its mirror badge";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return original->GetAllOffTheRecordProfiles().size() == otr_before;
  })) << "the mirror's off-the-record profile outlived the split";

  input::NativeWebKeyboardEvent escape(
      blink::WebKeyboardEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now());
  escape.windows_key_code = ui::VKEY_ESCAPE;
  window->HandleKeyboardEvent(escape);
  EXPECT_FALSE(window->IsSplit());
}

// close_split naming the mirror pane as the keeper. A mirror is
// no strip tab, so re-homing it would leave the primary WebView holding a
// WebContents this window is about to free.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       ClosingAMirroredSplitOntoTheMirrorPaneStillEndsIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  Profile* original = browser()->profile()->GetOriginalProfile();
  const size_t otr_before = original->GetAllOffTheRecordProfiles().size();

  base::ListValue args;
  args.Append(embedded_test_server()->GetURL("/title1.html").spec());
  args.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* mirrored = window->MirrorContentsForTesting();
  ASSERT_TRUE(mirrored);
  content::WaitForLoadStopWithoutSuccessCheck(mirrored);

  window->CloseSplit(mirrored);

  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting());
  EXPECT_NE(window->CurrentlyDisplayedWebContents(), mirrored)
      << "the freed mirror was re-homed onto the surviving pane";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return original->GetAllOffTheRecordProfiles().size() == otr_before;
  })) << "the mirror's off-the-record profile outlived the split";
}

// Space 2 (Overflow) is same-profile as Home,
// unlike Vault, which only exists to host the mirror target.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       MirrorEndsOnSwitchingSpaces) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return atrium::BrowserController::ResolveMirrorProfile("vault") !=
           nullptr;
  }));

  base::ListValue args;
  args.Append(embedded_test_server()->GetURL("/title1.html").spec());
  args.Append(std::string("vault"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());

  ASSERT_TRUE(controller->SelectSpace(2));

  // Mutation this pins: dropping ShowTab's mirror_contents_ guard falls
  // straight into the Arc-style swap, leaving IsSplit() true.
  EXPECT_FALSE(window->IsSplit());
  EXPECT_FALSE(window->MirrorContentsForTesting())
      << "the mirrored pane survived a space switch";
}

// a Logged out mirror's OTR profile must not outlive the split.
IN_PROC_BROWSER_TEST_F(AtriumMirrorProfileBrowserTest,
                       EndingALoggedOutMirrorDestroysItsOffTheRecordProfile) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  base::ListValue args;
  args.Append(std::string("https://example.com/"));
  args.Append(std::string(" logged-out"));
  window->OnSurfaceMessage("sidebarTabMirrored", args);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* mirrored = window->MirrorContentsForTesting();
  ASSERT_TRUE(mirrored);
  Profile* otr_profile = Profile::FromBrowserContext(mirrored->GetBrowserContext());
  ASSERT_TRUE(otr_profile->IsOffTheRecord());
  const Profile::OTRProfileID otr_id = otr_profile->GetOTRProfileID();
  Profile* original = otr_profile->GetOriginalProfile();

  input::NativeWebKeyboardEvent escape(
      blink::WebKeyboardEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers, base::TimeTicks::Now());
  escape.windows_key_code = ui::VKEY_ESCAPE;
  window->HandleKeyboardEvent(escape);
  ASSERT_FALSE(window->IsSplit());

  // Mutation this pins: EndMirror without the ProfileDestroyer call leaves
  // this true forever - the leak this test guards against.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !original->HasOffTheRecordProfile(otr_id); }))
      << "the Logged out mirror's OTR profile outlived its split";
}

namespace {

// BrowserController has no public accessor for its own Browser*; the same
// technique atrium_little_never_adopted_browsertest.cc uses.
class CloseWindowNewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

}  // namespace

using AtriumCloseWindowBrowserTest = InProcessBrowserTest;

// Cmd+Shift+W on a window whose space has no live tab. CreateWindowSlot
// opens a wired, non-little window with an EMPTY strip - exactly the state the
// Last-tab-out interception in OnWindowCloseRequested otherwise keeps open
// forever. CloseUserInitiated must override that suppression.
IN_PROC_BROWSER_TEST_F(AtriumCloseWindowBrowserTest,
                       CloseUserInitiatedClosesATablessWindow) {
  CloseWindowNewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* slot = atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  capture_observation.Reset();
  ASSERT_TRUE(slot) << "CreateWindowSlot returned nullptr";
  ASSERT_TRUE(capture.captured_)
      << "CreateWindowSlot's Browser::Create did not fire OnBrowserCreated";
  ASSERT_FALSE(slot->IsLittle())
      << "a little window is exempt from the suppression this test targets";

  Browser* slot_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(slot_browser);
  ASSERT_TRUE(slot_browser->tab_strip_model()->empty())
      << "CreateWindowSlot no longer opens tabless - test setup assumption "
         "broke";

  auto* slot_window =
      static_cast<atrium::AtriumBrowserWindow*>(slot_browser->window());
  ASSERT_TRUE(slot_window);

  // RegisterBrowserDidClose fires synchronously from Browser::OnWindowClosing's
  // delete-scheduling branch. Measured instead of waiting for the widget's own
  // async teardown (WaitForBrowserToClose): a second, never-focused window
  // from CreateWindowSlot does not reliably finish that teardown in this
  // harness, which hung on both fixed and broken code alike.
  bool browser_did_close = false;
  base::CallbackListSubscription subscription =
      slot_browser->RegisterBrowserDidClose(base::BindLambdaForTesting(
          [&](BrowserWindowInterface*) { browser_did_close = true; }));

  // Mutation this pins: without the user-initiated flag, the empty-strip
  // suppression returns before OnWindowClosing runs, so this stays false.
  slot_window->CloseUserInitiated();
  EXPECT_TRUE(browser_did_close)
      << "OnWindowClosing's delete-scheduling branch never ran - the "
         "empty-strip suppression still applied to a user-initiated close";
}

// The rule, which nothing else pins: an ordinary tab that merely
// navigated to a favourite's address is a different tab, and adopting it loses
// whatever the favourite's own was doing. This test asserted that adoption,
// which was the old contract.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarFavoriteClickedKeepsItsOwnTabRatherThanAdoptingOne) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL favorite = embedded_test_server()->GetURL("/title1.html");
  const GURL other = embedded_test_server()->GetURL("/title2.html");
  // Opened by hand and never claimed: the tab the click must not take.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), favorite));
  content::WebContents* ordinary =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(ordinary);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), other, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();
  ASSERT_EQ(2, count_before);

  base::ListValue args;
  args.Append(favorite.spec());
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);

  // Mutation this pins: matching on the url instead of the claim adopts
  // `ordinary` here and leaves the count at 2.
  ASSERT_EQ(count_before + 1, tabs->count())
      << "the click took a tab that was never the tile's, or opened nothing";
  content::WebContents* claimed = tabs->GetActiveWebContents();
  ASSERT_TRUE(claimed);
  ASSERT_NE(ordinary, claimed) << "the click adopted the ordinary tab";
  ASSERT_TRUE(content::WaitForLoadStop(claimed));

  // Away from it, so a second click that does nothing reads as one that never
  // came back rather than as one that was already there.
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), other, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const int count_with_other = tabs->count();
  ASSERT_NE(claimed, tabs->GetActiveWebContents());

  // Now a claim exists, so the click returns to it rather than stacking a third.
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  EXPECT_EQ(count_with_other, tabs->count())
      << "the click opened another copy of a page that was already open";
  EXPECT_EQ(claimed, tabs->GetActiveWebContents())
      << "the click did not bring the favourite's own tab to the front";
}

// /title1.html above never moves, so the URL scan always found it and
// the case that bites went unseen - a favourite that redirects commits a URL
// the tile does not carry, so every click stacked another tab.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarFavoriteClickedTwiceReusesARedirectedTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL landing = embedded_test_server()->GetURL("/title1.html");
  const GURL favorite =
      embedded_test_server()->GetURL("/server-redirect?" + landing.spec());
  const GURL other = embedded_test_server()->GetURL("/title2.html");

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  // No tab id: the tile has nothing to offer on a first click, which is exactly
  // when the tab gets claimed.
  base::ListValue args;
  args.Append(favorite.spec());
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  ASSERT_EQ(count_before + 1, tabs->count())
      << "the first click must open the favourite";

  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_NE(favorite.spec(), opened->GetVisibleURL().spec())
      << "the redirect must land elsewhere, or the URL scan could still find it "
         "and this test would prove nothing";

  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), other, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  const int count_with_other = tabs->count();
  ASSERT_NE(opened, tabs->GetActiveWebContents());

  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  EXPECT_EQ(count_with_other, tabs->count())
      << "the second click opened another copy of a favourite that redirected";
  EXPECT_EQ(opened, tabs->GetActiveWebContents())
      << "the second click did not return to the favourite's own tab";
}

// The palette's WebContents has exactly one SurfaceHost, CommandBarWidget, and
// it used to drop every name it did not handle itself. Its only offered command
// therefore reached that method and stopped, which is why the palette read as
// broken rather than as sparse.
IN_PROC_BROWSER_TEST_F(AtriumSpaceSwitchBrowserTest,
                       CommandBarVerbReachesTheWindow) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsCourtyardShowing())
      << "the courtyard is already open, so this proves nothing";

  // Courtyard rather than Settings: it is observable from here without opening
  // a second window, and it travels the same forward every palette verb does.
  window->SendToCommandBarHostForTesting("sidebarCourtyardOpened");

  EXPECT_TRUE(window->IsCourtyardShowing())
      << "a verb sent through the palette's own host never reached the window";
}

// An unpacked MV3 extension whose only job is to carry `name` into the
// sidebar's actions snapshot and to open a popup when its action is clicked.
// A non-empty `key` pins the extension id across two profiles' own temp dirs.
scoped_refptr<const extensions::Extension> LoadActionExtension(
    Profile* profile,
    const std::string& name,
    extensions::TestExtensionDir* dir,
    const std::string& key = std::string()) {
  dir->WriteManifest(base::StringPrintf(
      R"({"name":"%s","version":"1","manifest_version":3,)"
      R"("action":{"default_title":"%s","default_popup":"popup.html"}%s})",
      name.c_str(), name.c_str(),
      key.empty() ? "" : (",\"key\":\"" + key + "\"").c_str()));
  dir->WriteFile(FILE_PATH_LITERAL("popup.html"), "<html>ext</html>");
  return extensions::ChromeTestExtensionLoader(profile).LoadExtension(
      dir->UnpackedPath());
}

// Both defects the presenter gate carried, in the state that separates them:
// two profiles in ONE slot, each with an extension the other does not have, so
// a snapshot or a click that resolved through the window's own controller
// names the wrong extension rather than merely the wrong pointer.
//
// Both activations are needed, in order - the first registers this slot's
// presenter, which is what makes the second a swap rather than a first
// activation (see DockedDevtoolsFollowsThePresentingWindow).
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarExtensionsFollowACrossProfileSwap) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-ext-swap"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  // show=false creates the window with no tabs, and a slot cannot swap in a
  // controller that has nothing to show.
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  extensions::TestExtensionDir alpha_dir;
  extensions::TestExtensionDir bravo_dir;
  scoped_refptr<const extensions::Extension> alpha =
      LoadActionExtension(browser()->profile(), "Ext Alpha", &alpha_dir);
  ASSERT_TRUE(alpha);
  scoped_refptr<const extensions::Extension> bravo =
      LoadActionExtension(&other, "Ext Bravo", &bravo_dir);
  ASSERT_TRUE(bravo);

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);
  EXPECT_NE(presenting->SidebarExtensionActionsJSONForTesting().find(
                "Ext Alpha"),
            std::string::npos)
      << "before any swap the sidebar must describe this window's own tab";

  atrium::BrowserController::ActivateWindow(second);
  ASSERT_EQ(atrium::BrowserController::PresentedWindowForBrowser(other_browser),
            presenting)
      << "the slot did not swap, so the rest of this test proves nothing";

  const std::string after =
      presenting->SidebarExtensionActionsJSONForTesting();
  EXPECT_NE(after.find("Ext Bravo"), std::string::npos)
      << "the swapped-in profile's actions never reached the sidebar on screen";
  EXPECT_EQ(after.find("Ext Alpha"), std::string::npos)
      << "the sidebar still describes the presenter's own, now hidden, tab";

  extensions::ExtensionHostRegistry* hosts =
      extensions::ExtensionHostRegistry::Get(&other);
  ASSERT_TRUE(hosts);
  ASSERT_TRUE(hosts->GetHostsForExtension(bravo->id()).empty())
      << "a popup is open before the click, so it cannot prove the click ran";
  base::ListValue args;
  args.Append(bravo->id());
  presenting->OnSurfaceMessage("pillActionClicked", args);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !hosts->GetHostsForExtension(bravo->id()).empty();
  })) << "the click resolved through the presenter's own profile, where this "
         "extension does not exist";

  // A rail pane of the profile the swipe is leaving draws that profile's own
  // pins, so both strips have to survive the swap above.
  const std::string& kept_first =
      presenting->SidebarExtensionActionsJSONForProfileForTesting(
          first->ProfileId());
  EXPECT_NE(kept_first.find("Ext Alpha"), std::string::npos)
      << "the outgoing profile's strip was dropped, so its pane swipes in "
         "empty";
  const std::string& kept_second =
      presenting->SidebarExtensionActionsJSONForProfileForTesting(
          second->ProfileId());
  EXPECT_NE(kept_second.find("Ext Bravo"), std::string::npos);
  EXPECT_NE(first->ProfileId(), second->ProfileId())
      << "one key for two profiles, so the test above proves nothing";
  EXPECT_TRUE(presenting
                  ->SidebarExtensionActionsJSONForProfileForTesting(
                      "atrium-never-loaded")
                  .empty());
}

// After a cross-profile swap the presenter's own browser_ is hidden, so a page
// opened there gets a row in a space that is not on screen.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ManageExtensionsOpensInTheSwappedInProfile) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-manage-swap"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);
  atrium::BrowserController::ActivateWindow(second);
  ASSERT_EQ(atrium::BrowserController::PresentedWindowForBrowser(other_browser),
            presenting)
      << "the slot did not swap, so the rest of this test proves nothing";

  const int own_tabs = browser()->tab_strip_model()->count();
  const int other_tabs = other_browser->tab_strip_model()->count();
  presenting->OpenExtensionsPage();
  EXPECT_EQ(other_browser->tab_strip_model()->count(), other_tabs + 1)
      << "the extensions page did not open in the profile on screen";
  EXPECT_EQ(browser()->tab_strip_model()->count(), own_tabs)
      << "the extensions page opened in the presenter's hidden profile";
}

// an off-the-record profile's GetBaseName() answers its parent's
// path, so an incognito controller keyed by profile_id_ would file its pins
// under the parent profile and a regular pane would swipe them in.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       AnIncognitoPushDoesNotOverwriteTheParentProfileStrip) {
  atrium::BrowserController* regular =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(regular);
  atrium::BrowserController::ActivateWindow(regular);

  extensions::TestExtensionDir alpha_dir;
  scoped_refptr<const extensions::Extension> alpha =
      LoadActionExtension(browser()->profile(), "Ext Alpha", &alpha_dir);
  ASSERT_TRUE(alpha);

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);
  ASSERT_NE(presenting
                ->SidebarExtensionActionsJSONForProfileForTesting(
                    regular->ProfileId())
                .find("Ext Alpha"),
            std::string::npos)
      << "the regular profile never filed its own strip, so the overwrite this "
         "test looks for could not be seen";

  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  atrium::BrowserController* otr =
      atrium::BrowserController::ControllerForBrowser(incognito);
  ASSERT_TRUE(otr);
  ASSERT_TRUE(otr->IsOffTheRecordProfile());
  ASSERT_EQ(otr->ProfileId(), regular->ProfileId())
      << "the id collision this guards against is gone, so the skip in "
         "PushActionsToWindow can go with it";

  atrium::ExtensionActionBridge::PushActionsToWindow(presenting, otr,
                                                     "[{\"id\":\"otr\"}]");
  EXPECT_NE(presenting
                ->SidebarExtensionActionsJSONForProfileForTesting(
                    regular->ProfileId())
                .find("Ext Alpha"),
            std::string::npos)
      << "the incognito window's pins were filed under its parent profile";
}

// SelectSpace can move the rail onto a different profile's space without any
// ActivateWindow (see ActiveSpaceProfileId's own doc), so the strip needs its
// own resync there rather than relying on the in-window-swap force push.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarExtensionsFollowASpaceSwitchAcrossProfiles) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-space-swap"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  extensions::TestExtensionDir dir;
  scoped_refptr<const extensions::Extension> extension =
      LoadActionExtension(&other, "Ext Charlie", &dir);
  ASSERT_TRUE(extension);

  ASSERT_TRUE(second->CreateSpace())
      << "the new profile's own space never landed in the shared document";
  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const base::DictValue* space = (*spaces)[i].GetIfDict();
    const std::string* profile_id =
        space ? space->FindString("profileId") : nullptr;
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_NE(other_index, -1) << "the new space never reached the sidebar model";

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);

  ASSERT_TRUE(first->SelectSpace(other_index));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return presenting->SidebarExtensionActionsJSONForTesting().find(
               "Ext Charlie") != std::string::npos;
  })) << "the strip never picked up the new space's own profile's extensions";

  // A pin toggle sent from the presenter's own surface must resolve against
  // the active space's profile, not the presenter's own (now-hidden) one.
  ToolbarActionsModel* other_model = ToolbarActionsModel::Get(&other);
  ASSERT_TRUE(other_model);
  const bool pinned_before = other_model->IsActionPinned(extension->id());
  presenting->ToggleExtensionPin(extension->id());
  EXPECT_NE(other_model->IsActionPinned(extension->id()), pinned_before)
      << "the toggle never reached the active space's own profile's model";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return presenting->SidebarExtensionActionsJSONForTesting().find(
               pinned_before ? "\"pinned\":false" : "\"pinned\":true") !=
           std::string::npos;
  })) << "the toggle never reached the strip pushed to this window";

  // A click sent from the same surface must run the active space's own
  // profile's action, not look it up (and fail) against the presenter's own.
  extensions::ExtensionHostRegistry* other_hosts =
      extensions::ExtensionHostRegistry::Get(&other);
  ASSERT_TRUE(other_hosts);
  ASSERT_TRUE(other_hosts->GetHostsForExtension(extension->id()).empty())
      << "a popup is open before the click, so it cannot prove the click ran";
  base::ListValue click_args;
  click_args.Append(extension->id());
  presenting->OnSurfaceMessage("pillActionClicked", click_args);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !other_hosts->GetHostsForExtension(extension->id()).empty();
  })) << "the click resolved through the presenter's own profile, where this "
         "extension does not exist";
}

// A rail pane of another profile draws that profile's tiles, so it must read
// that profile's icon cache, not the presenter's.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EachSpaceCarriesItsOwnProfilesIconScope) {
  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-icon-scope"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);
  ASSERT_TRUE(second->CreateSpace());

  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  const std::string own_base =
      browser()->profile()->GetBaseName().MaybeAsASCII();
  int checked_other = 0;
  int checked_own = 0;
  for (const base::Value& value : *spaces) {
    const base::DictValue& space = value.GetDict();
    const std::string* profile_id = space.FindString("profileId");
    const std::string* scope = space.FindString("iconScope");
    ASSERT_TRUE(profile_id);
    if (*profile_id == other_base) {
      ASSERT_TRUE(scope) << "the other profile's space carries no scope";
      EXPECT_EQ(*scope, atrium::IconScopeFor(&other));
      ++checked_other;
    } else if (*profile_id == own_base) {
      ASSERT_TRUE(scope) << "the presenter's own space carries no scope";
      EXPECT_EQ(*scope, atrium::IconScopeFor(browser()->profile()));
      ++checked_own;
    }
  }
  EXPECT_GT(checked_other, 0) << "the new space never reached the model";
  EXPECT_GT(checked_own, 0);
  EXPECT_NE(atrium::IconScopeFor(&other),
            atrium::IconScopeFor(browser()->profile()));

  // A push built mid-switch, from the browser the slot is leaving, already
  // describes the other profile's space at the top level.
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const std::string* profile_id =
        (*spaces)[i].GetDict().FindString("profileId");
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_TRUE(first->SelectSpace(other_index));
  std::optional<base::Value> leaving = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(leaving && leaving->is_dict());
  ASSERT_EQ(leaving->GetDict().FindInt("activeSpace"), other_index);
  const std::string* top = leaving->GetDict().FindString("iconScope");
  ASSERT_TRUE(top);
  EXPECT_EQ(*top, atrium::IconScopeFor(&other))
      << "the top-level tiles are the other profile's, so is their cache";
}

// A second window that switches to another profile's space must show that
// profile's strip in its own window, not leave its own profile on screen.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ASecondWindowShowsAnotherProfilesSpace) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-second-window"));
  atrium::BrowserController* other_default =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(other_default);
  ASSERT_TRUE(other_default->CreateSpace());

  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const base::DictValue* space = (*spaces)[i].GetIfDict();
    const std::string* profile_id =
        space ? space->FindString("profileId") : nullptr;
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_NE(other_index, -1);

  atrium::BrowserController* slot = atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  ASSERT_TRUE(slot);
  const uint64_t slot_id = atrium::BrowserController::SlotOf(slot);
  ASSERT_NE(slot_id, atrium::BrowserController::kDefaultSlot);

  Browser* slot_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* b = candidate->GetBrowserForMigrationOnly();
        if (atrium::BrowserController::ControllerForBrowser(b) == slot) {
          slot_browser = b;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(slot_browser);

  ASSERT_TRUE(slot->SelectSpace(other_index));

  Browser* other_in_slot = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* b = candidate->GetBrowserForMigrationOnly();
        atrium::BrowserController* c =
            atrium::BrowserController::ControllerForBrowser(b);
        if (b->profile() == &other && c &&
            atrium::BrowserController::SlotOf(c) == slot_id) {
          other_in_slot = b;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_in_slot)
      << "the second window never got a strip for the other profile";
  EXPECT_EQ(atrium::BrowserController::VisibleBrowserForBrowser(slot_browser),
            other_in_slot)
      << "the second window still shows its own profile's strip";
  EXPECT_EQ(atrium::BrowserController::VisibleBrowserForBrowser(browser()),
            browser())
      << "the switch in the second window moved the first window's strip";
}

// A switch to a space whose profile is still loading keeps the window's own
// strip, and the load that lands later swaps the profile's strip into both
// windows on that space.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ALateProfileLoadShowsItsStripInEveryWindowOnItsSpace) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  // The space exists before its profile does, as a deferred profile's does.
  ASSERT_TRUE(first->SelectSpace(
      atrium_core_space_for_profile(-1, "atrium-late-load")));
  atrium::BrowserController* slot = atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  ASSERT_TRUE(slot);
  const uint64_t slot_id = atrium::BrowserController::SlotOf(slot);
  Browser* slot_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* b = candidate->GetBrowserForMigrationOnly();
        if (atrium::BrowserController::ControllerForBrowser(b) == slot) {
          slot_browser = b;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(slot_browser);
  ASSERT_EQ(atrium::BrowserController::VisibleBrowserForBrowser(browser()),
            browser());
  ASSERT_EQ(atrium::BrowserController::VisibleBrowserForBrowser(slot_browser),
            slot_browser);

  // What ProfileBootstrap::OnProfileAdded does when the load lands.
  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& late = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-late-load"));
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &late, /*show=*/false, /*startup_url=*/std::string(),
      atrium::BrowserController::kDefaultSlot));
  atrium::BrowserController::ShowLoadedProfile(&late);

  Browser* first_shown =
      atrium::BrowserController::VisibleBrowserForBrowser(browser());
  ASSERT_TRUE(first_shown);
  EXPECT_EQ(first_shown->profile(), &late)
      << "the first window kept its own strip after the load";
  Browser* slot_shown =
      atrium::BrowserController::VisibleBrowserForBrowser(slot_browser);
  ASSERT_TRUE(slot_shown);
  EXPECT_EQ(slot_shown->profile(), &late)
      << "the second window kept its own strip after the load";
  EXPECT_EQ(atrium::BrowserController::SlotOf(
                atrium::BrowserController::ControllerForBrowser(slot_shown)),
            slot_id)
      << "the second window shows the first window's strip";
}

// Two windows of one profile show two spaces, and a switch in either one
// leaves the other window's rail where it was.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       EachWindowKeepsItsOwnSpace) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);
  ASSERT_TRUE(first->CreateSpace());

  const auto active_space = [](Browser* b) {
    std::optional<base::Value> model = base::JSONReader::Read(
        atrium::BuildSidebarTabsJSON(b), base::JSON_PARSE_RFC);
    return model && model->is_dict()
               ? model->GetDict().FindInt("activeSpace").value_or(-1)
               : -1;
  };
  const int home = active_space(browser());
  ASSERT_NE(home, -1);
  const int away = home == 0 ? 1 : 0;

  atrium::BrowserController* slot = atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  ASSERT_TRUE(slot);
  Browser* slot_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* b = candidate->GetBrowserForMigrationOnly();
        if (atrium::BrowserController::ControllerForBrowser(b) == slot) {
          slot_browser = b;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(slot_browser);
  EXPECT_EQ(active_space(slot_browser), home)
      << "a new window did not open on the space of the window it came from";

  ASSERT_TRUE(slot->SelectSpace(away));
  EXPECT_EQ(active_space(slot_browser), away);
  EXPECT_EQ(active_space(browser()), home)
      << "the switch in the second window moved the first window's rail";

  ASSERT_TRUE(first->SelectSpace(away));
  ASSERT_TRUE(slot->SelectSpace(home));
  EXPECT_EQ(active_space(browser()), away);
  EXPECT_EQ(active_space(slot_browser), home)
      << "the switch back in the second window moved the first window's rail";
}

// ActiveSpaceController() answers `this` when the target space shares the
// invoking controller's profile, so the return trip pushes nothing.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarExtensionsComeBackWhenASpaceSwitchReturns) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-space-return"));
  atrium::BrowserController* second =
      atrium::BrowserController::CreateForProfile(
          &other, /*show=*/false, /*startup_url=*/std::string(),
          atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  extensions::TestExtensionDir home_dir;
  extensions::TestExtensionDir away_dir;
  scoped_refptr<const extensions::Extension> home =
      LoadActionExtension(browser()->profile(), "Ext Delta", &home_dir);
  ASSERT_TRUE(home);
  scoped_refptr<const extensions::Extension> away =
      LoadActionExtension(&other, "Ext Echo", &away_dir);
  ASSERT_TRUE(away);

  ASSERT_TRUE(second->CreateSpace())
      << "the new profile's own space never landed in the shared document";
  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const base::DictValue* space = (*spaces)[i].GetIfDict();
    const std::string* profile_id =
        space ? space->FindString("profileId") : nullptr;
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_NE(other_index, -1) << "the new space never reached the sidebar model";
  const int home_index = atrium_core_active_space_index();
  ASSERT_NE(home_index, other_index)
      << "both spaces are the same one, so a return trip cannot be tested";

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);

  // Read straight after the call, with no wait: SelectSpace pushes
  // synchronously, and a wait passes on a later unrelated resync instead.
  ASSERT_TRUE(first->SelectSpace(other_index));
  EXPECT_NE(
      presenting->SidebarExtensionActionsJSONForTesting().find("Ext Echo"),
      std::string::npos)
      << "the strip never picked up the other profile's extensions";

  ASSERT_TRUE(first->SelectSpace(home_index));
  const std::string back = presenting->SidebarExtensionActionsJSONForTesting();
  EXPECT_NE(back.find("Ext Delta"), std::string::npos)
      << "the return trip left the other profile's snapshot on the strip";
  EXPECT_EQ(back.find("Ext Echo"), std::string::npos)
      << "the strip still describes the space that is no longer active";
}

// A store extension's id is the same in every profile, so a plain
// this-model-first membership check would resolve a shared id back to the
// presenter's own profile even after a cross-profile SelectSpace.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarExtensionsSharedIdResolvesToTheActiveSpace) {
  constexpr char kSharedKey[] =
      "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp9a7XZFuYX3btZ8QnPMO"
      "JXCoGPvqv7C5kZ8cM2dd2s626G1Qg0YOwKSBnpAT1coV1w88Nr09bVAZlypZnpW8"
      "8Z+CdgEetirxk9hs5OkWemoxf3pwyGRVdMoa5hhGv+lRbfPblQcvg6jLvSV/l6cq"
      "KtfnIZO1Af17AZjZQ3cgs8AijYGHC8wUgsKDYglIEk39W2SCBmvkQ2qb1VMSv1qa"
      "8x2SY/B6kzqxgn79Q+X98hwSbr8oQU0j7XKF9kTT6MCUTVkMwUREYX4Yp2NJ4HEX"
      "EQO8yY3O3gRVdo7jMUSwlNSPdicC9YiNnbKnUW2WL0e1PAIQ09k9emrKG3u4EvX7"
      "kwIDAQAB";

  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-shared-ext-id"));
  atrium::BrowserController* second = atrium::BrowserController::CreateForProfile(
      &other, /*show=*/false, /*startup_url=*/std::string(),
      atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  extensions::TestExtensionDir this_dir;
  extensions::TestExtensionDir other_dir;
  scoped_refptr<const extensions::Extension> this_extension =
      LoadActionExtension(browser()->profile(), "Ext Shared", &this_dir,
                          kSharedKey);
  ASSERT_TRUE(this_extension);
  scoped_refptr<const extensions::Extension> other_extension =
      LoadActionExtension(&other, "Ext Shared", &other_dir, kSharedKey);
  ASSERT_TRUE(other_extension);
  ASSERT_EQ(this_extension->id(), other_extension->id())
      << "the shared-key manifest never pinned matching ids across profiles";

  ASSERT_TRUE(second->CreateSpace())
      << "the new profile's own space never landed in the shared document";
  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const base::DictValue* space = (*spaces)[i].GetIfDict();
    const std::string* profile_id =
        space ? space->FindString("profileId") : nullptr;
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_NE(other_index, -1) << "the new space never reached the sidebar model";

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);
  ASSERT_TRUE(first->SelectSpace(other_index));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return presenting->SidebarExtensionActionsJSONForTesting().find(
               "Ext Shared") != std::string::npos;
  })) << "the strip never picked up the new space's own profile's extensions";

  ToolbarActionsModel* this_model = ToolbarActionsModel::Get(browser()->profile());
  ToolbarActionsModel* other_model = ToolbarActionsModel::Get(&other);
  ASSERT_TRUE(this_model);
  ASSERT_TRUE(other_model);
  const std::string& id = this_extension->id();
  const bool this_pinned_before = this_model->IsActionPinned(id);
  const bool other_pinned_before = other_model->IsActionPinned(id);

  presenting->ToggleExtensionPin(id);

  EXPECT_NE(other_model->IsActionPinned(id), other_pinned_before)
      << "the toggle never reached the active space's own profile's model";
  EXPECT_EQ(this_model->IsActionPinned(id), this_pinned_before)
      << "the shared id resolved back to the presenter's own profile";
}

// A devbar rebuild mid cross-profile SelectSpace used to re-pull the strip
// from this window's own controller (PushSidebarExtensionsForNewSurface bypassed
// the tracker), overwriting the active space's own snapshot with the wrong one.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       SidebarExtensionsSurviveASurfaceRebuildAcrossProfiles) {
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  atrium::BrowserController::ActivateWindow(first);

  ProfileManager* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(manager);
  Profile& other = profiles::testing::CreateProfileSync(
      manager, manager->user_data_dir().AppendASCII("atrium-surface-rebuild"));
  atrium::BrowserController* second = atrium::BrowserController::CreateForProfile(
      &other, /*show=*/false, /*startup_url=*/std::string(),
      atrium::BrowserController::kDefaultSlot);
  ASSERT_TRUE(second);

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        if (candidate->GetProfile() == &other) {
          other_browser = candidate->GetBrowserForMigrationOnly();
          return false;
        }
        return true;
      });
  ASSERT_TRUE(other_browser) << "the second profile has no browser";
  chrome::AddTabAt(other_browser, GURL(url::kAboutBlankURL), -1,
                   /*foreground=*/true);

  extensions::TestExtensionDir dir;
  scoped_refptr<const extensions::Extension> extension =
      LoadActionExtension(&other, "Ext Delta", &dir);
  ASSERT_TRUE(extension);

  ASSERT_TRUE(second->CreateSpace())
      << "the new profile's own space never landed in the shared document";
  const std::string other_base = other.GetBaseName().MaybeAsASCII();
  std::optional<base::Value> model = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser()), base::JSON_PARSE_RFC);
  ASSERT_TRUE(model && model->is_dict());
  const base::ListValue* spaces = model->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces);
  int other_index = -1;
  for (size_t i = 0; i < spaces->size(); ++i) {
    const base::DictValue* space = (*spaces)[i].GetIfDict();
    const std::string* profile_id =
        space ? space->FindString("profileId") : nullptr;
    if (profile_id && *profile_id == other_base) {
      other_index = static_cast<int>(i);
      break;
    }
  }
  ASSERT_NE(other_index, -1) << "the new space never reached the sidebar model";

  auto* presenting =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(presenting);
  ASSERT_TRUE(first->SelectSpace(other_index));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return presenting->SidebarExtensionActionsJSONForTesting().find(
               "Ext Delta") != std::string::npos;
  })) << "the strip never picked up the new space's own profile's extensions";

  // Simulates a devbar surface rebuilding (a navigation, a reload of the
  // widget's document) while the active space is still the other profile's.
  presenting->OnSurfaceMessage("devBarReady", base::ListValue());

  EXPECT_NE(presenting->SidebarExtensionActionsJSONForTesting().find(
                "Ext Delta"),
            std::string::npos)
      << "the surface rebuild pulled this window's own profile instead of "
         "the active space's";

  ToolbarActionsModel* other_model = ToolbarActionsModel::Get(&other);
  ASSERT_TRUE(other_model);
  const bool pinned_before = other_model->IsActionPinned(extension->id());
  presenting->ToggleExtensionPin(extension->id());
  EXPECT_NE(other_model->IsActionPinned(extension->id()), pinned_before)
      << "after the rebuild a pin toggle no longer reached the active "
         "space's own profile's model";
}

// Clear archives the rows and used to leave their pages open, so the page on
// screen outlived its row and could never get one back.
IN_PROC_BROWSER_TEST_F(AtriumBrowserWindowBrowserTest,
                       ClearClosesThePagesOfTheRowsItArchives) {
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process";
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_TRUE(chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1,
                                        /*foreground=*/true));
  const int before = strip->count();
  ASSERT_GE(before, 2);

  ASSERT_TRUE(window->ArchiveSpaceTabs()) << "the space had no loose rows";

  EXPECT_TRUE(base::test::RunUntil([&]() { return strip->count() < before; }))
      << "the archived rows' pages stayed open";
}
