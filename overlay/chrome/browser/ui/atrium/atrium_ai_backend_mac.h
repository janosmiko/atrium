// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_MAC_H_

#include <optional>
#include <string>

#include "base/functional/callback_forward.h"

namespace atrium_ai {

// Wraps SystemLanguageModel.default.availability - false on any Mac/OS
// build where the framework or the on-device model isn't present.
bool IsOnDeviceSummarizeAvailable();

using SummarizeCallback =
    base::OnceCallback<void(std::optional<std::string> summary,
                             std::optional<std::string> error)>;

// Runs a LanguageModelSession against `page_text` entirely on-device - no
// network request. Calls `callback` with exactly one of summary or error.
void SummarizeOnDevice(const std::string& page_text,
                       SummarizeCallback callback);

// Same contract as SummarizeOnDevice, but answers `question` about the page.
void AskOnDevice(const std::string& page_text,
                 const std::string& question,
                 SummarizeCallback callback);

}  // namespace atrium_ai

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_BACKEND_MAC_H_
