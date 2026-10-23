# Total War Battle System — Plan Review Findings & Verification Record

**Review date:** 2026-10-23 · **Reviewer:** independent technical review (web-verified)
**Subjects reviewed:** `tw-battle-system-architecture.md` (Draft 1.0 → 1.1) · `tw-battle-system-roadmap-and-testing.md` (Draft 1.0 → 1.1)
**Primary consumer:** AI agents and future implementers. This file is the audit trail: full reasoning, evidence, and citations for every finding. The plan files carry only the corrected text and short `> **[REVIEW …]**` annotations pointing here.
**Verdict:** the plan is **approvable for Phase 0 after the corrections applied on 2026-10-23** — the architecture's shape is unchanged. 25 findings total: 6 errors (corrected in the plan text), 13 risks (decisions scheduled), 7 improvements (adopted or recorded). 7 load-bearing decisions verified correct against current official documentation.

**Finding ID convention.** `E#` = error (statement is wrong and was corrected). `R#` = risk/gap (decision absent or unspecified; a mitigation is scheduled). `I#` = improvement opportunity (exploits engine capability the plan did not mention). Severity: Crit / High / Med / Low. All version-sensitive statements reflect Unreal Engine 5.8-era documentation accessed on 2026-10-23 (see §11 Limitations).

---

## 1. How to Consume This File

1. Read the two plan files first; they are the operative specification and already contain the corrections.
2. Wherever a plan file shows `> **[REVIEW E# · 2026-10-23]**`, the corresponding section here (§4 for errors) holds the evidence trail: what was wrong, the source that proves it, the exact correction, and the test impact.
3. `ADR-13` (engine pin and upgrade policy) and `ADR-14` (shared-fragment uniqueness contract) were added to the architecture doc's decision log by this review; this file justifies both.
4. §8 lists the corrected reference snippets (wheel-rate formula, unique shared-fragment creation, morale tick) for direct implementation.
5. §12 is the source register: every external claim made anywhere in the three files resolves to a numbered reference here, with tier and access date.
6. Do not re-litigate verified items (§3) without new evidence; do re-verify version-sensitive findings (E4, E5, R1, R3, E6) after any engine upgrade, per ADR-13.

## 2. Review Scope & Method

**In scope:** both markdown plan documents (no source code or prototypes exist yet). Claims verified against: official Epic Games release notes and API references (Tier A), canonical community repositories (MassSample, GASDocumentation) and Epic staff statements (Tier B), and Medieval 2: Total War reverse-engineering communities (Tier B for combat-model provenance, Tier C for domain history).

**Method:** every external-facing assertion in the plan was checked against the current documentation; every internal formula was checked for dimensional consistency and implementability; every workflow the plan assumes (spawn, death, buff propagation, morale recompute, bake pipeline) was traced against the engine's actual API surface and execution semantics; the test plan was audited for gates that cannot pass as written.

## 3. Verified Strengths (checked out — do not churn these)

| # | Plan decision | Verified against | Result |
|---|---------------|------------------|--------|
| V1 | Hybrid split: GAS actors for units, Mass entities for soldiers (ADR-01) | GAS official docs [16][17][18]; Mass official docs [11]; City Sample production evidence [25][26] | Correct split; effect authority on ~40 actors is cheap, ECS throughput serves the crowd |
| V2 | Entity data schema mirrors M2TW EDU format (ADR-12) | EDU field references [27][28] | Community-reverse-engineered combat model matches §9.1/§10.2: defense = skill + shield (frontal arcs) + armor; armor-piercing halves armor; 1-HP infantry, mounts carry a second pool [29][30] |
| V3 | Sim-driven combat with timers; animation cosmetic (ADR-05) | M2TW mechanics [29]; City Sample decoupled-crowd pattern [25] | Stable under LOD; avoids animation-locked duel serialization |
| V4 | Command-buffer-only structural edits (§4 threading rules) | API and community docs [5][13] | Correct discipline; flush points must be stated (→ E5) |
| V5 | Single-player first; determinism hooks only (ADR-11) | Mass replication status: Epic staff confirmed in Nov 2025 that Iris-based Mass replication is still under investigation [14]; Iris production-ready in 5.8 but Mass-on-Iris not shipped [1][15] | Right posture for a solo project; invariants preserved via B.8 item 7 |
| V6 | Three-band representation with VAT mid band (ADR-10) | City Sample technique [25]; AnimToTexture status [22][24] | Feasible at target scale; Experimental plugin needs contingency (→ E6) |
| V7 | Automation pyramid mapped to UE tooling (B.0) | Automation Framework, Functional Tests, Gauntlet official docs [31] | Correct tooling per tier; Gauntlet drives packaged nightly tiers |

