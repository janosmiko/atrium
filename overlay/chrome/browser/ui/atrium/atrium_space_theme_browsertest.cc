// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_environment_variable_override.h"
#include "base/scoped_observation.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"

namespace {

constexpr char kThemedSpaceId[] = "work";
constexpr char kUnthemedSpaceId[] = "plain";

constexpr char kSpacesJson[] =
    R"({"spaces":[)"
    R"({"id":"work","name":"Work","emoji":"W","tint":"oceanBlue",)"
    R"("profileID":"Default","order":0,"pinnedTabs":[],"tabs":[]},)"
    R"({"id":"plain","name":"Plain","emoji":"P","tint":"#797281",)"
    R"("profileID":"Default","order":1,"pinnedTabs":[],"tabs":[]},)"
    R"({"id":"ambient","name":"Ambient","emoji":"A","tint":"oceanBlue",)"
    R"("profileID":"Default","order":2,"pinnedTabs":[],"tabs":[]})"
    R"(],"appState":{"schemaVersion":1,"sidebarWidth":220,)"
    R"("activeSpaceID":"work"}})";

std::string ThemeSpecJson(const std::string& name,
                          const std::string& color,
                          bool ambient_enabled) {
  return R"({"name":")" + name + R"(","stops":[)" +
        R"({"x":0,"y":0,"color":")" + color + R"(","alpha":1},)" +
        R"({"x":1,"y":1,"color":"#000000","alpha":1}],)" +
        R"("opacity":1,"effects":{"grain":0.2,"vignette":0,"frost":0,)"
        R"("warmth":0},"darkAdaptation":0,"nightShiftEnabled":false,)"
        R"("ambientEnabled":)" +
        (ambient_enabled ? "true" : "false") + R"(,"version":1})";
}

std::string ThemesFileJson() {
  return R"({"schemaVersion":1,"themes":{"work":)" +
        ThemeSpecJson("Sunset", "#FF8800", /*ambient_enabled=*/false) +
        R"(,"ambient":)" +
        ThemeSpecJson("Glow", "#00CCFF", /*ambient_enabled=*/true) + "}}";
}

// The Theme Studio's own edit, never written to themes.json - PreviewTheme
// must paint it without touching the stored spec ApplySpaceTheme later
// restores.
std::string PreviewSpecJson() {
  return ThemeSpecJson("Forest", "#00FF88", /*ambient_enabled=*/false);
}

// What the page sends: [spaceId, spec], or [spaceId, null] for "no theme".
base::ListValue ThemeArgs(const std::string& space_id,
                          const std::string* spec_json) {
  base::ListValue args;
  args.Append(space_id);
  if (!spec_json) {
    args.Append(base::Value());
    return args;
  }
  std::optional<base::Value> spec =
      base::JSONReader::Read(*spec_json, base::JSON_PARSE_RFC);
  CHECK(spec) << "the fixture spec is not valid JSON";
  args.Append(std::move(*spec));
  return args;
}

// BrowserController has no public accessor for its own Browser*, so a new
// slot's window is only reachable through the creation notification.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// The settings window is one per process and lives as long as its widget, so
// a test that opens one has to close it or the next one reuses it.
class ScopedSettingsWindow {
 public:
  explicit ScopedSettingsWindow(const std::string& pane) {
    atrium::AtriumSettingsWindow::Show(pane);
  }
  ~ScopedSettingsWindow() { atrium::AtriumSettingsWindow::CloseForTesting(); }

  atrium::AtriumSettingsWindow* get() {
    return atrium::AtriumSettingsWindow::ForTesting();
  }
};

}  // namespace

// Mirrors AtriumBrowserWindowBrowserTest's SetUp: a browser_test's Browser
// and AtriumBrowserWindow cannot be built outside a real launch, so the
// fixture is seeded before the launch rather than opened mid-test.
class AtriumSpaceThemeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath spaces_path =
        temp_dir_.GetPath().AppendASCII("spaces.json");
    // Sibling of spaces_path - cache::open() derives it with
    // path.with_file_name("themes.json"), never a second env var.
    const base::FilePath themes_path =
        temp_dir_.GetPath().AppendASCII("themes.json");
    ASSERT_TRUE(base::WriteFile(spaces_path, kSpacesJson));
    ASSERT_TRUE(base::WriteFile(themes_path, ThemesFileJson()));
    env_override_ = std::make_unique<base::ScopedEnvironmentVariableOverride>(
        "ATRIUM_SPACES_FILE", spaces_path.value());
    // atrium_core's cache is a process-global singleton keyed on the path it
    // first opened - without this, a second test in this binary would keep
    // reading whatever the previous test opened.
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

