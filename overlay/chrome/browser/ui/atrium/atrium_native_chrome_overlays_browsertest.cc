// Copyright 2026 The Atrium Authors

#include <memory>
#include <string>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_environment_variable_override.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_hint_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_toast_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/compositor/layer.h"
#include "ui/views/test/views_test_utils.h"
#include "url/gurl.h"

// Covers the surfaces that route through PushOverlayTheme rather than the
// sidebar's own push (AtriumSidebarAppearanceBrowserTest).
namespace atrium {
namespace {

constexpr char kReadNativeClass[] =
    "document.documentElement.classList.contains('atrium-native-chrome')";

class AtriumNativeChromeOverlaysBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    settings_path_ = temp_dir_.GetPath().AppendASCII("settings.json");
    settings_env_override_ =
        std::make_unique<base::ScopedEnvironmentVariableOverride>(
            "ATRIUM_SETTINGS_FILE", settings_path_.value());
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    settings_env_override_.reset();
  }

  void TearDownOnMainThread() override {
    AtriumSettingsWindow::CloseForTesting();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath settings_path_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride>
      settings_env_override_;
};

// A settings window opened after the mode is already on (loaded at
// startup, or set in an earlier save) must not come up wearing glass.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       SettingsOpenedAfterTheModeIsOnPaintsNative) {
  WindowAppearance appearance;
  appearance.opaque_surround = true;
  appearance.surround_argb = 0xFFF2F2F7;
  AtriumBrowserWindow::SetWindowAppearance(appearance);

  AtriumSettingsWindow::Show();
  AtriumSettingsWindow* settings = AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  base::ListValue ready;
  settings->OnSurfaceMessage("settingsReady", ready);

  EXPECT_EQ(true, content::EvalJs(page, kReadNativeClass))
      << "the settings window's own ready handshake never learned the mode "
         "was already on";
}

// Settings always wears the system window colour, never the
// surround colour the browser windows paint, and never colour 0: black ground
// under black ink.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       SettingsIgnoresTheSurroundColour) {
  WindowAppearance appearance;
  appearance.opaque_surround = true;
  appearance.surround_argb = 0xFF3355AA;
  AtriumBrowserWindow::SetWindowAppearance(appearance);

  AtriumSettingsWindow::Show();
  AtriumSettingsWindow* settings = AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  base::ListValue ready;
  settings->OnSurfaceMessage("settingsReady", ready);

  constexpr char kReadSurround[] =
      "document.documentElement.style.getPropertyValue("
      "'--atrium-surround-color')";
  const std::string settings_colour =
      content::EvalJs(page, kReadSurround).ExtractString();
  EXPECT_NE("#000000", settings_colour);
  EXPECT_NE("#3355aa", settings_colour)
      << "the settings window wore the browser's surround colour";
}

// Native app view off is the default, and settings is still native.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       SettingsIsNativeWithTheModeOff) {
  AtriumSettingsWindow::Show();
  AtriumSettingsWindow* settings = AtriumSettingsWindow::ForTesting();
  ASSERT_TRUE(settings);
  content::WebContents* page = settings->WebContentsForTesting();
  ASSERT_TRUE(page);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  base::ListValue ready;
  settings->OnSurfaceMessage("settingsReady", ready);

  EXPECT_EQ(true, content::EvalJs(page, kReadNativeClass))
      << "settings wore the tinted glass with the mode off";
}

// The hint bubble reads the mode fresh on every Show(), so a hint
// raised while the mode is on must not paint glass.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       HintBubbleRaisedWhileNativeIsOnPaintsNative) {
  window()->SetOpaqueSurround(1, 0xFFF2F2F7);

  base::ListValue args;
  args.Append("Open in a split");
  args.Append("shift click");
  args.Append(10);
  args.Append(20);
  args.Append(200);
  args.Append(24);
  args.Append(2500);
  window()->OnSurfaceMessage("sidebarHintShow", args);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->HintOverlayWidgetForTesting() != nullptr; }));
  content::WebContents* hint =
      window()->HintOverlayWidgetForTesting()->web_contents();
  ASSERT_TRUE(hint);
  ASSERT_TRUE(content::WaitForLoadStop(hint));

  EXPECT_EQ(true, content::EvalJs(hint, kReadNativeClass))
      << "the hint bubble painted as if the mode were off";
}

// The hover URL card shares the same push path as the hint bubble.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       HoverUrlCardRaisedWhileNativeIsOnPaintsNative) {
  window()->SetOpaqueSurround(1, 0xFFF2F2F7);

  window()->SetStatusBubbleURL(window()->CurrentlyDisplayedWebContents(),
                               GURL("https://example.com/path"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->HoverUrlWidgetForTesting() != nullptr; }));
  content::WebContents* card =
      window()->HoverUrlWidgetForTesting()->web_contents();
  ASSERT_TRUE(card);
  ASSERT_TRUE(content::WaitForLoadStop(card));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window()->HoverUrlWidgetForTesting()->IsShowingForTesting();
  }));

  EXPECT_EQ(true, content::EvalJs(card, kReadNativeClass))
      << "the hover URL card painted as if the mode were off";
}

