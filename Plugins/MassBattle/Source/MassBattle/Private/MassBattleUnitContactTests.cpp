// Battle.Unit.Contact.* -- unit-vs-unit OBB contact + policy table + shove
// blend (arch §8.6, gate T1.6 support). Includes the scripted two-unit wake
// recovery case: shove displaces the infantry, the servo recovers once the
// cavalry passes (all in pure-math, meters).
#include "Misc/AutomationTest.h"

#include "BattleUnitContact.h"
#include "BattleServo.h"
#include "BattleCompression.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContact_OBBOverlapGolden,
	"Battle.Unit.Contact.OBBOverlapGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContact_OBBOverlapGolden::RunTest(const FString& Parameters)
{
	FUnitOBB A;
	A.Center = FVector2D::ZeroVector;
	A.HalfFrontage = 5.f;
	A.HalfDepth = 1.f;
	A.YawDeg = 0.f;

	// Separated along X: no overlap.
	FUnitOBB B = A;
	B.Center = FVector2D(11.f, 0.f);
	TestEqual(TEXT("separated -> depth 0"), FUnitContact::OverlapDepth(A, B), 0.f);

	// Overlapping along X by 1 m (centers 9 m apart, halves sum 10 m).
	B.Center = FVector2D(9.f, 0.f);
	TestTrue(TEXT("head-on overlap depth 1"), FMath::IsNearlyEqual(FUnitContact::OverlapDepth(A, B), 1.f, 1e-3f));

	// Touching (exactly 10 m apart) counts as no penetration.
	B.Center = FVector2D(10.f, 0.f);
	TestEqual(TEXT("touching -> depth 0"), FUnitContact::OverlapDepth(A, B), 0.f);

	// Lateral overlap: side by side, 1.5 m apart, halves-depth sum 2 m -> 0.5 m.
	B.Center = FVector2D(0.f, 1.5f);
	TestTrue(TEXT("lateral overlap depth 0.5"), FMath::IsNearlyEqual(FUnitContact::OverlapDepth(A, B), 0.5f, 1e-3f));

	// Rotated crossing at right angles: depth on the tight axes.
	FUnitOBB C;
	C.Center = FVector2D(2.f, 0.f);
	C.HalfFrontage = 5.f;
	C.HalfDepth = 1.f;
	C.YawDeg = 90.f;
	TestTrue(TEXT("T-crossing overlaps"), FUnitContact::OverlapDepth(A, C) > 0.f);

	// Point containment: inside, outside, corner.
	TestTrue(TEXT("point inside"), FUnitContact::ContainsPoint(A, FVector2D(3.f, 0.5f)));
	TestFalse(TEXT("point outside"), FUnitContact::ContainsPoint(A, FVector2D(6.f, 0.f)));
	TestFalse(TEXT("point beyond corner"), FUnitContact::ContainsPoint(A, FVector2D(4.5f, 1.5f)));

	// §8.2 hard rule key: interpenetrating predicate matches the depth test.
	TestTrue(TEXT("interpenetrating true"), FUnitContact::Interpenetrating(A, C));
	TestFalse(TEXT("interpenetrating false"), FUnitContact::Interpenetrating(A, FUnitOBB{ FVector2D(50.f, 50.f), 5.f, 1.f, 0.f }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContact_PolicyTable,
	"Battle.Unit.Contact.PolicyTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContact_PolicyTable::RunTest(const FString& Parameters)
{
	using ECP = FUnitContact::EContactPolicy;
	// Hostile: always Block at Phase 1 (melee is Phase 2).
	TestEqual(TEXT("hostile -> block"), FUnitContact::ResolvePolicy(false, true, true), ECP::Block);
	// Friendly + mobility + right-of-way -> pass-through (cavalry over infantry).
	TestEqual(TEXT("cavalry pass -> PassThrough"), FUnitContact::ResolvePolicy(true, true, true), ECP::PassThrough);
	// Friendly + mobility, no right-of-way -> yield.
	TestEqual(TEXT("no right of way -> Yield"), FUnitContact::ResolvePolicy(true, true, false), ECP::Yield);
	// Friendly equal footing -> block (formations never merge).
	TestEqual(TEXT("equal friendly -> Block"), FUnitContact::ResolvePolicy(true, false, true), ECP::Block);

	// Shove blend bounds: clamped to [0,1]; mass and depth multiply.
	TestEqual(TEXT("zero depth -> 0"), FUnitContact::ShoveBlend(2.f, 0.f), 0.f);
	TestTrue(TEXT("shallow overlap -> 2*0.3=0.6"), FMath::IsNearlyEqual(FUnitContact::ShoveBlend(2.f, 0.3f), 0.6f, 1e-3f));
	TestEqual(TEXT("deep overlap clamps to 1"), FUnitContact::ShoveBlend(2.f, 2.f), 1.f);

	// Pass-through tuning constants (§8.6: ~50-60% speed, lowered floor).
	TestTrue(TEXT("speed factor in the 50-60% band"),
		FUnitContact::PassThroughSpeedFactor >= 0.5f && FUnitContact::PassThroughSpeedFactor <= 0.6f);
	TestTrue(TEXT("lowered floor below the 60% nominal"),
		FUnitContact::PassThroughCompressionFloor < FUnitCompression::CompressionFloor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContact_WakeRecoveryScripted,
	"Battle.Unit.Contact.WakeRecoveryScripted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContact_WakeRecoveryScripted::RunTest(const FString& Parameters)
{
	// Scripted two-unit case: cavalry OBB plows through an infantry entity's
	// slot; while overlapping the entity is displaced aside (shove blend);
	// once the cavalry passes, the servo alone re-dresses the wake to the
	// slot within T ticks (T1.6's "wake re-dresses within T", in miniature).
	FUnitOBB Cavalry;
	Cavalry.HalfFrontage = 3.f;
	Cavalry.HalfDepth = 1.f;
	Cavalry.YawDeg = 0.f;
	const FVector2D InfantrySlot(0.f, 0.f);
	FVector2D InfantryPos = InfantrySlot;

	const float DT = 1.f / 30.f;
	const float CavalrySpeed = 5.f * FUnitContact::PassThroughSpeedFactor; // m/s along +X
	Cavalry.Center = FVector2D(-8.f, 0.f);

	bool bWasShoved = false;
	int32 Tick = 0;
	const int32 MaxTicks = 900; // 30 s
	while (Tick < MaxTicks)
	{
		Cavalry.Center.X += CavalrySpeed * DT;
		++Tick;

		const float Depth = FUnitContact::OverlapDepth(Cavalry, FUnitOBB{ InfantryPos, 0.4f, 0.4f, 0.f });
		const float Blend = FUnitContact::ShoveBlend(2.0f, Depth);
		FVector2D NewVel;
		if (Blend > 0.f && FUnitContact::ContainsPoint(Cavalry, InfantryPos))
		{
			// Shoved aside, perpendicular to the cavalry facing.
			NewVel = FVector2D(0.f, InfantryPos.Y >= 0.f ? 1.f : -1.f) * FUnitContact::ShoveSpeed * Blend;
			bWasShoved = true;
		}
		else
		{
			// Servo toward the slot (§8.3), acceleration-clamped.
			NewVel = FUnitServo::ComputeServoVelocity(InfantryPos, InfantrySlot,
				FUnitServoConstants::Tau, 5.f, FUnitServoConstants::CatchupBoost);
		}
		InfantryPos += NewVel * DT;

		if (bWasShoved && Cavalry.Center.X > InfantryPos.X + Cavalry.HalfFrontage + 1.f)
		{
			break; // cavalry has passed
		}
	}

	TestTrue(TEXT("shove actually engaged"), bWasShoved);
	TestTrue(FString::Printf(TEXT("wake re-dressed within T ticks (took %d)"), Tick),
		FMath::IsNearlyEqual(InfantryPos.X, InfantrySlot.X, 0.35f)
		&& FMath::IsNearlyEqual(InfantryPos.Y, InfantrySlot.Y, 0.35f));

	return true;
}
