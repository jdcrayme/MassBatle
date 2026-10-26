#include "BattleCostGrid.h"

void FBattleCostGrid::InitFlat(int32 InDimX, int32 InDimY, const FVector2D& CenterMeters, float InCellSize)
{
	CellSize = InCellSize;
	DimX = InDimX;
	DimY = InDimY;
	Origin = CenterMeters - FVector2D(InDimX, InDimY) * (InCellSize * 0.5f);
	CellHeight.Init(0.f, NumCells());
	CellCost.Init(0, NumCells());
}

FVector2D FBattleCostGrid::CellCenter(int32 X, int32 Y) const
{
	return Origin + FVector2D(X + 0.5f, Y + 0.5f) * CellSize;
}

void FBattleCostGrid::WorldToCell(const FVector2D& WorldPos, int32& OutX, int32& OutY) const
{
	const FVector2D Rel = (WorldPos - Origin) / CellSize;
	OutX = FMath::FloorToInt32(Rel.X);
	OutY = FMath::FloorToInt32(Rel.Y);
}

int32 FBattleCostGrid::StampImpassable(const FVector2D& BoxMin, const FVector2D& BoxMax)
{
	return StampCost(BoxMin, BoxMax, Impassable);
}

int32 FBattleCostGrid::StampCost(const FVector2D& BoxMin, const FVector2D& BoxMax, uint8 Cost)
{
	int32 MinX, MinY, MaxX, MaxY;
	WorldToCell(BoxMin, MinX, MinY);
	WorldToCell(BoxMax, MaxX, MaxY);
	int32 Stamped = 0;
	for (int32 Y = FMath::Max(0, MinY); Y <= FMath::Min(DimY - 1, MaxY); ++Y)
	{
		for (int32 X = FMath::Max(0, MinX); X <= FMath::Min(DimX - 1, MaxX); ++X)
		{
			uint8& Cell = CellCost[IndexOf(X, Y)];
			if (Cell != Cost)
			{
				Cell = Cost;
				++Stamped;
			}
		}
	}
	return Stamped;
}

void FBattleCostGrid::ResetToFlat()
{
	for (uint8& Cost : CellCost)
	{
		Cost = 0;
	}
}
