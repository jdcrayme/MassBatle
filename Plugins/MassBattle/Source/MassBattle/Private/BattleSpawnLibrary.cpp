#include "BattleSpawnLibrary.h"
#include "BattleFragments.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "StructUtils/SharedStruct.h"

bool FBattleRowStats::FromEdu(const FEduUnit& Row, FBattleRowStats& OutStats, FString& OutError)
{
	const auto Need = [&Row, &OutError](const FString& Key, int32 MinValues) -> const FEduField*
	{
		const FEduField* F = Row.FindField(Key);
		if (!F || F->Values.Num() < MinValues)
		{
			OutError = FString::Printf(TEXT("EDU row missing field '%s' (%d values)"), *Key, F ? F->Values.Num() : 0);
			return nullptr;
		}
		return F;
	};

	const FEduField* Type = Need(TEXT("type"), 1);
	const FEduField* Soldier = Need(TEXT("soldier"), 2);
	const FEduField* Health = Need(TEXT("stat_health"), 1);
	const FEduField* Pri = Need(TEXT("stat_pri"), 2);
	const FEduField* Armour = Need(TEXT("stat_pri_armour"), 1);
	const FEduField* Mental = Need(TEXT("stat_mental"), 1);
	const FEduField* Charge = Need(TEXT("stat_charge_dist"), 1);
	const FEduField* Category = Need(TEXT("category"), 1);
	const FEduField* Class = Need(TEXT("class"), 1);
	if (!Type || !Soldier || !Health || !Pri || !Armour || !Mental || !Charge || !Category || !Class)
	{
		return false;
	}

	OutStats = FBattleRowStats();
	OutStats.TypeName = Type->GetValue();
	OutStats.SoldierCount = FCString::Atoi(*Soldier->Values[1]);
	OutStats.BaseHealth = FCString::Atof(*Health->Values[0]);
	OutStats.MeleeAttack = FCString::Atof(*Pri->Values[0]);
	OutStats.MeleeDefense = FCString::Atof(*Pri->Values[1]);
	OutStats.Armor = FCString::Atof(*Armour->Values[0]);
	OutStats.MoraleBase = FCString::Atof(*Mental->Values[0]);
	OutStats.ChargeDistance = FCString::Atof(*Charge->Values[0]);

	OutStats.bInfantry = (Category->GetValue() == TEXT("infantry"));
	OutStats.bCavalry = (Category->GetValue() == TEXT("cavalry"));
	// Phase 0 fixture classes are melee ('light', 'spear'); bow flag arrives with ranged rows.
	OutStats.bMelee = !OutStats.bBow;

	return true;
}

