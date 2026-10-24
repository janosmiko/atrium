// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_icon_source.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_SOURCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_SOURCE_H_

#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"

namespace atrium {

// The app icon variants, at chrome://atrium/appicon/<slug>.png.
//
// FILES, unlike the three caches in atrium_favicon_source.h, so a request
// reaches the disk: it goes off the UI thread, and the slug is checked before
// it becomes a path.

// Lowercase letters, digits and hyphens, at least one character. The slug
// arrives from the renderer's DOM and from settings.json, and neither is
// trusted with a path component.
bool IsAppIconSlug(std::string_view slug);

// <outer bundle>/Contents/Resources/atrium-icons/<slug>.png, which
// build/scripts/inject-icns.sh stages after every build. Empty for a slug
// IsAppIconSlug refuses.
base::FilePath AppIconPath(std::string_view slug);

// Null when the slug is refused or the file is absent. BLOCKING - call it on
// a thread that allows blocking.
scoped_refptr<base::RefCountedMemory> ReadAppIcon(const std::string& slug);

// ReadAppIcon on the thread pool, answering on the calling sequence. A null
// answer is the 404 that leaves the tile blank.
void ServeAppIcon(
    const std::string& slug,
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_SOURCE_H_
