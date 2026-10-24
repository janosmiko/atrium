// Copyright 2026 The Atrium Authors

#include <optional>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h"
#include "chrome/browser/ui/atrium/atrium_toast_overlay_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_zoom.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// the hover URL card used to anchor to the WINDOW's bottom leading
// corner, which includes the sidebar column - so it landed over the empty
// margin beside the page instead of over the page itself. These assert the
// card against AtriumBrowserWindow::GetWebContentBoundsInScreen() (the pane),
// the same source a fix that only reads a fixed leading inset would still
// fail.
namespace atrium {
namespace {

constexpr char kHoverUrlOverlayName[] = "AtriumHoverUrlOverlay";
constexpr char kConfirmationOverlayName[] = "AtriumToastOverlay";

class AtriumToastOverlayBrowserTest : public InProcessBrowserTest {
 protected:
  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // Sized well beyond any card or the sidebar's own range, so a resize or a
  // sidebar-width change below always leaves plenty of pane to assert against.
  void SetWindowBounds(int x, int y, int width, int height) {
    window()->widget()->SetBounds(gfx::Rect(x, y, width, height));
  }

  std::optional<gfx::Rect> OverlayWidgetBounds(const std::string& name) {
    for (views::Widget* child : views::Widget::GetAllChildWidgets(
             window()->widget()->GetNativeView())) {
      if (child->GetName() == name && child->IsVisible()) {
        return child->GetWindowBoundsInScreen();
      }
    }
    return std::nullopt;
  }

  std::optional<gfx::Rect> HoverCardWidgetBounds() {
    return OverlayWidgetBounds(kHoverUrlOverlayName);
  }

  std::optional<gfx::Rect> ConfirmationWidgetBounds() {
    return OverlayWidgetBounds(kConfirmationOverlayName);
  }

  // The toggle is a keydown dispatched into the panel document, so the pane
  // only narrows a few round trips later. Waiting on the pane rather than on
  // ai_panel_open_ is what makes the bounds below the post-layout ones.
  [[nodiscard]] bool OpenAiPanel() {
    content::WebContents* panel = window()->AiPanelContentsForTesting();
    if (!panel || !content::WaitForLoadStop(panel)) {
      return false;
    }
    const int before = window()->GetWebContentBoundsInScreen().right();
    AtriumBrowserWindow::SetAiEnabled(true);
    window()->ToggleAiPanel();
    return base::test::RunUntil([&]() {
      return window()->AiPanelOpenForTesting() &&
             window()->GetWebContentBoundsInScreen().right() < before;
    });
  }

  // The same call a copy makes. CopyCurrentPageLink itself needs a page in the
  // pane, and this suite runs with no space fixture, so the pane is empty.
  void RaiseConfirmation() {
    window()->ShowTransientConfirmationForTesting(u"Link copied");
  }

  gfx::Rect WaitForConfirmation() {
    std::optional<gfx::Rect> bounds;
    EXPECT_TRUE(base::test::RunUntil([&]() {
      bounds = ConfirmationWidgetBounds();
      return bounds.has_value();
    }));
    return bounds.value_or(gfx::Rect());
  }

  void ShowHoverCard(const std::string& url) {
    window()->SetStatusBubbleURL(window()->CurrentlyDisplayedWebContents(),
                                 GURL(url));
  }

