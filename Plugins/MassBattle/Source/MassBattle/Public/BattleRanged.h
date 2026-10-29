// BattleRanged.h -- Phase 3 ranged seam (spec ranged-stack §4.4-§4.8, §4.11;
// brief A1): weapon profiles, rank-limited volley selection, ammo accounting,
// and the skirmish decision -- free functions / plain structs, meters,
// deterministic, seed-derived RNG only (the A13 discipline: hash-family
// inputs, never streams, never wall-clock, never GFrameCounter).
//
// Task 0 ships this header + the CVAR block with STUB bodies (the Phase 2
// task-0 pattern); the volley/skirmish implementations land with the seam
// task and are unit-locked by T3.3. Ballistic solving and LOS live in their
// own seam (BattleBallistics.h) -- this seam CONSUMES solutions, it never
// integrates them.
//
// A6 posture (ADR-30): the profile constants below are T3.1-golden-locked
// placeholders honoring the community model's structure; the Phase-6 T6.1
// audit swaps ported constants with diff review, never silently.
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"
#include "BattleCostGrid.h"

/** CVAR block accessors (definitions + defaults in BattleRanged.cpp; the
 *  FCombatCVars pattern). Game-thread processors are the only runtime
 *  consumers; accessors read GetValueOnAnyThread() per the worker-read
 *  contract (engine-api Part B, the audit A-1 verification note). */
namespace FRangedCVars
{
	MASSBATTLE_API bool Enable();                    // Battle.Ranged.Enable (0 until impact lands, B.8 item 6)
	MASSBATTLE_API bool Step();                      // Battle.Ranged.Step
	MASSBATTLE_API bool Fire();                      // Battle.Ranged.Fire
	MASSBATTLE_API bool Skirmish();                  // Battle.Ranged.Skirmish
	MASSBATTLE_API int32 PoolCapacity();             // Battle.Ranged.PoolCapacity
	MASSBATTLE_API int32 VolleyRanks();              // Battle.Ranged.VolleyRanks (arced; flat = 1 fixed)
	MASSBATTLE_API float ReloadInterval();           // Battle.Ranged.ReloadInterval
	MASSBATTLE_API float MinRangeM();                // Battle.Ranged.MinRangeM
	MASSBATTLE_API float FrontageClearM();           // Battle.Ranged.FrontageClearM
	MASSBATTLE_API int32 FriendlyImpactTolerance();  // Battle.Ranged.FriendlyImpactTolerance
	MASSBATTLE_API float SkirmishBreakDistM();       // Battle.Ranged.SkirmishBreakDistM
	MASSBATTLE_API float SkirmishResumeFactor();     // Battle.Ranged.SkirmishResumeFactor
	MASSBATTLE_API bool SoakVolley();                // Battle.Ranged.SoakVolley (nightly variant; 0 = baseline identity)
}

/** Weapon profile per projectile kind (spec §4.2). Constants are A6
 *  placeholders golden-locked at T3.1 (ADR-30): speeds make the bolt's solved
 *  path near-flat (ONE solver, spec §4.1); the trajectory CLASS is the fire
 *  rule (bIndirect), never a second integrator (RC2). */
struct MASSBATTLE_API FProjectileProfile
{
	float MuzzleSpeedMps = 35.f;  // Arrow 35, Bolt 60 (placeholders)
	float GravityScale = 1.f;
	bool bIndirect = true;        // arced: frontage-clearance LOS only (fires over friends)
	float DispersionDeg = 4.f;    // cone half-angle at AccuracyMult 1 (Arrow 4, Bolt 2)
	float HitRadiusM = 0.5f;      // swept-segment impact radius
	bool bArmorPiercing = false;  // Bolt true (M2TW crossbow feel, placeholder)
};

/** The profile table (one row per EBattleProjectileKind != None). */
MASSBATTLE_API FProjectileProfile GetProjectileProfile(EBattleProjectileKind Kind);

/** Volley pass input: one ranged entity's fire-relevant view (spec §4.4).
 *  Plain mirror assembled by the processor in roster order -- the seam never
 *  touches fragments or worlds. */
