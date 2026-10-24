// Copyright 2026 The Atrium Authors

#include <cmath>
#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/scoped_environment_variable_override.h"
#include "base/time/time.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "content/public/browser/web_contents.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

// Four reverts were all verified in the VM under --v=1, which writes
// no crash report for the browser process. Here a fault is a test failure with
// a stack.
namespace {

class AtriumSidebarOverlayBrowserTest : public InProcessBrowserTest {
 protected:
  // Core::open() READS its file rather than creating one, so without a
  // spaces.json here the store never opens and no width persists.
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
        "\"url\":\"https://example.com/\",\"title\":\"Test\"}]}],"
        "\"appState\":{\"schemaVersion\":1,\"sidebarWidth\":220,"
        "\"activeSpaceID\":\"11111111-1111-1111-1111-111111111111\"}}"));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    // The cache is a process-global keyed on the path it first opened, so a
    // second test in this binary would keep reading the previous one's file.
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

  void TearDownOnMainThread() override {
    atrium::AtriumSettingsWindow::CloseForTesting();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  // Waits for the collapse or the dock to land, since both animate.
  bool WaitForCollapsed(bool collapsed) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
    while (window()->IsSidebarCollapsed() != collapsed &&
           base::TimeTicks::Now() < deadline) {
      Settle();
    }
    return window()->IsSidebarCollapsed() == collapsed;
  }

  atrium::AtriumBrowserWindow* window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // Collapsing animates too, so a test that asserts on the collapsed state has
  // to wait for it rather than for one task hop.
  void CollapseAndWait() {
    window()->SetSidebarWidth(0);
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
    while (!window()->IsSidebarCollapsed() &&
           base::TimeTicks::Now() < deadline) {
      Settle();
    }
    ASSERT_TRUE(window()->IsSidebarCollapsed())
        << "the pane never collapsed, so the panel is not being tested";
  }

  // Docking animates over kSidebarResizeAnimationDuration, so one task hop is
  // not enough to see it land.
  void ExpectDocksAgain() {
    window()->SetSidebarWidth(220);
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
    while (window()->IsSidebarCollapsed() &&
           base::TimeTicks::Now() < deadline) {
      Settle();
    }
    EXPECT_FALSE(window()->IsSidebarCollapsed())
        << "the sidebar never came back to its pane";
  }

  // The show is posted out of the reveal, so IsShowing is still false after one
  // hop - the same reason CollapseAndWait loops instead of hopping once.
  void RevealAndWait() {
    window()->RevealFloatingSidebar(true);
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
    atrium::AtriumSidebarOverlayWidget* panel = nullptr;
    while (base::TimeTicks::Now() < deadline) {
      Settle();
      panel = window()->SidebarOverlayWidgetForTesting();
      if (panel && panel->IsShowing()) {
        break;
      }
    }
    ASSERT_TRUE(panel) << "the reveal built no panel at all";
    ASSERT_TRUE(panel->IsShowing())
        << "the panel never showed, so every check below is vacuous";
  }

  // The reveal is posted, so nothing it does has happened when it returns.
  void Settle() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, loop.QuitClosure());
    loop.Run();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       RevealingAndDismissingRepeatedlySurvives) {
  ASSERT_TRUE(window());
  // The reveal refuses unless the pane is collapsed, which is the state the
  // edge zone is armed in.
  window()->SetSidebarWidth(0);
  Settle();

  // Twenty, because the crashes were races: three single-cycle scenarios all
  // passed against a build that died on the fourth run of the fourth.
  for (int i = 0; i < 20; ++i) {
    window()->RevealFloatingSidebar(true);
    Settle();
    window()->RevealFloatingSidebar(false);
    Settle();
  }

  // Reaching here is most of the point: a DCHECK on any cycle takes the test
  // down with the process. The check below is the other half - the sidebar has
  // to come back, or the page is left without one.
  ExpectDocksAgain();
}

