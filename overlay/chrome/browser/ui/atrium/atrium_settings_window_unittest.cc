// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_window_unittest.cc
#include "chrome/browser/ui/atrium/atrium_settings_window.h"

#include <string>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/data_element.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

constexpr char kProbeUrl[] = "https://custom-endpoint.test/v1/chat/completions";

// Installs and removes the observer, so a failing test cannot leave a dangling
// callback pointing at its own stack.
class ProbeResult {
 public:
  ProbeResult() {
    AtriumSettingsWindow::SetCustomEndpointConnectionResultObserverForTesting(
        base::BindLambdaForTesting(
            [this](bool result_reachable, const std::string& result_error) {
              reachable_ = result_reachable;
              error_ = result_error;
            }));
  }

  ~ProbeResult() {
    AtriumSettingsWindow::SetCustomEndpointConnectionResultObserverForTesting(
        base::RepeatingCallback<void(bool, const std::string&)>());
  }

  const std::optional<bool>& reachable() const { return reachable_; }
  const std::optional<std::string>& error() const { return error_; }

 private:
  std::optional<bool> reachable_;
  std::optional<std::string> error_;
};

// A correctly configured OpenRouter endpoint reported "Could not reach that
// endpoint" for 105 s of honest work, sending the user after a network fault
// that was not there. Slow and unreachable have to read differently.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, SlowEndpointReportsSlowNotUnreachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);

  task_environment.RunUntilIdle();
  EXPECT_FALSE(result.reachable().has_value());
  ASSERT_EQ(factory.NumPending(), 1);

  task_environment.FastForwardBy(base::Seconds(10));

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(), "That endpoint was too slow to answer");
}

// The other half of the split: a host that cannot be reached at all still has
// to say so, or the new message would just be the old bug with new wording.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, UnreachableEndpointStillReportsUnreachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.AddResponse(
      GURL(kProbeUrl), network::mojom::URLResponseHead::New(), std::string(),
      network::URLLoaderCompletionStatus(net::ERR_CONNECTION_REFUSED));
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(), "Could not reach that endpoint");
}

// The whole point of the setting: a user who configures 120 s must not still
// be cut off at the built-in 10 s.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, AConfiguredTimeoutReplacesTheDefault) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory,
                                             /*timeout_seconds=*/120);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  // Past the built-in deadline, which would already have given up here.
  task_environment.FastForwardBy(base::Seconds(60));
  EXPECT_FALSE(result.reachable().has_value()) << "gave up at the default";

  task_environment.FastForwardBy(base::Seconds(61));
  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(), "That endpoint was too slow to answer");
}

// A streamed endpoint sends its headers at once and the body over the next
// minute, so a stall arrives with a 200 already set. Gated on a zero status,
// the slow branch would miss it and the shape check would blame the JSON.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, SlowAfterHeadersIsStillSlow) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.AddResponse(
      GURL(kProbeUrl), network::CreateURLResponseHead(net::HTTP_OK),
      std::string(), network::URLLoaderCompletionStatus(net::ERR_TIMED_OUT));
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(), "That endpoint was too slow to answer");
}

// An uncapped probe waits on a whole answer. Measured at 105 s against a free
// 550B reasoning model, which is ten times the probe's own deadline.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, ProbeCapsTheAnswerLength) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  const network::ResourceRequest& request =
      factory.GetPendingRequest(0)->request;
  ASSERT_TRUE(request.request_body);
  const std::string body(request.request_body->elements()
                             ->at(0)
                             .As<network::DataElementBytes>()
                             .AsStringPiece());
  EXPECT_NE(body.find("\"max_tokens\""), std::string::npos);
}

// The cap and the parser are one change: capping a reasoning model empties
// content, and without the reasoning fallback the probe would call a working
// endpoint the wrong shape.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, ReasoningOnlyAnswerIsReachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(
      kProbeUrl,
      R"({"choices":[{"finish_reason":"length","message":{"role":"assistant",)"
      R"("content":"","reasoning":"The user wants me to say"}}]})");
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_TRUE(*result.reachable());
}

// The defect this replaced: a site root answers 200 with a landing page, the
// probe went green, and every later summarize failed on the shape.
TEST(AtriumSettingsWindowCustomEndpointProbeTest,
     TwoHundredThatIsNotChatCompletionsIsNotReachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(kProbeUrl, "<html>welcome</html>");
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(),
            "That URL answered, but not with chat completions");
}

TEST(AtriumSettingsWindowCustomEndpointProbeTest,
     ChatCompletionsBodyIsReachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(
      kProbeUrl,
      R"({"choices":[{"message":{"role":"assistant","content":"pong"}}]})");
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_TRUE(*result.reachable());
}

