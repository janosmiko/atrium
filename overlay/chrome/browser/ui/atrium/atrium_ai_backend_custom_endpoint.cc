// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_ai_chat_prompt.h"
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/dns/public/resolve_error_info.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_host_resolver.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace atrium_ai {
namespace {

// Same backstop as the Ollama backend's kMaxOllamaResponseBytes.
constexpr size_t kMaxCustomEndpointResponseBytes = 1024 * 1024;

// An idle deadline, not a total one - a slow reasoning model can take well
// over 30s to finish while still sending, and only a truly stalled stream
// should trip this.
constexpr base::TimeDelta kCustomEndpointIdleTimeout = base::Seconds(30);

// Self-deletes via Finish() on every exit path.
class CustomEndpointRequest : public network::SimpleURLLoaderStreamConsumer {
 public:
  static void Start(const std::string& url,
                    std::optional<std::string> api_key,
                    const std::string& model,
                    const std::string& page_text,
                    const std::string& question,
                    network::mojom::URLLoaderFactory* loader_factory,
                    network::mojom::NetworkContext* network_context,
                    CustomEndpointChatCallback callback,
                    CustomEndpointLimits limits,
                    AiChunkCallback on_chunk) {
    (new CustomEndpointRequest(std::move(api_key), model, page_text, question,
                               loader_factory, std::move(callback), limits,
                               std::move(on_chunk)))
        ->Run(url, network_context);
  }

 private:
  CustomEndpointRequest(std::optional<std::string> api_key,
                       std::string model,
                       std::string page_text,
                       std::string question,
                       network::mojom::URLLoaderFactory* loader_factory,
                       CustomEndpointChatCallback callback,
                       CustomEndpointLimits limits,
                       AiChunkCallback on_chunk)
      : api_key_(std::move(api_key)),
        model_(std::move(model)),
        page_text_(std::move(page_text)),
        question_(std::move(question)),
        loader_factory_(loader_factory),
        callback_(std::move(callback)),
        limits_(limits),
        on_chunk_(std::move(on_chunk)) {}

  void Run(const std::string& url,
          network::mojom::NetworkContext* network_context) {
    GURL parsed(url);
    if (!parsed.is_valid() || !parsed.SchemeIs(url::kHttpsScheme)) {
      Finish(std::nullopt, "Custom endpoint URL isn't a valid https:// URL.");
      return;
    }
    endpoint_url_ = parsed;

    network::mojom::ResolveHostParametersPtr parameters =
        network::mojom::ResolveHostParameters::New();
    network::mojom::HostResolverHostPtr host =
        network::mojom::HostResolverHost::NewHostPortPair(
            net::HostPortPair::FromURL(parsed));
    resolver_ = network::SimpleHostResolver::Create(network_context);
    resolver_->ResolveHost(
        std::move(host), net::NetworkAnonymizationKey::CreateTransient(),
        std::move(parameters),
        base::BindOnce(&CustomEndpointRequest::OnHostResolved,
                       base::Unretained(this)));
  }

  void OnHostResolved(
      int result,
      const net::ResolveErrorInfo& resolve_error_info,
      const net::AddressList& resolved_addresses,
      const net::HostResolverEndpointResults& endpoint_results) {
    resolver_.reset();
    if (result != net::OK) {
      Finish(std::nullopt, "Could not resolve the custom endpoint host.");
      return;
    }
    std::vector<net::IPAddress> addresses;
    for (const net::IPEndPoint& endpoint : resolved_addresses.endpoints()) {
      addresses.push_back(endpoint.address());
    }
    if (CustomEndpointRefusesResolvedAddresses(addresses)) {
      Finish(std::nullopt, "That address is not reachable from here.");
      return;
    }
    SendRequest();
  }

  void SendRequest() {
    auto resource_request = std::make_unique<network::ResourceRequest>();
    resource_request->url = endpoint_url_;
    resource_request->method = "POST";
    resource_request->load_flags = net::LOAD_DISABLE_CACHE;
    resource_request->credentials_mode =
        network::mojom::CredentialsMode::kOmit;
    if (api_key_ && !api_key_->empty()) {
      resource_request->headers.SetHeader(
          net::HttpRequestHeaders::kAuthorization, "Bearer " + *api_key_);
    }

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation(
            "atrium_custom_endpoint_summarize", R"(
          semantics {
            sender: "Atrium AI Panel - Custom Endpoint Backend"
            description:
              "Sends the active tab's visible text to the user's own BYOK "
              "endpoint (Settings > AI > Endpoint) for a summary or an "
              "answer to the user's question about the page, with the "
              "keychain-stored API key as a bearer credential."
            trigger: "The user asks the AI panel to summarize the page, or "
              "asks it a question, while Custom endpoint is the selected "
              "model."
            data: "The active tab's clamped visible text, the user's typed "
              "question when there is one, and the stored API key, sent "
              "only to the endpoint URL the user configured."
            destination: OTHER
          }
          policy {
            cookies_allowed: NO
            setting: "Only reached while AI is enabled, a custom endpoint "
              "URL is set, and its resolved address is not local."
            policy_exception_justification:
                "Not implemented - the destination is a URL the user typed "
                "themselves, same posture as the existing connectivity "
                "probe."
          })");
    loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                               traffic_annotation);
    // the resolve above cannot bind this connection, because the
    // loader resolves again under its own key. This re-checks the address it
    // really reached, before the page text and the bearer key go out.
    loader_->SetURLLoaderFactoryOptions(
        network::mojom::kURLLoadOptionBlockLocalRequest);
    loader_->AttachStringForUpload(
        BuildCustomEndpointChatRequestBody(
            model_, page_text_, question_,
            limits_.max_tokens > 0 ? std::optional<int>(limits_.max_tokens)
                                   : std::nullopt),
        "application/json");
    // Refuses rather than follows - a redirect target never configured by
    // the user must not receive the bearer key.
    loader_->SetOnRedirectCallback(
        base::BindRepeating(&CustomEndpointRequest::OnRedirect,
                            base::Unretained(this)));
    idle_timer_.Start(FROM_HERE, IdleTimeout(),
                      base::BindOnce(&CustomEndpointRequest::OnIdleTimeout,
                                    base::Unretained(this)));
    loader_->DownloadAsStream(loader_factory_, this);
  }

  void OnRedirect(const GURL& url_before_redirect,
                  const net::RedirectInfo& redirect_info,
                  const network::mojom::URLResponseHead& response_head,
                  std::vector<std::string>* removed_headers) {
    Finish(std::nullopt,
          "The custom endpoint's response redirected elsewhere.");
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
    if (total_bytes_received_ > kMaxCustomEndpointResponseBytes) {
      Finish(std::nullopt, "The custom endpoint's response was too large.");
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
      Finish(std::nullopt, "Could not reach the custom endpoint.");
      return;
    }
    // Before the success check, not after: a non-2xx response never reaches
    // OnDataReceived (SetAllowHttpErrorResults defaults to false), so an
    // expired key's 401 would otherwise be reported as an unreachable host.
    if (response_code < 200 || response_code >= 300) {
      Finish(std::nullopt, "The custom endpoint answered " +
                               base::NumberToString(response_code) + ".");
      return;
    }
    if (!success) {
      Finish(std::nullopt, "Could not reach the custom endpoint.");
      return;
    }
    // A cap stops a reasoning model before it writes an answer, so its
    // working-out is the only text there is. The fold is emptied as it is
    // promoted, or the same text shows twice.
    if (accumulated_answer_.empty() && !accumulated_reasoning_.empty()) {
      accumulated_answer_ = std::move(accumulated_reasoning_);
      accumulated_reasoning_.clear();
      PushSoFar();
    }
    if (accumulated_answer_.empty()) {
      Finish(std::nullopt,
            "The custom endpoint's response wasn't in the expected shape.");
      return;
    }
    Finish(std::move(accumulated_answer_), std::nullopt);
  }

  void OnRetry(base::OnceClosure start_retry) override {
    // Never invoked - SetRetryOptions is never called on `loader_`.
    NOTREACHED();
  }

  void OnIdleTimeout() {
    Finish(std::nullopt, "The custom endpoint stopped responding.");
  }

  base::TimeDelta IdleTimeout() const {
    return limits_.idle_timeout_seconds > 0
               ? base::Seconds(limits_.idle_timeout_seconds)
               : kCustomEndpointIdleTimeout;
  }

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
      ProcessSseLine(line);
      line_start = newline + 1;
    }
    stream_buffer_.erase(0, line_start);
  }

  // two accumulators, not one. The reasoning is the model's
  // working-out, and folding it away later is only possible while the two are
  // still apart.
  void ProcessSseLine(std::string_view line) {
    static constexpr std::string_view kDataPrefix = "data: ";
    if (!base::StartsWith(line, kDataPrefix)) {
      return;
    }
    const std::string_view payload = line.substr(kDataPrefix.size());
    if (payload == "[DONE]") {
      return;
    }
    const std::optional<base::DictValue> parsed =
        base::JSONReader::ReadDict(payload, base::JSON_PARSE_RFC);
    if (!parsed) {
      return;
    }
    const base::ListValue* choices = parsed->FindList("choices");
    if (!choices || choices->empty() || !(*choices)[0].is_dict()) {
      return;
    }
    const base::DictValue* delta = (*choices)[0].GetDict().FindDict("delta");
    if (!delta) {
      return;
    }
    const std::string* content = delta->FindString("content");
    const std::string* reasoning = delta->FindString("reasoning");
    bool changed = false;
    if (content && !content->empty()) {
      accumulated_answer_.append(*content);
      changed = true;
    }
    // Not an else: a delta may carry both, and dropping the reasoning on those
    // would leave a fold that stops filling the moment the answer starts.
    if (reasoning && !reasoning->empty()) {
      accumulated_reasoning_.append(*reasoning);
      changed = true;
    }
    if (changed) {
      PushSoFar();
    }
  }

  // the text so far, not the delta - the panel replaces what it
  // shows, so a dropped push costs a frame rather than a hole.
  void PushSoFar() {
    if (on_chunk_) {
      on_chunk_.Run(accumulated_answer_, accumulated_reasoning_);
    }
  }

  void Finish(std::optional<std::string> answer,
             std::optional<std::string> error) {
    idle_timer_.Stop();
    std::move(callback_).Run(std::move(answer), std::move(error));
    delete this;
  }

  std::optional<std::string> api_key_;
  std::string model_;
  std::string page_text_;
  std::string question_;
  raw_ptr<network::mojom::URLLoaderFactory> loader_factory_;
  GURL endpoint_url_;
  std::unique_ptr<network::SimpleHostResolver> resolver_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  CustomEndpointChatCallback callback_;
  CustomEndpointLimits limits_;
  AiChunkCallback on_chunk_;
  base::OneShotTimer idle_timer_;
  std::string stream_buffer_;
  std::string accumulated_answer_;
  std::string accumulated_reasoning_;
  size_t total_bytes_received_ = 0;
};

