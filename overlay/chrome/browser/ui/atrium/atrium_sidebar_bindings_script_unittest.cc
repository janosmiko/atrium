// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_bindings_script.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {

TEST(SidebarBindingsScript, EmitsOneKeyPerAction) {
  const std::string js =
      SidebarBindingsScript({{"toggleSidebar", "⌘S"}, {"newTab", "⌘T"}});
  EXPECT_NE(js.find("\"toggleSidebar\":\"⌘S\""), std::string::npos);
  EXPECT_NE(js.find("\"newTab\":\"⌘T\""), std::string::npos);
  EXPECT_NE(js.find("atriumSetSidebarBindings"), std::string::npos);
}

TEST(SidebarBindingsScript, EscapesAQuoteInAGlyph) {
  const std::string js = SidebarBindingsScript({{"x", "a\"b"}});
  EXPECT_NE(js.find("\"x\":\"a\\\"b\""), std::string::npos);
}

TEST(SidebarBindingsScript, GuardsOnTheHandlerBeforeCalling) {
  const std::string js = SidebarBindingsScript({{"toggleSidebar", "⌘S"}});
  EXPECT_NE(js.find("window.atriumSetSidebarBindings && "
                    "atriumSetSidebarBindings("),
            std::string::npos);
}

TEST(SidebarBindingsScript, EmptyMapStillCallsTheHandlerWithAnEmptyObject) {
  const std::string js = SidebarBindingsScript({});
  EXPECT_NE(js.find("atriumSetSidebarBindings({})"), std::string::npos);
}

}  // namespace atrium
