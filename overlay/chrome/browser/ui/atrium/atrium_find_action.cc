// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_action.cc
#include "chrome/browser/ui/atrium/atrium_find_action.h"

namespace atrium {

FindRequest FindRequestFor(FindTrigger trigger, bool query_empty) {
  FindRequest r;
  switch (trigger) {
    case FindTrigger::kOpen:
      r.start = !query_empty;
      r.find_match = false;
      break;
    case FindTrigger::kQueryChanged:
      if (query_empty) {
        r.stop = true;
        r.keep_selection = false;
      } else {
        r.start = true;
        r.find_match = true;
      }
      break;
    case FindTrigger::kNext:
      r.start = !query_empty;
      r.find_match = true;
      break;
    case FindTrigger::kPrevious:
      r.start = !query_empty;
      r.forward = false;
      r.find_match = true;
      break;
    case FindTrigger::kClose:
      r.stop = true;
      break;
  }
  return r;
}

}  // namespace atrium
