// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// These tests prove C++ reaches the Rust core through the
// hand-written header, and that the two agree about the shape of the ABI.
//
// The value of these tests is mostly in the link: they only build if the crate
// compiled under GN, exported the symbols unmangled, and the header declared
// them compatibly. cbindgen is not vendored, so nothing else checks that.
//
// What each store DOES with a file is tested against the golden corpus - see
// fixtures/stores and `make corpus-check`. These cover the boundary.

#include "third_party/atrium/core/include/atrium_core.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// 2026-01-01T00:00:00Z in seconds since Swift's reference date. The same
// instant the corpus harnesses use.
constexpr double kMigrationInstant = 788918400.0;

// Loads through the ABI and hands back the JSON, or "" when the core refused.
std::string Query(const char* store, const char* json) {
  char* raw = atrium_core_query(store, json, kMigrationInstant);
  if (!raw) {
    return std::string();
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

TEST(AtriumCoreAbiTest, TheLinkedCoreReportsTheHeadersAbiVersion) {
  EXPECT_EQ(atrium_core_abi_version(), ATRIUM_CORE_ABI_VERSION);
}

// A zero would also equal a zero, so pin the number the header carries.
// Bumping the ABI is meant to be a deliberate edit to this line.
TEST(AtriumCoreAbiTest, TheAbiVersionIsSixtyFive) {
  EXPECT_EQ(atrium_core_abi_version(), 65u);
}

// The version number pins a count, not which functions - an export landed
// only here or only in the header still compiles. Update this list by hand
// whenever an export is added, renamed, or removed.
constexpr const char* kExpectedExports[] = {
    "atrium_core_abi_version",
    "atrium_core_active_space_index",
    "atrium_core_active_space_profile_id",
    "atrium_core_add_doorway",
    "atrium_core_add_marginal",
    "atrium_core_add_profile",
    "atrium_core_archive_space_tabs",
    "atrium_core_archive_tab",
    "atrium_core_archive_tabs_below",
    "atrium_core_archived_overview",
    "atrium_core_archived_remove",
    "atrium_core_assign_tab_to_group",
    "atrium_core_bind_tab_to_row",
    "atrium_core_bytes_free",
    "atrium_core_close",
    "atrium_core_close_tab",
    "atrium_core_command_acts_on_visible_tab",
    "atrium_core_command_intercept",
    "atrium_core_create_group",
    "atrium_core_create_space",
    "atrium_core_delete_group",
    "atrium_core_delete_profile",
    "atrium_core_external_link_target",
    "atrium_core_favorite_keep_alive_seconds",
    "atrium_core_favorite_row",
    "atrium_core_forget_tab",
    "atrium_core_forget_visits",
    "atrium_core_get_space_note",
    "atrium_core_group_tab_by_tab_id",
    "atrium_core_hibernate_policy_for_tab",
    "atrium_core_icon_links",
    "atrium_core_import_preview",
    "atrium_core_import_run",
    "atrium_core_is_favorite_url",
    "atrium_core_is_open",
    "atrium_core_is_peek_source_url",
    "atrium_core_launch_plan",
    "atrium_core_marginalia_for_url",
    "atrium_core_move_tab_by_tab_id",
    "atrium_core_move_tab_id_to_space",
    "atrium_core_move_tab_run",
    "atrium_core_move_tab_to_space",
    "atrium_core_mozlz4_decode",
    "atrium_core_night_shift_offset",
    "atrium_core_note_create",
    "atrium_core_note_delete",
    "atrium_core_note_dismiss",
    "atrium_core_note_get",
    "atrium_core_note_set_body",
    "atrium_core_note_tab_replaced",
    "atrium_core_note_tab_title",
    "atrium_core_note_tab_url",
    "atrium_core_notes_overview",
    "atrium_core_open",
    "atrium_core_open_tab",
    "atrium_core_overlay_accent_tokens",
    "atrium_core_page_tint",
    "atrium_core_pin_folder",
    "atrium_core_pin_tab",
    "atrium_core_pin_tab_by_tab_id",
    "atrium_core_profile_search_engine",
    "atrium_core_query",
    "atrium_core_record_visit",
    "atrium_core_remove_cold_tab",
    "atrium_core_remove_favorite",
    "atrium_core_remove_pinned_tab",
    "atrium_core_rename_group",
    "atrium_core_rename_profile",
    "atrium_core_rename_space",
    "atrium_core_rename_tab",
    "atrium_core_reorder_favorite",
    "atrium_core_reorder_folder",
    "atrium_core_reorder_group_member",
    "atrium_core_reorder_pinned_item",
    "atrium_core_reorder_space",
    "atrium_core_reorder_tab",
    "atrium_core_resolved_ip_is_local",
    "atrium_core_restore_slice",
    "atrium_core_route_for_url",
    "atrium_core_save",
    "atrium_core_save_hotkeys",
    "atrium_core_save_routing",
    "atrium_core_save_settings",
    "atrium_core_set_active_space_index",
    "atrium_core_set_group_collapsed",
    "atrium_core_set_group_icon",
    "atrium_core_set_marginal_note",
    "atrium_core_set_pinned_collapsed",
    "atrium_core_set_profile_hib_threshold",
    "atrium_core_set_profile_hibernate",
    "atrium_core_set_profile_search_engine",
    "atrium_core_set_sidebar_width",
    "atrium_core_set_space_archive_ttl",
    "atrium_core_set_space_canvas_rect",
    "atrium_core_set_space_icon",
    "atrium_core_set_space_note",
    "atrium_core_set_space_profile",
    "atrium_core_set_space_theme",
    "atrium_core_set_tab_canvas_x",
    "atrium_core_settings_writable",
    "atrium_core_shelf_add",
    "atrium_core_shelf_overview",
    "atrium_core_shelf_remove",
    "atrium_core_sidebar_model",
    "atrium_core_sidebar_model_cached",
    "atrium_core_sidebar_width",
    "atrium_core_single_stop_theme",
    "atrium_core_snooze_tab",
    "atrium_core_snoozed_overview",
    "atrium_core_snoozed_remove",
    "atrium_core_space_for_profile",
    "atrium_core_space_id_at",
    "atrium_core_space_id_for_tab",
    "atrium_core_space_slot_for_id",
    "atrium_core_space_theme",
    "atrium_core_string_free",
    "atrium_core_sweep_archive",
    "atrium_core_tab_recency_end_cycle",
    "atrium_core_tab_recency_record_activation",
    "atrium_core_tab_recency_record_closed",
    "atrium_core_tab_recency_step",
    "atrium_core_theme_raster",
    "atrium_core_theme_tokens",
    "atrium_core_touch_tab",
    "atrium_core_ungroup_member",
    "atrium_core_unpin_tab",
    "atrium_core_visits_for_tab",
    "atrium_core_will_claim_command",
};

base::FilePath AtriumCoreDir() {
  base::FilePath root;
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root);
  return root.AppendASCII("third_party").AppendASCII("atrium").AppendASCII(
      "core");
}

std::string ReadTextFile(const base::FilePath& path) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string contents;
  CHECK(base::ReadFileToString(path, &contents)) << path;
  return contents;
}

