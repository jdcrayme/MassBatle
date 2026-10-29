# Phase 3 Report -- Ranged Stack (Bows and Crossbows at Volley Scale)

| | |
|---|---|
| **Date / agent** | 2026-10-28/29, single autonomous run (kickoff-artifact pass + Tasks 0-8), protocol per `Docs/AGENTS.md` |
| **Engine pin** | UE 5.8.4 CL 58902052 at `E:\Unreal\UE_5.8` (reference machine `ref-i7-9700F-1660Ti`) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin `MassBattle`, test module `MassBattleTest`) |
| **Status** | **Tasks 0-8 complete; TWO blockers open** (the mixed two-run equality at mm scale; the pre-existing staged soak dress-gate failure -- both attributed, both owner-visible). The phase's core exit criterion (the T3.6 perf gate) is MET with headroom. |

## 1. What was built

**Data spine** (`Plugins/MassBattle/Source/MassBattle/`)
- `BattleRangedFragments.h` -- `FRangedStateFragment` (kickoff) on ranged-capable compositions only.
- Registry fire control (`BattleRegistry.h/.cpp`): `SetUnitFireToggles`/`GetUnitFireToggles` (A11 reject lines), `GetUnitAmmoFraction` (live roster sum over the spawn-stamped denominator), once-only `FlipUnitOutOfAmmo` (A14), `SetUnitRangedSpawnState` (A8 skirmish default + the ammo half), `ApplyUnitSkirmishDecision` (the RC7 order-serial guard: retreat displaceable orders, player-bump cancel, resume past the band; `RetreatingFireTarget` retained).
- Save v2 (`BattleSave.h`): toggles + out-of-ammo + fraction denominators + `TeamId`; legacy v1 restores to defaults.
- `TeamId` on `FUnitSpawnSpec`/`AUnitActor` (the disclosed minimal side notion; melee never reads it).

**Seams (pure math, A1)** -- `BattleBallistics` (solver, dispersion, stepper, MarchClear, `StepPool`) and `BattleRanged` (profiles, `SelectVolleyShots`, ammo fraction, `EvaluateSkirmish`).

**Pipeline** -- `UBattleProjectileSubsystem` (pool reserve at battle start), `ProjectileStepProcessor` (FIRST in the Battle group; stepping + impact resolution), `RangedFireProcessor` (after AssignPairs; the exported `RunRangedFirePass`/`RunProjectileStepPass` drive it without a Mass tick for fixtures/tiers).

**Impact resolution (RC6)** -- the transient one-roll context reusing `FMeleeMath` (sector quantizer, arc bands, HitChance, `CombatRoll01`) with the A13 canonical roll identity; mount-pool drain first, then the F2 death path (`FDeadTag` via command buffer). `Battle.Ranged.Enable` defaults 1.

**ExecBridge** -- `Battle.Ranged.Toggle/Dump/SpawnVolley [line|skirmish]/PerfStress`; `BattleMetrics` gained APPENDED fields (F14): `InFlight/StepMs/GroundHits/TtlExpired/PoolDrops/PoolCapacity/FireMs/Shots/Holds/Impacts/FriendlyHits/InFlightPeak`; `Battle.DumpMetrics` + `ResetDiagnostics` cover them.

**Debug overlay (B.2, behind `Battle.Debug.ShowOverlay`)** -- trajectory segments (amber arced / blue flat), impact rings colored by the victim's team (friendly hits red-ringed), per-unit ammo bars (green/amber/red by fraction), hold-reason circles at the aim anchor.

**Scenarios (T3.4/T3.5)** -- `Battle.Func.S6_Volley` (dispersion bound 4.44 deg vs 4.5; flat-lane hold; hill blocks flat while arced passes; team-blind stray; drain to out-of-ammo at exactly 16x25) and `Battle.Func.SkirmishBehavior` (break/resume/hysteresis/hold-fire-still-retreats/order-override) -- both PASS, PR-gate citizens.

**Perf gate (T3.6)** -- `Battle.Ranged.PerfStress`: **0.236 ms (StepMs 0.063 + FireMs 0.174) @ 1,091 in flight < 0.300 budget, PoolDrops=0, Verdict=PASS** (`Tools/CI/t36_perf.log`) -- the phase's "1,000 arrows < 0.3 ms" exit criterion MET.

