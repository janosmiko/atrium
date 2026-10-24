// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_source.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_SOURCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/strings/cstring_view.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/atrium/atrium_import_history.h"

namespace atrium {

// Where settings.json is.
//
// Honours ATRIUM_SETTINGS_FILE, which vm-scenario.sh already stages - a reader
// that ignored the override would read the user's real settings during a test,
// which is the mistake SpacesFilePath exists to avoid for its own store.
//
// `app_support` is the Application Support directory. Returns an empty path
// when the override is unset and `app_support` is empty, so the caller falls
// through rather than reading from a made-up location.
base::FilePath SettingsFilePath(const std::string& env_override,
                                const base::FilePath& app_support);

// The same answer for THIS process: reads ATRIUM_SETTINGS_FILE and the
// Application Support directory itself, so a caller does not have to.
//
// Everything that reads or writes settings.json asks through here. A second
// caller working the environment out again is how two readers end up on
// different files, and a write that resolved differently from the read would
// edit a file the window is not showing.
// a path override the environment can set, honoured only in a build that
// is not shipped.
//
// The ATRIUM_*_FILE variables are test seams - the scenario harness stages a store
// and points the app at it - and they were live in every build. The settings path
// is a WRITE target too, so anything that could set the environment could redirect
// a 0600 write to a file of its choosing.
//
// One home rather than a gate per read site: there were five reads across three
// files and a check found the last two only after the first three were fixed.
//
// !OFFICIAL_BUILD rather than a debug gate, because the dev and VM builds run
// is_debug=false and a debug gate would take the scenario harness with it.
std::string DevOnlyPathOverride(base::cstring_view name);

base::FilePath ResolveSettingsFilePath();

// The settings the Rust core loads from `path`, as JSON.
//
// the running browser had no settings reader at all after the
// Swift cut - atrium_core_query appeared only in a unit test. This is the read
// path the settings surface needs.
//
// An unreadable or absent file yields the core's DEFAULTS rather than an
// error, which is what the store has always done. What it must never do is
// write those defaults back; nothing here writes.
std::string LoadSettingsJSON(const base::FilePath& path);

// What atrium_core_save_settings and its routing and hotkeys siblings return.
inline constexpr int32_t kSaveWritten = 1;
inline constexpr int32_t kSaveUnusableArguments = 0;
inline constexpr int32_t kSaveWriteFailed = -1;
inline constexpr int32_t kSaveDeclined = -2;

// atrium_core_save's answer when spaces.json reached disk and themes.json did
// not.
inline constexpr int32_t kCoreSaveThemesFailed = -2;

// Writes `settings_json` to the settings store at `path`.
//
// BLOCKING - the core fsyncs. Post it to a thread that allows blocking; doing
// this file work on the UI thread aborts the browser, which is the mistake
// PushSettings records.
//
// `settings_json` is a WHOLE settings object, in the shape LoadSettingsJSON
// returns, with the edit applied. A PARTIAL one is refused rather than
// completed from defaults: every key past the first four is optional, so a
// missing key and a default are the same bytes, and completing one would reset
// every field the surface did not send.
//
// The reader above never writes, which is what keeps a store this build cannot
// read from being replaced by defaults. This keeps that true from the other
// side: the core reads the file back before writing anything, and answers -2
// rather than touching one it does not understand (docs/model-rules.md R1.2).
//
// Returns the core's own code, which the caller reports apart: 1 written,
// 0 the arguments were not usable, -1 the write failed, -2 declined.
int32_t SaveSettingsJSON(const base::FilePath& path,
                         const std::string& settings_json);

// The ONE runner every settings write goes through, whoever starts it. A
// runner per caller orders writes within that caller and races across two, and
// the core renames without a lock, so the older edit can land last.
scoped_refptr<base::SequencedTaskRunner> SettingsWriteRunner();

// Where hotkeys.json is. Honours ATRIUM_HOTKEYS_FILE, which vm-scenario.sh
// already stages.
base::FilePath HotkeysFilePath(const std::string& env_override,
                               const base::FilePath& app_support);

// The hotkeys the Rust core loads from `path`, as JSON: schemaVersion plus an
// actions map of name to the combos bound to it.
//
// The core fills in every default, so an absent file yields the full map
// rather than an empty one - which is what the Shortcuts list needs to show a
// row per action.
//
// Two readers now: the Inputs pane draws the rows, and startup gets
// the same bytes so NativeHotkeyMap runs the bindings the pane shows.
std::string LoadHotkeysJSON(const base::FilePath& path);

// Writes `hotkeys_json` to the hotkeys store at `path` - same contract as
// SaveSettingsJSON, for hotkeys.json.
int32_t SaveHotkeysJSON(const base::FilePath& path,
                        const std::string& hotkeys_json);

// Whether every entry in `hotkeys_json` is one the native map can act on: a
// known action id and a combo that parses through ParseComboForTesting -
// the map's own load rule, checked here so a payload it would silently drop
// is refused before it is written.
bool HotkeysAreValid(const std::string& hotkeys_json);

// Where routing.json is. Honours ATRIUM_ROUTING_FILE, which the traffic-routing
// scenarios already stage.
base::FilePath RoutingFilePath(const std::string& env_override,
                               const base::FilePath& app_support);

// The routing rules the Rust core loads from `path`, as JSON. An absent or
// unreadable file yields empty rules, so routing is simply off rather than
// broken.
std::string LoadRoutingJSON(const base::FilePath& path);

// Writes `routing_json` to the routing store at `path` - same contract as
// SaveSettingsJSON, for routing.json.
int32_t SaveRoutingJSON(const base::FilePath& path,
                        const std::string& routing_json);

// Whether every rule in `routing_json` (the shape LoadRoutingJSON returns) is
// one the core can act on: a known match type, a non-empty pattern, and a
// target space that exists in the document currently open. Malformed JSON
// is not valid either - the caller refuses the whole payload rather than
// writing the rules it could parse.
bool RoutingRulesAreValid(const std::string& routing_json);

// The spaces document the Rust core loads from `path`, as JSON.
//
// The settings surface needs the whole document, not the sidebar model: it
// edits spaces rather than drawing one window's rows. SpacesFilePath, in
// atrium_sidebar_model_source.h, says where the file is.
std::string LoadSpacesJSON(const base::FilePath& path);

// Every source this build can import from.
inline constexpr std::string_view kImportSources[] = {"arc", "zen", "chrome",
                                                       "firefox"};

// The label Settings shows for `source`, or empty for one this build lacks.
std::string_view ImportSourceLabel(std::string_view source);

// The environment variable that overrides `source`'s data directory, or ""
// for one this build lacks.
std::string ImportDirOverrideName(std::string_view source);

// Where `source` keeps its files. `env_override` is read through
// DevOnlyPathOverride by the caller, which the import scenarios stage. Empty
// when the override is unset and `app_support` is empty, or `source` is
// unknown.
base::FilePath ImportDataDir(std::string_view source,
                             const std::string& env_override,
                             const base::FilePath& app_support);

// ImportDataDir resolved for THIS process, through DIR_APP_DATA and
// `source`'s own env override. Callers on the UI thread use this rather than
// working the environment out themselves.
base::FilePath ImportSourceDir(const std::string& source);

// Whether `source`'s files are in `dir`. Cheap: it reads no contents.
bool ImportDetected(std::string_view source, const base::FilePath& dir);

// The history file for the plan key `key` of `source` in `dir`, or an empty
// path. A key is another program's text, so only a plain folder name ever
// reaches a path. A Zen container shares its folder's places.sqlite with the
// folder's own profile, so only the `#0` key gets it.
base::FilePath ImportHistoryFile(std::string_view source,
                                 const base::FilePath& dir,
                                 std::string_view key);

// `source`'s import files as one JSON object of file name to contents, the
// shape atrium_core_import_preview and _run take. nullopt when nothing was
// detected. A missing or unreadable file is left out. BLOCKING.
std::optional<std::string> ReadImportFiles(std::string_view source,
                                           const base::FilePath& dir);

// ReadImportFiles's Zen path, with the shared decoded-bytes budget exposed so
// a test can set it small rather than writing real 64 MiB session files.
// Production code always goes through ReadImportFiles, at the real budget.
std::optional<std::string> ReadZenImportFilesForTesting(
    const base::FilePath& zen_dir,
    size_t total_budget);

// ReadImportFiles's Chrome path, with the shared budget exposed so a test can
// set it small. Production code always goes through ReadImportFiles.
std::optional<std::string> ReadChromeImportFilesForTesting(
    const base::FilePath& chrome_dir,
    size_t total_budget);

// ReadImportFiles's Firefox path, with the shared budget exposed so a test can
// set it small. Production code always goes through ReadImportFiles.
std::optional<std::string> ReadFirefoxImportFilesForTesting(
    const base::FilePath& firefox_dir,
    size_t total_budget);

// Whether this launch opens Settings on the import pane: spaces.json was
// absent, a source was found, and the offer was never shown.
bool ShouldOfferImport(bool spaces_absent,
                       bool detected,
                       const std::string& settings_json);

// `settings_json` with importPromptShown set to true, or "" when it is not a
// JSON object.
std::string WithImportPromptShown(const std::string& settings_json);

// A save during the import carried the old engines, so when `current` moved
// off `at_start` this is `current` with `imported`'s searchEngines, else "".
std::string ImportSettingsToResave(const std::string& at_start,
                                   const std::string& current,
                                   const std::string& imported);

// The core's preview answer for `source`'s files in `dir`. BLOCKING.
std::string PreviewImport(const std::string& source, const base::FilePath& dir);

// What an import left on disk.
struct ImportOutcome {
  // Which source this outcome came from.
  std::string source;
  // The page's result: the core's report, or {"error":...}. A routing or
  // settings save that failed adds "routingError" or "enginesError" with the
  // reason.
  std::string answer;
  // routing.json as written, or "" when no routing reached disk.
  std::string saved_routing;
  // settings.json as written when the import added search engines, or "".
  std::string saved_settings;
  // Each mapped profile's visits, for the UI thread to add to that profile's
  // history.
  std::vector<ImportedHistory> history;
  // `source`'s folder, for ReadOutcomeHistory. Empty on an outcome RunImport
  // returned before the spaces save, which never reads history.
  base::FilePath dir;
};

// Imports `source`'s files into the open document, opening a fresh one at
// `spaces_path` when none is open. Then writes spaces.json, routing.json only
// after the spaces write succeeds, and settings.json when the import added
// search engines. An empty `settings_path` leaves search engines out. Stops
// before history - see ReadOutcomeHistory. BLOCKING. Run on
// SettingsWriteRunner.
ImportOutcome RunImport(const std::string& source,
                        const base::FilePath& dir,
                        const base::FilePath& spaces_path,
                        const base::FilePath& routing_path,
                        const base::FilePath& settings_path);

// The history step RunImport stops before, so a Cmd+Q never waits on a large
// History file. Reads outcome.dir's history, appends notes to outcome.answer
// and fills outcome.history. No-op when outcome.answer has an error. BLOCKING.
ImportOutcome ReadOutcomeHistory(ImportOutcome outcome);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_SOURCE_H_