// Same parse `the_header_declares_exactly_the_exported_functions` runs on the
// Rust side, ported rather than shared, so a bug in one derivation can't line
// up with the other and pass by coincidence.
std::vector<std::string> ParseRustExports(const std::string& lib_rs) {
  std::vector<std::string> names;
  for (std::string_view line : base::SplitStringPiece(
           lib_rs, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (!base::StartsWith(line, "pub ")) {
      continue;
    }
    line.remove_prefix(4);
    if (base::StartsWith(line, "unsafe extern \"C\" fn ")) {
      line.remove_prefix(21);
    } else if (base::StartsWith(line, "extern \"C\" fn ")) {
      line.remove_prefix(14);
    } else {
      continue;
    }
    size_t paren = line.find('(');
    if (paren == std::string_view::npos) {
      continue;
    }
    names.emplace_back(line.substr(0, paren));
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> ParseHeaderDeclarations(const std::string& header) {
  std::vector<std::string> names;
  for (std::string_view line : base::SplitStringPiece(
           header, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (base::StartsWith(line, "//")) {
      continue;
    }
    size_t at = line.find("atrium_core_");
    if (at == std::string_view::npos) {
      continue;
    }
    std::string_view rest = line.substr(at);
    size_t paren = rest.find('(');
    if (paren == std::string_view::npos) {
      continue;
    }
    names.emplace_back(rest.substr(0, paren));
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

TEST(AtriumCoreAbiTest, TheAbiExportsMatchThisPinnedList) {
  const std::vector<std::string> expected(std::begin(kExpectedExports),
                                           std::end(kExpectedExports));
  const base::FilePath dir = AtriumCoreDir();
  const std::string header =
      ReadTextFile(dir.AppendASCII("include").AppendASCII("atrium_core.h"));
  const std::string lib_rs =
      ReadTextFile(dir.AppendASCII("src").AppendASCII("lib.rs"));

  EXPECT_EQ(ParseHeaderDeclarations(header), expected)
      << "the header declares a different set of atrium_core_* functions "
         "than kExpectedExports - update both when the ABI changes";
  EXPECT_EQ(ParseRustExports(lib_rs), expected)
      << "the Rust crate exports a different set of atrium_core_* functions "
         "than kExpectedExports - update both when the ABI changes";
}

TEST(AtriumCoreAbiTest, AnImportPreviewWithNoFilesIsNotDetected) {
  char* raw = atrium_core_import_preview("arc", nullptr);
  ASSERT_TRUE(raw);
  const std::string answer(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(answer, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict()) << answer;
  EXPECT_EQ(parsed->GetDict().FindBool("detected"), false);
  EXPECT_EQ(*parsed->GetDict().FindString("source"), "arc");
}

// The exhaustive per-space case lives in atrium-core's own
// a_space_note_round_trips_and_stays_scoped_to_its_own_space - this proves
// only that the boundary itself carries the call.
TEST(AtriumCoreAbiTest, TheSpaceNoteRoundTripsAcrossTheAbi) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
  static constexpr char kSpacesJson[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
      "sidebarWidthMigrated":true}})";
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
  }
  atrium_core_close();
  ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));

  ASSERT_TRUE(
      atrium_core_set_space_note(0, "hello from C++", kMigrationInstant));
  char* raw = atrium_core_get_space_note(0);
  ASSERT_TRUE(raw);
  std::string json(raw);
  atrium_core_string_free(raw);
  EXPECT_NE(json.find("hello from C++"), std::string::npos) << json;

  atrium_core_close();
}

// The shelf round trip at this boundary - the exhaustive round trip is
// atrium-core's own the_shelf_abi_adds_removes_and_persists_across_a_reopen.
TEST(AtriumCoreAbiTest, TheShelfRoundTripsAcrossTheAbi) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
  static constexpr char kSpacesJson[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
      "sidebarWidthMigrated":true}})";
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
  }
  atrium_core_close();
  ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));

  char* added = atrium_core_shelf_add("page", "Example",
                                      "https://example.com/", "a",
                                      kMigrationInstant);
  ASSERT_TRUE(added);
  std::string added_json(added);
  atrium_core_string_free(added);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(added_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const std::string* id = parsed->GetDict().FindString("id");
  ASSERT_TRUE(id);

  char* overview_raw = atrium_core_shelf_overview();
  ASSERT_TRUE(overview_raw);
  std::string overview(overview_raw);
  atrium_core_string_free(overview_raw);
  EXPECT_NE(overview.find(*id), std::string::npos) << overview;
  EXPECT_NE(overview.find("\"spaceName\":\"Work\""), std::string::npos)
      << overview;

  EXPECT_TRUE(atrium_core_shelf_remove(id->c_str()));
  overview_raw = atrium_core_shelf_overview();
  ASSERT_TRUE(overview_raw);
  overview = overview_raw;
  atrium_core_string_free(overview_raw);
  EXPECT_EQ(overview.find(*id), std::string::npos) << overview;

  atrium_core_close();
}

