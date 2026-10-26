# MassBatle — Phase 1 Audit (movement core)

**Date:** 2026-10-26
**Auditor:** Independent senior audit (4 parallel deep-review passes + orchestrator verification of every High finding and report-level claim)
**Scope:** Full audit of Phase 1 as committed at `823fb19` ("phase1: complete"): `Plugins/MassBattle/Source/MassBattle/` (all kernel + processor sources), `Plugins/MassBattle/Source/MassBattleTest/` (all 18 test sources, soak controller, harness), CI tooling (`Tools/CI/*`, `Jenkinsfile`), baselines (`Baselines/perf/*`), configs (`Config/*`, `.uproject`, `.uplugin`, Build.cs × 2), and the phase documents (`Docs/phases/phase-1/{brief,report,progress}.md`, `Docs/spec/{architecture,roadmap-and-testing}.md`).
**Method:** Findings checked against the code as committed at `823fb19`. HEAD (`1200455`, "garrison: complete") consulted only to record whether a finding was already repaired by the later phase. UE 5.8.4 engine source was not available locally; claims that require engine-source verification are explicitly marked **[needs engine verification]**. All High findings were re-verified line-by-line by the orchestrator before publication.

**Verdict:** The phase is substantively well-executed at the pure-math tier — the kernel modules are faithful to the spec, deterministic, and guarded by the best golden-case test suites in the repo, and the inventory/report/CI claims that can be checked mechanically (test counts, CVAR names, constants, commit discipline, H2 fix) all check out. However, the audit confirms that **the Phase 0 "contract-to-test drift" pattern has repeated — it moved from the math tier to the wiring tier.** Three spec-mandated behaviors are simply never performed by the runtime (the unit tests pass because they exercise the pure functions with synthetic inputs, and the functional tests sample only settled states), the hot-path allocation contract is violated in three of four hot processors while the report quietly narrows the checklist wording it is judged against, and one contracted scenario clause (S3 queue point) does not exist. Two of the three wiring defects were later fixed *by accident* in the garrison phase, whose fix comments read as bug reports against Phase 1.

| Severity | Count | Items |
|----------|-------|-------|
| High | 5 | H1–H5 |
| Medium | 19 | M1–M19 |
| Low / nit | ~22 | §3 (lows) + §4 (nits) |

---

## 1. High severity

### H1. Slot projection ignores the anchor's arc position — every slot is pinned to the path END for the whole march

`FormationSlotResolveProcessor.cpp:114` (audited commit):

```cpp
(void)FPathFrameLayout::Project(PathM, PathM.TotalLength, Rank, File, ...);
```

Architecture §8.6 specifies `SlotPos = PathPoint(s_anchor − rank·RankSpacing) + …`. The fragment even carries the anchor's arc position for exactly this purpose (`FBattleFormationSharedFragment::PathS`, "UnitLocomotion-owned", advanced every tick at `UnitLocomotionProcessor.cpp:355`). Passing `PathM.TotalLength` instead means rank 0 always projects at the path terminus: entities **beeline to their destination slots instead of riding the anchor**. §8.1 ("the unit is the locomotor"), §8.2 compression, and §8.6 path-frame layout never actually operate on a marching formation — the entire movement-stack spec is silently inert mid-march.

**Why the gates stayed green (drift):** T1.2 tests `Project()` with a synthetic `SAnchor` (correct); S1/S2/S3 assert separation/coherence only on the **settled** post-arrival state, so a mid-march blob is never sampled.

