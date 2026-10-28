# Ranged Stack -- Design Specification

| | |
|---|---|
| **Companion docs** | `Docs/spec/architecture.md` (parent design: §3.1-3.2, §4 pipeline rows 9-10, §9.3, §10.1-10.4, §11.5, §12; ADR log) · `Docs/spec/roadmap-and-testing.md` (Phase 3 scope, T3 gate table, B.0-B.8) · `Docs/spec/engagement-and-melee.md` (the melee machinery this spec reuses; §10 ranged extension point) · `Docs/AGENTS.md` (standing protocol) |
| **Status** | Draft 1 · 2026-10-29 · owner review pending. Elaborates architecture §9.3 into the Phase 3 implementation contract. On conflict, the architecture doc wins (AGENTS.md precedence); corrections land per AGENTS §4. |
| **Engine pin** | UE 5.8.4 (ADR-13) |
| **Audience** | Phase 3 brief author and the implementing agent (cited sections only; token discipline per AGENTS §1) |
| **Owner-approval status** | §8 ADR rows are PROPOSALS pending the owner's architecture-log commit (AGENTS §7; engagement precedent ADR-22..26) |

---

## 0. Behavior contract (the acceptance table)

Bows and crossbows at volley scale from ONE system: a pooled SoA projectile store, one ballistic solver, and impact resolution through the same directional-defense machinery melee uses. Every row below is observable in sim state (pool contents, fragment fields, tallies, hashes), never in visuals. Roadmap scenario asserts (B.3, S6) and gate T3.x rows cite these.

