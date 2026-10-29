/**
 * Copyright © 2026 <Stefano Bertelli>
 * GPL-3.0-or-later. See LICENSE.
 *
 * Ratio banks + the `arm` command. Deliberately kept out of Ramps.c (the hot ISR file)
 * so it doesn't collide with SynchroRefreshTimerIsr edits. Pure functions over
 * rampsSharedData_t — no HAL — so they're native-testable like Protocol.c's registry.
 */
#ifndef RAMPS_RATIO_BANKS_H
#define RAMPS_RATIO_BANKS_H

#include "Ramps.h"

/** Copy ratioBanks[bank].axis[i] into the live scales[i].syncRatioNum/Den/syncEnable
 *  fields (the ISR-read effective copy) for every i, and record fastData.activeRatioBank
 *  = bank. Returns 0 on success, -1 if bank >= RBANK_COUNT (shared left unmodified). */
int RatioBankActivate(rampsSharedData_t *shared, uint16_t bank);

/** Signed `project(triggerPos) - currentSteps`, where project(x) = x * ratioNum/ratioDen
 *  (truncating integer division). 0 if ratioDen == 0. The `arm` command's intercept
 *  condition for a non-rotary watched scale: engage once this reaches exactly zero or
 *  changes sign from its value at arm time — see RatioBankArm. */
int32_t RatioBankMatchError(int32_t triggerPos, int32_t ratioNum, int32_t ratioDen,
                             uint32_t currentSteps);

/** Shortest circular distance from `position`'s raw phase (position mod revSteps) to
 *  `targetPhase` (itself folded into [0,revSteps) first), in the same units as revSteps.
 *  0 if revSteps == 0. The `arm` command's intercept condition for a rotary watched
 *  scale. */
uint32_t RatioBankPhaseDistance(int32_t position, uint32_t revSteps, uint32_t targetPhase);

/** Signed, shortest-path distance from `position`'s raw phase to `targetPhase` (same
 *  phase convention as RatioBankPhaseDistance), in raw position counts. Positive =
 *  position is past the target; negative = short of it. 0 if revSteps == 0. Used to
 *  correct the `arm` command's engage registration for a rotary watched scale, so the
 *  registration doesn't depend on which ~100 ms poll tick detected the intercept. */
int32_t RatioBankPhaseResidualTo(int32_t position, uint32_t revSteps, uint32_t targetPhase);

/** `arm <bank> [phase]` — a Hardinge-style two-phase engage, picked up with a serial
 *  command (a UI button, in practice): arm `bank` directly from wherever the servo and
 *  every scale happen to be right now, stop (zero every scale's syncEnable), and wait
 *  for the one moment engaging would produce no sudden movement — then engage, free to
 *  run either direction from there.
 *
 *  `bank` here is just a stored per-axis {ratioNum, ratioDen, syncEnable} set
 *  (ratioBanks[bank] — see RatioBankActivate); arming reads those values to know which
 *  scales to watch and what ratio each one should resume at, nothing more.
 *
 *  Two use cases:
 *  - XY intercept, one powered axis: a non-rotary (revSteps == 0) watched scale.
 *    Snapshots RatioBankMatchError() at arm time; RatioBankArmUpdate() engages once that
 *    error returns to exactly zero or changes sign — whichever direction things move,
 *    the first coincidence reached is used, so no specific amount of travel is required.
 *  - Lathe threading: a rotary (revSteps != 0) watched scale. `phase` (default 0) selects
 *    which of the spindle's rotations to land on — i.e. which thread start — by picking
 *    the target value of `position mod revSteps`; engage happens once
 *    RatioBankPhaseDistance() comes within fastData.armPhaseTolerance of it. For jobs
 *    where any groove is as good as any other (no specific start to land back on), skip
 *    picking a `phase` at all: point revSteps at the sub-period instead of the full
 *    mechanical revolution (revSteps / number of starts) and arm with phase 0.
 *
 *  Either way, the engage snaps desiredSteps to currentSteps first (discarding any stale
 *  backlog from before arming), then, for the rotary case, nudges it by the sub-poll-
 *  period phase residual so the registration lands on the exact target phase rather than
 *  wherever the ~100 ms poll happened to detect it. Returns 0 on success, -1 if
 *  bank >= RBANK_COUNT (shared left unmodified). */
int RatioBankArm(rampsSharedData_t *shared, uint16_t bank, uint32_t targetPhase);

/** Call at servoEnableTask rate (~10 Hz: its osDelay(100) against the 1 kHz RTOS tick),
 *  never from the step ISR. If a bank is currently armed (fastData.armedRatioBank != 0)
 *  and its intercept condition now holds, applies the registration correction and
 *  engages it (RatioBankActivate() + servoMode = 1 + clears the armed state). No-op
 *  otherwise. See RatioBankArm. */
void RatioBankArmUpdate(rampsSharedData_t *shared);

#endif /* RAMPS_RATIO_BANKS_H */
