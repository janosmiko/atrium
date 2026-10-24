// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/strings/stringprintf.h"
#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/api/side_panel/side_panel_api.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_extension_panel_width.h"
#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"
#include "chrome/browser/ui/atrium/atrium_side_panel_ui.h"
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"
#include "chrome/browser/ui/atrium/atrium_window_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "components/saved_tab_groups/public/saved_tab_group.h"
#include "components/saved_tab_groups/public/tab_group_sync_service.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/test_extension_dir.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/compositor/layer.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace atrium {
namespace {

// Never installed: the slot tests only need an id to log and store under.
constexpr char kFakeExtensionId[] = "abcdefghijklmnopabcdefghijklmnop";
constexpr char kSimpleDefaultPanel[] = "api_test/side_panel/simple_default";

ui::MouseEvent LeftMouse(ui::EventType type, int root_x) {
  // Only the root x matters: the grip reads the drag as a root x delta.
  return ui::MouseEvent(type, gfx::Point(3, 10), gfx::Point(root_x, 300),
                        ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                        ui::EF_LEFT_MOUSE_BUTTON);
}

constexpr char kPanelManifest[] = R"({
  "name": "Atrium side panel test", "version": "1", "manifest_version": 3,
  "permissions": ["sidePanel"],
  "side_panel": {"default_path": "panel.html"},
  "action": {"default_title": "Side panel test"},
  "background": {"service_worker": "worker.js"}
})";

// No default_path, so no window entry: only a tab with its own options has a
// panel.
constexpr char kTabOnlyPanelManifest[] = R"({
  "name": "Atrium side panel test", "version": "1", "manifest_version": 3,
  "permissions": ["sidePanel"],
  "action": {"default_title": "Side panel test"},
  "background": {"service_worker": "worker.js"}
})";

// Reports onClosed, and says ready once its listener exists.
constexpr char kWorker[] = R"(
  chrome.sidePanel.onClosed.addListener(() => chrome.test.sendMessage('closed'));
  chrome.test.sendMessage('ready');
)";

// openPanelOnActionClick: a left-click opens the panel, as Claude for Chrome
// sets it.
constexpr char kActionClickWorker[] = R"(
  chrome.sidePanel.onClosed.addListener(() => chrome.test.sendMessage('closed'));
  chrome.sidePanel.setPanelBehavior({openPanelOnActionClick: true})
      .then(() => chrome.test.sendMessage('ready'));
)";

// Claude for Chrome's permissions. No default_path: its panel is per tab.
constexpr char kReaderManifest[] = R"({
  "name": "Atrium side panel reader", "version": "1", "manifest_version": 3,
  "permissions": ["sidePanel", "tabs", "tabGroups", "scripting", "debugger"],
  "host_permissions": ["<all_urls>"],
  "action": {"default_title": "Side panel reader"},
  "background": {"service_worker": "worker.js"}
})";

// Claude for Chrome's page-access steps. Function declarations only: the test
// evaluates this again before every call, and a second const would throw.
constexpr char kReaderProbeJs[] = R"JS(
async function atriumStep(steps, name, fn) {
  try {
    const value = await Promise.race([fn(), new Promise((_, reject) =>
        setTimeout(() => reject(new Error('no answer in 5s')), 5000))]);
    steps.push({name, ok: true, value: value === undefined ? null : value});
    return value;
  } catch (e) {
    steps.push({name, ok: false, error: String((e && e.message) || e)});
    return undefined;
  }
}
async function atriumPanelIds(realTab, realWindow) {
  const steps = [];
  await atriumStep(steps, 'tabs.query(active, currentWindow)', async () => {
    const tabs = (await chrome.tabs.query({active: true, currentWindow: true}))
        .map(t => ({id: t.id, windowId: t.windowId}));
    if (tabs.length !== 1 || tabs[0].id !== realTab ||
        tabs[0].windowId !== realWindow) {
      throw new Error(`got ${JSON.stringify(tabs)}, want tab ${realTab} ` +
                      `in window ${realWindow}`);
    }
    return tabs[0];
  });
  await atriumStep(steps, 'windows.getCurrent', async () => {
    const id = (await chrome.windows.getCurrent()).id;
    if (id !== realWindow) {
      throw new Error(`got window ${id}, want ${realWindow}`);
    }
    return id;
  });
  return steps;
}
async function atriumGroup(tabId, windowId) {
  const steps = [];
  await atriumStep(steps, 'tabs.ungroup', () => chrome.tabs.ungroup(tabId));
  const groupId = await atriumStep(steps, 'tabs.group', () => chrome.tabs.group(
      {tabIds: [tabId], createProperties: {windowId}}));
  await atriumStep(steps, 'tabGroups.update', async () => (
      await chrome.tabGroups.update(groupId, {title: 'Claude',
                                              color: 'orange'})).id);
  await atriumStep(steps, 'tabs.query(groupId)', async () => {
    const ids = (await chrome.tabs.query({groupId})).map(t => t.id);
    if (ids.length !== 1 || ids[0] !== tabId) {
      throw new Error('the group holds ' + JSON.stringify(ids));
    }
    return ids;
  });
  return {groupId: groupId === undefined ? -1 : groupId, steps};
}
async function atriumTool(tabId, groupId) {
  const steps = [];
  await atriumStep(steps, 'tabs.get(T).groupId', async () => {
    const t = await chrome.tabs.get(tabId);
    if (groupId === -1 || t.groupId !== groupId) {
      throw new Error(`groupId ${t.groupId}, want ${groupId} ` +
                      `(windowId ${t.windowId})`);
    }
    return {groupId: t.groupId, windowId: t.windowId};
  });
  await atriumStep(steps, 'scripting.executeScript', async () => (
      await chrome.scripting.executeScript(
          {target: {tabId}, func: () => document.title}))[0].result);
  const attached = await atriumStep(steps, 'debugger.attach', async () => {
    await chrome.debugger.attach({tabId}, '1.3');
    return true;
  });
  if (!attached) {
    steps.push({name: 'Page.captureScreenshot', ok: false,
                error: 'skipped, the attach failed'});
    return steps;
  }
  await atriumStep(steps, 'Page.captureScreenshot', async () => (
      await chrome.debugger.sendCommand({tabId}, 'Page.captureScreenshot'))
      .data.length);
  await chrome.debugger.detach({tabId}).catch(() => {});
  return steps;
}
)JS";

// Logs the probe's answer whole, then fails once per failed step.
void ExpectEveryStepOk(const std::string& where, const base::Value& steps) {
  LOG(WARNING) << "ATRIUM-PROBE " << where << " "
               << base::WriteJson(steps).value_or("<unserializable>");
  ASSERT_TRUE(steps.is_list()) << where << " returned no step list";
  for (const base::Value& step : steps.GetList()) {
    const base::DictValue& dict = step.GetDict();
    const std::string* name = dict.FindString("name");
    const std::string* error = dict.FindString("error");
    EXPECT_TRUE(dict.FindBool("ok").value_or(false))
        << where << ": " << (name ? *name : "?")
        << " failed: " << (error ? *error : "");
  }
}

