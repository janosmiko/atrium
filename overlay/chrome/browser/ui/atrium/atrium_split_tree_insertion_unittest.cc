// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_split_tree_insertion.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

using StringTree = AtriumSplitTree<std::string>;

TEST(AtriumSplitTreeInsertionTest, InsertAtLeafRightSplitsRowTargetFirst) {
  StringTree tree = StringTree::MakeSingleLeaf("A");
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ(StringTree::Direction::kRow, result.tree.root()->dir());
  EXPECT_EQ((std::vector<std::string>{"A", "B"}), result.tree.Leaves());
}

TEST(AtriumSplitTreeInsertionTest, InsertAtLeafLeftSplitsRowDropFirst) {
  StringTree tree = StringTree::MakeSingleLeaf("A");
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kLeft);
  EXPECT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ(StringTree::Direction::kRow, result.tree.root()->dir());
  EXPECT_EQ((std::vector<std::string>{"B", "A"}), result.tree.Leaves());
}

TEST(AtriumSplitTreeInsertionTest, InsertAtLeafBottomSplitsColTargetFirst) {
  StringTree tree = StringTree::MakeSingleLeaf("A");
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kBottom);
  EXPECT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ(StringTree::Direction::kCol, result.tree.root()->dir());
  EXPECT_EQ((std::vector<std::string>{"A", "B"}), result.tree.Leaves());
}

TEST(AtriumSplitTreeInsertionTest, InsertAtLeafTopSplitsColDropFirst) {
  StringTree tree = StringTree::MakeSingleLeaf("A");
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kTop);
  EXPECT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ(StringTree::Direction::kCol, result.tree.root()->dir());
  EXPECT_EQ((std::vector<std::string>{"B", "A"}), result.tree.Leaves());
}

TEST(AtriumSplitTreeInsertionTest, CenterDropSwapsIntoTargetLeaf) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto result = InsertAtLeaf<std::string>(tree, "A", "C", SplitEdge::kCenter);
  EXPECT_EQ(SplitInsertOutcome::kSwapped, result.outcome);
  EXPECT_EQ((std::vector<std::string>{"C", "B"}), result.tree.Leaves());
}

TEST(AtriumSplitTreeInsertionTest, SelfSplitRejectedWhenDropEqualsTarget) {
  StringTree tree = StringTree::MakeSingleLeaf("A");
  auto result = InsertAtLeaf<std::string>(tree, "A", "A", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kRejectedSelfSplit, result.outcome);
  EXPECT_TRUE(result.tree.empty());
}

TEST(AtriumSplitTreeInsertionTest, SelfSplitRejectedWhenDropAlreadyInTree) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kRejectedSelfSplit, result.outcome);
}

TEST(AtriumSplitTreeInsertionTest, RejectedWhenTargetNotFound) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto result = InsertAtLeaf<std::string>(tree, "Z", "C", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kRejectedTargetNotFound, result.outcome);
}

TEST(AtriumSplitTreeInsertionTest, RejectedOnEmptyTree) {
  StringTree tree;
  auto result = InsertAtLeaf<std::string>(tree, "A", "B", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kRejectedTargetNotFound, result.outcome);
}

// Splicing a 3rd leaf onto an existing 2-leaf row nests the new leaf's
// column into the row rather than flattening - normTree() only flattens
// same-direction branches (mR in swipe-lab.html's treeOf(), ~line 2294).
TEST(AtriumSplitTreeInsertionTest, ThirdLeafNestsAtInsertedEdge) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto result = InsertAtLeaf<std::string>(tree, "A", "C", SplitEdge::kTop);
  ASSERT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ((std::vector<std::string>{"C", "A", "B"}), result.tree.Leaves());
  const auto* root = result.tree.root();
  EXPECT_EQ(StringTree::Direction::kRow, root->dir());
  ASSERT_EQ(2u, root->kids().size());
  const auto& nested = *root->kids()[0];
  EXPECT_FALSE(nested.is_leaf());
  EXPECT_EQ(StringTree::Direction::kCol, nested.dir());
  EXPECT_TRUE(root->kids()[1]->is_leaf());
  EXPECT_EQ("B", root->kids()[1]->leaf());
}

