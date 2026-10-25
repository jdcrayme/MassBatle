// Battle.* console command implementations. Task 4 adds the registry probes;
// later tasks fill in their own functions and register their commands.
// Delegate signature note: FConsoleCommandWithWorldAndArgsDelegate is
// void(const TArray<FString>& Args, UWorld* World) -- args FIRST.
#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "BattleFragments.h"
#include "BattleDeathPath.h"
#include "BattleRngRegistry.h"
#include "BattleCostGridSubsystem.h"
#include "BattleSpawnLibrary.h"
#include "BattleStateHash.h"
#include "BattleUnitTrait.h"
#include "MassBattleModule.h"
#include "UnitActor.h"
#include "BattleDebugSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassEntityConfigAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "EngineUtils.h"

namespace
{
	UUnitRegistrySubsystem* GetRegistry(UWorld* World)
	{
		return World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	}
}

namespace BattleExec
{
	void DumpRegistry(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = GetRegistry(World);
		if (!Registry)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT DumpRegistry FAIL no world/registry"));
			return;
		}

		const TArray<FUnitHandle> Handles = Registry->GetSortedHandles();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpRegistry Begin Units=%d FreeList=%d"),
		       Handles.Num(), Registry->GetFreeListSize());
		for (const FUnitHandle& Handle : Handles)
		{
			const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
			const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
			const FBattleAttributeSnapshot* Attrs = Registry->FindCachedAttributes(Handle);
			UE_LOG(LogMassBattle, Display,
			       TEXT("EXEC_RESULT DumpRegistry Unit=%u:%u roster=%d dirty=%d row=%d name=%s morale=%.2f"),
			       Handle.Index, Handle.Serial,
			       State ? State->Roster.Num() : -1,
			       Registry->IsStatsDirty(Handle) ? 1 : 0,
			       Spec ? Spec->EduRowIndex : INDEX_NONE,
			       Spec ? *Spec->UnitName : TEXT("<none>"),
			       Attrs ? Attrs->Morale : 0.f);
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpRegistry End"));
	}

	void StateHash(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT StateHash FAIL no world"));
			return;
		}
		// Full sim-state CityHash64 (Task 10; audit M7 -- this command used to
		// emit the registry bookkeeping placeholder).
		const uint64 Hash = FBattleStateHash::Compute(World);
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT StateHash Hash=%016llx Units=%d"),
		       Hash, Registry ? Registry->GetUnitCount() : 0);
	}

	// -- Task 10: seeded-RNG discipline probe (brief 5 Task 10.2; restored by
	// the audit fix pass, which found it was never implemented) --
	void RngSelfTest(const TArray<FString>& Args, UWorld* World)
	{
		const int32 RootSeed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 20261024;
		FBattleRngRegistry A;
		FBattleRngRegistry B;
		A.Seed(RootSeed);
		B.Seed(RootSeed);

		uint64 FoldA = 1469598103934665603ull;
		uint64 FoldB = FoldA;
		for (int32 i = 0; i < 1000; ++i)
		{
			const uint32 Va = static_cast<uint32>(A.GetStream(TEXT("Pairing")).RandRange(0, 1000000));
			const uint32 Vb = static_cast<uint32>(B.GetStream(TEXT("Pairing")).RandRange(0, 1000000));
			FoldA = (FoldA ^ Va) * 1099511628211ull;
			FoldB = (FoldB ^ Vb) * 1099511628211ull;
		}
		FBattleRngRegistry C;
		C.Seed(RootSeed + 1);
		const uint32 FirstOther = static_cast<uint32>(C.GetStream(TEXT("Pairing")).RandRange(0, 1000000));
		const uint32 FirstA = static_cast<uint32>(A.GetStream(TEXT("Pairing")).RandRange(0, 1000000));

		const bool bEqualRootsMatch = (FoldA == FoldB);
		const bool bDifferentRootsDiverge = (FirstA != FirstOther);
		const bool bPass = bEqualRootsMatch && bDifferentRootsDiverge;
		UE_LOG(LogMassBattle, Display,
		       TEXT("EXEC_RESULT RngSelfTest Seed=%d EqualRootsMatch=%d DifferentRootsDiverge=%d Streams=%d"),
		       RootSeed, bEqualRootsMatch ? 1 : 0, bDifferentRootsDiverge ? 1 : 0, A.GetStreamCount());
		if (bPass)
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RngSelfTest PASS"));
		}
		else
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT RngSelfTest FAIL"));
		}
	}

	// -- Task 6: spawn path --
	void SpawnRoster(const TArray<FString>& Args, UWorld* World)
	{
		// Args: <EduRow> <Count> [Name]
		if (Args.Num() < 2 || !World)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnRoster FAIL usage: <EduRow> <Count> [Name]"));
			return;
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = FCString::Atoi(*Args[0]);
		Spec.SoldierCount = FCString::Atoi(*Args[1]);
		Spec.UnitName = Args.Num() > 2 ? Args[2] : FString::Printf(TEXT("Exec_%d"), Spec.EduRowIndex);

		const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString EduText;
		TArray<FEduUnit> Rows;
		FEduParseError Err;
		if (!FFileHelper::LoadFileToString(EduText, *FixturePath) || !FBattleEduParser::Parse(EduText, Rows, Err) || !Rows.IsValidIndex(Spec.EduRowIndex))
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnRoster FAIL bad EDU row %d (%s)"), Spec.EduRowIndex, *Err.Message);
			return;
		}
		FBattleSpawnResult Result;
		FString Error;
		if (UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[Spec.EduRowIndex], Result, Error))
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnRoster OK Unit=%u:%u Entities=%d"),
			       Result.Unit.Index, Result.Unit.Serial, Result.NumEntities); // SpawnUnit already logged SpawnPerf
		}
		else
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnRoster FAIL %s"), *Error);
		}
	}

	// -- Task 6: T0 config authoring (A5 route) --
	void BuildT0Configs(const TArray<FString>& Args, UWorld* World)
	{
#if WITH_EDITOR
		struct FConfigSpec { const TCHAR* Name; int32 Row; };
		const FConfigSpec Specs[] = {
			{ TEXT("PeasantLevy"), 0 },
			{ TEXT("SergeantSpearmen"), 1 },
		};

		int32 Authored = 0;
		for (const FConfigSpec& Spec : Specs)
		{
			const FString PackageName = FString::Printf(TEXT("/Game/MassBattle/Data/Units/M_Battle_%s"), Spec.Name);
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = CreatePackage(*PackageName);
			}
			const FName AssetName(*FString::Printf(TEXT("M_Battle_%s"), Spec.Name));
			UMassEntityConfigAsset* Asset = FindObject<UMassEntityConfigAsset>(Package, *AssetName.ToString());
			if (!Asset)
			{
				Asset = NewObject<UMassEntityConfigAsset>(Package, AssetName, RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(Asset);
			}

			// Trait/BuildTemplate route (A5): one composition trait per config.
			// AddTrait returns the existing instance when already present.
			UBattleUnitTrait* Trait = Cast<UBattleUnitTrait>(Asset->AddTrait(UBattleUnitTrait::StaticClass()));
			if (Trait)
			{
				// Fixture row stats (golden_edu_v1.txt) mirrored here so the
				// config matches the C++ archetype's stats; row index links back.
				Trait->EduRowIndex = Spec.Row;
				Trait->bInfantry = true;
				Trait->bMelee = true;
				if (Spec.Row == 0) // Peasant Levy
				{
					Trait->BaseHealth = 1.f; Trait->MeleeAttack = 1.f; Trait->MeleeDefense = 2.f;
					Trait->Armor = 0.f; Trait->MoraleBase = 2.f; Trait->ChargeDistance = 30.f;
				}
				else // Sergeant Spearmen
				{
					Trait->BaseHealth = 1.f; Trait->MeleeAttack = 4.f; Trait->MeleeDefense = 4.f;
					Trait->Armor = 4.f; Trait->MoraleBase = 6.f; Trait->ChargeDistance = 30.f;
				}
			}

			const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
			{
				++Authored;
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT BuildT0Configs Asset=%s Saved=%s"), *PackageName, *FileName);
			}
			else
			{
				UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT BuildT0Configs FAIL save %s"), *PackageName);
			}
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT BuildT0Configs DONE Authored=%d"), Authored);
#else
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT BuildT0Configs FAIL editor-only"));
#endif
	}

	// -- Task 12: B.4 metrics dump --
	void DumpMetrics(const TArray<FString>& Args, UWorld* World)
	{
		if (UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr)
		{
			Debug->DumpMetrics();
		}
		else
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT DumpMetrics FAIL no debug subsystem"));
		}
	}

	// -- Task 8: GAS attribute probe --
	void SetAttr(const TArray<FString>& Args, UWorld* World)
	{
		// Args: <UnitIndex> <AttributeName> <Value>
		if (Args.Num() < 3 || !World)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SetAttr FAIL usage: <UnitIndex> <AttributeName> <Value>"));
			return;
		}
		const uint32 UnitIndex = (uint32)FCString::Atoi(*Args[0]);
		const FString AttrName = Args[1];
		const float Value = FCString::Atof(*Args[2]);

		bool bFound = false;
		for (TActorIterator<AUnitActor> It(World); It; ++It)
		{
			AUnitActor* Actor = *It;
			if (!Actor->GetUnitHandle().IsValid() || Actor->GetUnitHandle().Index != UnitIndex)
			{
				continue;
			}
			// If a stale actor with the same slot index still exists, prefer the
			// actor whose handle the registry currently validates (audit low).
			if (UUnitRegistrySubsystem* Reg = GetRegistry(World);
				Reg && !Reg->IsHandleValid(Actor->GetUnitHandle()))
			{
				continue;
			}
			bFound = true;
			UAbilitySystemComponent* ASC = Actor->GetAbilitySystemComponent();
			const FGameplayAttribute Attribute = UUnitAttributeSet::FindAttributeByName(AttrName);
			if (!Attribute.IsValid())
			{
				UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SetAttr FAIL unknown attribute %s"), *AttrName);
				return;
			}
			ASC->SetNumericAttributeBase(Attribute, Value);
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SetAttr Unit=%u Attr=%s Value=%.4f"), UnitIndex, *AttrName, Value);
			break;
		}
		if (!bFound)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SetAttr FAIL no spawned unit %u"), UnitIndex);
		}
	}

	// -- Task 9: kill path (deferred REGULAR tag per ADR-15 final state +
	// explicit flush; E5 audited flush point: the observer fires during this
	// flush) --
	void KillEntity(const TArray<FString>& Args, UWorld* World)
	{
		// Args: <UnitIndex> <RosterIndex>
		if (Args.Num() < 2 || !World)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT KillEntity FAIL usage: <UnitIndex> <RosterIndex>"));
			return;
		}
		const uint32 UnitIndex = (uint32)FCString::Atoi(*Args[0]);
		const int32 RosterIndex = FCString::Atoi(*Args[1]);

		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
		if (!Registry || !ES)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT KillEntity FAIL no subsystems"));
			return;
		}
		FMassEntityManager& EM = ES->GetMutableEntityManager();
		checkf(!EM.IsProcessing(), TEXT("KillEntity: forbidden while Mass is processing"));

		FUnitHandle Target;
		for (const FUnitHandle& H : Registry->GetSortedHandles())
		{
			if (H.Index == UnitIndex) { Target = H; break; }
		}
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Target);
		if (!Target.IsValid() || !Roster || !Roster->IsValidIndex(RosterIndex))
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT KillEntity FAIL unit=%u roster=%d"), UnitIndex, RosterIndex);
			return;
		}
		const FMassEntityHandle Entity = (*Roster)[RosterIndex];
		EM.Defer().AddTag<FDeadTag>(Entity);
		EM.FlushCommands();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT KillEntity Unit=%u:%u RosterIndex=%d Entity=%u:%u Flushed=1"),
		       Target.Index, Target.Serial, RosterIndex, Entity.Index, Entity.SerialNumber);
	}

	// -- Task 6: shared-fragment dump --
	void DumpSharedFrag(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		if (!ES || !Registry)
		{
			return;
		}
		FMassEntityManager& EM = ES->GetMutableEntityManager();
		const TArray<FUnitHandle> Units = Registry->GetSortedHandles();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpSharedFrag Begin Units=%d"), Units.Num());
		for (const FUnitHandle& Handle : Units)
		{
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
			if (!Roster || Roster->Num() == 0) { continue; }
			const FUnitStatsSharedFragment* Stats = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*Roster)[0]);
			if (Stats)
			{
				UE_LOG(LogMassBattle, Display,
				       TEXT("EXEC_RESULT DumpSharedFrag Unit=%u:%u Mem=%p Speed=%.3f MeleeAtk=%.3f MoraleBonus=%.3f"),
				       Handle.Index, Handle.Serial, (const void*)Stats, Stats->SpeedMult, Stats->MeleeAttackMult, Stats->MoraleBonus);
			}
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpSharedFrag End"));
	}
	// -- Phase 1 Task 3: A5 cost-grid stamp override (meters, world space) --
	void StampImpassable(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 4)
		{
			UE_LOG(LogMassBattle, Display,
			       TEXT("EXEC_RESULT StampImpassable FAIL BadArgs (need: minX minY maxX maxY, meters)"));
			return;
		}
		UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
		if (!CostGrid)
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT StampImpassable FAIL NoSubsystem"));
			return;
		}
		const FVector2D BoxMin(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
		const FVector2D BoxMax(FCString::Atof(*Args[2]), FCString::Atof(*Args[3]));
		const int32 Stamped = CostGrid->StampImpassable(BoxMin, BoxMax);
		UE_LOG(LogMassBattle, Display,
		       TEXT("EXEC_RESULT StampImpassable OK Box=(%.2f,%.2f)-(%.2f,%.2f)m Cells=%d"),
		       BoxMin.X, BoxMin.Y, BoxMax.X, BoxMax.Y, Stamped);
	}
	// -- Phase 1 Task 10: A2 command surface --
	void OrderMove(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
		if (!Registry || Args.Num() < 3)
		{
			UE_LOG(LogMassBattle, Display,
			       TEXT("EXEC_RESULT OrderMove FAIL BadArgs (need: UnitIndex anchorX_m anchorY_m [facingDeg] [speedMps])"));
			return;
		}
		const int32 UnitIndex = FCString::Atoi(*Args[0]);
		FUnitHandle Matched;
		for (const FUnitHandle& Handle : Registry->GetSortedHandles())
		{
			if (Handle.Index == static_cast<uint32>(UnitIndex))
			{
				Matched = Handle;
				break;
			}
		}
		if (!Matched.IsValid())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderMove FAIL NoSuchUnit Index=%d"), UnitIndex);
			return;
		}
		const FVector2D AnchorCm(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f);
		const float Facing = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
		const float Speed = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.f;
		const bool bOk = Registry->IssueMoveOrder(Matched, AnchorCm, Facing, Speed);
		UE_LOG(LogMassBattle, Display,
		       TEXT("EXEC_RESULT OrderMove %s Unit=%u:%u Anchor=(%.1f,%.1f)m Facing=%.1f Speed=%.2f"),
		       bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, AnchorCm.X * 0.01f, AnchorCm.Y * 0.01f, Facing, Speed);
	}

	void OrderStop(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
		if (!Registry || Args.Num() < 1)
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderStop FAIL BadArgs (need: UnitIndex)"));
			return;
		}
		const int32 UnitIndex = FCString::Atoi(*Args[0]);
		FUnitHandle Matched;
		for (const FUnitHandle& Handle : Registry->GetSortedHandles())
		{
			if (Handle.Index == static_cast<uint32>(UnitIndex))
			{
				Matched = Handle;
				break;
			}
		}
		if (!Matched.IsValid())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderStop FAIL NoSuchUnit Index=%d"), UnitIndex);
			return;
		}
		const bool bOk = Registry->IssueStopOrder(Matched);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderStop %s Unit=%u:%u"),
		       bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial);
	}
}

