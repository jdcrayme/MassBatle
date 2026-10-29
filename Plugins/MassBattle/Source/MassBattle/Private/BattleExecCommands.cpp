// Battle.* console command implementations. Task 4 adds the registry probes;
// later tasks fill in their own functions and register their commands.
// Delegate signature note: FConsoleCommandWithWorldAndArgsDelegate is
// void(const TArray<FString>& Args, UWorld* World) -- args FIRST.
#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleDeathPath.h"
#include "BattleRangedFragments.h"
#include "BattleProjectileSubsystem.h"
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
	void EnableDemoCombatPipeline()
	{
		// The session-on gate list (D-3): SpatialHash, Clusters, Tally, Assign.
		// Plugin defaults stay 0 (flags-off identity); the demo session turns
		// the pipeline on WITHOUT touching defaults (ECVF_SetByCode).
		for (const TCHAR* Name : { TEXT("Battle.Combat.SpatialHash"), TEXT("Battle.Combat.Clusters"),
			TEXT("Battle.Combat.Tally"), TEXT("Battle.Combat.Assign") })
		{
			if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				C->Set(1, ECVF_SetByCode);
			}
		}
	}
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
			{ TEXT("PeasantArchers"), 2 },
			{ TEXT("MercenaryCrossbowmen"), 3 },
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
				else if (Spec.Row == 1) // Sergeant Spearmen
				{
					Trait->BaseHealth = 1.f; Trait->MeleeAttack = 4.f; Trait->MeleeDefense = 4.f;
					Trait->Armor = 4.f; Trait->MoraleBase = 6.f; Trait->ChargeDistance = 30.f;
				}
				else if (Spec.Row == 2) // Peasant Archers (spec ranged-stack section 3.1)
				{
					Trait->BaseHealth = 1.f; Trait->MeleeAttack = 4.f; Trait->MeleeDefense = 2.f;
					Trait->RangedAttack = 6.f; Trait->Armor = 0.f; Trait->MoraleBase = 4.f;
					Trait->ChargeDistance = 30.f;
					Trait->Kind = EBattleProjectileKind::Arrow; Trait->RangedRangeM = 120.f; Trait->AmmoPerMan = 25;
					Trait->bMelee = false; Trait->bBow = true;
				}
				else // Mercenary Crossbowmen
				{
					Trait->BaseHealth = 1.f; Trait->MeleeAttack = 5.f; Trait->MeleeDefense = 3.f;
					Trait->RangedAttack = 9.f; Trait->Armor = 3.f; Trait->MoraleBase = 5.f;
					Trait->ChargeDistance = 30.f;
					Trait->Kind = EBattleProjectileKind::Bolt; Trait->RangedRangeM = 160.f; Trait->AmmoPerMan = 18;
					Trait->bMelee = false; Trait->bBow = true;
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

// -- Phase 2 task 8: the M3 demo fixtures (geometry-only, A10). Spawns the
// representative clash shapes on the flat tile; the demo game mode registers
// the mob sample when nothing is level-placed.
namespace
{
	// One spawn request in a SpawnClash fixture: the EDU row is identified by
	// its DICTIONARY name (B-3), never by index -- re-ordering the golden EDU
	// must not silently swap knight for peasant in a scenario.
	struct FClashUnitSpec
	{
		const TCHAR* Dictionary; // EDU 'dictionary' value (unique per row)
		int32 Count;
		FVector2D PosM = FVector2D::ZeroVector;
		float YawDeg = 0.f;
	};

	int32 FindEduRowIndexByDictionary(const TArray<FEduUnit>& Rows, const FString& Dictionary)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FEduField* Dict = Rows[i].FindField(TEXT("dictionary"));
			if (Dict && Dict->GetValue().Equals(Dictionary, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	FUnitHandle SpawnClashUnitExec(UWorld* World, const TArray<FEduUnit>& Rows, const FClashUnitSpec& Unit)
	{
		const int32 EduRow = FindEduRowIndexByDictionary(Rows, Unit.Dictionary);
		if (EduRow == INDEX_NONE)
		{
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT SpawnClash FAIL unknown EDU dictionary '%s'"), Unit.Dictionary);
			return FUnitHandle();
		}
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(Rows[EduRow], Stats, Error))
		{
			return FUnitHandle();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = EduRow;
		Spec.SoldierCount = Unit.Count;
		Spec.UnitName = Stats.TypeName;
		Spec.SpawnPos = Unit.PosM * 100.f;
		Spec.SpawnYaw = Unit.YawDeg;
		Spec.NumRanks = 3;
		const FBattleSpawnResult Result = UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats);
		return Result.Unit;
	}
}

void CombatSpawnClash(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleCostGridSubsystem* CostGrid = World ? World->GetSubsystem<UBattleCostGridSubsystem>() : nullptr;
	if (!Registry || !CostGrid || Args.Num() < 1)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnClash FAIL BadArgs (need: wall|mob|charge)"));
		return;
	}
	CostGrid->ResetToFlat();
	// The demo fixture rides the combat machinery persistently (visual demo).
	EnableDemoCombatPipeline();

	// Fixture compositions (B-3): scenario DATA lives here as a table --
	// unit type by EDU dictionary name, count, spawn transform -- not as
	// scattered typed literals in call arguments. Adding a variant or
	// retuning a clash shape means editing THIS table only.
	const FString Variant = Args[0];
	TArray<FClashUnitSpec> WallUnits = {
		{TEXT("Peasant_Levy"), 40, FVector2D(0.f, 0.f), 180.f},
		{TEXT("Sergeant_Spearmen"), 40, FVector2D(-24.f, 0.f), 0.f},
	};
	TArray<FClashUnitSpec> MobUnits = {
		{TEXT("Sergeant_Spearmen"), 8, FVector2D(0.f, -3.f), 180.f},
		{TEXT("Sergeant_Spearmen"), 8, FVector2D(0.f, 3.f), 180.f},
		{TEXT("Peasant_Levy"), 40, FVector2D(-20.f, 0.f), 0.f},
	};
	TArray<FClashUnitSpec> ChargeUnits = {
		{TEXT("Peasant_Levy"), 12, FVector2D(0.f, 0.f), 180.f},
		{TEXT("Peasant_Levy"), 12, FVector2D(-24.f, 0.f), 0.f},
	};
	const TCHAR* VariantName = nullptr;
	TArray<FClashUnitSpec>* Units = nullptr;
	if (Variant == TEXT("wall"))
	{
		VariantName = TEXT("wall");
		Units = &WallUnits;
	}
	else if (Variant == TEXT("mob"))
	{
		VariantName = TEXT("mob");
		Units = &MobUnits;
	}
	else if (Variant == TEXT("charge"))
	{
		VariantName = TEXT("charge");
		Units = &ChargeUnits;
	}
	else
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnClash FAIL unknown variant %s"), *Variant);
		return;
	}

	// Load the EDU once per command; rows are matched by dictionary name.
	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath)
		|| !FBattleEduParser::Parse(EduText, Rows, Err))
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnClash FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		return;
	}

	TArray<FUnitHandle> Spawned;
	Spawned.Reserve(Units->Num());
	for (const FClashUnitSpec& Unit : *Units)
	{
		const FUnitHandle Handle = SpawnClashUnitExec(World, Rows, Unit);
		if (!Handle.IsValid())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnClash FAIL %s spawn"), VariantName);
			return;
		}
		Spawned.Add(Handle);
	}
	// Orders by convention: wall/charge = last unit attacks the first; mob =
	// the peasants (spawn order: knights first, peasants last) attack-move to
	// the origin.
	if (Variant == TEXT("mob"))
	{
		Registry->IssueAttackMoveOrder(Spawned.Last(), FVector2D(0.f, 0.f), 0.f);
	}
	else
	{
		Registry->IssueAttackUnitOrder(Spawned.Last(), Spawned[0]);
	}
	FString Ids;
	for (const FUnitHandle& H : Spawned)
	{
		Ids += FString::Printf(TEXT(" %u:%u"), H.Index, H.Serial);
	}
	const FString ChargeHint = Variant == TEXT("charge")
		? TEXT(" (command Battle.Movement.WalkSpeed 6 to reach the fresh-contact gate)")
		: TEXT("");
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnClash OK %s%s units:%s"),
		VariantName, *ChargeHint, *Ids);
}

