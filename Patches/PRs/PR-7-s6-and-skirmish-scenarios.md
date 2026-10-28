# PR-7: S6_Volley + SkirmishBehavior scenario actors (replace the skeletons)

| | |
|---|---|
| **Finds** | Phase-3 brief Task 6: the T3.4/T3.5 gates are registered as fail-by-design skeletons (kickoff patch `0011`); the real scenario machines need the PR-6 pipeline |
| **Blocks on** | PR-6 landed and green; the func tier deliberately runs RED on the two skeletons until this PR replaces them |
| **Owner** | None (pure code + the existing harness; no editor work) |

## Motivation

S6 is the Phase 3 exit criterion: dispersion, LOS, friendly-fire rules, and ammo accounting asserted in ONE scenario (roadmap B.3), plus the skirmish behaviors (T3.5). The skeleton actors, the harness registrations, the factory entries, and the cast branches all shipped in `0011` -- only the scenario bodies (spawn -> order -> sample -> assert phase machines) are missing, and they cannot be written against a pipeline that does not run yet.

## Proposed change

1. `AS6VolleyTest` body (replace `StartTest`/`Tick`): one archer unit vs one formed infantry unit at 100 m, flat ground, seeded; phase machine over fixed ticks:
   - dispersion: gather impact markers (the debug-subsystem records), assert the spread stays inside the profile cone bound projected at the target range;
   - LOS: the scripted hill variant (a `StampHill`-shaped fixture through the mutable cost grid) holds the flat-profile fire while the arced profile fires (the RC2/RC3 demonstrable pair);
   - friendly-fire: walk a friendly unit into the lane (holds) + a scripted stray-hit case (impact-point damage on a friendly);
   - ammo: decrements == shots (the `Battle.Ranged.Dump` probe), the aggregate reaches zero, `bOutOfAmmo` flips exactly once.
2. `ASkirmishBehaviorTest` body: archers with skirmish on; cavalry ordered at them. Assert: retreat triggers inside `SkirmishBreakDistM`, the unit keeps facing, resumes fire past the resume distance (hysteresis, no thrash over a settle window), the hold-fire variant never fires but still retreats, and a player `Move` order overrides the auto-retreat (order-serial guard).
3. Local cvar save/restore including the ranged block (the `ClashSetCvars` precedent); `EXEC_RESULT` evidence lines per phase.

## Files

- Edit: `Plugins/MassBattle/Source/MassBattleTest/Private/BattleFuncTestActor.cpp` (the two bodies; the declarations, factories, harness entries, and cast branches already exist)
- Edit: `Public/BattleFuncTestActor.h` only if the actors grow state fields (phase enums, sample arrays)

## Evidence to gather on the UE machine

- Func tier FULLY green (the two skeletons now pass; 23+ `Battle.Func.*` total)
- Verbatim `EXEC_RESULT S6_Volley ...` / `EXEC_RESULT SkirmishBehavior ...` lines with the sampled numbers (the S4/S5 evidence precedent)
- Determinism spot: rerun S6 under the same seed twice -> identical sample lines

## Rollback

The scenario actors are test-only code; reverting the bodies restores the skeletons (the func tier goes red on exactly those two, by design). No runtime surface is touched.
