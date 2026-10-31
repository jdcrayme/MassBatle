# PR-13: The four morale scenario bodies (T4.3-T4.6 -- S7_RoutCascade, OfficerDeath, EffectEndToEnd, ToggleBehaviors)

| | |
|---|---|
| **Finds** | Phase-4a brief Task 7 (spec morale-and-gas section 5): the four fail-by-design skeletons shipped in `0025`; PR-12's pipeline must land first (the scenario machines consume `ApplyUnitRoutDecision`, the GE catalog, and the morale read surface) |
| **Blocks on** | PR-12 executed + a full func-tier cycle on the reference machine (the fixed-tick phase machines need the live pipeline) |
| **Owner** | None beyond the standing brief approvals (the scenario contracts are the spec's section 5 rows, MC5-MC9) |

## Motivation

The exit criteria are scenario-shaped: "S7 passes; routing cascade observable across adjacent units; general death produces measurable morale shock and rally behavior" (roadmap Part A, Phase 4). The seam math is unit-locked (T4.1/T4.2) and the pipeline is PR-12's; what remains is the four fixed-tick phase machines that assert the behavior end to end -- the skeletons' planned `EPhase` machines, now real. Blocks on PR-12 because every assertion reads PR-12 surfaces.

## Proposed change

1. `Battle.Func.S7_RoutCascade` (T4.3, spec section 5): three adjacent same-team infantry engaged frontally + ranged attrition scripted onto the center unit. Phases: Spawn -> Attrition (scripted volley fire onto the center unit until its morale drops) -> Break (assert the wavering-then-break ORDER with an EXEC_RESULT line per transition; the flee: engagement records dead both directions, movement away from the threat, `Flee` movement state) -> Cascade (assert at least one neighbor enters wavering attributable to the cascade term -- an isolation sub-run with `Battle.Morale.CascadeRadiusM 0` diffs the outcome) -> Rally (a general unit walked into aura range: rally succeeds within a bounded window; a second sub-run without the general fails indefinitely -- two seeded runs) -> Chase (a pursuer's contact angle lands in the rear band against the fleeing unit's facing). `MoraleSetCvars/RestoreCvars` beside the existing helpers (the C-3 one-home rule; the list: `Battle.Morale.Enable/.TickPeriod is NOT session-on -- DeterministicTick is`).
2. `Battle.Func.OfficerDeath` (T4.4): a two-officer unit engaged; a scripted officer-entity kill (the ExecBridge kill targeting the officer roster index) -> assert `Battle.Unit.OfficerDied` event count on the unit's ASC, the amplified spike vs a baseline non-officer kill (two seeded sub-runs, the delta in the `Battle.Morale.Dump` breakdown), and the general variant: a `bIsGeneral` unit's officer 0 killed -> `Battle.Team.GeneralDied` events on every same-team ASC + the shock term decaying over subsequent ticks (sampled at two times, monotone).
3. `Battle.Func.EffectEndToEnd` (T4.5): drive a unit across two fatigue bands (scripted running); per band assert the GE -> attribute -> snapshot -> `FatigueAttackMult` fragment latency (<= 1 frame from the attribute write, the S0_SyncLatency assertion shape) and the multiplier values against the golden table; then an overlapping second effect applied mid-band -- the aggregate matches the documented modifier order.
4. `Battle.Func.ToggleBehaviors` (T4.6): GuardMode ON suppresses the impetuous auto-charge (an Impetuous-discipline unit with an enemy inside `ImpetuousChargeRadiusM` holds; OFF charges within a bounded window); the phase-3 toggle rows re-asserted unchanged (FireAtWill hold-fire, skirmish break/resume); a player order overrides the auto-charge (the serial-bump).
5. All four registered via the factory (already shipped in `0025`); fixed-tick machines; every completion logs its EXEC_RESULT line; cvar save/restore through the one-home helper.

## Files

- Edit: `BattleFuncTestActor.h/.cpp` (the four skeleton bodies replaced by the real machines -- the header's planned phase enums + members are already the shape), `MassBattleFuncTestHarness.cpp` (nothing -- the gates registered in `0025`)
- Edit: `BattleExecCommands.cpp` if the scripted officer-kill needs a targeted variant of the existing kill fixture (dictionary-keyed, the B-3 lesson)

## Evidence to gather on the UE machine

- Func tier: the four scenarios PASS with verbatim evidence lines in the progress log; the four known pre-existing failures unchanged (DeterminismTwoRun/S4_ShieldWall/S4_WidthOverride/S5_Mob -- Task 1/2 territory)
- The isolation sub-runs (cascade-off, no-general) each produce their diff line
- The 1-frame latency assertion sampled at both bands (the min/max across the sampled frames recorded)

## Rollback

The four scenarios join the PR gate; a revert restores the skeletons (fail-by-design) -- never a sim-behavior change. The scenario code is test-module only.

## Status (2026-10-31, the PR-chain session: EXECUTED -- all four scenarios PASS)

Func tier **23/27**: T4.3-T4.6 green; the failures are EXACTLY the four
known pre-existing ones (DeterminismTwoRun / S4_ShieldWall /
S4_WidthOverride / S5_Mob -- the Task 1/2 dossiers). Unit tier 83/83
unchanged. Evidence: `Tools/CI/pr13_evidence.log` (verbatim lines) +
`Docs/phases/phase-4/progress.md` (the PR-13 section, per-scenario
tuning notes and measured fixes). Highlights:

- S7_RoutCascade: wavering(18) < break(50) order; flee anchor -40 m away
  from the threat with the servo's Flee state stamped; the cascade wavers
  the neighbor with zero own casualties (isolation run B diffs it at
  CascadeRadiusM 0); rally at tick 447 under the general's aura (run B
  never rallies); the pursuer's contact angle 112 lands in the rear band
  [96,192).
- OfficerDeath: the amplified spike ratio EXACTLY 3.00 (officer 2.523 vs
  baseline 0.841 at a fixed tick alignment); the general variant's
  Battle.Team.GeneralDied events + the stamped shock decaying monotonically.
- EffectEndToEnd: the Tired goldens (0.800/0.850/0.900) visible in the
  fragment with latency 1 frame; the stacking aggregate 0.850 = 1 + (0.05
  base - 0.2 catalog mod).
- ToggleBehaviors: the guard suppression window, the charge at tick 10
  unguarded, FireAtWill hold/resume, the skirmish break, the player-order
  serial-bump cancel.

Measured fixes folded in (full notes in the progress log): the morale
cvar restore + fixture teardown ride CompleteTest (the framework's
Destroyed lands after the next test starts -- the leaked Enable=1 broke
S0_SyncLatency until fixed); the auto-charge serial-guard clear runs
regardless of the FSM state; the overlap write offsets the attribute
BASE, not the mod-inclusive current.
