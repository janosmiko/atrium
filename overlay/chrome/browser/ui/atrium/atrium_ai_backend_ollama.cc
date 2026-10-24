// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ai_backend_ollama.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_ai_chat_prompt.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace atrium_ai {
namespace {

// A summary this size would already have blown past kMaxSummarizeInputChars
// on the way in - this is a response-side backstop against a runaway or
// misbehaving server, not a real expected size.
constexpr size_t kMaxOllamaResponseBytes = 1024 * 1024;

// An idle deadline, not a total one - see kCustomEndpointIdleTimeout's
// sibling comment in atrium_ai_backend_custom_endpoint.cc.
constexpr base::TimeDelta kOllamaIdleTimeout = base::Seconds(30);

// Owns the in-flight loader and answers `callback` exactly once. Self-deletes
// via Finish() rather than a member on AtriumBrowserWindow, since this
// backend has no long-lived owner to hang a member field on.
class OllamaRequest : public network::SimpleURLLoaderStreamConsumer {
 public:
  static void Start(const std::string& host,
                    int port,
                    const std::string& model,
                    const std::string& page_text,
                    const std::string& question,
                    network::mojom::URLLoaderFactory* loader_factory,
                    OllamaChatCallback callback,
                    AiChunkCallback on_chunk) {
    (new OllamaRequest(std::move(callback), std::move(on_chunk)))
        ->Run(host, port, model, page_text, question, loader_factory);
  }

 private:
  OllamaRequest(OllamaChatCallback callback, AiChunkCallback on_chunk)
      : callback_(std::move(callback)), on_chunk_(std::move(on_chunk)) {}

  void Run(const std::string& host,
          int port,
          const std::string& model,
          const std::string& page_text,
          const std::string& question,
          network::mojom::URLLoaderFactory* loader_factory) {
    auto resource_request = std::make_unique<network::ResourceRequest>();
    resource_request->url = GURL(BuildOllamaChatUrl(host, port));
    resource_request->method = "POST";
    resource_request->load_flags = net::LOAD_DISABLE_CACHE;
    resource_request->credentials_mode =
        network::mojom::CredentialsMode::kOmit;

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("atrium_ollama_summarize", R"(
          semantics {
            sender: "Atrium AI Panel - Ollama Backend"
            description:
              "Sends the active tab's visible text to the user's own Ollama "
              "instance (Settings > AI > Ollama host/port) for a summary or "
              "an answer to the user's question about the page."
            trigger: "The user asks the AI panel to summarize the page, or "
              "asks it a question, while Ollama is the selected model."
            data: "The active tab's clamped visible text, and the user's "
              "typed question when there is one."
            destination: OTHER
          }
          policy {
            cookies_allowed: NO
            setting: "Only reached while AI is enabled and an Ollama host "
              "and port are configured."
            policy_exception_justification:
                "Not implemented - the destination is a host:port the user "
                "typed themselves for their own hardware, same posture as "
                "the existing connectivity probe."
          })");
    loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                               traffic_annotation);
    loader_->AttachStringForUpload(
        BuildOllamaChatRequestBody(model, page_text, question),
        "application/json");
    loader_->SetOnRedirectCallback(
        base::BindRepeating(&OllamaRequest::OnRedirect,
                            base::Unretained(this)));
    idle_timer_.Start(
        FROM_HERE, kOllamaIdleTimeout,
        base::BindOnce(&OllamaRequest::OnIdleTimeout, base::Unretained(this)));
    loader_->DownloadAsStream(loader_factory, this);
  }

