#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Servo.* -- gate support for the §8.3 servo + A1 avoidance +
// §8.5 unstuck ladder (T1.5/T1.6 pin the end-to-end behavior on the functional
// map; these unit tests pin the math). Pure-math seam, meters, ground rule 5
// streams where noise is needed.
#include "Misc/AutomationTest.h"

#include "BattleServo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServo_ClampsAndDirection,
	"Battle.Unit.Servo.ClampsAndDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FServo_ClampsAndDirection::RunTest(const FString& Parameters)
{
	// Proportional regime: 2 m away at Tau 0.25 -> 8 m/s toward the slot.
	const FVector2D V = FUnitServo::ComputeServoVelocity(FVector2D::ZeroVector, FVector2D(2.f, 0.f),
		FUnitServoConstants::Tau, 100.f, FUnitServoConstants::CatchupBoost);
	TestTrue(TEXT("proportional: 2m/Tau = 8 m/s"), FMath::IsNearlyEqual(V.X, 8.f, 1e-3f) && FMath::IsNearlyZero(V.Y));

	// Clamp regime: far away -> magnitude capped at MaxSpeed * CatchupBoost.
	const FVector2D VFar = FUnitServo::ComputeServoVelocity(FVector2D::ZeroVector, FVector2D(1000.f, 0.f),
		FUnitServoConstants::Tau, 5.f, FUnitServoConstants::CatchupBoost);
	TestTrue(TEXT("clamped at MaxSpeed*Boost (6.25)"), FMath::IsNearlyEqual(VFar.X, 5.f * 1.25f, 1e-3f));

	// Direction: diagonal target -> velocity points at the slot.
	const FVector2D VDiag = FUnitServo::ComputeServoVelocity(FVector2D(1.f, 1.f), FVector2D(3.f, 5.f),
		FUnitServoConstants::Tau, 100.f, 1.f);
	TestTrue(TEXT("direction toward slot"), VDiag.GetSafeNormal().Equals(FVector2D(2.f, 4.f).GetSafeNormal(), 1e-3f));

	// Acceleration clamp: a 100 m/s jump in one 16 ms tick is cut to MaxAccel*DT.
	const FVector2D VClamped = FUnitServo::ClampAcceleration(FVector2D::ZeroVector, FVector2D(100.f, 0.f), 4.f, 0.016f);
	TestTrue(TEXT("accel clamp bounds delta"), FMath::IsNearlyEqual(VClamped.X, 4.f * 0.016f, 1e-3f));
	// ...and small changes pass through untouched.
	const FVector2D VPass = FUnitServo::ClampAcceleration(FVector2D(1.f, 0.f), FVector2D(1.01f, 0.f), 4.f, 0.016f);
	TestTrue(TEXT("small accel passes"), FMath::IsNearlyEqual(VPass.X, 1.01f, 1e-4f));

	// cm/m boundary: velocities write back in cm/s (ground rule 2 golden).
	const float MaxSpeedCmS = 500.f; // fragment units
	const FVector2D VCm = FUnitServo::ComputeServoVelocity(FVector2D::ZeroVector, FVector2D(100.f, 0.f),
		FUnitServoConstants::Tau, MaxSpeedCmS * 0.01f, FUnitServoConstants::CatchupBoost);
	TestTrue(TEXT("cm/m: m/s result converts back"), FMath::IsNearlyEqual(VCm.X * 100.f, 625.f, 1e-2f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServo_FeedForward,
	"Battle.Unit.Servo.FeedForward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FServo_FeedForward::RunTest(const FString& Parameters)
{
	// Phase 2 A1: velocity matching on a moving slot. A slot marching at walk
	// speed with the entity exactly ON the slot must produce exactly the slot
	// velocity (the pure pursuit form would output 0 here and trail by Tau*v).
	const float DT = 1.f / 60.f;
	const float Walk = 1.4f;
	const FVector2D Target(10.f, 0.f);
	const FVector2D Prev = Target - FVector2D(Walk * DT, 0.f);
	const FVector2D VOnSlot = FUnitServo::ComputeServoVelocityFF(Target, Target, Prev,
		FUnitServoConstants::Tau, Walk, FUnitServoConstants::CatchupBoost, DT);
	TestTrue(TEXT("on-slot: output equals slot velocity (zero steady lag)"),
		VOnSlot.Equals(FVector2D(Walk, 0.f), 1e-3f));

	// The trailing entity (Tau*v behind) commands catch-up: slot velocity plus
	// the full pursuit term (no clamp at these magnitudes... 2.8 < 1.75x?
	// 1.4 + 0.35/0.25 = 2.8 > 1.75 -- CLAMPED). At member max the clamp binds;
	// use a higher ceiling to check the unclamped sum.
	const FVector2D PosTrailing = Target - FVector2D(FUnitServoConstants::Tau * Walk, 0.f);
	const FVector2D VTrailing = FUnitServo::ComputeServoVelocityFF(PosTrailing, Target, Prev,
		FUnitServoConstants::Tau, 10.f, FUnitServoConstants::CatchupBoost, DT);
	TestTrue(TEXT("trailing: slot velocity + full pursuit term"),
		VTrailing.Equals(FVector2D(Walk + (FUnitServoConstants::Tau * Walk) / FUnitServoConstants::Tau, 0.f), 1e-3f));

	// Clamp regime: the feed-forward sum can never exceed MaxSpeed * Boost.
	const FVector2D VClamped = FUnitServo::ComputeServoVelocityFF(FVector2D::ZeroVector,
		FVector2D(1000.f, 0.f), FVector2D(999.f, 0.f), FUnitServoConstants::Tau, Walk,
		FUnitServoConstants::CatchupBoost, DT);
	TestTrue(TEXT("clamped at MaxSpeed*Boost"),
		FMath::IsNearlyEqual(VClamped.X, Walk * FUnitServoConstants::CatchupBoost, 1e-3f));

	// Degenerate DT contributes no feed-forward (no NaN / no lurch); the
	// ceiling stays high enough that the pure pursuit term passes through.
	const FVector2D VZeroDt = FUnitServo::ComputeServoVelocityFF(FVector2D::ZeroVector, Target,
		FVector2D::ZeroVector, FUnitServoConstants::Tau, 100.f, 1.f, 0.f);
	TestTrue(TEXT("zero DT: pure pursuit fallback"), VZeroDt.Equals(FVector2D(10.f / FUnitServoConstants::Tau, 0.f), 1e-3f));

	// A stationary slot (Prev == Target) reduces exactly to the old math.
	const FVector2D VStatic = FUnitServo::ComputeServoVelocityFF(FVector2D(2.f, 0.f), FVector2D(10.f, 0.f),
		FVector2D(10.f, 0.f), FUnitServoConstants::Tau, 100.f, 1.f, DT);
	TestTrue(TEXT("static slot: identical to pure pursuit"),
		VStatic.Equals(FUnitServo::ComputeServoVelocity(FVector2D(2.f, 0.f), FVector2D(10.f, 0.f),
			FUnitServoConstants::Tau, 100.f, 1.f), 1e-3f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServo_PerStateAvoidanceWeights,
	"Battle.Unit.Servo.PerStateAvoidanceWeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FServo_PerStateAvoidanceWeights::RunTest(const FString& Parameters)
{
	// A conflict course: I march +X; a neighbor sits 1 m ahead, 0.3 m left,
	// stationary -- inside the predicted corridor.
	const FVector2D MyPos(0.f, 0.f);
	const FVector2D Desired(5.f, 0.f);
	TArray<FAvoidanceNeighbor> Neighbors;
	FAvoidanceNeighbor N;
	N.Pos = FVector2D(1.f, 0.3f);
	N.Vel = FVector2D::ZeroVector;
	N.Radius = 0.4f;
	N.HandleIndex = 99;
	Neighbors.Add(N);

	// HoldSlot (weight 0): no deflection even with the conflict.
	TestTrue(TEXT("HoldSlot: no deflection"),
		FUnitServo::ComputeAvoidanceDeflection(MyPos, Desired, 0.4f, 1, Neighbors, 0.f).IsNearlyZero());

	// CrossField (weight 1): deflection fires, perpendicular to the approach.
	const FVector2D DeflectFull = FUnitServo::ComputeAvoidanceDeflection(MyPos, Desired, 0.4f, 1, Neighbors, 1.0f);
	TestTrue(TEXT("CrossField: deflection fires"), !DeflectFull.IsNearlyZero());
	TestTrue(TEXT("deflection is lateral (no along-track push)"),
		FMath::IsNearlyZero(FVector2D::DotProduct(DeflectFull, Desired.GetSafeNormal()), 1e-2f));

	// ManeuverToSlot (weight 0.5): exactly half the CrossField deflection.
	const FVector2D DeflectMedium = FUnitServo::ComputeAvoidanceDeflection(MyPos, Desired, 0.4f, 1, Neighbors, 0.5f);
	TestTrue(TEXT("ManeuverToSlot = half of CrossField"),
		DeflectMedium.Equals(DeflectFull * 0.5f, 1e-3f));

	// Teammates-only filter is the CALLER's neighbor-gathering (§4.5): the
	// pure function honors whatever list it is given -- a filtered list
	// (teammates removed) produces zero deflection.
	TArray<FAvoidanceNeighbor> Empty;
	TestTrue(TEXT("filtered (teammates-only) list honored"),
		FUnitServo::ComputeAvoidanceDeflection(MyPos, Desired, 0.4f, 1, Empty, 0.5f).IsNearlyZero());

	// Asymmetric canonical-identity tie-break (RC8: the A13 canonical key
	// replaced the raw handle -- entity slots permute across a battle reset):
	// same conflict, both entities full weight -- the yielder (higher
	// canonical key) deflects 4x the priority entity (multipliers 2.0 vs 0.5).
	FAvoidanceNeighbor NLow = N;
	NLow.HandleIndex = 1;
	NLow.CanonicalKey = 1;
	FAvoidanceNeighbor NHigh = N;
	NHigh.HandleIndex = 2;
	NHigh.CanonicalKey = 2;
	const FVector2D DeflectAsPriority = FUnitServo::ComputeAvoidanceDeflection(FVector2D(0.f, 0.3f), FVector2D(5.f, 0.f), 0.4f, 1, { NHigh }, 1.0f);
	const FVector2D DeflectAsYielder = FUnitServo::ComputeAvoidanceDeflection(FVector2D(0.f, 0.3f), FVector2D(5.f, 0.f), 0.4f, 2, { NLow }, 1.0f);
	TestTrue(TEXT("yielder (higher handle) deflects 4x the priority entity"),
		FMath::IsNearlyEqual(DeflectAsPriority.Size() * 4.f, DeflectAsYielder.Size(), 1e-3f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FServo_UnstuckLadderEscalation,
	"Battle.Unit.Servo.UnstuckLadderEscalation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FServo_UnstuckLadderEscalation::RunTest(const FString& Parameters)
{
	// Slow + away from slot: the ladder escalates 0 -> 1 -> 2 -> 3 -> 4 and
	// TERMINATES at 4 (no unbounded growth).
	float Timer = 0.f;
	int32 Level = 0;
	int32 Steps = 0;
	while (Level < 4 && Steps < 100)
	{
		Level = FUnstuckLadder::Advance(Level, true, false, FUnitServoConstants::EscalateAfterSeconds * 1.1f,
			Timer, FUnitServoConstants::EscalateAfterSeconds);
		++Steps;
	}
	TestEqual(TEXT("ladder reaches terminal level 4"), Level, 4);
	// One more escalation stays clamped.
	Level = FUnstuckLadder::Advance(Level, true, false, FUnitServoConstants::EscalateAfterSeconds * 1.1f,
		Timer, FUnitServoConstants::EscalateAfterSeconds);
	TestEqual(TEXT("level 4 is terminal"), Level, 4);

	// Sub-threshold slowness does not escalate.
	Timer = 0.f;
	Level = 0;
	Level = FUnstuckLadder::Advance(0, true, false, FUnitServoConstants::EscalateAfterSeconds * 0.5f,
		Timer, FUnitServoConstants::EscalateAfterSeconds);
	TestEqual(TEXT("sub-threshold: no escalation, timer accumulates"), Level, 0);
	TestTrue(TEXT("timer accumulated"), FMath::IsNearlyEqual(Timer, FUnitServoConstants::EscalateAfterSeconds * 0.5f, 1e-3f));

	// Reaching the slot resets fully.
	Level = FUnstuckLadder::Advance(3, true, true, 0.016f, Timer, FUnitServoConstants::EscalateAfterSeconds);
	TestEqual(TEXT("near slot resets to 0"), Level, 0);
	TestTrue(TEXT("timer reset"), FMath::IsNearlyZero(Timer));

	// Step behavior hooks: level >= 1 doubles avoidance weight; level 2 wants
	// a sidestep.
	TestEqual(TEXT("level 0: weight x1"), FUnstuckLadder::AvoidanceWeightMultiplier(0), 1.f);
	TestEqual(TEXT("level 1: weight x2"), FUnstuckLadder::AvoidanceWeightMultiplier(1), 2.f);
	TestFalse(TEXT("level 1: no sidestep"), FUnstuckLadder::WantsSidestep(1));
	TestTrue(TEXT("level 2: sidestep"), FUnstuckLadder::WantsSidestep(2));

	// Audit M1: step 3 demands the forced slot swap with the blocking
	// teammate; steps 3/4 exist (the old code had four levels but only two
	// hooks -- levels 3/4 were no-ops).
	TestFalse(TEXT("level 2: no slot swap"), FUnstuckLadder::WantsSlotSwap(2));
	TestTrue(TEXT("level 3: slot swap"), FUnstuckLadder::WantsSlotSwap(3));
	TestTrue(TEXT("level 4: slot swap"), FUnstuckLadder::WantsSlotSwap(4));

	// Moving fine holds the current level (no unwind without slot arrival).
	Level = FUnstuckLadder::Advance(2, false, false, 0.016f, Timer, FUnitServoConstants::EscalateAfterSeconds);
	TestEqual(TEXT("moving fine holds level"), Level, 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
