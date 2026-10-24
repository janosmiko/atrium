// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_apply_changes_unittest.cc
#include "chrome/browser/ui/atrium/atrium_settings_window.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// SettingsApplyFields holds a base::DictValue and is not copyable, so each
// test builds its own `current` and `previous` from this rather than
// assigning one into the other.
SettingsApplyFields Baseline() {
  SettingsApplyFields fields;
  fields.confirm_to_quit = true;
  fields.external_links_open_in_little = false;
  fields.appearance.sidebar_on_right = false;
  fields.appearance.opaque_surround = true;
  fields.appearance.glass_alpha_percent = 80;
  fields.memory_saver_enabled = true;
  fields.memory_saver_threshold_minutes = 30;
  fields.notification_timeout = "short";
  fields.skylight = std::make_pair("dawn", true);
  fields.ai_enabled = true;
  fields.auto_summarize = true;
  fields.vim_mode_enabled = true;
  fields.translate_language = "hu";
  fields.chrome_page_tint = true;
  fields.app_icon_variant = "classic";
  return fields;
}

TEST(AtriumSettingsApplyChangesTest, FirstSaveAppliesEverything) {
  const SettingsApplyChanges changes =
      DetermineSettingsApplyChanges(Baseline(), std::nullopt);
  EXPECT_TRUE(changes.confirm_to_quit);
  EXPECT_TRUE(changes.external_links_open_in_little);
  EXPECT_TRUE(changes.appearance);
  EXPECT_TRUE(changes.memory_saver_enabled);
  EXPECT_TRUE(changes.memory_saver_threshold_minutes);
  EXPECT_TRUE(changes.notification_timeout);
  EXPECT_TRUE(changes.skylight);
  EXPECT_TRUE(changes.ai_enabled);
  EXPECT_TRUE(changes.auto_summarize);
  EXPECT_TRUE(changes.vim_mode_enabled);
  EXPECT_TRUE(changes.translate_language);
  EXPECT_TRUE(changes.chrome_page_tint);
  EXPECT_TRUE(changes.app_icon_variant);
}

TEST(AtriumSettingsApplyChangesTest, ARepeatedSaveWithNoEditsAppliesNothing) {
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      Baseline(), std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_FALSE(changes.confirm_to_quit);
  EXPECT_FALSE(changes.external_links_open_in_little);
  EXPECT_FALSE(changes.appearance);
  EXPECT_FALSE(changes.memory_saver_enabled);
  EXPECT_FALSE(changes.memory_saver_threshold_minutes);
  EXPECT_FALSE(changes.notification_timeout);
  EXPECT_FALSE(changes.skylight);
  EXPECT_FALSE(changes.ai_enabled);
  EXPECT_FALSE(changes.auto_summarize);
  EXPECT_FALSE(changes.vim_mode_enabled);
  EXPECT_FALSE(changes.translate_language);
  EXPECT_FALSE(changes.swipe_tuning);
  EXPECT_FALSE(changes.chrome_page_tint);
  EXPECT_FALSE(changes.app_icon_variant);
}

// AtriumBrowserWindow::SetChromePageTint restarts ambilight sampling on
// every live window, so an icon-only edit must not re-arm it.
TEST(AtriumSettingsApplyChangesTest, AnAppIconEditDoesNotTouchPageTint) {
  SettingsApplyFields current = Baseline();
  current.app_icon_variant = "midnight";
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.app_icon_variant);
  EXPECT_FALSE(changes.chrome_page_tint);
  EXPECT_FALSE(changes.appearance);
  EXPECT_FALSE(changes.ai_enabled);
}

TEST(AtriumSettingsApplyChangesTest, APageTintEditTriggersThePageTintPath) {
  SettingsApplyFields current = Baseline();
  current.chrome_page_tint = false;
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.chrome_page_tint);
  EXPECT_FALSE(changes.app_icon_variant);
}

TEST(AtriumSettingsApplyChangesTest, OnlyTheChangedAppearanceFieldFires) {
  SettingsApplyFields current = Baseline();
  current.appearance.glass_alpha_percent = 42;
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.appearance);
  EXPECT_FALSE(changes.chrome_page_tint);
  EXPECT_FALSE(changes.app_icon_variant);
}

// AppearanceValuesDiffer used to list fields by hand and never
// compared overlay_theme, so two different themes read as unchanged.
TEST(AtriumSettingsApplyChangesTest, AThemeOnlyChangeStillFiresAppearance) {
  SettingsApplyFields previous = Baseline();
  previous.appearance.overlay_theme = "crimson";
  SettingsApplyFields current = Baseline();
  current.appearance.overlay_theme = "dracula";
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(std::move(previous)));
  EXPECT_TRUE(changes.appearance);
}

// ApplyWindowAppearance's own accent-changed guard only ever runs when this
// outer gate fires, so a pure opaque_surround move must still trip it.
TEST(AtriumSettingsApplyChangesTest,
     AnOpaqueSurroundOnlyChangeStillFiresAppearanceWithAccentUnchanged) {
  SettingsApplyFields previous = Baseline();
  previous.appearance.overlay_theme = "crimson";
  SettingsApplyFields current = Baseline();
  current.appearance.overlay_theme = "crimson";
  current.appearance.opaque_surround = !*Baseline().appearance.opaque_surround;
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(std::move(previous)));
  EXPECT_TRUE(changes.appearance);
}

TEST(AtriumSettingsApplyChangesTest, AnAutoSummarizeEditTriggersItsOwnPath) {
  SettingsApplyFields current = Baseline();
  current.auto_summarize = false;
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.auto_summarize);
  EXPECT_FALSE(changes.ai_enabled);
  EXPECT_FALSE(changes.chrome_page_tint);
}

TEST(AtriumSettingsApplyChangesTest, AVimModeEditTriggersItsOwnPath) {
  SettingsApplyFields current = Baseline();
  current.vim_mode_enabled = false;
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.vim_mode_enabled);
  EXPECT_FALSE(changes.auto_summarize);
  EXPECT_FALSE(changes.ai_enabled);
}

// Vim mode takes single unmodified characters away from every site that binds
// its own, so a save that did not touch it must not switch it on.
TEST(AtriumSettingsApplyChangesTest, AnUnchangedVimModeIsNotReapplied) {
  SettingsApplyFields current = Baseline();
  current.translate_language = "fr";
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_FALSE(changes.vim_mode_enabled);
  EXPECT_TRUE(changes.translate_language);
}

TEST(AtriumSettingsApplyChangesTest, ATranslationLanguageEditTriggersItsOwnPath) {
  SettingsApplyFields current = Baseline();
  current.translate_language = "fr";
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      current, std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_TRUE(changes.translate_language);
  EXPECT_FALSE(changes.auto_summarize);
  EXPECT_FALSE(changes.ai_enabled);
}

TEST(AtriumSettingsApplyChangesTest, SwipeTuningNeverAppliesEmpty) {
  const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
      Baseline(), std::optional<SettingsApplyFields>(Baseline()));
  EXPECT_FALSE(changes.swipe_tuning);
}

}  // namespace
}  // namespace atrium