FBattleSpawnResult UBattleSpawnLibrary::SpawnUnit(UWorld* World, const FUnitSpawnSpec& Spec, const FBattleRowStats& Row)
{
	checkf(World, TEXT("SpawnUnit requires a world"));
	checkf(Spec.EduRowIndex != INDEX_NONE && Spec.SoldierCount > 0, TEXT("SpawnUnit: invalid spec"));

	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	checkf(Registry && EntitySubsystem, TEXT("SpawnUnit: missing world subsystems"));

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	checkf(!EntityManager.IsProcessing(), TEXT("SpawnUnit: structural changes are forbidden while Mass is processing (arch §4)"));

	const double StartTime = FPlatformTime::Seconds();

	// 1. Register the unit (handle + empty roster).
	const FUnitHandle Unit = Registry->RegisterUnit(Spec);
	checkf(Unit.IsValid(), TEXT("SpawnUnit: registry rejected the spec"));

	// 2. ADR-14 anchored shared fragment: one instance per unit.
	FUnitStatsSharedFragment StatsPayload;
	StatsPayload.OwnerUnit = Unit;          // uniqueness anchor -- keep first
	StatsPayload.SpeedMult = 1.f;
	StatsPayload.MeleeAttackMult = 1.f;
	StatsPayload.MeleeDefenseMult = 1.f;
	StatsPayload.RangedAttackMult = 1.f;
	StatsPayload.BlockMult = 1.f;
	StatsPayload.FatigueAttackMult = 1.f;
	StatsPayload.FatigueDefenseMult = 1.f;
	StatsPayload.ArmorAdd = Row.Armor * 0.0f; // ArmorAdd is the buff term; base armor lives on FBaseStatsFragment
	StatsPayload.AccuracyMult = 1.f;
	StatsPayload.MoraleBonus = 0.f;
	const FSharedStruct SharedStats = FSharedStruct::Make(StatsPayload);

	// 3. Per-role archetype (Route A): fragments + tags + the shared fragment type.
	const UScriptStruct* Fragments[] = {
		FOwnerUnitFragment::StaticStruct(),
		FLoadoutFragment::StaticStruct(),
		FBaseStatsFragment::StaticStruct(),
		FHealthFragment::StaticStruct(),
	};
	TArray<const UScriptStruct*> Tags;
	if (Row.bInfantry) { Tags.Add(FInfantryTag::StaticStruct()); }
	if (Row.bCavalry)  { Tags.Add(FCavalryTag::StaticStruct()); }
	if (Row.bMelee)    { Tags.Add(FMeleeTag::StaticStruct()); }
	if (Row.bBow)      { Tags.Add(FBowTag::StaticStruct()); }
	const UScriptStruct* Shareds[] = { FUnitStatsSharedFragment::StaticStruct() };

	FMassArchetypeCompositionDescriptor Descriptor(
		TConstArrayView<const UScriptStruct*>(Fragments),
		FMassTagBitSet(TConstArrayView<const UScriptStruct*>(Tags.GetData(), Tags.Num())),
		FMassChunkFragmentBitSet(),
		FMassSharedFragmentBitSet(TConstArrayView<const UScriptStruct*>(Shareds)),
		FMassConstSharedFragmentBitSet());

	FMassArchetypeCreationParams CreationParams;
	const FMassArchetypeHandle Archetype = EntityManager.CreateArchetype(Descriptor, CreationParams);
	if (UNLIKELY(!Archetype.IsValid()))
	{
		// Audit low: release the slot registered above or the failure path
		// leaks it (stale handle in the registry with no entities).
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SpawnRoster FAIL archetype creation failed"));
		Registry->UnregisterUnit(Unit);
		return FBattleSpawnResult();
	}

	// 4. Batch-create the roster into the archetype with the shared value.
	FMassArchetypeSharedFragmentValues SharedValues;
	(void)SharedValues.Add_GetRef(SharedStats);

	TArray<FMassEntityHandle> Entities;
	Entities.Reserve(Spec.SoldierCount);
	TSharedRef<FMassEntityManager::FEntityCreationContext> CreationContext =
		EntityManager.BatchCreateEntities(Archetype, SharedValues, Spec.SoldierCount, Entities);
	checkf(Entities.Num() == Spec.SoldierCount, TEXT("SpawnUnit: batch created %d of %d"), Entities.Num(), Spec.SoldierCount);

	// 5. Fixup pass: owner link, loadout, immutable base stats, initial HP.
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FMassEntityView View(EntityManager, Entities[i]);

		FOwnerUnitFragment& Owner = View.GetFragmentData<FOwnerUnitFragment>();
		Owner.Owner = Unit;
		Owner.RosterIndex = i;

		FLoadoutFragment& Loadout = View.GetFragmentData<FLoadoutFragment>();
		Loadout.EduRowIndex = Spec.EduRowIndex;
		Loadout.MeshId = INDEX_NONE;  // representation arrives Phase 5
		Loadout.WeaponId = INDEX_NONE;

		FBaseStatsFragment& Base = View.GetFragmentData<FBaseStatsFragment>();
		Base.BaseHealth = Row.BaseHealth;
		Base.MeleeAttack = Row.MeleeAttack;
		Base.MeleeDefense = Row.MeleeDefense;
		Base.RangedAttack = Row.RangedAttack;
		Base.Armor = Row.Armor;
		Base.MoraleBase = Row.MoraleBase;
		Base.ChargeDistance = Row.ChargeDistance;

		FHealthFragment& Health = View.GetFragmentData<FHealthFragment>();
		Health.CurrentHP = Row.BaseHealth;
		Health.QueuedDamage = 0.f;
	}

	// 6. Publish the roster + initial attribute cache.
	Registry->SetRoster(Unit, MoveTemp(Entities));

	FBattleAttributeSnapshot Snapshot;
	Snapshot.Morale = Row.MoraleBase;
	Registry->UpdateCachedAttributes(Unit, Snapshot);

	FBattleSpawnResult Result;
	Result.Unit = Unit;
	Result.NumEntities = Spec.SoldierCount;
	Result.ElapsedUs = (FPlatformTime::Seconds() - StartTime) * 1e6;

	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnRoster Unit=%u:%u Entities=%d Row=%d Name=%s"),
	       Unit.Index, Unit.Serial, Result.NumEntities, Spec.EduRowIndex, *Spec.UnitName);
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SpawnPerf Unit=%u:%u Micros=%.1f PerEntity=%.2f"),
	       Unit.Index, Unit.Serial, Result.ElapsedUs, Result.ElapsedUs / FMath::Max(1, Result.NumEntities));
	return Result;
}

bool UBattleSpawnLibrary::SpawnUnitFromEduRow(UWorld* World, const FUnitSpawnSpec& Spec, const FEduUnit& Row,
	FBattleSpawnResult& OutResult, FString& OutError)
{
	FBattleRowStats Stats;
	if (!FBattleRowStats::FromEdu(Row, Stats, OutError))
	{
		return false;
	}
	if (Spec.SoldierCount <= 0)
	{
		// Honor the row's own soldier count when the spec leaves it at 0.
		FUnitSpawnSpec Patched = Spec;
		Patched.SoldierCount = Stats.SoldierCount;
		OutResult = SpawnUnit(World, Patched, Stats);
	}
	else
	{
		OutResult = SpawnUnit(World, Spec, Stats);
	}
	if (!OutResult.Unit.IsValid())
	{
		OutError = TEXT("spawn failed");
		return false;
	}
	OutError.Reset();
	return true;
}