void ExpectOwnGroup(TabStripModel* model,
                    content::WebContents* contents,
                    const tab_groups::TabGroupId& group,
                    const tab_groups::TabGroupVisualData& visual,
                    const std::string& where) {
  const int index = model->GetIndexOfWebContents(contents);
  ASSERT_NE(index, TabStripModel::kNoTab) << where << ": the tab is elsewhere";
  EXPECT_EQ(model->GetTabGroupForTab(index), group)
      << where << ": the tab lost its group";
  ASSERT_TRUE(model->group_model() &&
              model->group_model()->ContainsTabGroup(group))
      << where << ": the group did not come along";
  const TabGroup* found = model->group_model()->GetTabGroup(group);
  EXPECT_EQ(found->tab_count(), 1) << where;
  EXPECT_EQ(found->visual_data()->title(), visual.title()) << where;
  EXPECT_EQ(found->visual_data()->color(), visual.color()) << where;
}

class AtriumExtensionSidePanelTest : public extensions::ExtensionBrowserTest {
 protected:
  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // A null `profile` loads into the test's own profile.
  const extensions::Extension* LoadPanelExtension(
      const char* worker_js,
      Profile* profile = nullptr,
      const char* manifest = kPanelManifest,
      const std::string& panel_html = "<html><body>panel</body></html>") {
    auto& dir = test_dirs_.emplace_back(
        std::make_unique<extensions::TestExtensionDir>());
    dir->WriteManifest(manifest);
    dir->WriteFile(FILE_PATH_LITERAL("panel.html"), panel_html);
    dir->WriteFile(FILE_PATH_LITERAL("worker.js"), worker_js);
    ExtensionTestMessageListener ready("ready");
    const extensions::Extension* extension = nullptr;
    if (!profile || profile == this->profile()) {
      extension = LoadExtension(dir->UnpackedPath());
    } else {
      extensions::ChromeTestExtensionLoader loader(profile);
      extension = loader.LoadExtension(dir->UnpackedPath()).get();
    }
    if (!extension || !ready.WaitUntilSatisfied()) {
      return nullptr;
    }
    return extension;
  }

  // chrome.sidePanel.open() with a user gesture, as Chromium's own side panel
  // browser tests run it.
  void RunOpenPanel(const extensions::Extension& extension,
                    std::optional<int> tab_id,
                    std::optional<int> window_id,
                    Profile* profile) {
    auto function = base::MakeRefCounted<extensions::SidePanelOpenFunction>();
    function->set_extension(&extension);
    base::DictValue options;
    if (tab_id) {
      options.Set("tabId", *tab_id);
    }
    if (window_id) {
      options.Set("windowId", *window_id);
    }
    function->set_user_gesture(true);
    EXPECT_TRUE(extensions::api_test_utils::RunFunction(
        function.get(),
        base::WriteJson(base::ListValue().Append(std::move(options)))
            .value_or(""),
        profile))
        << function->GetError();
  }

  void RunOpenWindowPanel(const extensions::Extension& extension,
                          Browser* target) {
    RunOpenPanel(extension, std::nullopt,
                 extensions::ExtensionTabUtil::GetWindowId(target),
                 target->profile());
  }

  void RunSetTabOptions(const extensions::Extension& extension, int tab_id) {
    auto function =
        base::MakeRefCounted<extensions::SidePanelSetOptionsFunction>();
    function->set_extension(&extension);
    const std::string args = base::StringPrintf(
        R"([{"tabId":%d,"path":"panel.html","enabled":true}])", tab_id);
    EXPECT_TRUE(extensions::api_test_utils::RunFunction(function.get(), args,
                                                        profile()))
        << function->GetError();
  }

  content::WebContents* AddTab(Browser* target,
                               bool foreground = true,
                               const GURL& url = GURL("about:blank")) {
    content::WebContents* contents =
        chrome::AddAndReturnTabAt(target, url, -1, foreground);
    EXPECT_TRUE(contents && content::WaitForLoadStop(contents));
    return contents;
  }

  bool WaitForPanel(AtriumBrowserWindow* w, bool showing) {
    return base::test::RunUntil(
        [&]() { return w->ExtensionPanelShowing() == showing; });
  }

  // Synchronous, unlike onClosed: destroying the panel view destroys the page
  // with it.
  bool PanelPageAlive(Profile* profile,
                      const extensions::Extension& extension) {
    return !extensions::ProcessManager::Get(profile)
                ->GetRenderFrameHostsForExtension(extension.id())
                .empty();
  }

  struct OtherProfile {
    raw_ptr<Profile> profile = nullptr;
    raw_ptr<BrowserController> controller = nullptr;
    raw_ptr<Browser> browser = nullptr;
  };

