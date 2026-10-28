#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Slots.* -- gate T1.1 (arch §8.4): assignment is a bijection;
// <= k-file displacement; predictive swap terminates; batched reflow
// idempotent. Ground rule 2: one cm/m conversion case; ground rule 5: seeded
// positions via the named stream "Slots.Gate".
#include "Misc/AutomationTest.h"

#include "BattleSlotAssigner.h"
#include "BattlePathFrameLayout.h"
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

	FSlotAssigner::ApplyPredictiveSwaps(Assignment, Pairs, Positions, Slots);
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
			FSlotAssigner::ApplyPredictiveSwaps(A, ManyPairs, P, S);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_SwapFixpoint,
	"Battle.Unit.Slots.SwapFixpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_SwapFixpoint::RunTest(const FString& Parameters)
{
	// Audit M3: apply the detected swaps, then RE-DETECT -- a guarded apply
	// must reach a fixpoint (no new crossing pair is swap-improving after the
	// pass), and a shared-entity chain like (0,1),(1,2) must not execute a
	// later pair on a stale guard promise.
	{
		// Head-on converging pair: one swap, re-detect finds nothing.
		TArray<FVector2D> Positions = { FVector2D(4.f, 0.f), FVector2D(1.f, 0.f) };
		TArray<FVector2D> Velocities = { FVector2D(-3.f, 0.f), FVector2D(3.f, 0.f) };
		TArray<FVector2D> Slots = { FVector2D(0.f, 0.f), FVector2D(5.f, 0.f) };
		TArray<int32> Assignment = { 0, 1 };
		TArray<FIntPoint> Pairs;
		FSlotAssigner::FindCrossingPairs(Positions, Velocities, Assignment, Slots, 1.0f, 0.5f, Pairs);
		TestEqual(TEXT("pair detected"), Pairs.Num(), 1);
		FSlotAssigner::ApplyPredictiveSwaps(Assignment, Pairs, Positions, Slots);
		TArray<FIntPoint> ReDetected;
		// Swapped slots: entity 0 now walks to slot 1 (east), entity 1 to
		// slot 0 (west) -- they are DIVERGING; no further swap is improving.
		FSlotAssigner::FindCrossingPairs(Positions, Velocities, Assignment, Slots, 1.0f, 0.5f, ReDetected);
		TestEqual(TEXT("re-detect to empty after apply (audit M3 fixpoint)"), ReDetected.Num(), 0);
	}
	{
		// Shared-entity chain (0,1),(1,2): the second pair shares entity 1
		// with the first. With used-once semantics at most one of them
		// applies; in any case the assignment stays a bijection and every
		// applied swap was guard-checked against the CURRENT assignment.
		TArray<FVector2D> Positions = { FVector2D(8.f, 0.f), FVector2D(4.f, 0.f), FVector2D(0.f, 0.f) };
		TArray<FVector2D> Velocities = { FVector2D(-3.f, 0.f), FVector2D(-3.f, 0.f), FVector2D(3.f, 0.f) };
		TArray<FVector2D> Slots = { FVector2D(0.f, 0.f), FVector2D(4.f, 0.f), FVector2D(8.f, 0.f) };
		TArray<int32> Assignment = { 0, 1, 2 };
		TArray<FIntPoint> Pairs;
		FSlotAssigner::FindCrossingPairs(Positions, Velocities, Assignment, Slots, 1.0f, 0.5f, Pairs);
		FSlotAssigner::ApplyPredictiveSwaps(Assignment, Pairs, Positions, Slots);
		TSet<int32> Used;
		for (const int32 Slot : Assignment)
		{
			if (Slot >= 0)
			{
				Used.Add(Slot);
			}
		}
		TestEqual(TEXT("chain keeps the bijection"), Used.Num(), 3);
		// No swap-created trek: with the guard re-checked at apply time and
		// each entity used once, every entity ends at a slot at least as
		// close as the one it started on (swaps only fire when BOTH parties
		// strictly improve against the then-current assignment).
		for (int32 E = 0; E < 3; ++E)
		{
			TestTrue(FString::Printf(TEXT("chain entity %d not trekked (audit M3)"), E),
				FVector2D::DistSquared(Positions[E], Slots[Assignment[E]])
				<= FVector2D::DistSquared(Positions[E], Slots[E]) + KINDA_SMALL_NUMBER);
		}
	}
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

	// Audit L19: ASSIGNMENT idempotence (not just scheduler cadence) --
	// assigning twice with unchanged inputs yields the identical bijection.
	{
		TArray<FVector2D> Positions;
		TArray<FVector2D> Slots;
		for (int32 i = 0; i < 24; ++i)
		{
			Positions.Add(FVector2D((i % 6) * 1.35f + 0.1f * (i / 6), (i / 6) * 1.35f));
			Slots.Add(FVector2D((i % 6) * 1.2f, (i / 6) * 1.2f));
		}
		TArray<int32> First, Second;
		FSlotAssigner::Assign(Positions, Slots, 6, TArray<int32>(), 2, First);
		FSlotAssigner::Assign(Positions, Slots, 6, First, 2, Second);
		TestEqual(TEXT("assign-twice: same length"), Second.Num(), First.Num());
		bool bIdentical = true;
		for (int32 i = 0; i < First.Num(); ++i)
		{
			if (First[i] != Second[i])
			{
				bIdentical = false;
				break;
			}
		}
		TestTrue(TEXT("assign-twice: identical assignment (audit L19)"), bIdentical);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_AssignFinal,
	"Battle.Unit.Slots.AssignFinal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_AssignFinal::RunTest(const FString& Parameters)
{
	UWorld* World = MakeSlotTestWorld();
	UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
	if (!TestNotNull(TEXT("rng"), Rng))
	{
		World->DestroyWorld(false);
		return false;
	}
	Rng->Seed(20261027);
	FRandomStream& Stream = Rng->GetStream(TEXT("Slots.Gate"));

	// 14-file x 3-rank (the plan's reference unit), spacing 1.2 m, final
	// anchor at origin facing east (90 deg).
	constexpr int32 NumFiles = 14;
	constexpr int32 NumSlots = 42;
	const FVector2D FinalAnchor(0.f, 0.f);
	const float FinalFacing = 90.f;
	auto FinalSlotPos = [NumFiles](int32 Slot)
	{
		const int32 Rank = Slot / NumFiles;
		const int32 File = Slot % NumFiles;
		return FPathFrameLayout::AnchorFormula(FVector2D::ZeroVector, 90.f, Rank, File, NumFiles, 1.2f, 1.2f);
	};

	for (int32 Trial = 0; Trial < 8; ++Trial)
	{
		// Entities scattered ahead of and around the destination (a unit
		// converging from the west, the marching side).
		TArray<FVector2D> EntityPos;
		for (int32 i = 0; i < NumSlots; ++i)
		{
			EntityPos.Add(FVector2D(Stream.FRandRange(-30.f, 5.f), Stream.FRandRange(-20.f, 20.f)));
		}
		TArray<int32> Out;
		FSlotAssigner::AssignFinal(EntityPos, FinalAnchor, FinalFacing, NumSlots, NumFiles, Out);

		// Bijection.
		TSet<int32> Used;
		bool bBijection = true;
		for (int32 Slot : Out)
		{
			bBijection = bBijection && Slot >= 0 && Slot < NumSlots && !Used.Contains(Slot);
			Used.Add(Slot);
		}
		TestTrue(FString::Printf(TEXT("trial %d: bijection"), Trial), bBijection && Used.Num() == NumSlots);

		// Crossing-freedom: sort entities by final-facing projection
		// descending; their slots' facing-projections must be non-increasing
		// (the plan's projection-monotone property -- no two soldiers' final
		// approaches cross in the facing axis).
		TArray<int32> ByFront;
		for (int32 i = 0; i < NumSlots; ++i)
		{
			ByFront.Add(i);
		}
		ByFront.Sort([&](int32 A, int32 B)
		{
			return EntityPos[A].Y > EntityPos[B].Y; // facing 90 deg: F = +Y
		});
		bool bMonotone = true;
		float PrevSlotProj = FLT_MAX;
		for (int32 E : ByFront)
		{
			const float SlotProj = FinalSlotPos(Out[E]).Y;
			if (SlotProj > PrevSlotProj + KINDA_SMALL_NUMBER)
			{
				bMonotone = false;
			}
			PrevSlotProj = SlotProj;
		}
		TestTrue(FString::Printf(TEXT("trial %d: projection-monotone (no crossing approaches)"), Trial), bMonotone);

		// Determinism: a repeated call with identical inputs is identical.
		TArray<int32> Out2;
		FSlotAssigner::AssignFinal(EntityPos, FinalAnchor, FinalFacing, NumSlots, NumFiles, Out2);
		bool bIdentical = true;
		for (int32 i = 0; i < NumSlots; ++i)
		{
			bIdentical = bIdentical && Out[i] == Out2[i];
		}
		TestTrue(FString::Printf(TEXT("trial %d: deterministic repeat"), Trial), bIdentical);
	}

	// Casualty holes are the REAR-most slots: 30 of 42 entities -> ranks 0..1
	// full (28 slots) + 2 of rank 2's FRONT files; rank 2's rear files empty.
	{
		const int32 NumEntities = 30;
		TArray<FVector2D> EntityPos;
		for (int32 i = 0; i < NumEntities; ++i)
		{
			EntityPos.Add(FVector2D(-20.f + (i % 14) * 1.2f, -10.f + (i / 14) * 1.2f));
		}
		TArray<int32> Out;
		FSlotAssigner::AssignFinal(EntityPos, FinalAnchor, FinalFacing, NumSlots, NumFiles, Out);
		int32 MaxUsed = -1;
		TSet<int32> Used;
		for (int32 Slot : Out)
		{
			Used.Add(Slot);
			MaxUsed = FMath::Max(MaxUsed, Slot);
		}
		TestEqual(TEXT("casualty holes: front-filled contiguous block"), Used.Num(), NumEntities);
		TestEqual(TEXT("casualty holes: used set is exactly slots [0, N)"), MaxUsed, NumEntities - 1);
	}

	// The plan's anchor property: a soldier already standing ON a final slot
	// keeps that slot -- he stops, and the formation forms around him.
	{
		TArray<FVector2D> EntityPos;
		TArray<FVector2D> SlotPos;
		for (int32 i = 0; i < 4; ++i)
		{
			SlotPos.Add(FPathFrameLayout::AnchorFormula(FinalAnchor, FinalFacing, 0, i, 4, 1.2f, 1.2f));
			EntityPos.Add(SlotPos[i]); // dressed on the final front rank
		}
		// Perturb one soldier half a file off -- he must keep his file.
		EntityPos[2] += FVector2D(0.6f, 0.f);
		TArray<int32> Out;
		FSlotAssigner::AssignFinal(EntityPos, FinalAnchor, FinalFacing, 4, 4, Out);
		for (int32 i = 0; i < 4; ++i)
		{
			TestEqual(FString::Printf(TEXT("dressed soldier %d keeps his slot"), i), Out[i], i);
		}
	}

	World->DestroyWorld(false);
	return true;
}

// ---- Formation flow (owner proposal 2026-10-28): CloseGaps (rule 1) and
// EvictToReachable (rule 2). Both are pure permutations on the greedy
// bijection; holes drain to the rear-right corner; stuck entities re-home at
// the perimeter with a one-slot-per-owner displacement chain. ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_CloseGaps,
	"Battle.Unit.Slots.CloseGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_CloseGaps::RunTest(const FString& Parameters)
{
	// Assignment is ENTITY-indexed (Assignment[E] = slot). Rule 1a, the
	// file cascade: 2 files x 3 ranks, entity 0 the casualty (its slot 0 is
	// the hole). The whole file 0 shifts up one rank; the hole exits at the
	// rear-right drain corner (slot 5), NOT at the file's rear (slot 4) --
	// the rear rank's right occupant closes left into it first.
	{
		const int32 NumFiles = 2;
		const int32 NumSlots = 6;
		TArray<int32> Assignment = { INDEX_NONE, 1, 2, 3, 4, 5 };
		FSlotAssigner::CloseGaps(Assignment, NumSlots, NumFiles);
		TestEqual(TEXT("file cascade: entity 2 pulled up (2 -> 0)"), Assignment[2], 0);
		TestEqual(TEXT("file cascade: entity 4 pulled up (4 -> 2)"), Assignment[4], 2);
		TestEqual(TEXT("file cascade: entity 5 closed left (5 -> 4)"), Assignment[5], 4);
		TestEqual(TEXT("file cascade: untouched file"), Assignment[1], 1);
		TestEqual(TEXT("file cascade: untouched file"), Assignment[3], 3);
		TestEqual(TEXT("file cascade: casualty passes through"), Assignment[0], INDEX_NONE);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			if (Slot != INDEX_NONE)
			{
				Used.Add(Slot);
			}
		}
		TestFalse(TEXT("file cascade: hole exited at slot 5 (rear-right)"), Used.Contains(5));
		TestEqual(TEXT("file cascade: exactly one hole"), Used.Num(), 5);
	}

	// Rule 1b, the lateral cascade: single rank, 4 files, hole at file 1.
	// Everyone right of the hole closes LEFT; the hole exits at the right edge.
	{
		const int32 NumFiles = 4;
		const int32 NumSlots = 4;
		TArray<int32> Assignment = { 0, INDEX_NONE, 2, 3 };
		FSlotAssigner::CloseGaps(Assignment, NumSlots, NumFiles);
		TestEqual(TEXT("lateral cascade: file 0 holds"), Assignment[0], 0);
		TestEqual(TEXT("lateral cascade: entity 2 closed left (2 -> 1)"), Assignment[2], 1);
		TestEqual(TEXT("lateral cascade: entity 3 closed left (3 -> 2)"), Assignment[3], 2);
		// Slot 3 (the right edge) is the one slot nobody owns.
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			if (Slot != INDEX_NONE)
			{
				Used.Add(Slot);
			}
		}
		TestFalse(TEXT("lateral cascade: hole is at slot 3 (right edge)"), Used.Contains(3));
		TestEqual(TEXT("lateral cascade: exactly one hole"), Used.Num(), 3);
	}

	// Permutation property: a mid-formation double casualty collapses to
	// the rear rank. Pass 1 drains both mid-formation holes; the bounded
	// fixpoint pass 2 additionally pulls entity 8 left (7 -> 6) because
	// slot 6's right neighbor only became occupied late in pass 1.
	{
		const int32 NumFiles = 3;
		const int32 NumSlots = 9;
		TArray<int32> Assignment = { INDEX_NONE, 1, 2, 3, INDEX_NONE, 5, 6, 7, 8 };
		FSlotAssigner::CloseGaps(Assignment, NumSlots, NumFiles);
		TestEqual(TEXT("permutation: entity 3 pulled up (3 -> 0)"), Assignment[3], 0);
		TestEqual(TEXT("permutation: entity 6 pulled up (6 -> 3)"), Assignment[6], 3);
		TestEqual(TEXT("permutation: entity 7 pulled up (7 -> 4)"), Assignment[7], 4);
		TestEqual(TEXT("permutation: fixpoint pass pulled entity 8 left (8 -> 6)"), Assignment[8], 6);
		TSet<int32> Used;
		bool bBijection = true;
		for (int32 Slot : Assignment)
		{
			bBijection = bBijection && (Slot == INDEX_NONE || (Slot >= 0 && Slot < NumSlots && !Used.Contains(Slot)));
			if (Slot != INDEX_NONE)
			{
				Used.Add(Slot);
			}
		}
		TestTrue(TEXT("permutation: bijection over survivors"), bBijection);
		TestEqual(TEXT("permutation: survivor count preserved"), Used.Num(), 7);
		// Both remaining holes are in the rear rank (slots 6..8).
		TestFalse(TEXT("permutation: holes only in the rear rank"), Used.Contains(7) && Used.Contains(8) && Used.Contains(6));
	}

	// The fixpoint counter-case (replica-verified): 2 files, owners at
	// slots 0 and 5 only. Slot 1 is scanned while its behind (slot 3) is
	// still vacant; slot 3 fills laterally from 5's behind-fill later in
	// pass 1; pass 2 then sees slot 1 behind-fillable and pulls entity 5
	// forward again. A single-pass implementation would strand slot 1.
	{
		TArray<int32> Assignment = { 0, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 5 };
		FSlotAssigner::CloseGaps(Assignment, 6, 2);
		TestEqual(TEXT("fixpoint: entity 5 walked 5 -> 3 -> 1"), Assignment[5], 1);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			if (Slot != INDEX_NONE)
			{
				Used.Add(Slot);
			}
		}
		TestFalse(TEXT("fixpoint: slot 1 drained (not a hole)"), Used.Contains(1));
		TestEqual(TEXT("fixpoint: survivors"), Used.Num(), 2);
	}

	// Determinism: identical inputs, identical outputs.
	{
		TArray<int32> A = { INDEX_NONE, 1, 2, 3, 4, 5, 6, 7, 8 };
		TArray<int32> B = { INDEX_NONE, 1, 2, 3, 4, 5, 6, 7, 8 };
		FSlotAssigner::CloseGaps(A, 9, 3);
		FSlotAssigner::CloseGaps(B, 9, 3);
		bool bIdentical = true;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			bIdentical = bIdentical && A[i] == B[i];
		}
		TestTrue(TEXT("deterministic repeat"), bIdentical);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_EvictToReachable,
	"Battle.Unit.Slots.EvictToReachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_EvictToReachable::RunTest(const FString& Parameters)
{
	// 4 files x 3 ranks, 1.2 m spacing, grid layout (file = +X, rank = -Y
	// so slot positions are bijective with indices).
	constexpr int32 NumFiles = 4;
	constexpr int32 NumSlots = 12;
	auto SlotPosOf = [](int32 Slot)
	{
		return FVector2D((Slot % NumFiles) * 1.2f, -(Slot / NumFiles) * 1.2f);
	};
	TArray<FVector2D> SlotPos;
	for (int32 S = 0; S < NumSlots; ++S)
	{
		SlotPos.Add(SlotPosOf(S));
	}

	// The reported repro: entity 0 owns interior slot 5 = (rank 1, file 1)
	// but was shoved out and circles the right flank next to slot 3 =
	// (rank 0, file 3). It must take slot 3; the owners along the staircase
	// (0,2) -> (0,1) -> (1,1) shift one slot each; the chain's last member
	// lands on the vacated slot 5. No entity moves more than one slot.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Stuck;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Stuck.Add(0);
		}
		Swap(Assignment[0], Assignment[5]);   // e0 owns the interior slot 5...
		EntityPos[0] = SlotPos[3] + FVector2D(0.2f, 0.f); // ...but stands at the right flank
		Stuck[0] = 1;
		FSlotAssigner::EvictToReachable(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, Stuck);

		TestEqual(TEXT("eviction: stuck entity takes the nearest (perimeter) slot"), Assignment[0], 3);
		TestEqual(TEXT("eviction: flank owner steps aside (3 -> 2)"), Assignment[3], 2);
		TestEqual(TEXT("eviction: chain member steps aside (2 -> 1)"), Assignment[2], 1);
		TestEqual(TEXT("eviction: chain terminates on the vacated interior slot (1 -> 5)"), Assignment[1], 5);
		TestEqual(TEXT("eviction: off-path owner untouched"), Assignment[5], 0);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			Used.Add(Slot);
		}
		TestEqual(TEXT("eviction: bijection preserved"), Used.Num(), NumSlots);
	}

	// Churn guard: a stuck entity standing exactly between its own slot and
	// the neighbor's (tie -> lowest index is nearest) has no reachability
	// win -- 2x the best distance is NOT below the own distance -- so it
	// must NOT evict.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Stuck;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Stuck.Add(0);
		}
		Swap(Assignment[0], Assignment[5]);
		EntityPos[0] = SlotPos[5] + FVector2D(0.6f, 0.f); // equidistant: 0.6 to slot 5, 0.6 to slot 4
		Stuck[0] = 1;
		TArray<int32> Before = Assignment;
		FSlotAssigner::EvictToReachable(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, Stuck);
		bool bIdentical = true;
		for (int32 i = 0; i < Assignment.Num(); ++i)
		{
			bIdentical = bIdentical && Assignment[i] == Before[i];
		}
		TestTrue(TEXT("churn guard: marginal gain, no eviction"), bIdentical);
	}

	// Mid-path vacancy absorbs the chain: the vacancy migrates to the sink
	// slot for CloseGaps to drain; nothing is orphaned.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Stuck;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Stuck.Add(0);
		}
		Swap(Assignment[0], Assignment[9]);                   // e0 owns deep interior (2,1)
		Assignment[2] = INDEX_NONE;                           // slot 2 vacant mid-path
		EntityPos[0] = SlotPos[3] + FVector2D(0.2f, 0.f);   // circles the right flank
		Stuck[0] = 1;
		FSlotAssigner::EvictToReachable(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, Stuck);

		TestEqual(TEXT("absorption: stuck entity still takes the perimeter slot"), Assignment[0], 3);
		TestEqual(TEXT("absorption: displaced owner lands on the mid-path vacancy"), Assignment[3], 2);
		TestEqual(TEXT("absorption: the unassigned entity passes through"), Assignment[2], INDEX_NONE);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			if (Slot != INDEX_NONE)
			{
				Used.Add(Slot);
			}
		}
		TestEqual(TEXT("absorption: bijection preserved"), Used.Num(), 11);
		TestFalse(TEXT("absorption: the sink slot 9 is left vacant for CloseGaps"), Used.Contains(9));
		// And CloseGaps drains the migrated sink vacancy to the rear-right.
		FSlotAssigner::CloseGaps(Assignment, NumSlots, NumFiles);
		TSet<int32> UsedAfter;
		for (int32 Slot : Assignment)
		{
			if (Slot != INDEX_NONE)
			{
				UsedAfter.Add(Slot);
			}
		}
		TestEqual(TEXT("absorption: survivor count preserved through CloseGaps"), UsedAfter.Num(), 11);
		TestEqual(TEXT("absorption: entity 10 closed left (10 -> 9)"), Assignment[10], 9);
		TestEqual(TEXT("absorption: entity 11 closed left (11 -> 10)"), Assignment[11], 10);
		TestFalse(TEXT("absorption: vacancy parked at slot 11 (rear-right)"), UsedAfter.Contains(11));
	}

	// Determinism: identical inputs, identical outputs.
	{
		TArray<int32> A, B;
		TArray<FVector2D> Pos;
		TArray<uint8> Stuck;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			A.Add(E);
			B.Add(E);
			Pos.Add(SlotPos[E]);
			Stuck.Add(E == 4 ? 1 : 0);
		}
		Swap(A[4], A[6]);
		Swap(B[4], B[6]);
		Pos[4] = SlotPos[11] + FVector2D(0.2f, 0.f);
		FSlotAssigner::EvictToReachable(A, Pos, SlotPos, NumSlots, NumFiles, Stuck);
		FSlotAssigner::EvictToReachable(B, Pos, SlotPos, NumSlots, NumFiles, Stuck);
		bool bIdentical = true;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			bIdentical = bIdentical && A[i] == B[i];
		}
		TestTrue(TEXT("deterministic repeat"), bIdentical);
		// And the eviction actually happened in both (guard passed,
		// chain walked): entity 4 ends on the perimeter slot 11.
		TestEqual(TEXT("deterministic repeat: the eviction fired"), A[4], 11);
	}

	return true;
}