  gfx::Rect WaitForHoverCard() {
    std::optional<gfx::Rect> bounds;
    EXPECT_TRUE(base::test::RunUntil([&]() {
      bounds = HoverCardWidgetBounds();
      return bounds.has_value();
    }));
    return bounds.value_or(gfx::Rect());
  }
};

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       HoverCardAnchorsToThePaneNotTheWindow) {
  SetWindowBounds(50, 50, 1200, 800);
  ShowHoverCard("https://example.com/");
  const gfx::Rect card = WaitForHoverCard();

  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  const gfx::Rect win = window()->widget()->GetWindowBoundsInScreen();
  ASSERT_GT(pane.x(), win.x())
      << "the sidebar must actually be reserving width in this window, or "
         "the pane/window corners coincide and this test proves nothing";

  EXPECT_TRUE(pane.Contains(card))
      << "card " << card.ToString() << " is not inside pane "
      << pane.ToString() << " (window was " << win.ToString() << ")";
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       HoverCardStaysAnchoredAfterWindowResize) {
  SetWindowBounds(50, 50, 1200, 800);
  ShowHoverCard("https://example.com/");
  WaitForHoverCard();

  SetWindowBounds(80, 80, 1400, 900);
  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  gfx::Rect card;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = HoverCardWidgetBounds();
    if (!bounds) {
      return false;
    }
    card = *bounds;
    return pane.Contains(card);
  })) << "card never settled inside the resized pane "
      << pane.ToString();
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       HoverCardStaysAnchoredAfterSidebarWidthChange) {
  SetWindowBounds(50, 50, 1200, 800);
  ShowHoverCard("https://example.com/");
  const gfx::Rect before = WaitForHoverCard();

  const int new_width = window()->SidebarWidthForTesting() + 100;
  window()->SetSidebarWidthLive(new_width);

  gfx::Rect after;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = HoverCardWidgetBounds();
    if (!bounds) {
      return false;
    }
    after = *bounds;
    return after.x() != before.x();
  })) << "card never moved off its pre-resize x after the sidebar widened";

  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  EXPECT_TRUE(pane.Contains(after))
      << "card " << after.ToString() << " is not inside the widened pane "
      << pane.ToString();
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       HoverCardFollowsThePaneWhenSidebarFlipsSide) {
  SetWindowBounds(50, 50, 1200, 800);
  ASSERT_FALSE(window()->IsSidebarOnRight());
  ShowHoverCard("https://example.com/");
  const gfx::Rect before = WaitForHoverCard();

  window()->SetSidebarPosition(/*on_right=*/true);

  gfx::Rect after;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = HoverCardWidgetBounds();
    if (!bounds) {
      return false;
    }
    after = *bounds;
    return after.x() != before.x();
  })) << "card never moved off its pre-flip x after the sidebar flipped side";

  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  const gfx::Rect win = window()->widget()->GetWindowBoundsInScreen();
  EXPECT_TRUE(pane.Contains(after))
      << "card " << after.ToString() << " is not inside the flipped pane "
      << pane.ToString();
  // A fix that reads a fixed leading inset (window.x() + inset) would still
  // land here after the flip - only a pane-relative, side-aware x fails on
  // that bug.
  EXPECT_GT(after.x(), win.x() + pane.width() / 2)
      << "card did not move to the trailing (sidebar) side after the flip";
}

// the confirmation corner was the last reader of window bounds. It
// takes the side opposite the sidebar, which is the side the AI panel opens
// on, so an open panel slid under a standing card.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ConfirmationAnchorsToThePaneWithTheAiPanelOpen) {
  SetWindowBounds(50, 50, 1200, 800);
  ASSERT_TRUE(OpenAiPanel());

  const gfx::Rect win = window()->widget()->GetWindowBoundsInScreen();
  gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  ASSERT_LT(pane.right(), win.right())
      << "the AI panel must actually be reserving width on the trailing side, "
         "or the pane and window corners coincide and this test proves nothing";

  RaiseConfirmation();
  gfx::Rect card;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = ConfirmationWidgetBounds();
    if (!bounds) {
      return false;
    }
    card = *bounds;
    pane = window()->GetWebContentBoundsInScreen();
    return pane.Contains(card);
  })) << "card " << card.ToString() << " never settled inside pane "
      << pane.ToString() << " (window was " << win.ToString() << ")";
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ConfirmationFollowsThePaneWhenTheAiPanelOpens) {
  SetWindowBounds(50, 50, 1200, 800);
  ASSERT_FALSE(window()->AiPanelOpenForTesting());
  RaiseConfirmation();
  const gfx::Rect before = WaitForConfirmation();

  ASSERT_TRUE(OpenAiPanel());

  gfx::Rect after;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = ConfirmationWidgetBounds();
    if (!bounds) {
      return false;
    }
    after = *bounds;
    return after.right() != before.right();
  })) << "card never moved off its pre-panel right edge " << before.ToString();

  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  EXPECT_TRUE(pane.Contains(after))
      << "card " << after.ToString() << " is not inside the narrowed pane "
      << pane.ToString();
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ConfirmationFollowsThePaneWhenSidebarFlipsSide) {
  SetWindowBounds(50, 50, 1200, 800);
  ASSERT_FALSE(window()->IsSidebarOnRight());
  RaiseConfirmation();
  const gfx::Rect before = WaitForConfirmation();

  window()->SetSidebarPosition(/*on_right=*/true);

  gfx::Rect after;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = ConfirmationWidgetBounds();
    if (!bounds) {
      return false;
    }
    after = *bounds;
    return after.x() != before.x();
  })) << "card never moved off its pre-flip x after the sidebar flipped side";

  const gfx::Rect pane = window()->GetWebContentBoundsInScreen();
  EXPECT_TRUE(pane.Contains(after))
      << "card " << after.ToString() << " is not inside the flipped pane "
      << pane.ToString();
}

