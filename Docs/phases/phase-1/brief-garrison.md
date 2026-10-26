# MassBattle Phase Garrison Brief -- Garrison Movement (Buildings, Walls, Trenches)

> **Archive note (2026-10-26 roll).** Garrison executed as its own folder (`Docs/phases/phase-garrison/`, decision A6) between Phase 1 and Phase 2; rolled into `Docs/phases/phase-1/` on 2026-10-26 because it is part of Phase 1. Frozen record; historical paths in the text refer to the original location.

| | |
|---|---|
| **Companion docs** | `Docs/spec/garrison-movement.md` (design spec: B1-B7 behavior contract, data model, mechanics, ADR proposals) · `Docs/AGENTS.md` (standing protocol) · `Docs/spec/roadmap-and-testing.md` (B.0 test contract, B.8 DoD) |
| **Engine pin** | UE 5.8.4 (ADR-13); no new engine API territory is expected (see §4) |
| **Run mode** | Single autonomous run, protocol per `Docs/AGENTS.md`; all CI evidence via `Tools/CI/run_tests.bat {unit,func}` |
| **Priority directive** | Movement-only garrison: file into structures, occupy firing positions, egress. Combat integration of any kind is out of scope (owner decision; see §3 A-table and the spec's non-goals) |
| **Owner-approval status** | §3.1 decisions are owner-approved as marked; ADR-18..21 in the spec are proposals pending the owner's architecture-log commit |
| **Sequencing** | Executes between Phase 1 (movement core, complete) and Phase 2 (engagement solver). Rationale: `Docs/spec/garrison-movement.md` §9 |

---

## §0. Mission + read order + token discipline

**Mission.** Implement ETW-style garrison orders: a unit ordered into a garrisonable structure approaches its entrance, files through the choke, distributes to firing positions, holds, and files out on egress -- for gray-box buildings, walls (elevated walkway), and trenches (sunken positions), with deterministic capacity booking. The full behavior contract is the spec's B1-B7 table; the architecture claim is one sentence: **garrison is a slot-source and path-source mode of the existing movement spine, not a new movement system.**

**Read order** (token discipline: nothing else; do not read the spec docs end-to-end):

1. `Docs/README.md` status block, then `Docs/AGENTS.md`.
2. This brief, all sections.
3. `Docs/spec/garrison-movement.md` -- §2 (architecture summary), §3 (data model), §4 (mechanics), §7 (touch-point table) are the implementation contract; §0 B-table is the acceptance contract.
4. Source, in this order (the whole phase is built from these): `Plugins/MassBattle/Source/MassBattle/Public/BattleMovementFragments.h` -> `Private/UnitLocomotionProcessor.cpp` -> `Private/FormationSlotResolveProcessor.cpp` -> `Public/BattleSlotAssigner.h` + `Public/BattlePassGeometry.h` -> `Public/BattleCostGrid.h` + `Public/BattlePathPlanner.h` -> `Public/BattleTypes.h` + `Public/BattleRegistry.h` -> `Private/BattleStateHash.cpp`.
5. Roadmap B.0 (test-mode contract) and B.8 (capability DoD) -- cite, do not restate.
6. `Docs/phases/phase-1/report.md` §1 (what exists) if a fragment or processor detail surprises you -- the facts digest below (§1) is already verified against these files; report a discrepancy per AGENTS §2.1 instead of re-deriving.

**Do not** open `Docs/reference/engine-api-5.8.md` end-to-end; nothing in this phase leaves project code. If an engine symbol misbehaves, follow AGENTS §4 (correct the owning reference, log it).

## §1. Phase facts digest (pre-verified 2026-10-25 against the working tree)

Every fact below was verified by reading the cited file in this repo. Cite fact IDs in the progress log instead of re-reading.

- **F1** `FUnitFormationSharedFragment` (Public/BattleMovementFragments.h) is the per-unit movement broadcast channel: ADR-14 anchor `OwnerUnit` first, blittable (fixed arrays only, no TArray/UObject*/FString), carries `AnchorPos, FacingYaw, SpeedLimit, SpacingScale, Generation, NumFiles, NumSlots, RankSpacing, FileSpacing, ReflowCooldown, PathS, PlannedOrderSerial, PassLocked, Path` (fixed 64-sample polyline). All garrison per-unit state extends THIS fragment -- no new shared fragments.
- **F2** Order consumption lives in `UUnitLocomotionProcessor::Execute` (Private/UnitLocomotionProcessor.cpp): plans when `Registry->GetOrderSerial(Unit) != Formation.PlannedOrderSerial`, via `FBattlePathPlanner::FindPath` + `Smooth(grid, 1.0f, ...)` + `Resample(2.0f)`; polyline is copied into the fragment by `PolylineFromPlan` (m -> cm at the boundary, fact F12). Game thread: `bRequiresGameThreadExecution = true`, group "Battle", after `UnitSyncProcessor`, gated by CVAR `Battle.Movement.Locomotion`. Unreachable goal = hold (no teleport). This is the pattern the garrison FSM extends.
- **F3** `UFormationSlotResolveProcessor` is worker-safe (`bRequiresGameThreadExecution = false`, fragment-only accesses -- no subsystem access possible). Per chunk it builds slot positions from path-frame layout (`FPathFrameLayout::Project`) or the anchor formula fallback, runs batched reflow via `FReflowScheduler` (0.5 s interval + rank-gap trigger, cooldown persisted in `Formation.ReflowCooldown`), applies predictive swaps (`Battle.Movement.PredictiveSwaps`) and the k-file bound (`Battle.Movement.MaxFileDisplacement`), then writes `FSlotTargetFragment`. The garrison slot-source branch goes here.
- **F4** Servo states and weights (Private/ServoProcessor.cpp, arch §8.3/A1): HoldSlot 0, ManeuverToSlot 0.5 (teammates only), CrossField 1.0; `ManeuverToSlot` closes into `HoldSlot` when within `FUnitServoConstants::SlotReachEps` (0.35 m); unstuck ladder `FUnstuckLadder::Advance` levels 0-4. Garrison reuses these states unchanged -- garrison is NOT a servo state.
- **F5** `UIntegrateProcessor` (Private/IntegrateProcessor.cpp) integrates `transform += v*DT`, pins Z to 0, and sets yaw = velocity heading while moving. `FSlotTargetFragment.TargetYaw` exists but is NOT consumed at rest -- resting entities keep their last motion heading. Garrisoned posts need yaw authority (risk R-G5): when a garrisoned entity closes to its slot, rotation to `TargetYaw` must be applied.
- **F6** `FBattleCostGrid` (Public/BattleCostGrid.h): `CellCost` uint8, `Impassable = 255`, `StampImpassable(BoxMin, BoxMax)` box API (meters, idempotent), `ResetToFlat`; owner `UBattleCostGridSubsystem` (game thread). Planner (Public/BattlePathPlanner.h): deterministic A* (fixed neighbor order, strict (F,H,index) tie-break, no corner cutting), `SegmentIsClear` is the passability oracle. Phase 1 call shape: `Smooth(Clearance=1.0)`, `Resample(MaxSpacing=2.0)`.
- **F7** `FUnitPassGeometry` (Public/BattlePassGeometry.h): `DecideSqueezeOrMorph(PassWidth, FormationWidth, FileSpacing)` and `MeasureClearWidth` (the PassWidth oracle). The commit rule lives in UnitLocomotion: decide once `PassWidthM < FormationWidth * 0.99f` within the 15 m ahead-scan, then `PassLocked = 1`. Garrison ingress pre-commits (`PassLocked = 1` at path adoption) -- the column decision is made by the entrance's declared `ClearWidthM`, not by a re-scan.
- **F8** `FUnitOrder` (Public/BattleTypes.h) is the thin order payload: `EType {None, Move, Stop}`, `AnchorPos` (cm), `FacingYaw` (deg), `CommandedSpeed` (m/s, 0 = walk). Registry API (Public/BattleRegistry.h): `IssueMoveOrder` / `IssueStopOrder` / `FindOrder` / `GetOrderSerial`; the serial is the change detector (F2). ExecBridge commands emit `EXEC_RESULT <Name> ...` lines (the evidence channel, Public/BattleExecCommands.h header contract); `Battle.Order.Move` in Private/BattleExecCommands.cpp is the worked example (args parsing, handle matching by index via `GetSortedHandles`).
- **F9** `FBattleStateHash::Compute` (Private/BattleStateHash.cpp) folds, in fixed order: registry bookkeeping hash, per-entity HP/dead/transform (1 cm quantization, yaw 0.1 deg), archetype count, then per-unit stats + formation terms (anchor 1 cm, facing 0.1 deg, limit/scale 1e-2, Generation) over `GetSortedHandles()`. New garrison terms MUST append after the formation terms, position-stable, and only in the same sorted order. Entity Z is already hashed at 1 cm -- elevation states hash correctly with no format change.
- **F10** Test contract (roadmap B.0): `UBattleTestSettings::ApplyTestMode()` / `AssertTestMode()` (Public/BattleTestSettings.h) applied + asserted at every S-scenario start; naming `Battle.Unit.<Area>.<Name>` (per commit) and `Battle.Func.S<n>_<Name>` (per PR); pure-math seam rule B.0 (math in free functions/plain structs; math inside `ForEachEntityChunk` is a review flag); soak = `BattleMovementSoakController` (Gauntlet, staged build, `Tools/CI/nightly.bat` step 3b). B.8 applies to this capability in full.
- **F11** Uniform movement spine: 6 archetypes constant through the T1.8 soak (phase-1 report §2). ADR-15 final state: death is a REGULAR tag (sparse adoption probed and reverted, Public/BattleFragments.h comment) -- tags flip archetypes. This phase adds NO tags and NO new archetypes; garrison state is fragment fields (F1).
- **F12** Unit conventions (phase-1 ground rule 2, the E2 bug class): fragment boundary = centimeters, seam math = meters; convert exactly once at the boundary (`* 0.01f` / `* 100.f`). Speeds m/s in math, cm/s inside `SpeedLimit`. Data files ASCII + LF, `--` not em-dashes (AGENTS §3).
- **F13** `UnitLocomotion` collects unit bodies (`FUnitOBB` from `UnitBodyQuery`, one chunk per unit) for the §8.2 facing lock via `FUnitContact::Interpenetrating`; `ServoProcessor` applies shove blends to entities inside overlapping unit bodies (`FServoStateFragment::DisplacementWeight`). Garrisoned units must be excluded from BOTH (their field OBB is meaningless once distributed; interior crowding is servo+compression; risk R-G3).
- **F14** Death path: the death-signal receiver calls `UUnitRegistrySubsystem::HandleUnitDeath(Unit, Entity, RosterIndex)` on the game thread (Public/BattleRegistry.h). Garrison booking release hooks here (a unit dying mid-occupation must release its booking; the receiving function is the single correct place).
- **F15** Scenario fixtures are C++-authored (the `Battle.BuildT0Configs` pattern -- configs are built in C++ through `UBattleUnitTrait::BuildTemplate`, audit C6) and run on `L_MassBattle_T0_Functional` (phase-1 report: "no new maps"). Gray-box garrison structures for tests follow the same route: ExecBridge-authored records, not editor-placed actors.
- **F16** `FReflowScheduler` is stateless-per-instance with cooldown persistence in the fragment (F3); garrison reflow reuse must keep that pattern (per-unit cooldown survives chunk boundaries; risk R-G4).

## §2. Pointers

MCP/editor automation: operating rules live in `Docs/reference/mcp-playbook.md` (check its §8 correction ledger before rediscovering anything). This phase is planned MCP-light: all fixtures are C++/ExecBridge-authored (F15), all evidence via `run_tests.bat`; open editor sessions only if a map edit proves unavoidable, and batch file-side work around it (AGENTS §3).

## §3. Phase ground rules + owner decisions + risk watchlist

### 3.1 Owner-decision table (pre-decided; implement as written, never re-litigate)

| ID | Decision | Choice |
|----|----------|--------|
| A1 | Slot-source branch placement | Branch inside `FormationSlotResolveProcessor` on `GarrisonMode` (no separate processor). Keeps pipeline shape (arch §4 rows 1/6/7/8) and the state-hash story in one place |
| A2 | Structure slot transport | Per-unit mirror of the BOOKED RANGE only, in fixed arrays on `FUnitFormationSharedFragment` (`MaxGarrisonSlotsPerUnit = 64`, ~1 KB). Not a per-structure shared fragment (dedup hazards, E1 class). Copy happens game-thread at `Occupy`, mirroring `PolylineFromPlan` |
| A3 | Interior speed model | Single factor per phase, not per slot: `CommandedSpeed x InteriorSpeedFactor` (default 0.5, CVAR `Battle.Garrison.InteriorSpeedFactor`) during Ingress/Egress; Settled = 0 |
| A4 | Order when structure full / lease busy | Queue-and-retry (ETW B2 behavior): unit holds at queue point, retries every 0.5 s (`Battle.Garrison.QueueRetry`). No order-failure signal v1 |
| A5 | Gate naming | `TG.1`..`TG.10` -- the engagement solver keeps its reserved `T2.x` numbers; zero roadmap renumbering |
| A6 | Phase folder | `Docs/phases/phase-garrison/` (descriptive, renumber-proof; precedent: `pre-phase-0`) |
| A7 | Move order while garrisoned | Rewrites as `Egress` first; the move parks in `FUnitRuntimeState.PendingAfterEgress` and resumes at the assembly point. `Stop` while settled = no-op |
| A8 | Z servo | Bounded rate: `Z += clamp(TargetZ - Z, +/- MaxZRate*DT)`, `MaxZRate` default 2.0 m/s (CVAR `Battle.Garrison.MaxZRate`); planar X/Y integration untouched |
| A9 | Cavalry in buildings | Rejected at order issue (guard clause, EDU class tag `FCavalryTag`); walls/trenches also infantry-only v1. No message plumbing -- EXEC_RESULT line only |
| A10 | Scope lock | Movement only. No cover/arc metadata on slots, no engagement-solver changes, no auto-deployment. Extension points are recorded in spec §10 and are NOT built |

### 3.2 Phase ground rules (additions to AGENTS.md only)

1. All garrison math goes through the new seam module `BattleGarrison.h/cpp` (free functions, meters, deterministic, no RNG). Processors stay thin shims (B.0).
2. No new processors, tags, or archetypes (A1/F11). `ExecuteInGroup = "Battle"` ordering is unchanged; `UnitLocomotion` stays the only game-thread consumer of the garrison subsystem.
3. The garrison subsystem is registered/read on the game thread only; worker processors read exclusively fragment mirrors (A2). Access-detector discipline per BattleRegistry.h header.
4. Every phase transition logs one `EXEC_RESULT Garrison Phase Unit=<i:s> <Phase>` line (Display). Transitions are event-rate, not per-frame.
5. Egress and ingress share the internal-path machinery; no second path builder (spec §4.3/§4.5 use the same `FGarrisonLayout` functions with direction flags).
6. Structure records are immutable after registration for the duration of a battle. Booking state is the only mutable part.

### 3.3 Risk watchlist

| ID | Risk | Mitigation / watch |
|----|------|--------------------|
| R-G1 | Shared-fragment growth (~1 KB fixed slot array per unit) | Bounded by A2 (booked range only); verify no regression in the T1.8 soak baseline (flag off = identical baseline) |
| R-G2 | Door choke vs 3 m grid cells: doorways narrower than a cell cannot be expressed geometrically | Gray-box fixtures keep `ClearWidthM >= 4` (filing width is declared, not measured -- F7 pre-commit); authored maps get a documented minimum-door rule in the spec's §3.1 record struct comment |
| R-G3 | Garrisoned unit bodies still collected for OBB contact -> phantom facing locks / shoves inside structures | Exclude `GarrisonMode != None` units in the `UnitBodyQuery` collection and in shove targets (F13); asserted by T-G.6 (no escalations while settled) |
| R-G4 | Reflow fighting path-frame filing (slot-source ambiguity during Ingress) | Reflow gated to `Occupy`/`Settled` phases only (spec §4.4); `PassLocked` pre-commits the column (F7) |
| R-G5 | Yaw authority at rest: entities at posts keep motion heading (F5) -- posts face the wrong way | Add the garrison rest-facing rule: when garrisoned and within `SlotReachEps`, rotate toward `TargetYaw` at `FacingDeltaRate`. Asserted in T-G.6/T-G.8 |
| R-G6 | State-hash term drift (garrison terms folded inconsistently between runs) | Terms append after formation terms in the same sorted-handle pass (F9); T-G.4 includes a hash-stability assertion across two identical runs |
| R-G7 | Egress/ingress deadlock (two units, one entrance) | Entry lease (one unit per entrance in Ingress/Egress); queue holds at `QueuePoints`; asserted by T-G.9 |

## §4. API digest

**None required.** This phase is project-code only: it extends existing project types (`FUnitFormationSharedFragment`, `FUnitOrder`, `FBattleCostGrid`, processors) and adds one new world subsystem + one seam module. No engine symbols outside the already-audited Phase 1 set are consumed. If reality forces a new engine symbol into scope, follow AGENTS §4: verify against `Docs/reference/engine-api-5.8.md`, record the correction in the reference file, log it in the progress log, and disclose in the handoff report.

## §5. Task plan

Gate-first discipline: each gate test lands in its owning task's commit (or the prior one) -- verified via git log at the DoD. Every task commits once, prefix `garrison:`.

### Task 0 -- Gate-first skeleton + feature flags

**Goal.** The whole TG gate family exists and fails; the CVAR block exists; the phase is CI-visible before any implementation.

**Steps.** (1) Create `BattleGarrisonTypes.h` (enums `EGarrisonStructureType`, `EGarrisonMode`, `FGarrisonPhase`, constants `MaxGarrisonSlotsPerUnit = 64`). (2) Stub seam header `BattleGarrison.h` with the §3 function signatures (empty bodies returning failure). (3) Register TG.1-TG.10 automation-test stubs (`Battle.Unit.Garrison.*`, `Battle.Func.S8_BuildingGarrison`, `S9_TrenchGarrison`, `S10_WallGarrison`) asserting `false` with "skeleton" messages. (4) Add CVARs: `Battle.Garrison.Enable` (default 0 until Task 5 -- B.8 item 6 keeps the baseline clean), `Battle.Garrison.InteriorSpeedFactor` (0.5), `Battle.Garrison.QueueRetry` (0.5), `Battle.Garrison.MaxSlotDisplacement` (3.0 m), `Battle.Garrison.MaxZRate` (2.0), `Battle.Garrison.SoakOrders` (0). (5) Create `Docs/phases/phase-garrison/progress.md` with the session header.

**Acceptance check.** Unit tier runs green EXCEPT the new Garrison stubs which fail-by-design (count them, list them); `Battle.Garrison.Enable` off leaves the T1.8 soak path untouched (no code path changes yet).

**Commit.** `garrison: task 0 gate skeleton + CVARs`

### Task 1 -- Garrison data spine

**Goal.** Structure records, booking ledger, fragment fields, state-hash terms -- the data model of spec §3, compile-complete and unit-tested.

**Steps.** (1) `UBattleGarrisonSubsystem` (UWorldSubsystem): `RegisterStructure(FGarrisonStructureRecord)`, `FindStructure`, booking API (`TryBook`/`ReleaseBooking`, sorted-handle determinism), entry-lease API, `GetBookedCount`. Plain structs only; game-thread-only access. (2) Extend `FUnitFormationSharedFragment` with the garrison fields (spec §3.3) and `FUnitRuntimeState` with `PendingAfterEgress`. (3) Extend `FBattlePathPoint`/`FBattlePathPolyline` with Z (cm); field paths keep Z = 0. (4) Extend `FSlotTargetFragment` with `TargetZ`. (5) Append garrison terms to `FBattleStateHash::Compute` after the formation terms (F9): mode, structure id, phase, slot base/count. (6) `Battle.Garrison.DumpStructures` ExecBridge probe (F8 pattern).

**Acceptance check.** Unit tests: state-hash stability (identical sim -> identical hash; garrison mutation changes it), booking determinism (same seed/order -> same ranges), subsystem lifecycle (register/book/release/re-register, stale-handle rejection). Archetype count still 6 after spawning with the extended fragment (F11).

**Commit.** `garrison: task 1 data spine (records, booking, fragments, hash)`

### Task 2 -- Cost-grid integration + gray-box structure fixtures

**Goal.** Structures stamp the grid so field A* routes to entrances; test fixtures exist.

**Steps.** (1) `FBattleCostGrid::StampCost(BoxMin, BoxMax, Cost)` mirroring `StampImpassable` (idempotent, deterministic; F6). (2) Registration-side stamping helper on the subsystem: footprint -> Impassable, entrance corridor boxes -> Cost 64. (3) ExecBridge gray-box fixtures: `Battle.Garrison.SpawnBuilding <x> <y> <rotDeg>` / `SpawnTrench` / `SpawnWall` (gray-box records: box footprint, one entrance, internal path, slot line; building floor Z = 0, wall top Z = 6 m, trench channel Z = -0.75 m; declared `ClearWidthM = 4` per R-G2). (4) Fixture IDs printed via EXEC_RESULT for scenario use.

**Acceptance check.** T-G.1 green: registration stamps footprint impassable (`IsImpassable` samples) while `SegmentIsClear` holds through the entrance corridor from QueuePos to DoorPos; `ResetToFlat` clears; determinism (register twice -> identical stamp counts).

**Commit.** `garrison: task 2 cost stamping + gray-box fixtures`

### Task 3 -- Garrison seam math (`BattleGarrison.h/cpp`)

**Goal.** All garrison pure math on the B.0 seam, unit-tested (no processor changes yet).

**Steps.** (1) `FGarrisonLayout::BuildInternalPath(Record, EntranceIndex, bEgress)` -- arc-length-parameterized polyline with Z: QueuePos -> DoorPos -> distribution node (entry) or reverse (egress); Z monotone across stair/ramp segments; endpoints exact. (2) `FGarrisonLayout::AssignSlots(SoldierPositionsM, SlotsM, PrevAssignment, MaxSlotDisplacementM, OutAssignment)` -- deterministic greedy nearest-vacant bijection over the booked range with a distance bound (the k-file bound's analog); ties to the lower slot index; roster order. (3) `FGarrisonLayout::QueuePoints(Record, EntranceIndex, MaxQueued)` -- offsets behind QueuePos along the approach normal. (4) `FGarrisonLayout::ZAtS(Path, S)` -- Z interpolation (monotone on stair fixtures). (5) `FGarrisonLayout::ColumnFilesFor(ClearWidthM, FileSpacingM)` -- the ingress column count clamp.

**Acceptance check.** T-G.2 (internal paths: door passage endpoints, arc-length monotonicity, Z monotone on wall/trench fixtures, egress path = reversed arc lengths), T-G.3 (assignment: bijection, bound respected, deterministic under repeated runs, disjoint ranges across two booking sets), T-G.5 (Z interpolation monotone, slot Z fixtures correct) -- all green.

**Commit.** `garrison: task 3 seam math + TG.2/3/5`

### Task 4 -- Order surface

**Goal.** Players (and tests) can issue garrison orders through the registry.

**Steps.** (1) `FUnitOrder::EType` += `Garrison`, `ExitGarrison`; payload += `uint32 StructureId` (thin-payload rules, F8). (2) Registry: `IssueGarrisonOrder(Handle, StructureId)`, `IssueExitGarrisonOrder(Handle)` (serial bump; cavalry guard clause per A9 -> EXEC_RESULT reject line). (3) ExecBridge: `Battle.Order.Garrison <unitIndex> <structureId>`, `Battle.Order.ExitGarrison <unitIndex>` (worked example: Battle.Order.Move, F8).

**Acceptance check.** T-G.4 booking semantics green: capacity enforcement (MaxUnits), disjoint slot ranges, lease exclusivity, release on unregister, stale-handle rejection, and a two-run hash-stability assertion (R-G6). Orders reach the registry (EXEC_RESULT evidence lines).

**Commit.** `garrison: task 4 order surface + TG.4`

### Task 5 -- UnitLocomotion garrison FSM

**Goal.** The phase machine (spec §4) drives units from order to settled occupation. This is the core task.

**Steps.** (1) On serial change, branch on order type: Garrison -> plan field path to the selected entrance's QueuePos (A* unchanged, F2/F6); ExitGarrison -> begin Egress; Move -> A7 rewrite (park + egress). (2) Phase machine per spec §4.2-4.5: Approach (field path; arrival at QueuePos = anchor within epsilon) -> booking attempt -> Queue (hold, 0.5 s retry, A4) -> Ingress (adopt internal path via the `PolylineFromPlan` pattern incl. Z, `PassLocked = 1`, save `FieldFilesSaved`, set column files per `ColumnFilesFor`, interior speed clamp A3) -> Occupy (copy booked slot range into the fragment mirror per A2, `++Generation`) -> Settled (slot error < `SlotReachEps` across live roster; up-path signal). (3) Egress: internal egress path as Formation.Path, path-frame slot source, restore field files at the assembly point, release lease + booking, resume `PendingAfterEgress` (A7). (4) Transition logging (ground rule 4). (5) OBB exclusion: skip `GarrisonMode != None` units in the `UnitBodyQuery` collection (F13/R-G3). (6) `Battle.Garrison.Enable` now defaults 1.

**Acceptance check.** `Battle.Func.S8_BuildingGarrison` core path green: unit files through the door (column width at the door <= ClearWidthM), all live entities inside footprint at slots within the assert window, settled phase reached, zero unstuck escalations at settle (T-G.6 part 1). Phase transitions visible in log.

**Commit.** `garrison: task 5 locomotion FSM + S8 core`

### Task 6 -- Slot-source branch + Z servo + rest-facing

**Goal.** Garrisoned soldiers stand at their posts, at the right height, facing the right way.

**Steps.** (1) `FormationSlotResolveProcessor`: when `GarrisonMode` is Garrisoned (Occupy/Settled), slot targets = fragment slot mirror (pos/yaw/Z); reflow assigner = `AssignSlots` (F3, R-G4 gating). (2) Write `TargetZ` in both sources (slot mirror; path interpolation via `ZAtS` during filing). (3) `IntegrateProcessor`: Z servo per A8 when garrisoned (or TargetZ != current), heightfield pin otherwise. (4) Rest-facing rule (R-G5): garrisoned entity within `SlotReachEps` rotates toward `TargetYaw` at `FacingDeltaRate` (integrate-side, deterministic).

**Acceptance check.** T-G.6 completes (yaw at posts within tolerance, Z = floor 0), T-G.7 green: trench unit occupies sunken posts (Z < 0 within tolerance), T-G.8 green: wall unit climbs (Z rises monotonically along the stair segment) and stands at wall-top slots facing outward; slot-error overlay draws garrison slots.

**Commit.** `garrison: task 6 slot branch + Z servo + S8/S9/S10`

### Task 7 -- Multi-unit + egress sequencing

**Goal.** Capacity behaves like ETW (B2/B6): second unit queues, enters on lease; egress re-forms cleanly.

**Steps.** (1) Queue offsets via `QueuePoints` (retry picks the free offset; deterministic). (2) Lease handoff on Egress completion releases to the queued unit next tick. (3) Egress polish: entities converge from posts into the outgoing column (slot source flip timing), no strays at the assembly point, field layout restored (F16 cooldown state respected).

**Acceptance check.** T-G.9 green: two units garrison one gray-box building (second queues with a bounded wait, then settles into its disjoint range); ordered sequential exit files both out with zero strays and no lease deadlock; booking ledger empty after both exit (`DumpStructures` evidence).

**Commit.** `garrison: task 7 queue + multi-unit + TG.9`

### Task 8 -- Scenario hardening + harness/soak integration

**Goal.** The three scenarios are PR-gate citizens; the soak can exercise garrison behind a flag.

**Steps.** (1) Finalize S8/S9/S10 asserts (T-G.6/7/8 full tables: filing time bounds, occupancy counts, zero escalations at settle, egress strays = 0). (2) Harness registration for the new actors/records (extend the phase-1 func harness pattern). (3) Soak: `Battle.Garrison.SoakOrders` = 1 adds garrison targets to the T1.8 controller's seeded order mix (structures registered by the soak bootstrap); flag off = byte-identical baseline behavior (R-G1). (4) Nightly: run the garrison soak variant once on the staged build as evidence (baseline JSON untouched while the flag defaults off).

**Acceptance check.** Func tier green with S8/S9/S10 added to the PR set; soak variant run: settled garrison at end of soak, stuck = 0, separation = 0 at the settled gate, archetype count still 6; default-off soak output identical to the T1.8 baseline shape.

**Commit.** `garrison: task 8 scenarios + soak flag`

### Task 9 -- Overlay, metrics, docs, final verification

**Goal.** B.8 closure: the capability is observable, measured, documented, and regression-protected.

**Steps.** (1) Debug overlay (A3 hook pattern): garrison phase color per unit, structure slot draws (occupied/free colors), queue points, lease state. (2) Metrics: filing time (order -> settled), queue wait, occupancy; wired into `Battle.DumpMetrics` + a `Battle.Garrison.Metrics` EXEC_RESULT line. (3) B.7 regression-checklist entry (roadmap): garrison gates + invariants appended. (4) Docs: `Docs/README.md` map rows for the spec + phase folder, status block update; ADR-18..21 rows handed to the owner for the architecture decision log (spec §8 -- spec edits are owner-applied per AGENTS §7; the report lists them as proposals); any facts corrections per AGENTS §4. (5) Final verification: full unit + func tiers, one nightly smoke + garrison soak variant, git-log gate-first audit.

**Acceptance check.** B.8 checklist (§7) fully checked with evidence pointers; CI green; progress log complete; handoff report written per AGENTS §5.

**Commit.** `garrison: task 9 overlay + metrics + docs` then `garrison: complete`

## §6. Gate -> implementation map

| Gate | Tier | Task | Test name | Asserts (short) |
|------|------|------|-----------|-----------------|
| TG.1 | Unit | 2 | `Battle.Unit.Garrison.Stamping` | footprint impassable; corridor clear (SegmentIsClear); deterministic stamps |
| TG.2 | Unit | 3 | `Battle.Unit.Garrison.InternalPath` | door passage; arc-length monotone; Z monotone on stairs; egress = reverse |
| TG.3 | Unit | 3 | `Battle.Unit.Garrison.SlotAssign` | bijection; distance bound; determinism; disjoint ranges |
| TG.4 | Unit | 4 | `Battle.Unit.Garrison.Booking` | capacity; lease exclusivity; release; stale handles; hash stability x2 runs |
| TG.5 | Unit | 3 | `Battle.Unit.Garrison.ZModel` | Z interpolation monotone; slot Z fixtures (wall 6 m / trench -0.75 m) |
| TG.6 | Func | 5-6 | `Battle.Func.S8_BuildingGarrison` | files through door; occupancy; settled; yaw authority; zero escalations at settle; egress re-form |
| TG.7 | Func | 6 | `Battle.Func.S9_TrenchGarrison` | ramp filing; sunken posts (Z < 0); egress |
| TG.8 | Func | 6 | `Battle.Func.S10_WallGarrison` | stair climb (Z rise); wall-top spread; outward yaw; egress down |
| TG.9 | Func | 7 | `Battle.Func.S8b_MultiUnitGarrison` | second unit queues then books; disjoint ranges; sequential egress; no deadlock |
| TG.10 | Soak | 8 | `Battle.Soak.GarrisonOrders` | garrison-mix soak: settled end-state, stuck = 0, separation = 0, archetypes constant |

## §7. DoD checklist

- [ ] TG.1-TG.10 green with evidence (unit tier + func tier via `run_tests.bat`; soak via `nightly.bat` step 3b variant) -- counts and EXEC_RESULT lines in the report
- [ ] B1-B7 behavior contract demonstrable (spec §0): each row maps to a passing assert or an EXEC_RESULT demonstration line
- [ ] B.8 item 1: unit tests over the math (BattleGarrison seam: paths, assignment, queue points, Z model)
- [ ] B.8 item 2: scenario tests S8/S9/S10 in the PR gate
- [ ] B.8 item 3: debug overlay exposing garrison phases, slots, occupancy, queue points
- [ ] B.8 item 4: metrics in the instrumented set (filing time, queue wait, occupancy)
- [ ] B.8 item 5: B.7 regression-checklist entry
- [ ] B.8 item 6: `Battle.Garrison.*` CVAR flags; `Battle.Garrison.Enable=0` and `SoakOrders=0` leave the T1.8 baseline byte-identical
- [ ] B.8 item 7: Iris-ready -- booking state reconstructible from spec + orders; no raw pointers across the registry/subsystem boundary; no new RNG streams
- [ ] Archetype count 6 constant through all scenarios and the garrison soak (F11)
- [ ] State-hash stability with garrison terms (T-G.4 two-run assertion)
- [ ] No per-frame allocations in UnitLocomotion/FormationSlotResolve hot paths beyond the existing pattern (garrison copies are event-rate, at transitions)
- [ ] Gate-first verified via git log (every TG test lands in its owning task's commit or prior)
- [ ] `Docs/phases/phase-garrison/progress.md` complete; `Docs/README.md` map + status updated; handoff report per AGENTS §5; ADR-18..21 proposals listed for the owner

## §8. System prompt

```text
You are the MassBattle Garrison-phase implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-garrison/brief.md.
Execute the brief autonomously under the standing protocol; brief §3.1
decisions are owner-approved. Stop at the brief's DoD (§7) after writing
Docs/phases/phase-garrison/report.md and the final commit.
```