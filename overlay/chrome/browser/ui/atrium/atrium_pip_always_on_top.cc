// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pip_always_on_top.cc
#include "chrome/browser/ui/atrium/atrium_pip_always_on_top.h"

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"

namespace atrium {

std::optional<bool> PipAlwaysOnTopFromSettings(std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("pipAlwaysOnTop");
}

bool& PipAlwaysOnTop() {
  static bool always_on_top = true;
  return always_on_top;
}

void LoadPipAlwaysOnTopFromSettings(std::string_view settings_json) {
  if (std::optional<bool> value = PipAlwaysOnTopFromSettings(settings_json)) {
    PipAlwaysOnTop() = *value;
  }
}

namespace {

// The one success code atrium_core_save_settings answers with.
constexpr int32_t kPipAlwaysOnTopWritten = 1;

int32_t WritePipAlwaysOnTop(bool enabled) {
  const base::FilePath path = ResolveSettingsFilePath();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(LoadSettingsJSON(path), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return 0;
  }
  parsed->GetDict().Set("pipAlwaysOnTop", enabled);
  std::string json;
  if (!base::JSONWriter::Write(*parsed, &json)) {
    return 0;
  }
  return SaveSettingsJSON(path, json);
}

}  // namespace

void SavePipAlwaysOnTop(bool enabled) {
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&WritePipAlwaysOnTop, enabled),
      base::BindOnce([](int32_t code) {
        if (code == kPipAlwaysOnTopWritten) {
          VLOG(1) << "Atrium: mini player always-on-top written";
          return;
        }
        LOG(WARNING) << "Atrium: mini player always-on-top not written, code "
                     << code;
      }));
}

}  // namespace atrium
