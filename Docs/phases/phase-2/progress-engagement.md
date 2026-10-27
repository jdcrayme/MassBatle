# MassBattle Phase 2 Progress Log -- Engagement Solver & Melee

| | |
|---|---|
| **Date** | 2026-10-27 |
| **Engine** | UE 5.8.4 at `E:\Unreal\UE_5.8` |
| **Project** | `E:\Unreal Projects\MassBattle` |
| **Protocol** | `Docs/AGENTS.md` + `Docs/phases/phase-2/brief-engagement.md` (owner pass-back; §3.1 decisions owner-approved per the brief's §8 system prompt) |

## Pre-run discrepancies (AGENTS §2.1; reality vs the brief's frozen text)

- **2026-10-27 (folder collision).** Brief task 0 step 5 says "Create `Docs/phases/phase-2/progress.md`" -- that folder is occupied by the completed formation-movement-cleanup run (brief/progress/report, `61f25fb`). This run uses `*-engagement`-suffixed files in the same folder; `Docs/README.md` map updated accordingly. Same resolution applies to the handoff report (`report-engagement.md`).
- **2026-10-27 (read-order path).** Brief §0 step 7 cites `Docs/phases/phase-garrison/report.md` §4 -- reality: `Docs/phases/phase-1/report-garrison.md` §4 (the garrison run was rolled into `phase-1/` 2026-10-26). Same content; read there.
- **2026-10-27 (sequencing).** The brief says "executes after phase garrison (complete 2026-10-26)" and its F-facts were pre-verified against the phase-garrison-complete tree. Reality: the formation-movement cleanup package (owner plan 2026-10-27) executed between garrison and this run -- ADR-19 landed (six default-off `Battle.Movement.*` cvars; progressive fall-in), unit 56/56 + func 15/15 at its closeout. The load-bearing F-facts were re-verified at closeout HEAD `61f25fb`: F1 (servo states incl. `PressRing`/`Charge` rows + weights; `FServoStateFragment::DisplacementWeight`), F2 (death path + `HandleUnitDeath`), F3 (stats fragment/EDU home), F4 (order surface + serial bump), F5 (hash garrison terms), F6 (6 archetypes; tags flip archetypes), F7 (RNG registry), F9-F13 all hold.
- **2026-10-27 (test-file naming).** The brief's touch-point row names `BattleCombatTests.cpp`; the test module convention is `MassBattle*Tests.cpp` (11 siblings) -- implemented as `MassBattleCombatTests.cpp`.

## Log

- [kickoff] Brief + spec transcribed from the owner Desktop pass-back into the repo (`brief-engagement.md`, `Docs/spec/engagement-and-melee.md`; ASCII/LF, `--` style verified). `Docs/README.md` map updated. Movement package closed out first (its D1/D2 tail committed `88c5d04` + report `61f25fb`) so this run starts from a clean tree.
- [task0.1] `BattleCombatFragments.h` created (ECombatPairState + FCombatIntentFragment / FCombatPairFragment / FChargeFragment, blittable, zero tags); `BattleTypes.h` FUnitOrder::EType += AttackMove/AttackUnit + TargetUnit payload. Build Succeeded.
- [task0.2] Seam stubs `BattleEngagement.h/cpp` (FCombatCVars block -- all 16 spec §4.11 cvars, `Enable` default 0; FCombatSolver SlotBudget/ComputeCoverage/PickExposedAngle/DesiredPressure returning failure sentinels) + `BattleMelee.h/cpp` (FMeleeMath HitChance/SkillArcMult/ShieldArcWindow/StaggeredPhaseOffset/ChargeBonusRemaining/KnockbackImpulse, failure sentinels).
- [task0.3] Gate skeletons in `MassBattleCombatTests.cpp` (brief's `BattleCombatTests.cpp` mapped per the naming discrepancy entry): unit T2.1-T2.3 + func T2.4-T2.8 + `Battle.Perf.Gate2` placeholder, all fail-by-design with owning-task notes.
- [task0.accept] Build Succeeded; unit tier 56 passed / 3 failed -- EXACTLY the by-design stubs (HitChance, BudgetCoverage, TallyAssign); func tier 15 passed / 5 failed -- EXACTLY the by-design stubs (S4_ShieldWall, S5_Mob, ChargeMechanics, PairLifecycle, DeterminismTwoRun); T2.9 placeholder registered under `Battle.Perf.Gate2` (runs in neither CI tier's filter). Flags off = no behavior change (no combat readers exist yet; T1.8 soak path untouched).