// The AI panel reads the sidebar's ink token, so a live toggle must change it.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       AiPanelInkFollowsTheNativeToggle) {
  content::WebContents* panel = window()->AiPanelContentsForTesting();
  ASSERT_TRUE(panel);
  ASSERT_TRUE(content::WaitForLoadStop(panel));
  constexpr char kReadInk[] =
      "getComputedStyle(document.getElementById('atrium-root'))"
      ".getPropertyValue('--atrium-ink').trim()";

  window()->SetOpaqueSurround(0, 0xFFF2F2F7);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return !content::EvalJs(panel, kReadInk).ExtractString().empty();
  })) << "the AI panel has no ink token with the mode off";
  const std::string ink_off = content::EvalJs(panel, kReadInk).ExtractString();

  window()->SetOpaqueSurround(1, 0xFFF2F2F7);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(panel, kReadNativeClass).ExtractBool();
  })) << "the AI panel painted as if the mode were off";

  EXPECT_NE(ink_off, content::EvalJs(panel, kReadInk).ExtractString())
      << "the AI panel kept the glass ink on the native surround";
}

// BrowserController::window() is private. OnBrowserCreated fires
// synchronously on CreateLittleWindow's own call stack (the same seam
// AtriumMoveLittleTabToSpaceBrowserTest uses to reach a little window).
class NewLittleWindowCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// The little window's foyer route has a WebContents like every other
// surface PushAiPanelInk fans to.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       FoyerRaisedWhileNativeIsOnPaintsNative) {
  NewLittleWindowCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  BrowserController* little = BrowserController::CreateLittleWindow(
      browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(little);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);
  AtriumBrowserWindow* little_window = static_cast<AtriumBrowserWindow*>(
      capture.captured_->GetBrowserForMigrationOnly()->window());
  ASSERT_TRUE(little_window);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return little_window->FoyerContentsForTesting() != nullptr; }));
  content::WebContents* foyer = little_window->FoyerContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(foyer));

  little_window->SetOpaqueSurround(1, 0xFFF2F2F7);

  EXPECT_EQ(true, content::EvalJs(foyer, kReadNativeClass))
      << "the foyer painted as if the mode were off";
}

// A background tab's title change must not push the foyer, which only shows
// the active tab.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       FoyerNotPushedForBackgroundTabTitle) {
  NewLittleWindowCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  BrowserController* little = BrowserController::CreateLittleWindow(
      browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(little);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);
  Browser* little_browser = capture.captured_->GetBrowserForMigrationOnly();

  content::WebContents* background = chrome::AddAndReturnTabAt(
      little_browser, GURL("about:blank"), -1, /*foreground=*/false);
  ASSERT_TRUE(background);
  ASSERT_TRUE(content::WaitForLoadStop(background));
  ASSERT_NE(background,
            little_browser->tab_strip_model()->GetActiveWebContents());

  ASSERT_TRUE(content::ExecJs(background, "document.title = 'same'"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return base::UTF16ToUTF8(background->GetTitle()) == "same"; }));
  const int pushes = little->FoyerPushesForTesting();

  ASSERT_TRUE(content::ExecJs(background, "document.title = 'next'"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return base::UTF16ToUTF8(background->GetTitle()) == "next"; }));
  EXPECT_EQ(pushes, little->FoyerPushesForTesting());
}

// PushAiPanelInk only runs on a CHANGE, so a foyer built after the
// last one needs its own push at build time too (the same rule as the devbar).
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       FoyerOpenedAfterTheModeIsOnPaintsNative) {
  WindowAppearance appearance;
  appearance.opaque_surround = true;
  appearance.surround_argb = 0xFFF2F2F7;
  AtriumBrowserWindow::SetWindowAppearance(appearance);

  NewLittleWindowCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  BrowserController* little = BrowserController::CreateLittleWindow(
      browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(little);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);
  AtriumBrowserWindow* little_window = static_cast<AtriumBrowserWindow*>(
      capture.captured_->GetBrowserForMigrationOnly()->window());
  ASSERT_TRUE(little_window);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return little_window->FoyerContentsForTesting() != nullptr; }));
  content::WebContents* foyer = little_window->FoyerContentsForTesting();
  ASSERT_TRUE(content::WaitForLoadStop(foyer));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(foyer, kReadNativeClass).ExtractBool();
  })) << "the foyer's own build path never learned the mode was already on";
}

// Read the pane's y, not its x: docked left, the sidebar owns
// the left gutter and this pane's own left inset is 0.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       ABorderWidthChangeReachesALiveWindow) {
  AtriumBrowserWindow* live = window();
  ASSERT_TRUE(live);
  views::test::RunScheduledLayout(live->ActivePaneForTesting()->parent());
  const int before = live->PaneBoundsForTesting(0).y();
  EXPECT_EQ(before, 10) << "the untouched default is not the old inset";

  WindowAppearance appearance;
  appearance.border_width = 18;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(live->ActivePaneForTesting()->parent());
  // Mutation this pins: dropping the border_width branch from
  // ApplyWindowAppearance leaves an already-open window at its old gutter,
  // which is the "without a restart" half of the criterion.
  EXPECT_EQ(live->PaneBoundsForTesting(0).y(), 18)
      << "the open window did not take the wider border";

  appearance.border_width = 4;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(live->ActivePaneForTesting()->parent());
  EXPECT_EQ(live->PaneBoundsForTesting(0).y(), 4)
      << "the open window did not come back down to the minimum";
}

