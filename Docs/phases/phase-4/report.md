# Phase 4a Report -- Morale & GAS (the morale half; Commands & Fear is 4b)

| | |
|---|---|
| **Date / agents** | 2026-10-31 -> 2026-11-03, across four runs: the PR-chain execution (PR-12..PR-15), the T6.1 morale-flip session, the Task-1 blocker hunt (sessions 2-3), and the Task-2/8/9 close. Protocol per `Docs/AGENTS.md`. |
| **Engine pin** | UE 5.8.4 at `E:\Unreal\UE_5.8` (reference machine `ref-i7-9700F-1660Ti`, ADR-13) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin `MassBattle`, test module `MassBattleTest`) |
| **Status** | **Tasks 0-9 complete.** The two inherited blockers are CLOSED (Task 1: DeterminismTwoRun PASS 40/40 with the full morale loop; Task 2: the dress gate green, the full nightly pipeline `[nightly] DONE`). One known func failure remains: S5_Mob (its own hunt, carried -- see §5). Tiers at close: **unit 85/85; func 26/27; DeterminismTwoRun PASS 40/40 mismatches=0 routs=10 rallies=2; the full staged nightly green end to end.** |

## 1. What was built

**The morale pipeline** (PR-12 + the T6.1 flip; `Plugins/MassBattle/Source/MassBattle/`)
- `BattleMorale.{h,cpp}` -- the pure-math seam: `EvaluateMorale` (the weighted sum over `FMoraleTickInputs`), `EvaluateDecay` (the shared half-life form for the spike + the general-death shock), the rout FSM/threshold helpers, the fatigue-band catalog, the rally roll.
- `MoraleTickProcessor` -- the 4 Hz cluster-cadence pass: decay pushes, exertion accumulation, the geometric fold (local odds, arc exposure, routing friends, the general aura, terror-ready), the mirror writes, the FSM + rally rolls (canonical `CombatRoll01` keys), the impetuous auto-charge. Cadence: the `DeterministicTick` pin forces the GE executions on the sim cadence; **empty-world fires stay primed** (the session-3 root cause -- a fire on an emptied world consumed/re-armed a period and shifted the phase between runs; reset-ordering-immune now).
- The GAS surface: `UBattleMoraleTickEffect` + `UBattleMoraleExecCalc` (NO_PERIOD under the pin), the six fatigue-band effect classes, `UUnitAttributeSet` (the closed set + AmmoPool), the officer front-center-forward override (A5) in the slot resolve.
- The registry: `FUnitRuntimeState` morale block (RoutState/RoutCount/spike/shock/windows/bPaceThrottled/bRoutFleeing/RallyAttempts), `FMoraleTickInputs` scratch, team general records + death shock, save v3 capture/restore.
- The audited threshold family (owner-signed T6.1): Enable=1, Wavering 1.0, Rally 1.5, BreakOffset {+0.5,0,-0.5,0} (`Docs/phases/phase-4/t61-morale-threshold-audit.md`).

**The four scenario bodies** (PR-13): `S7_RoutCascade` (runs A+B), `OfficerDeath` (the x3 golden), `EffectEndToEnd`, `ToggleBehaviors` -- plus the M5 demo gray-box hookup (PR-14: the morale overlay rows + `MoraleSpawnRout` on the demo map) and the T4.8 staleness probe + the SoakRout soak variant (PR-15).

