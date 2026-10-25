#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.MovementSpine.* -- phase-1 Task 2 archetype probe + data-spine
// init checks. Ground rule 1 (gate-first): asserts live composition queries
// (fragment presence per entity, distinct archetype handles), not flags.
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"

#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"

namespace
{
	UWorld* MakeSpineTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	bool LoadFixtureForSpine(TArray<FEduUnit>& OutRows)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		FEduParseError Err;
		return FBattleEduParser::Parse(Text, OutRows, Err);
	}
}

// The probe: spawning two units must yield role archetypes only -- the uniform
// movement-spine fragment set must NOT split composition further (per-entity
// unique fragments stamped as fragments would double archetypes per entity,
// arch §3.4). Also re-proves ADR-14 for the new formation shared fragment:
// two units, identical payloads except the handle anchor -> two instances.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementSpine_ArchetypeProbe,
	"Battle.Unit.MovementSpine.ArchetypeProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMovementSpine_ArchetypeProbe::RunTest(const FString& Parameters)
{
	UWorld* World = MakeSpineTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("entities"), ES))
	{
		World->DestroyWorld(false);
		return false;
	}

	TArray<FEduUnit> Rows;
	if (!TestTrue(TEXT("fixture"), LoadFixtureForSpine(Rows)) || Rows.Num() < 1)
	{
		World->DestroyWorld(false);
		return false;
	}

	// Two units from the same row: identical stats, distinct handles.
	TArray<FUnitHandle> Units;
	for (int32 UnitIdx = 0; UnitIdx < 2; ++UnitIdx)
	{
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = 0;
		Spec.SoldierCount = 10;
		Spec.UnitName = FString::Printf(TEXT("Spine_%d"), UnitIdx);
		Spec.SpawnPos = FVector2D(1000.f * (UnitIdx + 1), 2000.f);
		FBattleSpawnResult Result;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("spawn unit %d"), UnitIdx),
			UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[0], Result, Error)))
		{
			AddError(Error);
			World->DestroyWorld(false);
			return false;
		}
		Units.Add(Result.Unit);
	}

	FMassEntityManager& EM = ES->GetMutableEntityManager();

	// Live-composition probe: every entity carries the full movement spine;
	// all battle entities sit in exactly ONE archetype (the fixture rows share
	// a role composition, so no second archetype may appear).
	TSet<FMassArchetypeHandle> BattleArchetypes;
	int32 Probed = 0;
	for (const FUnitHandle& Handle : Units)
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!TestNotNull(TEXT("roster"), Roster))
		{
			World->DestroyWorld(false);
			return false;
		}
		for (const FMassEntityHandle& Entity : *Roster)
		{
			const FMassEntityView View(EM, Entity);
			const bool bHasSpine =
				View.GetFragmentDataPtr<FTransformFragment>() != nullptr &&
				View.GetFragmentDataPtr<FMassVelocityFragment>() != nullptr &&
				View.GetFragmentDataPtr<FSlotAssignmentFragment>() != nullptr &&
				View.GetFragmentDataPtr<FSlotTargetFragment>() != nullptr &&
				View.GetFragmentDataPtr<FMovementStateFragment>() != nullptr &&
				View.GetFragmentDataPtr<FServoStateFragment>() != nullptr;
			TestTrue(FString::Printf(TEXT("movement spine present on entity %d"), Probed), bHasSpine);
			BattleArchetypes.Add(EM.GetArchetypeForEntity(Entity));
			++Probed;
		}
	}
	TestEqual(TEXT("probed all entities"), Probed, 20);
	TestEqual(TEXT("one battle archetype (no spine-induced doubling)"), BattleArchetypes.Num(), 1);

	// ADR-14 for the formation fragment: same stats, distinct handles -> two
	// distinct shared instances (pointer inequality on live data).
	FUnitFormationSharedFragment* FormationA =
		EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Registry->FindRoster(Units[0]))[0]);
	FUnitFormationSharedFragment* FormationB =
		EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Registry->FindRoster(Units[1]))[0]);
	TestNotNull(TEXT("formation A"), FormationA);
	TestNotNull(TEXT("formation B"), FormationB);
	if (FormationA && FormationB)
	{
		TestNotEqual(TEXT("formation instances distinct per unit (ADR-14)"),
			reinterpret_cast<const void*>(FormationA), reinterpret_cast<const void*>(FormationB));
		TestEqual(TEXT("formation A anchored to unit A"), FormationA->OwnerUnit, Units[0]);
		TestEqual(TEXT("formation B anchored to unit B"), FormationB->OwnerUnit, Units[1]);
	}

	World->DestroyWorld(false);
	return true;
}

