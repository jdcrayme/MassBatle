// Battle.Unit.PathLayout.* -- gate T1.2 (arch §8.6): rank arc-lengths
// monotonic; laterals orthogonal to tangent; straight-path degeneracy equals
// the anchor formula. Pure-math seam tests -- no world, no fragments except
// the cm->m adapter case (ground rule 2).
#include "Misc/AutomationTest.h"

#include "BattlePathFrameLayout.h"

namespace
{
	/** Semicircular arc, radius R, sampled densely: deterministic curve with
	 *  continuously turning tangents (the anti-straight case). */
	FBattlePlannedPath MakeArcPath(float RadiusMeters, int32 Samples)
	{
		FBattlePlannedPath Path;
		for (int32 i = 0; i <= Samples; ++i)
		{
			const float Angle = PI * static_cast<float>(i) / Samples; // 0..pi
			Path.Points.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RadiusMeters);
		}
		Path.RebuildCumulativeS();
		return Path;
	}

	/** Straight +X path of the given length. */
	FBattlePlannedPath MakeStraightPath(float LengthMeters)
	{
		FBattlePlannedPath Path;
		Path.Points = { FVector2D::ZeroVector, FVector2D(LengthMeters, 0.f) };
		Path.RebuildCumulativeS();
		return Path;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathLayout_RankArcLengthsMonotonic,
	"Battle.Unit.PathLayout.RankArcLengthsMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPathLayout_RankArcLengthsMonotonic::RunTest(const FString& Parameters)
{
	const FBattlePlannedPath Arc = MakeArcPath(50.f, 64);
	if (!TestTrue(TEXT("arc path valid"), Arc.IsValid()))
	{
		return false;
	}

	const float SAnchor = Arc.TotalLength * 0.75f; // anchor three quarters along
	const float RankSpacing = 1.5f;
	float PreviousS = FLT_MAX;
	for (int32 Rank = 0; Rank < 6; ++Rank)
	{
		const float S = FPathFrameLayout::RankArcLength(SAnchor, Rank, RankSpacing, Arc.TotalLength);
		TestTrue(FString::Printf(TEXT("rank %d arc-length nonincreasing"), Rank), S <= PreviousS + KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("rank %d arc-length == SAnchor - rank*spacing (no clamp yet)"), Rank),
			FMath::IsNearlyEqual(S, SAnchor - Rank * RankSpacing, 1e-4f));
		PreviousS = S;
	}

	// Clamp regime: ranks far behind the path start pin to 0 (monotone flat).
	const float SAnchorShort = 2.0f;
	float PreviousClamped = FLT_MAX;
	for (int32 Rank = 0; Rank < 4; ++Rank)
	{
		const float S = FPathFrameLayout::RankArcLength(SAnchorShort, Rank, RankSpacing, Arc.TotalLength);
		TestTrue(FString::Printf(TEXT("clamped rank %d nonincreasing"), Rank), S <= PreviousClamped + KINDA_SMALL_NUMBER);
		PreviousClamped = S;
	}
	TestEqual(TEXT("deep rear rank clamps to path start"), FPathFrameLayout::RankArcLength(SAnchorShort, 3, RankSpacing, Arc.TotalLength), 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathLayout_LateralsOrthogonal,
	"Battle.Unit.PathLayout.LateralsOrthogonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPathLayout_LateralsOrthogonal::RunTest(const FString& Parameters)
{
	const FBattlePlannedPath Arc = MakeArcPath(50.f, 64);
	if (!TestTrue(TEXT("arc path valid"), Arc.IsValid()))
	{
		return false;
	}

	const float SAnchor = Arc.TotalLength * 0.6f;
	const int32 NumRanks = 3;
	const int32 NumFiles = 5;
	const float RankSpacing = 1.5f;
	const float FileSpacing = 1.0f;

	for (int32 Rank = 0; Rank < NumRanks; ++Rank)
	{
		for (int32 File = 0; File < NumFiles; ++File)
		{
			FVector2D SlotPos;
			float Yaw = 0.f;
			if (!TestTrue(FString::Printf(TEXT("project r%d f%d"), Rank, File),
				FPathFrameLayout::Project(Arc, SAnchor, Rank, File, NumFiles, RankSpacing, FileSpacing, SlotPos, Yaw)))
			{
				return false;
			}
			// The lateral offset is purely along the path normal: the slot's
			// rank point plus the lateral must keep dot(delta, tangent) == 0.
			FVector2D RankPos;
			FVector2D Tangent;
			(void)FPathFrameLayout::SampleAt(Arc, FPathFrameLayout::RankArcLength(SAnchor, Rank, RankSpacing, Arc.TotalLength), RankPos, Tangent);
			const FVector2D Delta = SlotPos - RankPos;
			const float AlongTangent = FVector2D::DotProduct(Delta, Tangent);
			TestTrue(FString::Printf(TEXT("r%d f%d lateral orthogonal to tangent"), Rank, File),
				FMath::IsNearlyZero(AlongTangent, 1e-3f));
			// Lateral magnitude exactly the layout offset.
			TestTrue(FString::Printf(TEXT("r%d f%d lateral magnitude"), Rank, File),
				FMath::IsNearlyEqual(Delta.Size(), FMath::Abs(File - (NumFiles - 1) * 0.5f) * FileSpacing, 1e-3f));
			// Per-rank facing = local tangent heading.
			const float ExpectedYaw = FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X));
			TestTrue(FString::Printf(TEXT("r%d f%d facing == tangent heading"), Rank, File),
				FMath::IsNearlyEqual(Yaw, ExpectedYaw, 1e-3f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathLayout_StraightDegeneracy,
	"Battle.Unit.PathLayout.StraightDegeneracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPathLayout_StraightDegeneracy::RunTest(const FString& Parameters)
{
	// Facing 0: straight +X path, anchor mid-path so PathPoint(SAnchor) is the
	// anchor itself -- the degeneracy must be EXACT equality (mod float eps).
	const FBattlePlannedPath Straight = MakeStraightPath(60.f);
	const float SAnchor = 30.f;
	const FVector2D Anchor(SAnchor, 0.f);
	const int32 NumRanks = 3;
	const int32 NumFiles = 6;
	const float RankSpacing = 1.5f;
	const float FileSpacing = 1.2f;

	for (int32 Rank = 0; Rank < NumRanks; ++Rank)
	{
		for (int32 File = 0; File < NumFiles; ++File)
		{
			FVector2D SlotPos;
			float Yaw = 0.f;
			if (!TestTrue(FString::Printf(TEXT("project r%d f%d"), Rank, File),
				FPathFrameLayout::Project(Straight, SAnchor, Rank, File, NumFiles, RankSpacing, FileSpacing, SlotPos, Yaw)))
			{
				return false;
			}
			const FVector2D Degenerate = FPathFrameLayout::AnchorFormula(Anchor, 0.f, Rank, File, NumFiles, RankSpacing, FileSpacing);
			TestTrue(FString::Printf(TEXT("r%d f%d straight degeneracy == anchor formula"), Rank, File),
				SlotPos.Equals(Degenerate, 1e-4f));
			TestTrue(FString::Printf(TEXT("r%d f%d facing == commanded facing"), Rank, File),
				FMath::IsNearlyEqual(Yaw, 0.f, 1e-4f));
		}
	}

	// Facing 90 degrees: rotate the whole frame; degeneracy still exact. The
	// path must lie along +Y with the anchor on it.
	FBattlePlannedPath StraightY;
	StraightY.Points = { FVector2D::ZeroVector, FVector2D(0.f, 60.f) };
	StraightY.RebuildCumulativeS();
	{
		FVector2D SlotPos;
		float Yaw = 0.f;
		const int32 Rank = 2;
		const int32 File = 1;
		TestTrue(TEXT("project on +Y path"),
			FPathFrameLayout::Project(StraightY, SAnchor, Rank, File, NumFiles, RankSpacing, FileSpacing, SlotPos, Yaw));
		const FVector2D Degenerate = FPathFrameLayout::AnchorFormula(FVector2D(0.f, SAnchor), 90.f, Rank, File, NumFiles, RankSpacing, FileSpacing);
		TestTrue(TEXT("facing-90 degeneracy == anchor formula"), SlotPos.Equals(Degenerate, 1e-4f));
		TestTrue(TEXT("facing-90 yaw"), FMath::IsNearlyEqual(Yaw, 90.f, 1e-4f));
	}

	// cm adapter (ground rule 2): a fragment polyline in cm converts to the
	// same meter path (points scale by 1/100, arc lengths rebuilt identically).
	FBattlePathPolyline Fragment;
	Fragment.NumPoints = 3;
	Fragment.TotalLength = 200.f; // cm
	Fragment.Points[0] = { FVector2D(0.f, 0.f), 0.f, FVector2D(1.f, 0.f) };
	Fragment.Points[1] = { FVector2D(100.f, 0.f), 100.f, FVector2D(1.f, 0.f) };
	Fragment.Points[2] = { FVector2D(200.f, 0.f), 200.f, FVector2D(1.f, 0.f) };
	FBattlePlannedPath Converted;
	TestTrue(TEXT("fragment converts"), FPathFrameLayout::FromFragment(Fragment, Converted));
	TestTrue(TEXT("converted valid"), Converted.IsValid());
	TestTrue(TEXT("converted total length in meters"), FMath::IsNearlyEqual(Converted.TotalLength, 2.f, 1e-4f));
	TestTrue(TEXT("converted midpoint"), Converted.Points[1].Equals(FVector2D(1.f, 0.f), 1e-4f));

	return true;
}
