#include "BattleCostGridSubsystem.h"

void UBattleCostGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Flat tile per BattleGridGeo (arch §12): 86x86 cells at 3 m = 258 m across,
	// ~7.4k cells -- A* worst case is well inside the re-plan budget the T1.9
	// gate asserts.
	Grid.InitFlat(BattleGridGeo::TileDimCells, BattleGridGeo::TileDimCells,
		FVector2D::ZeroVector, BattleGridGeo::CellSizeM);
}

int32 UBattleCostGridSubsystem::StampImpassable(const FVector2D& BoxMinMeters, const FVector2D& BoxMaxMeters)
{
	return Grid.StampImpassable(BoxMinMeters, BoxMaxMeters);
}
