// Battle.* console command implementations. Task 4 adds the registry probes;
// later tasks fill in their own functions and register their commands.
// Delegate signature note: FConsoleCommandWithWorldAndArgsDelegate is
// void(const TArray<FString>& Args, UWorld* World) -- args FIRST.
#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleDeathPath.h"
#include "BattleRngRegistry.h"
#include "BattleCostGridSubsystem.h"
#include "BattleSave.h"
#include "BattleSpawnLibrary.h"
#include "BattleStateHash.h"
#include "BattleUnitTrait.h"
#include "BattleGarrison.h"
#include "BattleGarrisonSubsystem.h"
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
			       TEXT("EXEC_RESULT OrderMove FAIL BadArgs (need: UnitIndex anchorX_m anchorY_m [facingDeg] [speedMps] [files])"));
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
		const int32 Files = Args.Num() > 5 ? FCString::Atoi(*Args[5]) : 0;
		const bool bOk = Registry->IssueMoveOrder(Matched, AnchorCm, Facing, Speed, Files);
		UE_LOG(LogMassBattle, Display,
		       TEXT("EXEC_RESULT OrderMove %s Unit=%u:%u Anchor=(%.1f,%.1f)m Facing=%.1f Speed=%.2f Files=%d"),
		       bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, AnchorCm.X * 0.01f, AnchorCm.Y * 0.01f, Facing, Speed, Files);
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

	// -- Phase garrison task 1/2/4: garrison probes, gray-box fixtures, orders --
	void GarrisonDumpStructures(const TArray<FString>& Args, UWorld* World)
	{
		UBattleGarrisonSubsystem* Garrison = World ? World->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
		if (!Garrison)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT Garrison.DumpStructures FAIL no subsystem"));
			return;
		}
		Garrison->DumpStructures();
	}

	bool SpawnGarrisonFixtureImpl(EGarrisonStructureType Type, const TCHAR* CommandName,
		const TArray<FString>& Args, UWorld* World)
	{
		UBattleGarrisonSubsystem* Garrison = World ? World->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
		if (!Garrison || Args.Num() < 2)
		{
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT %s FAIL BadArgs (need: x_m y_m [rotDeg])"), CommandName);
			return false;
		}
		const FVector2D PosM(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
		const float YawDeg = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		FGarrisonStructureRecord Record;
		if (!FGarrisonLayout::MakeFixture(Type, PosM, YawDeg, Record))
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT %s FAIL BadType"), CommandName);
			return false;
		}
		const uint32 Id = Garrison->RegisterStructure(Record);
		if (Id == 0)
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT %s FAIL RegisterRejected"), CommandName);
			return false;
		}
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT %s OK Structure=%u Pos=(%.1f,%.1f)m Rot=%.0f MaxUnits=%d Slots=%d"),
			CommandName, Id, PosM.X, PosM.Y, YawDeg, Record.MaxUnits, Record.Slots.Num());
		return true;
	}

	void GarrisonSpawnBuilding(const TArray<FString>& Args, UWorld* World)
	{
		SpawnGarrisonFixtureImpl(EGarrisonStructureType::Building, TEXT("Garrison.SpawnBuilding"), Args, World);
	}

	void GarrisonSpawnTrench(const TArray<FString>& Args, UWorld* World)
	{
		SpawnGarrisonFixtureImpl(EGarrisonStructureType::Trench, TEXT("Garrison.SpawnTrench"), Args, World);
	}

	void GarrisonSpawnWall(const TArray<FString>& Args, UWorld* World)
	{
		SpawnGarrisonFixtureImpl(EGarrisonStructureType::Wall, TEXT("Garrison.SpawnWall"), Args, World);
	}

	void GarrisonMetrics(const TArray<FString>& Args, UWorld* World)
	{
		UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
		UBattleGarrisonSubsystem* Garrison = World ? World->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
		if (!Debug || !Garrison)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT Garrison.Metrics FAIL no subsystems"));
			return;
		}
		Garrison->DumpStructures(); // per-structure occupancy evidence
		int32 Booked = 0;
		int32 Waiting = 0;
		for (const TPair<uint32, FGarrisonStructureRecord>& It : Garrison->GetAllStructures())
		{
			Booked += Garrison->GetBookedCount(It.Key);
			Waiting += Garrison->GetQueueWaitCount(It.Key);
		}
		Debug->DumpGarrisonMetrics(Booked, Waiting);
	}

	void OrderGarrison(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
		if (!Registry || Args.Num() < 2)
		{
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT OrderGarrison FAIL BadArgs (need: UnitIndex StructureId)"));
			return;
		}
		const int32 UnitIndex = FCString::Atoi(*Args[0]);
		const uint32 StructureId = static_cast<uint32>(FCString::Atoi(*Args[1]));
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
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderGarrison FAIL NoSuchUnit Index=%d"), UnitIndex);
			return;
		}
		const bool bOk = Registry->IssueGarrisonOrder(Matched, StructureId);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderGarrison %s Unit=%u:%u Structure=%u"),
			bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, StructureId);
	}

	void OrderExitGarrison(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
		if (!Registry || Args.Num() < 1)
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderExitGarrison FAIL BadArgs (need: UnitIndex)"));
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
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderExitGarrison FAIL NoSuchUnit Index=%d"), UnitIndex);
			return;
		}
		const bool bOk = Registry->IssueExitGarrisonOrder(Matched);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderExitGarrison %s Unit=%u:%u"),
			bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial);
	}

