// Copyright 2026 The Atrium Authors

#include <string>
#include <string_view>
#include <vector>

#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Two window values the SIDEBAR has to draw and C++ cannot draw
// for it: the glass alpha, because the floating panel is a child window whose
// material samples nothing, and the native-app chrome mode, whose fill stops at
// the sidebar column. Both cross as one push, and a push that never lands looks
// exactly like a page that ignored it - so the assertion is on the page's own
// state rather than on the call.
namespace {

class AtriumSidebarAppearanceBrowserTest : public InProcessBrowserTest {
 protected:
  atrium::AtriumBrowserWindow* window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // The push is an ExecuteJavaScript, so nothing it does has happened when the
  // setter returns.
  void Settle() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, loop.QuitClosure());
    loop.Run();
  }

  // Whether the pushed ink is the dark answer. Reads the three channels the
  // push writes, so it grades the CONTRAST decision rather than the particular
  // near-black or near-white Chromium picks to express it.
  bool IsDarkInk(content::WebContents* sidebar) {
    const std::string ink = ReadProperty(sidebar, "--atrium-surround-ink");
    const std::vector<std::string_view> channels = base::SplitStringPiece(
        ink, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (channels.size() != 3) {
      ADD_FAILURE() << "no ink was pushed at all, read '" << ink << "'";
      return false;
    }
    int total = 0;
    for (std::string_view channel : channels) {
      int value = 0;
      if (!base::StringToInt(channel, &value)) {
        ADD_FAILURE() << "the ink is not three numbers, read '" << ink << "'";
        return false;
      }
      total += value;
    }
    return total / 3 < 128;
  }

  // Waits, because before the document commits there is no documentElement and
  // ExtractString CHECKs on the JS TypeError rather than returning it - which
  // takes the whole process down and reports as a crash, not a failure.
  std::string ReadProperty(content::WebContents* sidebar, const char* name) {
    EXPECT_TRUE(content::WaitForLoadStop(sidebar));
    return content::EvalJs(
               sidebar,
               content::JsReplace(
                   "document.documentElement.style.getPropertyValue($1)", name))
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheSidebarLearnsTheGlassAlphaItHasToPaintWith) {
  ASSERT_TRUE(window());
  content::WebContents* sidebar = window()->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar page to push the appearance into";

  window()->SetGlassBackdropAlpha(40);
  Settle();
  // Two decimals, because the push writes two. The setting is a percent and
  // the property is a fraction, and doing that division once in C++ is what
  // stops 184/255 being rounded differently by whoever reads it next.
  EXPECT_EQ("0.40", ReadProperty(sidebar, "--atrium-glass-alpha"))
      << "the panel still paints its own fixed wash, so the Glass opacity "
         "setting cannot reach the one surface whose alpha IS the effect";
}

IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheNativeAppModeCrossesIntoTheSidebarAndBackOut) {
  ASSERT_TRUE(window());
  content::WebContents* sidebar = window()->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar page to push the appearance into";
  // Before the document commits there is no documentElement to read, and EvalJs
  // reports that as a JS TypeError rather than as a wait this test forgot.
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));
  constexpr char kReadMark[] =
      "document.documentElement.classList.contains('atrium-native-chrome')";

  EXPECT_EQ(false, content::EvalJs(sidebar, kReadMark))
      << "the mode is off by default";

  window()->SetOpaqueSurround(1, 0xFFF2F2F7);
  Settle();
  EXPECT_EQ(true, content::EvalJs(sidebar, kReadMark))
      << "the surround turned native and the sidebar was never told, so the "
         "two halves of the window's chrome do not match";
  EXPECT_EQ("#f2f2f7", ReadProperty(sidebar, "--atrium-surround-color"))
      << "the sidebar has no colour to paint, so it cannot match the surround "
         "it is beside";
  // The chrome's ink is white everywhere else, so a near-white surround has to
  // carry its own or the column comes back blank - measured at mean 251.7 with
  // the labels gone.
  //
  // Dark, not an exact triple: the ink is whatever answers the surround with
  // the most contrast, and Chromium's answer for "as dark as possible" is its
  // own near-black rather than 0,0,0. Pinning the triple would pin that
  // choice instead of the rule.
  EXPECT_TRUE(IsDarkInk(sidebar))
      << "a light surround was handed light ink, which is no ink at all: "
      << ReadProperty(sidebar, "--atrium-surround-ink");

  // A dark surround takes the other answer, which is what makes this a
  // contrast rule rather than a hardcoded flip.
  window()->SetOpaqueSurround(1, 0xFF1E252D);
  Settle();
  EXPECT_FALSE(IsDarkInk(sidebar))
      << "a dark surround was handed dark ink: "
      << ReadProperty(sidebar, "--atrium-surround-ink");

  window()->SetOpaqueSurround(0, 0);
  Settle();
  EXPECT_EQ(false, content::EvalJs(sidebar, kReadMark))
      << "the mark outlived the mode, so the sidebar stays opaque over glass";
}

