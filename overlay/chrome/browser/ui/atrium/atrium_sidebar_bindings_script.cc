// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_bindings_script.h"

#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/values.h"

namespace atrium {

std::string SidebarBindingsScript(
    const std::map<std::string, std::string>& glyphs_by_action) {
  base::DictValue bindings;
  for (const auto& [action, glyph] : glyphs_by_action) {
    bindings.Set(action, glyph);
  }
  std::string json;
  base::JSONWriter::Write(bindings, &json);
  return base::StrCat(
      {"window.atriumSetSidebarBindings && "
       "atriumSetSidebarBindings(",
       json, ")"});
}

}  // namespace atrium