// a flip moves the widget while the window stands still, so the
// view a shadow baked against at Register time no longer holds. The pane keeps
// its width, so PushPaneWidth early-returns and no push replaces it.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ASidebarSideFlipReleasesTheShadowsBakedOnTheOldSide) {
  SetWindowBounds(50, 50, 1200, 800);
  ASSERT_FALSE(window()->IsSidebarOnRight());
  ShowHoverCard("https://example.com/");
  const gfx::Rect before = WaitForHoverCard();

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->OverlayShadowCountForTesting() > 0u; }))
      << "no shadow was ever registered, so a release proves nothing";

  window()->SetSidebarPosition(/*on_right=*/true);

  ASSERT_TRUE(base::test::RunUntil([&]() {
    const std::optional<gfx::Rect> bounds = HoverCardWidgetBounds();
    return bounds && bounds->x() != before.x();
  })) << "the card never left its pre-flip x, so nothing could have gone stale";

  // The layer, not the widget's own mirror of it: a mirror emptied by the
  // release reads clean whether or not the layer let go of anything.
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return window()->OverlayShadowCountForTesting() == 0u; }))
      << "a shadow is still painting where the card sat before the flip";
}

// the widget resizes a round trip after the page lays out, so a
// burst is the only way to read a card against a viewport that disagrees with
// it. The shadow the page reports must still land on the widget it belongs to.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ARapidHoverAndModifierBurstKeepsShadowsOnTheWidget) {
  SetWindowBounds(50, 50, 1200, 800);
  ShowHoverCard("https://example.com/");
  WaitForHoverCard();

  for (int i = 0; i < 6; ++i) {
    ShowHoverCard(i % 2 == 0
                      ? "https://example.com/a/rather/long/path/number/" +
                            base::NumberToString(i)
                      : "https://b.example/s");
    AtriumBrowserWindow::SetSidebarNavModifiers(false, i % 2 == 0, false);
  }

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->OverlayShadowCountForTesting() > 0u; }))
      << "the burst left no shadow at all, so the readings below are empty";

  const gfx::Rect widget = WaitForHoverCard();
  for (const gfx::Rect& shadow :
       window()->HoverUrlWidgetForTesting()->RegisteredShadowRectsForTesting()) {
    EXPECT_TRUE(widget.Contains(shadow))
        << "shadow " << shadow.ToString() << " is outside its own widget "
        << widget.ToString() << ", which puts it on the sidebar";
  }
}

IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       TheRevealedPanelOutranksTheHoverCardBothWaysRound) {
  SetWindowBounds(100, 100, 1400, 900);

  // Card first, panel second - the reported order.
  ShowHoverCard("https://example.com/one");
  WaitForHoverCard();
  views::Widget* card = window()->HoverUrlWidgetForTesting()->widget();
  ASSERT_TRUE(card);
  // The panel is built on the first reveal, and the reveal itself is posted.
  window()->RevealFloatingSidebar(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    AtriumSidebarOverlayWidget* live = window()->SidebarOverlayWidgetForTesting();
    return live && live->IsShowing();
  })) << "the floating sidebar never revealed, so there is nothing to order";
  AtriumSidebarOverlayWidget* panel = window()->SidebarOverlayWidgetForTesting();
  EXPECT_GT(window()->OverlayChildOrderForTesting(panel->WidgetForTesting()),
            window()->OverlayChildOrderForTesting(card))
      << "the panel the user just summoned came up under the hover card";

  // Panel first, card second. A link hovered under a standing panel used to
  // raise its card, because a show puts a child window on top.
  ShowHoverCard("");
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !HoverCardWidgetBounds().has_value(); }));
  ShowHoverCard("https://example.com/two");
  WaitForHoverCard();
  EXPECT_GT(window()->OverlayChildOrderForTesting(panel->WidgetForTesting()),
            window()->OverlayChildOrderForTesting(card))
      << "hovering a link raised its card over the revealed panel";

  // The card is still there and still placed once the panel goes.
  window()->RevealFloatingSidebar(false);
  ASSERT_TRUE(base::test::RunUntil([&]() { return !panel->IsShowing(); }));
  const std::optional<gfx::Rect> after = HoverCardWidgetBounds();
  ASSERT_TRUE(after.has_value()) << "hiding the panel took the card with it";
  EXPECT_TRUE(window()->GetWebContentBoundsInScreen().Contains(after.value()))
      << "the card landed outside the pane at " << after->ToString();
}

// The hover card sat on the link that raised it, so the pointer left the page,
// the hover cleared, the card went, and the two alternated. Both corners are
// asserted: the confirmation's close button still needs the mouse.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       TheHoverCornerDeclinesTheMouseAndTheConfirmationDoesNot) {
  SetWindowBounds(50, 50, 1200, 800);

  ShowHoverCard("https://example.com/");
  WaitForHoverCard();
  ToastOverlayWidget* hover = window()->HoverUrlWidgetForTesting();
  ASSERT_TRUE(hover);
  EXPECT_TRUE(hover->DeclinesMouseForTesting())
      << "the hover card's window still hit-tests, so it takes the pointer off "
         "the link underneath it";

  RaiseConfirmation();
  WaitForConfirmation();
  ToastOverlayWidget* confirmation = window()->ConfirmationWidgetForTesting();
  ASSERT_TRUE(confirmation);
  EXPECT_FALSE(confirmation->DeclinesMouseForTesting())
      << "the confirmation corner stopped taking the mouse, so its close "
         "button can no longer be clicked";
}

// The card fades in over atrium-toast-peekin while its shadow is painted
// outside the document, so one registered solid is an outline around nothing.
// Order is the fix: only an away state set BEFORE Register builds it invisible.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       AnArrivingCardsShadowIsBuiltInvisible) {
  SetWindowBounds(50, 50, 1200, 800);

  ShowHoverCard("https://example.com/");
  WaitForHoverCard();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->OverlayShadowCountForTesting() > 0u; }))
      << "no shadow was ever registered, so there is nothing to have faded";

  EXPECT_TRUE(window()->OverlayShadowStartedAwayForTesting("toast-hover-0"))
      << "the card's shadow layer was built at full strength, so it paints "
         "before the card it belongs to";
}