**Soak + baselines** -- `Battle.Ranged.SoakVolley=1` variant (25% archer mix, teams, FINAL-line APPENDED counters; flag off = byte-identical); `entity_spawn_80_us` re-baselined to 47.4 us with the ranged fragment present (disclosed; the old 4.8 predated the fragment).

**Tests** -- unit tier **79/79** (nine `Battle.Unit.Ranged.*` suites + schema-v2 SaveRoundTrip + HashTerms extensions); `Battle.Unit.Servo.PerStateAvoidanceWeights` moved to the canonical-key semantics.

## 2. Test evidence (verbatim key lines)

- Unit tier: `passed=79 failed=0 total=79 completed=True exit=0 ok=True` (`Tools/CI/last_run.json`).
- T3.6: `EXEC_RESULT RangedPerf InFlightNow=549 InFlightPeak=1091 StepMs=0.063 FireMs=0.174 RangedMs=0.236 BudgetMs=0.300 PoolDrops=0 Verdict=PASS`.
- S6: `EXEC_RESULT S6_Volley PASS dispersion=4.44deg impacts=44 friendlyHits=1 fired=400`.
- Skirmish: `EXEC_RESULT SkirmishBehavior PASS break/resume/hysteresis/hold-fire/override all held; final dist=5.5m`.
- Kill chain: `EXEC_RESULT RangedKill Shooter=1:1 Unit=2:1 Victim=55:1 Sector=0 Friendly=0` / `... Friendly=1` (the team-blind half).
- Skirmish machine: `SkirmishRetreat ... Serial=2 FireTarget=2:1` / `SkirmishResume` / `SkirmishCancel ... PlayerOrderSerial=4`.
- M4 headless: `SpawnVolley OK Mode=skirmish` + live `SkirmishRetreat FireTarget=2:1` (`Tools/CI/m4_boot2.log`).
- Func tier final: **passed=19 failed=4** -- the two pre-existing phase-2 melts (S4_ShieldWall/S5_Mob) + the two task-7-disclosed tips (DeterminismTwoRun, S4_WidthOverride).

## 3. Facts corrections & deviations

- **`TeamId` added** (spec A7/A8 presume sides the data model never defined; contact-range records cannot drive the 25 m skirmish trigger) -- save v2 carries it; melee never reads it (A12 preserved).
- **RC7 completed**: the retreat retains the attack aim (`RetreatingFireTarget`); the resume keys on past-the-band (an out-of-ammo skirmisher still stops retreating); the retreat displaces attack approaches.
- **A11 gate added**: `bFireAtWill` gates the fire pass (the HoldFire phase exposed the missing toggle).
- **Own-ranks rule (RC2) applied twice**: flat-lane blockers and the impact pick exclude the shooter's own unit (the solved arcs clear own ranks; the first impact run had 20 own-unit hits).
- **Corpses never roll** (the melee rule) -- a killed-ledger guards double kills while FDeadTags are flush-invisible.
- **A13 canonical identity** (`BattleCanonicalEntityKey/Count`, BattleTypes.h): every hash-family draw (staggers, seeds, rolls) now keys on (unit, roster slot) -- entity slots permute across resets (five defects fixed; one mm-scale residual OPEN).
- **The phase-2 T2.8 gate was vacuous** (hashed an empty world) -- the extension makes it real.
- **Engine notes**: query-less processors must override `ShouldAllowQueryBasedPruning -> false` (MassProcessor.cpp:274); the spawn payload never stamped `FBaseStatsFragment.{Lethality, Shield}` (defaults ride; T6.1 owns completion).
- **Pool columns** `ShooterRollCount`/`Lethality` added at fire (disclosed layout growth).
- **The A14 GAS AmmoPool mirror** is written by the fire pass (interim; UnitSync owns the sync path at Phase 4+).

## 4. Where Phase 4 should start

