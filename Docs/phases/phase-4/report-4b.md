# Phase 4b Report -- Command & Fear (OrderLatency + TerrorAura + the climate slice)

| | |
|---|---|
| **Date / agent** | 2026-11-03, single autonomous run (the brief frozen at kickoff against the 4a report; B-1..B-8 owner-approved). Protocol per `Docs/AGENTS.md`. |
| **Engine pin** | UE 5.8.4 at `E:\Unreal\UE_5.8` (reference machine `ref-i7-9700F-1660Ti`, ADR-13) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin `MassBattle`, test module `MassBattleTest`) |
| **Status** | **Tasks 0-5 complete.** Tiers at close: **unit 88/88** (+3: Order.Queue, Morale.Terror, Morale.RateScale); **func 26/27** (only S5_Mob, the carried phase-2 hunt); **DeterminismTwoRun PASS 40/40 mismatches=0 with the 4b inputs live** (the queued releases + the terror drain + the climate rates, `-OrderLatencyTicks=2 -TerrorMountedBase=2`); the staged nightly green. |

## 1. What was built

**The OrderLatency queue** (Task 1; B-1..B-4)
- `FUnitRuntimeState::FPendingOrder` -- one pending player order (B-4's last-order-wins), transient (B-3: cleared with the battle, absent from the save; no schema v4).
- `UUnitRegistrySubsystem::QueuePlayerOrder` -- the ONLY player surface; the six exec order commands rewired; the direct `Issue*` functions remain the auto-order surface (B-2: reflexes never delay). Latency 0 (`Battle.Order.LatencyTicks`) = the byte-identical direct apply.
- Serial-at-issue (B-4): the RC7 guard sees the player intent immediately; the release applies through the EXISTING issue functions (no second apply; a stale target drops the queued order).
- `UOrderLatencyReleaseProcessor` -- the tick-boundary sweep (sorted-handle order, B-4), the nominal-query F4-6 pattern; advances the registry's absolute `SimTickCount` (F10: no accumulator -- the reset-ordering immunity is structural).
- The queue state is HASHED (bookkeeping: validity + release tick + type + the cm-quantized anchor + target) -- live sim state inside the equality.
- The harness param `-OrderLatencyTicks=N` (the -Phase2Flags lesson: the -game process cannot take multi-command `-ExecCmds` chains -- measured, the first latency boot hung).

**TerrorAura** (Task 2; B-5/B-6)
- `FMoraleWeights::TerrorAura = -1` -- the GeneralAura mirror (drain-only) at the same falloff shape.
- The fold: the nearest different-team frightener envelope in the cluster-cadence pass; the per-kind shaping cvars `Battle.Morale.TerrorAura{Foot,Mounted}{Base,RadiusM}` (Base 0 = the identity; mounted outranges foot at the defaults).
- The EDU `attributes` tokens: `frighten_foot`/`frighten_mounted` -> `FBattleRowStats`/`FUnitSpawnSpec` -> the registry spec; the fixture's other M2TW tokens (`sea_faring`, `hide_forest`, `can_withdraw`, `can_skirmish`, `spear_wall`) accepted and INERT (the spec §10 extension points own them); unknown anything else fails loudly.
- The overlay's red terror ring at frighteners; the `TerrorActive` metric (the AuraActive mirror).

**The climate slice** (Task 3; B-7)
- `ClimateFatigueRateScale` (hardy 0.85 / very_hardy 0.7 / else 1) in the seam; the exertion loop reads it from the registry spec -- **the F7 slot is WIRED**. The band/multiplier table untouched (R-C5). `stat_heat` + the ground/heightfield lookup stay Phase 5 (the spec §10 row records the slot decision).

**The determinism extension** (Task 4; B-8)
- The two-run fixture: the four attack orders route through the queue (the queued-state hashing + the boundary releases inside the equality); the T4.7 general is MOUNTED-FRIGHTENING (`AUnitActor::bFrightenMounted`, the bIsGeneral passthrough pattern) so the terror drain runs live against the enemy side.
- The harness param `-TerrorMountedBase=N`.
- The roadmap T4 table gains **T4b.1-T4b.4**; the spec §10 rows mark TerrorAura + the climate EDU half LANDED.

**Docs**: this report; `progress-4b.md`; the README rows.

## 2. Test evidence (verbatim, condensed)

- Unit: `[summarize] tier=unit passed=88 failed=0 total=88 completed=True exit=0 ok=True`
- Func: `[summarize] tier=func passed=26 failed=1 total=27` -- exactly `S5_Mob (Fail)` (the carried hunt).
- The queue suite: `EXEC_RESULT OrderQueued Unit=1:1 type=1 releaseTick=3 serial=2` -> `EXEC_RESULT OrderRelease Unit=1:1 type=1 applied=1 tick=3` (the exact boundary; the suite also covers the identity, serial-at-issue, last-order-wins, the auto-order interplay, and the reset-ordering immunity).
- The extended two-run (`-OrderLatencyTicks=2 -TerrorMountedBase=2`): `EXEC_RESULT DeterminismTwoRun Evidence samples=40/40 mismatches=0 firstDivergence=-1 routs=8 rallies=2 moraleTicks=230` / `PASS samples=40 firstDivergence=-1` -- **the queued releases, the terror terms, and the climate rates are inside the equality**; 20 OrderQueued/OrderRelease events; 84 kills with the drain live; the morale cadence identical across the runs (the T4.8 staleness re-assertion at the gate level: the queue adds no value-age).
- The climate's R-C5 check: func 26/27 unchanged with the hardy rows' 0.85x rates live (S7/EffectEndToEnd margins absorbed it; no re-golden owed).
- The staged nightly: `[nightly] DONE` (the 4b code through the full pipeline; the FINAL line's counters unchanged in shape -- the F14 rule held).

## 3. Facts corrections & deviations

1. **The multi-command `-ExecCmds` lesson re-confirmed at 4b cost** (the first latency boot hung; the run_tests.bat funcon comment already carried it) -- the harness-param family is the standing pattern; the playbook's §8 ledger owns the general fact.
2. **The loud-fail posture needs the known-inert set enumerated** -- the first attributes allowlist rejected the fixture's own M2TW tokens (three unit suites failed on `unknown attribute 'sea_faring'`); fixed by accepting the inert set with the extension-point pointer.
3. **A failed EDU parse leaves the rows array EMPTY** -- the token-parse golden's first landing bounds-asserted (`Array.h 1339`) on the unguarded `ParsedRows[0]`; the suite now guards + ships a complete 16-key row template (the crash teacher recorded in the test's comment).
4. **The terror drain shifts the two-run fixture's dynamics** (routs 10 -> 8, kills up) -- SYMMETRICALLY, which is what the equality requires; the Evidence line's routs/rallies are the fixture's own baseline, not constants.

## 4. Where Phase 5 should start

- **Representation** (the roadmap's Phase 5 head): the representation work picks up the registry's plain-data posture this phase hardened (the queue is plain data, the mirrors are plain ints).
- **The weather/terrain systems + `stat_heat`**: the climate slot's EDU half is landed; `stat_heat` joins the same `ClimateFatigueRateScale` seam (one call site) and the ground/heightfield lookup rides the cluster-cadence fold (the §10 row).
- **The demo-facing fear pass** (owner-scoped): the terror drain is live via cvars; the demo's beat arc may want a frightener unit authored on the map (the D.12 beat-5 wavering-friendly owner call may interact).
- **Carried**: S5_Mob (its own hunt, phase-2 territory); the owner batch (ADR applications, the PIE pass, keybinds, per-map assets, the screenshots).

## 5. Open blockers

**None for 4b's scope.**

## 6. DoD checklist (brief section 7, with evidence)

- [x] T4b.1-T4b.4 green with evidence -- the suites above; the two-run PASS 40/40 with the 4b inputs live; tiers green except S5_Mob (F11)
- [x] Archetype count unchanged -- the census list untouched (F9); the soak's bounded rule unaffected
- [x] Flags-off identity byte-identical -- latency 0 = the direct apply (the suite's identity case); the terror Base 0 = the zero term; the RateScale's no-token = 1.0
- [x] `CensusGuard` semantics untouched; `Det2RunCensus`/`Det2RunSec` still green (the two-run evidence)
- [x] The reset-ordering assertion present for the queue (the suite's immunity case; F10 structural: absolute release ticks + the counter zeroed with the battle)
- [x] B.8 items 1-7: the unit suites (1); the two-run extension (2); the overlay rings (3); TerrorActive + the queue's hashed state (4); the B.7 entry rides the 4b amendment + the cadence watch stands (5); the cvar flags with identity (6); the queue is plain data, rolls seed-derived (7)
- [x] The roadmap T4b rows + the spec §10 rows landed (ground rule 5), disclosed
- [x] `Docs/README.md` status + map updated in the introducing commits
- [x] progress-4b.md complete; this report per AGENTS §5; the ADR proposal for the queue posture (serial-at-issue, transient, auto-orders never delay) is embodied in the brief's approved B-2/B-3/B-4 rows -- the formal ADR-38 application remains the owner batch
