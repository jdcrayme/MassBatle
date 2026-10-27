# MassBattle Phase 2 Brief -- Engagement Solver & Melee (Formed Clash and Mob From One System)

| | |
|---|---|
| **Companion docs** | `Docs/spec/engagement-and-melee.md` (design spec: §0 EC-table behavior contract, §3 data model, §4 mechanics, §6 touch-point table) · `Docs/AGENTS.md` (standing protocol) · `Docs/spec/roadmap-and-testing.md` (Part A Phase 2, T2 gate table, B.0 test contract, B.8 DoD) |
| **Engine pin** | UE 5.8.4 (ADR-13); one new engine API territory expected (StateTree -- §4 digest + Task 1 audit) |
| **Run mode** | Single autonomous run, protocol per `Docs/AGENTS.md`; all CI evidence via `Tools/CI/run_tests.bat {unit,func}`; nightly via `Tools/CI/nightly.bat` |
| **Priority directive** | The combat heart: spatial hash, engagement records, tally-assign, exposure budgets/coverage, press-ring waiters, pair lifecycle, melee resolution with directional defense, charge state machine -- formed clashes and mobs from ONE system. Plus the roadmap's Phase 2 side-quests: StateTree prototype ADR (I4), save/load skeleton (R7), M3 demo geometry-only (R6). Ranged is Phase 3; morale/rout is Phase 4; melee-vs-garrison is out of scope (A9) |
| **Owner-approval status** | §3.1 decisions are PROPOSALS -- the owner marks them approved before this brief freezes; spec §8 ADR-22..26 are proposals pending the owner's architecture-log commit |
| **Sequencing** | Executes after phase garrison (complete 2026-10-26). Rationale: `Docs/spec/engagement-and-melee.md` §9 |

---

## §0. Mission + read order + token discipline

**Mission.** Implement the engagement solver and melee so that scenario tests S4/S5 pass and the exit criteria read: 2 knights vs 40 peasants ends with the knights pulled down by mob geometry, not special-case code; a shield wall holds at FormedSlots then melts as coverage drops. One system produces both behaviors: exposure-driven attack-slot budgets over registry-owned engagement records, per-entity pair fragments, and an attacker-authoritative resolution pass on sim timers. The full behavior contract is the spec's §0 EC-table (EC1-EC9); the architecture claim is one sentence: **engagement is registry-owned bookkeeping over unit geometry, and melee is one worker-side resolution pass -- a combat extension of the movement spine, not a new system.**

**Read order** (token discipline: nothing else; do not read the spec docs end-to-end):
1. `Docs/README.md` status block, then `Docs/AGENTS.md`.
2. This brief, all sections.
3. `Docs/spec/engagement-and-melee.md` -- §2 (architecture summary), §3 (data model), §4 (mechanics), §6 (touch-point table) are the implementation contract; §0 EC-table is the acceptance contract.
4. Roadmap Part A "Phase 2 -- Engagement solver & melee" + the T2 gate table; B.0 (test-mode contract), B.5 (determinism hooks), B.8 (capability DoD) -- cite, do not restate.
5. `Docs/spec/architecture.md` §7 and §9.1-9.2 ONLY as the parent design the spec elaborates (conflicts: architecture wins, report per AGENTS §2.1).
6. Source, in this order: `Plugins/MassBattle/Source/MassBattle/Public/BattleMovementFragments.h` -> `Private/ServoProcessor.cpp` -> `Public/BattleFragments.h` + `Public/BattleTypes.h` -> `Public/BattleRegistry.h` -> `Private/BattleExecCommands.cpp` -> `Private/BattleStateHash.cpp` -> `Public/BattleRngRegistry.h`.
7. `Docs/phases/phase-garrison/report.md` §4 ("Where Phase 2 should start") -- ready hooks and re-baseline reminders. `Docs/phases/phase-1/report.md` §4 if a movement detail surprises you; report a discrepancy per AGENTS §2.1 instead of re-deriving.
8. `Docs/reference/engine-api-5.8.md` -- Part B §2-§3 (processor + command-buffer facts) only; StateTree facts do NOT exist there yet (§4 below plans their audit).

**Do not** open `Docs/reference/engine-api-5.8.md` or `Docs/reference/mcp-playbook.md` end-to-end. If an engine symbol misbehaves, follow AGENTS §4 (correct the owning reference, log it).

## §1. Phase facts digest (pre-verified 2026-10-27 against the repo, reports, and spec docs)

Every fact below is verified against the cited source in this repo (working tree at phase-garrison complete). Cite fact IDs in the progress log instead of re-reading.

