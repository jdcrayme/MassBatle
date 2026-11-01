// DemoGameMode.cpp -- see header.
#include "DemoGameMode.h"
#include "DemoUnitActor.h"
#include "DemoCameraPawn.h"
#include "DemoPlayerController.h"
#include "DemoHUD.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleCostGridSubsystem.h"
#include "BattleExecCommands.h"
#include "BattleMovementFragments.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "BattleRanged.h"
#include "BattleArrowVisualComponent.h"
#include "DemoGarrisonProp.h"
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
	// PR-1 (demo-shell spec sections 4/7): ranged + morale join the
	// session-on posture on the DEMO MAP only (the SpawnRout pattern,
	// ECVF_SetByCode; plugin defaults untouched). GATED like the field
	// fixture fallback: the func maps ride this game mode too
	// (L_MassBattle_T0_Functional logs "Game class is DemoGameMode"), and
	// the func baseline is morale-OFF -- the PR-12 probe measured
	// PairLifecycle regressing under morale session-on (reproduced in the
	// PR-1 func run before this gate landed).
	if (GetWorld()->GetMapName().Contains(TEXT("Demo")))
	{
		BattleExec::EnableRangedPipeline();
		BattleExec::EnableMoralePipeline();
		// PR-1 shoring (the owner PIE report 2026-10-31, root-caused by the
		// Demo.AttackTest anchor telemetry): on a LONG march the straggler
		// pace reads the progressive-fall-in tear (the intentional spawn
		// shape, mean slot error ~11.6 m >> DressBreak 1.8) and floors the
		// anchor at the 0.25x walk pace for the WHOLE march -- an 81 m
		// attack approach takes ~4 minutes, the units never reach contact,
		// and the engagement records stay empty (Engagements=0 in the
		// owner's DumpPairs). The phase-2 alpha-guard treaty covers only
		// the tail morph zone. The demo session runs the pace OFF: the
		// anchor marches at walk speed and the fall-in blend owns
		// convergence. Saved + restored in EndPlay (the func maps ride this
		// game mode -- the PairLifecycle travel-leak lesson).
		if (IConsoleVariable* Pace = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.StragglerPace")))
		{
			SavedStragglerPace = Pace->GetInt();
			Pace->Set(0, ECVF_SetByCode);
		}
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoPipelines combat+ranged+morale session-on (PR-1; straggler pace off for the demo session)"));
	}
	else
	{
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoPipelines combat session-on (the func-tier baseline; ranged/morale stay default)"));
	}

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
	// (data-driven composition). Empty-asset maps on the demo map fall
	// through to the M7 field fixture below.
	if (LevelUnits.Num() == 0)
	{
		if (ScenarioAsset)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode spawning scenario asset %s (%d rows)"),
				*ScenarioAsset->GetName(), ScenarioAsset->Spawns.Num());
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnScenarioAsset);
		}
		// PR-1: the no-level-units fallback on the demo map is the M7 FIELD
		// fixture -- the composed demo-shell battle (the spec demo-shell
		// section 2 order of battle + the section 3 beat sequencer). The
		// M3/M4/M5 console fixtures stay available for CI (Battle.*.Spawn*
		// commands) but the demo map no longer boots through their
		// sequential-reset chain. Gated to the demo map exactly as before:
		// the automation maps have no placed units either, and a stray sample
		// would poison their world-state asserts (measured 2026-10-27: 56
		// extra live entities on L_MassBattle_T0_Functional).
		else if (GetWorld()->GetMapName().Contains(TEXT("Demo")))
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode no level units -- registering the M7 field fixture (the composed demo-shell battle)"));
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnFieldFixture);
		}
	}

	// Entity creation must land on the game thread with Mass idle (the soak
	// controller waits out the first ticks for the same reason).
	GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
}

void ADemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	// PR-1: the session-on flags are DEMO-MAP-LIFETIME state. The func tier
	// boots the default (demo) map before traveling to the functional maps,
	// and ECVF_SetByCode values persist across travel in one process -- the
	// PR-1 func run measured PairLifecycle regressing under the leaked
	// morale flag (exactly the PR-12 probe shape; attributed by the stash
	// run). Restore the shipped default on map end so the automation
	// baseline stays flags-off. The combat family leaks by pre-existing
	// design (the phase-2 baseline documents the movement suite green at
	// Enable=1); the ranged family's shipped defaults are already on.
	if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Morale.Enable")))
	{
		C->Set(0, ECVF_SetByCode);
	}
	// The demo-session movement override (see BeginPlay): restore the
	// shipped straggler-pace value so the func tier's march-discipline
	// baselines (S5_MarchRefront, S4_WidthOverride) stay intact.
	if (IConsoleVariable* Pace = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.StragglerPace")))
	{
		Pace->Set(SavedStragglerPace, ECVF_SetByCode);
	}
}

void ADemoGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// PR-1: the beat sequencer (the spec demo-shell section 3 arc; beats 0-3
	// in this carve-out).
	TickSequencer(DeltaSeconds);
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

const ADemoGameMode::FDemoBeat* ADemoGameMode::GetBeatTable(int32& OutCount)
{
	// The spec demo-shell section 3 table, beats 0-3 in PR-1 (the morale and
	// garrison beats 4-7 are the PR-4 carve-out; the Volley/Clash labels ride
	// their time fallbacks until the metric polling lands with them).
	static const FDemoBeat Beats[] = {
		{ 0.f,  TEXT("Deploy") },
		{ 8.f,  TEXT("Advance") },
		{ 18.f, TEXT("Volley") },
		{ 30.f, TEXT("Clash") },
	};
	OutCount = UE_ARRAY_COUNT(Beats);
	return Beats;
}

void ADemoGameMode::TickSequencer(float DeltaSeconds)
{
	if (!bSequencerRunning)
	{
		return;
	}
	SequencerTime += DeltaSeconds;
	int32 Count = 0;
	const FDemoBeat* Beats = GetBeatTable(Count);
	const int32 Next = CurrentBeat + 1;
	if (Beats && Next < Count && Next >= 0 && SequencerTime >= Beats[Next].AtSeconds)
	{
		FireBeat(Next);
	}
}

void ADemoGameMode::FireBeat(int32 BeatIndex)
{
	int32 Count = 0;
	const FDemoBeat* Beats = GetBeatTable(Count);
	if (!Beats || BeatIndex < 0 || BeatIndex >= Count)
	{
		return;
	}
	CurrentBeat = BeatIndex;
	BeatLabel = Beats[BeatIndex].Label;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoBeat %d %s (t=%.1f)"),
		BeatIndex, *BeatLabel.ToString(), SequencerTime);

	// Beat 1: the enemy line advances on the player center (the spec section
	// 3 table). Scripted orders ride the same registry surface the player
	// uses and skip player-touched units, so a player order mid-beat simply
	// wins -- the determinism story holds (no sim-side scripting).
	if (BeatIndex == 1)
	{
		if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
		{
			const ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetWorld()->GetFirstPlayerController());
			static const TCHAR* AdvanceRoles[] = { TEXT("E1"), TEXT("E2"), TEXT("E3") };
			int32 Issued = 0;
			int32 Skipped = 0;
			for (const TCHAR* RoleName : AdvanceRoles)
			{
				const FUnitHandle* H = FieldUnitByRole.Find(RoleName);
				if (!H || !H->IsValid() || (PC && PC->WasPlayerOrdered(*H)))
				{
					++Skipped;
					continue;
				}
				if (Registry->IssueAttackMoveOrder(*H, FVector2D(-10.f, 0.f) * 100.f, 180.f))
				{
					++Issued;
				}
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoBeatOrders Beat=1 Issued=%d Skipped=%d"), Issued, Skipped);
		}
	}
}

