// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_load_phase_script.h"

#include <algorithm>

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"

namespace atrium {

namespace {

const char* PhaseName(LoadPhase phase) {
  switch (phase) {
    case LoadPhase::kWaiting:
      return "waiting";
    case LoadPhase::kReceiving:
      return "receiving";
    case LoadPhase::kDone:
      return "done";
  }
}

}  // namespace

std::u16string BuildLoadPhaseScript(uint64_t tab_id,
                                    LoadPhase phase,
                                    double fraction) {
  const std::string body = base::StringPrintf(
      "window.atriumLoadPhase && atriumLoadPhase(%s, \"%s\", %.4f)",
      base::GetQuotedJSONString(base::NumberToString(tab_id)).c_str(),
      PhaseName(phase), std::clamp(fraction, 0.0, 1.0));
  return base::UTF8ToUTF16(body);
}

}  // namespace atrium