// No ABI adds a row here: the auto-archive TTL and snooze trigger
// are not built, so both rows are seeded directly in the file.
TEST(AtriumCoreAbiTest, TheArchivedAndSnoozedListsRoundTripAcrossTheAbi) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
  static constexpr char kSpacesJson[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
      "sidebarWidthMigrated":true},
      "archived":[{"id":"arch-1","url":"https://example.com/",
      "title":"Example","spaceID":"a","archivedAt":10}],
      "snoozed":[{"id":"snz-1","url":"https://example.com/",
      "title":"Example","spaceID":"a","snoozedAt":10,"wakeAt":100,
      "alert":true}]})";
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
  }
  atrium_core_close();
  ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));

  char* archived_raw = atrium_core_archived_overview();
  ASSERT_TRUE(archived_raw);
  std::string archived(archived_raw);
  atrium_core_string_free(archived_raw);
  EXPECT_NE(archived.find("\"id\":\"arch-1\""), std::string::npos) << archived;
  EXPECT_NE(archived.find("\"spaceName\":\"Work\""), std::string::npos)
      << archived;

  char* snoozed_raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(snoozed_raw);
  std::string snoozed(snoozed_raw);
  atrium_core_string_free(snoozed_raw);
  EXPECT_NE(snoozed.find("\"id\":\"snz-1\""), std::string::npos) << snoozed;
  EXPECT_NE(snoozed.find("\"alert\":true"), std::string::npos) << snoozed;

  EXPECT_TRUE(atrium_core_archived_remove("arch-1"));
  EXPECT_TRUE(atrium_core_snoozed_remove("snz-1"));

  archived_raw = atrium_core_archived_overview();
  ASSERT_TRUE(archived_raw);
  archived = archived_raw;
  atrium_core_string_free(archived_raw);
  EXPECT_EQ(archived, "[]") << archived;

  snoozed_raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(snoozed_raw);
  snoozed = snoozed_raw;
  atrium_core_string_free(snoozed_raw);
  EXPECT_EQ(snoozed, "[]") << snoozed;

  atrium_core_close();
}

