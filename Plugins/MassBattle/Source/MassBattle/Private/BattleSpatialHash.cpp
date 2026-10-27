// BattleSpatialHash.cpp -- counting-sort rebuild + circle queries (task 5).
#include "BattleSpatialHash.h"
#include "Mass/EntityHandle.h"

#include "Engine/World.h"

void UBattleSpatialHashSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DimCells = static_cast<int32>(FMath::CeilToInt((2.f * TileHalfExtentM) / CellSizeM));
	Reserve(512); // fixture scale; battle start re-reserves to the real size
}

void UBattleSpatialHashSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UBattleSpatialHashSubsystem::Reserve(int32 ExpectedEntries)
{
	// Arrays grow at battle start only (EC9): Reserve keeps existing capacity.
	const int32 NumCells = DimCells * DimCells;
	if (CellCounts.Num() < NumCells)
	{
		CellCounts.SetNum(NumCells);
		CellStarts.SetNum(NumCells + 1);
	}
	if (CellOffsets.Num() < NumCells)
	{
		CellOffsets.SetNum(NumCells);
	}
	SortedEntries.Reserve(ExpectedEntries);
	EntryPositionsM.Reserve(ExpectedEntries);
	InputScratch.Reserve(ExpectedEntries);
	InputPositionsScratch.Reserve(ExpectedEntries);
}

int32 UBattleSpatialHashSubsystem::CellIndexFor(const FVector2D& PosM) const
{
	const int32 X = FMath::FloorToInt((PosM.X - OriginM.X) / CellSizeM);
	const int32 Y = FMath::FloorToInt((PosM.Y - OriginM.Y) / CellSizeM);
	if (X < 0 || Y < 0 || X >= DimCells || Y >= DimCells)
	{
		return INDEX_NONE;
	}
	return Y * DimCells + X;
}

void UBattleSpatialHashSubsystem::Rebuild(TArray<FBattleHashEntry>&& Entries)
{
	TArray<FVector2D> ZeroPositions;
	ZeroPositions.SetNumUninitialized(Entries.Num());
	RebuildWithPositions(MoveTemp(Entries), MoveTemp(ZeroPositions));
}

void UBattleSpatialHashSubsystem::RebuildWithPositions(TArray<FBattleHashEntry>&& Entries, TArray<FVector2D>&& PositionsM)
{
	const double StartTime = FPlatformTime::Seconds();
	RebuildImpl(MoveTemp(Entries), MoveTemp(PositionsM));
	HashBuildMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
}

void UBattleSpatialHashSubsystem::RebuildImpl(TArray<FBattleHashEntry>&& Entries, TArray<FVector2D>&& PositionsM)
{
	checkf(Entries.Num() == PositionsM.Num(), TEXT("SpatialHash: entry/position count mismatch"));

	// 1. Handle-sort the source (spec §4.1: placement order = handle order, so
	// within-cell iteration is deterministic). Index sort: entries + parallel
	// positions permute together.
	InputScratch.SetNumUninitialized(Entries.Num());
	InputPositionsScratch.SetNumUninitialized(PositionsM.Num());
	OrderScratch.SetNumUninitialized(Entries.Num());
	TArray<int32>& Order = OrderScratch;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		Order[i] = i;
	}
	Order.Sort([&Entries](int32 A, int32 B)
	{
		const FMassEntityHandle& EA = Entries[A].Entity;
		const FMassEntityHandle& EB = Entries[B].Entity;
		if (EA.Index != EB.Index) { return EA.Index < EB.Index; }
		return EA.SerialNumber < EB.SerialNumber;
	});
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		InputScratch[i] = Entries[Order[i]];
		InputPositionsScratch[i] = PositionsM[Order[i]];
	}

	// 2. Counting sort, pass 1: per-cell counts.
	const int32 NumCells = DimCells * DimCells;
	for (int32 i = 0; i < NumCells; ++i)
	{
		CellCounts[i] = 0;
	}
	for (int32 i = 0; i < InputScratch.Num(); ++i)
	{
		const int32 Cell = CellIndexFor(InputPositionsScratch[i]);
		if (Cell >= 0)
		{
			++CellCounts[Cell];
		}
	}
	// Prefix sum -> cell start offsets.
	int32 Running = 0;
	for (int32 i = 0; i < NumCells; ++i)
	{
		CellStarts[i] = Running;
		CellOffsets[i] = Running;
		Running += CellCounts[i];
	}
	CellStarts[NumCells] = Running;

	// 3. Counting sort, pass 2: place (out-of-tile entries drop -- the battle
	// never leaves the tile; a drop is a caller bug surfaced by counts).
	SortedEntries.SetNumUninitialized(InputScratch.Num());
	EntryPositionsM.SetNumUninitialized(InputPositionsScratch.Num());
	for (int32 i = 0; i < InputScratch.Num(); ++i)
	{
		const int32 Cell = CellIndexFor(InputPositionsScratch[i]);
		if (Cell < 0)
		{
			continue;
		}
		const int32 Slot = CellOffsets[Cell]++;
		SortedEntries[Slot] = InputScratch[i];
		EntryPositionsM[Slot] = InputPositionsScratch[i];
	}
}

int32 UBattleSpatialHashSubsystem::FindSlotFor(FMassEntityHandle Entity) const
{
	int32 Lo = 0;
	int32 Hi = SortedEntries.Num() - 1;
	while (Lo <= Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		const FMassEntityHandle& MidEntity = SortedEntries[Mid].Entity;
		if (MidEntity == Entity)
		{
			return Mid;
		}
		if (MidEntity.Index < Entity.Index ||
			(MidEntity.Index == Entity.Index && MidEntity.SerialNumber < Entity.SerialNumber))
		{
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return INDEX_NONE;
}

void UBattleSpatialHashSubsystem::QueryCircle(const FVector2D& CenterM, float RadiusM, TArray<FBattleHashEntry>& OutEntries) const
{
	const int32 MinX = FMath::Max(0, FMath::FloorToInt((CenterM.X - RadiusM - OriginM.X) / CellSizeM));
	const int32 MaxX = FMath::Min(DimCells - 1, FMath::FloorToInt((CenterM.X + RadiusM - OriginM.X) / CellSizeM));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt((CenterM.Y - RadiusM - OriginM.Y) / CellSizeM));
	const int32 MaxY = FMath::Min(DimCells - 1, FMath::FloorToInt((CenterM.Y + RadiusM - OriginM.Y) / CellSizeM));
	if (MinX > MaxX || MinY > MaxY)
	{
		return;
	}
	const float RadiusSq = RadiusM * RadiusM;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const int32 Cell = Y * DimCells + X;
			for (int32 Slot = CellStarts[Cell]; Slot < CellStarts[Cell + 1]; ++Slot)
			{
				if (FVector2D::DistSquared(EntryPositionsM[Slot], CenterM) <= RadiusSq)
				{
					OutEntries.Add(SortedEntries[Slot]);
				}
			}
		}
	}
}
