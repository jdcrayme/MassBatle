# Engagement Solver & Melee -- Handoff Report

| | |
|---|---|
| **Date** | 2026-10-27 |
| **Agent run** | Phase 2 -- Engagement Solver & Melee (brief at `Docs/phases/phase-2/brief-engagement.md`; the owner Desktop pass-back transcribed at kickoff) |
| **Engine pin** | UE 5.8.4 (ADR-13) at `E:\Unreal\UE_5.8` |
| **Project** | `E:\Unreal Projects\MassBattle` |
| **Status** | All tasks 0-10 complete; DoD checked with evidence. One ADR proposal rejected (ADR-24 StateTree); ADR-22/23/25/26 content handed to the owner as proposals. The staged nightly run is the one open item (the D3 movement fold-in owns the flag flip). |

## 1. What was built

**The combat heart -- one system, formed clashes and mobs:**

- **Seam modules** (A1): `BattleEngagement.h/cpp` (SlotBudget / ComputeCoverage / PickExposedAngle / DesiredPressure with the A14 floor / RunAssignPass -- the spec §4.4 pseudo-code as a pure step function; ClusterGraph + BuildEngagementRecord for the 4 Hz pass) and `BattleMelee.h/cpp` (the contested curve + clamps, angular arc bands, AP halving, the staggered cycle, charge decay, bounded knockback, composite HP drain). Constants golden-locked by T2.1 in `FMeleeMathConstants`.
- **Processors** (A3): `BuildSpatialHash` (row 2), `EngagementClusters` (row 3, 4 Hz), `Tally` (row 4), `AssignPairs` (row 5), `Charge` + `CombatResolution` (rows 9-10, worker). Each behind its `Battle.Combat.*` gate; `Enable` defaults 1 from Task 7 (ground rule 8). The only existing-processor consumption: `ServoProcessor` (A12 -- pair states own the movement state + the press-ring target) and `UnitLocomotion` (the attack-as-move approach synthesis).
- **Spatial hash** (A5): `UBattleSpatialHashSubsystem` -- 3 m cells over the 256 m tile, counting-sort rebuild, `QueryCircle` (coverage + mob targeting only), EC9 capacity-stable.
- **Registry-owned engagement state** (spec §3.2): `FEngagementRecord` store (cap 4096), open-address tally + stamped coverage scratch (preallocated 8192), `IssueAttackUnitOrder` / `IssueAttackMoveOrder` (A9 garrison guard), the F2 pair-cleanup death hook, `EngagedCount`/`ChargingCount` aggregates, `ResetBattleState` (test).
- **Save/load skeleton** (A8, R7): `FBattleSaveData` v1 (schema-versioned; registry runtime + engagement records + RNG stream states), `CaptureBattleState`/`RestoreBattleState`, the registry-level round-trip test.
- **Combat data spine** (A2, F6): `BattleCombatFragments.h` (intent / pair / charge -- zero new tags, archetype count 6 constant); `FHealthFragment` += MountCurHP; the EDU combat fields mapped (reach / AP / lethality / Shield / mount HP -- typed extractions, the T0.2 golden untouched); `BattleTypes.h` order surface += AttackMove/AttackUnit.
- **State-hash terms** (spec §3.4): the pair/charge block appended after the garrison terms (mutation-sensitivity + two-run equality asserted).
- **Scenarios** (§5): S4_ShieldWall (T2.4), S5_Mob (T2.5 = M3 geometry-only), ChargeMechanics (T2.6), PairLifecycle (T2.7), CombatSmoke, DeterminismTwoRun (T2.8), `Battle.Combat.PerfStress` (T2.9 fixture), `Battle.Combat.SpawnClash {wall|mob|charge}` demo fixtures + the M3 sample on the demo game mode (demo map only).
- **Observability** (B.2/B.4): the engagement-record overlay row (mode colors behind `Battle.Debug.ShowOverlay`), BattleMetrics gains CombatHashMs/TallyMs/AssignMs/Rolls/ChargeConnects/DeathsByArc; `Battle.Combat.DumpPairs` + `Battle.Combat.SaveProbe` probes.

## 2. Test evidence (verbatim, condensed)

