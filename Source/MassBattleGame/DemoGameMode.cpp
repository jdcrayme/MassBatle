// DemoGameMode.cpp -- see header.
#include "DemoGameMode.h"
#include "DemoUnitActor.h"
#include "DemoCameraPawn.h"
#include "DemoPlayerController.h"
#include "DemoHUD.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleExecCommands.h"
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

	// Phase 2 (task 8, M3 geometry-only): the DEMO map with nothing
	// level-placed registers the representative mob sample through the
	// plugin's public Exec fixture (ADR-18 pattern: the game module consumes
	// plugin public API only). Gated to the demo map: the automation maps
	// have no placed units either, and a stray sample would poison their
	// world-state asserts (measured 2026-10-27: 56 extra live entities on
	// L_MassBattle_T0_Functional). The morale beat re-runs after Phase 4.
	if (LevelUnits.Num() == 0 && GetWorld()->GetMapName().Contains(TEXT("Demo")))
	{
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode no level units -- registering the M3 mob sample (geometry-only)"));
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::RegisterM3Sample);
	}

	// Entity creation must land on the game thread with Mass idle (the soak
	// controller waits out the first ticks for the same reason).
	GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
}

void ADemoGameMode::RegisterM3Sample()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Spawn needs Mass idle (the level-unit path waits out the same grace).
	if (!World->HasBegunPlay() || World->GetSubsystem<UMassEntitySubsystem>() == nullptr)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::RegisterM3Sample);
		return;
	}
	BattleExec::CombatSpawnClash({TEXT("mob")}, World);
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode M3 sample registered (SpawnClash mob)"));
}

void ADemoGameMode::VerifyGarrisonStructures()
{
	UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
	if (!Garrison || Garrison->GetStructureCount() == 0)
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoClickProbe none (no garrison structures registered)"));
		return;
	}
	// Probe each structure's own footprint center through the SAME static the
	// RMB click path uses (ADemoPlayerController::FindStructureAtWorld) -- a
	// miss here means a click there would fail too.
	int32 Ok = 0;
	int32 Probed = 0;
	for (const TPair<uint32, FGarrisonStructureRecord>& It : Garrison->GetAllStructures())
	{
		const FVector2D CenterM = It.Value.Footprint.GetCenter();
		const uint32 Resolved = ADemoPlayerController::FindStructureAtWorld(GetWorld(), CenterM * 100.f);
		++Probed;
		Ok += Resolved == It.Key ? 1 : 0;
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoClickProbe Structure=%u Center=(%.1f,%.1f)m Resolved=%u ok=%d"),
			It.Key, CenterM.X, CenterM.Y, Resolved, Resolved == It.Key ? 1 : 0);
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoClickProbe summary probes=%d ok=%d"), Probed, Ok);
	if (Ok != Probed)
	{
		UE_LOG(LogDemoBattle, Error,
			TEXT("EXEC_RESULT DemoClickProbe FAIL %d of %d structures not clickable"), Probed - Ok, Probed);
	}
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

	// Garrison structures are MAP actors (ADemoGarrisonProp placed in
	// L_MassBattle_Demo01); their BeginPlay registered the records + stamped
	// the cost grid at level start. One-time click-path probe, then units.
	VerifyGarrisonStructures();

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
