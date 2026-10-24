// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_hotkey_defaults_unittest.cc
//
// The bindings NativeHotkeyMap compiles in and
// the ones the Rust core hands out are two separate lists, written months
// apart, and until now nothing held them next to each other. A disagreement
// between them is a bug the user sees directly: the Inputs pane reads an
// action off the core and the key that fires comes off the table here, so the
// pane can describe a shortcut the build does not have. This test found one such case,
// and the two lists agree everywhere now.
//
// These go through atrium_core_query rather than a copy of default_hotkeys(),
// so the assertion is against the bytes the running browser is actually seeded
// with. A third list would only drift a third way.

#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

// What the core makes of `json` as a hotkeys file. An empty string is an
// absent file, which is what LoadHotkeysJSON hands over on a fresh machine -
// the core answers it with the full defaults.
base::DictValue Query(const std::string& json) {
  char* raw = atrium_core_query("hotkeys", json.c_str(),
                               /*migration_instant=*/0.0);
  EXPECT_TRUE(raw);
  const std::string out(raw ? raw : "");
  atrium_core_string_free(raw);
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(out, base::JSON_PARSE_RFC);
  EXPECT_TRUE(parsed) << out;
  return std::move(parsed).value_or(base::DictValue());
}

// Every action id the core's HOTKEY_ACTIONS lists, read from the core rather
// than a hand-copied literal - a fourth list would only drift a fourth way.
std::vector<std::string> CoreHotkeyActionIds() {
  char* raw = atrium_core_query("hotkey-action-ids", "",
                                /*migration_instant=*/0.0);
  EXPECT_TRUE(raw);
  const std::string out(raw ? raw : "");
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(out, base::JSON_PARSE_RFC);
  EXPECT_TRUE(parsed && parsed->is_list()) << out;
  std::vector<std::string> ids;
  if (parsed && parsed->is_list()) {
    for (const base::Value& id : parsed->GetList()) {
      ids.push_back(id.GetString());
    }
  }
  return ids;
}

// A hotkeys file binding `combo` to each of `action_ids`, at this build's
// schema.
std::string FileBinding(base::span<const std::string_view> action_ids,
                        std::string_view combo) {
  base::DictValue actions;
  for (std::string_view action_id : action_ids) {
    base::ListValue combos;
    combos.Append(std::string(combo));
    actions.Set(action_id, std::move(combos));
  }
  base::DictValue file;
  file.Set("schemaVersion", kNativeHotkeySchemaVersion);
  file.Set("actions", std::move(actions));
  return base::WriteJson(file).value_or(std::string());
}

// The combos the core has for `action_id`, or nullopt when it has no entry at
// all. Absent and empty are different answers: SetFromJSON leaves an absent
// action on its compiled-in binding and treats an empty list as a deliberate
// unbind, so a helper that flattened the two would hide the one case that
// changes what a key does.
std::optional<std::vector<std::string>> CombosFor(const base::DictValue& file,
                                                  std::string_view action_id) {
  const base::DictValue* actions = file.FindDict("actions");
  if (!actions) {
    return std::nullopt;
  }
  const base::ListValue* combos = actions->FindList(action_id);
  if (!combos) {
    return std::nullopt;
  }
  std::vector<std::string> out;
  for (const base::Value& combo : *combos) {
    out.push_back(combo.GetString());
  }
  return out;
}

bool Contains(const std::vector<std::string>& combos, std::string_view combo) {
  return std::find(combos.begin(), combos.end(), combo) != combos.end();
}

// Deliberately combo-less actions, named so a blank combo on a
// future row still fails below rather than reading as another one of these.
constexpr auto kShipsWithNoDefaultCombo =
    std::to_array<std::string_view>({"createSpace"});

