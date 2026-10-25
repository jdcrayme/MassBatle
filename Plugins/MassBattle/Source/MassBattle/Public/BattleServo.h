// BattleServo.h -- entity servo + velocity-space avoidance + unstuck ladder
// (arch §8.3/§8.5, decision A1, gate T1.5/T1.6). Pure functions on the seam,
// METERS. Velocity control, never force accumulation (ADR-07): the servo
// outputs a velocity; avoidance is a velocity-space deflection blended by a
// per-state weight -- the engine's force pipeline is NOT adopted (§4.2: the
// two are the same math in different units; A1 picks the project's own).
#pragma once

#include "CoreMinimal.h"

/** §8.3 constants (§12). */
namespace FUnitServoConstants
{
	constexpr float Tau = 0.25f;            // s, servo time constant
	constexpr float CatchupBoost = 1.25f;   // speed clamp multiplier
	constexpr float MaxAccel = 4.0f;        // m/s^2 acceleration clamp
	constexpr float SlotReachEps = 0.35f;   // m, "arrived at slot"
	constexpr float StuckSpeedEps = 0.2f;   // m/s, §12 unstuck epsilon
	constexpr float EscalateAfterSeconds = 1.5f; // §12 unstuck T
}

/** One avoidance candidate from the frame snapshot (all battle entities).
 *  HandleIndex is the entity handle's index -- the asymmetric tie-break key. */
struct FAvoidanceNeighbor
{
	FVector2D Pos = FVector2D::ZeroVector;
	FVector2D Vel = FVector2D::ZeroVector;
	float Radius = 0.4f;     // m
	uint32 HandleIndex = 0;
};

namespace FUnitServo
{
	/** §8.3: V = clamp((SlotPos - Pos) / Tau, MaxSpeed * CatchupBoost). */
	MASSBATTLE_API FVector2D ComputeServoVelocity(const FVector2D& Pos, const FVector2D& SlotPos,
		float Tau, float MaxSpeed, float CatchupBoost);

	/** Acceleration clamp: |New - Old| <= MaxAccel * DT. */
	MASSBATTLE_API FVector2D ClampAcceleration(const FVector2D& OldVel, const FVector2D& NewVel, float MaxAccel, float DT);

	/** A1 velocity-space avoidance deflection (RVO-lite): for each neighbor
	 *  whose predicted closest approach violates the combined radius, add a
	 *  lateral deflection away from the conflict, scaled by Weight (per-state,
	 *  §4.5) and the asymmetry rule: the LOWER handle index has priority and
	 *  deflects half; the higher yields double. Deterministic. */
	MASSBATTLE_API FVector2D ComputeAvoidanceDeflection(const FVector2D& MyPos, const FVector2D& MyDesiredVel,
		float MyRadius, uint32 MyHandleIndex, const TArray<FAvoidanceNeighbor>& Neighbors,
		float Weight, float HorizonSeconds = 1.5f);
}

namespace FUnstuckLadder
{
	/** §8.5 ladder: while moving slowly and not near the slot the timer
	 *  accumulates; past EscalateAfterSeconds the level rises (1..4, clamped
	 *  -- level 4 is the terminal "unit AI" handoff). Reaching the slot
	 *  resets to level 0. Returns the new level. */
	MASSBATTLE_API int32 Advance(int32 CurrentLevel, bool bMovingSlowly, bool bNearSlot,
		float DT, float& InOutTimer, float EscalateAfterSeconds);

	/** Avoidance weight multiplier at a ladder level (step 1 doubles it). */
	MASSBATTLE_API float AvoidanceWeightMultiplier(int32 Level);

	/** True when the level injects a sidestep impulse (step 2). */
	MASSBATTLE_API bool WantsSidestep(int32 Level);
}
