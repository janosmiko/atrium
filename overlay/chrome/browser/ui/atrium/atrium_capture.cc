// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture.cc
#include "chrome/browser/ui/atrium/atrium_capture.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_capture_math.h"
#include "chrome/browser/ui/atrium/atrium_cdp_response.h"
// Completes viz::CopyOutputBitmapWithMetadata - render_widget_host_view.h
// only forward-declares the CopyFromSurfaceResult payload type, but
// OnCopyFromSurfaceResult reads result->bitmap.
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {

namespace {

// Empty src_rect/output_size tells CopyFromSurface to copy the whole surface
// at its native (device pixel) size - no timeout, matching the atrium
// snapshot precedent in atrium_browser_window.cc's OnDisplayChangeStarted.
constexpr base::TimeDelta kNoTimeout;

enum class EncodeAs { kPng, kJpeg };

// Lossy is the point for a thumbnail: a PNG of the same downscaled frame runs
// several times larger, and sixty of them are held per tab.
constexpr int kThumbnailJpegQuality = 70;

std::optional<std::vector<uint8_t>> EncodeBitmap(const SkBitmap& bitmap,
                                                 EncodeAs format) {
  if (bitmap.drawsNothing()) {
    return std::nullopt;
  }
  if (format == EncodeAs::kJpeg) {
    return gfx::JPEGCodec::Encode(bitmap, kThumbnailJpegQuality);
  }
  return gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                           /*discard_transparency=*/false);
}

// Always hops to the UI thread before running `callback`: CopyFromSurface's
// callback is documented as not guaranteed to run on the calling sequence, so
// callers relying on this API's UI-thread contract must not skip the hop even
// when the callback happens to already be on the UI thread today.
void DeliverOnUIThread(CaptureResultCallback callback,
                       std::optional<std::vector<uint8_t>> result) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(result)));
}

void OnCopyFromSurfaceResult(EncodeAs format,
                             CaptureResultCallback callback,
                             const content::CopyFromSurfaceResult& result) {
  DeliverOnUIThread(
      std::move(callback),
      result.has_value() ? EncodeBitmap(result->bitmap, format) : std::nullopt);
}

// Shared tail for the surface-copy entry points: null-RWHV guard, then
// dispatch the copy. CopyFromSurface's own conventions - `src_rect` is view
// DIP and empty means the whole surface, empty `output_size` means native.
void CopyFromView(content::RenderWidgetHostView* view,
                  const gfx::Rect& src_rect,
                  const gfx::Size& output_size,
                  EncodeAs format,
                  CaptureResultCallback callback) {
  if (!view) {
    DeliverOnUIThread(std::move(callback), std::nullopt);
    return;
  }
  view->CopyFromSurface(
      src_rect, output_size, kNoTimeout,
      base::BindOnce(&OnCopyFromSurfaceResult, format, std::move(callback)));
}

void DeliverFrameOnUIThread(FrameResultCallback callback,
                            const SkBitmap& frame) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), frame));
}

void OnCopyFrameResult(FrameResultCallback callback,
                       const content::CopyFromSurfaceResult& result) {
  DeliverFrameOnUIThread(std::move(callback),
                         result.has_value() ? result->bitmap : SkBitmap());
}

// Only ever attaches one client and sends one request (id 1), so any CDP
// message carrying a different id (or no id at all, i.e. an event - none are
// enabled on this attachment, but a stray one costs nothing to ignore) simply
// isn't ours; ParseCaptureScreenshotMessage reports that via
// kNotOurResponse and we keep waiting instead of finishing early.
constexpr int kCaptureScreenshotRequestId = 1;

// Generous but bounded: a hung renderer must not leak this client or its
// DevToolsAgentHost attachment forever.
constexpr base::TimeDelta kCaptureFullPageTimeout = base::Seconds(10);

// One-shot, self-owning DevToolsAgentHostClient for a single
// Page.captureScreenshot{captureBeyondViewport:true} round trip. Deletes
// itself after delivering exactly one result (success, error, timeout, or
// the agent host closing) and always detaches on the way out.
class FullPageScreenshotClient : public content::DevToolsAgentHostClient {
 public:
  static void Capture(content::WebContents* web_contents,
                      CaptureResultCallback callback) {
    // Self-owning: deletes itself in Finish(). Not held by a unique_ptr.
    // Start() runs after construction completes and may synchronously call
    // Finish() (e.g. attach refused) - Finish() may `delete this`, which
    // would be undefined behavior if invoked from inside the constructor
    // itself, so the attach/dispatch logic lives in Start(), not here.
    FullPageScreenshotClient* client =
        new FullPageScreenshotClient(std::move(callback));
    client->Start(web_contents);
  }

  FullPageScreenshotClient(const FullPageScreenshotClient&) = delete;
  FullPageScreenshotClient& operator=(const FullPageScreenshotClient&) =
      delete;

