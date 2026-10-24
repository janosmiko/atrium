// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_cdp_response.cc
#include "chrome/browser/ui/atrium/atrium_cdp_response.h"

#include <optional>
#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/values.h"

namespace atrium {

CdpScreenshotResult::CdpScreenshotResult() = default;
CdpScreenshotResult::CdpScreenshotResult(CdpScreenshotOutcome outcome,
                                         std::vector<uint8_t> png_bytes)
    : outcome(outcome), png_bytes(std::move(png_bytes)) {}
CdpScreenshotResult::CdpScreenshotResult(CdpScreenshotResult&&) = default;
CdpScreenshotResult& CdpScreenshotResult::operator=(CdpScreenshotResult&&) =
    default;
CdpScreenshotResult::~CdpScreenshotResult() = default;

CdpScreenshotResult ParseCaptureScreenshotMessage(std::string_view message,
                                                  int request_id) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(message, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return {CdpScreenshotOutcome::kNotOurResponse, {}};
  }
  const base::DictValue& dict = parsed->GetDict();

  // Events (and responses to other requests) don't carry our "id" - ignore
  // them so the caller keeps waiting for the real response.
  std::optional<int> id = dict.FindInt("id");
  if (!id || *id != request_id) {
    return {CdpScreenshotOutcome::kNotOurResponse, {}};
  }

  if (dict.Find("error")) {
    return {CdpScreenshotOutcome::kFailed, {}};
  }

  const base::DictValue* result = dict.FindDict("result");
  if (!result) {
    return {CdpScreenshotOutcome::kFailed, {}};
  }

  const std::string* data = result->FindString("data");
  if (!data) {
    return {CdpScreenshotOutcome::kFailed, {}};
  }

  std::optional<std::vector<uint8_t>> decoded = base::Base64Decode(*data);
  if (!decoded) {
    return {CdpScreenshotOutcome::kFailed, {}};
  }

  return {CdpScreenshotOutcome::kSuccess, std::move(*decoded)};
}

}  // namespace atrium