// Reveal and dismiss without letting either settle, which is what a pointer
// crossing the arming strip produces. The posted reveal keeps a wanted-state
// flag rather than a queue precisely so this collapses to one action.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       RevealsCancelledBeforeTheyRunDoNotPileUp) {
  ASSERT_TRUE(window());
  window()->SetSidebarWidth(0);
  Settle();

  for (int i = 0; i < 20; ++i) {
    window()->RevealFloatingSidebar(true);
    window()->RevealFloatingSidebar(false);
  }
  Settle();

  ExpectDocksAgain();
}

// The instant-cancel case collapses to one action and every other
// test here waits the show out, so nothing covered the window between them - a
// dismiss landing after the handover but before the widget is visible.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       ADismissOneHopAfterTheRevealStillDocksTheSidebar) {
  ASSERT_TRUE(window());
  // Before anything: with no document the sidebar has no contents to hand
  // over, and every check below would pass on an empty panel.
  ASSERT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane holds no sidebar at rest, so this test cannot see a handover";
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());

  window()->RevealFloatingSidebar(true);
  Settle();
  ASSERT_FALSE(window()->SidebarPaneHoldsContentsForTesting())
      << "the reveal never took the sidebar, so the dismiss has nothing to "
         "give back and this test proves nothing";

  window()->RevealFloatingSidebar(false);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->SidebarPaneHoldsContentsForTesting() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane never got the sidebar back, so the page is left without one";
}

// The last hypothesis, measured rather than argued: two earlier
// explanations were ruled out in the VM. The only difference left from the
// strip test's original shape is that it collapsed with one Settle where
// CollapseAndWait loops, so the reveal may land in a pane still collapsing.
// A pass kills the hypothesis, which is worth knowing before anyone edits
// production code for it.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       ADismissDuringAnUnawaitedCollapseStillDocksTheSidebar) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane holds no sidebar at rest, so this test cannot see a handover";

  // The original shape: one hop, not CollapseAndWait's loop.
  window()->SetSidebarWidth(0);
  Settle();

  window()->RevealFloatingSidebar(true);
  Settle();
  window()->RevealFloatingSidebar(false);

  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->SidebarPaneHoldsContentsForTesting() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane never got the sidebar back after a dismiss issued during an "
         "un-awaited collapse - the remaining hypothesis holds";
}

// CONTROL. Both surviving crashes land in Chromium's own code - mojo here, cc
// the time before - with no atrium frame on the stack. So the question is
// whether the panel provokes them or whether this build trips them under any
// churn of this shape. This cycles the docked width the same number of times
// and builds no widget at all. If it crashes too, the panel is not the cause.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       ControlCyclingTheDockedWidthBuildsNoPanel) {
  ASSERT_TRUE(window());
  for (int i = 0; i < 20; ++i) {
    window()->SetSidebarWidth(0);
    Settle();
    window()->SetSidebarWidth(220);
    Settle();
  }
  ExpectDocksAgain();
}

// The reporter's bug, 2026-08-23: the revealed panel was empty. Two things arm
// on the same window edge - the parent's RevealFloatingSidebar and the panel's
// own strip tracker - and only the first handed the sidebar over, so whichever
// won the race decided whether the panel had anything in it.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       AStripRevealTakesTheSidebarJustAsTheParentPathDoes) {
  ASSERT_TRUE(window());
  // Every check here reads whether the pane HOLDS the sidebar, and with no
  // document it holds none at rest - so all of them would pass vacuously.
  ASSERT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane holds no sidebar at rest, so this test cannot see a handover";
  window()->SetSidebarWidth(0);
  Settle();

  // The strip path below needs an existing widget, which is what a second
  // hover finds. Waited out: dismissing a panel mid-show is another path.
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel) << "the reveal built no panel, so nothing here is tested";
  EXPECT_FALSE(window()->SidebarPaneHoldsContentsForTesting())
      << "the parent's own reveal path left the contents in the docked pane";

  window()->RevealFloatingSidebar(false);
  Settle();
  // The dock-back is posted out of the slide's completion, so give it room.
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->SidebarPaneHoldsContentsForTesting() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  ASSERT_TRUE(window()->SidebarPaneHoldsContentsForTesting())
      << "the pane never got the sidebar back, so the check below is vacuous";

  // The path that regressed: the panel's own tracker, with no parent call.
  panel->OnStripEntered();
  Settle();
  EXPECT_FALSE(window()->SidebarPaneHoldsContentsForTesting())
      << "the strip reveal showed a panel with no sidebar in it";
}

