// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_STARTUP_HOLD_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_STARTUP_HOLD_H_

#include <string_view>

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace atrium {

// How long a window waits for its sidebar's rows before it shows anyway. A
// cold profile took 2.1s to report them on the VM, and at the old 700ms every
// launch showed an empty window on the cap instead.
inline constexpr base::TimeDelta kStartupHoldCap = base::Milliseconds(3000);

// holds a window's first show until the sidebar draws its rows, or
// until the cap. `show` runs once, with "drawn" or "cap" as its reason.
class StartupHold {
 public:
  using ShowCallback = base::OnceCallback<void(std::string_view reason)>;

  StartupHold();
  ~StartupHold();

  void Begin(base::TimeDelta cap, ShowCallback show);
  void Drawn();
  // Shows a pending hold now, with "overlay" as its reason.
  void Release();
  void Cancel();
  bool pending() const { return !show_.is_null(); }

 private:
  void Fire(std::string_view reason);

  base::OneShotTimer timer_;
  ShowCallback show_;
  bool drawn_ = false;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_STARTUP_HOLD_H_
