// BattleUnitLocomotion.h -- unit locomotion math (arch §8.2, gate T1.3).
// Pure functions on the seam, METERS. The unit is the locomotor (§8.1): these
// functions produce the per-unit speed limit and anchor/facing progression the
// formation rides on. The wheel cap is the E2-corrected two-step form:
//   OmegaMax        = MaxMemberSpeed / max(OuterRadius, eps)          [rad/s]
//   WheelLinearLimit= OmegaMax * max(PathRadiusOfAnchor, eps)       [m/s]
//   FacingLinearLimit = FacingDeltaRate * max(PathRadiusOfAnchor, eps)  [m/s]
//   UnitSpeedLimit  = Min3(CommandedSpeed, MinMemberSpeed*Catchup, min(Wheel, Facing))
#pragma once

#include "CoreMinimal.h"
#include "BattlePathPlanner.h"

namespace FUnitLocomotion
{
	struct FSpeedLimitInputs
	{
		/** Order intent (m/s). */
		float CommandedSpeed = 0.f;
		/** MinOverMembers(EffectiveSpeed(i)) -- the slowest man gates the unit (m/s). */
		float MinMemberSpeed = 0.f;
		/** MaxMemberSpeed -- the fastest man sets the angular cap (m/s). */
		float MaxMemberSpeed = 0.f;
		/** Unit body outer radius (m). */
		float OuterRadius = 0.f;
		/** Anchor's turning radius: 0 = in-place pivot, huge = straight march (m). */
		float PathRadiusOfAnchor = 0.f;
		/** Commanded facing-change rate (rad/s), applied as a separate linear cap. */
		float FacingDeltaRate = 0.f;
		/** Slowest-man catchup multiplier (arch §12: 1.25). */
		float CatchupFactor = 1.25f;
	};

	/** The §8.2 Min3, verbatim. All meters/seconds. */
	MASSBATTLE_API float ComputeUnitSpeedLimit(const FSpeedLimitInputs& In);

	/** Hysteresis on the limit (arch §8.2): drops apply immediately, rises only
	 *  once the raw limit exceeds the current by HysteresisFraction. */
	MASSBATTLE_API float ApplyHysteresis(float CurrentLimit, float RawLimit, float HysteresisFraction);

	/** Anchor progression: advance arc length S by Speed*DT, clamped to
	 *  [0, TotalLength]. */
	MASSBATTLE_API float AdvanceAnchor(float S, float Speed, float DT, float TotalLength);

	/** Commanded facing-change rate cap: rotate CurrentYawDeg toward
	 *  TargetYawDeg by at most MaxRateRadS*DT (shortest arc). */
	MASSBATTLE_API float AdvanceFacing(float CurrentYawDeg, float TargetYawDeg, float MaxRateRadS, float DT);

	/** Anchor turn radius at arc length S: arc distance over heading change
	 *  across a symmetric lookahead window (straight path -> very large r). */
	MASSBATTLE_API float PathRadiusAt(const FBattlePlannedPath& Path, float S, float LookaheadMeters);

	/** Commanded-frontage resolution (A2 width control): returns the new file
	 *  count for a unit of NumSlots total entities. CommandedFiles <= 0 keeps
	 *  the current layout; values above NumSlots clamp to NumSlots (1 file of
	 *  NumSlots ranks). Returns 0 when the layout is unchanged. */
	MASSBATTLE_API int32 ResolveCommandedFiles(int32 CommandedFiles, int32 NumSlots, int32 CurrentNumFiles);
}