// The panel carries its own width handle in the page, because the
// in-window seam cannot serve it: the panel is a child window and takes every
// event over its own frame, and a band placed just outside it reads as a
// handle floating away from the sidebar. The page's handle sends these two,
// and this is the pair that has to reach the panel.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       ThePagesOwnHandleResizesThePanelAndCommitsTheWidth) {
  ASSERT_TRUE(window());
  // The commit below writes the document, and a closed store swallows it - so
  // without this the width assertion could only ever read the default back
  //.
  ASSERT_TRUE(atrium_core_is_open())
      << "atrium_core is not open in this test process, so no width can persist";
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(panel->IsShowing());
  const int before = panel->WidthForTesting();
  ASSERT_GT(before, 0);

  base::ListValue live;
  live.Append(before + 60);
  window()->OnSurfaceMessage("sidebarOverlayWidthLive", live);
  Settle();
  EXPECT_EQ(before + 60, panel->WidthForTesting())
      << "a drag tick from the page's handle did not reach the panel";
  EXPECT_TRUE(window()->IsSidebarCollapsed())
      << "the drag re-expanded the docked pane the panel is floating over";

  base::ListValue commit;
  commit.Append(before + 60);
  window()->OnSurfaceMessage("sidebarOverlayWidthCommit", commit);
  Settle();
  EXPECT_EQ(before + 60, window()->LastDockedSidebarWidth())
      << "the commit did not stick, so docking returns to the old width and "
         "the two modes drift apart";
}

// The panel is a child window and draws above the parent's frame
// view, so the real close/minimise/zoom buttons are behind it however they are
// arranged - moving them onto the panel leaves them under the page's own
// compositor layer. The page draws its own three dots instead, the way the
// lab's chrome row does, and this is the flag they key off.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheSidebarLearnsWhenItIsFloatingSoItCanDrawItsOwnDots) {
  ASSERT_TRUE(window());
  content::WebContents* sidebar = window()->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar page to push the flag into";
  // Before the document commits there is no documentElement to read, and EvalJs
  // reports that as a JS TypeError rather than as a wait this test forgot.
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  constexpr char kReadFlag[] =
      "document.documentElement.classList.contains('atrium-sidebar-floating')";

  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  EXPECT_EQ(false, content::EvalJs(sidebar, kReadFlag))
      << "a collapsed pane with no panel is not floating";

  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  EXPECT_EQ(true, content::EvalJs(sidebar, kReadFlag))
      << "the page was never told the panel is up, so it draws no window dots "
         "and the panel has no way to close the window";

  window()->RevealFloatingSidebar(false);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (content::EvalJs(sidebar, kReadFlag).ExtractBool() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_EQ(false, content::EvalJs(sidebar, kReadFlag))
      << "the flag outlived the panel, so the docked pane draws the dots on "
         "top of the real ones";
}

