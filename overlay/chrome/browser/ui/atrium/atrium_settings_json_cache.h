// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_json_cache.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_JSON_CACHE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_JSON_CACHE_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/task_runner.h"

namespace atrium {

// The settings JSON for this process, read once and never on the asking
// sequence.
//
// Only the first profile's window is built before
// BrowserMainLoop::PreMainMessageLoopRun calls DisallowUnresponsiveTasks, so a
// window that reads settings.json itself aborts on the blocking DCHECK from
// the second profile on. `loader` runs at most once per cache - a caller
// arriving mid-read joins it.
class SettingsJsonCache {
 public:
  using Loader = base::OnceCallback<std::string()>;
  using ReadyCallback = base::OnceCallback<void(const std::string&)>;

  SettingsJsonCache();
  SettingsJsonCache(const SettingsJsonCache&) = delete;
  SettingsJsonCache& operator=(const SettingsJsonCache&) = delete;
  ~SettingsJsonCache();

  // Runs `loader` on `runner`, then `on_ready` on the calling sequence. When
  // the text is already held `on_ready` runs before this returns, so a caller
  // that can act immediately still does.
  void Get(scoped_refptr<base::TaskRunner> runner,
           Loader loader,
           ReadyCallback on_ready);

  // The cache the browser shares. UI sequence only.
  static SettingsJsonCache& GetInstance();

 private:
  void OnLoaded(std::string json);

  bool loaded_ = false;
  bool loading_ = false;
  std::string json_;
  std::vector<ReadyCallback> waiting_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<SettingsJsonCache> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_JSON_CACHE_H_
