// Copyright 2026 The Atrium Authors
//
// Maps the Notification timeout row's five steps to toast-stack.js's
// dismissAfterMs seam.

export const NOTIFICATION_TIMEOUT_STEPS =
    ['oneSecond', 'threeSeconds', 'fiveSeconds', 'tenSeconds', 'keep'];

const TIMEOUT_MS = {
  oneSecond: 1000,
  threeSeconds: 3000,
  fiveSeconds: 5000,
  tenSeconds: 10000,
};

// 'off' and 'thirtySeconds' are retired. A value saved under either one maps
// to the nearest step left, so a profile saved before this change still
// resolves everywhere a step is read, not only in the settings row.
const RETIRED_STEP_MIGRATIONS = {
  off: 'oneSecond',
  thirtySeconds: 'tenSeconds',
};

export function migrateNotificationTimeoutStep(step) {
  return RETIRED_STEP_MIGRATIONS[step] || step;
}

// 'off' used to skip the toast entirely. It is retired, so every step -
// migrated or current - now shows one.
export function shouldShowToast(step) {
  return migrateNotificationTimeoutStep(step) !== 'off';
}

// showToast's dismissAfterMs argument for a given step. undefined for 'keep'
// and for any step this build does not recognise - showToast's own
// no-auto-dismiss default, the safe fallback for an unknown future value.
export function dismissAfterMsForTimeout(step) {
  return TIMEOUT_MS[migrateNotificationTimeoutStep(step)];
}