// The edge zone the reveal arms on is a strip in the parent window,
// and the panel covers none of it - so travelling from the zone onto the panel
// exits the zone. Closing there shut the panel the moment the pointer reached
// it, which is what the reporter felt as "instantly gets hidden".
// A child window hides the window's backdrop, and a page-drawn wash carries
// only the flat tint, so the panel paints the tint under the page itself.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheFloatingPanelPaintsTheWindowsTintUnderThePage) {
  ASSERT_TRUE(window());
  window()->SetBackdropTint(SkColorSetRGB(0x11, 0x22, 0x33));
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel);

  // The tint crossfades in, so the root and the panel are read together once
  // the root has landed on it.
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  std::optional<SkColor> root;
  while (base::TimeTicks::Now() < deadline) {
    root = window()->RootBackgroundColorForTesting();
    if (root && SkColorSetA(*root, 0xFF) == SkColorSetRGB(0x11, 0x22, 0x33)) {
      break;
    }
    Settle();
  }
  ASSERT_TRUE(root) << "the root never took the tint";
  const SkColor pixel = panel->BackdropPixelForTesting();
  // Within one: the readback canvas premultiplies, and 0x11 at the glass alpha
  // comes back as 0x10.
  EXPECT_NEAR(0x11, SkColorGetR(pixel), 1) << "the panel's wash is not the tint";
  EXPECT_NEAR(0x22, SkColorGetG(pixel), 1) << "the panel's wash is not the tint";
  EXPECT_NEAR(0x33, SkColorGetB(pixel), 1) << "the panel's wash is not the tint";
  EXPECT_EQ(SkColorGetA(*root), SkColorGetA(pixel))
      << "the panel's wash sits at a different glass alpha than the root's";
}

// The frost sits in the parent window at the panel's full rect. A frost that
// only fades shows as a lighter rectangle wherever the sliding content has
// not arrived yet, so it has to travel the content's path on both legs.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheFrostSlidesWithThePanelOnBothLegs) {
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  EXPECT_TRUE(window()->SidebarFrostSlidingForTesting())
      << "the reveal started no slide on the frost, so it fades in place";

  // The reveal's slide removes itself when it lands. Only once it is gone can
  // a slide found after the dismiss be the dismiss's own.
  base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (window()->SidebarFrostSlidingForTesting() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  ASSERT_FALSE(window()->SidebarFrostSlidingForTesting())
      << "the reveal's slide never landed";

  // The dismiss slide is held until the widget hides, so it is read before
  // the panel has gone.
  window()->RevealFloatingSidebar(false);
  deadline = base::TimeTicks::Now() + base::Seconds(5);
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel);
  bool slid = false;
  while (panel->IsShowing() && base::TimeTicks::Now() < deadline) {
    slid = slid || window()->SidebarFrostSlidingForTesting();
    Settle();
  }
  EXPECT_TRUE(slid)
      << "the dismiss started no slide on the frost, so it fades in place";
}

// The frost used to be its own clip and its own slide at once, so
// mid-slide it showed a band the panel did not cover yet, and the two together
// read as a thinner panel.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheFrostShowsTheSameBandAsThePanelMidSlide) {
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());

  window()->RevealFloatingSidebar(true);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  int sampled = 0;
  while (base::TimeTicks::Now() < deadline) {
    Settle();
    auto* panel = window()->SidebarOverlayWidgetForTesting();
    if (!panel || !panel->IsShowing()) {
      continue;
    }
    if (!window()->SidebarFrostSlidingForTesting()) {
      break;
    }
    ++sampled;
    EXPECT_NEAR(panel->SlideDxForTesting(),
                window()->SidebarFrostSlideDxForTesting(), 1.0)
        << "the frost sits at a different offset than the panel, so one of "
           "them shows page the other covers";
  }
  EXPECT_GT(sampled, 0) << "no frame of the reveal slide was sampled at all";
}

