// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_marginalia_script.h"

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumMarginaliaScriptTest, WrapSelectionJsonEscapesTheMarkId) {
  const std::string script =
      base::UTF16ToUTF8(BuildWrapSelectionScript("m1\"); alert(1); //"));
  EXPECT_NE(script.find("wrapSelection(\"m1\\\"); alert(1); //\")"),
            std::string::npos);
  EXPECT_EQ(script.find("wrapSelection(\"m1\"); alert(1); //\")"),
            std::string::npos);
}

TEST(AtriumMarginaliaScriptTest, ReanchorParsesRatherThanSplicesTheJson) {
  const std::string script = base::UTF16ToUTF8(
      BuildReanchorScript(R"([{"id":"m-1","quote":"hi"}])"));
  EXPECT_NE(script.find("reanchor(JSON.parse("), std::string::npos);
}

TEST(AtriumMarginaliaScriptTest, EveryScriptCarriesTheSharedLibrary) {
  EXPECT_NE(BuildWrapSelectionScript("a").find(u"__atriumMarginalia"),
            std::u16string::npos);
  EXPECT_NE(BuildReanchorScript("[]").find(u"__atriumMarginalia"),
            std::u16string::npos);
  EXPECT_NE(BuildDrainClicksScript().find(u"drainClicks"),
            std::u16string::npos);
  EXPECT_NE(BuildScrollToMarkScript("a").find(u"scrollToMark"),
            std::u16string::npos);
}

}  // namespace
}  // namespace atrium