  // A second profile in the same slot, not shown yet.
  OtherProfile CreateOtherProfile(const char* name) {
    BrowserController::ActivateWindow(
        BrowserController::ControllerForBrowser(browser()));
    ProfileManager* manager = g_browser_process->profile_manager();
    Profile& other = profiles::testing::CreateProfileSync(
        manager, manager->user_data_dir().AppendASCII(name));
    OtherProfile result;
    result.profile = &other;
    result.controller = BrowserController::CreateForProfile(
        &other, /*show=*/false, /*startup_url=*/std::string(),
        BrowserController::kDefaultSlot);
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* candidate) {
          if (candidate->GetProfile() == &other) {
            result.browser = candidate->GetBrowserForMigrationOnly();
            return false;
          }
          return true;
        });
    if (result.browser) {
      AddTab(result.browser);
    }
    return result;
  }

  // The appearance is process-wide, so a theme set here would reach later
  // tests.
  void TearDownOnMainThread() override {
    AtriumBrowserWindow::SetWindowAppearance(WindowAppearance());
    extensions::ExtensionBrowserTest::TearDownOnMainThread();
  }

 private:
  std::vector<std::unique_ptr<extensions::TestExtensionDir>> test_dirs_;
};

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AShownViewTakesTheEdgeOppositeTheSidebar) {
  AtriumBrowserWindow* w = window();
  ASSERT_TRUE(w);
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  const int page_before = w->GetWebContentBoundsInScreen().width();

  auto view = std::make_unique<views::View>();
  views::View* raw = view.get();
  w->ShowExtensionPanel(std::move(view), /*owner=*/nullptr, kFakeExtensionId);

  EXPECT_TRUE(w->ExtensionPanelShowing());
  EXPECT_EQ(w->ExtensionPanelContentForTesting(), raw);
  const gfx::Rect panel = w->ExtensionPanelBoundsForTesting();
  EXPECT_EQ(panel.width(), ResolveExtensionPanelWidth(std::nullopt, 1400));
  // The sidebar docks left by default, so the panel ends at the right edge.
  EXPECT_EQ(panel.right(), w->GetBounds().width());
  EXPECT_LT(w->GetWebContentBoundsInScreen().width(), page_before)
      << "the panel did not take its width from the page";
  const std::vector<gfx::Rect> regions = w->ClientRegionsForTesting();
  EXPECT_TRUE(std::find(regions.begin(), regions.end(), panel) != regions.end())
      << "the panel hit-tests as caption, so a press drags the window";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       DetachHandsTheViewBackAndCollapsesTheSlot) {
  AtriumBrowserWindow* w = window();
  ASSERT_TRUE(w);
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  const int page_before = w->GetWebContentBoundsInScreen().width();
  auto view = std::make_unique<views::View>();
  views::View* raw = view.get();
  w->ShowExtensionPanel(std::move(view), /*owner=*/nullptr, kFakeExtensionId);

  std::unique_ptr<views::View> back = w->DetachExtensionPanel();

  EXPECT_EQ(back.get(), raw) << "a detach must hand the same view back";
  EXPECT_FALSE(w->ExtensionPanelShowing());
  EXPECT_EQ(w->ExtensionPanelBoundsForTesting().width(), 0);
  EXPECT_EQ(w->GetWebContentBoundsInScreen().width(), page_before);
  EXPECT_FALSE(w->DetachExtensionPanel()) << "a second detach has no view";
  EXPECT_FALSE(w->ExtensionPanelShadowVisibleForTesting())
      << "a closed panel kept its shadow layer";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AClosedPanelStaysShadowlessAfterLeavingFullscreen) {
  AtriumBrowserWindow* w = window();
  ASSERT_TRUE(w);
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  w->SetRootImmersiveForTesting(true);
  w->SetRootImmersiveForTesting(false);
  EXPECT_FALSE(w->ExtensionPanelShowing());
  EXPECT_FALSE(w->ExtensionPanelShadowVisibleForTesting())
      << "leaving fullscreen turned the shadow on with no panel";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AGripDragWidensThePanelAndStoresTheWidth) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kSimpleDefaultPanel));
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        extension->id());
  ASSERT_EQ(w->ExtensionPanelBoundsForTesting().width(), 400);

  views::View* grip = w->ExtensionPanelGripForTesting();
  ASSERT_TRUE(grip);
  // The panel sits on the right, so its grip is its leftmost column.
  EXPECT_EQ(grip->x(), 0);
  ui::MouseEvent press = LeftMouse(ui::EventType::kMousePressed, 500);
  ASSERT_TRUE(grip->OnMousePressed(press));
  ui::MouseEvent drag = LeftMouse(ui::EventType::kMouseDragged, 400);
  grip->OnMouseDragged(drag);
  ui::MouseEvent release = LeftMouse(ui::EventType::kMouseReleased, 400);
  grip->OnMouseReleased(release);

  EXPECT_EQ(w->ExtensionPanelBoundsForTesting().width(), 500);
  int stored = 0;
  ASSERT_TRUE(extensions::ExtensionPrefs::Get(profile())->ReadPrefAsInteger(
      extension->id(), kExtensionPanelWidthPref, &stored));
  EXPECT_EQ(stored, 500);
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AStoredWidthComesBackOnTheNextShow) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kSimpleDefaultPanel));
  ASSERT_TRUE(extension);
  extensions::ExtensionPrefs::Get(profile())->UpdateExtensionPref(
      extension->id(), kExtensionPanelWidthPref, base::Value(360));
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));

  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        extension->id());

  EXPECT_EQ(w->ExtensionPanelBoundsForTesting().width(), 360);
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ANarrowedWindowCapsThePanelAtHalf) {
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        kFakeExtensionId);
  ASSERT_EQ(w->ExtensionPanelBoundsForTesting().width(), 400);

  w->SetBounds(gfx::Rect(0, 0, 700, 900));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return w->ExtensionPanelBoundsForTesting().width() == 350;
  })) << "got " << w->ExtensionPanelBoundsForTesting().width();
}