const ADemoGameMode::FDemoFieldUnit* ADemoGameMode::GetFieldTable(int32& OutCount)
{
	// The spec demo-shell section 2.1 order of battle, ~384 entities. ONE
	// home: SpawnFieldFixture spawns it; BindSequencerToLevelUnits matches
	// the level-placed actors against it (dictionary + spawn position).
	static const FDemoFieldUnit FieldUnits[] = {
		{ TEXT("Peasant_Levy"),          40, 4, FVector2D(-40.f, -30.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P1") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D(-40.f,   0.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P2") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D(-40.f,  30.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P3") },
		{ TEXT("Feudal_Knights"),        24, 3, FVector2D(-52.f, -52.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("C1") },
		{ TEXT("Peasant_Archers"),       40, 3, FVector2D(-58.f,  15.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P4") },
		{ TEXT("Feudal_Knights"),        12, 2, FVector2D(-70.f,   0.f),   0.f, 0, 2, true,  EDemoTeam::Player, TEXT("P5") },
		{ TEXT("Peasant_Levy"),          40, 4, FVector2D( 40.f,  15.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E1") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D( 40.f, -15.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E2") },
		{ TEXT("Sergeant_Spearmen"),     32, 4, FVector2D( 58.f,   0.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E3") },
		{ TEXT("Mercenary_Crossbowmen"), 40, 3, FVector2D( 58.f,  25.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E4") },
		{ TEXT("Feudal_Knights"),        24, 3, FVector2D( 52.f,  48.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("C2") },
		{ TEXT("Feudal_Knights"),        12, 2, FVector2D( 72.f,   0.f), 180.f, 1, 2, true,  EDemoTeam::Enemy,  TEXT("E5") },
	};
	OutCount = UE_ARRAY_COUNT(FieldUnits);
	return FieldUnits;
}

void ADemoGameMode::BindSequencerToLevelUnits()
{
	UWorld* World = GetWorld();
	UDemoUnitVisualSubsystem* Visuals = World ? World->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!World || !Visuals)
	{
		return;
	}
	int32 TableCount = 0;
	const FDemoFieldUnit* Table = GetFieldTable(TableCount);
	int32 Bound = 0;
	// Role = the table row matching the placed actor's dictionary AND spawn
	// position (the map pass authors actors at exactly the table positions;
	// 5 m tolerance absorbs float round-trip). The spawned handle rides the
	// same actor the level path registered.
	for (TActorIterator<ADemoUnitActor> It(World); It; ++It)
	{
		ADemoUnitActor* Actor = *It;
		const FUnitHandle* BoundHandle = nullptr;
		for (const TPair<FUnitHandle, TObjectPtr<ADemoUnitActor>>& Kvp : HandleByActor)
		{
			if (Kvp.Value == Actor) { BoundHandle = &Kvp.Key; break; }
		}
		if (!BoundHandle || !BoundHandle->IsValid())
		{
			continue;
		}
		const FVector Pos = Actor->GetActorLocation();
		for (int32 i = 0; i < TableCount; ++i)
		{
			const FDemoFieldUnit& Row = Table[i];
			const float DX = Pos.X * 0.01f - Row.PosM.X;
			const float DY = Pos.Y * 0.01f - Row.PosM.Y;
			if (FMath::Abs(DX) < 5.f && FMath::Abs(DY) < 5.f
				&& Actor->UnitDictionary == Row.Dictionary)
			{
				FieldUnitByRole.Add(Row.Role, *BoundHandle);
				++Bound;
				break;
			}
		}
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoBind roles=%d of %d (the sequencer rides the level units)"), Bound, TableCount);
	if (Bound == TableCount)
	{
		bSequencerRunning = true;
		SequencerTime = 0.f;
		FireBeat(0);
	}
}

void ADemoGameMode::EnsureRowStatsCache()
{
	if (bRowStatsCacheBuilt)
	{
		return;
	}
	bRowStatsCacheBuilt = true;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!BattleExec::LoadGoldenEduRows(Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoRowStats FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		return;
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		FBattleRowStats Stats;
		FString Error;
		if (FBattleRowStats::FromEdu(Rows[i], Stats, Error))
		{
			RowStatsByRowIdx.Add(i, Stats);
		}
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoRowStats rows=%d (the banner stats cache)"), RowStatsByRowIdx.Num());
}

const FBattleRowStats* ADemoGameMode::FindRowStatsForUnit(FUnitHandle Handle)
{
	EnsureRowStatsCache();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return nullptr;
	}
	const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	return RowStatsByRowIdx.Find(Spec->EduRowIndex);
}

void ADemoGameMode::AttackTestFire(int32 UnitIdx, int32 TargetIdx)
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	const FUnitHandle Attacker(static_cast<uint32>(UnitIdx), 1);
	const FUnitHandle Target(static_cast<uint32>(TargetIdx), 1);
	const bool bIssued = Registry->IssueAttackUnitOrder(Attacker, Target);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoAttackTest Fire %u->%u issued=%d"), Attacker.Index, Target.Index, bIssued ? 1 : 0);
	if (bIssued)
	{
		for (int32 Poll = 1; Poll <= 15; ++Poll)
		{
			GetWorldTimerManager().SetTimer(PollHandles[Poll - 1],
				FTimerDelegate::CreateUObject(this, &ADemoGameMode::AttackTestPoll, UnitIdx, TargetIdx, Poll),
				2.f * Poll, false);
		}
	}
}

void ADemoGameMode::AttackTestPoll(int32 UnitIdx, int32 TargetIdx, int32 Poll)
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !Mass)
	{
		return;
	}
	const FUnitHandle Attacker(static_cast<uint32>(UnitIdx), 1);
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Attacker);
	FVector2D Anchor = FVector2D::ZeroVector;
	FVector2D Blob = FVector2D::ZeroVector;
	float Fatigue = -1.f;
	if (Roster && Roster->Num() > 0)
	{
		if (const FUnitFormationSharedFragment* F =
			Mass->GetMutableEntityManager().GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
		{
			Anchor = F->AnchorPos;
		}
	}
	if (const FBattleAttributeSnapshot* Cached = Registry->FindCachedAttributes(Attacker))
	{
		Fatigue = Cached->Fatigue;
	}
	// The visual blob center (the trimmed entity centroid) + the delta from
	// the sim anchor: the number that sizes the entity-vs-banner tear the
	// owner reported (PR-2 shoring).
	if (UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>())
	{
		FDemoUnitBodyInfo Info;
		if (Visuals->GetUnitBodyInfo(Attacker, Info))
		{
			Blob = Info.VisualCentroidCm;
		}
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoAttackTest Poll=%d Unit=%u Anchor=(%.1f,%.1f)m Blob=(%.1f,%.1f)m Tear=%.1fm Fatigue=%.3f"),
		Poll, Attacker.Index, Anchor.X * 0.01f, Anchor.Y * 0.01f, Blob.X * 0.01f, Blob.Y * 0.01f,
		FVector2D::Distance(Anchor, Blob) * 0.01f, Fatigue);
}

void ADemoGameMode::SpawnFieldFixture()
{
	UWorld* World = GetWorld();
	if (!World || bFieldSpawned)
	{
		return;
	}
	// Spawn needs Mass idle (the level-unit path waits out the same grace).
	UMassEntitySubsystem* Mass = World->GetSubsystem<UMassEntitySubsystem>();
	if (!World->HasBegunPlay() || Mass == nullptr
		|| Mass->GetMutableEntityManager().IsProcessing())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnFieldFixture);
		return;
	}
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UDemoUnitVisualSubsystem* Visuals = World->GetSubsystem<UDemoUnitVisualSubsystem>();
	if (!CostGrid || !Registry || !Visuals)
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL no subsystems"));
		return;
	}

	// The session-on posture (the SpawnRout pattern) -- the fixture runs the
	// full stack regardless of the shipping defaults (PR-1).
	BattleExec::EnableDemoCombatPipeline();
	BattleExec::EnableRangedPipeline();
	BattleExec::EnableMoralePipeline();
	CostGrid->ResetToFlat();
	Registry->ResetBattleState();

	// Structures first: their BeginPlay registers the records + stamps the
	// cost grid, so the units below route around them from the first plan.
	// Positions are the spec demo-shell section 2.2 targets (meters).
	struct FFieldStructure
	{
		FVector2D PosM;
		float YawDeg;
		EGarrisonStructureType Type;
	};
	static const FFieldStructure Structures[] = {
		{ FVector2D(8.f, 52.f),    0.f,  EGarrisonStructureType::Building },  // the village objective (beat 6)
		{ FVector2D(52.f, 80.f),   0.f,  EGarrisonStructureType::Building },  // village second slot (north; the building
		                                                                       // fixture is 48x20 m -- the boot probe
		                                                                       // caught the first spacing overlapping)
		{ FVector2D(52.f, -52.f), 90.f, EGarrisonStructureType::Wall },      // covers the enemy right
		{ FVector2D(-26.f, 30.f),  0.f,  EGarrisonStructureType::Trench },   // in front of P3
	};
	int32 StructuresSpawned = 0;
	for (const FFieldStructure& S : Structures)
	{
		const FTransform Xform(FRotator(0.f, S.YawDeg, 0.f),
			FVector(S.PosM.X * 100.f, S.PosM.Y * 100.f, 0.f));
		ADemoGarrisonProp* Prop = World->SpawnActorDeferred<ADemoGarrisonProp>(
			ADemoGarrisonProp::StaticClass(), Xform);
		if (!Prop)
		{
			continue;
		}
		Prop->SetStructureType(S.Type);
		Prop->FinishSpawning(Xform);
		++StructuresSpawned;
	}

	// Units: dictionary-keyed (the PR-11 rule), generals stamped
	// (OfficerCount 2 + bIsGeneral). The spec demo-shell section 2.1 order
	// of battle, ~384 entities -- the ONE-HOME table (shared with the
	// level-unit binding).
	int32 FieldCount = 0;
	const FDemoFieldUnit* FieldUnits = GetFieldTable(FieldCount);

	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!BattleExec::LoadGoldenEduRows(Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		return;
	}

	int32 Spawned = 0;
	for (int32 FieldIdx = 0; FieldIdx < FieldCount; ++FieldIdx)
	{
		const FDemoFieldUnit& U = FieldUnits[FieldIdx];
		const int32 EduRow = BattleExec::FindEduRowIndexByDictionary(Rows, U.Dictionary);
		if (EduRow == INDEX_NONE)
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL unknown EDU dictionary '%s'"),
				U.Dictionary);
			return;
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = EduRow;
		Spec.SoldierCount = U.Count;
		Spec.UnitName = U.Role.ToString(); // the HUD/card surface reads the role
		Spec.SpawnPos = U.PosM * 100.f;
		Spec.SpawnYaw = U.YawDeg;
		Spec.NumRanks = U.NumRanks;
		Spec.TeamId = U.TeamId;
		Spec.OfficerCount = U.OfficerCount;
		Spec.bIsGeneral = U.bIsGeneral;
		FBattleSpawnResult Result;
		FString Error;
		if (!UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[EduRow], Result, Error)
			|| !Result.Unit.IsValid())
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL spawn %s: %s"),
				*U.Role.ToString(), *Error);
			return;
		}
		Visuals->RegisterUnit(Result.Unit, U.Team);
		FieldUnitByRole.Add(U.Role, Result.Unit);
		++Spawned;
	}

	bFieldSpawned = true;
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoField OK rows=%d of %d structures=%d (the demo-shell spec section 2 field, morale session-on)"),
		Spawned, FieldCount, StructuresSpawned);

	// The beat sequencer (the spec section 3 table; PR-1 runs beats 0-3).
	bSequencerRunning = true;
	SequencerTime = 0.f;
	FireBeat(0);
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

	TArray<FUnitHandle> Spawned;
	for (const FDemoScenarioSpawn& RowDef : ScenarioAsset->Spawns)
	{
		// ONE home for the dictionary lookup (the plugin's BattleExec helper;
		// this lambda was its third re-typed copy -- audit B-3).
		const int32 EduRow = BattleExec::FindEduRowIndexByDictionary(Rows, RowDef.Dictionary.ToString());
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
			HandleByActor.Add(Result.Unit, Unit);
			++Spawned;
		}
		else
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoSpawn FAIL Actor=%s: %s"), *Unit->GetName(), *Error);
		}
	}
	bLevelUnitsSpawned = true;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoSpawn spawned=%d of %d"), Spawned, LevelUnits.Num());
	// PR-4 early slice: the beat sequencer rides the level units (the map
	// pass authored them at the field-table positions; the binding matches
	// dictionary + position). The demo map now plays beats 0-3 by itself.
	BindSequencerToLevelUnits();
}