**Status at HEAD:** fixed by the garrison phase (`FormationSlotResolveProcessor.cpp:183-186` now passes `Formation.PathS * 0.01f`, with the comment: *"projecting at the path end instead would send every entity beelining for its final slot (the unit blobs apart mid-march)"* — the later phase's own words confirm materiality). The fix was never back-annotated as a Phase 1 defect; it reads as new behavior, not a repair.

**Fix:** backport `PathS` projection + the HEAD entry-tangent extrapolation for negative-arc rear ranks; add a **mid-march** sampling assertion to S1 (formation coherence while PathS is strictly between 0 and total length) so this class of regression cannot pass again.

### H2. `PassLocked` is never reset and the morph is never reverted — one transient corridor permanently narrows a formation; the S3 test asserts the wrong invariant

`UnitLocomotionProcessor.cpp:311` sets `Formation.PassLocked = 1`; grep over the audited commit shows **no reset site anywhere** (only the initializer, `BattleMovementFragments.h:119`). The decision gate at `:273` (`!Formation.PassLocked`) is therefore dead for the rest of the unit's life after the first narrowing:

- A later order through open field can never re-expand files (`NumFiles = Decision.MorphFiles` at `:302` is one-way).
- §8.6's "**Re-deploy width only past exit clearance**" is structurally unimplemented.
- A new move order resets `PathS` and rebuilds the path but keeps the morphed layout — a 20-file unit that once passed a gap stays a 3-file, 14-rank column forever.
- Worse, the decision commits on *first sight* of any narrowing inside the 15 m scan-ahead (`:280-288`) — the comment says "commits once the lead rank nears the mouth" but the lead rank is never measured at the mouth.

**Drift evidence:** the T1.7 func test **asserts the persistence as the goal** — `BattleFuncTestActor.cpp:1653-1657` fails if `NumFiles != 3` after exit, and `report.md:65` sells this as *"clean re-deploy"*. "Morph commits once / no thrash" (T1.7) is likewise inferred only from the final state of a latch that by construction cannot flip twice — no morph-decision counter exists, so thrash during approach is unobservable.

**Status at HEAD:** the garrison phase added `Formation.PassLocked = 0; // new order = new pass (T1.7 commit rule is per traversal)` (`UnitLocomotionProcessor.cpp:368`) — again an undocumented Phase 1 repair.

**Fix:** reset `PassLocked`/`NumFiles`/`SpacingScale` on order-serial change; commit only when the anchor's arc distance to the constriction is below a mouth threshold; add a post-exit re-deploy stage that restores layout params (with a `Generation` bump to force re-slot); add a morph-decision counter to the debug subsystem and assert ≤ 1 per order serial in S3.

### H3. The §8.6 contact policy layer is unwired: the 0.55 speed factor is a dead multiply, the 0.40 floor is a dead constant, `ResolvePolicy` has zero runtime callers

Three independent pieces of evidence at the audited commit:

1. `UnitLocomotionProcessor.cpp:345-348` multiplies `LimitIn.CommandedSpeed *= FUnitContact::PassThroughSpeedFactor` **after** `ComputeUnitSpeedLimit(LimitIn)` was last consumed (`:244`), and anchor progression (`:351-356`) uses the hysteresis fragment value `CurrentLimit`. The multiply writes a dead local — the cavalry anchor proceeds at **full** commanded speed while interpenetrating, and §8.6's "~50–60% speed" is not implemented. Report §1 (`report.md:33`) claims "pass-through speed factor" among this processor's behaviors.
2. `PassThroughCompressionFloor` (`BattleUnitContact.h:56`) has **zero runtime consumers** — grep finds only the definition and a test that asserts it is *less than* the normal floor. Compression always floors at 0.6.
3. `FUnitContact::ResolvePolicy` (`BattleUnitContact.cpp:54`) is called only from its own unit test. `ServoProcessor.cpp:152-198` shoves whenever any moving unit's OBB overlaps any unit, with a hardcoded 2.0 mass ratio and a comment admitting it is "a documented policy at Phase 1 fixtures" — yield/block, mobility advantage, and right-of-way never influence runtime behavior.

**Why the gates stayed green:** S2 bounds pass time only with a 3000-tick timeout; it never asserts reduced anchor speed or the lowered floor. The contact unit test asserts the policy table as **values**, not as behavior — it would pass with the policy deleted from the runtime, which is what happened.

**Fix:** call `ResolvePolicy` in ServoProcessor's pair loop and branch Block/Yield/PassThrough; move the speed factor before `ComputeUnitSpeedLimit` (or scale `CurrentLimit`); feed `PassThroughCompressionFloor` into `AdvanceSpacingScale` while a pass-through policy is active; assert the reduced anchor speed in S2. If the policy layer is deliberately deferred, that is a log-worthy deviation — the report records none.

### H4. Hot-path allocation contract violated in three of four hot processors, plus an O(world²) unculled avoidance gather — and the report quietly narrows the B.7 wording it is judged against

Roadmap B.7 (standing, per-phase merge checklist): *"no new per-frame allocations in hot processors."* Phase 1 reality:

- `ServoProcessor.cpp:264-265` — `TArray<FAvoidanceNeighbor> Neighbors; Neighbors.Reserve(128);` **inside the per-entity loop** (~an allocation per moving entity per frame), and the gather at `:266-283` iterates the **entire frame snapshot with no distance culling** (filtering happens only later, inside `ComputeAvoidanceDeflection`). At Phase-1 scale (320 entities) that is ~102k neighbor evaluations/frame; at the §12 target (2×2400) it is ~23M/frame across workers — the "< 4 ms sim @ 2500" budget is not credible with this shape.
- `ServoProcessor.cpp:93,117-121,148-151,201-202` — per-frame snapshot array (no `Reserve`), body arrays, shove arrays, and a `TMap` rebuilt every frame.
- `FormationSlotResolveProcessor.cpp:87-105,129-132` — five arrays per chunk per frame, plus a heap copy of the 64-point polyline per chunk per frame via `FromFragment` (`BattlePathFrameLayout.cpp:5-19`; same copy per unit per frame in `UnitLocomotionProcessor.cpp:220`); reflow-due ticks add three more arrays.
- `UnitLocomotionProcessor.cpp:101-102` — per-frame body arrays on the game thread.

**Drift evidence:** `report.md:121` (B.7 entry) re-scopes the claim to *"no per-frame **log** allocations"* — literally true (the Phase 0 M6 logging fix is real; first-execute traces and per-transition ladder logs only) — but the checklist item it replaces says **allocations**, and the narrowing is disclosed nowhere. The 8-unit soak cannot see any of this; the report even advertises the snapshot pattern ("frame-snapshot avoidance gather (flat array…)") without disclosing it allocates per tick.

**Fix:** hoist scratch buffers to per-processor members (`Reset()` per frame) or a frame-scoped arena; cull the avoidance gather by squared radius during collection; keep a fixed-capacity polyline mirror across the fragment boundary; cache CVAR pointers (`ServoProcessor.cpp:226-229` does a string `FindConsoleVariable` per idle chunk per frame); restore the unqualified B.7 wording or log the deviation.

### H5. Contracted scenario clause does not exist: S3 has no queue point, and the scenario geometry cannot produce one

Roadmap B.3 S3: *"Two units through a pass — **second unit holds queue point**; morph commits once; no re-morph thrash; clean re-deploy past exit."* Implementation (`BattleFuncTestActor.cpp:1505-1510,1571-1591`): two units are ordered **simultaneously through two different gaps** (8 m "Narrow" at y∈[-20,-12], 22 m "Wide" at y∈[8,30]). Both cross in parallel; the assert phase (`:1630-1727`) checks PassLocked, files, strays, separation, escalations — grep for "queue" in the whole test module finds no assertion of any kind. There is no follower, no single constriction, and no station-keeping assert: the one clause that exercises queuing pressure (a Phase 2 engagement-solver prerequisite) is absent, and **this absence is not in the report's deviation list** (§3 discloses only the soak settled-state semantics and two unit-test fixes). The report claims T1.7 "green" without qualification.

**Fix:** re-scope S3 to two units through **one** gap with an approach corridor that forces queueing; assert the follower holds a station near the pass mouth while the leader occupies the gap; or log the deviation per the project's own evidence-before-claim protocol.

---

## 2. Medium severity

### Kernel / math wiring

**M1 — Unstuck ladder steps 3 and 4 are no-ops; the report claims "unstuck ladder (levels 0-4, terminal)."** `BattleServo.cpp:129-137`: level ≥ 1 ⇒ weight ×2; level 2 ⇒ sidestep; **level 3 ⇒ nothing new; level 4 ⇒ nothing new** (comment claims "level 4 = unit AI handoff" — no handoff exists anywhere; grep confirms). Brief Task 8 explicitly demands "3) forced slot swap with blocking teammate → 4) escalate to unit AI." The unit test asserts only counter semantics and the level-0/1/2 hooks — it would pass with steps 3/4 deleted, which is what happened. Not in report §3 deviations. *Fix:* implement step 3 via the existing swap machinery; give step 4 even a minimal handoff flag; or log the deviation.

**M2 — "Axis-true SAT" tests only 2 axes, not the 4 OBB face axes — false-positive interpenetration for oblique elongated formations.** `BattleUnitContact.cpp:26-35` projects onto the center-delta axis ± perpendicular only. The boxes are true OBBs, but SAT over 2 axes misses separating axes: two long formations (24 m frontage) offset diagonally by a couple of meters report deep overlap. Consequences: the §8.2 facing lock engages spuriously (`UnitLocomotionProcessor.cpp:258-269`) and the shove zone over-triggers. The golden test covers only parallel and exact-90° cases — precisely where the reduced axis set works. *Fix:* test the 4 axes {A.front, A.side, B.front, B.side}; add a 45°/diagonal-offset golden case asserting depth 0 when separated.

**M3 — Predictive-swap application bypasses its own 2-opt guard; "thrash impossible / each slot used once per pass" is unproven.** `BattleSlotAssigner.cpp:141-150` applies every detected pair unconditionally; `FindCrossingPairs` (`:83-138`) does not enforce one-pair-per-entity, and the guard is evaluated against the *detection-time* assignment, never re-checked at apply time. Chains like (1,2),(2,3),(3,4) produce a bucket brigade in which a later entity receives a slot for which it holds no guard promise — a swap-created trek that bypasses the k-file bound (swaps skip `Assign`). "Terminates" (T1.1) is likewise only ever exercised as a single detect/apply pass, never as an iterated fixpoint. *Fix:* re-evaluate the guard against the current assignment at apply time (or mark entities used during the pass); add a detect→apply→re-detect-to-empty test and a shared-entity chain test.

**M4 — Avoidance deflection is unbounded and applied after the servo clamp.** `BattleServo.cpp:99-101` accumulates a push **per neighbor** (the overlap branch adds a full `Combined` ≈ 0.8 m/s each), `ServoProcessor.cpp:287` adds it to the already-clamped output, `:293` adds a sidestep of `MaxSpeed·0.5`, and nothing downstream caps speed (`ClampAcceleration` is a rate; `IntegrateProcessor` imposes none). Under crowding, desired/actual velocities ratchet past the §8.2 unit limit. Tests use single-neighbor scenarios. *Fix:* clamp post-blend magnitude to `MaxSpeed·CatchupBoost`; cap the per-neighbor overlap push.

**M5 — Paths silently truncate at 64 samples (~126 m); long orders strand the unit mid-way with a misleading log.** `UnitLocomotionProcessor.cpp:156-157` clamps to `MaxBattlePathPoints` after a 2 m resample; on the 256 m tile, orders longer than ~126 m lose their tail; `TotalLength` is then read from the truncated polyline, the anchor stops short, the order stays active, and nothing logs. The `PlanPath` log (`:191-193`) prints the **pre-truncation** count, so diagnostics actively mislead. The header's justification ("needs < 30 samples after smoothing") is false for long paths. *Fix:* adaptive resample spacing = length/(MaxPoints−1); log stored count and truncation; consider re-plan when `PathS` saturates short of the goal.

### Runtime / processors

**M6 — Cross-stage gate inconsistency: `Battle.Movement.Servo 0` leaves entities coasting forever.** With Servo gated out, nothing ever zeroes `FMassVelocityFragment`; Integrate keeps applying `Transform += Vel·DT` — every previously-moving unit drifts at constant speed indefinitely and silently. Same class for SlotResolve-off leaving stale `FSlotTargetFragment` values being actively servoed. B.8 item 6 exists so half-built systems can "opt out of the baseline" — an opt-out that corrupts the sim defeats it. *Fix:* zero planar velocity (or skip) when the servo gate is off; document required gate combinations.

**M7 — Order API mutates the registry without access-detector scopes (and repeats at HEAD).** `BattleRegistry.cpp:187-237`: `IssueMoveOrder`/`IssueStopOrder` write with no `UE_MT_SCOPED_WRITE_ACCESS`, `FindOrder`/`GetOrderSerial` read with no read scope, plus a TOCTOU between `IsHandleValid` and `Units.Find`. `BattleExternalSubsystemTraits.h` deliberately permits worker reads of the registry, so when that happens the detector will never see order-API violations. Today all callers are game-thread: discipline, not a live race. *Fix:* add the scopes; validate-and-fetch in one locked helper.

**M8 — No `FDeadTag` exclusion in any of the five movement queries.** Death-window corpses (multi-kill flushes, deferred destroys) are fully processed by the movement chain — including writes to their unit's shared formation fragment — until destroy completes. Phase 0's own audit standard (M1) demanded presence-None queries for exactly this. *Fix:* `AddTagRequirement<FDeadTag>(EMassFragmentPresence::None)` on all five queries.

**M9 — Slot-error overlay lines are enqueued per entity per frame from workers even when the overlay is off.** `FormationSlotResolveProcessor.cpp:210-220` constructs an `FBattleOverlayLine` and takes a mutex per entity per frame; `Battle.Debug.ShowOverlay` gates only the draw (`BattleDebugSubsystem.cpp:52`). ~320 locked ops/frame now, ~2400 at target — pure contention against the game-thread drain. *Fix:* read a CVAR mirror at the call site before constructing.

**M10 — Ladder-escalation metric counts recoveries as escalations.** `ServoProcessor.cpp:324-328` increments on `NewLevel != LadderLevel`, but `FUnstuckLadder::Advance` returns 0 on recovery — every de-escalation is counted and logged. The zero-assert gates stay conservative (over-count can't false-pass), but the soak's `duringSoak escalations=23` is uninterpretable and the T1.8 "stuck" semantics rest on this number. *Fix:* count only `NewLevel > LadderLevel`.

**M11 — Replans are unbounded on the game thread.** Every `IssueMoveOrder` — including re-issues of an identical order (`BattleRegistry.cpp:203`) — triggers full A*+Smooth+Resample that frame, no budget, no hysteresis. The cited evidence `PlannerReplan Replans=60 MeanMs=0.035` is a 64-call **unit-test micro-benchmark** bounding per-plan latency only (T1.9 as contracted), not frequency. A client issuing orders every tick replans every tick. *Fix:* skip replan when the goal is unchanged within epsilon; amortize under a GT time budget.

**M12 — IntegrateProcessor is scoped only by fragment presence and stomps any Mass entity.** `IntegrateProcessor.cpp:31-35` requires only velocity+transform — no battle tag: any non-battle Mass entity is z-pinned to 0 and yaw-snapped to velocity. Latent today (the project owns all Mass entities in test worlds), a landmine later. *Fix:* require `FOwnerUnitFragment` or a battle tag.

**M13 — [SUSPECTED, needs engine verification] Worker-eligible processors fetch world subsystems inline.** `ServoProcessor.cpp:82` and `FormationSlotResolveProcessor.cpp:198` (per chunk, in-lambda) call `Context.GetWorld()->GetSubsystem<…>()` while `bRequiresGameThreadExecution = false`, with no subsystem requirement declared on the query. Whether `GetSubsystem` is sanctioned off-game-thread during Mass processing in 5.8 needs engine-source verification. *Fix:* resolve once on the game thread or declare the requirement per engine idiom.

### Test tier

**M14 — S1 "no flank shedding" is judged on the settled state only; transient shedding during the wheel is invisible.** Body-envelope violations *delay* the settle instead of failing it (`BattleFuncTestActor.cpp:958-961` — the bounded wait treats violations as "still re-dressing"). A wheel that ejects flankers which the servo then reels back in passes. Not disclosed (the §3 settled-state disclosure is soak-scoped). *Fix:* track per-tick max envelope violation and assert max == 0 for the wheel phases.

**M15 — S2 "no pairwise stalls" is a post-pass delta only; during-pass stalls are explicitly exempt.** `:1348-1356` gates `GetLadderEscalations() − EscalationsAtPassEnd == 0` with a comment exempting contact-phase escalations as "the ladder doing its job." The report's evidence line quotes only the post-pass delta. *Fix:* gate during-pass escalations against a small explicit budget (disclosed) and/or assert forward progress during contact.

**M16 — T1.8 soak gate is near-unfalsifiable: the settle is manufactured by the test itself, and the order generator excludes the stuck/separation generators.** (a) At t=600 s the controller issues **stop-all** (`BattleMovementSoakController.cpp:256-260`), waits 900 ticks, then gates on ladder levels — with everything stopped on a flat map, `stuck=0` is close to tautological; the 23 escalations and 2168 separation events recorded during the run gate nothing. (b) Orders are home-boxed ±10 m (`:157-180`), so no unit ever marches across the field or through another formation — the classic stuck generators are excluded by construction; disclosed as a "feature" in report §1 but absent from the §3 deviation list. The disclosed deviation covers only "judged on SETTLED final state." *Fix:* gate a generous during-soak escalation/separation *rate* alongside the final state; widen a fraction of orders field-wide; disclose both semantics in report §3.

**M17 — Harness watchdog is a 60 s wall-clock deadline that is the *binding* limit vs actor `TimeLimit` up to 420 s.** `MassBattleFuncTestHarness.cpp:131` vs `BattleFuncTestActor.cpp:1450,700` — machine-dependent effective tick budget; a slow CI box spuriously fails (cannot falsely pass). Tension with B.0's "never wall-clock" discipline. *Fix:* derive from tick count × fixed dt, or raise above the largest TimeLimit.

### CI / build / docs

**M18 — Report claims the nightly baseline JSON "now also carries the T1.8 soak step"; nothing in the tooling or JSON supports it.** `write_baseline.py` has no soak input of any kind; `nightly.bat:80` passes only `--smoke-log`; the committed `nightly-smoke.json` contains exactly `smoke_test_duration_s` + `entity_spawn_80_us` (verified; also true at HEAD). The T1.8 gate is enforced per-run (good) but leaves **no machine-readable record** — a soak trend can never be diffed — and the handoff report asserts a capability that does not exist. *Fix:* add `--soak-log` parsing the `FINAL settled …` line into `soak_stuck_final` / `soak_separation_final` / `soak_archetypes`; pass it in nightly.bat step 4.

**M19 — LFS misconfiguration makes a plain clone unbuildable, and the Jenkins runbook omits LFS entirely.** `.gitattributes` routes `*.uproject` (a 2.8 KB **text** file) through LFS alongside `*.uasset`/`*.umap`; every committed `MassBattle.uproject` is a 129-byte pointer blob (verified via `git cat-file`), and a fresh non-LFS clone yields pointer files as project/maps — `Build.bat` and `-cook -stage` fail immediately. `JENKINS.md` documents the Jenkins plugin set and checkout flow with **no git-lfs plugin or smudge step**. (SUSPECTED as to why the live box is green — plugin present but doc stale — but the runbook as written cannot reproduce a working node.) *Fix:* drop `*.uproject` from LFS; add LFS install/fetch to JENKINS.md; add a checkout sanity check (fail if the uproject starts with `version https://git-lfs`).

---

## 3. Low severity

- **L1.** Vacuous-compare channels in the nightly loop (partial bypass of the Phase 0 H2 semantics): an unparseable smoke log (regex miss or midnight crossing) silently **seeds the reference value forward** and passes (`write_baseline.py:32-48,104-109`); metrics missing from the candidate produce `MISSING-IN-CANDIDATE` lines that are **not failures** (`compare_baseline.py:73-76`) — a metric whose producer disappears compares against itself forever (the spawn metric's case is disclosed; the mechanism is not). *Fix:* fail on measurement failure and on reference-metrics missing from the candidate; record per-metric provenance.
- **L2.** `nightly.bat:61` — `set SOAK_LOG=%NIGHT_DIR%gauntlet_soak.log` is **missing a backslash** (compare `:48`): the soak log lands as `Tools\CI\last_nightlygauntlet_soak.log`, outside the untracked `last_nightly/` dir and not gitignored — a nightly untracked artifact accumulating in the tree. One character.
- **L3.** Smoke/soak process exit codes are captured and echoed but never tested in `nightly.bat` (`:51-52,64-65`) — pass/fail rides solely on the `TEST COMPLETE` marker, despite the header's claim that the script "checks the process exit code + log." A crash after the marker, or a wrapper injecting a nonzero exit with a clean log, passes. Same for `run_tests.bat` (`ENGINE_EXIT` never propagated, no editor-closed/mutex guard). *Fix:* `if not "%X%"=="0" exit /b 1` after each marker check.
- **L4.** Test maps cook into Shipping: `Config/DefaultGame.ini:21-23` puts both T0 maps in `MapsToCook` unconditionally, contradicting B.0's "test maps excluded from shipping cook" — the Shipping build ships the A5 stamp-playground. *Fix:* per-config cook lists or command-line maps for the nightly's Development cook.
- **L5.** `summarize_run.py:88-95` — report-count cross-check **warns but never fails**; the Phase 0 audit's own §4.6 record ("exits nonzero on mismatch") overstates the tool. A test Success-in-log/Fail-in-report passes the tier. *Fix:* fail on disagreement.
- **L6.** `JENKINS.md:62-64` claims the `ALLOW_LOCAL_CHECKOUT` flag "is part of the start line above" — it is not (`:25-28`). Internal contradiction in the runbook the nightly cron depends on.
- **L7.** Baseline-commit failures are swallowed (`nightly.bat:103-105` — any `git commit` failure becomes "nothing to commit (non-fatal)", exit 0); the documented "values recorded in the nightly git commit for history" can silently stop. *Fix:* distinguish exit 1 from other failures.
- **L8.** Planner validity "oracle" is not independent: the T1.9 test's re-check **calls `SegmentIsClear` itself** (`MassBattlePlannerTests.cpp:33-45`), and `SegmentIsClear` is a sampled sweep (~0.75 m slack near boundary cells) while the header claims coverage of "every cell the segment passes through." A clearance-semantics bug makes planner and oracle agree. *Fix:* structurally independent cell-walk oracle; exact swept-AABB clearance.
- **L9.** Self-body OBB center inconsistent between the two construction sites in `UnitLocomotionProcessor` (`:116` centers at anchor − (NumRanks−1)·spacing/2; `:253` offsets by NumRanks·spacing/2 — half a rank-spacing difference): the §8.2 facing lock compares a mis-centered self-body against correctly-centered others, direction-asymmetrically. Root cause is the B.0 seam erosion (OBB construction inlined three times). *Fix:* one `MakeUnitBody(Formation)` pure helper.
- **L10.** Co-located-entity avoidance split by handle **parity** (`BattleServo.cpp:56-60`): two co-located entities with the same parity flee as one blob — the failure the comment claims to prevent. *Fix:* derive the side from handle comparison.
- **L11.** Slope-aware pathing is dead code as far as any gate can see: production passes `SlopeWeight = 0` (`UnitLocomotionProcessor.cpp:184-185`), heights are flat 0, and no test uses non-zero heights — the roadmap's "slope-aware costs" deliverable is unexercised. *Fix:* one golden test around a hill.
- **L12.** `FSlotAssignmentFragment::Generation` is a dead field (written at spawn, never read) — implement the staleness check the comment promises or delete it. Similarly `const_cast` write through the read-locked `FindCachedAttributes` (`UnitActor.cpp:99-100`).
- **L13.** Tick budgets count actor frames, not fixed sim steps (`BattleFuncTestActor.cpp:808-809` — the comment itself says "~66 s at 30 Hz" under a 1/60 contract) **[needs engine verification]**; scenario RNG runs on the implicit root seed 0 (never explicitly seeded; deterministic but not the B.1 discipline), and the soak's seed call is silently skipped if the world isn't up at Gauntlet `OnInit` with no log of the effective root.
- **L14.** Soak final assert **skips** units whose roster/formation lookup fails (`BattleMovementSoakController.cpp:304-307`) — a unit whose fragment vanished escapes stuck/separation accounting; archetypes are compared spawn-vs-final only ("constant" is not literally checked through the run); soak separation is intra-unit only while S1's all-pairs sweep exists unused.
- **L15.** Per-commit CI gate runs the unit tier only — B.0's per-commit tier includes "3 smoke functional tests," and no deviation is logged (`JENKINS.md:40`, `Jenkinsfile:15`) **[SUSPECTED — may be run manually]**.
- **L16.** Jenkinsfile uses `agent any` with hardcoded E:\ paths (breaks on any second node; should pin the reference-machine label), and the nightly job is inline (unversioned, undiffable).
- **L17.** Doc pointer rot (4 sites): `Tools/CI/README.md:1` + `Baselines/perf/README.md:9` → `Docs/reference-machine.md` (moved to `Docs/spec/`); `roadmap:210` → pre-split `BattleTestSettings.h` path; `uplugin:4` → deleted `Docs/tw-battle-system-architecture.md`.
- **L18.** Pivot `r_path` semantics conflict: architecture E2 note says **outer radius for an in-place pivot**; brief + test equate pivot with r→0 ⇒ zero translation. Implementation matches the brief; the governing doc says otherwise — needs an owner ruling.
- **L19.** T1.1 "batched reflow idempotent" is verified only as scheduler cadence (`ConsumeIfDue` fires once), never as assignment idempotence (assign-twice-unchanged).
- **L20.** T1.6 "pass time bounded" is a 3000-tick timeout, not a band; clear-width oracle test tolerance ±1.6 m on a 4.5 m expectation (~35%); hysteresis effectiveness pinned only as `HystFlips < RawFlips` (175 vs 201).

---

## 4. Nits

- First-execute traces are process-static, not per-world (`static bool bTracedOnce` × 4) — multi-world runs lose the ordering evidence the report cites.
- "phase0:" prefix on automated nightly commits during Phase 1 (history noise; `927967d` etc.).
- `gate-unit.json` is a static Phase 0 artifact (date 2026-10-23, "1 test") with no writer in CI — will drift silently.
- Per-metric tolerance of exactly 0 is impossible in `compare_baseline.py:60` (`0.0` is falsy → CLI default).
- `MACHINE_SPEC` duplicated across two Python tools; both use `encoding="ascii"` for JSON/log IO (one non-ASCII char crashes — fails closed at least).
- Compression semantics differ between the anchor branch (scales rank depth) and path branch (laterals only) — an undocumented design choice worth one comment.
- Threshold inconsistency: soak body-envelope slack 5 m vs S-scenarios' 3 m — "separation" means different things per tier.
- Soak `LoadFixture` discards parse errors — a half-parsed fixture silently spawns fewer distinct rows.
- Harness dead code: second `if (!TestActor)` unreachable; `FOpenBattleFuncMap::Update` can return false forever with no deadline (boot anomaly hangs CI rather than failing it — `run_tests.bat` has no global timeout).
- `BattleDebugSubsystem` comment says "relaxed atomicity" over a seq_cst `std::atomic`; bookkeeping hash carried in a `double` UPROPERTY ("placeholder carrier").

---

## 5. Verified-correct items (do not re-litigate)

1. **Pure-math kernel is faithful and genuinely tested.** A\* determinism is real (fixed neighbor order, strict (F,H,index) heap, no RNG/TMap/wall-clock, two-run bit-identity asserted); corner-cutting prevented with both-orthogonal-legs rule; octile heuristic admissible under the cost multiplier; cost-grid bounds/clamping conservative (OOB → impassable). §8.2 two-step wheel cap + Min3 + asymmetric hysteresis implemented verbatim with golden cases including degenerate radii and straight-march inactivity (T1.3 graded FULL). T1.2 graded FULL (monotonic arc-lengths, exact orthogonality, straight-path degeneracy == anchor formula as exact equality). T1.4 graded FULL (floor 0.6 exact, recovery 0.1/s bounded, "exactly one direction change" under step input). T1.9 graded FULL modulo L8's oracle caveat.
2. **Test inventory claims are exact.** 44/44 unit-tier registrations counted independently, group membership matches report §2 line-for-line; 7/7 func tier; no test outside the filters, none uncounted, no `AddExpectedError` whitelisting anywhere.
3. **CVAR claims are exact.** All 10 `Battle.Movement.*` CVARs exist with the exact claimed names and stage-gate via early return; plus `Battle.Debug.{ShowOverlay,Enabled}` and the A2/A5/`Battle.Test.*` command surface as claimed.
4. **ADR-14 shared-fragment anchoring discipline held.** Anchor-first construction at both shared-fragment sites, all three `GetMutableSharedFragment` sites operate on their own unit's instance, zero rebinding; movement spine probe asserts distinct instances per unit and no archetype doubling; fragments are blittable as claimed (`FBattlePathPoint Points[64]` fixed C array; no TArray/FString/UObject in any fragment).
5. **Processor chain ordering matches arch §4** (UnitSync→UnitLocomotion on GT; SlotResolve→Servo→Integrate worker chain via ExecuteAfter), and `PressRing`/`Charge`/`Flee` are genuinely unwritten ready-hooks (zero write sites — cannot be accidentally entered).
6. **Stop-order state hygiene is correct:** serial-change branch resets `PathS`, clears path, entities → HoldSlot; no stale `PlannedOrderSerial` survival; first-order planning works (serial 0 → 1 ≠ planned 0); invalid handles rejected with no state change.
7. **The Phase 0 H2 baseline fix held through the soak work:** candidate under untracked `last_nightly/`, compare vs committed reference, compare exit code checked, promote-on-PASS-only, bootstrap when no reference — all verified intact (residual channels: L1). The soak step does fail closed before any baseline touch when its marker is missing.
8. **Module split (Task 13) is correct:** runtime `MassBattle.Build.cs` carries no FunctionalTesting/Gauntlet, `MassMovement` private, MassNavigation absent; `MassBattleTest` is DeveloperTool with explicit `IMPLEMENT_MODULE`, both test deps private, 18/18 test sources behind `WITH_DEV_AUTOMATION_TESTS`; game module still empty per the architecture rule.
9. **Gate-first commit discipline verified:** `261e1df` contains T1.9 tests+impl together; T1.1/T1.3/T1.4/T1.7/T1.8 each land in their owning or prior commit; one-commit-per-task holds across the 21-commit phase span.
10. **Death path and teardown fixes from Phase 0 remain intact** (native tag, `FDeadSignaledTag` idempotence, roster restamp, EndPlay teardown), and the Phase 0 FApp fixed-step leak is properly fixed with `ON_SCOPE_EXIT` restore.
11. **Test-mode contract is applied everywhere new:** `ApplyTestMode`+`AssertTestMode` at every S-scenario start and in the harness for all 7, plus the soak's staged-boot assertion; the escalation zero-asserts read a cumulative monotonic counter reset at scenario start (airtight for whole-scenario coverage); scenario assertions read live engine sim state, not test-recorded counters; each scenario re-opens the map (isolation).
12. **Baseline schema is coherent:** both JSONs parse, machine spec matches `reference-machine.md` and both Python tools; spawn-metric null-pending + T2.9 deferral disclosed.

---

## 6. Plan-level observations

1. **The drift pattern migrated, it did not repeat by accident.** In Phase 0 the drift was in the *math tier* (weaker assertion shapes). Phase 1's math tier is now the strongest part of the repo. The drift moved to the **wiring tier** (H1, H2, H3: spec behaviors the processors never perform) and the **scenario tier** (H5, M14-M16: settled-state sampling that cannot see mid-flight behavior) — precisely the layers unit tests cannot reach and the functional tests only sample at rest. A closing "contract vs. runtime behavior" pass over §8.2/8.5/8.6 — for each numbered spec rule, name the runtime line that performs it — would have caught H1-H3 and M1.
2. **Two spec behaviors exist only as test-asserted constants.** The §8.6 policy table and the unstuck ladder's steps 3-4 are pinned as *values* in unit tests while their runtime wiring is absent (H3, M1). The tests create an appearance of coverage that the runtime does not have — a stronger form of the Phase 0 tautology problem.
3. **The garrison phase fixed two Phase 1 HIGHs as drive-bys** (H1, H2) with comments that describe the Phase 1 bugs, but neither the Phase 1 report's deviations section nor the README status block acknowledges them. Per the project's own errata flow (AGENTS §4), repairs of a completed phase's contract should be logged where the contract lives.
4. **The report is honest where it discloses and overreaching where it doesn't.** Disclosed deviations (soak settled-state, two unit-test fixes) are properly argued. Undisclosed: the S3 queue clause (H5), the allocation-wording narrowing (H4), the stop-all/home-boxed soak semantics beyond the settled-state note (M16), the S1/S2 settled-state sampling (M14/M15), ladder steps 3/4 (M1), and the false soak-baseline claim (M18). The pattern to institutionalize: **deviations are only deviations if logged before the audit finds them.**
5. **The §12 budgets are currently aspirational.** With H4's O(N²) gather + per-frame allocations, and M11's unbounded replans, the 2×2400-entity budget table cannot be met by scaling the current processors; nothing in the T1 gates measures at anything close to target scale (8 units everywhere). The first Phase 2 task should include a 1k-entity movement smoke so the budget table starts being measured before engagement load arrives.

---

## 7. Recommended fix order

1. **H1 + H2** — backport the garrison fixes onto the Phase 1 line explicitly (PathS projection; PassLocked per-traversal reset + exit-clearance re-deploy), add the mid-march coherence assert and the morph-decision counter; one commit + S1/S3 re-run.
2. **H3** — wire or formally de-scope the §8.6 policy layer (ResolvePolicy in the servo pair loop, speed factor before the limit computation, floor into compression); assert reduced anchor speed in S2.
3. **H5 + M14 + M15** — re-scope S3 to a single-gap queue scenario (or log the deviation), add during-wheel max-envelope tracking (S1) and a during-pass escalation budget (S2).
4. **H4** — hoist scratch buffers, cull the avoidance gather, cache CVAR pointers; restore the B.7 wording or log the deviation; add the 1k-entity movement smoke.
5. **M1, M2, M3, M4, M5** — ladder steps 3/4 (or deviation), 4-axis SAT + oblique golden, swap-apply guard + fixpoint test, post-blend velocity clamp, adaptive resample + truncation log.
6. **M6–M13** — gate-consistency, access-detector scopes, FDeadTag exclusions, overlay gating, metric fix, replan budget, Integrate scoping, subsystem access (after engine verification).
7. **M16–M19 + L1–L7** — soak semantics disclosure + rate gate, harness deadline, soak metrics in the baseline JSON, LFS/runbook repair, nightly.bat one-liners (SOAK_LOG backslash, exit codes, commit-result handling), compare provenance, shipping cook split.
8. **L8–L20 + nits** — opportunistically, per the Phase 0 method.

---

## 8. Audit-input notes

- The audited commit `823fb19` is 4 commits before HEAD; `UnitLocomotionProcessor.cpp` is 391 lines there (1045 at HEAD after garrison). All line references are to the audited commit.
- Findings H1/H2 carry garrison-phase confirmation quotes; no other Phase 1 finding is repaired at HEAD (verified per-slice: garrison adds same-pattern code in places — e.g. a second per-chunk `FindConsoleVariable` in Integrate, and `IssueGarrisonOrder` repeats the access-detector bypass).
- Engine-source-verification list for the owner: `FSubsystemCollectionBase::GetSubsystem` off-game-thread during Mass processing (M13); per-frame vs per-sub-step processor execution under `FApp::UseFixedTimeStep()` (L13); `UGauntletTestController::EndTest(n)` → process exit code propagation (L3's marker-only gating).
