// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_overlay_theme_script.h"

#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

#include "chrome/browser/ui/atrium/atrium_page_color.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

// Every token lands inside a single-quoted JS string literal. The core only
// ever answers hexes and digit triplets, and this keeps that a fact.
constexpr char kTokenChars[] = "#0123456789abcdefABCDEF ";

std::optional<std::string> ReadToken(const base::DictValue& dict,
                                     const char* key) {
  const std::string* value = dict.FindString(key);
  if (!value || value->empty() ||
      value->find_first_not_of(kTokenChars) != std::string::npos) {
    return std::nullopt;
  }
  return *value;
}

std::string SetRootProperty(const char* name, const std::string& value) {
  return base::StringPrintf(
      "document.documentElement?.style.setProperty('%s', '%s');", name,
      value.c_str());
}

std::string ClearRootProperty(const char* name) {
  return base::StringPrintf(
      "document.documentElement?.style.removeProperty('%s');", name);
}

}  // namespace

std::optional<OverlayAccentTokens> OverlayAccentTokensForHex(
    const std::string& accent_hex,
    int accent_index) {
  char* raw = atrium_core_overlay_accent_tokens(
      accent_hex.c_str(), static_cast<int32_t>(accent_index));
  if (!raw) {
    return std::nullopt;
  }
  const std::string json(raw);
  atrium_core_string_free(raw);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = parsed->GetDict();
  const std::optional<std::string> accent = ReadToken(dict, "accent");
  const std::optional<std::string> ink = ReadToken(dict, "ink");
  const std::optional<std::string> paper = ReadToken(dict, "paper");
  const std::optional<std::string> soft = ReadToken(dict, "soft");
  const std::optional<std::string> accent_ink = ReadToken(dict, "accentInk");
  if (!accent || !ink || !paper || !soft || !accent_ink) {
    return std::nullopt;
  }
  return OverlayAccentTokens{*accent, *ink, *paper, *soft, *accent_ink};
}

std::string OverlayThemeScript(
    SkColor tint,
    bool has_tint,
    bool native,
    uint32_t surround_argb,
    SkColor surround_ink,
    const std::string& theme,
    const std::optional<OverlayAccentTokens>& system_accent) {
  // A filled button on the tint used to ink itself white by rule, which a pale
  // custom tint swallows. InkForBackdrop grades it the way the chrome does.
  const SkColor tint_ink = InkForBackdrop(tint);
  std::string tint_js =
      has_tint ? base::StringPrintf(
                     "document.documentElement?.style.setProperty("
                     "'--atrium-space-tint', '%d %d %d');"
                     "document.documentElement?.style.setProperty("
                     "'--atrium-space-tint-ink', '%d %d %d');",
                     SkColorGetR(tint), SkColorGetG(tint), SkColorGetB(tint),
                     SkColorGetR(tint_ink), SkColorGetG(tint_ink),
                     SkColorGetB(tint_ink))
               : "document.documentElement?.style.removeProperty("
                 "'--atrium-space-tint');"
                 "document.documentElement?.style.removeProperty("
                 "'--atrium-space-tint-ink');";
  // Every atrium-theme- class is cleared first: they are alternatives, and a
  // switch that only added would leave the previous one to win on file order.
  // Guarded on a slug of [a-z0-9-] at the call below, so nothing user-typed
  // reaches this string.
  const std::string theme_js = base::StringPrintf(
      "(() => { const r = document.documentElement; if (!r) { return; } "
      "[...r.classList].filter((c) => c.startsWith('atrium-theme-'))"
      ".forEach((c) => r.classList.remove(c)); "
      "%s })();",
      theme.empty()
          ? ""
          : base::StringPrintf("r.classList.add('atrium-theme-%s');",
                               theme.c_str())
                .c_str());
  // Cleared rather than left standing when the accent is not the system one:
  // an inline property on the root outranks every class, so a stale value
  // here would keep painting over whichever theme was picked next.
  std::string system_js;
  if (system_accent) {
    system_js =
        SetRootProperty("--atrium-system-accent", system_accent->accent) +
        SetRootProperty("--atrium-system-ink", system_accent->ink) +
        SetRootProperty("--atrium-system-paper", system_accent->paper) +
        SetRootProperty("--atrium-system-soft", system_accent->soft) +
        SetRootProperty("--atrium-system-accent-ink",
                        system_accent->accent_ink);
  } else {
    system_js = ClearRootProperty("--atrium-system-accent") +
                ClearRootProperty("--atrium-system-ink") +
                ClearRootProperty("--atrium-system-paper") +
                ClearRootProperty("--atrium-system-soft") +
                ClearRootProperty("--atrium-system-accent-ink");
  }
  system_js += base::StringPrintf(
      "document.documentElement?.classList.toggle("
      "'atrium-overlay-accent-system', %s);",
      system_accent ? "true" : "false");

  // Inverted: the accent defaults to "brand", so the class pinning --ov-accent
  // to the crimson fallback is the one present absent a push. A theme carries
  // its own accent, so the two must not both be on.
  // reruns any surface's shadow-spec push - --ov-shadow-tight
  // varies with everything above, and a no-op where nothing defines it.
  return tint_js + theme_js + system_js + base::StringPrintf(
                       "document.documentElement?.style.setProperty("
                       "'--atrium-surround-color', '#%02x%02x%02x');"
                       "document.documentElement?.style.setProperty("
                       "'--atrium-surround-ink', '%d %d %d');"
                       "document.documentElement?.classList.toggle("
                       "'atrium-native-chrome', %s);"
                       "document.documentElement?.classList.toggle("
                       "'atrium-overlay-accent-brand', %s)",
                       (surround_argb >> 16) & 0xFF,
                       (surround_argb >> 8) & 0xFF, surround_argb & 0xFF,
                       SkColorGetR(surround_ink), SkColorGetG(surround_ink),
                       SkColorGetB(surround_ink), native ? "true" : "false",
                       (theme.empty() && !system_accent) ? "true" : "false") +
         ";window.__atriumPushOverlayShadowSpec && "
         "window.__atriumPushOverlayShadowSpec();";
}

}  // namespace atrium
