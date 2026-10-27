# MassBattle Phase 2 Brief -- Formation Movement Cleanup (TW progressive fall-in & march discipline)

| | |
|---|---|
| **Source** | Owner-approved plan "MassBattle -- Formation Movement Cleanup Plan" (senior-dev pass-back, 2026-10-27). Supersedes the roadmap Part A Phase 2 ordering for this work package; engagement-solver work keeps its roadmap position. |
| **Engine pin** | UE 5.8.x (ADR-13) -- all changes are Mass-framework processors in `Plugins/MassBattle` |
| **Repo state at kickoff** | `6eaacf9` ("phase1: audit fix pass"). NOTE: the plan was audited at `1200455`; the TW fan-in peel (`63277a0`) already landed a binary closer-walk peel + arrival facing SNAP. This brief re-grounds every task against current HEAD; line numbers below are current-file references. |
| **Scope** | Movement system only: the two reported defects (walk-past + double-back at destination; no dress/cover on the march) plus movement-related complexity found during audit. |
| **Fidelity target** | Faithful to Medieval 2: Total War behavior (owner decision, 2026-10-27) |
| **Deliverable shape** | 4 phases (A-D), each independently shippable, gated by cvars + tests; default-OFF until gates pass (Battle.Garrison.SoakOrders precedent) |

## 0. TL;DR -- the audit verdict

The architecture is right (unit = locomotor, entities = servos, slots = broadcast geometry), but implementation decisions break the TW feel; both are fixable by deleting special cases rather than adding them.

**Issue 1 (walk-past + double-back at destination).** Final formation (facing/width) is adopted only at anchor arrival: the path is dropped, `Generation++` triggers an unbounded nearest-slot re-assign (`UnitLocomotionProcessor.cpp` arrival branch; `FormationSlotResolveProcessor.cpp` re-decision path), and entities re-shuffle. TW instead hands each soldier his final slot at order time and lets him fall in as he approaches -- the formation morphs into its destination shape.

**Issue 2 (no dress / cover on the march; ends bow backwards).** Three compounding causes:
1. The servo clamp is tied to the anchor's pace (`Formation.SpeedLimit`, `ServoProcessor.cpp`), not the member's own max speed. In a wheel the anchor slows and every entity's ceiling drops with it -- but outer files must move faster than the anchor to track their sweeping slots. They can't. The ends fall behind.
2. The wheel cap is computed about the formation centroid (`OuterRadius`), but the path-frame layout wraps ranks along the path, so the true outer sweep radius in a bend is `r_path + HalfFrontage`. The cap is too generous on tight bends.
3. There is no straggler feedback: the Min3 slowest-man term uses a nominal uniform speed, so the arch section 8.2 promise "the exhausted flanker slows the whole unit until re-dressed" is unimplemented.

**Worked example** (14-file x 3-rank, spacing 1.2 m, walk 1.4 m/s, 90-deg bend r=8 m):

| Quantity | Current | Needed |
|---|---|---|
| Anchor speed cap (wheel) | 1.4 x 8 / 8.59 ~= 1.30 m/s | 1.4 x 8 / (8 + 8.4) ~= 0.68 m/s |
| Outer-file slot sweep | ~= 2.67 m/s | <= 1.4 m/s (= member max) |
| Servo clamp for that entity | ~= 1.63 m/s | 1.4 x 1.25 = 1.75 m/s |
| Outer-file deficit | ~= 1.0 m/s for ~10 s -> ends bow ~10 m | 0 -- tracks |

**Fix package, in dependency order:** Phase A "the servo tells the truth"; Phase B "the unit waits for its men"; Phase C "fall in as you arrive"; Phase D cleanup/docs/observability.

## 1. Findings register (re-grounded against current HEAD)

