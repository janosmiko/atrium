// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_settings_source.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_import_history.h"
#include "sql/database.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

namespace atrium {
namespace {

// One TEST, not two: the runner is a process-wide static, so a second test
// with its own TaskEnvironment would reuse a handle to a thread pool that the
// first test already tore down.
TEST(AtriumSettingsSourceTest, OneRunnerServesEveryWriterAndKeepsThemInOrder) {
  base::test::TaskEnvironment task_environment;

  // Whoever asks gets the same runner. Two runners is the race: the core reads,
  // merges and renames per call without a lock, so the older edit can rename
  // last and quietly replace the newer.
  EXPECT_EQ(SettingsWriteRunner(), SettingsWriteRunner());
  ASSERT_TRUE(SettingsWriteRunner());

  std::vector<std::string> order;
  bool inside = false;
  bool overlapped = false;

  // The sleep is the point: it holds the first write open long enough that an
  // unsequenced second one would start inside it.
  auto write = [&](const std::string& name) {
    overlapped = overlapped || inside;
    inside = true;
    base::PlatformThread::Sleep(base::Milliseconds(30));
    order.push_back(name);
    inside = false;
  };

  SettingsWriteRunner()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] { write("settings window"); }));
  SettingsWriteRunner()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] { write("address bar pin"); }));
  task_environment.RunUntilIdle();

  EXPECT_FALSE(overlapped);
  EXPECT_EQ(order, std::vector<std::string>(
                       {"settings window", "address bar pin"}));
}

// A rule added in the pane survives a restart, which this stands in for
// by round-tripping through the two functions the settings window
// actually calls rather than the Rust ABI directly.
TEST(AtriumSettingsSourceTest, SaveRoutingJSONRoundTripsThroughLoadRoutingJSON) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().Append("routing.json");
  ASSERT_TRUE(base::WriteFile(path, R"({"schemaVersion":1,"rules":[]})"));

  const std::string edited =
      R"({"schemaVersion":1,"rules":[{"id":"r1","enabled":true,)"
      R"("matchType":"contains","pattern":"corp.example",)"
      R"("targetSpaceID":"A"}]})";
  EXPECT_EQ(SaveRoutingJSON(path, edited), 1);

  const std::string read_back = LoadRoutingJSON(path);
  EXPECT_NE(read_back.find("corp.example"), std::string::npos) << read_back;
  EXPECT_NE(read_back.find(R"("targetSpaceID":"A")"), std::string::npos)
      << read_back;
}

// A rebind in the pane survives a restart, which this stands in for by
// round-tripping through the two functions the settings window actually
// calls rather than the Rust ABI directly.
TEST(AtriumSettingsSourceTest, SaveHotkeysJSONRoundTripsThroughLoadHotkeysJSON) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  base::FilePath path = dir.GetPath().Append("hotkeys.json");
  ASSERT_TRUE(base::WriteFile(path, R"({"schemaVersion":2,"actions":{}})"));

  const std::string edited =
      R"({"schemaVersion":2,"actions":{"toggleSidebar":["cmd+shift+k"]}})";
  EXPECT_EQ(SaveHotkeysJSON(path, edited), 1);

  const std::string read_back = LoadHotkeysJSON(path);
  EXPECT_NE(read_back.find("cmd+shift+k"), std::string::npos) << read_back;
}

TEST(AtriumSettingsSourceTest, AKnownActionAndAParsableComboAreValid) {
  EXPECT_TRUE(HotkeysAreValid(
      R"({"schemaVersion":2,"actions":{"toggleSidebar":["cmd+shift+j"]}})"));
}

TEST(AtriumSettingsSourceTest, AnUnknownActionIdIsRefused) {
  EXPECT_FALSE(HotkeysAreValid(
      R"({"schemaVersion":2,"actions":{"notARealAction":["cmd+j"]}})"));
}

TEST(AtriumSettingsSourceTest, AComboWithNoCmdOrCtrlIsRefused) {
  EXPECT_FALSE(HotkeysAreValid(
      R"({"schemaVersion":2,"actions":{"toggleSidebar":["opt+a"]}})"));
}

TEST(AtriumSettingsSourceTest, AnEmptyActionsMapIsValid) {
  EXPECT_TRUE(HotkeysAreValid(R"({"schemaVersion":2,"actions":{}})"));
}

constexpr double kMigrationInstant = 788918400.0;

class RoutingRulesAreValidTest : public testing::Test {
 protected:
  void SetUp() override {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    base::FilePath path = dir_.GetPath().Append("spaces.json");
    static constexpr char kSpacesJson[] =
        R"({"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
        "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
        "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
        "sidebarWidthMigrated":true}})";
    ASSERT_TRUE(base::WriteFile(path, kSpacesJson));
    atrium_core_close();
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), kMigrationInstant));
  }

  void TearDown() override { atrium_core_close(); }

  base::ScopedTempDir dir_;
};

std::string OneRule(const char* match_type, const char* pattern,
                    const char* target_space_id) {
  return std::string(R"({"schemaVersion":1,"rules":[{"id":"r1",)") +
        R"("enabled":true,"matchType":")" + match_type +
        R"(","pattern":")" + pattern + R"(","targetSpaceID":")" +
        target_space_id + R"("}]})";
}

TEST_F(RoutingRulesAreValidTest, AKnownMatchTypeARealPatternAndSpaceIsValid) {
  EXPECT_TRUE(RoutingRulesAreValid(OneRule("contains", "corp.example", "a")));
}

TEST_F(RoutingRulesAreValidTest, AnUnknownMatchTypeIsRefused) {
  EXPECT_FALSE(
      RoutingRulesAreValid(OneRule("startsWith", "corp.example", "a")));
}

TEST_F(RoutingRulesAreValidTest, AnEmptyPatternIsRefused) {
  EXPECT_FALSE(RoutingRulesAreValid(OneRule("contains", "", "a")));
}

TEST_F(RoutingRulesAreValidTest, ATargetSpaceThatDoesNotExistIsRefused) {
  EXPECT_FALSE(
      RoutingRulesAreValid(OneRule("contains", "corp.example", "not-a-space")));
}

TEST_F(RoutingRulesAreValidTest, AnEmptyRuleListIsValid) {
  EXPECT_TRUE(RoutingRulesAreValid(R"({"schemaVersion":1,"rules":[]})"));
}

TEST_F(RoutingRulesAreValidTest, ACurrentTargetAndUnknownExclusionsAreValid) {
  EXPECT_TRUE(RoutingRulesAreValid(
      R"({"schemaVersion":2,"defaultTargetSpaceID":"",
          "rules":[{"id":"r","enabled":true,"matchType":"contains",
          "pattern":"github.com","targetSpaceID":"current",
          "excludedSourceSpaceIDs":["gone"]}]})"));
}

TEST_F(RoutingRulesAreValidTest, AFallbackNamingAMissingSpaceIsInvalid) {
  EXPECT_FALSE(RoutingRulesAreValid(
      R"({"schemaVersion":2,"defaultTargetSpaceID":"nowhere","rules":[]})"));
}

TEST(AtriumSettingsSourceTest, ArcDataDirPrefersTheOverride) {
  const base::FilePath app_support("/Users/u/Library/Application Support");
  EXPECT_EQ(base::FilePath("/tmp/arc"),
            ImportDataDir("arc", "/tmp/arc", app_support));
  EXPECT_EQ(app_support.Append("Arc"), ImportDataDir("arc", "", app_support));
  EXPECT_TRUE(ImportDataDir("arc", "", base::FilePath()).empty());
}

TEST(AtriumSettingsSourceTest, ArcFilesAreReadOnlyWhenTheSidebarFileExists) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_FALSE(ImportDetected("arc", dir.GetPath()));
  EXPECT_FALSE(ReadImportFiles("arc", dir.GetPath()).has_value());

  ASSERT_TRUE(base::WriteFile(dir.GetPath().Append("StorableSidebar.json"),
                              R"({"sidebar":{}})"));
  ASSERT_TRUE(base::CreateDirectory(dir.GetPath().Append("User Data")));
  ASSERT_TRUE(base::WriteFile(
      dir.GetPath().Append("User Data").Append("Local State"), "{}"));
  EXPECT_TRUE(ImportDetected("arc", dir.GetPath()));

  std::optional<std::string> files = ReadImportFiles("arc", dir.GetPath());
  ASSERT_TRUE(files.has_value());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*files, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(R"({"sidebar":{}})", *dict.FindString("StorableSidebar.json"));
  EXPECT_EQ("{}", *dict.FindString("Local State"));
  EXPECT_FALSE(dict.Find("StorableLinkRouting.json"))
      << "a missing file is left out, not sent empty";
}

