#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Garrison.* -- phase-garrison gate family TG.1-TG.5 (brief §6).
// Task 0 shipped these as fail-by-design skeletons (gate-first discipline);
// each body lands with its owning task (or earlier, per §5 gate-first rule).
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "BattleGarrisonTypes.h"
#include "BattleGarrison.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "BattleStateHash.h"
#include "BattleFragments.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"

namespace
{
	UWorld* MakeGarrisonTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	bool LoadEduFixture(TArray<FEduUnit>& OutRows)
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

	/** Minimal record for ledger tests (slots grouped by entrance in one
	 *  contiguous block per side; TryBook first-fits inside the block). */
	FGarrisonStructureRecord MakeLedgerRecord(int32 MaxUnits, int32 SlotsPerEntrance)
	{
		FGarrisonStructureRecord Rec;
		Rec.Type = EGarrisonStructureType::Building;
		Rec.MaxUnits = MaxUnits;
		Rec.Footprint = FBox2D(FVector2D(-10.f, -6.f), FVector2D(10.f, 6.f));
		for (uint8 E = 0; E < 2; ++E)
		{
			FGarrisonEntrance Entrance;
			Entrance.DoorPos = FVector2D(0.f, E == 0 ? -6.f : 6.f);
			Entrance.QueuePos = FVector2D(0.f, E == 0 ? -12.f : 12.f);
			Entrance.AssemblyPos = FVector2D(0.f, E == 0 ? -16.f : 16.f);
			Entrance.ClearWidthM = 4.f;
			Rec.Entrances.Add(Entrance);
			for (int32 S = 0; S < SlotsPerEntrance; ++S)
			{
				FGarrisonSlot Slot;
				Slot.Pos = FVector2D(-4.5f + 1.5f * S, E == 0 ? -2.f : 2.f);
				Slot.Z = 0.f;
				Slot.YawDeg = E == 0 ? 180.f : 0.f;
				Slot.EntranceIndex = E;
				Rec.Slots.Add(Slot);
			}
		}
		return Rec;
	}

	bool GarrisonSkeleton(FAutomationTestBase& Test, const TCHAR* Gate)
	{
		Test.AddError(FString::Printf(TEXT("skeleton: %s lands with its owning task (brief §5)"), Gate));
		return false;
	}
}

// ---------------------------------------------------------------------------
// TG.1 Stamping (task 2)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_Stamping,
	"Battle.Unit.Garrison.Stamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_Stamping::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.1 Battle.Unit.Garrison.Stamping (task 2)"));
}

// ---------------------------------------------------------------------------
// TG.2 InternalPath (task 3)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_InternalPath,
	"Battle.Unit.Garrison.InternalPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_InternalPath::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.2 Battle.Unit.Garrison.InternalPath (task 3)"));
}

// ---------------------------------------------------------------------------
// TG.3 SlotAssign (task 3)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_SlotAssign,
	"Battle.Unit.Garrison.SlotAssign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_SlotAssign::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.3 Battle.Unit.Garrison.SlotAssign (task 3)"));
}