TEST(AtriumNativeHotkeyDefaultsTest, TheTableIsOneRowPerActionWithUniqueIds) {
  const base::span<const NativeHotkeyDefault> defaults = NativeHotkeyDefaults();
  ASSERT_EQ(defaults.size(), kNativeHotkeyActionCount);

  std::set<std::string_view> ids;
  std::set<std::string_view> combos;
  for (const NativeHotkeyDefault& entry : defaults) {
    EXPECT_FALSE(entry.action_id.empty());
    EXPECT_TRUE(ids.insert(entry.action_id).second) << entry.action_id;
    const bool listed_unassigned =
        std::ranges::find(kShipsWithNoDefaultCombo, entry.action_id) !=
        std::ranges::end(kShipsWithNoDefaultCombo);
    EXPECT_EQ(entry.combo.empty(), listed_unassigned)
        << entry.action_id
        << " disagrees with kShipsWithNoDefaultCombo about having a combo";
    if (entry.combo.empty()) {
      continue;
    }
    // Two actions sharing a compiled-in combo would be a conflict this build
    // shipped with, and MatchAction resolves a conflict silently, by
    // declaration order.
    EXPECT_TRUE(combos.insert(entry.combo).second) << entry.combo;
  }
}

// Walks the whole enum rather than the table, which is the direction that
// catches an action added to NativeHotkeyAction and forgotten in the table.
// NativeHotkeyDefaultFor would NOTREACHED on it, from NativeHotkeyMap's
// constructor, on the first key press of the launch.
TEST(AtriumNativeHotkeyDefaultsTest, EveryActionHasExactlyOneRow) {
  for (size_t i = 0; i <= static_cast<size_t>(NativeHotkeyAction::kMaxValue);
       ++i) {
    const auto action = static_cast<NativeHotkeyAction>(i);
    size_t rows = 0;
    for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
      rows += entry.action == action ? 1 : 0;
    }
    ASSERT_EQ(rows, 1u) << "action " << i;

    // And the two lookups agree about it, which is what the map reads.
    const NativeHotkeyDefault& entry = NativeHotkeyDefaultFor(action);
    EXPECT_EQ(entry.action, action);
    EXPECT_EQ(NativeHotkeyActionForId(entry.action_id),
              std::make_optional(action));
  }
}

// A hotkeys.json can name anything, so an id no row carries has to answer
// nullopt rather than resolving to some neighbouring row.
TEST(AtriumNativeHotkeyDefaultsTest, AnIdThisMapDoesNotRunIsNullopt) {
  EXPECT_EQ(NativeHotkeyActionForId("noSuchAction"), std::nullopt);
  EXPECT_EQ(NativeHotkeyActionForId(""), std::nullopt);
  // Case matters - the core's action names are camelCase and compared exactly.
  EXPECT_EQ(NativeHotkeyActionForId("TOGGLESIDEBAR"), std::nullopt);
}

// An id this map uses that the core drops is a binding a user can never make:
// load_hotkeys filters unknown action names out per entry, so the row would
// render, persist, and never reach the map.
TEST(AtriumNativeHotkeyDefaultsTest, EveryActionIdIsOneTheCoreKnows) {
  std::vector<std::string_view> ids;
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    ids.push_back(entry.action_id);
  }
  const base::DictValue loaded = Query(FileBinding(ids, "cmd+shift+f"));
  for (std::string_view action_id : ids) {
    const std::optional<std::vector<std::string>> combos =
        CombosFor(loaded, action_id);
    ASSERT_TRUE(combos.has_value()) << action_id;
    EXPECT_TRUE(Contains(*combos, "cmd+shift+f")) << action_id;
  }
}

// Empty - every core action now runs natively. Never add to this to make a
// build pass - add the NativeHotkeyAction instead, or drop the id from the
// core's HOTKEY_ACTIONS with the reason recorded there.
constexpr std::array<std::string_view, 0> kCoreActionsWithNoNativeAction{};

// The direction EveryActionIdIsOneTheCoreKnows does not check. goToSpace1..9
// and createSpace were each bindable in the core with nothing behind them, and
// each was found by hand rather than by a test.
TEST(AtriumNativeHotkeyDefaultsTest, EveryCoreActionRunsNatively) {
  const std::vector<std::string> ids = CoreHotkeyActionIds();
  EXPECT_FALSE(ids.empty()) << "the core listed no hotkey actions at all";
  for (const std::string& id : ids) {
    const bool excused =
        std::ranges::find(kCoreActionsWithNoNativeAction, id) !=
        std::ranges::end(kCoreActionsWithNoNativeAction);
    EXPECT_TRUE(NativeHotkeyActionForId(id).has_value() || excused)
        << id << " is bindable in the core and has no NativeHotkeyAction";
  }
}

