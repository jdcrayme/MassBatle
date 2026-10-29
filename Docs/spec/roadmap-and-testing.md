# Total War Battle System — Implementation Roadmap & Testing Strategy

**Companion to:** `Docs/spec/architecture.md` (section references below use that doc's numbering)
**Engine pin:** UE 5.8.x (ADR-13 in the architecture doc) · **Review:** 2026-10-23 corrections applied — full evidence in `Docs/reviews/plan-review-findings.md`, gate changes summarized in §B.9

---

## Part A — Build Order

Guiding principle: **riskiest systems first, gray-box always.** The engagement solver (§7) is the highest-risk, most signature-defining piece; movement (§8) is second. Art passes come last; nothing visual blocks a mechanical milestone.

### Phase 0 — Foundation

**Goal:** the translation layer and data spine working end to end.

- `UUnitRegistrySubsystem`: handles (index + generation), `FUnitRuntimeState`, roster arrays.
- `UMassEntityConfigAsset` per role; EDU-format entity table parsed at cook; batched spawn + fixup pass (§5.3).
- Unit actor with the closed GAS attribute set stubbed (§10.1); attribute delegates → registry dirty flag → `UnitSync` writing shared stat fragments (§5.1).
- Signal path wired: entity death signal → registry → gameplay event on unit.
- Debug overlay framework (see B.2) — build it now, not later.
- Pre-Phase 0 [Review 2026-10-23]: pin engine to **UE 5.8.x** (ADR-13) and adopt ADR-14 — apply the E1 correction (unit handle embedded in every per-unit shared fragment payload + handle-seeded creation hash) and the E4 correction (5.8 template-based spawn API) before any Mass code is written; run the scripted pinned-version API audit over every Mass/GAS symbol named in the architecture doc (I6).
- CI infrastructure as an explicit deliverable [R8]: automation host (Horde / Jenkins / hosted CI), packaged-build nightly orchestration for Gauntlet, versioned baseline-JSON storage, and a named reference machine whose specs are recorded in every perf baseline [R10].

**Exit criteria:** 2 units × 40 entities spawned from table data; a GAS attribute change is visible in `FUnitStatsSharedFragment` within 1 frame; killing an entity flips archetype, fires signal, updates roster. *(Phase 0 probe note: the archetype flip stays -- ADR-15 evaluated 5.8 sparse tags and reverted: chunk-granular storage + observer incompatibility, see `Docs/reference/engine-api-5.8.md`.)*

**Test gate T0 — Foundation:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T0.1 | Unit | Handle registry lifecycle | generation counter rejects stale handles; free-list reuse safe; 10k spawn/destroy round-trip leak-free |
| T0.2 | Unit | EDU parser round-trip | golden `.txt` → tables → re-serialize **canonical form** compared against a canonical golden file — byte-identical is over-strict (EDU comments/formatting are not reconstructible) [R11]; unknown fields rejected loudly |
| T0.3 | Editor | Entity-config integrity | every `MassEntityConfigAsset` resolves fragments/tags; archetype buildable; roster specs instantiate |
| T0.4 | Functional | Spawn + sync latency | attribute change → shared fragment ≤ 1 frame (asserted on tick count, not wall time) |
| T0.5 | Functional | Death path | `FDeadTag` removes entity from live queries within the stated flush contract — observer fires during the kill flush [E5]; signal updates roster; queued destroy completes. *(Implemented as Battle.Func.S0_DeathPath; ADR-15 final state: regular tag.)* |
| T0.6 | Unit | State-hash stability | identical sim state → identical hash; any relevant mutation changes it |
| T0.7 | Unit | RNG registry discipline | suite green under N different seeds — a seed-dependent failure means leaked global RNG |
| T0.8 [new] | Unit | Shared-fragment uniqueness | spawn 8 units with identical base stats → 8 distinct shared-fragment instances; a change on unit A is not observable on unit B in the same frame [E1] |
| T0.9 [new] | Unit | Test-mode isolation | 5.7/5.8 scheduling features (processor time-slicing, variable tick) verifiably **off** under test by reading the engine's active settings, not just CVars being set [R3] |

Standing rule from Phase 0 onward: pure-math seam audit at every code review (B.0).

### Phase 1 — Movement core

**Goal:** formations that march, wheel, compress, and re-dress without stuck or separated entities (§8).

- Unit locomotion: anchor on path, slowest-member speed limit, wheel-rate cap, `SpacingScale` compression + recovery (§8.2).
- Entity servo with `HoldSlot`/`ManeuverToSlot`/`CrossField` states and per-state avoidance weights (§8.3).
- Locality-aware slot assignment + predictive swaps (§8.4). Unstuck ladder v1 (§8.5).
- Unit-vs-unit OBB contact with yield/block policies (§8.6).
- Heightfield path planner as a first-class deliverable [R4, review 2026-10-23]: A*/jump-point search over the cost grid with slope-aware costs, path smoothing, path-frame sampler — everything downstream (wheels, squeezes, corridor morphs, S1–S3) assumes a path exists; gated by T1.9.
- State-gated engine avoidance: specify the force→velocity conversion per state before implementation — Mass Avoidance is force-based (`FMassForceFragment`) and Experimental [R2].
- Thin selection/command shell [R5]: minimal selection + order issuance so M1 is playable, with its own smoke test — or explicitly scope it out of v1 so the omission is a decision, not an accident.

**Exit criteria:** 8 units execute march, 90° wheel, mutual pass, and squeeze-through-gap with zero stuck events over a 10-minute soak; no flank shedding on wheels.

**Test gate T1 — Movement core:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T1.1 | Unit | Slot assigner | assignment is a bijection; ≤ k-file displacement; predictive swap terminates; batched reflow idempotent |
| T1.2 | Unit | Path-frame layout | rank arc-lengths monotonic; laterals orthogonal to tangent; straight-path degeneracy equals anchor formula |
| T1.3 | Unit | Speed limits | limit = min(commanded, slowest×1.25, v_wheel = ωmax·r_path, facing cap = FacingDeltaRate·r_path); golden cases incl. degenerate radii and straight-march inactivity [E2]; hysteresis holds under member noise |
| T1.4 | Unit | Compression model | 60% floor respected; recovery rate bounded; no oscillation under step input |
| T1.5 | Functional (S1) | March + wheel | zero unstuck escalations; separation < threshold; no flank shedding |
| T1.6 | Functional (S2) | Pass-through shove | no pairwise stalls; wake re-dresses within T; pass time bounded |
| T1.7 | Functional | Squeeze vs morph | threshold decision correct both sides; morph commits once; no thrash |
| T1.8 | Gauntlet (nightly) | Mini-soak | 8 units, 10 min seeded random orders: stuck = 0, separation = 0, archetype count constant |
| T1.9 [new] | Unit | Path planner | paths valid against impassable cells; arc-length monotonicity; deterministic under seeded RNG; bounded re-plan latency when anchors move [R4] |

### Phase 2 — Engagement solver & melee (prototype in capsules first)

**Goal:** the combat heart — formed clashes and mobs from one system (§7, §9.1–9.2).

- Spatial hash (game-thread rebuild, counting sort) (§7.1).
- Engagement records + cluster graph (§6.3); tally→assign pass (§7.2); budgets/coverage/exposed angles (§7.3); press-ring waiter behavior (§7.4); pair lifecycle with hysteresis (§7.5).
- Melee resolution with directional defense, AP, clamps — port community formulas (§9.1). Charge state machine (§9.2).
- StateTree prototype for the unit FSM (§6.2), half-day timebox; record the choice as an ADR either way before `OrderLatency` gating and impetuous overrides accrete [I4].
- Save/load skeleton with a schema-version field [R7]: registry runtime state + fragment payloads via rebuild-from-spec; grows with the schema so T6.3 validates maturity instead of discovering design debt.
- Demo M3 as **geometry-only** at this phase; the morale beat re-runs enriched after Phase 4 [R6].

**Exit criteria:** scenario tests S4/S5 pass (Part B); 2 knights vs 40 peasants ends with the knights pulled down by mob geometry, not special-case code; shield wall holds at FormedSlots then melts as coverage drops.

**Test gate T2 — Engagement & melee:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T2.1 | Unit | Hit-chance golden | formula + arcs + AP + clamps locked to golden table; any diff = conscious formula review |
| T2.2 | Unit | Budget/coverage invariants | exposed arcs filled before covered; budget bounds [1, MaxMob] never violated over 1k random assign/remove cycles |
| T2.3 | Unit | Tally/assign synthetic | budgets respected on synthetic populations; determinism flag yields index-sorted reproducibility |
| T2.4 | Functional (S4) | Shield wall curve | holds at FormedSlots; collapse accelerates as coverage drops (asserted curve shape, not just endpoint) |
| T2.5 | Functional (S5) | Mob test | isolated knight takes ≥ 4 exposed-arc attackers; pulled down within time band; waiters at contact ring (no carousel) |
| T2.6 | Functional | Charge mechanics | bonus only on fresh fast contact; knockback applied; decays to melee within window |
| T2.7 | Functional | Pair lifecycle | hysteresis breakup; vacated-slot handoff ≤ 1 tick; disengage releases all inbound pairs |
| T2.8 | Functional | Determinism two-run | same seed twice → per-tick state hash identical across a 5-min clash scenario (scoped to **same binary, same machine** — cross-platform float reordering is out of scope [R9]) |
| T2.9 | Perf | Pairing gate | < 0.5 ms game thread @ 1k in contact; baseline JSON checked in, CI compares |

### Phase 3 — Ranged stack

**Goal:** bows/crossbows at volley scale (§9.3).

- Pooled instanced projectiles, ballistic arcs vs flat trajectories, dispersion, rank-limited volleys, LOS via heightfield, friendly-fire checks.
- Per-entity ammo, unit-level out-of-ammo state; `SkirmishMode` auto-retreat behavior.

**Exit criteria:** S6 passes; 1,000 arrows in flight costs < 0.3 ms sim; friendly-fire rules demonstrable.

**Test gate T3 — Ranged stack:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T3.1 | Unit | Ballistic solver | arc vs flat trajectories; dispersion within bounds; deterministic per seed |
| T3.2 | Unit | LOS sampling | golden hill/occlusion cases; no false occlusion at equal height |
| T3.3 | Unit | Ammo accounting | shots == decrements; out-of-ammo tag fires at aggregate zero; unit aggregate == entity sum |
| T3.4 | Functional (S6) | Volley | dispersion, LOS, friendly-fire rules, and ammo accounting all asserted in one scenario |
| T3.5 | Functional | Skirmish behavior | auto-retreat triggers at melee range; resumes fire when clear; respects hold-fire |
| T3.6 | Perf | Projectile scale | 1,000 in flight < 0.3 ms sim; pool never allocates mid-volley |

### Phase 4 — Morale & full GAS

**Goal:** the soul of the battle (§10.5).

- Morale contribution stack behind an **explicit tick mechanism** [E3, review 2026-10-23]: periodic infinite-duration `MoraleTick` GE (period 0.25–0.5 s, §12 tunable) whose period execution runs the exec-calc over sim-fed SetByCaller inputs, or a sim-owned accumulator applying an instant evaluation GE; the fresh-casualty spike is a sim-tracked decay term — GAS exec-calcs fire on aggregation events, never on their own timer. Fatigue tick from exertion; effect catalog (§10.5).
- Routing state machine (wavering → breaking → rout), rally rolls, panic cascade; officer/banner death events riding existing signals; General/Terror auras; `OrderLatency`.

**Exit criteria:** S7 passes; routing cascade observable across adjacent units; general death produces measurable morale shock and rally behavior.

**Test gate T4 — Morale & GAS:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T4.1 | Unit | Morale stack golden | each contribution isolated at known weight; fresh-casualty spike decays per curve; thresholds fire in order; setup drives the chosen tick mechanism explicitly [E3] |
| T4.2 | Unit | Fatigue model | exertion integral → state thresholds → multiplier table golden |
| T4.3 | Functional (S7) | Rout + rally | routers chased with rear-arc resolution; rally succeeds under general aura, fails without |
| T4.4 | Functional | Officer death | banner-death signal → GAS event → measurable morale shock; cascade reaches neighboring units |
| T4.5 | Functional | Effect end-to-end | Exhausted GE → `SpeedMod`/`BlockChanceMod` → shared fragment → entity-visible ≤ 1 frame; stacking order honored with overlapping effects |
| T4.6 | Functional | Toggle behaviors | guard / fire-at-will / skirmish alter behavior as specified; impetuous pursue overrides orders |
| T4.7 | Functional | Full-battle determinism | two-run hash equality on a mixed battle including morale and effects |
| T4.8 [new] | Unit | Morale staleness bound | under load, Morale value age never exceeds one morale-tick period [E3] |

### Phase 5 — Representation

**Goal:** the three-band presentation (§11).

- LOD bands with hysteresis, skeletal actor pool, ISM pools per mesh variant, AnimToTexture bake pipeline, per-instance custom data with phase sync.
- AnimToTexture contingency [E6, review 2026-10-23]: pin the plugin version with the engine pin; bake pipeline re-runnable from source (skeletal clips + editor/cook-time bake script — committed baked textures are never the only artifact); fallback ladder documented (Niagara mesh-rendering VAT / instanced flipbook).
- Draw-call arithmetic [R13]: document the derivation behind the ≤ 25 gate (variants × unit types + corpse pool + projectile pools) so T5.5 asserts a derived number.
- Death/corpse state machine and pools; heraldry/tint variation; VFX/SFX aggregation rules (§11.3–11.5).

**Exit criteria:** zoom-in moment plays synchronized duels at ≤ 150 skeletal actors; full 2×2,400 battle at one draw call per mesh/material; LOD migration churn invisible in profiling.

**Test gate T5 — Representation:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T5.1 | Functional | LOD bands | entity in expected band at given distance; oscillating camera produces no flip-flop (hysteresis) |
| T5.2 | Functional | Phase sync | custom-data phase == f(AttackTimer) at the resolution beat; death transitions at correct beats |
| T5.3 | Functional | Corpse lifecycle | death → corpse instance within budget; oldest recycled at cap; zero instance leaks over 10-min battle |
| T5.4 | Screenshot (nightly) | Visual sanity | crowd present **and animating** per band — a frozen mid-band VAT fails [E6]; heraldry atlas correct; generous tolerance, gross failures only |
| T5.5 | Perf | Representation budget | 2×2,400: ≤ 25 draw calls, justified by the §12 derivation [R13]; custom-data writes/frame in budget; actor churn invisible in trace |
| T5.6 | Gauntlet soak | 60-min battle | memory flat post-warmup; no handle exhaustion; VFX/audio voice caps respected |

### Phase 6 — Tuning & hardening

**Goal:** M2TW feel and shipping budgets.

- Formula audit against community docs (open decision 3); tunable sweep using the §12 table.
- Perf gates (B.6) at 1k/2.5k/5k entities; save/load as unit state + entity rebuild; statistical far-LOD sim evaluated (open decision 2).

**Exit criteria:** perf budget table green at target counts; formula audit signed off; soak tests clean.

**Test gate T6 — Hardening:**

| ID | Tooling | Test | Expectation |
|----|---------|------|-------------|
| T6.1 | Unit | Formula audit | community-ported constants replace placeholders; golden files re-signed with diff review; friendly-fire rule resolved as an explicit port-or-documented-deviation [R12] |
| T6.2 | Perf | Full gates | 1k/2.5k/5k table green; per-cluster sharding path validated at 5k |
| T6.3 | Functional | Save/load round-trip | save → load → resume → state hash equal; pairs, corpses, morale preserved |
| T6.4 | Gauntlet | Full nightly | S1–S7 + soak + fuzz all green on packaged build |
| T6.5 | Functional | Far-LOD parity (if built) | statistical attrition within band of full entity sim on identical scenario |

### Milestone demos

| M | Demo | Proves |
|---|------|--------|
| M1 | Marching square | movement core |
| M2 | Formed clash | engagement solver, formed budgets |
| M3 | Mob test (knights vs peasants) | exposure budgets (geometry, Phase 2); morale pressure beat added after Phase 4 [R6] |
| M4 | Arrow volleys | ranged stack |
| M5 | Rout cascade | morale stack |
| M6 | Zoom-to-ground duel | representation sync |

---

## Part B — Testing Strategy & Automation Tooling

### B.0 UE automation tooling & test-mode contract

**Test pyramid → Unreal tooling:**

| Layer | UE tooling | Covers | Cadence |
|-------|-----------|--------|---------|
| Pure math / state | Automation Framework (`FAutomationTestBase`, `IMPLEMENT_SIMPLE_AUTOMATION_TEST`), no world | formulas, budgets, assignment, layout, RNG | per commit |
| World sim | Functional Tests (`AFunctionalTest` + `UFunctionalTestingManager`) on minimal maps | scenario tests S1–S7 | per PR |
| Long-run & fuzz | Gauntlet (`UGauntletTestController`) on packaged builds | soak, monkey-commander, memory invariants | nightly |
| Perf | Gauntlet + stat groups (`STAT_GROUP(Battle_*)`) vs checked-in JSON baseline | processor budgets (B.6) | nightly + pre-release |
| Data & assets | `EditorContext` automation tests + validation commandlet | EDU parse, entity-config integrity, archetype buildability | pre-cook, per commit |
| Visual | Screenshot comparison, generous tolerance | gross representation regressions only | nightly |

**The pure-math seam (Phase 0 requirement).** Every test-relevant algorithm lives as a free function or plain struct — `FBattleMath::MeleeHitChance()`, `FSlotAssigner::Assign()`, `FBattleSpatialHash`, `FPathFrameLayout::Project()`, fatigue multiplier table, morale aggregator. Processors are thin shims: read fragments → call pure function → write fragments. Math found inside a `ForEachEntityChunk` lambda is a refactor flag at code review, every phase.

**Test-mode contract** (all tests run under it; one switch, grouped as `Battle.Test.*` CVars -- implemented by `UBattleTestSettings::ApplyTestMode()` and asserted against active engine state by `UBattleTestSettings::AssertTestMode()` in `Plugins/MassBattle/Source/MassBattle/Public/BattleTestSettings.h`; applied + asserted at every S-scenario start):
- Fixed delta-time stepping; assertions run after N explicit sim ticks — never wall-clock waits.
- SimLOD forced to full detail and variable-tick disabled (LOD scheduling would make results nondeterministic); 5.7+ processor **time-slicing disabled** under the same contract — asserted by T0.9 against active engine settings, not just CVars [R3].
- Seeded RNG registry active; deterministic (index-sorted) iteration flag available.
- Assertions read sim state (tallies, coverage, stuck counters, morale) — never visuals (sole exception: T5.4).
- Everything behind `#if WITH_DEV_AUTOMATION_TESTS`; test maps excluded from shipping cook.

**Naming & labels:** `Battle.Unit.<Area>.<Name>` (per commit) · `Battle.Func.S<n>_<Name>` (per PR) · `Battle.Data.*` / `Battle.Editor.*` (pre-cook) · `Battle.Perf.Gate<n>`, `Battle.Soak.*`, `Battle.Fuzz.*` (nightly, packaged). Packed-client functional runs launch with `-ExecCmds="Automation RunTests Battle.Func" -TestExit="Automation Test Queue Empty"`; Gauntlet drives the nightly tiers. Known-benign warnings whitelisted via `AddExpectedError`; full suite runs in dev builds so Mass access-detector asserts surface in CI.

**CI tier table:**

| Gate | Content | Available from |
|------|---------|----------------|
| Per commit | `Battle.Unit.*` + `Battle.Data.*` + 3 smoke functional tests | Phase 0 |
| Per PR merge | full `Battle.Func.*` (all S-tests built so far) | Phase 2 |
| Nightly | `Battle.Soak.*`, `Battle.Fuzz.*`, `Battle.Perf.*`, screenshot tier | Phase 2 / 5 |
| Pre-release | full suite + perf gates at 5k + determinism full-battle | Phase 6 |

### B.1 Gray-box staging

Every phase ships in capsules/gray-box on a flat heightfield tile first, then one authored terrain. Art never gates a mechanical milestone. Deterministic seeds drive all scenario tests so failures reproduce.

### B.2 Debug overlays (built in Phase 0, kept forever)

- Pairing lines with `ContactAngle` and pair state colors; per-defender tally vs budget.
- Coverage heat (per-entity neighbor coverage 0–1).
- Unstuck ladder level per entity (step 1–4 colored).
- Slot error magnitude; reflow events; slot swaps.
- Engagement records (frontage segments, lanes, mode, pressure vs assigned).
- LOD band + representation type per entity; custom-data write counts/frame.
- Morale/fatigue readouts per unit; rout state.
- MassInsights / Mass Debugger feeds wired into this framework [I2, review 2026-10-23]: 5.8 tracing exposes archetype-move events, sparse-element visibility, and batched entity-move traces; cross-check B.4's hand-rolled metrics against them instead of reimplementing (the Mass Debugger's archetype/entity views surface E1-class shared-fragment collisions visually).