1. Read order: `Docs/README.md` -> `Docs/AGENTS.md` -> this report -> the phase-3 progress log (the discrepancy entries carry the why).
2. **The two open blockers first** (both dossier'd): (a) the mixed two-run mm-scale divergence -- next step is hex-float tracing of the servo's DesiredVel accumulation at ticks 11-14 for one unit's entities 8/11 (`Tools/CI/det_trace8.log` has the exact trace shape); (b) the staged soak dress-gate failure (30 m vs the 6 m bound with phase-2 flags on) -- PRE-EXISTING since 2026-10-26, phase-2 dossier territory, blocks the staged nightly and therefore the soak-variant staged evidence.
3. **Morale effects of casualties** (the phase-4 priority): the death path is proven observable (RangedKill/CombatKill -> DeathsByArc); GAS hooks live on the unit actors.
4. Ready hooks: the fire pass's target fallback chain (order -> retained aim -> record enemy) is where doctrine-level target selection lands; the pool's `ShooterRollCount`/`Lethality` columns are where combat modifiers stamp; `Battle.Ranged.*` CVARs are all owner-tunable (A6 placeholders, T6.1 audit owns the constants).
5. Re-baseline reminder: `entity_spawn_80_us` = 47.4 us (updated); the perf gate's measured line is in the progress log (the T2.9 pattern -- record, don't gate).
6. **Owner actions pending**: ADR-27..31 applications + the ADR-28 architecture-§4-row amendment (the proposals live in `Docs/spec/ranged-stack.md` §8); the S4_WidthOverride gate re-review (the canonical summation order moved it within the noise band -- tighten the bound or accept); the S4_ShieldWall/S5_Mob melt decision; the D3 fold-in checklist.

## 5. Open blockers

1. **DeterminismTwoRun (mixed fixture) divergence** -- mm-scale, one unit's entities 8/11, tick 14 onset; five fixes landed (each independently real); the residual seed is below the 1e-6 m trace resolution. The gate ships RED; the melee-only empty-world runs stay green as before.
2. **Staged movement-soak dress gate** -- `dressErr=30.25/6.0m ok=0` with task-7 changes, `33.76` without (attribution run); PRE-EXISTING since the 2026-10-26 staged nightly. Blocks the nightly's baseline-commit step and the staged soak-variant evidence.
3. Carried: S4_ShieldWall/S5_Mob melts (phase-2 dossier); the D3 movement fold-in checklist; ADR-22..26 + 27..31 owner applications.

## 6. DoD checklist (brief §7, with evidence pointers)

- [x] T3.1-T3.3 green with evidence (unit tier; ratification at `3087cde`, zero drift)
- [x] T3.4/T3.5 green with evidence (func tier; `937a2ba`)
- [x] T3.6 green with the measured line (0.236 ms @ 1,091 in flight; `t36_perf.log`)
- [ ] T3.6's nightly tier -- BLOCKED by blocker 2 (the perf gate itself is green; only the staged soak step fails, pre-existing)
- [x] RC1-RC5, RC7-RC9 demonstrable (the progress log's per-task evidence)
- [ ] RC6's two-run mixed equality -- BLOCKER 1 (the deterministic roll/seed/path machinery itself is proven; the transient is movement-side)
- [x] B.8 items 1-6 (unit tests over both seams; S6+Skirmish in the PR gate; the B.2 overlay set; the B.4 metric set; the B.7 entry appended; the CVAR block with flags-off identity)
- [x] B.8 item 7 (Iris-ready: pool + fragments plain data; rolls seed-derived; no raw pointers across the registry)
- [x] Archetype count stable (two-row world=4; growth itemized; the 8-count scenario/soak assertion deferred with blocker 2's soak)
- [x] State-hash stability (HashTerms mutation + revert; the two-run equality is blocker 1)
- [x] Zero per-tick heap allocations in fire/step steady state (file-static scratch; the pool preallocates)
- [x] Save v2 round-trip green (toggles + denominators + TeamId; legacy v1 accepted)
- [x] EDU golden re-sign ratified on the machine (task 0; zero drift since)
- [x] M4 demo gray-box running (`m4_boot2.log`; PR-8's visual pass remains)
- [x] `entity_spawn_80_us` re-baselined (47.4 us)
- [x] Gate-first verified via git log (T3.1/T3.2 at `3087cde`; T3.3 family kickoff `c3365f2`; T3.4/T3.5 at `937a2ba`; T3.6 at `31d9820` -- each landed with/before its owning task)
- [x] ADR-27..31 + the arch §4 row amendment handed to the owner (spec §8; this report section 4.6)
- [x] Progress log complete; README map + status updated; PR-6..PR-9 statuses: 6/7/9 executed on the machine, 8's gray-box done (the visual pass remains editor-scoped)
