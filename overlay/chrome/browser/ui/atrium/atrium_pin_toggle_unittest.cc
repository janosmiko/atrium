// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pin_toggle_unittest.cc
//
// Cmd+D did nothing in either direction. Both directions read the
// same sidebar model, so both are decided here rather than in the window.

#include "chrome/browser/ui/atrium/atrium_pin_toggle.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

PinToggleRow Live(std::string tab_id) {
  return PinToggleRow{std::move(tab_id), false};
}

PinToggleRow Showing(std::string tab_id) {
  return PinToggleRow{std::move(tab_id), true};
}

PinToggleRow Folder(std::string folder_id, std::vector<PinToggleRow> members) {
  PinToggleRow folder;
  folder.folder_id = std::move(folder_id);
  folder.members = std::move(members);
  return folder;
}

TEST(AtriumPinToggleTest, AShownRegularRowPins) {
  const PinToggleAction action =
      DecidePinToggle({Live("11")}, {Live("22"), Showing("33")});
  EXPECT_EQ(action.kind, PinToggleKind::kPin);
  EXPECT_EQ(action.tab_id, "33");
}

TEST(AtriumPinToggleTest, AShownPinnedRowUnpinsAtItsOwnIndex) {
  const PinToggleAction action =
      DecidePinToggle({Live("11"), Showing("22")}, {Live("33")});
  EXPECT_EQ(action.kind, PinToggleKind::kUnpin);
  EXPECT_EQ(action.pinned_index, 1u);
}

// The pinned section is read first. A snapshot that marked a row in each list
// would otherwise pin an already pinned tab, and the core refuses that - so
// Cmd+D would look dead on exactly the tab it was pressed for.
TEST(AtriumPinToggleTest, ThePinnedSectionWinsAConflict) {
  const PinToggleAction action =
      DecidePinToggle({Showing("22")}, {Showing("22")});
  EXPECT_EQ(action.kind, PinToggleKind::kUnpin);
  EXPECT_EQ(action.pinned_index, 0u);
}

// A cold row is a URL the space remembers with nothing open behind it, and the
// core pins by live tab id. Declining leaves Cmd+D on Bookmark This Tab.
TEST(AtriumPinToggleTest, AColdRowIsNotPinnable) {
  EXPECT_EQ(DecidePinToggle({}, {Showing(std::string())}).kind,
            PinToggleKind::kNothing);
}

// A folder is one pinned item, so a member has no item index of its own. The
// core finds it by its folder and its place in that folder.
TEST(AtriumPinToggleTest, AShownFolderMemberUnpinsThatMember) {
  const PinToggleAction action = DecidePinToggle(
      {Folder("G", {Live("11"), Showing("12"), Live("13")}), Live("21"),
       Live("22")},
      {});
  EXPECT_EQ(action.kind, PinToggleKind::kUnpinMember);
  EXPECT_EQ(action.folder_id, "G");
  EXPECT_EQ(action.member_index, 1u);
}

// Counting the folder's members would name the wrong slot for every pin after
// the folder.
TEST(AtriumPinToggleTest, APinAfterAFolderUnpinsAtItsItemIndex) {
  const PinToggleAction action = DecidePinToggle(
      {Folder("G", {Live("11"), Live("12"), Live("13")}), Live("21"),
       Showing("22")},
      {});
  EXPECT_EQ(action.kind, PinToggleKind::kUnpin);
  EXPECT_EQ(action.pinned_index, 2u);
}

TEST(AtriumPinToggleTest, NothingShownIsNothingToDo) {
  EXPECT_EQ(DecidePinToggle({Live("11")}, {Live("22")}).kind,
            PinToggleKind::kNothing);
  EXPECT_EQ(DecidePinToggle({}, {}).kind, PinToggleKind::kNothing);
}

}  // namespace
}  // namespace atrium
