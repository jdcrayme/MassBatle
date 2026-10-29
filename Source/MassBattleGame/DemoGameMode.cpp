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
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "BattleRanged.h"
#include "BattleArrowVisualComponent.h"
#include "Components/SceneComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassEntitySubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

ADemoGameMode::ADemoGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ADemoCameraPawn::StaticClass();
	PlayerControllerClass = ADemoPlayerController::StaticClass();
	HUDClass = ADemoHUD::StaticClass();
}

void ADemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Phase 2 (task 10, owner demo): the combat pipeline is SESSION-ON in the
	// demo -- level-placed units must fight when ordered without console
	// setup (the M3 auto-sample only covers the empty-map case). The PLUGIN
	// defaults stay 0: the T1.8 movement-soak identity argument needs the
	// pipeline out of the CI baseline until the fold-in nightly.
	BattleExec::EnableDemoCombatPipeline();
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoCombat pipeline on (hash/clusters/tally/assign session-on)"));

	// PR-8: the gray-box arrow feed -- one ISM batched from the projectile
	// pool each frame, attached when the ranged flags are on. Flag-gated so a
	// Battle.Ranged.Enable=0 session stays visually identical to pre-ranged.
	UWorld* World = GetWorld();
	if (FRangedCVars::Enable() && World)
	{
		if (AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass()))
		{
			USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("ArrowHostRoot"));
			Root->SetMobility(EComponentMobility::Movable);
			Root->RegisterComponent();
			Host->SetRootComponent(Root);
			ArrowVisuals = NewObject<UBattleArrowVisualComponent>(Host, TEXT("ArrowVisuals"));
			ArrowVisuals->SetupAttachment(Root);
			ArrowVisuals->RegisterComponent();
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoArrows visual feed attached (PR-8 gray-box)"));
		}
	}

	// Scenario authoring is level-driven: every ADemoUnitActor placed in the
	// map defines one unit (position = anchor, yaw = facing, EduRowIndex /
	// SoldierCount / NumRanks / FormationSpacing editable in the details
	// panel, Team for the demo faction split).
	for (TActorIterator<ADemoUnitActor> It(GetWorld()); It; ++It)
	{
		LevelUnits.Add(*It);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode discovered %d level units"), LevelUnits.Num());

	// PR-1: the map-assigned scenario asset is the first authoring channel
	// (data-driven composition). Empty-asset maps keep today's M3 sample
	// fallback -- gated to the demo map exactly as before: the automation maps
	// have no placed units either, and a stray sample would poison their
	// world-state asserts (measured 2026-10-27: 56 extra live entities on
	// L_MassBattle_T0_Functional). The morale beat re-runs after Phase 4.
	if (LevelUnits.Num() == 0)
	{
		if (ScenarioAsset)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode spawning scenario asset %s (%d rows)"),
				*ScenarioAsset->GetName(), ScenarioAsset->Spawns.Num());
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnScenarioAsset);
		}
		else if (GetWorld()->GetMapName().Contains(TEXT("Demo")))
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode no level units -- registering the M3 mob sample (geometry-only)"));
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::RegisterM3Sample);
		}
	}

	// Entity creation must land on the game thread with Mass idle (the soak
	// controller waits out the first ticks for the same reason).
	GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
}

void ADemoGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Phase 2 task 10: adopt registry units the visuals don't know (the M3
	// sample, console SpawnClash) on a 1 s cadence -- cheap at demo scale.
	if (UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr)
	{
		if (const int32 Adopted = Visuals->AdoptUnknownRegistryUnits(
			GetWorld()->GetSubsystem<UUnitRegistrySubsystem>()); Adopted > 0)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode adopted %d registry units into the visuals"), Adopted);
		}
	}
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
	// Phase 3 (task 6, M4): the ranged sample rides the same no-level-units
	// gate -- a skirmishing archer line vs an approaching threat. Arrows are
	// VISIBLE since PR-8's gray-box feed (UBattleArrowVisualComponent, attached
	// in BeginPlay when the ranged flags are on); the debug overlay stays the
	// analytic view on top.
	BattleExec::RangedSpawnVolley({TEXT("skirmish")}, World);
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode M4 sample registered (SpawnVolley skirmish, gray-box arrow feed)"));
}

void ADemoGameMode::SpawnScenarioAsset()
{
	UWorld* World = GetWorld();
	if (!World || !ScenarioAsset || bScenarioSpawned)
	{
		return;
	}
	// Spawn needs Mass idle (the level-unit path waits out the same grace).
	if (!World->HasBegunPlay() || World->GetSubsystem<UMassEntitySubsystem>() == nullptr)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnScenarioAsset);
		return;
	}

	// The golden EDU is the unit-type catalog; rows match by dictionary name
	// (B-3). Same fixture file the plugin's SpawnClash reads.
	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath)
		|| !FBattleEduParser::Parse(EduText, Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		bScenarioSpawned = true;
		return;
	}

	const auto FindRow = [&Rows](const FName& Dictionary) -> int32
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FEduField* Dict = Rows[i].FindField(TEXT("dictionary"));
			if (Dict && Dict->GetValue().Equals(Dictionary.ToString(), ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	};

	TArray<FUnitHandle> Spawned;
	for (const FDemoScenarioSpawn& RowDef : ScenarioAsset->Spawns)
	{
		const int32 EduRow = FindRow(RowDef.Dictionary);
		if (EduRow == INDEX_NONE)
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL unknown EDU dictionary '%s'"),
				*RowDef.Dictionary.ToString());
			return;
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = EduRow;
		Spec.SoldierCount = RowDef.Count;
		Spec.SpawnPos = RowDef.PosM * 100.f;
		Spec.SpawnYaw = RowDef.YawDeg;
		Spec.NumRanks = RowDef.NumRanks;
		Spec.TeamId = RowDef.TeamId;
		FBattleSpawnResult Result;
		FString Error;
		if (!UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[EduRow], Result, Error)
			|| !Result.Unit.IsValid())
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL spawn '%s': %s"),
				*RowDef.Dictionary.ToString(), *Error);
			return;
		}
		Spawned.Add(Result.Unit);
	}

	switch (ScenarioAsset->AutoOrder)
	{
	case EDemoScenarioAutoOrder::LastAttackMoveOrigin:
		if (UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>())
		{
			Registry->IssueAttackMoveOrder(Spawned.Last(), FVector2D(0.f, 0.f), 0.f);
		}
		break;
	case EDemoScenarioAutoOrder::LastAttacksFirst:
		if (UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>())
		{
			Registry->IssueAttackUnitOrder(Spawned.Last(), Spawned[0]);
		}
		break;
	default:
		break;
	}

	bScenarioSpawned = true;
	FString Ids;
	for (const FUnitHandle& H : Spawned)
	{
		Ids += FString::Printf(TEXT(" %u:%u"), H.Index, H.Serial);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoScenario OK asset=%s rows=%d units:%s"),
		*ScenarioAsset->GetName(), Spawned.Num(), *Ids);
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
