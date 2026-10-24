# What the Swift host-ops table reached

Generated from `BrowserController::kHostOps` at the commit that deleted it
(the Swift removal). Each row is one call the Swift chrome could make into C++,
and what it reached.

**The wrappers are gone. Every implementation in the right column is still
there and still works.** What is missing is a caller. A rebuild - the
settings surface most of all - wires the right column to a
`chrome.send` name rather than writing it again.

Recover an exact wrapper with:

```
git show c16b44164810f:chrome/browser/ui/atrium/atrium_browser_controller.mm
```

Comments across the Atrium sources still cite doc blocks by name - SIDEBAR
WIDTH, SPLIT VIEW, EDIT FOCUS, TAB CYCLING and the rest. Those lived in
`atrium_bridge.h`, 2116 lines of frozen ABI documentation deleted in the same
task. They are the semantics of each op, not just its signature, and they are
worth reading before rebuilding one:

```
git show c16b44164810f:chrome/browser/ui/atrium/atrium_bridge.h
```

58 ops.

| Op | Reaches |
|---|---|
| `Navigate` | `is_valid`, `LoadURL` |
| `NewTab` | `get`, `chrome::AddTabAt` |
| `CloseTab` | `tab_strip_model`, `CloseWebContentsAt` |
| `ActivateTab` | `tab_strip_model`, `ActivateTabAt`, `value_or`, `window`, `ArmMoveOutFreeze` |
| `GoBack` | `CanGoBack`, `GoBack` |
| `GoForward` | `CanGoForward`, `GoForward` |
| `Reload` | `Reload` |
| `NewTabWithURL` | `is_valid`, `get`, `chrome::AddTabAt` |
| `ActivateWindowOp` | _sets a field on the controller_ |
| `SetBackdropTintOp` | `window`, `SetBackdropTint` |
| `CommandBarQuery` | `Query` |
| `CommandBarStop` | `Stop` |
| `SetDefaultSearchEngineOp` | `SetDefaultSearchEngine` |
| `OpenCommandBarOp` | `OpenCommandBar` |
| `CommandBarDismissOp` | `window`, `CloseCommandBar` |
| `SetSidebarWidthOp` | `window`, `SetSidebarWidth` |
| `SetSidebarOverlayWidthOp` | `window`, `SetSidebarOverlayWidth` |
| `SetNativeHotkeysOp` | `SetFromJSON`, `NativeHotkeyMap::Get` |
| `SetDownloadsDirectoryOp` | `empty`, `profile`, `DownloadPrefs::FromBrowserContext`, `SetDownloadPath` |
| `SetPromptForDownloadOp` | `profile`, `SetBoolean` |
| `PeekPromoteOp` | `PromotePeek` |
| `PeekDismissOp` | `ClosePeek` |
| `SetBackdropThemeOp` | `window`, `SetBackdropTheme` |
| `SetSplitOp` | `window`, `EnterSplit` |
| `UpdateSplitOp` | `window`, `UpdateSplit` |
| `CloseSplitOp` | `window`, `CloseSplit` |
| `RequestFaviconForURLOp` | `profile`, `is_valid`, `FaviconServiceFactory::GetForProfile` |
| `SetSidebarPositionOp` | `window`, `SetSidebarPosition` |
| `OpenWindowSlotOp` | `get` |
| `CopyProfileNamesJSONOp` | `profile_manager`, `BaseName`, `AsUTF8Unsafe`, `ProfileBootstrap::IsSafeProfileId`, `Set` |
| `SetProfileDisplayNameOp` | `ProfileBootstrap::IsSafeProfileId`, `profile_manager`, `user_data_dir`, `AppendASCII`, `empty` |
| `OpenWindowSlotForProfileOp` | `ProfileBootstrap::IsSafeProfileId`, `get` |
| `SetAutoPipEnabledOp` | _sets a field on the controller_ |
| `ExtensionActionClickOp` | `ClickExtensionAction` |
| `OpenExtensionsPageOp` | `get`, `chrome::ShowExtensions` |
| `UninstallExtensionOp` | `profile`, `ExtensionRegistry::Get`, `management_policy`, `ExtensionSystem::Get`, `UserMayModifySettings` |
| `SetSidebarWidthLiveOp` | `window`, `SetSidebarWidthLive` |
| `EnsureProfileLoadedOp` | `ProfileBootstrap::EnsureProfileLoaded` |
| `CaptureViewportOp` | `tab_strip_model` |
| `CaptureFullOp` | `tab_strip_model` |
| `StartRegionSelectOp` | `window`, `BeginRegionCapture`, `tab_strip_model` |
| `CaptureRegionOp` | `tab_strip_model` |
| `SetConfirmToQuitOp` | `local_state`, `SetBoolean` |
| `GetConfirmToQuitOp` | `local_state` |
| `SetContentFadeOp` | `window`, `SetContentFade` |
| `SplitPeekOp` | `SplitPeek` |
| `CommandBarDeleteMatchOp` | `DeleteMatch` |
| `SetSidebarOverlayWidthLiveOp` | `window`, `SetSidebarOverlayWidthLive` |
| `SetDefaultSearchEngineFromChoiceScreenOp` | `SetDefaultSearchEngine` |
| `SetGlassBackdropAlphaOp` | `window`, `SetGlassBackdropAlpha` |
| `ToggleExtensionsDeveloperModeOp` | `profile` |
| `SetAtriumTextFocusOp` | `PushEditFocus` |
| `SetBackdropTintLiveOp` | `window`, `SetBackdropTintLive` |
| `SetOpaqueSurroundOp` | `window`, `SetOpaqueSurround` |
| `SetBackdropScrubThemeOp` | `window`, `SetBackdropScrubTheme` |
| `SetBackdropScrubBlendOp` | `window`, `SetBackdropScrubBlend` |
| `SetBackdropScrubBlendEndOp` | `window`, `SetBackdropScrubBlendEnd` |
| `SidebarModelChangedOp` | `window`, `PushSidebarSnapshot` |
