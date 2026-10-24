// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_media_hub_browsertest.cc

#include "chrome/browser/ui/atrium/atrium_media_hub.h"

#include <string>

#include "base/command_line.h"
#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/media_start_stop_observer.h"
#include "media/base/media_switches.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/media_session/public/cpp/test/mock_media_session.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

constexpr char kMediaPage[] = "/media/session/video-with-metadata.html";

class AtriumMediaHubBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kAutoplayPolicy,
                                    "no-user-gesture-required");
  }

  content::WebContents* PlayInTab(Browser* target, bool foreground) {
    content::WebContents* tab = chrome::AddAndReturnTabAt(
        target, embedded_test_server()->GetURL(kMediaPage), -1, foreground);
    EXPECT_TRUE(tab);
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    EXPECT_EQ(base::Value(), content::EvalJs(tab, "play()"));
    // play() returns before the element is really running, and a record only
    // exists once the session holds audio focus.
    EXPECT_EQ(true, content::EvalJs(tab, R"(
        new Promise(resolve => {
          const video = document.getElementById('video');
          const tick = setInterval(() => {
            if (!video.paused && video.currentTime > 0) {
              clearInterval(tick);
              resolve(true);
            }
          }, 50);
        }))"));
    return tab;
  }

  bool WaitForRecords(AtriumMediaHub* hub, size_t count) {
    return base::test::RunUntil(
        [&]() { return hub->records().records().size() == count; });
  }

  // Puts `browser()`'s window into a kInWindowSwap: its own native window
  // keeps presenting, the slot's shown content becomes a second profile's
  // browser. A first activation, then a second, is what makes ActivateWindow
  // choose the swap branch rather than kFirstActivation.
  Browser* SwapInSecondProfile(const std::string& dir_suffix) {
    atrium::BrowserController* first =
        atrium::BrowserController::ControllerForBrowser(browser());
    if (!first) {
      return nullptr;
    }
    atrium::BrowserController::ActivateWindow(first);

    ProfileManager* manager = g_browser_process->profile_manager();
    if (!manager) {
      return nullptr;
    }
    Profile& other = profiles::testing::CreateProfileSync(
        manager,
        manager->user_data_dir().AppendASCII("atrium-media-swap-" +
                                             dir_suffix));
    atrium::BrowserController* second =
        atrium::BrowserController::CreateForProfile(
            &other, /*show=*/false, /*startup_url=*/std::string(),
            atrium::BrowserController::kDefaultSlot);
    if (!second) {
      return nullptr;
    }
    atrium::BrowserController::ActivateWindow(second);

    Browser* other_browser = nullptr;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* candidate) {
          if (candidate->GetProfile() == &other) {
            other_browser = candidate->GetBrowserForMigrationOnly();
            return false;
          }
          return true;
        });
    return other_browser;
  }
};

IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest, BackgroundAudioGetsOneRecord) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/false);
  ASSERT_TRUE(WaitForRecords(hub, 1u));

  const MediaRecord& record = hub->records().records().front();
  EXPECT_EQ(TabId(tab), record.tab_id);
  EXPECT_TRUE(record.playing);
  EXPECT_FALSE(record.muted);
  EXPECT_TRUE(record.can_prev);
  EXPECT_TRUE(record.can_next);
  EXPECT_EQ(embedded_test_server()->GetURL(kMediaPage).host(), record.host);
  EXPECT_NE(std::string::npos, record.title.find("Big Buck Bunny"));
}

IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest, CommandsReachTheMediaSession) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/false);
  ASSERT_TRUE(WaitForRecords(hub, 1u));
  const uint64_t tab_id = TabId(tab);

  hub->PlayPause(tab_id);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)
               ->GetMediaSessionInfoSync()
               ->playback_state ==
           media_session::mojom::MediaPlaybackState::kPaused;
  })) << "mediaPlayPause never reached the session";

  hub->PlayPause(tab_id);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)
               ->GetMediaSessionInfoSync()
               ->playback_state ==
           media_session::mojom::MediaPlaybackState::kPlaying;
  })) << "a second mediaPlayPause never resumed the session";

  hub->ToggleMute(tab_id);
  EXPECT_TRUE(tab->IsAudioMuted()) << "mediaMuteToggled did not mute the tab";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    const auto& records = hub->records().records();
    return records.size() == 1u && records.front().muted;
  })) << "the muted record never reached the list: " << hub->records().ToJson();

  // One-way calls into the page's own handlers, so there is nothing to read
  // back - they run here to prove they do not crash the session.
  hub->PreviousTrack(tab_id);
  hub->NextTrack(tab_id);

  hub->CloseMedia(tab_id);
  EXPECT_TRUE(hub->records().records().empty())
      << "mediaClosed left the record behind";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)
               ->GetMediaSessionInfoSync()
               ->playback_state ==
           media_session::mojom::MediaPlaybackState::kPaused;
  })) << "mediaClosed did not stop the sound";
}

