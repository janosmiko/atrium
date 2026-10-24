// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_CUSTOM_ENDPOINT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_CUSTOM_ENDPOINT_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/atrium/atrium_ai_chat_prompt.h"

namespace net {
class IPAddress;
}  // namespace net

namespace network {
class SimpleURLLoader;
namespace mojom {
class URLLoaderFactory;
class NetworkContext;
}  // namespace mojom
}  // namespace network

namespace atrium_ai {

// Its own alias, not atrium_ai::OllamaChatCallback - no dependency on
// that source set.
using CustomEndpointChatCallback =
    base::OnceCallback<void(std::optional<std::string> answer,
                             std::optional<std::string> error)>;

// The advanced Settings > AI rows, or zero for "the user left it alone".
// One struct rather than two more parameters on an already long call.
struct CustomEndpointLimits {
  int max_tokens = 0;
  int idle_timeout_seconds = 0;
};

// Reused as the model_available argument to atrium::DecideSummarizeAction.
bool IsCustomEndpointConfigured(const std::string& url);

// Test-only: any reachable test server resolves local, so
// CustomEndpointRefusesResolvedAddresses always refuses before a live round
// trip can show the model on the wire. This observes it one hop earlier.
void SetCustomEndpointModelObserverForTesting(
    base::RepeatingCallback<void(const std::string&)> observer);

// OpenAI /v1/chat/completions shape - the common denominator most
// self-hosted and third-party BYOK endpoints already speak. An unset
// `max_tokens` sends no cap: an endpoint that does not know it can reject it.
std::string BuildCustomEndpointChatRequestBody(
    const std::string& model,
    const std::string& page_text,
    const std::string& question,
    std::optional<int> max_tokens = std::nullopt);

// `reasoning` is the model's working-out, kept apart from the
// answer so the panel can fold it away. `answer` falls back to it when a cap
// stopped the model before it wrote one, and `reasoning` is then empty.
struct CustomEndpointReply {
  std::string answer;
  std::string reasoning;
};

// Reads `choices[0].message`. nullopt on any unrecognized shape - never an
// empty-string answer.
std::optional<CustomEndpointReply> ParseCustomEndpointChatResponse(
    const std::string& response_body);

// The exact check CustomEndpointRequest::OnHostResolved makes before it ever
// creates a SimpleURLLoader - exposed so :unit_tests can assert this path's
// own SSRF refusal without spinning up a NetworkContext to resolve a host.
bool CustomEndpointRefusesResolvedAddresses(
    const std::vector<net::IPAddress>& addresses);

// Unlike Ollama, refuses to connect when the resolved address is local
//. `api_key` is the keychain's plaintext value, if any.
void ChatViaCustomEndpoint(const std::string& url,
                           const std::optional<std::string>& api_key,
                           const std::string& page_text,
                           const std::string& question,
                           network::mojom::URLLoaderFactory* loader_factory,
                           network::mojom::NetworkContext* network_context,
                           CustomEndpointChatCallback callback,
                           const std::string& model,
                           CustomEndpointLimits limits = {},
                           AiChunkCallback on_chunk = base::NullCallback());

}  // namespace atrium_ai

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_CUSTOM_ENDPOINT_H_
