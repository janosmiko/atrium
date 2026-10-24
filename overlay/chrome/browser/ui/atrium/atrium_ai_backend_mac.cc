// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ai_backend_mac.h"

#include <unistd.h>

#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/ui/atrium/atrium_ai_chat_prompt.h"
#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"

namespace atrium_ai {
namespace {

base::FilePath HelperPath() {
  base::FilePath dir;
  base::PathService::Get(base::DIR_EXE, &dir);
  return dir.AppendASCII("atrium_ai_fm_helper");
}

// The helper's own words are a Swift error description, which is not API and
// would read as noise in the panel. Only its first line is matched.
std::string FailureMessage(std::string_view helper_errors) {
  const std::string_view token =
      helper_errors.substr(0, helper_errors.find('\n'));
  if (token == "unsupported-language") {
    return "The on-device model does not handle this page's language.";
  }
  if (token == "unavailable") {
    return "The on-device model is not available on this Mac.";
  }
  return "The on-device model call failed.";
}

// Blocking process I/O - always run on a ThreadPool task.
std::pair<bool, std::string> RunHelperBlocking(std::vector<std::string> extra_args,
                                                std::string stdin_data) {
  base::FilePath helper = HelperPath();
  if (!base::PathExists(helper)) {
    return {false, "on-device summarize helper is missing"};
  }

  base::ScopedFD stdin_read, stdin_write;
  base::ScopedFD stdout_read, stdout_write;
  if (!base::CreatePipe(&stdin_read, &stdin_write) ||
      !base::CreatePipe(&stdout_read, &stdout_write)) {
    return {false, "could not create a pipe to the helper"};
  }

  // A file, not a second pipe: draining two pipes in sequence deadlocks as
  // soon as the helper fills the one this thread is not reading.
  base::FilePath errors_path;
  base::ScopedFILE errors(base::CreateAndOpenTemporaryStream(&errors_path));
  base::ScopedClosureRunner drop_errors(base::BindOnce(
      [](const base::FilePath& path) { base::DeleteFile(path); },
      errors_path));

  base::LaunchOptions options;
  options.fds_to_remap.emplace_back(stdin_read.get(), STDIN_FILENO);
  options.fds_to_remap.emplace_back(stdout_write.get(), STDOUT_FILENO);
  if (errors) {
    options.fds_to_remap.emplace_back(fileno(errors.get()), STDERR_FILENO);
  }

  std::vector<std::string> argv = {helper.value()};
  argv.insert(argv.end(), extra_args.begin(), extra_args.end());
  base::Process process = base::LaunchProcess(argv, options);
  stdin_read.reset();
  stdout_write.reset();
  if (!process.IsValid()) {
    return {false, "failed to launch the on-device summarize helper"};
  }

  if (!stdin_data.empty()) {
    write(stdin_write.get(), stdin_data.data(), stdin_data.size());
  }
  stdin_write.reset();

  std::string output;
  char buf[4096];
  ssize_t n;
  while ((n = read(stdout_read.get(), buf, sizeof(buf))) > 0) {
    output.append(buf, static_cast<size_t>(n));
  }

  int exit_code = -1;
  process.WaitForExit(&exit_code);
  if (exit_code == 0) {
    return {true, output};
  }
  std::string reason;
  base::ReadFileToString(errors_path, &reason);
  return {false, FailureMessage(reason)};
}

void ForwardResult(SummarizeCallback callback,
                    std::pair<bool, std::string> result) {
  if (result.first) {
    std::move(callback).Run(result.second, std::nullopt);
  } else {
    std::move(callback).Run(std::nullopt, result.second);
  }
}

// The helper's Swift prompt has no idea the text was cut, so the note has to
// travel inside the text itself.
std::string TextForHelper(const std::string& page_text) {
  std::string text = page_text;
  if (!atrium::StripTruncationMarker(text)) {
    return text;
  }
  return std::string(kTruncatedNote) + "\n\n" + text;
}

}  // namespace

bool IsOnDeviceSummarizeAvailable() {
  return RunHelperBlocking({"--check-availability"}, "").first;
}

void SummarizeOnDevice(const std::string& page_text,
                       SummarizeCallback callback) {
  // WaitForExit() below needs WithBaseSyncPrimitives() too - MayBlock()
  // alone still trips DCHECK(!tls_base_sync_primitives_disallowed).
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
      base::BindOnce(&RunHelperBlocking, std::vector<std::string>(),
                     TextForHelper(page_text)),
      base::BindOnce(&ForwardResult, std::move(callback)));
}

void AskOnDevice(const std::string& page_text,
                 const std::string& question,
                 SummarizeCallback callback) {
  // The helper splits stdin at the first NUL: question, then page text.
  std::string asked = question;
  atrium::StripTruncationMarker(asked);
  std::string stdin_data;
  base::RemoveChars(asked, std::string_view("\0", 1), &stdin_data);
  stdin_data.push_back('\0');
  stdin_data += TextForHelper(page_text);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
      base::BindOnce(&RunHelperBlocking, std::vector<std::string>{"--chat"},
                     std::move(stdin_data)),
      base::BindOnce(&ForwardResult, std::move(callback)));
}

}  // namespace atrium_ai