// the sweep timer's and the Snooze menu item's own calls.
TEST(AtriumCoreAbiTest, TheSweepArchiveAndSnoozeTabExportsCrossTheAbi) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
  static constexpr char kSpacesJson[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],
      "tabs":[{"url":"https://example.com/due","title":"Due"},
              {"url":"https://example.com/live","title":"Live"}],
      "order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
      "sidebarWidthMigrated":true}})";
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
  }
  atrium_core_close();
  ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));

  // Snooze the second row first - sweeping would take both, and the point
  // here is that each export addresses its own row.
  EXPECT_TRUE(atrium_core_snooze_tab(0, 0, 1, 3600.0, true, 0.0));

  char* snoozed_raw = atrium_core_snoozed_overview();
  ASSERT_TRUE(snoozed_raw);
  std::string snoozed(snoozed_raw);
  atrium_core_string_free(snoozed_raw);
  EXPECT_NE(snoozed.find("https://example.com/live"), std::string::npos)
      << snoozed;

  EXPECT_TRUE(atrium_core_sweep_archive("twelveHours", 50000.0));

  char* archived_raw = atrium_core_archived_overview();
  ASSERT_TRUE(archived_raw);
  std::string archived(archived_raw);
  atrium_core_string_free(archived_raw);
  EXPECT_NE(archived.find("https://example.com/due"), std::string::npos)
      << archived;
  EXPECT_EQ(archived.find("https://example.com/live"), std::string::npos)
      << archived;

  EXPECT_FALSE(atrium_core_sweep_archive("not-a-real-threshold", 50000.0));

  atrium_core_close();
}

// the entry point the sidebar consumer will call. Nothing calls it
// from the running browser yet - see the task for why the consumer cannot move
// until Rust owns the model - but the boundary is exercised here.
TEST(AtriumCoreAbiTest, TheSidebarModelComesBackAcrossTheAbi) {
  const std::string spaces =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],"tabs":[{"url":"https://example.com/one",
      "title":"One"}],"order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
      "sidebarWidthMigrated":true}})";
  const std::string live = R"([{"url":"https://example.com/one","id":11}])";

  char* raw = atrium_core_sidebar_model(spaces.c_str(), 0, live.c_str(),
                                        kMigrationInstant);
  ASSERT_NE(raw, nullptr);
  const std::string out(raw);
  atrium_core_string_free(raw);

  EXPECT_NE(out.find("\"spaceName\":\"Work\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"id\":\"11\""), std::string::npos)
      << "the live tab id has to reach the row: " << out;
}