// The sidebar hosts the toast stack, whose cards read every colour from
// overlay.css. Without the tint push the stack falls back to brand crimson on
// every space, which is the drift the shared layer exists to stop.
IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheSidebarLearnsTheSpaceTintItsToastsPaintWith) {
  ASSERT_TRUE(window());
  content::WebContents* sidebar = window()->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar) << "no sidebar page to push the theme into";
  // A push before the document commits is written on the page it replaces.
  ASSERT_TRUE(content::WaitForLoadStop(sidebar));

  window()->SetBackdropTintLive(0xFF112233);
  Settle();
  EXPECT_EQ("17 34 51", ReadProperty(sidebar, "--atrium-space-tint"))
      << "the overlay theme never reached the sidebar, so a toast card cannot "
         "wear the space it opened in";
}

// The native surround painted a flat fill on the root and nothing else, so a
// page's colour never reached the chrome in that mode.
IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheNativeSurroundCarriesThePagesGlow) {
  ASSERT_TRUE(window());
  window()->SetEdgeTint(atrium::EdgeTint());
  window()->SetOpaqueSurround(1, 0xFFF2F2F7);
  const SkColor bare = window()->root_glow_pixel_for_testing();

  atrium::EdgeTint glow;
  glow.SetEdge(atrium::Edge::kLeft, SK_ColorRED);
  glow.SetEdge(atrium::Edge::kRight, SK_ColorBLUE);
  glow.SetEdge(atrium::Edge::kTop, SK_ColorRED);
  glow.SetEdge(atrium::Edge::kBottom, SK_ColorBLUE);
  window()->SetEdgeTint(glow);
  // The first ticks of the crossfade shift the pixel by one unit, which a
  // channel comparison cannot grade.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window()->EdgeTintAnimatingForTesting(); }))
      << "the edge tint never finished its crossfade";
  EXPECT_NE(bare, window()->root_glow_pixel_for_testing())
      << "the glow never painted next to the card on the native surround";
  EXPECT_GT(SkColorGetR(window()->root_glow_pixel_for_testing()),
            SkColorGetB(window()->root_glow_pixel_for_testing()))
      << "the pixel left of the card does not carry the left edge's red";
}

