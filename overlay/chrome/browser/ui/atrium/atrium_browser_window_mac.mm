// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_browser_window_mac.mm
//
// the one AtriumBrowserWindow member whose signature names an
// AppKit type, kept apart so the rest of the window compiles as C++.
//
// It is not routed through AtriumWindowPlatform on purpose. Its two CHECKs
// state that an Atrium window is always in-process, and its only caller asks
// about a parent it has every reason to expect realised. Softening either to
// make the move tidier would take away an invariant to fix a defect it does
// not have.
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#import <objc/runtime.h>

#include "chrome/browser/ui/atrium/atrium_browser_window.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#import "chrome/browser/ui/atrium/atrium_courtyard_widget.h"
#import "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#import "ui/base/cocoa/base_view.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"

// Private in base_view.mm. enableTracking builds a new tracking area.
@interface BaseView (AtriumPointerTracking)
- (void)enableTracking;
- (void)disableTracking;
@end

namespace atrium {
namespace {

// AppKit posts NSSystemColorsDidChangeNotification, and cfprefsd posts the
// distributed one - which is the only one that arrives when the default is
// written from a shell rather than from System Settings.
NSString* const kAccentPreferenceChanged =
    @"AppleColorPreferencesChangedNotification";

id g_system_accent_observer = nil;
id g_system_accent_preference_observer = nil;

// The layer rides on the window rather than a table here, so a closed overlay
// takes its entry with it and nothing has to be told the window is gone.
const char kOverlayLayerKey = 0;

int AccentByte(CGFloat component) {
  const double clamped =
      component < 0.0 ? 0.0 : (component > 1.0 ? 1.0 : component);
  return static_cast<int>(clamped * 255.0 + 0.5);
}

// Private MultitouchSupport calls. NSHapticFeedbackManager never reaches an
// external Magic Trackpad, so without these it stays silent. Loaded by name so
// that a macOS release without them only loses the tap.
struct MultitouchApi {
  CFArrayRef (*create_device_list)() = nullptr;
  int32_t (*get_device_id)(CFTypeRef, uint64_t*) = nullptr;
  bool (*is_built_in)(CFTypeRef) = nullptr;
  CFTypeRef (*create_actuator)(uint64_t) = nullptr;
  int32_t (*open_actuator)(CFTypeRef) = nullptr;
  int32_t (*actuate)(CFTypeRef, int32_t, uint32_t, float, float) = nullptr;
  int32_t (*close_actuator)(CFTypeRef) = nullptr;
  bool loaded = false;
};

template <typename T>
void LoadSymbol(void* library, const char* name, T* out) {
  *out = reinterpret_cast<T>(dlsym(library, name));
}

const MultitouchApi& Multitouch() {
  static const MultitouchApi api = [] {
    MultitouchApi result;
    void* library = dlopen(
        "/System/Library/PrivateFrameworks/MultitouchSupport.framework/"
        "MultitouchSupport",
        RTLD_LAZY);
    if (!library) {
      return result;
    }
    LoadSymbol(library, "MTDeviceCreateList", &result.create_device_list);
    LoadSymbol(library, "MTDeviceGetDeviceID", &result.get_device_id);
    LoadSymbol(library, "MTDeviceIsBuiltIn", &result.is_built_in);
    LoadSymbol(library, "MTActuatorCreateFromDeviceID",
               &result.create_actuator);
    LoadSymbol(library, "MTActuatorOpen", &result.open_actuator);
    LoadSymbol(library, "MTActuatorActuate", &result.actuate);
    LoadSymbol(library, "MTActuatorClose", &result.close_actuator);
    result.loaded = result.create_device_list && result.get_device_id &&
                    result.is_built_in && result.create_actuator &&
                    result.open_actuator && result.actuate &&
                    result.close_actuator;
    return result;
  }();
  return api;
}

// The last MTActuatorActuate argument changes the feel, and ids 1 to 6 do not.
// Picked by hand on a Magic Trackpad, where 2 felt the softest.
struct Actuation {
  int32_t id;
  float strength;
};
constexpr Actuation kSoftTap = {6, 2.f};
constexpr Actuation kPlainTap = {6, 0.f};

// MTActuatorOpen takes about 35ms, and one swipe taps several times. So the
// actuators open once, off the UI thread, and close after a quiet spell.
constexpr base::TimeDelta kActuatorIdleClose = base::Seconds(2);

// One thread, not a sequence: the private API's thread needs are unknown.
scoped_refptr<base::SingleThreadTaskRunner> TapRunner() {
  static base::NoDestructor<scoped_refptr<base::SingleThreadTaskRunner>> runner(
      base::ThreadPool::CreateSingleThreadTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_BLOCKING}));
  return *runner;
}

