#include "BattleUnitLocomotion.h"
#include "BattlePathFrameLayout.h"

namespace FUnitLocomotion
{
	float ComputeUnitSpeedLimit(const FSpeedLimitInputs& In)
	{
		// Two explicit steps so every dimension is auditable (arch §8.2, E2).
		// The wheel radius is traversal-dependent (phase 2 A3, F-5): with a
		// path active the wrapped column's outer files sweep r_path +
		// HalfFrontage (marching form); the pivot keeps the formed-body
		// OuterRadius about the centroid (E2 original form).
		const float WheelRadius = FMath::Max(
			In.bPathActive ? In.PathRadiusOfAnchor + In.HalfFrontageM : In.OuterRadius,
			KINDA_SMALL_NUMBER);
		const float OmegaMax = In.MaxMemberSpeed / WheelRadius; // rad/s
		const float WheelLinearLimit = OmegaMax * FMath::Max(In.PathRadiusOfAnchor, KINDA_SMALL_NUMBER); // m/s
		const float FacingLinearLimit = In.FacingDeltaRate * FMath::Max(In.PathRadiusOfAnchor, KINDA_SMALL_NUMBER); // m/s

		const float Slowest = In.MinMemberSpeed * In.CatchupFactor;
		return FMath::Min3(In.CommandedSpeed, Slowest, FMath::Min(WheelLinearLimit, FacingLinearLimit));
	}

	float ApplyHysteresis(float CurrentLimit, float RawLimit, float HysteresisFraction)
	{
		if (RawLimit < CurrentLimit)
		{
			return RawLimit; // safety drops are immediate
		}
		// Rise only past the margin: no chatter while the input jitters.
		const float RiseThreshold = CurrentLimit * (1.f + HysteresisFraction);
		return RawLimit >= RiseThreshold ? RawLimit : CurrentLimit;
	}

	float AdvanceAnchor(float S, float Speed, float DT, float TotalLength)
	{
		return FMath::Clamp(S + Speed * DT, 0.f, TotalLength);
	}

	float AdvanceFacing(float CurrentYawDeg, float TargetYawDeg, float MaxRateRadS, float DT)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(CurrentYawDeg, TargetYawDeg); // shortest-arc step needed
		const float MaxStepDeg = FMath::RadiansToDegrees(MaxRateRadS) * DT;
		return CurrentYawDeg + FMath::Clamp(Delta, -MaxStepDeg, MaxStepDeg);
	}

	float PathRadiusAt(const FBattlePlannedPath& Path, float S, float LookaheadMeters)
	{
		FVector2D PosA, TanA, PosB, TanB;
		if (!FPathFrameLayout::SampleAt(Path, S - LookaheadMeters, PosA, TanA) ||
			!FPathFrameLayout::SampleAt(Path, S + LookaheadMeters, PosB, TanB))
		{
			return BIG_NUMBER; // degenerate window -> treat as straight
		}
		const float Angle = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(TanA, TanB), -1.f, 1.f));
		if (Angle < KINDA_SMALL_NUMBER)
		{
			return BIG_NUMBER; // straight
		}
		const float Arc = 2.f * LookaheadMeters;
		return Arc / Angle; // r = s / theta
	}

	int32 ResolveCommandedFiles(int32 CommandedFiles, int32 NumSlots, int32 CurrentNumFiles)
	{
		if (CommandedFiles <= 0 || NumSlots <= 0)
		{
			return 0; // keep the current layout
		}
		const int32 Clamped = FMath::Clamp(CommandedFiles, 1, NumSlots);
		return (Clamped != CurrentNumFiles) ? Clamped : 0;
	}

	float StragglerPaceFactor(const float MaxSlotErrorM, const float DressBreakM, const float PaceGain)
	{
		if (MaxSlotErrorM <= DressBreakM || PaceGain <= 0.f)
		{
			return 1.f; // dressed (or the policy is off): full pace
		}
		// ERRATA (measured): the plan's clamp floors at 0 -- a fully frozen
		// anchor recovers SLOWER than a creeping one (the men redress toward
		// slots that advance with the anchor), and S4/S5 flag-on marches
		// burned their whole budgets in stop-wait-crawl cycles. 0.25 keeps
		// the P4 "dips measurably" property while the dress drains.
		return FMath::Clamp(1.f - PaceGain * (MaxSlotErrorM - DressBreakM), 0.25f, 1.f);
	}

	bool MarchReflowSticky(const bool bHasPath, const float MaxSlotErrorM, const float DressBreakM)
	{
		return bHasPath && MaxSlotErrorM < DressBreakM;
	}

	float FallInBlendAlpha(const float RemainingPathM, const float MorphDistanceM)
	{
		return FMath::Clamp(1.f - RemainingPathM / FMath::Max(MorphDistanceM, KINDA_SMALL_NUMBER), 0.f, 1.f);
	}

	bool FallInHandoffLatch(const bool bLocked, const float DistFinalSlotM, const float HandoffDistM)
	{
		if (!bLocked && DistFinalSlotM <= HandoffDistM)
		{
			return true;
		}
		if (bLocked && DistFinalSlotM > HandoffDistM * 1.5f)
		{
			return false;
		}
		return bLocked;
	}

	float FallInMorphDistance(const float MorphDistanceCvarM, const int32 NumRanks, const float RankSpacingM)
	{
		const float DepthM = static_cast<float>(FMath::Max(1, NumRanks)) * RankSpacingM;
		return FMath::Max(MorphDistanceCvarM, 2.f * DepthM);
	}
}
