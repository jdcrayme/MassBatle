// Battle.Unit.Slots.* -- gate T1.1 (arch §8.4): assignment is a bijection;
// <= k-file displacement; predictive swap terminates; batched reflow
// idempotent. Ground rule 2: one cm/m conversion case; ground rule 5: seeded
// positions via the named stream "Slots.Gate".
#include "Misc/AutomationTest.h"

#include "BattleSlotAssigner.h"
#include "BattleRngRegistry.h"

#include "Engine/World.h"

namespace
{
	UWorld* MakeSlotTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_AssignmentBijection,
	"Battle.Unit.Slots.AssignmentBijection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_AssignmentBijection::RunTest(const FString& Parameters)
{
	UWorld* World = MakeSlotTestWorld();
	UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
	if (!TestNotNull(TEXT("rng"), Rng))
	{
		World->DestroyWorld(false);
		return false;
	}
	Rng->Seed(20261026);
	FRandomStream& Stream = Rng->GetStream(TEXT("Slots.Gate"));

	// 40 entities scattered around a 20x2 slot grid; every assignment must be
	// a bijection (each slot used exactly once).
	const int32 NumFiles = 20;
	const int32 NumSlots = 40;
	TArray<FVector2D> SlotPos;
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		SlotPos.Add(FVector2D(Slot % NumFiles, Slot / NumFiles) * 1.2f);
	}

	for (int32 Trial = 0; Trial < 8; ++Trial)
	{
		TArray<FVector2D> EntityPos;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			EntityPos.Add(FVector2D(Stream.FRandRange(-5.f, 25.f), Stream.FRandRange(-3.f, 5.f)));
		}
		TArray<int32> Out;
		FSlotAssigner::Assign(EntityPos, SlotPos, NumFiles, {}, 2, Out);

		TestEqual(FString::Printf(TEXT("trial %d: one assignment per entity"), Trial), Out.Num(), NumSlots);
		TSet<int32> Used;
		bool bBijection = true;
		for (int32 Slot : Out)
		{
			bBijection = bBijection && Slot >= 0 && Slot < NumSlots && !Used.Contains(Slot);
			Used.Add(Slot);
		}
		TestTrue(FString::Printf(TEXT("trial %d: bijection over slots"), Trial), bBijection && Used.Num() == NumSlots);