// -- Demo.SpawnField (PR-1) ----------------------------------------------------
// The headless/PIE boot command for the M7 field fixture: spawns the
// demo-shell spec section 2 order of battle + the garrison structures and
// starts the beat sequencer. Works on ANY map (the sandbox boot evidence
// path); the demo map runs it from the no-level-units fallback. The m5_boot
// precedent: -game -nullrhi -ExecCmds="Demo.SpawnField" produces the
// EXEC_RESULT evidence lines.

static FAutoConsoleCommandWithWorldAndArgs GDemoSpawnField(
	TEXT("Demo.SpawnField"),
	TEXT("M7 field fixture (PR-1): the demo-shell spec section 2 order of battle + the garrison structures + the beat sequencer; EXEC_RESULT DemoField"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr)
		{
			GM->SpawnFieldFixture();
		}
	}));

// -- Demo.AttackTest (PR-1 shoring) ---------------------------------------------
// The delayed real-attack repro for the owner report 2026-10-31 (RMB attack
// walks to the enemy and never engages): fires IssueAttackUnitOrder after
// [DelaySec], then polls engagement telemetry (head-entity anchor + cached
// fatigue) every 2 s x 15 -- the anchors answer the follow question, the
// fatigue/kill lines answer the engagement question. Usable in PIE (the
// console) and headless (-ExecCmds) alike; handles are 1-based unit indices.
static FAutoConsoleCommandWithWorldAndArgs GDemoAttackTest(
	TEXT("Demo.AttackTest"),
	TEXT("[unitIdx=1] [targetIdx=8] [delaySec=5] -- delayed AttackUnit + engagement telemetry (anchor trajectory + fatigue); EXEC_RESULT DemoAttackTest"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
	{
		if (ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr)
		{
			const int32 UnitIdx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
			const int32 TargetIdx = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 8;
			const float Delay = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 5.f;
			FTimerHandle FireHandle;
			GM->GetWorldTimerManager().SetTimer(FireHandle,
				FTimerDelegate::CreateUObject(GM, &ADemoGameMode::AttackTestFire, UnitIdx, TargetIdx),
				FMath::Max(1.f, Delay), false);
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoAttackTest armed unit=%d target=%d delay=%.1f"), UnitIdx, TargetIdx, Delay);
		}
	}));
