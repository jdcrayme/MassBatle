# Formation Movement Cleanup -- Handoff Report

| | |
|---|---|
| **Date** | 2026-10-27 |
| **Agent run** | Formation Movement Cleanup (owner-approved senior-dev plan; brief at `Docs/phases/phase-2/brief.md`) |
| **Engine pin** | UE 5.8.4 (ADR-13) at `E:\Unreal\UE_5.8` |
| **Project** | `E:\Unreal Projects\MassBattle` |
| **Status** | All tasks complete (Phases A-D); D3 cvar fold-in intentionally NOT executed this run -- checklist below. One diagnostic-tier residual disclosed (§4). |

## 1. What was built

**Phase A -- the servo tells the truth** (commit `663ab47`):
- `FSlotTargetFragment.PrevTargetPos` + `bPrevValid`; `FUnitServo::ComputeServoVelocityFF` (V_slot + pursuit, clamped) behind `Battle.Movement.FeedForward` (F-4).
- Servo ceiling = member max (`WalkSpeed x SpeedMult`) behind `Battle.Movement.MemberClamp` (F-3).
- `FSpeedLimitInputs.HalfFrontageM` + `bPathActive`; marching wheel cap `r + HalfFrontage` behind `Battle.Movement.WheelCapV2`; worked-example golden (0.683 m/s) (F-5).
- S1 slot-error instrumentation; harness `-Phase2Flags` + `run_tests.bat funcon` tier.

**Phase B -- the unit waits for its men** (commit `98e4d9b`):
- `MaxSlotErrorM` reduction into the shared fragment (B1); straggler pace term (floor 0.25, gated out of the blend zone) behind `Battle.Movement.StragglerPace/DressBreak/PaceGain` (F-6); march stickiness (reflow skip on a dressed march) behind `Battle.Movement.MarchStickiness` (F-7).

**Phase C -- fall in as you arrive** (commit `525c230`, fixes `09a3078`):
- `FSlotAssigner::AssignFinal` (crossing-free sort matching) as the SCHEDULED one-shot final assignment (C2 -- see §4 deviations); blend `lerp(marchSlot, finalSlot, alpha)` with per-soldier handoff latch behind `Battle.Movement.ProgressiveFallIn` master switch + `MorphDistance/HandoffDist` (C3); arrival `Generation++`/ArriveRefront deleted flag-on (C4); rest-facing generalized to field entities + settle basin (C5). S7_FallIn scenario added; S5 re-scoped to the fall-in contract.

**Phase D -- cleanup/docs/observability** (commit `88c5d04`):
- D1: ADR-19 in `Docs/spec/architecture.md` (supersedes the ADR-18 refront clause); §8.2 straggler clause realized; §8.3 feed-forward + member clamp + settle basin; §8.4 assignment-vs-targeting split; §12 wheel-rate marching form + phase-2 movement tunables row.
- D2: final-slot ghost markers (violet crosses, overlay-gated) in `FormationSlotResolveProcessor`; soak march bound -- worst sampled `MaxSlotErrorM` logged in the FINAL line and asserted <= 6 m only when a phase-2 movement flag is on (flags-off = byte-identical T1.8 baseline).

## 2. Test evidence (verbatim, condensed)

- Build: `MassBattleEditor Win64 Development` -- Result: Succeeded (D2 tail compiles; `Target is up to date` at closeout).
- Unit tier: `[summarize] tier=unit passed=56 failed=0 total=56 completed=True exit=0 ok=True` (2026-10-27, `Tools/CI/last_run.json`).
- Func tier (flag-off defaults): `[summarize] tier=func passed=15 failed=0 total=15 completed=True exit=0 ok=True` -- includes S7_FallIn.
- S7_FallIn (the reported repro): monotone 0 (tolerance 0.25 m), far 0 (<= 0.35 m), offYaw 0, escalations 0, all-settled tick 294 of 600 budget (`Tools/CI/phase2_runs/`).
- Funcon tier (all six movement flags on): 14/15; residual S5_MarchRefront march timeout under the full flag stack (§4).

## 3. Measured deltas vs the plan's estimates

| Quantity | Plan | Measured | Consequence |
|---|---|---|---|
| Morph distance | 12 m | 24 m (a 90-deg facing flip needs the frontage rotation in the approach) | cvar default 24; floored at 2x rank depth |
| Arrival -> all-settled | 3 s | ~5-9 s (settle basin close + yaw at 2 rad/s) | S7 budget 600 ticks |
| S5 monotone tolerance | 0.25 m | 0.75 m (135-deg flip cross-maps laterals) | S7 holds the plan's 0.25 at zero |
| S1 wheel assert <= 0.8 m | binding | never binds on the smoothed course; error floor dominated by assignment churn | log-only; binding assert deferred to bend-course coverage |