		// Locality: the greedy fill must beat (or tie) a random assignment on
		// total bid distance.
		double Total = 0.0;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Total += FVector2D::Distance(EntityPos[E], SlotPos[Out[E]]);
		}
		double Identity = 0.0;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Identity += FVector2D::Distance(EntityPos[E], SlotPos[E]);
		}
		TestTrue(FString::Printf(TEXT("trial %d: locality fill %.1f <= identity %.1f"), Trial, Total, Identity),
			Total <= Identity + 1e-3);
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_FileDisplacementBound,
	"Battle.Unit.Slots.FileDisplacementBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_FileDisplacementBound::RunTest(const FString& Parameters)
{
	// A survivor 6 files right of the killed slot must NOT trek; the bound
	// holds on every entity while a bijection still exists.
	const int32 NumFiles = 10;
	const int32 NumSlots = 20;
	TArray<FVector2D> SlotPos;
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		SlotPos.Add(FVector2D(Slot % NumFiles, Slot / NumFiles) * 1.2f);
	}

	// Everyone stands on their (identity) slot except entity 0, whose slot
	// (0,0) was just vacated -- it stands near file 6's column.
	TArray<FVector2D> EntityPos;
	for (int32 E = 0; E < NumSlots; ++E)
	{
		EntityPos.Add(SlotPos[E]);
	}
	const int32 VacatedSlot = 0;
	const int32 DistantFile = 6;
	EntityPos[0] = SlotPos[DistantFile]; // the survivor is way off-column

	TArray<int32> Previous;
	for (int32 E = 0; E < NumSlots; ++E)
	{
		Previous.Add(E);
	}

	TArray<int32> Out;
	FSlotAssigner::Assign(EntityPos, SlotPos, NumFiles, Previous, 2, Out);

	// The far-from-home entity cannot claim file 6 (bound): it must land
	// within 2 files of its previous file 0.
	TestTrue(TEXT("displacement bound respected"),
		FMath::Abs(Out[0] % NumFiles - Previous[0] % NumFiles) <= 2);
	// And the rest of the unit re-derives a bijection.
	TSet<int32> Used;
	for (int32 Slot : Out)
	{
		Used.Add(Slot);
	}
	TestEqual(TEXT("bijection preserved under the bound"), Used.Num(), NumSlots);

	// cm/m conversion case (ground rule 2): positions in cm convert to m; the
	// nearest-slot choice is conversion-invariant.
	TArray<FVector2D> SlotPosCm;
	for (const FVector2D& P : SlotPos)
	{
		SlotPosCm.Add(P * 100.f);
	}
	TArray<FVector2D> EntityPosCm;
	for (const FVector2D& P : EntityPos)
	{
		EntityPosCm.Add(P * 100.f);
	}
	for (FVector2D& P : SlotPosCm) { P *= 0.01f; }
	for (FVector2D& P : EntityPosCm) { P *= 0.01f; }
	TArray<int32> OutCm;
	FSlotAssigner::Assign(EntityPosCm, SlotPosCm, NumFiles, Previous, 2, OutCm);
	bool bSame = true;
	for (int32 E = 0; E < NumSlots; ++E)
	{
		bSame = bSame && OutCm[E] == Out[E];
	}
	TestTrue(TEXT("cm->m conversion yields identical assignment"), bSame);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_PredictiveSwapTerminates,
	"Battle.Unit.Slots.PredictiveSwapTerminates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_PredictiveSwapTerminates::RunTest(const FString& Parameters)
{
	// Two entities converging head-on, each nearer the OTHER's slot (the
	// 2-opt precondition): projections come within threshold within 1 s ->
	// one swap, each slot still used exactly once. (Head-on convergence never
	// strictly intersects -- the threshold check is the detector under test.)
	TArray<FVector2D> Positions = { FVector2D(4.f, 0.f), FVector2D(1.f, 0.f) };
	TArray<FVector2D> Velocities = { FVector2D(-3.f, 0.f), FVector2D(3.f, 0.f) };
	TArray<int32> Assignment = { 0, 1 };
	TArray<FVector2D> Slots = { FVector2D(0.f, 0.f), FVector2D(5.f, 0.f) };

	TArray<FIntPoint> Pairs;
	FSlotAssigner::FindCrossingPairs(Positions, Velocities, Assignment, Slots, 1.0f, 0.5f, Pairs);
	TestEqual(TEXT("converging pair detected"), Pairs.Num(), 1);

	FSlotAssigner::ApplyPredictiveSwaps(Assignment, Pairs);
	TestEqual(TEXT("swapped: entity 0 takes slot 1"), Assignment[0], 1);
	TestEqual(TEXT("swapped: entity 1 takes slot 0"), Assignment[1], 0);

		// Non-crossing neighbors (moving apart, each on their own slot) must
		// NOT pair up -- neither proximity nor 2-opt holds.
		TArray<FVector2D> Apart = { FVector2D(0.f, 0.f), FVector2D(5.f, 0.f) };
		TArray<FVector2D> Away = { FVector2D(-3.f, 0.f), FVector2D(3.f, 0.f) };
		TArray<FIntPoint> NoPairs;
		FSlotAssigner::FindCrossingPairs(Apart, Away, Assignment, Slots, 1.0f, 0.5f, NoPairs);
		TestEqual(TEXT("diverging pair not detected"), NoPairs.Num(), 0);

		// 2-opt guard: a converging pair already on their own slots (swap
		// would LENGTHEN both walks) is rejected even though the paths cross
		// (thrash prevention). Fresh assignment -- the block above swapped it.
		TArray<FVector2D> OwnSlots = { FVector2D(0.f, 0.f), FVector2D(5.f, 0.f) };
		TArray<int32> OwnAssignment = { 0, 1 };
		TArray<FIntPoint> NoSwapPairs;
		FSlotAssigner::FindCrossingPairs({ FVector2D(0.f, 0.f), FVector2D(5.f, 0.f) },
			{ FVector2D(3.f, 0.f), FVector2D(-3.f, 0.f) }, OwnAssignment, OwnSlots, 1.0f, 0.5f, NoSwapPairs);
		TestEqual(TEXT("non-improving swap rejected (2-opt guard)"), NoSwapPairs.Num(), 0);

		// Termination: 300 entities, random crossings, one pass -- runs and
		// keeps the assignment a bijection.
		UWorld* World = MakeSlotTestWorld();
		UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
		if (TestNotNull(TEXT("rng"), Rng))
		{
			Rng->Seed(20261027);
			FRandomStream& Stream = Rng->GetStream(TEXT("Slots.Gate"));
			const int32 N = 300;
			TArray<FVector2D> P, V, S;
			TArray<int32> A;
			for (int32 i = 0; i < N; ++i)
			{
				P.Add(FVector2D(Stream.FRandRange(-50.f, 50.f), Stream.FRandRange(-50.f, 50.f)));
				V.Add(FVector2D(Stream.FRandRange(-2.f, 2.f), Stream.FRandRange(-2.f, 2.f)));
				S.Add(FVector2D(Stream.FRandRange(-50.f, 50.f), Stream.FRandRange(-50.f, 50.f)));
				A.Add(i);
			}
			TArray<FIntPoint> ManyPairs;
			FSlotAssigner::FindCrossingPairs(P, V, A, S, 1.0f, 0.5f, ManyPairs);
			FSlotAssigner::ApplyPredictiveSwaps(A, ManyPairs);
			TSet<int32> Used;
			for (int32 Slot : A)
			{
				Used.Add(Slot);
			}
			TestEqual(TEXT("one pass over many pairs keeps the bijection"), Used.Num(), N);
		}
		World->DestroyWorld(false);
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_BatchedReflowIdempotent,
	"Battle.Unit.Slots.BatchedReflowIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_BatchedReflowIdempotent::RunTest(const FString& Parameters)
{
	// The scheduler reflows at most once per 0.5 s. Cooldown 0 = due now:
	// the first consume fires, then batching suppresses until the interval
	// elapses (idempotent: repeated calls inside the window do nothing).
	FReflowScheduler Scheduler;
	Scheduler.IntervalSeconds = 0.5f;

	TestTrue(TEXT("fresh scheduler is due"), Scheduler.ConsumeIfDue(0.1f, false));
	TestFalse(TEXT("inside cooldown: suppressed"), Scheduler.ConsumeIfDue(0.1f, false));
	TestTrue(TEXT("interval elapsed: fires again"), Scheduler.ConsumeIfDue(0.4f, false));
	TestFalse(TEXT("cooldown restarted after firing (idempotent)"), Scheduler.ConsumeIfDue(0.1f, false));
	TestFalse(TEXT("still cooling"), Scheduler.ConsumeIfDue(0.25f, false));
	TestTrue(TEXT("next interval fires"), Scheduler.ConsumeIfDue(0.3f, false));

	// Rank gap short-circuits a running cooldown.
	FReflowScheduler GapScheduler;
	GapScheduler.Cooldown = 0.3f;
	TestFalse(TEXT("no gap: timer holds"), GapScheduler.ConsumeIfDue(0.016f, false));
	TestTrue(TEXT("rank gap fires immediately despite cooldown"), GapScheduler.ConsumeIfDue(0.016f, true));
	TestFalse(TEXT("...and the cooldown applies after"), GapScheduler.ConsumeIfDue(0.016f, false));

	return true;
}