// A store file this build refuses to read is NULL, not an empty model. An
// empty model renders as a sidebar with nothing in it, which reads as data
// loss.
TEST(AtriumCoreAbiTest, ARefusedStoreFileGivesNoSidebarModel) {
  const std::string refused =
      R"({"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}})";
  EXPECT_EQ(atrium_core_sidebar_model(refused.c_str(), 0, "[]",
                                      kMigrationInstant),
            nullptr);
}

TEST(AtriumCoreAbiTest, TheSidebarModelSurvivesNullInput) {
  EXPECT_EQ(atrium_core_sidebar_model(nullptr, 0, "[]", kMigrationInstant),
            nullptr);
  EXPECT_EQ(atrium_core_sidebar_model("{}", 0, nullptr, kMigrationInstant),
            nullptr);
}

// The case C++ is in once Swift is gone: it does not know which space the
// window's slot is showing, so it passes a negative index and the document's
// own activeSpaceID decides.
TEST(AtriumCoreAbiTest, ANegativeIndexLetsTheDocumentChooseTheActiveSpace) {
  const std::string spaces =
      R"({"spaces":[
      {"id":"a","name":"First","emoji":"1","tint":"#111111","profileID":"p",
       "pinnedTabs":[],"tabs":[],"order":0,"groups":[]},
      {"id":"b","name":"Second","emoji":"2","tint":"#222222","profileID":"p",
       "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
      "appState":{"activeSpaceID":"b","sidebarWidth":220,"schemaVersion":3,
      "archiveEpoch":1,"sidebarWidthMigrated":true}})";

  char* raw = atrium_core_sidebar_model(spaces.c_str(), -1, "[]",
                                        kMigrationInstant);
  ASSERT_NE(raw, nullptr);
  const std::string out(raw);
  atrium_core_string_free(raw);

  EXPECT_NE(out.find("\"spaceName\":\"Second\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"activeSpace\":1"), std::string::npos) << out;
}

// the pair that keeps the read off the UI thread.
//
// PushSidebarSnapshot runs on the UI thread, where base::ReadFileToString is a
// fatal DCHECK - it killed the app three seconds after every launch on
// 2026-08-09. atrium_core_open does the read once, somewhere blocking is
// allowed; the snapshot call after it touches no file.
class AtriumCoreCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(path_, kSpaces));
    atrium_core_close();
  }

  void TearDown() override { atrium_core_close(); }

  // One space so a model is unmistakable, and one live tab so the id lands.
  static constexpr char kSpaces[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
      "profileID":"p","pinnedTabs":[],"tabs":[{"url":"https://example.com/one",
      "title":"One"}],"order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
      "sidebarWidthMigrated":true}})";

  base::ScopedTempDir dir_;
  base::FilePath path_;
};

TEST_F(AtriumCoreCacheTest, ASnapshotBeforeOpenIsNullRatherThanEmpty) {
  EXPECT_FALSE(atrium_core_is_open());
  // Null sends the caller to the flat tab list. An empty model would render an
  // empty sidebar, which is indistinguishable from lost data.
  EXPECT_EQ(atrium_core_sidebar_model_cached(-1, "[]"), nullptr);
  EXPECT_EQ(atrium_core_active_space_index(), -1);
}

TEST_F(AtriumCoreCacheTest, AnOpenCoreProducesTheModelWithNoPath) {
  ASSERT_TRUE(atrium_core_open(path_.value().c_str(), kMigrationInstant));
  EXPECT_TRUE(atrium_core_is_open());

  const std::string live = R"([{"url":"https://example.com/one","id":11}])";
  char* raw = atrium_core_sidebar_model_cached(-1, live.c_str());
  ASSERT_NE(raw, nullptr);
  const std::string out(raw);
  atrium_core_string_free(raw);

  EXPECT_NE(out.find("\"spaceName\":\"Work\""), std::string::npos) << out;
  EXPECT_NE(out.find("\"id\":\"11\""), std::string::npos) << out;
}

// The property the whole design exists for. If this ever needs the file, the
// read has crept back onto the UI thread.
TEST_F(AtriumCoreCacheTest, ASnapshotAfterTheFileIsDeletedStillWorks) {
  ASSERT_TRUE(atrium_core_open(path_.value().c_str(), kMigrationInstant));

  char* before = atrium_core_sidebar_model_cached(-1, "[]");
  ASSERT_NE(before, nullptr);
  const std::string first(before);
  atrium_core_string_free(before);

  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::DeleteFile(path_));
  }

  char* after = atrium_core_sidebar_model_cached(-1, "[]");
  ASSERT_NE(after, nullptr) << "the document must survive its file";
  const std::string second(after);
  atrium_core_string_free(after);

  EXPECT_EQ(first, second);
}