TEST(AtriumSettingsSourceTest, TheImportOfferNeedsAFreshProfileAndASource) {
  EXPECT_TRUE(ShouldOfferImport(true, true, "{}"));
  EXPECT_FALSE(ShouldOfferImport(false, true, "{}"));
  EXPECT_FALSE(ShouldOfferImport(true, false, "{}"));
  EXPECT_FALSE(ShouldOfferImport(true, true, R"({"importPromptShown":true})"));
  EXPECT_TRUE(ShouldOfferImport(true, true, R"({"importPromptShown":false})"));
}

TEST(AtriumSettingsSourceTest, WithImportPromptShownSetsOnlyThatKey) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      WithImportPromptShown(R"({"schemaVersion":1,"pipAlwaysOnTop":false})"),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ(true, parsed->GetDict().FindBool("importPromptShown"));
  EXPECT_EQ(false, parsed->GetDict().FindBool("pipAlwaysOnTop"));
  EXPECT_EQ(1, parsed->GetDict().FindInt("schemaVersion"));
  EXPECT_EQ("", WithImportPromptShown("not json"));
}

TEST(AtriumSettingsSourceTest, AnImportResavesOnlyAfterASaveDuringTheRun) {
  constexpr char kAtStart[] =
      R"({"confirmToQuitEnabled":false,"searchEngines":[]})";
  constexpr char kPageSave[] =
      R"({"confirmToQuitEnabled":true,"searchEngines":[]})";
  constexpr char kImported[] =
      R"({"confirmToQuitEnabled":false,"searchEngines":[{"id":"i-1"}]})";

  EXPECT_EQ("", ImportSettingsToResave(kAtStart, kAtStart, kImported))
      << "no save during the run, so the import's own save stands";
  EXPECT_EQ(R"({"confirmToQuitEnabled":true,"searchEngines":[{"id":"i-1"}]})",
            ImportSettingsToResave(kAtStart, kPageSave, kImported))
      << "the page's edit stays, and the import's engines come back";
  EXPECT_EQ("", ImportSettingsToResave(kAtStart, kPageSave, ""));
  EXPECT_EQ("", ImportSettingsToResave(kAtStart, "not json", kImported));
}

namespace {

// One space holding one tab: the smallest sidebar the Arc reader accepts.
constexpr char kTinyArcSidebar[] =
    R"({"sidebar":{"containers":[{"global":{}},{"topAppsContainerIDs":[],)"
    R"("spaces":["S",{"id":"S","title":"Solo","profile":{"default":true},)"
    R"("containerIDs":["unpinned","U"]}],"items":["U",{"id":"U",)"
    R"("childrenIds":["T"],"data":{"itemContainer":{}}},"T",{"id":"T",)"
    R"("childrenIds":[],"data":{"tab":{"savedURL":"https://solo.example.test/",)"
    R"("savedTitle":"Solo"}}}]}]}})";

// kTinyArcSidebar with a one-colour Arc background, so the import writes a
// theme too.
constexpr char kThemedArcSidebar[] =
    R"({"sidebar":{"containers":[{"global":{}},{"topAppsContainerIDs":[],)"
    R"("spaces":["S",{"id":"S","title":"Solo","profile":{"default":true},)"
    R"("customInfo":{"windowTheme":{"background":{"single":{"_0":{"style":)"
    R"({"color":{"_0":{"blendedSingleColor":{"_0":{"color":{"red":0.2,)"
    R"("green":0.4,"blue":0.6}}}}}}}}}}},)"
    R"("containerIDs":["unpinned","U"]}],"items":["U",{"id":"U",)"
    R"("childrenIds":["T"],"data":{"itemContainer":{}}},"T",{"id":"T",)"
    R"("childrenIds":[],"data":{"tab":{"savedURL":"https://solo.example.test/",)"
    R"("savedTitle":"Solo"}}}]}]}})";

// One rule that opens its host in the most recent space.
constexpr char kTinyArcRouting[] =
    R"({"version":1,"rules":[{"id":"R","sourceComponents":[{"componentType":)"
    R"({"urlMatch":{"_0":{"contains":{}},"_1":"solo.example.test"}}}],)"
    R"("destination":{"space":{"_0":{"mostRecent":{}}}}}]})";

}  // namespace

TEST(AtriumSettingsSourceTest, PreviewWithNoArcFolderIsNotDetected) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_NE(std::string::npos,
            PreviewImport("arc", dir.GetPath()).find(R"("detected":false)"));
}

TEST(AtriumSettingsSourceTest, RunArcImportOnAFreshProfileSavesTheSpace) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir arc;
  base::ScopedTempDir profile;
  ASSERT_TRUE(arc.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableSidebar.json"),
                              kTinyArcSidebar));
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableLinkRouting.json"),
                              kTinyArcRouting));
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const base::FilePath routing = profile.GetPath().Append("routing.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("arc", arc.GetPath(), spaces, routing, base::FilePath());

  std::optional<base::Value> answer =
      base::JSONReader::Read(outcome.answer, base::JSON_PARSE_RFC);
  ASSERT_TRUE(answer && answer->is_dict()) << outcome.answer;
  EXPECT_EQ(1, answer->GetDict().FindIntByDottedPath("report.spaces.added"))
      << outcome.answer;
  EXPECT_TRUE(base::PathExists(spaces)) << "the run saves, not its caller";
  EXPECT_EQ(0, atrium_core_save()) << "nothing is left unsaved";
  EXPECT_TRUE(base::PathExists(routing));
  EXPECT_NE(std::string::npos, outcome.saved_routing.find("solo.example.test"))
      << outcome.saved_routing;
  EXPECT_EQ(std::string::npos, outcome.answer.find("routingError"))
      << outcome.answer;
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, RunArcImportWithoutArcFilesChangesNothing) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  atrium_core_close();
  const ImportOutcome outcome =
      RunImport("arc", dir.GetPath(), dir.GetPath().Append("spaces.json"),
                dir.GetPath().Append("routing.json"), base::FilePath());
  EXPECT_NE(std::string::npos, outcome.answer.find(R"("error")"))
      << outcome.answer;
  EXPECT_FALSE(atrium_core_is_open()) << "no files, so no fresh document";
}

TEST(AtriumSettingsSourceTest, RunArcImportRefusesANewerRoutingFile) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir arc;
  base::ScopedTempDir profile;
  ASSERT_TRUE(arc.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableSidebar.json"),
                              kTinyArcSidebar));
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const base::FilePath routing = profile.GetPath().Append("routing.json");
  constexpr char kNewer[] = R"({"schemaVersion":999,"rules":[]})";
  ASSERT_TRUE(base::WriteFile(routing, kNewer));
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("arc", arc.GetPath(), spaces, routing, base::FilePath());

  EXPECT_NE(std::string::npos, outcome.answer.find(R"("error")"))
      << outcome.answer;
  EXPECT_FALSE(atrium_core_is_open());
  EXPECT_FALSE(base::PathExists(spaces));
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(routing, &after));
  EXPECT_EQ(kNewer, after);
}

TEST(AtriumSettingsSourceTest, RunArcImportWritesNoRoutesWhenSpacesFail) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir arc;
  base::ScopedTempDir profile;
  ASSERT_TRUE(arc.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableSidebar.json"),
                              kTinyArcSidebar));
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableLinkRouting.json"),
                              kTinyArcRouting));
  // A file where the folder should be, so the spaces write cannot land.
  const base::FilePath blocker = profile.GetPath().Append("blocker");
  ASSERT_TRUE(base::WriteFile(blocker, ""));
  const base::FilePath routing = profile.GetPath().Append("routing.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("arc", arc.GetPath(), blocker.Append("spaces.json"), routing,
                base::FilePath());

  EXPECT_NE(std::string::npos, outcome.answer.find(R"("error")"))
      << outcome.answer;
  EXPECT_FALSE(base::PathExists(routing))
      << "routes that name unsaved spaces are not written";
  EXPECT_TRUE(outcome.saved_routing.empty());
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, RunArcImportSavesRoutesWhenOnlyThemesFail) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir arc;
  base::ScopedTempDir profile;
  ASSERT_TRUE(arc.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableSidebar.json"),
                              kThemedArcSidebar));
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableLinkRouting.json"),
                              kTinyArcRouting));
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const base::FilePath routing = profile.GetPath().Append("routing.json");
  // A folder where themes.json goes, so only the themes write fails.
  ASSERT_TRUE(base::CreateDirectory(profile.GetPath().Append("themes.json")));
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("arc", arc.GetPath(), spaces, routing, base::FilePath());

  EXPECT_EQ(std::string::npos, outcome.answer.find(R"("error")"))
      << outcome.answer;
  EXPECT_NE(std::string::npos, outcome.answer.find(R"("themesError")"))
      << outcome.answer;
  EXPECT_TRUE(base::PathExists(spaces));
  EXPECT_TRUE(base::PathExists(routing));
  EXPECT_NE(std::string::npos, outcome.saved_routing.find("solo.example.test"))
      << outcome.saved_routing;
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, RunArcImportSaysWhenTheRoutesDidNotLand) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir arc;
  base::ScopedTempDir profile;
  ASSERT_TRUE(arc.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableSidebar.json"),
                              kTinyArcSidebar));
  ASSERT_TRUE(base::WriteFile(arc.GetPath().Append("StorableLinkRouting.json"),
                              kTinyArcRouting));
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  // A folder nobody may write in, so the routing write fails.
  const base::FilePath locked = profile.GetPath().Append("locked");
  ASSERT_TRUE(base::CreateDirectory(locked));
  ASSERT_TRUE(base::SetPosixFilePermissions(locked, 0500));
  const base::FilePath routing = locked.Append("routing.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("arc", arc.GetPath(), spaces, routing, base::FilePath());
  ASSERT_TRUE(base::SetPosixFilePermissions(locked, 0700));

  EXPECT_TRUE(base::PathExists(spaces));
  EXPECT_NE(std::string::npos, outcome.answer.find(R"("report")"))
      << outcome.answer;
  EXPECT_NE(std::string::npos, outcome.answer.find(R"("routingError")"))
      << outcome.answer;
  EXPECT_TRUE(outcome.saved_routing.empty());
  atrium_core_close();
}

