# MassBattle -- Phase 1 BRIEF (v1.0 -- owner-approved, ready to execute)

| | |
|---|---|
| **Companion to** | `Docs/spec/architecture.md` (arch), `Docs/spec/roadmap-and-testing.md` (roadmap), `Docs/reviews/plan-review-findings.md` (findings), `Docs/phases/phase-0/report.md` (phase-0 handoff), `Docs/phases/phase-0/audit.md` (the audit) |
| **Engine pin** | UE 5.8.4 (CL 58902052) at `E:\Unreal\UE_5.8` -- project `E:\Unreal Projects\MassBattle\MassBattle.uproject` |
| **Run mode** | Fully autonomous, single run (same protocol as Phase 0): progress log, one commit per task, reality-wins, stop at DoD |
| **Priority directive** | **The pure-math movement kernel (T1.9, T1.2, T1.3, T1.4, T1.1) is the critical path** -- Tasks 3-7. Land the API audit (Task 1) and data spine (Task 2), then nothing starves the kernel. Editor work is batched; the kernel stretch runs headless. |
| **Status** | **FINAL** -- owner approved A1-A5 on 2026-10-25; all approved items are merged into Part I and Part II is the decision log. Hand this file to the executing agent as-is. |

**How this document is organized**

- **Part I** is the operational brief and is self-consistent: full Phase 1 scope (§5 task plan) with the owner-approved decisions of §3.1 already folded in, the pre-researched movement-stack API digest (§4) so the agent verifies symbols instead of discovering them, and the agent system prompt (§8).
- **Part II** is the decision log (owner verdicts + rationale). The executing agent does not need it; it is kept for provenance.

---

# PART I -- BRIEF

## §0. Mission, read order, token discipline

**Mission.** Build the Phase 1 movement core per roadmap "Phase 1 -- Movement core": heightfield path planner (T1.9), path-frame layout (T1.2), unit locomotion math including the E2-corrected two-step wheel cap and compression (T1.3, T1.4), locality slot assignment (T1.1), entity servo with per-state avoidance weights per decision A1, unit-vs-unit OBB contact with yield/block/pass + shove, unstuck ladder v1, the A2 command surface (thin order API), S1/S2/squeeze scenarios green, and the T1.8 nightly mini-soak. Exit criteria (roadmap): 8 units execute march, 90-degree wheel, mutual pass, and squeeze-through-gap with zero stuck events over a 10-minute soak; no flank shedding on wheels.

**Read order (exact -- do not read more than this before coding):**

1. This brief: §1-§4 fully, then each task section of §5 just before executing it.
2. `Docs/reference/engine-api-5.8.md`: Part A §2-3 (processor/query signatures, flush contract) + Part B §4.3-4.5 (plus §4.10-4.11 when automation/ExecBridge work starts). Cite, never re-derive.
3. Arch: §8 (all -- movement doctrine, formulas, state table), §4 (pipeline + the 5.8 signature note), §12 (budgets/tunables), §3.1 (fragment rules).
4. Roadmap: Part A "Phase 1 -- Movement core" + T1 gate table + B.0 (seam, naming, test-mode contract), B.1 (gray-box), B.3 (S1/S2 assertions), B.8 (capability DoD).
5. `Docs/phases/phase-0/report.md` §7 ("Where Phase 1 should start" -- ready hooks).
6. `Docs/phases/phase-0/audit.md`: §2 (the contract-to-test drift pattern), §5 (plan-level observations), and findings M1/M2/M3 (the three assertion-shape rules encoded in ground rule 1).
7. Findings file: blocks **R2, R4, R5, E2** only.
8. `Docs/reference/mcp-playbook.md` -- skim §3 (operating rules) + §8 (correction ledger) before the first editor session.

**Token discipline rules.**

- Never re-derive a fact §1 states. Never re-research an API §4 states -- cite the digest entry (e.g. "§4.3") in the progress log instead.
- If reality contradicts this brief, **reality wins**: fix the brief's fact in place, log the discrepancy in `Docs/phases/phase-1/progress.md`, and mark the affected §4 entry for the Task 1 audit table.
- §4 symbols marked `[public]`/`[verify]` follow the verify-once-then-trust protocol (§4.0): Task 1 verifies each against the pinned source, then the merged reference file is trusted for the rest of the run.

## §1. Phase facts digest (pre-verified, new to this phase)

### 1.1 Carried facts (verified in Phase 0 -- trust; cite `Docs/phases/phase-0/report.md` §7)

| # | Fact |
|---|------|
| 1.1 | Engine + project pin: UE 5.8.4 (CL 58902052) at `E:\Unreal\UE_5.8`; project `E:\Unreal Projects\MassBattle\MassBattle.uproject`; all game C++ in `Plugins/MassBattle/Source/MassBattle/`; game module `MassBattleGame` stays empty (`Docs/reference/environment.md`). |
| 1.2 | Ready hooks: `FUnitStatsSharedFragment` is written by UnitSync every frame; per-unit archetypes localize death flips; `UBattleRngSubsystem` provides named RNG streams; `FBattleStateHash` gains transform terms (1 cm quantization) this phase -- Task 2. |
| 1.3 | The death path currently destroys corpses immediately (receiver-deferred) -- Phase 5 replaces it; build no corpse/representation anything this phase. |
| 1.4 | CI: per-commit gate + Jenkins `massbattle-gate` green; nightly `massbattle-nightly` (cron 02:00) wired -- **Jenkins must be running for the cron to fire** (start line in `Tools/CI/JENKINS.md`). Nightly drives the native game-side Gauntlet framework on the staged build (ADR-16). |
| 1.5 | Harness shape: functional tests follow the phase-0 pattern (S0 tests + static factory registry + `PrepareTest`/manual-completion lifecycle) -- see `Docs/phases/phase-0/progress.md`; reuse it for S1/S2/squeeze. Test-mode contract: `UBattleTestSettings::ApplyTestMode()` / `AssertTestMode()` (`Battle.Test.*` CVars), applied + asserted at every S-scenario start (roadmap B.0). |
| 1.6 | Loop commands: `Tools/CI/run_tests.bat unit` runs the per-commit tier headless with the editor closed; `func` tier runs functional tests on the functional map. These two carry Tasks 2-9 and 10-11 respectively. |

