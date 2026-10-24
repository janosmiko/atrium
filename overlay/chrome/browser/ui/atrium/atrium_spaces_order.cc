// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_spaces_order.cc
#include "chrome/browser/ui/atrium/atrium_spaces_order.h"

#include <algorithm>
#include <optional>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace atrium {

std::string SpacesInSidebarOrder(const std::string& spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return spaces_json;
  }
  base::ListValue* spaces = parsed->GetDict().FindList("spaces");
  if (!spaces) {
    return spaces_json;
  }
  // Stable, so two spaces sharing an `order` keep the file's own sequence
  // rather than swapping on every push. A space written before the field
  // existed sorts to the front, which is where the core's own sort puts it.
  std::stable_sort(spaces->begin(), spaces->end(),
                   [](const base::Value& a, const base::Value& b) {
                     const base::DictValue* left = a.GetIfDict();
                     const base::DictValue* right = b.GetIfDict();
                     return (left ? left->FindInt("order").value_or(0) : 0) <
                            (right ? right->FindInt("order").value_or(0) : 0);
                   });
  std::string out;
  return base::JSONWriter::Write(*parsed, &out) ? out : spaces_json;
}

}  // namespace atrium
