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

## Task 5 -- impact resolution (RC6) + Enable flip

- [task5.1] The step pass became the stepping + impact pass:
  `RunProjectileStepPass(World, DT)` (exported; the processor is the per-tick
  shim). Per tick: pre-step position snapshot (swept segments), the seam's
  `StepPool`, then per survivor the R-R2 impact query -- ONE midpoint circle
  (radius = half-segment + HitRadius + body radius), the height gate BEFORE
  the query, the height at the closest-approach parameter (not the midpoint:
  a body past this tick's segment end reads its true flyover height),
  team-blind nearest body. `BattleMetrics` += `Impacts=%d FriendlyHits=%d`.
- [task5.2] The transient one-roll context (spec 4.6): incoming angle ->
  victim-frame byte (the ContactAngle convention) -> the shared
  `SectorIndexFromAngle` -> melee arc bands -> `HitChance(attack, defense)`
  -> `CombatRoll01(shooterIndex, shooterSerial, shotsFiredAtEmission)` ->
  `Roll < P x Lethality`. Pool columns added (disclosed pool-layout growth):
  `ShooterRollCount` (the A13 roll count; `FVolleyShot.ShotsFired` carries it
  from the seam) + `Lethality` (the shooter's, stamped at fire -- a
  projectile has no chunks). Landed: the victim's `FHealthFragment.MountCurHP`
  drains first; at 0 the kill defers `FDeadTag` through `EM.Defer()` -- F2's
  death path, unchanged. `EXEC_RESULT RangedKill ... Sector=.. Friendly=..`
  is the ChargeMechanics-precedent evidence line.
- [task5.3] Two impact-pick corrections found by the first Impacts run
  (verbose diagnostics, `Tools/CI/impacts_verbose.log`):
  (a) the rank-2 arrows GRAZED their own front rank (solved clearance 0.22 m
  at the body plane, inside the 0.5 m hit radius -- 20 of 40 impacts were
  own-unit rear hits, 17 of them landed kills). The seam's RC2 rule lets
  arced volleys fire OVER friends, so the pick now EXCLUDES the shooter's own
  unit; other friendly units still take the strays (RC4's team-blind half,
  flow B). (b) Corpses never roll (the melee resolution's own query rule):
  a killed-this-pass body stays addressable in the last-tick hash while its
  FDeadTag is DEFERRED-invisible, so a killed-ledger TSet guards double
  kills; it clears when the pool batch empties (per-volley semantics).
- [task5.4] `Battle.Ranged.Enable` default 0 -> 1 (ground rule 7). The func
  tier proves melee unregressed WITH the ranged passes live: **passed=19
  failed=4 total=23**, the exact baseline (CombatSmoke + ChargeMechanics
  green; the two by-design skeletons + the two phase-2 melts are the reds).
  Identity mechanics: no ranged units -> the fire pass iterates and skips;
  empty pool -> the step pass early-outs (R-R8).
- [task5.5] `Battle.Unit.Ranged.Impacts` (MassBattleCombatTests.cpp), three
  flows: (A) 40-arrow volley vs a formed frontal line -> 25 impacts, kills in
  the golden band around Impacts x HitChance(6, defense 2) x lethality
  (band +-55% guards cross-machine float drift on the DETERMINISTIC roll
  sequence); DeathsByArc ledger == the deferred-dead count, frontal rows
  populated; (B) the aimed over-flight stray: a shot climbing over the enemy
  line descends onto the friendly line behind -> FriendlyHits > 0 and
  `RangedKill ... Friendly=1` (the team-blind impact-point half; the
  estimated-zone hold kept the volley itself legal); (C) the composite-HP
  ladder on aimed shots: mount pool 2 -> drain -> drain -> kill, each stage
  asserted with no statistics.
- [task5.6] Facts surfaced by the run (AGENTS 4): the spawn library never
  stamps `FBaseStatsFragment.{Lethality, Shield}` (defaults 1.0/0.0) -- the
  EDU mapping fills `OutStats` but the spawn payload skips both fields; the
  band asserts read the fragment's ACTUAL values, and the T6.1 constants
  audit owns the payload completion. Also `Battle.Combat.SectorCount` (8)
  is the ranged sector home -- one quantizer, two consumers as spec'd.
- [task5.7] Build -> Succeeded. Unit tier: **passed=79 failed=0 total=79
  completed=True exit=0 ok=True** (+1 = Impacts). Kill-chain evidence
  (verbatim, `Tools/CI/last_run/unit.log`):
  `EXEC_RESULT RangedKill Shooter=1:1 Unit=2:1 Victim=55:1 Sector=0 Friendly=0`
  / `EXEC_RESULT RangedKill Shooter=2:1 Unit=3:1 Victim=110:1 Sector=4
  Friendly=1` (10 RangedKill lines total across the flows).

## Task 6 -- scenarios S6_Volley + SkirmishBehavior (T3.4/T3.5)

- [task6.1] RC7 "resumes fire" completed: `FUnitRuntimeState.RetreatingFireTarget` --
  the attack aim the retreat's Move displaces is retained by `ApplyUnitSkirmishDecision` (stamped at retreat from an
  AttackUnit/AttackMove order, kept at resume, cleared on player cancel) and the fire
  pass reads it as a target fallback. The retreat guard now DISPLACES attack
  approaches (idle/Stop/AttackUnit/AttackMove are displaceable; player Move/Garrison
  never), and the resume keys on "not Retreat" (past the hysteresis band) rather than
  "Fire" -- an out-of-ammo skirmisher must still stop retreating when clear (found by
  the first SkirmishBehavior run: the archers drained to 0 and retreated forever).
- [task6.2] The HoldFire phase exposed a REAL contract gap: the fire pass never gated
  on `bFireAtWill` -- hold-fire did not suppress shooting. Gate added after target
  resolution (reload clocks still advance; skirmish still evaluates; A11).
- [task6.3] `AUnitActor.TeamId` (editor-visible, 0 default) stamps the spawn spec --
  scenario units declare sides through the actor.
- [task6.4] `AS6VolleyTest` (T3.4) replaced: six-phase machine (Spawn/Volley/
  FlatLane/Hill/Stray/Drain) -- dispersion bound sampled per-shot against each
  shooter's own aim axis (4.44 deg vs the 4.5 bound), the flat-lane friendly hold
  (24 shots frozen with a friendly on the corridor), the hill variant (flat blocked,
  arced passes: 440 archer shots), the deterministic team-blind stray
  (`RangedKill Friendly=1`), and the drain (out-of-ammo at exactly 16x25=400 shots,
  fraction 0, flipped once). `ASkirmishBehaviorTest` (T3.5) replaced: seven-phase
  machine -- break at 25.5 m (serial 2), firing-while-retreating, resume at 36.9 m
  (band 32.5, FireTarget retained), 12 s hysteresis stability, hold-fire retreats
  with shots frozen at 200, and the player-order override (cancel + never
  re-retreated). Scenario asserts are CompleteTest(Error) failures (actors are not
  automation tests); cvar save/restore incl. the ranged block + ReloadInterval 0.5 s.
- [task6.5] THREE fixture-vs-rule lessons from the verification runs (each measured,
  logged, and the FIXTURE corrected -- the rules were right every time):
  (a) the flat-lane friendly must sit ON the live corridor: a full-speed attack
  approach drifted ~5 m off the nominal lane in 3 s and the corridor legitimately
  passed below a nominally-placed friendly (body-circle rule; RangedLeak MinDist
  diagnostics) -- the crossbows now creep at 1 m/s AttackMove (A4 pre-designated
  aim) and the friendly spawns at 40% of the LIVE corridor with 16 men, whose
  frontage guarantees every shooter's crossing is inside it (the first placement
  put one body at EXACTLY the 1.30 corridor half-width and the boundary float rode
  an ulp); (b) the hill stamps on the live corridor beyond the friendly; (c) the
  hold watch arms 1 s after the spawn (same-tick zero-reloads raced the fire pass
  against the stale hash: 2 shots slipped through before the friendly was hashed).
