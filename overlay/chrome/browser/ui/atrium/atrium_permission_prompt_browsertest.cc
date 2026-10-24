// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_permission_prompt.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_environment_variable_override.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_permission_sheet_widget.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/permission_bubble/permission_prompt.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/permissions/request_type.h"
#include "components/permissions/test/mock_permission_prompt_delegate.h"
#include "components/permissions/test/mock_permission_request.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/views/test/widget_test.h"

namespace atrium {
namespace {

// Same technique atrium_little_never_adopted_browsertest.cc uses: capture
// the little window's Browser via the synchronous OnBrowserCreated call.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// Same fixture shape as AtriumBrowserWindowBrowserTest (atrium_browser_
// window_browsertest.cc): with no seeded spaces.json, atrium_profile_
// bootstrap's "nothing to restore" rule opens the window tab-less and
// GetActiveWebContents() is null for the whole fixture.
class AtriumPermissionPromptBrowserTest : public InProcessBrowserTest {
 protected:
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
    atrium_core_close();
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
    atrium_core_close();
    env_override_.reset();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // The theme push is an ExecuteJavaScript, so nothing it does has happened
  // when the setter returns.
  void Settle() {
    base::RunLoop loop;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, loop.QuitClosure());
    loop.Run();
  }

  std::string ReadProperty(content::WebContents* contents, const char* name) {
    return content::EvalJs(
               contents,
               content::JsReplace(
                   "document.documentElement.style.getPropertyValue($1)", name))
        .ExtractString();
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::ScopedEnvironmentVariableOverride> env_override_;
};

// A geolocation request on an Atrium (is_type_normal)
// window routes to AtriumPermissionPrompt - not the chip or bubble, which
// have no LocationBarView to anchor to - and an Allow answer from the
// sheet reaches the delegate, the same round trip the page observes as a
// granted permissions.query()/getCurrentPosition() call.
IN_PROC_BROWSER_TEST_F(AtriumPermissionPromptBrowserTest,
                       GeolocationRequestRoutesToAtriumSheetAndAllowAccepts) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  std::vector<std::unique_ptr<permissions::PermissionRequest>> requests;
  requests.push_back(std::make_unique<permissions::MockPermissionRequest>(
      GURL("https://example.com"), permissions::RequestType::kGeolocation));

  testing::NiceMock<permissions::MockPermissionPromptDelegate> delegate;
  ON_CALL(delegate, Requests())
      .WillByDefault(testing::ReturnRef(requests));
  ON_CALL(delegate, GetRequestingOrigin())
      .WillByDefault(testing::Return(GURL("https://example.com")));
  EXPECT_CALL(delegate, Accept(testing::_)).Times(1);

  std::unique_ptr<permissions::PermissionPrompt> prompt =
      CreatePermissionPrompt(web_contents, &delegate);
  ASSERT_TRUE(prompt);

  // No dynamic_cast: Chromium builds with RTTI off. browser()'s default
  // window is is_type_normal(), so CreatePermissionPrompt's Atrium branch is
  // the only path that can have produced this object.
  EXPECT_EQ(permissions::PermissionPromptDisposition::CUSTOM_MODAL_DIALOG,
            prompt->GetPromptDisposition());
  auto* atrium_prompt = static_cast<AtriumPermissionPrompt*>(prompt.get());

  PermissionSheetWidget* sheet = atrium_prompt->GetSheetForTesting();
  ASSERT_TRUE(sheet);

  base::ListValue no_args;
  // Past the anti-clickjacking window, because this test is about the
  // round trip and not about the guard. Without this the Allow lands inside it
  // and is refused, which is the next test's job.
  sheet->OnSurfaceMessage("permissionSheetReady", no_args);
  sheet->ExpireAllowGuardForTesting();
  sheet->OnSurfaceMessage("permissionSheetAllow", no_args);
}

// an Allow inside the window is refused.
//
// The sheet takes keyboard focus the moment it appears, so a page that tells the
// user to hold Enter and then calls getUserMedia had the in-flight autorepeat
// arrive as a grant. Enter no longer sends Allow at all, but the browser-side
// window is the half a page cannot route around - the renderer is the attacker's
// to drive, so a renderer-side delay would be theirs to skip.
IN_PROC_BROWSER_TEST_F(AtriumPermissionPromptBrowserTest,
                       AnAllowInsideTheGuardWindowIsRefused) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  std::vector<std::unique_ptr<permissions::PermissionRequest>> requests;
  requests.push_back(std::make_unique<permissions::MockPermissionRequest>(
      GURL("https://example.com"), permissions::RequestType::kCameraStream));

  testing::NiceMock<permissions::MockPermissionPromptDelegate> delegate;
  ON_CALL(delegate, Requests()).WillByDefault(testing::ReturnRef(requests));
  ON_CALL(delegate, GetRequestingOrigin())
      .WillByDefault(testing::Return(GURL("https://example.com")));
  // The assertion: camera is never granted by an Allow this early.
  EXPECT_CALL(delegate, Accept(testing::_)).Times(0);

