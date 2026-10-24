// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"

#include "base/no_destructor.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

namespace atrium {
namespace {

std::string& RulesJSON() {
  static base::NoDestructor<std::string> rules;
  return *rules;
}

bool g_routing_bypass = false;

}  // namespace

void SetRoutingRulesJSON(std::string rules_json) {
  RulesJSON() = std::move(rules_json);
}

std::optional<std::string> RouteForURL(const GURL& url,
                                       const std::string& source_space_id) {
  if (RulesJSON().empty() || !url.is_valid()) {
    return std::nullopt;
  }
  char* raw = atrium_core_route_for_url(
      RulesJSON().c_str(), url.spec().c_str(),
      source_space_id.empty() ? nullptr : source_space_id.c_str());
  if (!raw) {
    return std::nullopt;
  }
  std::string space(raw);
  atrium_core_string_free(raw);
  if (space.empty()) {
    return std::nullopt;
  }
  return space;
}

ScopedRoutingBypass::ScopedRoutingBypass() : previous_(g_routing_bypass) {
  g_routing_bypass = true;
}

ScopedRoutingBypass::~ScopedRoutingBypass() {
  // Restores rather than clears, so a nested scope cannot end the outer one -
  // a routed reopen that itself opens a tab would otherwise leave the next
  // navigation exposed.
  g_routing_bypass = previous_;
}

bool ScopedRoutingBypass::IsActive() {
  return g_routing_bypass;
}

}  // namespace atrium
