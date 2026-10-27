// BattleUnitLocomotion.h -- unit locomotion math (arch §8.2, gate T1.3).
// Pure functions on the seam, METERS. The unit is the locomotor (§8.1): these
// functions produce the per-unit speed limit and anchor/facing progression the
// formation rides on. The wheel cap is the E2-corrected two-step form:
//   OmegaMax        = MaxMemberSpeed / max(R_wheel, eps)                [rad/s]
//   WheelLinearLimit= OmegaMax * max(PathRadiusOfAnchor, eps)           [m/s]
//   FacingLinearLimit = FacingDeltaRate * max(PathRadiusOfAnchor, eps)  [m/s]
//   UnitSpeedLimit  = Min3(CommandedSpeed, MinMemberSpeed*Catchup, min(Wheel, Facing))
// R_wheel depends on the traversal (phase 2 A3, F-5):
//   path active   -> R_wheel = r_path + HalfFrontage. The path-frame layout
//                    wraps ranks ALONG the path, so the outer files sweep
//                    r_path + HalfFrontage, not the formed-body outer radius
//                    about the centroid -- the centroid form is too generous
//                    on bends and lets outer slots outrun the members.
//   no path       -> R_wheel = OuterRadius (in-place pivot: the formed body
//                    rotates about the anchor; the E2-audited original form).
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
		/** Unit body outer radius (m) -- the pivot-form wheel radius. */
		float OuterRadius = 0.f;
		/** Half the formed frontage (m) -- the marching-form wheel sweep term. */
		float HalfFrontageM = 0.f;
		/** 1 = a path is active (marching form); 0 = pivot form (OuterRadius). */
		bool bPathActive = false;
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

	/** Phase 2 B2 straggler pace factor (arch §8.2, F-6): 1.0 while the block
	 *  is dressed (max slot error at/below DressBreakM); beyond the threshold
	 *  the factor linearly throttles by PaceGain per meter of tear, floored at
	 *  0. The anchor rides the throttle through the existing hysteresis. */
	MASSBATTLE_API float StragglerPaceFactor(float MaxSlotErrorM, float DressBreakM, float PaceGain);

	/** Phase 2 B3 march stickiness predicate (F-7): while a path is active and
	 *  the block is dressed (max slot error strictly under DressBreakM), the
	 *  interval reflow + predictive swaps are churn, not information. Rank-gap
	 *  and layout re-decision triggers bypass this at the call site. */
	MASSBATTLE_API bool MarchReflowSticky(bool bHasPath, float MaxSlotErrorM, float DressBreakM);

	/** Phase 2 C3 progressive fall-in: unit-level blend weight from the
	 *  remaining path length. 0 far away (coherent path-frame block), 1 at the
	 *  path end (the block IS the final layout). Monotone nondecreasing as
	 *  the remaining length shrinks. */
	MASSBATTLE_API float FallInBlendAlpha(float RemainingPathM, float MorphDistanceM);

	/** Phase 2 C3 per-soldier handoff latch with hysteresis: latch once within
	 *  HandoffDistM of the final slot; unlatch only past 1.5x (avoidance
	 *  nudges must not flip the target regime at the boundary). */
	MASSBATTLE_API bool FallInHandoffLatch(bool bLocked, float DistFinalSlotM, float HandoffDistM);

	/** Phase 2 C3: the effective morph distance for a layout of NumRanks
	 *  ranks -- max(cvar, 2 x rank depth). ONE home for the floor so the
	 *  resolve blend and the locomotion throttle gate agree exactly. */
	MASSBATTLE_API float FallInMorphDistance(float MorphDistanceCvarM, int32 NumRanks, float RankSpacingM);
}