### B.3 Scripted scenario tests (automated where possible)

| ID | Scenario | Assertions |
|----|----------|-----------|
| S1 | March + wheel | zero unstuck escalations; no entity beyond separation threshold; wheel sheds no flankers |
| S2 | Cavalry through infantry | no pairwise-avoidance stalls; wake re-dresses to < slot error threshold within T; pass-through time bounded |
| S3 | Two units through a pass | second unit holds queue point; morph commits once; no re-morph thrash; clean re-deploy past exit |
| S4 | Shield wall vs frontal | defender budget stays at FormedSlots; wall holds until casualty threshold; collapse accelerates (coverage→budget curve) |
| S5 | 2 knights vs 40 peasants | knights surrounded (≥ 4 exposed-arc attackers); pulled down within expected time band; no queue carousel (waiters at contact ring) |
| S6 | Volley into formed line | dispersion within bounds; LOS respected; friendly-fire rules hold; ammo decrements match shots |
| S7 | Rout cascade | wavering→rout thresholds fire; routers chased with rear-arc resolution; rally roll succeeds under general aura |

### B.4 Metrics to instrument

Pair churn/sec and re-pair latency (ticks) · stuck events per 10 min · separation events · per-processor ms (target: pairing < 0.5 ms game thread @ 1k in contact) · archetype count (watch for unbounded growth) · custom-data writes/frame · actor spawn/despawn churn · concurrent VFX/audio voices · entity counts per band.