- [task6.6] `Battle.Ranged.SpawnVolley` gains the `skirmish` variant (dictionary-keyed
  arg): threat approaching, archers attacking -- the skirmish machine runs live. The
  M4 gray-box sample registers on the demo game mode behind the no-level-units gate
  (the demo map now has 5 level units, so the gate correctly skipped auto-register;
  the fixture itself is the M4 deliverable and was driven headless via -ExecCmds:
  `SpawnVolley OK Mode=skirmish` + continuous RangedFire lines +
  `SkirmishRetreat Unit=1:1 Anchor=(-87.5,0.0)m Facing=0.0 Serial=2 FireTarget=2:1`
  in `Tools/CI/m4_boot2.log`; arrows are the overlay segments until PR-8).
- [task6.7] Fire-pass skip diagnostics (Verbose, off in CI): `RangedSkip
  Reason=NoTarget|Blockers`, `RangedLeak` per emitting shot with blockers present.
- [task6.8] Func tier: **passed=21 failed=2 total=23** -- S6_Volley + SkirmishBehavior
  GREEN (in the PR set), the only reds the two pre-existing phase-2 melts
  (S4_ShieldWall, S5_Mob). Unit tier: **passed=79 failed=0 total=79**. Evidence
  (verbatim above in the run log; key lines): `S6_Volley PASS dispersion=4.44deg
  impacts=44 friendlyHits=1 fired=400` / `SkirmishBehavior PASS
  break/resume/hysteresis/hold-fire/override all held; final dist=5.5m`.

