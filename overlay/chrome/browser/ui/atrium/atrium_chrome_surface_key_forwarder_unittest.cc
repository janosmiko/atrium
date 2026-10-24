// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_chrome_surface_key_forwarder_unittest.cc
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"

#include <memory>

#include "base/time/time.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "url/gurl.h"

namespace atrium {
namespace {

class AtriumChromeSurfaceKeyForwarderTest
    : public ChromeRenderViewHostTestHarness {
 protected:
  void TearDown() override {
    AtriumChromeContext::ResetForTesting();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  std::unique_ptr<content::WebContents> CreateSurface() {
    return AtriumChromeContext::CreateForTesting(profile())
        ->CreateChromeContents(GURL("about:blank"));
  }

  input::NativeWebKeyboardEvent CommandKeyDown() {
    return input::NativeWebKeyboardEvent(
        blink::WebInputEvent::Type::kRawKeyDown, blink::WebInputEvent::kMetaKey,
        base::TimeTicks::Now());
  }
};

// The whole fix is one SetDelegate call in
// CreateChromeContents. A WebContents with no delegate drops every key the
// renderer does not handle - WebContentsImpl::HandleKeyboardEvent
// short-circuits on `delegate_ && ...` - so the chord dies with no log line
// and nothing else in the tree notices. Deleting that call would leave every
// other gate green, which is why this assertion exists.
TEST_F(AtriumChromeSurfaceKeyForwarderTest, CreateChromeContentsSetsADelegate) {
  std::unique_ptr<content::WebContents> contents = CreateSurface();
  ASSERT_TRUE(contents);
  EXPECT_NE(contents->GetDelegate(), nullptr);
}

// The forwarder looks its focus manager up per event, and
// UnhandledKeyboardEventHandler CHECKs that manager. A surface that is built
// but not yet parented in a widget has none, so the lookup failing has to end
// the call rather than reach the handler.
TEST_F(AtriumChromeSurfaceKeyForwarderTest,
       UnparentedSurfaceDeclinesInsteadOfCrashing) {
  std::unique_ptr<content::WebContents> contents = CreateSurface();
  ASSERT_TRUE(contents);
  content::WebContentsDelegate* delegate = contents->GetDelegate();
  ASSERT_TRUE(delegate);

  input::NativeWebKeyboardEvent event = CommandKeyDown();
  EXPECT_FALSE(delegate->HandleKeyboardEvent(contents.get(), event));
}

// Not covered here: the other branch, where a focus manager exists and the
// key is forwarded into it. Reaching it needs the surface's NSView parented in
// a live views::Widget, which a unit test at this layer cannot build. That
// path stays graded by the VM scenarios
// (savepage-swallow, shared-forwarder-covers-chords).

}  // namespace
}  // namespace atrium
