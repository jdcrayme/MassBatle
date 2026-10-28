# MassBattle Phase 3 Brief -- Ranged Stack (Bows and Crossbows at Volley Scale)

| | |
|---|---|
| **Companion docs** | `Docs/spec/ranged-stack.md` (design spec: §0 RC-table behavior contract, §3 data model, §4 mechanics, §6 touch-point table, §11 tunables) · `Docs/AGENTS.md` (standing protocol) · `Docs/spec/roadmap-and-testing.md` (Part A Phase 3, T3 gate table, B.0 test contract, B.8 DoD) |
| **Engine pin** | UE 5.8.4 (ADR-13); NO new engine API territory expected (projectiles are a custom SoA pool; no Niagara/ISM work this phase -- gray-box overlay only) |
| **Run mode** | Single autonomous run, protocol per `Docs/AGENTS.md`; all CI evidence via `Tools/CI/run_tests.bat {unit,func}`; nightly via `Tools/CI/nightly.bat` |
| **Priority directive** | The ranged stack: EDU ranged fields, the ballistic solver, heightfield LOS, rank-limited volleys with dispersion, friendly-fire rules, the pooled projectile store, impact resolution THROUGH THE MELEE DEFENSE MACHINERY, per-entity ammo + unit out-of-ammo, SkirmishMode auto-retreat. Morale effects of casualties are Phase 4; weapon switching, artillery, and arrow representation are out of scope (spec §1) |
| **Owner-approval status** | §3.1 decisions are PROPOSALS -- the owner marks them approved before this brief freezes; spec §8 ADR-27..31 are proposals pending the owner's architecture-log commit |
| **Sequencing** | Executes after Phase 2 engagement (complete 2026-10-27) + the 2026-10-28 audit and its fixes (HEAD `48f1553`). Rationale: spec `Docs/spec/ranged-stack.md` §9 |
| **Kickoff state** | The pure-math kickoff patches (`Patches/0010..0016`, branch `phase3/kickoff`) already deliver: this spec + brief, the EDU `stat_sec` extension + ranged fixture rows + spawn mapping, `FRangedStateFragment` + the `Battle.Ranged.*` CVAR block + failing S6/skirmish/perf stubs, the `BattleBallistics` seam + T3.1/T3.2 tests, the volley/ammo/skirmish seam + `BattleProjectilePool` + T3.3 tests, and the state-hash ranged terms. Task 0-4 below VERIFY and BUILD ON them (apply first; re-run the unit tier; re-sign what reality corrects) |

---

## §0. Mission + read order + token discipline

**Mission.** Implement the ranged stack so S6 passes and the exit criteria read: 1,000 arrows in flight cost < 0.3 ms sim; friendly-fire rules are demonstrable; a formed archer unit volleys by reload cycle, and an arrow that lands resolves through the same directional defense as a sword. The full behavior contract is the spec's §0 RC-table (RC1-RC9); the architecture claim is one sentence: **ranged is a fire-control pass over the existing spine plus a pooled SoA projectile store, and impact resolution is a consumer of the melee machinery -- not a new system.**

**Read order** (token discipline: nothing else; do not read the spec docs end-to-end):
1. `Docs/README.md` status block, then `Docs/AGENTS.md`.
2. This brief, all sections.
3. `Docs/spec/ranged-stack.md` -- §2 (architecture summary), §3 (data model), §4 (mechanics), §6 (touch-point table) are the implementation contract; §0 RC-table is the acceptance contract.
4. Roadmap Part A "Phase 3 -- Ranged stack" + the T3 gate table; B.0 (test-mode contract), B.5 (determinism hooks), B.8 (capability DoD) -- cite, do not restate.
5. `Docs/spec/architecture.md` §9.3 and §10.1-10.4 ONLY as the parent design (conflicts: architecture wins, report per AGENTS §2.1).
6. Source, in this order: `Plugins/MassBattle/Source/MassBattle/Public/BattleCombatFragments.h` -> `Private/CombatResolutionProcessor.cpp` -> `Public/BattleEngagement.h` + `Public/BattleMelee.h` -> `Public/BattleSpatialHash.h` -> `Public/BattleCostGrid.h` -> `Private/BattleStateHash.cpp` -> `Public/BattleRngRegistry.h`.
7. `Docs/phases/phase-2/report-engagement.md` §4 ("Where Phase 3 should start") -- ready hooks; `Docs/phases/phase-2/audit-engagement.md` §0-§1 if a combat detail surprises you (A-1..A-4, B-1..B-8 carry the why).
8. `Docs/reference/engine-api-5.8.md` -- Part B §2-§3 (processor + command-buffer facts) only.

