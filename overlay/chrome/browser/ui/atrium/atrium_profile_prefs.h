// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_prefs.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PREFS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PREFS_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"

class Profile;

namespace atrium {

// The resident Chromium profile for `profile_id`, or null. Refuses an id
// IsSafeProfileId rejects. A safe id that is not resident is asked for
// through ProfileBootstrap::EnsureProfileLoaded and still answers null this
// call - the load is asynchronous, and the row shows Unavailable until the
// next push.
Profile* LoadedProfileForId(const std::string& profile_id);

std::optional<bool> ProfileSuggestEnabled(const std::string& profile_id);
bool SetProfileSuggestEnabled(const std::string& profile_id, bool enabled);
std::optional<base::FilePath> ProfileDownloadDir(
    const std::string& profile_id);
bool SetProfileDownloadDir(const std::string& profile_id,
                            const base::FilePath& dir);

// Makes the mirrored engine `engine_id` that profile's user-selected
// default. Unknown id, or a service that has not loaded, is a logged no-op.
bool SetProfileDefaultSearchEngine(const std::string& profile_id,
                                    const std::string& engine_id);

// Re-applies the stored choice. Call after the engine mirror has run on that
// profile - the choice names an `atrium:` GUID the service lacks until then.
void ApplyStoredProfileSearchEngine(const std::string& profile_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PREFS_H_
