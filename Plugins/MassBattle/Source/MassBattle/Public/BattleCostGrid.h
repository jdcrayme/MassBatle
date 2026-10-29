// BattleCostGrid.h -- heightfield movement-cost grid (arch §4 row 1 "cost
// grid", §12 cell size; roadmap R4). Plain struct on the pure-math seam (B.0):
// ALL values in METERS -- the project convention. Conversion to/from engine
// centimeters happens at the fragment/subsystem boundary only (phase-1 ground
// rule 2). Owned per-world by UBattleCostGridSubsystem.
#pragma once

#include "CoreMinimal.h"

/** The Phase-1 default tile geometry (arch §12), named beside the grid data so
 *  consumers read one home instead of re-typing the extent (PR-3): 86x86 cells
 *  at 3 m spans 258 m across, centered on the origin. UBattleCostGridSubsystem
 *  builds its grid from these; the demo's order clamp reads TileHalfExtentM. */
namespace BattleGridGeo
{
	constexpr int32 TileDimCells = 86;
	constexpr float CellSizeM = 3.0f;
	/** Half extent of the default tile, meters (86 * 3 / 2 = 129). */
	constexpr float TileHalfExtentM = TileDimCells * CellSizeM * 0.5f;
}

/** 8-connected heightfield cost grid. Flat tile in Phase 1 (B.1); the height
 *  array + slope weight keep the cost model slope-aware for later terrain. */
struct MASSBATTLE_API FBattleCostGrid
{
	/** Impassable marker in CellCost. */
	static constexpr uint8 Impassable = 255;

	float CellSize = 3.0f;                    // m (arch §12)
	FVector2D Origin = FVector2D::ZeroVector; // world pos of cell (0,0) corner, m
	int32 DimX = 0;
	int32 DimY = 0;

	/** Terrain height per cell, m (flat 0 in Phase 1). */
	TArray<float> CellHeight;
	/** Traversal cost 0..254; 255 = impassable (A5 stamp overrides write this). */
	TArray<uint8> CellCost;

	/** Flat, passable tile centered on the given world point (m). */
	void InitFlat(int32 InDimX, int32 InDimY, const FVector2D& CenterMeters, float InCellSize = 3.0f);

	int32 NumCells() const { return DimX * DimY; }
	bool IsInside(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < DimX && Y < DimY; }
	int32 IndexOf(int32 X, int32 Y) const { return Y * DimX + X; }

	uint8 GetCost(int32 X, int32 Y) const { return IsInside(X, Y) ? CellCost[IndexOf(X, Y)] : Impassable; }
	float GetHeight(int32 X, int32 Y) const { return IsInside(X, Y) ? CellHeight[IndexOf(X, Y)] : 0.f; }
	bool IsImpassable(int32 X, int32 Y) const { return GetCost(X, Y) >= Impassable; }

	/** World<->cell helpers (m). CellCenter = Origin + ((X+0.5), (Y+0.5)) * CellSize. */
	FVector2D CellCenter(int32 X, int32 Y) const;
	void WorldToCell(const FVector2D& WorldPos, int32& OutX, int32& OutY) const;

	/** Phase 3 (spec ranged-stack §4.3): bilinear terrain height at a
	 *  WORLD position, meters -- the ballistic/LOS sampling read. The four
	 *  surrounding cell heights are blended (flat grids read exactly 0, so
	 *  the flags-off baseline is untouched); outside the grid reads 0
	 *  (GetHeight's bounds rule). Net-new API, disclosed in the spec
	 *  touch-point table: the ONLY BattleCostGrid change this phase. */
	float SampleHeight(const FVector2D& WorldPosM) const;

	/** A5 stamp override: mark all cells overlapping the world-space box
	 *  [Min,Max] (m, inclusive) impassable. Idempotent. Returns cells stamped. */
	int32 StampImpassable(const FVector2D& BoxMin, const FVector2D& BoxMax);

	/** Garrison mirror (spec §3.1): set all cells overlapping the box to Cost
	 *  (high-but-passable entrance corridors). Idempotent, deterministic;
	 *  Impassable is rejected -- use StampImpassable for that. Returns cells
	 *  whose cost changed. */
	int32 StampCost(const FVector2D& BoxMin, const FVector2D& BoxMax, uint8 Cost);

	/** Clear all stamps back to passable (flat baseline). */
	void ResetToFlat();
};