void CombatPerfStress(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleCostGridSubsystem* CostGrid = World ? World->GetSubsystem<UBattleCostGridSubsystem>() : nullptr;
	if (!Registry || !CostGrid)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT PerfStress FAIL no world subsystems"));
		return;
	}
	CostGrid->ResetToFlat();
	EnableDemoCombatPipeline();
	// Two 500-man lines in contact (the B.6 1k stress shape): 25 files x 20
	// ranks at 1.2 m -- the tile holds it; the front ranks interpenetrate the
	// dilation and the cluster pass emits the engagement.
	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath)
		|| !FBattleEduParser::Parse(EduText, Rows, Err))
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT PerfStress FAIL EDU fixture"));
		return;
	}
	auto SpawnLine = [&](int32 Row, int32 Count, const FVector2D& PosM, float Yaw) -> FUnitHandle
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(Rows[Row], Stats, Error))
		{
			return FUnitHandle();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = Row;
		Spec.SoldierCount = Count;
		Spec.UnitName = Stats.TypeName;
		Spec.SpawnPos = PosM * 100.f;
		Spec.SpawnYaw = Yaw;
		Spec.NumRanks = 20; // deep blocks: 25 x 20 = 500 per side
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats).Unit;
	};
	const FUnitHandle A = SpawnLine(0, 500, FVector2D(-10.f, 0.f), 0.f);
	const FUnitHandle B = SpawnLine(0, 500, FVector2D(10.f, 0.f), 180.f);
	Registry->IssueAttackUnitOrder(A, B);
	if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
	{
		Debug->ArmPerfProbe(100); // the steady state, ~1.7 s after the clash
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT PerfStress OK A=%u:%u B=%u:%u (1000 entities in contact; the probe dumps BattleMetrics at tick 100)"),
		A.Index, A.Serial, B.Index, B.Serial);
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

