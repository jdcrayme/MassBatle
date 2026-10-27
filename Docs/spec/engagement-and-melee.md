# Engagement Solver & Melee -- Design Specification

| | |
|---|---|
| **Companion docs** | `Docs/spec/architecture.md` (parent design: §3.1, §4, §6.2-6.3, §7, §9.1-9.2, §12; ADR log) · `Docs/spec/roadmap-and-testing.md` (Phase 2 scope, T2 gate table, B.0-B.8) · `Docs/AGENTS.md` (standing protocol) |
| **Status** | Draft 1 · 2026-10-27 · owner review pending. Elaborates the architecture sections above into the Phase 2 implementation contract. On conflict, the architecture doc wins (AGENTS.md precedence); corrections land per AGENTS §4. |
| **Engine pin** | UE 5.8.4 (ADR-13) |
| **Audience** | Phase 2 brief author and the implementing agent (cited sections only; token discipline per AGENTS §1) |
| **Owner-approval status** | §8 ADR rows are PROPOSALS pending the owner's architecture-log commit (AGENTS §7; garrison precedent ADR-18..21) |

---

## 0. Behavior contract (the acceptance table)

One system produces both formed clashes and mobs; every row below is observable in sim state (tallies, budgets, fragments, hashes), never in visuals. Roadmap scenario asserts (B.3, S4/S5) and gate T2.x rows cite these.

| ID | Behavior | Contract |
|----|----------|----------|
| EC1 | Formed clash | Two formed units meeting frontally stabilize a fight line at contact: attacker lanes fill to `FormedSlots` per defender (frontage segments, lane scan), waiters press behind, and both sides take casualties at contested-roll rates. No interpenetration beyond the contact ring; no pairing of non-contacting units. |
| EC2 | Shield-wall melt curve | While defender coverage holds, per-defender budget stays at `FormedSlots` (line behaves as a wall). As casualties thin the line, coverage drops, per-defender budget rises toward `MaxMobSlots`, and collapse accelerates. The curve shape is asserted (T2.4), not just the endpoint. |
| EC3 | Mob pull-down | An isolated defender accumulates attackers up to its exposure budget on exposed sectors (>= 4 simultaneous exposed-arc attackers for the knight test), is pulled down within the expected time band, and dies to geometry. Zero unit-type or count special cases anywhere in the solver. |
| EC4 | Waiter discipline | Unslotted attackers press to the contact ring (radii + weapon reach, never standoff queue distance), mill with RVO inside the mob radius only, never orbit (carousel). Slot handoff <= 1 tick after release; failed seeks back off 1-3 ticks, jittered. |
| EC5 | Pair lifecycle | Pairs release on target death, stale handle, or unit disengage; distance breakup fires only beyond the break radius plus 0.3-0.5 s hysteresis; vacated slots refill by attackers advancing (the advance IS the handoff, no special case). |
| EC6 | Directional defense | The same defender's survival differs by attack vector: defense skill applies on all arcs (reduced on flank/rear), shield contributes on frontal arcs only, armor is halved against AP weapons, and spear-class reach grants second-rank attackers through the front rank. |
| EC7 | Charge | Charge bonus applies only on fresh contact at speed, decays to normal melee within the charge window, applies a bounded knockback impulse at impact, and charging entities bypass pairing until impact seeds their first pair. |
| EC8 | Determinism | Same seed, same binary, same machine: per-tick state hash identical across two 5-minute clash runs (R9 scope; cross-platform float reordering out of scope). |
| EC9 | Solver budget | Tally + assign together < 0.5 ms game thread at 1,000 entities all in contact; zero per-tick heap allocations in the steady fight (counting sort, preallocated scratch, open-address tables). |

## 1. Scope and non-goals

In scope (Phase 2): spatial hash, engagement records and cluster graph, tally-assign passes, exposure budgets and coverage, press-ring waiters, pair lifecycle, melee resolution with directional defense, charge state machine, unit FSM decision (StateTree prototype, I4), save/load skeleton (R7), M3 demo as geometry-only (R6).

Out of scope (recorded, not accidental):