// slide_dx is the model transform, already at the target before the
// animation is added, so only the presentation layer says where a light draws.
// A 60fps recording put all three off screen two frames before the pane moved.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheLightsTravelInStepWithTheWidthThePaneHasLost) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->ProbeWindowControlsForTesting().exists);
  const int docked = window()->SidebarWidthForTesting();
  ASSERT_GT(docked, 0);

  window()->SetSidebarWidth(0);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  int sampled = 0;
  int worst_at = 0;
  double worst = 0;
  while (!window()->IsSidebarCollapsed() &&
         base::TimeTicks::Now() < deadline) {
    const auto probe = window()->ProbeWindowControlsForTesting();
    if (probe.sliding && probe.has_presentation) {
      ++sampled;
      const int lost = docked - window()->SidebarWidthForTesting();
      const double gap = std::abs(probe.presentation_dx + lost);
      if (gap > worst) {
        worst = gap;
        worst_at = lost;
      }
    }
    Settle();
  }
  EXPECT_GT(sampled, 0) << "no frame of the slide was sampled at all";
  // One tick of the pane's ease-out covers about 63pt at its steepest, so a
  // gap wider than this is the lights on a different clock, not jitter.
  EXPECT_LT(worst, 40.0)
      << "a light sat " << worst << "pt from the pane's closing edge when the "
      << "pane had lost " << worst_at << "pt, so the two are not travelling "
      << "together";
}

// The lights used to fade where they stood while the pane slid out
// from under them, leaving them over the page for the whole hide.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheLightsTravelTheWidthThePaneLoses) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->ProbeWindowControlsForTesting().exists);
  EXPECT_NEAR(0, window()->ProbeWindowControlsForTesting().slide_dx, 0.5)
      << "the docked lights are already offset";

  window()->SetSidebarWidth(0);
  base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  bool slid = false;
  while (!window()->IsSidebarCollapsed() &&
         base::TimeTicks::Now() < deadline) {
    slid = slid || window()->ProbeWindowControlsForTesting().sliding;
    Settle();
  }
  ASSERT_TRUE(window()->IsSidebarCollapsed()) << "the pane never collapsed";
  EXPECT_TRUE(slid) << "the lights ran no slide, so they faded in place";

  deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (window()->ProbeWindowControlsForTesting().sliding &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  // The spaces.json this fixture writes docks the pane at 220.
  EXPECT_NEAR(-220, window()->ProbeWindowControlsForTesting().slide_dx, 1.0)
      << "the lights did not land the width the pane lost, so they part "
         "company with its closing edge";

  ASSERT_NO_FATAL_FAILURE(ExpectDocksAgain());
  deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (window()->ProbeWindowControlsForTesting().sliding &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_NEAR(0, window()->ProbeWindowControlsForTesting().slide_dx, 1.0)
      << "the reveal left the lights short of where they started";
}

// AppKit draws a control below full alpha in its inactive grey, and
// an ease-out fade over the pane's duration also empties the band a third of
// the way into a slide the pane is two thirds from finishing.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheLightsKeepFullOpacityForEveryFrameOfTheHide) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->ProbeWindowControlsForTesting().exists);

  window()->SetSidebarWidth(0);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  bool sampled = false;
  while (!window()->IsSidebarCollapsed() &&
         base::TimeTicks::Now() < deadline) {
    const auto probe = window()->ProbeWindowControlsForTesting();
    if (probe.sliding) {
      sampled = true;
      EXPECT_FALSE(probe.layer_fading)
          << "a fade is back on the layer, which greys the button and empties "
             "the band before the pane leaves it";
      EXPECT_DOUBLE_EQ(1.0, probe.layer_opacity)
          << "the layer opacity dipped mid-slide, so the light is drawn grey "
             "for the rest of the hide";
      EXPECT_DOUBLE_EQ(1.0, probe.model_alpha)
          << "the button's own alpha dipped mid-slide, which is the redraw "
             "AppKit answers in its inactive grey";
    }
    Settle();
  }
  EXPECT_TRUE(sampled) << "no frame of the hide was sampled at all";
}

IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       AStaleHideDoesNotTakeTheLightsMidSlide) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->ProbeWindowControlsForTesting().exists);

  // Three chords inside one 150ms slide. A CATransaction completion block runs
  // on its own schedule, so the first hide's is still pending when the third
  // starts, and it answers for a collapse that is no longer the one on screen.
  window()->SetSidebarWidth(0);
  Settle();
  window()->SetSidebarWidth(220);
  Settle();
  window()->SetSidebarWidth(0);

  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  bool sampled = false;
  while (base::TimeTicks::Now() < deadline) {
    if (!window()->ProbeWindowControlsForTesting().sliding) {
      break;
    }
    sampled = true;
    EXPECT_TRUE(window()->WindowControlsVisibleForTesting())
        << "a stale hide took the lights away while the live slide was still "
           "running, so they vanish instead of travelling off";
    Settle();
  }
  EXPECT_TRUE(sampled) << "no frame of the last slide was sampled at all";
}

// Measured from what the buttons draw rather than taken from the
// macOS default: these lights draw 14pt across at 23pt centres, not 12 and 20.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheRealLightsMeasureTheSizeTheDotTokensCarry) {
  ASSERT_TRUE(window());
  const auto probe = window()->ProbeWindowControlsForTesting();
  ASSERT_TRUE(probe.exists);
  ASSERT_GT(probe.diameter, 0) << "the lights drew nothing to measure";

  // The page draws its own dots from trafficLightDotSize and
  // trafficLightDotPitch in rust/atrium-core/src/tokens.rs. Re-measure and
  // move those if macOS changes the lights under this.
  EXPECT_NEAR(14.0, probe.diameter, 0.5)
      << "the real lights are not the diameter the dot token carries";
  EXPECT_NEAR(23.0, probe.pitch, 0.5)
      << "the real lights are not the pitch the dot token carries";
}

IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       LeavingTheEdgeZoneDoesNotShutAPanelOnTheSpot) {
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(panel->IsShowing());

  // What the pointer moving off the strip and onto the panel produces.
  window()->NoteFloatingSidebarEdgeExit();
  Settle();

  EXPECT_TRUE(panel->IsShowing())
      << "the panel closed on the zone exit, so it shuts as soon as the "
         "pointer travels onto it";
}

// The page's dots send these, and a message that reaches the window
// and does nothing is indistinguishable from a dot that never sent it - which
// is the shape of the report.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheWindowButtonMessagesActOnTheWindow) {
  ASSERT_TRUE(window());
  ASSERT_FALSE(window()->widget()->IsMinimized());

  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarWindowMinimize", empty);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->widget()->IsMinimized() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->widget()->IsMinimized())
      << "sidebarWindowMinimize reached the window and did nothing";
}

// The earlier check ran with no panel up, which is not the state the
// dots are ever clicked in. A child window is attached to the parent while the
// panel floats, and that is exactly the case where a window command can be
// swallowed.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheWindowButtonsActWhileThePanelIsUp) {
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());
  auto* panel = window()->SidebarOverlayWidgetForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(panel->IsShowing()) << "no panel, so this is the old test again";

  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarWindowMinimize", empty);
  base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->widget()->IsMinimized() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->widget()->IsMinimized())
      << "minimise did nothing with the panel up";
  EXPECT_FALSE(panel->IsShowing())
      << "the panel outlived the window command that dismissed it";
}

// The red dot used to close the widget directly. That drained the tabs and
// came back as an automatic close, which the last-tab-out guard kept open.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheCloseDotClosesAWindowWithTabsOpen) {
  ASSERT_TRUE(window());
  ASSERT_FALSE(browser()->tab_strip_model()->empty());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());

  bool browser_did_close = false;
  auto subscription = browser()->RegisterBrowserDidClose(
      base::BindLambdaForTesting(
          [&](BrowserWindowInterface*) { browser_did_close = true; }));
  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarWindowClose", empty);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (!browser_did_close && base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(browser_did_close) << "the close dot left the window open";
}

