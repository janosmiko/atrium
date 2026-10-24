// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_quick_menu_browsertest.cc

#include "chrome/browser/ui/atrium/atrium_browser_window.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/test_extension_dir.h"
#include "chrome/grit/generated_resources.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/widget.h"

namespace atrium {
namespace {

using AtriumQuickMenuTest = extensions::ExtensionBrowserTest;

// A packed extension with a browser action. The same fixture the extension
// action tests in this directory use.
constexpr char kBrowserActionExtension[] = "api_test/browser_action/basics";

// Declares the sidePanel permission with an enabled default panel.
constexpr char kSidePanelExtension[] = "api_test/side_panel/simple_default";

// `pinned` for `id` in a strip snapshot, or nullopt while the id is not in it.
std::optional<bool> PinnedInSidebarJSON(const std::string& json,
                                        const std::string& id) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return std::nullopt;
  }
  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* action = entry.GetIfDict();
    const std::string* action_id = action ? action->FindString("id") : nullptr;
    if (action_id && *action_id == id) {
      return action->FindBool("pinned");
    }
  }
  return std::nullopt;
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, PinToggleFlipsTheModelAndTheJSON) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);

  ToolbarActionsModel* model = ToolbarActionsModel::Get(browser()->profile());
  ASSERT_TRUE(model);
  const std::string id = extension->id();
  const bool before = model->IsActionPinned(id);

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ToggleExtensionPin(id);

  EXPECT_NE(model->IsActionPinned(id), before)
      << "the pin did not move in Chromium's own model";

  // What OnToolbarPinnedActionsChanged pushed, not a snapshot built here: the
  // resync is the requirement, and a hand-built one would only prove that the
  // serializer echoes its input.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return PinnedInSidebarJSON(window->SidebarExtensionActionsJSONForTesting(),
                               id) == !before;
  })) << "the pin toggle never reached the strip's own snapshot";

  window->ToggleExtensionPin(id);
  EXPECT_EQ(model->IsActionPinned(id), before) << "the toggle is not a toggle";
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, TheSameButtonOpensAndClosesTheMenu) {
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsQuickMenuShowing());

  const gfx::Rect anchor(180, 40, 20, 20);
  window->ToggleQuickMenu(anchor);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsQuickMenuShowing();
  })) << "the first press did not open the menu";

  window->ToggleQuickMenu(anchor);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsQuickMenuShowing();
  })) << "a second press on the same button did not close the menu";
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, RightClickOpensTheExtensionContextMenu) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  // The menu model reads the active tab with no null check, same as upstream.
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_FALSE(window->IsExtensionContextMenuShowingForTesting());

  window->ShowExtensionContextMenuFromSurface(extension->id(),
                                              gfx::Rect(100, 100, 10, 10));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsExtensionContextMenuShowingForTesting();
  })) << "the right-click did not open the menu";

  window->CloseExtensionContextMenuForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsExtensionContextMenuShowingForTesting();
  })) << "the menu did not close";
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, SecondRightClickReplacesTheMenu) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  for (const gfx::Rect rect : {gfx::Rect(50, 50, 10, 10),
                               gfx::Rect(80, 80, 10, 10)}) {
    window->ShowExtensionContextMenuFromSurface(extension->id(), rect);
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return window->IsExtensionContextMenuShowingForTesting();
    })) << "the right-click did not open the menu";
  }

  window->CloseExtensionContextMenuForTesting();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !window->IsExtensionContextMenuShowingForTesting();
  })) << "the menu did not close";
}

