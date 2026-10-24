// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/check_op.h"

namespace atrium {

// Mirrors docs/lab/swipe-lab.html's {dir:"row"|"col", kids:[...]} shape
// (treeOf()/layoutOf(), ~line 2280). `Leaf` should be a small, non-owning
// value (e.g. a raw_ptr) - this type never constructs or destroys one.
template <typename Leaf>
class AtriumSplitTree {
 public:
  enum class Direction { kRow, kCol };

  class Node {
   public:
    static std::unique_ptr<Node> MakeLeaf(Leaf value) {
      return std::unique_ptr<Node>(new Node(std::move(value)));
    }
    static std::unique_ptr<Node> MakeBranch(
        Direction dir,
        std::vector<std::unique_ptr<Node>> kids) {
      return std::unique_ptr<Node>(new Node(dir, std::move(kids)));
    }

    // Drops the first leaf equal to `value` from the subtree at `n`. Null out
    // means `n` itself was that leaf. A branch left with a single kid
    // dissolves into that kid, so a two-leaf split becomes a bare leaf again.
    static std::unique_ptr<Node> DropLeaf(std::unique_ptr<Node> n,
                                          const Leaf& value,
                                          bool& removed) {
      if (n->is_leaf()) {
        if (!removed && n->leaf() == value) {
          removed = true;
          return nullptr;
        }
        return n;
      }
      std::vector<std::unique_ptr<Node>> kept;
      for (auto& kid : n->kids_) {
        auto result = DropLeaf(std::move(kid), value, removed);
        if (result) {
          kept.push_back(std::move(result));
        }
      }
      if (kept.size() == 1) {
        return std::move(kept.front());
      }
      n->kids_ = std::move(kept);
      return n;
    }

    bool is_leaf() const { return kids_.empty(); }
    const Leaf& leaf() const { return leaf_; }
    Direction dir() const { return dir_; }
    const std::vector<std::unique_ptr<Node>>& kids() const { return kids_; }

   private:
    explicit Node(Leaf value) : leaf_(std::move(value)) {}
    Node(Direction dir, std::vector<std::unique_ptr<Node>> kids)
        : dir_(dir), kids_(std::move(kids)) {}

    Leaf leaf_{};
    Direction dir_ = Direction::kRow;
    std::vector<std::unique_ptr<Node>> kids_;
  };

  AtriumSplitTree() = default;
  explicit AtriumSplitTree(std::unique_ptr<Node> root)
      : root_(std::move(root)) {}
  AtriumSplitTree(AtriumSplitTree&&) = default;
  AtriumSplitTree& operator=(AtriumSplitTree&&) = default;

  // Today's no-split shape: one leaf, no branch.
  static AtriumSplitTree MakeSingleLeaf(Leaf value) {
    return AtriumSplitTree(Node::MakeLeaf(std::move(value)));
  }

  // Today's only split shape: a root branch with exactly two leaf kids -
  // treeOf()'s 2-tab case ({dir, kids:tabs.map(L)}).
  static AtriumSplitTree MakeTwoLeafSplit(Direction dir,
                                          Leaf primary,
                                          Leaf secondary) {
    std::vector<std::unique_ptr<Node>> kids;
    kids.push_back(Node::MakeLeaf(std::move(primary)));
    kids.push_back(Node::MakeLeaf(std::move(secondary)));
    return AtriumSplitTree(Node::MakeBranch(dir, std::move(kids)));
  }

  bool empty() const { return root_ == nullptr; }
  const Node* root() const { return root_.get(); }

  // Pre-order leaf walk - layoutOf()'s leaves collection.
  std::vector<Leaf> Leaves() const {
    std::vector<Leaf> out;
    if (root_) {
      CollectLeaves(*root_, out);
    }
    return out;
  }

  // Leaf count via a single walk - no allocation (unlike Leaves()).
  size_t leaf_count() const {
    size_t count = 0;
    if (root_) {
      CountLeaves(*root_, count);
    }
    return count;
  }

  // Drops the first leaf equal to `value` - see Node::DropLeaf. Removing the
  // last leaf empties the tree.
  bool RemoveLeaf(const Leaf& value) {
    if (!root_) {
      return false;
    }
    bool removed = false;
    root_ = Node::DropLeaf(std::move(root_), value, removed);
    return removed;
  }

  // The leaf at `index` in pre-order. `index` must be < leaf_count().
  Leaf LeafAt(size_t index) const {
    DCHECK_LT(index, leaf_count());
    const Node* found = nullptr;
    size_t seen = 0;
    FindLeafAt(*root_, index, seen, found);
    return found->leaf();
  }

 private:
  static void CollectLeaves(const Node& n, std::vector<Leaf>& out) {
    if (n.is_leaf()) {
      out.push_back(n.leaf());
      return;
    }
    for (const auto& kid : n.kids()) {
      CollectLeaves(*kid, out);
    }
  }

  static void CountLeaves(const Node& n, size_t& count) {
    if (n.is_leaf()) {
      ++count;
      return;
    }
    for (const auto& kid : n.kids()) {
      CountLeaves(*kid, count);
    }
  }

  static void FindLeafAt(const Node& n,
                         size_t index,
                         size_t& seen,
                         const Node*& found) {
    if (found) {
      return;
    }
    if (n.is_leaf()) {
      if (seen == index) {
        found = &n;
      }
      ++seen;
      return;
    }
    for (const auto& kid : n.kids()) {
      FindLeafAt(*kid, index, seen, found);
      if (found) {
        return;
      }
    }
  }

  std::unique_ptr<Node> root_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_H_
