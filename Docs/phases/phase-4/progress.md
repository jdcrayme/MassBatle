# Phase 4a -- Morale & GAS: Progress Log

| | |
|---|---|
| **Session** | 2026-10-31, PR-chain execution (the open writeups PR-12..PR-15; protocol per `Docs/AGENTS.md`) |
| **Engine** | UE 5.8.4 at `E:\Unreal\UE_5.8` (this machine) |
| **Project** | `E:\Unreal Projects\MassBattle` (plugin module `MassBattle`, test module `MassBattleTest`) |
| **Start state** | Task 0 verified on `main` @ `d071ea3` (unit 83/83; func 19/27 = the expected 8); the run's task order picks up at the PR-chain prerequisite (Task 3, the GAS API audit) per the owner's directive to work the open PR writeups; Tasks 1-2 (the inherited blocker hunts) remain ahead in the brief's order -- PR-15 carries Task 1's disposition requirement |
| **Kickoff state** | Patch series `0024..0029` applied from the design-pass bundle (`TMP/`, the phase-4a design pass), landed on `main` @ `afb0b69..aaa0dd0` via `git am` (authorship preserved, zero conflicts), then machine-verified with fixes folded on top (F4-1..F4-5 below) |

## Discrepancies (dated, newest first)

- **2026-11-03 (T4.7 LANDED -- the two-run gate covers the full morale loop;
  the phase-3 dossier closed)** -- the PR-15 deferred item landed
  (`aa7e17d`): its recorded landing condition ("it lands with Task 1's
  disposition", pr15.3) was met by the green base. The fixture gains the
  general + the script unit (the row-1 sergeants, Disciplined stamped
  through the registry seam) + the impetuous row, and the Tick drives six
  scripted deaths at the S7 cadence; the Evidence line gains
  routs/rallies/moraleTicks. Result: **DeterminismTwoRun PASS 40/40
  mismatches=0 routs=10 rallies=2** isolated (`det_t47e.log`) AND in-gate
  (`last_run/func.log`); unit 85/85; func 25/27 = exactly the known pair.
  Three measured lessons rode the fixture iterations (full detail in the
  commit): the aura fold reads the FORMATION ANCHOR, which a rout-flee
  does not move (the rally geometry must sit on the ANCHOR, not the flee
  path); the RoutingFriends cascade drag (-~1.2/routing neighbor inside
  the cascade radius) must be designed out of a rally window; and the
  rally roll's attempt counter increments on every Routing pass, so the
  deterministic CombatRoll01 sequence positions the first hit -- the
  Disciplined factor pulls it inside the window (replicated offline, and
  the measured MoraleRally Roll=0.258 matched the computation exactly).
  **The phase-3 det_* dossier is CLOSED**: its signatures are explained
  and fixed by `4ace397` (the slot-assigner chunk order) + `b939037` (the
  morale cadence straddle); the mixed two-run gate is green; the dossier
  logs are untracked pre-fix artifacts -- no re-run owed. Remaining for
  the 4a close: Task 2 (the staged movement-soak dress gate), Task 8's
  staged-nightly remainder + the baselines, Task 9's B.7 entry, the
  report; owner-gated: H-4, the B-1..B-8 4b approvals, the cadence-watch
  line authorization (the closure pack items).

- **2026-11-02 (Task 1 hunt, session 3 -- the gate is GREEN; the residue had
  two layers)** -- the session plan
  (`Docs/phases/phase-4/task1-hunt-archetype-census.md`, executed with the
  recommended H-1/H-2/H-3; H-4 remains an owner call). Layer 1 confirmed
  the session-2 entry point's artifact model EXACTLY (probe
  `det_census.log`: run 1 sampled the archetype count 4->6->8 -- the death
  variants, +2 per first kill of a composition family; run 2 constant 8 at
  all 40 samples; sections 1-2 matching): the cumulative
  `DebugGetArchetypesCount` term violated the hash's own no-non-sim-state
  contract and was retired for the LIVE COMPOSITION CENSUS (per-entity
  presence bitset over the 20-type `kCensusTypes` list, one home =
  the `CensusBits<Ts...>` argument list; count demoted to the permanent
  `Det2RunCensus` gate telemetry). Layer 2: the falsifier row fired anyway
  -- samples 31-40 kept diverging with equal counts, and the per-section
  bisector (now 10 TRUE per-section hashes; the old 6 cumulative-prefix
  hashes mislabeled sections 4-10) pinned a REAL live drift at the first
  kill window (t720). The instrumented hunt (Battle.Det.RunTag/Tick cvars
  + DetPair/DetRoll/DetRngRoll/DetMor canonical traces; logs
  `det_census*.log`, `det_trace_s3*.log`): kills #1-4 TICK-IDENTICAL
  (681/682/693/694), then run 1's unit 3 BROKE (morale -0.093 at its t=694
  pass -> rout -> its kills stop) while run 2's t=693 pass still read
  +0.954 and kept killing at t=700-712. ROOT CAUSE: the morale pass's
  cadence accumulator is registry state that `ResetBattleState` zeroes
  mid-frame -- the same frame's pass then fired on the emptied world and
  RE-ARMED 0.35, so run 2 inherited a one-tick-shifted phase (21k vs
  1+21k) and the tick-identical kill #3 straddled the boundary. FIX: an
  empty-world fire STAYS PRIMED (cooldown -> 0; fire on the first
  populated tick) -- reset-ordering-immune (`MoraleTickProcessor.cpp`).
  Gate: `DeterminismTwoRun PASS samples=40 mismatches=0` isolated
  (`det_fix1.log`) AND in-gate; unit 85/85 (new
  `Battle.Unit.StateHash.CensusGuard`: exec-kill flips the census slice;
  an extra throwaway kill between identical mini-battles leaves it equal);
  func 25/27 = exactly S4_WidthOverride + S5_Mob (signatures unchanged).
  Owner-facing residuals: H-4 (the B.4/B.5 wording patch, proposed in the
  record §9); the phase-3 dossier re-read (its two-run signature is
  consistent with the same morale-phase straddle, now fixed); the morale
  goldens are cadence-sensitive in principle -- unit tier held this time.