### B.5 Determinism (hooks only, per ADR-11)

Seeded RNG registry (one stream per system, seeded per battle). A compile-time flag switches pairing/shuffle to index-sorted iteration with per-entity jitter. Optional post-M2: sim-state hash per tick compared across two runs of the same seed — cheap insurance that no hidden wall-clock or iteration-order dependency exists. Claims are scoped to **same-binary, same-machine** runs; cross-machine/compiler floating-point reordering (FMA contraction, fast-math) can diverge hashes without a real defect and is out of scope under ADR-11's hooks-only posture [R9].

### B.6 Perf gates

| Entity count | Pairing (GT) | Sim total/frame | Notes |
|--------------|--------------|-----------------|-------|
| 1,000 | < 0.5 ms | < 2 ms | all entities in contact stress case |
| 2,500 (target) | < 1 ms | < 4 ms | two 20-unit armies |
| 5,000 | < 2.5 ms | < 8 ms | triggers per-cluster sharding of assign pass if exceeded |

### B.7 Regression checklist (per phase merge)

Scenario tests S1–S(upto phase) green · debug overlay sanity pass · archetype count unchanged · no new per-frame allocations in hot processors · perf gate at current target count · formula constants unchanged (or diff-reviewed).

Phase 0 entry: `Battle.Unit.*` + `Battle.Data.*` + `Battle.Smoke` (15 tests) and `Battle.Func.*` (S0_SyncLatency / S0_DeathPath / S0_ExitCriteria) green via `Tools/CI/run_tests.bat` · archetype count 2 after the T0 scenario (watched via `Battle.DumpMetrics`) · spawn/sync/death paths each carry their `Battle.*` cvar hooks · no allocations added in UnitSync/DeathObserver hot paths.