  // Refuses rather than follows, same posture as the custom-endpoint probe.
  void OnRedirect(const GURL& url_before_redirect,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* removed_headers) {
    Finish(std::nullopt, "Ollama's response redirected elsewhere.");
  }

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view chunk,
                      base::OnceClosure resume) override {
    if (!callback_) {
      std::move(resume).Run();
      return;
    }
    idle_timer_.Reset();
    total_bytes_received_ += chunk.size();
    if (total_bytes_received_ > kMaxOllamaResponseBytes) {
      Finish(std::nullopt, "Ollama's response was too large.");
      return;
    }
    stream_buffer_.append(chunk);
    ConsumeCompleteLines();
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    if (!callback_) {
      return;  // OnRedirect, the idle deadline, or the size cap already answered.
    }
    const int response_code =
        loader_->ResponseInfo() && loader_->ResponseInfo()->headers
            ? loader_->ResponseInfo()->headers->response_code()
            : 0;
    if (response_code == 0) {
      Finish(std::nullopt, "Could not reach the Ollama endpoint.");
      return;
    }
    // Before the success check: a non-2xx response never reaches
    // OnDataReceived, so a missing-model 404 would read as unreachable.
    if (response_code < 200 || response_code >= 300) {
      Finish(std::nullopt,
            "Ollama answered " + base::NumberToString(response_code) + ".");
      return;
    }
    if (!success) {
      Finish(std::nullopt, "Could not reach the Ollama endpoint.");
      return;
    }
    if (accumulated_answer_.empty()) {
      Finish(std::nullopt, "Ollama's response wasn't in the expected shape.");
      return;
    }
    Finish(std::move(accumulated_answer_), std::nullopt);
  }

  void OnRetry(base::OnceClosure start_retry) override {
    // Never invoked - SetRetryOptions is never called on `loader_`.
    NOTREACHED();
  }

  void OnIdleTimeout() { Finish(std::nullopt, "Ollama stopped responding."); }

  // A chunk may split mid-UTF-8-sequence but never mid-line-ending, since
  // '\n' cannot occur inside a multi-byte sequence - safe to buffer by line.
  void ConsumeCompleteLines() {
    size_t line_start = 0;
    while (true) {
      const size_t newline = stream_buffer_.find('\n', line_start);
      if (newline == std::string::npos) {
        break;
      }
      std::string_view line(stream_buffer_);
      line = line.substr(line_start, newline - line_start);
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }
      ProcessNdjsonLine(line);
      line_start = newline + 1;
    }
    stream_buffer_.erase(0, line_start);
  }

  void ProcessNdjsonLine(std::string_view line) {
    if (line.empty()) {
      return;
    }
    const std::optional<base::DictValue> parsed =
        base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
    if (!parsed) {
      return;
    }
    const base::DictValue* message = parsed->FindDict("message");
    if (!message) {
      return;
    }
    const std::string* content = message->FindString("content");
    if (content && !content->empty()) {
      accumulated_answer_.append(*content);
      // the answer so far, not the delta - the panel replaces what
      // it shows, so a dropped push costs a frame rather than a hole. Ollama's
      // /api/chat has no reasoning field, so the fold stays empty here.
      if (on_chunk_) {
        on_chunk_.Run(accumulated_answer_, std::string());
      }
    }
  }

  void Finish(std::optional<std::string> answer,
             std::optional<std::string> error) {
    idle_timer_.Stop();
    std::move(callback_).Run(std::move(answer), std::move(error));
    delete this;
  }

  std::unique_ptr<network::SimpleURLLoader> loader_;
  OllamaChatCallback callback_;
  AiChunkCallback on_chunk_;
  base::OneShotTimer idle_timer_;
  std::string stream_buffer_;
  std::string accumulated_answer_;
  size_t total_bytes_received_ = 0;
};

}  // namespace

bool IsOllamaConfigured(const std::string& host, int port) {
  return !host.empty() && port > 0 && port <= 65535;
}

std::string BuildOllamaChatUrl(const std::string& host, int port) {
  return base::StringPrintf("http://%s:%d/api/chat", host.c_str(), port);
}

std::string BuildOllamaChatRequestBody(const std::string& model,
                                       const std::string& page_text,
                                       const std::string& question) {
  base::DictValue body;
  body.Set("model", model);
  body.Set("messages", BuildChatMessages(page_text, question));
  body.Set("stream", true);

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(body)), &json);
  return json;
}

std::optional<std::string> ParseOllamaChatResponse(
    const std::string& response_body) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(response_body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const base::DictValue* message = parsed->FindDict("message");
  if (!message) {
    return std::nullopt;
  }
  const std::string* content = message->FindString("content");
  if (!content) {
    return std::nullopt;
  }
  return *content;
}

void ChatViaOllama(const std::string& host,
                   int port,
                   const std::string& model,
                   const std::string& page_text,
                   const std::string& question,
                   network::mojom::URLLoaderFactory* loader_factory,
                   OllamaChatCallback callback,
                   AiChunkCallback on_chunk) {
  if (!IsOllamaConfigured(host, port)) {
    std::move(callback).Run(std::nullopt, "Ollama host/port isn't set.");
    return;
  }
  OllamaRequest::Start(host, port, model, page_text, question, loader_factory,
                      std::move(callback), std::move(on_chunk));
}

}  // namespace atrium_ai