// The fixture's startup space is "work", which holds a theme. Nothing in this
// test switches space, so the only thing that can have painted it is the
// window's own first-presenter path.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       AWindowPaintsItsStartupSpacesThemeWithoutASpaceSwitch) {
  ASSERT_TRUE(atrium_core_is_open());
  ASSERT_EQ(0, atrium_core_active_space_index());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  EXPECT_TRUE(window->has_theme_for_testing())
      << "the window opened on the flat tint - a relaunch and a session "
         "restore both land here";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       ASecondWindowOnTheSameSpaceIsThemedToo) {
  ASSERT_TRUE(atrium_core_is_open());
  ASSERT_EQ(0, atrium_core_active_space_index());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation(&capture);
  observation.Observe(GlobalBrowserCollection::GetInstance());
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false))
      << "CreateWindowSlot returned nullptr";
  observation.Reset();
  ASSERT_TRUE(capture.captured_);

  Browser* slot_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(slot_browser);
  auto* second =
      static_cast<atrium::AtriumBrowserWindow*>(slot_browser->window());
  ASSERT_TRUE(second);
  EXPECT_TRUE(second->has_theme_for_testing())
      << "a second window on a themed space opened flat";
}

// SetEdgeTint used to swap the root's painter for the ambilight one whatever
// was on it, and has_theme_ stayed true, so nothing reinstalled the theme's.
// The startup paint made the settings read's glow clear land after a theme.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       AClearedGlowLeavesTheThemesPainterOnTheRoot) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->has_theme_for_testing());

  // What SetChromePageTint(false) does on the settings read, and what the
  // last tab closing does. Neither is about the theme.
  window->SetEdgeTint(atrium::EdgeTint());
  window->RestoreSpaceTint();

  // Read off the painted root, not the bitmap - the bitmap was always
  // updated, the painter is what went missing.
  const SkColor before = window->root_background_pixel_for_testing();
  controller->PreviewThemeForTesting(kThemedSpaceId, PreviewSpecJson());
  EXPECT_NE(before, window->root_background_pixel_for_testing())
      << "the theme's painter is off the root, so an edit paints nothing";
}