TEST_F(AtriumCoreCacheTest, AMissingOrRefusedFileLeavesTheCacheShut) {
  const base::FilePath missing = dir_.GetPath().AppendASCII("nothing.json");
  EXPECT_FALSE(atrium_core_open(missing.value().c_str(), kMigrationInstant));
  EXPECT_FALSE(atrium_core_is_open());

  const base::FilePath refused = dir_.GetPath().AppendASCII("refused.json");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(
        refused,
        R"({"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}})"));
  }
  EXPECT_FALSE(atrium_core_open(refused.value().c_str(), kMigrationInstant));
  EXPECT_FALSE(atrium_core_is_open());
  EXPECT_EQ(atrium_core_sidebar_model_cached(-1, "[]"), nullptr);
}

TEST_F(AtriumCoreCacheTest, ANullPathAndNullStripAreSurvivable) {
  EXPECT_FALSE(atrium_core_open(nullptr, kMigrationInstant));
  ASSERT_TRUE(atrium_core_open(path_.value().c_str(), kMigrationInstant));
  EXPECT_EQ(atrium_core_sidebar_model_cached(-1, nullptr), nullptr);
}

TEST(AtriumCoreAbiTest, AStoreLoadsAndComesBackAsJson) {
  const std::string out = Query(
      "spaces",
      R"({"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":3,
      "archiveEpoch":1,"sidebarWidthMigrated":true}})");
  EXPECT_NE(out.find("\"schemaVersion\":6"), std::string::npos) << out;
}

// A file this build cannot read is NULL, not empty defaults. Defaults look
// writable and are not - see docs/model-rules.md R1.2.
TEST(AtriumCoreAbiTest, ARefusedFileComesBackNull) {
  char* raw = atrium_core_query(
      "spaces",
      R"({"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}})",
      kMigrationInstant);
  EXPECT_EQ(raw, nullptr);
}

TEST(AtriumCoreAbiTest, AnUnknownStoreNameComesBackNull) {
  EXPECT_EQ(atrium_core_query("no-such-store", "{}", kMigrationInstant),
            nullptr);
}

TEST(AtriumCoreAbiTest, ANullArgumentIsSurvivable) {
  EXPECT_EQ(atrium_core_query(nullptr, "{}", kMigrationInstant), nullptr);
  EXPECT_EQ(atrium_core_query("spaces", nullptr, kMigrationInstant), nullptr);
  atrium_core_string_free(nullptr);  // must not crash
}

// The v1 migration chain, across the boundary rather than only in Rust: the
// version is bumped, the epoch is stamped from the instant C++ passed, and the
// legacy 280 width is corrected.
TEST(AtriumCoreAbiTest, AV1FileComesBackMigrated) {
  const std::string out = Query(
      "spaces",
      R"({"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":1}})");
  EXPECT_NE(out.find("\"schemaVersion\":6"), std::string::npos) << out;
  EXPECT_NE(out.find("\"sidebarWidth\":220"), std::string::npos) << out;
  EXPECT_NE(out.find("\"archiveEpoch\":788918400"), std::string::npos) << out;
}

// the settings WRITE half. Every other export answers a
// question; this one is the only one that puts bytes on a path the caller
// owns, and its four return codes are what the settings window logs and shows
// apart - so each is pinned rather than read off the header.
class AtriumCoreSaveSettingsTest : public testing::Test {
 protected:
  void SetUp() override {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("settings.json");
  }

