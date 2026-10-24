// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint_unittest.cc
#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"

#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/ip_address.h"
#include "net/dns/host_resolver.h"
#include "net/log/net_log.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/data_element.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/test/test_network_context_with_host_resolver.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium_ai {
namespace {

TEST(AtriumAiBackendCustomEndpointTest, EmptyUrlIsNotConfigured) {
  EXPECT_FALSE(IsCustomEndpointConfigured(""));
}

TEST(AtriumAiBackendCustomEndpointTest, HttpUrlIsNotConfigured) {
  EXPECT_FALSE(IsCustomEndpointConfigured("http://api.example.com/v1"));
}

TEST(AtriumAiBackendCustomEndpointTest, InvalidUrlIsNotConfigured) {
  EXPECT_FALSE(IsCustomEndpointConfigured("not a url"));
}

TEST(AtriumAiBackendCustomEndpointTest, HttpsUrlIsConfigured) {
  EXPECT_TRUE(IsCustomEndpointConfigured("https://api.example.com/v1"));
}

TEST(AtriumAiBackendCustomEndpointTest, UnconfiguredEndpointIsUnavailable) {
  EXPECT_EQ(atrium::DecideSummarizeAction(/*ai_enabled=*/true,
                                          IsCustomEndpointConfigured(""),
                                          "some page text"),
           atrium::SummarizeOutcome::kUnavailable);
}

TEST(AtriumAiBackendCustomEndpointTest, ConfiguredEndpointWithTextCallsModel) {
  EXPECT_EQ(atrium::DecideSummarizeAction(
                /*ai_enabled=*/true,
                IsCustomEndpointConfigured("https://api.example.com/v1"),
                "some page text"),
           atrium::SummarizeOutcome::kCallModel);
}

TEST(AtriumAiBackendCustomEndpointTest, RequestBodyIsOpenAiChatShape) {
  const std::string body =
      BuildCustomEndpointChatRequestBody("gpt-4o-mini", "hello page", "");
  EXPECT_NE(body.find("\"messages\":"), std::string::npos);
  EXPECT_NE(body.find("hello page"), std::string::npos);
  EXPECT_NE(body.find("\"stream\":true"), std::string::npos);
  EXPECT_NE(body.find("\"role\":\"user\""), std::string::npos);
  EXPECT_NE(body.find("\"model\":"), std::string::npos);
}

// the chosen model, not a hardcoded one, has to reach the
// request body - a distinct value from the pre-task default rules out a test
// that would still pass against the old hardcoded "gpt-4o-mini".
TEST(AtriumAiBackendCustomEndpointTest, RequestBodyCarriesTheChosenModel) {
  const std::string body =
      BuildCustomEndpointChatRequestBody("claude-3-5-sonnet", "hello page", "");
  EXPECT_NE(body.find("\"model\":\"claude-3-5-sonnet\""), std::string::npos);
}

TEST(AtriumAiBackendCustomEndpointTest, RequestBodyEscapesPageTextSafely) {
  const std::string body = BuildCustomEndpointChatRequestBody(
      "gpt-4o-mini", "quote \" and \\ backslash", "");
  EXPECT_EQ(body.find("\"quote \" and"), std::string::npos);
}

TEST(AtriumAiBackendCustomEndpointTest, AQuestionReachesTheRequestBody) {
  const std::string body = BuildCustomEndpointChatRequestBody(
      "gpt-4o-mini", "hello page", "who wrote this?");
  EXPECT_NE(body.find("who wrote this?"), std::string::npos);
  EXPECT_NE(body.find("\"role\":\"system\""), std::string::npos);
  EXPECT_EQ(body.find("Summarize this page"), std::string::npos);
}

TEST(AtriumAiBackendCustomEndpointTest, ParsesContentFromChatCompletions) {
  std::optional<CustomEndpointReply> summary = ParseCustomEndpointChatResponse(
      R"({"choices":[{"message":{"role":"assistant","content":)"
      R"("a real summary"}}]})");
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->answer, "a real summary");
  EXPECT_EQ(summary->reasoning, "");
}