Phase 2 engagement entry: `Battle.Unit.Combat.*` (T2.1 HitChance golden / T2.2 BudgetCoverage / T2.3 TallyAssign / RecordLifecycle / OrderAttack / SaveRoundTrip / SpawnComposition / HashTerms / SpatialHash / ClusterRecords, 66 tests) + `Battle.Func.*` (S4_ShieldWall / S5_Mob / ChargeMechanics / PairLifecycle / CombatSmoke / DeterminismTwoRun, 21 tests) green via `run_tests.bat {unit,func}` · the pairing gate T2.9: tally+assign < 0.5 ms GT @ 1k in contact (`Battle.Combat.PerfStress`, BattleMetrics TallyMs+AssignMs; baseline JSON committed) · `Battle.Combat.Enable` flags-off = the pre-combat baseline (the movement suite green at Enable=1 too: combat idles without intents) · archetype count 6 constant (the combat fragments ride every battle entity; zero new tags) · state-hash stability with the combat terms (HashTerms mutation sensitivity + the T2.8 two-run equality) · debug overlay: engagement-record lines (mode colors) behind `Battle.Debug.ShowOverlay`; metrics via `Battle.DumpMetrics` (CombatHashMs/TallyMs/AssignMs/Rolls/ChargeConnects/DeathsByArc) · soak clash variant `Battle.Combat.SoakClash=1` (flag off = the T1.8 baseline).

