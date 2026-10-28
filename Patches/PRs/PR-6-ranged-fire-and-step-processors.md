# PR-6: Ranged fire + projectile stepping processors (the Phase 3 pipeline)

| | |
|---|---|
| **Finds** | Phase-3 brief Tasks 3-5 (spec ranged-stack sections 4.9-4.10): the seams, fragments, pool, hash terms, and gates all exist (kickoff patches `0011`-`0015`); the PROCESSORS that consume them do not |
| **Blocks on** | UE build + full unit/func tier cycle on the reference machine (processor registration, command-buffer defer paths, GAS attribute writes all need runtime evidence) |
| **Owner** | ADR-28 ratification (the game-thread deviation from arch section 4 row 9's worker label) when the architecture log is reconciled |

## Motivation

Everything Phase 3 needs is staged: `FRangedStateFragment` is spawn-stamped, `FBattleProjectilePool` lives in `UBattleProjectileSubsystem`, `SelectVolleyShots`/`EvaluateSkirmish` are T3.3-locked, the state hash covers ammo + pool, and the two S-scenario skeletons fail by design. What is missing is the runtime: two game-thread processors (fire + step), the impact resolution through the melee machinery (RC6), the registry toggle/out-of-ammo surface, and the ExecBridge fixtures that make it all observable. None of that can be evidenced without the UE machine.

## Proposed change

1. `ProjectileStepProcessor` (game-thread, FIRST in the `Battle` group, `Battle.Ranged.Step`): integrator loop over live pool entries (the `StepProjectile` seam), TTL/ground-hit release (`SampleHeight`), impact STUB first (log-only), then the real swept-segment query (midpoint circle + height early-out, R-R2). Kills defer `FDeadTag` through the command buffer -- the F2 death path, never a new damage queue.
2. `RangedFireProcessor` (game-thread, after `AssignPairs`, `Battle.Ranged.Fire`): roster-ordered shooter-view gather, `SelectVolleyShots`, pool writes, reload advance, out-of-ammo aggregate + the one-shot `bOutOfAmmo` flip + the `AmmoPool` GAS mirror (A14), skirmish evaluation on the 4 Hz cluster geometry with the self-issued retreat move (A8).
3. Registry: `SetUnitFireToggles` / `GetUnitFireToggles` / `GetUnitAmmoFraction` (the fields + spawn default already shipped in `0012`); bad-handle `EXEC_RESULT` rejects per the A11 pattern.
4. `BuildSpatialHashProcessor`: the `ECombatHashFlags::RangedCapable` stamp (one line; the enum shipped in `0011`).
5. ExecBridge: `Battle.Ranged.Toggle`, `Battle.Ranged.Dump`, `Battle.Ranged.SpawnVolley {line|skirmish}` (dictionary-keyed, the B-3 lesson), `Battle.Ranged.PerfStress` (the T3.6 fixture, PR-9 consumes it).
6. `Battle.Ranged.Enable` -> default 1 when the impact task lands end-to-end (B.8 item 6; the flags-off identity is asserted before the flip).

## Files

- New: `Plugins/MassBattle/Source/MassBattle/Public|Private/{RangedFireProcessor,ProjectileStepProcessor}.h|.cpp`
- Edit: `BattleRegistry.h/.cpp` (toggle surface + ammo fraction), `BattleExecCommands.cpp` (four commands), `BuildSpatialHashProcessor.cpp` (stamp), `BattleDebugSubsystem.h/.cpp` (Shots/Holds/Hits/FriendlyHits/Drops/InFlightPeak/FireMs/StepMs metrics + trajectory/impact overlay rows), `BattleMovementSoakController.cpp` (the SoakVolley variant, PR-9)
- Reset path: `ResetBattleState` clears the pool (`ResetForBattle`) + toggles

## Evidence to gather on the UE machine

- Unit tier green (the kickoff suites + extended `SaveRoundTrip` v2 once the toggles persist); func tier green EXCEPT the two skeletons until PR-7
- `Battle.Ranged.SpawnVolley line` -> `EXEC_RESULT` chain: eligibility -> shots -> pool entries -> decrements -> impacts -> `RosterDeath` (the ChargeMechanics kill-chain precedent)
- Hold-fire + friendly-lane holds observable via `Battle.Ranged.Dump` (Holds metric by rule)
- Flags-off identity: the unchanged T1.8/T2 soaks with `Battle.Ranged.Enable=0`
- Archetype count stable (8 expected) across all spawn paths

## Rollback

`Battle.Ranged.Enable=0` restores byte-identical pre-ranged behavior (the step processor early-outs on live-count 0; the fire processor gates on the flag). Every subsystem addition is additive.