// ---- Phase 2 task 2 (combat data spine probes) ----

namespace
{
	/** Resolves a unit by slot index (OrderMove pattern). */
	FUnitHandle MatchUnitIndex(UUnitRegistrySubsystem* Registry, int32 UnitIndex)
	{
		FUnitHandle Matched;
		for (const FUnitHandle& Handle : Registry->GetSortedHandles())
		{
			if (Handle.Index == static_cast<uint32>(UnitIndex))
			{
				Matched = Handle;
				break;
			}
		}
		return Matched;
	}
}

void OrderAttackUnit(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || Args.Num() < 2)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderAttackUnit FAIL BadArgs (need: UnitIndex TargetIndex)"));
		return;
	}
	const FUnitHandle Matched = MatchUnitIndex(Registry, FCString::Atoi(*Args[0]));
	const FUnitHandle Target = MatchUnitIndex(Registry, FCString::Atoi(*Args[1]));
	if (!Matched.IsValid() || !Target.IsValid())
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderAttackUnit FAIL NoSuchUnit Unit=%d Target=%d"),
			FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
		return;
	}
	const bool bOk = Registry->IssueAttackUnitOrder(Matched, Target);
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderAttackUnit %s Unit=%u:%u Target=%u:%u"),
		bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, Target.Index, Target.Serial);
}

void OrderAttackMove(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || Args.Num() < 3)
	{
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT OrderAttackMove FAIL BadArgs (need: UnitIndex anchorX_m anchorY_m [facingDeg] [speedMps])"));
		return;
	}
	const FUnitHandle Matched = MatchUnitIndex(Registry, FCString::Atoi(*Args[0]));
	if (!Matched.IsValid())
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderAttackMove FAIL NoSuchUnit Index=%d"), FCString::Atoi(*Args[0]));
		return;
	}
	const FVector2D AnchorCm(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f);
	const float Facing = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
	const float Speed = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.f;
	const bool bOk = Registry->IssueAttackMoveOrder(Matched, AnchorCm, Facing, Speed);
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT OrderAttackMove %s Unit=%u:%u Anchor=(%.1f,%.1f)m Facing=%.1f Speed=%.2f"),
		bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, AnchorCm.X * 0.01f, AnchorCm.Y * 0.01f, Facing, Speed);
}