// ---- Phase 3 task 1 (ranged data spine: spec §4.10 toggle/dump surface) ----

void RangedToggle(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || Args.Num() < 3)
	{
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT RangedToggle FAIL BadArgs (need: UnitIndex FireAtWill|Skirmish 0|1)"));
		return;
	}
	const FUnitHandle Matched = MatchUnitIndex(Registry, FCString::Atoi(*Args[0]));
	if (!Matched.IsValid())
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedToggle FAIL NoSuchUnit Index=%d"),
			FCString::Atoi(*Args[0]));
		return;
	}
	const FString& Toggle = Args[1];
	const bool bValue = FCString::Atoi(*Args[2]) != 0;
	bool bFireAtWill = false;
	bool bSkirmish = false;
	bool bOutOfAmmo = false;
	Registry->GetUnitFireToggles(Matched, bFireAtWill, bSkirmish, bOutOfAmmo);
	if (Toggle == TEXT("FireAtWill"))
	{
		bFireAtWill = bValue;
	}
	else if (Toggle == TEXT("Skirmish"))
	{
		bSkirmish = bValue;
	}
	else
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedToggle FAIL BadToggle %s (FireAtWill|Skirmish)"), *Toggle);
		return;
	}
	const bool bOk = Registry->SetUnitFireToggles(Matched, bFireAtWill, bSkirmish);
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedToggle %s Unit=%u:%u Toggle=%s Value=%d"),
		bOk ? TEXT("OK") : TEXT("FAIL"), Matched.Index, Matched.Serial, *Toggle, bValue ? 1 : 0);
}

