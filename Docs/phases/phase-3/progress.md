# Phase 3 -- Ranged Stack: Progress Log

| | |
|---|---|
| **Session** | 2026-10-28, kickoff-artifact pass + Task 0 machine verification (single autonomous run, protocol per `Docs/AGENTS.md`) |
| **Engine** | UE 5.8.4 CL 58902052 at `E:\Unreal\UE_5.8` (reference machine `ref-i7-9700F-1660Ti`) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin module `MassBattle`, test module `MassBattleTest`) |
| **Kickoff state** | Patch series `0010..0016` applied from the design-pass bundle, statically verified + corrected, landed on `main` @ `9be490d` (fixes F-K1..K6: see `Docs/phases/phase-3/kickoff-verification.md`) |

## Discrepancies (dated, newest first)

- **2026-10-28 (Task 0, run 1)** -- `Tools/CI/run_tests.bat` does NOT build: it launches
  `UnrealEditor-Cmd` on existing binaries. Run directly, it executed the pre-kickoff DLLs
  against the new EDU fixture -> 11 spurious failures, all `unknown field 'stat_sec'
  (schema has 14 fields)`. The Jenkinsfile builds `MassBattleEditor` FIRST (Build.bat ->
  run_tests.bat). Task 0 now follows that order; consider a build guard in the bat
  (owner call, logged not applied -- out of Task 0 scope).
- **2026-10-28 (Task 0, build)** -- First real compile of the engine in this workspace hit
  a stale rules-assembly blocker: `UE5Rules.dll` (precompiled, "File is installed")
  predated the Fab plugin's `Engine/Plugins/Fab` install, so `FabToolset` module rules did
  not exist and UBT aborted (`RulesError`). The makefile cache had masked it since the
  last clean rules build. Fix: delete `UE5Rules.{dll,pdb}` + `UE5RulesManifest.json`,
  lift `Engine/Build/InstalledBuild.txt`, run `-Mode=QueryTargets` (regenerates the
  rules assemblies incl. Fab), restore the marker. Recorded in
  `Docs/reference/environment.md` §4; blocks EVERY source change until it bites once.
- **2026-10-28 (Task 0, build)** -- F-K7 (compile slips in the kickoff series, found by
  the first real build): 7 `TestEqual` calls in `MassBattleRangedTests.cpp` passed
  LWC-double `FVector` components against float literals -> MSVC C2666
  (float/double overload ambiguity). Fixed on main (double literals at those sites).
