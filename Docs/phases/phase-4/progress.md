# Phase 4a -- Morale & GAS: Progress Log

| | |
|---|---|
| **Session** | 2026-10-31, PR-chain execution (the open writeups PR-12..PR-15; protocol per `Docs/AGENTS.md`) |
| **Engine** | UE 5.8.4 at `E:\Unreal\UE_5.8` (this machine) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin module `MassBattle`, test module `MassBattleTest`) |
| **Start state** | Task 0 verified on `main` @ `d071ea3` (unit 83/83; func 19/27 = the expected 8); the run's task order picks up at the PR-chain prerequisite (Task 3, the GAS API audit) per the owner's directive to work the open PR writeups; Tasks 1-2 (the inherited blocker hunts) remain ahead in the brief's order -- PR-15 carries Task 1's disposition requirement |
| **Kickoff state** | Patch series `0024..0029` applied from the design-pass bundle (`TMP/`, the phase-4a design pass), landed on `main` @ `afb0b69..aaa0dd0` via `git am` (authorship preserved, zero conflicts), then machine-verified with fixes folded on top (F4-1..F4-5 below) |

## Discrepancies (dated, newest first)

- **2026-10-31 (Task 3, API audit)** -- brief section 4 named
  `UAbilitySystemComponent::MakeOutgoingGameplayEffectSpec`; in pinned 5.8.4
  that name exists ONLY on `UGameplayAbility`
  (`Abilities/GameplayAbility.h:207,210`). The ASC-side helper is
  `UAbilitySystemComponent::MakeOutgoingSpec(TSubclassOf<UGameplayEffect>,
  float Level, FGameplayEffectContextHandle)`
  (`AbilitySystemComponent.h:372`). Corrected in the owning reference
  (engine-api Part A section 16 row 7); PR-12 constructs the spec directly
  (`FGameplayEffectSpec(Def, Context)`) anyway -- the per-application
  Period override needs the spec form.
- **2026-10-31 (Task 3, R-M2 verdict)** -- the forced-execution path
  (`ExecutePeriodicEffect` -> `InternalExecutePeriodicGameplayEffect`) does
  NOT reset or clear the natural repeating FTimerManager timer
  (`GameplayEffect.cpp:4497-4507`): forced + natural executions interleave
  at independent cadences -- the double-fire risk is REAL if the pin kept
  the natural timer. Fix designed (cleaner than the spec 4.8 fallback
  ladder, disclosed): under `Battle.Morale.DeterministicTick=1` the spec is
  applied with `Period = NO_PERIOD` -- the application gate
  (`GameplayEffect.cpp:4497`) then never creates the timer, so forced
  executions are the ONLY executions. Shipping mode keeps
  `Period = TickPeriod` (natural wall-clock; T4.8 bounds staleness).
  `ExecutePeriodicEffect` runs fine on a NO_PERIOD active GE (no period
  prerequisite on the forced path; the `check` at
  AbilitySystemComponent.cpp:1216 belongs to the NET path). Recorded in
  engine-api Part A section 16 + Part B section 4.14.

## Task 3 -- GAS API audit (the brief section 4 digest, executed)

