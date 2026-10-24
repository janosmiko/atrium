// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_CHAT_PROMPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_CHAT_PROMPT_H_

#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/values.h"

namespace atrium_ai {

// What a page cut at the cap has to tell the model. Exported because the
// on-device route builds its prompt in Swift and never reaches
// BuildChatMessages, which would otherwise be the only backend that hedges.
extern const std::string_view kTruncatedNote;

// The answer and the thinking so far, run on every delta. Two fields because
// concatenated they cannot be told apart again downstream. Here
// because both backends take one and neither may depend on the other.
using AiChunkCallback =
    base::RepeatingCallback<void(const std::string& answer_so_far,
                                 const std::string& reasoning_so_far)>;

// Its own target because the Ollama and custom-endpoint backends must not
// depend on each other, and the instructions that stop untrusted page text
// steering the model must not drift between them. Empty `question` summarizes.
base::ListValue BuildChatMessages(const std::string& page_text,
                                  const std::string& question);

}  // namespace atrium_ai

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_CHAT_PROMPT_H_
