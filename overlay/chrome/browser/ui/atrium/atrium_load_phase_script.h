// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOAD_PHASE_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOAD_PHASE_SCRIPT_H_

#include <cstdint>
#include <string>

namespace atrium {

// The three states the address field, the floating capsule, and the ring
// all read. Waiting is the stretch before the first byte, which Chromium
// reports no progress for.
enum class LoadPhase { kWaiting, kReceiving, kDone };

// The script both the sidebar document and the devbar document run on every
// loading signal of the active tab. The tab id goes over as a string because
// the row and tile marks key on strings.
std::u16string BuildLoadPhaseScript(uint64_t tab_id,
                                    LoadPhase phase,
                                    double fraction);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOAD_PHASE_SCRIPT_H_
