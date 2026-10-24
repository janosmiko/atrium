// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_actions_json_unittest.cc
#include "chrome/browser/ui/atrium/atrium_extension_actions_json.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// Parses the builder's output back so a test asserts the decoded shape rather
// than exact byte formatting (the double serialization of ARGB numbers is an
// implementation detail Swift's int64Value decode tolerates either way).
base::Value Parse(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  EXPECT_TRUE(parsed && parsed->is_list());
  return parsed ? std::move(*parsed) : base::Value();
}

TEST(AtriumExtensionActionsJSONTest, EmptyYieldsEmptyArray) {
  EXPECT_EQ(BuildExtensionActionsJSON({}), "[]");
}

TEST(AtriumExtensionActionsJSONTest, SingleEntryCarriesEveryField) {
  ExtensionActionSnapshot action;
  action.id = "abcdefghijklmnopabcdefghijklmnop";
  action.name = "uBlock Origin";
  action.visible = true;
  action.has_popup = true;
  action.pinned = true;
  action.action_title = "Block ads on this page";
  action.badge_text = "12";
  action.badge_bg = 0xFF2E7D32;
  action.badge_text_color = 0xFFFFFFFF;
  action.icon_rev = 3;
  action.icon_scope = "scope-a";

  base::Value value = Parse(BuildExtensionActionsJSON({action}));
  const base::ListValue& list = value.GetList();
  ASSERT_EQ(list.size(), 1u);
  const base::DictValue& entry = list[0].GetDict();
  EXPECT_EQ(*entry.FindString("id"), action.id);
  EXPECT_EQ(*entry.FindString("name"), action.name);
  EXPECT_EQ(entry.FindBool("visible"), true);
  EXPECT_EQ(entry.FindBool("hasPopup"), true);
  EXPECT_EQ(entry.FindBool("pinned"), true);
  EXPECT_EQ(*entry.FindString("actionTitle"), action.action_title);
  EXPECT_EQ(*entry.FindString("badgeText"), action.badge_text);
  // ARGB above INT_MAX survives the double round-trip intact.
  EXPECT_EQ(entry.FindDouble("badgeBg"),
            static_cast<double>(action.badge_bg));
  EXPECT_EQ(entry.FindDouble("badgeTextColor"),
            static_cast<double>(action.badge_text_color));
  EXPECT_EQ(entry.FindInt("iconRev"), 3);
  EXPECT_EQ(*entry.FindString("iconScope"), "scope-a");
}

TEST(AtriumExtensionActionsJSONTest, HiddenActionWithNoBadgeSerializesDefaults) {
  ExtensionActionSnapshot action;
  action.id = "id";
  action.name = "id";
  // visible/has_popup false, empty badge, zero (fully transparent) colors.

  base::Value value = Parse(BuildExtensionActionsJSON({action}));
  const base::ListValue& list = value.GetList();
  ASSERT_EQ(list.size(), 1u);
  const base::DictValue& entry = list[0].GetDict();
  EXPECT_EQ(entry.FindBool("visible"), false);
  EXPECT_EQ(entry.FindBool("hasPopup"), false);
  EXPECT_EQ(*entry.FindString("badgeText"), "");
  EXPECT_EQ(entry.FindDouble("badgeBg"), 0.0);
  EXPECT_EQ(entry.FindDouble("badgeTextColor"), 0.0);
}

TEST(AtriumExtensionActionsJSONTest, AnUnpinnedActionSaysSoRatherThanOmitting) {
  ExtensionActionSnapshot action;
  action.id = "id";
  action.name = "id";

  base::Value value = Parse(BuildExtensionActionsJSON({action}));
  const base::DictValue& entry = value.GetList()[0].GetDict();
  // Present and false, not absent: the strip reads this key directly, and an
  // absent key would read as undefined rather than "not pinned".
  EXPECT_EQ(entry.FindBool("pinned"), false);
}

TEST(AtriumExtensionActionsJSONTest, PreservesOrderAcrossEntries) {
  ExtensionActionSnapshot first;
  first.id = "first";
  ExtensionActionSnapshot second;
  second.id = "second";

  base::Value value = Parse(BuildExtensionActionsJSON({first, second}));
  const base::ListValue& list = value.GetList();
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(*list[0].GetDict().FindString("id"), "first");
  EXPECT_EQ(*list[1].GetDict().FindString("id"), "second");
}

TEST(ExtensionActionsPushGateTest, FirstCallAlwaysPushes) {
  ExtensionActionsPushGate gate;
  EXPECT_TRUE(gate.ShouldPush(1, "[]"));
}

TEST(ExtensionActionsPushGateTest, SameTabIdenticalJsonSkips) {
  ExtensionActionsPushGate gate;
  ASSERT_TRUE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
  EXPECT_FALSE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
}

TEST(ExtensionActionsPushGateTest, SameTabDifferentJsonPushes) {
  ExtensionActionsPushGate gate;
  ASSERT_TRUE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
  EXPECT_TRUE(gate.ShouldPush(1, "[{\"id\":\"b\"}]"));
}

TEST(ExtensionActionsPushGateTest, TabSwitchAlwaysPushesEvenIfJsonIdentical) {
  ExtensionActionsPushGate gate;
  ASSERT_TRUE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
  EXPECT_TRUE(gate.ShouldPush(2, "[{\"id\":\"a\"}]"));
}

TEST(ExtensionActionsPushGateTest, SwitchingBackSkipsIfContentUnchanged) {
  ExtensionActionsPushGate gate;
  ASSERT_TRUE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
  ASSERT_TRUE(gate.ShouldPush(2, "[{\"id\":\"b\"}]"));
  ASSERT_TRUE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
  EXPECT_FALSE(gate.ShouldPush(1, "[{\"id\":\"a\"}]"));
}

TEST(ExtensionActionsPushGateTest, DefaultTabIdSentinelIsAValidTabId) {
  // ExtensionAction::kDefaultTabId is -1; the gate must not special-case it.
  ExtensionActionsPushGate gate;
  ASSERT_TRUE(gate.ShouldPush(-1, "[]"));
  EXPECT_FALSE(gate.ShouldPush(-1, "[]"));
}

}  // namespace
}  // namespace atrium
