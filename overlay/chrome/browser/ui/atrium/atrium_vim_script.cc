// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_vim_script.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/atrium_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace atrium {

std::u16string BuildVimInstallScript() {
  // LoadDataResourceString, not GetRawDataResource: entries in
  // atrium_resources.pak are gzipped per entry, and this decompresses.
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  // Grammar first. vim-page.js reads the global it publishes, so the other
  // order leaves every page with no vim and no error to say why.
  return base::StrCat(
      {base::UTF8ToUTF16(bundle.LoadDataResourceString(
           IDR_ATRIUM_VIM_GRAMMAR_JS)),
       u"\n",
       base::UTF8ToUTF16(bundle.LoadDataResourceString(
           IDR_ATRIUM_VIM_PAGE_JS))});
}

std::u16string BuildVimPopupInstallScript() {
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  // The same two, plus the bridge that drives them. vim-popup reads the global
  // vim-page publishes, so it goes last for the reason the grammar goes first.
  return base::StrCat(
      {BuildVimInstallScript(), u"\n",
       base::UTF8ToUTF16(bundle.LoadDataResourceString(
           IDR_ATRIUM_VIM_POPUP_JS))});
}

std::u16string BuildVimKeyScript(const std::string& character) {
  return base::StrCat({u"(window.__atriumVim ? window.__atriumVim.key(",
                       base::UTF8ToUTF16(
                           base::GetQuotedJSONString(character)),
                       u") : false)"});
}

std::u16string BuildVimModeDrainScript() {
  return u"(window.__atriumVim ? window.__atriumVim.drainMode() : 'NORMAL')";
}

std::u16string BuildVimTargetCountScript() {
  return u"(window.__atriumVim ? window.__atriumVim.clickableTargets().length"
         u" : 0)";
}

std::u16string BuildVimPaintHintsScript(int offset, int total) {
  return base::StrCat({u"(window.__atriumVim ? window.__atriumVim.paintHints(",
                       base::NumberToString16(offset), u", ",
                       base::NumberToString16(total), u") : 0)"});
}

std::u16string BuildVimTeardownScript() {
  return u"(window.__atriumVim ? window.__atriumVim.hide() : null)";
}

}  // namespace atrium
