// Battle.Unit.Pass.* -- gate T1.7 support (arch §8.6): threshold decision
// correct both sides; morph column files = floor(PassWidth / FileSpacing);
// squeeze scale clamps to the compression floor; clear-width oracle vs a
// stamped gap (the A5 stamp data the planner consumes).
#include "Misc/AutomationTest.h"

#include "BattlePassGeometry.h"
#include "BattleCostGridSubsystem.h"

#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPass_ThresholdBothSides,
	"Battle.Unit.Pass.ThresholdBothSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPass_ThresholdBothSides::RunTest(const FString& Parameters)
{
	// Formation width 24 m, floor 0.6 -> threshold 14.4 m.
	// Above the threshold: squeeze, scale = PassWidth/Width clamped to [0.6, 1].
	{
		const auto D = FUnitPassGeometry::DecideSqueezeOrMorph(16.f, 24.f, 1.2f);
		TestFalse(TEXT("16 m pass: squeeze"), D.bMorph);
		TestTrue(TEXT("16 m pass: scale 0.667"), FMath::IsNearlyEqual(D.SqueezeScale, 16.f / 24.f, 1e-3f));
	}
	// Exactly at the threshold: squeeze (>=).
	{
		const auto D = FUnitPassGeometry::DecideSqueezeOrMorph(14.4f, 24.f, 1.2f);
		TestFalse(TEXT("threshold pass: squeeze"), D.bMorph);
		TestTrue(TEXT("threshold scale == floor"), FMath::IsNearlyEqual(D.SqueezeScale, 0.6f, 1e-3f));
	}
	// Below the threshold: morph to floor(PassWidth / FileSpacing) columns.
	{
		const auto D = FUnitPassGeometry::DecideSqueezeOrMorph(8.f, 24.f, 1.2f);
		TestTrue(TEXT("8 m pass: morph"), D.bMorph);
		TestEqual(TEXT("morph files = floor(8/1.2)"), D.MorphFiles, 6);
	}
	{
		const auto D = FUnitPassGeometry::DecideSqueezeOrMorph(2.f, 24.f, 1.2f);
		TestTrue(TEXT("2 m pass: morph"), D.bMorph);
		TestEqual(TEXT("morph files floor"), D.MorphFiles, 1);
	}
	// Absurd pass still yields >= 1 file (never zero columns).
	{
		const auto D = FUnitPassGeometry::DecideSqueezeOrMorph(0.1f, 24.f, 1.2f);
		TestTrue(TEXT("0.1 m pass: morph >= 1 file"), D.bMorph && D.MorphFiles >= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPass_ClearWidthOracle,
	"Battle.Unit.Pass.ClearWidthOracle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPass_ClearWidthOracle::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	if (!TestNotNull(TEXT("cost grid"), CostGrid))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FBattleCostGrid& Grid = CostGrid->GetGrid();
	CostGrid->ResetToFlat();

	// Wall across X=0 with an 8 m gap at Y in [-4, 4].
	CostGrid->StampImpassable(FVector2D(0.f, -129.f), FVector2D(3.f, -4.f));
	CostGrid->StampImpassable(FVector2D(0.f, 4.f), FVector2D(3.f, 129.f));

	// Scanning across (Y direction) from the gap center: ~8 m clear.
	const float WidthAtGap = FUnitPassGeometry::MeasureClearWidth(
		Grid, FVector2D(1.5f, 0.f), FVector2D(0.f, 1.f), 40.f);
	TestTrue(FString::Printf(TEXT("gap width ~8 m (got %.2f)"), WidthAtGap),
		FMath::IsNearlyEqual(WidthAtGap, 8.f, 1.6f));

	// Scanning from inside the wall: ~0 clear.
	const float WidthInWall = FUnitPassGeometry::MeasureClearWidth(
		Grid, FVector2D(1.5f, 30.f), FVector2D(0.f, 1.f), 40.f);
	TestTrue(FString::Printf(TEXT("wall width ~0 (got %.2f)"), WidthInWall), WidthInWall < 1.5f);

	// Open field: saturates the scan.
	const float WidthOpen = FUnitPassGeometry::MeasureClearWidth(
		Grid, FVector2D(-30.f, 0.f), FVector2D(0.f, 1.f), 40.f);
	TestTrue(TEXT("open field saturates"), WidthOpen >= 39.f);

	World->DestroyWorld(false);
	return true;
}