## Task 7 -- determinism, perf gate (T3.6), soak variant, re-baselines

- [task7.1] T3.6 PERF GATE GREEN: `Battle.Ranged.PerfStress` (two 500-man archer
  units at 100 m, mutual attacks, VolleyRanks 20 for the fixture session, the ranged
  perf probe at tick ~12 s) measured on the reference machine -- verbatim
  (`Tools/CI/t36_perf.log`): `EXEC_RESULT RangedPerf InFlightNow=549
  InFlightPeak=1091 StepMs=0.063 FireMs=0.174 RangedMs=0.236 BudgetMs=0.300
  PoolDrops=0 Verdict=PASS`. The exit criterion (1,000 arrows in flight < 0.3 ms
  sim) is MET with headroom; zero pool drops (no saturation). Metrics:
  `BattleMetrics` += `InFlightPeak` (both ranged passes sample; ResetDiagnostics
  covers it); the T2.9-pattern probe (`ArmRangedPerfProbe`) dumps the verdict line.
- [task7.2] Spawn re-baseline: 80-entity ARCHER spawn measured warm at **47.4 us**
  (`Battle.SpawnRoster 2 80`, `Tools/CI/spawn_rebase.log` -- the ranged fragment
  present); the reference baseline updated (`entity_spawn_80_us` 4.8 -> 47.4,
  disclosed re-sign: the old value predates the ranged fragment and the current
  measurement definition; the nightly compare runs against the updated reference).
- [task7.3] Soak volley variant: `Battle.Ranged.SoakVolley=1` rides the movement
  soak -- every 4th unit (~25%) spawns as the ARCHER row on team 1 (the rest team
  0) with an immediate AttackMove at the field center; the FINAL line gains the
  APPENDED counters `volleys=%d inFlightPeak=%d friendlyHits=%d poolDrops=%d`
  (F14: existing fields never move; flag off prints 0/0/0/0 -- byte-identical
  positions). Staged evidence: the nightly's flag-off soak (identity) + a staged
  flag-on run (below).
