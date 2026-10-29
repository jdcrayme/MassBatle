# PR-4: Registry-home the locomotion static maps; extract the assign-pass seams

| | |
|---|---|
| **Finds** | Audit A-2 (root) + D-4: function-local static state in the locomotion hot path; AssignPairsProcessor carries six responsibilities |
| **Blocks on** | Processor-surface refactor -> full unit + func + nightly tier on the UE box; also the owner's preference on processor split ownership (movement brief F-10 deferred the locomotion god-file) |
| **Owner** | Owner decision: do this as its own mini-phase or fold into the Phase-3 tail |

## Motivation

**A-2 root.** The audit shipped the minimal containment: amortized culls of the two function-local
`static TMap`s (`ProbeTicks`, `PaceThrottled`) in `UnitLocomotionProcessor`. The clean fix is for
that state to live on `UUnitRegistrySubsystem` per-unit runtime state (or the formation shared
fragment), where `ResetBattleState` clears it with everything else and the maps are per-world
instead of per-process. The function-local statics are also invisible to the docs' state-ownership
rules (arch §3: registry owns unit-scope state).

**D-4.** `AssignPairsProcessor` (640 lines) currently mixes:
- defender mirror construction
- record mirror construction
- intent resolution (ordered / auto-engage / demo-fix3 defensive reaction)
- disengage fan-out
- the seam call
- a write-back that owns ring geometry, overlay lines, AND charge knockback (the knockback block
  mutates OTHER entities' transforms from the assign pass -- it is charge's business living in
  assign's file; the only reason it is there is game-thread timing, which a
  `ExecuteBefore`-ordered dedicated step would satisfy equally)

The movement brief already logged the locomotion god-file as F-10 (explicitly out of scope then).
The assign file is the combat-side sibling of that finding.

## Proposed change (seams, not a rewrite)

1. **Knockback extraction**: a `UBattleKnockbackProcessor` (game thread, ordered after
   AssignPairs, before the worker phase) owning the one-shot displacement from
   `FChargeFragment {bConnected, bKnockbackApplied, ImpactSpeed}`. AssignPairs' write-back shrinks
   by the whole knockback block. Flag discipline unchanged (gated by the same
   `Battle.Combat.Assign`/`Battle.Combat.Charge` pair).
2. **Intent resolution helper**: `FCombatSolver` gains a pure
   `ResolveUnitIntents(Orders, Records, OutIntents)` covering the ordered/auto-engage/defensive
   table build (the demo-fix3 block), so the processor reads intents instead of computing them.
   Unit-testable on the seam, which today's inline block is not.
3. **Registry homes**: `ProbeTicks`/`PaceThrottled` move to `FUnitRuntimeState` (or a small
   side-map on the registry cleared by `ResetBattleState`); the audit's culls are then deleted.
4. Optional (owner call): the write-back's ring-point computation moves to the seam
   (`FCombatSolver::RingPointFor(...)`), making the processor a pure fragment-mirror bridge.

## Files

- `Plugins/MassBattle/Source/MassBattle/Private/{AssignPairsProcessor, ChargeProcessor, UnitLocomotionProcessor, BattleEngagement}.cpp`
- `Plugins/MassBattle/Source/MassBattle/Public/{BattleEngagement.h, BattleRegistry.h}`
- New: `Plugins/MassBattle/Source/MassBattle/{Public,Private}/KnockbackProcessor.h/.cpp` (+ the
  processors list in `MassBattleModule.cpp`)
- Tests: extend `MassBattleCombatTests` for `ResolveUnitIntents`; the func tier's
  ChargeMechanics/PairLifecycle pins the extracted knockback

## Evidence to gather on the UE machine

- Unit tier 66+ green (TallyAssign unchanged; new intent tests green)
- Func tier 21/21 green including ChargeMechanics (knockbacks=12, bounded impulse EXEC lines
  identical to the phase-2 evidence), PairLifecycle handoff/disengage counts unchanged
- DeterminismTwoRun: two-run hash identity preserved (the extraction must not move any
  game-thread write across a worker boundary)
- Nightly soak: escalation count still 0; AssignMs/TallyMs within baseline noise

## Rollback

Each seam (1-3) is independently revertible; land them as separate commits behind existing cvar
gates where meaningful (the knockback step rides the same flags).

## Status (2026-10-29)

EXECUTED with one falsified premise (`e085dbb`). Registry homes:
ContactProbeTicks/bPaceThrottled on FUnitRuntimeState (culls deleted).
Intent seam: FCombatSolver::ResolveUnitIntents + the
Battle.Unit.Combat.AssignIntents suite. Knockback: FCombatSolver::
ApplyChargeKnockback -- the dedicated UBattleKnockbackProcessor was
built and A/B-falsified (the write-back's waiter-ring math reads live
transforms and knockback victims are themselves seekers, so a post-assign
step drifts ChargeMechanics by one kill even with the count pinned);
the call site stays in the write-back. A/B parity: knockbacks=12
kills=10, PairLifecycle repaired=10, identical to the pre-PR-4 binary
across three runs. Unit 80/80; func 19/23 (the four known); the
nightly-soak item rides the next staged run. Optional step 4 (ring-point
to the seam) not taken -- owner call, unchanged.

## Lessons for future A/B work (2026-10-29)

- **Never judge a func-tier A/B on single runs.** An intermediate build
  produced `repaired=9` and then `repaired=10` across two runs of the
  SAME binary (the second run rode a stale DLL after a failed build).
  Run the compared scenario pair N times per binary and compare bands;
  the landing evidence above is N=3 identical. (ChargeMechanics'
  `knockbacks` was stable in every run observed; the tail counters are
  the noisy ones.)
- **When extracting logic out of a game-thread pass into a separate
  processor, A/B the func pins BEFORE trusting order-equivalence.** Mass
  topology tie-breaking makes "same tick, same flags" claims false in
  surprising ways (here: the write-back's ring math reads live
  transforms of entities that later iterations also read). The cheap
  falsifier is the A/B pair run, not code reading.
- **A failed UBT build leaves the previous binary in place** — an editor
  run after a failed build silently tests STALE code. Check `Result:
  Succeeded` between build and run, or label the logs with the build
  result.