// media_dialog_view_interactive_browsertest.cc drives the same
// EnterPictureInPicture() path from its own toggle button and gets a real
// window, so this asserts the window, not just the session flag.
IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest,
                       PictureInPictureCommandFlipsTheRecord) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/true);
  ASSERT_TRUE(WaitForRecords(hub, 1u));
  const uint64_t tab_id = TabId(tab);

  {
    content::MediaStartStopObserver observer(
        tab, content::MediaStartStopObserver::Type::kEnterPictureInPicture);
    hub->TogglePictureInPicture(tab_id);
    observer.Wait();
  }
  EXPECT_TRUE(tab->HasPictureInPictureVideo());
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return hub->records().records().front().picture_in_picture;
  })) << "entering picture-in-picture never reached the record";

  {
    content::MediaStartStopObserver observer(
        tab, content::MediaStartStopObserver::Type::kExitPictureInPicture);
    hub->TogglePictureInPicture(tab_id);
    observer.Wait();
  }
  EXPECT_FALSE(tab->HasPictureInPictureVideo());
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !hub->records().records().front().picture_in_picture;
  })) << "exiting picture-in-picture never reached the record";
}

IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest, SetVolumeUpdatesTheRecord) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/false);
  ASSERT_TRUE(WaitForRecords(hub, 1u));
  const uint64_t tab_id = TabId(tab);

  hub->SetVolume(tab_id, 0.4);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return hub->records().records().front().volume == 0.4;
  })) << "mediaVolume never reached the record: " << hub->records().ToJson();
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)->GetMediaSessionInfoSync()->state ==
           media_session::mojom::MediaSessionInfo::SessionState::kDucking;
  })) << "SetVolume below 1.0 never ducked the session";

  hub->SetVolume(tab_id, 1.0);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return hub->records().records().front().volume == 1.0;
  }));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)->GetMediaSessionInfoSync()->state !=
           media_session::mojom::MediaSessionInfo::SessionState::kDucking;
  })) << "SetVolume(1.0) never stopped ducking the session";

  // Out-of-range levels clamp rather than reaching the session raw.
  hub->SetVolume(tab_id, 5.0);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return hub->records().records().front().volume == 1.0;
  }));
}

// StartDucking() returns early while the session already ducks, so a drag's
// second level only reaches the players if the ducking stops first. The unduck
// notification is the proof.
IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest, SetVolumeReappliesEveryLevel) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/false);
  ASSERT_TRUE(WaitForRecords(hub, 1u));
  const uint64_t tab_id = TabId(tab);

  hub->SetVolume(tab_id, 0.6);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)->GetMediaSessionInfoSync()->state ==
           media_session::mojom::MediaSessionInfo::SessionState::kDucking;
  })) << "the first level never ducked the session";

  media_session::test::MockMediaSessionMojoObserver observer(
      *content::MediaSession::Get(tab));
  hub->SetVolume(tab_id, 0.2);
  observer.WaitForState(
      media_session::mojom::MediaSessionInfo::SessionState::kActive);
  observer.WaitForState(
      media_session::mojom::MediaSessionInfo::SessionState::kDucking);
  EXPECT_EQ(hub->records().records().front().volume, 0.2);
}

// The audio focus manager unducks every session in the stack whenever any
// tab requests audio focus (enforcement is off outside ChromeOS), so a level
// SetVolume applied must be reapplied rather than lost.
IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest,
                       SetVolumeSurvivesAnUnduck) {
  ASSERT_TRUE(embedded_test_server()->Start());
  AtriumMediaHub* hub = AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(hub);

  content::WebContents* tab = PlayInTab(browser(), /*foreground=*/false);
  ASSERT_TRUE(WaitForRecords(hub, 1u));
  const uint64_t tab_id = TabId(tab);

  hub->SetVolume(tab_id, 0.4);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)->GetMediaSessionInfoSync()->state ==
           media_session::mojom::MediaSessionInfo::SessionState::kDucking;
  }));

  // The call AudioFocusManager::EnforceSingleSession makes on every session.
  // A second playing tab is the real-world trigger, but PlayInTab hangs on
  // one: its wait for the video to start never returns.
  content::MediaSession::Get(tab)->StopDucking();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(tab)->GetMediaSessionInfoSync()->state ==
           media_session::mojom::MediaSessionInfo::SessionState::kDucking;
  })) << "the level was lost after the audio focus manager unducked";

  const MediaRecord* record = nullptr;
  for (const MediaRecord& candidate : hub->records().records()) {
    if (candidate.tab_id == tab_id) {
      record = &candidate;
    }
  }
  ASSERT_TRUE(record);
  EXPECT_EQ(0.4, record->volume);
}

IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest, PrivateWindowSeesOnlyItsOwn) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());

  AtriumMediaHub* regular = AtriumMediaHub::ForProfile(browser()->profile());
  AtriumMediaHub* private_hub =
      AtriumMediaHub::ForProfile(incognito->profile());
  ASSERT_TRUE(regular);
  ASSERT_TRUE(private_hub);
  ASSERT_NE(regular, private_hub);

  content::WebContents* private_contents =
      PlayInTab(incognito, /*foreground=*/true);
  const uint64_t private_tab = TabId(private_contents);
  ASSERT_TRUE(WaitForRecords(private_hub, 1u))
      << "private: " << private_hub->records().ToJson()
      << " regular: " << regular->records().ToJson();
  EXPECT_EQ(private_tab, private_hub->records().records().front().tab_id);
  EXPECT_TRUE(regular->records().records().empty())
      << "the regular profile's hub took a private window's record";
  EXPECT_FALSE(regular->records().Owns(private_tab));

  content::WebContents* regular_contents =
      PlayInTab(browser(), /*foreground=*/false);
  const uint64_t regular_tab = TabId(regular_contents);
  ASSERT_TRUE(WaitForRecords(regular, 1u));
  EXPECT_EQ(regular_tab, regular->records().records().front().tab_id);
  EXPECT_FALSE(private_hub->records().Owns(regular_tab));
  EXPECT_EQ(1u, private_hub->records().records().size());

  // A command naming the other profile's tab does nothing.
  private_hub->ToggleMute(regular_tab);
  EXPECT_FALSE(regular_contents->IsAudioMuted())
      << "a private window muted a regular window's tab";
}

// a cross-profile slot swap keeps the presenter's window on
// screen showing another profile's tabs. Its sidebar - the one the user can
// see - must render that SHOWN profile's controller, not its own profile's
// (which plays too, so a profile-keyed push would still find a hub to answer
// with the wrong list).
IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest,
                       CrossProfileSwapPushesTheShownProfilesController) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Browser* other_browser = SwapInSecondProfile("a");
  ASSERT_TRUE(other_browser);

  auto* presenting_window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  auto* other_own_window =
      static_cast<AtriumBrowserWindow*>(other_browser->window());
  ASSERT_EQ(presenting_window, atrium::BrowserController::
                                    PresentedWindowForBrowser(other_browser))
      << "the presenter must stay put across the swap";
  ASSERT_NE(presenting_window, other_own_window)
      << "nothing diverged, so this is not a real swap";

  PlayInTab(browser(), /*foreground=*/false);
  content::WebContents* shown_tab =
      PlayInTab(other_browser, /*foreground=*/true);
  const uint64_t shown_tab_id = TabId(shown_tab);
  AtriumMediaHub* shown_hub =
      AtriumMediaHub::ForProfile(other_browser->profile());
  ASSERT_TRUE(shown_hub);
  ASSERT_TRUE(WaitForRecords(shown_hub, 1u));

  content::WebContents* presenting_sidebar =
      presenting_window->SidebarContentsForTesting();
  ASSERT_TRUE(presenting_sidebar);
  const std::string script = "!!document.querySelector('.sbm-item[data-tab-"
                             "id=\"" +
                             std::to_string(shown_tab_id) + "\"]')";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(presenting_sidebar, script).ExtractBool();
  })) << "the on-screen sidebar never showed the shown profile's controller";
}

// the shown profile owns playback control during a swap. A
// media command sent through the presenting window's own surface router must
// resolve the same hub the sidebar's controller belongs to, and must leave
// the presenter's own (not shown) profile untouched.
IN_PROC_BROWSER_TEST_F(AtriumMediaHubBrowserTest,
                       CrossProfileSwapCommandReachesTheShownTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Browser* other_browser = SwapInSecondProfile("b");
  ASSERT_TRUE(other_browser);

  auto* presenting_window =
      static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_EQ(presenting_window, atrium::BrowserController::
                                    PresentedWindowForBrowser(other_browser));

  content::WebContents* presenter_tab =
      PlayInTab(browser(), /*foreground=*/false);
  AtriumMediaHub* presenter_hub =
      AtriumMediaHub::ForProfile(browser()->profile());
  ASSERT_TRUE(presenter_hub);
  ASSERT_TRUE(WaitForRecords(presenter_hub, 1u));

  content::WebContents* shown_tab =
      PlayInTab(other_browser, /*foreground=*/true);
  const uint64_t shown_tab_id = TabId(shown_tab);
  AtriumMediaHub* shown_hub =
      AtriumMediaHub::ForProfile(other_browser->profile());
  ASSERT_TRUE(shown_hub);
  ASSERT_TRUE(WaitForRecords(shown_hub, 1u));

  base::ListValue args;
  args.Append(std::to_string(shown_tab_id));
  presenting_window->OnSurfaceMessage("mediaPlayPause", args);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::MediaSession::Get(shown_tab)
               ->GetMediaSessionInfoSync()
               ->playback_state ==
           media_session::mojom::MediaPlaybackState::kPaused;
  })) << "mediaPlayPause during a swap did not reach the shown tab";

  EXPECT_EQ(media_session::mojom::MediaPlaybackState::kPlaying,
           content::MediaSession::Get(presenter_tab)
               ->GetMediaSessionInfoSync()
               ->playback_state)
      << "the command reached a hub outside the shown profile";
}

}  // namespace
}  // namespace atrium