- **F1** Servo states exist with combat's rows already weighted (phase-1 report §4): `EBattleMovementState` has 6 rows including `PressRing` (RVO within mob radius only) and `Charge` (no avoidance); weights live in `Private/ServoProcessor.cpp`. `FServoStateFragment::DisplacementWeight` is the shove/knockback displacement channel. `FUnitFormationSharedFragment` (`Public/BattleMovementFragments.h`) carries anchor/path/speed-limit the solver keys on.
- **F2** Death path (Phase 0): kill = `FDeadTag` REGULAR tag add via command buffer (ADR-15; sparse tags reverted); flush contract per engine-api Part B (per-processor-scope flush; "same tick" = before the next processor in the same phase observes state). Death signal -> `UUnitRegistrySubsystem::HandleUnitDeath` on the game thread (`Public/BattleRegistry.h`) is the single registry cleanup hook (garrison F14 pattern -- pair cleanup hooks here).
- **F3** `FOwnerUnitFragment` = unit handle + roster index, restamped on death (phase-0 audit M5). `FBaseStatsFragment` carries EDU base stats (`Public/BattleFragments.h`); `BattleEduParser` is the parser home. Field coverage check (weapon class, AP flag, lethality, stat_charge) is Task 2 step 1 -- extend the parser only via the disclosed T0.2 golden re-sign policy.
- **F4** Order surface pattern: `FUnitOrder` thin payload {EType, AnchorPos cm, FacingYaw deg, CommandedSpeed, ...}; `IssueMoveOrder` / `GetOrderSerial`; serial bump = change detector consumed in `UnitLocomotionProcessor::Execute` (garrison F2). ExecBridge emits `EXEC_RESULT ...` evidence lines (`Public/BattleExecCommands.h` contract); `Battle.Order.Move` is the worked example.
- **F5** State-hash append pattern: garrison terms fold after formation terms over `GetSortedHandles()` (`Private/BattleStateHash.cpp`); combat terms append after garrison terms, position-stable, same sorted order (spec §3.4).
- **F6** Archetype count 6 constant through the garrison phase (both soak FINAL lines). Tags flip archetypes; Phase 2 adds ZERO tags -- pair/charge state are fragment fields (spec §3.1). ADR-17: C++ archetype-first spawn via `UBattleUnitTrait::BuildTemplate` is the only unit spawn path.
- **F7** `UBattleRngRegistry` (`Public/BattleRngRegistry.h`): seeded per-stream registry (roadmap B.5). The assign-pass shuffle/jitter draws from a registered stream (`Combat.Assign`); combat rolls from `Combat.Roll`. Never `FMath::Rand`.
- **F8** Test contract (roadmap B.0): `UBattleTestSettings::ApplyTestMode()` / `AssertTestMode()` at every S-scenario start; naming `Battle.Unit.*` (per commit) and `Battle.Func.*` (per PR); scenarios on `L_MassBattle_T0_Functional` (no new maps); harness pattern `MassBattleFuncTestHarness.cpp`; soak = `BattleMovementSoakController` (Gauntlet, staged build, `Tools/CI/nightly.bat` step 3b). Test sources live in `Plugins/MassBattle/Source/MassBattleTest/` behind `#if WITH_DEV_AUTOMATION_TESTS`.
- **F9** Unit conventions: fragment boundary = centimeters, seam math = meters, convert exactly once at the boundary; ASCII + LF, `--` not em-dashes (AGENTS §3); commits prefix `phase2:`; processors `ExecuteInGroup = "Battle"`, game-thread ones set `bRequiresGameThreadExecution = true` + `EProcessorExecutionFlags::AllWorldModes` (engine-api Part B §2-§3).
- **F10** Debug subsystem: `UBattleDebugSubsystem` (`UTickableWorldSubsystem`) with a thread-safe overlay line queue gated by `Battle.Debug.ShowOverlay`, metrics via `Battle.DumpMetrics`, `ResetDiagnostics` (phase-1 A3 pattern). Roadmap B.2's combat rows (pairing lines with ContactAngle + state colors, per-defender tally vs budget, coverage heat, record debug) extend this framework.
- **F11** Garrison exclusions (garrison report §4.2): `GarrisonMode != None` units are excluded from OBB contact collection and never initiate moves. Phase 2 extends the exclusion: garrisoned units are not valid melee targets and do not seek pairs (A9; extension point recorded in spec §10).
- **F12** Nightly/baseline machinery: `Tools/CI/nightly.bat` runs smoke + movement soak on the staged build; baselines in `Baselines/perf` compared by `compare_baseline.py`; the garrison-phase baseline was re-committed (`fdc4813`); `entity_spawn_80_us` re-baseline was deferred to Phase 2 (garrison report §4.3) -- Task 9 executes it with garrison fields AND combat fragments present.
- **F13** No engine spatial structure is being reused: engine Mass avoidance is force-based Experimental (arch R2, rejected by phase-1 A1); the project hash is new code (spec §3.3), not a port. Engine Mass Crowds/ZoneGraph stay unlinked.
- **F14** CI tier availability (roadmap B.0 table): the full `Battle.Func.*` PR gate becomes available FROM this phase; nightly `Battle.Soak.*` / `Battle.Fuzz.*` / `Battle.Perf.*` likewise. The combat soak variant rides the existing nightly loop behind `Battle.Combat.SoakClash` (default 0).

