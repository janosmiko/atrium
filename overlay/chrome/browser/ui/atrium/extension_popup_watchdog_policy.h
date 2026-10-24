// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/extension_popup_watchdog_policy.h
#ifndef CHROME_BROWSER_UI_ATRIUM_EXTENSION_POPUP_WATCHDOG_POLICY_H_
#define CHROME_BROWSER_UI_ATRIUM_EXTENSION_POPUP_WATCHDOG_POLICY_H_

namespace atrium {

// pure decision logic for ExtensionPopupHost's hang-detection
// watchdog (design spec section 9 - a popup that never paints must not block
// the window forever). Browser-free and views-free, same posture as
// SlotPresentationTable, so it is a fast standalone gtest unit; the actual
// base::OneShotTimer and views::Widget teardown stay in ExtensionPopupHost -
// this class only decides what a given signal means.
//
// State machine: Arm() (popup opened, timer started) -> either OnShown()
// (ShowPopup's callback reported success - caller stops the timer) or
// OnTimerFired() (the timer won the race - caller reclaims via CloseNow) ->
// back to idle. OnShowFailed() and Reset() both return to idle directly (the
// popup closed itself, or the host is closing it) - no signal fires twice.
class ExtensionPopupWatchdogPolicy {
 public:
  ExtensionPopupWatchdogPolicy();
  ~ExtensionPopupWatchdogPolicy();

  ExtensionPopupWatchdogPolicy(const ExtensionPopupWatchdogPolicy&) = delete;
  ExtensionPopupWatchdogPolicy& operator=(const ExtensionPopupWatchdogPolicy&) =
      delete;

  // Call when a popup is opened and the watchdog timer is (re)started.
  void Arm();

  // Call when ShowPopup's callback reports the popup was successfully shown
  // (non-null ExtensionHost). Returns true if the watchdog was armed - the
  // caller should stop the timer - false if this is a stale/redundant signal.
  bool OnShown();

  // Call when ShowPopup's callback reports the popup failed to show (closed
  // itself before it could paint). Disarms the watchdog - there is nothing
  // left to reclaim.
  void OnShowFailed();

  // Call when the watchdog timer fires. Returns true if this is a genuine
  // hang (still armed, never shown) that the caller should reclaim via
  // CloseNow - false if the timer fired after the popup was already shown or
  // disarmed (shouldn't happen if the caller stops the timer promptly on
  // OnShown/OnShowFailed, but defensive against a race).
  bool OnTimerFired();

  // Returns to the idle state unconditionally - call whenever the host
  // itself closes the popup (Close/CloseNow), regardless of the current
  // phase.
  void Reset();

  bool is_armed() const { return state_ == State::kArmed; }

 private:
  enum class State { kIdle, kArmed, kShown };

  State state_ = State::kIdle;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_EXTENSION_POPUP_WATCHDOG_POLICY_H_
