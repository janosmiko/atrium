// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ai_route_decision.h"

namespace atrium_ai {

RouteTarget SelectSummarizeBackend(const std::string& ai_model) {
  if (ai_model == "ollama") {
    return RouteTarget::kOllama;
  }
  if (ai_model == "customEndpoint") {
    return RouteTarget::kCustomEndpoint;
  }
  return RouteTarget::kOnDevice;
}

std::string UnavailableSummarizeError(const std::string& ai_model) {
  if (ai_model == "ollama") {
    return "Ollama isn't configured. Add a host and port in Settings > AI.";
  }
  if (ai_model == "customEndpoint") {
    return "Custom endpoint isn't configured. Add an endpoint URL in "
          "Settings > AI.";
  }
  return "On-device summarize isn't available on this Mac.";
}

}  // namespace atrium_ai
