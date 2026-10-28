#if WITH_DEV_AUTOMATION_TESTS
// MassBattleRangedTests.cpp -- Phase 3 ranged gates (spec ranged-stack §7).
// T3.1 (Battle.Unit.Ranged.Ballistics): the solver + dispersion goldens.
// T3.2 (Battle.Unit.Ranged.LOS): heightfield march golden hill cases.
// T3.3 family (ammo/pool/volley/skirmish): unit-locked below.
//
// Golden posture: solver asserts are closed-form property checks with
// tolerances (1e-3 analytic, 0.35 m for stepped landings -- the sim's own
// semi-implicit integrator carries a g*T*DT/2 discretization band; the
// IMPACT system must consume the stepper's truth, not the analytic one).
#include "Misc/AutomationTest.h"

#include "BattleBallistics.h"
#include "BattleCostGrid.h"
#include "BattleRanged.h"

namespace
{
	/** Deterministic hill stamp for LOS fixtures: every cell whose center is
	 *  inside the world-space box gets the height (test-local; the shipped
	 *  stamping surface stays cost-shaped). */
	void StampHill(FBattleCostGrid& Grid, const FVector2D& BoxMin, const FVector2D& BoxMax, float Height)
	{
		int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
		Grid.WorldToCell(BoxMin, X0, Y0);
		Grid.WorldToCell(BoxMax, X1, Y1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				if (Grid.IsInside(X, Y))
				{
					Grid.CellHeight[Grid.IndexOf(X, Y)] = Height;
				}
			}
		}
	}

	/** Step a solved shot with the SEAM's own stepper for the flight time. */
	FVector StepFlight(const FBallisticSolution& Solution, const FVector2D& FromM, float FromZM,
		float SpeedMps, float GravityMps2, float DT)
	{
		FVector Pos(FromM.X, FromM.Y, FromZM);
		FVector Vel = Solution.LaunchDir * SpeedMps;
		int32 Steps = FMath::FloorToInt(Solution.FlightTimeS / DT);
		for (int32 i = 0; i < Steps; ++i)
		{
			FBallisticsMath::StepProjectile(Pos, Vel, GravityMps2, DT);
		}
		return Pos;
	}
}