void CombatDumpPairs(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !EntitySubsystem)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT DumpPairs FAIL no world/registry"));
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	int32 Paired = 0;
	int32 Intents = 0;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpPairs Begin Units=%d Engagements=%d"),
		Registry->GetUnitCount(), Registry->GetEngagementCount());
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster)
		{
			continue;
		}
		for (const FMassEntityHandle& Entity : *Roster)
		{
			const FMassEntityView View(EntityManager, Entity);
			const FCombatPairFragment* Pair = View.GetFragmentDataPtr<FCombatPairFragment>();
			const FCombatIntentFragment* Intent = View.GetFragmentDataPtr<FCombatIntentFragment>();
			if (Intent && Intent->TargetUnit.IsValid())
			{
				++Intents;
			}
			if (!Pair)
			{
				continue;
			}
			if (Pair->Target.IsValid())
			{
				++Paired;
			}
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT DumpPairs Entity=%u:%u Unit=%u:%u Target=%u:%u State=%d Angle=%d Timer=%.2f Age=%.2f Charge=%.2f Intent=%u:%u Lane=%d"),
				Entity.Index, Entity.SerialNumber, Handle.Index, Handle.Serial,
				Pair->Target.Index, Pair->Target.SerialNumber,
				static_cast<int32>(Pair->State), static_cast<int32>(Pair->ContactAngle),
				Pair->AttackTimer, Pair->PairAge, Pair->ChargeBonusRemaining,
				Intent ? Intent->TargetUnit.Index : 0, Intent ? Intent->TargetUnit.Serial : 0,
				Intent ? static_cast<int32>(Intent->LaneIndex) : -1);
		}
	}
	for (const FEngagementKey& Key : Registry->GetSortedEngagementKeys())
	{
		const FEngagementRecord* Record = Registry->FindEngagement(Key.Attacker, Key.Defender);
		if (!Record)
		{
			continue;
		}
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT DumpPairs Engagement A=%u:%u D=%u:%u Mode=%d Frontage=[%.1f,%.1f] Pressure=%d Assigned=%d Dead=%d"),
			Key.Attacker.Index, Key.Attacker.Serial, Key.Defender.Index, Key.Defender.Serial,
			static_cast<int32>(Record->Mode), Record->FrontageStart, Record->FrontageEnd,
			Record->DesiredPressure, Record->AssignedAttackers, static_cast<int32>(Record->bDead));
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpPairs End Paired=%d Intents=%d"), Paired, Intents);
}

void CombatSaveProbe(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SaveProbe FAIL no registry"));
		return;
	}

	FBattleSaveData Save;
	Registry->CaptureBattleState(Save);
	const uint32 HashBefore = Registry->ComputeBookkeepingHash();

	// Clear: unregister every unit, dead-flag + prune the records.
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		Registry->UnregisterUnit(Handle);
	}
	for (const FEngagementKey& Key : Registry->GetSortedEngagementKeys())
	{
		Registry->FlagUnitEngagementsDead(Key.Attacker);
	}
	Registry->PruneDeadEngagements();
	const uint32 HashCleared = Registry->ComputeBookkeepingHash();

	// Restore + compare.
	const bool bRestored = Registry->RestoreBattleState(Save);
	const uint32 HashAfter = Registry->ComputeBookkeepingHash();
	const bool bRoundTrip = bRestored && HashBefore == HashAfter && HashCleared != HashBefore;
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT SaveProbe %s Hash=%08x Cleared=%08x After=%08x Units=%d Engagements=%d Schema=%u"),
		bRoundTrip ? TEXT("PASS") : TEXT("FAIL"),
		HashBefore, HashCleared, HashAfter,
		Registry->GetUnitCount(), Registry->GetEngagementCount(), Save.SchemaVersion);
	if (!bRoundTrip)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SaveProbe FAIL round-trip mismatch"));
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
	TEXT("<UnitIndex> <anchorX_m> <anchorY_m> [facingDeg] [speedMps] [files] -- A2: issue a move order (files > 0 re-fronts); EXEC_RESULT OrderMove"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderMove));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderStop(
	TEXT("Battle.Order.Stop"),
	TEXT("<UnitIndex> -- A2: halt the unit; EXEC_RESULT OrderStop"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderStop));