// A 404 used to pass: DownloadHeadersOnly only looked at NetError(), which is
// net::OK for any answered request whatever its status.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, NotFoundIsNotReachable) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(GURL(kProbeUrl), "gpt-4o-mini",
                                             std::nullopt, &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(
      GURL(kProbeUrl), network::URLLoaderCompletionStatus(net::OK),
      network::CreateURLResponseHead(net::HTTP_NOT_FOUND), "not found");
  task_environment.RunUntilIdle();

  ASSERT_TRUE(result.reachable().has_value());
  EXPECT_FALSE(*result.reachable());
  EXPECT_EQ(*result.error(), "That endpoint answered 404");
}

// The panel sends the key, so a probe without it would fail against any
// endpoint that authenticates - a red cross there would mean nothing.
TEST(AtriumSettingsWindowCustomEndpointProbeTest, ProbeCarriesTheApiKeyAndModel) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::unique_ptr<AtriumSettingsWindow> window =
      AtriumSettingsWindow::CreateForTesting();
  ProbeResult result;

  window->StartCustomEndpointProbeForTesting(
      GURL(kProbeUrl), "gpt-4o-mini", std::optional<std::string>("secret-key"),
      &factory);
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  const network::ResourceRequest& request =
      factory.GetPendingRequest(0)->request;
  EXPECT_EQ(request.method, "POST");
  EXPECT_EQ(request.headers.GetHeader(net::HttpRequestHeaders::kAuthorization),
            "Bearer secret-key");
  ASSERT_TRUE(request.request_body);
  const std::string body(request.request_body->elements()
                             ->at(0)
                             .As<network::DataElementBytes>()
                             .AsStringPiece());
  EXPECT_NE(body.find("gpt-4o-mini"), std::string::npos);
}

// Every space-list sidebar route now calls RefreshSpaces() unconditionally,
// so the common case - no Settings window open - must not dereference Instance().
TEST(AtriumSettingsWindowSpacesTest, RefreshSpacesIsANoOpWhenNothingIsShowing) {
  ASSERT_FALSE(AtriumSettingsWindow::IsShowing());
  AtriumSettingsWindow::RefreshSpaces();
  EXPECT_FALSE(AtriumSettingsWindow::IsShowing());
}

TEST(ProbeMayCarryApiKeyTest, SavedUrlGetsKeyAfterNormalising) {
  EXPECT_TRUE(ProbeMayCarryApiKey(GURL("https://Custom-Endpoint.test:443/v1"),
                                  "https://custom-endpoint.test/v1"));
}

TEST(ProbeMayCarryApiKeyTest, OtherUrlGetsNoKey) {
  EXPECT_FALSE(
      ProbeMayCarryApiKey(GURL("https://attacker.test/v1"), kProbeUrl));
  EXPECT_FALSE(ProbeMayCarryApiKey(GURL("https://custom-endpoint.test/other"),
                                   kProbeUrl));
}

TEST(ProbeMayCarryApiKeyTest, EmptySavedUrlGetsNoKey) {
  EXPECT_FALSE(ProbeMayCarryApiKey(GURL(kProbeUrl), std::string()));
}

constexpr char kSavedJson[] =
    R"({"customEndpointUrl":"https://custom-endpoint.test/v1"})";

