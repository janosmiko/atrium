// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_split_tree_layout.h"

#include <functional>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

using StringTree = AtriumSplitTree<std::string>;
using Direction = StringTree::Direction;

const SplitLayoutParams kParams{
    .divider_extent = 8,
    .min_pane_extent = 200,
    .constrained_min_pane_extent = 50,
    .min_pane_extent_percentage = 0.1,
};

double HalfRatio(const SplitNodePath&) {
  return 0.5;
}

TEST(AtriumSplitTreeLayoutTest, TwoLeafRowSplitsHorizontallyWithRealRects) {
  StringTree tree = StringTree::MakeTwoLeafSplit(Direction::kRow, "A", "B");
  auto result =
      LayoutSplitTree<std::string>(tree, gfx::Rect(0, 0, 808, 600), kParams,
                                    HalfRatio);

  ASSERT_EQ(2u, result.leaves.size());
  EXPECT_EQ("A", result.leaves[0].leaf);
  EXPECT_EQ(gfx::Rect(0, 0, 400, 600), result.leaves[0].rect);
  EXPECT_EQ("B", result.leaves[1].leaf);
  EXPECT_EQ(gfx::Rect(408, 0, 400, 600), result.leaves[1].rect);

  ASSERT_EQ(1u, result.gutters.size());
  EXPECT_EQ(Direction::kRow, result.gutters[0].axis);
  EXPECT_TRUE(result.gutters[0].path.empty());
  EXPECT_EQ(gfx::Rect(400, 0, 8, 600), result.gutters[0].rect);
}

TEST(AtriumSplitTreeLayoutTest, TwoLeafColSplitsVerticallyWithRealRects) {
  StringTree tree = StringTree::MakeTwoLeafSplit(Direction::kCol, "A", "B");
  auto result =
      LayoutSplitTree<std::string>(tree, gfx::Rect(0, 0, 800, 608), kParams,
                                    HalfRatio);

  ASSERT_EQ(2u, result.leaves.size());
  EXPECT_EQ(gfx::Rect(0, 0, 800, 300), result.leaves[0].rect);
  EXPECT_EQ(gfx::Rect(0, 308, 800, 300), result.leaves[1].rect);

  ASSERT_EQ(1u, result.gutters.size());
  EXPECT_EQ(Direction::kCol, result.gutters[0].axis);
  EXPECT_EQ(gfx::Rect(0, 300, 800, 8), result.gutters[0].rect);
}

// row(A, col(B, C)) - the outer boundary resizes left/right (kRow), the
// inner boundary resizes up/down (kCol): a nested mixed layout with one
// gutter per axis.
TEST(AtriumSplitTreeLayoutTest, NestedRowOfLeafAndColGetsBothAxesGutters) {
  auto col_bc = StringTree::Node::MakeBranch(
      Direction::kCol, [] {
        std::vector<std::unique_ptr<StringTree::Node>> kids;
        kids.push_back(StringTree::Node::MakeLeaf("B"));
        kids.push_back(StringTree::Node::MakeLeaf("C"));
        return kids;
      }());
  std::vector<std::unique_ptr<StringTree::Node>> root_kids;
  root_kids.push_back(StringTree::Node::MakeLeaf("A"));
  root_kids.push_back(std::move(col_bc));
  StringTree tree(StringTree::Node::MakeBranch(Direction::kRow,
                                                std::move(root_kids)));

  auto result = LayoutSplitTree<std::string>(
      tree, gfx::Rect(0, 0, 808, 608), kParams, HalfRatio);

  ASSERT_EQ(3u, result.leaves.size());
  EXPECT_EQ("A", result.leaves[0].leaf);
  EXPECT_EQ(gfx::Rect(0, 0, 400, 608), result.leaves[0].rect);
  EXPECT_EQ("B", result.leaves[1].leaf);
  EXPECT_EQ(gfx::Rect(408, 0, 400, 300), result.leaves[1].rect);
  EXPECT_EQ("C", result.leaves[2].leaf);
  EXPECT_EQ(gfx::Rect(408, 308, 400, 300), result.leaves[2].rect);

  ASSERT_EQ(2u, result.gutters.size());
  EXPECT_EQ(Direction::kRow, result.gutters[0].axis);
  EXPECT_TRUE(result.gutters[0].path.empty());
  EXPECT_EQ(gfx::Rect(400, 0, 8, 608), result.gutters[0].rect);

  EXPECT_EQ(Direction::kCol, result.gutters[1].axis);
  EXPECT_EQ((SplitNodePath{1}), result.gutters[1].path);
  EXPECT_EQ(gfx::Rect(408, 300, 400, 8), result.gutters[1].rect);
}