void RangedDump(const TArray<FString>& Args, UWorld* World)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UBattleProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	if (!Registry || !EntitySubsystem)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT RangedDump FAIL no world/registry"));
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FBattleProjectilePool& Pool = Projectiles ? Projectiles->GetPool() : FBattleProjectilePool::EmptyPool();

	// Optional UnitIndex filter; absent dumps every live unit (the
	// spawn-stamp evidence pass: one archer spawn, one line).
	TArray<FUnitHandle> Handles = Registry->GetSortedHandles();
	if (Args.Num() > 0)
	{
		const FUnitHandle Matched = MatchUnitIndex(Registry, FCString::Atoi(*Args[0]));
		if (!Matched.IsValid())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedDump FAIL NoSuchUnit Index=%d"),
				FCString::Atoi(*Args[0]));
			return;
		}
		Handles = { Matched };
	}

	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedDump Begin Units=%d PoolLive=%d PoolCapacity=%d"),
		Handles.Num(), Pool.GetLiveCount(), Pool.GetCapacity());
	for (const FUnitHandle& Handle : Handles)
	{
		bool bFireAtWill = false;
		bool bSkirmish = false;
		bool bOutOfAmmo = false;
		Registry->GetUnitFireToggles(Handle, bFireAtWill, bSkirmish, bOutOfAmmo);
		const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
		const float Fraction = Registry->GetUnitAmmoFraction(Handle);

		// Per-entity ammo histogram (ascending ammo x count) -- dead
		// entities left the roster; unresolvable ones (registry-only
		// restore) do not contribute and Ranged=0 marks the state.
		TMap<int32, int32> Histogram;
		int32 RangedCount = 0;
		if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle))
		{
			for (const FMassEntityHandle& Entity : *Roster)
			{
				if (const FRangedStateFragment* Ranged =
					FMassEntityView(EntityManager, Entity).GetFragmentDataPtr<FRangedStateFragment>())
				{
					++RangedCount;
					++Histogram.FindOrAdd(static_cast<int32>(Ranged->AmmoCount));
				}
			}
		}
		TStringBuilder<256> HistBuilder;
		Histogram.KeySort(TLess<int32>());
		bool bFirst = true;
		for (const TPair<int32, int32>& Bucket : Histogram)
		{
			if (!bFirst)
			{
				HistBuilder.Append(TEXT("|"));
			}
			HistBuilder.Appendf(TEXT("%dx%d"), Bucket.Key, Bucket.Value);
			bFirst = false;
		}
		const FString HistStr = bFirst ? FString(TEXT("-")) : HistBuilder.ToString();
		// Pool attribution (spec §4.10): live projectiles this unit owns.
		int32 PoolOwned = 0;
		for (int32 Slot = 0; Slot < Pool.GetCapacity(); ++Slot)
		{
			if (Pool.IsLive(Slot) && Pool.OwnerUnit[Slot] == Handle)
			{
				++PoolOwned;
			}
		}

		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT RangedDump Unit=%u:%u Ranged=%d FireAtWill=%d Skirmish=%d OutOfAmmo=%d Fraction=%.3f Roster=%d InitialAmmo=%d InitialRoster=%d AmmoHist=%s PoolOwned=%d"),
			Handle.Index, Handle.Serial,
			RangedCount > 0 ? 1 : 0,
			bFireAtWill ? 1 : 0,
			bSkirmish ? 1 : 0,
			bOutOfAmmo ? 1 : 0,
			Fraction,
			State ? State->Roster.Num() : -1,
			State ? State->RangedInitialAmmo : 0,
			State ? State->InitialRosterSize : 0,
			*HistStr,
			PoolOwned);
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedDump End"));
}