// Only touched on TapRunner().
struct OpenActuators {
  std::vector<base::apple::ScopedCFTypeRef<CFTypeRef>> actuators;
  uint64_t last_tap = 0;
  // A Mac with only the built-in trackpad opens nothing, so this, not an
  // empty list, stops a device scan on every tap.
  bool scanned = false;
};

OpenActuators& Actuators() {
  static base::NoDestructor<OpenActuators> open;
  return *open;
}

void CloseActuators() {
  for (const auto& actuator : Actuators().actuators) {
    Multitouch().close_actuator(actuator.get());
  }
  Actuators().actuators.clear();
  Actuators().scanned = false;
}

void OpenExternalActuators() {
  const MultitouchApi& mt = Multitouch();
  base::apple::ScopedCFTypeRef<CFArrayRef> devices(mt.create_device_list());
  if (!devices) {
    return;
  }
  for (CFIndex i = 0; i < CFArrayGetCount(devices.get()); ++i) {
    CFTypeRef device = CFArrayGetValueAtIndex(devices.get(), i);
    uint64_t device_id = 0;
    // The built-in trackpad already got the public tap.
    if (mt.is_built_in(device) || mt.get_device_id(device, &device_id) != 0) {
      continue;
    }
    base::apple::ScopedCFTypeRef<CFTypeRef> actuator(
        mt.create_actuator(device_id));
    if (actuator && mt.open_actuator(actuator.get()) == 0) {
      Actuators().actuators.push_back(std::move(actuator));
    }
  }
}

void CloseActuatorsIfIdle(uint64_t tap) {
  if (tap == Actuators().last_tap) {
    CloseActuators();
  }
}

void TapOnRunner(Actuation actuation) {
  if (!Actuators().scanned) {
    OpenExternalActuators();
    Actuators().scanned = true;
  }
  bool failed = false;
  for (const auto& actuator : Actuators().actuators) {
    failed |= Multitouch().actuate(actuator.get(), actuation.id, 0, 0.f,
                                   actuation.strength) != 0;
  }
  // A trackpad that went away fails here. The next tap opens the list again.
  if (failed) {
    CloseActuators();
  }
  const uint64_t tap = ++Actuators().last_tap;
  TapRunner()->PostDelayedTask(FROM_HERE,
                               base::BindOnce(&CloseActuatorsIfIdle, tap),
                               kActuatorIdleClose);
}

void TapExternalTrackpads(Actuation actuation) {
  if (Multitouch().loaded) {
    TapRunner()->PostTask(FROM_HERE, base::BindOnce(&TapOnRunner, actuation));
  }
}

}  // namespace

