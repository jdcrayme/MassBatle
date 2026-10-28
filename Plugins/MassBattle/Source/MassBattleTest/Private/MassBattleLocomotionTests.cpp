#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.SpeedLimits.* -- gate T1.3 (arch §8.2, review E2): the unit
// speed limit is min(commanded, slowest x catchup, v_wheel = omega_max *
// r_path, facing cap = FacingDeltaRate * r_path). Golden cases carry the E2
// degenerate set (in-place pivot r->0, straight-march cap inactivity, facing
// cap active separately) and one cm/m conversion case (ground rule 2).
// Hysteresis asserted under seeded member noise (named stream, ground rule 5).
#include "Misc/AutomationTest.h"

#include "BattleUnitLocomotion.h"
#include "BattlePathFrameLayout.h"
#include "BattleRngRegistry.h"

#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"

namespace
{
	FUnitLocomotion::FSpeedLimitInputs BaseInputs()
	{
		FUnitLocomotion::FSpeedLimitInputs In;
		In.CommandedSpeed = 8.f;
		In.MinMemberSpeed = 6.f;
		In.MaxMemberSpeed = 5.f;
		In.OuterRadius = 10.f;
		In.PathRadiusOfAnchor = 20.f;
		In.FacingDeltaRate = 1.f;
		In.CatchupFactor = 1.25f;
		return In;
	}