// The theme's painter drew the picture and nothing else, so a page's glow
// crossfaded into edge_tint_ and never reached the screen on a themed space.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       AThemedWindowPaintsThePagesGlowOverThePicture) {
  ASSERT_TRUE(atrium_core_is_open());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->has_theme_for_testing());

  window->SetEdgeTint(atrium::EdgeTint());
  window->RestoreSpaceTint();
  const SkColor bare = window->root_glow_pixel_for_testing();

  atrium::EdgeTint glow;
  glow.SetEdge(atrium::Edge::kLeft, SK_ColorRED);
  glow.SetEdge(atrium::Edge::kRight, SK_ColorBLUE);
  glow.SetEdge(atrium::Edge::kTop, SK_ColorRED);
  glow.SetEdge(atrium::Edge::kBottom, SK_ColorBLUE);
  window->SetEdgeTint(glow);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->root_glow_pixel_for_testing() != bare;
  })) << "the glow never painted next to the page card on a themed space";
  EXPECT_TRUE(window->has_theme_for_testing())
      << "the glow took the theme down with it";
  EXPECT_GT(SkColorGetR(window->root_glow_pixel_for_testing()),
            SkColorGetB(window->root_glow_pixel_for_testing()))
      << "the pixel left of the card does not carry the left edge's red";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       ASaveSurvivesTheSettingsWindowClosing) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->has_theme_for_testing());

  // The startup sequence the scenario runs into, before any studio edit.
  window->SetEdgeTint(atrium::EdgeTint());
  window->RestoreSpaceTint();
  const SkColor stored = window->root_background_pixel_for_testing();

  ScopedSettingsWindow settings("spaces");
  ASSERT_TRUE(settings.get()) << "the settings window did not open";

  const std::string spec = PreviewSpecJson();
  settings.get()->OnSurfaceMessage("settingsThemePreview",
                                   ThemeArgs(kThemedSpaceId, &spec));
  settings.get()->OnSurfaceMessage("settingsThemeChanged",
                                   ThemeArgs(kThemedSpaceId, &spec));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->root_background_pixel_for_testing() != stored;
  })) << "the saved theme never reached the window's painted backdrop";
  const SkColor saved = window->root_background_pixel_for_testing();

  // Cmd+W on the settings window. The commit cleared preview_window_, so this
  // must not put the pre-edit theme back.
  atrium::AtriumSettingsWindow::CloseForTesting();
  window->CatchUpSidebarForNewPresenter();  // the re-activation catch-up
  EXPECT_EQ(saved, window->root_background_pixel_for_testing())
      << "closing the settings window undid the save";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       SelectSpacePaintsAThemeAndClearsItForAnUnthemedSpace) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // The window opens on "work" already themed, so leaving for an unthemed
  // space is the crossfading clear the third leg below waits on too.
  ASSERT_TRUE(controller->SelectSpace(1));  // "plain" - no theme
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->has_theme_for_testing(); }));

  ASSERT_TRUE(controller->SelectSpace(0));  // "work" - the two-stop theme
  EXPECT_TRUE(window->has_theme_for_testing());

  // The 300ms SelectSpace push crossfades the clear over the flat tint
  // (AtriumBrowserWindow::SetBackdropTheme), so has_theme_ only drops once
  // that fade finishes - real time, not mocked, in a browser_test.
  ASSERT_TRUE(controller->SelectSpace(1));  // back to "plain"
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !window->has_theme_for_testing(); }));
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       PreviewThemeChangesTheBitmapAndApplySpaceThemeRestores) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  ASSERT_TRUE(controller->SelectSpace(1));  // off "work" first - it starts active
  ASSERT_TRUE(controller->SelectSpace(0));
  ASSERT_TRUE(window->has_theme_for_testing());
  const SkBitmap stored = window->theme_bitmap_for_testing();
  ASSERT_FALSE(stored.drawsNothing());
  const SkColor stored_pixel = stored.getColor(0, 0);

  controller->PreviewThemeForTesting(kThemedSpaceId, PreviewSpecJson());
  const SkBitmap previewed = window->theme_bitmap_for_testing();
  ASSERT_FALSE(previewed.drawsNothing());
  EXPECT_NE(stored_pixel, previewed.getColor(0, 0))
      << "PreviewTheme did not change the painted bitmap";

  controller->ApplySpaceThemeForTesting(window, kThemedSpaceId, 0);
  const SkBitmap restored = window->theme_bitmap_for_testing();
  ASSERT_FALSE(restored.drawsNothing());
  EXPECT_EQ(stored_pixel, restored.getColor(0, 0))
      << "ApplySpaceTheme did not restore the stored theme's pixel";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       SettingsThemeChangedStoresTheSpecAndPaintsTheWindow) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // "plain" holds no theme, so a spec that turns up in the store afterwards
  // is unambiguously the one this test sent.
  ASSERT_TRUE(controller->SelectSpace(1));
  ASSERT_EQ(nullptr, atrium_core_space_theme(kUnthemedSpaceId));
  // The window opens themed on "work", so the wait at the end proves nothing
  // until that theme has faded out.
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !window->has_theme_for_testing(); }));

  ScopedSettingsWindow settings("spaces");
  ASSERT_TRUE(settings.get()) << "the settings window did not open";

  const std::string spec = PreviewSpecJson();
  settings.get()->OnSurfaceMessage("settingsThemeChanged",
                                   ThemeArgs(kUnthemedSpaceId, &spec));

  char* stored = atrium_core_space_theme(kUnthemedSpaceId);
  ASSERT_TRUE(stored) << "settingsThemeChanged never reached the themes store";
  const std::string stored_json(stored);
  atrium_core_string_free(stored);
  EXPECT_NE(stored_json.find("#00FF88"), std::string::npos)
      << "the store holds a different spec: " << stored_json;
  // The paint is chained behind the write now, so it lands a beat later.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window->has_theme_for_testing(); }))
      << "the saved theme was not applied to the window behind";
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       ACommitForANonActiveSpaceMovesTheWindowToIt) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // The window sits on "work" - the fixture's active space - while the studio
  // edits "plain". The backdrop belongs to the active space, so saving without
  // moving would paint "plain"'s theme over "work" and leave it there.
  ASSERT_EQ(0, atrium_core_active_space_index());
  ASSERT_TRUE(window->has_theme_for_testing());
  const SkColor work_stored = window->theme_bitmap_for_testing().getColor(0, 0);

  ScopedSettingsWindow settings("spaces");
  ASSERT_TRUE(settings.get()) << "the settings window did not open";

  const std::string spec = PreviewSpecJson();
  settings.get()->OnSurfaceMessage("settingsThemeChanged",
                                   ThemeArgs(kUnthemedSpaceId, &spec));
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return atrium_core_active_space_index() == 1; }))
      << "the window stayed on the space the edit was not about";
  // Themed already on arrival, so only a CHANGED pixel proves the save landed.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window->has_theme_for_testing() &&
           window->theme_bitmap_for_testing().getColor(0, 0) != work_stored;
  }));
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       APreviewForANonActiveSpaceMovesTheWindowAndReverts) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // What "work" looks like when its own stored theme is on screen, so the
  // revert below has something exact to come back to.
  ASSERT_TRUE(controller->SelectSpace(1));  // off "work" - it starts active
  ASSERT_TRUE(controller->SelectSpace(0));
  ASSERT_TRUE(window->has_theme_for_testing());
  const SkColor work_stored = window->theme_bitmap_for_testing().getColor(0, 0);

  // Now on "plain", which has no theme, while the studio edits "work".
  ASSERT_TRUE(controller->SelectSpace(1));
  ASSERT_EQ(1, atrium_core_active_space_index());

  ScopedSettingsWindow settings("spaces");
  ASSERT_TRUE(settings.get()) << "the settings window did not open";

  const std::string spec = PreviewSpecJson();
  settings.get()->OnSurfaceMessage("settingsThemePreview",
                                   ThemeArgs(kThemedSpaceId, &spec));
  EXPECT_EQ(0, atrium_core_active_space_index())
      << "the preview painted onto a space the window was not showing";
  ASSERT_TRUE(window->has_theme_for_testing());
  EXPECT_NE(work_stored, window->theme_bitmap_for_testing().getColor(0, 0))
      << "the preview did not reach the window";

  // What closing the studio unsaved sends. The store never moved, so the
  // window comes back to the theme "work" already holds.
  settings.get()->OnSurfaceMessage("settingsThemePreview",
                                   ThemeArgs(kThemedSpaceId, nullptr));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return work_stored == window->theme_bitmap_for_testing().getColor(0, 0);
  })) << "an undone preview left the window painted with an unsaved spec";

  char* stored = atrium_core_space_theme(kThemedSpaceId);
  ASSERT_TRUE(stored);
  const std::string stored_json(stored);
  atrium_core_string_free(stored);
  EXPECT_NE(stored_json.find("#FF8800"), std::string::npos)
      << "a preview must never write to the themes store: " << stored_json;
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       AThemeMessageForAnUnknownSpaceTouchesNothing) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  ASSERT_TRUE(controller->SelectSpace(1));  // off "work" first - it starts active
  ASSERT_TRUE(controller->SelectSpace(0));
  ASSERT_TRUE(window->has_theme_for_testing());
  const SkColor before = window->theme_bitmap_for_testing().getColor(0, 0);

  ScopedSettingsWindow settings("spaces");
  ASSERT_TRUE(settings.get()) << "the settings window did not open";

  // A NULL from atrium_core_space_theme means "no theme", never "no such
  // space", so an id the core does not know has to be refused by name.
  settings.get()->OnSurfaceMessage("settingsThemePreview",
                                   ThemeArgs("bogus", nullptr));
  const std::string spec = PreviewSpecJson();
  settings.get()->OnSurfaceMessage("settingsThemePreview",
                                   ThemeArgs("bogus", &spec));
  settings.get()->OnSurfaceMessage("settingsThemeChanged",
                                   ThemeArgs("bogus", &spec));

  EXPECT_EQ(0, atrium_core_active_space_index());
  EXPECT_TRUE(window->has_theme_for_testing());
  EXPECT_EQ(before, window->theme_bitmap_for_testing().getColor(0, 0))
      << "an unknown space id repainted the window";
}