Phase garrison entry: `Battle.Unit.Garrison.*` (TG.1 Stamping / TG.2 InternalPath / TG.3 SlotAssign / TG.4 Booking / TG.5 ZModel) + `Battle.Func.S8_BuildingGarrison` / `S9_TrenchGarrison` / `S10_WallGarrison` / `S8b_MultiUnitGarrison` (TG.6-TG.9) green via `run_tests.bat {unit,func}` · garrison soak variant (`Battle.Garrison.SoakOrders=1`) ends with settled-or-queueing garrisons, stuck = 0, separation = 0, archetypes 6 · flag-off soak (`SoakOrders=0`) byte-identical to the T1.8 baseline · debug overlay garrison draws (phase markers, footprint, entrance legs, slot posts) gated by `Battle.Debug.ShowOverlay` · metrics via `Battle.Garrison.Metrics` · CVARs `Battle.Garrison.Enable` / `SoakOrders` off restore pre-garrison behavior.

Phase 3 ranged entry (2026-10-29; appended by the phase-3 run per brief task 8 authorization): `Battle.Unit.Ranged.*` (T3.1 Ballistics / T3.2 LOS / T3.3 Pool + AmmoAccounting + VolleySelection + SkirmishMath + PoolStep + RegistrySpine + FirePass + Impacts) + `Battle.Data.EduRoundTrip` (15-key stat_sec schema) + `Battle.Unit.Combat.{SaveRoundTrip (schema v2), HashTerms (ranged terms + pool block)}` green via `run_tests.bat unit` · `Battle.Func.S6_Volley` + `Battle.Func.SkirmishBehavior` in the PR gate (dispersion bound, friendly-fire both halves, hill LOS, ammo accounting; skirmish break/resume/hysteresis/hold-fire/order-override) · perf gate T3.6: `Battle.Ranged.PerfStress` measured 0.236 ms step+fire @ 1,091 in flight < 0.3 ms budget, PoolDrops=0 (BattleMetrics InFlightPeak/StepMs/FireMs; the RangedPerf probe line) · `Battle.Ranged.Enable` default 1 with the melee func suite unregressed (flags-off identity asserted in FirePass/PoolStep) · archetype count stable (the ranged fragment rides ranged-capable compositions only; one bow/bolt composition, Kind is a fragment value) · state hash: ranged per-entity terms + the pool block (HashTerms mutation sensitivity; the mixed two-run extension carries an OPEN mm-scale blocker -- see the phase-3 report) · debug overlay: trajectory segments (amber arced / blue flat), team-colored impact rings, per-unit ammo bars, hold-reason circles, all behind `Battle.Debug.ShowOverlay` · metrics via `Battle.DumpMetrics` (InFlight/StepMs/FireMs/Shots/Holds/Impacts/FriendlyHits/PoolDrops/InFlightPeak -- APPENDED fields, the F14 positional rule) · soak variant `Battle.Ranged.SoakVolley=1` (the FINAL line gains volleys/inFlightPeak/friendlyHits/poolDrops; flag off = byte-identical) · save schema v2 (toggles + fraction denominators + TeamId; legacy v1 accepted).