- [task3.1] Scripted audit over the section 4 symbol list against the
  pinned source `E:\Unreal\UE_5.8\Engine\Plugins\Runtime\GameplayAbilities\
  Source\GameplayAbilities\` (the module is an engine PLUGIN in 5.8, not
  under `Runtime/`): GE authoring fields, `FGameplayEffectExecutionDefinition`,
  the exec-calc class + `Execute_Implementation` shape, execution params
  accessors, `AddOutputModifier`, capture-def structs/macros (empty capture
  list confirmed legal -- the executions loop runs each definition's CDO
  `Execute` unconditionally, `GameplayEffect.cpp:3274`), spec construction
  (MakeOutgoingSpec rename), application/removal, SetByCaller read/write,
  catalog static-modifier authoring, `ExecutePeriodicEffect` + the periodic
  timer path, `FActiveGameplayEffect` immutability posture,
  `OnPeriodicGameplayEffectExecuteOnSelf` evidence hook.
- [task3.2] Verdicts merged into `Docs/reference/engine-api-5.8.md`:
  Part A section 16 (the per-symbol verdict table with file:line citations)
  + Part B section 4.14 (the operational code-shape digest + the pin design
  + the facts list). A2 GO/NO-GO: **GO** -- the A2 shape stands, the pin
  ships as spec-side `Period=NO_PERIOD` + processor-forced
  `ExecutePeriodicEffect` at the sim cadence (strictly stronger than the
  fallback ladder's remove/re-apply: no GE churn, no double-fire, no GE
  removal churn in the FSM path). The fallback ladder stays recorded in
  the spec (owner reconciles at the ADR-33 application).
- [task3.3] No verdict killed the A2 shape; no spec fallback applied. The
  two corrections above (MakeOutgoingSpec rename; double-fire semantics +
  the NO_PERIOD pin) are the audit's findings, both in the reference file.

- **2026-10-31 (Task 0, build)** -- F4-1 (compile blocker, patch `0028` splice
  error): the v3 morale block (comment + 9 UPROPERTYs) was pasted TWICE into
  `FUnitSaveEntry` (`BattleSave.h`) -- UHT "shadowing is not allowed" x9. Fix:
  duplicate block removed on main; the landed header matches the patch's intent.
- **2026-10-31 (Task 0, build)** -- F4-2 (compile blocker, patch `0025` splice
  error): the four T4.3-T4.6 skeleton branches inserted into
  `MassBattleFuncTestHarness.cpp`'s BeginTest chain lost the Skirmish branch's
  closing brace and gained a spurious one before the T0.9 comment (net
  balanced, so the compiler surfaced only `illegal else without matching if`
  at the first inserted branch). Fix: the missing brace restored, the spurious
  one removed -- the chain reads exactly as the phase-3 pattern.
- **2026-10-31 (Task 0, run 1)** -- F4-3 (runtime crash, patches `0026`/`0028`
  authoring): `RebuildMoraleAuxState()` took `UE_MT_SCOPED_WRITE_ACCESS` while
  its only caller (`RestoreBattleState`) already holds the write access -- the
  MT access detector asserted (`SaveRoundTrip` aborted the unit queue with
  engine exit 3, 13/13 tests run). Fix: the codebase's
  `NotifyCombatEntityDeathInternal` pattern -- locked public wrapper +
  unlocked `RebuildMoraleAuxStateInternal()`; restore calls the Internal form.
- **2026-10-31 (Task 0, run 2)** -- F4-4 (golden re-signs the kickoff sweep
  missed, patch `0026`): the officer lines grow rosters (Row 1 +2, Rows 2/3
  +1) and three unit-tier count goldens stayed literal: `EntityConfigIntegrity`
  (row roster == 40) failed x3; `FirePass` (rule-holds == 40) and `Impacts`
  (volley in flight == 40) failed x1 each. Fix: the expectations now DERIVE
  from the data (per-row `FBattleRowStats::FromEdu` soldier+officer counts;
  `Registry->GetRosterSize` for the shooter counts) -- honest against future
  fixture edits, per the fixture-honesty (B-3) discipline.
- **2026-10-31 (Task 0, run 3)** -- F4-5 (same class, func tier): `S1_MarchWheel`
  asserted a literal 320 live entities (its 8 spawned units are 4x Levy 40 +
  4x Sergeants 42 = 328 now) and `S6_Volley`'s conservation identity hardcoded
  16 men x 25 ammo (the archers are 16 soldiers + 1 EDU officer = 17 x 25 =
  425). Fix: S1 sums the spawn results into `SpawnedEntityTotal` (the
  assertion is CONSERVATION across the march, now honest by construction);
  S6 derives the head count from the roster (the ammo-fraction denominator is
  already roster-stamped, so the zero-fraction check agrees).

## Task 0 -- kickoff patch application + verification

- [task0.1] Patches `0024..0029` applied with `git am` directly on `main`
  (the phase-3 audit-series precedent): `afb0b69` (docs) `69a6299` (gate
  skeleton) `6196711` (EDU spine) `9c63043` (seam bodies) `409b5b9` (state
  spine) `aaa0dd0` (PR writeups). Zero conflicts against `c61dfac` (the
  melt-fix commits touch `AssignPairsProcessor.cpp` + README regions the
  series' hunks do not overlap). The TMP doc copies were verified
  content-identical to the patch-embedded versions (LF/EOL only).
- [task0.2] Build `MassBattleEditor Win64 Development` -> **Succeeded** (after
  F4-1/F4-2). The only warning in the tree remains the pre-existing
  `BattleSpawnLibrary.cpp` C4457 (line shifted by the EDU patch; same site).
- [task0.3] Unit tier (`run_tests.bat unit`): **passed=83 failed=0 total=83**
  (was 80/80 pre-kickoff; +3 = `Battle.Unit.Morale.{Stack,Fatigue,RoutRally}`).
  Includes the re-signed goldens: `Battle.Data.EduRoundTrip` (16-key schema),
  `Battle.Unit.Combat.SaveRoundTrip` (v3 round-trip), `HashTerms` (block 10),
  `SpawnComposition` (count UNCHANGED -- officers add no compositions).
- [task0.4] Func tier (`run_tests.bat func`): **passed=19 failed=8 total=27**
  -- exactly the expected set, no surprises:
  - four known pre-existing (F17 class): `DeterminismTwoRun` (blocker 1),
    `S4_ShieldWall`, `S4_WidthOverride`, `S5_Mob` (the melt honest-signatures)
  - four new fail-by-design skeletons (36001-tick budget timeouts):
    `S7_RoutCascade`, `OfficerDeath`, `EffectEndToEnd`, `ToggleBehaviors`
  - (the two mid-pass regressions F4-5 were fixed and the tier re-run clean)
- [task0.5] Spawn perf with `bOfficer` present (F18 re-baseline check,
  record-don't-gate): the SaveRoundTrip 2-roster spawn pair reads
  `SpawnPerf 49.4 + 24.1 us` for 40 Levy + 42 Sergeants (73.5 us aggregate,
  82 entities) vs the phase-0 35.1 us / 80-entity record -- the roster grew
  by the officer counts and wall-clock micros are machine-state-sensitive
  (the phase-3 audit's perf-gate watch item); re-baseline properly on the
  nightly path with Tasks 8/9.
- [task0.6] Archetype stability: composition determinism asserted green by
  `Battle.Unit.Combat.SpawnComposition` (identical spawns -> identical
  archetype counts, now with the officer role variant in the composition).
  The explicit "count 8" soak-line evidence is the nightly's
  `BattleMovementSoakController` FINAL line -- deferred with Tasks 8/9
  (no nightly was run in this pass).

## Deferred to the implementing run (the brief's Tasks 1-9)

- **Task 1** -- blocker 1 hunt (mixed clash+volley two-run mm-scale
  divergence; `det_trace8.log` trace shape) -- unchanged, PRECEDES morale work.
- **Task 2** -- blocker 2 (staged movement-soak dress gate `dressErr
  30.25/6.0m`) -- unchanged; unblocks the nightly's baseline step.
- **Task 3** -- the GAS API audit (section 4 digest vs pinned 5.8.4 source;
  the R-M2 period-forcing symbol first) before PR-12 builds on it.
- **Tasks 4-9** -- data-spine ratification beyond the tier, the MoraleTick GE
  pipeline, the morale pass + Enable flip, the four T4.3-T4.6 scenario bodies,
  T4.7/T4.8 + soak + baselines, overlay/metrics/docs closure (the full DoD,
  brief section 7).
- **PR-12..PR-15** -- the machine-gated writeups (per-writeup status sections
  remain PENDING; landed with the series).
- **Owner items** -- ADR-32..37 proposals pending the architecture-log commit;
  the 4b brief at its kickoff.
