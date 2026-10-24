// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pip_always_on_top_unittest.cc
#include "chrome/browser/ui/atrium/atrium_pip_always_on_top.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumPipAlwaysOnTopTest, TheFieldIsReadBothWaysRound) {
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":true})"),
            std::make_optional(true));
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":false})"),
            std::make_optional(false));
}

// A settings file written before this field existed, which is every key past
// the first four.
TEST(AtriumPipAlwaysOnTopTest, AnAbsentFieldIsNoAnswer) {
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"glassOpacity":0.55})"),
            std::nullopt);
}

TEST(AtriumPipAlwaysOnTopTest, NothingUsableYieldsNoAnswer) {
  EXPECT_EQ(PipAlwaysOnTopFromSettings(""), std::nullopt);
  EXPECT_EQ(PipAlwaysOnTopFromSettings("not json"), std::nullopt);
  EXPECT_EQ(PipAlwaysOnTopFromSettings("[1,2,3]"), std::nullopt);
}

TEST(AtriumPipAlwaysOnTopTest, AFieldOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":"true"})"),
            std::nullopt);
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":1})"),
            std::nullopt);
  EXPECT_EQ(PipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":null})"),
            std::nullopt);
}

class AtriumPipAlwaysOnTopMirrorTest : public ::testing::Test {
 public:
  void TearDown() override { PipAlwaysOnTop() = true; }
};

TEST_F(AtriumPipAlwaysOnTopMirrorTest, LoadSeedsTheMirrorFromSettings) {
  PipAlwaysOnTop() = true;
  LoadPipAlwaysOnTopFromSettings(R"({"pipAlwaysOnTop":false})");
  EXPECT_FALSE(PipAlwaysOnTop());
}

TEST_F(AtriumPipAlwaysOnTopMirrorTest, AnAbsentFieldLeavesTheMirrorAlone) {
  PipAlwaysOnTop() = false;
  LoadPipAlwaysOnTopFromSettings(R"({"glassOpacity":0.55})");
  EXPECT_FALSE(PipAlwaysOnTop());
}

}  // namespace
}  // namespace atrium