### B.8 Definition of done for any new capability

A capability is not "done" until it has:

1. **Unit tests** over its math (golden values where formulas are ported),
2. **A scenario test** exercising it end-to-end (added to the PR gate),
3. **A debug overlay** exposing its internal state,
4. **A metric** added to the instrumented set (B.4),
5. **An entry** in the phase regression checklist (B.7),
6. **A `CVAR` feature flag** so half-built systems opt out of the baseline that perf/soak tiers measure.
7. **Iris-ready invariants maintained** [I3, review 2026-10-23]: no raw pointers across the registry boundary; all unit state reconstructible from serializable sim state; RNG streams seed-registered — Iris reached production-ready status in 5.8 and Epic continues toward Mass-on-Iris replication, so multiplayer-readiness becomes a byproduct of ordinary discipline.

Rule 6 keeps nightly baselines meaningful while systems are in flight — a capability mid-development must never silently shift the perf or soak baseline that other regressions are judged against.

### B.9 Review-derived changes (2026-10-23)

The 2026-10-23 plan review corrected six errors (E1–E6) and raised thirteen risks (R1–R13) plus seven improvements (I1–I7). Corrections are applied inline above and in the architecture doc; the gate changes are consolidated here. Full reasoning, evidence, and sources: `Docs/reviews/plan-review-findings.md`.

