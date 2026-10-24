// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_split_tree.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

using StringTree = AtriumSplitTree<std::string>;

TEST(AtriumSplitTreeTest, DefaultConstructedIsEmpty) {
  StringTree tree;
  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(0u, tree.leaf_count());
  EXPECT_TRUE(tree.Leaves().empty());
}

TEST(AtriumSplitTreeTest, SingleLeafHoldsOneValue) {
  StringTree tree = StringTree::MakeSingleLeaf("only");
  ASSERT_FALSE(tree.empty());
  EXPECT_EQ(1u, tree.leaf_count());
  EXPECT_EQ("only", tree.LeafAt(0));
  EXPECT_EQ(std::vector<std::string>({"only"}), tree.Leaves());
  ASSERT_TRUE(tree.root()->is_leaf());
}

TEST(AtriumSplitTreeTest, TwoLeafSplitRowOrdersPrimaryThenSecondary) {
  StringTree tree = StringTree::MakeTwoLeafSplit(
      StringTree::Direction::kRow, "primary", "secondary");
  EXPECT_EQ(2u, tree.leaf_count());
  EXPECT_EQ("primary", tree.LeafAt(0));
  EXPECT_EQ("secondary", tree.LeafAt(1));
  EXPECT_EQ(std::vector<std::string>({"primary", "secondary"}),
            tree.Leaves());
  ASSERT_FALSE(tree.root()->is_leaf());
  EXPECT_EQ(StringTree::Direction::kRow, tree.root()->dir());
  EXPECT_EQ(2u, tree.root()->kids().size());
}

TEST(AtriumSplitTreeTest, TwoLeafSplitColKeepsDirection) {
  StringTree tree = StringTree::MakeTwoLeafSplit(
      StringTree::Direction::kCol, "top", "bottom");
  EXPECT_EQ(StringTree::Direction::kCol, tree.root()->dir());
  EXPECT_EQ(std::vector<std::string>({"top", "bottom"}), tree.Leaves());
}

// Mirrors treeOf()'s canonical 4-pane grid shape.
TEST(AtriumSplitTreeTest, NestedFourLeafGridTraversesInLabOrder) {
  auto make_col = [](std::string a, std::string b) {
    std::vector<std::unique_ptr<StringTree::Node>> kids;
    kids.push_back(StringTree::Node::MakeLeaf(std::move(a)));
    kids.push_back(StringTree::Node::MakeLeaf(std::move(b)));
    return StringTree::Node::MakeBranch(StringTree::Direction::kCol,
                                        std::move(kids));
  };
  std::vector<std::unique_ptr<StringTree::Node>> columns;
  columns.push_back(make_col("a", "b"));
  columns.push_back(make_col("c", "d"));
  StringTree tree(StringTree::Node::MakeBranch(StringTree::Direction::kRow,
                                               std::move(columns)));

  EXPECT_EQ(4u, tree.leaf_count());
  EXPECT_EQ(std::vector<std::string>({"a", "b", "c", "d"}), tree.Leaves());
  EXPECT_EQ("a", tree.LeafAt(0));
  EXPECT_EQ("d", tree.LeafAt(3));
}

// closing one pane of a three or four pane split.
TEST(AtriumSplitTreeTest, RemoveLeafDissolvesATwoLeafSplitIntoTheSurvivor) {
  StringTree tree = StringTree::MakeTwoLeafSplit(
      StringTree::Direction::kRow, "primary", "secondary");
  EXPECT_TRUE(tree.RemoveLeaf("secondary"));
  EXPECT_EQ(1u, tree.leaf_count());
  EXPECT_EQ("primary", tree.LeafAt(0));
  EXPECT_TRUE(tree.root()->is_leaf());
}

TEST(AtriumSplitTreeTest, RemoveLeafKeepsTheSiblingsOfAThreeLeafSplit) {
  // Three leaves in InsertAtLeaf's shape for a top/bottom drop onto "b".
  std::vector<std::unique_ptr<StringTree::Node>> column;
  column.push_back(StringTree::Node::MakeLeaf("b"));
  column.push_back(StringTree::Node::MakeLeaf("c"));
  std::vector<std::unique_ptr<StringTree::Node>> row;
  row.push_back(StringTree::Node::MakeLeaf("a"));
  row.push_back(StringTree::Node::MakeBranch(StringTree::Direction::kCol,
                                             std::move(column)));
  StringTree tree(StringTree::Node::MakeBranch(StringTree::Direction::kRow,
                                               std::move(row)));

  EXPECT_TRUE(tree.RemoveLeaf("c"));
  EXPECT_EQ(2u, tree.leaf_count());
  EXPECT_EQ(std::vector<std::string>({"a", "b"}), tree.Leaves());
  // The column branch held one kid after the drop, so it dissolved - a stale
  // branch there would keep a gutter with no pane on one side of it.
  ASSERT_FALSE(tree.root()->is_leaf());
  EXPECT_EQ(2u, tree.root()->kids().size());
  EXPECT_TRUE(tree.root()->kids()[1]->is_leaf());
}

TEST(AtriumSplitTreeTest, RemoveLeafOnAFourLeafGridLeavesThree) {
  auto make_col = [](std::string a, std::string b) {
    std::vector<std::unique_ptr<StringTree::Node>> kids;
    kids.push_back(StringTree::Node::MakeLeaf(std::move(a)));
    kids.push_back(StringTree::Node::MakeLeaf(std::move(b)));
    return StringTree::Node::MakeBranch(StringTree::Direction::kCol,
                                        std::move(kids));
  };
  std::vector<std::unique_ptr<StringTree::Node>> columns;
  columns.push_back(make_col("a", "b"));
  columns.push_back(make_col("c", "d"));
  StringTree tree(StringTree::Node::MakeBranch(StringTree::Direction::kRow,
                                               std::move(columns)));

  EXPECT_TRUE(tree.RemoveLeaf("a"));
  EXPECT_EQ(std::vector<std::string>({"b", "c", "d"}), tree.Leaves());
  EXPECT_TRUE(tree.RemoveLeaf("d"));
  EXPECT_EQ(std::vector<std::string>({"b", "c"}), tree.Leaves());
}

TEST(AtriumSplitTreeTest, RemoveLeafReportsAnUnknownLeafAndChangesNothing) {
  StringTree tree = StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow,
                                                 "primary", "secondary");
  EXPECT_FALSE(tree.RemoveLeaf("stranger"));
  EXPECT_EQ(std::vector<std::string>({"primary", "secondary"}),
            tree.Leaves());
}

TEST(AtriumSplitTreeTest, RemoveLeafEmptiesASingleLeafTree) {
  StringTree tree = StringTree::MakeSingleLeaf("only");
  EXPECT_TRUE(tree.RemoveLeaf("only"));
  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(0u, tree.leaf_count());
}

TEST(AtriumSplitTreeTest, MoveAssignReplacesTree) {
  StringTree tree = StringTree::MakeSingleLeaf("first");
  tree = StringTree::MakeTwoLeafSplit(StringTree::Direction::kRow, "primary",
                                      "secondary");
  EXPECT_EQ(2u, tree.leaf_count());
  EXPECT_EQ("primary", tree.LeafAt(0));
}

}  // namespace
}  // namespace atrium
