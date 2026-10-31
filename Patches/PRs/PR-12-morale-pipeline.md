# PR-12: The morale pipeline -- MoraleTick GE, exec calc, fatigue catalog, MoraleTickProcessor (the Phase 4a runtime)

| | |
|---|---|
| **Finds** | Phase-4a brief Tasks 5-6 (spec morale-and-gas sections 4.8-4.10): the seam, the registry state, the EDU spine, the hash block, and the save schema all exist (kickoff patches `0025`-`0028`); the GAS effect surface + the processor that consume them do not |
| **Blocks on** | Task 3's GAS API audit FIRST (the R-M2 item: the 5.8.4 period-forcing symbol + the exec-calc/application signatures vs pinned source); then UE build + full unit/func tier cycle on the reference machine |
| **Owner** | ADR-33 (the E3 tick mechanism, owner 2026-10-31) + ADR-36 (the UnitSync placeholder corrections) ratification when the architecture log is reconciled |

## Motivation

Everything the morale core needs is staged: the `BattleMorale` seam is T4.1/T4.2-locked, `FUnitRuntimeState` carries the rout/exertion/spike block, officers stamp + raise events through the death path, hash block 10 + save v3 round-trip, and the four S-scenario skeletons fail by design. What is missing is the runtime: the MoraleTick gameplay effect + its exec calc (the E3 mechanism), the FatigueState effect catalog (the "full GAS" half -- T4.5's loop), the game-thread morale pass that pushes inputs and applies FSM decisions, and the ExecBridge fixtures that make it observable. None of that can be evidenced without the UE machine, and the GE authoring needs Task 3's pinned-source verdicts first (the first GAS effect code in the project).

## Proposed change

1. `UBattleMoraleTickEffect : UGameplayEffect` (C++-authored, no .uasset): infinite duration, `Period = Battle.Morale.TickPeriod`, one execution definition carrying the exec calc; applied per unit actor at battle start (and on `ResetBattleState` re-application). SetByCaller magnitudes for the effect-owned terms, stamped at application and re-applied only on aggregation events (aura entry/exit, guard flips) -- the remove/re-apply refresh path, per the spec's disclosed deviation from literal per-period SetByCaller mutation.
2. `UBattleMoraleExecCalc : UGameplayEffectExecutionCalculation`: pulls the sim-owned `FMoraleTickInputs` from the registry through the target ASC's owner actor (game thread), calls `BattleMorale::EvaluateMorale` + `EvaluateFatigueState` (ONE home -- the seam is never re-evaluated), writes `Morale` + `Fatigue` through `OutExecutionOutput.AddOutputModifier`. NEVER reads wall-clock, iterates entities, or issues orders (ground rule 2).
3. The deterministic pin (A2/R-M1): under `Battle.Morale.DeterministicTick=1`, a game-thread processor forces the period execution on the fixed sim cadence (the audited symbol; the fallback ladder is spec section 4.8). Verify the double-fire semantics from Task 3's verdict.
4. The FatigueState catalog (A8): six C++-authored infinite GEs writing ONLY the five `*Mod` attributes (ADR-09) at the golden table minus 1; applied/removed on band crossings (aggregation events, never timers).
5. `MoraleTickProcessor` (game thread, `Battle` group, after `TallyProcessor`): the period accumulator, the decay-term pushes (`EvaluateDecay` on the spike + the team shock), the exertion accumulation from movement/pair state, the geometric input folds at the cluster cadence (records + envelopes + engaged-sector mask + aura + cascade), the FSM + rally evaluation in sorted-handle order (canonical-key rolls, A13), and the rout/rally applies through the registry.
6. Registry: `ApplyUnitRoutDecision` (the RC7 generalization: flee = auto-flagged Move away from the threat at run speed x fatigue multiplier + `FlagUnitEngagementsDead` both directions; rally = auto-flagged Stop; player order-serial guard; `RoutCount++` on the break; EXEC_RESULT lines). `GetUnitMoraleState` read surface.
7. UnitSync corrections (A8/A15/R-M9): the `MoraleBonus = Snapshot->Morale` placeholder reverts to the aura sum; the `FatigueAttackMult/DefenseMult = 1 + Fatigue*0.01` couplings revert to the plain `1 + Mod` form (the catalog owns fatigue now); the D-4 resolution -- the `AmmoPool` mirror moves from the fire pass to UnitSync (the single mirror writer). Each disclosed with the consumer-free grep evidence.
8. Officer placement (A5): the layout seam gains the front-center-forward officer slots; the resolve processor consumes it (value-neutral to the spine -- identity/events already landed).
9. ExecBridge: `Battle.Morale.Toggle <unit> <Guard> <0|1>`, `Battle.Morale.Dump <unit>` (rout state, values + staleness, the contributions breakdown, rout count, toggles), `Battle.Morale.SpawnRout` (a general + adjacent clash fixture, dictionary-keyed rows).
10. `Battle.Morale.Enable` -> default 1 when the FSM lands end-to-end (ground rule 6; flags-off identity asserted before the flip).

## Files

- New: `Plugins/MassBattle/Source/MassBattle/Public|Private/{BattleMoraleEffects,MoraleTickProcessor}.h|.cpp` (the GE + exec calc + catalog + processor)
- Edit: `BattleRegistry.h/.cpp` (`ApplyUnitRoutDecision`, `GetUnitMoraleState`), `UnitSyncProcessor.cpp` (the corrections + the mirror move), `RangedFireProcessor.cpp` (the mirror writer REMOVED -- D-4), `BattlePathFrameLayout.h/.cpp` + `FormationSlotResolveProcessor.cpp` (officer slots), `BattleExecCommands.h/.cpp` (three commands), `BattleDebugSubsystem.h/.cpp` (MoraleMs/TickAgeMax/Routs/Rallies/Shattered/OfficerDeaths/AuraActive metrics + the morale-bar/aura-ring/flee-arrow overlay rows), `UnitActor.h/.cpp` (the GE application lifecycle)
- Reset path: `ResetBattleState` removes the active GE handles + clears the team state (already shipped)

## Evidence to gather on the UE machine

- Unit tier green (the kickoff suites unchanged); func tier green EXCEPT the four skeletons until PR-13
- A fatigue-band fixture: GE apply -> attribute write -> snapshot -> UnitSync -> `FatigueAttackMult` visible in the fragment within ONE frame (T4.5's wiring half; the S-scenario asserts the rest)
- Two overlapping effects aggregate in the documented order (the stacking assertion)
- `Battle.Morale.SpawnRout` -> the EXEC_RESULT chain: officer kill -> event count + the spike delta; rout -> pairs released + flee movement + `Flee` state; rally under the aura vs without (the T4.3 seam rehearsal at fixture level)
- Flags-off identity: the unchanged T1.8/T2/SoakVolley soaks with `Battle.Morale.Enable=0` (the death-path branch also gates on it -- no officer events, no spike)
- Archetype count stable (8) across all spawn paths (officers add no compositions)

## Rollback

`Battle.Morale.Enable=0` restores byte-identical pre-morale behavior: no GE applied, no catalog, the processor early-outs, the death-path officer branch gates off. Every subsystem addition is additive; the UnitSync corrections are the one behavior-visible pair (both couplings verified consumer-free -- disclosed, not reverted by the flag).
