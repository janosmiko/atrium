// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_web_card_loading_fill_browsertest.cc

#include <memory>
#include <optional>

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/views_test_utils.h"

// The page's native view shows nothing until its first frame, so a loading
// tab let the window backdrop through the card.
namespace {

constexpr char kHeldPath[] = "/held.html";

class AtriumWebCardLoadingFillBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    held_ = std::make_unique<net::test_server::ControllableHttpResponse>(
        embedded_test_server(), kHeldPath);
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // The request is out and unanswered, so the page has painted nothing.
  content::WebContents* OpenStalledTab() {
    content::WebContents* tab = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL(kHeldPath), -1,
        /*foreground=*/true);
    held_->WaitForRequest();
    return tab;
  }

  static std::optional<SkColor> CardFill(content::WebContents* tab) {
    content::RenderWidgetHostView* view = tab->GetRenderWidgetHostView();
    return view ? view->GetBackgroundColor() : std::nullopt;
  }

  std::unique_ptr<net::test_server::ControllableHttpResponse> held_;
};

IN_PROC_BROWSER_TEST_F(AtriumWebCardLoadingFillBrowserTest,
                       ALoadingTabFillsTheCardWhiteInLightMode) {
  ui::MockOsSettingsProvider os_settings;
  content::WebContents* tab = OpenStalledTab();
  ASSERT_TRUE(tab);

  const std::optional<SkColor> fill = CardFill(tab);
  ASSERT_TRUE(fill.has_value())
      << "the backdrop shows through the card until the page paints";
  EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(*fill));
  // Near-white rather than exactly white: the id resolves through a Material
  // surface tone, which is off-white on some keys.
  EXPECT_GE(SkColorGetR(*fill), 0xF0u);
  EXPECT_GE(SkColorGetG(*fill), 0xF0u);
  EXPECT_GE(SkColorGetB(*fill), 0xF0u);
}

IN_PROC_BROWSER_TEST_F(AtriumWebCardLoadingFillBrowserTest,
                       ALoadingTabFillsTheCardDarkInDarkMode) {
  ui::MockOsSettingsProvider os_settings;
  os_settings.SetPreferredColorScheme(
      ui::NativeTheme::PreferredColorScheme::kDark);
  content::WebContents* tab = OpenStalledTab();
  ASSERT_TRUE(tab);

  const std::optional<SkColor> fill = CardFill(tab);
  ASSERT_TRUE(fill.has_value())
      << "the backdrop shows through the card until the page paints";
  EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(*fill));
  EXPECT_TRUE(color_utils::IsDark(*fill)) << "a light fill in dark mode";
}

IN_PROC_BROWSER_TEST_F(AtriumWebCardLoadingFillBrowserTest,
                       ThePagesOwnBackgroundReplacesTheFill) {
  ui::MockOsSettingsProvider os_settings;
  os_settings.SetPreferredColorScheme(
      ui::NativeTheme::PreferredColorScheme::kDark);
  content::WebContents* tab = OpenStalledTab();
  ASSERT_TRUE(tab);
  ASSERT_TRUE(CardFill(tab).has_value());

  held_->Send(net::HTTP_OK, "text/html",
              "<body style=\"background:#ff0000\">held</body>");
  held_->Done();
  ASSERT_TRUE(content::WaitForLoadStop(tab));

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return CardFill(tab) == std::make_optional(SK_ColorRED); }))
      << "the loading fill stayed over the page's own background";
}

// Immersive squares the card off without resizing the pane, so the fill hears
// about the new radius only where the radius is pushed.
IN_PROC_BROWSER_TEST_F(AtriumWebCardLoadingFillBrowserTest,
                       TheFillClipsWithTheCardWhenOnlyTheRadiusChanges) {
  content::WebContents* tab = OpenStalledTab();
  ASSERT_TRUE(tab);
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  views::WebView* pane = window->ActivePaneForTesting()->web_view();
  ASSERT_TRUE(pane->layer());
  ASSERT_TRUE(pane->holder()->layer());
  ASSERT_FALSE(pane->layer()->rounded_corner_radii().IsEmpty())
      << "the card is not rounded to begin with, so this proves nothing";

  window->ActivePaneForTesting()->SetImmersive(true);
  views::test::RunScheduledLayout(window->ActivePaneForTesting());
  EXPECT_EQ(pane->holder()->layer()->rounded_corner_radii(),
            pane->layer()->rounded_corner_radii())
      << "the fill kept the rounded corners the card no longer has";

  window->ActivePaneForTesting()->SetImmersive(false);
  views::test::RunScheduledLayout(window->ActivePaneForTesting());
  EXPECT_EQ(pane->holder()->layer()->rounded_corner_radii(),
            pane->layer()->rounded_corner_radii());
}

}  // namespace