IN_PROC_BROWSER_TEST_F(
    AtriumExtensionSidePanelTest,
    TheHeaderNamesTheExtensionAndItsCloseButtonHidesThePanel) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kSimpleDefaultPanel));
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        extension->id());

  EXPECT_EQ(w->ExtensionPanelTitleForTesting(),
            u"Extension with default side panel path");
  views::Button* close = w->ExtensionPanelCloseButtonForTesting();
  ASSERT_TRUE(close);
  views::test::ButtonTestApi(close).NotifyClick(
      LeftMouse(ui::EventType::kMousePressed, 0));

  EXPECT_FALSE(w->ExtensionPanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       SidePanelOpenShowsThePanelInTheWindow) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());

  RunOpenWindowPanel(*extension, browser());

  EXPECT_TRUE(WaitForPanel(window(), true));
  EXPECT_EQ(AtriumSidePanelUI::From(browser())->CurrentExtensionId(),
            extension->id());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       WindowCloseInThePanelHidesItAndFiresOnClosed) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(window(), true));
  auto* web_view =
      static_cast<views::WebView*>(window()->ExtensionPanelContentForTesting());
  ASSERT_TRUE(web_view->web_contents());
  ASSERT_TRUE(content::WaitForLoadStop(web_view->web_contents()));
  ExtensionTestMessageListener closed("closed");

  // Async: the page goes away mid-script, so ExecJs would report a failure.
  content::ExecuteScriptAsync(web_view->web_contents(), "window.close()");

  EXPECT_TRUE(WaitForPanel(window(), false));
  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       TheHeaderCloseClosesThroughTheOwnerAndFiresOnClosed) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(window(), true));
  ExtensionTestMessageListener closed("closed");

  views::test::ButtonTestApi(window()->ExtensionPanelCloseButtonForTesting())
      .NotifyClick(LeftMouse(ui::EventType::kMousePressed, 0));

  EXPECT_FALSE(window()->ExtensionPanelShowing());
  AtriumSidePanelUI* owner = AtriumSidePanelUI::From(browser());
  EXPECT_FALSE(owner->IsSidePanelShowing());
  EXPECT_EQ(owner->CurrentExtensionId(), std::nullopt);
  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       TheContextMenuEntryOpensThePanel) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  // The menu command ignores a tab whose origin is opaque, as about:blank's is.
  ASSERT_TRUE(embedded_test_server()->Start());
  AddTab(browser(), true, embedded_test_server()->GetURL("/title1.html"));
  AtriumBrowserWindow* w = window();
  w->ShowExtensionContextMenuFromSurface(extension->id(),
                                         gfx::Rect(10, 10, 5, 5));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return w->IsExtensionContextMenuShowingForTesting(); }));
  ASSERT_TRUE(w->ExtensionContextMenuHasSidePanelEntryForTesting());

  w->ExecuteExtensionContextMenuSidePanelEntryForTesting();

  EXPECT_TRUE(WaitForPanel(w, true));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AStockEntryRequestShowsNothing) {
  AddTab(browser());
  SidePanelUI* ui = browser()->GetFeatures().side_panel_ui();
  ASSERT_TRUE(ui) << "the patch did not create a side panel UI";

  // The base class CHECKs on an unregistered key. Bookmarks is one here.
  ui->Show(SidePanelEntryId::kBookmarks);
  ui->Show(SidePanelEntryId::kReadingList);

  EXPECT_FALSE(ui->IsSidePanelShowing());
  EXPECT_FALSE(window()->ExtensionPanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       DisablingTheExtensionHidesItsPanel) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(window(), true));

  extensions::ExtensionRegistrar::Get(profile())->DisableExtension(
      extension->id(), {extensions::disable_reason::DISABLE_USER_ACTION});

  EXPECT_TRUE(WaitForPanel(window(), false));
  EXPECT_FALSE(AtriumSidePanelUI::From(browser())->IsSidePanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ClosingTheBrowserWithThePanelOpenFiresOnClosed) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  BrowserController* slot = BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  ASSERT_TRUE(slot);
  Browser* slot_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* bwi) {
        if (bwi->GetBrowserForMigrationOnly() != browser()) {
          slot_browser = bwi->GetBrowserForMigrationOnly();
        }
        return true;
      });
  ASSERT_TRUE(slot_browser);
  AddTab(slot_browser);
  auto* slot_window = static_cast<AtriumBrowserWindow*>(slot_browser->window());
  RunOpenWindowPanel(*extension, slot_browser);
  ASSERT_TRUE(WaitForPanel(slot_window, true));
  ExtensionTestMessageListener closed("closed");

  CloseBrowserSynchronously(slot_browser);

  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ASwappedPanelClosesSafelyAfterItsHostWindowIsGone) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-host-gone");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  BrowserController::ActivateWindow(other.controller);
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(WaitForPanel(window(), true))
      << "a swapped-in Browser's panel stayed out of the presenter";
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return PanelPageAlive(other.profile, *theirs); }));
  CloseBrowserSynchronously(browser());
  ASSERT_TRUE(PanelPageAlive(other.profile, *theirs))
      << "the panel page died with the host window";
  ExtensionTestMessageListener closed("closed");

  // The close reaches for the view, so a host left pointing at the closed
  // window is a use-after-free here.
  AtriumSidePanelUI::From(other.browser)->Close();

  EXPECT_FALSE(AtriumSidePanelUI::From(other.browser)->IsSidePanelShowing());
  EXPECT_FALSE(PanelPageAlive(other.profile, *theirs));
  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AProfileSwapShowsTheIncomingBrowsersPanelAndBack) {
  const extensions::Extension* own = LoadPanelExtension(kWorker);
  ASSERT_TRUE(own);
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-swap");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  AtriumBrowserWindow* w = window();
  RunOpenWindowPanel(*own, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  views::View* own_view = w->ExtensionPanelContentForTesting();
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return AtriumSidePanelUI::From(other.browser)->IsSidePanelShowing();
  }));
  ASSERT_EQ(w->ExtensionPanelContentForTesting(), own_view)
      << "a hidden profile's panel took the slot before any swap";
  ExtensionTestMessageListener closed("closed");

  BrowserController::ActivateWindow(other.controller);
  ASSERT_EQ(BrowserController::PresentedWindowForBrowser(other.browser), w);

  EXPECT_TRUE(w->ExtensionPanelShowing());
  EXPECT_NE(w->ExtensionPanelContentForTesting(), own_view)
      << "the window still shows the outgoing profile's panel";

  BrowserController::ActivateWindow(
      BrowserController::ControllerForBrowser(browser()));

  EXPECT_EQ(w->ExtensionPanelContentForTesting(), own_view)
      << "the swap back rebuilt the panel instead of reattaching it";
  EXPECT_FALSE(closed.was_satisfied()) << "a swap destroyed a panel view";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       APanelOpenedOnAHiddenBrowserWaitsForTheSwap) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-hidden");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  AtriumBrowserWindow* w = window();

  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return AtriumSidePanelUI::From(other.browser)->IsSidePanelShowing();
  }));
  EXPECT_FALSE(w->ExtensionPanelShowing())
      << "a Browser no pane displays put its panel on screen";

  BrowserController::ActivateWindow(other.controller);

  EXPECT_TRUE(WaitForPanel(w, true));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ClosingTheSwappedInBrowserHidesItsPanel) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-close-other");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  AtriumBrowserWindow* w = window();
  BrowserController::ActivateWindow(other.controller);
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(WaitForPanel(w, true));

  CloseBrowserSynchronously(other.browser);

  EXPECT_FALSE(w->ExtensionPanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ClosingTheHostWindowKeepsTheSwappedPanelAlive) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-close-host");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  BrowserController::ActivateWindow(other.controller);
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(WaitForPanel(window(), true));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return PanelPageAlive(other.profile, *theirs); }));
  ExtensionTestMessageListener closed("closed");

  CloseBrowserSynchronously(browser());

  EXPECT_TRUE(AtriumSidePanelUI::From(other.browser)->IsSidePanelShowing());
  EXPECT_TRUE(PanelPageAlive(other.profile, *theirs))
      << "the host window destroyed a view that belongs to another Browser";
  EXPECT_FALSE(closed.was_satisfied())
      << "the host window destroyed a view that belongs to another Browser";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AReclaimedPanelShowsWhenItsBrowserGetsItsOwnWindow) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-own-window");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  BrowserController::ActivateWindow(other.controller);
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(WaitForPanel(window(), true));
  // While B is on screen, closing A leaves B the slot's visible Browser, and
  // ActivateWindow(B) is then a no-op. Swap back first, so B's next
  // activation is a first activation in its own window.
  BrowserController::ActivateWindow(
      BrowserController::ControllerForBrowser(browser()));
  ASSERT_TRUE(WaitForPanel(window(), false));
  CloseBrowserSynchronously(browser());

  BrowserController::ActivateWindow(other.controller);

  EXPECT_TRUE(WaitForPanel(
      static_cast<AtriumBrowserWindow*>(other.browser->window()), true));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ABackgroundTabPanelShowsOnlyWhenTheTabActivates) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  content::WebContents* background = AddTab(browser(), /*foreground=*/false);
  const int tab_id = extensions::ExtensionTabUtil::GetTabId(background);
  // open() on a tab needs a tab-specific panel to open.
  RunSetTabOptions(*extension, tab_id);

  RunOpenPanel(*extension, tab_id, std::nullopt, profile());

  EXPECT_FALSE(window()->ExtensionPanelShowing())
      << "a background tab's panel showed while another tab was active";
  TabStripModel* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(background));
  EXPECT_TRUE(WaitForPanel(window(), true));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AWindowPanelStaysAcrossATabSwitch) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(window(), true));
  views::View* content = window()->ExtensionPanelContentForTesting();
  ExtensionTestMessageListener closed("closed");

  AddTab(browser());

  EXPECT_TRUE(window()->ExtensionPanelShowing());
  EXPECT_EQ(window()->ExtensionPanelContentForTesting(), content)
      << "the window panel was rebuilt, so its page reloaded";
  EXPECT_FALSE(closed.was_satisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ATabSwitchToATabWithNoPanelClosesIt) {
  const extensions::Extension* extension =
      LoadPanelExtension(kWorker, nullptr, kTabOnlyPanelManifest);
  ASSERT_TRUE(extension);
  content::WebContents* with_panel = AddTab(browser());
  RunSetTabOptions(*extension,
                   extensions::ExtensionTabUtil::GetTabId(with_panel));
  RunOpenPanel(*extension, extensions::ExtensionTabUtil::GetTabId(with_panel),
               std::nullopt, profile());
  ASSERT_TRUE(WaitForPanel(window(), true));
  content::WebContents* plain = AddTab(browser(), /*foreground=*/false);

  TabStripModel* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(plain));

  EXPECT_TRUE(WaitForPanel(window(), false));
  AtriumSidePanelUI* owner = AtriumSidePanelUI::From(browser());
  EXPECT_FALSE(owner->IsSidePanelShowing());
  EXPECT_EQ(owner->CurrentExtensionId(), std::nullopt);
  model->ActivateTabAt(model->GetIndexOfWebContents(with_panel));
  EXPECT_TRUE(WaitForPanel(window(), true));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AnActionClickOpensThePanelAndASecondClickClosesIt) {
  const extensions::Extension* extension =
      LoadPanelExtension(kActionClickWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());

  window()->ClickExtensionActionFromSurface(extension->id());
  ASSERT_TRUE(WaitForPanel(window(), true))
      << "the action click did not open the panel";

  ExtensionTestMessageListener closed("closed");
  window()->ClickExtensionActionFromSurface(extension->id());
  EXPECT_TRUE(WaitForPanel(window(), false));
  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       OpeningTheAiPanelClosesTheExtensionPanel) {
  AtriumBrowserWindow::SetAiEnabled(true);
  AtriumBrowserWindow* w = window();
  ASSERT_TRUE(w->AiPanelContentsForTesting());
  ASSERT_TRUE(content::WaitForLoadStop(w->AiPanelContentsForTesting()));
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  ExtensionTestMessageListener closed("closed");

  w->ToggleAiPanel();

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return w->AiPanelOpenForTesting(); }));
  EXPECT_TRUE(WaitForPanel(w, false));
  EXPECT_TRUE(closed.WaitUntilSatisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       OpeningTheExtensionPanelClosesTheAiPanel) {
  AtriumBrowserWindow::SetAiEnabled(true);
  AtriumBrowserWindow* w = window();
  content::WebContents* ai = w->AiPanelContentsForTesting();
  ASSERT_TRUE(ai);
  ASSERT_TRUE(content::WaitForLoadStop(ai));
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  w->ToggleAiPanel();
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return w->AiPanelOpenForTesting(); }));

  RunOpenWindowPanel(*extension, browser());

  ASSERT_TRUE(WaitForPanel(w, true));
  EXPECT_FALSE(w->AiPanelOpenForTesting());
  // The page state too, or its next toggle would think it is still open.
  constexpr char kPageOpen[] =
      "document.getElementById('atrium-root').classList.contains('aipopen')";
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !content::EvalJs(ai, kPageOpen).ExtractBool(); }));
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       TheCommandBarVerbReopensTheLastPanel) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AddTab(browser());
  AtriumBrowserWindow* w = window();
  EXPECT_FALSE(w->CanToggleExtensionPanel()) << "no panel was shown yet";
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  w->CloseExtensionPanel();
  ASSERT_TRUE(WaitForPanel(w, false));

  base::ListValue no_args;
  w->OnSurfaceMessage("extensionSidePanelToggleRequested", no_args);
  EXPECT_TRUE(WaitForPanel(w, true)) << "the verb did not reopen the panel";

  w->OnSurfaceMessage("extensionSidePanelToggleRequested", no_args);
  EXPECT_TRUE(WaitForPanel(w, false)) << "the verb did not close the panel";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       TheGripWearsTheOverlayThemeAccent) {
  AtriumBrowserWindow* w = window();
  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        kFakeExtensionId);
  WindowAppearance appearance;
  appearance.overlay_theme = "cyan";

  AtriumBrowserWindow::SetWindowAppearance(appearance);

  EXPECT_EQ(w->ExtensionPanelGripAccentForTesting(),
            BrandBookThemePalette("cyan")->accent)
      << "the panel grip and the sidebar seam disagree on the accent";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       APanelShownAfterAThemeChangeWearsTheAccent) {
  AtriumBrowserWindow* w = window();
  WindowAppearance appearance;
  appearance.overlay_theme = "cyan";
  AtriumBrowserWindow::SetWindowAppearance(appearance);

  w->ShowExtensionPanel(std::make_unique<views::View>(), nullptr,
                        kFakeExtensionId);

  EXPECT_EQ(w->ExtensionPanelGripAccentForTesting(),
            BrandBookThemePalette("cyan")->accent)
      << "a panel shown after a theme change kept the default accent";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ThePanelIsACardShapedLikeThePageCard) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  auto* web_view =
      static_cast<views::WebView*>(w->ExtensionPanelContentForTesting());
  views::View* card = w->ExtensionPanelCardForTesting();
  // Not the default 10, so a card that only got the inset at build time fails.
  constexpr int kInset = 6;
  WindowAppearance appearance;
  appearance.border_width = kInset;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(card->GetWidget());

  const gfx::Rect slot(w->ExtensionPanelBoundsForTesting().size());
  gfx::Rect expected = slot;
  expected.Inset(kInset);
  EXPECT_EQ(card->bounds(), expected)
      << "the card does not keep the page card's gutter from the slot";
  const float radius = w->ActivePaneForTesting()->TileCornerRadiusForTesting();
  ASSERT_GT(radius, 0);
  ASSERT_TRUE(card->layer()) << "the card has no layer to round and clip";
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(radius))
      << "the card's corners differ from the page card's";
  ASSERT_TRUE(web_view->holder()->layer());
  // The header covers the page's top edge, so only its bottom corners round.
  EXPECT_EQ(web_view->holder()->layer()->rounded_corner_radii(),
            gfx::RoundedCornersF(0, 0, radius, radius))
      << "the page squares off the card's bottom corners";

  // The sidebar docks right, so the traffic lights sit over the panel.
  appearance.sidebar_on_right = true;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(card->GetWidget());

  expected = gfx::Rect(w->ExtensionPanelBoundsForTesting().size());
  expected.Inset(gfx::Insets::TLBR(static_cast<int>(kTitlebarHeight), kInset,
                                   kInset, kInset));
  EXPECT_EQ(card->bounds(), expected)
      << "a left-docked card does not clear the traffic lights";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ThePanelCardFollowsThePageCardInFullscreen) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  auto* web_view =
      static_cast<views::WebView*>(w->ExtensionPanelContentForTesting());
  views::View* card = w->ExtensionPanelCardForTesting();
  views::test::RunScheduledLayout(card->GetWidget());
  const gfx::Rect windowed = card->bounds();
  const float radius = w->ActivePaneForTesting()->TileCornerRadiusForTesting();
  ASSERT_GT(radius, 0);
  ASSERT_TRUE(w->ExtensionPanelShadowVisibleForTesting());

  // Not SetImmersiveFullscreen: its async NSWindow transition outlives the
  // test. This is the seam the window drives.
  w->SetRootImmersiveForTesting(true);
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds(), gfx::Rect(w->ExtensionPanelBoundsForTesting().size()))
      << "the card kept its gutter in fullscreen";
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(0))
      << "the card kept its corners in fullscreen";
  EXPECT_EQ(web_view->holder()->layer()->rounded_corner_radii(),
            gfx::RoundedCornersF(0))
      << "the page kept its bottom corners in fullscreen";
  EXPECT_FALSE(w->ExtensionPanelShadowVisibleForTesting())
      << "the shadow would darken the screen edge";

  w->SetRootImmersiveForTesting(false);
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds(), windowed);
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(radius));
  EXPECT_EQ(web_view->holder()->layer()->rounded_corner_radii(),
            gfx::RoundedCornersF(0, 0, radius, radius));
  EXPECT_TRUE(w->ExtensionPanelShadowVisibleForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       APanelOpenedInFullscreenIsEdgeToEdge) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  AddTab(browser());
  w->SetRootImmersiveForTesting(true);
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  auto* web_view =
      static_cast<views::WebView*>(w->ExtensionPanelContentForTesting());
  views::View* card = w->ExtensionPanelCardForTesting();
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds(), gfx::Rect(w->ExtensionPanelBoundsForTesting().size()))
      << "a panel opened in fullscreen has a gutter";
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(0));
  EXPECT_EQ(web_view->holder()->layer()->rounded_corner_radii(),
            gfx::RoundedCornersF(0));
  EXPECT_FALSE(w->ExtensionPanelShadowVisibleForTesting());

  w->SetRootImmersiveForTesting(false);
  views::test::RunScheduledLayout(card->GetWidget());

  const float radius = w->ActivePaneForTesting()->TileCornerRadiusForTesting();
  ASSERT_GT(radius, 0);
  EXPECT_NE(card->bounds(),
            gfx::Rect(w->ExtensionPanelBoundsForTesting().size()))
      << "the gutter did not come back";
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(radius));
  EXPECT_TRUE(w->ExtensionPanelShadowVisibleForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AShapeChangedInFullscreenAppliesOnExit) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  views::View* card = w->ExtensionPanelCardForTesting();
  w->SetRootImmersiveForTesting(true);
  views::test::RunScheduledLayout(card->GetWidget());

  constexpr int kInset = 6;
  WindowAppearance appearance;
  appearance.border_width = kInset;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds(), gfx::Rect(w->ExtensionPanelBoundsForTesting().size()))
      << "an appearance edit broke the fullscreen shape";
  EXPECT_FALSE(w->ExtensionPanelShadowVisibleForTesting());

  w->SetRootImmersiveForTesting(false);
  views::test::RunScheduledLayout(card->GetWidget());

  gfx::Rect expected(w->ExtensionPanelBoundsForTesting().size());
  expected.Inset(kInset);
  EXPECT_EQ(card->bounds(), expected)
      << "the card kept the old inset after fullscreen";
  const float radius = w->ActivePaneForTesting()->TileCornerRadiusForTesting();
  ASSERT_GT(radius, 0);
  EXPECT_EQ(card->layer()->rounded_corner_radii(), gfx::RoundedCornersF(radius))
      << "the card kept the old radius after fullscreen";
  EXPECT_TRUE(w->ExtensionPanelShadowVisibleForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ALeftDockedPanelLosesItsTitlebarBandInFullscreen) {
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* w = window();
  w->SetBounds(gfx::Rect(0, 0, 1400, 900));
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  views::View* card = w->ExtensionPanelCardForTesting();
  WindowAppearance appearance;
  appearance.sidebar_on_right = true;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(card->GetWidget());
  ASSERT_EQ(card->bounds().y(), static_cast<int>(kTitlebarHeight))
      << "setup: the left-docked band is missing";

  w->SetRootImmersiveForTesting(true);
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds(), gfx::Rect(w->ExtensionPanelBoundsForTesting().size()))
      << "the traffic light band stayed in fullscreen";

  w->SetRootImmersiveForTesting(false);
  views::test::RunScheduledLayout(card->GetWidget());

  EXPECT_EQ(card->bounds().y(), static_cast<int>(kTitlebarHeight))
      << "the band did not come back";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ClosingTheSwappedInBrowserShowsThePresentersPanel) {
  const extensions::Extension* own = LoadPanelExtension(kWorker);
  ASSERT_TRUE(own);
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-close-other-own");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* theirs =
      LoadPanelExtension(kWorker, other.profile);
  ASSERT_TRUE(theirs);
  AtriumBrowserWindow* w = window();
  RunOpenWindowPanel(*own, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  views::View* own_view = w->ExtensionPanelContentForTesting();
  BrowserController::ActivateWindow(other.controller);
  RunOpenWindowPanel(*theirs, other.browser);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return w->ExtensionPanelShowing() &&
           w->ExtensionPanelContentForTesting() != own_view;
  }));

  CloseBrowserSynchronously(other.browser);

  EXPECT_TRUE(WaitForPanel(w, true))
      << "the presenter's own panel stayed open but hidden";
  EXPECT_EQ(w->ExtensionPanelContentForTesting(), own_view);
  EXPECT_TRUE(AtriumSidePanelUI::From(browser())->IsSidePanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       TheAiPanelsTabMemoryLeavesAWindowPanelOpen) {
  AtriumBrowserWindow::SetAiEnabled(true);
  AtriumBrowserWindow* w = window();
  content::WebContents* ai = w->AiPanelContentsForTesting();
  ASSERT_TRUE(ai);
  ASSERT_TRUE(content::WaitForLoadStop(ai));
  const extensions::Extension* extension = LoadPanelExtension(kWorker);
  ASSERT_TRUE(extension);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* first =
      AddTab(browser(), true, embedded_test_server()->GetURL("/title2.html"));
  w->ToggleAiPanel();
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return w->AiPanelOpenForTesting(); }));
  AddTab(browser(), true, embedded_test_server()->GetURL("/title3.html"));
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !w->AiPanelOpenForTesting(); }));
  RunOpenWindowPanel(*extension, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  views::View* content = w->ExtensionPanelContentForTesting();
  ExtensionTestMessageListener closed("closed");

  TabStripModel* model = browser()->tab_strip_model();
  model->ActivateTabAt(model->GetIndexOfWebContents(first));

  // The page names the tab and reopens its panel in one script, so once the
  // name shows, only the native answer to that reopen is still in flight.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ai, "document.querySelector('.aictx').textContent")
               .ExtractString() == "About: Title Of Awesomeness";
  }));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ai,
                           "document.getElementById('atrium-root')"
                           ".classList.contains('aipopen')")
               .ExtractBool() == w->AiPanelOpenForTesting();
  }));
  EXPECT_TRUE(w->ExtensionPanelShowing())
      << "the AI panel's tab memory destroyed the window panel";
  EXPECT_EQ(w->ExtensionPanelContentForTesting(), content);
  EXPECT_FALSE(w->AiPanelOpenForTesting());
  EXPECT_FALSE(closed.was_satisfied());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       ClosingAReplacingPanelReleasesTheReplacedOne) {
  const extensions::Extension* first = LoadPanelExtension(kWorker);
  ASSERT_TRUE(first);
  const extensions::Extension* second = LoadPanelExtension(kWorker);
  ASSERT_TRUE(second);
  AddTab(browser());
  AtriumBrowserWindow* w = window();
  RunOpenWindowPanel(*first, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return PanelPageAlive(profile(), *first); }));
  views::View* first_view = w->ExtensionPanelContentForTesting();
  ExtensionTestMessageListener first_closed("closed");
  first_closed.set_extension_id(first->id());
  RunOpenWindowPanel(*second, browser());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return w->ExtensionPanelShowing() &&
           w->ExtensionPanelContentForTesting() != first_view;
  }));

  w->CloseExtensionPanel();

  EXPECT_TRUE(first_closed.WaitUntilSatisfied());
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !PanelPageAlive(profile(), *first);
  })) << "the replaced panel's page outlived the close";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       APanelWhosePageStillLoadsClosesAtOnce) {
  ASSERT_TRUE(embedded_test_server()->Start());
  // The image never answers, so the page never finishes loading.
  const std::string page = base::StringPrintf(
      R"(<html><body><img src="%s"></body></html>)",
      embedded_test_server()->GetURL("/hung").spec().c_str());
  const extensions::Extension* extension =
      LoadPanelExtension(kWorker, nullptr, kPanelManifest, page);
  ASSERT_TRUE(extension);
  AddTab(browser());
  RunOpenWindowPanel(*extension, browser());
  AtriumSidePanelUI* owner = AtriumSidePanelUI::From(browser());
  ASSERT_TRUE(owner->IsSidePanelShowing())
      << "the open waits for the page, so a close can miss it";
  auto* web_view =
      static_cast<views::WebView*>(window()->ExtensionPanelContentForTesting());
  ASSERT_TRUE(web_view && web_view->web_contents());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return web_view->web_contents()->IsLoading(); }));

  owner->Close();

  EXPECT_FALSE(owner->IsSidePanelShowing());
  EXPECT_FALSE(window()->ExtensionPanelShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AProfileSwapForgetsTheOutgoingProfilesLastPanel) {
  AddTab(browser());
  OtherProfile other = CreateOtherProfile("atrium-panel-last-id");
  ASSERT_TRUE(other.browser);
  const extensions::Extension* own = LoadPanelExtension(kWorker);
  ASSERT_TRUE(own);
  AtriumBrowserWindow* w = window();
  RunOpenWindowPanel(*own, browser());
  ASSERT_TRUE(WaitForPanel(w, true));
  w->CloseExtensionPanel();
  ASSERT_TRUE(w->CanToggleExtensionPanel());

  BrowserController::ActivateWindow(other.controller);

  EXPECT_FALSE(w->CanToggleExtensionPanel())
      << "the toggle offers a panel the profile on screen does not have";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AnExtensionPanelCanReadItsTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const extensions::Extension* extension =
      LoadPanelExtension(kWorker, nullptr, kReaderManifest);
  ASSERT_TRUE(extension);
  // An http page: about:blank has an opaque origin.
  content::WebContents* page =
      AddTab(browser(), true, embedded_test_server()->GetURL("/title1.html"));
  const int tab_id = extensions::ExtensionTabUtil::GetTabId(page);
  AtriumBrowserWindow* w = window();
  auto run_in_worker = [&](const std::string& call) {
    return ExecuteScriptInBackgroundPage(
        extension->id(), std::string(kReaderProbeJs) + "(" + call +
                             ").then(r => chrome.test.sendScriptResult(r));");
  };
  // As Claude's worker opens it: a per-tab path, then open() on the tab.
  EXPECT_EQ(ExecuteScriptInBackgroundPage(
                extension->id(),
                base::StringPrintf(
                    "chrome.sidePanel.setOptions({tabId: %d, path: "
                    "'panel.html?tabId=%d', enabled: true}).then("
                    "() => chrome.test.sendScriptResult('ok'), "
                    "e => chrome.test.sendScriptResult(String(e)));",
                    tab_id, tab_id)),
            base::Value("ok"));
  RunOpenPanel(*extension, tab_id, std::nullopt, profile());
  ASSERT_TRUE(WaitForPanel(w, true));

  base::Value group = run_in_worker(base::StringPrintf(
      "atriumGroup(%d, %d)", tab_id,
      extensions::ExtensionTabUtil::GetWindowId(browser())));
  ASSERT_TRUE(group.is_dict());
  const base::Value* group_steps = group.GetDict().Find("steps");
  ASSERT_TRUE(group_steps);
  ExpectEveryStepOk("panel connect", *group_steps);
  const int group_id = group.GetDict().FindInt("groupId").value_or(-1);

  auto probe = [&](const std::string& label) {
    if (!WaitForPanel(w, true)) {
      // Reopen, so the steps below still run and report.
      ADD_FAILURE() << label << ": the tab's panel did not come back";
      RunOpenPanel(*extension, tab_id, std::nullopt, profile());
      ASSERT_TRUE(WaitForPanel(w, true));
    }
    auto* web_view =
        static_cast<views::WebView*>(w->ExtensionPanelContentForTesting());
    ASSERT_TRUE(web_view && web_view->web_contents());
    content::WebContents* panel = web_view->web_contents();
    ASSERT_TRUE(content::WaitForLoadStop(panel));
    const int real_tab = extensions::ExtensionTabUtil::GetTabId(
        browser()->tab_strip_model()->GetActiveWebContents());
    const int real_window =
        extensions::ExtensionTabUtil::GetWindowId(browser());
    LOG(WARNING) << "ATRIUM-PROBE " << label << " T=" << tab_id
                 << " group=" << group_id << " real active tab=" << real_tab
                 << " real window=" << real_window
                 << " panel=" << panel->GetLastCommittedURL();
    EXPECT_EQ(real_tab, tab_id) << label << ": T is not the active tab";
    ExpectEveryStepOk(
        label + " panel ids",
        base::Value(content::EvalJs(panel, std::string(kReaderProbeJs) +
                                               base::StringPrintf(
                                                   "atriumPanelIds(%d, %d)",
                                                   real_tab, real_window))
                        .ExtractList()
                        .Clone()));
    ExpectEveryStepOk(label + " worker tool",
                      run_in_worker(base::StringPrintf(
                          "atriumTool(%d, %d)", tab_id, group_id)));
    ExpectEveryStepOk(
        label + " panel tool",
        base::Value(content::EvalJs(panel, std::string(kReaderProbeJs) +
                                               base::StringPrintf(
                                                   "atriumTool(%d, %d)",
                                                   tab_id, group_id))
                        .ExtractList()
                        .Clone()));
  };

  probe("initial");

  TabStripModel* model = browser()->tab_strip_model();
  AddTab(browser(), true, embedded_test_server()->GetURL("/title2.html"));
  model->ActivateTabAt(model->GetIndexOfWebContents(page));
  probe("after a tab switch away and back");

  // Atrium's own move primitive, out to a second window and back.
  const std::optional<tab_groups::TabGroupId> tab_group =
      model->GetTabGroupForTab(model->GetIndexOfWebContents(page));
  ASSERT_TRUE(tab_group);
  const tab_groups::TabGroupVisualData visual =
      *model->group_model()->GetTabGroup(*tab_group)->visual_data();
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();
  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      model, model->GetIndexOfWebContents(page), other_model,
      other_model->count()));
  ExpectOwnGroup(other_model, page, *tab_group, visual,
                 "moved to the other window");
  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      other_model, other_model->GetIndexOfWebContents(page), model,
      model->count()));
  ExpectOwnGroup(model, page, *tab_group, visual, "moved back");
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
  browser()->window()->Activate();
  probe("after a move out to another window and back");

  OtherProfile swap = CreateOtherProfile("atrium-panel-reader-swap");
  ASSERT_TRUE(swap.browser);
  BrowserController::ActivateWindow(swap.controller);
  ASSERT_EQ(BrowserController::PresentedWindowForBrowser(swap.browser), w);
  BrowserController::ActivateWindow(
      BrowserController::ControllerForBrowser(browser()));
  probe("after a profile swap and back");
}

