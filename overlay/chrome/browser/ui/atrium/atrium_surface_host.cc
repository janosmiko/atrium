// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_surface_host.h"

#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"
#include "content/public/browser/web_contents.h"

namespace atrium {

namespace {

// Holds a bare pointer: the host owns the WebContents, so it always outlives
// this. Clearing happens in DetachFromContents rather than being relied on.
class SurfaceHostData : public base::SupportsUserData::Data {
 public:
  explicit SurfaceHostData(SurfaceHost* host) : host_(host) {}
  SurfaceHost* host() const { return host_; }

 private:
  const raw_ptr<SurfaceHost> host_;
};

// Address-of-static as the key, the standard Chromium idiom (see TabIdData in
// atrium_browser_controller.cc).
const void* const kSurfaceHostKey = &kSurfaceHostKey;

}  // namespace

SurfaceHost::~SurfaceHost() = default;

void SurfaceHost::AttachToContents(content::WebContents* contents) {
  if (contents) {
    contents->SetUserData(kSurfaceHostKey,
                          std::make_unique<SurfaceHostData>(this));
  }
}

void SurfaceHost::DetachFromContents(content::WebContents* contents) {
  if (contents) {
    contents->RemoveUserData(kSurfaceHostKey);
  }
}

gfx::Rect ScreenRectFromSurfaceArg(const base::Value& arg) {
  if (!arg.is_list()) {
    return gfx::Rect();
  }
  const base::ListValue& list = arg.GetList();
  if (list.size() != 4) {
    return gfx::Rect();
  }
  for (const base::Value& part : list) {
    if (!part.is_int()) {
      return gfx::Rect();
    }
  }
  return gfx::Rect(list[0].GetInt(), list[1].GetInt(), list[2].GetInt(),
                   list[3].GetInt());
}

SurfaceHost* SurfaceHostFor(content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  auto* data =
      static_cast<SurfaceHostData*>(contents->GetUserData(kSurfaceHostKey));
  return data ? data->host() : nullptr;
}

}  // namespace atrium