// LayoutNode reads one stored ratio per gutter and a ratio only exists for a
// two-kid branch, so a three-kid branch reaches the window with two gutters
// and no ratios, and the CHECK there kills the browser.
TEST(AtriumSplitTreeInsertionTest, ThirdLeafOnTheSameAxisStaysBinary) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto result = InsertAtLeaf<std::string>(tree, "B", "C", SplitEdge::kRight);
  ASSERT_EQ(SplitInsertOutcome::kInserted, result.outcome);
  EXPECT_EQ((std::vector<std::string>{"A", "B", "C"}), result.tree.Leaves());
  const auto* root = result.tree.root();
  EXPECT_EQ(StringTree::Direction::kRow, root->dir());
  ASSERT_EQ(2u, root->kids().size());
  EXPECT_TRUE(root->kids()[0]->is_leaf());
  const auto& nested = *root->kids()[1];
  ASSERT_FALSE(nested.is_leaf());
  EXPECT_EQ(StringTree::Direction::kRow, nested.dir());
  ASSERT_EQ(2u, nested.kids().size());
  EXPECT_EQ("B", nested.kids()[0]->leaf());
  EXPECT_EQ("C", nested.kids()[1]->leaf());
}

// A 4th leaf always canonicalizes to the 2x2 grid, regardless of the mixed
// shape splicing would otherwise produce - layoutOf()'s "g" token once
// leaves.length>=4, swipe-lab.html ~line 2305.
TEST(AtriumSplitTreeInsertionTest, FourthLeafCanonicalizesToGrid) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto three = InsertAtLeaf<std::string>(tree, "A", "C", SplitEdge::kTop);
  ASSERT_EQ(SplitInsertOutcome::kInserted, three.outcome);
  auto four =
      InsertAtLeaf<std::string>(three.tree, "B", "D", SplitEdge::kRight);
  ASSERT_EQ(SplitInsertOutcome::kInserted, four.outcome);
  EXPECT_EQ((std::vector<std::string>{"C", "A", "B", "D"}),
            four.tree.Leaves());
  const auto* root = four.tree.root();
  EXPECT_EQ(StringTree::Direction::kRow, root->dir());
  ASSERT_EQ(2u, root->kids().size());
  EXPECT_EQ(StringTree::Direction::kCol, root->kids()[0]->dir());
  EXPECT_EQ(StringTree::Direction::kCol, root->kids()[1]->dir());
  EXPECT_EQ("C", root->kids()[0]->kids()[0]->leaf());
  EXPECT_EQ("A", root->kids()[0]->kids()[1]->leaf());
  EXPECT_EQ("B", root->kids()[1]->kids()[0]->leaf());
  EXPECT_EQ("D", root->kids()[1]->kids()[1]->leaf());
}

TEST(AtriumSplitTreeInsertionTest, CapacityRejectedAtFourLeaves) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto three = InsertAtLeaf<std::string>(tree, "A", "C", SplitEdge::kTop);
  auto four =
      InsertAtLeaf<std::string>(three.tree, "B", "D", SplitEdge::kRight);
  auto five =
      InsertAtLeaf<std::string>(four.tree, "D", "E", SplitEdge::kRight);
  EXPECT_EQ(SplitInsertOutcome::kRejectedAtCapacity, five.outcome);
  EXPECT_TRUE(five.tree.empty());
}

TEST(AtriumSplitTreeInsertionTest, CenterDropAllowedAtFourLeaves) {
  StringTree tree =
      StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "A", "B");
  auto three = InsertAtLeaf<std::string>(tree, "A", "C", SplitEdge::kTop);
  auto four =
      InsertAtLeaf<std::string>(three.tree, "B", "D", SplitEdge::kRight);
  auto swapped =
      InsertAtLeaf<std::string>(four.tree, "A", "E", SplitEdge::kCenter);
  EXPECT_EQ(SplitInsertOutcome::kSwapped, swapped.outcome);
  EXPECT_EQ((std::vector<std::string>{"C", "E", "B", "D"}),
            swapped.tree.Leaves());
}

}  // namespace
}  // namespace atrium