// The sidebar's theme popover sends the settings window's two messages to the
// BROWSER window's router, where the space id arrives straight from a renderer.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       ASidebarThemeEditForAnUnknownSpaceWritesNothing) {
  ASSERT_TRUE(atrium_core_is_open());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_EQ(nullptr, atrium_core_space_theme(kUnthemedSpaceId));

  const std::string spec = PreviewSpecJson();
  window->OnSurfaceMessage("settingsThemeChanged",
                           ThemeArgs("no-such-space", &spec));
  window->OnSurfaceMessage("settingsThemePreview",
                           ThemeArgs("no-such-space", &spec));

  EXPECT_EQ(nullptr, atrium_core_space_theme("no-such-space"));
  EXPECT_EQ(nullptr, atrium_core_space_theme(kUnthemedSpaceId))
      << "an unknown id reached the themes store";
  // The same edit on a real id still lands, so the guard refuses the id rather
  // than the whole route.
  window->OnSurfaceMessage("settingsThemeChanged",
                           ThemeArgs(kUnthemedSpaceId, &spec));
  char* stored = atrium_core_space_theme(kUnthemedSpaceId);
  ASSERT_TRUE(stored) << "the guard refused a known space id too";
  atrium_core_string_free(stored);
}

IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       TheAnimatorRunsOnlyWhileItsWindowIsVisible) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  ASSERT_TRUE(controller->SelectSpace(2));  // "ambient" - ambientEnabled:true
  EXPECT_TRUE(window->has_theme_for_testing());
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return controller->theme_animator_running_for_testing(); }))
      << "the animator never started for a visible, ambient-enabled theme";

  window->Hide();
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return !controller->theme_animator_running_for_testing(); }))
      << "the animator kept running after the window hid";

  window->Show();
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return controller->theme_animator_running_for_testing(); }))
      << "the animator did not resume once the window showed again";
}