std::optional<OverlayAccentTokens> SystemOverlayAccentTokens() {
  // Converted out of the catalog colour space first: the raw controlAccentColor
  // has no RGB components to read and answering it directly throws.
  NSColor* accent = [NSColor.controlAccentColor
      colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (!accent) {
    return std::nullopt;
  }
  // Read every time, and never cached alongside the colour: this is the half
  // that stays live, and it is the only reason a running window can follow the
  // Appearance pane at all.
  id stored =
      [NSUserDefaults.standardUserDefaults objectForKey:@"AppleAccentColor"];
  const int accent_index = [stored isKindOfClass:NSNumber.class]
                               ? static_cast<NSNumber*>(stored).intValue
                               : kNoMacosAccentIndex;
  return OverlayAccentTokensForHex(
      base::StringPrintf("#%02X%02X%02X", AccentByte(accent.redComponent),
                         AccentByte(accent.greenComponent),
                         AccentByte(accent.blueComponent)),
      accent_index);
}

void AtriumBrowserWindow::EnsureSystemAccentObserver() {
  if (g_system_accent_observer) {
    return;
  }
  // mainQueue, not nil: the distributed centre delivers on its own thread, and
  // the repaint walks the window list and touches WebContents.
  void (^repaint)(NSNotification*) = ^(NSNotification* note) {
    AtriumBrowserWindow::OnSystemAccentChanged();
  };
  g_system_accent_observer = [NSNotificationCenter.defaultCenter
      addObserverForName:NSSystemColorsDidChangeNotification
                  object:nil
                   queue:NSOperationQueue.mainQueue
              usingBlock:repaint];
  g_system_accent_preference_observer =
      [NSDistributedNotificationCenter.defaultCenter
          addObserverForName:kAccentPreferenceChanged
                      object:nil
                       queue:NSOperationQueue.mainQueue
                  usingBlock:repaint];
}

// The private path ignores the system haptics switch, so it reads it here.
void AtriumBrowserWindow::OnSurface_SidebarSwipeHaptic(
    const base::ListValue& args) {
  const bool plain = !args.empty() && args[0].is_string() &&
                     args[0].GetString() == "plain";
  [NSHapticFeedbackManager.defaultPerformer
      performFeedbackPattern:plain ? NSHapticFeedbackPatternLevelChange
                                  : NSHapticFeedbackPatternAlignment
             performanceTime:NSHapticFeedbackPerformanceTimeNow];
  id haptics_on = [NSUserDefaults.standardUserDefaults
      objectForKey:@"com.apple.trackpad.forceClick"];
  if (![haptics_on isKindOfClass:NSNumber.class] ||
      [haptics_on boolValue]) {
    TapExternalTrackpads(plain ? kPlainTap : kSoftTap);
  }
}

NSWindow* AtriumBrowserWindow::GetNSWindowForAnimation() {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      GetNativeWindow());
  CHECK(host);
  NSWindow* window = host->GetInProcessNSWindow();
  CHECK(window);  // Atrium windows are always in-process (see
                  // AtriumNativeWidgetMac::OnWindowInitialized).
  return window;
}

