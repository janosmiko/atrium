// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_tab_cycle_unittest.cc
#include "chrome/browser/ui/atrium/atrium_tab_cycle.h"

#include <string_view>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

base::DictValue Snapshot(std::string_view json) {
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  CHECK(root) << "the test's own snapshot is not a JSON object";
  return std::move(*root);
}

// The walk as one string, so a wrong ORDER fails differently from a wrong set.
std::string Walk(const std::vector<CycleRow>& rows) {
  std::string out;
  for (const CycleRow& row : rows) {
    out += row.tab_id + ",";
  }
  return out;
}

std::vector<CycleRow> Rows(size_t count, int selected_index) {
  std::vector<CycleRow> rows;
  for (size_t i = 0; i < count; ++i) {
    CycleRow row;
    row.tab_id = std::to_string(i + 1);
    row.selected = static_cast<int>(i) == selected_index;
    rows.push_back(std::move(row));
  }
  return rows;
}

// Top to bottom on screen: the favourites grid, then pinned, then each
// folder's rows, then the ordinary tabs.
TEST(AtriumCycleOrderTest, TilesComeBeforeEveryRow) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://fav.test/", "url": "https://fav.test/",
                   "tabId": "7"}],
    "pinned": [{"id": "1", "url": "https://one.test/"}],
    "folders": [{"tabs": [{"id": "2", "url": "https://two.test/"}]}],
    "tabs": [{"id": "3", "url": "https://three.test/"}]
  })");

  EXPECT_EQ(Walk(CycleOrderRows(root)), "7,1,2,3,");
}

// A tile's `id` is the favourite's URL. Reading it as a tab id would hand
// ShowRow a string that is not a number, and the walk would activate nothing.
TEST(AtriumCycleOrderTest, ATileIsWalkedByItsHeldTabNotItsUrl) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://fav.test/", "url": "https://fav.test/",
                   "tabId": "7"}],
    "tabs": [{"id": "3", "url": "https://three.test/"}]
  })");

  const std::vector<CycleRow> rows = CycleOrderRows(root);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].tab_id, "7");
  EXPECT_EQ(rows[0].url, "https://fav.test/");
}

// An unclaimed tile hides no row and stands for no live tab. Walking into one
// would open a page rather than move to a tab that is already open.
TEST(AtriumCycleOrderTest, ATileHoldingNoTabIsNotATarget) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://cold.test/", "url": "https://cold.test/",
                   "tabId": ""},
                  {"id": "https://none.test/", "url": "https://none.test/"}],
    "tabs": [{"id": "3", "url": "https://three.test/"}]
  })");

  EXPECT_EQ(Walk(CycleOrderRows(root)), "3,");
}

// Two of the three live tabs are held by tiles, so the drawn rows are ONE row
// and a walk over one row refuses. Both directions have to reach a tile.
TEST(AtriumCycleOrderTest, ASpaceOfMostlyTilesIsStillACycle) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://a.test/", "url": "https://a.test/",
                   "tabId": "5"},
                  {"id": "https://b.test/", "url": "https://b.test/",
                   "tabId": "6", "selected": true}],
    "tabs": [{"id": "3", "url": "https://three.test/"}]
  })");

  EXPECT_EQ(NextCycleTarget(DrawnOrderRows(root), 1), std::nullopt)
      << "the drawn rows alone still have to be the one-row list this is about";

  const std::vector<CycleRow> rows = CycleOrderRows(root);
  ASSERT_EQ(rows.size(), 3u);
  const std::optional<size_t> forward = NextCycleTarget(rows, 1);
  ASSERT_TRUE(forward.has_value());
  EXPECT_EQ(rows[*forward].tab_id, "3");
  const std::optional<size_t> backward = NextCycleTarget(rows, -1);
  ASSERT_TRUE(backward.has_value());
  EXPECT_EQ(rows[*backward].tab_id, "5");
}

