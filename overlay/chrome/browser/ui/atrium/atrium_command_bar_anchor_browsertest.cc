// Copyright 2026 The Atrium Authors

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_open_reason.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace {

class AtriumCommandBarAnchorBrowserTest : public InProcessBrowserTest {};

// The first open of the session runs before the /commandbar document
// exists, so the placement can only arrive through the commandBarReady
// replay - the same gap the prefill already had to survive.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarAnchorBrowserTest,
                       AnAnchoredOpenLaysTheCardOnTheClickedRow) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCommandBar(atrium::CommandBarOpenReason::kEditAtAnchor,
                         gfx::Rect(12, 96, 240, 34));
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette) << "the anchored open built no palette document";
  ASSERT_TRUE(content::WaitForLoadStop(palette));

  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(palette,
                           "!!document.querySelector('.cbcard.cbanchored')")
        .ExtractBool();
  })) << "the placement never reached the page, so the card is still centred";

  EXPECT_EQ("12px",
            content::EvalJs(palette,
                            "document.querySelector('.cbcard').style.left")
                .ExtractString());
  EXPECT_EQ("96px",
            content::EvalJs(palette,
                            "document.querySelector('.cbcard').style.top")
                .ExtractString());
  EXPECT_EQ("320px",
            content::EvalJs(palette,
                            "document.querySelector('.cbcard').style.width")
                .ExtractString())
      << "a 240pt row did not widen to the minimum";
}

// The reason carries the prefill as well as the rect: an anchored open is a
// Cmd+L on the row, not a blank new-tab prompt.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarAnchorBrowserTest,
                       AnAnchoredOpenPrefillsTheDisplayedUrl) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // Atrium's bootstrap can start a window with zero tabs, unlike stock
  // Chromium's about:blank default - CurrentlyDisplayedWebContents() is then
  // null and the field stays empty no matter what OpenCommandBar does.
  chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                   /*foreground=*/true);

  window->OpenCommandBar(atrium::CommandBarOpenReason::kEditAtAnchor,
                         gfx::Rect(12, 96, 400, 34));
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette);
  ASSERT_TRUE(content::WaitForLoadStop(palette));

  EXPECT_TRUE(base::test::RunUntil([&] {
    return !content::EvalJs(palette,
                            "document.querySelector('.cbinput').value")
                .ExtractString()
                .empty();
  })) << "the anchored open left the field empty, so it is a new-tab prompt";
}

// The control. Without it the placement could be unconditional and the
// assertions above would still pass.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarAnchorBrowserTest,
                       AnOrdinaryOpenLeavesTheCardCentred) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCommandBar();
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette);
  ASSERT_TRUE(content::WaitForLoadStop(palette));

  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(palette, "!!document.querySelector('.cbcard')")
        .ExtractBool();
  }));
  EXPECT_FALSE(
      content::EvalJs(palette,
                      "document.querySelector('.cbcard')"
                      ".classList.contains('cbanchored')")
          .ExtractBool())
      << "Cmd+T opened an anchored card";
}

// an anchored open hides the sidebar's address row, and Close() is
// the only path that puts it back. A dead renderer reaches no other one, so
// the palette used to stand as a blank card until an unrelated deactivation.
IN_PROC_BROWSER_TEST_F(AtriumCommandBarAnchorBrowserTest,
                       ACrashedPaletteRendererClosesTheWidget) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  window->OpenCommandBar(atrium::CommandBarOpenReason::kEditAtAnchor,
                         gfx::Rect(12, 96, 240, 34));
  content::WebContents* palette = window->CommandBarContentsForTesting();
  ASSERT_TRUE(palette);
  ASSERT_TRUE(content::WaitForLoadStop(palette));
  ASSERT_TRUE(window->IsCommandBarShowing())
      << "the anchored open never showed, so the crash proves nothing";

  content::RenderProcessHost* host = palette->GetPrimaryMainFrame()->GetProcess();
  content::RenderProcessHostWatcher watcher(
      host, content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);
  host->Shutdown(content::RESULT_CODE_KILLED);
  watcher.Wait();

  EXPECT_TRUE(base::test::RunUntil(
      [&] { return !window->IsCommandBarShowing(); }))
      << "the palette outlived its renderer, so the address row stays hidden";
}

}  // namespace
