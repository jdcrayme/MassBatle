// DemoGameMode.cpp -- see header.
#include "DemoGameMode.h"
#include "DemoUnitActor.h"
#include "DemoCameraPawn.h"
#include "DemoPlayerController.h"
#include "DemoHUD.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassEntitySubsystem.h"
#include "TimerManager.h"

ADemoGameMode::ADemoGameMode()
{
	DefaultPawnClass = ADemoCameraPawn::StaticClass();
	PlayerControllerClass = ADemoPlayerController::StaticClass();
	HUDClass = ADemoHUD::StaticClass();
}

void ADemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Scenario authoring is level-driven: every ADemoUnitActor placed in the
	// map defines one unit (position = anchor, yaw = facing, EduRowIndex /
	// SoldierCount / NumRanks / FormationSpacing editable in the details
	// panel, Team for the demo faction split).
	for (TActorIterator<ADemoUnitActor> It(GetWorld()); It; ++It)
	{
		LevelUnits.Add(*It);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode discovered %d level units"), LevelUnits.Num());

	// Entity creation must land on the game thread with Mass idle (the soak
	// controller waits out the first ticks for the same reason).
	GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
}

void ADemoGameMode::TrySpawnLevelUnits()
{
	if (bLevelUnitsSpawned)
	{
		return;
	}
	UWorld* World = GetWorld();
	UMassEntitySubsystem* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = World ? World->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!World || !Mass || !Visuals)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
		return;
	}
	// Wait for begin play + the first Mass ticks (soak-controller grace).
	constexpr int32 GraceTicks = 10;
	if (!World->HasBegunPlay() || SpawnGraceTicks++ < GraceTicks || Mass->GetMutableEntityManager().IsProcessing())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
		return;
	}

	int32 Spawned = 0;
	for (const TObjectPtr<ADemoUnitActor>& Unit : LevelUnits)
	{
		if (Unit == nullptr)
		{
			continue;
		}
		FBattleSpawnResult Result;
		FString Error;
		if (Unit->SpawnEntities(Result, Error) && Result.Unit.IsValid())
		{
			Visuals->RegisterUnit(Result.Unit, Unit->Team);
			++Spawned;
		}
		else
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoSpawn FAIL Actor=%s: %s"), *Unit->GetName(), *Error);
		}
	}
	bLevelUnitsSpawned = true;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoSpawn spawned=%d of %d"), Spawned, LevelUnits.Num());
}