 private:
  explicit FullPageScreenshotClient(CaptureResultCallback callback)
      : callback_(std::move(callback)) {}

  ~FullPageScreenshotClient() override = default;

  void Start(content::WebContents* web_contents) {
    agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents);
    if (!agent_host_ || !agent_host_->AttachClient(this)) {
      agent_host_.reset();
      Finish(std::nullopt);
      return;
    }

    timeout_timer_.Start(FROM_HERE, kCaptureFullPageTimeout,
                         base::BindOnce(&FullPageScreenshotClient::OnTimeout,
                                       base::Unretained(this)));

    base::DictValue params;
    params.Set("format", "png");
    params.Set("captureBeyondViewport", true);
    base::DictValue request;
    request.Set("id", kCaptureScreenshotRequestId);
    request.Set("method", "Page.captureScreenshot");
    request.Set("params", std::move(params));

    const std::string json = base::WriteJson(request).value_or(std::string());
    agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json));
  }

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override {
    DCHECK_EQ(agent_host, agent_host_.get());
    CdpScreenshotResult result = ParseCaptureScreenshotMessage(
        base::as_string_view(message), kCaptureScreenshotRequestId);
    switch (result.outcome) {
      case CdpScreenshotOutcome::kNotOurResponse:
        return;  // Keep waiting for the real response.
      case CdpScreenshotOutcome::kFailed:
        Finish(std::nullopt);
        return;
      case CdpScreenshotOutcome::kSuccess:
        Finish(std::move(result.png_bytes));
        return;
    }
  }

  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override {
    DCHECK_EQ(agent_host, agent_host_.get());
    // The host is already gone - don't try to detach from it.
    agent_host_.reset();
    Finish(std::nullopt);
  }

  void OnTimeout() { Finish(std::nullopt); }

  void Finish(std::optional<std::vector<uint8_t>> result) {
    timeout_timer_.Stop();
    if (agent_host_) {
      agent_host_->DetachClient(this);
      agent_host_.reset();
    }
    DeliverOnUIThread(std::move(callback_), std::move(result));
    delete this;
  }

  CaptureResultCallback callback_;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  base::OneShotTimer timeout_timer_;
};

}  // namespace

void CaptureViewport(content::WebContents* web_contents,
                     CaptureResultCallback callback) {
  content::RenderWidgetHostView* view =
      web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
  CopyFromView(view, gfx::Rect(), gfx::Size(), EncodeAs::kPng,
               std::move(callback));
}

void CaptureViewportFrame(content::WebContents* web_contents,
                          const gfx::Size& output_size,
                          FrameResultCallback callback) {
  content::RenderWidgetHostView* view =
      web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    DeliverFrameOnUIThread(std::move(callback), SkBitmap());
    return;
  }
  view->CopyFromSurface(
      gfx::Rect(), output_size, kNoTimeout,
      base::BindOnce(&OnCopyFrameResult, std::move(callback)));
}

void CaptureThumbnail(content::WebContents* web_contents,
                      int target_width,
                      CaptureResultCallback callback) {
  content::RenderWidgetHostView* view =
      web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
  const gfx::Size bounds = view ? view->GetViewBounds().size() : gfx::Size();
  if (target_width <= 0 || bounds.IsEmpty()) {
    DeliverOnUIThread(std::move(callback), std::nullopt);
    return;
  }
  // Upscaling a narrow view would cost bytes and add no detail.
  const int width = std::min(target_width, bounds.width());
  const int height = std::max(1, width * bounds.height() / bounds.width());
  CopyFromView(view, gfx::Rect(), gfx::Size(width, height), EncodeAs::kJpeg,
               std::move(callback));
}

void CaptureRegion(content::WebContents* web_contents,
                   const gfx::Rect& content_rect,
                   CaptureResultCallback callback) {
  content::RenderWidgetHostView* view =
      web_contents ? web_contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    DeliverOnUIThread(std::move(callback), std::nullopt);
    return;
  }

  // DIP, not device pixels: CopyFromSurface takes its src_rect in view
  // coordinates and applies the device scale itself.
  const gfx::Rect src_rect_dip =
      ClampRegionToView(content_rect, view->GetViewBounds().size());
  if (src_rect_dip.IsEmpty()) {
    DeliverOnUIThread(std::move(callback), std::nullopt);
    return;
  }

  CopyFromView(view, src_rect_dip, gfx::Size(), EncodeAs::kPng,
               std::move(callback));
}

void CaptureFullPage(content::WebContents* web_contents,
                     CaptureResultCallback callback) {
  if (!web_contents) {
    DeliverOnUIThread(std::move(callback), std::nullopt);
    return;
  }
  FullPageScreenshotClient::Capture(web_contents, std::move(callback));
}

}  // namespace atrium