- **2026-10-28 (Task 0, run 2)** -- F-K8 (runtime test defects, found by the first run
  with fresh binaries): (a) inline EDU fixtures in `MassBattleGarrisonTests.cpp` and
  `BattleFuncTestActor.cpp` lacked `stat_sec` -> rejected by the 15-key schema (the
  kickoff's re-sign sweep missed them; all four test files now carry it); (b) the
  ballistics `StepFlight` helper truncated the final partial step, banding landings by
  up to v*DT (1 m at bolt speed) vs the 0.35 m golden tolerance -> final partial step
  simulated; (c) the LOS golden "11 m mid-hill blocks" was knife-edge under the march's
  bilinear + entry-station-per-cell sampling (measured window 8-11 m clear, 12 m
  blocks) -> golden moved to 12 m with the reason in a comment; (d) two
  `VolleySelection` blocks reused a shooter view across sub-cases -- a fired shot arms
  the reload timer and poisons later cases (the two "hold" asserts passed for the
  wrong reason) -> fresh shooter per case. Unit tier re-run: 75/75.

## Task 0 -- kickoff patch application + verification

- [task0.1] Patches `0010..0016` applied (git am, clean), statically verified,
  corrected in-branch (F-K1..K6), landed on `main` -> HEAD `9be490d` before machine
  work began; series exported as `Patches/0010..0016.patch` + record.
- [task0.2] Build `MassBattleEditor Win64 Development` -> **Succeeded** (after the
  rules-assembly recovery + F-K7 fix).
- [task0.3] Unit tier (`run_tests.bat unit`): **passed=75 failed=0 total=75
  completed=True exit=0 ok=True** (`Tools/CI/last_run.json`, run 3). New suites all
  green: `Battle.Unit.Ranged.{Ballistics, LOS, Pool, AmmoAccounting, VolleySelection,
  SkirmishMath}`; re-signed goldens green: `Battle.Data.EduRoundTrip` (15 keys, 4
  fixture units), `Battle.Unit.Combat.{SpawnComposition, HashTerms, SaveRoundTrip}`,
  `Battle.Unit.Garrison.Booking`, `Battle.Data.EntityConfigIntegrity`,
  `Battle.Unit.AttributeSet.DelegateAndDirtyFlag`.
- [task0.4] Archetype probe (SpawnComposition AddInfo, verbatim):
  `archetypes: melee-only=3 with-ranged=4 (growth 1, one ranged composition expected;
  itemized in the phase report)`. Isolated-world probe: +1 live composition (arrow and
  bolt share it -- Kind is a fragment value). The F6 alive/dead 6 -> 8 count is
  asserted at scenario/soak level (Tasks 6/7 gates: soak FINAL line `archetypes=8`).
- [task0.5] Func tier (`run_tests.bat func`): **passed=19 failed=4 total=23**. RED on
  the two fail-by-design skeletons (counted = 2): `Battle.Func.S6_Volley`,
  `Battle.Func.SkirmishBehavior` -- both fail via the harness's M17 tick budget
  (36001 ticks) because the skeleton actors complete before the harness's watch window
  opens; Task 6 replaces them. ADDITIONALLY red: `S4_ShieldWall` + `S5_Mob`
  (`FAIL no pull-down (240 s; exposed=3 waiters=44)`) -- the two KNOWN phase-2
  deferred melts (`Docs/phases/phase-2/audit-engagement-verification.md` "Still
  deferred: S4_ShieldWall + S5_Mob"); they fail at base `48f1553` identically, so they
  are NOT kickoff regressions. No other func regressions.

## Task 1 -- data spine on the UE machine (registry + save v2 + live spawn path)

- [task1.1] Registry fire-control surface implemented (spec §3.2/§4.10, A11/A14):
  `SetUnitRangedSpawnState(Handle, bSkirmish, InitialAmmo)` (replaces the kickoff's
  `SetUnitSkirmishDefault`; same spawn-library call site, now stamping the fraction
  denominator's ammo half), `SetUnitFireToggles` + `GetUnitFireToggles` (bad handles
  reject with EXEC_RESULT lines), `GetUnitAmmoFraction` (live roster
  `FRangedStateFragment.AmmoCount` sum over `RangedInitialAmmo x InitialRosterSize`,
  -1 non-ranged/unresolvable), `FlipUnitOutOfAmmo` (once-only, never un-flips, EXEC_RESULT
  line on the flip). Denominator home: `FUnitRuntimeState.{RangedInitialAmmo,
  InitialRosterSize}` -- `InitialRosterSize` stamps at `SetRoster` (the one spawn
  publish; restore routes around it), so deaths shrink the roster, not the denominator.
- [task1.2] Save v2 (spec §3.5, A9): `FBattleSaveData.SchemaVersion` 1 -> 2;
  `FUnitSaveEntry` += `bFireAtWill/bSkirmish/bOutOfAmmo/RangedInitialAmmo/InitialRosterSize`;
  restore accepts 1 (toggles default on/off/off, denominators 0 -- no v1 save can contain
  a ranged row) and 2; the mismatch error line now reads "want 1 or 2". Entity ammo +
  pool stay unsaved (T6.3 by design).
- [task1.3] ExecBridge surface (spec §4.10): `Battle.Ranged.Toggle <UnitIndex>
  <FireAtWill|Skirmish> <0|1>` (MatchUnitIndex resolution, A11 rejection lines) and
  `Battle.Ranged.Dump [UnitIndex]` (toggles, live fraction, ascending ammo x count
  histogram, pool attribution) -- both registered, both exercised from the unit tier.
- [task1.4] `Battle.Unit.Ranged.RegistrySpine` added (MassBattleCombatTests.cpp, combat
  helpers): spawn-stamp defaults (archer Skirmish=1 / levy Skirmish=0), fraction 1 ->
  decrement band -> 0, melee -1, once-only flip + latching, toggle set/cleared with
  OutOfAmmo surviving, bad-handle rejects, archetype-count stability across identical
  ranged spawns (two-row world=4, AddInfo).
- [task1.5] `Battle.Unit.Combat.SaveRoundTrip` extended: non-default toggles + the flip
  captured (entry-field asserts), restored verbatim (state asserts), legacy v1 downgrade
  restores defaults (FireAtWill on, Skirmish/OutOfAmmo off, denominators 0); bookkeeping
  hash independent of toggles (still round-trip equal).
- [task1.6] HashTerms extension (ammo/reload/shots mutations + pool entry/position/
  release) verified green on the machine from Task 0 -- unchanged this task; it IS the
  state-hash mutation-sensitivity evidence. The `Battle.StateHash` two-run-with-volley
  expansion is the Task 7 DeterminismTwoRun extension (unit-tier compute stability is
  asserted by HashTerms' recomputes + revert-restores).
- [task1.7] Build MassBattleEditor Win64 Development -> **Succeeded** (21 actions,
  166.8 s). Unit tier: **passed=76 failed=0 total=76 completed=True exit=0 ok=True**
  (`Tools/CI/last_run.json`; was 75 -- +1 = RegistrySpine). Evidence lines (verbatim,
  from `Tools/CI/last_run/unit.log`):
  - spawn stamp: `EXEC_RESULT RangedDump Unit=2:1 Ranged=1 FireAtWill=1 Skirmish=1
    OutOfAmmo=0 Fraction=1.000 Roster=40 InitialAmmo=25 InitialRoster=40 AmmoHist=25x40
    PoolOwned=0` (levy row: `Ranged=0 ... Skirmish=0 Fraction=-1.000 ... AmmoHist=-`)
  - out-of-ammo flip: `EXEC_RESULT OutOfAmmo Unit=2:1 Roster=40 InitialAmmo=25`
  - toggled state: `EXEC_RESULT RangedDump Unit=2:1 Ranged=1 FireAtWill=0 Skirmish=0
    OutOfAmmo=1 Fraction=0.000 Roster=40 InitialAmmo=25 InitialRoster=40 AmmoHist=0x40
    PoolOwned=0`
  - A11 rejects: `EXEC_RESULT RangedToggle FAIL StaleHandle Handle=987:1` /
    `EXEC_RESULT OutOfAmmo FAIL StaleHandle Handle=987:1` /
    `EXEC_RESULT RangedToggle FAIL NoSuchUnit Index=999`
  - archetype probe: `archetypes: two-row world=4` (melee alive/dead + ranged alive/dead;
    stable across identical re-spawns; the 8-count scenario/soak assertion stays with
    Tasks 6/7 per Task 0's note).
- [task1.8] Func tier (`run_tests.bat func`): **passed=19 failed=4 total=23** -- identical
  to the Task 0 baseline: the two by-design skeletons (`S6_Volley`, `SkirmishBehavior`,
  Task 6 replaces them) + the two pre-existing phase-2 melts (`S4_ShieldWall`,
  `S5_Mob`). No new func regressions from the data-spine work.
- [task1.9] Acceptance check: unit tier green (SaveRoundTrip schema-2 + HashTerms
  extended); EXEC_RESULT evidence lines for spawn-stamp, toggle reject, out-of-ammo flip
  (verbatim above); archetype count stable across all spawns at this tier (two-row
  world=4; Task 0's isolated-world probe unchanged).

## Task 2 -- ballistics + LOS seams on the machine (T3.1/T3.2 ratification)

- [task2.1] T3.1/T3.2 ratified on the machine, NO golden drift: `Battle.Unit.Ranged.Ballistics`
  + `Battle.Unit.Ranged.LOS` both `Result={Success}` (unit tier 76/76 after the additions;
  `Tools/CI/last_run/unit.log`). The kickoff's closed-form checks (1e-3/1e-4 tolerances)
  held under the same-machine compiler -- no re-sign, nothing disclosed.
- [task2.2] The two DDA cell-corner cases added to the LOS test (the cases the kickoff
  could not hand-verify), on the exact test grid (origin -129, 3 m cells; world (3,3) is
  the shared corner of cells (43,43)/(43,44)/(44,43)/(44,44)); lane (0,0)->(30,30) steps
  1.5 m (29 samples), straddle stations (60/29, 60/29) with bilinear tx=ty=0.1897 and
  (90/29, 90/29) with tx=ty=0.5345 -- a corner-adjacent hill blends at w=0.1537/0.2488:
  - Case A (single corner-touching hill cell, 10 m): blocks through the corner bleed
    alone (2.49 m at the second straddle > ray 1.6 + clearance 0.3); 2 m control clears
    (0.50 m).
  - Case B (the diagonal keyhole: two hill cells touching ONLY at the corner): a pure
    cell-DDA would advance (43,43) -> (44,44) and enter neither hill; the sub-cell
    march's bilinear read taps both (3.07 m at the first straddle) -> blocked; 2 m
    control clears (0.61 m). All four asserts green first run -- hand-computed
    expectations matched machine behavior.
- [task2.3] SampleHeight bilinear behavior recorded (the net-new grid API's first live
  evidence; the test now prints a measured lattice through a synthetic 4 m hill cell
  (46,46) at 1.5 m spacing, verbatim from `Tools/CI/last_run/unit.log`):
  ```
  SampleHeight lattice y=+7.5 : 0.000 0.000 0.000 0.000 0.000
  SampleHeight lattice y=+9.0 : 0.000 1.000 2.000 1.000 0.000
  SampleHeight lattice y=+10.5: 0.000 2.000 4.000 2.000 0.000
  SampleHeight lattice y=+12.0: 0.000 1.000 2.000 1.000 0.000
  SampleHeight lattice y=+13.5: 0.000 0.000 0.000 0.000 0.000
  ```
  Reads as: exact (4.000) at the hill cell's center, the bilinear tent half-step out
  (2.000), quarter height (1.000) at the four shared cell corners, 0 outside the
  neighbor ring; outside the grid reads 0 (GetHeight's bounds rule, asserted by the
  existing fade case). The measured table matches the analytic blend exactly.

## Task 3 -- projectile pool + subsystem + step processor

- [task3.1] Battle-start pool wiring: `UUnitRegistrySubsystem::ResetBattleState` now
  calls `UBattleProjectileSubsystem::ReserveForBattle()` (one idempotent call = the
  battle-start reserve at `Battle.Ranged.PoolCapacity` + the clear; no projectile
  crosses a battle boundary). The state-hash pool block (kickoff) reads this subsystem.
- [task3.2] Stepping math in the seam (A1): `FBallisticsMath::StepPool(Pool, Grid, DT)`
  + `FPoolStepCounts{GroundHits, TtlExpired}` in `BattleBallistics` -- index-ordered
  semi-implicit Euler via the existing `StepProjectile` (per-slot gravity =
  `FBallisticsConstants::GravityMps2 * GravityScale`), TTL decrement, release on ground
  hit (`SampleHeight` at the advanced (X,Y); null grid reads 0) or TTL expiry. Pure,
  allocation-free (EC9). The processor owns the gates + timing only.
- [task3.3] `UProjectileStepProcessor` (new `ProjectileStepProcessor.h/cpp`): game-thread
  (ADR-28), PrePhysics, group "Battle", `ExecuteBefore(UnitSyncProcessor)` = FIRST in the
  group (A4; F11's last-tick-hash contract), AllWorldModes. Gates: `Enable() && Step()`
  (flags-off identity) then live-count 0 early-out (R-R8). Impact stub: ground/TTL
  releases are log-only (Verbose aggregate line); resolution lands in Task 5.
- [task3.4] Engine-API note (recorded per AGENTS §4, no reference change needed): a
  query-less processor must override `ShouldAllowQueryBasedPruning` -> false; otherwise
  the dependency solver's archetype sweep logs `has no registered queries while being
  asked for matching archetypes` (MassProcessor.cpp:274 -- the engine's own
  misconfiguration notice). Found by the first func run: the warning leaked into every
  func test's log window and the summarizer re-bucketed all 19 passes as
  succeededWithWarnings. Override added; counts agree again.
- [task3.5] Metrics: `BattleMetrics` line gains APPENDED fields (F14 rule -- existing
  fields never move): `InFlight=%d StepMs=%.3f GroundHits=%d TtlExpired=%d PoolDrops=%d
  PoolCapacity=%d` (InFlight/Drops/Capacity read from the pool at dump time; StepMs +
  causes recorded by the processor). `ResetDiagnostics` zeroes the new counters.
  Evidence (unit tier, verbatim): `EXEC_RESULT BattleMetrics Frame=0 ... DeathsF/F/R=0/0/0
  InFlight=0 StepMs=0.000 GroundHits=0 TtlExpired=0 PoolDrops=0 PoolCapacity=0`.
- [task3.6] `Battle.Unit.Ranged.PoolStep` (MassBattleRangedTests.cpp): the brief's
  synthetic volley -- 64 shots (32 arced TTL-2 s + 32 steep-down TTL-20 s), 600 ticks at
  1/60: all dead, live count never grows, capacity never changes, zero drops; single-cause
  controls (arced-only drains at its 2 s TTL within float slack; steep-only records
  exactly 32 ground hits); two-run per-tick live-count equality (RC8's stepping half at
  unit tier); hill twin (5 m hill under the lane kills on tick 1 vs the flat twin's
  tick ~6 -- ground reads the heightfield); empty-pool no-op (the seam half of the
  early-out).
- [task3.7] Build -> Succeeded. Unit tier: **passed=77 failed=0 total=77 completed=True
  exit=0 ok=True** (`Tools/CI/last_run.json`; was 76 -- +1 = PoolStep). Func tier:
  **passed=19 failed=4 total=23**, counts agree, the exact Task 0/1 baseline (the two
  by-design skeletons + the two phase-2 melts) -- the new processor in the live Battle
  group leaves the melee baseline unregressed (R-R8).

## Task 4 -- volley selection + RangedFire processor

- [task4.1] DISCLOSED DEVIATION (AGENTS 2.1 -- reality won): the spec's A7/A8 wording
  presumes FRIEND/ENEMY sides the data model never defined (no TeamId anywhere; the
  cluster records form at contact range, never at the 25 m skirmish break distance, so
  records cannot drive either rule). Minimal honest addition: `FUnitSpawnSpec.TeamId`
  (uint8, default 0), carried in `SpecsByIndex` (no new registry state), read by the
  ranged pass ONLY -- melee stays intent/record-driven and never reads it (A12
  preserved). Save v2 gained `FUnitSaveEntry.TeamId` (a restore that dropped it would
  merge the battle's sides); the spec's v2 field list gets the addition called out in
  the report. Friendly = same team; skirmish threat = nearest team-different unit
  anchor (unit-level geometry, F12-compliant -- never a per-entity scan).
- [task4.2] `FUnitRuntimeState` += transient skirmish state (RC7 serial guard):
  `bRetreating` + `AutoRetreatSerial` (NOT saved -- a restored battle resumes
  evaluation from idle). `Registry::ApplyUnitSkirmishDecision(Handle, Decision,
  RetreatAnchorCm, Facing)` implements the guard: retreat issues only for an idle
  (None/Stop) unit or refreshes while the move is still OURS; a player serial bump
  cancels and is never overridden; Fire past the resume distance stops OUR retreat.
  EXEC lines: `SkirmishRetreat/SkirmishResume/SkirmishCancel`.
- [task4.3] Seam adjustment (1 line, disclosed): the flat-lane corridor test now skips
  a blocker coincident with the shooter's own position (mm tolerance) -- the shooter's
  body sits at the segment start and would otherwise hold every flat shot forever.
- [task4.4] `URangedFireProcessor` (new `RangedFireProcessor.h/cpp`): game-thread
  (ADR-28), group "Battle", after AssignPairs / before FormationSlotResolve (A4);
  `ShouldAllowQueryBasedPruning -> false` (the task-3 engine note). The core is the
  EXPORTED `RunRangedFirePass(World, DT)` (the exec fixtures + unit tier drive it
  without a Mass tick; the processor is the per-tick shim). Per tick: pass-level
  anchor + record-enemy maps (sorted order, deterministic); per ranged unit --
  reload advance (weapon clocks tick target-less), roster-order shooter views (rank =
  SlotIndex/NumFiles), A11 target (order target, else record enemy; none = HOLD),
  A7 blockers via the sanctioned hash circle queries (arced: impact-zone radius;
  flat: corridor midpoint; same-team bodies), the seam call, writeback (accounting
  IS the shot), pool writes with the impact attack term stamped at fire
  (RangedAttack x RangedAttackMult x AccuracyMult, spec 4.6), trajectory overlay
  segments, out-of-ammo flip at aggregate zero + the A14 UI mirror
  (`MirrorAmmoPoolToGas` writes the unit actor's AmmoPool attribute here -- spec 3.2
  assigns the mirror to UnitSync; the fire pass is the interim writer until Task 8
  wires the sync path, disclosed). File-static GT-only scratch (EC9: allocated once,
  Reset per pass -- no steady-volley allocation).
- [task4.5] Metrics: `BattleMetrics` += `FireMs=%.3f Shots=%d Holds=%d` (appended,
  F14 rule); Holds = cheap-eligible shooters that did not emit -- the R-R6
  rule-vs-bug discriminator; ResetDiagnostics covers the new counters.
- [task4.6] ExecBridge: `Battle.Ranged.SpawnVolley [line|skirmish]` -- resets the
  battle, flips the session flags (combat pipeline + the ranged block,
  ECVF_SetByCode), spawns archers vs a formed line at 80 m on OPPOSING teams, issues
  the attack (the skirmish variant is Task 6's M4 extension). Evidence flows from the
  live sim: RangedFire/RangedDump/OutOfAmmo/Skirmish lines.
- [task4.7] `Battle.Unit.Ranged.FirePass` (MassBattleCombatTests.cpp): three live
  flows on real spawned worlds. Flow A (RC1/RC5): attack-ordered archers -> one volley
  of 40 (shots == roster, ammo 25 -> 24 each, reload armed, pool live 40, stamped
  attack term 6.0), reload cycle suppresses the second pass, drain flips out-of-ammo
  once and the spent unit never fires again, flags-off pass is a no-op. Flow B (A7):
  a friendly unit inside the arced impact zone -> 0 shots, rule-holds == 40 (R-R6
  evidence). Flow C (RC7): retreat triggers inside 25 m (Move away, anchor -50 m,
  facing held), no churn while retreating, resume stops past 32.5 m, the threat's
  return re-retreats, and a player Move cancels + is never overridden.
  Two test-side defects found and fixed en route: flows A/B initially omitted the
  attack order (the pass correctly HELD per A11 -- the fix was the fixture, not the
  code), and flow A's flags-off check left Enable=0 for flows B/C.
- [task4.8] Build -> Succeeded. Unit tier: **passed=78 failed=0 total=78
  completed=True exit=0 ok=True** (+1 = FirePass). Func tier: **passed=19 failed=4
  total=23**, counts agree, the exact baseline -- the fire processor rides the live
  Battle group with plugin defaults OFF (byte-identical melee, R-R8).
- [task4.9] Acceptance evidence (verbatim, `Tools/CI/last_run/unit.log`):
  `EXEC_RESULT RangedFire Unit=1:1 Shots=40 Holds=0 AmmoSum=960 PoolLive=40` /
  `EXEC_RESULT OutOfAmmo Unit=1:1 Roster=40 InitialAmmo=25` /
  `EXEC_RESULT SkirmishRetreat Unit=1:1 Anchor=(-50.0,0.0)m Facing=0.0 Serial=1` /
  `EXEC_RESULT SkirmishResume Unit=1:1` /
  `EXEC_RESULT SkirmishRetreat ... Serial=3` /
  `EXEC_RESULT SkirmishCancel Unit=1:1 PlayerOrderSerial=4`.

## Open items carried

- S4_ShieldWall / S5_Mob melts: pre-existing, owned by the phase-2 dossier's deferred
  decision; untouched this phase.
- Skeleton-red func failures: by design until Task 6.