// The drift guard proper. The core's default list for an action must CONTAIN
// the combo compiled in here - containment rather than equality, because the
// core carries extra bindings for several actions (nextTab also answers Cmd+J
// and Cmd+Shift+], nextSpace three more chords) and the map picks all of them
// up once seeded. What the two must never disagree about is the combo this
// build starts bound to, because that is also the one the IDC intercepts
// release when it goes.
TEST(AtriumNativeHotkeyDefaultsTest, TheCoreAgreesWithEveryCompiledInCombo) {
  const base::DictValue defaults = Query(std::string());
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    // goToSpace1..9 ship with no default on either side - the core
    // has never listed one for them, so there is nothing here to agree with.
    if (entry.combo.empty()) {
      EXPECT_FALSE(CombosFor(defaults, entry.action_id).has_value())
          << entry.action_id << " has a core default but no compiled-in one";
      continue;
    }
    const std::optional<std::vector<std::string>> combos =
        CombosFor(defaults, entry.action_id);
    ASSERT_TRUE(combos.has_value())
        << entry.action_id << " is bound to " << entry.combo
        << " here and has no core default at all";
    EXPECT_TRUE(Contains(*combos, entry.combo))
        << entry.action_id << " is bound to " << entry.combo << " here and to "
        << base::JoinString(*combos, ", ") << " in the core";
  }
}

// A payload the core emits has to be one SetFromJSON reads. These were 2 and 1
// respectively, which meant every seeded payload was refused wholesale and the
// map kept its compiled-in bindings for the life of the process.
TEST(AtriumNativeHotkeyDefaultsTest, TheSchemaVersionIsTheOneTheCoreEmits) {
  EXPECT_EQ(Query(std::string()).FindInt("schemaVersion"),
            std::make_optional(kNativeHotkeySchemaVersion));
}

// parse_combo fixes modifier order (cmd, shift, opt, ctrl) and lowercases, so
// a combo spelled another way here would come back from the core as a
// different string - and IsDefaultComboBound compares combos, so the stock key
// would read as released the moment the file was written back.
//
// Containment, not equality: a file at a version BELOW the core's current one
// is unioned with the defaults rather than replacing them, so an equality here
// would fail for the schema drift instead of the spelling and say the wrong
// thing about why.
TEST(AtriumNativeHotkeyDefaultsTest, EveryCompiledInComboIsSpeltCanonically) {
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    // Nothing to spell-check for goToSpace1..9 - they have no
    // compiled-in combo at all.
    if (entry.combo.empty()) {
      continue;
    }
    const std::string_view ids[] = {entry.action_id};
    const std::optional<std::vector<std::string>> loaded =
        CombosFor(Query(FileBinding(ids, entry.combo)), entry.action_id);
    ASSERT_TRUE(loaded.has_value()) << entry.action_id;
    EXPECT_TRUE(Contains(*loaded, entry.combo))
        << entry.action_id << " is spelt " << entry.combo << " here and comes "
        << "back from the core as " << base::JoinString(*loaded, ", ");
  }
}

// Not every core default survives the trip. NativeHotkeyMap::ParseCombo drops
// a combo with neither cmd nor ctrl, because performKeyEquivalent delivery is
// only guaranteed for those, and two of the core's space defaults are exactly
// that - so seeding leaves an action with FEWER combos than the Inputs pane
// lists for it. Pinned rather than fixed: which layer should give is a design
// call, and widening ParseCombo would start delivering chords this map has
// never claimed.
TEST(AtriumNativeHotkeyDefaultsTest, TheCoreShipsSpaceCombosThisMapCannotBind) {
  const base::DictValue defaults = Query(std::string());
  const std::optional<std::vector<std::string>> next =
      CombosFor(defaults, "nextSpace");
  ASSERT_TRUE(next.has_value());
  EXPECT_TRUE(Contains(*next, "opt+tab"));

  const std::optional<std::vector<std::string>> previous =
      CombosFor(defaults, "previousSpace");
  ASSERT_TRUE(previous.has_value());
  EXPECT_TRUE(Contains(*previous, "shift+opt+tab"));
}

