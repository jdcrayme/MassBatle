// DemoGameMode.h -- demo game mode: wires the demo classes and discovers
// level-placed ADemoUnitActors, spawning their rosters through the plugin's
// batched spawn path once the world is safe (game thread, Mass not
// processing). All scenario content (ground, lights, units) is authored in
// the map; nothing is spawned procedurally.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DemoTypes.h"
#include "DemoGameMode.generated.h"

class ADemoUnitActor;

UCLASS()
class MASSBATTLEGAME_API ADemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADemoGameMode();

	// -- AGameModeBase --
	virtual void BeginPlay() override;

private:
	/** Deferred spawn pump: retries until the world + Mass are ready. */
	void TrySpawnLevelUnits();

	/** Registers the representative garrison sample (building / wall /
	 *  trench) on the demo map. The ADemoGarrisonProp class is level-placeable;
	 *  this bootstrap exists because the map file was not authored in the
	 *  garrison run -- owner can re-place them in-editor later (deviation
	 *  disclosed in the phase report). */
	void SpawnGarrisonSample();

	/** Grace-tick counter for the deferred spawn pump. */
	int32 SpawnGraceTicks = 0;

	/** True once every discovered unit actor has spawned its roster. */
	bool bLevelUnitsSpawned = false;

	/** True once the garrison sample is registered. */
	bool bGarrisonSampleRegistered = false;

	/** Level-placed unit actors found at BeginPlay (authored in the map). */
	UPROPERTY() TArray<TObjectPtr<ADemoUnitActor>> LevelUnits;
};