- Ranged combat of any kind (Phase 3; projectiles bypass pairing and seed pairs at impact -- §10).
- Morale, fatigue effects, routing, rally, and charge morale shock (Phase 4; §10 records the hooks).
- Melee-vs-garrison: garrisoned interiors are not valid melee targets this phase (owner decision; garrison report §4.2 extension points stand -- slot cover/arc metadata and booking classification are the seam).
- `Withdraw` command flavor and `OrderLatency` gating (Phase 4); Phase 2 disengage = re-task with Move/Stop while engaged.
- Impetuous pursue overrides, guard/fire-at-will toggles (Phase 4).
- Cross-platform/cross-binary determinism (ADR-11 hooks-only posture, R9).
- Animation-driven combat (forbidden by ADR-05; combat is sim-driven on timers, animation is cosmetic and later).

## 2. Architecture summary

**The one-sentence claim: engagement is registry-owned bookkeeping over unit geometry, and melee is one worker-side resolution pass over per-entity pair fragments -- a combat extension of the existing movement spine, not a new system.**

Pipeline placement (architecture §4 rows; processor names are binding):

```text
game thread, per tick:   UnitSync -> UnitLocomotion -> BuildSpatialHash -> EngagementClusters (4 Hz)
                              -> Tally -> AssignPairs
worker, per tick:         FormationSlotResolve -> Servo -> Integrate -> Charge -> CombatResolution
command buffer:           Death (existing Phase 0 path) -> UnitAggregate (existing)
```

- Rows 2-5 are new game-thread processors; rows 9-10 (melee half of 9, row 10) are new worker processors. `RangedFire` (the other half of row 9) is Phase 3.
- The tally/assign split exists so the assign pass reads one coherent per-defender count table instead of racing itself (architecture §7.2); both run in `ExecuteInGroup = "Battle"`, after `UnitLocomotion`, before `FormationSlotResolve`.
- The existing movement processors are untouched except for one consumption point each: `ServoProcessor` consumes pair state to pick movement state and the press-ring target (§4.6), and accepts the knockback impulse from `ChargeProcessor` through the existing displacement blend (`FServoStateFragment::DisplacementWeight`, phase 1).
- Unit AI is three layers (architecture §6.2): unit picks enemy UNIT (orders + FSM) -> solver picks entity PAIRS (assign pass) -> entities execute (servo + resolution). Nothing skips a layer.

## 3. Data model

### 3.1 Per-entity combat fragments (new file `Plugins/MassBattle/Source/MassBattle/Public/BattleCombatFragments.h`)

Blittable rules per architecture §3.1 (no `FString`, no `UObject*`, no `TArray`); fields are handles, fixed scalars, enums.

| Fragment | Fields (first pass) | Written by |
|----------|--------------------|-----------|
| `FCombatIntentFragment` | `FUnitHandle TargetUnit`; `uint8 LaneIndex`; `uint8 bAutoEngage` (AttackMove) | order surface (game thread) + FSM |
| `FCombatPairFragment` | `FMassEntityHandle Target`; `uint8 ContactAngle` (quantized sector, 0-255 -> full circle); `ECombatPairState State` (Seeking / Pressing / Striking / Recovering); `float AttackTimer`; `float PairAge`; `float ChargeBonusRemaining` | assign pass (game thread); timers advanced by resolution (worker) |
| `FChargeFragment` | `float DistanceTraveled`; `float ImpactSpeed`; `uint8 bConnected` | charge processor (worker) |
| `FHealthFragment` (existing, `BattleFragments.h`) | extends: `int16 MountCurHP` (0 for foot); damage stays queued-scalar | combat resolution |

Design notes:

- `ContactAngle` is one value with two consumers (architecture §7.3): contact-ring steering and directional defense lookup. Quantized once at assign time; the defense arc bands are compared against it, never re-derived.
- Pair state lives per entity, not per unit; the unit-level picture is the engagement record (§3.2). This is the ADR-06 data-model consequence: budgets are exposure-driven geometry, so the data that varies per attacker-defender adjacency is per-entity.
- No new shared fragments. Engagement data that is per-unit is registry-owned (§3.2); if a future phase needs an entity-visible unit-mode broadcast, `FUnitStateSharedFragment` (architecture §3.1) is the designated home -- it stays unbuilt until a consumer exists (open note, §10).
- Zero new tags this phase; pair/charge state are fragment fields. `FRoutingTag` and ranged tags belong to Phase 3-4. Archetype count target: unchanged from the garrison-phase baseline of 6 (composition of the uniform spine grows, count does not).

