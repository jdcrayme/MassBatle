#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Garrison.* -- phase-1 garrison gate family TG.1-TG.5 (brief §6).
// Task 0 shipped these as fail-by-design skeletons (gate-first discipline);
// each body lands with its owning task (or earlier, per §5 gate-first rule).
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "BattleGarrisonTypes.h"
#include "BattleGarrison.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleCostGrid.h"
#include "BattleCostGridSubsystem.h"
#include "BattlePathPlanner.h"
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
}

// ---------------------------------------------------------------------------
// TG.1 Stamping (task 2): footprint impassable, corridor clear +
// high-but-passable, ResetToFlat clears, deterministic stamp counts.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrison_Stamping,
	"Battle.Unit.Garrison.Stamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrison_Stamping::RunTest(const FString& Parameters)
{
	UWorld* World = MakeGarrisonTestWorld();
	UBattleGarrisonSubsystem* Garrison = World->GetSubsystem<UBattleGarrisonSubsystem>();
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	if (!TestNotNull(TEXT("garrison"), Garrison) || !TestNotNull(TEXT("cost grid"), CostGrid))
	{
		World->DestroyWorld(false);
		return false;
	}

	FGarrisonStructureRecord Rec;
	TestTrue(TEXT("building fixture builds"), FGarrisonLayout::MakeFixture(EGarrisonStructureType::Building,
		FVector2D(30.f, 30.f), 0.f, Rec));

	// Footprint impassable at samples across the body.
	CostGrid->ResetToFlat();
	const int32 FirstStamp = FGarrisonLayout::StampStructure(Rec, CostGrid->GetMutableGrid());
	TestTrue(TEXT("stamped some cells"), FirstStamp > 0);
	const FVector2D BodySamples[] = {
		FVector2D(30.f, 30.f), FVector2D(12.f, 24.f), FVector2D(48.f, 36.f), FVector2D(30.f, 38.f)
	};
	for (const FVector2D& S : BodySamples)
	{
		int32 X, Y;
		CostGrid->GetGrid().WorldToCell(S, X, Y);
		TestTrue(FString::Printf(TEXT("footprint impassable at (%.0f,%.0f)"), S.X, S.Y),
			CostGrid->GetGrid().IsImpassable(X, Y));
	}

	// Entrance corridor: SegmentIsClear QueuePos->DoorPos holds and the
	// corridor cells carry the high-but-passable cost.
	const FGarrisonEntrance& E = Rec.Entrances[0];
	TestTrue(TEXT("corridor segment clear (QueuePos->DoorPos)"),
		FBattlePathPlanner::SegmentIsClear(CostGrid->GetGrid(), E.QueuePos, E.DoorPos, 0.1f));
	{
		int32 X, Y;
		const FVector2D Mid((E.QueuePos + E.DoorPos) * 0.5f);
		CostGrid->GetGrid().WorldToCell(Mid, X, Y);
		TestEqual(TEXT("corridor cost is the garrison mirror"),
			CostGrid->GetGrid().GetCost(X, Y), GarrisonCorridorCost);
	}

	// ResetToFlat clears the stamps.
	CostGrid->ResetToFlat();
	{
		int32 X, Y;
		CostGrid->GetGrid().WorldToCell(FVector2D(30.f, 30.f), X, Y);
		TestFalse(TEXT("ResetToFlat clears the footprint"), CostGrid->GetGrid().IsImpassable(X, Y));
	}

	// Determinism + state idempotence: two fresh grids -> identical stamp
	// counts; restamping leaves the final cell state byte-identical (corridor
	// boxes stamp last over the footprint, so the final state is stable even
	// though the overlap cells flip each pass).
	FBattleCostGrid GridA, GridB;
	GridA.InitFlat(64, 64, FVector2D::ZeroVector, 3.f);
	GridB.InitFlat(64, 64, FVector2D::ZeroVector, 3.f);
	const int32 CountA = FGarrisonLayout::StampStructure(Rec, GridA);
	const int32 CountB = FGarrisonLayout::StampStructure(Rec, GridB);
	TestEqual(TEXT("deterministic stamps"), CountA, CountB);
	const TArray<uint8> StateAfterFirst = GridA.CellCost;
	FGarrisonLayout::StampStructure(Rec, GridA);
	TestEqual(TEXT("restamp leaves the grid state identical"), GridA.CellCost == StateAfterFirst, true);

	// Registration-side stamping (the subsystem path) leaves the planner able
	// to route to the queue point: register the fixture and re-check.
	CostGrid->ResetToFlat();
	const uint32 Id = Garrison->RegisterStructure(Rec);
	TestTrue(TEXT("registered"), Id != 0);
	TestTrue(TEXT("post-registration corridor clear"),
		FBattlePathPlanner::SegmentIsClear(CostGrid->GetGrid(), E.QueuePos, E.DoorPos, 0.1f));

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// TG.2 InternalPath (task 3): door passage endpoints, arc-length monotonicity,
// Z monotone on wall/trench fixtures, egress = reversed walk to the assembly.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrison_InternalPath,
	"Battle.Unit.Garrison.InternalPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrison_InternalPath::RunTest(const FString& Parameters)
{
	const EGarrisonStructureType Types[3] = {
		EGarrisonStructureType::Building, EGarrisonStructureType::Wall, EGarrisonStructureType::Trench
	};
	for (const EGarrisonStructureType Type : Types)
	{
		FGarrisonStructureRecord Rec;
		const bool bBuilt = FGarrisonLayout::MakeFixture(Type, FVector2D(0.f, 0.f), 0.f, Rec);
		AddInfo(FString::Printf(TEXT("type=%d built=%d entrances=%d slots=%d maxunits=%d"),
			static_cast<int32>(Type), bBuilt ? 1 : 0, Rec.Entrances.Num(), Rec.Slots.Num(), Rec.MaxUnits));
		if (!TestTrue(TEXT("fixture builds"), bBuilt)
			|| !TestEqual(TEXT("entrances present"), Rec.Entrances.Num(), 1))
		{
			return false;
		}
		const FGarrisonEntrance& E = Rec.Entrances[0];

		TArray<FBattlePathPoint3> Entry;
		if (!TestTrue(TEXT("entry path builds"), FGarrisonLayout::BuildInternalPath(Rec, 0, false, Entry)))
		{
			return false;
		}
		// Endpoints exact; passes through the door within a sample step.
		AddInfo(FString::Printf(TEXT("type=%d points=%d length=%.1f"),
			static_cast<int32>(Type), Entry.Num(), Entry.Last().S));
		TestEqual(TEXT("entry starts at QueuePos"), Entry[0].Pos.Equals(E.QueuePos, 1e-4f), true);
		TestEqual(TEXT("entry ends at distribution node"), Entry.Last().Pos.Equals(E.DistributionPos, 1e-4f), true);
		float MinDoorDist = FLT_MAX;
		for (const FBattlePathPoint3& P : Entry)
		{
			MinDoorDist = FMath::Min(MinDoorDist, FVector2D::Distance(P.Pos, E.DoorPos));
		}
		TestTrue(TEXT("entry passes through the door"), MinDoorDist <= 1.01f);

		// Arc-length monotonicity.
		bool bMonotone = true;
		for (int32 i = 1; i < Entry.Num(); ++i)
		{
			bMonotone = bMonotone && Entry[i].S > Entry[i - 1].S;
		}
		TestTrue(TEXT("entry arc-length monotone"), bMonotone);

		// Z monotone in the entry direction: wall climbs, trench sinks,
		// building stays flat (brief §5 task 3: "Z monotone on stair/ramp
		// fixtures" -- monotone in the fixture's intended direction).
		const bool bSinks = Type == EGarrisonStructureType::Trench;
		bool bZMonotone = true;
		for (int32 i = 1; i < Entry.Num(); ++i)
		{
			bZMonotone = bZMonotone && (bSinks
				? Entry[i].Z <= Entry[i - 1].Z + KINDA_SMALL_NUMBER
				: Entry[i].Z >= Entry[i - 1].Z - KINDA_SMALL_NUMBER);
		}
		TestTrue(TEXT("entry Z monotone in the fixture direction"), bZMonotone);
		if (Type == EGarrisonStructureType::Wall)
		{
			TestEqual(TEXT("wall entry Z ends at walkway height"), FMath::RoundToInt(Entry.Last().Z * 100.f), 600);
		}
		if (Type == EGarrisonStructureType::Trench)
		{
			TestEqual(TEXT("trench entry Z ends at channel depth"), FMath::RoundToInt(Entry.Last().Z * 100.f), -75);
		}

		// Egress: endpoints exact (node -> door -> assembly), Z reverses.
		TArray<FBattlePathPoint3> Egress;
		if (!TestTrue(TEXT("egress path builds"), FGarrisonLayout::BuildInternalPath(Rec, 0, true, Egress)))
		{
			return false;
		}
		TestEqual(TEXT("egress starts at node"), Egress[0].Pos.Equals(E.DistributionPos, 1e-4f), true);
		TestEqual(TEXT("egress ends at assembly"), Egress.Last().Pos.Equals(E.AssemblyPos, 1e-4f), true);
		bool bZReversed = true;
		for (int32 i = 1; i < Egress.Num(); ++i)
		{
			// Egress reverses the elevation walk: the wall descends, the
			// trench climbs back out.
			bZReversed = bZReversed && (bSinks
				? Egress[i].Z >= Egress[i - 1].Z - KINDA_SMALL_NUMBER
				: Egress[i].Z <= Egress[i - 1].Z + KINDA_SMALL_NUMBER);
		}
		TestTrue(TEXT("egress Z monotone back toward ground"), bZReversed);
		bool bEgressMonotone = true;
		for (int32 i = 1; i < Egress.Num(); ++i)
		{
			bEgressMonotone = bEgressMonotone && Egress[i].S > Egress[i - 1].S;
		}
		TestTrue(TEXT("egress arc-length monotone"), bEgressMonotone);
	}
	return true;
}

// ---------------------------------------------------------------------------
// TG.3 SlotAssign (task 3): bijection, distance bound, determinism, disjoint
// booking ranges stay separate assignment domains.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrison_SlotAssign,
	"Battle.Unit.Garrison.SlotAssign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrison_SlotAssign::RunTest(const FString& Parameters)
{
	// 20 slots along a line, 20 soldiers near each slot with a known shuffle.
	TArray<FVector2D> Slots;
	for (int32 i = 0; i < 20; ++i)
	{
		Slots.Add(FVector2D(1.2f * i, 0.f));
	}
	TArray<FVector2D> Soldiers;
	TArray<int32> Truth;
	for (int32 i = 0; i < 20; ++i)
	{
		const int32 From = (i * 7 + 3) % 20; // a fixed permutation
		Soldiers.Add(Slots[From] + FVector2D(0.1f, 0.2f));
		Truth.Add(From);
	}

	// First dress (no previous): deterministic bijection.
	TArray<int32> A1, A2;
	FGarrisonLayout::AssignSlots(Soldiers, Slots, TArray<int32>(), 3.f, A1);
	FGarrisonLayout::AssignSlots(Soldiers, Slots, TArray<int32>(), 3.f, A2);
	TSet<int32> Used;
	bool bBijective = true;
	for (int32 i = 0; i < 20; ++i)
	{
		bBijective = bBijective && A1[i] != INDEX_NONE && !Used.Contains(A1[i]);
		Used.Add(A1[i]);
	}
	TestEqual(TEXT("bijection over the slot set"), Used.Num(), 20);
	TestTrue(TEXT("every soldier assigned"), bBijective);
	TestEqual(TEXT("deterministic under repeat"), A1 == A2, true);

	// Each soldier got a NEAR slot (the permutation is recoverable within eps).
	bool bNear = true;
	for (int32 i = 0; i < 20; ++i)
	{
		bNear = bNear && A1[i] == Truth[i];
	}
	TestTrue(TEXT("greedy nearest recovers the permutation"), bNear);

	// Distance bound: previous assignment shuffled far; small bound must hold
	// on the second dress (bounded pass) wherever a bounded vacancy exists.
	TArray<int32> Prev;
	for (int32 i = 0; i < 20; ++i)
	{
		Prev.Add((i + 10) % 20); // every soldier 10 slots away from its old post
	}
	TArray<FVector2D> ShiftedSoldiers; // soldiers stand at their PREVIOUS posts
	for (int32 i = 0; i < 20; ++i)
	{
		ShiftedSoldiers.Add(Slots[Prev[i]]);
	}
	TArray<int32> Bounded;
	FGarrisonLayout::AssignSlots(ShiftedSoldiers, Slots, Prev, 0.5f, Bounded);
	bool bBoundRespectedOrFallback = true;
	for (int32 i = 0; i < 20; ++i)
	{
		const float FromPrev = FVector2D::Distance(Slots[Bounded[i]], Slots[Prev[i]]);
		// Bound respected OR the unbounded fallback fired (soldier kept a
		// nearest vacancy because the bounded pass found none).
		bBoundRespectedOrFallback = bBoundRespectedOrFallback && (FromPrev <= 0.5f || Bounded[i] == INDEX_NONE);
	}
	TSet<int32> UsedB;
	for (int32 i = 0; i < 20; ++i)
	{
		UsedB.Add(Bounded[i]);
	}
	TestEqual(TEXT("bounded second dress still bijective"), UsedB.Num(), 20);
	TestTrue(TEXT("bound respected or fallback"), bBoundRespectedOrFallback);

	// Disjoint booking ranges: assignments computed over one range never land
	// in the other (two units, one structure).
	FGarrisonStructureRecord Rec;
	if (!TestTrue(TEXT("ledger fixture"), FGarrisonLayout::MakeFixture(EGarrisonStructureType::Building, FVector2D::ZeroVector, 0.f, Rec))
		|| !TestEqual(TEXT("ledger fixture slots"), Rec.Slots.Num(), 80))
	{
		return false;
	}
	TArray<FVector2D> RangeA, RangeB;
	for (int32 i = 0; i < 40; ++i)
	{
		(RangeA.Add(Rec.Slots[i].Pos));
	}
	for (int32 i = 40; i < 80; ++i)
	{
		(RangeB.Add(Rec.Slots[i].Pos));
	}
	TArray<int32> AssignA, AssignB;
	FGarrisonLayout::AssignSlots(RangeA, RangeA, TArray<int32>(), 3.f, AssignA);
	FGarrisonLayout::AssignSlots(RangeB, RangeB, TArray<int32>(), 3.f, AssignB);
	bool bDisjoint = true;
	for (int32 i = 0; i < 40; ++i)
	{
		bDisjoint = bDisjoint && AssignA[i] >= 0 && AssignA[i] < 40 && AssignB[i] >= 0 && AssignB[i] < 40;
	}
	TestTrue(TEXT("disjoint ranges across two booking sets"), bDisjoint);
	return true;
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

	// -- Order surface (task 4): orders reach the registry with serial bumps,
	//    the A9 cavalry guard rejects at issue, A7 parking round-trips. --
	const uint32 SerialBefore = Registry->GetOrderSerial(SpawnResult.Unit);
	TestTrue(TEXT("IssueGarrisonOrder accepted"), Registry->IssueGarrisonOrder(SpawnResult.Unit, StructureA));
	{
		const FUnitOrder* Order = Registry->FindOrder(SpawnResult.Unit);
		if (TestNotNull(TEXT("garrison order present"), Order))
		{
			TestEqual(TEXT("order type Garrison"), static_cast<int32>(Order->Type),
				static_cast<int32>(FUnitOrder::EType::Garrison));
			TestEqual(TEXT("order structure id"), Order->StructureId, StructureA);
		}
		TestEqual(TEXT("serial bumped once"), Registry->GetOrderSerial(SpawnResult.Unit), SerialBefore + 1);
	}
	TestTrue(TEXT("IssueExitGarrisonOrder accepted"), Registry->IssueExitGarrisonOrder(SpawnResult.Unit));
	TestEqual(TEXT("order type ExitGarrison"),
		static_cast<int32>(Registry->FindOrder(SpawnResult.Unit)->Type),
		static_cast<int32>(FUnitOrder::EType::ExitGarrison));

	// A9 guard: a cavalry unit is rejected at issue (no order written).
	const FString CavalryEdu = TEXT("\n")
		TEXT("type Test Cavalry\n")
		TEXT("dictionary Test_Cavalry\n")
		TEXT("category cavalry\n")
		TEXT("class light\n")
		TEXT("soldier Peasant, 40, 0, 1\n")
		TEXT("attributes sea_faring\n")
		TEXT("formation 1.2, 1.2, 2.4, 2.4, 3, square\n")
		TEXT("stat_health 1, 1\n")
		TEXT("stat_pri 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n")
		TEXT("stat_sec 1, 2, no, 0, 0, melee, simple, other, 10, 0.2, 0.2\n")
		TEXT("stat_pri_armour 0, 1, 0, flesh\n")
		TEXT("stat_mental 2, low, untrained\n")
		TEXT("stat_charge_dist 30\n")
		TEXT("stat_cost 1, 100, 90, 40, 60, 90\n")
		TEXT("ownership slave, england\n");
	{
		TArray<FEduUnit> CavRows;
		FEduParseError CavErr;
		if (TestTrue(TEXT("cavalry fixture parses"), FBattleEduParser::Parse(CavalryEdu, CavRows, CavErr)) && CavRows.Num() > 0)
		{
			FUnitSpawnSpec CavSpec;
			CavSpec.EduRowIndex = 0;
			CavSpec.SoldierCount = 10;
			CavSpec.UnitName = TEXT("GarrisonCav");
			CavSpec.SpawnPos = FVector2D(9000.f, 1000.f);
			FBattleSpawnResult CavResult;
			FString CavError;
			if (TestTrue(TEXT("cavalry spawn"), UBattleSpawnLibrary::SpawnUnitFromEduRow(World, CavSpec, CavRows[0], CavResult, CavError)))
			{
				const uint32 CavSerial = Registry->GetOrderSerial(CavResult.Unit);
				TestFalse(TEXT("cavalry garrison rejected at issue (A9)"),
					Registry->IssueGarrisonOrder(CavResult.Unit, StructureA));
				TestEqual(TEXT("no order written for cavalry"), Registry->GetOrderSerial(CavResult.Unit), CavSerial);
			}
		}
	}

	// A7 parking: a parked order survives and resumes with a serial bump.
	{
		FUnitOrder Parked;
		Parked.Type = FUnitOrder::EType::Move;
		Parked.AnchorPos = FVector2D(12300.f, 4500.f);
		Registry->ParkPendingAfterEgress(SpawnResult.Unit, Parked);
		FUnitOrder Popped;
		TestTrue(TEXT("parked order resumes"), Registry->ConsumePendingAfterEgress(SpawnResult.Unit, Popped));
		TestEqual(TEXT("parked order type"), static_cast<int32>(Popped.Type), static_cast<int32>(FUnitOrder::EType::Move));
		TestEqual(TEXT("parked order anchor"), Popped.AnchorPos, FVector2D(12300.f, 4500.f));
		TestEqual(TEXT("resume set CurrentOrder"),
			static_cast<int32>(Registry->FindOrder(SpawnResult.Unit)->Type), static_cast<int32>(FUnitOrder::EType::Move));
		FUnitOrder Second;
		TestFalse(TEXT("second consume finds nothing"), Registry->ConsumePendingAfterEgress(SpawnResult.Unit, Second));
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// TG.5 ZModel (task 3): Z interpolation monotone + slot-Z fixtures
// (wall 6 m / trench -0.75 m / building floor 0), fragment-boundary twin.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrison_ZModel,
	"Battle.Unit.Garrison.ZModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrison_ZModel::RunTest(const FString& Parameters)
{
	FGarrisonStructureRecord Wall;
	TestTrue(TEXT("wall fixture"), FGarrisonLayout::MakeFixture(EGarrisonStructureType::Wall, FVector2D::ZeroVector, 0.f, Wall));
	TArray<FBattlePathPoint3> Path;
	TestTrue(TEXT("wall entry path"), FGarrisonLayout::BuildInternalPath(Wall, 0, false, Path));

	// Z interpolation monotone across the whole climb.
	bool bMonotone = true;
	for (int32 i = 1; i < Path.Num(); ++i)
	{
		const float S = (Path[i].S + Path[i - 1].S) * 0.5f;
		bMonotone = bMonotone && FGarrisonLayout::ZAtS(Path, S) >= FGarrisonLayout::ZAtS(Path, Path[i - 1].S) - 1e-4f;
	}
	TestTrue(TEXT("ZAtS monotone along the stair fixture"), bMonotone);
	TestEqual(TEXT("ZAtS(0) is ground"), FMath::RoundToInt(FGarrisonLayout::ZAtS(Path, 0.f) * 100.f), 0);
	TestEqual(TEXT("ZAtS(total) is walkway"), FMath::RoundToInt(FGarrisonLayout::ZAtS(Path, Path.Last().S) * 100.f), 600);
	// Stair midpoint: door sits at the stair base; halfway along the path the
	// interpolation is between 0 and 6 (linear per segment).
	const float HalfZ = FGarrisonLayout::ZAtS(Path, Path.Last().S * 0.5f);
	TestTrue(TEXT("midpath Z between ground and walkway"), HalfZ > 0.5f && HalfZ < 5.5f);

	// Slot-Z fixtures.
	FGarrisonStructureRecord Trench;
	FGarrisonStructureRecord Building;
	TestTrue(TEXT("trench fixture"), FGarrisonLayout::MakeFixture(EGarrisonStructureType::Trench, FVector2D::ZeroVector, 0.f, Trench));
	TestTrue(TEXT("building fixture"), FGarrisonLayout::MakeFixture(EGarrisonStructureType::Building, FVector2D::ZeroVector, 0.f, Building));
	bool bWallZ = true, bTrenchZ = true, bBuildingZ = true;
	for (const FGarrisonSlot& S : Wall.Slots)
	{
		bWallZ = bWallZ && FMath::IsNearlyEqual(S.Z, 6.f);
	}
	for (const FGarrisonSlot& S : Trench.Slots)
	{
		bTrenchZ = bTrenchZ && FMath::IsNearlyEqual(S.Z, -0.75f);
	}
	for (const FGarrisonSlot& S : Building.Slots)
	{
		bBuildingZ = bBuildingZ && FMath::IsNearlyEqual(S.Z, 0.f);
	}
	TestTrue(TEXT("wall slots at walkway Z"), bWallZ);
	TestTrue(TEXT("trench slots at channel Z"), bTrenchZ);
	TestTrue(TEXT("building slots at floor Z"), bBuildingZ);

	// Fragment-boundary twin (cm): two-point climb 0 -> 600 cm.
	FBattlePathPolyline Frag;
	Frag.NumPoints = 2;
	Frag.Points[0].Pos = FVector2D::ZeroVector;
	Frag.Points[0].S = 0.f;
	Frag.Points[0].Z = 0.f;
	Frag.Points[1].Pos = FVector2D(600.f, 0.f);
	Frag.Points[1].S = 600.f;
	Frag.Points[1].Z = 600.f;
	Frag.TotalLength = 600.f;
	TestEqual(TEXT("fragment Z interpolation at 300 cm"),
		FMath::RoundToInt(FGarrisonLayout::ZAtSFragment(Frag, 300.f)), 300);
	TestEqual(TEXT("fragment Z clamps past the end"),
		FMath::RoundToInt(FGarrisonLayout::ZAtSFragment(Frag, 1000.f)), 600);

	// Column clamp (spec §4.3): 4 m declared width / 1.2 m spacing -> 3 files.
	TestEqual(TEXT("ColumnFilesFor(4, 1.2) = 3"), FGarrisonLayout::ColumnFilesFor(4.f, 1.2f, 40), 3);
	TestEqual(TEXT("ColumnFilesFor clamps to 1"), FGarrisonLayout::ColumnFilesFor(0.5f, 1.2f, 40), 1);
	TestEqual(TEXT("ColumnFilesFor clamps to files"), FGarrisonLayout::ColumnFilesFor(99.f, 1.2f, 6), 6);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
