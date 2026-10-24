// Copyright 2026 The Atrium Authors

#include <cstdint>
#include <optional>

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

// The Courtyard's ground is glass over the window's own NSVisualEffectView
// backdrop, so the sidebar, the page and root_view_'s painted background all
// have to be out of the way while it is open, and all three back on close.
namespace atrium {
namespace {

constexpr uint32_t kTint = 0xFF2A4C6E;
constexpr uint32_t kOtherTint = 0xFF7E3B2A;

class AtriumCourtyardGlassBrowserTest : public InProcessBrowserTest {
 protected:
  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // A tint gives the root a background to lose and to get back - without one
  // RestoreRootBackground's own answer is nullptr, and the close assertion
  // would pass against a build that restores nothing.
  void OpenOverATintedRoot() {
    window()->SetBackdropTintLive(kTint);
    ASSERT_TRUE(window()->RootBackgroundColorForTesting().has_value());
    // Re-opened per poll: Show() is fail-soft until the reserved chrome
    // context lands, and it refuses to show at all with no page behind it.
    ASSERT_TRUE(base::test::RunUntil([&]() {
      window()->OpenCourtyard();
      return window()->CourtyardHidesContentForTesting();
    })) << "opening the Courtyard left the sidebar, the pane or the root "
           "background painting";
  }
};

// Mutation this pins: dropping courtyard_open_ from InstallRootBackground's
// guard lets a space switch repaint the root under the open glass.
IN_PROC_BROWSER_TEST_F(AtriumCourtyardGlassBrowserTest,
                       ATintPushWhileOpenDoesNotRepaintTheRoot) {
  ASSERT_NO_FATAL_FAILURE(OpenOverATintedRoot());

  window()->SetBackdropTintLive(kOtherTint);

  EXPECT_EQ(std::nullopt, window()->RootBackgroundColorForTesting());
  EXPECT_TRUE(window()->CourtyardHidesContentForTesting());
}

IN_PROC_BROWSER_TEST_F(AtriumCourtyardGlassBrowserTest,
                       CloseHandsBackTheContentAndTheBackground) {
  ASSERT_NO_FATAL_FAILURE(OpenOverATintedRoot());

  window()->CloseCourtyard();

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->CourtyardContentRestoredForTesting(); }))
      << "closing the Courtyard did not bring back all three of the sidebar "
         "slot, the pane container and the root background";
  EXPECT_TRUE(window()->RootBackgroundColorForTesting().has_value())
      << "the root came back painting nothing, so the tint was lost with the "
         "Courtyard rather than restored";
}

}  // namespace
}  // namespace atrium
