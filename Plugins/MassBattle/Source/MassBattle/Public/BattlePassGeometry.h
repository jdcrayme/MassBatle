// BattlePassGeometry.h -- narrow-pass decisions (arch §8.6, gate T1.7).
// Pure functions on the seam, METERS:
//   PassWidth >= FormationWidth * CompressionFloor  -> squeeze (spacing scale)
//   else                                            -> morph to column
// Morph column files = floor(PassWidth / FileSpacing). The commit rule (morph
// locks once the lead rank enters the mouth) lives with the caller; the pure
// decision is stateless so it can never thrash on its own.
#pragma once

#include "CoreMinimal.h"
#include "BattleCostGrid.h"
#include "BattleCompression.h"

namespace FUnitPassGeometry
{
	struct FPassDecision
	{
		bool bMorph = false;      // true = morph to column; false = squeeze through
		int32 MorphFiles = 0;     // valid when bMorph (>= 1)
		float SqueezeScale = 1.f; // valid when !bMorph (in [Floor, 1])
	};

	/** The §8.6 threshold, both sides. */
	MASSBATTLE_API FPassDecision DecideSqueezeOrMorph(float PassWidthM, float FormationWidthM,
		float FileSpacingM, float CompressionFloor = FUnitCompression::CompressionFloor);

	/** Clear width across the path at a point: walk along PerpDir (and back)
	 *  until impassable cells; returns the total clear span (m). Used by the
	 *  locomotion ahead-scan and by tests as the PassWidth oracle. */
	MASSBATTLE_API float MeasureClearWidth(const FBattleCostGrid& Grid, const FVector2D& Pos,
		const FVector2D& PerpDir, float MaxScanM);
}
