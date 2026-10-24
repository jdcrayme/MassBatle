#include "BattlePathFrameLayout.h"

namespace FPathFrameLayout
{
	bool FromFragment(const FBattlePathPolyline& FragmentPath, FBattlePlannedPath& OutPathMeters)
	{
		OutPathMeters.Points.Reset();
		OutPathMeters.CumulativeS.Reset();
		OutPathMeters.TotalLength = 0.f;
		if (!FragmentPath.IsValid())
		{
			return false;
		}
		OutPathMeters.Points.Reserve(FragmentPath.NumPoints);
		for (int32 i = 0; i < FragmentPath.NumPoints; ++i)
		{
			OutPathMeters.Points.Add(FragmentPath.Points[i].Pos * 0.01f);
		}
		OutPathMeters.RebuildCumulativeS();
		return true;
	}

	bool SampleAt(const FBattlePlannedPath& Path, float S, FVector2D& OutPos, FVector2D& OutTangent)
	{
		if (!Path.IsValid())
		{
			return false;
		}
		S = FMath::Clamp(S, 0.f, Path.TotalLength);
		// Binary search the segment containing S (CumulativeS is nondecreasing).
		int32 Lo = 0;
		int32 Hi = Path.CumulativeS.Num() - 1;
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if (Path.CumulativeS[Mid] < S)
			{
				Lo = Mid + 1;
			}
			else
			{
				Hi = Mid;
			}
		}
		const int32 Seg = FMath::Max(1, Lo);
		const float S0 = Path.CumulativeS[Seg - 1];
		const float S1 = Path.CumulativeS[Seg];
		const FVector2D A = Path.Points[Seg - 1];
		const FVector2D B = Path.Points[Seg];
		const float Alpha = (S1 - S0) > KINDA_SMALL_NUMBER ? (S - S0) / (S1 - S0) : 0.f;
		OutPos = FMath::Lerp(A, B, Alpha);
		OutTangent = (B - A).GetSafeNormal();
		return OutTangent.SizeSquared() > KINDA_SMALL_NUMBER;
	}

	float RankArcLength(float SAnchor, int32 Rank, float RankSpacing, float TotalLength)
	{
		return FMath::Clamp(SAnchor - static_cast<float>(Rank) * RankSpacing, 0.f, TotalLength);
	}

	bool Project(const FBattlePlannedPath& Path, float SAnchor, int32 Rank, int32 File, int32 NumFiles,
		float RankSpacing, float FileSpacing, FVector2D& OutPos, float& OutYawDeg)
	{
		FVector2D Pos;
		FVector2D Tangent;
		if (!SampleAt(Path, RankArcLength(SAnchor, Rank, RankSpacing, Path.TotalLength), Pos, Tangent))
		{
			return false;
		}
		const FVector2D Normal(-Tangent.Y, Tangent.X); // left of travel
		const float Lateral = (static_cast<float>(File) - (static_cast<float>(NumFiles) - 1.f) * 0.5f) * FileSpacing;
		OutPos = Pos + Normal * Lateral;
		OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X));
		return true;
	}

	FVector2D AnchorFormula(const FVector2D& Anchor, float FacingYawDeg, int32 Rank, int32 File, int32 NumFiles,
		float RankSpacing, float FileSpacing)
	{
		const float Rad = FMath::DegreesToRadians(FacingYawDeg);
		const float Cos = FMath::Cos(Rad);
		const float Sin = FMath::Sin(Rad);
		// Layout offset in the facing frame: back along facing, lateral left.
		const FVector2D Offset(-static_cast<float>(Rank) * RankSpacing,
			(static_cast<float>(File) - (static_cast<float>(NumFiles) - 1.f) * 0.5f) * FileSpacing);
		return Anchor + FVector2D(Offset.X * Cos - Offset.Y * Sin, Offset.X * Sin + Offset.Y * Cos);
	}
}
