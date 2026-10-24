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