// Arc wraps and so does this, tiles included: stepping forward off the last
// row lands on the first tile rather than stopping.
TEST(AtriumCycleOrderTest, TheWalkWrapsFromTheLastRowOntoTheFirstTile) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://a.test/", "url": "https://a.test/",
                   "tabId": "5"}],
    "tabs": [{"id": "3", "url": "https://three.test/", "selected": true}]
  })");

  const std::vector<CycleRow> rows = CycleOrderRows(root);
  const std::optional<size_t> forward = NextCycleTarget(rows, 1);
  ASSERT_TRUE(forward.has_value());
  EXPECT_EQ(rows[*forward].tab_id, "5");
}

// The tile hides the row it claimed only once the core knows about the claim,
// so a snapshot pushed in between carries both. Walking the tab twice would
// send one step back where the last came from.
TEST(AtriumCycleOrderTest, ATabInBothTheGridAndTheRowsIsWalkedOnce) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://a.test/", "url": "https://a.test/",
                   "tabId": "5"}],
    "tabs": [{"id": "5", "url": "https://a.test/"},
             {"id": "3", "url": "https://three.test/"}]
  })");

  EXPECT_EQ(Walk(CycleOrderRows(root)), "5,3,");
}

// Two cold rows carry the same empty id and are still two rows, so the guard
// above must not collapse them.
TEST(AtriumCycleOrderTest, ColdRowsAreNotEachOthersDuplicates) {
  const base::DictValue root = Snapshot(R"({
    "tabs": [{"url": "https://cold-one.test/"},
             {"url": "https://cold-two.test/"}]
  })");

  const std::vector<CycleRow> rows = CycleOrderRows(root);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].url, "https://cold-one.test/");
  EXPECT_EQ(rows[1].url, "https://cold-two.test/");
}

// A pinned folder is not itself a row - it carries no id or url the core can
// activate - and its members sit where the folder sits, not at the end.
TEST(AtriumCycleOrderTest, APinnedFolderWalksItsMembersInPlace) {
  const base::DictValue root = Snapshot(R"({
    "pinned": [{"kind": "folder", "id": "G", "tabs": [
                  {"id": "2", "url": "https://two.test/"},
                  {"id": "3", "url": "https://three.test/"}]},
               {"kind": "tab", "id": "1", "url": "https://one.test/"}]
  })");

  EXPECT_EQ(Walk(DrawnOrderRows(root)), "2,3,1,");
}

TEST(AtriumCycleOrderTest, AnEmptyPinnedFolderIsNotARow) {
  const base::DictValue root = Snapshot(R"({
    "pinned": [{"kind": "folder", "id": "G", "tabs": []}]
  })");

  EXPECT_TRUE(DrawnOrderRows(root).empty());
}

// A space switch shows this list's first entry, and a tile belongs to the
// PROFILE - one leaking in here would fill the empty pane a space with no tabs
// of its own is meant to show.
TEST(AtriumCycleOrderTest, DrawnOrderRowsLeavesTheGridOut) {
  const base::DictValue root = Snapshot(R"({
    "favorites": [{"id": "https://a.test/", "url": "https://a.test/",
                   "tabId": "5"}],
    "pinned": [],
    "folders": [],
    "tabs": []
  })");

  EXPECT_TRUE(DrawnOrderRows(root).empty());
  EXPECT_EQ(Walk(CycleOrderRows(root)), "5,");
}

TEST(AtriumTabCycleTest, ForwardStepsToTheNextRow) {
  EXPECT_EQ(NextCycleTarget(Rows(4, 1), 1), 2u);
}

TEST(AtriumTabCycleTest, BackwardStepsToThePreviousRow) {
  EXPECT_EQ(NextCycleTarget(Rows(4, 2), -1), 1u);
}

// Wrapping is what makes this a cycle. Without it the key stops working at the
// ends, which reads as the binding being broken rather than as a boundary.
TEST(AtriumTabCycleTest, ForwardWrapsPastTheLastRow) {
  EXPECT_EQ(NextCycleTarget(Rows(3, 2), 1), 0u);
}

