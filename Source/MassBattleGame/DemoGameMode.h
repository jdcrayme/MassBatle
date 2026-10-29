// DemoGameMode.h -- demo game mode: wires the demo classes and discovers
// level-placed ADemoUnitActors, spawning their rosters through the plugin's
// batched spawn path once the world is safe (game thread, Mass not
// processing). All scenario content (ground, lights, units) is authored in
// the map; nothing is spawned procedurally.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DemoTypes.h"
#include "BattleScenarioAsset.h"
#include "DemoGameMode.generated.h"

class ADemoUnitActor;
class UBattleArrowVisualComponent;

UCLASS()
class MASSBATTLEGAME_API ADemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADemoGameMode();

	// -- AGameModeBase --
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Deferred spawn pump: retries until the world + Mass are ready. */
	void TrySpawnLevelUnits();
	void RegisterM3Sample();
	/** PR-1: spawns the map-assigned scenario asset's rows (data-driven
	 *  composition; the M3 sample below is the empty-asset fallback). */
	void SpawnScenarioAsset();
	void AdoptVisuals();

	/** PR-1: the authored scenario (spawn rows + auto order) assigned in the
	 *  map. Empty keeps the M3 sample fallback. */
	UPROPERTY(EditAnywhere, Category = "Battle|Demo")
	TObjectPtr<UDemoScenarioAsset> ScenarioAsset;

	/** True once the scenario asset spawned (or none was assigned). */
	bool bScenarioSpawned = false;

	/** One-time click-path probe after the level settles: resolves each
	 *  registered structure's own footprint center through the SAME static
	 *  the RMB click uses -- EXEC_RESULT DemoClickProbe evidence that
	 *  map-placed structures are clickable. */
	void VerifyGarrisonStructures();

	/** Grace-tick counter for the deferred spawn pump. */
	int32 SpawnGraceTicks = 0;

	/** True once every discovered unit actor has spawned its roster. */
	bool bLevelUnitsSpawned = false;

	/** Level-placed unit actors found at BeginPlay (authored in the map). */
	UPROPERTY() TArray<TObjectPtr<ADemoUnitActor>> LevelUnits;

	/** PR-8: the gray-box arrow feed (one ISM batched from the projectile
	 *  pool), attached when the ranged flags are on. */
	UPROPERTY(Transient) TObjectPtr<UBattleArrowVisualComponent> ArrowVisuals;
};