namespace {

// One literal-only LZ4 block, the shape build/scripts/mozlz4-pack.py writes.
std::string PackMozLz4(std::string_view text) {
  std::string out("mozLz40\0", 8);
  const uint32_t size = static_cast<uint32_t>(text.size());
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((size >> shift) & 0xff));
  }
  size_t rest = text.size();
  out.push_back(static_cast<char>(std::min<size_t>(rest, 15) << 4));
  if (rest >= 15) {
    for (rest -= 15; rest >= 255; rest -= 255) {
      out.push_back('\xff');
    }
    out.push_back(static_cast<char>(rest));
  }
  out.append(text);
  return out;
}

base::FilePath WriteZenProfile(const base::FilePath& zen_dir,
                               std::string_view session) {
  const base::FilePath profile =
      zen_dir.Append("Profiles").Append("p1.default");
  CHECK(base::CreateDirectory(profile));
  CHECK(base::WriteFile(zen_dir.Append("profiles.ini"),
                        "[Profile0]\nName=Main\nIsRelative=1\n"
                        "Path=Profiles/p1.default\n"));
  CHECK(base::WriteFile(profile.Append("zen-sessions.jsonlz4"),
                        PackMozLz4(session)));
  CHECK(base::WriteFile(profile.Append("containers.json"),
                        R"({"identities":[]})"));
  return profile;
}

// Default is named, "../escape" tries to leave the Chrome folder, and
// "Profile 9" is named but has no folder.
constexpr char kChromeLocalState[] =
    R"({"profile":{"info_cache":{"Default":{"name":"Personal"},)"
    R"("../escape":{"name":"Escape"},"Profile 9":{"name":"Empty"}}}})";
constexpr char kChromeBookmarks[] =
    R"({"roots":{"bookmark_bar":{"type":"folder","children":[)"
    R"({"type":"url","name":"Wiki","url":"https://wiki.example.test/"}]}}})";
constexpr char kKeywordsTable[] =
    "CREATE TABLE keywords (id INTEGER PRIMARY KEY, short_name TEXT, "
    "keyword TEXT, url TEXT, safe_for_autoreplace INTEGER, prepopulate_id "
    "INTEGER, created_by_policy INTEGER, is_active INTEGER, starter_pack_id "
    "INTEGER, sync_guid TEXT)";
constexpr char kWikiKeyword[] =
    "INSERT INTO keywords (short_name, keyword, url, safe_for_autoreplace, "
    "prepopulate_id, created_by_policy, is_active, starter_pack_id, "
    "sync_guid) VALUES ('Wiki', 'wiki', "
    "'https://wiki.example.test/w?search={searchTerms}', 0, 0, 0, 1, 0, "
    "'G-WIKI')";

void WriteWebData(const base::FilePath& path,
                  base::cstring_view create,
                  base::cstring_view insert) {
  sql::Database web_data(sql::DatabaseOptions(), "Test");
  CHECK(web_data.Open(path));
  CHECK(web_data.Execute(create));
  CHECK(web_data.Execute(insert));
}

// A Chrome folder with one importable profile, "Default". Returns it.
base::FilePath WriteChromeFolder(const base::FilePath& chrome_dir) {
  CHECK(base::WriteFile(chrome_dir.Append("Local State"), kChromeLocalState));
  const base::FilePath profile = chrome_dir.Append("Default");
  CHECK(base::CreateDirectory(profile));
  CHECK(base::WriteFile(profile.Append("Bookmarks"), kChromeBookmarks));
  CHECK(base::WriteFile(
      profile.Append("Preferences"),
      R"({"default_search_provider":{"guid":"G-WIKI"},"extensions":{}})"));
  WriteWebData(profile.Append("Web Data"), kKeywordsTable, kWikiKeyword);
  const base::FilePath unlisted = chrome_dir.Append("Unlisted");
  CHECK(base::CreateDirectory(unlisted));
  CHECK(base::WriteFile(unlisted.Append("Bookmarks"), kChromeBookmarks));
  return profile;
}

constexpr char kChromiumUrlsTable[] =
    "CREATE TABLE urls(id INTEGER PRIMARY KEY, url TEXT, title TEXT, "
    "visit_count INTEGER, typed_count INTEGER, hidden INTEGER)";
constexpr char kChromiumVisitsTable[] =
    "CREATE TABLE visits(id INTEGER PRIMARY KEY, url INTEGER, "
    "visit_time INTEGER, transition INTEGER)";
constexpr char kQuokkaUrl[] =
    "INSERT INTO urls VALUES (1, 'https://quokka.example.test/field-notes', "
    "'Quokka field notes', 4, 4, 0)";
// Typed visits a second apart, newest yesterday: 4 of them, or one past the
// per-profile cap.
constexpr char kFourVisits[] =
    "WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n "
    "WHERE i < 3) INSERT INTO visits(url, visit_time, transition) "
    "SELECT 1, (strftime('%s','now') + 11644473600 - 86400 - i) * 1000000, "
    "536870913 FROM n";
constexpr char kVisitsPastTheCap[] =
    "WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n "
    "WHERE i < 20000) INSERT INTO visits(url, visit_time, transition) "
    "SELECT 1, (strftime('%s','now') + 11644473600 - 86400 - i) * 1000000, "
    "536870913 FROM n";

void WriteChromiumHistory(const base::FilePath& path,
                          base::cstring_view visits) {
  sql::Database db(sql::DatabaseOptions(), "Test");
  CHECK(db.Open(path));
  CHECK(db.Execute(kChromiumUrlsTable));
  CHECK(db.Execute(kChromiumVisitsTable));
  CHECK(db.Execute(kQuokkaUrl));
  CHECK(db.Execute(visits));
}

ImportOutcome RunChromeImportWithHistory(base::cstring_view visits,
                                         const base::FilePath& profile_dir) {
  base::ScopedTempDir chrome;
  CHECK(chrome.CreateUniqueTempDir());
  const base::FilePath profile = WriteChromeFolder(chrome.GetPath());
  if (visits.empty()) {
    CHECK(base::WriteFile(profile.Append("History"), "not a database"));
  } else {
    WriteChromiumHistory(profile.Append("History"), visits);
  }
  atrium_core_close();
  ImportOutcome outcome = RunImport(
      "chrome", chrome.GetPath(), profile_dir.Append("spaces.json"),
      profile_dir.Append("routing.json"), base::FilePath());
  atrium_core_close();
  return ReadOutcomeHistory(std::move(outcome));
}

std::optional<base::Value> ParsedFiles(
    const std::optional<std::string>& files) {
  return files ? base::JSONReader::Read(*files, base::JSON_PARSE_RFC)
               : std::nullopt;
}

base::FilePath WriteZenSessionFolder(const base::FilePath& zen_dir,
                                     const std::string& folder_name,
                                     std::string_view session) {
  const base::FilePath profile = zen_dir.Append("Profiles").Append(folder_name);
  CHECK(base::CreateDirectory(profile));
  CHECK(base::WriteFile(profile.Append("zen-sessions.jsonlz4"),
                        PackMozLz4(session)));
  return profile;
}

