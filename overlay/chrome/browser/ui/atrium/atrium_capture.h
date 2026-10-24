// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/rect.h"

class SkBitmap;

namespace content {
class WebContents;
}  // namespace content

namespace atrium {

// capture-core stage of the screenshot utility. No ObjC
// dependency (WebContents, RenderWidgetHostView, and gfx::PNGCodec are all
// plain C++), so this is a .h/.cc pair rather than .mm - it stays includable
// from the //chrome/browser/ui/atrium:unit_tests source_set's plain-.cc TUs
// without pulling in an NSView-carrying header, which the chromium-rawptr
// plugin forbids there.
//
// Always delivered on the UI thread, regardless of which sequence
// RenderWidgetHostView::CopyFromSurface's callback happens to run on.
// nullopt on any failure: null `web_contents`, no RenderWidgetHostView, an
// empty/failed surface copy, or an encode failure.
using CaptureResultCallback =
    base::OnceCallback<void(std::optional<std::vector<uint8_t>>)>;

// Captures the current viewport (the full visible render surface) as PNG.
void CaptureViewport(content::WebContents* web_contents,
                     CaptureResultCallback callback);

// An empty bitmap on any failure, on the same terms as CaptureResultCallback's
// nullopt.
using FrameResultCallback = base::OnceCallback<void(const SkBitmap&)>;

// The viewport as raw pixels, downscaled to `output_size` by the GPU before the
// readback. No encode step, unlike CaptureViewport: a caller sampling several
// times a second would spend more on a PNG round trip than on the copy itself.
void CaptureViewportFrame(content::WebContents* web_contents,
                          const gfx::Size& output_size,
                          FrameResultCallback callback);

// Captures `content_rect` (DIP, in the WebContents' content coordinate
// space) as PNG, clipped to the rect. Converts to device pixels and clamps
// to the view's bounds internally (see atrium_capture_math.h). An empty or
// out-of-bounds `content_rect` yields nullopt.
void CaptureRegion(content::WebContents* web_contents,
                   const gfx::Rect& content_rect,
                   CaptureResultCallback callback);

// the viewport as a JPEG at most `target_width` px wide, aspect
// preserved, never upscaled. The surface copy does the downscale, so a stop
// costs a few tens of KB rather than a screenshot. nullopt on
// CaptureViewport's failures, plus a view with no size.
void CaptureThumbnail(content::WebContents* web_contents,
                      int target_width,
                      CaptureResultCallback callback);

// Captures the full page - including content beyond the current viewport
// (e.g. requiring scroll) - as one PNG. Unlike CaptureViewport/CaptureRegion,
// this attaches an internal content::DevToolsAgentHost to `web_contents` and
// dispatches Page.captureScreenshot{captureBeyondViewport:true} over the CDP
// wire format (NOT a devtools socket - in-process only), which lets content's
// own PageHandler perform the emulation dance (GetFullPageSize,
// record_whole_document) needed to render off-screen content. nullopt on:
// null `web_contents`, attach refusal, a malformed/error CDP response, a
// missing/undecodable `data` field, or a timeout.
void CaptureFullPage(content::WebContents* web_contents,
                     CaptureResultCallback callback);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_H_