- **2026-11-01 (Task 1 blocker hunt, session 2 -- three canonical-order fixes
  landed, the residue narrowed)** -- the DeterminismTwoRun hunt on `main`
  (`4ace397`). THREE real defects fixed: (1) the func gate's map pin -- the
  demo shell's GameDefaultMap change made the unpinned -game gate silently
  load L_MassBattle_Demo01 (the two-run gate ran inside the demo GameMode's
  world; the tests self-isolate, but the gate's world was wrong since PR-1);
  (2) the two-run gate never pinned the morale tick (the spec 4.8 text
  prescribes it; the T6.1 flip put the wall-clock GE inside the equality);
  (3) TWO canonical-order defects: the slot assigner bid in CHUNK order
  (tied bids swapped roster slots 8/11 between runs -- the phase-3 dossier's
  entities-8/11 signature explained) and the assign stage's Defenders/
  Seekers arrays were mass-handle/chunk ordered (the ring-target divergence;
  the write-back now gathers entity-keyed, replacing the two-cursor walk).
  Diagnostic infrastructure built and reverted: the tagged hex-float servo/
  resolver traces (det_hex*.log/det_sec*.log) + the SECTION BISECTOR wired
  into the test permanently (the hash's OutSectionHashes; on mismatch it
  names the first diverging section). Current DeterminismTwoRun state:
  sections 1 bookkeeping + 2 transforms MATCH (the fixes verified); the
  residue = section 3 ARCHETYPE COUNT (some tag-set membership differs per
  run) cascading into the shared fragments -- the next hunt's entry point
  (probe: per-archetype instance counts under WITH_MASSENTITY_DEBUG).
  S4_WidthOverride ("S4 refront assertions failed") and S5_Mob ("no pull-down
  (240 s; exposed=5 waiters=66)") signatures UNCHANGED -- independent
  mechanisms, their own sessions. Tiers: unit 84/84; func 24/27 = the three
  known. Tooling: UE worker-thread log interleaving defeats file-order log
  segmentation -- cross-run traces need an explicit run tag + a per-entity
  tick counter (the playbook carries the Git Bash MSYS note already).

- **2026-11-01 (T6.1 flip landed + the demo re-clock)** -- the owner signed
  off the audited family; `t61-morale-threshold-audit` fast-forwarded to
  `main` (`a0c5362`) + the spec tables rode the flip commit (`8e83f4e`:
  morale-and-gas 4.12/4.4, demo-shell section 7). The re-clock boots ran the
  demo arc WITHOUT the ResolveProbe (`m7_reclock_boot.log`, 300 game-s;
  `_boot2`, 700 game-s): the real cascade fires on the demo map --
  GeneralDeath Shock=+6.00 then 3 ORGANIC enemy routs (E1 levy 0.17, E2/E3
  sergeants -1.18/-1.00; the PR-12-era stalemate is gone), beats 0-6 on the
  scheduled clock (0/8/18/30/85.1/105.1/120.1) -- and the verdict does NOT
  fire naturally: the field goes static past t~300 (zero kills 300->700;
  E4/C2 are never ordered forward, no auto-acquisition). Disposition:
  `Demo.ResolveProbe` STAYS (re-documented as the arc-completion driver,
  not a threshold mask); beat 5's `Router=none` (the winning side doesn't
  route; the field-center fallback covered it) leaves "script a wavering
  friendly for D.12" as an owner design call. Tooling note: Git Bash mangles
  the leading `/Game/...` map arg into `C:/Program Files/Git/Game/...`
  (MSYS path conversion) -- run the demo boots with
  `MSYS2_ARG_CONV_EXCL="*"`, first boot logged a wrong-map no-op
  (m7_reclock_boot.log's first run was overwritten; the empty-EXEC evidence
  is in the session transcript, not the repo).

- **2026-11-01 (T6.1 prep, the morale threshold co-audit)** -- the morale half
  of T6.1 pulled forward on branch `t61-morale-threshold-audit` (PENDING owner
  sign-off): the PR-12 evidence block quantified (levy base 2 vs
  WaveringThreshold 4.0 -> spawn-wavering, one-casualty routing; the Low
  break offset +2.0 equaled the levy row's whole base) and the family
  re-placed against the M2TW anchors (break at zero absolute, wavering the
  band just above, the base `stat_mental` carrying the spread -- our
  CasualtyFraction weight -8 already reproduces the community casualty
  curve -2/-4/-6 at 25/50/75%). Proposed swap: Enable 0->1, Wavering 4.0->1.0,
  Rally 6.0->1.5, BreakOffset {+2,0,-2,0}->{+0.5,0,-0.5,0}, everything else
  kept. Verified with the candidates LIVE as defaults, no session flags:
  unit 84/84; func 24/27 = exactly the three known (PairLifecycle PASS --
  the PR-12 probe cleared; S0_SyncLatency PASS; OfficerDeath ratio 3.00
  golden; S7 order + cascade green). Full record + the M2TW source register:
  `Docs/phases/phase-4/t61-morale-threshold-audit.md`. Follow-ups recorded
  there: the Feudal_Knights `impetuous` deviation call, the nightly
  re-baseline, the demo beat 4/5 re-clock (+ the ResolveProbe retirement
  candidate), the spec section 4.12 table update riding the flip commit.

- **2026-10-31 (PR-12, the Enable flip is EVIDENCE-BLOCKED)** -- the task-6
  flip (`Battle.Morale.Enable` default 0 -> 1) regressed the PR gate on the
  probe: `PairLifecycle` failed with `MoraleRout Unit=1:1 State=Wavering
  Morale=2.00` at spawn and `State=Routing Morale=0.62` after casualties.
  Root cause: the EDU fixture's levy base morale (2) sits BELOW the
  placeholder WaveringThreshold (4.0), and the Low-discipline break offset
  (+2 on the 0.0 break threshold) puts levy one casualty from routing --
  two A16 placeholder families (the 4.12 thresholds vs the phase-0 fixture
  stat_mental values) never co-audited. Per ground rule 6's gate-proven
  intent the default stays 0; the cvar help text carries the finding; the
  T4 scenarios (PR-13) set the flag session-on so the gates are unaffected.
  The flip is a T6.1-audit follow-up with this evidence recorded.
- **2026-10-31 (PR-12, F4-6)** -- the 5.8 Mass scheduler SKIPS zero-query
  processors (`LogMass: Warning: MoraleTickProcessor_0 has no registered
  queries while being asked for matching archetypes`): the pass never ran
  until a nominal `FMassEntityQuery` (FOwnerUnitFragment, never iterated)
  was registered. The first Enable=1 probe was invalid before the fix; the
  re-run after it produced the flip evidence above.
- **2026-10-31 (PR-12, API)** -- `UAbilitySystemComponent::
  ExecutePeriodicEffect` is PROTECTED in 5.8.4 (the natural timer's delegate
  binds it from engine-internal friend context; no external caller exists).
  Amendment to the Task 3 audit (engine-api Part A section 16 row 9): the
  deterministic pin's forced execution goes through a project ASC subclass
  (`UBattleAbilitySystemComponent::ForceExecutePeriodicEffect` -- the same
  engine code path, no reimplementation). The subclass's vtable pulled
  `UGameplayTasksComponent` symbols: `GameplayTasks` added to the module
  dependencies (the MassCore reality-fix pattern).
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

## PR-12 -- the morale pipeline (brief Tasks 5-6 content; the writeup's ten items)

- [pr12.1] `BattleMoraleEffects.h/.cpp` (new): `UBattleMoraleTickEffect`
  (infinite, one execution definition), `UBattleMoraleExecCalc` (registry-fed
  inputs -> `EvaluateMorale` ONCE (A14) -> Override writes on Morale +
  Fatigue; the T4.8 staleness stamp is its one registry write, sim game
  seconds), `UBattleAbilitySystemComponent` (the protected-symbol forward),
  the six-class FatigueState catalog (additive `*Mod` writes at the golden
  table minus 1; Fresh never applies; the reload multiplier rides
  RangedAttackMod -- disclosed).
- [pr12.2] `MoraleTickProcessor.h/.cpp` (new): the game-thread pass, group
  Battle, after TallyProcessor / before RangedFireProcessor. Per-frame cost
  = one accumulator compare; ALL work at the tick boundary (decay pushes,
  exertion, the geometric folds over records/envelopes, the pin's forced
  executions, the mirror + FSM + rally in sorted-handle order, the
  auto-charge, the flee flag, the metrics). Nominal query for the 5.8
  scheduler (F4-6).
- [pr12.3] Registry: `MoraleInputsScratch` (EC9 per-battle entries),
  `FindMoraleInputs(Mutable)`, `StampMoraleTickTime`, the tick-cooldown
  accumulator, `ApplyUnitRoutDecision` (the RC7 generalization: flee Move
  away from the threat + serial guard + EXEC_RESULT lines; rally Stop),
  `GetUnitMoraleState`, `UnitHasLiveEngagement`, `DecayMoraleTeamShocks`;
  `FUnitRuntimeState` += DeathWindow/KillWindow/threat anchor/auto-charge
  bookkeeping (transient class); `NotifyMoraleCasualty` feeds the
  DeathWindow; `ResetBattleState` clears the scratch.
- [pr12.4] UnitActor: `EnsureMoraleTickApplied` (apply-on-first-sight; the
  spec's Period rides the pin: NO_PERIOD under DeterministicTick=1 -- no
  natural timer, the Task 3 verdict), `ForceMoraleTickExecution`,
  `ApplyFatigueBand` (the catalog crossing), `RemoveMoraleEffects`
  (EndPlay); the ASC is now the subclass.
- [pr12.5] UnitSync corrections (A8/A15/ADR-36, the R-M9 grep posture:
  zero consumers): `FatigueAttackMult/DefenseMult` -> identity 1.f (the
  catalog owns fatigue through the *Mod aggregation); `MoraleBonus` -> the
  aura sum (the input block's GeneralAuraBonus; the absolute level no
  longer leaks into a bonus). D-4: the AmmoPool GAS mirror moved from the
  fire pass to UnitSync (single mirror writer; the mirror runs for every
  chunk -- the fragment ammo sum changes without the snapshot dirty flag).
- [pr12.6] Officer placement (A5): `FPathFrameLayout::OfficerSlotPositionM`
  (front-center-forward, rank -1, fractional-file centering) +
  FormationSlotResolve's override (gated `Battle.Morale.Enable`, skipped
  while garrison-filing; officers excluded from the dressing-error metric).
  Servo: `bFleeMove` (formation fragment) stamps `EBattleMovementState::Flee`
  (a movement flag, never morale state -- A12).
- [pr12.7] ExecBridge: `Battle.Morale.Toggle <Unit> Guard <0|1>`,
  `Battle.Morale.Dump <Unit>` (state/values/staleness + the ten-term input
  breakdown), `Battle.Morale.SpawnRout` (the M5 fixture: general + two
  adjacent + enemy line, dictionary-keyed, morale session-on), and
  `EnableMoralePipeline` (the D-3 session-gate pattern). Metrics:
  MoraleMs/TickAgeMax/Routs/Rallies/Shattered/OfficerDeaths/AuraActive in
  ResetDiagnostics + BattleMetrics (APPENDED fields, the F14 rule);
  RecordOfficerDeath rides the death-path officer branch.
- [pr12.8] Evidence: build green (the only tree warnings are the
  pre-existing C4457 class); unit tier **83/83** (Enable=0; the flags-off
  identity); func tier **19/27 = exactly the expected 8** (four known +
  four skeletons; PairLifecycle back green). The Enable=1 probe (after
  F4-6's fix): the pipeline live end to end -- `MoraleTickApplied` +
  `MoraleRout` wavering/routing with `RoutFlee` orders in the func log --
  with PairLifecycle's morale-driven regression the recorded flip blocker
  (the disclosure above). SpawnRout's PIE/evidence run rides the next
  editor session (PR-14's session; the fixture is registered).
- [pr12.9] NOT done here (disclosed): the `Battle.Morale.Enable` -> 1 flip
  (evidence-blocked, see the discrepancy); the morale overlay rows
  (morale bars/aura rings/flee arrows) are PR-14's editor-session item with
  the counters in place; WinningLocally's kills half has no feed (the
  resolution pass runs worker-side -- KillWindow stays 0, disclosed).

## Task-1 hunt, session 3 (2026-11-02) -- the section-3 residue closed

- [1.S3-a] F-table re-verified on the working tree @ `c4ebf97` (all rows
  held; one plan-fact correction: `OutSectionHashes` received SIX
  cumulative-prefix hashes, not 10 -- the test's 10-name array mislabeled
  4-6). Probe landed (`Det2RunCensus` pre-run + both sampling sites) ->
  run isolated: model CONFIRMED, all signature rows hit
  (`det_census.log`: pre-run 4; run 1 = 4->6@t720->8@t1800; run 2 = const
  8; sections 1-2 match). -> evidence in the discrepancy entry above.
- [1.S3-c] Census landed (H-1 option A): `BattleStateHash.cpp` section 3
  folds the per-entity 64-bit presence bitset over the 20-type
  `CensusBits<Ts...>` list (Route A composition + trait tags + both death
  tags; one home = the template argument list); Shipping-safe, no debug
  dependency; the count stays a metric (the census line + DumpMetrics +
  the soak line). Header contract restored ("no non-sim state" now true).
  -> build green; compile-checked.
- [1.S3-b] Per-section bisector landed (H-2): each section accumulates its
  OWN byte slice (main stream byte-identical; 10 sections, names 1..10 now
  true); per-sample `Det2RunSec` telemetry added. -> the sample-0 cascade
  artifact gone (`det_census2.log`: sections 4/6 MATCH while neighbors
  DIFF -- independent signal at last).
- [1.S3-e] Guard test landed: `Battle.Unit.StateHash.CensusGuard`
  (liveness: an exec-path kill flips the census slice; history-independence:
  an extra throwaway kill between identical mini-battles leaves the census
  equal; monotone-count soft assertions documented in-file). -> unit tier
  **85/85** (`last_run/unit.log`, CensusGuard Success).
- [1.S3-d] The hatch executed (falsifier row 4 fired): instrumentation
  (`Battle.Det.RunTag`/`Battle.Det.Tick` + DetPair/DetRoll/DetRngRoll/
  DetMor, opt-in, canonical identities) -> kills #1-4 tick-identical
  (681/682/693/694); morale pass phases 1+21k vs 21k; the straddle at the
  t=693 kill -> run 1's unit 3 broke at its t=694 pass, run 2's didn't ->
  kill streams forked at t=700. ROOT CAUSE + FIX: the empty-world morale
  fire consumed/re-armed a period after the mid-frame reset -- now STAYS
  PRIMED (`MoraleTickProcessor.cpp`). -> **DeterminismTwoRun PASS 40/40
  mismatches=0** isolated (`det_fix1.log`) AND in-gate (`last_run/func.log`:
  PASS samples=40 firstDivergence=-1); func tier **25/27** = exactly
  S4_WidthOverride + S5_Mob (independent sessions, untouched); per-commit
  gate green (unit run exit 0; the func summarize's report-vs-parse note is
  the succeededWithWarnings bucket, counts agree at 25/27). Evidence logs:
  `det_census.log`, `det_census2.log`, `det_trace_s3*.log`, `det_fix1.log`
  (untracked, like the det_* set).
- [H-4] NOT executed (spec docs read-only without explicit authorization):
  the proposed B.4/B.5 wording patch is recorded in the plan doc's §9 for
  the owner.
- [gate] Jenkins `massbattle-gate` green: the host was DOWN at session
  start (the portable install, no at-logon entry) -- started per
  `Tools/CI/JENKINS.md`. Build #4 FAILED in 5 s at Checkout: the git
  plugin's local-directory guard rejects the job's `file:///` remote
  without `-Dhudson.plugins.git.GitSCM.ALLOW_LOCAL_CHECKOUT=true` (a
  pre-existing config gap; earlier boots predate the guard). Restarted
  with the property (the JENKINS.md start line corrected in place -- AGENTS
  §4), re-triggered: **build #5 SUCCESS** (checkout -> UBT editor build ->
  run_tests.bat unit, all green).
- Commits: the code rides two commits (the hash/bisector/guard/telemetry
  set, then the morale cadence fix that flips the gate green -- file
  coupling disclosed per AGENTS §3); the records ride the third.

## PR-13 -- the four morale scenario bodies (T4.3-T4.6)

- [pr13.1] Helpers beside the C-3 one-home ranged list: `MoraleSetCvars/
  MoraleRestoreCvars` (the 12-slot indexed list: Enable + DeterministicTick
  session-on, the tuned threshold/aura/rally subset), `KillRosterEntity`
  (a scripted kill through the death path: FDeadTag deferred + flushed in
  the test Tick -- the observer/receiver chain fires), `MoraleSpawnActor`
  (the officer/general authoring passthrough).
- [pr13.2] `S7_RoutCascade` (T4.3) -- TWO seeded runs. Run A: sergeant
  fixture (base 6, Disciplined) + the general at 28 m + the enemy at 30 m;
  scripted attrition onto the center; wavering(18) < break(50) order; the
  flee (anchor -40 m away from the threat, bRoutFleeing, the servo's Flee
  state); the cascade (the neighbor wavers with zero own casualties);
  the rally at tick 447 under the aura; the rear-arc chase (angle 112 in
  [96,192)). Run B (isolation): no general + CascadeRadiusM 0 -- the
  neighbor stays Steady, the center stays Routing. Tuning notes: the
  rally threshold 2.5 sits above the post-break morale (~1.1 fresh spike)
  so the flee window survives and below the recovered morale (~2.9); the
  neighbors clear the corridor at the chase (contact auto-pairing ambushed
  the marching pursuer -- measured); the run-B radius switch rides
  BeginRunB.
- [pr13.3] `OfficerDeath` (T4.4) -- three sub-runs at a FIXED tick
  alignment (kill at +12, sample at +42, two morale ticks of decay): the
  baseline soldier spike 0.841; the officer spike 2.523 = ratio 3.00
  (the x3 golden); OfficerDeaths=1; the general kill stamps the team
  shock 5.897 + `Battle.Team.GeneralDied` on the watcher's ASC (x3
  same-team events); the shock decays monotonically (5.796 -> 5.598).