// Settings and the hotkeys can reach one window through two controllers. Each
// kept its own ambient timer on it, and the two pictures took turns (the
// recorded blink).
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       ASecondControllerThemingTheWindowStopsTheFirstsAnimator) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* first =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation(&capture);
  observation.Observe(GlobalBrowserCollection::GetInstance());
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false));
  observation.Reset();
  ASSERT_TRUE(capture.captured_);
  atrium::BrowserController* second =
      atrium::BrowserController::ControllerForBrowser(
          capture.captured_->GetBrowserForMigrationOnly());
  ASSERT_TRUE(second);
  ASSERT_NE(first, second);

  first->ApplySpaceThemeForTesting(window, "ambient", /*transition_ms=*/0);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return first->theme_animator_running_for_testing(); }));

  second->ApplySpaceThemeForTesting(window, "ambient", /*transition_ms=*/0);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return second->theme_animator_running_for_testing(); }));
  EXPECT_FALSE(first->theme_animator_running_for_testing())
      << "two animators paint one window";
}

// Coverage is the painted alpha: the more of the translucent backdrop the
// pictures cover, the less of the desktop shows through.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       AThemeCrossfadeNeverCoversMoreThanEitherEnd) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->SetGlassBackdropAlpha(30);

  const gfx::Size size = window->GetBounds().size();
  const auto coverage = [&]() {
    gfx::Canvas canvas(size, 1.f, /*is_opaque=*/false);
    window->PaintThemeBackdrop(&canvas);
    return SkColorGetA(
        canvas.GetBitmap().getColor(size.width() / 2, size.height() / 2));
  };
  const auto settle = [&]() {
    const base::TimeTicks until = base::TimeTicks::Now() + base::Seconds(1);
    ASSERT_TRUE(
        base::test::RunUntil([&]() { return base::TimeTicks::Now() >= until; }));
  };

  controller->ApplySpaceThemeForTesting(window, kThemedSpaceId, 0);
  ASSERT_TRUE(window->has_theme_for_testing());
  const SkAlpha stored_end = coverage();
  controller->PreviewThemeForTesting(kThemedSpaceId, PreviewSpecJson());
  settle();
  const SkAlpha preview_end = coverage();

  controller->ApplySpaceThemeForTesting(window, kThemedSpaceId, 300);
  SkAlpha peak = 0;
  const base::TimeTicks until = base::TimeTicks::Now() + base::Seconds(1);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    peak = std::max<SkAlpha>(peak, coverage());
    return base::TimeTicks::Now() >= until;
  }));
  EXPECT_LE(peak, std::max(stored_end, preview_end) + 2)
      << "the crossfade stacked both pictures: stored " << int{stored_end}
      << ", preview " << int{preview_end} << ", peak " << int{peak};
}

// A flat space and a themed one with different tints: the wash under the
// picture has to travel with the picture's fade, not jump after it.
IN_PROC_BROWSER_TEST_F(AtriumSpaceThemeBrowserTest,
                       TheWashCrossfadesUnderAThemeThatFadesIn) {
  ASSERT_TRUE(atrium_core_is_open());
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  ASSERT_TRUE(controller->SelectSpace(1));  // "plain"
  base::TimeTicks until = base::TimeTicks::Now() + base::Seconds(1);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return base::TimeTicks::Now() >= until; }));
  ASSERT_FALSE(window->has_theme_for_testing());
  const SkColor flat_wash = window->backdrop_tint_for_testing();

  ASSERT_TRUE(controller->SelectSpace(0));  // "work"
  ASSERT_TRUE(window->has_theme_for_testing());
  std::vector<SkColor> washes;
  until = base::TimeTicks::Now() + base::Seconds(1);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    const SkColor wash = window->backdrop_tint_for_testing();
    if (washes.empty() || washes.back() != wash) {
      washes.push_back(wash);
    }
    return base::TimeTicks::Now() >= until;
  }));
  EXPECT_NE(flat_wash, washes.back())
      << "the themed space kept the flat space's wash";
  EXPECT_GT(washes.size(), 3u)
      << "the wash jumped to the themed space's tint instead of fading";
}