base::RepeatingCallback<void(const std::string&)>&
GetCustomEndpointModelObserverForTesting() {
  static base::NoDestructor<base::RepeatingCallback<void(const std::string&)>>
      observer;
  return *observer;
}

}  // namespace

void SetCustomEndpointModelObserverForTesting(
    base::RepeatingCallback<void(const std::string&)> observer) {
  GetCustomEndpointModelObserverForTesting() = std::move(observer);
}

bool CustomEndpointRefusesResolvedAddresses(
    const std::vector<net::IPAddress>& addresses) {
  return atrium::AnyResolvedAddressIsLocal(addresses);
}

bool IsCustomEndpointConfigured(const std::string& url) {
  if (url.empty()) {
    return false;
  }
  GURL parsed(url);
  return parsed.is_valid() && parsed.SchemeIs(url::kHttpsScheme);
}

std::string BuildCustomEndpointChatRequestBody(const std::string& model,
                                               const std::string& page_text,
                                               const std::string& question,
                                               std::optional<int> max_tokens) {
  base::DictValue body;
  body.Set("model", model);
  body.Set("messages", BuildChatMessages(page_text, question));
  body.Set("stream", true);
  if (max_tokens && *max_tokens > 0) {
    body.Set("max_tokens", *max_tokens);
  }

  std::string json;
  base::JSONWriter::Write(base::Value(std::move(body)), &json);
  return json;
}

