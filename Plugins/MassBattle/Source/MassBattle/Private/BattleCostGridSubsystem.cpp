#include "BattleCostGridSubsystem.h"

void UBattleCostGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Flat 256x256 m tile at 3 m cells (arch §12): 86x86 cells, ~7.4k -- A*
	// worst case is well inside the re-plan budget the T1.9 gate asserts.
	Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
}

int32 UBattleCostGridSubsystem::StampImpassable(const FVector2D& BoxMinMeters, const FVector2D& BoxMaxMeters)
{
	return Grid.StampImpassable(BoxMinMeters, BoxMaxMeters);
}