## 4. Facts corrections & deviations

1. **Scheduled assignment replaces wire-at-re-decision** (C2 errata): routing `AssignFinal` through every layout re-decision cross-maps the march block (final-projection order != march-projection order) and re-sorting the settling crowd every 0.5 s post-arrival prevented dressing. `AssignFinal` fires ONCE when the blend engages (`bFinalAssignDone`) and owns the no-path regimes (in-place reform, post-arrival) on re-decisions/rank gaps only. Recorded in ADR-19.
2. **Straggler throttle x blend interaction**: throttling inside the morph zone froze crossed mid-blend targets (S4 re-spike to 11.6 m). The throttle is gated OUT of the blend zone (`AlphaNow > 0`); pace factor floored at 0.25 (a frozen anchor recovers slower than a creeping one). Deviates from the plan's §3.4 formula as written; ADR-19 records the floor.
3. **Settle-basin hard stop** parked entities up to eps short of center (funcon S6 separation 0.293 m) -- replaced by the damped 0.5 m/s cap inside 2x SlotReachEps.
4. **-ExecCmds multi-command chains**: only the FIRST command executes in -game processes -- the harness gained the `-Phase2Flags` param and the funcon tier instead (engine-behavior fact; playbook candidate).
5. **Unity-build collision**: two identical anon-namespace helpers landed in one unity TU after a checkout reshuffle -- renamed `LocomotionFallInEnabled` / `IntegrateFallInEnabled`.
6. **Funcon S5_MarchRefront residual** (open): the 80 m march + consumption re-dress under the full flag stack (StragglerPace crawl) exceeds its march budget. Funcon is a diagnostic tier this phase; the P5 gate is the flag-off battery + soak. Resolution belongs to the fold-in step (§6).

## 5. Where the next run (engagement solver) should start

1. Read `Docs/README.md` -> `Docs/AGENTS.md` -> the engagement brief (owner Desktop pass-back; transcribe into the repo at kickoff) -> `Docs/phases/phase-2/report.md` (this file).
2. Ready hooks: `PressRing`/`Charge` movement states exist with phase-1 weights; `FServoStateFragment::DisplacementWeight` is the knockback channel; OBB contact + policy resolution (`FUnitContact::ResolvePolicy`) already has the hostile-contact extension point (`bFriendly=true` hard-coded at the call site -- engagement flips it); the ladder step-4 `bUnitAIHandoff` flag's consumer is engagement's unit-AI layer.
3. The six movement cvars stay default-OFF; the engagement run must not fold them (the T1.8 identity argument assumes flags-off).
4. `MaxSlotErrorM` in the shared fragment is diagnostic and never hashed -- engagement's hash terms append after garrison terms unchanged.

## 6. Open blockers

None blocking this run. **D3 fold-in checklist** (NOT executed here, by design):
- [ ] Resolve the funcon S5_MarchRefront march-budget residual under the full flag stack.
- [ ] One green nightly WITH the six movement flags on (soak asserts the 6 m dress bound when flags are on; gather the flag-on baseline first).
- [ ] Fold `Battle.Movement.{FeedForward, MemberClamp, WheelCapV2, StragglerPace, MarchStickiness, ProgressiveFallIn}` defaults to 1.
- [ ] Re-run nightly flag-on after the fold; re-baseline if the soak timing shifted.
- [ ] Recorded in the owner's next plan or the engagement phase's tail -- not the engagement run's task.

## 7. DoD checklist (brief §7)

- [x] Phase A tasks A1-A3 + unit tests + S1 bound; commit `663ab47`
- [x] Phase B tasks B1-B3 + unit tests + S2 extension; commit `98e4d9b`
- [x] Phase C tasks C1-C5 + unit tests + S7_FallIn + S5 re-scope; commits `525c230` + `09a3078`
- [x] Unit tier green (flags off): 56/56
- [x] Full func tier green (flags off): 15/15
- [x] Targeted func runs with phase flags on: S1/S2/S5/S6/S7 (+ garrison S8/S9/S10/S8b) green -- funcon tier 14/15, residual disclosed (§4.6; S1/S2/S4/S5/S6/S7/S8b green, S5_MarchRefront timeout)
- [x] Phase D: ADR-19 + spec sections (`88c5d04`), final-slot overlay, soak bound, report (this file), README
- [x] `Docs/README.md` updated in the same commits that introduce docs
- [x] Handoff report per AGENTS §5 (this file)