TEST(AtriumAiBackendCustomEndpointTest, RejectsNonJsonResponse) {
  EXPECT_FALSE(ParseCustomEndpointChatResponse("not json at all").has_value());
}

TEST(AtriumAiBackendCustomEndpointTest, RejectsJsonMissingChoicesField) {
  EXPECT_FALSE(ParseCustomEndpointChatResponse(R"({"model":"gpt"})")
                  .has_value());
}

TEST(AtriumAiBackendCustomEndpointTest, RejectsEmptyChoicesList) {
  EXPECT_FALSE(ParseCustomEndpointChatResponse(R"({"choices":[]})")
                  .has_value());
}

TEST(AtriumAiBackendCustomEndpointTest, RejectsMessageMissingContentField) {
  EXPECT_FALSE(ParseCustomEndpointChatResponse(
                   R"({"choices":[{"message":{"role":"assistant"}}]})")
                  .has_value());
}

// A max_tokens cap stops a reasoning model inside its reasoning, so content
// comes back empty with the only text in `reasoning`. Read as a wrong shape,
// that reports a working endpoint as broken.
TEST(AtriumAiBackendCustomEndpointTest, FallsBackToReasoningWhenNoContent) {
  std::optional<CustomEndpointReply> answer = ParseCustomEndpointChatResponse(
      R"({"choices":[{"finish_reason":"length","message":{)"
      R"("role":"assistant","content":"","reasoning":"half a thought"}}]})");
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->answer, "half a thought");
  // promoted, not also left in the fold - behind a disclosure it
  // would be the whole reply hidden from the user who asked for it.
  EXPECT_EQ(answer->reasoning, "");
}

// Content wins as the answer, and the reasoning is kept beside it
// rather than thrown away: folding it is only possible while the two are apart.
TEST(AtriumAiBackendCustomEndpointTest, ContentAndReasoningComeBackApart) {
  std::optional<CustomEndpointReply> answer = ParseCustomEndpointChatResponse(
      R"({"choices":[{"message":{"role":"assistant",)"
      R"("content":"the answer","reasoning":"the notes"}}]})");
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->answer, "the answer");
  EXPECT_EQ(answer->reasoning, "the notes");
}

TEST(AtriumAiBackendCustomEndpointTest, OmitsMaxTokensWhenUnset) {
  const std::string body =
      BuildCustomEndpointChatRequestBody("gpt-4o-mini", "hello page", "");
  EXPECT_EQ(body.find("max_tokens"), std::string::npos);
}

TEST(AtriumAiBackendCustomEndpointTest, SendsMaxTokensWhenSet) {
  const std::string body =
      BuildCustomEndpointChatRequestBody("gpt-4o-mini", "hello page", "", 64);
  EXPECT_NE(body.find("\"max_tokens\":64"), std::string::npos);
}

// Zero is the settings row's off value, and it has to reach the wire as an
// absent field rather than as max_tokens 0, which would ask for no answer.
TEST(AtriumAiBackendCustomEndpointTest, AZeroCapSendsNoMaxTokens) {
  const std::string body =
      BuildCustomEndpointChatRequestBody("gpt-4o-mini", "hello page", "", 0);
  EXPECT_EQ(body.find("max_tokens"), std::string::npos);
}

TEST(AtriumAiBackendCustomEndpointTest, DefaultLimitsAreBothOff) {
  CustomEndpointLimits limits;
  EXPECT_EQ(limits.max_tokens, 0);
  EXPECT_EQ(limits.idle_timeout_seconds, 0);
}

