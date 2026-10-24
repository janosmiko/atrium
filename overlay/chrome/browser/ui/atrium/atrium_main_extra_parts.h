// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_main_extra_parts.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MAIN_EXTRA_PARTS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MAIN_EXTRA_PARTS_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace atrium {

// defaults --user-data-dir, --no-first-run and
// --no-default-browser-check for a bundled Atrium build, so a Dock/Finder
// launch (which gets none of run-atrium.sh's flags) doesn't silently fall
// back to a vanilla profile/first-run flow. Must run from
// ChromeMainDelegate::PreSandboxStartup (chrome_main_delegate.cc), NOT from
// AtriumMainExtraParts::PreBrowserStart below - PreBrowserStart runs inside
// ChromeBrowserMainParts::PreMainMessageLoopRunImpl, long after
// InitializeUserDataDir() has already resolved chrome::DIR_USER_DATA from
// whatever --user-data-dir was on the command line at PreSandboxStartup time,
// and after first_run::IsChromeFirstRun() has already been read. Each switch
// is only appended if absent, so an explicit flag (run-atrium.sh, or a manual
// override) always wins.
void ApplyDefaultLaunchSwitches();

// factored out of the timer callback so a browsertest can
// call it on a seeded `now` instead of waiting on the real clock. Wakes
// every snoozed tab past wake_at, reopening it into its space and firing
// the toast/glow when its alert flag is set. False when nothing was due,
// or no live window was found to reopen into.
bool WakeDueSnoozedTabs(double now);

// Diffs the Archived list before/after the sweep, since
// atrium_core_sweep_archive only reports whether anything moved. Empty when
// nothing was due.
std::vector<std::string> SweepArchivedTabIds(const char* threshold,
                                              double now);

// archive-fly.js clones swept rows from the sidebar DOM, so
// `notify` must run before `push` rebuilds it away. Closures, not an
// AtriumBrowserWindow*, so the order is unit-testable without a live Browser.
void NotifyThenPushArchiveSweep(
    const std::vector<std::string>& swept,
    base::OnceCallback<void(const std::vector<std::string>&)> notify,
    base::OnceClosure push);

// Starts the Atrium chrome layer before the first browser window.
class AtriumMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  AtriumMainExtraParts();
  ~AtriumMainExtraParts() override;

  void PreBrowserStart() override;
  // Runs on the UI thread after RunLoop returns, before thread and profile
  // teardown.
  void PostMainMessageLoopRun() override;

 private:
  // one process-wide timer, not a per-window one like this
  // file's animation siblings - the core's document is a process-wide
  // singleton, so a per-window timer would sweep it once per open window.
  void RunArchiveSweep();

  // same shape as RunArchiveSweep, but this one has to reopen
  // a tab in a live window rather than just move a bookkeeping row.
  void RunWakeSweep();

  base::RepeatingTimer archive_sweep_timer_;
  base::RepeatingTimer wake_check_timer_;
  base::WeakPtrFactory<AtriumMainExtraParts> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MAIN_EXTRA_PARTS_H_
