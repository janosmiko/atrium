// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_OLLAMA_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_OLLAMA_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/atrium/atrium_ai_chat_prompt.h"

namespace network {
class SimpleURLLoader;
namespace mojom {
class URLLoaderFactory;
}  // namespace mojom
}  // namespace network

namespace atrium_ai {

// Deliberately its own alias, not atrium_ai::SummarizeCallback - that one
// lives in the mac-only ai_backend_mac source set and this isn't mac-only.
using OllamaChatCallback =
    base::OnceCallback<void(std::optional<std::string> answer,
                             std::optional<std::string> error)>;

// Settings only accepts a host/port pair when both are non-empty and the
// port is in range - this is that same check, reused as the model_available
// argument to atrium::DecideSummarizeAction rather than a second gate.
bool IsOllamaConfigured(const std::string& host, int port);

// http://host:port/api/chat - Ollama's own chat endpoint, matching the GET
// /api/tags host:port pattern the reachability probe already uses.
std::string BuildOllamaChatUrl(const std::string& host, int port);

// The non-streaming /api/chat request body. `model` is whatever the caller
// resolved.
std::string BuildOllamaChatRequestBody(const std::string& model,
                                        const std::string& page_text,
                                        const std::string& question);

// Pulls `message.content` out of a non-streaming /api/chat response body.
// nullopt on any unrecognized shape - never an empty-string answer.
std::optional<std::string> ParseOllamaChatResponse(
    const std::string& response_body);

// Calls `callback` with exactly one of answer or error. Deliberately does
// not run atrium::AnyResolvedAddressIsLocal - see the .cc file for why.
void ChatViaOllama(const std::string& host,
                   int port,
                   const std::string& model,
                   const std::string& page_text,
                   const std::string& question,
                   network::mojom::URLLoaderFactory* loader_factory,
                   OllamaChatCallback callback,
                   AiChunkCallback on_chunk = base::NullCallback());

}  // namespace atrium_ai

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_OLLAMA_H_