// The settings row is worth nothing if the number stops at the call boundary,
// so this reads the body off the wire rather than trusting the plumbing.
TEST(AtriumAiBackendCustomEndpointTest, AConfiguredCapReachesTheRequestBody) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string>, std::optional<std::string>) {}),
      "gpt-4o-mini", CustomEndpointLimits{/*max_tokens=*/256,
                                          /*idle_timeout_seconds=*/120});
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  const network::ResourceRequest& request =
      factory.GetPendingRequest(0)->request;
  ASSERT_TRUE(request.request_body);
  const std::string body(request.request_body->elements()
                             ->at(0)
                             .As<network::DataElementBytes>()
                             .AsStringPiece());
  EXPECT_NE(body.find("\"max_tokens\":256"), std::string::npos);
}

// unlike Ollama (which is exempt on purpose), this
// backend must refuse a loopback or private resolved address on this exact
// path, not just via the shared guard utility in isolation.
TEST(AtriumAiBackendCustomEndpointTest, RefusesLoopbackResolvedAddress) {
  net::IPAddress loopback;
  ASSERT_TRUE(loopback.AssignFromIPLiteral("127.0.0.1"));
  EXPECT_TRUE(CustomEndpointRefusesResolvedAddresses({loopback}));
}

TEST(AtriumAiBackendCustomEndpointTest, RefusesPrivateResolvedAddress) {
  net::IPAddress private_addr;
  ASSERT_TRUE(private_addr.AssignFromIPLiteral("192.168.1.5"));
  EXPECT_TRUE(CustomEndpointRefusesResolvedAddresses({private_addr}));
}

TEST(AtriumAiBackendCustomEndpointTest, RefusesEmptyResolvedAddressList) {
  EXPECT_TRUE(CustomEndpointRefusesResolvedAddresses({}));
}

TEST(AtriumAiBackendCustomEndpointTest, AcceptsPublicResolvedAddress) {
  net::IPAddress public_addr;
  ASSERT_TRUE(public_addr.AssignFromIPLiteral("93.184.216.34"));
  EXPECT_FALSE(CustomEndpointRefusesResolvedAddresses({public_addr}));
}

// A wrong or expired key answers 401 with a JSON error object, which the shape
// check would otherwise report as a malformed response.
TEST(AtriumAiBackendCustomEndpointTest, AnAuthRefusalNamesTheStatusNotTheShape) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions",
      std::optional<std::string>("stale-key"), "page text", "", &factory,
      &network_context,
      base::BindLambdaForTesting([&](std::optional<std::string> result_answer,
                                     std::optional<std::string> result_error) {
        answer = std::move(result_answer);
        error = std::move(result_error);
      }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(
      GURL("https://custom-endpoint.test/v1/chat/completions"),
      network::URLLoaderCompletionStatus(net::OK),
      network::CreateURLResponseHead(net::HTTP_UNAUTHORIZED),
      R"({"error":{"message":"invalid_api_key"}})");
  task_environment.RunUntilIdle();

  EXPECT_FALSE(answer.has_value());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, "The custom endpoint answered 401.");
}

TEST(AtriumAiBackendCustomEndpointTest, AHungEndpointTimesOutAndReportsAnError) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> summary;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_summary,
              std::optional<std::string> result_error) {
            summary = std::move(result_summary);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_FALSE(answered);
  ASSERT_EQ(factory.NumPending(), 1);

  task_environment.FastForwardBy(base::Minutes(1));

  ASSERT_TRUE(answered);
  EXPECT_FALSE(summary.has_value());
  EXPECT_TRUE(error.has_value());
}

// Drives the response by hand, unlike SimulateResponseForPendingRequest,
// so a test can split the body into separate OnDataReceived calls.
void StartStream(network::TestURLLoaderFactory* factory,
                 net::HttpStatusCode status,
                 mojo::ScopedDataPipeProducerHandle* producer_out,
                 uint32_t pipe_capacity_bytes = 0) {
  network::TestURLLoaderFactory::PendingRequest* pending =
      factory->GetPendingRequest(0);
  ASSERT_TRUE(pending);
  mojo::ScopedDataPipeConsumerHandle consumer;
  if (pipe_capacity_bytes == 0) {
    ASSERT_EQ(mojo::CreateDataPipe(nullptr, *producer_out, consumer),
             MOJO_RESULT_OK);
  } else {
    MojoCreateDataPipeOptions options = {};
    options.struct_size = sizeof(options);
    options.element_num_bytes = 1;
    options.capacity_num_bytes = pipe_capacity_bytes;
    ASSERT_EQ(mojo::CreateDataPipe(&options, *producer_out, consumer),
             MOJO_RESULT_OK);
  }
  pending->client->OnReceiveResponse(network::CreateURLResponseHead(status),
                                     std::move(consumer), std::nullopt);
}

