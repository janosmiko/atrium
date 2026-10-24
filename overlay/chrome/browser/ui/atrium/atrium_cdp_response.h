// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_cdp_response.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CDP_RESPONSE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CDP_RESPONSE_H_

#include <cstdint>
#include <string_view>
#include <vector>

namespace atrium {

// pure JSON->bytes parsing for a Page.captureScreenshot CDP
// response, extracted so it's a fast, standalone gtest unit (capture_math's
// posture) - no content:: or DevToolsAgentHost dependency, just //base's
// JSON reader and base64 decoder.
enum class CdpScreenshotOutcome {
  // `message` isn't the response to our request (an unrelated event, or a
  // response carrying a different "id") - the caller should keep waiting.
  kNotOurResponse,
  // It is our response, but it failed: not a JSON object, carries an
  // "error" field, or its result.data is missing or not valid base64.
  kFailed,
  // It is our response and decoded successfully; `png_bytes` is populated.
  kSuccess,
};

struct CdpScreenshotResult {
  // Out-of-line special members: the chromium-style plugin requires them
  // for any struct with a non-trivial member (png_bytes).
  CdpScreenshotResult();
  CdpScreenshotResult(CdpScreenshotOutcome outcome,
                      std::vector<uint8_t> png_bytes);
  CdpScreenshotResult(CdpScreenshotResult&&);
  CdpScreenshotResult& operator=(CdpScreenshotResult&&);
  ~CdpScreenshotResult();

  CdpScreenshotOutcome outcome = CdpScreenshotOutcome::kNotOurResponse;
  std::vector<uint8_t> png_bytes;
};

// Parses a raw CDP JSON message received by a DevToolsAgentHostClient after
// it sent a Page.captureScreenshot command with id `request_id`.
CdpScreenshotResult ParseCaptureScreenshotMessage(std::string_view message,
                                                  int request_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CDP_RESPONSE_H_