### 1.2 New-territory facts (public research, `[verify]` in Task 1)

| # | Fact |
|---|------|
| 1.7 | **Movement engine symbols are NOT yet in `Docs/reference/engine-api-5.8.md`** (Part A "ExecutionOrder" group names are the only movement-adjacent row). §4 below is the pre-researched digest; Task 1 audits it and merges results into the reference file. This is the phase's only unverified-API territory. |
| 1.8 | **Module homes** `[public]`: MassMovement and MassNavigation live in plugins (`/Engine/Plugins/Runtime/MassGameplay/Source/MassMovement/`, `/Engine/Plugins/Runtime/MassAI/Source/MassNavigation/`). Phase 1 adds **MassMovement** to `MassBattle.Build.cs` private deps (same pattern as the MassLOD audit-fix addition). MassNavigation is **not** linked under decision A1 (read its source on disk for the audit only). |
| 1.9 | **Unit boundary** `[public]`: engine fragments are centimeters (`FAgentRadiusFragment.Radius = 40` means 40 cm); project formulas (arch §8.2/8.6/12) are meters. Convert at fragment boundaries only -- ground rule 2. This is the E2 class of bug (dimensionally plausible, wrong). |
| 1.10 | **Mass Avoidance status** `[public]`: force-based (`FMassForceFragment`) and **Experimental** in 5.8 ("use caution when shipping with it"). Decision A1 does not adopt it; the digest still records it because Task 1 verifies the force->velocity semantics that justify A1. |
| 1.11 | Scale sanity: Phase 1 scenarios run 8 units x ~40 entities = ~320 sim entities; CrossField neighbor queries use roster + OBB data, not a spatial hash (Phase 2 owns the arch §7.1 grid). Do not pull the spatial hash forward. |

## §2. MCP operating rules

Operating manual + gotchas ledger: `Docs/reference/mcp-playbook.md` (do not rediscover). Phase-1 addition: during the kernel stretch (Tasks 2-9) the editor stays **closed** -- UBT + `run_tests.bat unit` cycles only; the MCP server is dead then, which is fine: nothing in those tasks needs it. Batch editor work into the functional stretch (Tasks 10-12) per playbook §3.7 (restart batching).

## §3. Ground rules (phase-specific additions to `Docs/AGENTS.md`)

1. **Gate-first test skeletons.** For every T1.x gate, the assertion-shape skeleton (test file with the exact asserts from the T1 gate-table wording, failing or skipped) is committed in the same commit as -- or before -- the implementation task that owns it. Writing rules from the audit (M1/M2/M3): assert against **live queries** (presence-None counts, not flags); assert against **active engine state** (not project CVARs); use `TestEqualSensitive` for any string/canonical compare. This makes the audit's contract-vs-test drift pattern (audit §5.1) structurally impossible to repeat.
2. **Units: fragments cm, math m.** Convert at fragment boundaries only; the golden tests carry one cm/m conversion case each (fact 1.9).
3. **No engine movement/avoidance processors on battle entities** (decision A1): no `FMassMoveTargetFragment`, no `FMassMovementParameters`, no SteerToMoveTarget/Avoidance adoption in v1. `Battle` processor-group ordering stays self-contained (arch §4; engine group names are recorded for reference only).
4. **Overlay drawing scope (decision A3):** path polyline, slot-error magnitude, unstuck ladder level -- only. Minimal `DrawDebug`-class lines/boxes via the debug subsystem; everything else stays hook-only per the Phase 0 A1 decision.
5. **Determinism:** no wall-clock anywhere in movement (the test contract owns time); handle-ordered tie-breaks (index, serial); all noise via `UBattleRngSubsystem` named streams; no `FMath::RandRange` in processors.
6. **Headless-first ordering** (§2): Tasks 2-9 must not require editor launches; any surprise that demands one is logged as a deviation.
7. **State-hash transform terms land in Task 2** (1 cm quantization), not at S1 time -- S1's separation assert and the Phase 2 determinism gates both want them.
8. **MassInsights over prints:** wheel/formation debugging uses the verified trace path (`engine-api` §12) rather than new per-chunk logs -- the M6 lesson (hot-path logging) generalizes.

### §3.1 Owner-approved decisions (A1-A5, decided 2026-10-25) -- pre-decided; do not reverse or re-litigate

