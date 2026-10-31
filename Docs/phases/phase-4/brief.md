# MassBattle Phase 4a Brief -- Morale & GAS (The Soul of the Battle)

| | |
|---|---|
| **Companion docs** | `Docs/spec/morale-and-gas.md` (design spec: section 0 MC-table behavior contract, section 3 data model, section 4 mechanics, section 6 touch-point table, section 12-equivalent tunables at 4.12) · `Docs/AGENTS.md` (standing protocol) · `Docs/spec/roadmap-and-testing.md` (Part A Phase 4, T4 gate table, B.0 test contract, B.8 capability DoD) |
| **Engine pin** | UE 5.8.4 (ADR-13); NEW ENGINE API TERRITORY this phase (the GAS effect surface -- brief section 4 carries the digest + audit plan; the task-0 symbol audit is Task 3) |
| **Run mode** | Single autonomous run, protocol per `Docs/AGENTS.md`; all CI evidence via `Tools/CI/run_tests.bat {unit,func}`; nightly via `Tools/CI/nightly.bat` |
| **Priority directive** | The morale core: the contribution stack behind the periodic MoraleTick GE (E3, owner 2026-10-31), the fatigue model + FatigueState effect catalog, the rout state machine (wavering -> breaking -> rout -> shattered) with flee/rally through the existing order surface, the panic cascade, officer + general death events riding the existing signals, the GeneralAura, GuardMode + impetuous auto-charge. OrderLatency + TerrorAura + climate fatigue are 4b (spec section 1); representation is Phase 5 |
| **Owner-approval status** | Section 3.1 decisions are PROPOSALS -- the owner marks them approved before this brief freezes; spec section 8 ADR-32..37 are proposals pending the owner's architecture-log commit |
| **Sequencing** | Executes after the phase-3 audit landing + machine verification (2026-10-30, HEAD `4357321`). The 4a/4b split is the owner decision of 2026-10-31 (spec section 9); this brief is the 4a run. Rationale: spec section 9 |
| **Blockers inherited** | TWO open phase-3 blockers are this run's Tasks 1-2, FIRST (the phase-3 report section 4's own ordering): (a) the mixed clash+volley two-run mm-scale divergence (servo DesiredVel accumulation, ticks 11-14, entities 8/11; `Tools/CI/det_trace8.log` has the trace shape); (b) the pre-existing staged movement-soak dress-gate failure (`dressErr=30.25/6.0m`, phase-2 dossier territory, blocks the nightly's baseline-commit step). Task 0 lands first (kickoff verification); Tasks 1-2 precede all morale work |
| **Kickoff state** | The pure-math kickoff patches (`Patches/0024..0029`, branch `phase4/kickoff`) already deliver: this spec + brief + the README map/status; the gate skeleton (the four morale enums, the `Battle.Morale.*` CVAR block, `FUnitRuntimeState` morale fields, `FBaseStatsFragment.bOfficer` + the dead `FOfficerTag` deleted, `FUnitSpawnSpec`/`AUnitActor` OfficerCount + bIsGeneral, save v3 fields + capture/restore, the four fail-by-design scenario stubs); the EDU data spine (the `officer` key with the MaxOccurrences-3 schema rule, `stat_mental` discipline/training consumption, the fixture re-sign, FromEdu + spawn stamping + registry bookkeeping); the full `BattleMorale` seam (stack, decay forms, fatigue model + multiplier table, rout FSM, rally) unit-locked by T4.1/T4.2 + the RoutRally suite; the state spine (hash block 10 + the HashTerms/SaveRoundTrip extensions). Tasks 0-4 below VERIFY and BUILD ON them (apply first; re-run the unit tier; re-sign what reality corrects) |

---

## 0. Mission + read order + token discipline

**Mission.** Implement the morale core so S7 passes and the exit criteria read: a routing cascade is observable across adjacent units; general death produces measurable morale shock and rally behavior; a mixed battle including morale and effects is two-run deterministic; the fatigue effect loop is entity-visible within one frame. The full behavior contract is the spec's section 0 MC-table (MC1-MC10); the architecture claim is one sentence: **morale is a periodic sim-fed evaluation writing GAS attributes through one gameplay effect, and routing is a registry state machine that flees through the existing order surface -- a consumer of the Phase 0-3 spine, not a new system.**

**Read order** (token discipline: nothing else; do not read the spec docs end-to-end):
1. `Docs/README.md` status block, then `Docs/AGENTS.md`.
2. This brief, all sections.
3. `Docs/spec/morale-and-gas.md` -- section 2 (architecture summary), section 3 (data model), section 4 (mechanics), section 6 (touch-point table) are the implementation contract; section 0 MC-table is the acceptance contract.
4. Roadmap Part A "Phase 4 -- Morale & full GAS" + the T4 gate table; B.0 (test-mode contract), B.5 (determinism hooks), B.8 (capability DoD) -- cite, do not restate.
5. `Docs/spec/architecture.md` sections 10.1-10.5 and 5 ONLY as the parent design (conflicts: architecture wins, report per AGENTS section 2.1).
6. `Docs/phases/phase-3/report.md` section 4 ("Where Phase 4 should start") -- the blocker dossiers + ready hooks; `Docs/phases/phase-3/audit.md` section 0-1 if a ranged detail surprises you (the seam/constants discipline carry the why).
7. `Docs/reference/engine-api-5.8.md` -- Part B sections 2-3 (processor + command-buffer facts) ONLY, until Task 3 extends it with the GAS digest.
8. `Docs/spec/ranged-stack.md` section 4.8 ONLY (the RC7 order-serial guard this run generalizes) + section 3.2 (the registry toggle-field pattern).

**Do not** open `Docs/reference/engine-api-5.8.md` or `Docs/reference/mcp-playbook.md` end-to-end. If an engine symbol misbehaves, follow AGENTS section 4 (correct the owning reference, log it).

## 1. Phase facts digest (pre-verified 2026-10-31 against the repo at HEAD `4357321`)

Every fact below is verified against the cited source. Cite fact IDs in the progress log instead of re-reading.

- **F1** The GAS attribute surface exists and is closed: `UUnitAttributeSet` (Public/UnitActor.h) carries `Morale`, `Fatigue`, `Experience`, `ArmourUpgrade`, the five `*Mod` targets, `OrderLatency`, `AmmoPool` with `ATTRIBUTE_ACCESSORS_BASIC` accessors; `GetAllAttributes`/`FindAttributeByName` exist for completeness assertions. Attribute-change delegates are bound in `SpawnEntities` (NOT PostInitializeComponents -- on-machine lesson, the header comment); the handler writes the registry snapshot + dirty flag. Zero new attributes or shared fragments this phase (spec section 3.2).
- **F2** The GAS up-path is proven: entity death -> `FDeadTag` defer -> `UBattleDeathObserver` -> `Battle.UnitDeath` signal -> `UBattleDeathSignalReceiver::SignalEntities` (game thread) calls `HandleUnitDeath(Unit, Entity, RosterIndex)` THEN raises `Battle.Unit.Died` on the unit actor's ASC then defers destroy. The receiver ALREADY receives the roster index (Public/BattleDeathPath.h) -- officer-ness resolves from registry bookkeeping against it, no new query. The receiver's kill-flush ordering (FDeadSignaledTag idempotence) is untouched by the officer branch.
- **F3** The RC7 auto-order pattern to generalize: `UUnitRegistrySubsystem::ApplyUnitSkirmishDecision` (Public/BattleRegistry.h) -- reads transient retreat state, honors the order-serial guard (a player bump cancels; auto only replaces idle or ITS OWN retreat), issues the retreat Move away from the threat (facing preserved), logs EXEC_RESULT lines. The rout apply (spec 4.4) is this shape + `FlagUnitEngagementsDead` + a Stop on rally. `bRetreating`/`AutoRetreatSerial`/`RetreatingFireTarget` (BattleTypes.h FUnitRuntimeState) are the transient-bookkeeping precedent; rout state rides the same transient posture (spec 3.5).
- **F4** The engagement geometry source: `EngagementClustersProcessor` at 4 Hz maintains unit envelopes (`FUnitClusterNode`) + engagement records (`FEngagementRecord`, DesiredPressure/AssignedAttackers) + the tally/coverage scratch; `GetCombatClusterCooldown()` accumulates the cadence. The morale geometric inputs (spec 4.6) read THIS geometry (records, envelopes, pair contact angles via `FCombatSolver::SectorIndexFromAngle`) -- never per-entity scans (the F12-class discipline).
- **F5** The EDU parser schema is a 15-key arity table (Private/BattleEduParser.cpp GetSchema); `stat_mental` is arity-3 {EduInt, EduToken, EduToken} and the fixture rows already carry the discipline/training tokens (`low, untrained` / `normal, trained`) -- only FromEdu's mapping is new. Duplicate keys are REJECTED (canonicalization rule 7) -- the `officer` key's MaxOccurrences-3 amendment (spec 3.3) is the disclosed exception, schema 15 -> 16. `FBattleRowStats::FromEdu` maps loud-fail on unknown tokens (the B-3 posture; the ranged missile-branch precedent).
- **F6** State-hash layout (Private/BattleStateHash.cpp): sections 1-9 in order (bookkeeping, per-entity HP/transform, archetype count, shared stats, formation+garrison, per-entity pair/charge/mount/ranged, pool). The morale block appends as section 10 AFTER the pool block, per sorted handle, fixed term count, zero-fill on absent state (spec 3.4) -- existing section indices stay stable. `Quantize2` = 1e-2; the HashTerms test's mutation/revert discipline is the coverage pattern.
- **F7** Save schema v2 (Private/BattleRegistry.cpp CaptureBattleState/RestoreBattleState): restore accepts 1 and 2 with `bV2` branching; the round-trip asserts bookkeeping-hash equality + per-field reads (`Battle.Unit.Combat.SaveRoundTrip`, Private/MassBattleCombatTests.cpp:386). v3 follows the same shape: fields append to FUnitSaveEntry, SchemaVersion 3, restore accepts 2 (legacy defaults) and 3.
- **F8** UnitSync's derivation block (Private/UnitSyncProcessor.cpp:81-97) holds the two placeholder couplings this run CORRECTS (spec 3.2, ADR-36): `Stats.MoraleBonus = Snapshot->Morale` (the absolute level fed in as a bonus -- no consumer, behavior-neutral to fix) and `FatigueAttackMult/DefenseMult = 1 + Snapshot->Fatigue * 0.01f` (the placeholder the catalog replaces with its own `*Mod` writes). The dirty-flag path (MarkStatsDirty -> IsStatsDirty -> ClearStatsDirty) is the 1-frame latency channel T4.5 asserts through.
- **F9** `FOfficerTag` (Public/BattleFragments.h:112) has ZERO usages outside its declaration (verified by grep at HEAD) -- the deletion (spec 3.3, ADR-35) is value-neutral by construction. Officer identity = `FBaseStatsFragment.bOfficer` on the LAST OfficerCount roster entities (spawn batch order is roster order; `SetRoster` publishes it; `InitialRosterSize` already stamps the denominator the casualty fraction reads).
- **F10** `EBattleMovementState::Flee` already exists (Public/BattleMovementFragments.h:27, "routing (Phase 4+)") -- the servo path's flee profile was designed for this phase; ZERO new movement states.
- **F11** Test contract (roadmap B.0): `UBattleTestSettings::ApplyTestMode()/AssertTestMode()` at every S-scenario start; naming `Battle.Unit.Morale.*` (per commit) + `Battle.Func.*` (per PR); scenarios on `L_MassBattle_T0_Functional`; the factory registry (`FBattleFuncTestRegistry::FRegistrar`) beside each actor; fixed 1/60 tick phase machines (never wall-clock); every completion logs an EXEC_RESULT line. The fail-by-design skeleton shape is the phase-3 task-0 pattern (Patches/0011: ctor sets TimeLimit/TimesUpResult; StartTest completes Error with the "skeleton:" message; Tick empty).
- **F12** CVAR block pattern: file-local `TAutoConsoleVariable` + namespace accessors (Private/BattleRanged.cpp the reference; worker-read contract `GetValueOnAnyThread()`). `Battle.Morale.Enable` defaults 0 until Task 6 lands end-to-end (B.8 item 6), then 1.
- **F13** Deterministic draws: `BattleCanonicalEntityKey/Count` (Public/BattleTypes.h) + the `CombatRoll01` hash family (Public/BattleMelee.h) -- rally rolls draw `CombatRoll01(UnitIndex, UnitSerial, RallyAttemptCount)`; the A13 rule (run-invariant inputs, never GFrameCounter, never wall-clock) binds every morale-side roll.
- **F14** Unit conventions: fragment boundary = centimeters, seam math = meters; ASCII + LF, `--` not em-dashes; commits prefix `phase4a:`; game-thread processors: `bRequiresGameThreadExecution = true` + `EProcessorExecutionFlags::AllWorldModes`, `ExecuteInGroup = "Battle"` (engine-api Part B sections 2-3; the morale pass is game-thread BY DESIGN -- spec section 2, the ADR-28 evidence class).
- **F15** Debug subsystem: `EnqueueOverlayLine(FBattleOverlayLine{From, To, Color, Shape, Radius})` thread-safe; metrics via `Record*` + `Battle.DumpMetrics`; `ResetDiagnostics` coverage. B.2's morale rows (morale bars by rout state, aura rings, flee arrows) extend this framework (Task 9).
- **F16** The demo/authoring surface: `AUnitActor` exposes `UnitDictionary` (PR-11) + `TeamId`; `OnConstruction` syncs the demo actor's `Team` -> `TeamId` (audit A-1's fix). `OfficerCount`/`bIsGeneral` follow the same authoring pattern (plain EditAnywhere passthrough, no OnConstruction coupling needed).
- **F17** The 4 known func-tier failures at HEAD (the PR sweep's record): `DeterminismTwoRun` (blocker 1), `S4_ShieldWall`, `S4_WidthOverride`, `S5_Mob` (phase-2 dossier melts). Task 0's expected func tier: those four + the four new skeletons = 8 red, counted and listed; NO new failures beyond them.
- **F18** Perf/baseline machinery: `Battle.Ranged.PerfStress` fixture precedent -> the T4.8 staleness probe rides the same BattleMetrics channel (`MoraleMs`, `TickAgeMax`); the soak variant rides `BattleMovementSoakController` as a CVAR-read flag (`Battle.Combat.SoakClash`/`Battle.Ranged.SoakVolley` precedents; FINAL line fields APPEND, never reorder -- the F14 positional rule); `entity_spawn_80_us` re-baseline check if the bOfficer field moves the number (record, don't gate -- the T2.9 pattern).

## 2. Pointers

MCP/editor automation: operating rules live in `Docs/reference/mcp-playbook.md` (check its section 8 correction ledger first). This phase is planned MCP-light: fixtures are C++/ExecBridge-authored (the dictionary-keyed pattern), all evidence via `run_tests.bat`; the only editor-side items are the M5 demo gray-box sample and the PIE eyeballs the PRs carry (PR-13's map placements). The blocker hunts (Tasks 1-2) are headless/log-driven.

## 3. Phase ground rules + owner decisions + risk watchlist

### 3.1 Owner-decision table (PROPOSALS -- owner marks approved before the run; implement as written, never re-litigate)

| ID | Decision | Choice |
|----|----------|--------|
| A1 | Seam module | ONE B.0 seam `BattleMorale.h/cpp` (kicked off in the patches): the input block, `EvaluateMorale`, the decay forms, the fatigue model + multiplier table, the rout FSM, the rally roll, `FMoraleCVars` + `FMoraleWeights`/`FFatigueConstants`. Free functions, plain structs, deterministic, seed-derived RNG only (F13). Processors/actors stay thin shims |
| A2 | Tick mechanism | The periodic infinite-duration MoraleTick GE (E3 preferred, owner 2026-10-31): `UBattleMoraleTickEffect` per unit actor + ONE exec calc calling the seam (spec 4.8). SetByCaller magnitudes for effect-owned terms (re-applied on aggregation events); the registry input block for continuously-varying terms (the disclosed deviation). `Battle.Morale.DeterministicTick=1` pins the cadence for gates (the T4.7 carve-out); shipping runs the natural period |
| A3 | Data spine | UNIT-LEVEL ONLY: zero per-entity fragments, zero new GAS attributes (F1); morale/fatigue/rout state in `FUnitRuntimeState` (spec 3.1); per-team general records registry-side. Officer-ness is the `bOfficer` fragment VALUE on the last-N roster entities; `FOfficerTag` deleted (F9) |
| A4 | EDU spine | `officer` key (arity 1, EduName, MaxOccurrences 3 -- the duplicate-rule amendment); `stat_mental[1..2]` mapped loud on unknown tokens; fixture re-sign 15 -> 16 keys + officer lines (0/2/1/1); `SpawnComposition` count UNCHANGED (officers add no compositions -- the fragment-value ruling) |
| A5 | Officer identity | Last OfficerCount roster entities, `bOfficer` stamped at fixup; registry stores `OfficerRosterBase` for the O(1) death-path resolve; placement (front-center-forward layout slots) rides PR-12 with the layout seam -- identity/events/bookkeeping land with the spine |
| A6 | General identity | `bIsGeneral` on `FUnitSpawnSpec` + `AUnitActor` (the TeamId authoring precedent); the general ENTITY is officer slot 0 of that unit; registry per-team `FGeneralRecord` (unit + entity handles) drives the aura input + death shock (spec 4.7) |
| A7 | Rout FSM application | The RC7 generalization: flee = auto-flagged Move directly away from the nearest threat (facing away, run speed x fatigue multiplier), rally = auto-flagged Stop; both directions `FlagUnitEngagementsDead` (mass release); player order-serial bump cancels OUR flee (re-evaluates next tick); `RoutState`/`RoutCount` persist, the flee bookkeeping is transient (F3, spec 4.4) |
| A8 | Effect catalog | Six `FatigueState` GE instances writing ONLY the five `*Mod` attributes (ADR-09), applied/removed on band crossings (aggregation events, never timers); UnitSync's two placeholder couplings revert to the plain `1 + Mod` form (F8, ADR-36 -- the correction is disclosed in the report); stacking order = GAS modifier aggregation (T4.5 asserts with an overlapping effect) |
| A9 | Save schema | v2 -> v3: rout state/count, exertion, both values, spike, guard toggle, officer count, bIsGeneral; restore accepts 2 (defaults) and 3; transient: tick stamp, flee bookkeeping, team general records (rebuild from specs) -- spec 3.5 |
| A10 | State hash | Block 10 after the pool block, per sorted handle, fixed terms, zero-fill (F6, spec 3.4); the tick stamp excluded (pipeline state, the dirty-flag precedent) |
| A11 | Order surface | No new order ETypes. `SetUnitGuardToggle` + ExecBridge `Battle.Morale.Toggle/Dump/SpawnRout`; impetuous auto-charge = registry self-issued AttackUnit at the cluster cadence (auto-flagged, serial-guarded, suppressed by GuardMode/player orders/routing -- MC9) |
| A12 | Movement untouched | `ServoProcessor`/`UnitLocomotion` gain ZERO morale consumption (a rout flee is a normal move order; F10). `BuildSpatialHashProcessor` untouched |
| A13 | Determinism | Rally rolls via `CombatRoll01` over canonical unit keys (F13); FSM transitions in sorted-handle order; the morale pass reads inputs at fixed tick boundaries; T4.7 runs under `DeterministicTick=1`; the mixed two-run gains morale+rout+effects terms (block 10) |
| A14 | Authority | The registry owns the FSM + the sim mirror (`LastMoraleValue/LastFatigueValue` are hash/save terms); the GAS `Morale`/`Fatigue` attributes are written ONLY by the exec calc (A2); the snapshot path (F1) is the read-back. One evaluation per tick, inside the exec calc -- the seam function is never evaluated twice for the same instant |
| A15 | Mirror ownership | D-4 resolves: UnitSync becomes the single GAS-mirror writer for aggregate attributes (the AmmoPool mirror moves from the fire pass to UnitSync; the morale pass never writes mirrors directly -- spec 3.2) |
| A16 | Constants posture | `FMoraleWeights`/`FFatigueConstants` golden-locked placeholders honoring the community model's structure (ADR-30 continued); the cvar block carries the runtime-tunable subset (spec 4.12); T6.1 owns the swap |

### 3.2 Phase ground rules (additions to AGENTS.md only)

1. All morale math lives in the seam (A1) -- B.0's review flag applies unchanged.
2. The exec-calc never reads wall-clock, never iterates entities, never issues orders (A2/A14); the morale pass never evaluates the stack (the exec calc owns evaluation; the pass pushes inputs + applies decisions).
3. Game-thread morale work never allocates per tick in the steady state: the input blocks are registry-resident; scratch rides the EC9 discipline.
4. Morale CVARs default OFF until their owning task lands (B.8 item 6); flags-off = byte-identical baseline, the unchanged T1.8/T2/SoakVolley soaks prove it (the R-R8 pattern).
5. Spec doc sections are read-only (AGENTS section 7): proposed patches only where this brief authorizes them (the roadmap's Phase-4 split annotation ships with patch 0024; the B.7 entry lands with Task 9); disclosed in the report.
6. `Battle.Morale.Enable` flips to default 1 only when Task 6's FSM lands end-to-end (the impact-resolution precedent).
7. The kickoff patches are PRE-APPLIED at run start (apply `Patches/0024..0029` via `git am` if the owner has not merged them; re-run the unit tier FIRST and record the result -- any patch-compile failure is a Task 0 blocker, logged with full error text).
8. The blocker hunts (Tasks 1-2) precede all morale implementation; if a hunt exceeds three distinct diagnostic approaches without a root cause, write the dossier, record the owner-escalation, and proceed to Task 3 (the failure policy, scoped).

### 3.3 Risk watchlist

| ID | Risk | Mitigation / watch |
|----|------|--------------------|
| R-M1 | GE period rides wall-clock; two-run determinism breaks on sampling phase | The `DeterministicTick` pin (A2) forces the period execution at the sim cadence in every gated scenario; T4.7 asserts under the pin; the natural path is T4.8's staleness-bounded shipping mode |
| R-M2 | The force-execution symbol does not exist / behaves differently in 5.8.4 | Task 3 audits it against pinned source BEFORE Task 5 builds on it; fallback ladder (spec 4.8): remove/re-apply at cadence -> gate carve-out with the natural period + bounded staleness. The exec-calc purity (A14) makes any fallback value-equivalent |
| R-M3 | Exec-calc attribute writes bypass the delegate path (no snapshot update) | T4.5 asserts the FULL loop (GE -> attribute -> delegate -> snapshot -> UnitSync -> fragment <= 1 frame); the S0_SyncLatency scenario is the existing precedent for the assertion shape |
| R-M4 | Panic cascade feedback loop (units rout BECAUSE neighbors rout, forever) | The cascade term is bounded (RoutingFriendsNearby x weight, spec 4.6); rally requires morale recovery (the term stack recovers as fights end); S7's cascade assertion includes the isolation sub-run; the `SoakRout` nightly watches rout fraction over 10 min |
| R-M5 | Rout flee thrash (break -> rally -> break oscillation) | Wavering hysteresis (spec 4.4) + rally-chance decay per RoutCount + the Shattered lockout; settle assertions in S7; the FSM evaluates at tick cadence, not per frame |
| R-M6 | The officer schema amendment breaks the canonical round-trip (officer lines reorder) | The canonicalizer emits officer lines in original relative order, slotted after soldier (spec 3.3); `Battle.Data.EduRoundTrip` re-signs with the 16-key goldens in the kickoff patch; Task 4 ratifies on the machine |
| R-M7 | Morale pass cost at 2,400-entity scale (per-unit folds over rosters for arc exposure) | The fold is per-UNIT over ITS roster's pair fragments at the 4 Hz cadence (bounded by engaged pairs, the tally shape); `MoraleMs` metric + the perf fixture's budget line; the input block is registry-resident (no per-tick allocation) |
| R-M8 | Fatigue catalog churn (band edges flapping) | Band crossings are one-way per tick with the integral monotone (no rest this phase -- spec 4.3); the apply/remove pair rides aggregation events; a hysteresis margin is the T6.1 tuning knob if the soak shows flapping |
| R-M9 | The MoraleBonus/Fatigue placeholder corrections regress a consumer nobody knows about | Both couplings verified consumer-free at HEAD (F8/F9's grep posture); the corrections are disclosed in the report with the grep evidence; func tier full run is the regression net |
| R-M10 | Blocker 1's root cause is morale-adjacent (the servo transient resurfaces inside rout flees) | The flee is a normal move order (A7/A12) -- if the transient reproduces on flee paths, the Task 1 dossier extends with the new reproduction; the hunt's hex-float tooling carries over unchanged |

## 4. API digest -- NEW ENGINE TERRITORY (the GAS effect surface)

Phase 4 is the first consumer of the gameplay-effect side of GAS (Phase 0 stubbed the attribute set only). Symbols the spec names, to audit against pinned 5.8.4 source in Task 3 (the I6 pattern; results merge into `Docs/reference/engine-api-5.8.md` Part A/B per AGENTS section 4):

- `UGameplayEffect` C++ authoring: `DurationPolicy = EGameplayEffectDurationType::Infinite`, `Period`, the execution-definition array carrying the exec-calc class; the GE-subclass constructor as the authoring surface (no .uassets -- the project's C++-authored posture, A5 precedent).
- `UGameplayEffectExecutionCalculation::Execute` + `FGameplayEffectAttributeCaptureDefinition` (Source/Target capture, `bSnapshot=false` for target-attribute writes) + `OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData)`.
- Application: `UAbilitySystemComponent::MakeOutgoingGameplayEffectSpec` + `ApplyGameplayEffectSpecToSelf` (vs `ApplyGameplayEffectToSelf`); `FActiveGameplayEffectHandle` lifecycle; `RemoveActiveGameplayEffect`.
- SetByCaller: `FGameplayEffectSpec::SetSetByCallerMagnitude(FGameplayTag, float)` -- the data-name tags are native (`BattleGameplayTags.h`, the H1 lesson: native registration or -game/packaged builds lose them).
- **Period forcing (the R-M2 item)**: the exact 5.8.4 symbol that forces a periodic GE's execution (`UAbilitySystemComponent::ExecutePeriodicEffect`-class API) -- audit its existence, thread requirements, and whether the natural timer double-fires after a forced call; the fallback ladder is spec 4.8.
- `FActiveGameplayEffect` immutability (the SetByCaller refresh path: remove + re-apply, never spec mutation).

Plan: Task 3 runs the scripted audit over every symbol above (pinned source at `E:\Unreal\UE_5.8`), records verdicts C-style in the reference file, and files corrections per AGENTS section 4. Task 5 consumes the verdicts; if a verdict kills the A2 shape, the fallback ladder applies and the deviation is reported (never silently re-designed).

## 5. Task plan

Gate-first discipline: each gate test lands in its owning task's commit (or the prior one) -- verified via git log at the DoD. The kickoff patches already landed the seam and the unit-tier tests (T4.1/T4.2 + RoutRally); the run's tasks verify them on the UE machine, then build the pipeline.

### Task 0 -- Kickoff patch application + verification

**Goal.** The phase is CI-visible and the seams compile green before any new work; the inherited state is measured.

**Steps.** (1) Apply `Patches/0024..0029` (`git am`) if not already merged by the owner; resolve any conflict by hand and log it. (2) Run the unit tier: expected GREEN including `Battle.Unit.Morale.{Stack,Fatigue,RoutRally}`, the re-signed `Battle.Data.Edu*` (16-key), `Battle.Unit.Combat.SaveRoundTrip` (schema 3 asserts), `Battle.Unit.Combat.HashTerms` (block 10), and `Battle.Unit.Combat.SpawnComposition` (count UNCHANGED -- officers add no compositions). (3) Run the func tier: expected RED on exactly the four new skeletons (`Battle.Func.S7_RoutCascade`, `Battle.Func.OfficerDeath`, `Battle.Func.EffectEndToEnd`, `Battle.Func.ToggleBehaviors`) + the four known (F17) -- count them, list them in the progress log. (4) Create `Docs/phases/phase-4/progress.md` with the session header. (5) Record the current `entity_spawn_80_us` reading with the bOfficer field present (the re-baseline check, F18).

**Acceptance check.** Unit tier green with the re-signed counts recorded verbatim; func tier 8 red, all named, no surprises; archetype count 8 stable across spawns (progress log line).
**Commit.** `phase4a: task 0 kickoff verified (patches applied, unit tier green)`

### Task 1 -- Blocker 1: the mixed two-run mm-scale divergence

**Goal.** Root-cause or bound the servo transient that breaks the mixed clash+volley two-run equality.

**Steps.** (1) Reproduce: two 5-minute mixed runs, hash every 60 ticks; confirm the divergence signature (one unit, entities 8/11, tick-14 onset; `Tools/CI/det_*.log` dossiers). (2) The next diagnostic step per the phase-3 report: hex-float tracing of the servo's `DesiredVel` accumulation at ticks 11-14 for the diverging entities (`det_trace8.log` holds the trace shape to extend). (3) Diagnose against the five landed A13 fixes (the residual seed is below the 1e-6 m trace resolution -- look for a non-canonical input the fixes missed: order-dependent accumulation, a float contraction the compiler flags, a hash-term quantization edge). (4) Fix with a canonical-input patch (byte-value-neutral or disclosed) OR write the bound dossier + owner escalation (ground rule 8). (5) Either way: the melee-only empty-world runs stay green (the standing invariant).

**Acceptance check.** `DeterminismTwoRun` mixed: PASS with mismatches=0, or the dossier extended with the trace evidence + the escalation recorded; melee-only runs green.
**Commit.** `phase4a: task 1 determinism blocker (fix or dossier)`

### Task 2 -- Blocker 2: the staged movement-soak dress gate

**Goal.** The nightly's baseline-commit step unblocked (or the failure owner-escalated with a full attribution).

**Steps.** (1) Reproduce on the staged path: `dressErr` vs the 6 m bound with phase-2 movement flags on. (2) The phase-2 dossier territory: re-run the attribution ladder (task-7 changes on/off; the 2026-10-26 first-appearance commit bisect). (3) Root-cause the dress error's growth (30.25 m against the 6 m bound); candidate classes: the settle-basin cap interaction with the staged build's frame pacing, or a layout-seam regression at the soak's spawn mix. (4) Fix OR record the owner escalation with the evidence file (`Tools/CI/` log). (5) One clean staged nightly run as evidence (the soak-variant staged evidence rides this unblock).

**Acceptance check.** Staged nightly: `dressErr` within bound + baseline committed, or the escalation dossier filed; the SoakVolley variant's staged evidence recorded.
**Commit.** `phase4a: task 2 staged soak dress gate (fix or escalation)`

### Task 3 -- GAS API audit (the section 4 digest, executed)

**Goal.** Every spec-named GAS symbol has a pinned-source verdict before the pipeline builds on it.

**Steps.** (1) The scripted audit over the section 4 symbol list (the I6 pattern; pinned source at `E:\Unreal\UE_5.8`). (2) Verdicts recorded per symbol: exists / renamed / absent + the exact signature + thread requirements. (3) The R-M2 item first: the period-forcing symbol + its double-fire semantics. (4) Merge the digest into `Docs/reference/engine-api-5.8.md` (Part A additions + a Part B GAS section), corrections per AGENTS section 4. (5) If a verdict kills the A2 shape: apply the spec 4.8 fallback ladder, disclose in the progress log + report.

**Acceptance check.** The reference file carries the GAS digest with per-symbol evidence (file:line citations); the A2 shape is go/no-go with the fallback decision recorded.
**Commit.** `phase4a: task 3 GAS API audit merged`

### Task 4 -- Data spine on the UE machine (EDU + registry + save v3 + hash block live)

**Goal.** The 16-key EDU, officer stamping, registry morale fields, save v3, and hash block 10 are live and ratified.

**Steps.** (1) Verify beyond the tier: `Battle.SpawnRoster` on an officer row stamps `bOfficer` on the last-N entities + `OfficerRosterBase` in the registry; a `bIsGeneral` spawn registers the team record. (2) `SetUnitGuardToggle` reject/accept lines; `GetUnitMoraleState` reads defaults. (3) Save v3: capture/restore carries rout state/values/guard; legacy v2 takes defaults. (4) Hash block 10: mutation/revert sensitivity on each term (the HashTerms extension ratifies on the machine); two-run sanity with a morale fixture. (5) The FromEdu loud-fail: an unknown discipline token rejects with the EXEC_RESULT line.

**Acceptance check.** Unit tier green (re-signed goldens + SaveRoundTrip v3 + HashTerms block 10); EXEC_RESULT evidence for stamping, toggle reject, loud-fail; archetype count 8 stable.
**Commit.** `phase4a: task 4 data spine verified + save v3 + hash block 10`

### Task 5 -- The GAS half: MoraleTick GE + exec calc + fatigue catalog + UnitSync corrections

**Goal.** T4.5's loop exists: GE -> attributes -> snapshot -> UnitSync -> shared fragment, one frame, stacking honored.

**Steps.** (1) `UBattleMoraleTickEffect` + `UBattleMoraleExecCalc` per the Task 3 verdicts (A2): applied at battle start per unit actor, SetByCaller tags native-registered, the input block read from the registry. (2) The six FatigueState GE instances (A8) writing only the `*Mod` attributes. (3) UnitSync: the MoraleBonus correction + the Fatigue derivation reversion + the D-4 AmmoPool mirror move (A15) -- each disclosed with the consumer-free grep evidence (R-M9). (4) The deterministic pin (A2): the sim-forced cadence under `DeterministicTick=1`; the double-fire check from Task 3's verdict. (5) `Battle.Morale.Dump` (values, staleness, contributions) for the evidence lines. (6) Flags-off identity: no GE applied, no catalog, byte-identical hash.

**Acceptance check.** A scripted fixture drives a unit across two fatigue bands: EXEC_RESULT lines show GE apply -> attribute write -> snapshot -> fragment delta within 1 frame; two overlapping effects aggregate in the documented order; flags-off hash equality.
**Commit.** `phase4a: task 5 MoraleTick GE + catalog + UnitSync corrections`

### Task 6 -- The morale pass: FSM + rally + cascade + auras + events (Enable flip)

**Goal.** MC5-MC9 live end to end; `Battle.Morale.Enable` defaults 1.

**Steps.** (1) `MoraleTickProcessor` (game thread, Battle group, after TallyProcessor): the period accumulator, decay-term pushes (4.2), exertion accumulation (4.3), the geometric input folds at the cluster cadence (4.6), FSM + rally evaluation in sorted-handle order, the rout/rally applies through the registry (A7). (2) The death-path officer branch (spec 4.7): spike amplification, `Battle.Unit.OfficerDied`, the general's `Battle.Team.GeneralDied` + shock + record invalidation. (3) Impetuous auto-charge + GuardMode suppression (A11). (4) The ExecBridge fixtures: `Battle.Morale.SpawnRout` (general + adjacent units, dictionary-keyed rows) + `Battle.Morale.Toggle`. (5) `Battle.Morale.Enable` -> 1 (ground rule 6). (6) The kill-chain evidence: officer death -> event count + spike delta; rout -> pairs released + flee movement + Flee state; rally under aura.

**Acceptance check.** Func tier: CombatSmoke + ChargeMechanics + S6_Volley + SkirmishBehavior still green (combat/ranged unregressed); the SpawnRout fixture's EXEC_RESULT lines show the full FSM cycle; flags-off identity re-asserted.
**Commit.** `phase4a: task 6 morale pass + events + Enable=1`

### Task 7 -- Scenarios S7_RoutCascade + OfficerDeath + EffectEndToEnd + ToggleBehaviors (T4.3-T4.6)

**Goal.** The exit-criteria scenarios are PR-gate citizens (the skeletons from Task 0 replaced).

**Steps.** (1) `Battle.Func.S7_RoutCascade` (spec section 5): the threshold order, the flee evidence, the cascade isolation sub-run, the rally with/without general sub-runs, the rear-arc chase assertion. (2) `Battle.Func.OfficerDeath`: the event counts, the amplified-spike delta vs baseline, the general variant's team-wide events + decay. (3) `Battle.Func.EffectEndToEnd`: the 1-frame bound per band + the stacking-order assertion. (4) `Battle.Func.ToggleBehaviors`: guard suppression, impetuous charge window, the phase-3 toggle rows re-asserted, the player-override. (5) All four registered via the factory (F11); fixed-tick machines; `MoraleSetCvars/RestoreCvars` beside the existing helpers (the C-3 one-home lesson). (6) The M5 gray-box sample on the demo game mode (overlay-only).

**Acceptance check.** Func tier green with the four scenarios added to the PR set; verbatim evidence lines in the progress log; M5 runs headless.
**Commit.** `phase4a: task 7 S7 + officer + effect + toggle scenarios`

### Task 8 -- Determinism (T4.7), staleness (T4.8), soak variant, re-baselines

**Goal.** MC10/MC1 proven; the nightly morale tier exists; baselines current.

**Steps.** (1) `DeterminismTwoRun` extended to the mixed morale battle (clash + volley + routs + effects, 40 s, hash every 60 ticks, under `DeterministicTick=1`). (2) The staleness probe: a loaded fixture (the PerfStress shape) asserts `TickAgeMax <= TickPeriod` across a 60 s window (T4.8); `MoraleMs` recorded in the baseline notes (the T2.9 pattern). (3) `Battle.Morale.SoakRout=1` (F18): the mix gains a general + impetuous rows; FINAL line += routs/rallies/shattered/officerDeaths/moraleTicks; flag off = byte-identical. (4) The spawn baseline: record with the bOfficer field (re-baseline only if moved beyond noise, disclosed). (5) One staged nightly run as evidence (rides Task 2's unblock).

**Acceptance check.** Two-run hash equality (mismatches=0) with morale terms in; T4.8's bound held with the measured line; soak variant ends with sane counters + stuck=0 separation=0; baselines current.
**Commit.** `phase4a: task 8 determinism + staleness + soak + baselines`

### Task 9 -- Overlay, metrics, docs, final verification

**Goal.** B.8 closure: observable, measured, documented, regression-protected.

**Steps.** (1) B.2 morale overlay rows complete (morale bars colored by rout state, general aura rings, flee arrows). (2) Metrics wired into `Battle.DumpMetrics` + `ResetDiagnostics` (MoraleMs, TickAgeMax, Routs, Rallies, Shattered, OfficerDeaths, AuraActive -- APPENDED fields, the F14 positional rule). (3) B.7 regression-checklist entry appended to the roadmap (authorized here): Phase 4a gates + invariants. (4) Docs: `Docs/README.md` map + status; the ADR-32..37 proposals handed to the owner; facts corrections per AGENTS section 4; the 4b handoff sketch (the report's section 5 names the 4b entry points). (5) Final verification: full unit + func tiers, one nightly (baseline + rout variant), git-log gate-first audit, archetype count 8 asserted everywhere; the PR-12..PR-15 statuses reviewed.

**Acceptance check.** B.8 checklist (section 7) fully checked with evidence pointers; CI green; progress log complete; handoff report per AGENTS section 5.
**Commit.** `phase4a: task 9 overlay + metrics + docs` then `phase4a: complete`

## 6. Gate -> implementation map

| Gate | Tier | Task | Test name | Asserts (short) |
|------|------|------|-----------|-----------------|
| T4.1 | Unit | patch/Task 4 | `Battle.Unit.Morale.Stack` | each contribution isolated at known weight; spike decay per curve; thresholds fire in order |
| T4.2 | Unit | patch/Task 4 | `Battle.Unit.Morale.Fatigue` | exertion integral -> state thresholds -> multiplier table golden |
| -- | Unit | patch/Task 4 | `Battle.Unit.Morale.RoutRally` | FSM transitions + discipline scaling + rally roll math |
| T4.3 | Func | 7 | `Battle.Func.S7_RoutCascade` | wavering->rout order; flee + release; cascade; rally under aura; rear-arc chase |
| T4.4 | Func | 7 | `Battle.Func.OfficerDeath` | officer/general events; spike amplification; team shock decay |
| T4.5 | Func | 7 | `Battle.Func.EffectEndToEnd` | GE -> fragment <= 1 frame; multiplier goldens; stacking order |
| T4.6 | Func | 7 | `Battle.Func.ToggleBehaviors` | guard suppression; impetuous window; phase-3 toggles; player override |
| T4.7 | Func | 8 | `DeterminismTwoRun` (morale-extended) | two-run hash equality incl. block 10 under the pin |
| T4.8 | Fixture | 8 | staleness probe (BattleMetrics) | tick age <= one period under load |
| -- | Unit | patch/Task 4 | `SaveRoundTrip` (v3) + `HashTerms` (block 10) + `EduRoundTrip` (16-key) | the state + data spines |

## 7. DoD checklist

- [ ] T4.1-T4.8 green with evidence (unit/func via `run_tests.bat`; staleness via the fixture + measured BattleMetrics line; verbatim lines in the report)
- [ ] MC1-MC10 demonstrable with evidence pointers (spec section 0)
- [ ] B.8 item 1: unit tests over the math (`BattleMorale`: stack, fatigue, FSM/rally)
- [ ] B.8 item 2: the four scenarios in the PR gate
- [ ] B.8 item 3: debug overlay (morale bars, aura rings, flee arrows)
- [ ] B.8 item 4: metrics (MoraleMs, TickAgeMax, Routs, Rallies, Shattered, OfficerDeaths, AuraActive)
- [ ] B.8 item 5: B.7 regression-checklist entry (roadmap, this phase)
- [ ] B.8 item 6: `Battle.Morale.*` CVARs; flags off = pre-morale behavior (unchanged soak identity asserted)
- [ ] B.8 item 7: Iris-ready -- registry state + input blocks are plain data; no raw pointers across boundaries; rolls seed-derived
- [ ] Blockers 1 + 2: fixed or owner-escalated with dossiers (Tasks 1-2; the escalation is a legitimate close)
- [ ] Archetype count 8 constant through all scenarios and soak runs (officers add no compositions)
- [ ] State-hash stability with morale terms (mutation sensitivity + two-run equality under the pin)
- [ ] Zero per-tick heap allocations in the morale pass steady state (EC9; registry-resident inputs)
- [ ] Save schema v3 round-trip green (rout + values + guard; legacy v2 accepted) -- A9
- [ ] EDU golden re-sign ratified on the machine (16-key + officer lines; drift disclosed) -- F5/R-M6
- [ ] The UnitSync placeholder corrections landed + disclosed (MoraleBonus, Fatigue coupling, D-4 mirror move) -- A8/A15/R-M9
- [ ] The GAS API digest merged into `Docs/reference/engine-api-5.8.md` (Task 3)
- [ ] M5 demo gray-box running (SpawnRout fixtures; overlay readouts)
- [ ] `entity_spawn_80_us` recorded with the bOfficer field (re-baseline only if moved, disclosed)
- [ ] Gate-first verified via git log (each T4 gate landed with/before its owning task; the kickoff patches' tests counted)
- [ ] ADR-32..37 handed to the owner as proposals; the 4b entry points named in the report's section 5
- [ ] `Docs/phases/phase-4/progress.md` complete; `Docs/README.md` map + status updated; handoff report per AGENTS section 5; PR-12..PR-15 statuses recorded

## 8. System prompt

```text
You are the MassBattle Phase 4a implementation agent.
Read Docs/README.md, then Docs/AGENTS.md, then Docs/phases/phase-4/brief.md.
Execute the brief autonomously under the standing protocol; brief section 3.1
decisions are owner-approved as marked. The kickoff patches (Patches/0024..0029)
are pre-applied: Task 0 verifies them first; Tasks 1-2 close the inherited
blockers before morale work. Stop at the brief's DoD (section 7) after writing
Docs/phases/phase-4/report.md and the final commit.
```