// row(col(A,B), col(C,D)) - the 4-leaf grid. 3 boundaries total: the outer
// row split plus one col split inside each column, all independently
// resizable.
TEST(AtriumSplitTreeLayoutTest, FourLeafGridEachBranchGetsOwnGutter) {
  auto make_col = [](const std::string& top, const std::string& bottom) {
    std::vector<std::unique_ptr<StringTree::Node>> kids;
    kids.push_back(StringTree::Node::MakeLeaf(top));
    kids.push_back(StringTree::Node::MakeLeaf(bottom));
    return StringTree::Node::MakeBranch(Direction::kCol, std::move(kids));
  };
  std::vector<std::unique_ptr<StringTree::Node>> root_kids;
  root_kids.push_back(make_col("A", "B"));
  root_kids.push_back(make_col("C", "D"));
  StringTree tree(StringTree::Node::MakeBranch(Direction::kRow,
                                                std::move(root_kids)));

  auto result = LayoutSplitTree<std::string>(
      tree, gfx::Rect(0, 0, 808, 608), kParams, HalfRatio);

  ASSERT_EQ(4u, result.leaves.size());
  EXPECT_EQ(gfx::Rect(0, 0, 400, 300), result.leaves[0].rect);      // A
  EXPECT_EQ(gfx::Rect(0, 308, 400, 300), result.leaves[1].rect);    // B
  EXPECT_EQ(gfx::Rect(408, 0, 400, 300), result.leaves[2].rect);    // C
  EXPECT_EQ(gfx::Rect(408, 308, 400, 300), result.leaves[3].rect);  // D

  ASSERT_EQ(3u, result.gutters.size());
  EXPECT_EQ(Direction::kRow, result.gutters[0].axis);
  EXPECT_TRUE(result.gutters[0].path.empty());
  EXPECT_EQ(gfx::Rect(400, 0, 8, 608), result.gutters[0].rect);

  EXPECT_EQ(Direction::kCol, result.gutters[1].axis);
  EXPECT_EQ((SplitNodePath{0}), result.gutters[1].path);
  EXPECT_EQ(gfx::Rect(0, 300, 400, 8), result.gutters[1].rect);

  EXPECT_EQ(Direction::kCol, result.gutters[2].axis);
  EXPECT_EQ((SplitNodePath{1}), result.gutters[2].path);
  EXPECT_EQ(gfx::Rect(408, 300, 400, 8), result.gutters[2].rect);
}

// A dragged-thin ratio still respects kMinPaneExtent inside a nested branch -
// the same rule the root split already enforces, not a new one.
TEST(AtriumSplitTreeLayoutTest, NestedBranchRatioClampedToMinPaneExtent) {
  auto col_bc = [] {
    std::vector<std::unique_ptr<StringTree::Node>> kids;
    kids.push_back(StringTree::Node::MakeLeaf("B"));
    kids.push_back(StringTree::Node::MakeLeaf("C"));
    return StringTree::Node::MakeBranch(Direction::kCol, std::move(kids));
  }();
  std::vector<std::unique_ptr<StringTree::Node>> root_kids;
  root_kids.push_back(StringTree::Node::MakeLeaf("A"));
  root_kids.push_back(std::move(col_bc));
  StringTree tree(StringTree::Node::MakeBranch(Direction::kRow,
                                                std::move(root_kids)));

  // Requests a near-zero ratio for the nested col branch (path {1}),
  // squeezing B toward 0px. kMinPaneExtent (200) must still win.
  std::function<double(const SplitNodePath&)> ratio_for =
      [](const SplitNodePath& path) {
        return path == SplitNodePath{1} ? 0.01 : 0.5;
      };

  // Height 3008 makes kMinPaneExtentPercentage's share (300) exceed
  // kMinPaneExtent (200), so the flat 200px floor is the one that binds.
  auto result = LayoutSplitTree<std::string>(
      tree, gfx::Rect(0, 0, 808, 3008), kParams, ratio_for);

  ASSERT_EQ(3u, result.leaves.size());
  EXPECT_EQ("B", result.leaves[1].leaf);
  EXPECT_EQ(200, result.leaves[1].rect.height());
}

}  // namespace
}  // namespace atrium
