// BattleBallistics.h -- Phase 3 ballistic seam (spec ranged-stack §4.1-§4.3,
// brief A1): the launch solver, deterministic dispersion, the fixed-DT
// stepper, and the heightfield LOS march. Free functions / plain structs,
// meters, pure and deterministic -- no RNG streams, no wall-clock, no
// GFrameCounter (the A13 discipline). ONE solver serves both trajectory
// classes (RC2): bows and crossbows differ by profile speed and the bIndirect
// FIRE RULE, never by a second integrator.
//
// A6 posture (ADR-30): constants below are T3.1-golden-locked placeholders;
// the Phase-6 T6.1 audit swaps ported constants with diff review.
#pragma once

#include "CoreMinimal.h"
#include "BattleCostGrid.h"

/** T3.1-golden-locked placeholder constants (A6). */
namespace FBallisticsConstants
{
	/** Standard gravity, m/s^2 (placeholder; T6.1 may port the game's feel). */
	constexpr float GravityMps2 = 9.81f;
	/** Numerical guards. */
	constexpr float DistanceEpsilonM = 1e-4f;
	constexpr float ConeEpsilonDeg = 1e-4f;
}

/** Launch solution for one shot (spec §4.1). */
struct MASSBATTLE_API FBallisticSolution
{
	/** Unit launch direction: X/Y horizontal, Z up. */
	FVector LaunchDir = FVector::ForwardVector;
	/** Flight time to the target point, seconds. */
	float FlightTimeS = 0.f;
	/** False when the target is outside the speed/gravity envelope (the
	 *  volley pass holds fire -- never force-fires an unreachable shot). */
	bool bValid = false;
};

namespace FBallisticsMath
{
	/** Low-arc ballistic solution from FromM/FromZM to ToM/ToZM at the given
	 *  muzzle speed (spec §4.1):
	 *    tan(theta) = (v^2 - sqrt(v^4 - g*(g*d^2 + 2*dh*v^2))) / (g*d)
	 *  d = horizontal distance, dh = target height minus launch height. The
	 *  discriminant root selects the LOW arc (the M2TW flat-ish feel); a
	 *  negative discriminant is out of envelope (bValid = false). Flight time
	 *  = d / (v*cos(theta)). Deterministic; identical inputs -> identical
	 *  bits. */
	MASSBATTLE_API FBallisticSolution SolveLaunch(
		const FVector2D& FromM, float FromZM, const FVector2D& ToM, float ToZM,
		float SpeedMps, float GravityMps2);

	/** Deterministic dispersion (spec §4.2): perturb Dir within a cone of
	 *  half-angle ConeHalfDeg. Two Knuth-family hashes of the seed draw a
	 *  point in the cone's DISC (rejection-free: radial sqrt + angle), so
	 *  every perturbed direction stays inside the cone by construction.
	 *  Same seed -> same bits; the caller derives the seed from entity
	 *  identity + ShotsFired (ShotSeed) so it is run-invariant (A13). */
	MASSBATTLE_API FVector PerturbDirection(const FVector& Dir, float ConeHalfDeg, uint32 Seed);

	/** Run-invariant shot seed: the hash of entity identity + the lifetime
	 *  shot count (never GFrameCounter -- the A13 rule; same Knuth family as
	 *  FMeleeMath::CombatRoll01 / StaggeredPhaseOffset, the B-2 one-home
	 *  posture -- a future audit may unify the constant's home). */
	MASSBATTLE_API uint32 ShotSeed(uint32 EntityIndex, uint32 EntitySerial, uint16 ShotsFired);

	/** Semi-implicit Euler step (the IntegrateProcessor family): position
	 *  advances on the POST-update velocity, which keeps the parabola stable
	 *  at the fixed sim DT. Returns the advanced position/velocity by
	 *  reference; no allocation, branch-free. */
	MASSBATTLE_API void StepProjectile(FVector& InOutPosM, FVector& InOutVelMps, float GravityMps2, float DT);

	/** Heightfield line-of-sight march (spec §4.3, RC3): walks the segment in
	 *  sub-cell steps (CellSize/2, deduped per crossed cell -- a DDA variant),
	 *  sampling SampleHeight at each; blocked when terrain + ClearanceM is
	 *  STRICTLY above the ray (the equal-height epsilon rule: flat ground
	 *  never false-occludes). Arced fire calls this on the frontage segment
	 *  only; flat fire on the full lane (the caller's fire rule, RC2). */
	MASSBATTLE_API bool MarchClear(
		const FBattleCostGrid& Grid, const FVector2D& FromM, float FromEyeZM,
		const FVector2D& ToM, float ToZM, float ClearanceM);
}
