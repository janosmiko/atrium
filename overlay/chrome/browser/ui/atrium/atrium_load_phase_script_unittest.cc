// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_load_phase_script.h"

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::string Script(uint64_t tab_id, LoadPhase phase, double fraction) {
  return base::UTF16ToUTF8(BuildLoadPhaseScript(tab_id, phase, fraction));
}

TEST(AtriumLoadPhaseScriptTest, WaitingCarriesTheTabIdAsAQuotedString) {
  EXPECT_EQ(Script(41, LoadPhase::kWaiting, 0.0),
            "window.atriumLoadPhase && atriumLoadPhase(\"41\", \"waiting\", "
            "0.0000)");
}

TEST(AtriumLoadPhaseScriptTest, ReceivingCarriesTheFraction) {
  EXPECT_EQ(Script(7, LoadPhase::kReceiving, 0.375),
            "window.atriumLoadPhase && atriumLoadPhase(\"7\", \"receiving\", "
            "0.3750)");
}

TEST(AtriumLoadPhaseScriptTest, DoneCarriesAFullFraction) {
  EXPECT_EQ(Script(9, LoadPhase::kDone, 1.0),
            "window.atriumLoadPhase && atriumLoadPhase(\"9\", \"done\", "
            "1.0000)");
}

TEST(AtriumLoadPhaseScriptTest, TheFractionIsClampedToTheUnitRange) {
  EXPECT_NE(Script(1, LoadPhase::kReceiving, 4.2).find("1.0000"),
            std::string::npos);
  EXPECT_NE(Script(1, LoadPhase::kReceiving, -3.0).find("0.0000"),
            std::string::npos);
}

// The window pushes the same script into both documents. A phase name that
// drifts from the receiver's switch is the failure this pins.
TEST(AtriumLoadPhaseScriptTest, EveryPhaseNameIsOneOfTheThree) {
  const LoadPhase phases[] = {LoadPhase::kWaiting, LoadPhase::kReceiving,
                              LoadPhase::kDone};
  for (LoadPhase phase : phases) {
    const std::string script = Script(3, phase, 0.5);
    const bool named = script.find("\"waiting\"") != std::string::npos ||
                       script.find("\"receiving\"") != std::string::npos ||
                       script.find("\"done\"") != std::string::npos;
    EXPECT_TRUE(named) << script;
  }
}

}  // namespace
}  // namespace atrium