// the core has shipped pinToggleTab on cmd+d since the store was
// written, so the Inputs pane listed the shortcut all along while no row here
// ran it and the key fell through to Bookmark This Tab.
TEST(AtriumNativeHotkeyDefaultsTest, CmdDRunsThePinToggle) {
  const std::optional<NativeHotkeyAction> action =
      NativeHotkeyActionForId("pinToggleTab");
  ASSERT_TRUE(action.has_value()) << "no row runs the core's pinToggleTab";
  EXPECT_EQ(NativeHotkeyDefaultFor(*action).combo, "cmd+d");
}

// The favourite tiles moved off the plain digits and must keep
// a chord: unbound, nine actions go dark. The ninth id is the core's own - a
// last slot rather than a ninth, which is Arc's reading.
TEST(AtriumNativeHotkeyDefaultsTest, EachDigitRunsItsOwnSidebarItemAction) {
  const auto kIds = std::to_array<std::string_view>(
      {"goToSidebarItem1", "goToSidebarItem2", "goToSidebarItem3",
       "goToSidebarItem4", "goToSidebarItem5", "goToSidebarItem6",
       "goToSidebarItem7", "goToSidebarItem8", "goToSidebarItemLast"});
  std::set<NativeHotkeyAction> actions;
  for (size_t i = 0; i < kIds.size(); ++i) {
    const std::string combo =
        "cmd+opt+" + std::string(1, '1' + static_cast<char>(i));
    const std::optional<NativeHotkeyAction> action =
        NativeHotkeyActionForId(kIds[i]);
    ASSERT_TRUE(action.has_value()) << kIds[i] << " has no row";
    EXPECT_EQ(NativeHotkeyDefaultFor(*action).combo, combo);
    // One action per digit, so any one of them can be rebound alone.
    EXPECT_TRUE(actions.insert(*action).second) << kIds[i];
  }
}

// The plain digits are the spaces. A digit bound to two families at once is
// what made one press look like two actions.
TEST(AtriumNativeHotkeyDefaultsTest, EachSpaceDigitRunsItsOwnSpaceAction) {
  const auto kIds = std::to_array<std::string_view>(
      {"goToSpace1", "goToSpace2", "goToSpace3", "goToSpace4", "goToSpace5",
       "goToSpace6", "goToSpace7", "goToSpace8", "goToSpace9"});
  std::set<NativeHotkeyAction> actions;
  for (size_t i = 0; i < kIds.size(); ++i) {
    const std::string combo =
        "cmd+" + std::string(1, '1' + static_cast<char>(i));
    const std::optional<NativeHotkeyAction> action =
        NativeHotkeyActionForId(kIds[i]);
    ASSERT_TRUE(action.has_value()) << kIds[i] << " has no row";
    EXPECT_EQ(NativeHotkeyDefaultFor(*action).combo, combo);
    // One action per digit, so any one of them can be rebound alone.
    EXPECT_TRUE(actions.insert(*action).second) << kIds[i];
    EXPECT_EQ(NativeHotkeyActionForSpaceIndex(static_cast<int>(i)), action)
        << "rail position " << i;
  }
}

TEST(AtriumNativeHotkeyDefaultsTest, NoSpaceActionPastTheNinth) {
  EXPECT_EQ(NativeHotkeyActionForSpaceIndex(9), std::nullopt);
  EXPECT_EQ(NativeHotkeyActionForSpaceIndex(-1), std::nullopt);
}

}  // namespace