struct FVolleyShooterView
{
	FMassEntityHandle Entity;
	FUnitHandle Unit;
	int32 RosterIndex = -1;   // canonical identity input (A13, RC8)
	FVector2D PosM = FVector2D::ZeroVector;   // planar position, meters
	float EyeZM = 1.6f;                       // launch height, meters
	EBattleProjectileKind Kind = EBattleProjectileKind::None;
	float RangeM = 0.f;                       // weapon range, meters (fragment-stamped)
	int16 Ammo = 0;
	float ReloadTimer = 0.f;
	uint16 ShotsFired = 0;
	int32 Rank = 0;                           // 0 = front rank
};

/** Volley pass target view (the intent/record enemy, spec §4.4 + brief A11). */
struct FVolleyTargetView
{
	FVector2D PosM = FVector2D::ZeroVector;
	float ZM = 0.f;
	FUnitHandle Unit;
};

/** One emitted shot (the pool write payload). LaunchDir is a unit vector
 *  (X/Y horizontal, Z up); Seed is the deterministic dispersion/roll seed
 *  (hash of entity identity + ShotsFired, computed by the caller so the
 *  pool entry stays plain data). ShotsFired is the shooter's lifetime count
 *  AT EMISSION -- the impact roll's A13 run-invariant count input (spec
 *  §4.6 draws CombatRoll01(ShooterIndex, Serial, ShotsFired)). */
struct FVolleyShot
{
	FMassEntityHandle Entity;
	FUnitHandle Unit;
	EBattleProjectileKind Kind = EBattleProjectileKind::Arrow;
	FVector LaunchPosM = FVector::ZeroVector;
	FVector LaunchDir = FVector::ForwardVector;
	float FlightTimeS = 0.f;
	uint32 Seed = 0;
	uint32 ShotsFired = 0;
	/** The impact roll's canonical count input (roster slot << 16 | shots
	 *  fired -- BattleTypes' A13 fold); the pool stores it verbatim. */
	uint32 RollCountInput = 0;
};

/** Rank-limited volley selection (spec §4.4, RC1/RC4). Eligibility per
 *  shooter: reload elapsed, ammo > 0, rank within the profile limit
 *  (VolleyRanks cvar arced / front rank flat), distance in (MinRangeM,
 *  RangeM], LOS pass, friendly-rule pass. Passing shooters mutate in place
 *  (ammo--, reload reset, shots++) and append a solved+dispersed shot to
 *  OutShots (preallocated by the caller; never grown per entity). Returns
 *  the shot count. Emission order = shooter array order (deterministic).
 *
 *  FriendlyLaneBlockersM: friendly body positions for the flat-lane rule
 *  (A7); the arced impact-zone estimate uses the same list around Target.
 *  STUB until the seam task: returns 0, mutates nothing. */
MASSBATTLE_API int32 SelectVolleyShots(
	TArray<FVolleyShooterView>& InOutShooters,
	const FVolleyTargetView& Target,
	const FProjectileProfile& Profile,
	const FBattleCostGrid& Grid,
	const TArray<FVector2D>& FriendlyLaneBlockersM,
	float AccuracyMult,
	int32 FriendlyImpactTolerance,
	TArray<FVolleyShot>& OutShots);

/** Unit ammo fraction for the registry aggregate (RC5): AmmoSum over
 *  (InitialAmmo * RosterSize); -1 for non-ranged (InitialAmmo == 0). */
MASSBATTLE_API float ComputeAmmoFraction(int32 AmmoSum, int32 InitialAmmo, int32 RosterSize);

/** Skirmish decision (spec §4.8, RC7). Hysteresis: Fire requires
 *  NearestEnemyDistM > BreakDistM (or bRetreating and > ResumeDistM);
 *  Retreat inside BreakDistM. FireAtWill gates Fire only -- Retreat still
 *  fires (hold-fire is not hold-ground). STUB until the seam task: Hold. */
enum class ESkirmishDecision : uint8 { Hold = 0, Fire = 1, Retreat = 2 };
MASSBATTLE_API ESkirmishDecision EvaluateSkirmish(
	float NearestEnemyDistM, bool bReloadReady, bool bFireAtWill,
	bool bRetreating, float BreakDistM, float ResumeDistM);