| ID | Behavior | Contract |
|----|----------|----------|
| RC1 | Volley integrity | A formed missile unit fires by reload cycle: only entities whose reload has elapsed, that hold ammo, that are within weapon range, and that pass the friendly-fire rule release shots; per-entity reload phases are handle-hash staggered so volleys never serialize (ADR-05 discipline). Rank-limited participation: only the front `VolleyRanks` ranks fire (arced) / front rank (flat). Zero per-arrow actors, zero per-arrow Mass entities. |
| RC2 | Trajectory classes from one solver | Bows fire arced solutions that can drop onto a target behind a friendly frontage; crossbows fire the SAME solver's solution at a profile speed that makes the path near-flat and are gated on lane clearance. The trajectory class is a fire-rule property of the weapon profile (`bIndirect`), never a second integrator. |
| RC3 | LOS truth | Occlusion is sampled from the heightfield (cost grid `CellHeight`, bilinear); arced weapons require frontage clearance only, flat weapons require clearance along the whole ground-projected lane. Golden hill cases: a hill between shooter and target blocks a flat shot; equal-height flat ground never false-occludes; the boundary cell case is deterministic. |
| RC4 | Friendly fire | The fire DECISION holds when the rule says hold (arced: friendly density at the impact zone above tolerance; flat: friendly body inside the lane corridor). The impact itself is team-blind: a dispersed arrow that lands on a friendly resolves against it exactly as against an enemy (impact-point rule, arch §9.3). Both halves are separately demonstrable in S6. |
| RC5 | Ammo economy | Every shot decrements exactly one shooter's `AmmoCount`; a unit's aggregate ammo is the sum of its entities'; the unit out-of-ammo flag flips exactly when the aggregate hits zero; out-of-ammo entities never roll a shot (no negative ammo, no reload zombie). `AmmoPool` (GAS attribute, already in the closed set) mirrors the fraction for UI. |
| RC6 | Impact resolution reuses melee defense | An arrow hit builds a transient one-roll context shaped like a pair defense mirror (attack from `RangedAttack` x `RangedAttackMult` x `AccuracyMult`, defense from the victim's skill/shield/armor with the incoming angle quantized through the SHARED sectorizer): shield protects frontal arcs only, AP halves armor, lethality multiplies. Projectiles never form persistent pairs; a landed hit kills through the existing death path (FDeadTag defer -> signal -> roster). |
| RC7 | Skirmish | A skirmish-toggled missile unit auto-retreats when an enemy unit closes inside `SkirmishBreakDistM`, keeps retreating with hysteresis until clear, then resumes fire; `FireAtWill=off` (hold fire) suppresses shooting without suppressing retreat. Auto-retreat never overrides a player-issued order (order-serial guard). |
| RC8 | Determinism | Same seed, same binary, same machine: per-tick state hash identical across two 5-minute mixed clash+volley runs, projectiles in flight included as hash terms (R9 scope). |
| RC9 | Scale budget | 1,000 projectiles in flight cost < 0.3 ms sim per frame (stepping + impact queries + hash terms); the pool never allocates mid-volley (reserve at battle start; acquire beyond capacity fails closed into a drop metric, never grows). |

## 1. Scope and non-goals

In scope (Phase 3): EDU ranged fields (`stat_sec`, missile rows of `stat_pri`), the ranged data spine (fragment, registry state, tags), the ballistic solver seam, dispersion, heightfield LOS, rank-limited volley selection, friendly-fire rules, the pooled projectile store (SoA subsystem), projectile stepping and impact resolution, per-entity ammo + unit out-of-ammo, SkirmishMode auto-retreat + FireAtWill/HoldFire toggles, S6 + skirmish scenarios, M4 demo (gray-box), T3.1-T3.6.

Out of scope (recorded, not accidental):

- Morale effects of casualties (Phase 4 reads the same death signals; the fresh-casualty spike term is fed for free).
- Fatigue from missile fire, wet-bowstring weather (`WeatherRain` -> `RangedAttackMod`, range) -- the multiplier slot exists in `FUnitStatsSharedFragment` (`AccuracyMult`, `RangedAttackMult`); the weather system that writes it is Phase 4+.
- Weapon switching between `stat_pri` and `stat_sec` mid-combat (archers fight with melee backup when engaged): v1 maps `stat_sec` attack/defense as the melee stats of missile units; an explicit swap state machine is an extension point (§10).
- Artillery / gunpowder / thrown weapons beyond the two profile classes (arrow arced, bolt flat); the profile table is where javelins/bullets land later.
- Projectile representation beyond gray-box (arrow ISM pools, volley Niagara batching, arrow-stick decals are Phase 5; §10 records the seam).
- Save/load of in-flight projectiles (entity payloads and pool state mature at T6.3; registry-level toggles DO join the save schema, §3.5).
- Statistical far-LOD ranged attrition (open decision 2, Phase 6).
- Melee-vs-garrison unchanged (A9 posture); garrisoned units are additionally not valid ranged IMPACT targets this phase (extension point §10).

## 2. Architecture summary

One sentence: **ranged is a fire-control pass over the existing unit/formation spine plus a pooled SoA projectile store, and an arrow lands through the melee defense machinery -- a consumer of the Phase 0-2 spine, not a new system.**

- The data spine grows one per-entity fragment (`FRangedStateFragment`) on ranged-capable compositions only (arch §3.2: "a spearman has no ranged fragments, so the ranged-fire processor never touches his chunks"), zero new shared fragments (R-C4 doctrine), zero new GAS attributes (`AmmoPool`, `RangedAttackMod` already exist in the closed set).
- The projectile store is a subsystem-owned structure-of-arrays pool, NOT Mass entities, NOT actors (arch §9.3 "pooled instanced projectiles"; 1,000 arrows must not cost 1,000 archetype members or handle slots). Pool entries are plain floats + handles; identity is (index, generation) miniaturized from the registry handle pattern.
- Threading: `RangedFireProcessor` and `ProjectileStepProcessor` are GAME-THREAD processors in the `Battle` group. Reality wins over the architecture diagram's "row 9 worker" label: the fire pass reads the heightfield and the spatial-hash subsystem (game-thread owners, A5) and writes the pool (game-thread subsystem); the step pass queries last tick's hash for impacts. Both record the deviation per AGENTS §4 (evidence: the Phase 2 worker contract forbids subsystem access worker-side, and the ranged pass needs three subsystems). Worker-side math still lives on the seams; the processors stay thin shims (B.0).
- Impact resolution reuses `FMeleeMath` arc bands + the `FCombatSolver` sectorizer (ONE quantizer, now THREE consumers: contact-ring steering, melee defense, projectile incoming angle) and reuses the F2 death path. No second damage system exists.
- Determinism: rolls draw from the seed-derived hash family (`CombatRoll01` / `StaggeredPhaseOffset` -- the B-2 one-home rule extended); pool iteration is index-ordered; state-hash terms append after the combat block (F5 pattern).

## 3. Data model

### 3.1 Per-entity ranged fragment (new file `Plugins/MassBattle/Source/MassBattle/Public/BattleRangedFragments.h`)

Blittable rules per arch §3.1 (handles, fixed scalars, enums; no FString / UObject* / TArray). Stamped at spawn fixup from the EDU row; immutable fields (kind, profile index) are spawn-truth, mutable fields are sim state.

```cpp
enum class EBattleProjectileKind : uint8 { None = 0, Arrow = 1, Bolt = 2 };

USTRUCT()
struct FRangedStateFragment : public FMassFragment
{
    GENERATED_BODY()
    // -- spawn-immutable --
    UPROPERTY() EBattleProjectileKind Kind = EBattleProjectileKind::None;
    UPROPERTY() float RangeM = 0.f;            // weapon range, meters (seam convention)
    UPROPERTY() uint16 InitialAmmo = 0;        // ammo per man at spawn
    // -- sim state (hash terms, §3.4) --
    UPROPERTY() int16 AmmoCount = 0;           // decrements per shot; clamped at 0
    UPROPERTY() float ReloadTimer = 0.f;       // seconds to next eligibility; staggered at spawn
    UPROPERTY() uint16 ShotsFired = 0;         // lifetime counter (T3.3 accounting + roll seed input)
};
```

Archetype discipline: the fragment rides ranged-capable compositions only, so the archetype count grows by the bow-capable compositions (expected 6 -> 8: one new infantry composition x alive/dead; crossbow is a fragment VALUE, `EBattleProjectileKind::Bolt`, not a new composition). `FBowTag` (spawn-plumbed since Phase 0, never yet stamped) rides the same rows as the debug/overlay marker; `FCrossbowTag` stays unbuilt (§10). The `SpawnComposition` test re-signs to the measured count with the growth itemized in the report -- a disclosed re-sign, same policy class as the T0.2 golden.

### 3.2 Registry-owned ranged state (`BattleRegistry.h/cpp`)

Per-unit, in `FUnitRuntimeState` (bookkeeping, not per-entity):

```cpp
bool bFireAtWill = true;    // toggle family (arch §10.4); default on
bool bSkirmish = false;     // default per-class at spawn (skirmisher rows flip it on)
bool bOutOfAmmo = false;    // flipped when the entity-sum aggregate hits zero (RC5)
```

APIs: `SetUnitFireToggles(Handle, bFireAtWill, bSkirmish)` (reject + EXEC_RESULT line on bad handle), `GetUnitFireToggles`, aggregate reader `GetUnitAmmoFraction(Handle)` (sum / (InitialAmmo x initial roster); -1 for non-ranged). The `AmmoPool` GAS attribute mirrors the fraction for UI (UnitSync writes it; authority stays registry-side per arch §10.1). Out-of-ammo never auto-flips back (no resupply v1).

### 3.3 Projectile pool (new `BattleProjectilePool.h/cpp`, subsystem `UBattleProjectileSubsystem`)

Plain SoA struct + a thin `UWorldSubsystem` wrapper (game thread):

```cpp
struct FBattleProjectilePool
{
    static constexpr int32 InvalidIndex = -1;
    void Reserve(int32 Capacity);                    // battle start only; never mid-volley
    int32 Acquire();                                 // InvalidIndex + Drops++ when full (fails closed)
    void Release(int32 Index);                       // generation++ on the slot
    bool IsValid(int32 Index, uint32 Generation) const;
    // SoA arrays, all preallocated to Capacity:
    TArray<float> PosX, PosY, PosZ;                  // meters (seam convention)
    TArray<float> VelX, VelY, VelZ;                  // m/s
    TArray<float> GravityScale;                      // profile gravity multiplier
    TArray<float> TTL;                               // seconds; ground hit ends early
    TArray<uint8>  Kind;                             // EBattleProjectileKind
    TArray<uint32> Generation;                       // slot stamp (stale-index rejection)
    TArray<FUnitHandle> OwnerUnit;                  // shooter's unit (attribution + friendly checks)
    // free-list: index stack; live count; Drops counter (metric)
};
```

Default capacity 4,096 (CVAR `Battle.Ranged.PoolCapacity`; reserve at battle start alongside the engagement scratch, EC9 discipline). Entries are NOT serialized v1 (§3.5). The `ECombatHashFlags` reserved byte on `FBattleHashEntry` gains its first defined bit: `RangedCapable = 1<<0` (stamped by the hash builder from the loadout; targeting priority + impact debug read it).

### 3.4 State-hash terms

Per-entity ranged block appended AFTER the charge block in the same sorted-handle pass (F5 / spec engagement §3.4 pattern; fixed term count with zero-fill when the fragment is absent keeps the stream position-stable): `AmmoCount` (int), `ReloadTimer` (1e-2 s), `ShotsFired` (int). Kind/RangeM/InitialAmmo are spawn-immutable and excluded (base-stats precedent). Pool block appended after the per-entity blocks: live count, then per live entry in INDEX order `PosX/PosY/PosZ, VelX/VelY/VelZ, TTL` (all 1e-2) -- index order is deterministic by construction (RC8). `RingPos`-class derived values never hash.

### 3.5 Save/load schema (v1 -> v2)

`FUnitSaveEntry` += `bFireAtWell/bFireAtWill/bSkirmish/bOutOfAmmo` (booleans) and `InitialAmmo/InitialRosterSize` (for the fraction denominator); `SchemaVersion` -> 2; restore accepts 1 (legacy: toggles default, out-of-ammo false) and 2. Entity ammo and pool state stay unsaved (T6.3 owns entity payloads, by design -- the A8 "grows with the schema" discipline). Round-trip test extended: toggle states + out-of-ammo survive capture/clear/restore.

## 4. Mechanics

### 4.1 Ballistic solver (seam `BattleBallistics.h/cpp`, T3.1)

One solver, weapon profiles, meters, pure and deterministic:

```cpp
struct FBallisticSolution { FVector LaunchDir; /* unit: X/Y horizontal, Z up */ float FlightTimeS; bool bValid; };
FBallisticSolution SolveLaunch(const FVector2D& FromM, float FromZM, const FVector2D& ToM, float ToZM,
        float SpeedMps, float GravityMps2);
```

Low-arc root of the standard equation: `tan(theta) = (v^2 - sqrt(v^4 - g*(g*d^2 + 2*dh*v^2))) / (g*d)`, `d` = horizontal distance, `dh` = target height minus launch height; `bValid=false` when the discriminant is negative (out of envelope: the volley pass holds fire, never force-fires). Flight time = `d / (v*cos(theta))`. Flat profiles (bolt) use high `SpeedMps` so the solved path is near-linear; nothing branches on trajectory class INSIDE the solver (RC2). Stepping is semi-implicit Euler at the fixed sim DT: `Pos += Vel*DT; Vel.Z -= Gravity*GravityScale*DT` -- same integrator family as `IntegrateProcessor`, no per-substep allocation.

### 4.2 Weapon profiles and dispersion (seam, T3.1)

```cpp
struct FProjectileProfile   // per EBattleProjectileKind; constants golden-locked (A6 posture)
{
    float MuzzleSpeedMps;   // Arrow 35, Bolt 60 (placeholders)
    float GravityScale;     // Arrow 1.0, Bolt 1.0
    bool  bIndirect;        // Arrow true (fires over friendly frontage), Bolt false (needs lane)
    float DispersionDeg;    // cone half-angle at AccuracyMult 1: Arrow 4, Bolt 2 (placeholders)
    float HitRadiusM;       // swept-segment hit radius: 0.5 (both, placeholder)
    bool  bArmorPiercing;   // Bolt true (M2TW crossbow feel), Arrow false
};
FVector PerturbDirection(const FVector& Dir, float ConeHalfDeg, uint32 Seed); // Knuth-hash family (B-2 one-home)
```

Cone actually applied = `DispersionDeg / max(AccuracyMult, Epsilon)` read from `FUnitStatsSharedFragment` (the Phase 0 `AccuracyMult` slot finally consumes). Two orthogonal angular draws per seed, bounded by the cone (rejection-free: scale, never resample). Seed = `Hash(EntityIndex, EntitySerial, ShotsFired)` -- run-invariant (the A13 rule; never `GFrameCounter`).

### 4.3 Heightfield LOS (seam + `FBattleCostGrid` extension, T3.2)

Net-new grid API (disclosed; the only `BattleCostGrid` change): `float SampleHeight(const FVector2D& WorldPosM) const` -- bilinear over the 4 surrounding `CellHeight` cells (flat fixture reads 0 everywhere, so the flags-off baseline is untouched). LOS march on the seam:

```cpp
bool MarchClear(const FBattleCostGrid& Grid, const FVector2D& FromM, float FromEyeZM,
        const FVector2D& ToM, float ToZM, float ClearanceM);
```

2D DDA over crossed cells; at each crossing, the ray's height must exceed `SampleHeight(cell) + ClearanceM`. Arced fire calls it on the FRONTAGE segment only (shooter eye to a point `FrontageClearM` ahead, default 8 m); flat fire calls it on the full lane to the target. Golden cases (T3.2): hill-between blocks; equal heights never block (the epsilon rule: strictly-greater required); boundary-crossing deterministic; a hill shorter than the arc apex does not block an arced profile's frontage check by construction (frontage only).

### 4.4 Volley selection (seam `BattleRanged.h/cpp`, T3.4 scenario consumer)

Pure step function over a formation view (no world):

```cpp
struct FVolleyShooterView { FMassEntityHandle Entity; FUnitHandle Unit; FVector2D PosM; float EyeZM;
        EBattleProjectileKind Kind; int16 Ammo; float ReloadTimer; uint16 ShotsFired; int32 Rank; };
struct FVolleyTargetView  { FVector2D PosM; float ZM; FUnitHandle Unit; };
void SelectVolleyShots(TArray<FVolleyShooterView>& InOutShooters, const FVolleyTargetView& Target,
        const FProjectileProfile& Profile, const FBattleCostGrid& Grid,
        const TArray<FVector2D>& FriendlyLaneBlockersM, float AccuracyMult,
        float FriendlyImpactTolerance, TArray<FVolleyShot>& OutShots /* preallocated */);
```

Eligibility per shooter (RC1): reload elapsed, ammo > 0, rank <= profile rank limit (`VolleyRanks` cvar; flat = 1), horizontal distance in `(MinRangeM, RangeM]`, LOS pass (§4.3), friendly rule pass (§4.5). Passing shooters get a solved + dispersed shot (§4.1-4.2) appended to `OutShots` and their view mutated (ammo--, reload reset, shots++). Order of emission = shooter array order (deterministic; the processor feeds roster order).

### 4.5 Friendly-fire rules (RC4; R12/T6.1 port-or-deviation posture)

Fire-decision rules, both CVAR-tunable, both demonstrable in S6:

- **Arced (indirect):** estimate the impact zone as the disc of radius `DispersionDeg`-scaled spread around the aim point; hold fire if the count of FRIENDLY entities inside exceeds `Battle.Ranged.FriendlyImpactTolerance` (default 0 -- M2TW archers will not shoot into a melee they would hit friends; the stray hits that still happen come from dispersion landing outside the estimate, which is the impact-point half).
- **Flat (direct):** hold fire if any friendly entity's body circle intersects the ground-projected lane corridor (width = `2*BodyRadiusM + HitRadiusM`); blockers are gathered via the spatial hash `QueryCircle` at the corridor midpoint (A5 amendment: impact/lane queries join coverage + mob targeting as sanctioned circle-query consumers; formed-fight targeting still never circle-queries).

Impact resolution itself is team-blind (RC4): the nearest entity within `HitRadiusM` of the swept segment takes the roll regardless of team. The v1 rules above are the documented deviation candidates for T6.1's explicit port-or-deviation resolution.

### 4.6 Impact resolution (RC6; the melee reuse)

On a hit, build the transient one-roll context (the "impact-seeded pair" -- engagement spec §10): incoming horizontal direction -> angle relative to the victim's facing -> `FCombatSolver::SectorIndexFromAngle` (the shared quantizer) -> `FMeleeMath::SkillArcMult` / `ShieldArcWindow` / `ArmorMultiplier` exactly as a melee strike. Attack term = shooter's `RangedAttack` (stamped into the pool entry at fire time) x `RangedAttackMult` x `AccuracyMult`; defense term = victim defense per the arc bands; hit chance = `FMeleeMath::HitChance` with the same floor/cap CVARs; lethality multiplies. A landed hit on a mount-pool victim drains the mount pool first (composite model), else defers `FDeadTag` on the victim through the command buffer -- the F2 death path, unchanged. Misses live on as ground impacts (decal/representation seam, Phase 5). The roll draws `CombatRoll01(ShooterIndex, ShooterSerial, ShotsFired)` -- run-invariant seed inputs only.

### 4.7 Ammo accounting and out-of-ammo (RC5, T3.3)

Every emitted shot decrements the shooter fragment at fire time (the pool entry carries attribution). The fire pass accumulates per-unit sums; the registry flips `bOutOfAmmo` when a ranged unit's sum hits zero and stamps the GAS `AmmoPool` attribute to 0 for UI. Invariants asserted at unit tier: shots == decrements (over randomized cycles), unit aggregate == entity sum, no negative ammo (clamp + loud drop metric if ever attempted), out-of-ammo flips exactly once. Non-ranged units never enter the pass (query composition).

### 4.8 Skirmish behavior (RC7, T3.5)

Unit-level, registry-driven, no new processor: the fire pass computes each skirmish-toggled unit's nearest enemy distance (from engagement-record geometry at 4 Hz, not per-entity scans -- the cluster pass already maintains unit envelopes). State function on the seam:

```cpp
ESkirmishDecision { Hold, Fire, Retreat };
ESkirmishDecision EvaluateSkirmish(float NearestEnemyDistM, bool bReloadReady, bool bFireAtWill,
        bool bRetreating, float BreakDistM, float ResumeDistM);
```

Retreat = the registry self-issues a Move order directly away from the nearest enemy anchor (flagged auto; a player order-serial bump cancels it); hysteresis via `ResumeDistM = 1.3 * BreakDistM`. `FireAtWill=off` suppresses Fire but not Retreat (RC7). Default `bSkirmish` at spawn: on for `class missile` rows (EDU-derived), off otherwise.

### 4.9 Processors (game-thread, group "Battle")

- `ProjectileStepProcessor` -- FIRST in the group: steps every live pool entry (§4.1 integrator), ground-hit check via `SampleHeight`, swept-segment entity query against last tick's spatial hash, impact resolution (§4.6), release, metrics (in-flight, hits, friendly hits, drops), hash terms (§3.4).
- `RangedFireProcessor` -- after `AssignPairs`, before `FormationSlotResolve` (skirmish retreat must reach the servo this tick): advances reloads, evaluates skirmish, selects the volley (§4.4), writes pool entries, decrements ammo, aggregates out-of-ammo, stamps `ECombatHashFlags::RangedCapable` consumers' data path.
- `BuildSpatialHashProcessor` gains one stamp line (Flags bit from loadout); no other existing processor changes (A12 discipline: `ServoProcessor` untouched -- skirmish retreat is a normal move order to it).
- `Battle.Ranged.Enable` gates the whole block (default 0 until the impact task lands end-to-end, then 1 -- B.8 item 6); `Battle.Ranged.Step`, `Battle.Ranged.Fire`, `Battle.Ranged.Skirmish` sub-flags default 1 behind it. Flags-off = byte-identical movement/melee baseline (unchanged T1.8/T2 soaks prove it).

### 4.10 Unit fire control (toggle family, arch §10.4)

`SetUnitFireToggles` + ExecBridge `Battle.Ranged.Toggle <unit> <FireAtWill|Skirmish> <0|1>` + `Battle.Ranged.Dump <unit>` (ammo fraction, per-entity ammo histogram, toggles, pool attribution) -- the A4/F4 order-surface pattern. The demo's HUD key for toggles is PR-scoped (editor-owned input, PR-2 precedent).

### 4.11 Tunables (CVAR block `Battle.Ranged.*`; A6 placeholders, T6.1 owns the audit)

| CVar | Default | Meaning |
|------|---------|---------|
| `Battle.Ranged.Enable` | 0 -> 1 at landing | master flag (B.8 item 6) |
| `Battle.Ranged.Step` / `.Fire` / `.Skirmish` | 1 | sub-flags |
| `Battle.Ranged.PoolCapacity` | 4096 | pool reserve (battle start only) |
| `Battle.Ranged.VolleyRanks` | 2 | arced firing ranks (flat = 1, fixed) |
| `Battle.Ranged.ReloadInterval` | 3.0 s | base reload (staggered per entity) |
| `Battle.Ranged.MinRangeM` | 8 | minimum engagement distance |
| `Battle.Ranged.FrontageClearM` | 8 | arced LOS check length |
| `Battle.Ranged.FriendlyImpactTolerance` | 0 | arced hold-fire friendly count |
| `Battle.Ranged.SkirmishBreakDistM` | 25 | auto-retreat trigger |
| `Battle.Ranged.SkirmishResumeFactor` | 1.3 | hysteresis multiplier |
| `Battle.Ranged.SoakVolley` | 0 | soak variant flag (nightly; 0 = baseline identity) |
| `Battle.Ranged.StateTree` | -- | (not present; FSM stays the registry pattern per ADR-24) |

## 5. Scenario and demo plan

- **S6_Volley (T3.4, func):** one archer unit vs one formed infantry unit at 100 m, flat ground; seeded; asserts (a) dispersion -- impact points within the profile cone bound; (b) LOS -- a scripted hill variant blocks flat-profile fire (arced passes); (c) friendly-fire -- a friendly unit walked into the lane holds flat fire, and a scripted stray-hit case damages a friendly; (d) ammo -- decrements == shots, aggregate reaches zero, out-of-ammo flips. Evidence: `EXEC_RESULT S6_Volley ...` lines + `Battle.Ranged.Dump`.
- **SkirmishBehavior (T3.5, func):** archers with skirmish on, cavalry ordered at them: retreat triggers inside break distance, unit keeps facing, resumes fire past resume distance; hold-fire variant fires never but still retreats; player Move order overrides the auto-retreat.
- **M4 demo (gray-box):** ExecBridge `Battle.Ranged.SpawnVolley {line|skirmish}` fixtures (dictionary-keyed, B-3 lesson) + the demo game-mode sample; arrows render as the existing debug overlay segments (z-heighted) until Phase 5's ISM/Niagara pass (PR-scoped).
- **Soak variant:** `Battle.Ranged.SoakVolley=1` rides `BattleMovementSoakController` (the SoakClash precedent): ~25% of spawned units become archer rows issuing fire orders; FINAL line += `volleys=%d inFlightPeak=%d friendlyHits=%d poolDrops=%d`; flag off = byte-identical baseline (R-C9 pattern).

## 6. Touch-point table

| Existing thing | Change | Why |
|---|---|---|
| `BattleEduParser` schema | +`stat_sec` row (11-value arity, same kinds as `stat_pri`); `SchemaFieldCount` 14 -> 15 | ranged rows are data, arch §10.2 |
| `golden_edu_v1.txt` | +2 rows (Peasant Archers, Mercenary Crossbowmen); existing 2 rows gain `stat_sec ... no ...` lines | T0.2 golden re-sign (disclosed) |
| `FBattleRowStats::FromEdu` | missile-kind branch: `stat_pri[2] != "no"` -> RangedAttack = pri[0], Melee* = sec[0..1], Range/Ammo = pri[3..4], bBow | the game-side view of ranged rows |
| `BattleSpawnLibrary` fixup | stamp `FRangedStateFragment` (+ `FBowTag`) on ranged rows | §3.1 |
| `BattleTypes.h` | `EBattleProjectileKind`; `FUnitRuntimeState` += toggle/out-of-ammo fields | §3.2 |
| `BattleStateHash.cpp` | ranged block after charge block + pool block | §3.4, RC8 |
| `BattleSave.h` / registry capture-restore | schema v2 fields | §3.5 |
| `BuildSpatialHashProcessor` | stamp `ECombatHashFlags::RangedCapable` | §3.3 |
| `BattleDebugSubsystem` | metrics: InFlightPeak, Shots, Hits, FriendlyHits, PoolDrops, StepMs, FireMs; overlay: trajectory segments + impact markers | B.2/B.4 |
| `BattleMovementSoakController` | SoakVolley variant | §5 |
| `MassBattleFuncTestHarness` + `BattleFuncTestActor` | S6_Volley + SkirmishBehavior actors/factories | §5 |
| `ServoProcessor` / `UnitLocomotion` | NONE (skirmish retreat is a normal move order) | A12 discipline |

## 7. Test-gate cross-reference

| Gate | Tier | Test | Seams exercised | Contract rows |
|------|------|------|-----------------|---------------|
| T3.1 | Unit | `Battle.Unit.Ranged.Ballistics` | `SolveLaunch`, `PerturbDirection`, stepper | RC1, RC2 |
| T3.2 | Unit | `Battle.Unit.Ranged.LOS` | `MarchClear`, `SampleHeight` | RC3 |
| T3.3 | Unit | `Battle.Unit.Ranged.AmmoAccounting` (+ pool/volley/skirmish-math tests) | `SelectVolleyShots`, pool, `EvaluateSkirmish` | RC1, RC4, RC5, RC7 |
| T3.4 | Func | `Battle.Func.S6_Volley` | end-to-end fire -> flight -> impact | RC1-RC6 |
| T3.5 | Func | `Battle.Func.SkirmishBehavior` | skirmish + toggles | RC7 |
| T3.6 | Perf | `Battle.Perf.Gate3` (fixture `Battle.Ranged.PerfStress`) | step + fire + hash terms @ 1k in flight | RC9 |
| -- | Func | `DeterminismTwoRun` extended (mixed clash+volley) | hash terms incl. pool | RC8 |
| -- | Unit | `SpawnComposition` re-signed; `SaveRoundTrip` v2 | data spine | §3.1, §3.5 |

## 8. ADR proposals (owner-applied per AGENTS §7)

Numbering continues the engagement spec's ADR-22..26 block (the owner reconciles pending numbers when applying).

| ADR | Proposal |
|-----|----------|
| ADR-27 | Projectiles are pool entries in a game-thread subsystem-owned SoA (index + generation identity), never Mass entities and never actors; capacity reserved at battle start, acquisition fails closed. 1,000 arrows must not cost archetype members or entity handles. |
| ADR-28 | Ranged fire and projectile stepping are GAME-THREAD processors (deviation from arch §4 row 9's worker label): the passes read the heightfield, the spatial hash, and the pool -- three game-thread owners -- and the Phase 2 worker contract forbids subsystem access worker-side. The arch §4 table row is amended when applied. |
| ADR-29 | Impact resolution reuses the melee defense machinery (shared sectorizer, arc bands, HitChance, composite pools, F2 death path) through a transient one-roll context; projectiles never form persistent pairs (engagement spec §10 realized). ONE defense model, two ammunition paths. |
| ADR-30 | Ranged constants ship as T3.1-golden-locked placeholders (speeds, dispersion, hit radius, reload, AP posture) with the community model's STRUCTURE; the Phase-6 T6.1 audit swaps ported constants (incl. the friendly-fire rule as explicit port-or-deviation, R12) with diff review. |
| ADR-31 (amendment) | A5 amendment: spatial-hash `QueryCircle` consumers now include projectile lane/impact queries (coverage + mob targeting + ranged); formed-fight targeting still reads slot tables only. |

## 9. Sequencing rationale

Phase 3 executes after the engagement solver (2026-10-27 complete) per the roadmap build order: ranged consumes pairing only at impact (which bypasses it), so it needed melee's DEFENSE machinery, not its lifecycle; and the uniform spine (movement, orders, death path, spatial hash, state hash, save skeleton) is now proven. Deferring ranged further would starve S7/morale of its casualty feed (Phase 4's fresh-casualty spike wants volley kills); pulling it earlier would have built impact resolution with no melee machinery to reuse. The M2TW feel signature (arrow storms) also gates the Phase-5 representation plan (VAT crowd + volley Niagara batching).

## 10. Extension points (designed-for, NOT built)

| Future | Seam |
|--------|------|
| Weapon switching (stat_pri/stat_sec mid-combat) | `FRangedStateFragment.Kind` is spawn-truth v1; a swap state machine would live beside the intent fragment, not in the fire pass |
| Javelins / bullets / artillery | new `FProjectileProfile` rows (the profile table is the whole addition); thrown weapons want a `bIndirect=false` short-range arc |
| Weather (`WeatherRain`) | writes `RangedAttackMod` + range scale through the existing snapshot slot; no ranged change |
| Arrow representation (Phase 5) | pool -> ISM per-instance transforms (one Niagara batch per volley, arch §11.5); the pool's SoA layout IS the render feed |
| Pool save (T6.3) | `FBattleProjectilePool` is plain data; serialize live entries into the schema when entity payloads land |
| Ranged-vs-garrison | `ECombatHashFlags` bits + garrison booking classification decide whether garrisoned interiors are valid impact targets (A9 posture today: not) |
| Volley AI (focus fire, elevated ranks all-fire) | `SelectVolleyShots` takes a target view per call; the caller owns targeting policy |
| FCrossbowTag | only if a composition-level discriminator is ever needed; `Kind` is the functional discriminator |