**Do not** open `Docs/reference/engine-api-5.8.md` or `Docs/reference/mcp-playbook.md` end-to-end. If an engine symbol misbehaves, follow AGENTS §4 (correct the owning reference, log it).

## §1. Phase facts digest (pre-verified 2026-10-29 against the repo at HEAD `48f1553`)

Every fact below is verified against the cited source. Cite fact IDs in the progress log instead of re-reading.

- **F1** The melee defense machinery is fully reusable as-is: `FMeleeMath::{HitChance, SkillArcMult, ShieldArcWindow, ArmorMultiplier, CombatRoll01}` (`Public/BattleMelee.h`), the shared quantizer `FCombatSolver::SectorIndexFromAngle` (`Public/BattleEngagement.h`), constants golden-locked in `FMeleeMathConstants`. `CombatRoll01(EntityIndex, EntitySerial, RollCount)` is the seed-derived roll (A13 discipline -- no streams, no wall-clock). The impact path needs NO change to `BattleMelee`.
- **F2** The death path is the impact exit: kill = `FDeadTag` defer via command buffer -> observer -> signal -> `UUnitRegistrySubsystem::HandleUnitDeath` (roster removal, pair scratch cleanup, `EXEC_RESULT RosterDeath`). The resolution processor's precedent: `Context.Defer().AddTag<FDeadTag>(Pair.Target)` (CombatResolutionProcessor.cpp:135). Mount-pool victims drain `DefenderMountRemaining` first. NOTE: `FHealthFragment.QueuedDamage` has NO writer today (audit A-tier note) -- impacts follow the kill-tag path, not a new damage queue.
- **F3** The EDU parser schema is a 14-key arity table (`Private/BattleEduParser.cpp` GetSchema); the kickoff patch inserts `stat_sec` (same 11-value arity/kinds as `stat_pri`) between `stat_pri` and `stat_pri_armour` -> 15 keys. `FBattleRowStats::FromEdu` (`Private/BattleSpawnLibrary.cpp`) reads stat_pri[0..1] as melee attack/defense; the patch adds the missile branch (pri[2] != "no" -> ranged row: RangedAttack = pri[0], melee stats from sec[0..1], range = pri[3], ammo = pri[4]). T0.2 golden re-sign policy: disclosed in the patch commit, `SchemaFieldCount` 14 -> 15, fixture + embedded `GoldenCanonical` gain the rows.
- **F4** Ranged data slots ALREADY exist and are combat-inert: `FBaseStatsFragment.RangedAttack`, `FUnitStatsSharedFragment.{RangedAttackMult, AccuracyMult}`, `FBattleAttributeSnapshot.{RangedAttackMod, AmmoPool}`, GAS `UUnitAttributeSet` attributes, save round-trip fields, `FBowTag` + spawn `if (Row.bBow)` stamp (`BattleSpawnLibrary.cpp:170`), `FBattleHashEntry.Flags` ("reserved for ranged/LOD", `ECombatHashFlags` undefined -- the kickoff patch defines `RangedCapable = 1<<0`). Nothing consumes them yet; no new GAS attributes or shared fragments are needed this phase.
- **F5** State-hash append pattern: per-entity blocks in `GetSortedHandles()` order, ranged block appends AFTER the charge block, fixed term count, zero-fill when absent; pool block (live count + per-index quantized terms) appends last (`Private/BattleStateHash.cpp`; the engagement spec §3.4 discipline). `Quantize2` = 1e-2 precision; `CityHash64` fold.
- **F6** Archetype count is 6 today, asserted by `Battle.Unit.Combat.SpawnComposition`. Ranged-capable rows add compositions: EXPECTED 6 -> 8 (one bow infantry composition x alive/dead); `FCrossbowTag` stays unbuilt (Kind is a fragment value). The SpawnComposition constant is re-signed with the growth itemized in the report (disclosed re-sign, same policy class as T0.2).
- **F7** CVAR block pattern: file-local `TAutoConsoleVariable` + namespace accessors (`Private/BattleEngagement.cpp`); worker-read contract uses `GetValueOnAnyThread()` (audit A-1 verification note). The kickoff patch ships `FRangedCVars` in `BattleRanged.h` the same way. `Battle.Ranged.Enable` default 0 until Task 6 lands end-to-end (B.8 item 6), then 1.
- **F8** Test contract (roadmap B.0): `UBattleTestSettings::ApplyTestMode()/AssertTestMode()` at every S-scenario start; naming `Battle.Unit.Ranged.*` (per commit) + `Battle.Func.*` (per PR); scenarios on `L_MassBattle_T0_Functional`; the harness opens the map, finds/spawns `AFunctionalTest` actors via `FBattleFuncTestRegistry` factories (reflection unreliable in -game); scenario actors are `EPhase` tick-state machines counting fixed 1/60 ticks (never wall-clock); every completion logs an `EXEC_RESULT` line. Test sources: `Plugins/MassBattle/Source/MassBattleTest/` behind `#if WITH_DEV_AUTOMATION_TESTS`.
- **F9** Unit conventions: fragment boundary = centimeters, seam math = meters, convert exactly once; pool + fragments store meters where the spec says so (§3.3 layout). ASCII + LF, `--` not em-dashes; commits prefix `phase3:`; game-thread processors set `bRequiresGameThreadExecution = true` + `EProcessorExecutionFlags::AllWorldModes`, `ExecuteInGroup = "Battle"` (engine-api Part B §2-§3; the ranged processors are game-thread BY DESIGN -- spec §8 ADR-28).
- **F10** Debug subsystem: `EnqueueOverlayLine(FBattleOverlayLine{From, To, Color, Shape, Radius})` thread-safe; metrics via `Record*` + `Battle.DumpMetrics` (`EXEC_RESULT BattleMetrics ...` line -- extend the format with Ranged fields); `ResetDiagnostics` coverage. B.2's ranged rows (trajectory segments, impact markers, ammo readouts) extend this framework.
- **F11** Spatial hash: `UBattleSpatialHashSubsystem` 3 m cells, counting-sort rebuild from end-of-last-tick transforms, `QueryCircle(CenterM, RadiusM, OutEntries, OutPositionsM)` APPENDS. Sanctioned circle-query consumers after this phase's A5 amendment: coverage, mob targeting, projectile lane + impact queries. `FindSlotFor(Entity)` binary-searches. The step pass consumes LAST tick's hash (rebuilt mid-frame after UnitLocomotion) -- impact queries read end-of-last-tick positions, consistent with the rebuild contract.
- **F12** The 4 Hz cluster pass (`EngagementClustersProcessor`) maintains unit envelopes (`FUnitClusterNode{AnchorM, YawDeg, HalfAlongM, HalfAcrossM, ...}`) + engagement records; skirmish distance reads THIS geometry (never per-entity scans). `GetCombatClusterCooldown()` accumulates the cadence.
- **F13** Nightly/baseline machinery: `Tools/CI/nightly.bat` (smoke + movement soak on the staged build; `write_baseline.py` -> `compare_baseline.py --tolerance 0.5`); soak variants ride `BattleMovementSoakController` as CVAR-read flags at the order cadence (`Battle.Combat.SoakClash` precedent; flag off = byte-identical baseline). Perf evidence channel: `Battle.Combat.PerfStress` fixture precedent -> `Battle.Ranged.PerfStress` + BattleMetrics line (T2.9 pattern: 0.452 ms @ 1k read from the line, not the baseline JSON).
- **F14** Soak FINAL line format + budgets live in `Private/BattleMovementSoakController.cpp` (verbatim `GAUNTLET BattleMovementSoak FINAL ...`); the volley variant appends its counters AFTER the existing fields (never reorders them -- the write_baseline regex parses positionally).