| Gate | Change | Motivated by |
|------|--------|--------------|
| T0.2 (amend) | Canonical-form comparison replaces byte-identical EDU round-trip | R11 |
| T0.5 (amend) | Assert command-buffer flush point directly, or end-of-phase semantics | E5 |
| T0.8 (new) | Shared-fragment uniqueness under identical payloads | E1 |
| T0.9 (new) | Test-mode isolation: time-slicing + variable tick verifiably off | R3 |
| T1.3 (amend) | Golden cases for corrected ωmax / v_wheel incl. degenerate radii | E2 |
| T1.9 (new) | Path planner validity, determinism, re-plan latency | R4 |
| T4.1 (amend) | Setup drives the explicit morale tick mechanism | E3 |
| T4.8 (new) | Morale staleness bound ≤ one tick period | E3 |
| T5.4 (amend) | Nightly fails on frozen mid-band VAT animation | E6 |
| T5.5 (amend) | ≤ 25 draw calls justified by §12 derivation | R13 |
| T6.1 (amend) | Friendly-fire rule resolved as port-or-documented-deviation | R12 |

Non-gate changes: engine pin + ADR-13/14 and CI infrastructure in Phase 0 (E4, R8, R10); path planner, avoidance conversion, and command shell in Phase 1 (R4, R2, R5); StateTree ADR, save/load skeleton, and M3 re-label in Phase 2 (I4, R7, R6); determinism scope in B.5 (R9); MassInsights adoption in B.2 (I2); Iris invariants in B.8 (I3); document-control blocks in both file headers (I7).
