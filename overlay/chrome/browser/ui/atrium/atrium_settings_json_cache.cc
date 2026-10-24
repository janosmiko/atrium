// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_settings_json_cache.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"

namespace atrium {

SettingsJsonCache::SettingsJsonCache() = default;

SettingsJsonCache::~SettingsJsonCache() = default;

// static
SettingsJsonCache& SettingsJsonCache::GetInstance() {
  static base::NoDestructor<SettingsJsonCache> cache;
  return *cache;
}

void SettingsJsonCache::Get(scoped_refptr<base::TaskRunner> runner,
                            Loader loader,
                            ReadyCallback on_ready) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (loaded_) {
    std::move(on_ready).Run(json_);
    return;
  }
  waiting_.push_back(std::move(on_ready));
  if (loading_) {
    return;
  }
  loading_ = true;
  runner->PostTaskAndReplyWithResult(
      FROM_HERE, std::move(loader),
      base::BindOnce(&SettingsJsonCache::OnLoaded, weak_factory_.GetWeakPtr()));
}

void SettingsJsonCache::OnLoaded(std::string json) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  json_ = std::move(json);
  loaded_ = true;
  loading_ = false;
  std::vector<ReadyCallback> waiting = std::move(waiting_);
  waiting_.clear();
  for (ReadyCallback& callback : waiting) {
    std::move(callback).Run(json_);
  }
}

}  // namespace atrium