## 4. Errors (E1–E6) — corrected in the plan text on 2026-10-23

### E1 (Critical) — Per-unit shared fragments collide with Mass's value-hash deduplication
**Location (original Draft 1.0):** ADR-03; §3.3; §3.1 fragment table (`FUnitStatsSharedFragment`, `FUnitFormationSharedFragment`, `FUnitStateSharedFragment`); §5.1 UnitSync write path.

**Finding.** The plan asserted shared fragments provide "one copy per unit" and mandated "created at spawn, then mutated in place," calling the hash-dedup "incidental." In Mass, shared fragment instances are deduplicated **by content**: the canonical creation path searches for an identical struct by CRC hash and returns the existing instance if one matches. MassSample states it directly: *"Make sure your shared fragments are Crc hashable or else you may not actually create a new instance when you call `GetOrCreateSharedFragmentByHash`… You can actually pass in your own hash … which can help if you prefer to control what makes each one unique,"* and its template example *"search[es] for an identical struct with the hash. If there are none, make a new one"* [5] (MassSample README §4.2.1 and the `BuildTemplate` shared-fragment example; also *"configs with the same settings will share the same Shared Fragment"*). None of the plan's three shared fragments listed the unit handle among its fields, so two freshly spawned units with identical stats — two militia units with no active effects; two idle units in the same formation — are value-identical and receive **one shared instance**. `UnitSync`'s in-place mutation then writes through to every unit sharing it: silent cross-unit stat corruption during combat, with no assert and no archetype change to notice.

**Evidence:** [5] (MassSample, built for 5.6/5.7, dedup semantics unchanged through 5.8); 5.8 release notes add observer support for shared fragments but do not change dedup semantics [1].

**Correction (applied).** Uniqueness is constructed, not free: `FUnitHandle` (index + serial) embedded as the first field of every per-unit shared fragment; creation through a handle-seeded hash (`HashCombine(Index, Serial)`) rather than content hashing; ADR-14 records the contract; ADR-03 reworded from "the hash-dedup is incidental" to adversarial. Debug overlay flags any shared-fragment instance whose owner count exceeds its unit's roster size.