| ID | Decision | Lands in |
|----|----------|----------|
| A1 | **R2 avoidance policy -- Option A:** the 8.3 state table's "avoidance" column is implemented as per-state weights on a **project velocity-space avoidance term** inside Servo/Integrate; engine Mass Avoidance (force pipeline) is **not adopted** in v1; the conversion column is recorded as "n/a -- velocity-space native" per state (authorized arch 8.3 patch, Task 8). Option B (engine avoidance behind a CVAR) re-evaluates only if CrossField RVO underperforms T1.5/T1.6, with `dV = F * DT` (unit mass) as the documented conversion and a determinism probe before any gate relies on it. | Tasks 1, 8 |
| A2 | **R5 command shell -- thin C++ order API + ExecBridge smoke test:** registry-owned order functions (`IssueMoveOrder(handle, anchor, facing)`, `IssueStop`, drag-place params) + `Battle.Order.*` ExecBridge commands + one smoke functional test (issue Move, unit reaches anchor). **Selection/picking/UI/camera explicitly out of scope v1** (Phase 5 territory per R5's Phase 1-2 landing). | Task 10 |
| A3 | **Overlay drawing scope Phase 1:** path polyline, slot-error magnitude, unstuck ladder level -- only (ground rule 4). | Tasks 2, 3, 7, 8 |
| A4 | **Deferred items disposition:** soak-map cook-list entry = in scope (Task 12); Developer-type test module split (Shipping link fix) = in scope, last code task (Task 13); `Mass/EntityHandle.h` include fix = opportunistic (Task 1); `.uproject` `TargetAllowList` pruning = stays owner-deferred; spawn re-baseline = stays T2.9 (Phase 2). | Tasks 1, 12, 13 |
| A5 | **Squeeze/morph geometry = test-only cost-grid override:** ExecBridge `Battle.Test.StampImpassable minX minY maxX maxY` (or a `ABattleTestCostStamp` actor) stamps impassable cells the planner already consumes; **no authored corridor map**, no MCP map labor. | Tasks 3, 11 |

With A1-A5 pre-decided, **no decision point remains inside Phase 1**: DP-1 (R2 conversion) is A1; DP-2 (command shell) is A2; DP-3 (overlay drawing) is A3; DP-4 (deferred items) is A4; DP-5 (make-way corridor) was already closed by arch Open Decision 4 (shove-only first); DP-6 (which states route through engine avoidance) is subsumed by A1 (none).

### §3.2 Risk watchlist (keep visible during the run)

| Risk | Watch | Mitigation hook |
|------|-------|-----------------|
| R2 regression | Servo behavior diverges from the 8.3 table | T1.5/T1.6 assertions + A1 conversion column |
| R4 planner perf | Re-plan latency unbounded when anchors move | T1.9 bounded re-plan assert |
| cm/m unit mixing | `FAgentRadiusFragment` 40 cm vs 0.4 m math | ground rule 2 + golden conversion case |
| Wheel-cap edge cases | Degenerate radii, straight march | T1.3 golden cases (E2): in-place pivot r_path->0, straight march = wheel cap inactive, facing cap active separately |
| S2 stalls | Pairwise-avoidance deadlocks | T1.6 no-pairwise-stall assert; asymmetric handle-ordered tie-break |
| Soak on staged build | Test contract not applied on the `-game` staged path | verify `ApplyTestMode` on staged boot (Task 12); cook-list entry (A4) |
| Silent cron death | Jenkins not running -> nightly silently missing | `JENKINS.md` start line (Task 12 verify) |
| Restart thrash | 15 tasks x editor launches | headless-first ordering; batch edits (§2) |
| S2 prerequisites | Shove needs OBB overlap queries live before scenarios | Task 9 lands before Tasks 10/11 -- ordering is binding |

## §4. Pre-researched movement-stack API digest (verify-then-trust)

> **§4.0 Protocol.** Symbols below are `[public]` (Epic docs / community source excerpts, 5.8-updated) unless marked `[verify]`. Nothing here is yet in `Docs/reference/engine-api-5.8.md`. Task 1 audits every row against the pinned source (`E:\Unreal\UE_5.8\Engine\Plugins\Runtime\MassGameplay\Source\MassMovement\`, `...\MassAI\Source\MassNavigation\`) and merges corrections into the reference file (corrections-table format; sources into Part C). After Task 1, trust the reference file over this digest.

### §4.1 Module map

| Module | Home | Phase 1 relevance |
|--------|------|-------------------|
| MassMovement | MassGameplay plugin (`/Engine/Plugins/Runtime/MassGameplay/Source/MassMovement/`) | `FMassVelocityFragment`, `FMassForceFragment`, `FMassMovementParameters`, apply-force / apply-movement processors. **Add to `MassBattle.Build.cs` private deps** (Task 1). |
| MassNavigation | MassAI plugin | Reference-only under A1 (avoidance colliders, `FMassMoveTargetFragment`, obstacle grid). Read on disk for the audit; do not link. |
| MassCommon | MassGameplay plugin | Already linked (`FTransformFragment`, `FAgentRadiusFragment`, group names). |
| MassBattle (project) | `Plugins/MassBattle/Source/MassBattle/` | All Phase 1 processors (UnitLocomotion, FormationSlotResolve, Servo, Integrate) are new code here -- the engine movement stack is *reference*, not base class. |

### §4.2 What the engine movement pipeline actually does (the R2 crux)

1. **"Force" is a velocity-space accumulator, not a physical force.** `UMassSteerToMoveTargetProcessor` (MassNavigation) writes the goal force: `Force.Value = SteerK * (Steering.DesiredVelocity - Velocity.Value)` with `SteerK = 1 / MovingSteeringParams.ReactionTime` -- a P-controller on velocity, structurally identical to the project servo `V = clamp((SlotPos - Pos) / Tau, ...)`. **The project's Tau is the engine's ReactionTime.** `[public]`
2. **Avoidance adds into the same accumulator.** `UMassMovingAvoidanceProcessor` (Experimental) sums predictive avoidance + separation forces from nearby colliders (`FMassAvoidanceColliderFragment`) and edges into `FMassForceFragment`, gathering neighbors via `FNavigationObstacleHashGrid2D` (owned by `UMassNavigationSubsystem`, fed by `UMassNavigationObstacleGridProcessor`). `[public]`
3. **ApplyForce integrates at implicit unit mass.** `DesiredVelocity += Force * DeltaTime` (the fragment is an acceleration in disguise); clamps by `FMassMovementParameters` (MaxSpeed / MaxAcceleration) apply in this area `[verify]`.
4. **ApplyMovement integrates position** ("updates entities position based on desired velocity") `[verify]` -- exact body: transform += velocity*dt, any ground snap / Z handling.
5. **Order:** SteerToMoveTarget runs `ExecuteAfter Tasks, ExecuteBefore Avoidance`; then Avoidance; then ApplyForces; then Movement (built-in group names already recorded: `engine-api` Part A §2, `MassCommonTypes.h`). `[public]`
6. **Debug surface if ever adopted:** Visual Logger categories `LogAvoidance`, `LogAvoidanceVelocity`, `LogAvoidanceAgents`; gameplay debugger Shift+V. `[public]`

**Consequence (A1's footing):** the ADR-07 doctrine ("velocity control, never force accumulation") and the engine pipeline are the same math wearing different units. R2 was never a conversion-policy question -- it is a choice of *whose neighbor-finding and avoidance math* feeds the servo. A1 answers: the project's own.

### §4.3 Symbol table

| Symbol | Module | Verdict + notes |
|--------|--------|-----------------|
| `FMassVelocityFragment` | MassMovement | `FVector Value` -- actual physical velocity. Project Integrate owns this or a project fragment; Task 1/2 picks one and records it as an arch §3.1 note (the §3.1 row already exists; duplicate the concept never, the name is fine). |
| `FMassForceFragment` | MassMovement | `FVector Value` -- steering accumulator (acceleration-like). Not used under A1. |
| `FAgentRadiusFragment` | MassCommon | `float Radius = 40.f` -- **centimeters** (fact 1.9). |
| `FTransformFragment` | MassCommon | Already used by the Phase 0 spawn path. |
| `FMassMoveTargetFragment` | MassNavigation | Engine move target. **Rejected by A1** (targets live in slot/formation data). Required as an *input* by the engine avoidance processors -- coupling noted for the audit (`[verify]` hard or optional). |
| `FMassSteeringFragment` | MassNavigation | `DesiredVelocity` cache -- engine comment says "not used for moving the entity". Reference only. |
| `FMassMovementParameters` | MassMovement | Const shared struct (MaxSpeed, MaxAcceleration, ...) `[verify]` fields. **Not adopted** -- unit speed authority is `FUnitStatsSharedFragment.SpeedMult` + the §8.2 Min3 limit; double-sourcing speed is a bug factory. |
| `UMassSteerToMoveTargetProcessor` | MassNavigation | Goal-force writer; its order pattern (after Tasks, before Avoidance) is the placement template for UnitLocomotion. |
| `UMassMovingAvoidanceProcessor` / `UMassStandingAvoidanceProcessor` | MassNavigation (Avoidance) | Experimental, force-based; neighbor grid `FNavigationObstacleHashGrid2D`. Not adopted under A1. |
| `UMassNavigationObstacleGridProcessor` | MassNavigation | Feeds the obstacle grid per entity (requires Navigation Obstacle trait). |
| `FMassMovingAvoidanceParameters` / `FMassStandingAvoidanceParameters` | MassNavigation (`MassAvoidanceFragments.h`) | Tunables; param doc lives in that header. Not adopted. |
| `UMassApplyForceProcessor` | MassMovement | "Calculate desired movement based on input forces" -- the unit-mass integration `[verify]`. |
| `UMassApplyMovementProcessor` | MassMovement | "Updates entities position based on desired velocity" `[verify]`. |
| `UMassSimpleMovementProcessor` / `UMassSimpleMovementTrait` | MassMovement | Trivial transform += velocity*dt path -- minimal reference for the custom Integrate. |
| Spring movement family (`USpringMovementTrait` et al.) | MassMovement | Newer 5.x addition; note-only (the project servo is first-order). |
| `UE::Mass::ProcessorGroupNames::Avoidance / ApplyForces / Movement` | MassCommon | Already recorded (`engine-api` Part A §2). Under A1 the Battle processors do not interoperate with these; keep `Battle` group ordering self-contained. |

### §4.4 Verify-on-machine checklist (Task 1)

1. `UMassApplyForceProcessor::Execute` -- exact integration + clamp order (`[public]`: `DesiredVelocity += Force*DT`, unit mass).
2. `UMassApplyMovementProcessor::Execute` -- transform integration, any ground snap / Z handling.
3. `FMassMovementParameters` field list; whether any default is silently cm-vs-m mixed.
4. `UMassMovingAvoidanceProcessor` query requirements -- is `FMassMoveTargetFragment` a hard requirement? Is grid iteration order deterministic (A1 Option B contingency)?
5. Exact header paths for all §4.3 rows (they belong in the reference file's source column).
6. Whether battle entities adopt `FMassVelocityFragment` or the project adds `FVelocityFragment` (arch §3.1 row exists; pick one in Task 2 and record it).

### §4.5 A1 implementation shape (binding)

Per-state weights on a project velocity-space avoidance term inside Servo/Integrate: **HoldSlot** = none (servo + compression own crowding); **ManeuverToSlot** = medium, teammates-only (roster/formation fragments); **CrossField** = full RVO + asymmetric tie-break (handle-ordered); **PressRing / Charge / Flee** rows arrive with their phases -- record the column "n/a -- velocity-space native" for all six rows in the arch 8.3 patch (Task 8). CrossField neighbor queries read roster + OBB data at Phase 1 scale (8 units x ~40) -- no spatial hash. The 8.6 OBB resolver (Task 9) feeds cross-unit contact into the same term.

### §4.6 Sources for §4 (Task 1 merges into `engine-api-5.8.md` Part C)

- Epic 5.8 docs: Mass Avoidance Overview (Experimental status, processors, fragments, grid, debug categories); MassMovement API page (processor/struct inventory).
- vrealmatic Mass wiki, "Steering" page (5.8-updated signatures; goal-force formula; trait/fragment shapes).
- Community MassGameplay source-analysis repo (ApplyForce `DesiredVelocity += Acceleration * DeltaTime` with `Acceleration = Force` -- marked `[verify]`).

## §5. Phase 1 task plan

Scope = roadmap Phase 1 **in full** plus the owner-approved decisions §3.1 (A1-A5). Ordering = headless-first (§2); the kernel chain (Tasks 3-7) is the critical path; one task = one commit (+ progress-log lines + gate evidence where applicable); ground rule 1 (gate-first skeletons) applies to every T1.x task. Dependency shape:

```
 Task 2 data spine (fragments, processor skeletons, CVAR gates, state-hash transform terms)
   |
   +--> [T3] Cost grid + path planner (T1.9) -- pure; stamp-override hook (A5)
   |        |
   |        +--> [T4] Path-frame layout (T1.2)                     } the five
   |        +--> [T5] UnitLocomotion: Min3 + E2 wheel cap (T1.3)   } decision-free
   |        +--> [T6] Compression model (T1.4)                     } pure-math
   |        +--> [T7] Slot assigner + reflow + swaps (T1.1)        } tasks
   |
   +--> [T8] Servo + Integrate (A1 per-state weights, unstuck v1)   <-- A1 consumed here
            |
            +--> [T9] Unit-vs-unit OBB contact + yield/block/pass + shove
                     |
                     +--> [T10] Command surface (A2) + S1 march+wheel (T1.5)  -- editor stretch
                     +--> [T11] S2 pass-through shove (T1.6) + squeeze/morph (T1.7)
   +--> [T12] T1.8 mini-soak (nightly, staged build)                <-- A4 cook-list lands here
   +--> [T13] Developer test-module split (A4)