// Every push used to release all ten slots and register them again. That
// destroys the layer and the fade running on it, so a shadow halfway in
// dropped to nothing and climbed a second time.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ARetargetKeepsTheCardsOwnShadowLayer) {
  SetWindowBounds(50, 50, 1200, 800);

  ShowHoverCard("https://example.com/a-much-longer-path-than-the-second-one");
  const gfx::Rect first = WaitForHoverCard();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window()->OverlayShadowBuildCountForTesting("toast-hover-0") > 0u;
  })) << "no shadow was ever built, so there is nothing to have kept";
  const size_t built =
      window()->OverlayShadowBuildCountForTesting("toast-hover-0");

  // A narrower url re-measures the card without moving the stack's leading
  // corner, so this is a re-register rather than the release a move does.
  ShowHoverCard("https://ex.co/b");
  std::optional<gfx::Rect> second;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    second = HoverCardWidgetBounds();
    return second && second->width() != first.width();
  })) << "the card never re-measured, so nothing re-registered its shadow";
  ASSERT_EQ(first.origin(), second->origin())
      << "the stack moved, which releases its shadows on purpose - this "
         "reading cannot tell that apart from the rebuild it is about";

  EXPECT_EQ(built,
            window()->OverlayShadowBuildCountForTesting("toast-hover-0"))
      << "the shadow layer was rebuilt from nothing, dropping any fade in "
         "flight on it";
}

// A settled stack of `count` cards, with no keyframe still running. The widget
// is sized to the stack, so a card measured mid-entry reports a width the
// corner will not keep - and the corner moving is what releases every slot.
// Children of the stack box only: resolveCardShadow keeps an off-screen
// .toast of its own to read the ring colour from, and it counts otherwise.
constexpr char kWaitForSettledCards[] = R"(
  new Promise((done) => {
    const check = () => {
      const cards = [...document.querySelectorAll('.toasts > .toast')];
      if (cards.length === %d &&
          cards.every((c) => c.getAnimations().length === 0)) {
        done(true);
      } else {
        // setTimeout, not requestAnimationFrame: the widget hides when the
        // stack empties, and frames stop arriving for an occluded surface.
        setTimeout(check, 16);
      }
    };
    check();
  })
)";

// The other half of keeping a layer alive: a slot is index-keyed and the stack
// prepends, so dismissing the newest card hands slot 0 to the one below it. A
// card that inherits the away shift the last occupant left there fades out and
// never comes back.
IN_PROC_BROWSER_TEST_F(AtriumToastOverlayBrowserTest,
                       ASlotThatChangesHandsIsReleasedFirst) {
  SetWindowBounds(50, 50, 1200, 800);

  // One card only to build the surface, then the timeout goes off and that card
  // is dismissed. A card raised on the default five seconds expires partway
  // through the sequence below, which reads as the stack moving.
  RaiseConfirmation();
  WaitForConfirmation();
  content::WebContents* stack =
      window()->ConfirmationWidgetForTesting()->web_contents();
  ASSERT_TRUE(stack);
  ASSERT_TRUE(content::ExecJs(stack, "atriumSetNotificationTimeout('keep')"));
  ASSERT_TRUE(content::ExecJs(stack, "document.querySelector('.ctcl').click()"));
  ASSERT_EQ(true, content::EvalJs(stack, base::StringPrintf(
                                             kWaitForSettledCards, 0)));

  ASSERT_TRUE(content::ExecJs(
      stack, "void atriumConfirmToast('Link copied', '', true)"));
  ASSERT_TRUE(content::ExecJs(
      stack, "void atriumConfirmToast('Link copied', '', true)"));
  ASSERT_EQ(true, content::EvalJs(stack, base::StringPrintf(
                                             kWaitForSettledCards, 2)));
  const gfx::Rect two_cards = ConfirmationWidgetBounds().value_or(gfx::Rect());
  ASSERT_FALSE(two_cards.IsEmpty());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window()->OverlayShadowBuildCountForTesting("toast-confirm-1") > 0u;
  })) << "the older card never reached slot 1, so no slot can change hands";
  const size_t built =
      window()->OverlayShadowBuildCountForTesting("toast-confirm-0");

  // The stack prepends, so the first close button belongs to the newest card,
  // and dismissing that one is what hands slot 0 to the card below it.
  ASSERT_TRUE(content::ExecJs(stack, "document.querySelector('.ctcl').click()"));
  ASSERT_EQ(true, content::EvalJs(stack, base::StringPrintf(
                                             kWaitForSettledCards, 1)));

  // The page settling is not the push landing - the surviving card's rects
  // still have a round trip to make.
  bool corner_moved = false;
  const bool rebuilt = base::test::RunUntil([&]() {
    std::optional<gfx::Rect> bounds = ConfirmationWidgetBounds();
    corner_moved =
        corner_moved || !bounds || bounds->origin() != two_cards.origin();
    return window()->OverlayShadowBuildCountForTesting("toast-confirm-0") >
           built;
  });
  // Both cards carry the same text, so the stack is as wide with one as with
  // two and hangs off the same corner. Read beside the rebuild, not instead of
  // it: a corner that moved releases every slot on purpose, which is the one
  // thing that makes the reading above pass without the code it is about.
  EXPECT_FALSE(corner_moved)
      << "the stack moved, which releases every slot on purpose - this reading "
         "cannot tell that apart from the release it is about";
  EXPECT_TRUE(rebuilt)
      << "slot 0 kept the dismissed card's layer, so the card promoted into it "
         "inherits that card's fade-out and never comes back";
}