## §2. Pointers

MCP/editor automation: operating rules live in `Docs/reference/mcp-playbook.md` (check its section 8 correction ledger before rediscovering anything). This phase is planned MCP-light: all fixtures are C++/ExecBridge-authored (F8; the garrison brief's F15 fixture precedent), all evidence via `run_tests.bat`; open editor sessions only if a map edit proves unavoidable, and batch file-side work around it (AGENTS §3).

## §3. Phase ground rules + owner decisions + risk watchlist

### 3.1 Owner-decision table (PROPOSALS -- owner marks approved before the run; implement as written, never re-litigate)

| ID | Decision | Choice |
|----|----------|--------|
| A1 | Combat seam modules | Two new B.0 seams: `BattleEngagement.h/cpp` (solver math: budgets, coverage, sectors, tally, assign, pressure) and `BattleMelee.h/cpp` (resolution math: hit chance, arc bands, AP, clamps, attack cycle, charge bonus). Free functions, meters, deterministic; RNG only via registered streams (F7). Processors stay thin shims |
| A2 | Fragment home + archetype discipline | New `BattleCombatFragments.h` (`FCombatIntentFragment`, `FCombatPairFragment`, `FChargeFragment`); ZERO new tags, ZERO new shared fragments; archetype count target stays 6 (F6; composition grows, count does not) |
| A3 | Processor additions | Per arch §4 rows: `BuildSpatialHashProcessor`, `EngagementClustersProcessor` (4 Hz), `TallyProcessor`, `AssignPairsProcessor` (game thread, after `UnitLocomotion`, before `FormationSlotResolve`), `ChargeProcessor`, `CombatResolutionProcessor` (worker, after `Integrate`). Each behind a `Battle.Combat.*` CVAR gate (B.8 item 6). `RangedFire` is Phase 3 -- do not build it |
| A4 | Order surface | `FUnitOrder::EType` += `AttackMove`, `AttackUnit` (payload += target unit handle); registry APIs `IssueAttackUnitOrder` / `IssueAttackMoveOrder`; ExecBridge `Battle.Order.AttackUnit` / `Battle.Order.AttackMove`. Disengage = re-task with Move/Stop while engaged (records die -> mass release); `Withdraw` flavor + `OrderLatency` are Phase 4 |
| A5 | Spatial hash | `UBattleSpatialHashSubsystem`: 3 m cells over the 256 m tile, counting-sort rebuild from end-of-last-tick transforms, preallocated arrays (zero per-tick alloc). `QueryCircle` for coverage/mob targeting ONLY; formed-fight targeting reads defender slot tables (spec §3.3) |
| A6 | Formula constants | The community model's STRUCTURE ships (contested A-vs-D, directional shield, AP halves armor, lethality, clamps -- spec §4.8); CONSTANTS are placeholders, golden-locked at T2.1, swapped only by the Phase-6 T6.1 audit with diff review (spec ADR-26 proposal) |
| A7 | StateTree prototype (I4) | Half-day HARD timebox, behind `Battle.Combat.StateTree` (default 0); verdict recorded as an ADR either way BEFORE combat wiring accretes (spec §4.10 criteria list); the run does not exceed the timebox to save the prototype |
| A8 | Save/load skeleton (R7) | Schema-versioned `FBattleSaveData` v1: registry snapshot + per-unit shared-fragment payloads + RNG stream states; entities rebuilt from spec via `BattleSpawnLibrary` (ADR-17). Skeleton round-trip asserted at registry level only; full resume is T6.3 by design (spec §3.5) |
| A9 | Melee-vs-garrison | Out of scope: garrisoned units are not valid melee targets and do not seek; engagement records skip them (F11). Extension point recorded in spec §10, not built |
| A10 | M3 demo | Geometry-only this phase (R6): the mob scenario IS M3; demo sample registered by the game mode + ExecBridge fixtures. The morale beat re-runs enriched after Phase 4 |
| A11 | Rout behaviors | `FRoutingTag` chase/rear-arc/budget-bypass are Phase 4; Phase 2 pair lifecycle ships death/stale/distance/disengage rules only (spec §4.7). Do not special-case routers |
| A12 | Servo mapping | Pair states drive movement states: Seeking -> `CrossField` toward press point; Pressing -> `PressRing`; Striking/Recovering -> hold at contact (HoldSlot weights, compression owns crowding); Charging -> `Charge`. Knockback consumes the existing displacement blend (F1). `ServoProcessor` is the ONLY existing movement processor that gains pair-state consumption |
| A13 | Iteration determinism | Assign-pass seeker iteration shuffled per tick from stream `Combat.Assign`; the B.5 compile-time determinism flag switches to index-sorted iteration with per-entity jitter. Rolls draw from `Combat.Roll`. Both streams seed-registered per battle |
| A14 | Mob pressure floor (T2.5 resolution) | Mob-mode `DesiredPressure` = `Min(attackerAlive, Max(defenderAlive x MobPressureFactor, MaxMobSlots))` (spec §3.2). The `MaxMobSlots` floor resolves the tension between architecture §6.3's `defenderCount x 2-3` cap and roadmap T2.5's "isolated knight takes >= 4 exposed-arc attackers": with 1-2 defenders the unfloored cap admits only 2-6 attackers total and starves the mob test. Large units unaffected (attackerAlive binds first). Amendment recorded for the owner's architecture-log reconciliation (spec §8) |

### 3.2 Phase ground rules (additions to AGENTS.md only)

1. All combat math lives in the two seam modules (A1) -- B.0's review flag applies: math inside a `ForEachEntityChunk` lambda is a refactor flag, every task.
2. Game-thread bookkeeping (hash build, clusters, tally, assign) never allocates per tick in the steady fight: counting sort, preallocated scratch, open-address tables (EC9). Arrays grow at battle start only.
3. Worker processors (charge, resolution) write only per-entity fragments; no subsystem access worker-side; access requirements fully declared (access-detector clean, CI runs dev builds to surface it).
4. Pair/charge transitions are NOT logged individually (too hot): metrics carry churn rates; the overlay shows live state; EXEC_RESULT evidence lines are event-rate only (scenario results, dump probes, charge connects sampled).
5. State-hash terms append after garrison terms (F5); mutation sensitivity asserted with the pair tests.
6. Combat CVARs default OFF until their owning task lands (B.8 item 6); the flags-off path is byte-identical to the pre-combat baseline and the unchanged T1.8 soak proves it.
7. Spec doc sections are read-only (AGENTS §7): proposed patches only where this brief authorizes them (Task 1's StateTree ADR content, Task 10's B.7 entry); disclosed in the report.
8. `Battle.Combat.Enable` flips to default 1 only when Task 7's resolution pass lands end-to-end (the same task that makes combat observable in scenarios).

### 3.3 Risk watchlist

| ID | Risk | Mitigation / watch |
|----|------|--------------------|
| R-C1 | Pair churn thrash (weak hysteresis -> assign/release storms) | `PairHysteresis` 0.3-0.5 s band; pair churn/sec + re-pair latency metrics (B.4); T2.7 asserts breakup behavior |
| R-C2 | Assign pass cost at 1k-in-contact exceeds the 0.5 ms gate | Counting sort + open-address scratch + coverage cache scoping (spec §4.3); lane scans read slot tables, never circle queries (A5); per-cluster sharding is the recorded escape hatch (spec §10) |
| R-C3 | Mob degenerate cases: carousel waiters, single-defender pile-ups, starvation | Press-ring discipline (spec §4.6) + exposed-sector preference + jittered backoff + the A14 pressure floor; S5 asserts waiters at ring, no carousel, >= 4 exposed-arc attackers |
| R-C4 | E1-class shared-fragment contamination via combat data | Engagement state is REGISTRY-owned scratch + per-entity fragments; no new shared fragments (A2); any per-unit combat broadcast is a flagged design change |
| R-C5 | Determinism leaks: iteration order, unregistered RNG, wall-clock | F7 streams + A13 discipline; B.5 determinism mode; T2.8 two-run hash on a 5-min clash, same binary/machine (R9 scope) |
| R-C6 | Mass-death archetype churn during a rout-scale collapse interacting with pair cleanup | ADR-15 deferral note: one archetype per unit localizes flips; pair release rides validation (top-of-pass) + `HandleUnitDeath` registry cleanup (F2); watch archetype-count metric during S4 melt |
| R-C7 | StateTree prototype scope creep | Hard half-day timebox (A7); prototype scope = 4 states, one unit type, headless (spec §4.10); failure evidence IS the deliverable if rejected |
| R-C8 | Save skeleton overreach | Registry-level round-trip only (A8); entity rebuild asserts nothing about pairs/corpses -- T6.3 owns maturity |
| R-C9 | Combat interfering with the movement baseline | A12 keeps `ServoProcessor` the only consumption point; flags-off T1.8 identity asserted; soak with clash mix behind `SoakClash` runs alongside, never replacing, the baseline variant |
| R-C10 | EDU field gaps discovered mid-phase (lethality/charge/AP absent from the fixture) | Task 2 step 1 coverage check FIRST; parser extension via disclosed golden re-sign (F3); placeholder constants acceptable (A6), missing STRUCTURE is not |

## §4. API digest -- new engine territory: StateTree (I4)

Pre-researched digest for symbols NOT yet in `Docs/reference/engine-api-5.8.md`. Task 1 audits these against the pinned 5.8.4 source and merges verdicts into the reference file (Part A §15) per AGENTS §4 -- the audit is a deliverable, not optional.

Known-good going in (verify exact 5.8.4 paths, do not trust memory):

- `UStateTree` asset + `UStateTreeComponent` (actor-level execution); `FStateTreeExecutionContext` for tick/eval; states, transitions, tasks (`UStateTreeTaskBase`), conditions, evaluators; the StateTree editor/debugger tooling.
- A Mass-side bridge has shipped with the engine in recent versions (search `MassStateTree` / `FMassStateTree` symbols under the engine's AI/StateTree plugin tree) -- whether 5.8.4 exposes it usable-from-plugin, and at what stability tier (experimental?), is exactly what the audit must record. The unit FSM is ACTOR-level (one FSM per unit, ~40 instances) -- the Mass bridge is optional convenience, not a requirement; verdict criteria (spec §4.10) decide on merit.
- Linkage: the StateTree module name + `Build.cs` dependency line for the audit to record; the plugin currently links `MassEntity`, `MassMovement`, `MassSignals`... (verify in `Plugins/MassBattle/Source/MassBattle/MassBattle.Build.cs`).
- Watch: the B.0 test-mode contract (fixed DT, no wall-clock) must hold under StateTree ticking; if StateTree scheduling cannot honor fixed-DT stepping headlessly, that alone is a rejection-grade fact for the ADR.

If the audit contradicts any line above, correct this section's owning reference entry (engine-api Part A §15), log it in the progress log with evidence, and disclose in the handoff report (AGENTS §4).

## §5. Task plan

Gate-first discipline: each gate test lands in its owning task's commit (or the prior one) -- verified via git log at the DoD. Every task commits once, prefix `phase2:`.

### Task 0 -- Gate-first skeleton + feature flags

**Goal.** The whole T2 gate family exists and fails; the CVAR block exists; the phase is CI-visible before any implementation.

**Steps.** (1) Create `BattleCombatFragments.h` (fragment stubs per spec §3.1) and `BattleTypes.h` enum additions, compile-only. (2) Stub seam headers `BattleEngagement.h` / `BattleMelee.h` with spec §4 signatures (empty bodies returning failure). (3) Register T2.1-T2.9 automation-test stubs (`Battle.Unit.Combat.HitChance / BudgetCoverage / TallyAssign` + `Battle.Func.S4_ShieldWall` / `S5_Mob` / `ChargeMechanics` / `PairLifecycle` / `DeterminismTwoRun` + perf-gate placeholder) asserting `false` with "skeleton" messages. (4) Add the CVAR block (spec §4.11 table; `Battle.Combat.Enable` default 0, per-flag defaults as listed). (5) Create `Docs/phases/phase-2/progress.md` with the session header.

**Acceptance check.** Unit tier green EXCEPT the new Combat stubs, which fail-by-design (count them, list them); flags off = no behavior change (T1.8 soak path untouched).
**Commit.** `phase2: task 0 gate skeleton + CVARs`

### Task 1 -- StateTree prototype + ADR (I4) -- TIMEBOXED

**Goal.** The unit-FSM tooling decision is made and recorded before combat wiring accretes; the engine facts land in the reference file.

**Steps.** (1) Audit the §4 digest against pinned 5.8.4 source; record verdicts in `Docs/reference/engine-api-5.8.md` Part A §15 (corrections ledger format). (2) Prototype behind `Battle.Combat.StateTree` (default 0): Idle / Move / AttackUnit / Engaged for one unit type, headless, driven from registry state via the serial-bump pattern (F4). (3) Evaluate the six verdict criteria (spec §4.10) with evidence. (4) Write the ADR row content (adopt-with-flag or reject-with-evidence) for the owner's architecture log; the report lists it as a proposal.

**Acceptance check.** Either the prototype passes the criteria with evidence lines, or the rejection evidence is concrete (which criterion failed, how it was measured); the reference-file §15 exists with sources; the timebox was respected (progress log records the clock).
**Commit.** `phase2: task 1 StateTree audit + prototype + ADR evidence`

### Task 2 -- Combat data spine

**Goal.** Fragments, engagement records, order surface, state-hash terms, save skeleton -- the data model of spec §3, compile-complete and unit-tested.

**Steps.** (1) EDU field coverage check (F3): weapon class/reach, AP flag, lethality, stat_charge; extend the parser only via the disclosed golden re-sign (R-C10). (2) Fill the fragment stubs (spec §3.1); extend `FHealthFragment` with the mount HP pool. (3) Registry: engagement record store + preallocated scratch, `IssueAttackUnitOrder` / `IssueAttackMoveOrder` (serial bump; garrisoned-unit guard per A9 -> EXEC_RESULT reject line), `HandleUnitDeath` pair-cleanup hook (F2). (4) State-hash terms append after garrison terms (F5, spec §3.4). (5) Save/load skeleton: `FBattleSaveData` v1 + capture/restore API + registry-level round-trip test (A8, spec §3.5). (6) ExecBridge probes: `Battle.Order.AttackUnit/AttackMove`, `Battle.Combat.DumpPairs`, `Battle.Combat.SaveProbe`.

**Acceptance check.** Unit tests: state-hash stability (identical sim -> identical hash; pair mutation changes it), record lifecycle (create/refresh/cap/dead-flag/mass release), order reach + A9 rejection, save round-trip (capture -> clear -> restore -> hash equal). Archetype count still 6 after spawning with the extended fragments (F6).
**Commit.** `phase2: task 2 data spine (fragments, records, orders, hash, save)`

### Task 3 -- Melee resolution seam (`BattleMelee.h/cpp`)

**Goal.** All resolution pure math on the B.0 seam, golden-locked, no processor changes yet.

**Steps.** (1) `FMeleeMath::HitChance(A, D)` contested curve with floor/cap clamps (spec §4.8; placeholders per A6). (2) Arc band math: `SkillArcMult`, `ShieldArcWindow`, AP armor halving -- driven by the quantized ContactAngle sector (one value, two consumers -- spec §4.5). (3) Attack cycle math: staggered phase offsets (handle-hashed), interval/recover windows. (4) Charge bonus math: fresh-contact gate, decay window, knockback impulse magnitude/direction (bounded, deterministic). (5) Composite HP model: rider + mount pool drain.

**Acceptance check.** T2.1 golden: formula + arcs + AP + clamps locked to a golden table (checked-in values; any diff = conscious formula review); cycle stagger deterministic under seeded streams; arc bands cover frontal/flank/rear/behind cases including sector-boundary values.
**Commit.** `phase2: task 3 melee seam + T2.1`

### Task 4 -- Engagement solver seam (`BattleEngagement.h/cpp`)

**Goal.** All solver pure math on the B.0 seam, unit-tested over synthetic populations.

**Steps.** (1) `SlotBudget` / `ComputeCoverage` / `PickExposedAngle` (spec §4.5, architecture §7.3 verbatim). (2) `DesiredPressure` formulas (frontal / mob with the A14 floor). (3) Tally table (open-address, reset-not-realloc) + assign step function over synthetic defender tables and seeker arrays (spec §4.4 pseudo-code as the contract). (4) Sector invariants: exposed filled before covered; budget bounds per spec §4.5's bound reading; handoff-on-release within the same pass.

**Acceptance check.** T2.2 green: exposed-arc-first invariant + budget bounds over 1k randomized assign/remove cycles; T2.3 green: budgets respected on synthetic populations; determinism flag yields index-sorted reproducibility (A13).
**Commit.** `phase2: task 4 solver seam + T2.2/T2.3`

### Task 5 -- Spatial hash + cluster graph (game-thread bookkeeping)

**Goal.** Rows 2-3 of the pipeline: the hash subsystem and the 4 Hz record machine, wired to registry state.

**Steps.** (1) `UBattleSpatialHashSubsystem`: counting-sort rebuild, `QueryCircle`, handle-sorted placement (spec §3.3, §4.1). (2) `EngagementClustersProcessor`: OBB dilation, flood-fill clusters, record emission incl. FrontageStart/End + Mode (spec §4.2); garrisoned units excluded (A9). (3) `BuildSpatialHashProcessor` before it, group "Battle", after `UnitLocomotion`. (4) Unit tests: hash correctness (insert/query/miss), rebuild determinism across two identical runs, zero-allocation assertion in the steady fight; cluster fixtures (frontal / flank / mob / none).

**Acceptance check.** Unit tier green with the new hash/cluster tests; `Battle.DumpMetrics` carries hash build ms; records observable via `Battle.Combat.DumpPairs` probe on a spawned two-unit contact fixture.
**Commit.** `phase2: task 5 spatial hash + clusters + records`

### Task 6 -- Assign pass + pair lifecycle + servo integration

**Goal.** Rows 4-5 plus the single existing-processor consumption point: pairs form, seekers move, waiters press, lifecycle rules hold.

**Steps.** (1) `TallyProcessor` + `AssignPairsProcessor` (game thread, spec §4.3-§4.4): validation/release top-of-pass, unit-level cap, laned vs mob targeting, budget check, sector assignment, staggered attack timers. (2) `FCombatIntentFragment` wiring at order issue (A4). (3) `ServoProcessor` pair-state consumption per A12 (Seeking/Pressing/Striking-Recovering/Charging mapping; press-ring target tracks the pair target; movement states reuse the existing weights, F1). (4) Unit-tier pair-rule tests: release conditions, hysteresis band, vacated-slot handoff with synthetic deaths (F2 path), disengage mass release.

**Acceptance check.** Unit tier green (pair rules); func smoke: two units ordered into contact form pairs and press the ring (overlay evidence + `DumpPairs` line); no T1.8 soak regressions with flags off.
**Commit.** `phase2: task 6 assign pass + pair lifecycle + servo mapping`

### Task 7 -- Combat resolution + charge processors

**Goal.** Rows 9-10 melee: damage flows, entities die through the existing death path, charges connect. Combat becomes observable end-to-end; `Battle.Combat.Enable` defaults 1.

**Steps.** (1) `CombatResolutionProcessor` (worker, after `Integrate`): attack-timer advance, roll via `Combat.Roll` stream (A13), damage through `FHealthFragment` queued damage into the Phase 0 death path (F2); death-by-arc tallies into the debug subsystem. (2) `ChargeProcessor` (worker): charge state machine per spec §4.9, impact detection, first-pair seeding with `ChargeBonusRemaining`, knockback through the displacement blend (F1). (3) CVAR `Battle.Combat.Enable` -> default 1 (B.8 item 6 done: flags-off identity already asserted in Task 6). (4) Func scenario `Battle.Func.ChargeMechanics` (T2.6).

**Acceptance check.** T2.6 green: bonus only on fresh fast contact; knockback applied (displacement observable in transform deltas); decays to melee within the window; kill chain evidenced (`EXEC_RESULT` / log lines: roll -> damage -> `FDeadTag` -> signal -> roster).
**Commit.** `phase2: task 7 resolution + charge + T2.6`

### Task 8 -- Scenarios S4 / S5 + M3 demo (geometry-only)

**Goal.** The exit-criteria scenarios are PR-gate citizens; the mob test IS the M3 demo.

**Steps.** (1) `Battle.Func.S4_ShieldWall` (T2.4): formed defender, frontal attacker; assert budget stays at FormedSlots while coverage holds, then the melt CURVE (shape, not endpoint; sampled budget-vs-coverage points). (2) `Battle.Func.S5_Mob` (T2.5): 2 knights vs 40 peasants; assert >= 4 exposed-arc attackers on an isolated knight, pull-down within the time band, waiters at the contact ring, no carousel. (3) `Battle.Func.PairLifecycle` full assert (T2.7): hysteresis breakup, vacated-slot handoff <= 1 tick, disengage mass release -- now with real resolution-driven deaths. (4) Demo: ExecBridge `Battle.Combat.SpawnClash {wall|mob|charge}` fixtures + demo-game-mode M3 sample registration when nothing is level-placed (A10; ADR-18 pattern: plugin public API only).

**Acceptance check.** Func tier green with S4/S5/ChargeMechanics/PairLifecycle added to the PR set; scenario evidence lines (budget curves, attacker counts, time bands) captured verbatim in the progress log; M3 sample runs on the demo map headless.
**Commit.** `phase2: task 8 S4/S5 scenarios + M3 demo`

### Task 9 -- Determinism, perf gate, soak variant, re-baselines

**Goal.** EC8/EC9 proven; the nightly combat tier exists; baselines are current.

**Steps.** (1) `Battle.Func.DeterminismTwoRun` (T2.8): 5-min mixed clash, same seed twice, per-tick state-hash equality (R9 scope; same binary, same machine). (2) Perf: `STAT_GROUP(Battle_Combat)` counters (hash build / tally / assign / resolution ms); 1k-all-in-contact stress fixture; `Battle.DumpMetrics` emits pairing ms; baseline JSON checked in; CI compares via the existing `compare_baseline.py` path (F12). Assert assign+tally < 0.5 ms GT (T2.9). (3) Soak: `Battle.Combat.SoakClash=1` variant on `BattleMovementSoakController` (clash mix, seeded attack orders, garrisoned-aware per F11); flag off = byte-identical T1.8 baseline (R-C9). (4) Re-baseline `entity_spawn_80_us` with garrison fields AND combat fragments present (F12 deferred item -- discharges the garrison report §4.3 reminder). (5) One staged nightly run as evidence.

**Acceptance check.** T2.8 green; T2.9 gate green with the baseline JSON committed; soak variant ends engaged-but-settled, stuck = 0, separation = 0, archetypes constant; `entity_spawn_80_us` baseline updated with evidence.
**Commit.** `phase2: task 9 determinism + perf gate + soak + baselines`

### Task 10 -- Overlay, metrics, docs, final verification

**Goal.** B.8 closure: the capability is observable, measured, documented, and regression-protected.

**Steps.** (1) Debug overlays (B.2 combat rows, F10 pattern): pairing lines with ContactAngle + pair-state colors; per-defender tally vs budget; coverage heat (0-1); engagement-record debug (frontage segments, lanes, mode, pressure vs assigned). All behind `Battle.Debug.ShowOverlay`. (2) Metrics (B.4): pair churn/sec, re-pair latency (ticks), assign ms, coverage distribution, deaths by arc, charge connects; wired into `Battle.DumpMetrics` + `Battle.Combat.Metrics` line; `ResetDiagnostics` coverage. Consider the phase-1 deferred "during-soak escalation-rate" signal as a stall-rate-under-pressure metric (phase-1 report §4). (3) B.7 regression-checklist entry (roadmap): Phase 2 gates + invariants appended (authorized here). (4) Docs: `Docs/README.md` map rows for the spec + phase folder, status block update; ADR-22..26 content handed to the owner for the architecture log (spec §8 -- spec edits are owner-applied per AGENTS §7; the report lists them as proposals); facts corrections per AGENTS §4. (5) Final verification: full unit + func tiers, one nightly (baseline + clash variant), git-log gate-first audit, archetype count 6 asserted everywhere.

**Acceptance check.** B.8 checklist (§7) fully checked with evidence pointers; CI green; progress log complete; handoff report written per AGENTS §5.
**Commit.** `phase2: task 10 overlay + metrics + docs` then `phase2: complete`

## §6. Gate -> implementation map

| Gate | Tier | Task | Test name | Asserts (short) |
|------|------|------|-----------|-----------------|
| T2.1 | Unit | 3 | `Battle.Unit.Combat.HitChance` | formula + arcs + AP + clamps golden-locked; any diff = conscious review |
| T2.2 | Unit | 4 | `Battle.Unit.Combat.BudgetCoverage` | exposed arcs before covered; budget bounds [1, MaxMob] over 1k random cycles |
| T2.3 | Unit | 4 | `Battle.Unit.Combat.TallyAssign` | budgets respected on synthetic populations; determinism flag = index-sorted reproducibility |
| T2.4 | Func | 8 | `Battle.Func.S4_ShieldWall` | holds at FormedSlots; melt curve SHAPE asserted |
| T2.5 | Func | 8 | `Battle.Func.S5_Mob` | >= 4 exposed-arc attackers; pulled down in time band; waiters at ring, no carousel |
| T2.6 | Func | 7 | `Battle.Func.ChargeMechanics` | fresh fast contact only; knockback; decay within window |
| T2.7 | Func | 8 | `Battle.Func.PairLifecycle` | hysteresis breakup; handoff <= 1 tick; disengage mass release |
| T2.8 | Func | 9 | `Battle.Func.DeterminismTwoRun` | per-tick hash identical, same seed, same binary/machine, 5-min clash |
| T2.9 | Perf | 9 | `Battle.Perf.Gate` (pairing) | < 0.5 ms GT @ 1k in contact; baseline JSON checked in, CI compares |

## §7. DoD checklist

- [ ] T2.1-T2.9 green with evidence (unit/func tiers via `run_tests.bat`; perf + soak via the staged nightly; verbatim lines in the report)
- [ ] EC1-EC9 demonstrable with evidence pointers to scenarios/probes (spec §0)
- [ ] B.8 item 1: unit tests over the math (both seams: `BattleEngagement`, `BattleMelee`)
- [ ] B.8 item 2: scenario tests S4/S5 + ChargeMechanics + PairLifecycle in the PR gate
- [ ] B.8 item 3: debug overlay (pairing lines + ContactAngle + state colors, tally vs budget, coverage heat, records)
- [ ] B.8 item 4: metrics (pair churn, re-pair latency, assign ms, coverage, deaths by arc, charge connects)
- [ ] B.8 item 5: B.7 regression-checklist entry (roadmap, this phase)
- [ ] B.8 item 6: `Battle.Combat.*` CVARs; flags off = pre-combat behavior (unchanged T1.8 soak identity asserted)
- [ ] B.8 item 7: Iris-ready -- no raw pointers across the registry boundary; engagement state reconstructible from serializable sim state (save skeleton proves the registry half); RNG streams seed-registered
- [ ] Archetype count 6 constant through all scenarios and soak runs (F6)
- [ ] State-hash stability with pair/charge terms (mutation sensitivity + two-run equality via T2.8)
- [ ] Zero per-tick heap allocations in hash/tally/assign steady state (asserted; EC9)
- [ ] StateTree ADR content recorded (adopt or reject, with evidence + reference-file §15) -- I4 resolved either way
- [ ] Save/load skeleton v1 round-trip green (registry level; schema-version field present) -- R7
- [ ] M3 demo geometry-only running (mob sample; no morale dependencies) -- R6
- [ ] `entity_spawn_80_us` re-baselined with garrison + combat fields present (garrison deferred item discharged)
- [ ] Gate-first verified via git log (each T2 gate landed with/before its owning task)
- [ ] `Docs/phases/phase-2/progress.md` complete; `Docs/README.md` map + status updated; handoff report per AGENTS §5; ADR-22..26 handed to the owner as proposals

## §8. System prompt

```text
You are the MassBattle Phase 2 implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-2/brief.md.
Execute the brief autonomously under the standing protocol; brief §3.1
decisions are owner-approved as marked. Stop at the brief's DoD (§7)
after writing Docs/phases/phase-2/report.md and the final commit.
```
