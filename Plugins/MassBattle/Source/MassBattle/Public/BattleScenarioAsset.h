// BattleScenarioAsset.h -- data-driven scenario composition (PR-1, audit
// D-2 root). A scenario is game data: unit type by EDU dictionary name,
// count, spawn transform -- reviewable in a diff, tunable in the editor, and
// authorable without the C++ module. The plugin owns the row/asset types
// (the demo module consumes them via the public API, ADR-18 pattern);
// SpawnClash resolves a scenario asset by name before falling back to its
// C++ tables (the CI backwards-compat path).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BattleScenarioAsset.generated.h"

/** One spawn request in a scenario: the EDU row is identified by its
 *  DICTIONARY name (B-3), never by index -- re-ordering the golden EDU must
 *  not silently swap knight for peasant in a scenario. */
USTRUCT(BlueprintType)
struct MASSBATTLE_API FDemoScenarioSpawn
{
	GENERATED_BODY()

	/** EDU 'dictionary' value (unique per row). */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FName Dictionary;

	UPROPERTY(EditAnywhere, Category = "Scenario")
	int32 Count = 8;

	/** Formation anchor, meters (the flat-tile fixture convention; the spawn
	 *  spec takes cm internally). */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector2D PosM = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Scenario")
	float YawDeg = 0.f;

	/** Ranks in the formed layout (files = ceil(Count / NumRanks)). */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	int32 NumRanks = 3;

	/** Faction stamp on the spawn spec (phase-3 TeamId; melee never reads
	 *  it). 0 = the fixture default. */
	UPROPERTY(EditAnywhere, Category = "Scenario")
	uint8 TeamId = 0;
};

/** The automatic post-spawn order (the C++ fixtures' behavior conventions;
 *  richer per-side scripting is deliberately not modeled yet -- the demo can
 *  always issue its own orders on top). */
UENUM(BlueprintType)
enum class EDemoScenarioAutoOrder : uint8
{
	None,
	/** wall/charge convention: the last spawned unit attacks the first. */
	LastAttacksFirst,
	/** mob convention: the last spawned unit attack-moves to the origin. */
	LastAttackMoveOrigin,
};

/** One authored scenario (a named clash shape): the spawn rows + the
 *  automatic order. Author under /Game/MassBattle/Data/Scenarios/ as
 *  DA_<variant>; SpawnClash resolves DA_<variant> by name before falling
 *  back to its C++ tables. */
UCLASS(BlueprintType)
class MASSBATTLE_API UDemoScenarioAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Scenario")
	TArray<FDemoScenarioSpawn> Spawns;

	UPROPERTY(EditAnywhere, Category = "Scenario")
	EDemoScenarioAutoOrder AutoOrder = EDemoScenarioAutoOrder::None;
};
