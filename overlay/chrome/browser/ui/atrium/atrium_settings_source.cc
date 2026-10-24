// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_source.cc
#include "chrome/browser/ui/atrium/atrium_settings_source.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/environment.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_import_sql_reader.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

base::FilePath SettingsFilePath(const std::string& env_override,
                                const base::FilePath& app_support) {
  if (!env_override.empty()) {
    return base::FilePath(env_override);
  }
  if (app_support.empty()) {
    return base::FilePath();
  }
  return app_support.Append("Atrium").Append("settings.json");
}

std::string DevOnlyPathOverride(base::cstring_view name) {
#if defined(OFFICIAL_BUILD)
  return std::string();
#else
  return base::Environment::Create()->GetVar(name).value_or(std::string());
#endif
}

base::FilePath ResolveSettingsFilePath() {
  const std::string override_path = DevOnlyPathOverride("ATRIUM_SETTINGS_FILE");
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  return SettingsFilePath(override_path, app_support);
}

namespace {

constexpr char kArcSidebarFile[] = "StorableSidebar.json";
constexpr char kArcRoutingFile[] = "StorableLinkRouting.json";
constexpr char kArcLocalStateFile[] = "Local State";
// Arc's sidebar file runs to tens of megabytes on a heavy profile. The cap
// only stops a runaway file from taking the browser's memory.
constexpr size_t kMaxArcFileBytes = 64u * 1024 * 1024;

constexpr char kMozProfilesFile[] = "profiles.ini";
constexpr char kZenSessionsFile[] = "zen-sessions.jsonlz4";
constexpr char kZenContainersFile[] = "containers.json";
// profiles.ini and containers.json are a few kilobytes.
constexpr size_t kMaxSmallImportFileBytes = 1u * 1024 * 1024;
// More Zen profiles than this is not a real setup.
constexpr size_t kMaxImportProfiles = 16;
// LZ4 can expand about 255x, so many small profiles that each decode near
// kMaxArcFileBytes can together build one JSON string past PartitionAlloc's
// single-allocation ceiling and crash the browser. This caps their sum.
constexpr size_t kMaxZenSessionsTotalBytes = kMaxArcFileBytes;

constexpr char kMozPlacesFile[] = "places.sqlite";
constexpr char kMozBookmarksRows[] = "places.sqlite#bookmarks";
constexpr char kMozBookmarksQuery[] =
    "SELECT b.id, b.parent, b.type, b.position, b.title, b.guid, p.url, "
    "p.last_visit_date FROM moz_bookmarks b LEFT JOIN moz_places p "
    "ON p.id = b.fk ORDER BY b.parent, b.position";
constexpr std::string_view kMozBookmarksColumns[] = {
    "id",    "parent", "type", "position",
    "title", "guid",   "url",  "last_visit_date"};
// Far more bookmarks than a person keeps. It bounds the JSON a crafted
// places.sqlite can build.
constexpr size_t kMaxMozBookmarkRows = 100000;

constexpr char kFirefoxSessionFile[] = "sessionstore.jsonlz4";
constexpr char kFirefoxBackupsFolder[] = "sessionstore-backups";
constexpr char kFirefoxRecoveryFile[] = "recovery.jsonlz4";
constexpr char kFirefoxSearchFile[] = "search.json.mozlz4";
// The same ceiling as Zen and Chrome, for the same reason.
constexpr size_t kMaxFirefoxFilesTotalBytes = kMaxArcFileBytes;

constexpr char kChromeLocalStateFile[] = "Local State";
constexpr const char* kChromeBookmarksFiles[] = {"Bookmarks",
                                                 "AccountBookmarks"};
constexpr char kChromePreferencesFile[] = "Preferences";
constexpr char kChromeWebDataFile[] = "Web Data";
constexpr char kChromeKeywordsRows[] = "Web Data#keywords";
constexpr char kChromeKeywordsQuery[] =
    "SELECT short_name, keyword, url, safe_for_autoreplace, prepopulate_id, "
    "created_by_policy, is_active, starter_pack_id, sync_guid FROM keywords";
constexpr std::string_view kChromeKeywordsColumns[] = {
    "short_name",     "keyword",           "url",       "safe_for_autoreplace",
    "prepopulate_id", "created_by_policy", "is_active", "starter_pack_id",
    "sync_guid"};
// Far more rows than a person adds by hand. It bounds the JSON a crafted
// Web Data file can build.
constexpr size_t kMaxChromeKeywordRows = 500;
// The same ceiling as the Zen sessions, for the same reason: JSON escaping
// can grow the text six times before it reaches the core.
constexpr size_t kMaxChromeFilesTotalBytes = kMaxArcFileBytes;

struct ImportSourceInfo {
  std::string_view id;
  std::string_view label;
  std::string_view dir_override;
  // Under DIR_APP_DATA.
  std::string_view folder;
};

constexpr ImportSourceInfo kImportSourceInfo[] = {
    {"arc", "Arc", "ATRIUM_IMPORT_ARC_DIR", "Arc"},
    {"zen", "Zen", "ATRIUM_IMPORT_ZEN_DIR", "zen"},
    {"chrome", "Chrome", "ATRIUM_IMPORT_CHROME_DIR", "Google/Chrome"},
    {"firefox", "Firefox", "ATRIUM_IMPORT_FIREFOX_DIR", "Firefox"},
};

const ImportSourceInfo* FindImportSource(std::string_view source) {
  for (const ImportSourceInfo& info : kImportSourceInfo) {
    if (info.id == source) {
      return &info;
    }
  }
  return nullptr;
}

// Local State is another program's file, so a key that is not a plain folder
// name must never reach a read outside the Chrome folder. Chrome's own
// System and Guest folders hold no user bookmarks.
bool IsPlainFolderName(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of(std::string_view("/\\\0", 3)) ==
             std::string_view::npos &&
         name != "System Profile" && name != "Guest Profile";
}

// Shared by both readers: an unreadable file is not an error, it is the
// defaults, which is what these stores have always done. Nothing here writes.
std::string QueryStore(const char* store, const base::FilePath& path) {
  std::string contents;
  if (!path.empty()) {
    base::ReadFileToString(path, &contents);
  }
  char* raw = atrium_core_query(store, contents.c_str(),
                                /*migration_instant=*/0.0);
  if (!raw) {
    return std::string();
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

std::string TakeCoreString(char* raw) {
  if (!raw) {
    return std::string();
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

std::string ImportError(const std::string& message) {
  base::DictValue error;
  error.Set("error", message);
  std::string json;
  base::JSONWriter::Write(error, &json);
  return json;
}

}  // namespace

base::FilePath ImportHistoryFile(std::string_view source,
                                 const base::FilePath& dir,
                                 std::string_view key) {
  if (source == "chrome" || source == "arc") {
    const std::string_view folder =
        source == "arc" && key == "default" ? "Default" : key;
    if (!IsPlainFolderName(folder)) {
      return base::FilePath();
    }
    const base::FilePath root = source == "arc" ? dir.Append("User Data") : dir;
    return root.Append(folder).Append("History");
  }
  if (source == "firefox" || source == "zen") {
    constexpr std::string_view kProfiles = "Profiles/";
    if (!base::StartsWith(key, kProfiles)) {
      return base::FilePath();
    }
    std::string_view folder = key.substr(kProfiles.size());
    if (source == "zen") {
      if (!base::EndsWith(folder, "#0")) {
        return base::FilePath();
      }
      folder.remove_suffix(2);
    }
    if (!IsPlainFolderName(folder)) {
      return base::FilePath();
    }
    return dir.Append("Profiles").Append(folder).Append(kMozPlacesFile);
  }
  return base::FilePath();
}

base::FilePath HotkeysFilePath(const std::string& env_override,
                               const base::FilePath& app_support) {
  if (!env_override.empty()) {
    return base::FilePath(env_override);
  }
  if (app_support.empty()) {
    return base::FilePath();
  }
  return app_support.Append("Atrium").Append("hotkeys.json");
}

std::string LoadHotkeysJSON(const base::FilePath& path) {
  return QueryStore("hotkeys", path);
}

int32_t SaveHotkeysJSON(const base::FilePath& path,
                        const std::string& hotkeys_json) {
  return atrium_core_save_hotkeys(path.AsUTF8Unsafe().c_str(),
                                  hotkeys_json.c_str());
}

bool HotkeysAreValid(const std::string& hotkeys_json) {
  const std::optional<base::Value> root =
      base::JSONReader::Read(hotkeys_json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    return false;
  }
  const base::DictValue* actions = root->GetDict().FindDict("actions");
  if (!actions) {
    return false;
  }
  for (const auto [id, value] : *actions) {
    if (!NativeHotkeyActionForId(id)) {
      return false;
    }
    const base::ListValue* combos = value.GetIfList();
    if (!combos) {
      return false;
    }
    for (const base::Value& combo : *combos) {
      const std::string* raw = combo.GetIfString();
      if (!raw ||
          !NativeHotkeyMap::ParseComboForTesting(*raw).has_value()) {
        return false;
      }
    }
  }
  return true;
}

base::FilePath RoutingFilePath(const std::string& env_override,
                               const base::FilePath& app_support) {
  if (!env_override.empty()) {
    return base::FilePath(env_override);
  }
  if (app_support.empty()) {
    return base::FilePath();
  }
  return app_support.Append("Atrium").Append("routing.json");
}

std::string LoadRoutingJSON(const base::FilePath& path) {
  return QueryStore("routing", path);
}

int32_t SaveRoutingJSON(const base::FilePath& path,
                        const std::string& routing_json) {
  return atrium_core_save_routing(path.AsUTF8Unsafe().c_str(),
                                  routing_json.c_str());
}

std::string LoadSettingsJSON(const base::FilePath& path) {
  return QueryStore("settings", path);
}

int32_t SaveSettingsJSON(const base::FilePath& path,
                         const std::string& settings_json) {
  // An empty path reaches the core rather than being caught here, so there is
  // one place that decides what an unusable argument is. It answers 0.
  return atrium_core_save_settings(path.AsUTF8Unsafe().c_str(),
                                   settings_json.c_str());
}

scoped_refptr<base::SequencedTaskRunner> SettingsWriteRunner() {
  // BLOCK_SHUTDOWN because the edit most likely to be lost is the last one
  // before quit.
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return *runner;
}

std::string LoadSpacesJSON(const base::FilePath& path) {
  return QueryStore("spaces", path);
}

bool RoutingRulesAreValid(const std::string& routing_json) {
  const std::optional<base::Value> root =
      base::JSONReader::Read(routing_json, base::JSON_PARSE_RFC);
  if (!root || !root->is_dict()) {
    return false;
  }
  const base::ListValue* rules = root->GetDict().FindList("rules");
  if (!rules) {
    return false;
  }
  if (const std::string* fallback =
          root->GetDict().FindString("defaultTargetSpaceID");
      fallback && !fallback->empty()) {
    char* raw = atrium_core_space_slot_for_id(fallback->c_str());
    if (!raw) {
      return false;
    }
    atrium_core_string_free(raw);
  }
  for (const base::Value& item : *rules) {
    const base::DictValue* rule = item.GetIfDict();
    if (!rule) {
      return false;
    }
    const std::string* match_type = rule->FindString("matchType");
    if (!match_type || (*match_type != "contains" && *match_type != "regex")) {
      return false;
    }
    const std::string* pattern = rule->FindString("pattern");
    if (!pattern || pattern->empty()) {
      return false;
    }
    const std::string* target = rule->FindString("targetSpaceID");
    if (!target || target->empty()) {
      return false;
    }
    if (*target != "current") {
      char* raw = atrium_core_space_slot_for_id(target->c_str());
      if (!raw) {
        return false;
      }
      atrium_core_string_free(raw);
    }
  }
  return true;
}

std::string_view ImportSourceLabel(std::string_view source) {
  const ImportSourceInfo* info = FindImportSource(source);
  return info ? info->label : std::string_view();
}

std::string ImportDirOverrideName(std::string_view source) {
  const ImportSourceInfo* info = FindImportSource(source);
  return info ? std::string(info->dir_override) : std::string();
}

base::FilePath ImportDataDir(std::string_view source,
                             const std::string& env_override,
                             const base::FilePath& app_support) {
  const ImportSourceInfo* info = FindImportSource(source);
  if (!info) {
    return base::FilePath();
  }
  if (!env_override.empty()) {
    return base::FilePath(env_override);
  }
  if (app_support.empty()) {
    return base::FilePath();
  }
  return app_support.Append(info->folder);
}

base::FilePath ImportSourceDir(const std::string& source) {
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  return ImportDataDir(
      source, DevOnlyPathOverride(ImportDirOverrideName(source)), app_support);
}

bool ImportDetected(std::string_view source, const base::FilePath& dir) {
  if (dir.empty()) {
    return false;
  }
  if (source == "arc") {
    return base::PathExists(dir.Append(kArcSidebarFile));
  }
  if (source == "zen") {
    base::FileEnumerator folders(dir.Append("Profiles"), /*recursive=*/false,
                                 base::FileEnumerator::DIRECTORIES);
    for (base::FilePath folder = folders.Next(); !folder.empty();
         folder = folders.Next()) {
      if (base::PathExists(folder.Append(kZenSessionsFile))) {
        return true;
      }
    }
  }
  if (source == "chrome") {
    if (!base::PathExists(dir.Append(kChromeLocalStateFile))) {
      return false;
    }
    base::FileEnumerator folders(dir, /*recursive=*/false,
                                 base::FileEnumerator::DIRECTORIES);
    for (base::FilePath folder = folders.Next(); !folder.empty();
         folder = folders.Next()) {
      if (!IsPlainFolderName(folder.BaseName().value())) {
        continue;
      }
      for (const char* file : kChromeBookmarksFiles) {
        if (base::PathExists(folder.Append(file))) {
          return true;
        }
      }
    }
  }
  if (source == "firefox") {
    base::FileEnumerator folders(dir.Append("Profiles"), /*recursive=*/false,
                                 base::FileEnumerator::DIRECTORIES);
    for (base::FilePath folder = folders.Next(); !folder.empty();
         folder = folders.Next()) {
      if (base::PathExists(folder.Append(kMozPlacesFile))) {
        return true;
      }
    }
  }
  return false;
}

namespace {

std::optional<std::string> ReadArcImportFiles(const base::FilePath& arc_dir) {
  if (!ImportDetected("arc", arc_dir)) {
    return std::nullopt;
  }
  const std::pair<const char*, base::FilePath> files[] = {
      {kArcSidebarFile, arc_dir.Append(kArcSidebarFile)},
      {kArcRoutingFile, arc_dir.Append(kArcRoutingFile)},
      {kArcLocalStateFile,
       arc_dir.Append("User Data").Append(kArcLocalStateFile)},
  };
  base::DictValue out;
  for (const auto& [key, path] : files) {
    std::string text;
    if (base::ReadFileToStringWithMaxSize(path, &text, kMaxArcFileBytes) &&
        base::IsStringUTF8(text)) {
      out.Set(key, std::move(text));
    }
  }
  if (!out.Find(kArcSidebarFile)) {
    return std::nullopt;
  }
  std::string json;
  if (!base::JSONWriter::Write(out, &json)) {
    return std::nullopt;
  }
  return json;
}

// A FIFO answers PathExists like a real file, and ReadFileToStringWithMaxSize
// then blocks in open() until a writer connects.
bool IsRegularFile(const base::FilePath& path) {
  base::stat_wrapper_t info;
  return base::File::Stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

// A decoded mozLz4 file, or "" for one that is not a regular file, is over
// the per-file cap, or does not decode.
std::string ReadMozLz4Text(const base::FilePath& path) {
  std::string packed;
  if (!IsRegularFile(path) ||
      !base::ReadFileToStringWithMaxSize(path, &packed, kMaxArcFileBytes)) {
    return std::string();
  }
  char* decoded = atrium_core_mozlz4_decode(packed.data(), packed.size());
  return decoded ? TakeCoreString(decoded) : std::string();
}

// "" when the copy or the query failed, so the core can say so.
std::string MozBookmarkRows(const base::FilePath& places) {
  return ReadImportRows(places, kMozBookmarksQuery, kMozBookmarksColumns,
                        kMaxMozBookmarkRows)
      .value_or(std::string());
}

// Without profiles.ini the core names profiles after their folders, so a
// missing or unreadable file is left out, not failed.
void AddProfilesIni(const base::FilePath& root, base::DictValue& out) {
  const base::FilePath ini_path = root.Append(kMozProfilesFile);
  std::string ini;
  if (IsRegularFile(ini_path) &&
      base::ReadFileToStringWithMaxSize(ini_path, &ini,
                                        kMaxSmallImportFileBytes) &&
      base::IsStringUTF8(ini)) {
    out.Set(kMozProfilesFile, std::move(ini));
  }
}

// Zen's files as the core's files map, each session decoded under its path.
// One that fails to decode, read, or fit the shared budget arrives empty, so
// the core fails the whole source with one message.
std::optional<std::string> ReadZenImportFiles(const base::FilePath& zen_dir,
                                              size_t total_budget) {
  base::DictValue out;
  size_t total = 0;
  auto budgeted = [&total, total_budget](std::string text) {
    total += text.size();
    return total > total_budget ? std::string() : std::move(text);
  };
  std::vector<base::FilePath> profile_folders;
  base::FileEnumerator folders(zen_dir.Append("Profiles"), false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath folder = folders.Next();
       !folder.empty() && profile_folders.size() < kMaxImportProfiles;
       folder = folders.Next()) {
    const std::string prefix =
        base::StrCat({"Profiles/", folder.BaseName().AsUTF8Unsafe(), "/"});
    const base::FilePath session_path = folder.Append(kZenSessionsFile);
    // A folder with no session file is not a profile, so it must not fail the
    // whole source the way a damaged one does.
    if (!base::PathExists(session_path)) {
      continue;
    }
    out.Set(prefix + kZenSessionsFile,
            budgeted(ReadMozLz4Text(session_path)));
    profile_folders.push_back(folder);
    const base::FilePath containers_path = folder.Append(kZenContainersFile);
    std::string containers;
    if (IsRegularFile(containers_path) &&
        base::ReadFileToStringWithMaxSize(containers_path, &containers,
                                          kMaxSmallImportFileBytes) &&
        base::IsStringUTF8(containers)) {
      out.Set(prefix + kZenContainersFile, std::move(containers));
    }
  }
  if (profile_folders.empty()) {
    return std::nullopt;
  }
  // A rows value that does not fit what is left of the budget is dropped
  // without being charged, so it never starves a sibling profile's rows.
  for (const base::FilePath& folder : profile_folders) {
    const base::FilePath places = folder.Append(kMozPlacesFile);
    if (!base::PathExists(places)) {
      continue;
    }
    const std::string prefix =
        base::StrCat({"Profiles/", folder.BaseName().AsUTF8Unsafe(), "/"});
    std::string rows = MozBookmarkRows(places);
    if (total + rows.size() > total_budget) {
      rows.clear();
    } else {
      total += rows.size();
    }
    out.Set(prefix + kMozBookmarksRows, std::move(rows));
  }
  AddProfilesIni(zen_dir, out);
  std::string json;
  if (!base::JSONWriter::Write(out, &json)) {
    return std::nullopt;
  }
  return json;
}

// The text of a regular file within the per-file cap, or "" for anything
// else, so the core fails the source with one message.
std::string ReadCappedText(const base::FilePath& path) {
  std::string text;
  if (!IsRegularFile(path) ||
      !base::ReadFileToStringWithMaxSize(path, &text, kMaxArcFileBytes) ||
      !base::IsStringUTF8(text)) {
    return std::string();
  }
  return text;
}

// Local State names the profile folders.
std::vector<std::string> ChromeProfileFolders(const std::string& local_state) {
  std::vector<std::string> folders;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(local_state, base::JSON_PARSE_RFC);
  const base::DictValue* cache =
      parsed && parsed->is_dict()
          ? parsed->GetDict().FindDictByDottedPath("profile.info_cache")
          : nullptr;
  if (!cache) {
    return folders;
  }
  for (const auto [name, info] : *cache) {
    if (folders.size() == kMaxImportProfiles) {
      break;
    }
    if (IsPlainFolderName(name)) {
      folders.push_back(name);
    }
  }
  return folders;
}

// Preferences runs to megabytes, and the core needs one key from it. Only
// that key travels on, so the file does not use up the shared budget.
std::string ChromeDefaultEngine(const std::string& preferences) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(preferences, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  base::DictValue out;
  // Chrome writes no guid while the user keeps its default engine, so "{}"
  // tells the core that apart from a damaged file.
  if (const std::string* guid = parsed->GetDict().FindStringByDottedPath(
          "default_search_provider.guid")) {
    out.SetByDottedPath("default_search_provider.guid", *guid);
  }
  return base::WriteJson(out).value_or(std::string());
}

// Chrome's files as the core's files map, keyed by folder path. A bookmarks
// file that exists but cannot be read, or that passes the shared budget,
// arrives empty, and the core fails the whole source with one message.
std::optional<std::string> ReadChromeImportFiles(
    const base::FilePath& chrome_dir,
    size_t total_budget) {
  const base::FilePath local_state_path =
      chrome_dir.Append(kChromeLocalStateFile);
  if (!base::PathExists(local_state_path)) {
    return std::nullopt;
  }
  size_t total = 0;
  auto budgeted = [&total, total_budget](std::string text) {
    total += text.size();
    return total > total_budget ? std::string() : std::move(text);
  };
  base::DictValue out;
  std::string local_state = budgeted(ReadCappedText(local_state_path));
  const std::vector<std::string> folders = ChromeProfileFolders(local_state);
  out.Set(kChromeLocalStateFile, std::move(local_state));
  for (const std::string& name : folders) {
    const base::FilePath folder = chrome_dir.Append(name);
    const std::string prefix = name + "/";
    bool has_bookmarks = false;
    for (const char* file : kChromeBookmarksFiles) {
      const base::FilePath path = folder.Append(file);
      // A folder with no bookmarks file is not a profile to import, so it
      // must not fail the source the way a damaged file does.
      if (!base::PathExists(path)) {
        continue;
      }
      has_bookmarks = true;
      out.Set(prefix + file, budgeted(ReadCappedText(path)));
    }
    if (!has_bookmarks) {
      continue;
    }
    const base::FilePath preferences = folder.Append(kChromePreferencesFile);
    if (base::PathExists(preferences)) {
      out.Set(prefix + kChromePreferencesFile,
              budgeted(ChromeDefaultEngine(ReadCappedText(preferences))));
    }
    const base::FilePath web_data = folder.Append(kChromeWebDataFile);
    if (base::PathExists(web_data)) {
      out.Set(
          prefix + kChromeKeywordsRows,
          budgeted(ReadImportRows(web_data, kChromeKeywordsQuery,
                                  kChromeKeywordsColumns, kMaxChromeKeywordRows)
                       .value_or(std::string())));
    }
  }
  std::string json;
  if (!base::JSONWriter::Write(out, &json)) {
    return std::nullopt;
  }
  return json;
}

// A running Firefox writes only recovery.jsonlz4, and a clean quit writes
// sessionstore.jsonlz4, so the later of the two holds the newest tabs.
base::FilePath NewerSessionFile(const base::FilePath& folder) {
  const base::FilePath quit = folder.Append(kFirefoxSessionFile);
  const base::FilePath recovery =
      folder.Append(kFirefoxBackupsFolder).Append(kFirefoxRecoveryFile);
  base::File::Info quit_info;
  base::File::Info recovery_info;
  const bool has_quit = base::GetFileInfo(quit, &quit_info);
  const bool has_recovery = base::GetFileInfo(recovery, &recovery_info);
  if (has_quit && has_recovery) {
    return recovery_info.last_modified > quit_info.last_modified ? recovery
                                                                 : quit;
  }
  if (has_quit) {
    return quit;
  }
  return has_recovery ? recovery : base::FilePath();
}

// Firefox's files as the core's files map, keyed by profile path. The newer
// session file arrives under sessionstore.jsonlz4, whichever one it was.
std::optional<std::string> ReadFirefoxImportFiles(
    const base::FilePath& firefox_dir,
    size_t total_budget) {
  size_t total = 0;
  auto budgeted = [&total, total_budget](std::string text) {
    total += text.size();
    return total > total_budget ? std::string() : std::move(text);
  };
  base::DictValue out;
  size_t profiles = 0;
  base::FileEnumerator folders(firefox_dir.Append("Profiles"), false,
                               base::FileEnumerator::DIRECTORIES);
  for (base::FilePath folder = folders.Next();
       !folder.empty() && profiles < kMaxImportProfiles;
       folder = folders.Next()) {
    const base::FilePath places = folder.Append(kMozPlacesFile);
    // A folder with no places.sqlite is not a profile, so it must not fail
    // the whole source the way a damaged one does.
    if (!base::PathExists(places)) {
      continue;
    }
    ++profiles;
    const std::string prefix =
        base::StrCat({"Profiles/", folder.BaseName().AsUTF8Unsafe(), "/"});
    out.Set(prefix + kMozBookmarksRows, budgeted(MozBookmarkRows(places)));
    const base::FilePath session = NewerSessionFile(folder);
    if (!session.empty()) {
      out.Set(prefix + kFirefoxSessionFile,
              budgeted(ReadMozLz4Text(session)));
    }
    const base::FilePath search = folder.Append(kFirefoxSearchFile);
    if (base::PathExists(search)) {
      out.Set(prefix + kFirefoxSearchFile, budgeted(ReadMozLz4Text(search)));
    }
  }
  if (profiles == 0) {
    return std::nullopt;
  }
  AddProfilesIni(firefox_dir, out);
  std::string json;
  if (!base::JSONWriter::Write(out, &json)) {
    return std::nullopt;
  }
  return json;
}

}  // namespace

std::optional<std::string> ReadImportFiles(std::string_view source,
                                           const base::FilePath& dir) {
  if (source == "arc") {
    return ReadArcImportFiles(dir);
  }
  if (source == "zen") {
    return ReadZenImportFiles(dir, kMaxZenSessionsTotalBytes);
  }
  if (source == "chrome") {
    return ReadChromeImportFiles(dir, kMaxChromeFilesTotalBytes);
  }
  if (source == "firefox") {
    return ReadFirefoxImportFiles(dir, kMaxFirefoxFilesTotalBytes);
  }
  return std::nullopt;
}

std::optional<std::string> ReadZenImportFilesForTesting(
    const base::FilePath& zen_dir,
    size_t total_budget) {
  return ReadZenImportFiles(zen_dir, total_budget);
}

std::optional<std::string> ReadChromeImportFilesForTesting(
    const base::FilePath& chrome_dir,
    size_t total_budget) {
  return ReadChromeImportFiles(chrome_dir, total_budget);
}

std::optional<std::string> ReadFirefoxImportFilesForTesting(
    const base::FilePath& firefox_dir,
    size_t total_budget) {
  return ReadFirefoxImportFiles(firefox_dir, total_budget);
}

bool ShouldOfferImport(bool spaces_absent,
                       bool detected,
                       const std::string& settings_json) {
  if (!spaces_absent || !detected) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  const base::DictValue* dict = parsed ? parsed->GetIfDict() : nullptr;
  return !(dict && dict->FindBool("importPromptShown").value_or(false));
}

std::string WithImportPromptShown(const std::string& settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  parsed->GetDict().Set("importPromptShown", true);
  std::string json;
  base::JSONWriter::Write(*parsed, &json);
  return json;
}

std::string PreviewImport(const std::string& source,
                          const base::FilePath& dir) {
  std::optional<std::string> files = ReadImportFiles(source, dir);
  return TakeCoreString(atrium_core_import_preview(
      source.c_str(), files ? files->c_str() : nullptr));
}

std::string ImportSettingsToResave(const std::string& at_start,
                                   const std::string& current,
                                   const std::string& imported) {
  if (current == at_start) {
    return std::string();
  }
  std::optional<base::Value> merged =
      base::JSONReader::Read(current, base::JSON_PARSE_RFC);
  std::optional<base::Value> from =
      base::JSONReader::Read(imported, base::JSON_PARSE_RFC);
  const base::ListValue* engines =
      from && from->is_dict() ? from->GetDict().FindList("searchEngines")
                              : nullptr;
  if (!merged || !merged->is_dict() || !engines) {
    return std::string();
  }
  merged->GetDict().Set("searchEngines", engines->Clone());
  return base::WriteJson(*merged).value_or(std::string());
}

namespace {

// Why a routing save left routing.json as it was, for the result line.
std::string RoutingNotSavedReason(int32_t code) {
  switch (code) {
    case kSaveWriteFailed:
      return "the file could not be written";
    case kSaveDeclined:
      return "routing.json is in a form this build cannot read";
    default:
      return "the rules could not be used";
  }
}

// Why a settings save left settings.json as it was, for the result line.
std::string SettingsNotSavedReason(int32_t code) {
  return code == kSaveDeclined
             ? "settings.json is in a form this build cannot read"
             : "the file could not be written";
}

// A Zen key is "Profiles/<folder>#<container>". Only "#0" reads the folder's
// places.sqlite, so a folder whose spaces all use a container never has one.
size_t CountZenFoldersWithNoDefaultKey(const base::DictValue& ids) {
  std::map<std::string, bool> folder_has_zero;
  constexpr std::string_view kProfiles = "Profiles/";
  for (const auto entry : ids) {
    const std::string_view key = entry.first;
    if (!base::StartsWith(key, kProfiles)) {
      continue;
    }
    const std::string_view rest = key.substr(kProfiles.size());
    const size_t hash = rest.rfind('#');
    if (hash == std::string_view::npos) {
      continue;
    }
    bool& has_zero = folder_has_zero[std::string(rest.substr(0, hash))];
    has_zero = has_zero || rest.substr(hash) == "#0";
  }
  return std::count_if(
      folder_has_zero.begin(), folder_has_zero.end(),
      [](const auto& folder) { return !folder.second; });
}

// Read after the spaces save, so a slow copy of a large History file never
// holds back the spaces. A history problem is a note, never an import error:
// the spaces are already on disk.
std::vector<ImportedHistory> ReadMappedHistory(const std::string& source,
                                               const base::FilePath& dir,
                                               base::DictValue& answer) {
  std::vector<ImportedHistory> out;
  base::DictValue* report = answer.FindDict("report");
  const base::DictValue* ids =
      report ? report->FindDict("profileIds") : nullptr;
  base::ListValue* notes = report ? report->FindList("notes") : nullptr;
  if (!ids || !notes) {
    return out;
  }
  const bool mozilla = source == "zen" || source == "firefox";
  const HistoryDb db = mozilla ? HistoryDb::kMozilla : HistoryDb::kChromium;
  const history::VisitSource visit_source =
      mozilla ? history::SOURCE_FIREFOX_IMPORTED
              : history::SOURCE_OS_MIGRATION_IMPORTED;
  size_t left = kMaxImportHistoryVisitsTotal;
  bool cut = false;
  bool unreadable = false;
  // One batch per Atrium id: two source keys can map to the same id, and
  // their rows must land together for the newest-visit rule to see both.
  std::map<std::string, size_t> batch_for_id;
  std::map<std::string, std::set<std::pair<GURL, base::Time>>> seen_for_id;
  // Rows already taken for an id, so two merged keys share one per-id cap
  // instead of each getting kMaxImportHistoryVisits on its own.
  std::map<std::string, size_t> taken_for_id;
  for (const auto [key, id] : *ids) {
    const base::FilePath file = ImportHistoryFile(source, dir, key);
    if (!id.is_string() || file.empty() || !IsRegularFile(file)) {
      continue;
    }
    if (left == 0) {
      cut = true;
      continue;
    }
    const std::string& profile_id = id.GetString();
    const size_t taken = taken_for_id[profile_id];
    const size_t id_left = taken < kMaxImportHistoryVisits
                               ? kMaxImportHistoryVisits - taken
                               : 0;
    const size_t budget = std::min(id_left, left);
    if (budget == 0) {
      cut = true;
      continue;
    }
    std::optional<HistoryRead> read = ReadImportHistory(file, db, budget);
    if (!read) {
      unreadable = true;
      continue;
    }
    cut = cut || read->cut;
    left -= read->rows.size();
    taken_for_id[profile_id] += read->rows.size();
    if (read->rows.empty()) {
      continue;
    }
    const auto [it, inserted] =
        batch_for_id.try_emplace(profile_id, out.size());
    if (inserted) {
      out.push_back({profile_id, visit_source, {}});
    }
    history::URLRows& batch_rows = out[it->second].rows;
    std::set<std::pair<GURL, base::Time>>& seen = seen_for_id[profile_id];
    for (history::URLRow& row : read->rows) {
      if (seen.insert({row.url(), row.last_visit()}).second) {
        batch_rows.push_back(std::move(row));
      }
    }
  }
  const std::string_view label = ImportSourceLabel(source);
  if (cut) {
    notes->Append(base::StrCat(
        {"Older ", label, " history was left out. Atrium imports the newest ",
         base::NumberToString(kMaxImportHistoryVisits),
         " visits for each profile, and ",
         base::NumberToString(kMaxImportHistoryVisitsTotal), " in all."}));
  }
  if (unreadable) {
    notes->Append(base::StrCat(
        {label, " history could not be read. This can happen if ", label,
         " is running."}));
  }
  if (source == "zen") {
    const size_t lost = CountZenFoldersWithNoDefaultKey(*ids);
    if (lost > 0) {
      notes->Append(base::StrCat(
          {"Zen history was not imported for ", base::NumberToString(lost),
           " profile folder", lost == 1 ? "" : "s",
           ", because every space in them uses a container."}));
    }
  }
  return out;
}

}  // namespace

ImportOutcome RunImport(const std::string& source,
                        const base::FilePath& dir,
                        const base::FilePath& spaces_path,
                        const base::FilePath& routing_path,
                        const base::FilePath& settings_path) {
  std::optional<std::string> files = ReadImportFiles(source, dir);
  if (!files) {
    return {source,
            ImportError(base::StrCat(
                {ImportSourceLabel(source), "'s files are not on this Mac"}))};
  }
  // The file as it is, not LoadRoutingJSON's normalised copy, so the core
  // refuses a routing.json from a newer build instead of replacing it.
  std::string routing;
  if (base::PathExists(routing_path) &&
      !base::ReadFileToString(routing_path, &routing)) {
    return {source, ImportError("routing.json could not be read")};
  }
  // Engines ride on the whole settings object, because SaveSettingsJSON
  // refuses a partial one.
  std::optional<base::Value> settings;
  std::string engines;
  // Asked before the core call: a profile the import creates takes the
  // source's default engine, and a declined save must not leave its id behind.
  const int32_t settings_code =
      settings_path.empty()
          ? kSaveUnusableArguments
          : atrium_core_settings_writable(settings_path.AsUTF8Unsafe().c_str());
  if (settings_code == kSaveWritten) {
    settings = base::JSONReader::Read(LoadSettingsJSON(settings_path),
                                      base::JSON_PARSE_RFC);
  }
  if (settings && settings->is_dict()) {
    const base::ListValue* list = settings->GetDict().FindList("searchEngines");
    engines = list ? base::WriteJson(*list).value_or("[]") : "[]";
  }
  // A fresh profile has no spaces.json, so the core never opened. The core
  // opens one at `spaces_path` only after the source's files parse, and
  // never over an existing file.
  std::string answer = TakeCoreString(
      atrium_core_import_run(source.c_str(), files->c_str(), routing.c_str(),
                             engines.c_str(), spaces_path.value().c_str()));
  std::optional<base::Value> parsed =
      base::JSONReader::Read(answer, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return {source, ImportError("the import could not start")};
  }
  if (parsed->GetDict().Find("error")) {
    return {source, std::move(answer)};
  }
  // Spaces first: routing.json names space ids, so it must never reach disk
  // ahead of the spaces it points at.
  const int32_t saved = atrium_core_save();
  if (saved < 0 && saved != kCoreSaveThemesFailed) {
    return {source, ImportError("the imported spaces could not be saved, "
                                "and a relaunch loses them")};
  }
  if (saved == kCoreSaveThemesFailed) {
    parsed->GetDict().Set("themesError", "the file could not be written");
  }
  ImportOutcome outcome;
  outcome.source = source;
  outcome.dir = dir;
  if (const base::Value* rules = parsed->GetDict().Find("routing");
      rules && rules->is_dict()) {
    std::string routing_json;
    base::JSONWriter::Write(*rules, &routing_json);
    const int32_t code = RoutingRulesAreValid(routing_json)
                             ? SaveRoutingJSON(routing_path, routing_json)
                             : kSaveUnusableArguments;
    if (code == kSaveWritten) {
      outcome.saved_routing = std::move(routing_json);
    } else {
      parsed->GetDict().Set("routingError", RoutingNotSavedReason(code));
    }
  }
  if (const base::Value* list = parsed->GetDict().Find("searchEngines");
      list && list->is_list() && settings && settings->is_dict()) {
    settings->GetDict().Set("searchEngines", list->Clone());
    std::string settings_json = base::WriteJson(*settings).value_or("");
    const int32_t code = SaveSettingsJSON(settings_path, settings_json);
    if (code == kSaveWritten) {
      outcome.saved_settings = std::move(settings_json);
    } else {
      parsed->GetDict().Set("enginesError", SettingsNotSavedReason(code));
    }
  } else if (settings_code == kSaveWriteFailed ||
             settings_code == kSaveDeclined) {
    // Only a source with engines lost any. Arc and Zen carry none.
    std::optional<base::Value> preview =
        base::JSONReader::Read(TakeCoreString(atrium_core_import_preview(
                                   source.c_str(), files->c_str())),
                               base::JSON_PARSE_RFC);
    if (preview && preview->is_dict() &&
        preview->GetDict().FindIntByDottedPath("summary.engines").value_or(0) >
            0) {
      parsed->GetDict().Set("enginesError",
                            SettingsNotSavedReason(settings_code));
    }
  }
  base::JSONWriter::Write(*parsed, &outcome.answer);
  return outcome;
}

ImportOutcome ReadOutcomeHistory(ImportOutcome outcome) {
  if (outcome.answer.find(R"("error")") != std::string::npos) {
    return outcome;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(outcome.answer, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return outcome;
  }
  outcome.history =
      ReadMappedHistory(outcome.source, outcome.dir, parsed->GetDict());
  base::JSONWriter::Write(*parsed, &outcome.answer);
  return outcome;
}

}  // namespace atrium
