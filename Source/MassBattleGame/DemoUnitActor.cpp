// DemoUnitActor.cpp -- see header.
#include "DemoUnitActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

ADemoUnitActor::ADemoUnitActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADemoUnitActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// ONE team notion on the authoring surface: the demo faction (EDemoTeam)
	// drives the sim side (AUnitActor::TeamId stamps the spawn spec). An
	// author who sets Team=Enemy but leaves TeamId at its default would tint
	// the unit enemy while the ranged rules treat it as a friend -- the sync
	// keeps the two from disagreeing (audit B-3).
	TeamId = static_cast<uint8>(Team);

	// Editor placement aid only; in-game the ISM capsules are the visual.
	// Finite lifetime (refreshed on every construction/move/property change):
	// persistent lines lingered in the viewport after PIE and the old global
	// flush wiped every other actor's footprint.
	UWorld* World = GetWorld();
	if (World && World->WorldType == EWorldType::Editor)
	{
		DrawEditorFootprint();
	}
}

void ADemoUnitActor::DrawEditorFootprint() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	constexpr float LifeSec = 30.f;

	// Mirror SpawnEntities' spec resolution (0 = spec default). Row soldier
	// count is 40 in the golden fixture; the preview uses the same default.
	const int32 Count = SoldierCount > 0 ? SoldierCount : 40;
	const int32 Ranks = NumRanks > 0 ? NumRanks : 2;
	const float SpacingCm = (FormationSpacing > 0.f ? FormationSpacing : 1.2f) * 100.f;
	const int32 Files = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Count) / Ranks));

	const FVector Loc = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();

	// Front line (the anchor): green; depth extends BEHIND it.
	const FVector FrontL = Loc - Right * (Files * SpacingCm * 0.5f);
	const FVector FrontR = Loc + Right * (Files * SpacingCm * 0.5f);
	DrawDebugLine(World, FrontL, FrontR, FColor::Green, /*bPersistent*/ false, LifeSec, 0.f, 3.f);

	// Depth box behind the front line.
	const FVector BackL = FrontL - Forward * ((Ranks - 1) * SpacingCm);
	const FVector BackR = FrontR - Forward * ((Ranks - 1) * SpacingCm);
	DrawDebugLine(World, FrontL, BackL, FColor::Silver, false, LifeSec, 0.f, 1.f);
	DrawDebugLine(World, FrontR, BackR, FColor::Silver, false, LifeSec, 0.f, 1.f);
	DrawDebugLine(World, BackL, BackR, FColor::Silver, false, LifeSec, 0.f, 1.f);

	// Slot dots (rank-major, rank 0 = front).
	for (int32 Rank = 0; Rank < Ranks; ++Rank)
	{
		for (int32 File = 0; File < Files; ++File)
		{
			const FVector Slot = FrontL + Right * (File * SpacingCm) - Forward * (Rank * SpacingCm);
			DrawDebugPoint(World, Slot, 8.f, Rank == 0 ? FColor::Orange : FColor::Silver, false, LifeSec);
		}
	}

	// Facing arrow.
	DrawDebugLine(World, Loc, Loc + Forward * (SpacingCm * 2.f), FColor::Yellow, false, LifeSec, 0.f, 2.f);
}
