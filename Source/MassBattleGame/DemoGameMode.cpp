// DemoGameMode.cpp -- see header.
#include "DemoGameMode.h"
#include "DemoUnitActor.h"
#include "DemoGarrisonProp.h"
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

void ADemoGameMode::SpawnGarrisonSample()
{
	if (bGarrisonSampleRegistered)
	{
		return;
	}
	UWorld* World = GetWorld();

	// Level-placed props first (authored in the map when present).
	int32 LevelProps = 0;
	for (TActorIterator<ADemoGarrisonProp> It(World); It; ++It)
	{
		++LevelProps; // self-register in their own BeginPlay
	}

	// Representative sample bootstrap (deviation, disclosed in the phase
	// report): one structure per family, clear of the demo unit lines at
	// x = +-50 m. Idempotent with level placement: skipped when the map
	// already carries props.
	if (LevelProps == 0)
	{
		struct FSample { EGarrisonStructureType Type; FVector2D PosM; float RotDeg; const TCHAR* Name; };
		const FSample Samples[3] = {
			{ EGarrisonStructureType::Building, FVector2D(0.f, 90.f), 0.f, TEXT("DemoKeep") },
			{ EGarrisonStructureType::Wall, FVector2D(-90.f, 0.f), 90.f, TEXT("DemoWall") },
			{ EGarrisonStructureType::Trench, FVector2D(90.f, 0.f), 90.f, TEXT("DemoTrench") },
		};
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.Name = NAME_None;
		for (const FSample& S : Samples)
		{
			const FVector Loc(S.PosM.X * 100.f, S.PosM.Y * 100.f, 0.f);
			ADemoGarrisonProp* Prop = World->SpawnActor<ADemoGarrisonProp>(
				Loc, FRotator(0.f, S.RotDeg, 0.f), Params);
			(void)Prop; // each prop self-registers in its own BeginPlay
		}
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGarrisonSample spawned 3 (building/wall/trench)"));
	}
	else
	{
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGarrisonSample level props present=%d (bootstrap skipped)"), LevelProps);
	}
	bGarrisonSampleRegistered = true;
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

	// Garrison sample first: registration stamps the cost grid so any early
	// player orders route around the structures from the first tick.
	SpawnGarrisonSample();

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