// ---------------------------------------------------------------------------
// T3.1: Battle.Unit.Ranged.Ballistics -- solver, dispersion, stepper.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_Ballistics,
	"Battle.Unit.Ranged.Ballistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_Ballistics::RunTest(const FString& Parameters)
{
	using namespace FBallisticsMath;

	// -- level ground at the 45-degree equivalence point (v^2/g = d): the
	//    hand golden. v=10, g=10, d=10 -> theta = 45deg, T = sqrt(2).
	{
		const FBallisticSolution S = SolveLaunch(FVector2D::ZeroVector, 1.6f, FVector2D(10.f, 0.f), 1.6f, 10.f, 10.f);
		TestTrue(TEXT("level 45deg solution valid"), S.bValid);
		TestEqual(TEXT("level 45deg dir.X = cos(45)"), S.LaunchDir.X, 0.70710678f, 1e-3f);
		TestEqual(TEXT("level 45deg dir.Z = sin(45)"), S.LaunchDir.Z, 0.70710678f, 1e-3f);
		TestEqual(TEXT("level 45deg flight time = sqrt(2)"), S.FlightTimeS, 1.41421356f, 1e-3f);
		TestEqual(TEXT("launch dir is unit"), S.LaunchDir.Size(), 1.f, 1e-4f);
	}

	// -- out of envelope: max level range is v^2/g; beyond it, invalid (the
	//    volley pass holds fire, never force-fires).
	{
		const FBallisticSolution S = SolveLaunch(FVector2D::ZeroVector, 1.6f, FVector2D(30.f, 0.f), 1.6f, 5.f, 9.81f);
		TestFalse(TEXT("out-of-envelope shot is invalid"), S.bValid);
	}

	// -- stepped-landing property at the profile speeds: the sim's own
	//    integrator lands on the target within the discretization band.
	{
		const FProjectileProfile Arrow = GetProjectileProfile(EBattleProjectileKind::Arrow);
		const FVector2D From(0.f, 0.f);
		const FVector2D To(80.f, 0.f);
		const FBallisticSolution S = SolveLaunch(From, 1.6f, To, 1.0f, Arrow.MuzzleSpeedMps, FBallisticsConstants::GravityMps2 * Arrow.GravityScale);
		TestTrue(TEXT("arrow solution at 80 m valid"), S.bValid);
		const FVector Landed = StepFlight(S, From, 1.6f, Arrow.MuzzleSpeedMps,
			FBallisticsConstants::GravityMps2 * Arrow.GravityScale, 1.f / 60.f);
		TestEqual(TEXT("arrow stepped landing X"), Landed.X, To.X, 0.35f);
		TestEqual(TEXT("arrow stepped landing Z"), Landed.Z, 1.0f, 0.35f);

		const FProjectileProfile Bolt = GetProjectileProfile(EBattleProjectileKind::Bolt);
		const FBallisticSolution B = SolveLaunch(From, 1.6f, To, 1.0f, Bolt.MuzzleSpeedMps, FBallisticsConstants::GravityMps2 * Bolt.GravityScale);
		TestTrue(TEXT("bolt solution at 80 m valid"), B.bValid);
		const FVector LandedB = StepFlight(B, From, 1.6f, Bolt.MuzzleSpeedMps,
			FBallisticsConstants::GravityMps2 * Bolt.GravityScale, 1.f / 60.f);
		TestEqual(TEXT("bolt stepped landing X"), LandedB.X, To.X, 0.35f);
		TestEqual(TEXT("bolt stepped landing Z"), LandedB.Z, 1.0f, 0.35f);
	}

	// -- RC2: ONE solver, profile-speed flatness. Same shot, higher speed ->
	//    strictly flatter launch (the bolt's near-flat path is a data
	//    property, not a second integrator).
	{
		const FVector2D From(0.f, 0.f);
		const FVector2D To(60.f, 0.f);
		const FBallisticSolution Slow = SolveLaunch(From, 1.6f, To, 1.6f, 35.f, 9.81f);
		const FBallisticSolution Fast = SolveLaunch(From, 1.6f, To, 1.6f, 60.f, 9.81f);
		TestTrue(TEXT("both solutions valid"), Slow.bValid && Fast.bValid);
		TestTrue(TEXT("higher speed -> flatter launch (RC2)"), Fast.LaunchDir.Z < Slow.LaunchDir.Z);
		TestTrue(TEXT("higher speed -> shorter flight"), Fast.FlightTimeS < Slow.FlightTimeS);
	}

	// -- determinism: identical inputs -> identical bits (two calls).
	{
		const FBallisticSolution A = SolveLaunch(FVector2D(3.f, -7.f), 1.6f, FVector2D(65.f, 12.f), 2.2f, 35.f, 9.81f);
		const FBallisticSolution B = SolveLaunch(FVector2D(3.f, -7.f), 1.6f, FVector2D(65.f, 12.f), 2.2f, 35.f, 9.81f);
		TestEqual(TEXT("solver deterministic: dir.X"), A.LaunchDir.X, B.LaunchDir.X);
		TestEqual(TEXT("solver deterministic: dir.Y"), A.LaunchDir.Y, B.LaunchDir.Y);
		TestEqual(TEXT("solver deterministic: dir.Z"), A.LaunchDir.Z, B.LaunchDir.Z);
		TestEqual(TEXT("solver deterministic: flight time"), A.FlightTimeS, B.FlightTimeS);
	}

	// -- dispersion: bounded by the cone, deterministic per seed, zero cone
	//    is identity (RC1).
	{
		const FVector Dir = FVector(1.f, 0.f, 0.f);
		const float ConeDeg = 4.f; // arrow profile placeholder
		float MaxAngleDeg = 0.f;
		bool bAllDeterministic = true;
		bool bSpread = false;
		for (uint32 i = 0; i < 256; ++i)
		{
			const FVector P1 = PerturbDirection(Dir, ConeDeg, i);
			const FVector P2 = PerturbDirection(Dir, ConeDeg, i);
			bAllDeterministic = bAllDeterministic && P1.Equals(P2, 1e-6f);
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, P1), -1.f, 1.f)));
			MaxAngleDeg = FMath::Max(MaxAngleDeg, AngleDeg);
			bSpread = bSpread || (AngleDeg > ConeDeg * 0.25f); // not all collapsed at the axis
		}
		TestTrue(TEXT("dispersion deterministic per seed"), bAllDeterministic);
		TestTrue(TEXT("dispersion within the cone"), MaxAngleDeg <= ConeDeg + 0.25f);
		TestTrue(TEXT("dispersion actually spreads"), bSpread);
		TestTrue(TEXT("zero cone is identity"),
			PerturbDirection(Dir, 0.f, 12345u).Equals(Dir, 1e-6f));
	}

	// -- shot seed: run-invariant inputs -> deterministic, distinct per shot.
	{
		const uint32 S0 = ShotSeed(7u, 3u, 0);
		const uint32 S0Again = ShotSeed(7u, 3u, 0);
		const uint32 S1 = ShotSeed(7u, 3u, 1);
		TestEqual(TEXT("shot seed deterministic"), S0, S0Again);
		TestTrue(TEXT("shot seed differs per shot"), S0 != S1);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T3.2: Battle.Unit.Ranged.LOS -- heightfield march golden hill cases.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_LOS,
	"Battle.Unit.Ranged.LOS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_LOS::RunTest(const FString& Parameters)
{
	using namespace FBallisticsMath;

	// The subsystem default shape: 86x86 cells, 3 m, centered at world 0.
	FBattleCostGrid Grid;
	Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);

	// -- flat ground never occludes at equal height (RC3's epsilon rule).
	TestTrue(TEXT("flat: 50 m shot is clear"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 1.6f, 0.3f));
	TestTrue(TEXT("flat: long diagonal shot is clear"),
		MarchClear(Grid, FVector2D(-60.f, -40.f), 1.6f, FVector2D(60.f, 45.f), 1.6f, 0.3f));

	// -- a hill between shooter and target blocks the flat lane.
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 5.f);
	TestFalse(TEXT("hill 5 m blocks a 1.6 m eye line"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 1.6f, 0.3f));

	// -- a low bump does not block.
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 0.5f);
	TestTrue(TEXT("bump 0.5 m is clear under a 1.6 m eye line"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 1.6f, 0.3f));

	// -- the equal-height rule: terrain EXACTLY at the ray height does not
	//    block (occlusion is strictly-greater, the RC3 epsilon rule).
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 1.6f);
	TestTrue(TEXT("terrain at ray height does not false-occlude"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 1.6f, 0.0f));

	// -- shooting AT a hilltop target: the ray rises above a mid-slope hill.
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 10.f);
	TestTrue(TEXT("rising lane over a 10 m mid-hill to a 20 m target is clear"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 20.f, 0.3f));
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 11.f);
	TestFalse(TEXT("an 11 m mid-hill blocks the rising lane"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 20.f, 0.3f));

	// -- bilinear SampleHeight: exact at a cell center, averaged at a corner.
	{
		FBattleCostGrid HGrid;
		HGrid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		int32 CX = 0, CY = 0;
		HGrid.WorldToCell(FVector2D(10.f, 10.f), CX, CY);
		HGrid.CellHeight[HGrid.IndexOf(CX, CY)] = 4.f;
		const FVector2D Center = HGrid.CellCenter(CX, CY);
		TestEqual(TEXT("SampleHeight is exact at a cell center"), HGrid.SampleHeight(Center), 4.f, 1e-4f);
		TestEqual(TEXT("SampleHeight fades off the hill"), HGrid.SampleHeight(Center + FVector2D(6.f, 0.f)), 0.f, 1e-4f);
		// A corner between the 4 m cell and three flat cells averages ~1 m.
		const FVector2D Corner = Center + FVector2D(1.5f, 1.5f);
		TestEqual(TEXT("SampleHeight bilinear at a cell corner"), HGrid.SampleHeight(Corner), 1.f, 1e-3f);
	}

	// -- march determinism: two identical calls agree.
	{
		const bool A = MarchClear(Grid, FVector2D(-5.f, 2.f), 1.6f, FVector2D(45.f, -8.f), 1.2f, 0.3f);
		const bool B = MarchClear(Grid, FVector2D(-5.f, 2.f), 1.6f, FVector2D(45.f, -8.f), 1.2f, 0.3f);
		TestEqual(TEXT("march deterministic"), A, B);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
