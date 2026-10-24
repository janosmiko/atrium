// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_spaces_order.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::string NamesInOrder(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  const base::ListValue* spaces =
      (parsed && parsed->is_dict()) ? parsed->GetDict().FindList("spaces") : nullptr;
  if (!spaces) {
    return "no spaces";
  }
  std::string out;
  for (const base::Value& space : *spaces) {
    const std::string* name = space.GetDict().FindString("name");
    out += (name ? *name : "?") + std::string(" ");
  }
  return out;
}

constexpr char kOutOfOrder[] =
    R"({"spaces":[{"id":"c","name":"Play","order":2},)"
    R"({"id":"a","name":"Home","order":0},)"
    R"({"id":"b","name":"Work","order":1}],)"
    R"("profiles":[{"id":"Default"}]})";

TEST(SpacesInSidebarOrder, SortsByTheOrderFieldAndKeepsSiblingKeys) {
  const std::string sorted = SpacesInSidebarOrder(kOutOfOrder);
  EXPECT_EQ(NamesInOrder(sorted), "Home Work Play ");
  std::optional<base::Value> parsed =
      base::JSONReader::Read(sorted, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_TRUE(parsed->GetDict().FindList("profiles"))
      << "the sort dropped a sibling key";
}

// A missing or non-numeric `order` sorts as 0 rather than refusing to
// compare - a document the core itself could never load must still render
// in a stable order instead of scrambling the rest of the list.
TEST(SpacesInSidebarOrder, AnUnparseableOrderDoesNotReorderTheRest) {
  EXPECT_EQ(NamesInOrder(SpacesInSidebarOrder(
                R"({"spaces":[{"name":"NoOrder"},{"name":"One","order":1},)"
                R"({"name":"AlsoNone"}]})")),
            "NoOrder AlsoNone One ");
}

// The pane drawing a mis-sorted list beats it drawing nothing.
TEST(SpacesInSidebarOrder, UnreadableInputComesBackUnchanged) {
  EXPECT_EQ(SpacesInSidebarOrder("not json"), "not json");
  EXPECT_EQ(SpacesInSidebarOrder(R"({"profiles":[]})"), R"({"profiles":[]})");
  EXPECT_EQ(SpacesInSidebarOrder(std::string()), std::string());
}

}  // namespace
}  // namespace atrium