void RangedSpawnVolley(const TArray<FString>& Args, UWorld* World)
{
	// Args: [line|skirmish] -- the M4 gray-box sample (task 6 extends the
	// skirmish variant). Spawns archers vs a formed line at volley range on
	// OPPOSING teams, issues the attack, and flips the session flags (the
	// combat pipeline + the ranged block; ECVF_SetByCode, plugin defaults
	// untouched). Evidence then flows from the live sim: RangedFire /
	// SkirmishRetreat / OutOfAmmo EXEC lines + Battle.Ranged.Dump.
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || !World)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnVolley FAIL no world/registry"));
		return;
	}

	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath) || !FBattleEduParser::Parse(EduText, Rows, Err) || Rows.Num() < 3)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnVolley FAIL bad EDU fixture (%s)"), *Err.Message);
		return;
	}

	Registry->ResetBattleState();
	BattleExec::EnableDemoCombatPipeline();
	for (const TCHAR* Name : { TEXT("Battle.Ranged.Enable"), TEXT("Battle.Ranged.Fire"),
		TEXT("Battle.Ranged.Step"), TEXT("Battle.Ranged.Skirmish") })
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			C->Set(1, ECVF_SetByCode);
		}
	}

	auto SpawnAt = [&Rows, &World](int32 RowIdx, const FVector2D& PosM, float YawDeg, uint8 TeamId) -> FUnitHandle
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(Rows[RowIdx], Stats, Error))
		{
			return FUnitHandle();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = RowIdx;
		Spec.SoldierCount = FMath::Max(1, Stats.SoldierCount);
		Spec.UnitName = Stats.TypeName;
		Spec.SpawnPos = PosM * 100.f;
		Spec.SpawnYaw = YawDeg;
		Spec.NumRanks = 2;
		Spec.TeamId = TeamId;
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats).Unit;
	};

	// Archers west; the `line` variant puts the target 80 m east standing
	// (attack orders both ways); the `skirmish` variant puts a threat line
	// 30 m east APPROACHING the archers (Move order -- the skirmish machine
	// engages live: break / fire-while-retreating / resume). Evidence flows
	// from the live sim: RangedFire / SkirmishRetreat / OutOfAmmo lines.
	const bool bSkirmish = Args.Num() > 0 && Args[0] == TEXT("skirmish");
	const FUnitHandle Archers = SpawnAt(2, FVector2D(-40.f, 0.f), 0.f, 0);
	const FUnitHandle Target = SpawnAt(0, FVector2D(bSkirmish ? -10.f : 40.f, 0.f), 180.f, 1);
	if (!Archers.IsValid() || !Target.IsValid())
	{
		return;
	}
	if (bSkirmish)
	{
		// The threat approaches; the archers shoot it (attack order) and the
		// skirmish machine runs live: break inside 25 m, fire-while-
		// retreating (the retained aim), resume past the hysteresis band.
		Registry->IssueAttackUnitOrder(Archers, Target);
		Registry->IssueMoveOrder(Target, FVector2D(-3500.f, 0.f), 180.f);
	}
	else
	{
		Registry->IssueAttackUnitOrder(Archers, Target);
	}
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT SpawnVolley OK Mode=%s Archers=%u:%u Target=%u:%u (flags on; watch RangedFire/RangedDump/OutOfAmmo/SkirmishRetreat lines)"),
		bSkirmish ? TEXT("skirmish") : TEXT("line"), Archers.Index, Archers.Serial, Target.Index, Target.Serial);
}