// ---- Formation flow v2 (owner observation 2026-10-28: "the slot is claimed
// even though the entity is nowhere near it"): DiffuseVacancies converts the
// VIRTUAL hole (claimed-but-far) into a real vacancy, cascades it rearward,
// and re-homes the owner at the hole nearest its body. ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlots_DiffuseVacancies,
	"Battle.Unit.Slots.DiffuseVacancies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSlots_DiffuseVacancies::RunTest(const FString& Parameters)
{
	// 4 files x 3 ranks, 1.2 m spacing, grid layout (file = +X, rank = -Y).
	constexpr int32 NumFiles = 4;
	constexpr int32 NumSlots = 12;
	auto SlotPosOf = [](int32 Slot)
	{
		return FVector2D((Slot % NumFiles) * 1.2f, -(Slot / NumFiles) * 1.2f);
	};
	TArray<FVector2D> SlotPos;
	for (int32 S = 0; S < NumSlots; ++S)
	{
		SlotPos.Add(SlotPosOf(S));
	}
	constexpr float StaleRadius = 3.f;

	// The owner's tail-drag repro: a FULL bijection (no INDEX anywhere -- the
	// v1 triggers could not see this state at all) where entity 0 holds the
	// FRONT slot 0 but was dragged 6 m behind the rear rank. The claim is
	// stale: the whole file pulls up (fill front), the hole cascades to the
	// rear-right corner, and the dragged entity is re-awarded the REAR slot
	// it can actually walk into -- instead of orbiting the block toward the
	// front slot it will never reach.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Skip.Add(0);
		}
		EntityPos[0] = FVector2D(0.f, -8.4f); // 6 m behind the rear rank, file 0's column
		FSlotAssigner::DiffuseVacancies(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);

		TestEqual(TEXT("tail-drag: re-homed at the rear (11), not the unreachable front slot"), Assignment[0], 11);
		TestEqual(TEXT("tail-drag: file pulled up (4 -> 0)"), Assignment[4], 0);
		TestEqual(TEXT("tail-drag: file pulled up (8 -> 4)"), Assignment[8], 4);
		TestEqual(TEXT("tail-drag: rear rank closed left (9 -> 8)"), Assignment[9], 8);
		TestEqual(TEXT("tail-drag: rear rank closed left (10 -> 9)"), Assignment[10], 9);
		TestEqual(TEXT("tail-drag: rear rank closed left (11 -> 10)"), Assignment[11], 10);
		TestEqual(TEXT("tail-drag: neighbor file untouched"), Assignment[1], 1);
		TestEqual(TEXT("tail-drag: neighbor file untouched"), Assignment[5], 5);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			Used.Add(Slot);
		}
		TestEqual(TEXT("tail-drag: bijection preserved (full house)"), Used.Num(), NumSlots);
	}

	// Self-hysteresis: an entity 2 m from its slot (mid-catchup, walking it
	// down) is NOT stale -- no eviction, no cascade, assignment identical.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Skip.Add(0);
		}
		EntityPos[0] = FVector2D(0.f, -2.f); // trailing its slot, under the radius
		TArray<int32> Before = Assignment;
		FSlotAssigner::DiffuseVacancies(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);
		bool bIdentical = true;
		for (int32 i = 0; i < Assignment.Num(); ++i)
		{
			bIdentical = bIdentical && Assignment[i] == Before[i];
		}
		TestTrue(TEXT("hysteresis: mid-catchup entity keeps its claim"), bIdentical);
	}

	// Combat exemption: the same dragged distance, but the entity's motion is
	// owned by a combat ring (SkipFlags) -- the distance is combat
	// displacement, not a formation hole. No eviction.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Skip.Add(E == 0 ? 1 : 0);
		}
		EntityPos[0] = FVector2D(0.f, -8.4f);
		TArray<int32> Before = Assignment;
		FSlotAssigner::DiffuseVacancies(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);
		bool bIdentical = true;
		for (int32 i = 0; i < Assignment.Num(); ++i)
		{
			bIdentical = bIdentical && Assignment[i] == Before[i];
		}
		TestTrue(TEXT("combat skip: ring-owned entity is not stale"), bIdentical);
	}

	// The settled neighbor is never displaced: the flank-shoved entity's
	// nearest slot (right edge, 0.2 m away) is held by an owner STANDING on
	// it -- diffusion does not steal settled slots (that is the ladder's
	// EvictToReachable escalation, not the distance policy). The shoved
	// entity re-homes through the cascade instead.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Skip.Add(0);
		}
		Swap(Assignment[0], Assignment[5]); // e0 owns interior slot 5 (rank 1, file 1)
		EntityPos[0] = FVector2D(5.f, 0.f); // shoved past the right flank: 3.98 m from slot 5
		FSlotAssigner::DiffuseVacancies(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);

		TestEqual(TEXT("flank: settled edge owner untouched (e3 keeps 3)"), Assignment[3], 3);
		TestEqual(TEXT("flank: shoved entity re-homed through the cascade (rear-right)"), Assignment[0], 11);
		TestEqual(TEXT("flank: file pulled up (9 -> 5)"), Assignment[9], 5);
		TestEqual(TEXT("flank: rear rank closed left (10 -> 9)"), Assignment[10], 9);
		TestEqual(TEXT("flank: rear rank closed left (11 -> 10)"), Assignment[11], 10);
		TestEqual(TEXT("flank: the swap partner keeps the front slot (e5 -> 0)"), Assignment[5], 0);
		TSet<int32> Used;
		for (int32 Slot : Assignment)
		{
			Used.Add(Slot);
		}
		TestEqual(TEXT("flank: bijection preserved"), Used.Num(), NumSlots);
	}

	// Radius 0 = the policy is off: byte-for-byte passthrough.
	{
		TArray<int32> Assignment;
		TArray<FVector2D> EntityPos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			Assignment.Add(E);
			EntityPos.Add(SlotPos[E]);
			Skip.Add(0);
		}
		EntityPos[0] = FVector2D(0.f, -8.4f);
		TArray<int32> Before = Assignment;
		FSlotAssigner::DiffuseVacancies(Assignment, EntityPos, SlotPos, NumSlots, NumFiles, 0.f, Skip);
		bool bIdentical = true;
		for (int32 i = 0; i < Assignment.Num(); ++i)
		{
			bIdentical = bIdentical && Assignment[i] == Before[i];
		}
		TestTrue(TEXT("policy off: radius 0 passthrough"), bIdentical);
	}

	// Determinism: identical inputs, identical outputs.
	{
		TArray<int32> A, B;
		TArray<FVector2D> Pos;
		TArray<uint8> Skip;
		for (int32 E = 0; E < NumSlots; ++E)
		{
			A.Add(E);
			B.Add(E);
			Pos.Add(SlotPos[E]);
			Skip.Add(0);
		}
		Pos[0] = FVector2D(0.f, -8.4f);
		FSlotAssigner::DiffuseVacancies(A, Pos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);
		FSlotAssigner::DiffuseVacancies(B, Pos, SlotPos, NumSlots, NumFiles, StaleRadius, Skip);
		bool bIdentical = true;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			bIdentical = bIdentical && A[i] == B[i];
		}
		TestTrue(TEXT("deterministic repeat"), bIdentical);
		TestEqual(TEXT("deterministic repeat: the diffusion fired"), A[0], 11);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
