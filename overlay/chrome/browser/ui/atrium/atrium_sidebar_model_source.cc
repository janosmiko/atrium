// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_model_source.cc
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"

#include "base/command_line.h"
#include "base/json/string_escape.h"
#include "content/public/common/content_switches.h"
#include "base/strings/string_number_conversions.h"

namespace atrium {

SidebarModelSource ChooseSidebarModelSource(const std::string& dylib_model,
                                            bool store_file_readable) {
  if (!dylib_model.empty()) {
    return SidebarModelSource::kDylib;
  }
  if (store_file_readable) {
    return SidebarModelSource::kRustCore;
  }
  return SidebarModelSource::kFlatTabListOnly;
}

base::FilePath SpacesFilePath(const std::string& env_override,
                              const base::FilePath& app_support) {
  if (!env_override.empty()) {
    return base::FilePath(env_override);
  }
  // A temp --user-data-dir does not move DIR_APP_DATA, so a test that stages
  // no override would read and write the developer's live store.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kTestType)) {
    return base::FilePath();
  }
  if (app_support.empty()) {
    return base::FilePath();
  }
  return app_support.Append("Atrium").Append("spaces.json");
}

std::string LiveStripJSON(const std::vector<LiveTab>& tabs) {
  std::string out = "[";
  for (const LiveTab& tab : tabs) {
    if (out.size() > 1) {
      out += ",";
    }
    out += "{\"url\":";
    out += base::GetQuotedJSONString(tab.url);
    out += ",\"id\":";
    out += base::NumberToString(tab.id);
    out += ",\"active\":";
    out += tab.active ? "true" : "false";
    // Only when set: the common strip stays byte-identical to what every
    // reader of it was built against.
    if (!tab.claimed.empty()) {
      out += ",\"claimed\":";
      out += base::GetQuotedJSONString(tab.claimed);
    }
    if (tab.elsewhere) {
      out += ",\"elsewhere\":true";
    }
    out += "}";
  }
  out += "]";
  return out;
}

}  // namespace atrium
