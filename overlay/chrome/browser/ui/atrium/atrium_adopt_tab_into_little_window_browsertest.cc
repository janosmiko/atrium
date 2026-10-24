// Copyright 2026 The Atrium Authors

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// Records the TabRemovedReason for one specific WebContents so the test can
// assert it without guessing at ordering relative to the detach itself.
class RemoveReasonRecorder : public TabStripModelObserver {
 public:
  explicit RemoveReasonRecorder(content::WebContents* watched)
      : watched_(watched) {}

  std::optional<TabRemovedReason> reason() const { return reason_; }

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    if (change.type() != TabStripModelChange::kRemoved) {
      return;
    }
    for (const auto& removed : change.GetRemove()->contents) {
      if (removed.contents == watched_) {
        reason_ = removed.remove_reason;
      }
    }
  }

 private:
  raw_ptr<content::WebContents> watched_;
  std::optional<TabRemovedReason> reason_;
};

using AtriumAdoptTabIntoLittleWindowBrowserTest = InProcessBrowserTest;

// AdoptTabIntoLittleWindow (atrium_browser_controller.cc)
// tears a live tab out of its current window and into a brand-new little
// window via MoveWebContentsBetweenStrips' DetachTabAtForInsertion +
// InsertDetachedTabAt pairing - the same primitive ActivateTab's
// cross-window move and MoveLittleTabToSpace's same-profile branch already
// use elsewhere in this file (untouched by this test). This is an
// InProcessBrowserTest rather than a unit_test: pulling
// chrome/browser/ui/tabs:test_support into atrium's :unit_tests source_set
// would drag //chrome/browser/ui/views/permissions/chip (a real Views/AppKit
// dependency chain: test_support -> tabs:tab_strip -> tabs:tabs -> chip's
// public_deps) into what is otherwise a pure-logic target.
// This exact browsertest shape is gateable
// headlessly via build/vm.sh run-gtest.
IN_PROC_BROWSER_TEST_F(AtriumAdoptTabIntoLittleWindowBrowserTest,
                       MovesTabWithoutReloadIntoAFreshLittleWindow) {
  const GURL adopted_url("https://example.com/adopt-me");
  chrome::AddTabAt(browser(), adopted_url, /*index=*/-1,
                    /*foreground=*/true);

  TabStripModel* source_strip = browser()->tab_strip_model();
  const int adopted_index = source_strip->count() - 1;
  content::WebContents* adopted_contents =
      source_strip->GetWebContentsAt(adopted_index);
  ASSERT_TRUE(adopted_contents);

  content::NavigationController* controller_before =
      &adopted_contents->GetController();
  content::RenderProcessHost* process_before =
      adopted_contents->GetPrimaryMainFrame()->GetProcess();
  const int source_count_before = source_strip->count();

  RemoveReasonRecorder recorder(adopted_contents);
  source_strip->AddObserver(&recorder);

  atrium::BrowserController* little =
      atrium::BrowserController::AdoptTabIntoLittleWindow(adopted_contents,
                                                            std::nullopt);

  source_strip->RemoveObserver(&recorder);

  ASSERT_TRUE(little) << "AdoptTabIntoLittleWindow returned nullptr - "
                          "either the controller scan or the detach failed";

  // The SAME WebContents landed in the new window - same
  // NavigationController, same render process. Never a reload. The
  // WebContents' own TabInterface names its current window directly, so
  // there is no need for BrowserController's private Browser* accessor.
  tabs::TabInterface* landed_tab =
      tabs::TabInterface::GetFromContents(adopted_contents);
  ASSERT_TRUE(landed_tab);
  BrowserWindowInterface* dest_window = landed_tab->GetBrowserWindowInterface();
  ASSERT_TRUE(dest_window);
  TabStripModel* dest_strip = dest_window->GetTabStripModel();
  ASSERT_EQ(1, dest_strip->count());
  content::WebContents* landed_contents = dest_strip->GetWebContentsAt(0);
  EXPECT_EQ(adopted_contents, landed_contents)
      << "a different WebContents landed in the little window - the tab "
         "was recreated rather than moved";
  EXPECT_EQ(controller_before, &landed_contents->GetController())
      << "the NavigationController changed - the move reloaded the tab "
         "instead of carrying it across";
  EXPECT_EQ(process_before, landed_contents->GetPrimaryMainFrame()->GetProcess())
      << "the render process changed - the move reloaded the tab instead "
         "of carrying it across";

  // Matches CreateLittleWindow's single-tab, no-sidebar contract.
  EXPECT_TRUE(little->IsLittle());
  EXPECT_EQ(1, dest_strip->count())
      << "a little window adopted from an existing tab must hold exactly "
         "that one tab - no seeded space, no extra startup tab";

  // The source strip lost exactly one tab, and the removal reason
  // recorded for it was the move-out reason, never a genuine close.
  EXPECT_EQ(source_count_before - 1, source_strip->count());
  ASSERT_TRUE(recorder.reason().has_value())
      << "no kRemoved notification was observed for the adopted tab";
  EXPECT_EQ(TabRemovedReason::kInsertedIntoOtherTabStrip,
            *recorder.reason())
      << "the adopted tab's removal reason was not "
         "kInsertedIntoOtherTabStrip - OnTabStripModelChanged would route "
         "this to a genuine close (RemoveTab) instead of DetachTabForMove";
}

}  // namespace