```

### Task 0 -- Kickoff
1. Read order per §0.
2. `git status` clean check on `main` (repo convention: commit directly to `main`, prefix `phase1:`).
3. Create `Docs/phases/phase-1/progress.md` with the session header (date, engine, project, protocol line).
4. If the editor is closed, defer the MCP refresh (restart batching, §2) -- the kernel stretch does not need it.

**Verify**: clean tree; progress log exists. **Commit**: `phase1: kickoff`.

### Task 1 -- Pinned-API audit (movement stack) + Build.cs dep + A4 include fix
1. Audit the §4.4 checklist row by row against the pinned source (`§4.0` paths); record per-row verdicts.
2. `MassBattle.Build.cs`: add `MassMovement` to private deps (fact 1.8; MassNavigation stays unlinked per A1).
3. Merge corrections into `Docs/reference/engine-api-5.8.md` in the corrections-table format (Part A style); move §4.6 sources into Part C.
4. Opportunistic (A4): `MassEntityHandle.h` include -> `"Mass/EntityHandle.h"` across the plugin.
5. Record the velocity-fragment adoption decision (§4.4 item 6) as an arch §3.1 note (Phase-0-A7-style authorized patch).

**Verify**: audit table merged with per-row verdicts; UBT build green with the new dep; `run_tests.bat unit` still green; include fix compiled. **Commit**: `phase1: movement API audit + MassMovement dep`.

### Task 2 -- Movement data spine
1. Fragments per arch §3.1 + ADR-14: `FUnitFormationSharedFragment` (unit handle anchor, path, anchor/facing/speed-limit/spacing-scale, generation) created through the handle-seeded hash; per-entity movement-state fragment (8.3 enum -- acceptable fallback form per arch §3.2), slot/servo per-entity fragments as §3.1 rows specify.
2. Processor skeletons registered + ordered per arch §4 rows 1/6/7/8 -- UnitLocomotion (game thread), FormationSlotResolve / Servo / Integrate (worker) -- each behind a `Battle.Movement.*` CVAR gate (B.8 item 6), all no-op.
3. State-hash transform terms: 1 cm quantization into `FBattleStateHash` (ground rule 7).
4. Archetype probe: unit test asserts movement fragments on flagged entities (officers/banner) do not unintentionally double archetypes.

**Verify**: compile + `run_tests.bat unit` green; processors visible in the execution ordering; hash stability test green. **Commit**: `phase1: movement data spine`.

### Task 3 -- Cost grid + path planner (T1.9) -- critical path
1. Heightfield cost grid over the flat tile (cell size 3 m, arch §12) as plain structs on the pure-math seam; consumed by UnitLocomotion.
2. Planner free functions: A*/jump-point search with slope-aware costs, path smoothing, path-frame sampler.
3. A5 stamp override: ExecBridge `Battle.Test.StampImpassable minX minY maxX maxY` (or `ABattleTestCostStamp` actor) writing impassable cells the planner already consumes.
4. T1.9 skeleton first (ground rule 1), then implementation; bounded re-plan when anchors move [R4].

**Verify (T1.9 verbatim)**: paths valid against impassable cells; arc-length monotonicity; deterministic under seeded RNG; bounded re-plan latency when anchors move. **Commit**: `phase1: cost grid + path planner (T1.9)`.

### Task 4 -- Path-frame layout (T1.2)
1. `FPathFrameLayout::Project` per arch §8.6 code: slot = PathPoint(s_anchor - rank * RankSpacing) + PathNormal(s) * lateral offset; per-rank facing = local path tangent; degenerates to anchor + R(facing) on straight paths.
2. Slot layout function for formed ranks/files (consumed by the assigner).
3. T1.2 skeleton first, then implementation; cross-check the straight-path degeneracy against the anchor formula in the same test.

**Verify (T1.2 verbatim)**: rank arc-lengths monotonic; laterals orthogonal to tangent; straight-path degeneracy equals anchor formula. **Commit**: `phase1: path-frame layout (T1.2)`.

### Task 5 -- UnitLocomotion math (T1.3)
1. Implement arch §8.2 verbatim (two-step wheel cap, E2-corrected): `OmegaMax = MaxMemberSpeed / max(OuterRadius, eps)`; `WheelLinearLimit = OmegaMax * max(PathRadiusOfAnchor, eps)`; `FacingLinearLimit = FacingDeltaRate * max(PathRadiusOfAnchor, eps)`; `UnitSpeedLimit = Min3(CommandedSpeed, MinOverMembers(EffectiveSpeed(i) * 1.25), min(WheelLinearLimit, FacingLinearLimit))`.
2. Hysteresis on the limit with a seeded-noise input loop (named RNG stream -- name it in the test, do not improvise).
3. Anchor progression along the planner path; commanded facing-change rate cap.
4. T1.3 skeleton first; golden cases carry the E2 degenerate set: in-place pivot (r_path -> 0), straight march (wheel cap inactive), facing cap active separately; one cm/m conversion case (ground rule 2).

**Verify (T1.3 verbatim)**: limit = min(commanded, slowest x 1.25, v_wheel, facing cap); golden cases pass; hysteresis holds under member noise. **Commit**: `phase1: unit locomotion math (T1.3)`.

### Task 6 -- Compression model (T1.4)
1. `slot = anchor + R(facing) * LayoutFn(slot) * SpacingScale`; floor 60% of nominal spacing; recovery ~10%/s (arch §8.2, §12).
2. T1.4 skeleton first; step-input test for oscillation.

**Verify (T1.4 verbatim)**: 60% floor respected; recovery rate bounded; no oscillation under step input. **Commit**: `phase1: compression model (T1.4)`.

### Task 7 -- Slot assigner (T1.1)
1. Greedy per-file locality fill (entities bid for nearest vacant slot); bijection guarantee; <= k-file displacement bound.
2. Batched reflow: ~0.5 s interval or on detected rank gap (arch §8.4, §12) -- never per-casualty instant; idempotent.
3. Predictive swaps (same-unit pairs whose servo paths cross within ~1 s swap slot indices) behind a CVAR.
4. Wire FormationSlotResolve (row 6) to consume assignment + shared formation and write move targets; slot-error overlay hook (A3).

**Verify (T1.1 verbatim)**: assignment is a bijection; <= k-file displacement; predictive swap terminates; batched reflow idempotent. **Commit**: `phase1: slot assigner (T1.1)`.

### Task 8 -- Servo + Integrate (A1 consumed here -- DP-1)
1. Servo per arch §8.3: `V = clamp((SlotPos - Pos) / Tau, MaxSpeed * CatchupBoost)`, Tau 0.25 s, catchup 1.25x, acceleration clamp (§12) -- velocity control, never force accumulation (ADR-07).
2. Per-state avoidance weights per §4.5 (A1): HoldSlot none; ManeuverToSlot medium teammates-only; CrossField full RVO + asymmetric handle-ordered tie-break; neighbor data from roster/OBB fragments.
3. Unstuck ladder v1 (arch §8.5): 1) double avoidance weight -> 2) sidestep impulse perpendicular to servo -> 3) forced slot swap with blocking teammate -> 4) escalate to unit AI (displacement < eps -> temporary column, sidestep order, or hold); every step logged + colored (A3).
4. Integrate: own `FMassVelocityFragment` or the project fragment per the Task 1/2 decision; transform integration modeled on `UMassSimpleMovementProcessor` (§4.3); no engine movement processors (ground rule 3).
5. Apply the authorized arch patch (A1): conversion column "n/a -- velocity-space native" added to the 8.3 state table for all six states; call it out in the report (A7 pattern).

**Verify**: unit tests for servo clamps + per-state weights + ladder escalation order and termination; arch 8.3 patch applied. **Commit**: `phase1: servo + integrate + unstuck ladder (A1)`.

### Task 9 -- Unit-vs-unit OBB contact + policies
1. Unit-body OBB overlap queries at Phase 1 scale (8 units -- roster data, no spatial hash).
2. Policy resolution: yield / block / pass-through by friendliness, mass, mobility, right-of-way (arch §8.6).
3. Pass-through shove: cavalry anchor proceeds at ~50-60% speed with lowered compression floor; infantry entities in the computed overlap zone receive displacement velocity blended over the servo (`w = clamp(MassRatio * OverlapDepth)`); servo recovers the wake.
4. Enforce the §8.2 hard rule: no facing change while interpenetrating another unit.

**Verify**: unit tests for overlap + policy table + shove blend bounds; wake recovery asserted on a scripted two-unit case. **Commit**: `phase1: unit-vs-unit OBB contact + shove`.

### Task 10 -- Command surface (A2) + S1 march + wheel (T1.5) -- editor stretch starts
1. Registry-owned order functions: `IssueMoveOrder(handle, anchor, facing)`, `IssueStop`, drag-place params (A2). Selection/picking/UI/camera stay out of scope v1.
2. `Battle.Order.*` ExecBridge commands emitting `EXEC_RESULT` evidence lines; one smoke functional test: issue Move -> unit reaches anchor.
3. S1 on `L_MassBattle_T0_Functional` (reuse; no new map): 8 units march + 90-degree wheel per roadmap B.3; test-mode contract applied + asserted at scenario start (fact 1.5).
4. Batch all editor-side work from here on (§2); the phase-0 functional-harness pattern (fact 1.5) is the template.

**Verify (T1.5 verbatim)**: zero unstuck escalations; separation < threshold; no flank shedding. Smoke test green via `run_tests.bat func`. **Commit**: `phase1: command surface (A2) + S1 (T1.5)`.

### Task 11 -- S2 pass-through shove (T1.6) + squeeze-vs-morph (T1.7)
1. S2: cavalry passes through friendly infantry; assert no pairwise stalls; wake re-dresses to < slot-error threshold within T; pass time bounded (B.3 S2).
2. Squeeze-vs-morph: `PassWidth >= FormationWidth * CompressionFloor` -> squeeze; else morph to column (`files = floor(PassWidth / FileSpacing)`) via layout re-parameterization + locality re-slot (arch §8.6).
3. Commit rule: morph locks once the lead rank crosses the mouth -- no re-decision mid-corridor; no thrash; clean re-deploy past exit.
4. Stamp the gap with the A5 override (no authored map).

**Verify (T1.6 + T1.7 verbatim)**: no pairwise stalls; wake re-dresses within T; pass time bounded; threshold decision correct both sides; morph commits once; no thrash. **Commit**: `phase1: S2 pass-through + squeeze/morph (T1.6, T1.7)`.

### Task 12 -- T1.8 nightly mini-soak
1. `Battle.Soak.Movement` Gauntlet controller: 8 units, 10 minutes of seeded random orders issued through the A2 order API; assert stuck = 0, separation = 0, archetype count constant.
2. A4: soak-map cook-list entry so the map exists in the cooked build.
3. Verify `ApplyTestMode` applies on the staged `-game` boot (fixed-step + full-detail assertions must hold on staged builds, or the soak is vacuous).
4. Verify the nightly wiring end-to-end; Jenkins must be running for the 02:00 cron (`Tools/CI/JENKINS.md` start line).

**Verify (T1.8 verbatim)**: nightly JSON shows stuck = 0, separation = 0, archetype count constant over 10 min. **Commit**: `phase1: movement mini-soak (T1.8)`.

### Task 13 -- Developer-type test module split (A4)
1. Split `FunctionalTesting`/`Gauntlet` deps into a Developer-type test module (audit-flagged "needs a second module -- Phase 1 work"); test sources stay behind `#if WITH_DEV_AUTOMATION_TESTS`.
2. Verify all three targets compile (Editor / Game / Shipping).

