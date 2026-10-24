// Battle.Unit.Compression.* -- gate T1.4 (arch §8.2/§12): 60% floor respected;
// recovery rate bounded; no oscillation under step input. One cm/m conversion
// case (ground rule 2).
#include "Misc/AutomationTest.h"

#include "BattleCompression.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompression_FloorRespected,
	"Battle.Unit.Compression.FloorRespected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompression_FloorRespected::RunTest(const FString& Parameters)
{
	// A demand to compress past the floor clamps at exactly 60%.
	const float Floor = FUnitCompression::AdvanceSpacingScale(1.f, 0.3f, 0.25f);
	TestEqual(TEXT("floor clamps at 0.6"), Floor, FUnitCompression::CompressionFloor);

	// Compressed slots respect the floor geometrically: no slot may sit closer
	// than Floor * nominal offset from the anchor.
	const FVector2D Anchor(100.f, 50.f);
	const FVector2D Offset(-2.f, 3.f); // meters, 3.6 m nominal distance
	const FVector2D Slot = FUnitCompression::CompressedSlot(Anchor, 30.f, Offset, FUnitCompression::CompressionFloor);
	TestTrue(TEXT("slot distance >= floor * nominal"),
		FVector2D::Distance(Anchor, Slot) >= FUnitCompression::CompressionFloor * Offset.Size() - 1e-4f);

	// Golden case at facing 0: compression scales the offset exactly.
	const FVector2D Slot0 = FUnitCompression::CompressedSlot(FVector2D::ZeroVector, 0.f, Offset, 0.6f);
	TestTrue(TEXT("facing-0 golden: offset * floor"),
		Slot0.Equals(Offset * FUnitCompression::CompressionFloor, 1e-4f));

	// cm/m conversion golden (ground rule 2): offsets arrive from fragments in
	// cm; math is meters; the result converts back to cm at the boundary.
	const FVector2D OffsetCm(-200.f, 300.f);
	const FVector2D SlotM = FUnitCompression::CompressedSlot(FVector2D::ZeroVector, 0.f, OffsetCm * 0.01f, 0.6f);
	const FVector2D SlotCm = SlotM * 100.f;
	TestTrue(TEXT("cm/m round trip"), SlotCm.Equals(FVector2D(-120.f, 180.f), 1e-3f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompression_RecoveryBounded,
	"Battle.Unit.Compression.RecoveryBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompression_RecoveryBounded::RunTest(const FString& Parameters)
{
	// Per-step recovery never exceeds rate * DT.
	const float Step = FUnitCompression::AdvanceSpacingScale(0.6f, 1.f, 0.25f);
	TestTrue(TEXT("per-step recovery <= rate*DT"),
		Step <= 0.6f + FUnitCompression::RecoveryPerSecond * 0.25f + 1e-5f);

	// 0.6 -> 1.0 at 0.1/s takes ~4 s of sim time; convergence check.
	float Scale = FUnitCompression::CompressionFloor;
	const float DT = 0.016f; // fixed-step contract granularity
	int32 Frames = 0;
	while (Scale < 1.f - 1e-4f && Frames < 1000)
	{
		Scale = FUnitCompression::AdvanceSpacingScale(Scale, 1.f, DT);
		++Frames;
	}
	const float Seconds = Frames * DT;
	TestTrue(FString::Printf(TEXT("0.6 -> 1.0 in ~4 s (took %.2f s)"), Seconds),
		FMath::IsNearlyEqual(Seconds, 4.f, 0.2f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompression_NoOscillationUnderStep,
	"Battle.Unit.Compression.NoOscillationUnderStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCompression_NoOscillationUnderStep::RunTest(const FString& Parameters)
{
	// Step input: crowd demand for 2 s (60 frames at 30 Hz), then release for
	// 8 s (240 frames) -- enough for the full 4 s recovery at 0.1/s. The whole
	// response is ONE direction change (drop, then a single monotone recovery)
	// and never a phase-internal reversal.
	float Scale = 1.f;
	bool bWasCompressing = true;
	int32 DirectionChanges = 0;
	float Previous = Scale;

	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		const float Target = (Frame < 60) ? FUnitCompression::CompressionFloor : 1.f;
		Scale = FUnitCompression::AdvanceSpacingScale(Scale, Target, 1.f / 30.f);

		if (FMath::Abs(Scale - Previous) > KINDA_SMALL_NUMBER)
		{
			const bool bCompressing = Scale < Previous;
			if (bCompressing != bWasCompressing)
			{
				++DirectionChanges;
				bWasCompressing = bCompressing;
			}
		}
		Previous = Scale;
	}

	TestEqual(TEXT("exactly one direction change: drop then monotone recovery"), DirectionChanges, 1);
	TestTrue(TEXT("fully recovered after release"), FMath::IsNearlyEqual(Scale, 1.f, 1e-3f));

	return true;
}