### 3.2 Registry-owned engagement state (`BattleRegistry.h/cpp`)

Game-thread written, worker never touches; plain structs only (Iris-ready per B.8 item 7).

- `FEngagementRecord` per (attacker unit, defender unit), recomputed at 4 Hz:
  - `EEngagementMode Mode` (Frontal / Flank / Mob);
  - `FrontageStart`, `FrontageEnd` (segment parameters of the defender line; flanks wrap the end);
  - `int32 DesiredPressure` -- frontal = `defenderAlive * FormedSlots`; mob = `Min(attackerAlive, Max(defenderAlive * MobPressureFactor, MaxMobSlots))`. The `MaxMobSlots` floor is the T2.5 resolution (brief A14): with 1-2 defenders the unfloored `defenderAlive x 2-3` cap would admit only 2-6 total attackers and starve the isolated-knight mob test (>= 4 exposed-arc attackers on one knight); large units are unaffected (attackerAlive binds first). Amendment to architecture §6.3 recorded for the owner's log reconciliation; the factor stays tunable (`2-3` range);
  - `int32 AssignedAttackers` -- tallied fresh each tick (never persisted across ticks).
- Scratch (preallocated, per battle, reset not reallocated): open-address tally table keyed by defender entity handle; per-defender coverage cache with a valid-tick stamp; lane scan cursor arrays.
- Cluster graph: flood-fill over dilated unit contact envelopes (the phase-1 `FUnitOBB` machinery), recomputed at 4 Hz; records are independent so per-cluster sharding stays the scaling escape hatch (architecture §6.3).
- Unit-level engagement bookkeeping on `FUnitRuntimeState`: `EngagedCount`, `ChargingCount` (aggregate inputs for the FSM and metrics).

### 3.3 Spatial hash subsystem (new `BattleSpatialHash.h/cpp`)

`UBattleSpatialHashSubsystem` (`UWorldSubsystem`, game thread):

- Uniform grid, 3 m cells over the battle tile (matches cost-grid footprint, 256 x 256 m flat tile, phase 1).
- Entry: `{ FMassEntityHandle Entity; FUnitHandle Unit; uint8 Flags; uint8 RadiusCm; uint8 ReachClass; }` -- packed, blittable, sorted by cell.
- Rebuild each tick from end-of-last-tick transforms: two-pass counting sort into preallocated arrays; zero per-tick allocation (EC9).
- `QueryCircle(Center, Radius, OutEntries)` -- used for coverage and mob targeting ONLY. Formed-fight targeting reads defender slot tables (data, not proximity; architecture §7.1). This is the load-bearing distinction: lanes never do circle queries.
- Radius/reach classes come from the loadout (EDU weapon class), stamped at spawn.

### 3.4 State-hash terms

Pair/charge terms append AFTER the garrison terms in the same sorted-handle pass (`BattleStateHash.cpp`; garrison brief F9 pattern): pair target handle, quantized contact angle (1/255), pair state, attack timer (1e-2 s), charge distance/impact (1 cm / 1e-2 m/s), mount HP. Deaths already hash (per-entity HP/dead are in since phase 0); the archetype count term catches any accidental archetype growth.

### 3.5 Save/load schema v1 (the R7 skeleton)

- `FBattleSaveData` (plain struct, `BattleSave.h`): `uint32 SchemaVersion = 1`; registry runtime snapshot (roster, orders, engagement records, bookings); per-unit shared-fragment payloads (stats + formation, fixed arrays); RNG stream states (seed-registered per B.5).
- Entities are NOT serialized: restore = rebuild-from-spec through the existing spawn library (`BattleSpawnLibrary`, ADR-17 Route A), then overwrite fragment payloads. This is why the skeleton grows with the schema instead of accreting a parallel entity serialization.
- Registry API: `CaptureBattleState(OutFBattleSaveData)` / `RestoreBattleState(InFBattleSaveData)`; both game thread.
- Phase 2 asserts the skeleton round-trip at registry level (capture -> clear -> restore -> identical state hash, no entities required). Full save -> load -> resume with pairs/corpses/morale is gate T6.3 (Phase 6 maturity, by design).

## 4. Mechanics

### 4.1 Spatial hash rebuild