// The other menu tests call ShowExtensionContextMenuFromSurface directly.
// This drives the real chip, to prove the DOM wiring in front of it too.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest,
                       RightClickOnASidebarChipOpensTheExtensionContextMenu) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  const std::string id = extension->id();

  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ToolbarActionsModel* model = ToolbarActionsModel::Get(browser()->profile());
  ASSERT_TRUE(model);
  if (!model->IsActionPinned(id)) {
    window->ToggleExtensionPin(id);
  }
  ASSERT_TRUE(model->IsActionPinned(id));

  content::WebContents* sidebar = window->sidebar_contents();
  ASSERT_TRUE(sidebar) << "no sidebar page to hold the chip";
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  const std::string chip_expr = content::JsReplace(
      "document.querySelector('.exs-chip[data-extension-id=\"' + $1 + '\"]')",
      id);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(sidebar, chip_expr + " !== null").ExtractBool();
  })) << "the pinned chip never rendered on the strip";

  // A simulated native click drops the gesture in this VM's browsertests -
  // a real DOM event exercises the same listener the mouse would.
  ASSERT_TRUE(content::ExecJs(
      sidebar, chip_expr +
                   ".dispatchEvent(new MouseEvent('contextmenu', "
                   "{bubbles: true, cancelable: true}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsExtensionContextMenuShowingForTesting();
  })) << "the chip's right-click never reached the native menu";
}

// The tile's own contextmenu listener sends extensionChipContextMenu, not
// pillActionClicked - a right click opens the same native menu as a chip.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest,
                       RightClickOnAQuickMenuTileOpensTheExtensionContextMenu) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  const std::string id = extension->id();

  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ToggleQuickMenu(gfx::Rect(180, 40, 20, 20));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->IsQuickMenuShowing(); }));

  content::WebContents* quick_menu = window->QuickMenuContentsForTesting();
  ASSERT_TRUE(quick_menu) << "no quick menu page to hold the tile";
  ASSERT_TRUE(content::WaitForLoadStop(quick_menu));

  const std::string tile_expr = content::JsReplace(
      "document.querySelector('.qm-tile[data-extension-id=\"' + $1 + '\"]')",
      id);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(quick_menu, tile_expr + " !== null").ExtractBool();
  })) << "the extension's tile never rendered in the quick menu";

  ASSERT_TRUE(content::ExecJs(
      quick_menu, tile_expr +
                      ".dispatchEvent(new MouseEvent('contextmenu', "
                      "{bubbles: true, cancelable: true}))"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsExtensionContextMenuShowingForTesting();
  })) << "the tile's right-click never reached the native menu";
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest,
                       UnpinnedExtensionMenuOffersToPinNotUnpin) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  const std::string id = extension->id();
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ToolbarActionsModel* model = ToolbarActionsModel::Get(browser()->profile());
  ASSERT_TRUE(model);
  if (model->IsActionPinned(id)) {
    window->ToggleExtensionPin(id);
  }
  ASSERT_FALSE(model->IsActionPinned(id));

  window->ShowExtensionContextMenuFromSurface(id, gfx::Rect(10, 10, 5, 5));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsExtensionContextMenuShowingForTesting();
  })) << "the right-click did not open the menu";
  EXPECT_EQ(window->ExtensionContextMenuPinLabelForTesting(),
           l10n_util::GetStringUTF16(IDS_EXTENSIONS_PIN_TO_TOOLBAR))
      << "an unpinned extension's menu must offer to pin it, not unpin it";
}

// An Atrium window has a side panel UI now, so the extension menu carries
// Chromium's own "Open side panel" entry for a side panel extension.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest,
                       SidePanelExtensionMenuOffersTheSidePanelEntry) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kSidePanelExtension));
  ASSERT_TRUE(extension);
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowExtensionContextMenuFromSurface(extension->id(),
                                              gfx::Rect(10, 10, 5, 5));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->IsExtensionContextMenuShowingForTesting();
  })) << "the right-click did not open the menu";
  EXPECT_TRUE(window->ExtensionContextMenuHasSidePanelEntryForTesting());
}

