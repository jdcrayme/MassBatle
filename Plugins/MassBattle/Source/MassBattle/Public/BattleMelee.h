// BattleMelee.h -- melee resolution pure math on the B.0 seam (brief A1; phase
// ground rule 1). Free functions / plain structs, deterministic, meters, no
// RNG and no wall clock: the resolution processor draws from the registered
// `Combat.Roll` stream (F7/A13) and calls these for the math. Constants are
// PLACEHOLDERS per A6 -- the community model's STRUCTURE ships, golden-locked
// at T2.1, swapped only by the Phase-6 T6.1 audit with diff review.
//
// Task 0 skeleton: signatures exist and return failure sentinels; bodies land
// in task 3 (brief task plan).
#pragma once

#include "CoreMinimal.h"

namespace FMeleeMath
{
	/** Contested hit chance (spec §4.8, arch §9.1): P = f(Attack, Defense)
	 *  with floor/cap clamps from the CVAR block. The community-reverse-
	 *  engineered M2TW curve ports in task 3. FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float HitChance(float Attack, float Defense);

	/** Defense-skill arc multiplier (spec §4.8): all arcs get skill, flank/
	 *  rear reduced. Input is the defender-relative attack sector index
	 *  (0 = dead ahead, SectorCount-1 = just left of ahead, from the
	 *  quantized ContactAngle -- one value, two consumers, §4.5).
	 *  FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float SkillArcMult(int32 SectorIndexFromFacing, int32 SectorCount);

	/** Shield-arc window (spec §4.8): shield defense contributes on frontal
	 *  arcs only. Same sector convention as SkillArcMult.
	 *  FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float ShieldArcWindow(int32 SectorIndexFromFacing, int32 SectorCount);

	/** Attack-cycle phase offset for one entity (spec §4.8): handle-hashed
	 *  stagger in [0, AttackInterval) so combat is never serialized (ADR-05).
	 *  FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float StaggeredPhaseOffset(uint32 EntityHandleIndex, float AttackInterval);

	/** Charge-bonus value remaining (spec §4.9): BaseCharge decaying linearly
	 *  over ChargeDecayWindow since ImpactAgeSeconds; <= 0 once expired.
	 *  FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float ChargeBonusRemaining(float BaseCharge, float ImpactAgeSeconds, float DecayWindow);

	/** Knockback impulse magnitude at impact (spec §4.9): bounded by clamp,
	 *  linear in impact speed above the threshold. Deterministic (no
	 *  knockdown RNG v1). FAILURE STUB: returns -1.f. */
	MASSBATTLE_API float KnockbackImpulse(float ImpactSpeedMps, float SpeedThresholdMps);
}
