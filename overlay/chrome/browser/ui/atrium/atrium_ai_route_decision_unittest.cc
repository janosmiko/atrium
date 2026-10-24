// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_ai_route_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_ai_route_decision.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium_ai {
namespace {

TEST(AtriumAiRouteDecisionTest, OllamaSelectsTheOllamaBackend) {
  EXPECT_EQ(SelectSummarizeBackend("ollama"), RouteTarget::kOllama);
}

TEST(AtriumAiRouteDecisionTest, OnDeviceSelectsTheOnDeviceBackend) {
  EXPECT_EQ(SelectSummarizeBackend("onDevice"), RouteTarget::kOnDevice);
}

TEST(AtriumAiRouteDecisionTest, CustomEndpointSelectsTheCustomEndpointBackend) {
  EXPECT_EQ(SelectSummarizeBackend("customEndpoint"),
           RouteTarget::kCustomEndpoint);
}

// An unrecognised string should never happen once settings.rs's one_of
// ladder has run - fails closed to on-device rather than silently doing
// nothing.
TEST(AtriumAiRouteDecisionTest, UnrecognisedValueFailsClosedToOnDevice) {
  EXPECT_EQ(SelectSummarizeBackend(""), RouteTarget::kOnDevice);
  EXPECT_EQ(SelectSummarizeBackend("bogus"), RouteTarget::kOnDevice);
}

TEST(AtriumAiRouteDecisionTest, OllamaUnavailableErrorNamesOllama) {
  EXPECT_EQ(UnavailableSummarizeError("ollama"),
           "Ollama isn't configured. Add a host and port in Settings > AI.");
}

TEST(AtriumAiRouteDecisionTest, CustomEndpointUnavailableErrorIsExplicit) {
  EXPECT_EQ(UnavailableSummarizeError("customEndpoint"),
           "Custom endpoint isn't configured. Add an endpoint URL in "
           "Settings > AI.");
}

TEST(AtriumAiRouteDecisionTest, OnDeviceUnavailableErrorNamesTheMac) {
  EXPECT_EQ(UnavailableSummarizeError("onDevice"),
           "On-device summarize isn't available on this Mac.");
}

// The three messages must stay distinguishable from each other - a shared
// generic message would defeat the point of having three ai_model branches.
TEST(AtriumAiRouteDecisionTest, TheThreeUnavailableMessagesAreAllDifferent) {
  const std::string ollama = UnavailableSummarizeError("ollama");
  const std::string custom = UnavailableSummarizeError("customEndpoint");
  const std::string on_device = UnavailableSummarizeError("onDevice");
  EXPECT_NE(ollama, custom);
  EXPECT_NE(ollama, on_device);
  EXPECT_NE(custom, on_device);
}

}  // namespace
}  // namespace atrium_ai