// A row click on a tab that lives in another window moves it here, through
// CompleteMoveIn.
IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AnActivationMoveKeepsATabsOwnGroup) {
  content::WebContents* moving = AddTab(browser());
  AddTab(browser());
  TabStripModel* model = browser()->tab_strip_model();
  const tab_groups::TabGroupId group =
      model->AddToNewGroup({model->GetIndexOfWebContents(moving)});
  const tab_groups::TabGroupVisualData visual =
      *model->group_model()->GetTabGroup(group)->visual_data();
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();

  BrowserController::ActivateTab(BrowserController::ControllerForBrowser(other),
                                 TabId(moving));

  ASSERT_TRUE(base::test::RunUntil([&]() {
    return other_model->GetIndexOfWebContents(moving) != TabStripModel::kNoTab;
  })) << "the activation never moved the tab";
  ExpectOwnGroup(other_model, moving, group, visual, "after the move-in");
  EXPECT_EQ(other_model->GetActiveWebContents(), moving);
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AMoveTakesATabOutOfASharedGroup) {
  content::WebContents* moving = AddTab(browser());
  content::WebContents* staying = AddTab(browser());
  TabStripModel* model = browser()->tab_strip_model();
  const tab_groups::TabGroupId group =
      model->AddToNewGroup({model->GetIndexOfWebContents(moving),
                            model->GetIndexOfWebContents(staying)});
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();

  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      model, model->GetIndexOfWebContents(moving), other_model,
      other_model->count()));

  const int moved = other_model->GetIndexOfWebContents(moving);
  ASSERT_NE(moved, TabStripModel::kNoTab);
  EXPECT_EQ(other_model->GetTabGroupForTab(moved), std::nullopt)
      << "the tab took a group that still has other tabs";
  EXPECT_EQ(model->GetTabGroupForTab(model->GetIndexOfWebContents(staying)),
            group);
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->tab_count(), 1);
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