- [pr13.4] `EffectEndToEnd` (T4.5) -- the drive (a far Move order; Tired at
  integral 45 = 300 morale ticks): the Tired goldens attack 0.800 /
  defense 0.850 / speed 0.900 with fragment latency 1 frame from the
  attribute write; the stacking aggregate 0.850 = 1 + (0.05 base write -
  0.2 catalog mod). Fix recorded: the overlap offsets the BASE
  (GetNumericAttributeBase), not the mod-inclusive current (the first
  probe double-counted the aggregator mod -- measured 0.65).
- [pr13.5] `ToggleBehaviors` (T4.6) -- GuardMode suppresses the auto-charge
  for the 10 s window (enemy inside the 30 m radius); unguarded charges at
  tick 10; the phase-3 rows: FireAtWill=0 holds fire (4 s), re-enabling
  resumes (0 -> 36 shots), the skirmish break fires at tick 1745 under the
  closing threat; the player Move cancels the auto-charge (the serial
  guard -- the pass's clear now runs REGARDLESS of the FSM state; the
  Steady-gated clear left stale bookkeeping -- measured). The subject's
  Impetuous discipline is stamped through the registry seam (the fixture
  has no impetuous EDU token -- disclosed).
- [pr13.6] Test-hygiene fix (measured): the cvar restore + fixture teardown
  moved into CompleteTest (the framework's Destroyed lands AFTER the next
  test starts -- the leaked Enable=1 broke S0_SyncLatency's dirty-flag
  window; restored to green). The morale scenarios now leave the world
  exactly as they found it.
