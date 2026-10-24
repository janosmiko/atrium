// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_icon_source.cc
#include "chrome/browser/ui/atrium/atrium_icon_source.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"

namespace atrium {

bool IsAppIconSlug(std::string_view slug) {
  if (slug.empty()) {
    return false;
  }
  for (const char c : slug) {
    const bool allowed =
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

base::FilePath AppIconPath(std::string_view slug) {
  if (!IsAppIconSlug(slug)) {
    return base::FilePath();
  }
  // The OUTER bundle, matching inject-icns.sh: it writes beside app.icns, and
  // the main bundle is the app shortcut's when Atrium is launched from one.
  return base::apple::OuterBundlePath()
      .Append("Contents")
      .Append("Resources")
      .Append("atrium-icons")
      .AppendASCII(base::StrCat({slug, ".png"}));
}

scoped_refptr<base::RefCountedMemory> ReadAppIcon(const std::string& slug) {
  const base::FilePath path = AppIconPath(slug);
  if (path.empty()) {
    return nullptr;
  }
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(path);
  if (!bytes || bytes->empty()) {
    return nullptr;
  }
  return base::MakeRefCounted<base::RefCountedBytes>(std::move(*bytes));
}

void ServeAppIcon(
    const std::string& slug,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback) {
  // USER_VISIBLE rather than BEST_EFFORT: these tiles are the picker, so a
  // deferred read is a pane that renders empty for as long as it is deferred.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadAppIcon, slug), std::move(callback));
}

}  // namespace atrium
