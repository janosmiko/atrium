// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_ai_backend_ollama_unittest.cc
#include "chrome/browser/ui/atrium/atrium_ai_backend_ollama.h"

#include <string>

#include "base/containers/span.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium_ai {
namespace {

TEST(AtriumAiBackendOllamaTest, EmptyHostIsNotConfigured) {
  EXPECT_FALSE(IsOllamaConfigured("", 11434));
}

TEST(AtriumAiBackendOllamaTest, ZeroPortIsNotConfigured) {
  EXPECT_FALSE(IsOllamaConfigured("localhost", 0));
}

TEST(AtriumAiBackendOllamaTest, PortAbove65535IsNotConfigured) {
  EXPECT_FALSE(IsOllamaConfigured("localhost", 65536));
}

TEST(AtriumAiBackendOllamaTest, HostAndInRangePortIsConfigured) {
  EXPECT_TRUE(IsOllamaConfigured("localhost", 11434));
  EXPECT_TRUE(IsOllamaConfigured("192.168.1.5", 1));
  EXPECT_TRUE(IsOllamaConfigured("my-mac.local", 65535));
}

// reuse the same kDisabled > kUnavailable > kEmptyPage >
// kCallModel gate the on-device backend uses, with IsOllamaConfigured as the
// model_available argument, rather than a second decision function.
TEST(AtriumAiBackendOllamaTest, UnconfiguredOllamaIsUnavailable) {
  EXPECT_EQ(atrium::DecideSummarizeAction(/*ai_enabled=*/true,
                                          IsOllamaConfigured("", 0),
                                          "some page text"),
           atrium::SummarizeOutcome::kUnavailable);
}

TEST(AtriumAiBackendOllamaTest, ConfiguredOllamaWithTextCallsModel) {
  EXPECT_EQ(atrium::DecideSummarizeAction(
                /*ai_enabled=*/true, IsOllamaConfigured("localhost", 11434),
                "some page text"),
           atrium::SummarizeOutcome::kCallModel);
}

TEST(AtriumAiBackendOllamaTest, BuildsTheChatUrlFromHostAndPort) {
  EXPECT_EQ(BuildOllamaChatUrl("localhost", 11434),
           "http://localhost:11434/api/chat");
  EXPECT_EQ(BuildOllamaChatUrl("192.168.1.5", 8080),
           "http://192.168.1.5:8080/api/chat");
}

TEST(AtriumAiBackendOllamaTest, RequestBodyCarriesModelAndPageTextAsMessage) {
  const std::string body =
      BuildOllamaChatRequestBody("llama3", "hello page", "");
  EXPECT_NE(body.find("\"model\":\"llama3\""), std::string::npos);
  EXPECT_NE(body.find("hello page"), std::string::npos);
  EXPECT_NE(body.find("\"stream\":true"), std::string::npos);
  EXPECT_NE(body.find("\"role\":\"user\""), std::string::npos);
}

// a model other than the pre-task default ("llama3")
// rules out an implementation that quietly ignores the argument.
TEST(AtriumAiBackendOllamaTest, RequestBodyCarriesTheChosenModel) {
  const std::string body =
      BuildOllamaChatRequestBody("llama3:70b-instruct", "hello page", "");
  EXPECT_NE(body.find("\"model\":\"llama3:70b-instruct\""), std::string::npos);
}

TEST(AtriumAiBackendOllamaTest, RequestBodyEscapesPageTextSafely) {
  const std::string body =
      BuildOllamaChatRequestBody("llama3", "quote \" and \\ backslash", "");
  // Valid JSON with the hostile characters escaped, not raw.
  EXPECT_EQ(body.find("\"quote \" and"), std::string::npos);
}

TEST(AtriumAiBackendOllamaTest, AQuestionReachesTheRequestBody) {
  const std::string body =
      BuildOllamaChatRequestBody("llama3", "hello page", "who wrote this?");
  EXPECT_NE(body.find("who wrote this?"), std::string::npos);
  EXPECT_NE(body.find("\"role\":\"system\""), std::string::npos);
  EXPECT_EQ(body.find("Summarize this page"), std::string::npos);
}

TEST(AtriumAiBackendOllamaTest, ParsesMessageContentFromChatResponse) {
  std::optional<std::string> summary = ParseOllamaChatResponse(
      R"({"model":"llama3","message":{"role":"assistant","content":)"
      R"("a real summary"},"done":true})");
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(*summary, "a real summary");
}

TEST(AtriumAiBackendOllamaTest, RejectsNonJsonResponse) {
  EXPECT_FALSE(ParseOllamaChatResponse("not json at all").has_value());
}

TEST(AtriumAiBackendOllamaTest, RejectsJsonMissingMessageField) {
  EXPECT_FALSE(ParseOllamaChatResponse(R"({"model":"llama3","done":true})")
                  .has_value());
}

TEST(AtriumAiBackendOllamaTest, RejectsMessageMissingContentField) {
  EXPECT_FALSE(
      ParseOllamaChatResponse(R"({"message":{"role":"assistant"}})")
          .has_value());
}

// A model name Ollama does not have answers 404 with a JSON error object,
// which the shape check would otherwise report as a malformed response.
TEST(AtriumAiBackendOllamaTest, AMissingModelNamesTheStatusNotTheShape) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting([&](std::optional<std::string> result_answer,
                                     std::optional<std::string> result_error) {
        answer = std::move(result_answer);
        error = std::move(result_error);
      }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);
  factory.SimulateResponseForPendingRequest(
      GURL(BuildOllamaChatUrl("localhost", 11434)),
      network::URLLoaderCompletionStatus(net::OK),
      network::CreateURLResponseHead(net::HTTP_NOT_FOUND),
      R"({"error":"model 'llama3' not found"})");
  task_environment.RunUntilIdle();