bool AtriumBrowserWindow::CourtyardCmdQRoutesToMainMenuForTesting() {
  if (!courtyard_widget_) {
    return false;
  }
  NSWindow* ns_window =
      courtyard_widget_->NativeWindowForTesting().GetNativeNSWindow();
  if (!ns_window ||
      ![ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    return false;
  }
  id<CommandDispatcherDelegate> delegate =
      [static_cast<id<CommandDispatchingWindow>>(ns_window) commandDispatcher]
          .delegate;
  if (!delegate) {
    return false;
  }
  NSEvent* cmd_q = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                     location:NSZeroPoint
                                modifierFlags:NSEventModifierFlagCommand
                                    timestamp:0
                                 windowNumber:ns_window.windowNumber
                                      context:nil
                                   characters:@"q"
                  charactersIgnoringModifiers:@"q"
                                    isARepeat:NO
                                      keyCode:12];
  return [delegate prePerformKeyEquivalent:cmd_q window:ns_window] ==
         ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

bool AtriumBrowserWindow::DeliverComboForTesting(std::string_view combo) {
  // Built from the map's own parse rather than from a second reading of the
  // combo string, so a test cannot press a chord the map would never bind.
  const std::optional<NativeHotkeyMap::ParsedCombo> parsed =
      NativeHotkeyMap::ParseComboForTesting(combo);
  if (!parsed) {
    return false;
  }
  NSWindow* ns_window = GetNSWindowForAnimation();
  if (!ns_window ||
      ![ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    return false;
  }
  id<CommandDispatcherDelegate> delegate =
      [static_cast<id<CommandDispatchingWindow>>(ns_window) commandDispatcher]
          .delegate;
  if (!delegate) {
    return false;
  }
  NSString* chars =
      [NSString stringWithUTF8String:parsed->character.c_str()] ?: @"";
  NSEvent* event = [NSEvent
                keyEventWithType:NSEventTypeKeyDown
                        location:NSZeroPoint
                   modifierFlags:static_cast<NSEventModifierFlags>(
                                     parsed->modifiers)
                       timestamp:0
                    windowNumber:ns_window.windowNumber
                         context:nil
                      characters:chars
     charactersIgnoringModifiers:chars
                       isARepeat:NO
                         keyCode:static_cast<unsigned short>(
                                     parsed->key_code < 0 ? 0
                                                          : parsed->key_code)];
  return [delegate prePerformKeyEquivalent:event window:ns_window] ==
         ui::PerformKeyEquivalentResult::kHandled;
}

void AtriumBrowserWindow::OrderOverlayChild(views::Widget* child,
                                            OverlayLayer layer) {
  NSWindow* window = child->GetNativeWindow().GetNativeNSWindow();
  NSWindow* parent = window.parentWindow;
  if (!parent) {
    return;
  }
  objc_setAssociatedObject(window, &kOverlayLayerKey,
                           @(static_cast<int>(layer)),
                           OBJC_ASSOCIATION_RETAIN_NONATOMIC);

  // addChildWindow:ordered: is a no-op on an already-parented window.
  NSMutableArray<NSWindow*>* ranked = [NSMutableArray array];
  for (NSWindow* sibling in parent.childWindows) {
    if (sibling.isVisible &&
        objc_getAssociatedObject(sibling, &kOverlayLayerKey)) {
      [ranked addObject:sibling];
    }
  }
  [ranked sortUsingComparator:^NSComparisonResult(NSWindow* a, NSWindow* b) {
    return [objc_getAssociatedObject(a, &kOverlayLayerKey)
        compare:objc_getAssociatedObject(b, &kOverlayLayerKey)];
  }];
  for (NSWindow* ordered in ranked) {
    [parent removeChildWindow:ordered];
  }
  for (NSWindow* ordered in ranked) {
    [parent addChildWindow:ordered ordered:NSWindowAbove];
  }
  VLOG(1) << "Atrium: overlay restack for layer " << static_cast<int>(layer)
          << " over " << ranked.count << " ranked of "
          << parent.childWindows.count << " children";
}

int AtriumBrowserWindow::OverlayChildOrderForTesting(
    views::Widget* child) const {
  // AppKit's own z-order, not the layer this file stamped on the window. A
  // test reading back the stamp would pass even if the reorder never landed.
  NSArray<NSWindow*>* front_to_back = NSApp.orderedWindows;
  const NSUInteger index = [front_to_back
      indexOfObject:child->GetNativeWindow().GetNativeNSWindow()];
  if (index == NSNotFound) {
    return -1;
  }
  return static_cast<int>(front_to_back.count - index);
}

// static
int AtriumBrowserWindow::AddTrafficLights(views::Widget* bubble) {
  NSWindow* window = bubble->GetNativeWindow().GetNativeNSWindow();
  if (!window) {
    return 0;
  }
  // No Miniaturizable or Resizable (cleared, not just left unset - a real
  // TYPE_WINDOW can pick either up from its WidgetDelegate capabilities).
  // FullSizeContentView keeps the frame equal to the content rect.
  window.styleMask = (window.styleMask | NSWindowStyleMaskTitled |
                      NSWindowStyleMaskClosable |
                      NSWindowStyleMaskFullSizeContentView) &
                     ~(NSWindowStyleMaskMiniaturizable |
                       NSWindowStyleMaskResizable);
  window.titleVisibility = NSWindowTitleHidden;
  window.titlebarAppearsTransparent = YES;
  return static_cast<int>(NSHeight(window.frame) -
                          NSHeight(window.contentLayoutRect));
}

// static
bool AtriumBrowserWindow::HasTrafficLightsForTesting(views::Widget* widget) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  NSButton* close = [window standardWindowButton:NSWindowCloseButton];
  return window && (window.styleMask & NSWindowStyleMaskTitled) &&
         (window.styleMask & NSWindowStyleMaskFullSizeContentView) &&
         !(window.styleMask & NSWindowStyleMaskResizable) &&
         window.titleVisibility == NSWindowTitleHidden && window.hasShadow &&
         close && !close.hidden && close.enabled &&
         [window standardWindowButton:NSWindowMiniaturizeButton] &&
         [window standardWindowButton:NSWindowZoomButton];
}

// static
void AtriumBrowserWindow::ClickCloseLightForTesting(views::Widget* widget) {
  NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow();
  [[window standardWindowButton:NSWindowCloseButton] performClick:nil];
}

void AtriumBrowserWindow::RearmPagePointerTracking(
    content::WebContents* contents) {
  content::RenderWidgetHostView* rwhv = contents->GetRenderWidgetHostView();
  if (!rwhv) {
    return;
  }
  BaseView* view =
      base::apple::ObjCCast<BaseView>(rwhv->GetNativeView().GetNativeNSView());
  if (![view respondsToSelector:@selector(enableTracking)]) {
    return;
  }
  [view disableTracking];
  [view enableTracking];
}

}  // namespace atrium