## §2. Pointers

MCP/editor automation: operating rules live in `Docs/reference/mcp-playbook.md` (check its §8 correction ledger first). This phase is planned MCP-light: fixtures are C++/ExecBridge-authored (the B-3 dictionary-keyed pattern), all evidence via `run_tests.bat`; the only editor-side item is the M4 demo visualization, which is PR-scoped (PR-8, gray-box overlay until Phase 5).

## §3. Phase ground rules + owner decisions + risk watchlist

### 3.1 Owner-decision table (PROPOSALS -- owner marks approved before the run; implement as written, never re-litigate)

| ID | Decision | Choice |
|----|----------|--------|
| A1 | Seam modules | Two B.0 seams (kicked off in the patches): `BattleBallistics.h/cpp` (SolveLaunch, PerturbDirection, StepProjectile, MarchClear) and `BattleRanged.h/cpp` (profiles, SelectVolleyShots, ammo accounting, EvaluateSkirmish, FRangedCVars). Free functions, meters, deterministic, seed-derived RNG only (F1). Processors stay thin shims |
| A2 | Data spine | `FRangedStateFragment` rides ranged-capable compositions ONLY (arch §3.2); ZERO new shared fragments, ZERO new GAS attributes (F4); registry toggle/out-of-ammo fields in `FUnitRuntimeState`; archetype count 6 -> 8 expected, SpawnComposition re-signed disclosed (F6) |
| A3 | Projectile store | `FBattleProjectilePool` SoA + `UBattleProjectileSubsystem` (game thread); NOT entities, NOT actors; index + generation identity; capacity reserved at battle start, Acquire fails closed (drops metric); default 4096 (spec §3.3, ADR-27) |
| A4 | Processor placement | `RangedFireProcessor` + `ProjectileStepProcessor` GAME-THREAD, group "Battle" (spec §8 ADR-28 -- the deviation from arch §4 row 9's worker label is recorded, evidence: three game-thread-owned subsystems + the Phase 2 worker no-subsystem rule). Step FIRST in group; Fire after AssignPairs, before FormationSlotResolve |
| A5 | Impact resolution | Transient one-roll context reusing the melee machinery (sectorizer + arc bands + HitChance + composite pools + F2 death path); projectiles NEVER form persistent pairs (spec §4.6, ADR-29). Attack = RangedAttack x RangedAttackMult x AccuracyMult |
| A6 | Constants | A6-posture placeholders golden-locked at T3.1 (speeds 35/60, dispersion 4/2 deg, hit radius 0.5 m, reload 3 s, AP bolt); T6.1 swaps ported constants incl. the friendly-fire rule as explicit port-or-deviation (ADR-30, R12) |
| A7 | Friendly fire v1 | Arced: hold when friendly count in the estimated impact zone > `FriendlyImpactTolerance` (default 0). Flat: hold when a friendly body intersects the lane corridor. Impacts are team-blind (the impact-point rule). Both halves asserted in S6 (spec §4.5) |
| A8 | Skirmish | Unit-level via the 4 Hz cluster geometry (F12); retreat = registry self-issued Move order away from the threat (auto-flagged; player order-serial bump cancels); hysteresis 1.3x; FireAtWill=off suppresses fire, NOT retreat (spec §4.8). `bSkirmish` default on for `class missile` rows |
| A9 | Save schema | v1 -> v2: per-unit `bFireAtWill/bSkirmish/bOutOfAmmo` + fraction denominators; restore accepts 1 (defaults) and 2; entity ammo + pool stay unsaved (T6.3 by design, spec §3.5) |
| A10 | Rank limits | Arced fires from the front `VolleyRanks` ranks (default 2); flat from the front rank only. Elevated all-fire is an extension point (spec §10), not built |
| A11 | Order surface | No new order ETypes (fire-at-will/skirmish are TOGGLES, not orders -- arch §10.4); registry `SetUnitFireToggles` + ExecBridge `Battle.Ranged.Toggle` + `Battle.Ranged.Dump` (spec §4.10). Attack orders aim fire implicitly (the fire pass targets the intent/record enemy) |
| A12 | Movement untouched | `ServoProcessor`/`UnitLocomotion` gain ZERO ranged consumption (retreat is a normal move order). `BuildSpatialHashProcessor` gains the Flags stamp line only |
| A13 | Determinism | Rolls via the `CombatRoll01` hash family with run-invariant inputs (ShotsFired as the count -- never GFrameCounter); pool iteration index-ordered; hash terms per spec §3.4; the two-run test extended to a mixed clash+volley scenario (RC8) |
| A14 | Out-of-ammo authority | Registry flips `bOutOfAmmo` at aggregate zero (once); `AmmoPool` GAS attribute mirrors the fraction for UI only (authority registry-side, arch §10.1); no resupply v1 |

### 3.2 Phase ground rules (additions to AGENTS.md only)

1. All ranged math lives in the two seam modules (A1) -- B.0's review flag applies unchanged.
2. Game-thread ranged passes never allocate per tick in the steady volley: SoA pool + preallocated shot buffers + the existing scratch discipline (EC9).
3. The pool never grows at runtime: Acquire beyond capacity returns `InvalidIndex`, increments the Drops metric, and the shot is skipped (fail closed, never an assert in shipping paths).
4. Projectiles are never visible to the registry as units or entities: attribution rides `FUnitHandle` + shooter entity index/serial in the pool entry only.
5. Ranged CVARs default OFF until their owning task lands (B.8 item 6); flags-off = byte-identical baseline, the unchanged T1.8/T2 soaks prove it.
6. Spec doc sections are read-only (AGENTS §7): proposed patches only where this brief authorizes them (Task 8's B.7 entry + the ADR-28 architecture-table amendment content); disclosed in the report.
7. `Battle.Ranged.Enable` flips to default 1 only when Task 5's impact resolution lands end-to-end.
8. The kickoff patches are PRE-APPLIED at run start (apply `Patches/0010..0016` via `git am` if the owner has not merged them; re-run the unit tier FIRST and record the result -- any patch-compile failure is a Task 0 blocker, logged with full error text).

### 3.3 Risk watchlist

| ID | Risk | Mitigation / watch |
|----|------|--------------------|
| R-R1 | Volley spam: every eligible entity solving ballistics every tick | Reload-cycle gating (only elapsed-reload entities solve); staggered reload phases (F1 hash family); per-tick FireMs metric; the profile solve is closed-form (no iteration) |
| R-R2 | Impact query cost: 1,000 swept-segment queries against the hash | Query ONE circle per projectile per tick at the segment midpoint (radius = half-segment + HitRadius + BodyRadius); most projectiles mid-flight are above body height -- early-out on the height check before querying |
| R-R3 | Pool churn storms (volley release spikes) | Free-list stack (O(1) acquire/release); Release never reallocates; Drops metric watches saturation; PoolCapacity cvar is the escape hatch |
| R-R4 | Determinism leak: impact order depends on hash-cell iteration | Impact resolution per projectile is independent (no cross-projectile state); pool index order is the iteration order; the two-run test catches any leak (RC8) |
| R-R5 | Skirmish oscillation (retreat <-> fire thrash) | Hysteresis band 1.3x (A8); cluster-geometry 4 Hz cadence low-passes the trigger; retreat keeps unit facing (no wheel thrash); settle assertions in SkirmishBehavior |
| R-R6 | Friendly-fire rule starves fire entirely (tolerance 0 + dense melee) | That is the M2TW behavior (archers hold); S6 scripts BOTH halves; the tolerance cvar is the tuning knob; logged Holds metric distinguishes rule-holds from bug-holds |
| R-R7 | Archetype growth exceeds 8 (unexpected compositions) | SpawnComposition asserts the measured count at every spawn path; growth itemized in the report; the fragment rides ONLY ranged rows (spawn library branches on bBow) |
| R-R8 | Melee baseline regression from the new processors sitting in the group | Flags-off identity (ground rule 5); the step processor early-outs on live-count 0; AssignPairs ordering preserved (Fire runs AFTER it) |
| R-R9 | EDU golden re-sign churn (fixture + embedded canonical + FromEdu disagreement) | The kickoff patch ships all three consistently; Task 0 verifies the unit tier green FIRST; any drift is a facts-correction entry (AGENTS §4), not an in-place brief edit |
| R-R10 | T3.6 gate misses the budget at 1k in flight | FireMs + StepMs + hash-term ms measured separately (BattleMetrics line); the pool SoA is branch-free in the step loop; fallback ladder recorded: substep batching -> every-N-tick impact queries -> per-cluster impact sharding (never entity-ize the pool) |

## §4. API digest -- no new engine territory

None expected. Projectile pooling is custom SoA code; no Niagara, no ISM authoring, no MassRepresentation touches this phase (gray-box overlay only -- PR-8 owns the visual arrow). If ANY engine symbol surprises (e.g. a processor-phase registration detail), follow AGENTS §4: verify against pinned 5.8.4 source, correct `Docs/reference/engine-api-5.8.md`, log it. The game-thread processor pattern (F9) is fully covered by engine-api Part B §2-§3.

## §5. Task plan

Gate-first discipline: each gate test lands in its owning task's commit (or the prior one) -- verified via git log at the DoD. The kickoff patches already landed the seams and unit-tier tests (Tasks 0-4's math); the run's tasks verify them on the UE machine, then build the pipeline.

### Task 0 -- Kickoff patch application + verification

**Goal.** The phase is CI-visible and the seams compile green before any new work.

**Steps.** (1) Apply `Patches/0010..0016` (`git am`) if not already merged by the owner; resolve any conflict by hand and log it. (2) Run the unit tier: expected GREEN including the new `Battle.Data.Edu*` (re-signed), `Battle.Unit.Ranged.*` (Ballistics, LOS, AmmoAccounting, Pool, VolleySelection, SkirmishMath, HashTerms extension), `Battle.Unit.Combat.SpawnComposition` (re-signed 6 -> 8). (3) Run the func tier: expected RED ONLY on the two fail-by-design skeletons (`Battle.Func.S6_Volley`, `Battle.Func.SkirmishBehavior` -- count them, list them in the progress log; NO perf placeholder stub ships -- the audit's C-6 lesson: T3.6 lands as a fixture + measured metrics with Task 7). (4) Create `Docs/phases/phase-3/progress.md` with the session header.

**Acceptance check.** Unit tier green with the re-signed counts recorded verbatim; the two skeletons are the ONLY func failures; archetype count 8 measured at spawn (progress log line).
**Commit.** `phase3: task 0 kickoff verified (patches applied, unit tier green)`

### Task 1 -- Data spine on the UE machine (registry + save v2 + live spawn path)

**Goal.** Ranged rows flow end to end: parse -> FromEdu -> spawn stamp -> registry toggles -> save v2.

**Steps.** (1) Verify the live path beyond the tier: `Battle.SpawnRoster` on an archer row stamps `FRangedStateFragment` + `FBowTag`; `Battle.Ranged.Dump` prints the toggle defaults, skirmish on. (2) Registry APIs: `SetUnitFireToggles` + `GetUnitAmmoFraction` + the `bOutOfAmmo` flip; the A11 rejection lines on bad handles. (3) Save v2: capture/restore carries toggles + out-of-ammo; legacy v1 load path takes defaults; extend `Battle.Unit.Combat.SaveRoundTrip` (schema 2 asserts). (4) State-hash terms: HashTerms extension green (mutation sensitivity: ammo decrement, reload advance, pool entry); `Battle.StateHash` two-run sanity with a volley fixture.

**Acceptance check.** Unit tier green (SaveRoundTrip + HashTerms extended); `EXEC_RESULT` evidence lines for spawn-stamp, toggle reject, out-of-ammo flip; archetype count 8 stable across all spawns.
**Commit.** `phase3: task 1 data spine verified + save v2`

### Task 2 -- Ballistics + LOS seams on the machine (T3.1/T3.2 ratification)

**Goal.** The kickoff seams' goldens ratify on the machine; any float drift is corrected and re-locked (disclosed).

**Steps.** (1) Run `Battle.Unit.Ranged.Ballistics` + `Battle.Unit.Ranged.LOS`; if any golden literal drifts (cross-compiler float ordering is out of scope, R9, but same-machine FMA contraction can bite -- the fixtures use closed-form checks with 1e-4 tolerances), re-sign with the diff recorded. (2) Add the two DDA boundary cases the patch could not hand-verify (cell-corner crossings) if not already green. (3) Record `SampleHeight` bilinear behavior on a synthetic hill grid in the progress log (the net-new grid API's first live evidence).

**Acceptance check.** T3.1/T3.2 green with evidence lines; any re-sign disclosed per AGENTS §4.
**Commit.** `phase3: task 2 T3.1/T3.2 ratified`

### Task 3 -- Projectile pool + subsystem + step processor

**Goal.** Projectiles live, fly, and die on the machine; RC9's stepping half is measurable.

**Steps.** (1) `UBattleProjectileSubsystem`: Reserve at battle start (PoolCapacity), ResetBattleState clears; the pool block in the state hash. (2) `ProjectileStepProcessor` (game-thread, FIRST in the Battle group, `Battle.Ranged.Step`): integrator loop, TTL/ground-hit release, live-count + StepMs metrics, impact STUB (log-only hits) -- resolution lands in Task 5. (3) Ground-hit: `SampleHeight` at (X,Y), release below. (4) Unit test: step a synthetic volley from the pool (spawn 64, step 600 ticks, all dead by TTL or ground, zero capacity violations).

**Acceptance check.** Unit tier green; `Battle.DumpMetrics` carries InFlight/StepMs; the hash two-run equality holds with in-flight entries; flags-off identity (no pool entries, step early-outs).
**Commit.** `phase3: task 3 pool + step processor`

### Task 4 -- Volley selection + RangedFire processor

**Goal.** The fire pass: reloads, eligibility, LOS, friendly rules, dispersion, ammo, out-of-ammo, skirmish.

**Steps.** (1) `RangedFireProcessor` (game-thread, after AssignPairs, `Battle.Ranged.Fire`): per-unit gather of shooter views (roster order), the seam call (A1), pool writes, ammo decrements, reload staggers. (2) Target selection: the unit's intent/record enemy (A11) -- AttackUnit/AttackMove orders aim fire; no order + FireAtWill = hold. (3) Friendly rules per A7 (lane blockers via the hash; impact-zone estimate). (4) Out-of-ammo aggregate + GAS AmmoPool mirror. (5) Skirmish: `EvaluateSkirmish` per the 4 Hz geometry; the auto-retreat move order (A8). (6) Metrics: Shots, Holds (by rule), FireMs, ammo fractions; overlay rows (trajectory segments, ammo readouts).

**Acceptance check.** A scripted ExecBridge volley fixture (`Battle.Ranged.SpawnVolley line`) fires: `EXEC_RESULT` lines show shooters eligible -> shots -> pool entries -> decrements; holds fire with a friendly in the lane; out-of-ammo flips once; skirmish retreat triggers and resumes.
**Commit.** `phase3: task 4 ranged fire pass`

### Task 5 -- Impact resolution (RC6) + Enable flip

**Goal.** Arrows kill through the melee machinery; `Battle.Ranged.Enable` defaults 1.

**Steps.** (1) Impact query per R-R2 (midpoint circle + height early-out). (2) The transient roll context: incoming angle -> sector -> arc bands -> HitChance -> mount-pool drain or `FDeadTag` defer (F2). (3) Team-blind impacts; FriendlyHits metric + overlay marker. (4) `Battle.Ranged.Enable` -> 1 (ground rule 7; the flags-off identity already asserted in Tasks 3/4). (5) The kill chain evidence: roll -> tag -> signal -> roster (`EXEC_RESULT` lines, the ChargeMechanics precedent).

**Acceptance check.** A volley against a formed line produces kills at the golden rate (tolerance band); friendly stray hits demonstrable; the death path identical to melee (DeathsByArc gains the ranged-sector rows); func tier: CombatSmoke + ChargeMechanics + S4/S5 still green (melee unregressed).
**Commit.** `phase3: task 5 impact resolution + Enable=1`

### Task 6 -- Scenarios S6_Volley + SkirmishBehavior (T3.4/T3.5)

**Goal.** The exit-criteria scenarios are PR-gate citizens (the skeletons from Task 0 replaced).

**Steps.** (1) `Battle.Func.S6_Volley` (spec §5): dispersion bound, LOS hill variant, friendly-fire both halves, ammo accounting -- sampled assertions with evidence lines. (2) `Battle.Func.SkirmishBehavior`: break/resume/hysteresis, hold-fire-still-retreats, player-order override. (3) Both actors registered via the factory registry (F8); fixed-tick phase machines; cvar save/restore (`ClashSetCvars` precedent including the ranged block). (4) Demo fixtures: `Battle.Ranged.SpawnVolley {line|skirmish}` (dictionary-keyed) + the M4 gray-box sample on the demo game mode (overlay-only arrows).

**Acceptance check.** Func tier green with S6/Skirmish added to the PR set; scenario evidence lines verbatim in the progress log; M4 runs on the demo map headless.
**Commit.** `phase3: task 6 S6 + skirmish scenarios + M4 gray-box`

### Task 7 -- Determinism, perf gate (T3.6), soak variant, re-baselines

**Goal.** RC8/RC9 proven; the nightly ranged tier exists; baselines current.

**Steps.** (1) Extend `DeterminismTwoRun` to a mixed clash+volley scenario (archers + infantry both sides, 40 s, hash every 60 ticks). (2) Perf: `Battle.Ranged.PerfStress` fixture (2 archer units of 500 @ 100 m -> 1,000 in flight peak); BattleMetrics line += `StepMs/FireMs/InFlightPeak/PoolDrops`; assert step+fire+hash-terms < 0.3 ms @ 1k in flight; record the measured line in the baseline notes (the T2.9 pattern). (3) Soak: `Battle.Ranged.SoakVolley=1` variant (F13/F14 -- counters APPEND to the FINAL line); flag off = byte-identical. (4) Re-baseline `entity_spawn_80_us` with the ranged fragment present. (5) One staged nightly run as evidence.

**Acceptance check.** Two-run hash equality (mismatches=0); T3.6 gate green with the measured line; soak variant ends stuck=0 separation=0 archetypes=8 with volley counters sane; spawn baseline updated.
**Commit.** `phase3: task 7 determinism + perf gate + soak + baselines`

### Task 8 -- Overlay, metrics, docs, final verification

**Goal.** B.8 closure: observable, measured, documented, regression-protected.

**Steps.** (1) B.2 ranged overlay rows complete (trajectories z-heighted, impact markers colored by team, per-unit ammo readouts, hold-reason flags). (2) Metrics wired into `Battle.DumpMetrics` + `ResetDiagnostics`; the BattleMetrics format extension documented in the progress log. (3) B.7 regression-checklist entry appended to the roadmap (authorized here): Phase 3 gates + invariants (the F14 FINAL format note included). (4) Docs: `Docs/README.md` map + status; the ADR-27..31 proposals + the ADR-28 architecture-§4-row amendment content handed to the owner; facts corrections per AGENTS §4. (5) Final verification: full unit + func tiers, one nightly (baseline + volley variant), git-log gate-first audit, archetype count 8 asserted everywhere; PR-6..PR-9 statuses reviewed (executed or still pending owner/editor).

**Acceptance check.** B.8 checklist (§7) fully checked with evidence pointers; CI green; progress log complete; handoff report per AGENTS §5.
**Commit.** `phase3: task 8 overlay + metrics + docs` then `phase3: complete`

## §6. Gate -> implementation map

| Gate | Tier | Task | Test name | Asserts (short) |
|------|------|------|-----------|-----------------|
| T3.1 | Unit | patch/Task 2 | `Battle.Unit.Ranged.Ballistics` | solver + dispersion goldens; deterministic per seed |
| T3.2 | Unit | patch/Task 2 | `Battle.Unit.Ranged.LOS` | hill occlusion; equal-height no false block; boundary cases |
| T3.3 | Unit | patch/Task 1 | `Battle.Unit.Ranged.AmmoAccounting` (+ Pool/Volley/Skirmish) | shots == decrements; aggregate == sum; out-of-ammo once; pool fails closed |
| T3.4 | Func | 6 | `Battle.Func.S6_Volley` | dispersion + LOS + friendly-fire + ammo in one scenario |
| T3.5 | Func | 6 | `Battle.Func.SkirmishBehavior` | break/resume/hysteresis; hold-fire still retreats; order override |
| T3.6 | Perf | 7 | `Battle.Ranged.PerfStress` | 1,000 in flight < 0.3 ms; no mid-volley alloc |
| -- | Func | 7 | `DeterminismTwoRun` (extended) | two-run hash equality, volley included |

## §7. DoD checklist

- [ ] T3.1-T3.6 green with evidence (unit/func via `run_tests.bat`; perf via the fixture + measured BattleMetrics line; soak via the staged nightly; verbatim lines in the report)
- [ ] RC1-RC9 demonstrable with evidence pointers (spec §0)
- [ ] B.8 item 1: unit tests over the math (both seams: `BattleBallistics`, `BattleRanged`)
- [ ] B.8 item 2: S6_Volley + SkirmishBehavior in the PR gate
- [ ] B.8 item 3: debug overlay (trajectories, impact markers, ammo readouts, hold reasons)
- [ ] B.8 item 4: metrics (shots, holds, hits, friendly hits, drops, in-flight peak, FireMs, StepMs)
- [ ] B.8 item 5: B.7 regression-checklist entry (roadmap, this phase)
- [ ] B.8 item 6: `Battle.Ranged.*` CVARs; flags off = pre-ranged behavior (unchanged T1.8/T2 soak identity asserted)
- [ ] B.8 item 7: Iris-ready -- pool + fragments reconstructible from serializable state; no raw pointers across boundaries; rolls seed-derived
- [ ] Archetype count 8 constant through all scenarios and soak runs (F6; growth itemized)
- [ ] State-hash stability with ranged terms + pool (mutation sensitivity + two-run equality)
- [ ] Zero per-tick heap allocations in fire/step steady state (EC9 discipline; pool capacity preallocated)
- [ ] Save schema v2 round-trip green (toggles + out-of-ammo; legacy v1 accepted) -- A9
- [ ] EDU golden re-sign ratified on the machine (T0.2 policy; drift disclosed) -- F3/R-R9
- [ ] M4 demo gray-box running (SpawnVolley fixtures; overlay arrows) -- PR-8 tracks the visual pass
- [ ] `entity_spawn_80_us` re-baselined with the ranged fragment present
- [ ] Gate-first verified via git log (each T3 gate landed with/before its owning task; the kickoff patches' tests counted)
- [ ] ADR-27..31 + the arch §4 row amendment handed to the owner as proposals
- [ ] `Docs/phases/phase-3/progress.md` complete; `Docs/README.md` map + status updated; handoff report per AGENTS §5; PR-6..PR-9 statuses recorded

## §8. System prompt

```text
You are the MassBattle Phase 3 implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-3/brief.md.
Execute the brief autonomously under the standing protocol; brief §3.1
decisions are owner-approved as marked. The kickoff patches (Patches/0010..0016)
are pre-applied: Task 0 verifies them first. Stop at the brief's DoD (§7)
after writing Docs/phases/phase-3/report.md and the final commit.
```