// Data-spine init: spec -> fragment values (cm/m conversion at the boundary),
// slot layout (rank-major linear ids), spawn transform propagation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovementSpine_SlotLayoutInit,
	"Battle.Unit.MovementSpine.SlotLayoutInit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMovementSpine_SlotLayoutInit::RunTest(const FString& Parameters)
{
	UWorld* World = MakeSpineTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("entities"), ES))
	{
		World->DestroyWorld(false);
		return false;
	}

	TArray<FEduUnit> Rows;
	if (!TestTrue(TEXT("fixture"), LoadFixtureForSpine(Rows)))
	{
		World->DestroyWorld(false);
		return false;
	}

	FUnitSpawnSpec Spec;
	Spec.EduRowIndex = 0;
	Spec.SoldierCount = 40;
	Spec.UnitName = TEXT("SpineLayout");
	Spec.SpawnPos = FVector2D(5000.f, -3000.f);
	Spec.SpawnYaw = 90.f;
	Spec.FormationSpacing = 1.2f; // meters (project convention)
	Spec.NumRanks = 2;
	FBattleSpawnResult Result;
	FString Error;
	if (!TestTrue(TEXT("spawn"), UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[0], Result, Error)))
	{
		AddError(Error);
		World->DestroyWorld(false);
		return false;
	}

	FMassEntityManager& EM = ES->GetMutableEntityManager();
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Result.Unit);
	if (!TestNotNull(TEXT("roster"), Roster))
	{
		World->DestroyWorld(false);
		return false;
	}

	FUnitFormationSharedFragment* Formation =
		EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]);
	if (!TestNotNull(TEXT("formation"), Formation))
	{
		World->DestroyWorld(false);
		return false;
	}

	// 1.2 m -> 120 cm at the fragment boundary (ground rule 2 golden case).
	TestEqual(TEXT("rank spacing in cm"), Formation->RankSpacing, 120.f);
	TestEqual(TEXT("file spacing in cm"), Formation->FileSpacing, 120.f);
	TestEqual(TEXT("files = ceil(40/2)"), Formation->NumFiles, 20);
	TestEqual(TEXT("anchor from spec"), Formation->AnchorPos, Spec.SpawnPos);
	TestEqual(TEXT("facing from spec"), Formation->FacingYaw, 90.f);
	TestTrue(TEXT("path empty until planner runs"), !Formation->Path.IsValid());

	// Bijection-by-construction probe: slot ids are 0..N-1, each once; the
	// full T1.1 assigner guarantees land in Task 7.
	TSet<int32> SlotIds;
	bool bTransformsOk = true;
	const float Rad = FMath::DegreesToRadians(90.f);
	for (int32 i = 0; i < Roster->Num(); ++i)
	{
		const FMassEntityView View(EM, (*Roster)[i]);
		const FSlotAssignmentFragment* Slot = View.GetFragmentDataPtr<FSlotAssignmentFragment>();
		if (Slot)
		{
			SlotIds.Add(Slot->SlotIndex);
		}
		const FTransformFragment* Transform = View.GetFragmentDataPtr<FTransformFragment>();
		if (Transform)
		{
			// Formed spawn (phase-1 Task 10 fix): entity i sits at its slot
			// offset around the anchor -- rank behind, files lateral, rotated
			// by the spawn yaw. Facing yaw propagates to every entity.
			const FVector Pos = Transform->GetTransform().GetTranslation();
			const int32 Rank = i / Formation->NumFiles;
			const int32 File = i % Formation->NumFiles;
			const FVector2D Offset(
				-static_cast<float>(Rank) * Formation->RankSpacing,
				(static_cast<float>(File) - (Formation->NumFiles - 1) * 0.5f) * Formation->FileSpacing);
			const FVector2D Expected = Spec.SpawnPos
				+ FVector2D(Offset.X * FMath::Cos(Rad) - Offset.Y * FMath::Sin(Rad),
					Offset.X * FMath::Sin(Rad) + Offset.Y * FMath::Cos(Rad));
			bTransformsOk = bTransformsOk
				&& FMath::IsNearlyEqual(Pos.X, Expected.X, 1.f)
				&& FMath::IsNearlyEqual(Pos.Y, Expected.Y, 1.f)
				&& FMath::IsNearlyEqual(Transform->GetTransform().Rotator().Yaw, 90.f);
		}
		else
		{
			bTransformsOk = false;
		}
	}
	TestEqual(TEXT("slot ids are a 0..39 bijection"), SlotIds.Num(), 40);
	TestTrue(TEXT("formed spawn transforms (anchor + slot offset, cm)"), bTransformsOk);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