std::string PreviewFromFiles(const std::string& files_json) {
  char* raw = atrium_core_import_preview("zen", files_json.c_str());
  if (!raw) {
    return std::string();
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

}  // namespace

TEST(AtriumSettingsSourceTest, ImportDataDirKnowsEachSourceFolder) {
  const base::FilePath app_support("/Users/u/Library/Application Support");
  EXPECT_EQ(base::FilePath("/tmp/zen"),
            ImportDataDir("zen", "/tmp/zen", app_support));
  EXPECT_EQ(app_support.Append("zen"), ImportDataDir("zen", "", app_support));
  EXPECT_TRUE(ImportDataDir("zen", "", base::FilePath()).empty());
  EXPECT_TRUE(ImportDataDir("opera", "", app_support).empty());
  EXPECT_EQ("ATRIUM_IMPORT_ZEN_DIR", ImportDirOverrideName("zen"));
  EXPECT_EQ("Zen", ImportSourceLabel("zen"));
  EXPECT_TRUE(ImportSourceLabel("opera").empty());
}

TEST(AtriumSettingsSourceTest, ZenFilesArriveDecodedUnderTheirProfilePath) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_FALSE(ImportDetected("zen", dir.GetPath()));
  EXPECT_FALSE(ReadImportFiles("zen", dir.GetPath()).has_value());

  WriteZenProfile(dir.GetPath(), R"({"spaces":[]})");
  EXPECT_TRUE(ImportDetected("zen", dir.GetPath()));

  std::optional<std::string> files = ReadImportFiles("zen", dir.GetPath());
  ASSERT_TRUE(files.has_value());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*files, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(R"({"spaces":[]})",
            *dict.FindString("Profiles/p1.default/zen-sessions.jsonlz4"));
  EXPECT_EQ(R"({"identities":[]})",
            *dict.FindString("Profiles/p1.default/containers.json"));
  EXPECT_TRUE(dict.FindString("profiles.ini"));
}

TEST(AtriumSettingsSourceTest, ADamagedZenSessionFailsThePreview) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile = WriteZenProfile(dir.GetPath(), "{}");
  ASSERT_TRUE(base::WriteFile(profile.Append("zen-sessions.jsonlz4"),
                              "mozLz40 not a block"));
  const std::string answer = PreviewImport("zen", dir.GetPath());
  EXPECT_NE(std::string::npos, answer.find(R"("error")")) << answer;
}

TEST(AtriumSettingsSourceTest,
     TwoZenProfilesOverTheSharedBudgetFailThePreview) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  constexpr std::string_view kSession = R"({"spaces":[]})";
  WriteZenSessionFolder(dir.GetPath(), "p1.default", kSession);
  WriteZenSessionFolder(dir.GetPath(), "p2.default", kSession);

  std::optional<std::string> files = ReadZenImportFilesForTesting(
      dir.GetPath(), /*total_budget=*/kSession.size());
  ASSERT_TRUE(files.has_value());
  EXPECT_NE(std::string::npos, PreviewFromFiles(*files).find(R"("error")"))
      << *files;
}

TEST(AtriumSettingsSourceTest, AZenProfileWithNoSessionFileIsSkipped) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteZenProfile(dir.GetPath(), R"({"spaces":[]})");
  ASSERT_TRUE(base::CreateDirectory(
      dir.GetPath().Append("Profiles").Append("p2.unused")));

  std::optional<std::string> files = ReadImportFiles("zen", dir.GetPath());
  ASSERT_TRUE(files.has_value());
  EXPECT_EQ(std::string::npos, files->find("p2.unused")) << *files;
  EXPECT_EQ(std::string::npos,
            PreviewImport("zen", dir.GetPath()).find(R"("error")"));
}

TEST(AtriumSettingsSourceTest, AnUnreadableZenSessionStillCountsAsAProfile) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile =
      WriteZenProfile(dir.GetPath(), R"({"spaces":[]})");
  const base::FilePath session = profile.Append("zen-sessions.jsonlz4");
  ASSERT_EQ(0, chmod(session.value().c_str(), 0));

  EXPECT_TRUE(ImportDetected("zen", dir.GetPath()))
      << "the folder is still there, so the first-run offer must still fire";
  std::optional<std::string> files = ReadImportFiles("zen", dir.GetPath());
  ASSERT_TRUE(files.has_value());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*files, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ("", *parsed->GetDict().FindString(
                    "Profiles/p1.default/zen-sessions.jsonlz4"));

  chmod(session.value().c_str(), 0644);
}

TEST(AtriumSettingsSourceTest, AZenFifoIsSkippedInsteadOfOpened) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile =
      dir.GetPath().Append("Profiles").Append("p1.default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_EQ(
      0, mkfifo(profile.Append("zen-sessions.jsonlz4").value().c_str(), 0644));

  // A read that did not guard the file type would block here forever, since
  // nothing ever connects as a writer.
  std::optional<std::string> files = ReadImportFiles("zen", dir.GetPath());
  ASSERT_TRUE(files.has_value());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(*files, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ("", *parsed->GetDict().FindString(
                    "Profiles/p1.default/zen-sessions.jsonlz4"));
}

TEST(AtriumSettingsSourceTest, RunZenImportOnAFreshProfileSavesTheSpace) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir zen, profile;
  ASSERT_TRUE(zen.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteZenProfile(zen.GetPath(),
                  R"({"spaces":[{"uuid":"W1","name":"Zen Space"}],"tabs":[]})");
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const ImportOutcome outcome =
      RunImport("zen", zen.GetPath(), spaces,
                profile.GetPath().Append("routing.json"), base::FilePath());
  EXPECT_EQ("zen", outcome.source);
  EXPECT_EQ(std::string::npos, outcome.answer.find(R"("error")"))
      << outcome.answer;
  std::string saved;
  ASSERT_TRUE(base::ReadFileToString(spaces, &saved));
  EXPECT_NE(std::string::npos, saved.find("Zen Space"));
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, EveryImportSourceHasALabelAndAFolder) {
  const base::FilePath app_support("/Users/u/Library/Application Support");
  for (std::string_view source : kImportSources) {
    EXPECT_FALSE(ImportSourceLabel(source).empty()) << source;
    EXPECT_FALSE(ImportDataDir(source, "", app_support).empty()) << source;
  }
  EXPECT_EQ(app_support.Append("Google").Append("Chrome"),
            ImportDataDir("chrome", "", app_support));
  EXPECT_EQ("ATRIUM_IMPORT_CHROME_DIR", ImportDirOverrideName("chrome"));
  EXPECT_EQ("Chrome", ImportSourceLabel("chrome"));
  EXPECT_EQ(app_support.Append("Firefox"),
            ImportDataDir("firefox", "", app_support));
  EXPECT_EQ("ATRIUM_IMPORT_FIREFOX_DIR", ImportDirOverrideName("firefox"));
  EXPECT_EQ("Firefox", ImportSourceLabel("firefox"));
}

TEST(AtriumSettingsSourceTest, ChromeNeedsLocalStateAndABookmarksFile) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_FALSE(ImportDetected("chrome", dir.GetPath()));
  ASSERT_TRUE(base::WriteFile(dir.GetPath().Append("Local State"), "{}"));
  EXPECT_FALSE(ImportDetected("chrome", dir.GetPath()));
  WriteChromeFolder(dir.GetPath());
  EXPECT_TRUE(ImportDetected("chrome", dir.GetPath()));
}

TEST(AtriumSettingsSourceTest, ChromeFilesArriveOnlyFromNamedFolders) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteChromeFolder(dir.GetPath());

  std::optional<std::string> files = ReadImportFiles("chrome", dir.GetPath());
  std::optional<base::Value> parsed = ParsedFiles(files);
  ASSERT_TRUE(parsed && parsed->is_dict()) << files.value_or("");
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(kChromeLocalState, *dict.FindString("Local State"));
  EXPECT_EQ(kChromeBookmarks, *dict.FindString("Default/Bookmarks"));
  EXPECT_EQ(R"({"default_search_provider":{"guid":"G-WIKI"}})",
            *dict.FindString("Default/Preferences"))
      << "only the one key the core needs travels on";
  EXPECT_EQ(
      R"([{"created_by_policy":0,"is_active":1,"keyword":"wiki",)"
      R"("prepopulate_id":0,"safe_for_autoreplace":0,"short_name":"Wiki",)"
      R"("starter_pack_id":0,"sync_guid":"G-WIKI",)"
      R"("url":"https://wiki.example.test/w?search={searchTerms}"}])",
      *dict.FindString("Default/Web Data#keywords"));
  EXPECT_EQ(4u, dict.size())
      << *files << "\nno escape, Unlisted or Profile 9 key";
  EXPECT_NE(std::string::npos,
            PreviewImport("chrome", dir.GetPath()).find(R"("engines":1)"));
}

TEST(AtriumSettingsSourceTest, AnUnreadableChromeBookmarksFileFailsThePreview) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath bookmarks =
      WriteChromeFolder(dir.GetPath()).Append("Bookmarks");
  ASSERT_EQ(0, chmod(bookmarks.value().c_str(), 0));
  EXPECT_NE(std::string::npos,
            PreviewImport("chrome", dir.GetPath()).find(R"("error")"));
  chmod(bookmarks.value().c_str(), 0644);
}

