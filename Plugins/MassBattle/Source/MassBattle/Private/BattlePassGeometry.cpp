#include "BattlePassGeometry.h"

namespace FUnitPassGeometry
{
	FPassDecision DecideSqueezeOrMorph(float PassWidthM, float FormationWidthM,
		float FileSpacingM, float CompressionFloor)
	{
		FPassDecision Out;
		const float Threshold = FormationWidthM * CompressionFloor;
		if (PassWidthM >= Threshold)
		{
			Out.bMorph = false;
			Out.SqueezeScale = FMath::Clamp(PassWidthM / FMath::Max(FormationWidthM, KINDA_SMALL_NUMBER),
				CompressionFloor, 1.f);
		}
		else
		{
			Out.bMorph = true;
			Out.MorphFiles = FMath::Max(1, FMath::FloorToInt(PassWidthM / FMath::Max(FileSpacingM, KINDA_SMALL_NUMBER)));
		}
		return Out;
	}

	float MeasureClearWidth(const FBattleCostGrid& Grid, const FVector2D& Pos,
		const FVector2D& PerpDir, float MaxScanM)
	{
		const FVector2D Dir = PerpDir.GetSafeNormal();
		if (Dir.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float Step = Grid.CellSize * 0.5f;
		auto Passable = [&Grid](const FVector2D& P)
		{
			int32 X, Y;
			Grid.WorldToCell(P, X, Y);
			return !Grid.IsImpassable(X, Y);
		};
		float Plus = 0.f;
		while (Plus < MaxScanM && Passable(Pos + Dir * (Plus + Step)))
		{
			Plus += Step;
		}
		float Minus = 0.f;
		while (Minus < MaxScanM && Passable(Pos - Dir * (Minus + Step)))
		{
			Minus += Step;
		}
		return Plus + Minus;
	}
}