// ---- Command registration (Task 4 portion) ----

static FAutoConsoleCommandWithWorldAndArgs GBattleDumpRegistry(
	TEXT("Battle.DumpRegistry"),
	TEXT("Dumps registry units + rosters; emits EXEC_RESULT DumpRegistry lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::DumpRegistry));

static FAutoConsoleCommandWithWorldAndArgs GBattleStateHash(
	TEXT("Battle.StateHash"),
	TEXT("Emits EXEC_RESULT StateHash with the full sim-state CityHash64 (registry + entities + shared fragments)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::StateHash));

static FAutoConsoleCommandWithWorldAndArgs GBattleRngSelfTest(
	TEXT("Battle.RngSelfTest"),
	TEXT("[RootSeed] -- derives streams from two equal-root registries and asserts identical 1000-draw folds; EXEC_RESULT RngSelfTest PASS/FAIL"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::RngSelfTest));

static FAutoConsoleCommandWithWorldAndArgs GBattleSpawnRoster(
	TEXT("Battle.SpawnRoster"),
	TEXT("<EduRow> <Count> [Name] -- spawns a unit roster from the EDU fixture; EXEC_RESULT lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::SpawnRoster));

static FAutoConsoleCommandWithWorldAndArgs GBattleBuildT0Configs(
	TEXT("Battle.BuildT0Configs"),
	TEXT("Authors + saves the two T0 UMassEntityConfigAsset configs under /Game/MassBattle/Data/Units (editor)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::BuildT0Configs));

static FAutoConsoleCommandWithWorldAndArgs GBattleSetAttr(
	TEXT("Battle.SetAttr"),
	TEXT("<UnitIndex> <AttributeName> <Value> -- sets a GAS attribute on a spawned unit actor"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::SetAttr));

static FAutoConsoleCommandWithWorldAndArgs GBattleDumpSharedFrag(
	TEXT("Battle.DumpSharedFrag"),
	TEXT("Dumps per-unit FUnitStatsSharedFragment instances (address + key values)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::DumpSharedFrag));

static FAutoConsoleCommandWithWorldAndArgs GBattleDumpMetrics(
	TEXT("Battle.DumpMetrics"),
	TEXT("Dumps the B.4 metrics line (archetype count, per-frame UnitSync ms, unit count)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::DumpMetrics));

static FAutoConsoleCommandWithWorldAndArgs GBattleStampImpassable(
	TEXT("Battle.Test.StampImpassable"),
	TEXT("<minX> <minY> <maxX> <maxY> -- A5: stamps impassable cost-grid cells overlapping the box (meters, world space); EXEC_RESULT StampImpassable"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::StampImpassable));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderMove(
	TEXT("Battle.Order.Move"),
	TEXT("<UnitIndex> <anchorX_m> <anchorY_m> [facingDeg] [speedMps] -- A2: issue a move order; EXEC_RESULT OrderMove"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderMove));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderStop(
	TEXT("Battle.Order.Stop"),
	TEXT("<UnitIndex> -- A2: halt the unit; EXEC_RESULT OrderStop"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderStop));