TEST(AtriumSettingsSourceTest, AChromeBookmarksFifoIsNotOpened) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath bookmarks =
      WriteChromeFolder(dir.GetPath()).Append("Bookmarks");
  ASSERT_TRUE(base::DeleteFile(bookmarks));
  ASSERT_EQ(0, mkfifo(bookmarks.value().c_str(), 0644));
  // A read that did not guard the file type would block here forever.
  std::optional<base::Value> parsed =
      ParsedFiles(ReadImportFiles("chrome", dir.GetPath()));
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ("", *parsed->GetDict().FindString("Default/Bookmarks"));
}

TEST(AtriumSettingsSourceTest, ChromeFilesOverTheSharedBudgetArriveEmpty) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteChromeFolder(dir.GetPath());
  std::optional<base::Value> parsed =
      ParsedFiles(ReadChromeImportFilesForTesting(
          dir.GetPath(), /*total_budget=*/std::size(kChromeLocalState)));
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ("", *parsed->GetDict().FindString("Default/Bookmarks"));
}

TEST(AtriumSettingsSourceTest, APreferencesFileWithNoEngineIsNotDamaged) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath preferences =
      WriteChromeFolder(dir.GetPath()).Append("Preferences");
  auto read_preferences = [&dir] {
    std::optional<base::Value> parsed =
        ParsedFiles(ReadChromeImportFilesForTesting(dir.GetPath(),
                                                    /*total_budget=*/1 << 20));
    CHECK(parsed && parsed->is_dict());
    return *parsed->GetDict().FindString("Default/Preferences");
  };

  // Chrome leaves the guid out while the user keeps its default engine.
  ASSERT_TRUE(base::WriteFile(preferences, R"({"extensions":{}})"));
  EXPECT_EQ("{}", read_preferences());
  const std::string preview = PreviewImport("chrome", dir.GetPath());
  EXPECT_EQ(std::string::npos, preview.find("could not be read")) << preview;

  ASSERT_TRUE(base::WriteFile(preferences, "not json"));
  EXPECT_EQ("", read_preferences());
}

TEST(AtriumSettingsSourceTest, AnOldChromeWebDataOnlyLosesItsEngines) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile = WriteChromeFolder(dir.GetPath());
  ASSERT_TRUE(base::DeleteFile(profile.Append("Web Data")));
  WriteWebData(profile.Append("Web Data"),
               "CREATE TABLE keywords (short_name TEXT, keyword TEXT, "
               "url TEXT)",
               "INSERT INTO keywords VALUES ('Old', 'old', "
               "'https://old.example.test/?q={searchTerms}')");

  std::optional<base::Value> parsed =
      ParsedFiles(ReadImportFiles("chrome", dir.GetPath()));
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ("", *parsed->GetDict().FindString("Default/Web Data#keywords"));
  const std::string preview = PreviewImport("chrome", dir.GetPath());
  EXPECT_EQ(std::string::npos, preview.find(R"("error")")) << preview;
  EXPECT_NE(std::string::npos, preview.find(R"("spaces":1)")) << preview;
}

TEST(AtriumSettingsSourceTest, RunChromeImportSavesItsEnginesIntoSettings) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir chrome, profile;
  ASSERT_TRUE(chrome.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteChromeFolder(chrome.GetPath());
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const base::FilePath settings = profile.GetPath().Append("settings.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("chrome", chrome.GetPath(), spaces,
                profile.GetPath().Append("routing.json"), settings);

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_NE(std::string::npos, outcome.saved_settings.find("import-chrome-1"));
  std::string written;
  ASSERT_TRUE(base::ReadFileToString(settings, &written));
  // The core's own writer always escapes a slash (docs/model-rules.md, to
  // match the legacy Swift byte layout), so the saved file carries \/ rather
  // than the plain URL.
  EXPECT_NE(std::string::npos,
            written.find(R"(https:\/\/wiki.example.test\/w?search=%s)"))
      << written;
  std::string saved_spaces;
  ASSERT_TRUE(base::ReadFileToString(spaces, &saved_spaces));
  EXPECT_NE(std::string::npos, saved_spaces.find("import-chrome-1"))
      << saved_spaces;
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, RunChromeImportLeavesANewerSettingsFileAlone) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir chrome, profile;
  ASSERT_TRUE(chrome.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteChromeFolder(chrome.GetPath());
  const base::FilePath settings = profile.GetPath().Append("settings.json");
  constexpr char kNewer[] =
      R"({"schemaVersion":999,"appIconVariant":"crimson-ink"})";
  ASSERT_TRUE(base::WriteFile(settings, kNewer));
  atrium_core_close();

  const ImportOutcome outcome = RunImport(
      "chrome", chrome.GetPath(), profile.GetPath().Append("spaces.json"),
      profile.GetPath().Append("routing.json"), settings);

  EXPECT_NE(std::string::npos,
            outcome.answer.find(R"("enginesError":"settings.json is in a )"
                                R"(form this build cannot read")"))
      << outcome.answer;
  EXPECT_TRUE(outcome.saved_settings.empty());
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(settings, &after));
  EXPECT_EQ(kNewer, after);
  std::string saved_spaces;
  ASSERT_TRUE(base::ReadFileToString(profile.GetPath().Append("spaces.json"),
                                     &saved_spaces))
      << "the spaces still land";
  EXPECT_EQ(std::string::npos, saved_spaces.find("import-chrome"))
      << "no profile names an engine settings.json never got\n"
      << saved_spaces;
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, AnImportWithNoEnginesLeavesSettingsUnwritten) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir zen, profile;
  ASSERT_TRUE(zen.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteZenProfile(zen.GetPath(),
                  R"({"spaces":[{"uuid":"W1","name":"Zen Space"}],"tabs":[]})");
  const base::FilePath settings = profile.GetPath().Append("settings.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("zen", zen.GetPath(), profile.GetPath().Append("spaces.json"),
                profile.GetPath().Append("routing.json"), settings);

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_FALSE(base::PathExists(settings));
  EXPECT_TRUE(outcome.saved_settings.empty());
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, ANewerSettingsFileCostsAZenImportNothing) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir zen, profile;
  ASSERT_TRUE(zen.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteZenProfile(zen.GetPath(),
                  R"({"spaces":[{"uuid":"W1","name":"Zen Space"}],"tabs":[]})");
  const base::FilePath settings = profile.GetPath().Append("settings.json");
  ASSERT_TRUE(base::WriteFile(settings, R"({"schemaVersion":999})"));
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("zen", zen.GetPath(), profile.GetPath().Append("spaces.json"),
                profile.GetPath().Append("routing.json"), settings);

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  atrium_core_close();
}