| ID | Sev | Defect | Phase |
|---|---|---|---|
| F-1 | Crit | Final formation adopted only after arrival; no per-entity blend from march slot to final slot (the landed fan-in peel is a binary closer-walk rule, not the TW convergence) | C |
| F-2 | Crit | Arrival `Generation++` triggers unbounded nearest-slot re-assign after the path drop | C |
| F-3 | High | Servo clamp tied to the anchor's pace instead of the member's own max speed (arch 8.3 MaxSpeed means member max -- spec/impl drift) | A |
| F-4 | High | No slot-velocity feed-forward: pure position servo => systematic Tau*v trailing lag and weak tracking of sweeping slots | A |
| F-5 | High | Wheel cap computed about the centroid while the wrapped column's outer files sweep r_path + HalfFrontage | A |
| F-6 | High | "Exhausted flanker slows the whole unit" unimplemented -- no feedback from actual slot error | B |
| F-7 | Med | Interval reflow + predictive swaps run during clean marches -- assignment churn is a dress-noise generator | B |
| F-8 | Med | Field entities never rotate to slot yaw on settle -- rest-facing is garrison-only (IntegrateProcessor R-G5 branch) | C |
| F-9 | Low | Arrival churn feeds avoidance transients and the unstuck ladder | C (falls out) |
| F-10 | Note | UnitLocomotionProcessor mixes the garrison FSM with field locomotion (~1200 lines). Real refactor candidate; explicitly out of scope -- logged for a future phase. | -- |

Out of scope but recorded: post-corridor width restore semantics stay as-is (current morph + new-order redeploy behavior); final slots recompute from live NumFiles so the plan composes with the existing morph behavior unchanged. Garrison flow, combat states, path planning, compression model, unstuck ladder, NumSlots casualty semantics: unchanged.

## 2. Target behavior -- the TW contract, stated testably

| # | Property |
|---|---|
| B-1 | An order is an intent to converge: (anchor, facing, width) describe the destination formation; soldiers walk into it. No rigid-block-then-rotate phase for marched arrivals. |
| B-2 | Fall-in: every soldier owns a final slot from order consumption; his walk target blends march->final as the unit closes (alpha = 1 at/before arrival). |
| B-3 | Early settle: a soldier within handoff distance of his final slot stops riding the block and settles -- stands and turns to the slot facing while others still march. |
| B-4 | No double-back: once within handoff distance of his final slot, a soldier's distance to it never grows (0.25 m tolerance for avoidance nudges). |
| B-5 | March discipline: files dress and ranks cover on straights and bends; the ends do not trail (max slot error <= 0.6 m sampled at 1 Hz through two 90-deg bends, 14 files). |
| B-6 | Wheels: outer files quickstep up to member max; the anchor slows so the outermost slot sweeps <= member max. |
| B-7 | Straggler pacing: the anchor never outruns its men -- unit speed throttles when max slot error breaks threshold, recovers when re-dressed. |
| B-8 | In-place reform stays a block reshaping in place (TW behavior), same crossing-free assignment and settle-facing. |

## 3. Design

### 3.1 Final slots + progressive blend (the one new idea)
Assignment (who owns which slot -- stable, computed once per order/layout) is separated from targeting (where do I walk now -- the blend). The final formation becomes first-class at order time; per entity:

- `alpha_unit = clamp01(1 - RemainingPathLength / MorphDistance)` -- shapes the whole block (MorphDistance ~= max(2 x formation depth, 12 m), cvar).
- `alpha_entity = 1` when `dist(Pos, finalSlot) <= HandoffDist` (2 x SlotReachEps, cvar) -- the per-soldier early fall-in, latched with hysteresis; else `alpha_unit`.
- target = lerp(marchSlot, finalSlot, alpha). Far away (alpha ~= 0) the unit is exactly today's coherent path-frame block; the blend only engages near the destination.

Existing data (`FUnitFormationSharedFragment::FinalAnchorPos/FinalFacingYaw/FanInDestValid`, armed at Move consumption, cleared on every other order consumption) IS the plan's final frame; `FinalValid` = `FanInDestValid`. Final slot positions derive from the LIVE NumFiles/NumSlots each resolve tick, which satisfies "refresh finals on any mid-order NumFiles change" for free.

