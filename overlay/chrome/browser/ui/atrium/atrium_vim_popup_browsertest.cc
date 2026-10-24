// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_popup_browsertest.cc
// Read out of the real popup rather than the install code.

#include "base/test/run_until.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_registry.h"
#include "extensions/common/extension.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using AtriumVimPopupTest = extensions::ExtensionBrowserTest;

// A packed extension whose browser action opens a popup page.
constexpr char kPopupExtension[] = "api_test/browser_action/popup";

content::WebContents* OpenThePopup(Browser* browser,
                                   const std::string& extension_id) {
  // The same entry the strip chips and the quick-menu tiles use. The popup no
  // longer needs an active tab to open.
  static_cast<AtriumBrowserWindow*>(browser->window())
      ->ClickExtensionActionFromSurface(extension_id);

  auto find_host = [&]() -> extensions::ExtensionHost* {
    for (extensions::ExtensionHost* host :
         extensions::ExtensionHostRegistry::Get(browser->profile())
             ->GetHostsForExtension(extension_id)) {
      if (host->extension_host_type() ==
          extensions::mojom::ViewType::kExtensionPopup) {
        return host;
      }
    }
    return nullptr;
  };
  if (!base::test::RunUntil([&]() { return find_host() != nullptr; })) {
    return nullptr;
  }
  content::WebContents* contents = find_host()->host_contents();
  return content::WaitForLoadStop(contents) ? contents : nullptr;
}

// typeof, not the object: reading the object across worlds would serialise it.
std::string VimInPopup(content::WebContents* popup) {
  return content::EvalJs(popup, "typeof window.__atriumVim",
                         content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                         ISOLATED_WORLD_ID_CHROME_INTERNAL)
      .ExtractString();
}

IN_PROC_BROWSER_TEST_F(AtriumVimPopupTest, VimOffLeavesThePopupUntouched) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));
  AtriumBrowserWindow::SetVimModeEnabled(false);

  content::WebContents* popup = OpenThePopup(browser(), extension->id());
  ASSERT_TRUE(popup) << "the extension popup never opened";

  EXPECT_EQ(VimInPopup(popup), "undefined")
      << "a user who never enabled vim must open a popup with no Atrium "
         "script in it at all";
}

IN_PROC_BROWSER_TEST_F(AtriumVimPopupTest, VimOnReachesThePopup) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));
  AtriumBrowserWindow::SetVimModeEnabled(true);

  content::WebContents* popup = OpenThePopup(browser(), extension->id());
  ASSERT_TRUE(popup) << "the extension popup never opened";

  // The presence half of the pair above: without it the absence test passes on
  // a build where the install point was never reached at all.
  EXPECT_EQ(VimInPopup(popup), "object");
  EXPECT_EQ(content::EvalJs(popup, "typeof window.__atriumVimPopup",
                            content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                            ISOLATED_WORLD_ID_CHROME_INTERNAL)
                .ExtractString(),
            "object")
      << "the bridge is what reads the popup's own keys";
  AtriumBrowserWindow::SetVimModeEnabled(false);
}

}  // namespace
}  // namespace atrium