void RangedPerfStress(const TArray<FString>& Args, UWorld* World)
{
	// T3.6 (the T2.9 pattern): two 500-man archer units at 100 m, mutual
	// attack orders; the VolleyRanks cvar rides to 20 for the session (the
	// A6 default 2 would limit 500-man blocks to 2 ranks = 50 shooters) and
	// the ranged perf probe dumps the verdict ~12 s in (steady volley:
	// ~1,000+ in flight). Evidence: the EXEC_RESULT RangedPerf line.
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || !World)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT RangedPerfStress FAIL no world/registry"));
		return;
	}
	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath) || !FBattleEduParser::Parse(EduText, Rows, Err) || Rows.Num() < 3)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT RangedPerfStress FAIL bad EDU fixture (%s)"), *Err.Message);
		return;
	}
	Registry->ResetBattleState();
	BattleExec::EnableDemoCombatPipeline();
	for (const TCHAR* Name : { TEXT("Battle.Ranged.Enable"), TEXT("Battle.Ranged.Fire"),
		TEXT("Battle.Ranged.Step"), TEXT("Battle.Ranged.Skirmish") })
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			C->Set(1, ECVF_SetByCode);
		}
	}
	// The fixture-session rank bump (disclosed): 20 ranks of shooters.
	if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.VolleyRanks")))
	{
		C->Set(20, ECVF_SetByCode);
	}
	if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.SkirmishBreakDistM")))
	{
		// The archers stand 100 m apart; keep the skirmish machine out of
		// the perf window (no retreat marches mid-measurement).
		C->Set(1.f, ECVF_SetByCode);
	}

	auto SpawnLine = [&](int32 RowIdx, int32 Count, const FVector2D& PosM, float YawDeg, uint8 TeamId) -> FUnitHandle
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(Rows[RowIdx], Stats, Error))
		{
			return FUnitHandle();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = RowIdx;
		Spec.SoldierCount = Count;
		Spec.UnitName = Stats.TypeName;
		Spec.SpawnPos = PosM * 100.f;
		Spec.SpawnYaw = YawDeg;
		Spec.NumRanks = 20; // deep blocks: 25 x 20 = 500 per side
		Spec.TeamId = TeamId;
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats).Unit;
	};
	const FUnitHandle A = SpawnLine(2, 500, FVector2D(-50.f, 0.f), 0.f, 0);
	const FUnitHandle B = SpawnLine(2, 500, FVector2D(50.f, 0.f), 180.f, 1);
	if (!A.IsValid() || !B.IsValid())
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT RangedPerfStress FAIL spawn"));
		return;
	}
	Registry->IssueAttackUnitOrder(A, B);
	Registry->IssueAttackUnitOrder(B, A);
	if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
	{
		Debug->ResetDiagnostics();
		Debug->ArmRangedPerfProbe(700); // ~11.7 s: the steady volley
	}
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT RangedPerfStress OK A=%u:%u B=%u:%u (1000 archers at 100 m; the probe dumps RangedPerf at tick 700)"),
		A.Index, A.Serial, B.Index, B.Serial);
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


static FAutoConsoleCommandWithWorldAndArgs GBattleCombatPerfStress(
	TEXT("Battle.Combat.PerfStress"),
	TEXT("Spawns the T2.9 1k-in-contact stress fixture (two 500-man lines) and orders the clash; read TallyMs/AssignMs from BattleMetrics"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::CombatPerfStress));

static FAutoConsoleCommandWithWorldAndArgs GBattleCombatSpawnClash(
	TEXT("Battle.Combat.SpawnClash"),
	TEXT("<wall|mob|charge> -- task 8 M3 demo fixture (geometry-only, A10); spawns the clash shape and issues the orders; EXEC_RESULT SpawnClash"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::CombatSpawnClash));

static FAutoConsoleCommandWithWorldAndArgs GBattleRangedToggle(
	TEXT("Battle.Ranged.Toggle"),
	TEXT("<UnitIndex> <FireAtWill|Skirmish> <0|1> -- spec ranged-stack 4.10 toggle surface; EXEC_RESULT RangedToggle"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::RangedToggle));

static FAutoConsoleCommandWithWorldAndArgs GBattleRangedDump(
	TEXT("Battle.Ranged.Dump"),
	TEXT("[UnitIndex] -- toggles, ammo fraction + per-entity histogram, pool attribution; EXEC_RESULT RangedDump lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::RangedDump));

static FAutoConsoleCommandWithWorldAndArgs GBattleRangedPerfStress(
	TEXT("Battle.Ranged.PerfStress"),
	TEXT("T3.6: two 500-man archer units at 100 m, mutual attacks, VolleyRanks 20; the probe dumps EXEC_RESULT RangedPerf at tick 700"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::RangedPerfStress));

static FAutoConsoleCommandWithWorldAndArgs GBattleRangedSpawnVolley(
	TEXT("Battle.Ranged.SpawnVolley"),
	TEXT("[line|skirmish] -- task 4 M4 gray-box fixture: archers vs a formed line at volley range, flags on; EXEC_RESULT SpawnVolley"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::RangedSpawnVolley));
