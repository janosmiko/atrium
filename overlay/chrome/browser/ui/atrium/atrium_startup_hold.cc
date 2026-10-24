// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_startup_hold.h"

#include <utility>

#include "base/functional/bind.h"

namespace atrium {

StartupHold::StartupHold() = default;
StartupHold::~StartupHold() = default;

void StartupHold::Begin(base::TimeDelta cap, ShowCallback show) {
  show_ = std::move(show);
  if (drawn_) {
    Fire("drawn");
    return;
  }
  timer_.Start(FROM_HERE, cap,
               base::BindOnce(&StartupHold::Fire, base::Unretained(this),
                              "cap"));
}

void StartupHold::Drawn() {
  drawn_ = true;
  Fire("drawn");
}

void StartupHold::Release() {
  Fire("overlay");
}

void StartupHold::Cancel() {
  timer_.Stop();
  show_.Reset();
}

void StartupHold::Fire(std::string_view reason) {
  timer_.Stop();
  if (show_.is_null()) {
    return;
  }
  std::move(show_).Run(reason);
}

}  // namespace atrium