Game thread, start of combat bookkeeping, from end-of-last-tick transforms (never mid-pass). Counting sort: pass 1 counts per cell, prefix sum, pass 2 places. Rebuild is O(n); queries are O(entries-in-circle). Determinism note: iteration order within a cell is placement order = sorted-by-handle if entities are placed in handle order (they are -- the counting-sort source array is built handle-sorted); no hidden iteration dependency.

### 4.2 Cluster graph and engagement records

- Every 0.25 s (tunable, `Battle.Combat.ClusterPeriod`): dilate each unit's OBB by the engage distance (radius sum + reach, one scalar), find overlapping enemy pairs, flood-fill to clusters, emit `FEngagementRecord`s.
- Mode: Frontal if the attacker's approach axis crosses the defender frontage interior; Flank if it wraps a defended end; Mob if the defender is surrounded on >= 3 sectors or the defender unit is not in formation (scattered / isolated).
- Frontage: attacker files subdivide the defender-line segment assigned to the attacking unit; `FCombatIntentFragment.LaneIndex` (one lane per attacking file) restricts candidate targets -- the in-lane scan walks the defender slot table within the lane's slice (data, not proximity).

### 4.3 Tally pass

Iterate live `FCombatPairFragment`s once; count attackers per defender into the scratch open-address table; refresh the per-defender coverage cache (stamp = current tick). Everything derived fresh each tick -- no state to desync (architecture §7.2). Coverage itself is computed here (from the spatial hash, §4.5) only for defenders with at least one inbound pair or seeker (the perf scope cut).

### 4.4 Assign pass (single game-thread pass, the pseudo-code from architecture §7.2, elaborated)

```cpp
// per seeker (entities of units with an intent toward a live enemy unit):
if (Pair.Target.IsValid() && !IsValidTarget(Pair.Target)) Release(Pair);   // died / stale serial
if (BeyondBreakDistance(Pair) && Pair.PairAge > Hysteresis) Release(Pair);
if (Pair.Target.IsValid()) continue;                                        // keep
const FEngagementRecord& Rec = Registry.GetEngagement(OwnerUnit, Intent.TargetUnit);
if (Rec.AssignedAttackers >= Rec.DesiredPressure) { Pair.State = Pressing; continue; }   // unit-level cap
FMassEntityHandle T = (Rec.Mode == EEngagementMode::Mob)
    ? FindMobTarget(Hash, Rec)                     // circle query, exposed-sector preference
    : FindLanedTarget(DefenderSlotTable, Rec, Intent.LaneIndex);   // in-lane scan
if (!T.IsValid()) { Pair.State = Pressing; continue; }                      // press the line anyway
const float Cov = CachedCoverage(T);
if (Scratch.Tally(T) >= SlotBudget(T, Cov, Attacker)) continue;             // per-defender budget
Pair.Target = T;
Pair.ContactAngle = PickExposedAngle(T, Cov);
Pair.State = ECombatPairState::Pressing;
Pair.AttackTimer = StaggeredDelay(Entity);       // handle-hashed phase offset
Scratch.Tally(T)++;  Rec.AssignedAttackers++;
```

- Seeker iteration: shuffled per tick from the registered stream `Combat.Assign` (`BattleRngRegistry`); the determinism mode (B.5 compile-time flag) switches to index-sorted iteration with per-entity jitter. Never `FMath::Rand`.
- Release rules are checked top-of-pass every tick; releases are immediate (same pass), so vacated slots are visible to later seekers in the same tick -- the <= 1 tick handoff (EC5) falls out of the structure.
- Failed seeks (budget full, unit cap full) back off 1-3 ticks (jittered from the same stream) before re-seeking; waiters keep pressing meanwhile (§4.6).

### 4.5 Budgets, coverage, exposed angles (the mobbing model; architecture §7.3 verbatim)

```cpp
int32 SlotBudget(const FDefenderView& D, float Coverage, const FAttackerView& A)
{
    int32 B = FMath::Lerp(MaxMobSlots /*6-8, tunable*/, FormedSlots /*2*/, Coverage);
    if (A.ReachClass >= EWeaponReach::Spear) B += SecondRankBonus;   // spears strike through the front rank
    return FMath::Max(1, B);
}
float ComputeCoverage(const FDefenderView& D)   // same-unit living neighbors, spatial hash
{
    const int32 N = SameUnitLivingNeighborsWithin(D.Pos, D.Spacing * 1.25f);
    return FMath::Min(1.f, float(N) / float(NeighborsForFullCoverage));
}
```