**Verify**: Shipping target compiles clean; per-commit + func tiers still green. **Commit**: `phase1: developer test module split`.

### Task 14 -- Handoff report
Write `Docs/phases/phase-1/report.md` per `Docs/AGENTS.md` §5 (all seven required sections, verbatim evidence); update `Docs/README.md` status block + doc map; write the B.7 Phase 1 regression-checklist entry; record A4 dispositions; final commit `phase1: complete`. **STOP.**

## §6. T1 gate -> implementation map

| Gate | Test / artifact | Route | Task |
|------|-----------------|-------|------|
| T1.1 | `Battle.Unit.Slots.*` | per-commit tier (headless) | 7 |
| T1.2 | `Battle.Unit.PathLayout.*` | per-commit tier (headless) | 4 |
| T1.3 | `Battle.Unit.SpeedLimits.*` | per-commit tier (headless) | 5 |
| T1.4 | `Battle.Unit.Compression.*` | per-commit tier (headless) | 6 |
| T1.5 | `Battle.Func.S1_MarchWheel` | functional map + toolset | 10 |
| T1.6 | `Battle.Func.S2_PassThrough` | functional map + toolset | 11 |
| T1.7 | `Battle.Func.S3_SqueezeMorph` | functional map + A5 stamp override | 11 |
| T1.8 | `Battle.Soak.Movement` | Gauntlet nightly, staged build | 12 |
| T1.9 | `Battle.Unit.Planner.*` | per-commit tier (headless) | 3 |