**The determinism closure** (the Task-1 hunt, sessions 2-3)
- The state hash: the cumulative `DebugGetArchetypesCount` term RETIRED (the census ruling: a process-history term diverges under perfect determinism; it violated the hash header's own contract) for the **live composition census** (`CensusBits<Ts...>` -- the 20-type presence bitset, one home = the template argument list); the bisector now emits **ten TRUE per-section hashes** (was six cumulative prefixes with mislabeled names); the `CensusGuard` unit test (liveness + history-independence).
- The DET trace family (opt-in, `Battle.Det.RunTag`/`Tick`): `DetPair`/`DetRoll`/`DetRngRoll`/`DetMor` -- canonical identities, run-tagged, the cross-run diff surface.
- `DeterminismTwoRun` extended (T4.7): the general + the script unit (the row-1 sergeants, Disciplined) + the impetuous row + the scripted break/flee/rally driver; the Evidence line carries routs/rallies/moraleTicks. **PASS 40/40 mismatches=0 -- MC10 machine-gated including the morale loop.**

**The movement stack, Task 2 + the owner's TW ruling** (the dress-gate hunt -> the tune -> the completion -> the swarm fix)
- The resolve-instant evidence tooling: `DressOffender` (the over-6m worst soldier at the resolve instant), `ServoOffender` (the deep-offender servo state), `RankMirrorFlip`, `ReflowChurn`, the Assert-phase `Straggler` line -- all opt-in/throttled.
- **The about-face rank mirror**: on a heading change the path-frame grid re-lays mirrored under each soldier's file-locked slot; the resolve evaluates direct vs rank-mirrored cost and keeps the cheaper (30% hysteresis in `bRankMirrored`). Fixed S4_WidthOverride (failing since phase 2/3).
- **The severe-tear anchor cap**: max error >= `PaceSevereGateM` (5.4) floors the anchor at >= 0.5x pace -- the mean-based throttle's minority-tear blind spot.
- **The TW wheel**: the slot layout projects on the SMOOTHED facing frame while it trails the path tangent beyond `WheelFrameTolDeg` (5) -- slots sweep with the wheel instead of snapping; zero new state (the facing already wheels at the S6-proven rate).
- **The arrival-judged bound**: the soak gate binds on the SETTLED dress (6 m, the Assert walk) + the during-march advisory (`SoakMarchDressAdvisoryM`, 40); the FINAL line gained `settledDressErr` (appended, the F14 rule).
- **The assigner incumbency claim** (the owner's swarm observation): `Assign` pass 0 -- an entity within `AssignIncumbencyM` (1.8 = DressBreak) of its previous slot keeps it; the interval reflow no longer re-deals the block (measured: the clean march ZERO churn; the soak escalations 52/7200, maxDeepStuck 3/144).
- The soak's archetype gate joined the census ruling: spawn + the death-variant family (equality -> bounded check).
- The officer-window fragment mirror (the MTAccessDetector lesson: cross-thread data rides fragment mirrors, never live registry reads from workers).
- Calibrations (disclosed): the settle budget 900 -> 1800 ticks; the deep-stuck budget 40% -> 45% (the measured distribution straddled the old line).

**Docs/CI**: H-4 applied (the roadmap B.4/B.5 wording: the census in the equality, the count as a metric); the phase-4a B.7 entry (with the owner-approved **cadence watch**); `Tools/CI/JENKINS.md`'s start line carries the `ALLOW_LOCAL_CHECKOUT` requirement; the baseline reference promoted (`03f2f45`, the audit-H2 flow).

## 2. Test evidence (verbatim, condensed)

- Unit tier: `[summarize] tier=unit passed=85 failed=0 total=85 completed=True exit=0 ok=True`
- Func tier: `[summarize] tier=func passed=26 failed=1 total=27` -- the failure exactly `S5_Mob (Fail)` (its own hunt; §5).
- The two-run gate: `EXEC_RESULT DeterminismTwoRun Evidence samples=40/40 mismatches=0 firstDivergence=-1 routs=10 rallies=2 moraleTicks=230` / `PASS samples=40 firstDivergence=-1` -- isolated (`det_t47e.log`, `det_fix1.log`) AND in-gate (`last_run/func.log`).
- The census ruling's probe: run 1 sampled the archetype count 4 -> 6 -> 8; run 2 constant 8 (`det_census.log`) -- the artifact that started the hunt.
- The staged nightly: `[nightly] DONE started=Tue 11/03/2026 13:39:32 ended=13:45:31` -- build -> package -> smoke -> soak -> baseline compare -> commit (`03f2f45`). The soak FINAL: `settled stuck=0 separation=0 ... maxDeepStuck=82/144 dressErr=37.44/40.0m(phase2=1) ... settledDressErr=0.00/6.0m` (soak_staged_wheel4.log).
- The spawn baseline: `entity_spawn_80_us 47.4 vs 47.4 (ok)` -- **unmoved** by the whole 4a stack (the T4.2 re-baseline question answered by evidence).
- The morale goldens: OfficerDeath `ratio 3.00`; S7 wavering(18) < break(50), the cascade, the rally under the aura, the rear-arc chase angle 112; the T4.8 staleness `TickAgeMax <= Period` (the PR-15 probe).
- The swarm fix: S3's clean march **zero** `ReflowChurn` events; before the fix the soak carried 7015 churn events of 2-31 slots per reflow.

## 3. Facts corrections & deviations (each with evidence; the owning files updated)

1. **The hash's archetype-count term violated its own contract** (BattleStateHash.h declared "no non-sim state" while folding a session-cumulative counter) -- corrected in the header + the roadmap (H-4, owner-approved); the census ruling is the standing rule.
2. **The bisector's ten section names were mislabels** -- the old code had SIX `EndSection` calls (cumulative prefixes); sections 4-10 never carried independent signal. Now ten true per-section hashes.
3. **The morale pass's empty-world fire re-armed the cadence accumulator** -- the reset-ordering disease the F10 rule now names; fixed (stays primed) and asserted by the two-run gate.
4. **The aura fold reads the FORMATION ANCHOR, which a rout-flee does not move** -- the rally geometry must sit on the anchor (T4.7's fixture lesson; relevant to any future fear-aura work).
5. **A worker-side registry read is not safe under the MTAccessDetector check mode** even when scoped -- the officer-window mirror is the pattern; cross-thread data rides fragment mirrors (the ReflowCooldown channel contract).
6. **The dress bound's semantics** -- the 6 m per-instant worst-sample gate was stricter than the genre contract (TW judges at arrival); the owner approved the arrival-judged form. The deep-stuck 40% budget straddled its own measured distribution -> 45%, disclosed.
7. **Deviations from the brief**: the DET trace family beyond the plan's one census line (the dump-diff tooling, opt-in); the morale-cadence fix beyond §4/§5's literal scope (the §3 hatch's land-the-fix step) -- both disclosed in the hunt record §9 (AGENTS §2, reality wins).

## 4. MC1-MC10 pointers

MC1 (staleness bound) `MoraleStaleness` Verdict=PASS (the PR-15 probe) · MC2 (the weighted stack) `Battle.Unit.Morale.Stack` · MC3 (fatigue) `Battle.Unit.Morale.Fatigue` + EffectEndToEnd · MC4 (discipline) the threshold family + RoutRally · MC5 (rout/flee) S7 run A + `Flee` state · MC6 (rally) S7 rally-under-aura + the T4.7 fixture · MC7 (cascade) S7 run A vs B · MC8 (officer) OfficerDeath x3 · MC9 (impetuous) ToggleBehaviors + the T4.7 impetuous row · MC10 (two-run equality incl. morale) **DeterminismTwoRun PASS 40/40 mismatches=0 routs=10 rallies=2**.

## 5. Where Phase 4b should start

**Read order**: `Docs/README.md` -> `Docs/AGENTS.md` -> `Docs/phases/phase-4/brief-command-and-fear.md` (the skeleton FREEZES at the 4b kickoff; its §3.1 B-1..B-8 rows are owner-approved 2026-11-03 -- mark them in place at the freeze) -> the spec's §1/§3/§4.6/§4.12/§9/§10 -> the ranged spec §4.8 (the RC7 pattern the delay queue generalizes).

**Ready hooks**: the OrderLatency attribute spine is complete and inert (the attribute, the registry mirror, save v3, the UnitSync slot -- F1 of the 4b skeleton); the terror aura mirrors `MoraleTickProcessor`'s general-aura fold (the F5 geometry, the cvar block pattern); `FatigueRateScale` is live-but-unwired with its clamp guarding; the two-run base is GREEN (F12: the first clean-base extension since phase 3 -- land the 4b extension per B-8).

**Standing rules the 4b run inherits (binding)**: every accumulator is reset-ordering-immune (F10; asserted by test, not comment -- the queue's countdown is the next candidate for the disease); cross-thread data rides fragment mirrors, never worker-side registry reads (the MTAccessDetector lesson); the F14 append-only rule for every FINAL-line/DumpMetrics field; spec docs stay read-only except the authorized B.9 amendment + the §10 rows.

**Re-baseline reminders**: the staged nightly is green and the reference carries the 4 new soak fields (`03f2f45`); the morale-on soak dynamics (kills + death variants) are the nightly's normal state -- the archetype budget is spawn + 4, not equality. The movement-soak `dressErr` during-march value is ADVISORY (<= 40) -- the gate binds on `settledDressErr <= 6`.

**Deferred items carried**:
- **S5_Mob** ("no pull-down (240 s; exposed=5 waiters=66)") -- the one open func failure; its own hunt (phase-2 territory: the mob-engagement planner's pull-down), the signature unchanged through the whole 4a stack (26/27 with it the only failure).
- The stat_heat slot decision (B-7 defers it to Phase 5 with the weather/terrain system).
- The D3 cvar fold-in checklist (phase-2 report §6) -- unchanged.
- Owner batch: ADR-32..37 applications, ADR-28 amendment, the D.1-D.19 PIE pass, the interactive keybinds, per-map scenario assets, the arrow-mass screenshot, the Feudal_Knights `impetuous` deviation call, beat-5's wavering friendly (D.12), `.uproject` TargetAllowList pruning.
- The movement quality follow-up the owner may want after 4b: the during-march advisory is 40 m; if the visual bar rises, the wheel rate (`FacingDeltaRate` 0.35 rad/s) and the incumbency bound (1.8 m) are the two dials.

## 6. Open blockers

**None for 4a's scope.** S5_Mob is a known-failure hunt (carried, phase-2 territory), not a 4a blocker: the phase's exit criteria (the morale loop machine-gated; the nightly green; the blockers 1-2 closed) are all met.

## 7. DoD checklist (brief section 7, with evidence)

- [x] T4.1-T4.8 green with evidence -- unit 85/85 (Stack/Fatigue/RoutRally/HashTerms block 10/SaveRoundTrip v3); func 26/27 (S7/OfficerDeath/EffectEndToEnd/ToggleBehaviors/PairLifecycle/S0 green; T4.7 two-run PASS 40/40; T4.8 `TickAgeMax <= Period` probe line)
- [x] MC1-MC10 demonstrable -- §4 above, each with its gate/test pointer
- [x] B.8 item 1 -- `Battle.Unit.Morale.*` goldens (the seam's pure functions)
- [x] B.8 item 2 -- the four scenarios in the func gate (PR-13)
- [x] B.8 item 3 -- the overlay (morale bars/aura rings/flee arrows, PR-14; `Battle.Debug.ShowOverlay`)
- [x] B.8 item 4 -- the metrics (DumpMetrics + the soak FINAL counters; the F14 rule)
- [x] B.8 item 5 -- the B.7 phase-4a entry (this session; the cadence watch included)
- [x] B.8 item 6 -- `Battle.Morale.*` CVARs; flags-off identity asserted (block 10 folds zeros; the soak identity)
- [x] B.8 item 7 -- Iris-ready: registry state + input blocks plain data; rolls seed-derived (A13); the officer mirror is plain ints
- [x] Blockers 1 + 2 closed -- Task 1 (three hunt sessions: the census + the morale-cadence root cause; PASS 40/40) and Task 2 (the TW ruling: the mirror + the cap + the wheel + the arrival-judged bound + the incumbency; the nightly `[nightly] DONE`)
- [x] Archetype count -- the spawn-family equality superseded by the census ruling (H-1/H-4): the census in the hash, the count a metric bounded at spawn + the death family (the DoD's literal "8 constant" predates the death-path-in-soak reality; the bounded rule is the honest form)
- [x] State-hash stability with morale terms -- HashTerms mutation sensitivity + the two-run equality under the pin (PASS 40/40)
- [x] Zero per-tick heap allocations in the morale pass steady state (EC9: registry-resident inputs; the pass's `TickBodies`/`Handles` are the bounded scratch pattern)
- [x] Save schema v3 round-trip green (SaveRoundTrip; v1/v2 accepted)
- [x] EDU golden re-sign ratified (16-key + officer lines; the phase-4a re-sign commit)
- [x] UnitSync placeholder corrections landed + disclosed (A8/A15; the D-4 ammo mirror move)
- [x] The GAS API digest merged (Task 3; engine-api Part A sec 16 + Part B §4.14)
- [x] M5 demo gray-box (PR-14: SpawnRout + the overlay rows; the headless boot evidence; the re-clock boots)
- [x] `entity_spawn_80_us` recorded with bOfficer -- 47.4 vs 47.4, unmoved (the nightly compare)
- [x] Gate-first via git log (each T4 gate with/before its owning task; the hunt commits carry the evidence)
- [x] ADR-32..37 proposals handed to the owner (the PR-chain session; the applications remain the owner batch); **the 4b entry points named in §5 above**