namespace {

// The smallest places.sqlite the bookmarks query reads: the root, the
// toolbar, and one link on it.
constexpr base::cstring_view kPlacesStatements[] = {
    "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT, "
    "last_visit_date INTEGER, title TEXT, visit_count INTEGER DEFAULT 0, "
    "hidden INTEGER DEFAULT 0)",
    "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, "
    "fk INTEGER, parent INTEGER, position INTEGER, title TEXT, guid TEXT)",
    "CREATE TABLE moz_historyvisits (id INTEGER PRIMARY KEY, "
    "place_id INTEGER, visit_date INTEGER, visit_type INTEGER)",
    "INSERT INTO moz_places (id, url, last_visit_date) "
    "VALUES (1, 'https://news.example.test/', NULL)",
    "INSERT INTO moz_bookmarks VALUES (1, 2, NULL, 0, 0, '', 'root________')",
    "INSERT INTO moz_bookmarks VALUES (3, 2, NULL, 1, 0, 'toolbar', "
    "'toolbar_____')",
    "INSERT INTO moz_bookmarks VALUES (10, 1, 1, 3, 0, 'News', "
    "'fixtureL0001')",
};
constexpr char kPlacesRowsJson[] =
    R"([{"guid":"root________","id":1,"last_visit_date":null,"parent":0,)"
    R"("position":0,"title":"","type":2,"url":null},)"
    R"({"guid":"toolbar_____","id":3,"last_visit_date":null,"parent":1,)"
    R"("position":0,"title":"toolbar","type":2,"url":null},)"
    R"({"guid":"fixtureL0001","id":10,"last_visit_date":null,"parent":3,)"
    R"("position":0,"title":"News","type":1,)"
    R"("url":"https://news.example.test/"}])";
constexpr char kFirefoxSession[] =
    R"({"windows":[{"tabs":[{"entries":[{"url":"https://mail.example.test/",)"
    R"("title":"Mail"}],"index":1,"pinned":true}]}]})";
constexpr char kFirefoxSearch[] =
    R"({"engines":[{"id":"e1","_name":"Docs","_metaData":{"alias":"docs"},)"
    R"("_urls":[{"template":"https://docs.example.test/search?q={searchTerms}"}]}],)"
    R"("metaData":{"defaultEngineId":"e1"}})";
constexpr char kFirefoxPrefix[] = "Profiles/p1.default-release/";

void WritePlaces(const base::FilePath& path) {
  sql::Database places(sql::DatabaseOptions(), "Test");
  CHECK(places.Open(path));
  for (base::cstring_view statement : kPlacesStatements) {
    CHECK(places.Execute(statement));
  }
}

// A Firefox folder with one profile whose only session file is the recovery
// file, plus a folder with no places.sqlite. Returns the profile.
base::FilePath WriteFirefoxFolder(const base::FilePath& firefox_dir) {
  CHECK(base::WriteFile(firefox_dir.Append("profiles.ini"),
                        "[Profile0]\nName=Personal\nIsRelative=1\n"
                        "Path=Profiles/p1.default-release\n"));
  const base::FilePath profile =
      firefox_dir.Append("Profiles").Append("p1.default-release");
  CHECK(base::CreateDirectory(profile.Append("sessionstore-backups")));
  WritePlaces(profile.Append("places.sqlite"));
  CHECK(base::WriteFile(
      profile.Append("sessionstore-backups").Append("recovery.jsonlz4"),
      PackMozLz4(kFirefoxSession)));
  CHECK(base::WriteFile(profile.Append("search.json.mozlz4"),
                        PackMozLz4(kFirefoxSearch)));
  const base::FilePath stray = firefox_dir.Append("Profiles").Append("stray");
  CHECK(base::CreateDirectory(stray));
  CHECK(base::WriteFile(stray.Append("sessionstore.jsonlz4"),
                        PackMozLz4(kFirefoxSession)));
  return profile;
}

std::string FirefoxFile(const base::FilePath& dir, std::string_view name) {
  std::optional<base::Value> parsed =
      ParsedFiles(ReadImportFiles("firefox", dir));
  CHECK(parsed && parsed->is_dict());
  const std::string* text =
      parsed->GetDict().FindString(base::StrCat({kFirefoxPrefix, name}));
  return text ? *text : "<missing>";
}

}  // namespace

TEST(AtriumSettingsSourceTest, FirefoxNeedsAProfileWithPlaces) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_FALSE(ImportDetected("firefox", dir.GetPath()));
  EXPECT_FALSE(ReadImportFiles("firefox", dir.GetPath()).has_value());
  const base::FilePath only_session =
      dir.GetPath().Append("Profiles").Append("stray");
  ASSERT_TRUE(base::CreateDirectory(only_session));
  ASSERT_TRUE(base::WriteFile(only_session.Append("sessionstore.jsonlz4"),
                              PackMozLz4(kFirefoxSession)));
  EXPECT_FALSE(ImportDetected("firefox", dir.GetPath()));
  WriteFirefoxFolder(dir.GetPath());
  EXPECT_TRUE(ImportDetected("firefox", dir.GetPath()));
}

TEST(AtriumSettingsSourceTest, FirefoxFilesArriveUnderTheirProfilePath) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteFirefoxFolder(dir.GetPath());

  std::optional<std::string> files = ReadImportFiles("firefox", dir.GetPath());
  std::optional<base::Value> parsed = ParsedFiles(files);
  ASSERT_TRUE(parsed && parsed->is_dict()) << files.value_or("");
  const base::DictValue& dict = parsed->GetDict();
  const std::string prefix = kFirefoxPrefix;
  EXPECT_EQ(kPlacesRowsJson,
            *dict.FindString(prefix + "places.sqlite#bookmarks"));
  EXPECT_EQ(kFirefoxSession, *dict.FindString(prefix + "sessionstore.jsonlz4"))
      << "the recovery file arrives under the one name the core reads";
  EXPECT_EQ(kFirefoxSearch, *dict.FindString(prefix + "search.json.mozlz4"));
  EXPECT_TRUE(dict.FindString("profiles.ini"));
  EXPECT_EQ(4u, dict.size()) << *files << "\nno key for the stray folder";
  EXPECT_NE(std::string::npos,
            PreviewImport("firefox", dir.GetPath()).find(R"("engines":1)"));
}

TEST(AtriumSettingsSourceTest, TheNewerFirefoxSessionFileWins) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile = WriteFirefoxFolder(dir.GetPath());
  const base::FilePath quit = profile.Append("sessionstore.jsonlz4");
  const base::FilePath recovery =
      profile.Append("sessionstore-backups").Append("recovery.jsonlz4");
  ASSERT_TRUE(base::WriteFile(quit, PackMozLz4(R"({"windows":[]})")));
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(base::TouchFile(quit, now, now - base::Hours(1)));
  ASSERT_TRUE(base::TouchFile(recovery, now, now));
  EXPECT_EQ(kFirefoxSession,
            FirefoxFile(dir.GetPath(), "sessionstore.jsonlz4"))
      << "a running Firefox writes only the recovery file";
  ASSERT_TRUE(base::TouchFile(quit, now, now + base::Hours(1)));
  EXPECT_EQ(R"({"windows":[]})",
            FirefoxFile(dir.GetPath(), "sessionstore.jsonlz4"));
}

TEST(AtriumSettingsSourceTest, FirefoxFilesOverTheSharedBudgetArriveEmpty) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  WriteFirefoxFolder(dir.GetPath());
  std::optional<base::Value> parsed =
      ParsedFiles(ReadFirefoxImportFilesForTesting(
          dir.GetPath(), /*total_budget=*/std::size(kPlacesRowsJson)));
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& dict = parsed->GetDict();
  const std::string prefix = kFirefoxPrefix;
  EXPECT_EQ(kPlacesRowsJson,
            *dict.FindString(prefix + "places.sqlite#bookmarks"));
  EXPECT_EQ("", *dict.FindString(prefix + "sessionstore.jsonlz4"));
  EXPECT_EQ("", *dict.FindString(prefix + "search.json.mozlz4"));
}

TEST(AtriumSettingsSourceTest, ADamagedPlacesFileFailsTheFirefoxPreview) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile = WriteFirefoxFolder(dir.GetPath());
  ASSERT_TRUE(
      base::WriteFile(profile.Append("places.sqlite"), "not a database"));
  const std::string answer = PreviewImport("firefox", dir.GetPath());
  EXPECT_NE(std::string::npos,
            answer.find("Firefox's bookmarks could not be read"))
      << answer;
}

TEST(AtriumSettingsSourceTest, RunFirefoxImportSavesItsSpaceAndEngines) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir firefox, profile;
  ASSERT_TRUE(firefox.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  WriteFirefoxFolder(firefox.GetPath());
  const base::FilePath spaces = profile.GetPath().Append("spaces.json");
  const base::FilePath settings = profile.GetPath().Append("settings.json");
  atrium_core_close();

  const ImportOutcome outcome =
      RunImport("firefox", firefox.GetPath(), spaces,
                profile.GetPath().Append("routing.json"), settings);

  EXPECT_EQ("firefox", outcome.source);
  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_NE(std::string::npos, outcome.saved_settings.find("import-firefox-1"));
  std::string saved_spaces;
  ASSERT_TRUE(base::ReadFileToString(spaces, &saved_spaces));
  EXPECT_NE(std::string::npos, saved_spaces.find("news.example.test"))
      << saved_spaces;
  EXPECT_NE(std::string::npos, saved_spaces.find("mail.example.test"))
      << saved_spaces;
  atrium_core_close();
}

TEST(AtriumSettingsSourceTest, RunFirefoxImportReadsTheMappedProfilesHistory) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir firefox, profile;
  ASSERT_TRUE(firefox.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  const base::FilePath places =
      WriteFirefoxFolder(firefox.GetPath()).Append("places.sqlite");
  sql::Database db(sql::DatabaseOptions(), "Test");
  ASSERT_TRUE(db.Open(places));
  ASSERT_TRUE(db.Execute(
      "INSERT INTO moz_historyvisits (place_id, visit_date, visit_type) "
      "VALUES (1, (strftime('%s','now') - 86400) * 1000000, 2)"));
  db.Close();
  atrium_core_close();

  const ImportOutcome outcome = ReadOutcomeHistory(RunImport(
      "firefox", firefox.GetPath(), profile.GetPath().Append("spaces.json"),
      profile.GetPath().Append("routing.json"), base::FilePath()));
  atrium_core_close();

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_EQ(std::string::npos,
            outcome.answer.find("history could not be read"))
      << outcome.answer;
  ASSERT_EQ(1u, outcome.history.size());
  EXPECT_EQ("Default", outcome.history[0].profile_id);
  EXPECT_EQ(history::SOURCE_FIREFOX_IMPORTED,
            outcome.history[0].visit_source);
  ASSERT_EQ(1u, outcome.history[0].rows.size());
  EXPECT_EQ(GURL("https://news.example.test/"),
            outcome.history[0].rows[0].url());
}