- `PickExposedAngle(D, Cov)`: quantize the defender's surroundings into `SectorCount` sectors (8-12, tunable; 8 default); mark a sector covered if a living same-unit neighbor sits in it; fill exposed sectors first, covered arcs open only when exposed are full (asserted by T2.2's invariant battery).
- T2.2 bound reading: budget >= 1 always; upper bound = `MaxMobSlots` for non-spear attackers (the roadmap gate's [1, MaxMob] phrasing) and `MaxMobSlots + SecondRankBonus` for spear-class (the architecture formula's explicit add -- the bonus is geometry, not a bound violation).
- The defense arc bands (§4.8) read the same sector value -- one number, two consumers.
- Emergent properties are the point (ADR-06): casualty -> coverage drop -> budget increase (EC2); opened mob budgets mid-pass-through (vulnerability windows for Phase 3 to exploit).

### 4.6 Waiter behavior (press ring)

- Contact-ring radius = `DefenderRadius + AttackerRadius + WeaponReach` -- never a standoff queue distance.
- Unslotted attackers (`State == Pressing`) servo to the ring point at their `ContactAngle`; inside the mob radius they mill with the existing RVO state (`EBattleMovementState::PressRing`, RVO within mob radius only, phase-1 weights). The ring point tracks the defender each tick (target reads the pair target's transform).
- Slot handoff <= 1 tick after release (structural, §4.4); attack timers keep independent phase offsets -- combat is never serialized (ADR-05).

### 4.7 Pair lifecycle

- Release: target death (validation, §4.4), stale handle, unit disengage (records flagged dead -> mass release of inbound pairs, same pass), distance breakup only beyond break radius AND `PairAge > Hysteresis` (0.3-0.5 s, tunable).
- Vacated-slot handoff: attackers advance into the slot (it IS the handoff); asserted at unit tier with synthetic deaths and functionally in T2.7.
- Charging entities bypass pairing until impact seeds their first pair (§4.9).
- Rout behavior (budgets bypassed with a perf cap, chase state, rear-arc resolution) is Phase 4 -- the release rules above do not special-case routers yet; the `FRoutingTag` seam is recorded in §10.

### 4.8 Melee resolution (worker, `CombatResolutionProcessor`)

Attacker-side rolls on sim timers; the defender holds no combat authority (ADR-05 extension -- no defender-driven serialization, stable under LOD). Per attack-timer expiry:

```cpp
// structure is the community model; CONSTANTS ARE PLACEHOLDERS (A6) --
// golden-locked at T2.1, swapped by the Phase-6 T6.1 community-formula audit.
float A = BaseAttack                       // FBaseStatsFragment (EDU stat_pri/sec)
        + ChargeBonusRemaining              // §4.9, decaying
        * StatsShared.MeleeAttackMult;     // identity (1.0) until GAS effects land (Phase 4)
float D = DefenseSkill * SkillArcMult(Arc)  // all arcs; flank/rear reduced
        + Shield   * ShieldArcWindow(Arc)   // frontal arcs only
        + Armor    * (bArmorPiercing ? 0.5f : 1.0f);
float P = FMeleeMath::HitChance(A, D);      // contested curve, floor/cap clamped
bool  bLanded = StreamCombatRoll.Roll() < P * Lethality;
```

- HP model: humans are 1 HP (a landed hit kills); cavalry is a composite entity -- effective HP pool = rider (1) + `MountHP`, landed hits drain 1 from the pool, entity dies at 0 (documented simplification: no riderless mounts v1; recorded in §10 for the audit).
- Damage flows through `FHealthFragment` queued damage into the EXISTING Phase 0 death path (HP <= 0 -> `FDeadTag` via command buffer -> signal -> `HandleUnitDeath`, which is where registry-side cleanup including engagement tallies hooks -- the garrison F14 pattern).
- Attack cycle: `Striking` (roll) -> `Recovering` (cooldown) -> repeat; interval tunable, per-entity staggered phase offsets (handle-hashed at assign, §4.4). Timers advance on the worker with fixed DT under the test-mode contract (B.0).
- Death-by-arc bookkeeping (frontal/flank/rear tallies in `UBattleDebugSubsystem`) makes EC6 observable in metrics without visuals.

### 4.9 Charge state machine (worker, `ChargeProcessor`)

- Per-entity state: `Charging` (accelerate toward impact point; movement state `Charge`, no avoidance -- phase-1 weight) -> `Connected` (impact) -> decay into normal melee pairing.
- Fresh contact at speed: speed above `ChargeSpeedThreshold` AND no prior pair this engagement seeds the first pair with `ChargeBonusRemaining = BaseCharge` (EDU stat_charge); the bonus decays over `ChargeDecayWindow` (seconds, tunable) and applies to attack rolls only while positive (EC7).
- Knockback: bounded displacement impulse at impact, magnitude from impact speed (clamped), direction = charge heading, consumed through the existing displacement blend (`FServoStateFragment::DisplacementWeight`); decays within the recover window. Deterministic (no knockdown RNG v1 -- knockdown states are representation-side, Phase 5).
- Cavalry = single entity with composite stats (ADR-06 family; mount HP pool above).
- Charge morale shock: NOT built (Phase 4; the charge-connected signal is the hook -- §10).

### 4.10 Unit FSM and the StateTree prototype (roadmap I4)

- FSM states (architecture §6.2): Idle / Move / AttackMove / AttackUnit / Engaged (+ Routing reserved for Phase 4). Transitions consume registry orders (serial-bump pattern) and engagement aggregates (`EngagedCount`).
- Prototype: half-day hard timebox, behind `Battle.Combat.StateTree` (default 0). Scope: wire Idle / Move / AttackUnit / Engaged transitions for one unit type against the existing registry state, driven headless.
- Verdict criteria (recorded as the ADR either way, A7): (a) determinism under the B.0 test-mode contract (fixed DT, no wall-clock, seeded); (b) headless assertability -- state readable from sim state, testable without editor; (c) per-unit cost at ~40 units; (d) integration with order-consumption without a second command path; (e) debugging fit against the project's overlay + EXEC_RESULT evidence channels; (f) module/dependency surface (StateTree linkage).
- If adopted: the FSM ships behind the flag this phase and the ADR records the migration path. If rejected: the FSM is the registry order-consumption pattern (garrison FSM precedent), the ADR records why, and StateTree is not re-litigated until a future phase's brief re-opens it (AGENTS §2.3).
- The decision must land BEFORE `OrderLatency` gating and impetuous overrides accrete (roadmap I4) -- hence Task 1, before combat wiring.

### 4.11 Tunables (CVAR block, `Battle.Combat.*`; defaults are placeholders pending T6.1 audit)

| CVAR | Default | Meaning |
|------|---------|---------|
| `Battle.Combat.Enable` | 0 -> 1 at Task 7 | master flag; off = pre-combat behavior (B.8 item 6) |
| `Battle.Combat.ClusterPeriod` | 0.25 s | engagement record recompute |
| `Battle.Combat.MaxMobSlots` | 7 | budget ceiling |
| `Battle.Combat.FormedSlots` | 2 | budget floor (formed line) |
| `Battle.Combat.SecondRankBonus` | 1 | spear reach budget add |
| `Battle.Combat.NeighborsForFullCoverage` | 6 | coverage normalization |
| `Battle.Combat.SectorCount` | 8 | exposed-angle quantization |
| `Battle.Combat.MobPressureFactor` | 3 | mob DesiredPressure multiplier (with the `MaxMobSlots` floor, §3.2) |
| `Battle.Combat.PairHysteresis` | 0.4 s | distance breakup age gate |
| `Battle.Combat.AttackInterval` | 1.5 s | base attack cycle (staggered) |
| `Battle.Combat.HitChanceFloor` / `Cap` | 0.05 / 0.95 | roll clamps |
| `Battle.Combat.ChargeSpeedThreshold` | 4.0 m/s | fresh-contact gate |
| `Battle.Combat.ChargeDecayWindow` | 5.0 s | charge bonus decay |
| `Battle.Combat.SoakClash` | 0 | soak variant flag (nightly) |
| `Battle.Combat.StateTree` | 0 | I4 prototype flag |
| `Battle.Movement.*` (existing) | unchanged | movement spine untouched |

## 5. Scenario and demo plan

| Scenario | Setup | Asserts |
|----------|-------|---------|
| `Battle.Func.S4_ShieldWall` (T2.4) | formed defender, frontal attacker, flat tile | budget holds at FormedSlots while coverage high; melt curve shape as coverage drops |
| `Battle.Func.S5_Mob` (T2.5, = M3 geometry-only) | 2 knights vs 40 peasants, open field | knights take >= 4 exposed-arc attackers; pulled down within time band; waiters at ring, no carousel |
| `Battle.Func.ChargeMechanics` (T2.6) | cavalry charge vs standing line | bonus only on fresh fast contact; knockback applied; decays within window |
| `Battle.Func.PairLifecycle` (T2.7) | engaged pair, scripted kill + re-task | hysteresis breakup; handoff <= 1 tick; disengage mass-releases |
| `Battle.Func.DeterminismTwoRun` (T2.8) | 5-min mixed clash, same seed twice | per-tick hash equality |
| Perf (T2.9) | 1k entities all-in-contact stress | assign+tally < 0.5 ms GT; baseline JSON checked in |
| Soak variant | `Battle.Combat.SoakClash=1` on the movement soak controller | clash mix ends engaged-but-settled; stuck = 0; archetypes constant |

Demo (`Source/MassBattleGame`, ADR-18 pattern -- plugin public API only): ExecBridge `Battle.Combat.SpawnClash <variant>` fixtures (shield wall / mob / charge) + a representative M3 sample registered by the demo game mode when nothing is level-placed. Capsules, gray-box, no new maps (`L_MassBattle_T0_Functional` for scenarios; demo map for the sample).

## 6. Touch-point table

| File | Change |
|------|--------|
| `Public/BattleCombatFragments.h` (new) | `FCombatIntentFragment`, `FCombatPairFragment`, `FChargeFragment` |
| `Public/BattleEngagement.h` + `Private/BattleEngagement.cpp` (new) | solver seam: budgets, coverage, sectors, tally, assign, pressure formulas (meters, deterministic) |
| `Public/BattleMelee.h` + `Private/BattleMelee.cpp` (new) | resolution seam: hit chance, arc bands, AP, clamps, attack cycle, charge bonus math |
| `Public/BattleSpatialHash.h` + `Private/BattleSpatialHash.cpp` (new) | subsystem + counting sort + `QueryCircle` |
| `Public/BattleSave.h` + `Private/BattleSave.cpp` (new) | `FBattleSaveData` v1, capture/restore |
| `Public/BattleFragments.h` | `FHealthFragment` += mount HP pool; (verify queued-damage consumption) |
| `Public/BattleTypes.h` | `FUnitOrder::EType` += `AttackMove`, `AttackUnit`; payload += target unit handle |
| `Public/BattleRegistry.h` + `Private/BattleRegistry.cpp` | engagement records + scratch, `IssueAttackUnitOrder` / `IssueAttackMoveOrder`, `HandleUnitDeath` pair cleanup, capture/restore API |
| `Private/BattleStateHash.cpp` | pair/charge terms appended after garrison terms |
| `Private/BuildSpatialHashProcessor.cpp` (new) | row 2 |
| `Private/EngagementClustersProcessor.cpp` (new) | row 3, 4 Hz gate |
| `Private/TallyProcessor.cpp` + `Private/AssignPairsProcessor.cpp` (new) | rows 4-5, game thread |
| `Private/ChargeProcessor.cpp` (new) | row 9 melee half |
| `Private/CombatResolutionProcessor.cpp` (new) | row 10 |
| `Private/ServoProcessor.cpp` | pair-state -> movement-state mapping; press-ring target; knockback displacement consumption |
| `Private/BattleExecCommands.cpp` | `Battle.Order.AttackUnit/AttackMove`, `Battle.Combat.SpawnClash`, `Battle.Combat.DumpPairs`, `Battle.Combat.Metrics`, `Battle.Combat.SaveProbe` |
| `Private/BattleDebugSubsystem.cpp` | overlays (B.2): pairing lines + ContactAngle + state colors, tally vs budget, coverage heat, record debug; metrics: pair churn/s, re-pair latency, assign ms, deaths by arc |
| `Private/BattleEduParser.cpp` | ONLY if field coverage gaps found (weapon class/AP/lethality/charge); golden re-sign disclosed per T0.2 policy |
| `Plugins/MassBattle/Source/MassBattleTest/` | `BattleCombatTests.cpp` (T2.1-T2.3 + pair rules + hash/cluster), scenario actors (§5), soak controller extension |
| `Source/MassBattleGame/` | M3 geometry-only sample registration (public API only) |

## 7. Test-gate cross-reference

The T2 gate table is owned by the roadmap (Part A Phase 2); this section maps gates to spec sections, not to gate text:

| Gate | Spec sections |
|------|---------------|
| T2.1 hit-chance golden | §4.8, §4.11 |
| T2.2 budget/coverage invariants | §4.5 |
| T2.3 tally/assign synthetic | §4.3, §4.4 |
| T2.4 shield wall curve (S4) | §4.5, §5 |
| T2.5 mob test (S5 / M3) | §3.2, §4.4-4.6, §5 |
| T2.6 charge mechanics | §4.9 |
| T2.7 pair lifecycle | §4.4, §4.7 |
| T2.8 determinism two-run | §4.4, §4.11, §3.4 |
| T2.9 pairing perf gate | §4.1, §4.3-4.4, §3.3 |

## 8. ADR proposals (owner-applied per AGENTS §7)

Numbering assumes the owner applies the garrison spec's four pending proposals first (the garrison spec's "ADR-18..21" collide with the applied ADR-18 demo row; the owner reconciles when applying -- shift these numbers as needed).

