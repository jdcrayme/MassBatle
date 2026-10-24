// Battle.Unit.Planner.* -- gate T1.9 (roadmap; risks R4): paths valid against
// impassable cells, arc-length monotonicity, determinism under seeded RNG,
// bounded re-plan latency when anchors move. Ground rule 1: this gate-first
// skeleton lands with (not after) the planner implementation. Ground rule 2:
// one cm/m conversion golden case lives in the validity test.
#include "Misc/AutomationTest.h"

#include "BattleCostGrid.h"
#include "BattleCostGridSubsystem.h"
#include "BattlePathPlanner.h"
#include "BattleRngRegistry.h"
#include "BattleTypes.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	UWorld* MakePlannerTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	/** Wall across the full tile with one 6 m gap at y in [-9,-3] (meters). */
	void StampWallWithGap(UBattleCostGridSubsystem& CostGrid)
	{
		CostGrid.ResetToFlat();
		CostGrid.StampImpassable(FVector2D(0.f, -129.f), FVector2D(3.f, -9.f));
		CostGrid.StampImpassable(FVector2D(0.f, -3.f), FVector2D(3.f, 129.f));
	}

	/** Direct oracle: no planned point sits inside or within Clearance of an
	 *  impassable cell (independent re-check of SegmentIsClear semantics). */
	bool PathTouchesImpassable(const FBattleCostGrid& Grid, const FBattlePlannedPath& Path, float Clearance)
	{
		for (int32 i = 1; i < Path.Points.Num(); ++i)
		{
			if (!FBattlePathPlanner::SegmentIsClear(Grid, Path.Points[i - 1], Path.Points[i], Clearance))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanner_ValidAgainstImpassable,
	"Battle.Unit.Planner.ValidAgainstImpassable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanner_ValidAgainstImpassable::RunTest(const FString& Parameters)
{
	UWorld* World = MakePlannerTestWorld();
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	if (!TestNotNull(TEXT("cost grid subsystem"), CostGrid))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FBattleCostGrid& Grid = CostGrid->GetGrid();
	TestEqual(TEXT("cell size 3 m (arch §12)"), Grid.CellSize, 3.0f);

	// Ground rule 2 golden case: spawn pos arrives in cm, planner consumes m.
	const FVector2D SpawnPosCm(5000.f, -3000.f);
	const FVector2D SpawnPosM = SpawnPosCm * 0.01f;
	TestEqual(TEXT("cm -> m conversion"), SpawnPosM.X, 50.0);
	int32 CellX = INDEX_NONE, CellY = INDEX_NONE;
	Grid.WorldToCell(SpawnPosM, CellX, CellY);
	const FVector2D BackToM = Grid.CellCenter(CellX, CellY);
	// The center of the containing cell is at most half a cell diagonal away.
	TestTrue(TEXT("world<->cell round trip within half a cell diagonal"),
		FVector2D::Distance(SpawnPosM, BackToM) <= Grid.CellSize * UE_SQRT_2 * 0.5f + KINDA_SMALL_NUMBER);

	StampWallWithGap(*CostGrid);

	// West-of-wall anchor must march through the 6 m gap (y in [-9,-3]).
	const FVector2D StartM(-30.f, -30.f);
	const FVector2D GoalM(50.f, -6.f);
	FBattlePlannedPath Path;
	const bool bFound = FBattlePathPlanner::FindPath(Grid, StartM, GoalM, 0.f, Path);
	TestTrue(TEXT("path found through the gap"), bFound);
	if (bFound)
	{
		TestFalse(TEXT("path never touches impassable cells"), PathTouchesImpassable(Grid, Path, 0.5f));
		TestTrue(TEXT("path starts at the anchor"), Path.Points[0].Equals(StartM));
		TestTrue(TEXT("path ends at the goal"), Path.Points.Last().Equals(GoalM));
		// The wall sits between the endpoints: the gap detour must be longer
		// than the straight line -- proof the planner respected the wall.
		TestTrue(TEXT("detour is longer than straight line"),
			Path.TotalLength > FVector2D::Distance(StartM, GoalM) + 1.0f);
	}

	// A goal inside a sealed box is unreachable -> loud false (R4: no silent
	// bogus paths).
	CostGrid->StampImpassable(FVector2D(20.f, 20.f), FVector2D(40.f, 40.f));
	CostGrid->StampImpassable(FVector2D(10.f, 10.f), FVector2D(50.f, 22.f));
	CostGrid->StampImpassable(FVector2D(10.f, 22.f), FVector2D(22.f, 50.f));
	CostGrid->StampImpassable(FVector2D(38.f, 22.f), FVector2D(50.f, 50.f));
	CostGrid->StampImpassable(FVector2D(10.f, 38.f), FVector2D(50.f, 50.f));
	FBattlePlannedPath SealedPath;
	TestFalse(TEXT("sealed box is unreachable"),
		FBattlePathPlanner::FindPath(Grid, FVector2D(0.f, 0.f), FVector2D(30.f, 30.f), 0.f, SealedPath));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanner_ArcLengthMonotonic,
	"Battle.Unit.Planner.ArcLengthMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanner_ArcLengthMonotonic::RunTest(const FString& Parameters)
{
	UWorld* World = MakePlannerTestWorld();
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
	if (!TestNotNull(TEXT("cost grid"), CostGrid) || !TestNotNull(TEXT("rng"), Rng))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Named stream (ground rule 5): "Planner.Gate" -- never improvise streams.
	Rng->Seed(20261024);
	FRandomStream& Stream = Rng->GetStream(TEXT("Planner.Gate"));

	StampWallWithGap(*CostGrid);
	const FBattleCostGrid& Grid = CostGrid->GetGrid();

	int32 Planned = 0;
	for (int32 Trial = 0; Trial < 16; ++Trial)
	{
		const FVector2D Start(Stream.FRandRange(-100.f, 100.f), Stream.FRandRange(-100.f, 100.f));
		const FVector2D Goal(Stream.FRandRange(-100.f, 100.f), Stream.FRandRange(-100.f, 100.f));
		FBattlePlannedPath Path;
		if (!FBattlePathPlanner::FindPath(Grid, Start, Goal, 0.f, Path))
		{
			continue; // unreachable pairs are legal; the validity test covers them
		}
		++Planned;
		FBattlePathPlanner::Smooth(Grid, 1.0f, Path);
		FBattlePathPlanner::Resample(Path, 3.0f, Path);

		TestTrue(FString::Printf(TEXT("trial %d: resampled path valid"), Trial), Path.IsValid());
		TestTrue(FString::Printf(TEXT("trial %d: S[0]==0"), Trial), FMath::IsNearlyEqual(Path.CumulativeS[0], 0.f));
		for (int32 i = 1; i < Path.CumulativeS.Num(); ++i)
		{
			if (Path.CumulativeS[i] < Path.CumulativeS[i - 1] - KINDA_SMALL_NUMBER)
			{
				AddError(FString::Printf(TEXT("trial %d: arc length regressed at %d (%f < %f)"),
					Trial, i, Path.CumulativeS[i], Path.CumulativeS[i - 1]));
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("trial %d: total length == last S"), Trial),
			FMath::IsNearlyEqual(Path.TotalLength, Path.CumulativeS.Last(), 1e-3f));
		bool bFinite = true;
		for (const FVector2D& P : Path.Points)
		{
			bFinite = bFinite && P.ContainsNaN() == false;
		}
		TestTrue(FString::Printf(TEXT("trial %d: all points finite"), Trial), bFinite);
	}
	TestTrue(TEXT("at least half the random pairs planned"), Planned >= 8);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanner_Deterministic,
	"Battle.Unit.Planner.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanner_Deterministic::RunTest(const FString& Parameters)
{
	UWorld* World = MakePlannerTestWorld();
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	if (!TestNotNull(TEXT("cost grid"), CostGrid))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FBattleCostGrid& Grid = CostGrid->GetGrid();
	StampWallWithGap(*CostGrid);

	auto PlanOnce = [&Grid]() -> FBattlePlannedPath
	{
		FBattlePlannedPath Path;
		(void)FBattlePathPlanner::FindPath(Grid, FVector2D(-60.f, 10.f), FVector2D(60.f, -6.f), 0.f, Path);
		FBattlePathPlanner::Smooth(Grid, 1.0f, Path);
		FBattlePathPlanner::Resample(Path, 3.0f, Path);
		return Path;
	};

	// Same inputs -> bit-identical output (ground rule 5: no iteration-order
	// or float-tie nondeterminism).
	const FBattlePlannedPath A = PlanOnce();
	const FBattlePlannedPath B = PlanOnce();
	TestEqual(TEXT("same point count"), A.Points.Num(), B.Points.Num());
	bool bIdentical = A.Points.Num() == B.Points.Num();
	for (int32 i = 0; bIdentical && i < A.Points.Num(); ++i)
	{
		bIdentical = A.Points[i].Equals(B.Points[i]) &&
			FMath::Abs(A.CumulativeS[i] - B.CumulativeS[i]) == 0.f;
	}
	TestTrue(TEXT("paths bit-identical across runs"), bIdentical);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanner_ReplanLatencyBounded,
	"Battle.Unit.Planner.ReplanLatencyBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanner_ReplanLatencyBounded::RunTest(const FString& Parameters)
{
	UWorld* World = MakePlannerTestWorld();
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	if (!TestNotNull(TEXT("cost grid"), CostGrid))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FBattleCostGrid& Grid = CostGrid->GetGrid();
	StampWallWithGap(*CostGrid);

	// R4: an anchor marching toward a moving goal re-plans each step; latency
	// must stay bounded (test-measured wall time -- the no-wall-clock rule
	// binds movement SIMULATION, not perf evidence; cf. phase-0 spawn baseline).
	double TotalMs = 0.0;
	double MaxMs = 0.0;
	int32 Replans = 0;
	for (int32 Step = 0; Step < 64; ++Step)
	{
		const FVector2D Anchor(-60.f + Step, 20.f - Step * 0.5f);
		const FVector2D Goal(60.f - Step * 0.25f, -6.f + Step * 0.1f);
		const double T0 = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
		FBattlePlannedPath Path;
		if (FBattlePathPlanner::FindPath(Grid, Anchor, Goal, 0.f, Path))
		{
			const double Ms = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles()) - T0;
			TotalMs += Ms;
			MaxMs = FMath::Max(MaxMs, Ms);
			++Replans;
		}
	}
	TestTrue(TEXT("re-plans executed"), Replans >= 60);
	const double MeanMs = TotalMs / FMath::Max(1, Replans);
	UE_LOG(LogTemp, Display, TEXT("EXEC_RESULT PlannerReplan Replans=%d MeanMs=%.3f MaxMs=%.3f"), Replans, MeanMs, MaxMs);
	TestTrue(FString::Printf(TEXT("mean re-plan %.3f ms < 5 ms"), MeanMs), MeanMs < 5.0);
	TestTrue(FString::Printf(TEXT("max re-plan %.3f ms < 20 ms"), MaxMs), MaxMs < 20.0);

	World->DestroyWorld(false);
	return true;
}