### 3.2 Crossing-free final assignment (replaces the unbounded greedy at re-decisions)
`FSlotAssigner::AssignFinal` (pure, meters, deterministic): sort slots in canonical rank-major/file-ascending order (== sorted by facing-projection descending, lateral ascending); sort entities by facing-projection descending, tie-break lateral ascending, then roster index; pair in order. O(n log n), projection-monotone (no two soldiers' final approaches cross in the facing axis), deterministic; surplus slots (casualty holes) are the REAR-most -- frontage holds, identical to today's reflow semantics.

### 3.3 March-discipline servo (feed-forward + member clamp)
Per entity: `V_slot = (Target - PrevTarget)/DT`; `V_des = clamp(V_slot + (Target - Pos)/Tau, MemberMaxSpeed x CatchupBoost)`. `MemberMaxSpeed = WalkSpeed x SpeedMult` (the stats shared fragment carries SpeedMult). The unit SpeedLimit continues to pace the anchor only. Corrected wheel cap while a path is active: `OmegaCap = MemberMaxSpeed / (r_path + HalfFrontage)`; `AnchorCap = OmegaCap x r_path`. In-place pivot (no path) keeps the OuterRadius form.

### 3.4 Straggler pacing (the arch 8.2 clause)
FormationSlotResolve reduces `MaxSlotErrorM` into the shared fragment (same worker->game pattern as ReflowCooldown). UnitLocomotion folds it in after hysteresis: `if (MaxSlotErrorM > DressBreakM) Limit *= clamp01(1 - PaceGain * (MaxSlotErrorM - DressBreakM))`. Recovery rides the existing ApplyHysteresis (drops immediate, rises gated).

### 3.5 Arrival & settle (what gets deleted)
Keep: path drop at Remaining <= 1 cm; the H2 corridor files-restore safety net; the arrival facing snap (with finals + blend the men are already in their slots; the snap makes the anchor metadata coincide). Delete (flag on): the arrival `Generation++` -- no layout re-decision at arrival anymore. Generalize the garrison rest-facing branch (R-G5) in Integrate to all entities under the master switch. Unit order-complete reporting stays a soak/diagnostic concern (MaxSlotErrorM is the metric).

### 3.6 Data changes (all blit-safe, no archetype impact)

| Fragment | Field | Writer | Reader |
|---|---|---|---|
| FSlotTargetFragment | PrevTargetPos (cm) | FormationSlotResolve | Servo (feed-forward) |
| FSlotAssignmentFragment | bFinalLocked (uint8 latch) | FormationSlotResolve | FormationSlotResolve (handoff hysteresis) |
| FUnitFormationSharedFragment | MaxSlotErrorM (float, diagnostic -- never hashed) | FormationSlotResolve (worker) | UnitLocomotion pace term, debug, soak assert |

## 4. Task plan

### Phase A -- "The servo tells the truth" (Issue 2 core)
| Task | Files | Change | Acceptance |
|---|---|---|---|
| A1 | BattleMovementFragments.h, FormationSlotResolveProcessor.cpp, ServoProcessor.cpp, BattleServo.h/.cpp | PrevTargetPos on FSlotTargetFragment (resolve writes it before overwriting); servo: V_des = V_slot + (Target-Pos)/Tau via new pure fn ComputeServoVelocityFF; old signature kept for the flag-off path. Cvar Battle.Movement.FeedForward | Unit: FF cancels constant slot velocity (steady lag 0); flag-off golden equivalence |
| A2 | ServoProcessor.cpp | MaxSpeedM = WalkSpeed x Stats->SpeedMult (member max) under cvar Battle.Movement.MemberClamp; the SpeedLimit <= 0 no-order fallback keeps its walk-speed ceiling | Unit: clamp binds at MemberMax x Boost |
| A3 | BattleUnitLocomotion.h/.cpp, UnitLocomotionProcessor.cpp | FSpeedLimitInputs gains HalfFrontageM + bPathActive; marching form AnchorCap = v_member x r/(r+HalfFrontage) when a path is active; pivot (no path) keeps OuterRadius form; header doc-comment updated. Cvar Battle.Movement.WheelCapV2 | Unit: r->inf -> cap = member max; r->0 -> cap -> 0; worked-example dimension audit; flag-off golden |

Phase A func gate: S1_MarchWheel gains a max-slot-error bound (<= 0.8 m during the wheel) asserted when the A flags are on (computed in the test actor; B1 has not landed yet). LOC ~120 + ~150 test.

### Phase B -- "The unit waits for its men" (Issue 2 closure)
| Task | Files | Change | Acceptance |
|---|---|---|---|
| B1 | FormationSlotResolveProcessor.cpp, BattleMovementFragments.h | Reduce MaxSlotErrorM in the target-write loop into the shared fragment (running max, zero allocation); unconditional (diagnostic) | Value visible to UnitLocomotion next frame |
| B2 | BattleUnitLocomotion.h/.cpp, UnitLocomotionProcessor.cpp | Pure fn StragglerPaceFactor; pace term after hysteresis. Cvars Battle.Movement.StragglerPace (gate), Battle.Movement.DressBreak (m), Battle.Movement.PaceGain | Unit: pace curve points (below threshold = 1.0, beyond = floor) |
| B3 | FormationSlotResolveProcessor.cpp, BattleUnitLocomotion.h/.cpp | March stickiness: while Path.IsValid() && MaxSlotErrorM < DressBreak, skip the interval reflow + predictive swaps (rank-gap and layout re-decision triggers stay). Cvar Battle.Movement.MarchStickiness; pure helper MarchReflowSticky | Unit: stickiness gating truth table |

Phase B func gate: S2_PassThrough extension -- after the pass, MaxSlotErrorM recovers <= 0.5 m within 4 s (asserted when B flags are on; logged otherwise). LOC ~130 + ~100 test.

### Phase C -- "Fall in as you arrive" (Issue 1)
| Task | Files | Change | Acceptance |
|---|---|---|---|
| C1 | (verify-only) UnitLocomotionProcessor.cpp | Finals frame already armed at Move consumption and cleared on every other consumption (garrison/Stop) -- the plan's FinalValid = FanInDestValid; finals derive from live NumFiles (morph refresh free). Document the mapping | S4/S6 unchanged flag-off |
| C2 | BattleSlotAssigner.h/.cpp, FormationSlotResolveProcessor.cpp | AssignFinal per 3.2; wired into the resolve re-decision branch under the master switch (replaces the unbounded greedy there when finals are valid) | Unit: crossing-freedom, casualty holes = rear slots, determinism, bijection |
| C3 | FormationSlotResolveProcessor.cpp | Blend per 3.1: alpha_unit (cvar Battle.Movement.MorphDistance), per-entity handoff latch (cvar Battle.Movement.HandoffDist; latch on FSlotAssignmentFragment.bFinalLocked, cleared on layout re-decision); write blended TargetPos/TargetYaw; PrevTargetPos still written pre-blend | Unit: blend alpha monotonicity + handoff hysteresis |
| C4 | UnitLocomotionProcessor.cpp | Flag on: delete the arrival Generation++ + ArriveRefront (log ArriveFallIn instead); keep path drop, files restore, facing snap; the finals frame stays armed post-arrival so targets are continuous across the path drop. Zero-length plans (in-place reform) unchanged | No unbounded re-assign at arrival; S5 re-scoped green |
| C5 | IntegrateProcessor.cpp | Generalize the R-G5 rest-facing branch to all entities under the master switch (keep the garrison Z servo gated) | Settled field bodies face the slot yaw |
| C6 | optional | Approach alignment (plan-to-facing-offset final leg) -- OPTIONAL, A/B-able, deferred unless playtests demand | -- |

Master switch Battle.Movement.ProgressiveFallIn gates C2-C5 (C1 is inert data without the readers).

Phase C func gates: new S7_FallIn scenario (= P1 below); S5 re-scope -- EarlyRefrontViolations/OffTangentViolations encoded the superseded contract; S5 now sets the master switch and asserts: settled facing ~= commanded within 5 deg, settle-time bound, zero escalations, per-entity monotone-approach property. LOC ~300 + ~250 test.

### Phase D -- Cleanup, docs, observability
| Task | Files | Change |
|---|---|---|
| D1 | Docs/spec/architecture.md | ADR-19: supersedes the ADR-18 refront clause; rewrite 8.2 (wheel cap marching form, straggler pacing now real), 8.3 (feed-forward, member clamp), 8.4 (assignment vs targeting split); 12 tunables table gains MorphDistance / HandoffDist / DressBreakM / PaceGain |
| D2 | FormationSlotResolveProcessor.cpp, BattleMovementSoakController.cpp | Final-slot ghost markers (distinct color channel) under the overlay + master switch; soak march variants gain the MaxSlotErrorM bound (asserted when phase flags on, logged otherwise) |
| D3 | cvar audit | Fold A-C switches to defaults ONLY after one green nightly with the flags on -- NOT in this run; recorded as the explicit fold-in checklist in the report |

LOC ~100.

## 5. Playtest scenarios (verification gates)

All on L_MassBattle_T0_Functional, asserted (not eyeballed), 40-man levy, spacing 1.2 m, NumRanks 3 -> 14 files unless stated.

| # | Scenario | Setup | Pass criteria |
|---|---|---|---|
| P1 (=S7) | North->East fall-in (the reported repro) | Unit spawns facing 0 (path +X analog of north), ordered 60 m ahead with FacingYaw = 90, CommandedFiles = 10 | (a) B-4 monotonicity: after first coming within 0.7 m of its final slot, no entity's distance-to-final-slot grows > 0.25 m; (b) anchor-arrival -> all-settled <= 3 s; (c) zero unstuck escalations; (d) final geometry: per-entity distance to its final slot <= 0.35 m, settled entity yaw within 5 deg of commanded |
| P2 | Wide->narrow re-front on arrival | Spawn NumRanks 2 (20 files), march 40 m same facing, CommandedFiles = 5 | (a)-(d) as P1; (e) no entity crosses the final anchor line before settling (no front overshoot) -- covered by S5's re-scoped coherence + monotone-approach probes |
| P3 | Long-march cohesion (dress & cover) | 120 m march through two stamped 90-deg bends (cost-grid walls), 14 files | (a) MaxSlotErrorM <= 0.6 m at 1 Hz throughout; (b) end-of-line along-track lag <= 0.8 m during bends; (c) time-to-formed after arrival <= 2 s; (d) zero escalations |
| P4 | Straggler pacing / pass-through | Unit A marches through standing unit B, then 40 m on | (a) A's anchor speed dips while MaxSlotErrorM > DressBreakM; (b) MaxSlotErrorM <= 0.5 m within 4 s of clearing B; (c) B re-dresses (existing S2 wake criterion) |
| P5 | Regression battery | S0-S6 + S8/S9/S10/S8b + unit tier + soak | All green with the re-scoped S5; soak march variants include the MaxSlotErrorM bound |

P3/P4 ride the S1/S2 scenarios extended with flag-conditional bounds (the audit's scenario tier already covers the shapes); the dedicated bend-course variant is logged as follow-up soak coverage if the nightly shows drift.

## 6. Risks, mitigations, rollbacks

| Risk | Mitigation / rollback |
|---|---|
| Behavior change vs pinned golden tests (S5 asserts the opposite of the new contract) | Intentional; S5 re-scoped inside Phase C's commit so the suite is never red at rest. Called out in ADR-19 |
| Feed-forward + avoidance could oscillate | Tau, accel clamp, avoidance weights unchanged -- FF only removes the systematic lag term. Battle.Movement.FeedForward=0 restores old math; S1 catches oscillation (error bound) |
| Member-max clamp raises outer-file speeds in wheels (anim cadence) | Ceiling is member walk x SpeedMult x 1.25 -- what the anim LOD was authored for. Cvar off-switch; S1/S7 bounds |
| Final slots could land in blocked cost cells | Same behavior as today (slots are geometric, planner clamps the anchor); note in ADR-19 |
| Determinism (assignment on workers) | AssignFinal has fully specified tie-breaks (projection -> lateral -> roster index); MassBattleSlotAssignerTests pins repeat-call equality |
| Perf | Added per-entity cost ~= 2 vector ops + 1 store; assignment drops from O(n*m) greedy to O(n log n) at re-decisions. Nightly soak re-baselines |
| Short-path / degenerate orders (in-place reform, 1-2 m moves) | Zero-length plans never enter the blend (no path at consumption => in-place branch unchanged); short-but-valid paths start at alpha ~= 1 -- correct TW feel (flow into shape). Unit tests pin the alpha curve |

## 7. Definition of Done

- [ ] Phase A tasks A1-A3 + unit tests + S1 bound; commit `phase2:`
- [ ] Phase B tasks B1-B3 + unit tests + S2 extension; commit `phase2:`
- [ ] Phase C tasks C1-C5 + unit tests + S7_FallIn + S5 re-scope; commit `phase2:`
- [ ] Unit tier green (flags off, the shipping default), full func tier green (flags off)
- [ ] Targeted func runs with phase flags on: S1/S2/S5/S6/S7 (+ garrison S8/S9/S10/S8b for the A2 clamp) green
- [ ] Phase D: ADR-19 + spec sections, final-slot overlay, soak bound, report, README
- [ ] `Docs/README.md` updated in the same commits that introduce docs
- [ ] Handoff report `Docs/phases/phase-2/report.md` per AGENTS section 5

## 8. Ground rules (phase-specific)

1. Every behavior change lands behind its default-off cvar; flag-off paths stay byte-equivalent to the audit-fix state (unit golden tests pin this).
2. New fragment fields are diagnostic unless a processor consumes them; nothing new enters BattleStateHash.
3. ASCII + LF only in docs; em-dashes forbidden.
4. Spec edits limited to D1's authorized sections (architecture.md ADR-19 + 8.2/8.3/8.4/12); roadmap untouched.
5. The worked-example numbers in section 0 are the A3 dimension audit's golden case.
