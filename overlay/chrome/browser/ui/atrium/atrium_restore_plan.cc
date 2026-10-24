// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_restore_plan.cc
#include "chrome/browser/ui/atrium/atrium_restore_plan.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/values.h"

namespace atrium {

RestorePlan::RestorePlan() = default;
RestorePlan::RestorePlan(RestorePlan&&) = default;
RestorePlan& RestorePlan::operator=(RestorePlan&&) = default;
RestorePlan::~RestorePlan() = default;

RestorePlan ParseRestorePlan(std::string_view json) {
  RestorePlan plan;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return plan;
  }
  plan.parsed = true;
  const base::DictValue& dict = parsed->GetDict();

  if (const base::ListValue* closing = dict.FindList("closeTabIds")) {
    for (const base::Value& id : *closing) {
      // The ids come back as numbers from the core and as strings from
      // CloseSidebarTab's point of view; one conversion at the seam rather
      // than two shapes of id travelling further.
      if (id.is_double() || id.is_int()) {
        plan.close_tab_ids.push_back(static_cast<uint64_t>(id.GetDouble()));
      }
    }
  }

  if (const base::ListValue* reopening = dict.FindList("reopen")) {
    for (const base::Value& row : *reopening) {
      if (!row.is_dict()) {
        continue;
      }
      const base::DictValue& entry = row.GetDict();
      const std::string* url = entry.FindString("url");
      if (!url || url->empty()) {
        continue;
      }
      RestoreReopenStep step;
      step.url = *url;
      step.selected = entry.FindBool("selected").value_or(false);
      step.space = static_cast<size_t>(entry.FindDouble("space").value_or(0));
      plan.reopen.push_back(std::move(step));
    }
  }

  if (std::optional<double> select = dict.FindDouble("selectTabId")) {
    plan.select_tab_id = static_cast<uint64_t>(*select);
  }

  plan.active_space =
      static_cast<size_t>(dict.FindDouble("activeSpace").value_or(0));

  return plan;
}

std::vector<RestoreReopenStep> ReopenRowsForActiveSpace(
    const RestorePlan& plan) {
  std::vector<RestoreReopenStep> rows;
  for (const RestoreReopenStep& row : plan.reopen) {
    if (row.space == plan.active_space) {
      rows.push_back(row);
    }
  }
  return rows;
}

}  // namespace atrium