- [pr13.7] Evidence: build green; unit tier 83/83 (unchanged); func tier
  **23/27 -- the four morale scenarios PASS, the failures are EXACTLY the
  four known** (DeterminismTwoRun / S4_ShieldWall / S4_WidthOverride /
  S5_Mob -- the Tasks 1-2 dossiers, untouched). Verbatim lines:
  `Tools/CI/pr13_evidence.log` (20 lines) + the full log in
  `Tools/CI/last_run/func.log`.

## PR-14 -- the M5 demo gray-box (code side; the PIE eyeball is owner-scoped)

- [pr14.1] The morale overlay rows (B.2, PR-12's deferred item):
  `MoraleTickProcessor::DrawMoraleOverlay` -- per-unit morale bars colored
  by rout state (green/yellow/red/dark-red, length ~ the morale value), the
  flee arrow toward the flee anchor (orange), the general's aura ring (blue,
  the radius cvar). Gated by `Battle.Debug.ShowOverlay` (the M9 cached-cvar
  posture); enqueued through the existing `EnqueueOverlayLine` framework.
- [pr14.2] The demo hookup: `ADemoGameMode::RegisterM3Sample` boots
  `BattleExec::MoraleSpawnRout` after the M4 sample (the fixture chain is
  sequential by construction -- each fixture resets the battle, so the M5
  field is the demo's live sample). The morale session flags ride
  SpawnRout's EnableMoralePipeline.
- [pr14.3] Headless boot evidence (`Tools/CI/m5_boot.log`, the m4_boot
  posture): the demo map loads in `-game -nullrhi`, the five re-authored
  level units spawn through the dictionary path (`DemoSpawn spawned=5 of 5`,
  the PR-11 line), the combat pipeline session-on, no crash across the
  window. The m4-era boot log (demo_ux_boot.log) has the IDENTICAL shape
  (zero sample-registration lines -- the samples register on the
  interactive path), so the M5 SpawnRout line + the screenshot ride the
  owner's PIE session (the writeup's editor-session item; no Unreal MCP
  session was available -- the PR-11 posture).