| ADR | Proposal |
|-----|----------|
| ADR-22 | Engagement bookkeeping is registry-owned scratch + per-entity pair fragments; attack slots remain exposure-driven geometry (ADR-06 extended to the data model). No per-unit UObject combat state; no new shared fragments this phase. |
| ADR-23 | Combat resolution is attacker-authoritative on independent staggered sim timers; the defender holds no combat authority (ADR-05 extension). Damage flows through the existing queued-damage + death path. |
| ADR-24 | StateTree verdict for the unit FSM (filled by the I4 prototype: adopt-with-flag or reject-with-evidence). |
| ADR-25 | Save/load skeleton: schema-versioned registry snapshot + shared-fragment payloads + entity rebuild-from-spec (ADR-17 Route A); T6.3 is the maturity gate, by design. |
| ADR-26 | Melee constants ship as T2.1-golden-locked placeholders honoring the community model's structure; the Phase-6 T6.1 audit swaps ported constants with diff review (never silent). |

## 9. Sequencing rationale

Phase 2 executes after phase garrison (2026-10-26 complete) per the roadmap build order: the engagement solver is the highest-risk, most signature-defining system, and everything it needs from movement now exists -- servo states with PressRing/Charge rows and weights, OBB contact and shove blend, the planner/cost grid, the garrison exclusions, and the uniform six-archetype spine. Phase 3 (ranged) consumes pairing at projectile impact; Phase 4 (morale) consumes casualties, pairs, and charge-connected signals as inputs. Deferring the solver would defer the riskiest piece, violating the roadmap's guiding principle; pulling it earlier was impossible without the movement spine.

