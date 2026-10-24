// BattlePathPlanner.h -- heightfield path planner (roadmap R4, gate T1.9):
// A* over the cost grid with slope-aware costs, string-pulling smoothing with
// clearance, arc-length resampler. Free functions on the pure-math seam (B.0),
// all values in METERS. Fully deterministic: fixed neighbor order, strict
// (F, H, index) heap tie-break -- no RNG, no wall clock, no iteration-order
// dependence (phase-1 ground rule 5).
#pragma once

#include "CoreMinimal.h"
#include "BattleCostGrid.h"

/** Planner output: world-space polyline, meters. CumulativeS is monotone
 *  nondecreasing; Points.Num() == CumulativeS.Num() >= 2 on success. */
struct FBattlePlannedPath
{
	TArray<FVector2D> Points;
	TArray<float> CumulativeS;
	float TotalLength = 0.f;

	bool IsValid() const { return Points.Num() >= 2 && CumulativeS.Num() == Points.Num(); }
	void RebuildCumulativeS();
};

namespace FBattlePathPlanner
{
	/** A* over 8-connected grid (no corner cutting on diagonals). Slope-aware
	 *  step cost: dist * (1 + SlopeWeight * |dh| / dist). Deterministic.
	 *  Returns false when the goal is unreachable (or inputs degenerate). */
	bool FindPath(const FBattleCostGrid& Grid, const FVector2D& StartMeters, const FVector2D& GoalMeters,
		float SlopeWeight, FBattlePlannedPath& OutPath);

	/** String-pulling smoothing: greedy line-of-sight shortcuts where every
	 *  sample along the shortcut keeps ClearanceMeters from impassable cells.
	 *  Never increases length; keeps endpoints. */
	void Smooth(const FBattleCostGrid& Grid, float ClearanceMeters, FBattlePlannedPath& InOutPath);

	/** Resample so consecutive points are at most MaxSpacingMeters apart
	 *  (arc-length preserving; endpoints kept). Feeds the path-frame layout. */
	void Resample(const FBattlePlannedPath& InPath, float MaxSpacingMeters, FBattlePlannedPath& OutPath);

	/** True when every cell the segment [A,B] (m) passes through -- including
	 *  a ClearanceMeters margin -- is passable. Used by Smooth and by tests
	 *  as the "path valid against impassable cells" oracle. */
	bool SegmentIsClear(const FBattleCostGrid& Grid, const FVector2D& A, const FVector2D& B, float ClearanceMeters);
}