// ---------------------------------------------------------------------------
// TG.4 Booking (body landed with task 1 -- the data-spine unit tier; brief §5
// gate-first allows a gate to land in the prior task's commit). Covers:
// capacity enforcement, disjoint slot ranges, lease exclusivity, release +
// stale/duplicate-handle rejection, queue-wait slots, booking determinism,
// hash stability + garrison-mutation sensitivity (R-G6), and archetype
// invariance under the extended shared fragment (F11/A1).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrison_Booking,
	"Battle.Unit.Garrison.Booking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrison_Booking::RunTest(const FString& Parameters)
{
	UWorld* World = MakeGarrisonTestWorld();
	UBattleGarrisonSubsystem* Garrison = World->GetSubsystem<UBattleGarrisonSubsystem>();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("garrison"), Garrison) || !TestNotNull(TEXT("registry"), Registry)
		|| !TestNotNull(TEXT("entities"), Entities))
	{
		World->DestroyWorld(false);
		return false;
	}

	// -- Ledger semantics: capacity, disjoint ranges, determinism. --
	const uint32 StructureA = Garrison->RegisterStructure(MakeLedgerRecord(2, 40));
	TestTrue(TEXT("structure A registered"), StructureA != 0);

	FUnitHandle U1, U2, U3;
	U1.Index = 1; U1.Serial = 1;
	U2.Index = 2; U2.Serial = 1;
	U3.Index = 3; U3.Serial = 1;

	FGarrisonBooking B1, B2, B3;
	TestTrue(TEXT("book unit 1"), Garrison->TryBook(U1, StructureA, 0, 20, B1));
	TestTrue(TEXT("book unit 2"), Garrison->TryBook(U2, StructureA, 0, 20, B2));
	TestFalse(TEXT("capacity: third unit refused (MaxUnits)"), Garrison->TryBook(U3, StructureA, 0, 20, B3));
	TestEqual(TEXT("booked count"), Garrison->GetBookedCount(StructureA), 2);

	// Disjoint ranges + block contiguity.
	TestEqual(TEXT("range 1 starts at block base"), B1.SlotBase, 0);
	TestTrue(TEXT("ranges disjoint"), B2.SlotBase >= B1.SlotBase + B1.SlotCount);
	TestEqual(TEXT("range sizes"), B1.SlotCount + B2.SlotCount, 40);

	// Determinism: same issue order over a fresh record (fresh handles, since
	// a unit holds at most one booking) -> identical ranges.
	FUnitHandle U4, U5;
	U4.Index = 4; U4.Serial = 1;
	U5.Index = 5; U5.Serial = 1;
	const uint32 StructureB = Garrison->RegisterStructure(MakeLedgerRecord(2, 40));
	FGarrisonBooking C1, C2;
	TestTrue(TEXT("book unit 4 on fresh record"), Garrison->TryBook(U4, StructureB, 0, 20, C1));
	TestTrue(TEXT("book unit 5 on fresh record"), Garrison->TryBook(U5, StructureB, 0, 20, C2));
	TestEqual(TEXT("deterministic first-fit: same base 1"), C1.SlotBase, B1.SlotBase);
	TestEqual(TEXT("deterministic first-fit: same base 2"), C2.SlotBase, B2.SlotBase);

	// Stale/duplicate handles.
	FGarrisonBooking Dup;
	FUnitHandle NullHandle; // serial 0 = null
	TestFalse(TEXT("null handle rejected"), Garrison->TryBook(NullHandle, StructureA, 0, 5, Dup));
	TestFalse(TEXT("duplicate booking rejected"), Garrison->TryBook(U1, StructureA, 0, 5, Dup));
	TestTrue(TEXT("release"), Garrison->ReleaseBooking(U1));
	TestFalse(TEXT("double release rejected"), Garrison->ReleaseBooking(U1));
	TestFalse(TEXT("oversized range refused"), Garrison->TryBook(U3, StructureA, 0, 45, Dup));
	TestTrue(TEXT("book into freed range"), Garrison->TryBook(U3, StructureA, 0, 20, Dup));
	TestEqual(TEXT("freed range reused from block base"), Dup.SlotBase, 0);

	// -- Lease exclusivity. --
	TestTrue(TEXT("lease acquire"), Garrison->TryAcquireEntryLease(StructureA, 0, U2));
	TestFalse(TEXT("lease exclusive"), Garrison->TryAcquireEntryLease(StructureA, 0, U3));
	TestTrue(TEXT("re-acquire by holder idempotent"), Garrison->TryAcquireEntryLease(StructureA, 0, U2));
	TestTrue(TEXT("other entrance independent"), Garrison->TryAcquireEntryLease(StructureA, 1, U3));
	Garrison->ReleaseEntryLease(StructureA, 0, U2);
	TestTrue(TEXT("lease freed"), Garrison->IsEntryLeaseFree(StructureA, 0));
	Garrison->ReleaseEntryLease(StructureA, 1, U3);

	// -- Queue-wait slots: deterministic smallest-free assignment. --
	TestEqual(TEXT("queue slot 0"), Garrison->RegisterQueueWait(U1, StructureA, 0), 0);
	TestEqual(TEXT("queue slot 1"), Garrison->RegisterQueueWait(U2, StructureA, 0), 1);
	Garrison->UnregisterQueueWait(U1);
	TestEqual(TEXT("freed queue slot reused"), Garrison->RegisterQueueWait(U3, StructureA, 0), 0);
	Garrison->UnregisterQueueWait(U2);
	Garrison->UnregisterQueueWait(U3);

	// -- State hash: stability under no mutation + garrison sensitivity. --
	const uint64 Hash1 = FBattleStateHash::Compute(World);
	const uint64 Hash2 = FBattleStateHash::Compute(World);
	TestEqual(TEXT("identical sim -> identical hash (R-G6)"), Hash1, Hash2);

	// Spawn two real units so the garrison terms travel the sorted-handle pass,
	// then flip one unit's garrison terms and require the hash to move.
	TArray<FEduUnit> Rows;
	if (!TestTrue(TEXT("edu fixture"), LoadEduFixture(Rows)) || Rows.Num() < 1)
	{
		World->DestroyWorld(false);
		return false;
	}
	FUnitSpawnSpec Spec;
	Spec.EduRowIndex = 0;
	Spec.SoldierCount = 10;
	Spec.UnitName = TEXT("GarrisonHash");
	Spec.SpawnPos = FVector2D(1000.f, 1000.f);
	FBattleSpawnResult SpawnResult;
	FString Error;
	if (!TestTrue(TEXT("spawn"), UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[0], SpawnResult, Error)))
	{
		AddError(Error);
		World->DestroyWorld(false);
		return false;
	}
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(SpawnResult.Unit);
	if (!TestNotNull(TEXT("roster"), Roster) || Roster->Num() == 0)
	{
		World->DestroyWorld(false);
		return false;
	}
	FMassEntityManager& EM = Entities->GetMutableEntityManager();
	FUnitFormationSharedFragment* Formation =
		EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]);
	if (!TestNotNull(TEXT("formation"), Formation))
	{
		World->DestroyWorld(false);
		return false;
	}
	const uint64 HashField = FBattleStateHash::Compute(World);
	Formation->GarrisonMode = EGarrisonMode::Garrisoned;
	Formation->GarrisonStructureId = StructureA;
	Formation->GarrisonPhase = EGarrisonPhase::Settled;
	Formation->GarrisonSlotBase = 5;
	Formation->GarrisonSlotCount = 10;
	const uint64 HashGarrisoned = FBattleStateHash::Compute(World);
	TestNotEqual(TEXT("garrison mutation changes the hash (R-G6)"), HashField, HashGarrisoned);
	Formation->GarrisonMode = EGarrisonMode::None;
	Formation->GarrisonStructureId = 0;
	Formation->GarrisonPhase = EGarrisonPhase::None;
	Formation->GarrisonSlotBase = 0;
	Formation->GarrisonSlotCount = 0;
	TestEqual(TEXT("hash returns on revert"), FBattleStateHash::Compute(World), HashField);

	// -- Archetype invariance (F11/A1): the extended shared fragment must not
	//    split the battle composition; both spawned units sit in ONE archetype. --
	FUnitSpawnSpec Spec2 = Spec;
	Spec2.SpawnPos = FVector2D(5000.f, 1000.f);
	FBattleSpawnResult SpawnResult2;
	if (!TestTrue(TEXT("spawn 2"), UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec2, Rows[0], SpawnResult2, Error)))
	{
		AddError(Error);
		World->DestroyWorld(false);
		return false;
	}
	TSet<FMassArchetypeHandle> Archetypes;
	for (const FUnitHandle* Handle : { &SpawnResult.Unit, &SpawnResult2.Unit })
	{
		if (const TArray<FMassEntityHandle>* R = Registry->FindRoster(*Handle))
		{
			for (const FMassEntityHandle& E : *R)
			{
				Archetypes.Add(EM.GetArchetypeForEntity(E));
			}
		}
	}
	TestEqual(TEXT("one battle archetype with extended fragment (no garrison-induced split)"), Archetypes.Num(), 1);

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// TG.5 ZModel (task 3)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_ZModel,
	"Battle.Unit.Garrison.ZModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_ZModel::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.5 Battle.Unit.Garrison.ZModel (task 3)"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