// SimpleURLLoaderImpl fails the request if decoded_body_length (reported by
// FinishStream) doesn't match the bytes actually read, so this tracks it.
void WriteChunk(const mojo::ScopedDataPipeProducerHandle& producer,
               base::test::TaskEnvironment* task_environment,
               const std::string& chunk,
               int64_t* total_bytes_written) {
  size_t written = 0;
  ASSERT_EQ(producer->WriteData(base::as_byte_span(chunk),
                                MOJO_WRITE_DATA_FLAG_NONE, written),
           MOJO_RESULT_OK);
  ASSERT_EQ(written, chunk.size());
  *total_bytes_written += written;
  task_environment->RunUntilIdle();
}

void FinishStream(network::TestURLLoaderFactory* factory,
                  mojo::ScopedDataPipeProducerHandle* producer,
                  base::test::TaskEnvironment* task_environment,
                  int64_t total_bytes_written) {
  network::TestURLLoaderFactory::PendingRequest* pending =
      factory->GetPendingRequest(0);
  ASSERT_TRUE(pending);
  producer->reset();
  network::URLLoaderCompletionStatus status(net::OK);
  status.decoded_body_length = total_bytes_written;
  pending->client->OnComplete(status);
  task_environment->RunUntilIdle();
}

TEST(AtriumAiBackendCustomEndpointTest,
    ChunksSplitMidLineReassembleIntoTheFullAnswer) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  const std::string full_body =
      "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
      "data: [DONE]\n\n";
  const size_t split = full_body.find("\"con");
  ASSERT_NE(split, std::string::npos);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment, full_body.substr(0, split),
            &total_bytes_written);
  WriteChunk(producer, &task_environment, full_body.substr(split),
            &total_bytes_written);
  FinishStream(&factory, &producer, &task_environment, total_bytes_written);

  ASSERT_TRUE(answered);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(*answer, "Hello world");
  EXPECT_FALSE(error.has_value());
}

TEST(AtriumAiBackendCustomEndpointTest,
    DeltaOnlyReasoningStreamProducesTheAnswer) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"content\":\"\","
            "\"reasoning\":\"half a thought\"}}]}\n\n",
            &total_bytes_written);
  FinishStream(&factory, &producer, &task_environment, total_bytes_written);

  ASSERT_TRUE(answered);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(*answer, "half a thought");
  EXPECT_FALSE(error.has_value());
}

// The shape a reasoning model really sends: reasoning deltas first,
// then content deltas. Concatenated, as this path did before, they cannot be
// told apart again and the panel opens on working-out.
TEST(AtriumAiBackendCustomEndpointTest,
    AReasoningStreamKeepsTheThinkingApartFromTheAnswer) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::vector<std::pair<std::string, std::string>> chunks;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
          }),
      "gpt-4o-mini", CustomEndpointLimits{},
      base::BindLambdaForTesting(
          [&](const std::string& so_far, const std::string& reasoning) {
            chunks.emplace_back(so_far, reasoning);
          }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"reasoning\":\"The user \"}}]}\n\n",
            &total_bytes_written);
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"reasoning\":\"wants a list.\"}}]}\n\n",
            &total_bytes_written);
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"content\":\"Here it is.\"}}]}\n\n",
            &total_bytes_written);
  FinishStream(&factory, &producer, &task_environment, total_bytes_written);

  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(*answer, "Here it is.")
      << "the thinking leaked into the answer the panel shows";
  ASSERT_EQ(chunks.size(), 3u);
  // The thinking arrives on its own channel before the answer starts,
  // which is what lets the panel fill the fold while the model is still working.
  EXPECT_EQ(chunks[0], std::make_pair(std::string(), std::string("The user ")));
  EXPECT_EQ(chunks[1],
            std::make_pair(std::string(), std::string("The user wants a list.")));
  EXPECT_EQ(chunks[2], std::make_pair(std::string("Here it is."),
                                      std::string("The user wants a list.")));
}