	UWorld* MakeLocomotionTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_Min3Golden,
	"Battle.Unit.SpeedLimits.Min3Golden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_Min3Golden::RunTest(const FString& Parameters)
{
	// Base: omega = 5/10 = 0.5 rad/s; v_wheel = 0.5*20 = 10; facing cap = 1*20 = 20;
	// slowest*catchup = 7.5; commanded 8 -> limit 7.5 (slowest man binds).
	FUnitLocomotion::FSpeedLimitInputs In = BaseInputs();
	TestEqual(TEXT("slowest man binds"), FUnitLocomotion::ComputeUnitSpeedLimit(In), 7.5f);

	// Commanded binds: drop the order intent below the slowest-man term.
	In.CommandedSpeed = 4.f;
	TestEqual(TEXT("commanded binds"), FUnitLocomotion::ComputeUnitSpeedLimit(In), 4.f);

	// Wheel cap binds: straighten the members out and open the throttle.
	In = BaseInputs();
	In.CommandedSpeed = 12.f;
	In.MinMemberSpeed = 12.f; // slowest*1.25 = 15
	TestEqual(TEXT("wheel cap binds (v_wheel = omega_max * r_path)"),
		FUnitLocomotion::ComputeUnitSpeedLimit(In), 10.f);

	// Facing cap binds separately (E2: FacingDeltaRate is its own linear cap).
	In.PathRadiusOfAnchor = 100.f; // wheel = 50, inactive
	In.FacingDeltaRate = 0.05f;    // facing cap = 5
	TestEqual(TEXT("facing cap binds"), FUnitLocomotion::ComputeUnitSpeedLimit(In), 5.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_E2DegenerateSet,
	"Battle.Unit.SpeedLimits.E2DegenerateSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_E2DegenerateSet::RunTest(const FString& Parameters)
{
	// In-place pivot: r_path -> 0 means the unit cannot translate while
	// pivoting -- the wheel limit collapses to ~0 (eps floor).
	FUnitLocomotion::FSpeedLimitInputs In = BaseInputs();
	In.PathRadiusOfAnchor = 0.f;
	const float PivotLimit = FUnitLocomotion::ComputeUnitSpeedLimit(In);
	TestTrue(TEXT("in-place pivot: wheel cap collapses to ~0"), PivotLimit < 1e-3f);

	// Straight march: r_path huge -> wheel + facing caps huge -> INACTIVE
	// (limit = min(commanded, slowest*catchup) only). The E2 point: the wheel
	// cap must not leak into straight-line marching.
	In = BaseInputs();
	In.CommandedSpeed = 12.f;
	In.MinMemberSpeed = 12.f;
	In.PathRadiusOfAnchor = 1e6f;
	In.FacingDeltaRate = 1.f;
	TestEqual(TEXT("straight march: caps inactive"),
		FUnitLocomotion::ComputeUnitSpeedLimit(In), FMath::Min(12.f, 12.f * 1.25f));

	// Degenerate outer radius is floored, not NaN/inf: OuterRadius -> 0 with a
	// real turn radius makes omega huge but v_wheel = omega * r_path stays
	// finite and sane.
	In = BaseInputs();
	In.OuterRadius = 0.f;
	const float Limit = FUnitLocomotion::ComputeUnitSpeedLimit(In);
	TestTrue(TEXT("degenerate outer radius stays finite"), Limit > 0.f && Limit < 100.f);

	// cm/m conversion golden (ground rule 2): fragment speeds arrive in cm/s;
	// the math is meters; the result converts back at the boundary.
	const float MinMemberCmS = 600.f;   // 6 m/s
	const float CommandedCmS = 800.f;   // 8 m/s
	FUnitLocomotion::FSpeedLimitInputs CmCase = BaseInputs();
	CmCase.MinMemberSpeed = MinMemberCmS * 0.01f;
	CmCase.CommandedSpeed = CommandedCmS * 0.01f;
	const float LimitM = FUnitLocomotion::ComputeUnitSpeedLimit(CmCase);
	TestEqual(TEXT("cm/m round trip: limit back in cm/s"), LimitM * 100.f, 750.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_MarchingWheelCapV2,
	"Battle.Unit.SpeedLimits.MarchingWheelCapV2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_MarchingWheelCapV2::RunTest(const FString& Parameters)
{
	// Phase 2 A3 (F-5): with a path active the wheel radius is r_path +
	// HalfFrontage (the wrapped column's outer-file sweep), not the centroid's
	// OuterRadius. Worked example (brief section 0): 14-file x 3-rank at
	// spacing 1.2 m -> HalfFrontage 8.4 m; 90-deg bend r=8; member walk 1.4.
	{
		FUnitLocomotion::FSpeedLimitInputs In;
		In.CommandedSpeed = 8.f;
		In.MinMemberSpeed = 1.4f;
		In.MaxMemberSpeed = 1.4f;
		In.OuterRadius = 8.59f;             // the centroid form's answer
		In.HalfFrontageM = 8.4f;
		In.bPathActive = true;
		In.PathRadiusOfAnchor = 8.f;
		In.FacingDeltaRate = 0.35f;
		const float Expected = 1.4f * 8.f / (8.f + 8.4f); // ~= 0.683 m/s
		const float Limit = FUnitLocomotion::ComputeUnitSpeedLimit(In);
		TestTrue(FString::Printf(TEXT("worked example: anchor cap %.3f ~= %.3f"), Limit, Expected),
			FMath::IsNearlyEqual(Limit, Expected, 1e-3f));
	}

	// r -> huge (straight path): the marching cap converges to member max --
	// exactly (member * r / (r + HF)) with vanishing HF influence; commanded
	// and slowest-man terms sit above it so the wheel term binds.
	{
		FUnitLocomotion::FSpeedLimitInputs In;
		In.CommandedSpeed = 8.f;
		In.MinMemberSpeed = 6.f;
		In.MaxMemberSpeed = 5.f;
		In.OuterRadius = 10.f;
		In.HalfFrontageM = 4.f;
		In.bPathActive = true;
		In.PathRadiusOfAnchor = 1e6f;
		In.FacingDeltaRate = 1.f;
		const float Limit = FUnitLocomotion::ComputeUnitSpeedLimit(In);
		TestTrue(FString::Printf(TEXT("r->inf: cap -> member max (got %.4f)"), Limit),
			FMath::IsNearlyEqual(Limit, 5.f, 1e-2f));
	}

	// r -> 0 (tight bend): the cap collapses to ~0 -- the unit cannot
	// translate through a hairpin regardless of frontage.
	{
		FUnitLocomotion::FSpeedLimitInputs In;
		In.CommandedSpeed = 8.f;
		In.MinMemberSpeed = 6.f;
		In.MaxMemberSpeed = 5.f;
		In.OuterRadius = 10.f;
		In.HalfFrontageM = 4.f;
		In.bPathActive = true;
		In.PathRadiusOfAnchor = 0.f;
		In.FacingDeltaRate = 1.f;
		TestTrue(TEXT("r->0: cap collapses to ~0"),
			FUnitLocomotion::ComputeUnitSpeedLimit(In) < 1e-3f);
	}

	// Flag-off golden: bPathActive = false must reproduce the E2 pivot form
	// EXACTLY, HalfFrontageM notwithstanding (the cvar-off contract).
	{
		FUnitLocomotion::FSpeedLimitInputs In;
		In.CommandedSpeed = 12.f;
		In.MinMemberSpeed = 12.f;
		In.MaxMemberSpeed = 5.f;
		In.OuterRadius = 10.f;
		In.HalfFrontageM = 8.4f; // set but MUST be ignored without a path
		In.bPathActive = false;
		In.PathRadiusOfAnchor = 20.f;
		In.FacingDeltaRate = 1.f;
		// omega = 5/10 = 0.5; wheel = 0.5*20 = 10; slowest*1.25 = 15 -> 10.
		TestEqual(TEXT("flag-off: OuterRadius form preserved"),
			FUnitLocomotion::ComputeUnitSpeedLimit(In), 10.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_HysteresisUnderNoise,
	"Battle.Unit.SpeedLimits.HysteresisUnderNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_HysteresisUnderNoise::RunTest(const FString& Parameters)
{
	UWorld* World = MakeLocomotionTestWorld();
	UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
	if (!TestNotNull(TEXT("rng"), Rng))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Named stream (ground rule 5): "Locomotion.Gate".
	Rng->Seed(20261025);
	FRandomStream& Stream = Rng->GetStream(TEXT("Locomotion.Gate"));

	FUnitLocomotion::FSpeedLimitInputs In = BaseInputs();
	In.CommandedSpeed = 12.f; // open the throttle so the noisy term binds
	In.MinMemberSpeed = 6.f;

	constexpr float Hysteresis = 0.1f;
	float HystLimit = 0.f;
	float RawLimit = 0.f;
	int32 HystFlips = 0;
	int32 RawFlips = 0;
	bool bHystRising = true;
	bool bRawRising = true;

	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		// Member speeds jitter +-20% around 6 m/s: the raw limit chatters.
		In.MinMemberSpeed = 6.f * (1.f + Stream.FRandRange(-0.2f, 0.2f));
		const float Raw = FUnitLocomotion::ComputeUnitSpeedLimit(In);

		const float PrevHyst = HystLimit;
		const float PrevRaw = RawLimit;
		HystLimit = FUnitLocomotion::ApplyHysteresis(HystLimit, Raw, Hysteresis);
		RawLimit = Raw;

		if (Frame > 0)
		{
			const bool bHystNowRising = HystLimit > PrevHyst;
			if (bHystNowRising != bHystRising && FMath::Abs(HystLimit - PrevHyst) > KINDA_SMALL_NUMBER)
			{
				++HystFlips;
				bHystRising = bHystNowRising;
			}
			// Hysteresis contract: a rise only happens past the margin.
			if (HystLimit > PrevHyst)
			{
				TestTrue(FString::Printf(TEXT("frame %d rise past margin"), Frame),
					Raw >= PrevHyst * (1.f + Hysteresis) - KINDA_SMALL_NUMBER);
			}
			// Drops are immediate.
			if (Raw < PrevHyst)
			{
				TestEqual(FString::Printf(TEXT("frame %d immediate drop"), Frame), HystLimit, Raw);
			}

			const bool bRawNowRising = RawLimit > PrevRaw;
			if (bRawNowRising != bRawRising && FMath::Abs(RawLimit - PrevRaw) > KINDA_SMALL_NUMBER)
			{
				++RawFlips;
				bRawRising = bRawNowRising;
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("EXEC_RESULT LocomotionHysteresis RawFlips=%d HystFlips=%d"), RawFlips, HystFlips);
	TestTrue(TEXT("hysteresis strictly reduces direction flips under noise"), HystFlips < RawFlips);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_AnchorAndFacingProgression,
	"Battle.Unit.SpeedLimits.AnchorAndFacingProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_AnchorAndFacingProgression::RunTest(const FString& Parameters)
{
	// Anchor progression: linear advance, clamped to the path end.
	TestEqual(TEXT("advance along path"), FUnitLocomotion::AdvanceAnchor(10.f, 5.f, 0.5f, 100.f), 12.5f);
	TestEqual(TEXT("advance clamps to path end"), FUnitLocomotion::AdvanceAnchor(99.f, 5.f, 0.5f, 100.f), 100.f);
	TestEqual(TEXT("no reverse past path start"), FUnitLocomotion::AdvanceAnchor(1.f, -10.f, 0.5f, 100.f), 0.f);

	// Facing rate cap: 1 rad/s for 0.25 s = 14.32 deg max step.
	TestTrue(TEXT("facing advances toward target"),
		FMath::IsNearlyEqual(FUnitLocomotion::AdvanceFacing(0.f, 90.f, 1.f, 0.25f), FMath::RadiansToDegrees(0.25f), 1e-3f));
	TestEqual(TEXT("facing reaches target without overshoot"),
		FUnitLocomotion::AdvanceFacing(89.f, 90.f, 1.f, 0.25f), 90.f);
	// Shortest-arc wrap: 359 -> 1 degrees steps +2 deg through 0 (result 361).
	TestTrue(TEXT("facing wrap shortest arc"),
		FMath::IsNearlyEqual(FUnitLocomotion::AdvanceFacing(359.f, 1.f, 2.f, 0.25f), 361.f, 1e-2f));

	// Path radius: straight -> huge; arc of radius R -> ~R.
	FBattlePlannedPath Straight;
	Straight.Points = { FVector2D::ZeroVector, FVector2D(40.f, 0.f) };
	Straight.RebuildCumulativeS();
	TestTrue(TEXT("straight path radius is huge"),
		FUnitLocomotion::PathRadiusAt(Straight, 20.f, 2.f) > 1e4f);

	FBattlePlannedPath Arc;
	for (int32 i = 0; i <= 128; ++i)
	{
		const float A = PI * static_cast<float>(i) / 128.f;
		Arc.Points.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)) * 50.f);
	}
	Arc.RebuildCumulativeS();
	const float Radius = FUnitLocomotion::PathRadiusAt(Arc, Arc.TotalLength * 0.5f, 2.f);
	TestTrue(FString::Printf(TEXT("arc radius ~50 m (got %.1f)"), Radius), FMath::IsNearlyEqual(Radius, 50.f, 5.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_StragglerPaceAndStickiness,
	"Battle.Unit.SpeedLimits.StragglerPaceAndStickiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_StragglerPaceAndStickiness::RunTest(const FString& Parameters)
{
	// Phase 2 B2 pace curve: below the threshold = 1.0; at the threshold = 1.0
	// (the term bites strictly beyond it); linear in the tear; floored at 0;
	// gain 0 = the policy off.
	constexpr float Break = 1.8f;
	constexpr float Gain = 0.2f;
	TestEqual(TEXT("below threshold: full pace"), FUnitLocomotion::StragglerPaceFactor(1.0f, Break, Gain), 1.f);
	TestEqual(TEXT("at threshold: full pace"), FUnitLocomotion::StragglerPaceFactor(Break, Break, Gain), 1.f);
	TestTrue(TEXT("0.5 m of tear: -0.1"), FMath::IsNearlyEqual(FUnitLocomotion::StragglerPaceFactor(Break + 0.5f, Break, Gain), 0.9f, 1e-6f));
	TestTrue(TEXT("3 m of tear: -0.6"), FMath::IsNearlyEqual(FUnitLocomotion::StragglerPaceFactor(Break + 3.f, Break, Gain), 0.4f, 1e-6f));
	TestTrue(TEXT("massive tear: floored at 0.25 (a frozen anchor recovers slower than a creeping one)"),
		FMath::IsNearlyEqual(FUnitLocomotion::StragglerPaceFactor(Break + 100.f, Break, Gain), 0.25f, 1e-6f));
	TestEqual(TEXT("gain 0 = off"), FUnitLocomotion::StragglerPaceFactor(Break + 5.f, Break, 0.f), 1.f);

	// Phase 2 B3 stickiness truth table: sticky needs a path AND a dressed
	// block (strictly under the threshold).
	TestTrue(TEXT("dressed + path: sticky"), FUnitLocomotion::MarchReflowSticky(true, 0.5f, Break));
	TestFalse(TEXT("at threshold: not sticky"), FUnitLocomotion::MarchReflowSticky(true, Break, Break));
	TestFalse(TEXT("torn: not sticky"), FUnitLocomotion::MarchReflowSticky(true, 2.0f, Break));
	TestFalse(TEXT("no path: never sticky"), FUnitLocomotion::MarchReflowSticky(false, 0.0f, Break));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeedLimits_FallInBlend,
	"Battle.Unit.SpeedLimits.FallInBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpeedLimits_FallInBlend::RunTest(const FString& Parameters)
{
	// Phase 2 C3 blend alpha: 0 far away, 1 at the path end, monotone
	// nondecreasing as the remaining length shrinks.
	constexpr float Morph = 12.f;
	TestEqual(TEXT("far away: alpha 0"), FUnitLocomotion::FallInBlendAlpha(200.f, Morph), 0.f);
	TestEqual(TEXT("at the morph distance: alpha 0"), FUnitLocomotion::FallInBlendAlpha(Morph, Morph), 0.f);
	TestTrue(TEXT("half way: alpha 0.5"), FMath::IsNearlyEqual(FUnitLocomotion::FallInBlendAlpha(Morph * 0.5f, Morph), 0.5f, 1e-6f));
	TestEqual(TEXT("at arrival: alpha 1"), FUnitLocomotion::FallInBlendAlpha(0.f, Morph), 1.f);
	TestEqual(TEXT("overshoot clamp: alpha 1"), FUnitLocomotion::FallInBlendAlpha(-5.f, Morph), 1.f);

	float Prev = -1.f;
	bool bMonotone = true;
	for (int32 i = 0; i <= 20; ++i)
	{
		const float Remaining = 24.f * (1.f - static_cast<float>(i) / 20.f);
		const float Alpha = FUnitLocomotion::FallInBlendAlpha(Remaining, Morph);
		if (Alpha < Prev - KINDA_SMALL_NUMBER)
		{
			bMonotone = false;
		}
		Prev = Alpha;
	}
	TestTrue(TEXT("alpha monotone nondecreasing while approaching"), bMonotone);

	// Handoff latch hysteresis: latches at/below the handoff distance, stays
	// latched inside the band, unlatches only past 1.5x.
	constexpr float Handoff = 0.7f;
	TestEqual(TEXT("outside: unlatched"), FUnitLocomotion::FallInHandoffLatch(false, 1.0f, Handoff), false);
	TestEqual(TEXT("at handoff: latch"), FUnitLocomotion::FallInHandoffLatch(false, Handoff, Handoff), true);
	TestEqual(TEXT("inside: latch"), FUnitLocomotion::FallInHandoffLatch(false, 0.3f, Handoff), true);
	TestEqual(TEXT("drift inside band: stays latched"), FUnitLocomotion::FallInHandoffLatch(true, 0.9f, Handoff), true);
	TestEqual(TEXT("at 1.5x boundary: stays latched"), FUnitLocomotion::FallInHandoffLatch(true, 1.05f, Handoff), true);
	TestEqual(TEXT("past 1.5x: unlatch"), FUnitLocomotion::FallInHandoffLatch(true, 1.1f, Handoff), false);

	// Morph-distance floor (formation-flow v2): max(cvar, 2x depth, half
	// the frontage -- the owner's "one unit radius" rule: a 10 m wide unit
	// starts its final-slot seeking no later than ~5 m out).
	TestEqual(TEXT("morph: cvar dominates when large"),
		FUnitLocomotion::FallInMorphDistance(24.f, 3, 1.f, 4, 1.2f), 24.f);
	TestEqual(TEXT("morph: depth floor (2x 3 ranks x 1 m)"),
		FUnitLocomotion::FallInMorphDistance(4.f, 3, 1.f, 4, 1.2f), 6.f);
	TestTrue(TEXT("morph: frontage floor (0.5 x 9 files x 1.2 m = 5.4)"),
		FMath::IsNearlyEqual(FUnitLocomotion::FallInMorphDistance(4.f, 2, 1.f, 10, 1.2f), 5.4f, 1e-4f));
	TestEqual(TEXT("morph: single file has no frontage term"),
		FUnitLocomotion::FallInMorphDistance(4.f, 2, 1.f, 1, 1.2f), 4.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
