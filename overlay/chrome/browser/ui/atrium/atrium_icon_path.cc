// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_icon_path.h"

#include <string_view>

#include "base/strings/escape.h"
#include "base/strings/string_util.h"

namespace atrium {

namespace {

// "favicon/1234.png" -> the id "1234". The id is whatever sits between the
// prefix and the suffix.
bool SplitIconPath(const std::string& path,
                   std::string_view prefix,
                   std::string* id) {
  constexpr std::string_view kSuffix = ".png";
  if (!base::StartsWith(path, prefix) || !base::EndsWith(path, kSuffix)) {
    return false;
  }
  const size_t start = prefix.size();
  const size_t end = path.size() - kSuffix.size();
  if (end <= start) {
    return false;
  }
  *id = path.substr(start, end - start);
  return true;
}

// The leading segment names the profile whose caches may answer. Left empty
// when there is none, which reaches no cache - the filter still claims the
// path, so it answers 404 rather than falling through to the resource map.
void TakeScope(IconRequest* out) {
  const size_t slash = out->id.find('/');
  if (slash == std::string::npos) {
    return;
  }
  out->scope = out->id.substr(0, slash);
  out->id = out->id.substr(slash + 1);
}

}  // namespace

IconRequest ParseIconPath(const std::string& path) {
  IconRequest out;
  // A tab or extension icon may carry ?r=<rev> (the Peek badge, extIconPath).
  // A url icon's query is part of its encoded key.
  const std::string no_query = path.substr(0, path.find('?'));
  if (SplitIconPath(no_query, "favicon/", &out.id)) {
    out.kind = IconKind::kFavicon;
    TakeScope(&out);
    return out;
  }
  if (SplitIconPath(no_query, "exticon/", &out.id)) {
    out.kind = IconKind::kExtension;
    TakeScope(&out);
    return out;
  }
  if (SplitIconPath(path, "appicon/", &out.id)) {
    out.kind = IconKind::kAppIcon;
    // Left raw, like an extension id. This id becomes a FILE NAME, so it is
    // checked against the slug alphabet before it does - see AppIconPath.
    return out;
  }
  if (SplitIconPath(path, "urlicon/", &out.id)) {
    out.kind = IconKind::kUrlIcon;
    TakeScope(&out);
    // A URL has to survive as ONE path segment, so it travels
    // percent-encoded, and the separators must be decoded here or the key
    // never equals the URL that was asked for. That is safe because the split
    // above already ran on the RAW path: an encoded slash cannot cut the key
    // in two, because by the time it becomes a slash there is nothing left to
    // split. Leaving it encoded looks like the cautious choice and is simply
    // the wrong key - it was caught by a test rather than by review.
    out.id = base::UnescapeURLComponent(
        out.id,
        base::UnescapeRule::PATH_SEPARATORS |
            base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);
    return out;
  }
  out.id.clear();
  return out;
}

IconKind IconRequestKind(const std::string& path) {
  return ParseIconPath(path).kind;
}

}  // namespace atrium
