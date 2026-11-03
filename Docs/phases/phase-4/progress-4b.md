# Phase 4b -- Command & Fear: Progress Log

| | |
|---|---|
| **Session** | 2026-11-03 kickoff, autonomous run (the frozen brief: `brief-command-and-fear.md`, B-1..B-8 owner-approved; protocol per `Docs/AGENTS.md`) |
| **Engine** | UE 5.8.4 at `E:\Unreal\UE_5.8` |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin `MassBattle`, test module `MassBattleTest`) |
| **Start state** | `main` @ `811b6f7` (the freeze commit); the 4a report filed, the nightly green, unit 85/85, func 26/27 = only S5_Mob |

## Task log (one line per completed step, AGENTS §3)

- [task0.1] The §1 digest re-verified at the freeze (F1 exact, all six
  OrderLatency anchors unchanged; F5's fold re-cited 610-613; F6/F7
  verified; F11 rewritten from the 4a close) -- recorded in the frozen
  brief's §1 and §9. -> the freeze commit `811b6f7`.
- [task0.2] Tiers run at kickoff: **unit 85/85** (`[summarize] tier=unit
  passed=85 failed=0 ... ok=True`), **func 26/27 = exactly S5_Mob**
  (`[summarize] tier=func passed=26 failed=1 ... FAILED: S5_Mob`) --
  F11's expectations met verbatim. Rerun at the run HEAD after the freeze
  (the tiers above are the kickoff HEAD's).

- [task1.1] The queue landed: FPendingOrder on the runtime state (B-3/B-4),
  QueuePlayerOrder (the six exec commands rewired; the direct Issue*
  surface stays the auto-order path), the release processor (the nominal-
  query pattern; the absolute SimTickCount), the queue state hashed into
  the bookkeeping. -> unit 86/86 (Battle.Unit.Order.Queue: the identity,
  the boundary release, serial-at-issue, last-order-wins, the auto-order
  interplay, the reset-ordering immunity); the two-run green with
  latency=2 live. -> commit `4692672`.
- [task2.1 + task3.1] TerrorAura + the climate slice (ONE commit, the EDU
  token-parse coupling disclosed): the frighten/hardy/very_hardy tokens +
  the inert M2TW set; the terror weight/fold/cvars/overlay/metric;
  ClimateFatigueRateScale wired to the exertion loop (the F7 slot). Two
  crash teachers fixed in-place (the empty-rows bounds assert; the
  heredoc-authored literals). -> unit 88/88 (Terror + RateScale green;
  the three fixture regressions cleared); func 26/27; the two-run PASS
  with the climate live. -> commit `ad9afa0`.
- [task4.1] The two-run extension: the fixture's attack orders through the
  queue + the T4.7 general mounted-frightening (AUnitActor.bFrightenMounted,
  the bIsGeneral pattern); the harness -TerrorMountedBase param; the T4b
  roadmap rows + the spec section-10 LANDED marks. -> the extended two-run
  with -OrderLatencyTicks=2 -TerrorMountedBase=2: PASS 40/40 mismatches=0
  routs=8 rallies=2 (the queued releases + the terror terms + the climate
  rates inside the equality); the T4.8 staleness re-asserted (the morale
  cadence identical, the queue adds no value-age). -> commit `fb373d0`.
- [task5.1] The nightly evidence: the FIRST staged nightly hit the melt
  tail (the watch item above); the SECOND GREEN (`[nightly] DONE`, the
  baseline promoted `d61dad1`). -> the DoD's nightly line met.
- [task5.2] The report per AGENTS section 5 filed (report-4b.md) + the
  README rows.

## Discrepancies (dated, newest first)

- **2026-11-03 (the staged-nightly melt tail -- a WATCH ITEM recorded, not a
  4b regression)** -- the FIRST 4b staged nightly failed at the soak:
  `settled stuck=31 separation=18 settledDressErr=24.75` -- ONE unit (6)
  melted (29 entities at ladder level 4, 9-25 m off slots, never re-formed
  in the 30 s settle; the Assert-phase `Straggler` line named them). The
  editor soak did NOT reproduce (green, stuck=0, settledDressErr=0.00);
  the queue is inert in the soak (zero OrderQueued lines; the soak's
  cadence orders ride the direct auto path); the SECOND staged nightly
  passed GREEN (stuck=0, maxDeepStuck 130/144, the baseline promoted
  `d61dad1`). Disposition: a rare statistical tail of the staged-timing
  regime (untethered frames: escalations 3397-4364 staged vs 395-679
  editor -- the shove/ladder churn ~8x harsher), consistent with the
  whole-unit-tear class the phase-2 machinery owns; NOT 4b-attributable
  (the queue never engaged; the terror/climate are morale-pass-side and
  the soak runs morale-off). Watch item for the movement soak's owner
  review: if the tail recurs, the next levers are the incumbency cvar
  bisect (`Battle.Movement.AssignIncumbencyM 0` on the staged path via the
  harness param family) + the level-4 handoff consumer (the Phase-2 ready
  hook).

- **2026-11-03 (the multi-command ExecCmds lesson re-confirmed)** -- the
  first latency boot HUNG on the two-command `-ExecCmds` chain (the
  run_tests.bat funcon comment carried the fact); the harness-param family
  (`-OrderLatencyTicks=N`, `-TerrorMountedBase=N`) is the standing pattern
  (the -Phase2Flags precedent).