// The green dot is full screen on macOS, not zoom.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheGreenDotEntersFullScreen) {
  ASSERT_TRUE(window());
  ASSERT_FALSE(window()->IsFullscreen());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());

  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarWindowZoom", empty);
  base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (!window()->IsFullscreen() && base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->IsFullscreen())
      << "the green dot did not enter full screen";

  // Back out, so the window is not left full screen for the teardown.
  window()->OnSurfaceMessage("sidebarWindowZoom", empty);
  deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (window()->IsFullscreen() && base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_FALSE(window()->IsFullscreen())
      << "a second press did not leave full screen";
}

// A collapsed sidebar hides the traffic lights. In full screen they are the
// menu bar reveal's only way out, so they come back for the stay and hide
// again on exit.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheTrafficLightsShowInFullScreenWithTheSidebarHidden) {
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  // The collapse fade's completion does not land in this harness, so the
  // rule is checked directly and the applied state only where it is set
  // without a fade.
  ASSERT_TRUE(window()->HidesWindowControlsWhen(true))
      << "a collapsed sidebar keeps the lights, so this test proves nothing";

  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarWindowZoom", empty);
  base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (!window()->IsFullscreen() && base::TimeTicks::Now() < deadline) {
    Settle();
  }
  ASSERT_TRUE(window()->IsFullscreen());
  EXPECT_FALSE(window()->HidesWindowControlsWhen(true));
  // IsFullscreen() answers about 60ms before the widget notification that
  // brings the lights back, so reading on this same tick asks the window a
  // question it has not been told the answer to yet.
  deadline = base::TimeTicks::Now() + base::Seconds(5);
  while (!window()->WindowControlsVisibleForTesting() &&
         base::TimeTicks::Now() < deadline) {
    Settle();
  }
  EXPECT_TRUE(window()->WindowControlsVisibleForTesting())
      << "full screen with a hidden sidebar has no lights to leave by";

  window()->OnSurfaceMessage("sidebarWindowZoom", empty);
  deadline = base::TimeTicks::Now() + base::Seconds(10);
  while (window()->IsFullscreen() && base::TimeTicks::Now() < deadline) {
    Settle();
  }
  ASSERT_FALSE(window()->IsFullscreen());
  EXPECT_TRUE(window()->HidesWindowControlsWhen(true))
      << "the lights would stay up after full screen with the sidebar hidden";
}

// The settings window's Hide sidebar switch acts on the window that opened
// it, both ways, through the same toggle Cmd+S uses.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheSettingsSwitchCollapsesAndRestoresTheSidebar) {
  ASSERT_TRUE(window());
  ASSERT_FALSE(window()->IsSidebarCollapsed());

  atrium::AtriumSettingsWindow::Show();
  atrium::AtriumSettingsWindow* settings =
      atrium::AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(settings);
  ASSERT_TRUE(content::WaitForLoadStop(settings->WebContentsForTesting()));

  base::ListValue hide;
  hide.Append(true);
  settings->OnSurfaceMessage("settingsSidebarHiddenChanged", hide);
  EXPECT_TRUE(WaitForCollapsed(true)) << "the switch never collapsed the sidebar";

  base::ListValue show;
  show.Append(false);
  settings->OnSurfaceMessage("settingsSidebarHiddenChanged", show);
  EXPECT_TRUE(WaitForCollapsed(false)) << "the switch never brought it back";
}

