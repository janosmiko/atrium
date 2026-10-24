// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_accelerator_lookup_mac.mm
#include "chrome/browser/ui/atrium/atrium_accelerator_lookup.h"

#import "chrome/browser/ui/cocoa/accelerators_cocoa.h"
#include "ui/base/accelerators/accelerator.h"

namespace atrium {

bool AcceleratorForCommand(int command_id, ui::Accelerator* out) {
  const ui::Accelerator* found =
      AcceleratorsCocoa::GetInstance()->GetAcceleratorForCommand(command_id);
  if (!found) {
    return false;
  }
  *out = *found;
  return true;
}

}  // namespace atrium
