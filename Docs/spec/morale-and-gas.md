# Morale & GAS -- Design Specification

| | |
|---|---|
| **Companion docs** | `Docs/spec/architecture.md` (parent design: section 10.1-10.5 the closed attribute set + the contribution stack, section 5 the GAS/entity split, section 12 budgets; ADR log) · `Docs/spec/roadmap-and-testing.md` (Phase 4 scope, T4 gate table, B.0-B.8) · `Docs/spec/engagement-and-melee.md` (the pair/coverage machinery this spec reads for odds + arc exposure) · `Docs/spec/ranged-stack.md` (the casualty feed this spec consumes; the RC7 order-serial guard precedent) · `Docs/AGENTS.md` (standing protocol) |
| **Status** | Draft 1 · 2026-10-31 · owner review pending. Elaborates architecture section 10.5 into the Phase 4 implementation contract. On conflict, the architecture doc wins (AGENTS.md precedence); corrections land per AGENTS section 4. |
| **Engine pin** | UE 5.8.4 (ADR-13) |
| **Audience** | Phase 4 brief author and the implementing agent (cited sections only; token discipline per AGENTS section 1) |
| **Owner-approval status** | Section 8 ADR rows are PROPOSALS pending the owner's architecture-log commit (AGENTS section 7; the ranged-stack precedent ADR-27..31). The 4a/4b run split is the owner decision of 2026-10-31. |

---

## 0. Behavior contract (the acceptance table)

The soul of the battle from ONE mechanism: a sim-fed contribution stack evaluated on an explicit tick, writing GAS attributes that flow back to entities through the existing UnitSync channel, and a registry-owned rout state machine that drives fleeing through the existing order surface. Every row below is observable in sim state (registry fields, attribute snapshots, shared fragments, hashes, EXEC_RESULT lines), never in visuals. Roadmap scenario asserts (B.3, S7) and gate T4.x rows cite these.