  // The whole settings object this build produces, with one bool set.
  //
  // Built from the core's own defaults rather than spelled out here: settings
  // adds fields WITHOUT a schema bump (docs/model-rules.md R1.3), so a literal
  // would go stale into a PARTIAL object - which the writer refuses, and this
  // would fail for a reason that has nothing to do with what it is testing.
  static std::string WholeObject(bool confirm_to_quit) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(Query("settings", ""), base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return std::string();
    }
    parsed->GetDict().Set("confirmToQuitEnabled", confirm_to_quit);
    return base::WriteJson(*parsed).value_or(std::string());
  }

  int32_t Save(const std::string& settings_json) {
    return atrium_core_save_settings(path_.AsUTF8Unsafe().c_str(),
                                     settings_json.c_str());
  }

  std::string OnDisk() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string out;
    base::ReadFileToString(path_, &out);
    return out;
  }

  void Stage(std::string_view contents) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(path_, contents));
  }

  base::ScopedTempDir dir_;
  base::FilePath path_;
};

// One field, across the ABI: toggle it, write it, read it back. The
// relaunch half needs a browser, but this is the part C++ owns. Nothing exists
// at the path yet, which is also a fresh profile's first save.
TEST_F(AtriumCoreSaveSettingsTest, AnEditIsWrittenAndTheNextReadFindsIt) {
  const std::string payload = WholeObject(true);
  ASSERT_FALSE(payload.empty());
  EXPECT_EQ(Save(payload), 1);

  const std::string reloaded = Query("settings", OnDisk().c_str());
  EXPECT_NE(reloaded.find("\"confirmToQuitEnabled\":true"), std::string::npos)
      << reloaded;
}

// The single most dangerous shape the export could accept. Every key past the
// first four is optional, so a missing key and a default are the same bytes -
// completing this one would reset every field the surface did not send.
TEST_F(AtriumCoreSaveSettingsTest, APartialObjectIsRefusedRatherThanCompleted) {
  EXPECT_EQ(Save(R"({"confirmToQuitEnabled":true})"), 0);
  base::ScopedAllowBlockingForTesting allow_blocking;
  EXPECT_FALSE(base::PathExists(path_))
      << "a refused payload must not leave a file behind";
}

TEST_F(AtriumCoreSaveSettingsTest, UnusableArgumentsAreRefusedRatherThanFatal) {
  const std::string payload = WholeObject(true);
  ASSERT_FALSE(payload.empty());
  EXPECT_EQ(atrium_core_save_settings(nullptr, payload.c_str()), 0);
  EXPECT_EQ(atrium_core_save_settings("", payload.c_str()), 0);
  EXPECT_EQ(atrium_core_save_settings(path_.AsUTF8Unsafe().c_str(), nullptr),
            0);
}

// docs/model-rules.md R1.2. The reader answers such a file with DEFAULTS, so
// the window shows values and the user can edit them - which is exactly why
// the refusal has to be a code the caller can tell apart and show, rather than
// a silent no-op.
TEST_F(AtriumCoreSaveSettingsTest, AFileThisBuildCannotReadIsDeclined) {
  const std::string newer =
      R"({"schemaVersion":999,"appIconVariant":"solarized-ink",
      "animationsEnabled":true,"startupBehavior":"restoreLastActive"})";
  Stage(newer);
  const std::string payload = WholeObject(true);
  ASSERT_FALSE(payload.empty());

  EXPECT_EQ(Save(payload), -2);
  EXPECT_EQ(OnDisk(), newer) << "the file has to be left exactly as it was";
}

// A MISSING parent directory is not a failure - the core creates it, which is
// what lets a fresh profile's first save work. A parent that is a regular FILE
// is, and it is the only way to reach -1 without a full disk.
TEST_F(AtriumCoreSaveSettingsTest, AWriteThatCannotLandReportsFailure) {
  const base::FilePath blocker = dir_.GetPath().AppendASCII("blocker");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(blocker, "not a directory"));
  }
  path_ = blocker.AppendASCII("settings.json");
  const std::string payload = WholeObject(true);
  ASSERT_FALSE(payload.empty());

  EXPECT_EQ(Save(payload), -1);
}

// Checked at this boundary rather than only in the Rust suite: a key this build
// does not know is copied back as the bytes the file carried, digits included.
// Parsing it would move the last ones.
TEST_F(AtriumCoreSaveSettingsTest, AKeyALaterBuildAddedSurvivesTheSave) {
  Stage(R"({"schemaVersion":1,"appIconVariant":"solarized-ink",
      "animationsEnabled":true,"startupBehavior":"restoreLastActive",
      "aKeyFromALaterBuild":0.1234567890123456789})");
  const std::string payload = WholeObject(true);
  ASSERT_FALSE(payload.empty());

  EXPECT_EQ(Save(payload), 1);
  EXPECT_NE(OnDisk().find("0.1234567890123456789"), std::string::npos)
      << OnDisk();
}

}  // namespace

