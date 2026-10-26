#include "BattleUnitLocomotion.h"
#include "BattlePathFrameLayout.h"

namespace FUnitLocomotion
{
	float ComputeUnitSpeedLimit(const FSpeedLimitInputs& In)
	{
		// Two explicit steps so every dimension is auditable (arch §8.2, E2).
		const float OmegaMax = In.MaxMemberSpeed / FMath::Max(In.OuterRadius, KINDA_SMALL_NUMBER); // rad/s
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
}
