// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_cdp_response_unittest.cc
#include "chrome/browser/ui/atrium/atrium_cdp_response.h"

#include "base/base64.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr int kRequestId = 1;

TEST(AtriumCdpResponseTest, SuccessDecodesPngBytes) {
  const std::string encoded = base::Base64Encode(std::string_view("PNGDATA"));
  const std::string message =
      "{\"id\":1,\"result\":{\"data\":\"" + encoded + "\"}}";

  const CdpScreenshotResult result =
      ParseCaptureScreenshotMessage(message, kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kSuccess);
  EXPECT_EQ(result.png_bytes,
           std::vector<uint8_t>({'P', 'N', 'G', 'D', 'A', 'T', 'A'}));
}

TEST(AtriumCdpResponseTest, EventWithNoIdIsNotOurResponse) {
  const CdpScreenshotResult result = ParseCaptureScreenshotMessage(
      "{\"method\":\"Page.frameNavigated\",\"params\":{}}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kNotOurResponse);
}

TEST(AtriumCdpResponseTest, ResponseToADifferentRequestIsNotOurResponse) {
  const CdpScreenshotResult result = ParseCaptureScreenshotMessage(
      "{\"id\":2,\"result\":{}}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kNotOurResponse);
}

TEST(AtriumCdpResponseTest, NotJsonIsNotOurResponse) {
  const CdpScreenshotResult result =
      ParseCaptureScreenshotMessage("not json at all", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kNotOurResponse);
}

TEST(AtriumCdpResponseTest, JsonArrayIsNotOurResponse) {
  const CdpScreenshotResult result =
      ParseCaptureScreenshotMessage("[1,2,3]", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kNotOurResponse);
}

TEST(AtriumCdpResponseTest, ErrorFieldFails) {
  const CdpScreenshotResult result = ParseCaptureScreenshotMessage(
      "{\"id\":1,\"error\":{\"message\":\"boom\"}}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kFailed);
}

TEST(AtriumCdpResponseTest, MissingResultFails) {
  const CdpScreenshotResult result =
      ParseCaptureScreenshotMessage("{\"id\":1}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kFailed);
}

TEST(AtriumCdpResponseTest, MissingDataFieldFails) {
  const CdpScreenshotResult result =
      ParseCaptureScreenshotMessage("{\"id\":1,\"result\":{}}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kFailed);
}

TEST(AtriumCdpResponseTest, InvalidBase64DataFails) {
  const CdpScreenshotResult result = ParseCaptureScreenshotMessage(
      "{\"id\":1,\"result\":{\"data\":\"not-valid-base64!!\"}}", kRequestId);

  EXPECT_EQ(result.outcome, CdpScreenshotOutcome::kFailed);
}

}  // namespace
}  // namespace atrium
