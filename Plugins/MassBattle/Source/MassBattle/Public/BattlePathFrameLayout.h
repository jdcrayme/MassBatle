// BattlePathFrameLayout.h -- path-frame slot layout (arch §8.6, gate T1.2).
// Pure functions on the seam, METERS:
//   SlotPos = PathPoint(s_anchor - rank*RankSpacing)        (arc-length behind anchor)
//           + PathNormal(s) * (file - (files-1)/2) * FileSpacing   (lateral in path frame)
//   per-rank facing = local path tangent
// Degenerates to anchor + R(facing) * LayoutOffset on straight paths (the §8.2
// compression formula with SpacingScale=1) -- AnchorFormula below IS that
// degenerate form and doubles as the no-path fallback for FormationSlotResolve.
#pragma once

#include "CoreMinimal.h"
#include "BattlePathPlanner.h"
#include "BattleMovementFragments.h"

namespace FPathFrameLayout
{
	/** Fragment-boundary adapter (ground rule 2): the reflected fragment
	 *  polyline is centimeters; the layout math is meters. Recomputes nothing
	 *  else -- points and arc lengths scale by 1/100. Returns false when the
	 *  fragment path is empty (UnitLocomotion has not planned yet). */
	bool FromFragment(const FBattlePathPolyline& FragmentPath, FBattlePlannedPath& OutPathMeters);

	/** Position + unit tangent at arc length S (clamped to [0, TotalLength]). */
	bool SampleAt(const FBattlePlannedPath& Path, float S, FVector2D& OutPos, FVector2D& OutTangent);

	/** Arc length of the given rank behind the anchor, clamped to the path
	 *  (monotone nonincreasing in Rank; gate T1.2 asserts this directly). */
	float RankArcLength(float SAnchor, int32 Rank, float RankSpacing, float TotalLength);

	/** §8.6 slot projection. Rank 0 = front; File 0 = leftmost when looking
	 *  along the path tangent. Returns false on a degenerate path. */
	bool Project(const FBattlePlannedPath& Path, float SAnchor, int32 Rank, int32 File, int32 NumFiles,
		float RankSpacing, float FileSpacing, FVector2D& OutPos, float& OutYawDeg);

	/** The straight-path degenerate form: anchor + R(facing) * LayoutOffset.
	 *  LayoutOffset = (-rank*RankSpacing, (file-(files-1)/2)*FileSpacing). */
	FVector2D AnchorFormula(const FVector2D& Anchor, float FacingYawDeg, int32 Rank, int32 File, int32 NumFiles,
		float RankSpacing, float FileSpacing);
}