## §7. Definition of done (checklist for the handoff report)

- [ ] T1.1-T1.9 green with evidence (unit tier headless + func tier JSON + nightly soak JSON)
- [ ] Exit criteria (roadmap Phase 1): 8 units march / 90-degree wheel / mutual pass / squeeze-through-gap; zero stuck events over the 10-minute soak; no flank shedding
- [ ] B.8 per capability (planner, layout, speed limits, compression, assigner, servo, OBB resolver): unit tests over the math, scenario or unit coverage as applicable, overlay hook, metric, regression-checklist entry, CVAR flag -- each checked with the real artifact named (audit §5.2 lesson: no overclaimed checkboxes)
- [ ] Gate-first skeletons verifiable via git log: every T1.x skeleton commit shares or predates its implementation commit
- [ ] §4 audited; corrections merged into `Docs/reference/engine-api-5.8.md` (Part A corrections table + Part C sources); velocity-fragment adoption decision recorded (arch §3.1 note)
- [ ] A1 arch patch applied: conversion column "n/a -- velocity-space native" in the 8.3 state table (A7-style authorized patch) -- called out in the report
- [ ] Soak-map cook-list entry landed; nightly green end-to-end; A4 dispositions recorded (include fix / module split done, TargetAllowList + spawn re-baseline noted as still deferred)
- [ ] `Docs/phases/phase-1/progress.md` complete; `Docs/README.md` status block updated; B.7 Phase 1 regression entry written; `phase1: complete` commit exists
- [ ] Editor left clean (no dirty maps); MCP responsive; all evidence reproducible

