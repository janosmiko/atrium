// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_tab_cycle.h"

#include <utility>

#include "base/containers/flat_set.h"

namespace atrium {
namespace {

// Appends `list`'s rows in the order the surface draws them.
void CollectCycleRows(const base::ListValue* list,
                      std::vector<CycleRow>* out) {
  if (!list) {
    return;
  }
  for (const base::Value& row : *list) {
    const base::DictValue* dict = row.GetIfDict();
    if (!dict) {
      continue;
    }
    if (const std::string* kind = dict->FindString("kind");
        kind && *kind == "folder") {
      // A folder entry in `pinned` is not itself a row - its members are.
      CollectCycleRows(dict->FindList("tabs"), out);
      continue;
    }
    CycleRow entry;
    if (const std::string* id = dict->FindString("id")) {
      entry.tab_id = *id;
    }
    if (const std::string* url = dict->FindString("url")) {
      entry.url = *url;
    }
    entry.selected = dict->FindBool("selected").value_or(false);
    if (entry.tab_id.empty() && entry.url.empty()) {
      continue;  // Neither activatable nor openable - a decoration.
    }
    out->push_back(std::move(entry));
  }
}

// Appends the tabs `tiles` hold, in grid order. A tile's `id` is the
// favourite's URL rather than a tab id, so it cannot go through
// CollectCycleRows.
void CollectCycleTiles(const base::ListValue* tiles,
                       std::vector<CycleRow>* out) {
  if (!tiles) {
    return;
  }
  for (const base::Value& tile : *tiles) {
    const base::DictValue* dict = tile.GetIfDict();
    const std::string* tab_id = dict ? dict->FindString("tabId") : nullptr;
    if (!tab_id || tab_id->empty()) {
      // A tile holding no tab hides no row and stands for no live tab. It is a
      // bookmark, and stepping into one would open a page rather than move.
      continue;
    }
    CycleRow entry;
    entry.tab_id = *tab_id;
    if (const std::string* url = dict->FindString("url")) {
      entry.url = *url;
    }
    entry.selected = dict->FindBool("selected").value_or(false);
    out->push_back(std::move(entry));
  }
}

}  // namespace

std::vector<CycleRow> DrawnOrderRows(const base::DictValue& root) {
  std::vector<CycleRow> rows;
  CollectCycleRows(root.FindList("pinned"), &rows);
  if (const base::ListValue* folders = root.FindList("folders")) {
    for (const base::Value& folder : *folders) {
      if (const base::DictValue* dict = folder.GetIfDict()) {
        CollectCycleRows(dict->FindList("tabs"), &rows);
      }
    }
  }
  CollectCycleRows(root.FindList("tabs"), &rows);
  return rows;
}

std::vector<CycleRow> CycleOrderRows(const base::DictValue& root) {
  std::vector<CycleRow> rows;
  CollectCycleTiles(root.FindList("favorites"), &rows);
  base::flat_set<std::string> seen;
  for (const CycleRow& tile : rows) {
    seen.insert(tile.tab_id);
  }
  std::vector<CycleRow> drawn = DrawnOrderRows(root);
  for (CycleRow& row : drawn) {
    // A push between the claim and the core hiding the row carries both, and
    // the same tab twice sends one step back where the last came from. Empty
    // ids are exempt: every cold row has one and they are different rows.
    if (!row.tab_id.empty() && !seen.insert(row.tab_id).second) {
      continue;
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::optional<size_t> NextCycleTarget(const std::vector<CycleRow>& rows,
                                      int direction) {
  // One row is not a cycle. Returning it would re-activate the tab the user is
  // already on, which reads as the key doing nothing while actually doing
  // something - the worse of the two.
  if (rows.size() < 2 || direction == 0) {
    return std::nullopt;
  }
  const size_t count = rows.size();
  for (size_t i = 0; i < count; ++i) {
    if (!rows[i].selected) {
      continue;
    }
    // Modular, so both ends wrap. The + count keeps the intermediate positive
    // for a backward step from row 0.
    return (i + count + (direction > 0 ? 1 : count - 1)) % count;
  }
  // Nothing is selected - the active tab is not a row in this space, which
  // happens for a favourite-bound tab and while a space is being
  // switched. Enter the list from the end the user is moving towards rather
  // than refusing: the key should do something visible.
  return direction > 0 ? 0 : count - 1;
}

std::optional<size_t> NextSpaceIndex(size_t count, int active, int direction) {
  // One space is not a cycle, for the same reason one row is not.
  if (count < 2 || direction == 0) {
    return std::nullopt;
  }
  if (active < 0 || static_cast<size_t>(active) >= count) {
    return direction > 0 ? 0 : count - 1;
  }
  // Modular, so both ends wrap. The + count keeps the intermediate positive
  // for a backward step from space 0.
  const size_t from = static_cast<size_t>(active);
  return (from + count + (direction > 0 ? 1 : count - 1)) % count;
}

}  // namespace atrium
