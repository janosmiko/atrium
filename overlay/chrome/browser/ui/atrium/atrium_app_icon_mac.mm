// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_app_icon_mac.mm
#include "chrome/browser/ui/atrium/atrium_app_icon_mac.h"

#import <AppKit/AppKit.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_icon_source.h"

namespace atrium {

namespace {

// UI thread only. The thread pool reads two PNGs in no fixed order, so a
// reply for an older pick must not overwrite a newer one.
std::string& WantedSlug() {
  static base::NoDestructor<std::string> slug;
  return *slug;
}

// The variant that names no artwork of its own. The bundle stack carries the
// light and dark tiles, and only AppKit picks between them, so this one is
// applied by taking the override off rather than by putting one on.
constexpr char kAutoSlug[] = "auto";

void SetDockIcon(const std::string& slug,
                 scoped_refptr<base::RefCountedMemory> png) {
  if (slug != WantedSlug()) {
    return;
  }
  // Once per process. A bundle built before inject-icns.sh staged the PNGs
  // misses on every save, and the warning would then ride every click.
  static bool complained = false;
  if (!png) {
    if (!complained) {
      complained = true;
      LOG(WARNING) << "Atrium: no app icon for variant '" << slug
                   << "'; the Dock keeps the bundle icon";
    }
    return;
  }
  NSData* data = [NSData dataWithBytes:png->data() length:png->size()];
  NSImage* image = [[NSImage alloc] initWithData:data];
  if (!image) {
    return;  // Bytes that are not an image; the bundle icon is the fallback.
  }
  NSApp.applicationIconImage = image;
  VLOG(1) << "Atrium: app icon variant '" << slug << "' applied";
}

}  // namespace

void AtriumApplyAppIcon(const std::string& slug) {
  WantedSlug() = slug;
  if (slug == kAutoSlug) {
    // nil is not "leave it alone" - it drops any override this process set, so
    // a switch back to Auto after another variant takes effect without a
    // relaunch.
    NSApp.applicationIconImage = nil;
    VLOG(1) << "Atrium: app icon follows the system appearance";
    return;
  }
  // The asset catalog holds one Icon Composer stack per variant, and AppKit
  // composites the glass material itself. The PNG path stays for a bundle
  // built without build/Assets.car.
  NSImage* glass = [NSImage imageNamed:base::SysUTF8ToNSString(slug)];
  if (glass) {
    NSApp.applicationIconImage = glass;
    VLOG(1) << "Atrium: app icon variant '" << slug << "' applied from the catalog";
    return;
  }
  ServeAppIcon(slug, base::BindOnce(&SetDockIcon, slug));
}

}  // namespace atrium
