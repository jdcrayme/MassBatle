#include "BattleSpawnLibrary.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleRangedFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "BattleMelee.h"
#include "BattleRanged.h"
#include "MassBattleModule.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
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
	OutStats.Armor = FCString::Atof(*Armour->Values[0]);
	OutStats.MoraleBase = FCString::Atof(*Mental->Values[0]);
	OutStats.ChargeDistance = FCString::Atof(*Charge->Values[0]);

	OutStats.bInfantry = (Category->GetValue() == TEXT("infantry"));
	OutStats.bCavalry = (Category->GetValue() == TEXT("cavalry"));

	// Phase 3 ranged branch (spec ranged-stack section 6; brief F3):
	// stat_pri is the missile weapon when its missile slot != "no";
	// stat_sec is then the melee backup. Unknown missile tokens fail
	// loudly (a "catapult_ball" row must not silently become melee).
	if (Pri->Values.Num() > 4)
	{
		const FString& Missile = Pri->Values[2];
		if (Missile == TEXT("no"))
		{
			OutStats.Kind = EBattleProjectileKind::None;
		}
		else if (Missile == TEXT("arrow"))
		{
			OutStats.Kind = EBattleProjectileKind::Arrow;
		}
		else if (Missile == TEXT("bolt"))
		{
			OutStats.Kind = EBattleProjectileKind::Bolt;
		}
		else
		{
			OutError = FString::Printf(TEXT("EDU row '%s': unknown missile kind '%s' (arrow|bolt|no)"),
				*OutStats.TypeName, *Missile);
			return false;
		}
	}
	OutStats.bBow = (OutStats.Kind != EBattleProjectileKind::None);
	OutStats.bSkirmishDefault = (Class->GetValue() == TEXT("missile"));

	if (OutStats.bBow)
	{
		// Missile row: stat_pri carries the ranged weapon, stat_sec the
		// melee backup (weapon-switch modeling is a spec section 10
		// extension point -- v1 maps the backup as the melee stats).
		OutStats.RangedAttack = FCString::Atof(*Pri->Values[0]);
		OutStats.RangedRangeM = FCString::Atof(*Pri->Values[3]);
		OutStats.AmmoPerMan = FCString::Atoi(*Pri->Values[4]);
		const FEduField* Sec = Need(TEXT("stat_sec"), 2);
		if (!Sec)
		{
			return false;
		}
		OutStats.MeleeAttack = FCString::Atof(*Sec->Values[0]);
		OutStats.MeleeDefense = FCString::Atof(*Sec->Values[1]);
	}
	else
	{
		// Melee row: stat_pri is the melee weapon (the Phase 0-2
		// mapping, unchanged); stat_sec mirrors it in the fixture and
		// is not consumed.
		OutStats.MeleeAttack = FCString::Atof(*Pri->Values[0]);
		OutStats.MeleeDefense = FCString::Atof(*Pri->Values[1]);
	}
	OutStats.bMelee = !OutStats.bBow;

	// Phase 2 combat fields (task 2 step 1, R-C10): typed extractions of the
	// EXISTING parsed fixture fields -- no schema/parser change, the T0.2
	// golden round-trip is untouched. Field identities are A6 placeholders for
	// the Phase-6 T6.1 audit; the STRUCTURE (reach class gates the second-rank
	// budget bonus, AP halves armor, lethality multiplies the hit chance,
	// mounts carry a second pool) ships now.
	//   stat_pri fixture shape: attack, defense, no, 0, 0, melee,
	//   <weapon_type simple|blade_piercing>, <tech other|spear>, <n>, <dec>, <dec>
	if (Pri->Values.Num() > 7)
	{
		OutStats.bArmorPiercing = (Pri->Values[6] == TEXT("blade_piercing"));
		const FString& Tech = Pri->Values[7];
		OutStats.ReachClass = (Tech == TEXT("spear"))
			? static_cast<uint8>(EBattleWeaponReach::Spear)
			: (Tech == TEXT("pike"))
				? static_cast<uint8>(EBattleWeaponReach::Pike)
				: (Tech == TEXT("long_spear"))
					? static_cast<uint8>(EBattleWeaponReach::LongSpear)
					: static_cast<uint8>(EBattleWeaponReach::Normal);
	}
	if (Pri->Values.Num() > 9)
	{
		OutStats.Lethality = FCString::Atof(*Pri->Values[9]);
	}
	if (Armour->Values.Num() > 2)
	{
		OutStats.Shield = FCString::Atof(*Armour->Values[2]); // frontal-arc defense
	}
	if (Health->Values.Num() > 1 && OutStats.bCavalry)
	{
		OutStats.MountHP = FCString::Atoi(*Health->Values[1]); // mount pool, cavalry rows only
	}

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

	// 2b. ADR-14 anchored formation fragment (phase-1 data spine): layout
	// params from the spec; meters->centimeters conversion happens here at the
	// fragment boundary (ground rule 2). Path stays empty until the planner
	// (Task 3) fills it via UnitLocomotion.
	FUnitFormationSharedFragment FormationPayload;
	FormationPayload.OwnerUnit = Unit;      // uniqueness anchor -- keep first
	FormationPayload.AnchorPos = Spec.SpawnPos;
	FormationPayload.FacingYaw = Spec.SpawnYaw;
	FormationPayload.SpeedLimit = 0.f;
	FormationPayload.SpacingScale = 1.f;
	FormationPayload.Generation = 1;
	// The spawn layout IS the locality answer: stamping AssignedGeneration
	// skips the first-tick unbounded re-assign, whose entity-to-slot
	// tie-breaking iterates in chunk order -- entity-creation-dependent
	// across a battle reset, and every two-run transform hash diverged on
	// the swapped slots (measured 2026-10-29, RC8).
	FormationPayload.AssignedGeneration = FormationPayload.Generation;
	FormationPayload.NumFiles = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Spec.SoldierCount) / FMath::Max(1, Spec.NumRanks)));
	FormationPayload.NumSlots = Spec.SoldierCount;
	FormationPayload.RankSpacing = Spec.FormationSpacing * 100.f;
	FormationPayload.FileSpacing = Spec.FormationSpacing * 100.f;
	const FSharedStruct SharedFormation = FSharedStruct::Make(FormationPayload);

	// 3. Per-role archetype (Route A): fragments + tags + the shared fragment types.
	// Phase 1: the movement spine rides on every battle entity uniformly so the
	// role archetypes do not split further (Task 2 archetype probe).
	// Phase 3 (spec ranged-stack §3.1): the ranged fragment rides
	// RANGED-CAPABLE rows only (arch §3.2: a spearman has no ranged
	// fragments, so the ranged-fire processor never touches his chunks);
	// bow and crossbow share ONE composition (Kind is a fragment value).
	// The archetype count grows by the ranged compositions -- measured +
	// itemized by SpawnComposition, growth disclosed in the phase report
	// (F6).
	const bool bRanged = Row.Kind != EBattleProjectileKind::None;
	TArray<const UScriptStruct*> Fragments;
	Fragments.Reserve(16);
	Fragments.Add(FOwnerUnitFragment::StaticStruct());
	Fragments.Add(FLoadoutFragment::StaticStruct());
	Fragments.Add(FBaseStatsFragment::StaticStruct());
	Fragments.Add(FHealthFragment::StaticStruct());
	Fragments.Add(FTransformFragment::StaticStruct());   // MassCommon: position (engine stack, cm)
	Fragments.Add(FMassVelocityFragment::StaticStruct());// MassMovement: adopted per arch §3.1 note
	Fragments.Add(FSlotAssignmentFragment::StaticStruct());
	Fragments.Add(FSlotTargetFragment::StaticStruct());
	Fragments.Add(FMovementStateFragment::StaticStruct());
	Fragments.Add(FServoStateFragment::StaticStruct());
	// Phase 2 combat spine (spec §3.1, A2): on EVERY battle entity
	// uniformly -- zero new tags, composition grows (F6).
	Fragments.Add(FCombatIntentFragment::StaticStruct());
	Fragments.Add(FCombatPairFragment::StaticStruct());
	Fragments.Add(FChargeFragment::StaticStruct());
	if (bRanged)
	{
		Fragments.Add(FRangedStateFragment::StaticStruct());
	}
	TArray<const UScriptStruct*> Tags;
	if (Row.bInfantry) { Tags.Add(FInfantryTag::StaticStruct()); }
	if (Row.bCavalry)  { Tags.Add(FCavalryTag::StaticStruct()); }
	if (Row.bMelee)    { Tags.Add(FMeleeTag::StaticStruct()); }
	if (Row.bBow)      { Tags.Add(FBowTag::StaticStruct()); }
	const UScriptStruct* Shareds[] = { FUnitStatsSharedFragment::StaticStruct(), FUnitFormationSharedFragment::StaticStruct() };

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

	// 4. Batch-create the roster into the archetype with the shared values.
	// Add_GetRef does not sort (MassEntityTypes.cpp) -- Sort() before the batch
	// create; BatchCreateEntities asserts a sorted value set (MassArchetypeData
	// PrepareNextEntitiesSpanInternal).
	FMassArchetypeSharedFragmentValues SharedValues;
	(void)SharedValues.Add_GetRef(SharedStats);
	(void)SharedValues.Add_GetRef(SharedFormation);
	SharedValues.Sort();

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
		Health.MountCurHP = static_cast<int16>(Row.MountHP);

		// Phase 2 combat spine init (spec §3.1): unpaired, unengaged, zero
		// timers. The assign pass (task 6) writes the pair state.
		FCombatIntentFragment& Intent = View.GetFragmentData<FCombatIntentFragment>();
		Intent.TargetUnit = FUnitHandle();
		Intent.LaneIndex = 0;
		Intent.bAutoEngage = 0;

		FCombatPairFragment& Pair = View.GetFragmentData<FCombatPairFragment>();
		Pair.Target = FMassEntityHandle();
		Pair.ContactAngle = 0;
		Pair.State = ECombatPairState::Seeking;
		Pair.AttackTimer = 0.f;
		Pair.PairAge = 0.f;
		Pair.ChargeBonusRemaining = 0.f;

		FChargeFragment& Charge = View.GetFragmentData<FChargeFragment>();
		Charge.DistanceTraveled = 0.f;
		Charge.ImpactSpeed = 0.f;
		Charge.bConnected = 0;

		// Phase 3 ranged spine init (spec ranged-stack §3.1): spawn-immutable
		// fields from the row; reload staggered via the handle-hash family so
		// volleys never serialize (RC1 -- the StaggeredPhaseOffset
		// discipline, one home for the Knuth multipliers). Fragments carry
		// METERS here (the seam convention; the pool boundary converts once).
		if (bRanged)
		{
			FRangedStateFragment& Ranged = View.GetFragmentData<FRangedStateFragment>();
			Ranged.Kind = Row.Kind;
			Ranged.RangeM = Row.RangedRangeM;
			Ranged.InitialAmmo = static_cast<uint16>(FMath::Clamp(Row.AmmoPerMan, 0, 65535));
			Ranged.AmmoCount = static_cast<int16>(Ranged.InitialAmmo);
			Ranged.ReloadTimer = FMeleeMath::StaggeredPhaseOffset(
				BattleCanonicalEntityKey(Unit, i), FRangedCVars::ReloadInterval());
			Ranged.ShotsFired = 0;
		}

		// Movement spine init (phase-1 Task 2): spawn transform, zeroed
		// velocity, initial slot layout, HoldSlot state. Entities spawn at
		// their FORMED slot positions (rank-major around the anchor) -- a
		// co-located spawn makes the avoidance term push the whole blob
		// coherently instead of spreading it (S1 evidence: 320 entities fled
		// as one mass).
		FTransformFragment& Transform = View.GetFragmentData<FTransformFragment>();
		{
			const int32 Rank = i / FormationPayload.NumFiles;
			const int32 File = i % FormationPayload.NumFiles;
			const FVector2D Offset(
				-static_cast<float>(Rank) * FormationPayload.RankSpacing,
				(static_cast<float>(File) - (FormationPayload.NumFiles - 1) * 0.5f) * FormationPayload.FileSpacing);
			const float Rad = FMath::DegreesToRadians(Spec.SpawnYaw);
			const float Cos = FMath::Cos(Rad);
			const float Sin = FMath::Sin(Rad);
			const FVector2D World = Spec.SpawnPos + FVector2D(Offset.X * Cos - Offset.Y * Sin, Offset.X * Sin + Offset.Y * Cos);
			Transform.GetMutableTransform().SetTranslation(FVector(World.X, World.Y, 0.f));
		}
		Transform.GetMutableTransform().SetRotation(FQuat(FRotator(0.f, Spec.SpawnYaw, 0.f)));

		FMassVelocityFragment& Velocity = View.GetFragmentData<FMassVelocityFragment>();
		Velocity.Value = FVector::ZeroVector;

		FSlotAssignmentFragment& Slot = View.GetFragmentData<FSlotAssignmentFragment>();
		Slot.SlotIndex = i;
		Slot.Generation = FormationPayload.Generation;

		FMovementStateFragment& MoveState = View.GetFragmentData<FMovementStateFragment>();
		MoveState.State = EBattleMovementState::HoldSlot;
	}

	// 6. Publish the roster + initial attribute cache.
	Registry->SetRoster(Unit, MoveTemp(Entities));

	// Phase 3 (spec ranged-stack §3.2, A8): class-missile rows default the
	// skirmish toggle on at spawn; the registry owns the toggle state. The
	// same stamp carries the fraction denominator's ammo half (melee rows
	// pass AmmoPerMan 0 -> the fraction reads -1 there).
	Registry->SetUnitRangedSpawnState(Unit, Row.bSkirmishDefault, Row.AmmoPerMan);

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