std::optional<CustomEndpointReply> ParseCustomEndpointChatResponse(
    const std::string& response_body) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(response_body, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  const base::ListValue* choices = parsed->FindList("choices");
  if (!choices || choices->empty() || !(*choices)[0].is_dict()) {
    return std::nullopt;
  }
  const base::DictValue* message = (*choices)[0].GetDict().FindDict("message");
  if (!message) {
    return std::nullopt;
  }
  const std::string* content = message->FindString("content");
  const std::string* reasoning = message->FindString("reasoning");
  if (content && !content->empty()) {
    return CustomEndpointReply{
        *content, reasoning ? *reasoning : std::string()};
  }
  // A reasoning model spends its budget on `reasoning` first, so a max_tokens
  // cap leaves the answer empty with the only text in there. Promoted, not
  // left behind a fold that would be the whole reply.
  if (reasoning && !reasoning->empty()) {
    return CustomEndpointReply{*reasoning, std::string()};
  }
  return std::nullopt;
}

void ChatViaCustomEndpoint(
    const std::string& url,
    const std::optional<std::string>& api_key,
    const std::string& page_text,
    const std::string& question,
    network::mojom::URLLoaderFactory* loader_factory,
    network::mojom::NetworkContext* network_context,
    CustomEndpointChatCallback callback,
    const std::string& model,
    CustomEndpointLimits limits,
    AiChunkCallback on_chunk) {
  if (!IsCustomEndpointConfigured(url)) {
    std::move(callback).Run(std::nullopt, "Custom endpoint URL isn't set.");
    return;
  }
  if (GetCustomEndpointModelObserverForTesting()) {
    GetCustomEndpointModelObserverForTesting().Run(model);
  }
  CustomEndpointRequest::Start(url, api_key, model, page_text, question,
                              loader_factory, network_context,
                              std::move(callback), limits,
                              std::move(on_chunk));
}

}  // namespace atrium_ai