  EXPECT_FALSE(answer.has_value());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, "Ollama answered 404.");
}

TEST(AtriumAiBackendOllamaTest, AHungEndpointTimesOutAndReportsAnError) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> summary;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_summary,
              std::optional<std::string> result_error) {
            summary = std::move(result_summary);
            error = std::move(result_error);
            answered = true;
          }));
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

TEST(AtriumAiBackendOllamaTest, ChunksSplitMidLineReassembleIntoTheFullAnswer) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  const std::string full_body =
      "{\"message\":{\"role\":\"assistant\",\"content\":\"Hello\"}}\n"
      "{\"message\":{\"role\":\"assistant\",\"content\":\" world\"},"
      "\"done\":true}\n";
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

TEST(AtriumAiBackendOllamaTest, AStalledStreamFailsOnTheIdleDeadline) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  int64_t total_bytes_written = 0;
  WriteChunk(producer, &task_environment,
            "{\"message\":{\"role\":\"assistant\",\"content\":\"Hel\"}}\n",
            &total_bytes_written);
  ASSERT_FALSE(answered);

  task_environment.FastForwardBy(base::Seconds(31));

  ASSERT_TRUE(answered);
  EXPECT_FALSE(answer.has_value());
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(*error, "Ollama stopped responding.");
}

// Regression: a total deadline fails a slow-but-alive stream,
// an idle one reset on every chunk must not.
TEST(AtriumAiBackendOllamaTest, ASlowButAliveStreamOutlivesTheOldTotalDeadline) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }));
  task_environment.RunUntilIdle();
  ASSERT_EQ(factory.NumPending(), 1);

  mojo::ScopedDataPipeProducerHandle producer;
  StartStream(&factory, net::HTTP_OK, &producer);
  const char* kPieces[] = {
      "{\"message\":{\"role\":\"assistant\",\"content\":\"Hel\"}}\n",
      "{\"message\":{\"role\":\"assistant\",\"content\":\"lo \"}}\n",
      "{\"message\":{\"role\":\"assistant\",\"content\":\"wor\"}}\n",
      "{\"message\":{\"role\":\"assistant\",\"content\":\"ld!\"},\"done\":true}\n",
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

TEST(AtriumAiBackendOllamaTest, TheAccumulatedSizeBackstopTrips) {
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::IO);
  network::TestURLLoaderFactory factory;
  std::optional<std::string> answer;
  std::optional<std::string> error;
  bool answered = false;
  ChatViaOllama(
      "localhost", 11434, "llama3", "page text", "", &factory,
      base::BindLambdaForTesting(
          [&](std::optional<std::string> result_answer,
              std::optional<std::string> result_error) {
            answer = std::move(result_answer);
            error = std::move(result_error);
            answered = true;
          }));
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
  EXPECT_EQ(*error, "Ollama's response was too large.");
}

}  // namespace
}  // namespace atrium_ai