## §8. Agent system prompt (paste-able)

```text
You are the MassBattle Phase 1 implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-1/brief.md.
Execute the brief autonomously under the standing protocol; brief §3.1 decisions
(A1-A5) are owner-approved. Editor work is batched -- movement tasks are
headless-first (§2); MCP calls are serial per Docs/reference/mcp-playbook.md.
Stop at the brief's DoD (§7) after writing Docs/phases/phase-1/report.md and
the final commit.
```

---

# PART II -- DECISION LOG (owner verdicts 2026-10-25)

**Verdicts: A1-A5 approved.** Everything approved is already merged into Part I (§3.1 table + the task sections). The notes below are kept for rationale; the executing agent does not need them.

### A1. R2 avoidance policy: Option A -- APPROVED -> Tasks 1, 8
- **Option A (adopted):** Servo/Integrate own everything; per-state weights (8.3 table) applied to a small project velocity-space avoidance term: teammates via roster/formation fragments (HoldSlot = none, ManeuverToSlot = medium teammates-only), cross-unit contact via the 8.6 OBB resolver (CrossField full RVO + asymmetric tie-break; PressRing arrives with its phase). No engine movement/avoidance processors, no `FMassMoveTargetFragment`, no `FMassMovementParameters`, no Experimental dependency. Pros: zero Experimental exposure; no schema pollution; deterministic under the test contract (own seeded RNG, handle-ordered tie-breaks); the servo math is the architecture as written; T1.5/T1.6 assert project behavior directly. Cost: own the RVO-ish math for CrossField (small at Phase 1 scale; the Phase 2 arch §7.x solver reuses it) + roster/OBB neighbor queries.
- **Option B (contingency only):** engine avoidance behind a CVAR, consume `FMassForceFragment` in Integrate with `dV = F * DT` (unit mass) documented, determinism probe before any gate relies on it (engine grid iteration order is `[verify]`). Re-evaluate only if CrossField RVO underperforms T1.5/T1.6.
- **Option C (rejected):** replace the servo with engine steering (SteerToMoveTarget + Avoidance) -- violates ADR-07 and §8.3; formation behavior is a unit property, not an agent steering property (findings §9 counterargument).
- **Why pre-decidable:** the digest (§4.2) shows the engine pipeline and the project servo are the same math in different units; the "force->velocity conversion policy" question dissolves once the servo owns avoidance -- the conversion column records "n/a -- velocity-space native" per state.

