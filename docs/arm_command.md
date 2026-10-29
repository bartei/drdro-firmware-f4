# `arm` command — two-phase engage

`arm <bank> [phase]` is sent over the serial protocol (in practice, from a UI button)
to pick up sync without a sudden move — a Hardinge-style two-phase engage: stop, wait
for the one moment nothing would jump, then engage.

A **bank** here is just a stored per-axis `{ratioNum, ratioDen, syncEnable}` set
(`ratioBanks[bank]`). Arming reads those values to know which scales to watch and what
ratio each one should resume at — that's all it does right now.

## What it does

1. Stop: every scale's `syncEnable` goes to 0 immediately.
2. Watch, per scale that `bank` syncs:
   - **Non-rotary** (`revSteps == 0`) — e.g. an XY axis intercepting a single powered
     axis: engage once the watched scale's position, projected through `bank`'s ratio
     for it, comes back to matching the servo's actual position (or passes through it)
     — whichever direction things move, the first coincidence is used.
   - **Rotary** (`revSteps != 0`) — e.g. lathe threading: engage once the watched
     scale's position modulo `revSteps` comes within tolerance of `phase` (default 0).
3. Engage: snap the servo's commanded position to its actual position (discarding any
   stale backlog from before arming), then resume sync at `bank`'s ratio.

Once engaged, it's ordinary synced motion — free to run either direction from there.

## `phase`

For the rotary case, `phase` selects which point of the revolution to land on — e.g.
which thread start, if there's more than one. For jobs where any point is as good as
any other (nothing to land back on), don't bother picking a `phase`: point the scale's
configured counts-per-revolution at the sub-period you actually care about (divide it
by the number of starts yourself) instead of the full mechanical revolution, and arm
with `phase` at its default of 0.