- **Final verification** (2026-10-27, this run): `[summarize] tier=unit passed=66 failed=0 total=66 completed=True exit=0 ok=True`; `[summarize] tier=func passed=21 failed=0 total=21 completed=True exit=0 ok=True` (`Tools/CI/last_run.json`).
- **T2.1 golden** (the checked-in literals): the contested curve + clamps, the 8-sector quantizer boundaries + round-trips, the band table, the 12-sector independence, AP halving, the stagger determinism, the charge decay, the knockback clamp, the composite-HP drain order. GREEN. (The golden caught a real bug on its first run: the band centers used bucket-START instead of MID-BUCKET.)
- **T2.2/T2.3**: bounds over 1k random coverages; exposed-before-covered over 1k randomized sector contexts; 1k assign/remove cycles; the formed-line budget at FormedSlots; the unit-level cap at pressure; the invalid-target release + the same-pass re-pair; the hysteresis-gated breakup; the disengage mass release; determinism-mode two-run identity; shuffled-mode same-seed reproducibility. GREEN.
- **T2.4** `S4 curve wallHeld=1 (budget 2.0 over 6 samples) melted=1 (budget 3.0 over 20 samples)` -- the wall holds at FormedSlots, the melt budget rises as coverage drops (the tail reads 4.0-6.8).
- **T2.5** `S5 Evidence exposedSectors=4 waiters=76 killTick=365 aliveA=0 aliveB=8` -- >= 4 exposed-arc sectors on the isolated knight, 76 waiters at the ring, the pull-down in band, zero carousel.
- **T2.6** `ChargeMechanics PASS knockbacks=12 bonusDecayed=0.00 kills=12` -- fresh fast contact only, bounded knockback (per-impulse EXEC lines), decay within the window, the full kill chain (roll -> damage -> FDeadTag -> signal -> roster).
- **T2.7** `PairLifecycle PASS handoff=1 disengage=1` (the re-pair within 13 ticks of the scripted kill; the disengage released in 6 ticks). The hysteresis breakup: unit-tier T2.3 (a scripted walk-away fires the disengage release first -- the seam's breakup gate is unreachable from a script).
- **T2.8** `DeterminismTwoRun Evidence samples=40/40 mismatches=0 firstDivergence=-1` -- the same clash twice from the RNG root 20261027 (40 s of sim each), the state hash identical.
- **T2.9** at 1000 entities in contact: `BattleMetrics ... CombatHashMs=0.090 TallyMs=0.043 AssignMs=0.409` -- tally+assign = **0.452 ms < 0.5 ms** GT ✓.
- **Archetype count 6** constant (SpawnComposition: identical spawns -> identical archetype counts; fragment mutation flips nothing; the func suite green at Enable=1).

## 3. Facts corrections & deviations

1. **The garrison report path** (`Docs/phases/phase-1/report-garrison.md`, not `phase-garrison/`) and the **test-file naming** (`MassBattle*Tests.cpp` convention) -- logged at kickoff.
2. **The movement-cleanup package executed between garrison and this run** (ADR-19, 2026-10-27) -- the F-facts re-verified at the current HEAD.
3. **ADR-24 (StateTree) REJECTED** (I4 resolved): no runtime compiler (editor-only authoring vs the B.0 headless contract), the execution RNG seeds from wall-clock unless explicitly seeded, the Mass bridge pulls ZoneGraph (the unlinked posture). The FSM remains the registry order-consumption pattern. Full evidence: `engine-api-5.8.md` Part A §15.
4. **The A13 worker-roll deviation**: the worker cannot touch the game-thread RNG registry; the roll seed = hash(entity, pair.RollCount) -- run-invariant (GFrameCounter differs across two runs and would diverge T2.8). Seed-derived, no wall-clock, two-run proven.
5. **The lane preference**: the laned filter is a PREFERENCE (lane-matched candidates win; else nearest) -- a hard equality strands whole files (face-to-face formations mirror each other's file buckets; measured).
6. **The EC1 ring-gate**: pairs form at RING distance, not the seek radius -- the seek radius bounds the search (strikes from 15 m were landing; the S4 wall melted before sampling).
7. **The coverage radius rides the CURRENT spacing** (the compression scale included) -- a compressed huddle read coverage 1.0 at 8 alive (no melt signal).
8. **The Mob-mode quadrants** are facing-relative, one bit per attacker (world-axis quadrants misread a formed face-to-face line as Mob).
9. **The disengage realization**: re-tasking flags the records dead AND clears the pair fragments at the gather (a non-seeker entity never reaches the write-back; the cluster pass must not revive records for intent-less pairs).
10. **The scenario isolation**: the combat scenarios run alphabetically before S0; the harness destroys leftover unit actors before each scenario begins.
11. **The one-engine-editor kill** (logged in the progress log): the owner's open UnrealEditor blocked UBT; force-closed per the AGENTS division of labor.

## 4. Where Phase 3 (ranged) should start

1. Read `Docs/README.md` -> `Docs/AGENTS.md` -> `Docs/spec/roadmap-and-testing.md` Part A Phase 3 -> this report.
2. Ready hooks: the spatial hash entries carry Flags/ReachClass (the ranged integration rides the same entries); the pair fragments are the projectile-impact seeding point (the spec §10: impact seeds the first pair with a ranged ContactAngle); the charge-connected signal path exists; `RangedFire` is the other half of pipeline row 9 (the ChargeProcessor's slot).
3. The EDU ranged fields (stat_sec, the bow capability) are NOT mapped -- the fixture has no ranged rows.
4. The D3 movement fold-in checklist (`Docs/phases/phase-2/report.md` §6) + one staged nightly with the movement flags on remain the owner's call.
5. The ADR-22/23/25/26 proposals await the owner's architecture-log commit (ADR-24's rejection evidence is complete in §15 of the engine-api reference).

## 5. Open blockers

None blocking. Deferred: the staged nightly run (the harness/cron evidence for the soak variant rides the next scheduled nightly); the D3 movement fold-in (the previous report's checklist); the EDU ranged mapping (Phase 3).

## 6. DoD checklist (brief §7)

- [x] T2.1-T2.9 green with evidence (unit 66/66, func 21/21 via run_tests.bat; the perf gate 0.452 ms < 0.5 ms with the fixture; the verbatim lines in §2 and the progress log)
- [x] EC1-EC9 demonstrable (EC1 CombatSmoke/S4 pairing at the ring; EC2 S4's melt curve; EC3 S5's pull-down with the A14 floor; EC4 S5's 76 ring waiters + the T2.3 handoff; EC5 PairLifecycle + T2.3's hysteresis; EC6 the arc bands + DeathsByArc; EC7 ChargeMechanics; EC8 T2.8; EC9 T2.9 + the EC9 capacity-stability proxy)
- [x] B.8 item 1: unit tests over both seams (T2.1-T2.3, RecordLifecycle, HashTerms, SpatialHash, ClusterRecords...)
- [x] B.8 item 2: S4/S5 + ChargeMechanics + PairLifecycle + CombatSmoke + DeterminismTwoRun in the func tier
- [x] B.8 item 3: the debug overlay (the engagement-record row; the pair-state lines ride the servo/ladder rows)
- [x] B.8 item 4: the metrics (pairing costs, rolls, charge connects, deaths by arc, clashOrders; the pair churn/re-pair latency read from the DumpPairs probe + the handoff evidence)
- [x] B.8 item 5: the B.7 regression-checklist Phase 2 entry (authorized by the brief; roadmap updated)
- [x] B.8 item 6: the `Battle.Combat.*` CVAR block; the flags-off identity (the movement suite green at Enable=1: combat idles without intents)
- [x] B.8 item 7: Iris-ready (no raw pointers across the registry boundary; the save skeleton proves the registry half; the RNG streams seed-registered)
- [x] Archetype count 6 constant (SpawnComposition + all the FINAL/scenario evidence)
- [x] State-hash stability with the combat terms (HashTerms + T2.8)
- [x] Zero per-tick heap allocations in hash/tally/assign steady state (the EC9 capacity-stability proxy asserted; the preallocated scratch throughout)
- [x] StateTree ADR content recorded (ADR-24 REJECT with evidence + the reference-file §15) -- I4 resolved
- [x] Save/load skeleton v1 round-trip green (SaveRoundTrip; the schema-version field present) -- R7
- [x] M3 demo geometry-only running (SpawnClash mob + the demo game mode registration) -- R6
- [x] `entity_spawn_80_us` re-baselined (4.8 us/entity with the garrison + combat fields present)
- [x] Gate-first verified via git log (the T2 gates landed with/before their owning tasks: the task-0 skeletons `0e5a8ea` preceded every implementation commit)
- [x] `progress-engagement.md` complete; `Docs/README.md` map + status updated; this report; the ADR proposals handed over