TEST(SavedEndpointUrlChangedTest, ChangedUrlIsAChange) {
  EXPECT_TRUE(SavedEndpointUrlChanged(
      kSavedJson, R"({"customEndpointUrl":"https://attacker.test/v1"})"));
  EXPECT_TRUE(
      SavedEndpointUrlChanged(kSavedJson, R"({"customEndpointUrl":""})"));
}

TEST(SavedEndpointUrlChangedTest, SameOrNormalisedUrlIsNotAChange) {
  EXPECT_FALSE(SavedEndpointUrlChanged(kSavedJson, kSavedJson));
  EXPECT_FALSE(SavedEndpointUrlChanged(
      kSavedJson,
      R"({"customEndpointUrl":"https://Custom-Endpoint.test:443/v1"})"));
  EXPECT_FALSE(SavedEndpointUrlChanged("{}", "{}"));
}

TEST(SavedEndpointUrlChangedTest, SavedUrlDroppedOrBadNewJsonIsAChange) {
  EXPECT_TRUE(SavedEndpointUrlChanged(kSavedJson, R"({"aiEnabled":true})"));
  EXPECT_TRUE(SavedEndpointUrlChanged(kSavedJson, "not json"));
}

TEST(SavedEndpointUrlChangedTest, EmptyOldUrlToANewUrlIsAChange) {
  constexpr char kNew[] = R"({"customEndpointUrl":"https://attacker.test/v1"})";
  EXPECT_TRUE(SavedEndpointUrlChanged(R"({"customEndpointUrl":""})", kNew));
  EXPECT_TRUE(SavedEndpointUrlChanged("{}", kNew));
  EXPECT_TRUE(SavedEndpointUrlChanged(std::nullopt, kNew));
}

TEST(SavedEndpointUrlChangedTest, EmptyOldUrlStaysEmptyIsNotAChange) {
  EXPECT_FALSE(SavedEndpointUrlChanged("{}", R"({"aiEnabled":true})"));
  EXPECT_FALSE(SavedEndpointUrlChanged("{}", R"({"customEndpointUrl":""})"));
  EXPECT_FALSE(SavedEndpointUrlChanged(std::nullopt, "{}"));
}

TEST(SavedEndpointUrlChangedTest, UnreadableOldFileIsAChange) {
  constexpr char kNew[] = R"({"customEndpointUrl":"https://attacker.test/v1"})";
  EXPECT_TRUE(SavedEndpointUrlChanged("garbage", kNew));
  EXPECT_TRUE(SavedEndpointUrlChanged("", kNew));
  EXPECT_TRUE(SavedEndpointUrlChanged("[]", kNew));
}

TEST(ApiKeyWriteAllowedTest, RefusedWithoutAValidSavedUrl) {
  EXPECT_FALSE(ApiKeyWriteAllowed("sk-key", std::nullopt));
  EXPECT_FALSE(ApiKeyWriteAllowed("sk-key", "{}"));
  EXPECT_FALSE(ApiKeyWriteAllowed("sk-key", R"({"customEndpointUrl":""})"));
  EXPECT_FALSE(
      ApiKeyWriteAllowed("sk-key", R"({"customEndpointUrl":"not a url"})"));
  EXPECT_FALSE(ApiKeyWriteAllowed("sk-key", "garbage"));
  EXPECT_FALSE(ApiKeyWriteAllowed(
      "sk-key", R"({"customEndpointUrl":"http://custom-endpoint.test/v1"})"));
}

TEST(ApiKeyWriteAllowedTest, AllowedWithAValidSavedUrlOrWhenClearing) {
  EXPECT_TRUE(ApiKeyWriteAllowed("sk-key", kSavedJson));
  EXPECT_TRUE(ApiKeyWriteAllowed("", std::nullopt));
  EXPECT_TRUE(ApiKeyWriteAllowed("", "garbage"));
}

TEST(ShouldDeleteOrphanApiKeyTest, DeletesOnlyAnOrphan) {
  EXPECT_TRUE(ShouldDeleteOrphanApiKey("{}", false, [] { return true; }));
  EXPECT_TRUE(
      ShouldDeleteOrphanApiKey(std::nullopt, false, [] { return true; }));
  EXPECT_FALSE(ShouldDeleteOrphanApiKey("{}", false, [] { return false; }));
  EXPECT_FALSE(
      ShouldDeleteOrphanApiKey(kSavedJson, false, [] { return true; }));
}

TEST(ShouldDeleteOrphanApiKeyTest, KeepsTheKeyWhenTheFileCannotBeRead) {
  EXPECT_FALSE(ShouldDeleteOrphanApiKey("", true, [] { return true; }));
}

TEST(SaveSettingsClearingKeyTest, FailedDeleteSkipsTheWrite) {
  int writes = 0;
  EXPECT_EQ(kSaveWriteFailed,
            SaveSettingsClearingKey(
                kSavedJson, R"({"customEndpointUrl":"https://attacker.test"})",
                [] { return false; },
                [&] {
                  ++writes;
                  return kSaveWritten;
                }));
  EXPECT_EQ(0, writes);
}

TEST(SaveSettingsClearingKeyTest, DeletesOnlyWhenTheUrlChanged) {
  int deletes = 0;
  auto delete_key = [&] {
    ++deletes;
    return true;
  };
  EXPECT_EQ(kSaveWritten,
            SaveSettingsClearingKey(
                kSavedJson, R"({"customEndpointUrl":"https://attacker.test"})",
                delete_key, [] { return kSaveWritten; }));
  EXPECT_EQ(1, deletes);
  EXPECT_EQ(kSaveWritten,
            SaveSettingsClearingKey(kSavedJson, kSavedJson, delete_key,
                                    [] { return kSaveWritten; }));
  EXPECT_EQ(1, deletes);
}

}  // namespace
}  // namespace atrium