// The exhaustive cases live in atrium-core's own
// the_marginalia_abi_stores_a_quote_edits_its_note_and_survives_a_reopen -
// this proves only that the three new exports carry across the boundary.
TEST(AtriumCoreAbiTest, RoutingDecidesAcrossTheAbi) {
  const std::string rules = R"({"schemaVersion":1,"rules":[
      {"id":"r1","enabled":true,"matchType":"contains",
       "pattern":"corp.example","targetSpaceID":"SPACE-A"}]})";

  char* raw = atrium_core_route_for_url(
      rules.c_str(), "https://mail.corp.example/inbox", nullptr);
  ASSERT_NE(nullptr, raw) << "a matching url must come back with its space";
  EXPECT_EQ("SPACE-A", std::string(raw));
  atrium_core_string_free(raw);

  // No match and every failure are the same answer on purpose, so both are
  // pinned here - a caller that cannot tell them apart still opens the url
  // where it was already going.
  EXPECT_EQ(nullptr, atrium_core_route_for_url(
                          rules.c_str(), "https://other.test/", nullptr));
  EXPECT_EQ(nullptr,
            atrium_core_route_for_url("not json", "https://x.test/", nullptr));
  EXPECT_EQ(nullptr,
            atrium_core_route_for_url(nullptr, "https://x.test/", nullptr));
  EXPECT_EQ(nullptr, atrium_core_route_for_url(rules.c_str(), nullptr, nullptr));

  const std::string excluding_rules = R"({"schemaVersion":2,"rules":[
      {"id":"r1","enabled":true,"matchType":"contains",
       "pattern":"corp.example","targetSpaceID":"SPACE-A",
       "excludedSourceSpaceIDs":["home"]}]})";
  EXPECT_EQ(nullptr,
            atrium_core_route_for_url(excluding_rules.c_str(),
                                      "https://mail.corp.example/inbox",
                                      "home"));
  char* still_routes = atrium_core_route_for_url(
      excluding_rules.c_str(), "https://mail.corp.example/inbox", "work");
  ASSERT_NE(nullptr, still_routes)
      << "a non-excluded source space must still route";
  EXPECT_EQ("SPACE-A", std::string(still_routes));
  atrium_core_string_free(still_routes);
}

TEST(AtriumCoreAbiTest, TheMarginaliaExportsCrossTheAbi) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
  static constexpr char kSpacesJson[] =
      R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
          "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
          "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
          "sidebarWidthMigrated":true}})";
  ASSERT_TRUE(base::WriteFile(path, kSpacesJson));

  atrium_core_close();
  ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));

  EXPECT_FALSE(atrium_core_add_marginal(0, "https://example.com/", "ab",
                                        kMigrationInstant))
      << "a two-character selection must be refused across the ABI";

  char* id_raw = atrium_core_add_marginal(0, "https://example.com/",
                                          "the passage", kMigrationInstant);
  ASSERT_TRUE(id_raw);
  const std::string id(id_raw);
  atrium_core_string_free(id_raw);
  EXPECT_TRUE(atrium_core_set_marginal_note(id.c_str(), "why it matters"));

  char* marks_raw = atrium_core_marginalia_for_url(0, "https://example.com/");
  ASSERT_TRUE(marks_raw);
  const std::string marks(marks_raw);
  atrium_core_string_free(marks_raw);
  EXPECT_NE(marks.find("the passage"), std::string::npos) << marks;
  EXPECT_NE(marks.find("why it matters"), std::string::npos) << marks;

  atrium_core_close();
}

TEST(AtriumCoreAbiTest, AMozLz4FileDecodesAcrossTheAbi) {
  const char kPacked[] = "mozLz40\0\x05\0\0\0\x50hello";
  char* raw = atrium_core_mozlz4_decode(kPacked, sizeof(kPacked) - 1);
  ASSERT_TRUE(raw);
  EXPECT_EQ("hello", std::string(raw));
  atrium_core_string_free(raw);
  EXPECT_FALSE(atrium_core_mozlz4_decode("mozLz4", 6));
}
