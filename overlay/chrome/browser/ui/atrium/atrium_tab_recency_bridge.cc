// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_tab_recency_bridge.h"

#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

void RecordTabRecencyActivation(uint64_t tab_id) {
  atrium_core_tab_recency_record_activation(tab_id);
}

void RecordTabRecencyClosed(uint64_t tab_id) {
  atrium_core_tab_recency_record_closed(tab_id);
}

std::optional<uint64_t> StepTabRecencyCycle(int32_t direction) {
  const int64_t result = atrium_core_tab_recency_step(direction);
  if (result < 0) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(result);
}

void EndTabRecencyCycle() {
  atrium_core_tab_recency_end_cycle();
}

}  // namespace atrium