// The other direction. Without this, a branch that pushed the gutter on every
// appearance push would pass the test above and never be caught.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       ASaveCarryingNoBorderWidthLeavesTheGutterAlone) {
  AtriumBrowserWindow* live = window();
  ASSERT_TRUE(live);

  WindowAppearance appearance;
  appearance.border_width = 14;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  views::test::RunScheduledLayout(live->ActivePaneForTesting()->parent());
  ASSERT_EQ(live->PaneBoundsForTesting(0).y(), 14) << "setup did not take";

  // An old settings.json has no borderWidth at all, and WindowAppearance
  // carries nullopt for it. The chosen width has to survive that.
  WindowAppearance without;
  without.opaque_surround = true;
  ASSERT_FALSE(without.border_width.has_value());
  AtriumBrowserWindow::SetWindowAppearance(without);
  views::test::RunScheduledLayout(live->ActivePaneForTesting()->parent());

  // The presence half. On its own, "the gutter did not move" passes just as
  // well when the broadcast never ran at all, so prove it did: the new
  // appearance is the stored one, and it really does carry no width.
  EXPECT_EQ(AtriumBrowserWindow::CurrentAppearance().opaque_surround,
            std::make_optional(true))
      << "the broadcast never landed, so the check below proves nothing";
  EXPECT_FALSE(
      AtriumBrowserWindow::CurrentAppearance().border_width.has_value());

  EXPECT_EQ(live->PaneBoundsForTesting(0).y(), 14)
      << "a save carrying no border width reset the gutter";
}

// Nothing re-derives these tiles' radii on the way
// out of fullscreen, so a width changed while immersive used to strand them at
// the immersive 0 and the card came back square.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       AWidthChangedDuringFullscreenSurvivesTheExit) {
  AtriumBrowserWindow* live = window();
  ASSERT_TRUE(live);
  AtriumPaneView* pane = live->ActivePaneForTesting();
  ASSERT_TRUE(pane);
  ASSERT_TRUE(pane->empty_card() && pane->empty_card()->layer());

  // Not SetImmersiveFullscreen: its async NSWindow transition was still
  // running at teardown, which the run log called a zombie window. This is
  // the seam AtriumRootView drives, and where the defect was.
  auto* container = static_cast<AtriumPaneContainerView*>(pane->parent());
  ASSERT_TRUE(container);
  container->SetImmersive(true);
  WindowAppearance appearance;
  appearance.border_width = 16;
  AtriumBrowserWindow::SetWindowAppearance(appearance);
  container->SetImmersive(false);
  views::test::RunScheduledLayout(container);

  // 26 - 16 with the concentric floor at 6, which 10 clears.
  const float expected = ConcentricCornerRadius(AtriumWindowCornerRadius(), 16);
  EXPECT_FLOAT_EQ(pane->empty_card()->layer()->rounded_corner_radii().upper_left(),
                  expected)
      << "the card came out of fullscreen at the wrong corner";
  EXPECT_GT(expected, 0) << "a zero radius here would pass the check above "
                            "for the wrong reason";
}

// The unit test asks this of the shipping radius. This asks the
// value actually selected at runtime, including the diagnostic override.
IN_PROC_BROWSER_TEST_F(AtriumNativeChromeOverlaysBrowserTest,
                       TheGutterStaysConcentricAtEveryOfferedWidth) {
  AtriumBrowserWindow* live = window();
  ASSERT_TRUE(live);
  AtriumPaneView* pane = live->ActivePaneForTesting();
  ASSERT_TRUE(pane && pane->empty_card() && pane->empty_card()->layer());

  // Asked through the clamp rather than written out, so widening the range in
  // the store brings the new ends here instead of leaving this testing the old
  // ones.
  const std::optional<int> narrowest =
      WindowAppearanceFromSettings(R"({"borderWidth":0})").border_width;
  const std::optional<int> widest =
      WindowAppearanceFromSettings(R"({"borderWidth":9999})").border_width;
  ASSERT_TRUE(narrowest.has_value() && widest.has_value());

  const float outer = AtriumWindowCornerRadius();
  ASSERT_GT(outer, 0) << "no window corner to be concentric with";

  for (int inset : {*narrowest, 10, *widest}) {
    WindowAppearance appearance;
    appearance.border_width = inset;
    AtriumBrowserWindow::SetWindowAppearance(appearance);
    views::test::RunScheduledLayout(pane->parent());

    // Not ConcentricCornerRadius(outer, inset) as the expected: that is the
    // code under test, and it would agree with itself even if it pinched.
    EXPECT_FLOAT_EQ(
        pane->empty_card()->layer()->rounded_corner_radii().upper_left(),
        outer - inset)
        << "the gutter pinches at the corner at a border width of " << inset;
  }
}

}  // namespace
}  // namespace atrium