TEST(AtriumSettingsSourceTest, ZenPlacesRowsArriveNextToTheSession) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath profile =
      WriteZenProfile(dir.GetPath(), R"({"spaces":[]})");
  WritePlaces(profile.Append("places.sqlite"));
  std::optional<base::Value> parsed =
      ParsedFiles(ReadImportFiles("zen", dir.GetPath()));
  ASSERT_TRUE(parsed && parsed->is_dict());
  EXPECT_EQ(kPlacesRowsJson,
            *parsed->GetDict().FindString(
                "Profiles/p1.default/places.sqlite#bookmarks"));
  const std::string answer = PreviewImport("zen", dir.GetPath());
  EXPECT_NE(std::string::npos, answer.find(R"("spaces":1)")) << answer;
  EXPECT_NE(std::string::npos, answer.find(R"("tabs":1)")) << answer;
}

TEST(AtriumSettingsSourceTest,
     ZenBookmarkRowsNeverStarveALaterProfilesSession) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  constexpr std::string_view kSession = R"({"spaces":[]})";
  const base::FilePath first =
      WriteZenSessionFolder(dir.GetPath(), "p1.default", kSession);
  WritePlaces(first.Append("places.sqlite"));
  const base::FilePath second =
      WriteZenSessionFolder(dir.GetPath(), "p2.default", kSession);
  WritePlaces(second.Append("places.sqlite"));

  // Fits both sessions exactly, leaving no room for either profile's rows.
  std::optional<std::string> files = ReadZenImportFilesForTesting(
      dir.GetPath(), /*total_budget=*/kSession.size() * 2);

  ASSERT_TRUE(files.has_value());
  std::optional<base::Value> parsed = ParsedFiles(files);
  ASSERT_TRUE(parsed && parsed->is_dict()) << *files;
  const base::DictValue& dict = parsed->GetDict();
  EXPECT_EQ(kSession, *dict.FindString("Profiles/p1.default/"
                                       "zen-sessions.jsonlz4"))
      << "a profile's own bookmark rows must not starve any profile's "
         "session, read before rows are ever charged";
  EXPECT_EQ(kSession, *dict.FindString("Profiles/p2.default/"
                                       "zen-sessions.jsonlz4"))
      << "a profile's own bookmark rows must not starve any profile's "
         "session, read before rows are ever charged";
  EXPECT_EQ("", *dict.FindString(
                    "Profiles/p1.default/places.sqlite#bookmarks"));
  EXPECT_EQ("", *dict.FindString(
                    "Profiles/p2.default/places.sqlite#bookmarks"));
}

TEST(AtriumSettingsSourceTest, EachSourceKeyNamesOneHistoryFile) {
  const base::FilePath dir("/x");
  EXPECT_EQ(dir.Append("Default").Append("History"),
            ImportHistoryFile("chrome", dir, "Default"));
  EXPECT_EQ(dir.Append("Profile 1").Append("History"),
            ImportHistoryFile("chrome", dir, "Profile 1"));
  EXPECT_EQ(dir.Append("User Data").Append("Default").Append("History"),
            ImportHistoryFile("arc", dir, "default"));
  EXPECT_EQ(dir.Append("User Data").Append("Profile 2").Append("History"),
            ImportHistoryFile("arc", dir, "Profile 2"));
  EXPECT_EQ(dir.Append("Profiles").Append("a.default").Append("places.sqlite"),
            ImportHistoryFile("firefox", dir, "Profiles/a.default"));
  EXPECT_EQ(dir.Append("Profiles").Append("z.default").Append("places.sqlite"),
            ImportHistoryFile("zen", dir, "Profiles/z.default#0"));

  const std::pair<std::string_view, std::string_view> kRejected[] = {
      {"chrome", ""},
      {"chrome", "../x"},
      {"chrome", "System Profile"},
      {"arc", "a/b"},
      {"firefox", "a.default"},
      {"firefox", "Profiles/a/b"},
      {"firefox", "Profiles/.."},
      {"zen", "Profiles/z.default"},
      {"zen", "Profiles/z.default#2"},
      {"safari", "Default"},
  };
  for (const auto& [source, key] : kRejected) {
    EXPECT_TRUE(ImportHistoryFile(source, dir, key).empty())
        << source << " " << key;
  }
}

TEST(AtriumSettingsSourceTest, RunChromeImportReadsTheMappedProfilesHistory) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());

  const ImportOutcome outcome =
      RunChromeImportWithHistory(kFourVisits, profile.GetPath());

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_NE(std::string::npos, outcome.answer.find(R"("profileIds")"))
      << outcome.answer;
  EXPECT_EQ(std::string::npos, outcome.answer.find("history was left out"))
      << outcome.answer;
  EXPECT_EQ(std::string::npos, outcome.answer.find("history could not be read"))
      << outcome.answer;
  ASSERT_EQ(1u, outcome.history.size());
  EXPECT_EQ("Default", outcome.history[0].profile_id);
  EXPECT_EQ(history::SOURCE_OS_MIGRATION_IMPORTED,
            outcome.history[0].visit_source);
  ASSERT_EQ(4u, outcome.history[0].rows.size());
  EXPECT_EQ(GURL("https://quokka.example.test/field-notes"),
            outcome.history[0].rows[0].url());
}

TEST(AtriumSettingsSourceTest, AChromeHistoryPastTheCapIsCutWithANote) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());

  const ImportOutcome outcome =
      RunChromeImportWithHistory(kVisitsPastTheCap, profile.GetPath());

  ASSERT_EQ(1u, outcome.history.size());
  EXPECT_EQ(kMaxImportHistoryVisits, outcome.history[0].rows.size());
  EXPECT_NE(std::string::npos,
            outcome.answer.find("Older Chrome history was left out. Atrium "
                                "imports the newest 20000 visits for each "
                                "profile, and 50000 in all."))
      << outcome.answer;
}

TEST(AtriumSettingsSourceTest, AnUnreadableChromeHistoryGivesANote) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());

  const ImportOutcome outcome = RunChromeImportWithHistory("", profile.GetPath());

  EXPECT_EQ(std::string::npos, outcome.answer.find(R"("error")"))
      << "history never fails the import: " << outcome.answer;
  EXPECT_TRUE(outcome.history.empty());
  EXPECT_NE(std::string::npos,
            outcome.answer.find("Chrome history could not be read. This can "
                                "happen if Chrome is running."))
      << outcome.answer;
}

TEST(AtriumSettingsSourceTest, RunImportLeavesHistoryForReadOutcomeHistory) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir chrome, profile;
  ASSERT_TRUE(chrome.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  const base::FilePath chrome_profile = WriteChromeFolder(chrome.GetPath());
  ASSERT_TRUE(
      base::WriteFile(chrome_profile.Append("History"), "not a database"));
  atrium_core_close();

  ImportOutcome outcome = RunImport(
      "chrome", chrome.GetPath(), profile.GetPath().Append("spaces.json"),
      profile.GetPath().Append("routing.json"), base::FilePath());
  atrium_core_close();

  EXPECT_EQ(std::string::npos,
            outcome.answer.find("history could not be read"))
      << "RunImport must not read history: " << outcome.answer;
  EXPECT_TRUE(outcome.history.empty());

  outcome = ReadOutcomeHistory(std::move(outcome));

  EXPECT_NE(std::string::npos,
            outcome.answer.find("Chrome history could not be read. This can "
                                "happen if Chrome is running."))
      << outcome.answer;
  EXPECT_TRUE(outcome.history.empty());
}

