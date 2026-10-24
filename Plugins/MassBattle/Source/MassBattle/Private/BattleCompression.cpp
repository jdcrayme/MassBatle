#include "BattleCompression.h"

namespace FUnitCompression
{
	float AdvanceSpacingScale(float CurrentScale, float TargetScale, float DT,
		float FloorScale, float RecoveryPerSec)
	{
		if (TargetScale < CurrentScale)
		{
			// Compress immediately, never past the floor.
			return FMath::Max(TargetScale, FloorScale);
		}
		if (TargetScale > CurrentScale)
		{
			return FMath::Min(CurrentScale + RecoveryPerSec * DT, TargetScale);
		}
		return CurrentScale;
	}

	FVector2D CompressedSlot(const FVector2D& Anchor, float FacingYawDeg,
		const FVector2D& LayoutOffset, float SpacingScale)
	{
		const float Rad = FMath::DegreesToRadians(FacingYawDeg);
		const float Cos = FMath::Cos(Rad);
		const float Sin = FMath::Sin(Rad);
		const FVector2D Scaled = LayoutOffset * SpacingScale;
		return Anchor + FVector2D(Scaled.X * Cos - Scaled.Y * Sin, Scaled.X * Sin + Scaled.Y * Cos);
	}
}
