/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * See RampsRatioBanks.h.
 */
#include "RampsRatioBanks.h"

int RatioBankActivate(rampsSharedData_t *shared, uint16_t bank) {
  if (bank >= RBANK_COUNT) return -1;

  for (int i = 0; i < SCALES_COUNT; i++) {
    const ratioBankAxis_t *a = &shared->ratioBanks[bank].axis[i];
    shared->scales[i].syncRatioNum = a->ratioNum;
    shared->scales[i].syncRatioDen = a->ratioDen;
    shared->scales[i].syncEnable   = a->syncEnable;
  }
  shared->fastData.activeRatioBank = bank;
  return 0;
}

int32_t RatioBankMatchError(int32_t triggerPos, int32_t ratioNum, int32_t ratioDen,
                             uint32_t currentSteps) {
  if (ratioDen == 0) return 0;
  int64_t projected = (int64_t) triggerPos * (int64_t) ratioNum / (int64_t) ratioDen;
  return (int32_t) (projected - (int64_t) currentSteps);
}

/* position's raw phase: position mod revSteps, corrected for negative position. */
static uint32_t rawPhase(int32_t position, uint32_t revSteps) {
  int64_t m = (int64_t)revSteps;
  int64_t r = (int64_t)position % m;
  if (r < 0) r += m;
  return (uint32_t)r;
}

uint32_t RatioBankPhaseDistance(int32_t position, uint32_t revSteps, uint32_t targetPhase) {
  if (revSteps == 0) return 0;
  uint32_t phase = rawPhase(position, revSteps);
  uint32_t target = targetPhase % revSteps;
  uint32_t diff = (phase >= target) ? (phase - target) : (target - phase);
  return (diff <= revSteps - diff) ? diff : (revSteps - diff);
}

int32_t RatioBankPhaseResidualTo(int32_t position, uint32_t revSteps, uint32_t targetPhase) {
  if (revSteps == 0) return 0;
  uint32_t phase = rawPhase(position, revSteps);
  uint32_t target = targetPhase % revSteps;
  int64_t rel64 = (int64_t)phase - (int64_t)target;
  int64_t m = (int64_t)revSteps;
  int64_t rel = rel64 % m;
  if (rel < 0) rel += m;
  uint32_t relU = (uint32_t)rel;
  return (relU <= revSteps - relU) ? (int32_t)relU : -(int32_t)(revSteps - relU);
}

int RatioBankArm(rampsSharedData_t *shared, uint16_t bank, uint32_t targetPhase) {
  if (bank >= RBANK_COUNT) return -1;
  for (int i = 0; i < SCALES_COUNT; i++) shared->scales[i].syncEnable = 0;
  shared->fastData.servoMode = 0;
  shared->fastData.armedRatioBank = (uint16_t) (bank + 1);
  for (int i = 0; i < SCALES_COUNT; i++) {
    const ratioBankAxis_t *a = &shared->ratioBanks[bank].axis[i];
    uint32_t revSteps = shared->scales[i].revSteps;
    shared->fastData.armInitialError[i] = 0;
    shared->fastData.armTargetPhase[i] = 0;
    if (a->syncEnable == 0) continue;
    if (revSteps != 0) {
      shared->fastData.armTargetPhase[i] = targetPhase % revSteps;
    } else {
      shared->fastData.armInitialError[i] = RatioBankMatchError(shared->scales[i].position,
                                                                   a->ratioNum, a->ratioDen,
                                                                   shared->servo.currentSteps);
    }
  }
  return 0;
}

/* Index of the first synced scale in the armed bank whose arm condition currently
 * holds, or -1 if none. Rotary scales (revSteps != 0) use a windowed distance-to-
 * target-phase check; non-rotary scales (revSteps == 0) use a sign-change/zero-crossing
 * check against armInitialError[i] — whichever direction you move, the first crossing
 * reached is used, not a pre-chosen point. See RatioBankArm. */
static int armInterceptedScale(const rampsSharedData_t *shared) {
  uint16_t bank = (uint16_t) (shared->fastData.armedRatioBank - 1);
  for (int i = 0; i < SCALES_COUNT; i++) {
    const ratioBankAxis_t *a = &shared->ratioBanks[bank].axis[i];
    if (a->syncEnable == 0) continue;
    uint32_t revSteps = shared->scales[i].revSteps;
    if (revSteps != 0) {
      uint32_t dist = RatioBankPhaseDistance(shared->scales[i].position, revSteps,
                                              shared->fastData.armTargetPhase[i]);
      if (dist <= shared->fastData.armPhaseTolerance) return i;
    } else {
      int32_t was = shared->fastData.armInitialError[i];
      int32_t now = RatioBankMatchError(shared->scales[i].position, a->ratioNum, a->ratioDen,
                                         shared->servo.currentSteps);
      if (now == 0 || was == 0 || (was > 0) != (now > 0)) return i;
    }
  }
  return -1;
}

/* Snap desiredSteps to currentSteps first (discards any stale backlog from before
 * arming), then, for a rotary intercept, nudge it by the sub-poll-period residual to
 * armTargetPhase so the engage lands on the exact target phase rather than wherever the
 * ~100 ms poll happened to detect it. */
static void applyArmCorrection(rampsSharedData_t *shared, int scaleIndex, uint16_t bank) {
  shared->servo.desiredSteps = shared->servo.currentSteps;
  if (scaleIndex < 0 || shared->scales[scaleIndex].revSteps == 0) return;
  const ratioBankAxis_t *a = &shared->ratioBanks[bank].axis[scaleIndex];
  if (a->syncEnable == 0 || a->ratioDen == 0) return;
  int32_t residual = RatioBankPhaseResidualTo(shared->scales[scaleIndex].position,
                                               shared->scales[scaleIndex].revSteps,
                                               shared->fastData.armTargetPhase[scaleIndex]);
  int32_t correction = (residual * a->ratioNum) / a->ratioDen;
  shared->servo.desiredSteps = (uint32_t)((int32_t)shared->servo.desiredSteps - correction);
}

void RatioBankArmUpdate(rampsSharedData_t *shared) {
  if (shared->fastData.armedRatioBank == 0) return;
  int si = armInterceptedScale(shared);
  if (si < 0) return;

  uint16_t bank = (uint16_t)(shared->fastData.armedRatioBank - 1);
  applyArmCorrection(shared, si, bank);
  RatioBankActivate(shared, bank);
  shared->fastData.servoMode = 1;
  shared->fastData.armedRatioBank = 0;
}