// The gap is user-tuned, so nothing derives it and nothing else would notice a
// change. OnStripEntered also rests on it: the uncovered band along the docked
// edge is what lets a window resize drag start there instead of on the panel.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       ThePanelSitsFivePointsInFromTheWindowEdges) {
  constexpr int kExpectedInset = 5;
  ASSERT_TRUE(window());
  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  ASSERT_NO_FATAL_FAILURE(RevealAndWait());

  const gfx::Rect win = window()->widget()->GetWindowBoundsInScreen();
  const gfx::Rect panel =
      window()->SidebarOverlayWidgetForTesting()->BoundsForTesting();
  ASSERT_FALSE(panel.IsEmpty()) << "the panel reported no bounds";

  EXPECT_EQ(kExpectedInset, panel.x() - win.x());
  EXPECT_EQ(kExpectedInset, panel.y() - win.y());
  EXPECT_EQ(kExpectedInset, win.bottom() - panel.bottom());

  // Not ConcentricCornerRadius(outer, kExpectedInset) as the expected: that is
  // the code the panel's own radius comes from, and it would agree with itself
  // even if the curve pinched at the corner.
  const float outer = atrium::AtriumWindowCornerRadius();
  ASSERT_GT(outer, 0) << "no window corner to be concentric with";
  EXPECT_FLOAT_EQ(
      outer - kExpectedInset,
      window()->SidebarOverlayWidgetForTesting()->CornerRadiusForTesting())
      << "the panel's curve is not parallel to the window's at the new gap";
}

// A CALayer transform moves drawn pixels, and AppKit routes the
// mouse through the view's own frame, which never moves. The band the lights
// vacate is content the user can see and will click.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheLightsStopTakingClicksAtTheCornerTheyLeft) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(window()->ProbeWindowControlsForTesting().exists);
  ASSERT_TRUE(
      window()->ProbeWindowControlsForTesting().settled_corner_takes_a_click)
      << "a docked light does not answer a click at its own centre, so this "
         "probe cannot tell a live hit target from a released one";

  window()->SetSidebarWidth(0);
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  bool caught_mid_slide = false;
  bool took_a_click_mid_slide = false;
  while (!window()->IsSidebarCollapsed() && base::TimeTicks::Now() < deadline) {
    const auto probe = window()->ProbeWindowControlsForTesting();
    // Only frames where a light has left its corner and is still on screen:
    // before that a hit there is correct, after it there is nothing to hit.
    if (probe.sliding && std::abs(probe.slide_dx) > probe.diameter) {
      caught_mid_slide = true;
      took_a_click_mid_slide =
          took_a_click_mid_slide || probe.settled_corner_takes_a_click;
    }
    Settle();
  }
  ASSERT_TRUE(window()->IsSidebarCollapsed()) << "the pane never collapsed";
  ASSERT_TRUE(caught_mid_slide)
      << "no frame was sampled with a light off its corner, so the reading "
         "below is of a slide that was never observed";

  EXPECT_FALSE(took_a_click_mid_slide)
      << "a click at the lights' settled corner still closes, minimises or "
         "zooms the window while they are drawn elsewhere";
}

// Cmd+S twice. AppKit reports a light at alpha 0 as disabled, so a reveal that
// saved the enabled state after the settled hide wrote NO back when it landed.
IN_PROC_BROWSER_TEST_F(AtriumSidebarOverlayBrowserTest,
                       TheLightsTakeClicksAfterARevealThatFollowsASettledHide) {
  ASSERT_TRUE(window());
  ASSERT_TRUE(
      window()->ProbeWindowControlsForTesting().settled_corner_takes_a_click);
  const auto wait_for_slide_end = [&] {
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
    while (window()->ProbeWindowControlsForTesting().sliding &&
           base::TimeTicks::Now() < deadline) {
      Settle();
    }
  };

  ASSERT_NO_FATAL_FAILURE(CollapseAndWait());
  wait_for_slide_end();
  ASSERT_FALSE(window()->ProbeWindowControlsForTesting().sliding)
      << "the hide never settled, so the reveal below starts mid-slide";

  // Polled: the width grows before the reveal slide lands, and the lights are
  // disabled on purpose until it does. A broken reveal never recovers.
  ExpectDocksAgain();
  const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(5);
  bool takes_a_click = false;
  while (!takes_a_click && base::TimeTicks::Now() < deadline) {
    Settle();
    takes_a_click =
        window()->ProbeWindowControlsForTesting().settled_corner_takes_a_click;
  }
  EXPECT_TRUE(takes_a_click) << "the lights came back disabled and grey";
}

}  // namespace
