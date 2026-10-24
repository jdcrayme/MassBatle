import io

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleExecCommands.cpp"
s = io.open(p, encoding="utf-8").read()

old_stubs = """	// -- Implemented by their owning tasks; stubs log a TODO until then. --

	void SpawnRoster(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT SpawnRoster FAIL not-implemented (Task 6)"));
	}

	void BuildT0Configs(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT BuildT0Configs FAIL not-implemented (Task 6)"));
	}

	void SetAttr(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT SetAttr FAIL not-implemented (Task 8)"));
	}

	void KillEntity(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT KillEntity FAIL not-implemented (Task 9)"));
	}

	void DumpSharedFrag(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT DumpSharedFrag FAIL not-implemented (Task 6)"));
	}
}"""

new_impls = """	// -- Task 6: spawn path --
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

		const FString FixturePath = FPaths::ProjectDir() / TEXT("Data/EDU/golden_edu_v1.txt");
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

			Asset->GetMutableConfig().ConfigName = FName(Spec.Name);

			// Trait/BuildTemplate route (A5): one composition trait per config.
			if (!Asset->FindTrait(UBattleUnitTrait::StaticClass()))
			{
				Asset->AddTrait(UBattleUnitTrait::StaticClass());
			}
			if (UBattleUnitTrait* Trait = Cast<UBattleUnitTrait>(Asset->FindTrait(UBattleUnitTrait::StaticClass())))
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

	void KillEntity(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT KillEntity FAIL not-implemented (Task 9)"));
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
			const FMassEntityView View(EM, (*Roster)[0]);
			if (const FUnitStatsSharedFragment* Stats = View.GetFragmentDataPtr<FUnitStatsSharedFragment>())
			{
				UE_LOG(LogMassBattle, Display,
				       TEXT("EXEC_RESULT DumpSharedFrag Unit=%u:%u Mem=%p Speed=%.3f MeleeAtk=%.3f MoraleBonus=%.3f"),
				       Handle.Index, Handle.Serial, (const void*)Stats, Stats->SpeedMult, Stats->MeleeAttackMult, Stats->MoraleBonus);
			}
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpSharedFrag End"));
	}
}"""

assert old_stubs in s, "stubs block not found"
s = s.replace(old_stubs, new_impls)

old_inc = """#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "Engine/World.h\""""
new_inc = """#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "BattleUnitTrait.h"
#include "MassBattleModule.h"
#include "UnitActor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassSpawner/MassEntityConfigAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Packages/PackageName.h"
#include "UObject/Package.h\""""
assert old_inc in s, "includes block not found"
s = s.replace(old_inc, new_inc)

old_reg = """static FAutoConsoleCommandWithWorldAndArgs GBattleStateHash(
	TEXT("Battle.StateHash"),
	TEXT("Emits EXEC_RESULT StateHash with the registry bookkeeping hash"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::StateHash));"""
new_reg = old_reg + """

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
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::DumpSharedFrag));"""
assert old_reg in s, "registration block not found"
s = s.replace(old_reg, new_reg)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("exec commands expanded OK")
