// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_ROUTE_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_ROUTE_DECISION_H_

#include <string>

namespace atrium_ai {

// Which backend the "aiModel" settings value routes a summarize request to,
// once atrium::DecideSummarizeAction has already said kCallModel.
enum class RouteTarget {
  kOnDevice,
  kOllama,
  kCustomEndpoint,
};

// Unrecognised values fail closed to on-device, same posture as settings.rs's
// own one_of ladder for aiModel.
RouteTarget SelectSummarizeBackend(const std::string& ai_model);

// atrium::SummarizeOutcome::kUnavailable's message, worded per backend so
// "customEndpoint" (not wired yet) doesn't show the
// on-device-specific wording.
std::string UnavailableSummarizeError(const std::string& ai_model);

}  // namespace atrium_ai

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_ROUTE_DECISION_H_
