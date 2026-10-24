// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_INSERTION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_INSERTION_H_

#include <memory>
#include <utility>
#include <vector>

#include "chrome/browser/ui/atrium/atrium_split_tree.h"

namespace atrium {

// zoneOf()'s L/R/T/B/C zones, swipe-lab.html ~line 4192-4194.
enum class SplitEdge { kLeft, kRight, kTop, kBottom, kCenter };

// The lab's `pageState.split.length>=4` cap, swipe-lab.html ~line 4245.
inline constexpr size_t kMaxSplitLeaves = 4;

enum class SplitInsertOutcome {
  kInserted,               // `drop` added as a new leaf at `target`'s edge.
  kSwapped,                // Center drop: `drop` replaced `target` in place.
  kRejectedSelfSplit,      // `drop` is `target`, or already a leaf in `tree`.
  kRejectedAtCapacity,     // `tree` already has kMaxSplitLeaves leaves.
  kRejectedTargetNotFound  // `target` is not a leaf of `tree`.
};

template <typename Leaf>
struct SplitInsertion {
  SplitInsertOutcome outcome = SplitInsertOutcome::kRejectedTargetNotFound;
  // Meaningful only for kInserted/kSwapped. Rejections leave this empty.
  AtriumSplitTree<Leaf> tree;
};

namespace internal {

template <typename Leaf>
using SplitNode = typename AtriumSplitTree<Leaf>::Node;

template <typename Leaf>
std::unique_ptr<SplitNode<Leaf>> CloneNode(const SplitNode<Leaf>& n) {
  if (n.is_leaf()) {
    return SplitNode<Leaf>::MakeLeaf(n.leaf());
  }
  std::vector<std::unique_ptr<SplitNode<Leaf>>> kids;
  for (const auto& kid : n.kids()) {
    kids.push_back(CloneNode<Leaf>(*kid));
  }
  return SplitNode<Leaf>::MakeBranch(n.dir(), std::move(kids));
}

template <typename Leaf>
bool ContainsLeaf(const SplitNode<Leaf>& n, const Leaf& value) {
  if (n.is_leaf()) {
    return n.leaf() == value;
  }
  for (const auto& kid : n.kids()) {
    if (ContainsLeaf<Leaf>(*kid, value)) {
      return true;
    }
  }
  return false;
}

// insertAtLeaf(n,target,drop,edge), swipe-lab.html ~line 2321.
template <typename Leaf>
std::unique_ptr<SplitNode<Leaf>> SpliceAtLeaf(const SplitNode<Leaf>& n,
                                               const Leaf& target,
                                               const Leaf& drop,
                                               SplitEdge edge,
                                               bool& found) {
  using Node = SplitNode<Leaf>;
  using Direction = typename AtriumSplitTree<Leaf>::Direction;
  if (n.is_leaf()) {
    if (!(n.leaf() == target)) {
      return CloneNode<Leaf>(n);
    }
    found = true;
    const Direction dir = (edge == SplitEdge::kLeft || edge == SplitEdge::kRight)
                               ? Direction::kRow
                               : Direction::kCol;
    std::vector<std::unique_ptr<Node>> kids;
    if (edge == SplitEdge::kLeft || edge == SplitEdge::kTop) {
      kids.push_back(Node::MakeLeaf(drop));
      kids.push_back(Node::MakeLeaf(target));
    } else {
      kids.push_back(Node::MakeLeaf(target));
      kids.push_back(Node::MakeLeaf(drop));
    }
    return Node::MakeBranch(dir, std::move(kids));
  }
  std::vector<std::unique_ptr<Node>> kids;
  for (const auto& kid : n.kids()) {
    kids.push_back(SpliceAtLeaf<Leaf>(*kid, target, drop, edge, found));
  }
  return Node::MakeBranch(n.dir(), std::move(kids));
}

// normTree(), swipe-lab.html ~line 2310 - SpliceAtLeaf's raw output needs
// this before it matches one of treeOf()'s canonical shapes.
template <typename Leaf>
std::unique_ptr<SplitNode<Leaf>> NormalizeNode(
    std::unique_ptr<SplitNode<Leaf>> n) {
  using Node = SplitNode<Leaf>;
  if (n->is_leaf()) {
    return n;
  }
  const auto dir = n->dir();
  std::vector<std::unique_ptr<Node>> kids;
  for (const auto& kid : n->kids()) {
    kids.push_back(CloneNode<Leaf>(*kid));
  }
  std::vector<std::unique_ptr<Node>> flat_kids;
  for (auto& kid : kids) {
    auto normalized = NormalizeNode<Leaf>(std::move(kid));
    if (!normalized->is_leaf() && normalized->dir() == dir) {
      for (const auto& gk : normalized->kids()) {
        flat_kids.push_back(CloneNode<Leaf>(*gk));
      }
    } else {
      flat_kids.push_back(std::move(normalized));
    }
  }
  if (flat_kids.size() == 1) {
    return std::move(flat_kids.front());
  }
  // LayoutNode reads one stored ratio per gutter, and a stored ratio only
  // exists for a two-kid branch. Flattening a same-direction drop produces
  // three kids, so fold the tail back into nested pairs, leaf order intact.
  while (flat_kids.size() > 2) {
    std::vector<std::unique_ptr<Node>> pair;
    pair.push_back(std::move(flat_kids[flat_kids.size() - 2]));
    pair.push_back(std::move(flat_kids.back()));
    flat_kids.pop_back();
    flat_kids.back() = Node::MakeBranch(dir, std::move(pair));
  }
  return Node::MakeBranch(dir, std::move(flat_kids));
}

// layoutOf() always tokenizing a 4+-leaf tree as "g", swipe-lab.html
// ~line 2305, and treeOf()'s "g" reconstruction, ~line 2289-2290.
template <typename Leaf>
std::unique_ptr<SplitNode<Leaf>> MakeCanonicalGrid(
    const std::vector<Leaf>& leaves) {
  using Node = SplitNode<Leaf>;
  using Direction = typename AtriumSplitTree<Leaf>::Direction;
  std::vector<std::unique_ptr<Node>> col1;
  col1.push_back(Node::MakeLeaf(leaves[0]));
  col1.push_back(Node::MakeLeaf(leaves[1]));
  std::vector<std::unique_ptr<Node>> col2;
  col2.push_back(Node::MakeLeaf(leaves[2]));
  col2.push_back(Node::MakeLeaf(leaves[3]));
  std::vector<std::unique_ptr<Node>> cols;
  cols.push_back(Node::MakeBranch(Direction::kCol, std::move(col1)));
  cols.push_back(Node::MakeBranch(Direction::kCol, std::move(col2)));
  return Node::MakeBranch(Direction::kRow, std::move(cols));
}

}  // namespace internal

// The self-split guard mirrors the lab's drop-site checks
// (`st===myT||pageState.split?.includes(st)`, ~line 4199 and 4224).
template <typename Leaf>
SplitInsertion<Leaf> InsertAtLeaf(const AtriumSplitTree<Leaf>& tree,
                                   const Leaf& target,
                                   const Leaf& drop,
                                   SplitEdge edge) {
  using Node = internal::SplitNode<Leaf>;
  SplitInsertion<Leaf> result;
  if (tree.empty()) {
    result.outcome = SplitInsertOutcome::kRejectedTargetNotFound;
    return result;
  }
  if (drop == target || internal::ContainsLeaf<Leaf>(*tree.root(), drop)) {
    result.outcome = SplitInsertOutcome::kRejectedSelfSplit;
    return result;
  }
  if (!internal::ContainsLeaf<Leaf>(*tree.root(), target)) {
    result.outcome = SplitInsertOutcome::kRejectedTargetNotFound;
    return result;
  }
  if (edge == SplitEdge::kCenter) {
    // swapLeaves()'s single-match branch, ~line 2328-2331 - `drop` is
    // guaranteed by the guard above not to already be in `tree`.
    struct ReplaceOnce {
      static std::unique_ptr<Node> Run(const Node& n,
                                        const Leaf& target,
                                        const Leaf& drop) {
        if (n.is_leaf()) {
          return Node::MakeLeaf(n.leaf() == target ? drop : n.leaf());
        }
        std::vector<std::unique_ptr<Node>> kids;
        for (const auto& kid : n.kids()) {
          kids.push_back(Run(*kid, target, drop));
        }
        return Node::MakeBranch(n.dir(), std::move(kids));
      }
    };
    result.tree =
        AtriumSplitTree<Leaf>(ReplaceOnce::Run(*tree.root(), target, drop));
    result.outcome = SplitInsertOutcome::kSwapped;
    return result;
  }
  if (tree.leaf_count() >= kMaxSplitLeaves) {
    result.outcome = SplitInsertOutcome::kRejectedAtCapacity;
    return result;
  }
  bool found = false;
  auto spliced =
      internal::SpliceAtLeaf<Leaf>(*tree.root(), target, drop, edge, found);
  DCHECK(found);
  auto normalized = internal::NormalizeNode<Leaf>(std::move(spliced));
  AtriumSplitTree<Leaf> new_tree(std::move(normalized));
  if (new_tree.leaf_count() == kMaxSplitLeaves) {
    new_tree = AtriumSplitTree<Leaf>(
        internal::MakeCanonicalGrid<Leaf>(new_tree.Leaves()));
  }
  result.tree = std::move(new_tree);
  result.outcome = SplitInsertOutcome::kInserted;
  return result;
}

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_INSERTION_H_