## 10. Extension points (designed-for, NOT built)

| Future | Seam |
|--------|------|
| Rout chase (Phase 4) | `FRoutingTag` -> budget bypass w/ perf cap, chase state, rear-arc resolution; release rules gain a rout clause (§4.7) |
| Ranged integration (Phase 3) | projectiles bypass pairing; impact seeds the first pair with a ranged ContactAngle; LOS from the heightfield/cost grid |
| Melee-vs-garrison | `FGarrisonSlot` cover/arc metadata + booking classification (garrison report §4.2); garrisoned units are not valid melee targets until an eviction story is specced (owner decision) |
| Morale shock on charge (Phase 4) | charge-connected signal rides the existing signal path; contribution stack reads it |
| `OrderLatency` + impetuous overrides (Phase 4) | FSM hook (§4.10); the ADR-24 verdict must land first (roadmap I4) |
| Officer/banner death, General/Terror auras (Phase 4) | existing signals + engagement aggregates |
| Riderless mounts | representation-side state (Phase 5) if the audit wants them; sim keeps composite HP v1 |
| Per-cluster sharding (5k) | records are independent (§3.2); assign pass partitions by cluster |
| Statistical far-LOD sim (open decision 2) | budget/coverage model is the attrition kernel a far-LOD pass would sample |