class AtriumZoomToastBrowserTest : public AtriumToastOverlayBrowserTest {
 protected:
  content::WebContents* NavigateActiveTab() {
    content::WebContents* tab = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/true);
    if (!tab || !content::WaitForLoadStop(tab)) {
      return nullptr;
    }
    return tab;
  }

  std::string CardText(content::WebContents* stack) {
    return content::EvalJs(stack, "document.querySelector('.toast .ctl') ? "
                                  "document.querySelector('.toast .ctl')."
                                  "textContent : ''")
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(AtriumZoomToastBrowserTest,
                       ZoomingTheActiveTabShowsAConfirmationWithItsPercent) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab = NavigateActiveTab();
  ASSERT_TRUE(tab);
  zoom::ZoomController* zoom_controller =
      zoom::ZoomController::FromWebContents(tab);
  ASSERT_TRUE(zoom_controller);

  chrome::Zoom(browser(), content::PAGE_ZOOM_IN);
  const gfx::Rect card = WaitForConfirmation();
  EXPECT_FALSE(card.IsEmpty());

  const std::string want =
      base::NumberToString(zoom_controller->GetZoomPercent()) + "%";
  content::WebContents* stack =
      window()->ConfirmationWidgetForTesting()->web_contents();
  ASSERT_TRUE(stack);
  EXPECT_EQ(want, CardText(stack));
}

// A repeated zoom must update the one card AtriumBrowserWindow::OnZoomChanged
// keys "zoom", not stack a second confirmation on top of it.
IN_PROC_BROWSER_TEST_F(AtriumZoomToastBrowserTest,
                       RepeatedZoomsUpdateOneCardInsteadOfStacking) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab = NavigateActiveTab();
  ASSERT_TRUE(tab);
  zoom::ZoomController* zoom_controller =
      zoom::ZoomController::FromWebContents(tab);
  ASSERT_TRUE(zoom_controller);

  chrome::Zoom(browser(), content::PAGE_ZOOM_IN);
  WaitForConfirmation();
  content::WebContents* stack =
      window()->ConfirmationWidgetForTesting()->web_contents();
  ASSERT_TRUE(stack);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    // .ov-card: toast-surface.js keeps a .toast shadow probe in the stack.
    return content::EvalJs(stack,
                           "document.querySelectorAll('.toast.ov-card').length")
               .ExtractInt() == 1;
  }));

  chrome::Zoom(browser(), content::PAGE_ZOOM_IN);
  const std::string want =
      base::NumberToString(zoom_controller->GetZoomPercent()) + "%";
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return CardText(stack) == want; }));
  EXPECT_EQ(1, content::EvalJs(
                   stack, "document.querySelectorAll('.toast.ov-card').length"));
}

}  // namespace
}  // namespace atrium
