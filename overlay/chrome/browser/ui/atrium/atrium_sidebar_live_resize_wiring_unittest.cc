// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Read out of the source rather than driven: the behaviour needs a
// live AtriumBrowserWindow with a running timer, and no gtest in this tree
// builds one (see atrium_sidebar_snapshot_gate_unittest.cc's note on that).

#include <string>
#include <string_view>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

std::string ReadBrowserWindowSource() {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  const base::FilePath path = root.AppendASCII("chrome")
                                  .AppendASCII("browser")
                                  .AppendASCII("ui")
                                  .AppendASCII("atrium")
                                  .AppendASCII("atrium_browser_window.cc");
  std::string contents;
  CHECK(base::ReadFileToString(path, &contents)) << path;
  return contents;
}

// Empty when the signature is gone, which the caller must treat as a failure -
// a body nobody found would otherwise satisfy every check below.
std::string FunctionBody(const std::string& source,
                         std::string_view signature) {
  const size_t start = source.find(signature);
  if (start == std::string::npos) {
    return std::string();
  }
  // Free functions close on a brace in column 0.
  const size_t end = source.find("\n}\n", start);
  if (end == std::string::npos) {
    return std::string();
  }
  return source.substr(start, end - start);
}

TEST(AtriumSidebarLiveResizeWiringTest, AGripDragStepRunsTheLiveResizeCallback) {
  const std::string body =
      FunctionBody(ReadBrowserWindowSource(),
                   "void AtriumRootView::OnGripDragged(int width) {");
  ASSERT_FALSE(body.empty())
      << "AtriumRootView::OnGripDragged is gone or was reformatted - this "
         "guard cannot see the wiring any more, so fix the guard rather than "
         "deleting it";

  EXPECT_NE(body.find("on_live_resize_.Run("), std::string::npos)
      << "a grip drag step no longer runs the window's live-resize entry "
         "point, so nothing stops sidebar_resize_timer_ - a drag that starts "
         "while the pane is animating loses every step to the next tick";
  EXPECT_EQ(body.find("SetPreferredSize"), std::string::npos)
      << "a grip drag step writes the sidebar slot's preferred size directly "
         "again - the exact shape that fights an in-flight resize animation";
}

TEST(AtriumSidebarLiveResizeWiringTest, TheWindowSuppliesSetSidebarWidthLive) {
  EXPECT_NE(ReadBrowserWindowSource().find(
                "base::BindRepeating(&AtriumBrowserWindow::SetSidebarWidthLive"),
            std::string::npos)
      << "AtriumBrowserWindow no longer hands its live-resize entry point to "
         "AtriumRootView, which leaves the grip's callback bound to something "
         "else or to nothing";
}

TEST(AtriumSidebarLiveResizeWiringTest, AGripDragDoesNotHoldTheBrowserFrame) {
  const std::string source = ReadBrowserWindowSource();
  const std::string body = FunctionBody(
      source, "void AtriumBrowserWindow::SetSidebarWidthLive(double width) {");
  ASSERT_FALSE(body.empty())
      << "AtriumBrowserWindow::SetSidebarWidthLive is gone or was reformatted "
         "- fix this guard rather than deleting it";
  EXPECT_NE(body.find("SetPreferredSize"), std::string::npos)
      << "a live tick no longer sets the sidebar slot's preferred size, so "
         "the pane cannot move during a drag at all";

  // each of these paced the pane at the page's reflow rate - about
  // 5Hz on reddit against a native window-edge resize's 60Hz. Unpaced, the
  // realistic-page rig reads 34 pane widths for 40 pointer steps against 23.
  EXPECT_EQ(source.find("SetForceSpecifiedDeadline"), std::string::npos)
      << "the grip drag holds the browser's compositor frame for the page "
         "renderer again";
  EXPECT_EQ(source.find("RenderWidgetHostDidUpdateVisualProperties"),
            std::string::npos)
      << "the grip drag gates its ticks on a renderer resize ack again";
  EXPECT_EQ(body.find("DeprecatedLayoutImmediately"), std::string::npos)
      << "a live tick forces a synchronous relayout again, which lays the "
         "page out in the same pass and ties the pane to its reflow cost";
}

}  // namespace