// A model with no reasoning must push an empty second field all
// the way through, or every plain answer grows a disclosure with nothing in it.
TEST(AtriumAiBackendCustomEndpointTest, APlainStreamCarriesNoThinking) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::vector<std::pair<std::string, std::string>> chunks;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string>, std::optional<std::string>) {}),
      "gpt-4o-mini", CustomEndpointLimits{},
      base::BindLambdaForTesting(
          [&](const std::string& so_far, const std::string& reasoning) {
            chunks.emplace_back(so_far, reasoning);
          }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"content\":\"Just an answer.\"}}]}\n\n",
            &total_bytes_written);
  FinishStream(&factory, &producer, &task_environment, total_bytes_written);

  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0],
            std::make_pair(std::string("Just an answer."), std::string()));
}

TEST(AtriumAiBackendCustomEndpointTest, AStalledStreamFailsOnTheIdleDeadline) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment,
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n",
            &total_bytes_written);
  ASSERT_FALSE(answered);

  task_environment.FastForwardBy(base::Seconds(31));

  ASSERT_TRUE(answered);
  EXPECT_FALSE(answer.has_value());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, "The custom endpoint stopped responding.");
}

// Regression: a real free reasoning model took 105s with a
// genuine HTTP 200 the whole time. A total deadline fails this, an idle one
// reset on every chunk must not.
TEST(AtriumAiBackendCustomEndpointTest,
    ASlowButAliveStreamOutlivesTheOldTotalDeadline) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  const char* kPieces[] = {
      "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n",
      "data: {\"choices\":[{\"delta\":{\"content\":\"lo \"}}]}\n\n",
      "data: {\"choices\":[{\"delta\":{\"content\":\"wor\"}}]}\n\n",
      "data: {\"choices\":[{\"delta\":{\"content\":\"ld!\"}}]}\n\n",
      "data: [DONE]\n\n",
  };
  int64_t total_bytes_written = 0;
  for (const char* piece : kPieces) {
    WriteChunk(producer, &task_environment, piece, &total_bytes_written);
    ASSERT_FALSE(answered);
    task_environment.FastForwardBy(base::Seconds(20));
  }
  FinishStream(&factory, &producer, &task_environment, total_bytes_written);

  ASSERT_TRUE(answered);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(*answer, "Hello world!");
  EXPECT_FALSE(error.has_value());
}

TEST(AtriumAiBackendCustomEndpointTest, TheAccumulatedSizeBackstopTrips) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestNetworkContextWithHostResolver network_context(
      net::HostResolver::CreateStandaloneResolver(
          net::NetLog::Get(), /*options=*/std::nullopt,
          "MAP custom-endpoint.test 93.184.216.34",
          /*enable_caching=*/false));
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaCustomEndpoint(
      "https://custom-endpoint.test/v1/chat/completions", std::nullopt,
      "page text", "", &factory, &network_context,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }),
      "gpt-4o-mini");
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer, /*pipe_capacity_bytes=*/
             1024 * 1024);
  const std::string oversized_chunk(400000, 'x');
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment, oversized_chunk,
            &total_bytes_written);
  WriteChunk(producer, &task_environment, oversized_chunk,
            &total_bytes_written);
  WriteChunk(producer, &task_environment, oversized_chunk,
            &total_bytes_written);

  ASSERT_TRUE(answered);
  EXPECT_FALSE(answer.has_value());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, "The custom endpoint's response was too large.");
}

}  // namespace
}  // namespace atrium_ai