TEST(AtriumTabCycleTest, BackwardWrapsPastTheFirstRow) {
  EXPECT_EQ(NextCycleTarget(Rows(3, 0), -1), 2u);
}

// A single row would otherwise resolve to itself, and re-activating the tab
// the user is already on looks like the key did nothing while it did.
TEST(AtriumTabCycleTest, ASingleRowIsNotACycle) {
  EXPECT_EQ(NextCycleTarget(Rows(1, 0), 1), std::nullopt);
  EXPECT_EQ(NextCycleTarget(Rows(1, 0), -1), std::nullopt);
}

TEST(AtriumTabCycleTest, AnEmptyListHasNowhereToGo) {
  EXPECT_EQ(NextCycleTarget({}, 1), std::nullopt);
}

// The active tab is not always in the list: a space switch leaves nothing
// marked mid-flight.
TEST(AtriumTabCycleTest, WithNothingSelectedTheListIsEnteredFromTheNearEnd) {
  EXPECT_EQ(NextCycleTarget(Rows(4, -1), 1), 0u);
  EXPECT_EQ(NextCycleTarget(Rows(4, -1), -1), 3u);
}

// A cold row is a legitimate target - it is on screen, so the cycle must reach
// it. What happens then is the caller's business: there is no id to activate,
// so its URL is reopened.
TEST(AtriumTabCycleTest, AColdRowIsStillATarget) {
  std::vector<CycleRow> rows = Rows(3, 0);
  rows[1].tab_id.clear();
  rows[1].url = "https://cold.example/";
  const std::optional<size_t> target = NextCycleTarget(rows, 1);
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, 1u);
  EXPECT_TRUE(rows[*target].tab_id.empty());
}

TEST(AtriumTabCycleTest, ADirectionOfZeroMovesNothing) {
  EXPECT_EQ(NextCycleTarget(Rows(4, 1), 0), std::nullopt);
}

// The core IGNORES an index it does not have rather than clamping
// it, so the wrap has to be right before the call - an off-by-one here is a
// chord that silently does nothing at one end of the rail.
TEST(AtriumSpaceCycleTest, ForwardStepsToTheNextSpace) {
  EXPECT_EQ(NextSpaceIndex(3, 0, 1), 1u);
}

TEST(AtriumSpaceCycleTest, BackwardStepsToThePreviousSpace) {
  EXPECT_EQ(NextSpaceIndex(3, 2, -1), 1u);
}

TEST(AtriumSpaceCycleTest, ForwardWrapsPastTheLastSpace) {
  EXPECT_EQ(NextSpaceIndex(3, 2, 1), 0u);
}

TEST(AtriumSpaceCycleTest, BackwardWrapsPastTheFirstSpace) {
  EXPECT_EQ(NextSpaceIndex(3, 0, -1), 2u);
}

TEST(AtriumSpaceCycleTest, ASingleSpaceIsNotACycle) {
  EXPECT_EQ(NextSpaceIndex(1, 0, 1), std::nullopt);
  EXPECT_EQ(NextSpaceIndex(1, 0, -1), std::nullopt);
}

TEST(AtriumSpaceCycleTest, NoSpacesHasNowhereToGo) {
  EXPECT_EQ(NextSpaceIndex(0, -1, 1), std::nullopt);
}

// -1 is what the core answers when no document is open, and a saved
// activeSpaceID can outlive the space it names.
TEST(AtriumSpaceCycleTest, AnActiveIndexOutOfRangeEntersFromTheNearEnd) {
  EXPECT_EQ(NextSpaceIndex(4, -1, 1), 0u);
  EXPECT_EQ(NextSpaceIndex(4, -1, -1), 3u);
  EXPECT_EQ(NextSpaceIndex(4, 9, 1), 0u);
  EXPECT_EQ(NextSpaceIndex(4, 9, -1), 3u);
}

TEST(AtriumSpaceCycleTest, ADirectionOfZeroMovesNothing) {
  EXPECT_EQ(NextSpaceIndex(4, 1, 0), std::nullopt);
}

}  // namespace
}  // namespace atrium
