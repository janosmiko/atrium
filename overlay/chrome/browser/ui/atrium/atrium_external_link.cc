// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_external_link.cc
#include "chrome/browser/ui/atrium/atrium_external_link.h"

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/values.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

// Process-wide, UI thread only. The seed post writes it once at startup and
// the settings surface writes it again on a save; openURLs reads it.
bool& OpensLittle() {
  // A settings file without the key reads true - the core's answer for the
  // same file, and the value a profile that has never seen the row runs on.
  static bool enabled = true;
  return enabled;
}

}  // namespace

std::optional<bool> ExternalLinksOpenInLittleFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  // FindBool rather than Find, for ConfirmToQuitFromSettings' reason: a field
  // carrying a string or a number is a settings file this build cannot
  // honour, and guessing at it would reroute every external link for someone
  // who never asked.
  return parsed->GetDict().FindBool("externalLinksOpenInLittle");
}

bool ExternalLinksOpenInLittle() {
  return OpensLittle();
}

void SetExternalLinksOpenInLittle(bool enabled) {
  OpensLittle() = enabled;
  // Two whole lines rather than one with the word interpolated, so a scenario
  // asserting on either can find it in the source - the rule
  // build/scenarios/lib/parse.py checks.
  if (enabled) {
    VLOG(1) << "Atrium: external links open in a little window";
  } else {
    VLOG(1) << "Atrium: external links open in a space window";
  }
}

bool ExternalLinkOpensLittleWindow(bool setting_enabled,
                                   bool has_url,
                                   bool is_http_or_https) {
  const AtriumExternalLinkInputs input{
      .external_links_open_in_little = setting_enabled,
      .has_url = has_url,
      .is_http_or_https = is_http_or_https,
  };
  return atrium_core_external_link_target(&input) ==
         ATRIUM_EXTERNAL_LINK_TARGET_LITTLE_WINDOW;
}

}  // namespace atrium