- [task7.4] RC8 two-run extension -- PARTIALLY LANED, DIVERGENCE OPEN: the
  mixed clash+volley fixture (24+24 infantry, 16+16 archers, teams 0/1, mutual
  attack orders) now replays identically in both runs from the same RNG root
  (the phase-2 gate hashed an EMPTY world -- DestroyOtherUnitActors cleared the
  map units and nothing re-spawned). The extension exposed FIVE genuine
  run-sensitivity defects, all fixed (permanent hardening):
  (a) the state hash's pair-target term folded raw FMassEntityHandles --
  canonicalized to (owner unit, roster index);
  (b) run 1 created the ranged compositions while run 2 reused them (the
  archetype-count term) -- an archetype warm-up spawn/teardown precedes run 1;
  (c) reload staggers, shot seeds, and combat rolls keyed on raw entity
  handles -- `BattleCanonicalEntityKey`/`BattleCanonicalCount` (BattleTypes.h,
  the A13 one-home) now feed every hash-family call site (spawn stagger, assign
  stagger, shot seed, melee roll, impact roll);
  (d) the avoidance deflection summed neighbors in chunk order and compared raw
  handle parity/asymmetry INSIDE the physics -- canonical sort + canonical keys
  (FAvoidanceNeighbor.CanonicalKey);
  (e) the failed-seek backoff drew the shuffle stream per seeker in chunk order
  -- canonical per-entity jitter (the stream stays for the defender-pick
  shuffle, whose consumption is count-deterministic).
  RESIDUAL (blocker, disclosed): after all five fixes the two runs still diverge
  (40/40 samples) by a sub-microscopic servo transient -- pinpointed by a
  per-tick field trace (`Tools/CI/det_trace*.log`) to entities 8/11 of ONE unit,
  first visible at tick 14 (vel Y 1.5e-3 cm/s; positions identical through
  tick 13 at 1e-6 m print precision), during the approach march with all
  printed state (pos/target/vel/block facing/anchors/slots) otherwise equal.
  The seed sits below the trace resolution in the servo's float pipeline.
  Per AGENTS 3 (distinct approaches exhausted) the gate ships RED with the
  dossier in `Tools/CI/det_bisect*.log`/`det_trace*.log`; the melee-only two-run
  remains green-empty as before. Next session: full-precision (hex-float)
  tracing of the servo's DesiredVel accumulation at ticks 11-14.
- [task7.5] Staged nightly: run record below (the staged build + smoke + flag-off
  soak + baseline compare + commit; the soak VARIANT evidence rides the staged
  exe with the cvar set).

- [task7.6] Two marginal func gates tipped by the canonicalization (disclosed, owner
  calls): (a) `Battle.Func.S4_WidthOverride` now reports `separation 0.285 m` at its
  refront gate -- the avoidance deflection's summation order changed (the canonical
  sort IS the determinism fix; the old order was the pre-reset chunk order), moving
  the float result within the gate's noise band. The sim remains deterministic --
  just different-but-equal floats. (b) `Battle.Func.DeterminismTwoRun` (above).
  Func tier: **passed=19 failed=4** (DeterminismTwoRun + S4_WidthOverride tipped +
  the two pre-existing melts S4_ShieldWall/S5_Mob). Unit tier: **79/79** (the servo
  tie-break unit test updated to the canonical-key semantics -- same 4:1 ratio).
- [task7.7] Nightly attribution: the staged nightly failed at the movement soak's
  PHASE-2 DRESS gate (`dressErr=30.25/6.0m ok=0`, flag off, volleys=0). Attribution
  run with the task-7 changes STASHED (commit 937a2ba rebuilt): the baseline fails
  IDENTICALLY (`dressErr=33.76/6.0m ok=0`, `Tools/CI/soak_base.log`) -- the failure
  is PRE-EXISTING since the last staged nightly (2026-10-26, the garrison phase;
  the engagement + ranged phases never ran staged). NOT a task-7 regression; the
  dress-gate investigation belongs to the phase-2 dossier (the staged soak gap the
  phase-2 report flagged). The nightly's baseline-compare + commit steps did not
  run; the spawn reference was updated manually (task7.2) and stands.

## Open items carried

- S4_ShieldWall / S5_Mob melts: pre-existing, owned by the phase-2 dossier's deferred
  decision; untouched this phase.
- Skeleton-red func failures: by design until Task 6.