### A2. R5 command shell: thin C++ order API; UI out of scope v1 -- APPROVED -> Task 10
Facts that drove the call: (a) M1 is proven by S1/T1.5, which is automated -- clickable selection is not needed to prove it; (b) T1.8's Gauntlet controller must *issue* seeded random orders programmatically -- that is a command API, not a UI; (c) Phase 4 adds `OrderLatency` and toggles, for which a thin C++ order API is the natural substrate. The API-first shell is both faster than a UI shell and more useful to later phases; selection/picking/UI/camera remain Phase 5 territory (R5's "Phase 1-2" landing is satisfied by the explicit scoping decision, not an accident).

### A3. Overlay drawing scope -- APPROVED -> Tasks 2, 3, 7, 8
Phase 0 A1 deferred all overlay drawing to the first drawable state. Phase 1's drawable states are exactly three: path polyline (planner), slot-error magnitude (assigner), unstuck ladder level (§8.5 says "colored"; B.8 rule 3 demands an overlay per capability). Drawing anything else stays deferred; hooks remain the B.8 satisfaction mechanism for capabilities without drawable state.

### A4. Deferred items disposition -- APPROVED -> Tasks 1, 12, 13
- **In scope:** soak-map cook-list entry (Task 12 -- the audit deferred exactly this: the map "will not exist in a cooked build unless added to the cook's map list"); Developer-type test module split (Task 13 -- audit-flagged "needs a second module -- Phase 1 work"; Shipping link fix); `Mass/EntityHandle.h` include fix (opportunistic, Task 1).
- **Stays deferred:** `.uproject` `TargetAllowList` pruning of MCP toolset plugins (owner call); spawn re-baseline `entity_spawn_80_us` (T2.9, Phase 2 -- no real perf gate lands this phase).
- Guard rail: the T1.8 staged-build run must verify the test contract applies on the `-game` staged path (`ApplyTestMode` at staged boot), or soak assertions are vacuous -- see the risk watchlist.

### A5. Squeeze/morph geometry: test-only cost-grid override -- APPROVED -> Tasks 3, 11
The alternative (MCP-brick an authored corridor map per playbook §6) is restart-batched, fiddly, and adds a second map. The override is data the planner already consumes, deterministic, needs zero MCP, and keeps B.1's gray-box rule. Real authored terrain stays out of Phase 1 anyway (B.1: flat tile first).

---

*Prepared 2026-10-25 from full `Docs/` research + web-verified UE 5.8 movement-stack research (A1-A5 approved by the owner the same day). Frozen at kickoff per `Docs/AGENTS.md` §4.3 -- post-kickoff corrections land in the owning reference files, never here.*