## PR-15 -- determinism (T4.7), staleness (T4.8), the SoakRout variant (code side)

- [pr15.1] The T4.8 staleness probe landed as an exec fixture:
  `Battle.Morale.StalenessProbe` (the T2.9/T3.6 loaded shape -- 2x500
  sergeants at contact, 1000 entities) with the morale pass on the
  SHIPPING natural period (Enable=1, DeterministicTick=0 -- under the pin
  the age is the tick cadence by construction, the T4.7 carve-out); the
  probe dumps `EXEC_RESULT MoraleStaleness TickAgeMax=%.3f Period=%.3f
  MoraleTicks=%d Verdict=PASS/OVER` at tick 2100 via the debug subsystem's
  `ArmMoraleStalenessProbe` (the T3.6 pattern; the bound is one period +
  the frame-quantization margin).
- [pr15.2] The SoakRout soak variant: `Battle.MovementSoakController`'s
  spawn mix gains the ARMY GENERAL (unit 0: bIsGeneral + one officer) and
  the IMPETUOUS row (unit 1, the discipline stamped through the registry
  seam); the morale session flags (Enable=1 + DeterministicTick=1 -- the
  soak's determinism) ride the mix flag. The FINAL line APPENDS
  `routs=%d rallies=%d shattered=%d officerDeaths=%d moraleTicks=%d` (the
  F14 rule; flag off prints zeros -- the parsed positions do not move).
  `MoraleTicks` joins the debug counters (RecordMoraleTick per pass tick).
- [pr15.3] The T4.7 two-run extension: NOT landed this session -- the gate
  it extends (`DeterminismTwoRun`, the mixed clash+volley two-run) is the
  Task 1 blocker itself (still failing; the hunt has not run -- the run's
  task order picks up at Tasks 1-2). Per the PR-15 writeup's own
  disposition row: the extension's morale-term delta is provable only on a
  green base or the melee-only worlds; landing the extension now would add
  a red-on-red gate with no isolatable signal. The extension's shape (the
  general + impetuous row + the scripted break/rally + the pin) is
  specified in the writeup; it lands with Task 1's disposition.
- [pr15.4] Machine evidence this session: build green; unit tier 83/83;
  func tier 23/27 = exactly the four known (the PR-15 code is additive --
  no regressions). The T4.8 verdict line + the SoakRout FINAL line need
  their harness hookup (the probe as a PR-gate fixture) + one staged
  nightly -- Task 8's remainder, riding Task 2's unblock.

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