// cmd+shift+] moved no tab. charactersIgnoringModifiers keeps shift,
// so the event carried "}" while the store spelled the binding "]".
TEST(AtriumNativeHotkeyDefaultsTest, AShiftedGlyphMapsBackToItsUnshiftedKey) {
  EXPECT_EQ(UnshiftedGlyph("}"), "]") << "the pair that made this a bug";
  EXPECT_EQ(UnshiftedGlyph("{"), "[") << "and its mirror, bound to previousTab";
  EXPECT_EQ(UnshiftedGlyph("!"), "1");
  EXPECT_EQ(UnshiftedGlyph("?"), "/");
  EXPECT_EQ(UnshiftedGlyph(":"), ";");
  EXPECT_EQ(UnshiftedGlyph("~"), "`");

  // A glyph shift does not change has no unshifted form to offer, and neither
  // does an unknown one. Both must come back empty rather than guessing, or the
  // matcher would compare against a character nobody typed.
  EXPECT_EQ(UnshiftedGlyph("]"), "") << "already unshifted";
  EXPECT_EQ(UnshiftedGlyph("a"), "") << "letters are handled by lowercasing";
  EXPECT_EQ(UnshiftedGlyph(""), "");
  EXPECT_EQ(UnshiftedGlyph("}}"), "") << "one glyph at a time";
}

// The bindings this actually exists to rescue. If the core ever stops shipping a
// bracket chord for these, the fix above is dead weight and should go with it.
TEST(AtriumNativeHotkeyDefaultsTest, TheTabCycleStillShipsABracketChord) {
  const base::DictValue defaults = Query(std::string());
  const std::optional<std::vector<std::string>> next =
      CombosFor(defaults, "nextTab");
  const std::optional<std::vector<std::string>> previous =
      CombosFor(defaults, "previousTab");
  ASSERT_TRUE(next.has_value());
  ASSERT_TRUE(previous.has_value());
  EXPECT_TRUE(Contains(*next, "cmd+shift+]"))
      << "nextTab no longer carries cmd+shift+]";
  EXPECT_TRUE(Contains(*previous, "cmd+shift+["))
      << "previousTab no longer carries cmd+shift+[";
}

TEST(AtriumNativeHotkeyDefaultsTest, TheClaimedChordsAreReserved) {
  EXPECT_TRUE(IsReservedHotkeyCombo("tab", false, false, false, true));
  EXPECT_TRUE(IsReservedHotkeyCombo("tab", false, true, false, true));
  EXPECT_TRUE(IsReservedHotkeyCombo("w", true, true, false, false))
      << "cmd+shift+w closes the selected window, claimed before this map";
}

// Neighbours of the two claimed chords. A rule that swallowed these would take
// bindings away from actions nothing is shadowing.
TEST(AtriumNativeHotkeyDefaultsTest, NeighbouringChordsStayBindable) {
  EXPECT_FALSE(IsReservedHotkeyCombo("w", true, false, false, false))
      << "cmd+w is Close Tab, not ours";
  EXPECT_FALSE(IsReservedHotkeyCombo("w", true, true, true, false))
      << "adding opt makes a chord nothing claims";
  EXPECT_FALSE(IsReservedHotkeyCombo("w", false, true, false, true));
  EXPECT_FALSE(IsReservedHotkeyCombo("tab", true, false, false, false));
  EXPECT_FALSE(IsReservedHotkeyCombo("t", true, false, false, false));
}

// The guard that stops a compiled-in default going silently dead: ParseCombo
// refuses a reserved combo, and ResetToDefaults CHECKs on that refusal.
TEST(AtriumNativeHotkeyDefaultsTest, NoDefaultBindingIsReserved) {
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    // goToSpace1..9 have no default combo to check.
    if (entry.combo.empty()) {
      continue;
    }
    const std::vector<std::string> segments =
        base::SplitString(std::string(entry.combo), "+", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);
    ASSERT_FALSE(segments.empty()) << entry.action_id;
    bool cmd = false, shift = false, opt = false, ctrl = false;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
      cmd = cmd || segments[i] == "cmd";
      shift = shift || segments[i] == "shift";
      opt = opt || segments[i] == "opt";
      ctrl = ctrl || segments[i] == "ctrl";
    }
    EXPECT_FALSE(IsReservedHotkeyCombo(segments.back(), cmd, shift, opt, ctrl))
        << entry.action_id << " is bound to the reserved combo " << entry.combo;
  }
}

}  // namespace atrium