**Test impact:** new **T0.8** — spawn 8 units with identical base stats, assert 8 distinct instances, assert a change on unit A is not observable on unit B in the same frame. T0.4 extended conceptually to two identical units mutated differently in one tick (covered by T0.8's second assertion).

### E2 (High) — Wheel-rate formula dimensionally inconsistent
**Location:** §8.2 speed-limit formula; §12 "Wheel rate" row; T1.3.

**Finding.** Original: `Min(CommandedSpeed, MinOverMembers(EffectiveSpeed × 1.25), MaxMemberSpeed / OuterRadius × FacingDeltaRate)`. The third term mixes units: `speed / radius` is rad/s (a valid angular cap — the outer rim of a wheeling formation must not exceed the fastest member's linear speed), but multiplying by an undefined `FacingDeltaRate` leaves the term unitless-or-wrong depending on that symbol's meaning. As written, two implementers could build two different systems from the same line.

**Correction (applied).** Two explicit steps: (1) ωmax = MaxMemberSpeed / max(OuterRadius, ε) [rad/s]; (2) anchor linear limit v_wheel = ωmax × r_path(anchor), where r_path is the anchor's turning radius (outer radius for in-place pivot, path radius during a moving wheel). `FacingDeltaRate` defined as commanded facing-change rate [rad/s], applied as a separate linear cap `FacingDeltaRate × r_path(anchor)`. §12 row updated.

**Test impact:** T1.3 amended with golden cases for ωmax and v_wheel including degenerate radii (r → ε) and straight-march inactivity.

### E3 (High) — "Periodic exec-calc" is not a GAS execution model
**Location:** §10.5 morale contribution stack; §5.3 stat-stacking note; roadmap Phase 4, T4.1/T4.2.

**Finding.** GAS attribute aggregation runs an execution calculation when its captured inputs change — on gameplay effect application, removal, or aggregation invalidation — not on a clock. No engine behavior re-executes an exec-calc periodically on its own; periodicity exists only through period-based gameplay effects (the DOT/regeneration pattern) [19][20]. The morale stack contains terms that are functions of time and continuous sim state (engagement tallies change every tick; the fresh-casualty spike decays over seconds). A passive exec-calc produces stale `Morale` values between effect applications and cannot express spike decay at all.

**Correction (applied).** Explicit tick source, documented in §10.5: Option A (preferred, pure GAS) — a periodic, infinite-duration `MoraleTick` gameplay effect (period 0.25–0.5 s, §12 tunable) whose period execution runs the exec-calc over sim-fed SetByCaller inputs; Option B (hybrid) — sim-side accumulator applies an instant evaluation GE each period. Fresh-casualty spike decay is a sim-tracked term pushed in each period.

**Test impact:** T4.1 amended (setup drives the chosen tick mechanism); new **T4.8** — under load, Morale value age never exceeds one period.

### E4 (High) — Unpinned engine version; spawn API reflects pre-5.8
**Location:** document header (no version stated); §5.3 spawn path (`UMassSpawnerSubsystem::SpawnEntitiesFromConfig`); Phase 0 task list; ADR-11.

**Finding.** The plan named an API whose shape changed across 5.7/5.8 without stating a target engine. In the 5.8 documentation, `UMassSpawnerSubsystem`'s public spawning surface is template-based (`SpawnEntities` over `FMassEntityTemplate`), with the entity-template registry exposed alongside it [7][8]; the config-asset-named call corresponds to earlier releases. 5.8 additionally renamed batch element APIs (`AddElementToEntities` → `BatchAddElementToEntities`), unified command-buffer element commands, moved Mass Signals into the engine core, split out `MassCore`, and overhauled processor execution and dependency resolution [1]. Mass API churn between minor releases is documented fact, not hypothetical.

**Correction (applied).** Engine pinned to **UE 5.8.x** in both headers (terminal UE5 release — rides the most-overhauled, final form of the UE5 Mass stack [1][32]); ADR-13 added: re-baseline only at phase boundaries via a scripted API-diff audit (I6); §5.3 spawn call replaced with the 5.8 template-based entry point; a full symbol audit of every Mass/GAS name in §3–§5 is required before Phase 0 code (I6 automates it).

**Test impact:** T0.3 asserts against the pinned version's config/template resolution path; CI per-commit tier compiles against the exact pinned engine build.

### E5 (Medium) — Same-tick death visibility depends on unstated flush points
**Location:** §4 processor table row 11 ("Death — tag via cmd buffer"); §5.3 death path; T0.5.

**Finding.** Queuing the death tag on a command buffer and asserting same-tick removal from live queries holds only if the buffer flushes before the next consumer's query executes. Community code analysis and the API surface indicate Mass flushes command buffers when a processing context is destroyed (flush-on by default) and again at processing-phase end [12][13]. Whether T0.5's assertion held depended on which phase Death runs in, whether UnitAggregate runs in the same phase after a flush, and how the aggregate's query excludes pending-death entities — none of which the original plan stated. Also imprecise: a tag add/remove historically moves *individual entities* between chunks ("chunk granularity" overstated); 5.8's sparse tags remove the move entirely [1].

**Correction (applied).** Flush contract stated in §4 (architecture doc): verify the pinned 5.8.x flush discipline once in source and record it; Death either flushes explicitly before UnitAggregate, or T0.5 is re-scoped to "no later than end of phase, never visible to combat resolution in the following tick."

**Test impact:** T0.5 amended to assert buffer state directly post-Death processor, or end-of-phase semantics; T5.6's long soak guards against drift if engine upgrades change flush timing.

### E6 (Medium) — AnimToTexture dependency without a re-bake contingency
**Location:** ADR-10; §11.1 mid band; Phase 5.

**Finding.** The plan hard-depends on AnimToTexture for its mid band but treats it as stable. It is not: in 5.8 the plugin still lives under `/Engine/Plugins/Experimental/` [22], and the community has already been through one breaking transition — assets baked in 5.2 stopped animating correctly after upgrading to 5.4 because the plugin's data format and editor tooling changed [23][24]. A solo project that loses its bake pipeline to an engine upgrade faces re-authoring, not just re-baking, unless source assets are preserved.

**Correction (applied).** Phase 5 pre-tasks added: pin the plugin version with the engine pin; keep the bake pipeline re-runnable from source (skeletal clips + editor/cook-time bake script; baked textures never the only committed artifact); T5.4 nightly fails if mid-band VAT animation is frozen; fallback ladder documented (Niagara mesh-rendering VAT or hand-authored instanced flipbook).

**Test impact:** T5.4 tolerance policy amended from "crowd present per band" to "crowd present **and animating** per band."

## 5. Risks and Gaps (R1–R13) — mitigations scheduled in the roadmap

| ID | Sev. | Location | Finding & required decision | Land in |
|----|------|----------|------------------------------|---------|
| R1 | High | §5.3, §3.2 | **Sparse fragments are the cheaper death path, and the plan predates them.** 5.8's sparse/virtual fragments add/remove optional fragments and tags *without archetype changes*, with sparse-aware commands and observers throughout the command API [1]. Evaluate sparse tags for `FDeadTag` and combat-mode switches; mass death events during a routing collapse stop churning archetypes. Open decision 7 added to the architecture doc. | Phase 0 |
| R2 | High | §8.3, T1.5–T1.6 | **Mass Avoidance is force-based and Experimental.** Engine docs describe "a force-based avoidance system" outputting a steering force into `FMassForceFragment`, with an Experimental warning [10] — while §8.3 mandates velocity control. An explicit force→velocity conversion per state (mass/accel clamp, or weighted steering direction before the servo clamp) must be specified, with a conversion column in the state table. | Phase 1 |
| R3 | Med | T2.8, T4.7 | **Processor scheduling changes cut both ways.** 5.7 added processor time-slicing [4]; 5.8 overhauled execution/dependency resolution for multi-core [1]. Both are frame-stability wins but new nondeterminism sources for two-run hash tests unless the test-mode contract forces unsliced, full-detail execution. T0.9 asserts scheduling isolation against active settings. | Phase 0 wk1 |
| R4 | High | §8.2, Phase 1 | **The heightfield path planner is missing.** §8.2 begins "anchor on path" and §4 row 1 reads a "cost grid," but no task builds the planner that turns a Move order into that path without navmesh (ADR-08). Everything downstream (wheels, squeezes, corridor morphs, S1–S3) assumes a path exists. Added as a first-class Phase 1 deliverable: A*/jump-point search over the cost grid, slope-aware costs, path smoothing, path-frame sampler; gated by T1.9. | Phase 1 |
| R5 | Med | §2.1, §6.1, §11.5 | **Command/selection surface owned but unscheduled.** Selection/UI authority, the command set, and ISM-click picking are specified, but no phase exercises selection, order issuance, or the camera-to-order path. M1 needs a minimal shell: declared a thin Phase 1–2 deliverable with a smoke test, or explicitly scoped out of v1. | Phase 1–2 |
| R6 | Med | M3 | **M3 mixes Phase 2 geometry with Phase 4 morale.** The knights-vs-peasants demo's "morale pressure" half needs the Phase 4 stack. M3 now demos geometry-only at Phase 2 and re-runs enriched after Phase 4. | Phase 2 |
| R7 | Med | T6.3, Phase 6 | **Save/load arrives too late for schema stability.** A hash-equal save/load round-trip requires the entity schema to have been stable since Phase 2. A schema-versioned skeleton (registry runtime state + fragment payloads via rebuild-from-spec) now starts at Phase 2 and grows with the schema. | Phase 2 |
| R8 | Med | Part B tiers | **CI infrastructure implied but unplanned.** Per-commit/PR/nightly/pre-release gates from Phase 0/2 require a build farm, packaged-build orchestration (Gauntlet [31]), and baseline storage — now a Phase 0 deliverable (host choice, machine spec, versioned baseline JSONs). | Phase 0 |
| R9 | Low | T2.8, T4.7 | **Determinism claims need a same-machine qualifier.** Cross-machine/compiler float reordering (FMA contraction, fast-math) can diverge hashes without a real defect. Claims scoped to same-binary, same-machine in B.5. | Phase 2 |
| R10 | Low | B.6 | **Target hardware never stated.** Perf gates are meaningless without a machine spec; plausible on current-gen desktop given Mass's demonstrated scale [25][26], but a named reference machine with recorded specs is now required in every baseline JSON. | Phase 0 |
| R11 | Low | T0.2 | **Byte-identical EDU round-trip is over-strict.** EDU files carry comments, blank-line conventions, and formatting that parsed tables cannot faithfully regenerate [27][28]. T0.2 now compares canonical form against a canonical golden file; "unknown fields rejected loudly" remains the strictness point. | Phase 0 |
| R12 | Low | §9.3 | **Friendly-fire semantics deserve a cited decision.** The community record on M2TW projectile friendly-fire is murkier than for melee [29]. Since the doctrine is "port, don't invent," the rule is added to the Phase 6 formula audit as an explicit port-or-documented-deviation (T6.1). | Phase 6 |
| R13 | Low | §12, T5.5 | **Budget-table arithmetic shown once.** "~15–25 ISM draw calls" reconciles with "one draw call per mesh/material" only if ≤ 25 covers both armies' mesh variants plus corpse and projectile pools; the derivation is now recorded (§12 row + Phase 5 task) so T5.5 asserts a derived number. | Phase 5 |

## 6. Improvements (I1–I7)

| ID | Action | Priority | Effort | Benefit |
|----|--------|----------|--------|---------|
| I1 | Exploit/record 5.8 **off-game-thread entity creation** (archetype-based, lock-free scheduling [1]) for spawn/death bursts; even if v1 keeps game-thread-only, ADR-02 records it as a choice, not an engine limit | Med | M | Headroom under T6.2 perf gates |
| I2 | Adopt **Mass Insights + Mass Debugger** into the Phase 0 instrumentation; 5.8 tracing adds archetype-move events, sparse-element visibility, batched entity-move traces [1]; cross-check B.4 metrics instead of reimplementing; the debugger's archetype/entity views surface E1-class collisions visually | High | L | Catches E1-class defects; free metrics |
| I3 | Write **Iris-ready invariants** into B.8's definition of done (no raw pointers across the registry boundary; all unit state reconstructible from serializable sim state; RNG streams seed-registered). Iris is production-ready in 5.8 [1][15]; Epic continues toward Mass-on-Iris replication [14] | Med | L | Cheap multiplayer option preservation |
| I4 | Evaluate **StateTree** for the six-state unit FSM (part of the MassGameplay toolkit [11]; gained authoring flexibility in 5.8 [1]); half-day prototype in Phase 2 before OrderLatency gating accretes; ADR either way | Low | L | Editor debugging; avoids FSM rework |
| I5 | Add the tests this review implies: **T0.8** (shared-fragment uniqueness), **T0.9** (test-mode scheduling isolation), **T1.9** (path planner), **T4.8** (morale staleness) | High | M | Converts corrections into enforcement |
| I6 | Automate the **pinned-version API audit**: a script greps the codebase for every Mass/GAS symbol named in the architecture doc and diffs against the pinned engine's API reference; reruns at every engine upgrade; future-proofs the UE6 transition | Med | L | Docs/code never drift |
| I7 | **Document-control hygiene**: version/revision tables, engine pin, hardware target, ADR change-log; ADR-13/14 added; applied to both file headers | High | L | Plan stays authoritative as code evolves |

## 7. Engine Version Landscape (verified 2026-10-23)

| Release | Date | Mass-relevant facts used by this review |
|---------|------|------------------------------------------|
| UE 5.6 | 2025-06-03 [33] | Baseline for MassSample's current README [5] |
| UE 5.7 | 2025-11-12 [3][4] | Mass processor **time-slicing** (long processors split across frames); Mass low-level optimizations — a nondeterminism source under test (R3) |
| UE 5.8 | 2026-06, **final UE5 release** [1][2][32] | Mass overhaul: **MassCore** module split; **Mass Signals into engine core**; **off-game-thread entity creation** via archetype-based, lock-free scheduling; **sparse/virtual fragments** (optional fragments/tags without archetype changes); processor execution & dependency resolution overhaul; batch element API renames; shared-fragment observers; extended MassInsights/tracing. **Iris production-ready** [1][15]. AnimToTexture still Experimental [22] |

## 8. Corrected Reference Snippets

**8.1 Wheel-rate speed limit (fixes E2)** — units explicit: v [m/s], ω [rad/s], r [m]:

```cpp
// Angular cap from wheel geometry: outer rim speed must not exceed the fastest member
const float OmegaMax = MaxMemberSpeed / FMath::Max(OuterRadius, KINDA_SMALL_NUMBER);  // rad/s

// Anchor linear speed limit while wheeling: v = w * r_path(anchor)
const float WheelLinearLimit = OmegaMax * FMath::Max(PathRadiusOfAnchor, KINDA_SMALL_NUMBER);  // m/s

// FacingDeltaRate is the COMMANDED facing-change rate in rad/s (independent cap):
const float FacingLinearLimit = FacingDeltaRate_rad_s * FMath::Max(PathRadiusOfAnchor, KINDA_SMALL_NUMBER);

UnitSpeedLimit = Min3(CommandedSpeed,                       // order intent
                      SlowestMemberSpeed * CatchupFactor,   // slowest man (1.25x)
                      Min(WheelLinearLimit, FacingLinearLimit));
```

**8.2 Shared-fragment creation with guaranteed per-unit uniqueness (fixes E1)**:

```cpp
// UnitSync (game thread): derive stats, then publish to the unit's shared fragment.
// (1) The unit handle is part of the payload -> two units can never be value-identical.
FUnitStatsSharedFragment Stats;
Stats.UnitHandle  = Handle;              // FUnitHandle{Index, Serial} - uniqueness anchor
Stats.SpeedMult   = Derived.SpeedMult;   // ... remaining derived multipliers ...

// (2) Seed the creation hash with the handle so dedup can never merge two units,
//     even if a future refactor drops the handle from the payload.
const uint32 Seed = HashCombine(Handle.Index, Handle.Serial);
EntityManager.GetOrCreateSharedFragmentByHash<FUnitStatsSharedFragment>(Seed, Stats);

// (3) Unit test T0.8: spawn N units with identical stats; assert N distinct instances;
//     assert a change to unit A's derived stats is not visible on unit B in the same tick.
```

**8.3 Morale stack with an explicit tick source (fixes E3)** — Option A shown (pure GAS); Option B substitutes a sim-side accumulator applying an instant evaluation GE each period; either way the clock is explicit and tested:

```cpp
// One-time setup (unit actor): infinite, periodic morale re-evaluation effect
// MoraleTickGE: DurationPolicy = Infinite, Period = 0.25 s (tunable in the Section 12 table)
FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(MoraleTickGE_Class, 1.f, CtxHandle);
ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

// The GE's period execution runs an exec-calc (AggregatorExecuteLogic) that captures:
//   base mental, casualty fraction, fresh-casualty spike (sim-decayed value via SetByCaller),
//   engagement odds (registry tallies), arc exposure, routing friends, general proximity/alive,
//   height advantage, local winning state -> writes the Morale meta-attribute.
// Fresh-casualty spike decay is a SIM-side exponential decay pushed to the GE each period;
// GAS has no immanent time decay for exec-calc inputs.

// Test T4.8: under load, Morale value age must never exceed one GE period.
```

## 9. Counterarguments — the plan's contested choices, defended

**Building on Mass at all.** The most attackable decision: several Mass plugins remain Experimental, Epic's public posture toward the framework has been restrained, and 5.8's overhaul proves API churn is real [1][5]. The counterargument, which this review endorses, is threefold. First, production evidence exists at the highest level: Epic's own City Sample ships Mass-simulated crowds as a reference implementation and was re-validated against 5.8 in August 2026 [25][26]. Second, the architecture deliberately minimizes API-churn blast radius — all Mass interaction funnels through thin processors, a pure-math seam, and one registry boundary, so a breaking engine change touches shims, not simulation logic. Third, pinning 5.8 (E4) targets the terminal, most-stabilized UE5 release; the migration hazard moves beyond the project's foreseeable horizon. A hand-rolled ECS would trade that risk for a worse one: reimplementing, alone, the scheduling, LOD, representation, and tooling Epic has shipped.

**No-navmesh heightfield movement (ADR-08).** Detour navmeshes and the ZoneGraph are the supported movement paths. But Total War-style formation movement is exactly the case navmeshes handle badly — anchors, wheels, ranks, and spacing are formation properties, not per-agent steering properties — and the unit-as-locomotor split (ADR-07) is the established fix for the classic failure modes the plan lists. The cost is owning a path planner (R4), absorbable in Phase 1 now that the gap is named.

**GAS for forty units.** Skeptics note forty units barely justify an ability system. The counterargument: GAS's exec-calc discipline, effect catalog, and attribute delegates give the morale/fatigue/buff stack a tested implementation path [16][19], and the hybrid design quarantines GAS's costs to the cheap side of the registry. The E3 correction fixed the one place the plan asked GAS to do something it does not do.

## 10. Prioritized Action Sequence (as applied)

1. **Pre-Phase 0 (done in plan text, enforce in code):** engine pinned 5.8.x (ADR-13); ADR-14 adopted; E1 correction (handle-in-payload + seeded hash); E4 spawn API replaced.
2. **Phase 0, week one:** E2 wheel-rate golden tests; T0.8, T0.9; T0.5 flush-point assertion; T0.2 canonical comparison; API audit script (I6).
3. **Phase 0 infra:** CI tiers stood up for real — automation host, packaged Gauntlet nightly, baseline storage, named reference machine (R8, R10).
4. **Phase 1:** path planner first-class with T1.9 (R4); force→velocity conversion column (R2); T1.3 corrected cases; command-shell decision (R5).
5. **Phase 2:** StateTree prototype + ADR (I4); save/load skeleton (R7); M3 geometry-only (R6); T2.8 same-machine scope (R9).
6. **Phase 4:** morale stack on the explicit tick mechanism with T4.8 (E3).
7. **Phase 5:** AnimToTexture pinned with re-bake-from-source and frozen-VAT nightly check (E6); draw-call derivation (R13).
8. **Phase 6:** friendly-fire rule resolved as port-or-documented-deviation (R12).
9. **Ongoing:** API audit rerun at every engine upgrade (I6); metrics cross-checked against MassInsights (I2); Iris-ready invariants in definition of done (I3).

## 11. Limitations of This Review

1. **Documents only.** No source code, no prototypes; implementation-level errors beyond what the documents state are out of scope, and the documents may understate correctness handled informally by the implementer.
2. **Documentation drift.** Statements reflect 5.8-era documentation as published and accessed on 2026-10-23. A reader applying this after the next engine release must re-verify the version-sensitive findings (E4, E5, R1, R3, E6) against then-current notes, per ADR-13.
3. **Community constants pending.** M2TW hit-chance clamps, fatigue tables, and shield arcs live in forum threads of varying rigor [27]–[30]; the plan's placeholder-then-audit posture is correct, but the audit itself (Phase 6) remains future work outside this review.
4. **Performance verdicts are feasibility judgments** from published scale evidence (City Sample [25][26]), not benchmarks; the plan's own perf gates (B.6) executed on pinned hardware (R10) are the authoritative arbiter.

## 12. References

All sources accessed **2026-10-23**. Tier A = official Epic Games documentation/release notes; Tier B = canonical community references and Epic staff statements; Tier C = context/domain-history.

| # | Source | Tier | URL |
|---|--------|------|-----|
| [1] | Epic Games. "Unreal Engine 5.8 Release Notes." (Mass overhaul; sparse fragments; MassCore; Iris production-ready; StateTree changes) | A | https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=en-US |
| [2] | Epic Games. "Unreal Engine 5.8 is now available." June 2026 | A | https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available |
| [3] | Epic Games Forums. "Unreal Engine 5.7 Released." 2025-11-12 | A | https://forums.unrealengine.com/t/unreal-engine-5-7-released/2673913 |
| [4] | T. Looman. "Unreal Engine 5.7 Performance Highlights." 2025-11-12 (Mass processor time-slicing) | B | https://tomlooman.com/unreal-engine-5-7-performance-highlights |
| [5] | Megafunk (K. Mavko), vorixo (A. Jover). "MassSample." GitHub — shared-fragment CRC/hash dedup semantics, subsystem guidance, command buffers, spawning (built for 5.6/5.7) | B | https://github.com/Megafunk/MassSample |
| [6] | Epic Games. "FMassEntityHandle" API reference, 5.8 (Index + SerialNumber semantics) | A | https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassCore/FMassEntityHandle?lang=en-US |
| [7] | Epic Games. "UMassSpawnerSubsystem" API reference, 5.8 | A | https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/MassSpawner/UMassSpawnerSubsystem?lang=en-US |
| [8] | Epic Games. "UMassSpawnerSubsystem::SpawnEntities" API reference, 5.8 (template-based spawning) | A | https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/MassSpawner/UMassSpawnerSubsystem/SpawnEntities?lang=en-US |
| [9] | Epic Games. "UMassSignalSubsystem" API reference, 5.8 | A | https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassSignals/UMassSignalSubsystem?lang=en-US |
| [10] | Epic Games. "Mass Avoidance Overview in Unreal Engine." (force-based avoidance via `UMassMovingAvoidanceProcessor` / `FMassForceFragment`; Experimental notice) | A | https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine?lang=en-US |
| [11] | Epic Games. "Overview of Mass Gameplay in Unreal Engine." (representation, spawning, LOD, replication, StateTree integration) | A | https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-gameplay-in-unreal-engine?lang=en-US |
| [12] | r/UnrealEngine5. "Defer() in Mass Framework." Community code analysis of command-buffer flush timing (`FMassProcessingContext` destructor flush; `FMassProcessingPhaseManager::OnPhaseEnd`) | B | https://www.reddit.com/r/UnrealEngine5/comments/1pgntv3/defer_in_mass_framework |
| [13] | Epic Games. "FMassEntityManager::FlushCommands" API reference, 5.8 | A | https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/MassEntity/FMassEntityManager/FlushCommands?lang=en-US |
| [14] | Epic Games Forums. "MassAI Replication State in 5.7 and future roadmap?" 2025-11 — Epic staff statement on Iris-based Mass replication investigation | A | https://forums.unrealengine.com/t/massai-replication-state-in-5-7-and-future-roadmap/2684012 |
| [15] | Epic Games. "Migrate to Iris in Unreal Engine." | A | https://dev.epicgames.com/documentation/unreal-engine/migrate-to-iris-in-unreal-engine |
| [16] | Epic Games. "Gameplay Ability System for Unreal Engine." | A | https://dev.epicgames.com/documentation/unreal-engine/gameplay-ability-system-for-unreal-engine?lang=en-US |
| [17] | Epic Games. "Understanding the Unreal Engine Gameplay Ability System." | A | https://dev.epicgames.com/documentation/unreal-engine/understanding-the-unreal-engine-gameplay-ability-system?lang=en-US |
| [18] | Epic Games Community. "Gameplay Ability System — Best Practices for Setup." 2025-02 | A | https://dev.epicgames.com/community/learning/tutorials/DPpd/unreal-engine-gameplay-ability-system-best-practices-for-setup |
| [19] | tranek. "GASDocumentation." GitHub | B | https://github.com/tranek/GASDocumentation |
| [20] | Epic Games Community. "Durational Gameplay Effects." (period-based effects; DOT/regeneration patterns) | A | https://dev.epicgames.com/community/learning/tutorials/b6WV/unreal-engine-durational-gameplay-effects |
| [21] | Stack Overflow. "Unreal GAS: attribute change delegation." (`GetGameplayAttributeValueChangeDelegate` semantics) | B | https://stackoverflow.com/questions/53036364/unreal-gas-print-out-the-current-value-of-an-attribute-to-ui-when-it-changes |
| [22] | Epic Games. "AnimToTexture" plugin API reference, 5.8 (location: `/Engine/Plugins/Experimental/AnimToTexture`) | A | https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AnimToTexture?lang=en-US |
| [23] | unrealcode.net. "Animation Textures Part 2 — Using the AnimToTexture plugin." 2024-12 (plugin changed in 5.4) | B | https://www.unrealcode.net/AnimationTextures |
| [24] | Epic Games Forums. "Anim to Texture in 5.4." 2024-08 (community report: baked VAT assets breaking on 5.2→5.4 upgrade) | B | https://forums.unrealengine.com/t/anim-to-texture-in-5-4/1980839 |
| [25] | Epic Games. "City Sample Project Unreal Engine Demonstration." (The Matrix Awakens; Mass AI crowds) | A | https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration?lang=en-US |
| [26] | Epic Games. "City Sample gets a major update with PCG and Unreal MCP workflows." 2026-08 (City Sample updated for UE 5.8) | A | https://www.unrealengine.com/learning/city-sample-gets-a-major-update-with-pcg-and-unreal-mcp-workflows |
| [27] | HeavenGames. "A guide to the export_descr_units.txt file — Medieval 2." (EDU field reference) | B | https://medieval2.heavengames.com/m2tw/mod_portal/tutorials/export_descr_units_file_guide |
| [28] | The Org Forums. "Guide: The Complete EDU Guide (WIP)." | B | https://forums.totalwar.org/vb/showthread.php/88859-Guide-The-Complete-EDU-Guide-(WIP) |
| [29] | The Org Forums. "How does the combat system work in Medieval 2 Total War?" 2014-05 (defense composition; AP halving; 1-HP infantry) | B | https://forums.totalwar.org/vb/showthread.php/147297-How-does-the-combat-system-work-in-Medieval-2-Total-War |
| [30] | Total War Center. "Armies damage/armor calculations." 2010-01 | B | https://www.twcenter.net/threads/armies-damage-armor-calculations.328156 |
| [31] | Epic Games. "Gauntlet Automation Framework Overview in Unreal Engine." | A | https://dev.epicgames.com/documentation/unreal-engine/gauntlet-automation-framework-overview-in-unreal-engine?lang=en-US |
| [32] | Wikipedia. "Unreal Engine 5." (version history; 5.8 as final UE5 release; UE6 successor) | C | https://en.wikipedia.org/wiki/Unreal_Engine_5 |
| [33] | Epic Games. "Unreal Engine 5.6 is now available." 2025-06-03 | A | https://www.unrealengine.com/news/unreal-engine-5-6-is-now-available |
| [34] | Epic Games. "Storing Custom Data in Materials Per-Primitive." (custom primitive data float budget) | A | https://dev.epicgames.com/documentation/unreal-engine/storing-custom-data-in-unreal-engine-materials-per-primitive?lang=en-US |
| [35] | Epic Games Community. "Designing Scalable Crowds with Mass AI — a comprehensive UE guide." 2025-12 | A | https://dev.epicgames.com/community/learning/tutorials/zqZZ/unreal-engine-epic-for-indies-designing-scalable-crowds-with-mass-ai-a-comprehensive-ue-guide |