  std::unique_ptr<permissions::PermissionPrompt> prompt =
      CreatePermissionPrompt(web_contents, &delegate);
  ASSERT_TRUE(prompt);
  auto* atrium_prompt = static_cast<AtriumPermissionPrompt*>(prompt.get());
  PermissionSheetWidget* sheet = atrium_prompt->GetSheetForTesting();
  ASSERT_TRUE(sheet);

  base::ListValue no_args;
  // The page has not announced itself yet, so the user has seen no origin.
  sheet->OnSurfaceMessage("permissionSheetAllow", no_args);

  // Ready arrives: the window starts here, so an Allow right after is refused
  // even though the guard was already expired for the widget's creation time.
  sheet->ExpireAllowGuardForTesting();
  sheet->OnSurfaceMessage("permissionSheetReady", no_args);
  sheet->OnSurfaceMessage("permissionSheetAllow", no_args);

  // Still refused on a second attempt, so the refusal is not a one-shot that the
  // next keypress slips past.
  sheet->OnSurfaceMessage("permissionSheetAllow", no_args);
}

// The sheet draws its card and its Allow button out of overlay.css, which reads
// the tint off the document. A push that never lands looks exactly like a page
// that ignored it, so the assertion is on the sheet's own state.
IN_PROC_BROWSER_TEST_F(AtriumPermissionPromptBrowserTest,
                       TheLiveSheetCarriesTheSpaceTint) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  std::vector<std::unique_ptr<permissions::PermissionRequest>> requests;
  requests.push_back(std::make_unique<permissions::MockPermissionRequest>(
      GURL("https://example.com"), permissions::RequestType::kGeolocation));

  testing::NiceMock<permissions::MockPermissionPromptDelegate> delegate;
  ON_CALL(delegate, Requests()).WillByDefault(testing::ReturnRef(requests));
  ON_CALL(delegate, GetRequestingOrigin())
      .WillByDefault(testing::Return(GURL("https://example.com")));

  std::unique_ptr<permissions::PermissionPrompt> prompt =
      CreatePermissionPrompt(web_contents, &delegate);
  ASSERT_TRUE(prompt);
  auto* atrium_prompt = static_cast<AtriumPermissionPrompt*>(prompt.get());
  PermissionSheetWidget* sheet = atrium_prompt->GetSheetForTesting();
  ASSERT_TRUE(sheet);
  ASSERT_TRUE(sheet->web_contents())
      << "the sheet built no document, so there is nothing to theme";
  ASSERT_TRUE(content::WaitForLoadStop(sheet->web_contents()));

  // Live, not SetBackdropTint: the crossfade would defer the push past the
  // read below.
  window()->SetBackdropTintLive(0xFF112233);
  Settle();
  EXPECT_EQ("17 34 51",
            ReadProperty(sheet->web_contents(), "--atrium-space-tint"))
      << "the sheet is registered but the theme push does not reach it, so it "
         "shows the brand accent over whatever space the user is in";
}

// A little window, closed via chrome::CloseWindow rather than tab-strip
// teardown, so the sheet - held by this test's own `prompt`, outside any
// PermissionRequestManager - is still alive when the window is destroyed.
IN_PROC_BROWSER_TEST_F(AtriumPermissionPromptBrowserTest,
                       ASheetOutlivingItsWindowDoesNotTouchIt) {
  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  BrowserController* little = BrowserController::CreateLittleWindow(
      browser()->profile(), GURL("https://example.com/permsheet-teardown"),
      std::nullopt);
  capture_observation.Reset();
  ASSERT_TRUE(little);
  ASSERT_TRUE(capture.captured_);

  Browser* little_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(little_browser);
  content::WebContents* little_contents =
      little_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(little_contents);

  auto* little_window =
      static_cast<AtriumBrowserWindow*>(little_browser->window());
  ASSERT_TRUE(little_window);
  views::test::WidgetDestroyedWaiter destroy_waiter(little_window->widget());

  std::vector<std::unique_ptr<permissions::PermissionRequest>> requests;
  requests.push_back(std::make_unique<permissions::MockPermissionRequest>(
      GURL("https://example.com"), permissions::RequestType::kGeolocation));
  testing::NiceMock<permissions::MockPermissionPromptDelegate> delegate;
  ON_CALL(delegate, Requests()).WillByDefault(testing::ReturnRef(requests));
  ON_CALL(delegate, GetRequestingOrigin())
      .WillByDefault(testing::Return(GURL("https://example.com")));

  std::unique_ptr<permissions::PermissionPrompt> prompt =
      CreatePermissionPrompt(little_contents, &delegate);
  ASSERT_TRUE(prompt);
  auto* atrium_prompt = static_cast<AtriumPermissionPrompt*>(prompt.get());
  PermissionSheetWidget* sheet = atrium_prompt->GetSheetForTesting();
  ASSERT_TRUE(sheet);

  chrome::CloseWindow(little_browser);
  ui_test_utils::WaitForBrowserToClose();
  destroy_waiter.Wait();

  // `prompt` (and the sheet inside it) is still alive here, past its
  // window's destruction. Its own destructor runs next, at scope exit -
  // reaching the end of this test without a crash is the assertion.
}

}  // namespace
}  // namespace atrium
