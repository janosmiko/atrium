// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_prefs.cc
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"

#include "base/logging.h"
#include "base/strings/strcat.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/choice_made_location.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

namespace {

constexpr char kGuidPrefix[] = "atrium:";

void LogUnknownProfile(std::string_view op, const std::string& profile_id) {
  VLOG(1) << "Atrium: " << op << " names an unknown profile " << profile_id;
}

}  // namespace

Profile* LoadedProfileForId(const std::string& profile_id) {
  if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
    return nullptr;
  }
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager) {
    return nullptr;
  }
  const base::FilePath path =
      profile_manager->user_data_dir().AppendASCII(profile_id);
  return profile_manager->GetProfileByPath(path);
}

std::optional<bool> ProfileSuggestEnabled(const std::string& profile_id) {
  Profile* profile = LoadedProfileForId(profile_id);
  if (!profile) {
    return std::nullopt;
  }
  return profile->GetPrefs()->GetBoolean(prefs::kSearchSuggestEnabled);
}

bool SetProfileSuggestEnabled(const std::string& profile_id, bool enabled) {
  Profile* profile = LoadedProfileForId(profile_id);
  if (!profile) {
    LogUnknownProfile("set_profile_suggest_enabled", profile_id);
    return false;
  }
  profile->GetPrefs()->SetBoolean(prefs::kSearchSuggestEnabled, enabled);
  return true;
}

std::optional<base::FilePath> ProfileDownloadDir(
    const std::string& profile_id) {
  Profile* profile = LoadedProfileForId(profile_id);
  if (!profile) {
    return std::nullopt;
  }
  return profile->GetPrefs()->GetFilePath(prefs::kDownloadDefaultDirectory);
}

bool SetProfileDownloadDir(const std::string& profile_id,
                            const base::FilePath& dir) {
  Profile* profile = LoadedProfileForId(profile_id);
  if (!profile) {
    LogUnknownProfile("set_profile_download_dir", profile_id);
    return false;
  }
  profile->GetPrefs()->SetFilePath(prefs::kDownloadDefaultDirectory, dir);
  return true;
}

bool SetProfileDefaultSearchEngine(const std::string& profile_id,
                                    const std::string& engine_id) {
  Profile* profile = LoadedProfileForId(profile_id);
  if (!profile) {
    // Not resident yet - kick the load so the bootstrap's own apply step
    // (atrium_profile_bootstrap.cc) picks up the choice already written to
    // the core once the profile finishes loading.
    ProfileBootstrap::EnsureProfileLoaded(profile_id);
    LogUnknownProfile("set_profile_default_search_engine", profile_id);
    return false;
  }
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile);
  if (!service) {
    LogUnknownProfile("set_profile_default_search_engine", profile_id);
    return false;
  }
  TemplateURL* match =
      service->GetTemplateURLForGUID(base::StrCat({kGuidPrefix, engine_id}));
  if (!match) {
    LogUnknownProfile("set_profile_default_search_engine", profile_id);
    return false;
  }
  service->SetUserSelectedDefaultSearchProvider(
      match, search_engines::ChoiceMadeLocation::kSearchSettings);
  VLOG(1) << "Atrium: profile '" << profile_id << "' default engine set to "
          << engine_id;
  return true;
}

void ApplyStoredProfileSearchEngine(const std::string& profile_id) {
  char* engine = atrium_core_profile_search_engine(profile_id.c_str());
  if (!engine) {
    return;
  }
  SetProfileDefaultSearchEngine(profile_id, engine);
  atrium_core_string_free(engine);
}

}  // namespace atrium
