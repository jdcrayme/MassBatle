#if WITH_DEV_AUTOMATION_TESTS
// Battle.Data.EntityConfig* -- T0.3: entity-config integrity (configs resolve
// fragments/tags, archetype buildable) + roster specs instantiated from the
// parsed table. Self-heals by running the Battle.BuildT0Configs authoring
// (A5 ExecBridge route) when the assets are missing.
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "BattleEduParser.h"
#include "BattleExecCommands.h"
#include "BattleFragments.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "MassEntityConfigAsset.h"
#include "MassSpawnerSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "BattleUnitTrait.h"

namespace
{
	UWorld* MakeConfigTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	bool LoadFixtureRows(TArray<FEduUnit>& OutRows, FString& OutError)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = TEXT("fixture unreadable");
			return false;
		}
		FEduParseError Err;
		if (!FBattleEduParser::Parse(Text, OutRows, Err))
		{
			OutError = Err.Message;
			return false;
		}
		return true;
	}

	UMassEntityConfigAsset* LoadOrBuildConfig(const TCHAR* AssetName)
	{
		const FString Path = FString::Printf(TEXT("/Game/MassBattle/Data/Units/M_Battle_%s"), AssetName);
		if (UMassEntityConfigAsset* Existing = LoadObject<UMassEntityConfigAsset>(nullptr, *Path))
		{
			return Existing;
		}
		// A5 route: the ExecBridge command authors + saves the configs.
		BattleExec::BuildT0Configs(TArray<FString>(), nullptr);
		return LoadObject<UMassEntityConfigAsset>(nullptr, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityConfig_Integrity,
	"Battle.Data.EntityConfigIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEntityConfig_Integrity::RunTest(const FString& Parameters)
{
	UWorld* World = MakeConfigTestWorld();
	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("MassEntitySubsystem"), EntitySubsystem);
	if (!EntitySubsystem)
	{
		World->DestroyWorld(false);
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// -- configs exist, resolve, and are archetype-buildable --
	for (const TCHAR* Name : {TEXT("PeasantLevy"), TEXT("SergeantSpearmen")})
	{
		UMassEntityConfigAsset* Config = LoadOrBuildConfig(Name);
		if (!TestNotNull(FString::Printf(TEXT("config %s resolves"), Name), Config))
		{
			continue;
		}
		if (!TestTrue(FString::Printf(TEXT("config %s carries the composition trait"), Name),
			Config->FindTrait(UBattleUnitTrait::StaticClass()) != nullptr))
		{
			continue;
		}

		const FMassEntityTemplate& Template = Config->GetOrCreateEntityTemplate(*World);
		if (!TestTrue(FString::Printf(TEXT("config %s template valid"), Name), Template.IsValid()))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("config %s archetype built by template"), Name),
			Template.GetArchetype().IsValid());

		// Fragments resolved: the immutable stats made it into initial values.
		bool bFoundStats = false;
		for (const FInstancedStruct& Initial : Template.GetInitialFragmentValues())
		{
			if (Initial.GetScriptStruct() == FBaseStatsFragment::StaticStruct())
			{
				const FBaseStatsFragment* Stats = Initial.GetPtr<FBaseStatsFragment>();
				bFoundStats = (Stats && Stats->BaseHealth > 0.f);
			}
		}
		TestTrue(FString::Printf(TEXT("config %s initial stats resolved"), Name), bFoundStats);

		// Tags resolved: spawn 3 entities from the config (Route B, E4) and
		// check the capability tags per entity.
		UMassSpawnerSubsystem* Spawner = World->GetSubsystem<UMassSpawnerSubsystem>();
		if (TestNotNull(FString::Printf(TEXT("config %s spawner"), Name), Spawner))
		{
			TArray<FMassEntityHandle> ConfigEntities;
			Spawner->SpawnEntities(Template.GetTemplateID(), 3, FConstStructView(), nullptr, ConfigEntities);
			TestEqual(FString::Printf(TEXT("config %s spawned 3"), Name), ConfigEntities.Num(), 3);
			int32 TagErrors = 0;
			for (const FMassEntityHandle& E : ConfigEntities)
			{
				const FMassEntityView View(EntityManager, E);
				if (!View.HasTag<FInfantryTag>() || !View.HasTag<FMeleeTag>())
				{
					++TagErrors;
				}
			}
			TestEqual(FString::Printf(TEXT("config %s entities carry capability tags"), Name), TagErrors, 0);
		}
	}

	// -- roster specs instantiate from the parsed table (2 units x 40) --
	TArray<FEduUnit> Rows;
	FString Error;
	if (!TestTrue(TEXT("fixture parsed"), LoadFixtureRows(Rows, Error)))
	{
		AddError(Error);
		World->DestroyWorld(false);
		return false;
	}

	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TArray<FUnitHandle> Units;
	for (int32 RowIdx = 0; RowIdx < Rows.Num(); ++RowIdx)
	{
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = RowIdx; // SoldierCount 0 -> the row's own count (40)
		Spec.UnitName = FString::Printf(TEXT("T0_%d"), RowIdx);

		FBattleSpawnResult Result;
		if (!TestTrue(FString::Printf(TEXT("row %d spawns"), RowIdx),
			UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[RowIdx], Result, Error)))
		{
			AddError(Error);
			continue;
		}
		Units.Add(Result.Unit);

		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Result.Unit);
		if (TestNotNull(FString::Printf(TEXT("row %d roster published"), RowIdx), Roster))
		{
			TestEqual(FString::Printf(TEXT("row %d roster size == EDU soldier count"), RowIdx),
				Roster->Num(), 40);
		}
	}

	// T0.8 spawn-level: the two units' first entities carry DIFFERENT shared
	// fragment instances (per-unit uniqueness at the spawn path, E1).
	if (Units.Num() == 2)
	{
		const TArray<FMassEntityHandle>* RosterA = Registry->FindRoster(Units[0]);
		const TArray<FMassEntityHandle>* RosterB = Registry->FindRoster(Units[1]);
		if (RosterA && RosterB && RosterA->Num() > 0 && RosterB->Num() > 0)
		{
			const FUnitStatsSharedFragment* StatsA = EntityManager.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterA)[0]);
			const FUnitStatsSharedFragment* StatsB = EntityManager.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterB)[0]);
			if (TestNotNull(TEXT("unit A shared fragment"), StatsA) && TestNotNull(TEXT("unit B shared fragment"), StatsB))
			{
				TestNotEqual(TEXT("identical-stat units hold DISTINCT shared instances"), (const void*)StatsA, (const void*)StatsB);
				TestEqual(TEXT("unit A anchor"), StatsA->OwnerUnit.Index, Units[0].Index);
				TestEqual(TEXT("unit B anchor"), StatsB->OwnerUnit.Index, Units[1].Index);
			}

			// Owner fragments are valid on every entity of both rosters.
			int32 OwnerErrors = 0;
			for (int32 u = 0; u < 2; ++u)
			{
				const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Units[u]);
				for (int32 i = 0; i < Roster->Num(); ++i)
				{
					const FMassEntityView View(EntityManager, (*Roster)[i]);
					const FOwnerUnitFragment* Owner = View.GetFragmentDataPtr<FOwnerUnitFragment>();
					if (!Owner || Owner->Owner != Units[u] || Owner->RosterIndex != i)
					{
						++OwnerErrors;
					}
				}
			}
			TestEqual(TEXT("all 80 owner fragments fixup correctly"), OwnerErrors, 0);
		}
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