| ID | Behavior | Contract |
|----|----------|----------|
| MC1 | Tick integrity | Every living unit's `Morale` and `Fatigue` values refresh on the MoraleTick mechanism (section 4.8) at the tuned period (0.25-0.5 s); under load the value's age never exceeds one period (T4.8). GAS exec-calcs fire only on GE period executions and aggregation events, never on their own timer (E3). The tick never runs worker-side. |
| MC2 | Contribution stack | `Morale` = the weighted sum of the ten documented terms (base mental, casualty fraction, fresh-casualty spike, local engagement odds, flanked/rear arc exposure, routing friends nearby, general aura, general-death shock, height advantage, winning locally; section 4.1). Each term is isolated at a known weight (T4.1 golden); discipline scales break resistance and rally chance, never the weights. |
| MC3 | Fatigue model | The unit-level exertion integral (run/fight rates, section 4.3) quantizes into six states (Fresh, Active, QuiteTired, Tired, VeryTired, Exhausted). The state's attack/defense/speed/reload multiplier table is golden (T4.2). State transitions apply/remove `FatigueState` gameplay effects through the `*Mod` attributes ONLY (ADR-09) -- never direct shared-fragment writes. |
| MC4 | Effect end-to-end | An applied effect (e.g. Exhausted) reaches entity-visible shared-fragment multipliers within one frame of the attribute write (UnitSync; T4.5). Overlapping effects aggregate in GAS's fixed modifier order; the derived multiplier resolves in UnitSync's fixed `1 + Mod` form (arch section 5.3: one resolution point, effects never resolve against each other ad hoc). |
| MC5 | Rout FSM | A unit enters `Wavering` when morale crosses the wavering threshold (combat continues, no flee; the UI/save-visible warning state); enters `Routing` at the break threshold -- pairs released (the F2/mass-release path), a flee Move self-issued directly away from the nearest threat (run speed x fatigue multiplier; the RC7 auto-flag + order-serial guard pattern), `EBattleMovementState::Flee` stamped by the servo path; enters `Shattered` at the rally-exhaustion limit (flees forever). Transitions evaluate on the tick, in sorted-handle order, deterministically. |
| MC6 | Rally | While `Routing`, a rally roll runs on the tick cadence: success requires morale above the rally threshold AND a living friendly general within the aura radius (T4.3: rally succeeds under general aura, fails without); success re-enters `Wavering` and self-issues a Stop. Each break increments `RoutCount`; the rally chance decays with it; `Shattered` never rallies. |
| MC7 | Panic cascade | Each routing friendly within `CascadeRadiusM` of a unit's envelope contributes the cascade term (section 4.6); a breaking unit measurably drags adjacent units toward wavering (S7 asserts the cascade crossing at least one neighbor). The term is a read of unit envelopes at the 4 Hz cluster cadence, never a per-entity scan. |
| MC8 | Officer/general events | An officer entity's death rides the existing death path and raises `Battle.Unit.OfficerDied` on the unit's ASC plus an amplified fresh-casualty spike for that unit (T4.4). The general entity's death (officer slot 0 of a `bIsGeneral` unit) raises `Battle.Team.GeneralDied` on every same-team unit's ASC, stamps the team-wide general-death shock decay term, and removes the aura contribution (the aura input reads the general's living anchor). |
| MC9 | Toggles | `GuardMode` suppresses pursuit and impetuous auto-charge; an `Impetuous`-discipline unit auto-charges the nearest enemy inside `ImpetuousChargeRadiusM` when not guarded, not player-ordered, not routing (registry self-issued AttackUnit order, auto-flagged; a player order-serial bump cancels; T4.6). Phase 3's FireAtWill/Skirmish semantics are unchanged. |
| MC10 | Determinism | Same seed, same binary, same machine: per-tick state hash identical across two 5-minute mixed battles including morale, fatigue effects, routs, and rallies (T4.7), under the deterministic tick pin (section 4.8). All morale/rally rolls draw from the seed-derived hash family through canonical identity inputs (the A13 rule). |

## 1. Scope and non-goals

In scope (Phase 4a, this spec's implementation contract): the morale contribution stack + tick mechanism (E3, owner decision 2026-10-31: the periodic MoraleTick GE), the fatigue model + `FatigueState` effect catalog, the rout FSM (wavering/breaking/rout/shattered) + flee orders + rally rolls, the panic cascade, officer identity + officer/general death events, the GeneralAura, GuardMode + impetuous auto-charge toggles, the EDU `stat_mental` discipline/training + `officer` line consumption, save schema v3, state-hash morale terms, S7_RoutCascade + OfficerDeath + EffectEndToEnd + ToggleBehaviors scenarios, T4.1-T4.8.

Out of scope (recorded, not accidental -- the 4b run `Docs/phases/phase-4/brief-command-and-fear.md` owns them):

- `OrderLatency` (command response delay): the attribute exists in the closed set; the order-delay queue that consumes it needs the order-surface rework (a parked-order generalization) and its own gates. 4b.
- `TerrorAura` (frighten units draining enemy morale): the mirror of GeneralAura against enemies; the EDU `frighten_foot`/`frighten_mounted` attribute tokens join the parse with it. 4b.
- Climate/terrain fatigue (`stat_heat`, TerrainPenalty, WeatherRain): no weather or ground-material system exists; the fatigue-rate slots they would write are designed for (section 4.3) but the systems are Phase 5+ (the ranged spec's non-goal posture repeated).
- Morale-driven AI (a low-morale AI general refusing orders, army morale retreat decisions): the FSM is per-unit; army-level decision-making is Phase 6 (open decision 2 adjacent).
- Far-LOD statistical morale (the morale analog of far-LOD attrition): Phase 6, open decision 2.
- Representation: routing banners, morale-tinted unit cards, fleeing animations are Phase 5; this phase ships gray-box overlay rows only (section 5).
- The melee math never reads morale: combat effectiveness flows ONLY through the shared-fragment multipliers (ADR-09 discipline; FatigueState effects) and rout-driven disengagement. No `MoraleBonus` consumer appears inside `FMeleeMath`.

## 2. Architecture summary

One sentence: **morale is a periodic evaluation over sim-fed inputs that writes GAS attributes through one gameplay effect, and routing is a registry state machine that flees through the existing order surface -- a consumer of the Phase 0-3 spine (death path, engagement tallies, cluster envelopes, orders, UnitSync), not a new system.**

- The data spine grows ZERO per-entity fragments and ZERO new GAS attributes (the closed set already carries `Morale`, `Fatigue`, every `*Mod` target, `OrderLatency`, `AmmoPool`; arch section 10.1). Morale and fatigue are unit-level -- a spearman has no morale fragment, exactly as he has no ranged fragment (arch section 3.2). The per-entity additions are two spawn-immutable values on `FBaseStatsFragment` (`bOfficer`, and discipline rides the registry) and nothing else.
- The tick mechanism (E3 resolved, owner 2026-10-31): ONE periodic infinite-duration gameplay effect per unit actor (`UBattleMoraleTickEffect`, section 4.8) whose period execution runs ONE exec-calc (`UBattleMoraleExecCalc`) which calls the pure seam (`BattleMorale::EvaluateMorale` -- one home, the B.0 discipline). Inputs: SetByCaller magnitudes for effect-owned terms (aura presence stamped at application, refreshed on aggregation events) + the registry's sim-owned input block for continuously-varying terms (the disclosed deviation from literal SetByCaller, section 4.8). Outputs: `Morale` + `Fatigue` attribute writes.
- The rout FSM lives in the registry (`FUnitRuntimeState`), evaluated by a game-thread processor on the tick cadence, driving flee/rally through the existing order APIs -- the `ApplyUnitSkirmishDecision` pattern generalized (RC7's order-serial guard, auto-flag, facing preservation all reused verbatim).
- Threading: the morale pass is a GAME-THREAD processor in the `Battle` group (the ADR-28 evidence class: it reads the registry, the cluster envelopes, and the unit actors' ASCs -- three game-thread owners). The exec-calc and the effect application run on the game thread by construction (GAS actors are game-thread).
- Determinism: the morale VALUE is a pure function of sim state evaluated inside the exec-calc; the tick cadence is pinned by the sim under `Battle.Morale.DeterministicTick=1` (section 4.8); rout/rally rolls draw from the `CombatRoll01` hash family keyed on canonical identity (A13); state-hash terms append after the pool block (section 3.4).

## 3. Data model

### 3.1 Registry-owned morale state (`BattleTypes.h` / `BattleRegistry.h/.cpp`)

Per-unit, in `FUnitRuntimeState` (bookkeeping, not per-entity):

```cpp
// Phase 4 (spec morale-and-gas section 3.1)
EBattleRoutState RoutState = EBattleRoutState::Steady;   // Steady/Wavering/Routing/Shattered
uint8 RoutCount = 0;              // breaks so far; rally chance decays; Shattered at MaxRallies
float ExertionIntegral = 0.f;     // fatigue accumulator (unit level, section 4.3)
float FreshCasualtySpike = 0.f;   // sim-tracked decay term (E3; section 4.2)
float LastMoraleValue = 0.f;      // sim mirror of the GE-written attribute (hash term; section 3.4)
float LastFatigueValue = 0.f;     // same, fatigue
float LastMoraleTickSimTime = -1.f; // staleness stamp (T4.8); -1 = never ticked
bool bGuardMode = false;          // toggle family (MC9)
EBattleDiscipline Discipline = EBattleDiscipline::Normal;   // EDU stat_mental[1]
```

Officer identity: spawn stamps `FBaseStatsFragment.bOfficer` (uint8, 0/1) on the LAST `OfficerCount` roster entities (spawn-order determinism; the batch order is the roster order). The registry stores `OfficerRosterBase` (the first officer roster index) so the death path resolves officer-ness from the dying entity's roster index in O(1) without touching fragments (the receiver already receives `RosterIndex`). The officer ENTITY placement (front-center-forward layout slots) is a layout-seam change that rides the pipeline PR (section 4.7); identity and events land with the data spine.

Per-team state (registry, game thread): `FGeneralRecord { FUnitHandle Unit; FMassEntityHandle OfficerEntity; }` per team -- the living general anchor the aura input reads (updated at spawn/registration and on officer-entity death), plus `GeneralDeathShock` (the team-wide decay term, section 4.2) and its stamp.

APIs (the A11 order-surface pattern; reject + EXEC_RESULT line on bad handles): `SetUnitGuardToggle(Handle, bGuard)`, `GetUnitMoraleState(Handle, ...)` (rout state, values, staleness), `ApplyUnitRoutDecision(Handle, Decision, FleeAnchorCm, FacingYawDeg)` (the RC7 generalized apply: flee Move away / rally Stop, auto-flagged, order-serial guarded), `NotifyOfficerDeath(Handle, RosterIndex)` (death-path hook: spike amplification + events + general bookkeeping).

### 3.2 GAS surface (the closed set, consumed at last)

Zero new attributes (arch section 10.1). Zero new shared fragments. The consumed slots:

- `Morale` -- written ONLY by the exec-calc (meta attribute; NOT an effect target per ADR-09). Read back through the existing attribute-change delegate into `CachedAttributes.Morale` (the arch section 5.1 path, proven since Phase 0).
- `Fatigue` -- same path.
- `SpeedMod` / `MeleeAttackMod` / `MeleeDefenseMod` / `BlockChanceMod` / `RangedAttackMod` -- the effect targets the `FatigueState` catalog writes (ADR-09: buffs/debuffs target `*Mod` only).
- `MoraleBonus` in `FUnitStatsSharedFragment` -- CORRECTED this phase: UnitSync's current `Stats.MoraleBonus = Snapshot->Morale` line (a Phase 0 placeholder that fed the absolute morale level in as a bonus) becomes the aura sum (`Snapshot->Morale` no longer reaches it; the general aura + future effect-catalog morale bonuses aggregate in the exec-calc and ride the snapshot's own path). Disclosed correction per AGENTS section 2.1: no consumer exists today, so the change is behavior-neutral until the melee-adjacent systems consume it.

The `AmmoPool` mirror's interim writer (the fire pass; audit D-4) moves to UnitSync this phase: the sync processor becomes the single GAS-mirror writer for all aggregate attributes (A14's stated Phase 4+ plan).

### 3.3 EDU mapping (the data spine)

- `stat_mental[0]` base morale (consumed since Phase 0 as `MoraleBase`); `[1]` discipline token (`low`/`normal`/`disciplined`/`impetuous` -> `EBattleDiscipline`; unknown token fails FromEdu loudly, the B-3 posture); `[2]` training token (`untrained`/`trained`/`highly_trained` -> `EBattleTraining`; rides row stats, inert this phase -- the formation-neatness consumer is future movement polish).
- `officer` lines: the parser schema gains the key (arity 1, `EduName`, `MaxOccurrences 3` -- the schema's duplicate-key rule gains a per-key occurrence cap; 4+ officers rejects loudly). Rows may carry zero. `FBattleRowStats::OfficerCount` = the parsed count. Fixture re-sign: the four golden rows gain officer lines (Peasant_Levy 0, Sergeant_Spearmen 2, Peasant Archers 1, Mercenary Crossbowmen 1); `SchemaFieldCount` 15 -> 16 (disclosed re-sign, the T0.2 policy).
- `FUnitSpawnSpec` += `OfficerCount` (int32; 0 = the row's own count; >0 overrides, clamped to 3) and `bIsGeneral` (bool, default false). `AUnitActor` exposes both (the TeamId/UnitDictionary authoring-surface precedent); `OnConstruction` needs no sync (plain passthrough).
- `FOfficerTag` (declared Phase 0, never stamped, zero usages -- verified) is DELETED in favor of the fragment value: officer-ness is per-entity within a composition, and the ranged phase already ruled that per-entity-within-composition discriminators are fragment VALUES, not tags (`EBattleProjectileKind` precedent; also avoids per-entity archetype flips at fixup, the ADR-15 class).

### 3.4 State-hash terms

Unit-level morale block appended as block 10, AFTER the pool block (position-stable; existing section indices unchanged), per sorted handle: `RoutState` (uint8), `RoutCount` (uint8), `ExertionIntegral` (1e-2), `LastMoraleValue` (1e-2), `LastFatigueValue` (1e-2), `FreshCasualtySpike` (1e-2), `bGuardMode` (uint8). Fixed term count, zero-fill on absent state (unspawned/legacy paths). `LastMoraleTickSimTime` is pipeline state, not sim state -- excluded (the dirty-flag precedent, audit M4). Discipline/Training/officer identity are spawn-immutable -- excluded (the base-stats precedent).

### 3.5 Save/load schema (v2 -> v3)

`FUnitSaveEntry` += `RoutState`/`RoutCount` (uint8), `ExertionIntegral`/`LastMoraleValue`/`LastFatigueValue`/`FreshCasualtySpike` (floats), `bGuardMode`/`bIsGeneral` (uint8), `OfficerCount` (int32). `SchemaVersion` -> 3; restore accepts 2 (legacy: morale defaults, rout Steady) and 3. Transient by design (NOT saved): `LastMoraleTickSimTime`, the retreat/rout auto-order bookkeeping (`bRetreating`-class transient state -- a restored battle re-evaluates from idle, the RC7 restore posture), the per-team general records (rebuild from specs at restore). Round-trip test extended: rout state + values + guard toggle survive capture/clear/restore; legacy v2 accepted.

## 4. Mechanics

### 4.1 The contribution stack (seam `BattleMorale.h/cpp`, T4.1)

Pure function over an input block; meters/seconds; deterministic; no world, no fragments:

```cpp
struct FMoraleTickInputs
{
    float BaseMorale = 0.f;          // EDU stat_mental[0]
    float CasualtyFraction = 0.f;    // dead / InitialRosterSize (0..1)
    float FreshCasualtySpike = 0.f;  // section 4.2 (sim-tracked, decaying)
    float LocalOdds = 0.f;           // section 4.6 (paired-attackers differential, normalized)
    uint8 ArcExposureMask = 0;       // section 4.6 (flanked/rear bits from the coverage mask)
    int32 RoutingFriendsNearby = 0;  // section 4.6 (cascade count)
    float GeneralAuraBonus = 0.f;    // section 4.6 (0 when no living general in radius)
    float GeneralDeathShock = 0.f;   // section 4.2 (team-wide decay term)
    float HeightAdvantage = 0.f;     // -1..1 (unit anchor Z vs engaged enemies' mean Z, normalized)
    float WinningLocally = 0.f;      // -1..1 (recent kill differential, decaying)
};
float EvaluateMorale(const FMoraleTickInputs& Inputs);   // weighted sum, weights section 4.12
```

Sum form: `Base + sum(weight_i * term_i)` with each term's sign baked into its weight (negative for penalties). Weights are named constants in `FMoraleWeights` (one home, golden-locked placeholders -- ADR-30 posture continued). The stack's M2TW structure (community model): casualties grind morale down; a SPIKE of fresh casualties hits harder than the same losses slowly (the 4.2 term); losing the local fight, being flanked, friends routing nearby, and the general's death stack penalties; the general's presence and high ground give bonuses. Discipline does NOT scale the weights: it scales the THRESHOLDS (break resistance) and the rally chance (section 4.5) -- an impetuous unit breaks as easily but rallies worse and auto-charges (MC9); a disciplined unit absorbs more before breaking.

### 4.2 The decay terms (sim-tracked, pushed each period -- E3's ruling)

GAS has no immanent time decay for exec-calc inputs; the two decay terms are sim accumulators in the registry, pushed as inputs each period:

- `FreshCasualtySpike`: on each death the unit's spike += `SpikePerCasualty` (amplified `OfficerSpikeMultiplier` when the death is an officer, MC8); each tick the spike decays `*= exp(-Dt / SpikeHalfLife)` (the closed form; golden-locked). The term makes a 10-man burst break a unit that 10 losses over two minutes would not.
- `GeneralDeathShock` (per team): set to `GeneralShockBase` when the general entity dies; decays the same way. The greatest penalty lands immediately after the death and improves gradually (the M2TW observation).
- `WinningLocally`: kills-by-this-unit minus deaths-in-this-unit over a decaying window (registry accumulators, same decay form).

### 4.3 The fatigue model (seam, T4.2)

Unit-level exertion integral accumulated by the morale pass from inputs the spine already maintains:

```cpp
void AccumulateExertion(float& InOutExertion, float Dt, bool bRunning, bool bFighting, float FatigueRateScale);
EBattleFatigueState EvaluateFatigueState(float ExertionIntegral);   // 6 bands
struct FFatigueMultipliers { float AttackMult; float DefenseMult; float SpeedMult; float ReloadMult; };
FFatigueMultipliers GetFatigueMultipliers(EBattleFatigueState State);  // the golden table
```

Rates: running accumulates `RunRate` (per second), fighting `FightRate`, walking/idle accumulates nothing (v1; the climate/terrain rates are the 4b+/Phase 5 slots). `FatigueRateScale` carries the `hardy`/`very_hardy` attribute posture when those tokens join the parse (4b; the slot exists so the rate table has one home). The integral NEVER decreases this phase (no rest model -- M2TW rest is 4b candidate; recorded in section 10). States at fixed integral bands (0, 10, 25, 45, 70 -> Fresh..Exhausted, placeholder bands). The multiplier table (all 1.0 at Fresh, ~0.55/0.65 attack/defense at Exhausted, speed to ~0.75, reload to ~0.7 -- A6 placeholders with the community model's shape) is golden-locked at T4.2.

### 4.4 The rout state machine (seam + registry application, MC5)

```cpp
enum class EBattleRoutState : uint8 { Steady = 0, Wavering = 1, Routing = 2, Shattered = 3 };
EBattleRoutState EvaluateRoutTransition(EBattleRoutState Current, float Morale,
    float WaveringThreshold, float BreakThreshold, uint8 RoutCount, uint8 MaxRallies,
    EBattleDiscipline Discipline, float Dt);
```

Thresholds are ABSOLUTE morale values (placeholder defaults section 4.12) scaled by discipline: `BreakThreshold * DisciplineBreakResistance[Discipline]` (Low 0.85, Normal 1.0, Disciplined 1.2, Impetuous 1.0 -- the impetuous unit is brave in the charge, not in the rout). Transitions: `Steady -> Wavering` when `Morale < WaveringThreshold`; `Wavering -> Routing` when `Morale < BreakThreshold` (hysteresis: Wavering recovers to Steady when `Morale > WaveringThreshold + WaveringHysteresis`); `* -> Shattered` when `RoutCount >= MaxRallies`. The registry applies a Routing entry exactly like RC7 applies a retreat: `FlagUnitEngagementsDead` (mass release both directions), self-issued Move directly away from the nearest threat anchor (auto-flagged; facing away from the threat; CommandedSpeed = run x fatigue speed multiplier), `bRoutFleeing` bookkeeping with the order-serial guard (a player order bump cancels OUR flee -- the unit is under player control again; it will re-evaluate on the next tick). Routers are legitimate melee targets; pursuers gain the rear-arc resolution the sectorizer already gives (T4.3's chase half needs zero new combat code -- the fleeing unit's facing serves rear arcs to its pursuers).

### 4.5 Rally (seam + registry, MC6)

```cpp
bool EvaluateRally(float Morale, float RallyThreshold, EBattleDiscipline Discipline,
    uint8 RoutCount, bool bGeneralAuraActive, uint32 RollSeed);
```

Rolled on the tick cadence while Routing: requires `Morale >= RallyThreshold` AND `bGeneralAuraActive` (a living friendly general within `GeneralAuraRadiusM`); the roll itself is `CombatRoll01`-family over the canonical unit key (A13) with the rally-attempt count as the roll count input; the chance base decays `RallyBase * pow(RallyDecay, RoutCount)`; `RallyChance * DisciplineRallyFactor[Discipline]` (Low 0.7, Normal 1.0, Disciplined 1.3, Impetuous 0.8). Success: re-enter Wavering, self-issue Stop. Shattered units skip the roll entirely.

### 4.6 The geometric inputs (cluster-cadence reads, never per-entity scans)

- `LocalOdds`: from the engagement records + tally scratch the cluster pass already maintains -- the unit's paired attackers vs its engaged count, normalized to [-1, 1] (`(AssignedAgainst - AssignedFor) / Max(1, AssignedAgainst + AssignedFor)` placeholder form).
- `ArcExposureMask`: the coverage scratch's sector mask (same-unit living neighbors) is the WRONG mask for morale (it measures crowd, not threat direction). The morale input derives from the unit's ENGAGED sectors: the tally of inbound pair contact angles quantized through the shared sectorizer (`FCombatSolver::SectorIndexFromAngle`, one home) against the unit's facing -- bits set for sectors with inbound attackers; flanked = side sectors occupied; rear = rear sectors occupied. The pass computes it from pair fragments at the cluster cadence (a per-unit fold over its entities' pairs -- bounded by the roster, same shape as the 4 Hz aggregates).
- `RoutingFriendsNearby`: count of same-team units in `Routing`/`Shattered` whose envelope center lies within `CascadeRadiusM` of this unit's envelope center (envelope-vs-envelope center distance, the skirmish precedent's geometry source).
- `GeneralAuraBonus`: `GeneralAuraBase * GeneralAuraScale(dist)` when a living friendly general's anchor is within `GeneralAuraRadiusM` (scale falls linearly to 0 at the radius; 0 outside).
- `HeightAdvantage`: the unit envelope anchor Z minus the mean Z of engaged enemy envelopes, normalized by `HeightNormM` and clamped.

### 4.7 Officer and general death events (riding the existing signals)

The death path is proven observable (F2: `FDeadTag` defer -> observer -> signal -> `HandleUnitDeath(RosterIndex)`). The receiver's roster-index parameter resolves officer-ness against `OfficerRosterBase` (section 3.1): an officer death (a) amplifies the unit's fresh-casualty spike (MC8), (b) raises `Battle.Unit.OfficerDied` on the unit actor's ASC (a new native tag beside `TAG_Battle_Unit_Died`), (c) when the officer is slot 0 of a `bIsGeneral` unit: raises `Battle.Team.GeneralDied` on every same-team unit's ASC, stamps the team `GeneralDeathShock`, and invalidates the general record (aura input reads 0 thereafter). The GAS events are observable via the existing event-count assertions (the `GetDeathEventCount` pattern); the morale effects are observable in the next tick's value. Officer entity PLACEMENT (front-center-forward layout slots) rides the pipeline PR: the layout seam gains the officer slot offset and the resolve processor consumes it; identity + events + bookkeeping land with the data spine so the placement change is value-neutral to the spine.

### 4.8 The MoraleTick GE + exec calc (E3 resolved; the determinism pin)

- `UBattleMoraleTickEffect : UGameplayEffect` (C++-authored, applied at battle start per unit actor): infinite duration, `Period = Battle.Morale.TickPeriod` (0.25-0.5 s tunable), ONE custom exec calc. The application context stamps SetByCaller magnitudes for the effect-owned terms (aura presence flag, guard context) and is re-applied only when those change (aggregation events -- aura entry/exit, toggle flips; the remove/re-apply is the documented SetByCaller refresh path since active specs are not mutable through public API).
- `UBattleMoraleExecCalc : UGameplayEffectExecutionCalculation`: pulls the sim-owned input block from the registry through the target ASC's owner actor (game thread), plus the SetByCaller magnitudes, calls `BattleMorale::EvaluateMorale` (ONE home) and `EvaluateFatigueState`, writes `Morale` and `Fatigue` through `OutExecutionOutput.AddOutputModifier`. The exec-calc NEVER reads wall-clock, never iterates entities, never issues orders.
- **The determinism pin (the T4.7 carve-out, disclosed):** the GE's natural period rides the ASC's wall-clock timing; two runs would phase-diverge. Under `Battle.Morale.DeterministicTick=1` (set by every determinism-gated scenario and the two-run gate), a game-thread morale processor forces the period execution on the fixed sim cadence (tick-count accumulator; the forced-execution symbol is the brief's section 4 API-audit item with the fallback ladder: force-API -> remove/re-apply at cadence -> documented gate carve-out). The shipping path keeps the natural period; T4.8 bounds its staleness. The morale VALUE remains a pure function of sim state either way -- only the sampling instant differs.
- The morale pass (`MoraleTickProcessor`, game thread, `Battle` group, after `TallyProcessor`): accumulates the period, pushes the decay terms (4.2) and exertion (4.3) per tick, evaluates the FSM transitions (4.4) and rally rolls (4.5) on the tick, applies rout/rally decisions through the registry (the RC7 apply pattern), maintains the team general records. Flags-off (`Battle.Morale.Enable 0`) = byte-identical baseline: no GE applied, no pass state, the hash block folds zeros (position-stable).

### 4.9 The FatigueState effect catalog (T4.5's wiring)

Six C++-authored infinite GEs (`UBattleFatigueStateEffect` parameterized by state, or one class + six CDOs -- the brief's A-row picks): each carries additive modifiers on `MeleeAttackMod`, `MeleeDefenseMod`, `SpeedMod`, `BlockChanceMod`, `RangedAttackMod` equal to the state's table row minus 1 (the multipliers resolve in UnitSync's fixed `1 + Mod` form). The morale pass applies state N's GE and removes state N-1's when `EvaluateFatigueState` crosses a band (aggregation event -- never a timer). Overlapping with any future buff GEs, GAS's modifier aggregation applies (MC4). This is the "full GAS" half of the phase: the catalog proves the effect -> attribute -> delegate -> snapshot -> UnitSync -> shared fragment -> entity loop end to end (T4.5 asserts the 1-frame bound and the stacking order with two overlapping effects).

### 4.10 Processors

- `MoraleTickProcessor` (new, game thread, group "Battle", after `TallyProcessor`, before `RangedFireProcessor`): section 4.8's pass. The ONLY processor this phase adds.
- `UnitSyncProcessor` (existing): unchanged mechanically; its derivation block is where `FatigueAttackMult`/`FatigueDefenseMult` begin to mean something (they already read `Snapshot->Fatigue * 0.01f` -- the placeholder coupling is REPLACED by the catalog's `*Mod` writes and the derivation reverts to the plain `1 + Mod` form for those two fields; disclosed correction, the placeholder predates the catalog).
- `ServoProcessor` / `UnitLocomotion`: ZERO morale consumption (the A12 discipline continued) -- a rout flee is a normal move order at a commanded speed; the `Flee` movement state's avoidance profile is already spec'd (BattleMovementFragments.h).
- `BattleDeathPath`: the receiver gains the officer-branch (4.7) -- a read of registry bookkeeping + event raise; no new signal name for unit death.

### 4.11 Unit fire control (toggle family, arch section 10.4)

`SetUnitGuardToggle` + ExecBridge `Battle.Morale.Toggle <unit> <Guard> <0|1>` + `Battle.Morale.Dump <unit>` (rout state, morale/fatigue values + staleness, contributions breakdown, rout count, toggles -- the A4 order-surface pattern). Guard semantics (MC9): suppresses impetuous auto-charge and pursuit-chasing (a guarded unit's attack orders hold ground: the assign pass's pursue extension consumes the flag; v1 assertion is the auto-charge suppression). Impetuous auto-charge: at the cluster cadence, an Impetuous-discipline unit with no player order since its last auto order, not routing, not guarded, with a nearest enemy envelope inside `ImpetuousChargeRadiusM` (default 30 m), self-issues an AttackUnit order (auto-flagged, serial-guarded). The demo HUD keys are PR-scoped (editor-owned input, PR-2 precedent).

### 4.12 Tunables (CVAR block `Battle.Morale.*`; A6 placeholders, T6.1 owns the audit)

| CVar | Default | Meaning |
|------|---------|---------|
| `Battle.Morale.Enable` | 0 -> 1 at landing | master flag (B.8 item 6; flags-off = byte-identical baseline) |
| `Battle.Morale.TickPeriod` | 0.35 s | the GE period (arch section 12 band 0.25-0.5) |
| `Battle.Morale.DeterministicTick` | 0 | pin the tick to the sim cadence (gates/soak set 1) |
| `Battle.Morale.WaveringThreshold` | 4.0 | absolute morale below which a unit wavers |
| `Battle.Morale.BreakThreshold` | 0.0 | absolute morale below which a unit breaks |
| `Battle.Morale.WaveringHysteresis` | 1.0 | recovery margin above the wavering threshold |
| `Battle.Morale.RallyThreshold` | 6.0 | morale required to attempt rally |
| `Battle.Morale.RallyBase` / `.RallyDecay` | 0.4 / 0.5 | rally chance base / per-break decay |
| `Battle.Morale.MaxRallies` | 2 | breaks beyond which a unit is Shattered |
| `Battle.Morale.SpikePerCasualty` / `.SpikeHalfLifeS` / `.OfficerSpikeMultiplier` | 1.0 / 4.0 s / 3.0 | the fresh-casualty term (4.2) |
| `Battle.Morale.GeneralShockBase` / `.GeneralShockHalfLifeS` | 6.0 / 20.0 s | the team shock term (4.2) |
| `Battle.Morale.CascadeRadiusM` | 45 | routing-friends contribution radius |
| `Battle.Morale.GeneralAuraRadiusM` / `.GeneralAuraBase` | 60 / 2.0 | the aura geometry + magnitude |
| `Battle.Morale.ImpetuousChargeRadiusM` | 30 | the auto-charge trigger |
| `Battle.Morale.SoakRout` | 0 | soak variant flag (nightly; 0 = baseline identity) |

(The weights of section 4.1 are `FMoraleWeights` constants in the seam header and the fatigue rates/state bands are `FFatigueConstants` -- both NOT cvars: the A6/T6.1 audit swaps them as reviewed sets; the cvar surface stays the runtime-tunable subset, the `FRangedConstants` precedent. Exertion rates: Run 0.6/s, Fight 0.8/s; bands 0/10/25/45/70 -> Fresh..Exhausted.)

## 5. Scenario and demo plan

- **S7_RoutCascade (T4.3, func):** three adjacent friendly infantry units engaged frontally, ranged attrition scripted onto the center unit until it breaks; asserts (a) the wavering-then-break threshold order with evidence lines per transition; (b) the flee: pairs released both directions, movement away from the threat, Flee state; (c) the cascade: at least one neighbor crosses into wavering attributable to the cascade term (isolated by a second run with `CascadeRadiusM 0`); (d) rally: a general unit walked into aura range flips the roll's context -- rally succeeds within a bounded window, fails indefinitely without the general (two seeded sub-runs); (e) pursuers gain rear-arc resolution on the fleeing unit (the contact-angle band assertion).
- **OfficerDeath (T4.4, func):** a two-officer unit engaged; scripted officer-entity kill (ExecBridge targets the officer roster index); asserts the `Battle.Unit.OfficerDied` event count, the amplified spike vs a baseline non-officer kill (two seeded sub-runs, delta in the contribution breakdown), and the morale shock propagation. The general variant: a `bIsGeneral` unit's officer 0 killed -> `Battle.Team.GeneralDied` events on every same-team ASC + the shock term decaying over subsequent ticks.
- **EffectEndToEnd (T4.5, func):** a unit driven to each fatigue band; asserts the GE -> attribute -> snapshot -> shared-fragment latency (<= 1 frame from the attribute write to `FatigueAttackMult` visible in the fragment), the multiplier values against the golden table, and the stacking order with a second overlapping effect applied mid-band (the aggregate matches the documented order).
- **ToggleBehaviors (T4.6, func):** GuardMode ON suppresses the impetuous auto-charge (an Impetuous unit with an enemy inside the radius holds; OFF charges within a bounded window); FireAtWill/Skirmish semantics re-asserted unchanged (the phase-3 contract rows); a player order overrides the auto-charge (serial bump).
- **M5 demo (gray-box):** ExecBridge `Battle.Morale.SpawnRout` (a clash fixture with a general + adjacent units, dictionary-keyed rows) + overlay rows: per-unit morale bars (color by rout state), aura rings at the general, flee arrows. Overlay-only until Phase 5.
- **Soak variant:** `Battle.Morale.SoakRout=1` rides `BattleMovementSoakController` (the SoakVolley precedent): the spawned mix gains a general unit + impetuous rows; FINAL line += `routs=%d rallies=%d shattered=%d officerDeaths=%d moraleTicks=%d`; flag off = byte-identical.

## 6. Touch-point table

| Existing thing | Change | Why |
|---|---|---|
| `BattleTypes.h` | `EBattleDiscipline`/`EBattleTraining`/`EBattleFatigueState`/`EBattleRoutState`; `FUnitRuntimeState` morale block (section 3.1); `FUnitSpawnSpec` OfficerCount/bIsGeneral | sections 3.1, 3.3 |
| `BattleFragments.h` | `FBaseStatsFragment.bOfficer`; `FOfficerTag` DELETED (zero usages, the fragment-value ruling) | section 3.3 |
| `BattleEduParser` schema | `officer` key (arity 1, EduName, MaxOccurrences 3 -- the duplicate-key rule gains the per-key cap); `SchemaFieldCount` 15 -> 16 | section 3.3 |
| `golden_edu_v1.txt` | officer lines on three rows (0/2/1/1); re-sign disclosed | T0.2 policy |
| `FBattleRowStats::FromEdu` | discipline/training token mapping (loud on unknown) + OfficerCount | section 3.3 |
| `BattleSpawnLibrary` fixup | stamp `bOfficer` on the last OfficerCount roster entities; registry stamps Discipline + OfficerRosterBase + the team general record | section 3.1 |
| `BattleRegistry.h/.cpp` | morale state block + APIs (3.1); save v3 capture/restore; per-team general records | sections 3.1, 3.5 |
| `BattleSave.h` | v3 fields; SchemaVersion 3; restore accepts 2 | section 3.5 |
| `BattleStateHash.cpp` | block 10 (unit-level morale terms, after the pool block) | section 3.4 |
| `UnitActor.h/.cpp` | OfficerCount + bIsGeneral authoring properties; SpawnEntities plumbs the spec | section 3.3 |
| `UnitSyncProcessor.cpp` | the MoraleBonus correction + the Fatigue derivation reversion + the D-4 mirror move (AmmoPool writer) | section 3.2 |
| `BattleDeathPath.cpp` | the receiver's officer branch (events + spike + general bookkeeping) | section 4.7 |
| `BattleGameplayTags.h/.cpp` | `TAG_Battle_Unit_OfficerDied`, `TAG_Battle_Team_GeneralDied` (native, the H1 lesson) | section 4.7 |
| NEW `BattleMorale.h/.cpp` | the seam: inputs struct, EvaluateMorale, decay forms, fatigue model + table, rout FSM, rally, FMoraleCVars + FMoraleWeights/FatigueConstants | sections 4.1-4.5, 4.12 |
| NEW `MoraleTickProcessor` + `UBattleMoraleTickEffect` + `UBattleMoraleExecCalc` + the fatigue GE catalog | the pipeline (PR-scoped for the machine) | sections 4.8-4.10 |
| `BattleExecCommands` | `Battle.Morale.Toggle/Dump/SpawnRout` (PR-scoped) | section 4.11 |
| `BattleDebugSubsystem` | metrics (MoraleMs, TickAgeMax, Routs, Rallies, Shattered, OfficerDeaths, AuraActive) + overlay rows | B.2/B.4 |
| `BattleMovementSoakController` | SoakRout variant | section 5 |
| `MassBattleFuncTestHarness` + `BattleFuncTestActor` | S7_RoutCascade + OfficerDeath + EffectEndToEnd + ToggleBehaviors actors/factories | section 5 |
| `ServoProcessor` / `UnitLocomotion` | NONE (a rout flee is a normal move order) | A12 discipline |

## 7. Test-gate cross-reference

| Gate | Tier | Test | Seams exercised | Contract rows |
|------|------|------|-----------------|---------------|
| T4.1 | Unit | `Battle.Unit.Morale.Stack` | `EvaluateMorale`, decay forms, `EvaluateRoutTransition` threshold order | MC2, MC5 |
| T4.2 | Unit | `Battle.Unit.Morale.Fatigue` | `AccumulateExertion`, `EvaluateFatigueState`, `GetFatigueMultipliers` | MC3 |
| T4.3 | Func | `Battle.Func.S7_RoutCascade` | end-to-end FSM + flee + cascade + rally | MC5, MC6, MC7 |
| T4.4 | Func | `Battle.Func.OfficerDeath` | death-path branch + events + shock | MC8 |
| T4.5 | Func | `Battle.Func.EffectEndToEnd` | the GE catalog + UnitSync loop | MC4 |
| T4.6 | Func | `Battle.Func.ToggleBehaviors` | guard/impetuous + phase-3 toggles | MC9 |
| T4.7 | Func | `DeterminismTwoRun` (morale-extended) | the whole hash incl. block 10 under the pin | MC10 |
| T4.8 | Unit/fixture | staleness probe (the perf-fixture pattern) | tick age bound under load | MC1 |
| -- | Unit | `Battle.Unit.Combat.SaveRoundTrip` (v3) + `HashTerms` (block 10) | the state spine | sections 3.4, 3.5 |
| -- | Unit | `Battle.Data.EduRoundTrip` (16-key) + `SpawnComposition` (unchanged count) | the data spine | section 3.3 |

## 8. ADR proposals (owner-applied per AGENTS section 7)

Numbering continues the ranged spec's ADR-27..31 block (the owner reconciles pending numbers when applying).

| ADR | Proposal |
|-----|----------|
| ADR-32 | Morale and fatigue are UNIT-level state (registry + GAS attributes on the unit actor); there is no per-entity morale fragment. The rout decision, the fatigue state, and every contribution input live at unit granularity; entities observe only the shared-fragment multipliers and the orders the FSM issues. |
| ADR-33 | The E3 tick mechanism is the periodic infinite-duration MoraleTick GE (owner decision 2026-10-31): one effect per unit actor, period 0.25-0.5 s, one exec-calc calling the pure seam. Sim-fed inputs reach the exec-calc as SetByCaller magnitudes for effect-owned terms and through the registry's input block for continuously-varying terms (the disclosed deviation: active effect specs are not mutable through public API). The deterministic-tick pin (a sim-forced period execution under `Battle.Morale.DeterministicTick`) is the sanctioned T4.7 carve-out. |
| ADR-34 | Rout and rally drive the existing order surface: flee = auto-flagged Move with the RC7 order-serial guard; rally = auto-flagged Stop; engagement mass-release rides `FlagUnitEngagementsDead`. No new order ETypes, no processor writes to the servo, the A12 discipline extended to morale. |
| ADR-35 | Officer identity is a fragment value (`FBaseStatsFragment.bOfficer`, the last-N roster entities), never a tag and never a composition; the declared-but-unstamped `FOfficerTag` is deleted. Officers are extra roster entities (SoldierCount + OfficerCount) whose front-center placement is a layout-seam concern, not a data-model concern. |
| ADR-36 | Combat effectiveness flows ONLY through GAS `*Mod` attributes into UnitSync's fixed derivation (ADR-09 restated for the effect catalog); the melee math and the servo never read morale or fatigue directly. The Phase 0 placeholder couplings (`MoraleBonus = Snapshot->Morale`; `FatigueAttackMult = 1 + Fatigue*0.01`) are corrected this phase. |
| ADR-37 | Morale constants ship as golden-locked placeholders honoring the community model's structure (weights, bands, thresholds, decay forms; ADR-30's posture continued); the Phase-6 T6.1 audit swaps ported values with diff review. The 4b run (OrderLatency, TerrorAura) lands as its own brief when the 4a report's section 5 names it. |

## 9. Sequencing rationale

Phase 4a executes after the phase-3 audit landing (2026-10-30, HEAD `4357321`) because morale consumes exactly what that state proves: a casualty feed (melee + ranged death paths, both observable), engagement geometry (records + tallies + envelopes at 4 Hz), an auto-order pattern with player-override semantics (RC7), a GAS mirror convention (A14), and a state-hash discipline that survived a real determinism hunt. Deferring morale further would leave the effect catalog unconsumed (T4.5's loop needs a real writer) and the rout FSM without its casualty spike; pulling it earlier would have built the stack against an unproven spine.

The 4a/4b split (owner decision 2026-10-31): 4a ships the gated scope (T4.1-T4.8 all belong to one mechanism family -- the stack, the FSM, the effects); OrderLatency and TerrorAura are order-surface and enemy-aura work respectively, ungated by the T4 table, and each carries rework risk in shared surfaces (the order queue; the aura input's enemy half). The phase-2 precedent (movement cleanup + engagement as two runs in `phase-2/`) is the shape: one spec, two briefs, the second written at its kickoff against the first's report.

## 10. Extension points (designed-for, NOT built)

| Future | Seam |
|--------|------|
| `OrderLatency` (4b) | the order APIs gain a delay queue keyed on the attribute; the parked-order machinery (A7) is the precedent for parked-but-countdown state |
| `TerrorAura` (4b) | the aura input's mirror against enemy envelopes; the EDU `frighten_*` tokens join FromEdu with the same loud-map posture |
| Fatigue rest/recovery | `AccumulateExertion` takes the rate block; a negative-rest rate is a table row, not a mechanism change |
| Climate/terrain fatigue (`stat_heat`, TerrainPenalty) | the `FatigueRateScale` slot + a ground/heightfield material lookup at the cluster cadence |
| WeatherRain | writes `RangedAttackMod` + range scale (the ranged spec's section 10 row, unchanged) |
| Per-entity morale variance (stragglers break first) | the FSM stays unit-level; a variance term would stamp intent fragments at the assign pass -- explicitly not this design |
| Morale-visible UI (Phase 5) | `GetUnitMoraleState` + the overlay rows are the read surface; representation never reads the registry directly |
| Army-level morale decisions (Phase 6) | the per-team shock term + the general record are the aggregate seeds |
