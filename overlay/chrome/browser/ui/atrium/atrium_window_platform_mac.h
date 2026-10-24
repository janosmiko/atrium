// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_platform_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_PLATFORM_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_PLATFORM_MAC_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "ui/gfx/native_ui_types.h"

namespace input {
struct NativeWebKeyboardEvent;
}

namespace views {
class Widget;
}

namespace atrium {

// the pure-virtual interface this implemented had no other
// subclass and no test fake, and was deleted. A second port re-introduces it.
class AtriumWindowPlatform {
 public:
  // Out of line: pending_hide_ makes the class complex for chromium-style.
  explicit AtriumWindowPlatform(views::Widget* widget);

  ~AtriumWindowPlatform();

  // A non-zero `duration` leaves the controls on screen for the caller to
  // travel with the pane. The hide waits for that travel to land,
  // so `confirm_hide` re-asks at the end rather than answering now.
  void SetWindowControlsVisible(bool visible,
                                base::TimeDelta duration,
                                base::OnceCallback<bool()> confirm_hide);

  // Puts the controls `dx` points sideways, this frame. Driven from the
  // sidebar's own stepper: a CABasicAnimation runs on the render server's
  // clock and was 63pt out by the pane's first moved frame.
  void StepWindowControlsSlide(double dx);

  // Ends the travel. A control at the far end still takes clicks, so a hide
  // asked for at the start only happens here, and re-asks first.
  void SettleWindowControlsSlide();
  // True while the controls are in the hit-testing tree, so a fade-out that
  // has not finished still reads as visible.
  bool AreWindowControlsVisible() const;

  // Test-only. `diameter` and `pitch` are measured from what the buttons
  // draw, not from their frames: the frame is the hit target and runs wider
  // than the light inside it.
  struct ControlProbe {
    bool exists = false;
    bool sliding = false;
    double slide_dx = 0;
    // Where a light actually drew, as against slide_dx's model value. The two
    // part company the moment anything animates the transform behind our back,
    // which is the whole defect.
    bool has_presentation = false;
    double presentation_dx = 0;
    // Both stay at 1 for every frame the lights are on screen. Either one
    // dipping means something is fading them, which AppKit answers by
    // redrawing the button in its inactive grey.
    double layer_opacity = 1;
    double model_alpha = 1;
    // A fade animation under our own key, so a reinstated fade is visible to
    // a test even on a frame that samples between its steps.
    bool layer_fading = false;
    double diameter = 0;
    double pitch = 0;
    // What a click at the lights' settled corner would land on. A CALayer
    // transform moves the drawn pixels only, so this stays true for the whole
    // travel while the buttons are drawn somewhere else.
    bool settled_corner_hits_a_light = false;
    // And whether that light would act on it. hitTest: answers for a disabled
    // view too, so the geometric hit above is not the harm on its own - an
    // NSControl declines the click itself.
    bool settled_corner_takes_a_click = false;
    // How far the button's own frame has travelled, against slide_dx's layer
    // transform. A click follows this one.
    double frame_dx = 0;
  };
  ControlProbe ProbeWindowControlsForTesting() const;

  // The yellow button as a command. The reveal panel is a child window over
  // the parent's frame view, so while the sidebar floats the real buttons are
  // behind it and the page draws its own. Close and full screen go
  // through the window's own paths, which need no platform hook.
  void MinimizeWindow();

  // Puts a frosted backdrop behind the window's compositor surface and makes
  // the window itself non-opaque so the backdrop shows through wherever that
  // surface has alpha. Called once, after the widget is initialised. The tint
  // painted over it stays the window's own business.
  void InstallBackdrop();

  // Clips the window's own layers to `radius`. A non-opaque window is not
  // corner-masked for us, so without this the backdrop shows square corners
  // inside the frame's rounded shadow.
  void UpdateCornerMask(float radius);

  // True while the window fills the screen. There is no rounded frame to
  // follow then, and a radius would round the page against black instead.
  bool IsWindowFullScreen() const;

  // Fades `view` to `alpha`, 0 to 1, over `duration`. A zero duration sets it
  // at once and cancels a fade already in flight.
  //
  // Web content composites above its siblings, so an in-hierarchy overlay can
  // never paint over it and the fade has to reach the native view itself.
  void FadeNativeView(gfx::NativeView view,
                      double alpha,
                      base::TimeDelta duration);

  // True when `event` is a key going down, carried by a real OS event.
  // Shortcuts must not fire on key up, and an event with no OS event behind
  // it cannot be forwarded.
  bool IsKeyDownEvent(const input::NativeWebKeyboardEvent& event) const;

  // Hands `event` back to `window` so the OS gets its turn at a chord the
  // browser chose not to take. `window` is not always this platform's own
  // window: a slot can be presented by a different one.
  bool RedispatchKeyEvent(gfx::NativeWindow window,
                          const input::NativeWebKeyboardEvent& event);

  // Whether the widget has a native window behind it yet. Reports rather than
  // aborting: a window torn down mid-animation is a real state, not a bug.
  bool HasRealizedWindow() const;

  // Takes the window out of the OS window-cycling order, or puts it back.
  // Atrium hides a window rather than closing it, and a hidden window must
  // not answer the cycle chord.
  void SetExcludedFromWindowCycling(bool excluded);

 private:
  const raw_ptr<views::Widget> widget_;

  // The hide SettleWindowControlsSlide owes, non-null only while one is in
  // flight. A newer call overwrites it, so a collapse the user reversed
  // mid-slide cannot still take the buttons away.
  // Puts each light's enabled flag back the way the slide found it.
  void RestoreWindowControlsHitPath();

  base::OnceCallback<bool()> pending_hide_;

  // How far the current slide has moved the buttons' own frames. The settled
  // corner a click should miss is the live frame less this.
  double slide_frame_dx_ = 0;

  // Each light's enabled flag from before the slide took it out of the hit
  // path. AppKit disables a button in its own right, so a blanket re-enable at
  // the end would hand back one it had taken away. Empty between slides.
  std::vector<bool> lights_were_enabled_;
};

// SkyLight exports no getter for the window-server radius, so the
// push records itself. Written only after the transaction commits, so every
// early return in SetServerCornerRadius leaves this untouched.
struct ServerCornerRadiusPush {
  int count = 0;
  double radius = -1;
  // macOS 26 reads this one and AppKit skips it for a translucent window.
  bool system_setter_ran = false;
};

ServerCornerRadiusPush LastServerCornerRadiusPush();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_PLATFORM_MAC_H_