// The Claude extension's left-click: action.onClicked calls sidePanel.open,
// and the panel opens in the window.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest,
                       SidePanelOpenFromAnActionClickOpensThePanel) {
  extensions::TestExtensionDir dir;
  dir.WriteManifest(R"({
    "name": "Side panel opener", "version": "1", "manifest_version": 3,
    "permissions": ["sidePanel"],
    "side_panel": {"default_path": "panel.html"},
    "action": {"default_title": "Open"},
    "background": {"service_worker": "worker.js"}
  })");
  dir.WriteFile(FILE_PATH_LITERAL("panel.html"), "<html>panel</html>");
  dir.WriteFile(FILE_PATH_LITERAL("worker.js"), R"(
    chrome.action.onClicked.addListener((tab) => {
      chrome.sidePanel.open({tabId: tab.id})
          .then(() => chrome.test.sendMessage('opened'))
          .catch(() => chrome.test.sendMessage('refused'));
    });
  )");
  const extensions::Extension* extension = LoadExtension(dir.UnpackedPath());
  ASSERT_TRUE(extension);
  content::WebContents* contents = chrome::AddAndReturnTabAt(
      browser(), GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ExtensionTestMessageListener opened("opened");
  window->ClickExtensionActionFromSurface(extension->id());
  EXPECT_TRUE(opened.WaitUntilSatisfied());
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->ExtensionPanelShowing(); }));
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, UnknownExtensionIdOpensNothing) {
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowExtensionContextMenuFromSurface("not-a-real-extension-id",
                                              gfx::Rect(10, 10, 5, 5));
  EXPECT_FALSE(window->IsExtensionContextMenuShowingForTesting());
}

// This fixture's browser starts with no tab at all, which is the state
// BrowserOwningExtensionForContextMenu's active-tab guard exists for.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, NoActiveTabOpensNothing) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
  ASSERT_TRUE(extension);
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->ShowExtensionContextMenuFromSurface(extension->id(),
                                              gfx::Rect(10, 10, 5, 5));
  EXPECT_FALSE(window->IsExtensionContextMenuShowingForTesting());
}

// Pass condition is "does not crash": OnExtensionContextMenuOwnerClosed must
// drop the model before the closing browser it points at actually tears down.
IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, OwnerBrowserClosingCancelsTheMenu) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kBrowserActionExtension));
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
  ASSERT_TRUE(slot_browser) << "CreateWindowSlot's Browser is not in the collection";

  content::WebContents* contents = chrome::AddAndReturnTabAt(
      slot_browser, GURL("about:blank"), -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  auto* slot_window = static_cast<AtriumBrowserWindow*>(slot_browser->window());
  ASSERT_TRUE(slot_window);
  slot_window->ShowExtensionContextMenuFromSurface(extension->id(),
                                                    gfx::Rect(10, 10, 5, 5));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return slot_window->IsExtensionContextMenuShowingForTesting();
  })) << "the right-click did not open the menu";

  CloseBrowserSynchronously(slot_browser);
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, SiteInfoShowsAPageInfoWidget) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("/title1.html");

  // content::NavigateToURL on the initial tab can hang in an Atrium browser
  // test. AddAndReturnTabAt is the idiom every other Atrium browsertest uses
  // instead.
  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey(),
                                       "PageInfoBubbleView");
  window->ShowPageInfoForQuickMenu(gfx::Rect(120, 80, 20, 20));
  views::Widget* bubble = waiter.WaitIfNeededAndGet();
  ASSERT_TRUE(bubble) << "no page info widget appeared";
  EXPECT_TRUE(bubble->IsVisible());
}

IN_PROC_BROWSER_TEST_F(AtriumQuickMenuTest, SiteJSONCarriesHostAndState) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL url = embedded_test_server()->GetURL("/title1.html");

  content::WebContents* contents =
      chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  std::optional<base::Value> parsed = base::JSONReader::Read(
      window->CurrentQuickMenuSiteJSON(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& site = parsed->GetDict();
  EXPECT_EQ(*site.FindString("host"), url.host());
  // http on 127.0.0.1 is loopback, so it reads secure - the same rule
  // insecure-origin.js applies on the page side.
  EXPECT_EQ(site.FindBool("secure"), true);
  EXPECT_EQ(site.FindBool("shareable"), true);
}

}  // namespace
}  // namespace atrium