// The core drifts the space tint at most a fifth of the way toward the page
// and pulls grey edges toward the favicon, so a navy header and a white body
// both came out as the same grey band on the flat surround.
IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheNativeSurroundGlowsThePagesOwnColour) {
  ASSERT_TRUE(window());
  window()->SetOpaqueSurround(1, 0xFFF2F2F7);
  window()->SetEdgeTint(atrium::EdgeTint());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window()->EdgeTintAnimatingForTesting(); }));
  const SkColor bare = window()->root_glow_pixel_for_testing();

  atrium::AtriumBrowserWindow::SetChromePageTint(true);
  // The window starts with no tab of its own to navigate.
  content::WebContents* tab = chrome::AddAndReturnTabAt(
      browser(),
      GURL("data:text/html,<meta name=theme-color content=%23102040>"
           "<body style=background:%23102040></body>"),
      -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window()->root_glow_pixel_for_testing() != bare &&
           !window()->EdgeTintAnimatingForTesting();
  })) << "the page's colour never reached the native surround";

  const SkColor glow = window()->root_glow_pixel_for_testing();
  // Navy at the band's strength over F2F2F7 lands near 118,127,146. The
  // drifted space tint stays well above 180 on red.
  EXPECT_LT(SkColorGetR(glow), 170u)
      << "the band beside the card is the space tint drifted, not the page's "
         "navy: "
      << base::StringPrintf("#%08X", glow);
  EXPECT_GT(SkColorGetB(glow), SkColorGetR(glow))
      << "the band does not lean blue: " << base::StringPrintf("#%08X", glow);
}

// The floating bar draws its own hints on the shared .ov-card, and
// the theme fan-out reached six surfaces without it.
IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheFloatingBarLearnsTheSpaceTintItsHintsPaintWith) {
  ASSERT_TRUE(window());
  window()->ToggleDevBarMode();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->DevBarContentsForTesting() != nullptr; }))
      << "no floating bar page to push the theme into";
  content::WebContents* bar = window()->DevBarContentsForTesting();

  window()->SetBackdropTintLive(0xFF112233);
  Settle();
  EXPECT_EQ("17 34 51", ReadProperty(bar, "--atrium-space-tint"))
      << "the overlay theme never reached the floating bar, so its hint cannot "
         "wear the space the sidebar's does";
}

// The sidebar's web layer stops kSidebarGripWidth short of the card,
// and that seam is covered by neither the pane container's fill nor the page -
// the grip's own OnPaint says so. It showed the window tint, which read as a
// hard vertical line between two otherwise white surfaces.
//
// The root carries the surround now, so the seam does too. Asserted on the
// root's colour rather than on pixels: a 6px column is exactly the measure
// this harness cannot grade.
IN_PROC_BROWSER_TEST_F(AtriumSidebarAppearanceBrowserTest,
                       TheSeamBesideTheSidebarTakesTheSurroundColour) {
  ASSERT_TRUE(window());

  window()->SetOpaqueSurround(1, 0xFFF2F2F7);
  EXPECT_EQ(std::make_optional(SkColorSetRGB(0xF2, 0xF2, 0xF7)),
            window()->RootBackgroundColorForTesting())
      << "the seam still shows whatever the root was painting, which is the "
         "tint the mode was supposed to replace";

  // A space switch installs a tint of its own, and it used to paint straight
  // over the surround on the next space the user swiped to.
  window()->SetBackdropTint(SkColorSetRGB(0x11, 0x22, 0x33));
  EXPECT_EQ(std::make_optional(SkColorSetRGB(0xF2, 0xF2, 0xF7)),
            window()->RootBackgroundColorForTesting())
      << "a tint repainted the root while the native mode owned it";

  // On the channels, not the whole colour: the tint is stored with the glass
  // alpha already in it, so an exact ARGB here would pin backdrop_glass_alpha_
  // rather than the handover this asserts.
  window()->SetOpaqueSurround(0, 0);
  // A tint is installed by now, so the mode crossfades out rather than snapping
  // and RestoreRootBackground only runs when that fade lands.
  window()->FinishSurroundFadeForTesting();
  const std::optional<SkColor> restored =
      window()->RootBackgroundColorForTesting();
  ASSERT_TRUE(restored.has_value())
      << "turning the mode off left the root painting nothing";
  EXPECT_EQ(SkColorSetRGB(0x11, 0x22, 0x33), SkColorSetA(*restored, 0xFF))
      << "turning the mode off did not give the tint its root back";
}

}  // namespace