TEST(AtriumSettingsSourceTest, TwoChromeProfilesMergeIntoOneHistoryBatch) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir chrome, profile;
  ASSERT_TRUE(chrome.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  // Neither folder is "Default", so the core maps the first by its name and
  // reuses that id for the second, which shares the same name.
  ASSERT_TRUE(base::WriteFile(
      chrome.GetPath().Append("Local State"),
      R"({"profile":{"info_cache":{"Profile 1":{"name":"Personal"},)"
      R"("Profile 2":{"name":"Personal"}}}})"));
  for (const char* name : {"Profile 1", "Profile 2"}) {
    const base::FilePath dir = chrome.GetPath().Append(name);
    ASSERT_TRUE(base::CreateDirectory(dir));
    ASSERT_TRUE(base::WriteFile(dir.Append("Bookmarks"), kChromeBookmarks));
  }
  // The same visit_time on both profiles, so the merge drops the repeat once.
  const int64_t shared_us = (base::Time::Now() - base::Days(1))
                                .ToDeltaSinceWindowsEpoch()
                                .InMicroseconds();
  const int64_t profile1_only_us = (base::Time::Now() - base::Hours(2))
                                       .ToDeltaSinceWindowsEpoch()
                                       .InMicroseconds();
  const int64_t profile2_only_us = (base::Time::Now() - base::Hours(3))
                                       .ToDeltaSinceWindowsEpoch()
                                       .InMicroseconds();
  auto write_history = [](const base::FilePath& path, int64_t shared_us,
                          int64_t only_us, const std::string& only_url) {
    sql::Database db(sql::DatabaseOptions(), "Test");
    CHECK(db.Open(path));
    CHECK(db.Execute(kChromiumUrlsTable));
    CHECK(db.Execute(kChromiumVisitsTable));
    CHECK(db.Execute(kQuokkaUrl));
    CHECK(db.Execute("INSERT INTO urls VALUES (2, '" + only_url +
                     "', 'Only', 1, 1, 0)"));
    CHECK(db.Execute(
        "INSERT INTO visits(url, visit_time, transition) VALUES (1, " +
        base::NumberToString(shared_us) + ", 536870913), (2, " +
        base::NumberToString(only_us) + ", 536870913)"));
  };
  write_history(chrome.GetPath().Append("Profile 1").Append("History"),
               shared_us, profile1_only_us, "https://alpha.example.test/");
  write_history(chrome.GetPath().Append("Profile 2").Append("History"),
               shared_us, profile2_only_us, "https://beta.example.test/");
  atrium_core_close();

  const ImportOutcome outcome = ReadOutcomeHistory(RunImport(
      "chrome", chrome.GetPath(), profile.GetPath().Append("spaces.json"),
      profile.GetPath().Append("routing.json"), base::FilePath()));
  atrium_core_close();

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(outcome.answer, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict()) << outcome.answer;
  const base::DictValue* ids =
      parsed->GetDict().FindDictByDottedPath("report.profileIds");
  ASSERT_TRUE(ids) << outcome.answer;
  const std::string* id1 = ids->FindString("Profile 1");
  const std::string* id2 = ids->FindString("Profile 2");
  ASSERT_TRUE(id1 && id2) << outcome.answer;
  ASSERT_EQ(*id1, *id2) << "both folders must map to the same Atrium id";

  ASSERT_EQ(1u, outcome.history.size()) << outcome.answer;
  EXPECT_EQ(*id1, outcome.history[0].profile_id);
  EXPECT_EQ(3u, outcome.history[0].rows.size()) << outcome.answer;
}

// 15000 unique-timestamp visits to one URL, using the same recursive-CTE
// seam as kVisitsPastTheCap, so two merged profiles can exceed the per-id
// cap without a slow per-row insert loop.
constexpr char kFifteenThousandVisits[] =
    "WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n "
    "WHERE i < 14999) INSERT INTO visits(url, visit_time, transition) "
    "SELECT 1, (strftime('%s','now') + 11644473600 - 86400 - i) * 1000000, "
    "536870913 FROM n";

TEST(AtriumSettingsSourceTest, TheMergedBatchStaysAtThePerIdCap) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir chrome, profile;
  ASSERT_TRUE(chrome.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      chrome.GetPath().Append("Local State"),
      R"({"profile":{"info_cache":{"Profile 1":{"name":"Personal"},)"
      R"("Profile 2":{"name":"Personal"}}}})"));
  for (const char* name : {"Profile 1", "Profile 2"}) {
    const base::FilePath dir = chrome.GetPath().Append(name);
    ASSERT_TRUE(base::CreateDirectory(dir));
    ASSERT_TRUE(base::WriteFile(dir.Append("Bookmarks"), kChromeBookmarks));
  }
  // Each profile alone fits under the cap, but the two together hold
  // 30000 unique rows for one Atrium id, well past kMaxImportHistoryVisits.
  auto write_history = [](const base::FilePath& path,
                          const std::string& url) {
    sql::Database db(sql::DatabaseOptions(), "Test");
    CHECK(db.Open(path));
    CHECK(db.Execute(kChromiumUrlsTable));
    CHECK(db.Execute(kChromiumVisitsTable));
    CHECK(db.Execute("INSERT INTO urls VALUES (1, '" + url +
                     "', 'Only', 1, 1, 0)"));
    CHECK(db.Execute(kFifteenThousandVisits));
  };
  write_history(chrome.GetPath().Append("Profile 1").Append("History"),
               "https://alpha.example.test/");
  write_history(chrome.GetPath().Append("Profile 2").Append("History"),
               "https://beta.example.test/");
  atrium_core_close();

  const ImportOutcome outcome = ReadOutcomeHistory(RunImport(
      "chrome", chrome.GetPath(), profile.GetPath().Append("spaces.json"),
      profile.GetPath().Append("routing.json"), base::FilePath()));
  atrium_core_close();

  ASSERT_EQ(1u, outcome.history.size()) << outcome.answer;
  EXPECT_EQ(kMaxImportHistoryVisits, outcome.history[0].rows.size());
  EXPECT_NE(std::string::npos,
            outcome.answer.find("Older Chrome history was left out."))
      << outcome.answer;
}

// Root and toolbar only: no bookmark link, so read_bookmarks finds nothing
// pinned and never asks for the folder's own "#0" profile.
constexpr base::cstring_view kNoBookmarkPlacesStatements[] = {
    "CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT, "
    "last_visit_date INTEGER, title TEXT, visit_count INTEGER DEFAULT 0, "
    "hidden INTEGER DEFAULT 0)",
    "CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, type INTEGER, "
    "fk INTEGER, parent INTEGER, position INTEGER, title TEXT, guid TEXT)",
    "CREATE TABLE moz_historyvisits (id INTEGER PRIMARY KEY, "
    "place_id INTEGER, visit_date INTEGER, visit_type INTEGER)",
    "INSERT INTO moz_places (id, url, last_visit_date) "
    "VALUES (1, 'https://lost.example.test/', NULL)",
    "INSERT INTO moz_bookmarks VALUES (1, 2, NULL, 0, 0, '', 'root________')",
    "INSERT INTO moz_bookmarks VALUES (3, 2, NULL, 1, 0, 'toolbar', "
    "'toolbar_____')",
    "INSERT INTO moz_historyvisits (place_id, visit_date, visit_type) "
    "VALUES (1, (strftime('%s','now') - 86400) * 1000000, 2)",
};

TEST(AtriumSettingsSourceTest, AZenFolderWithNoDefaultContainerGetsANote) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir zen, profile;
  ASSERT_TRUE(zen.CreateUniqueTempDir());
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  // Folder "a"'s only space uses a declared container, so its key is
  // "Profiles/a#1" and its places.sqlite is never read. Folder "b" has no
  // container, so its key is "Profiles/b#0" and it must still be read.
  const base::FilePath a = WriteZenSessionFolder(
      zen.GetPath(), "a",
      R"({"spaces":[{"uuid":"W1","name":"A","containerTabId":1}]})");
  ASSERT_TRUE(base::WriteFile(
      a.Append("containers.json"),
      R"({"identities":[{"userContextId":1,"public":true,"name":"Work"}]})"));
  const base::FilePath b = WriteZenSessionFolder(
      zen.GetPath(), "b", R"({"spaces":[{"uuid":"W2","name":"B"}]})");
  {
    sql::Database db(sql::DatabaseOptions(), "Test");
    ASSERT_TRUE(db.Open(a.Append("places.sqlite")));
    for (base::cstring_view statement : kNoBookmarkPlacesStatements) {
      ASSERT_TRUE(db.Execute(statement));
    }
  }
  WritePlaces(b.Append("places.sqlite"));
  sql::Database db(sql::DatabaseOptions(), "Test");
  ASSERT_TRUE(db.Open(b.Append("places.sqlite")));
  ASSERT_TRUE(db.Execute(
      "INSERT INTO moz_historyvisits (place_id, visit_date, visit_type) "
      "VALUES (1, (strftime('%s','now') - 86400) * 1000000, 2)"));
  db.Close();
  atrium_core_close();

  const ImportOutcome outcome = ReadOutcomeHistory(
      RunImport("zen", zen.GetPath(), profile.GetPath().Append("spaces.json"),
               profile.GetPath().Append("routing.json"), base::FilePath()));
  atrium_core_close();

  EXPECT_EQ(std::string::npos, outcome.answer.find("error")) << outcome.answer;
  EXPECT_NE(std::string::npos,
            outcome.answer.find(
                "Zen history was not imported for 1 profile folder, because "
                "every space in them uses a container."))
      << outcome.answer;
  ASSERT_EQ(1u, outcome.history.size()) << outcome.answer;
  ASSERT_EQ(1u, outcome.history[0].rows.size());
  EXPECT_EQ(GURL("https://news.example.test/"),
            outcome.history[0].rows[0].url());
}

}  // namespace
}  // namespace atrium