static FAutoConsoleCommandWithWorldAndArgs GBattleGarrisonDumpStructures(
	TEXT("Battle.Garrison.DumpStructures"),
	TEXT("Dumps garrison structure records + booking state; EXEC_RESULT Garrison.Structure / Garrison.Booking lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::GarrisonDumpStructures));

static FAutoConsoleCommandWithWorldAndArgs GBattleGarrisonMetrics(
	TEXT("Battle.Garrison.Metrics"),
	TEXT("Dumps garrison B.4 metrics (filings, filing-time avg, queue-wait avg, occupancy); EXEC_RESULT GarrisonMetrics line"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::GarrisonMetrics));

static FAutoConsoleCommandWithWorldAndArgs GBattleGarrisonSpawnBuilding(
	TEXT("Battle.Garrison.SpawnBuilding"),
	TEXT("<x_m> <y_m> [rotDeg] -- task 2 gray-box building fixture (24x20 m, 2 units, 80 posts); EXEC_RESULT Garrison.SpawnBuilding"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::GarrisonSpawnBuilding));

static FAutoConsoleCommandWithWorldAndArgs GBattleGarrisonSpawnTrench(
	TEXT("Battle.Garrison.SpawnTrench"),
	TEXT("<x_m> <y_m> [rotDeg] -- task 2 gray-box trench fixture (64 m channel, Z -0.75, 48 posts); EXEC_RESULT Garrison.SpawnTrench"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::GarrisonSpawnTrench));

static FAutoConsoleCommandWithWorldAndArgs GBattleGarrisonSpawnWall(
	TEXT("Battle.Garrison.SpawnWall"),
	TEXT("<x_m> <y_m> [rotDeg] -- task 2 gray-box wall fixture (64 m, walkway Z 6, 48 posts); EXEC_RESULT Garrison.SpawnWall"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::GarrisonSpawnWall));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderGarrison(
	TEXT("Battle.Order.Garrison"),
	TEXT("<UnitIndex> <StructureId> -- task 4 garrison order; EXEC_RESULT OrderGarrison"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderGarrison));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderExitGarrison(
	TEXT("Battle.Order.ExitGarrison"),
	TEXT("<UnitIndex> -- task 4 egress order; EXEC_RESULT OrderExitGarrison"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderExitGarrison));

}
// ---- Phase 2 task 2 registration (combat data spine probes) ----

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderAttackUnit(
	TEXT("Battle.Order.AttackUnit"),
	TEXT("<UnitIndex> <TargetIndex> -- task 2 attack order (A9 garrison guard rejects with EXEC_RESULT line); EXEC_RESULT OrderAttackUnit"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderAttackUnit));

static FAutoConsoleCommandWithWorldAndArgs GBattleOrderAttackMove(
	TEXT("Battle.Order.AttackMove"),
	TEXT("<UnitIndex> <anchorX_m> <anchorY_m> [facingDeg] [speedMps] -- task 2 attack-move order (auto-engage en route); EXEC_RESULT OrderAttackMove"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::OrderAttackMove));

static FAutoConsoleCommandWithWorldAndArgs GBattleCombatDumpPairs(
	TEXT("Battle.Combat.DumpPairs"),
	TEXT("Dumps per-entity pair/intent fragments + engagement records; EXEC_RESULT DumpPairs lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::CombatDumpPairs));

static FAutoConsoleCommandWithWorldAndArgs GBattleCombatSaveProbe(
	TEXT("Battle.Combat.SaveProbe"),
	TEXT("Capture -> clear -> restore round-trip at registry level; EXEC_RESULT SaveProbe PASS/FAIL"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::CombatSaveProbe));