// Tab-modal dialogs and bubbles hide on the outgoing tab's deactivate signal.
IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AnActiveGroupMoveDeactivatesTheTargetsActiveTab) {
  content::WebContents* moving = AddTab(browser());
  TabStripModel* model = browser()->tab_strip_model();
  model->AddToNewGroup({model->GetIndexOfWebContents(moving)});
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();
  tabs::TabInterface* previous = other_model->GetActiveTab();
  ASSERT_TRUE(previous);
  bool deactivated = false;
  base::CallbackListSubscription subscription =
      previous->RegisterWillDeactivate(base::BindRepeating(
          [](bool* flag, tabs::TabInterface*) { *flag = true; },
          &deactivated));

  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      model, model->GetIndexOfWebContents(moving), other_model,
      other_model->count(), AddTabTypes::ADD_ACTIVE));

  EXPECT_EQ(other_model->GetActiveWebContents(), moving);
  EXPECT_TRUE(deactivated)
      << "the target's old active tab never heard it went to the background";
  subscription = base::CallbackListSubscription();
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

// RouteContentDrop's shape: the drop places the pane itself, so the insert
// must not take the target's active tab.
IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AnInactiveGroupMoveKeepsTheTargetsActiveTab) {
  content::WebContents* moving = AddTab(browser());
  TabStripModel* model = browser()->tab_strip_model();
  const tab_groups::TabGroupId group =
      model->AddToNewGroup({model->GetIndexOfWebContents(moving)});
  ASSERT_EQ(model->GetActiveWebContents(), moving);
  const tab_groups::TabGroupVisualData visual =
      *model->group_model()->GetTabGroup(group)->visual_data();
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();
  content::WebContents* previous = other_model->GetActiveWebContents();

  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      model, model->GetIndexOfWebContents(moving), other_model,
      other_model->count(), AddTabTypes::ADD_NONE));

  ExpectOwnGroup(other_model, moving, group, visual, "after an ADD_NONE move");
  EXPECT_EQ(other_model->GetActiveWebContents(), previous)
      << "an ADD_NONE move took the target's active tab";
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionSidePanelTest,
                       AGroupMoveKeepsItsSavedGroup) {
  tab_groups::TabGroupSyncService* service =
      tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile());
  ASSERT_TRUE(service) << "no group sync service, so nothing here tests it";
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* moving =
      AddTab(browser(), true, embedded_test_server()->GetURL("/title1.html"));
  TabStripModel* model = browser()->tab_strip_model();
  const tab_groups::TabGroupId group =
      model->AddToNewGroup({model->GetIndexOfWebContents(moving)});
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return service->GetGroup(group).has_value(); }))
      << "the group was never saved";
  const base::Uuid saved_guid = service->GetGroup(group)->saved_guid();
  Browser* other = CreateBrowser(profile());
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* other_model = other->tab_strip_model();

  ASSERT_TRUE(MoveWebContentsBetweenStrips(
      model, model->GetIndexOfWebContents(moving), other_model,
      other_model->count()));
  base::RunLoop().RunUntilIdle();

  std::optional<tab_groups::SavedTabGroup> after =
      service->GetGroup(saved_guid);
  ASSERT_TRUE(after) << "the move deleted the saved group";
  EXPECT_EQ(after->local_group_id(), std::optional(group))
      << "the saved group lost its tie to the moved group";
  EXPECT_EQ(after->saved_tabs().size(), 1u)
      << "the move emptied the saved group";
  EXPECT_EQ(service->GetAllGroups().size(), 1u)
      << "the move saved a second copy of the group";
  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

}  // namespace
}  // namespace atrium
