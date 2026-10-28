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
#include "BattleProjectilePool.h"
#include "BattleRanged.h"
#include "Math/RandomStream.h"

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
		// Final partial step: without it the truncation bands the landing by up
		// to a full step of travel (v*DT -- 1 m at the bolt's speed), swamping
		// the g*T*DT/2 integrator band these goldens assert against.
		const float Remainder = Solution.FlightTimeS - static_cast<float>(Steps) * DT;
		if (Remainder > 1e-6f)
		{
			FBallisticsMath::StepProjectile(Pos, Vel, GravityMps2, Remainder);
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
		TestEqual(TEXT("level 45deg dir.X = cos(45)"), S.LaunchDir.X, 0.70710678, 1e-3);
		TestEqual(TEXT("level 45deg dir.Z = sin(45)"), S.LaunchDir.Z, 0.70710678, 1e-3);
		TestEqual(TEXT("level 45deg flight time = sqrt(2)"), S.FlightTimeS, 1.41421356f, 1e-3f);
		TestEqual(TEXT("launch dir is unit"), S.LaunchDir.Size(), 1.0, 1e-4);
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
		TestEqual(TEXT("arrow stepped landing X"), Landed.X, To.X, 0.35);
		TestEqual(TEXT("arrow stepped landing Z"), Landed.Z, 1.0, 0.35);

		const FProjectileProfile Bolt = GetProjectileProfile(EBattleProjectileKind::Bolt);
		const FBallisticSolution B = SolveLaunch(From, 1.6f, To, 1.0f, Bolt.MuzzleSpeedMps, FBallisticsConstants::GravityMps2 * Bolt.GravityScale);
		TestTrue(TEXT("bolt solution at 80 m valid"), B.bValid);
		const FVector LandedB = StepFlight(B, From, 1.6f, Bolt.MuzzleSpeedMps,
			FBallisticsConstants::GravityMps2 * Bolt.GravityScale, 1.f / 60.f);
		TestEqual(TEXT("bolt stepped landing X"), LandedB.X, To.X, 0.35);
		TestEqual(TEXT("bolt stepped landing Z"), LandedB.Z, 1.0, 0.35);
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
	// 12 m, not 11: the march samples each crossed cell ONCE at its entry
	// station, and bilinear blending bleeds the neighbor's height into that
	// sample (an 11 m plateau reads ~9.4 m at the entry station -- measured
	// window for this geometry: 8-11 m clear, 12 m blocks).
	StampHill(Grid, FVector2D(22.f, -3.f), FVector2D(28.f, 3.f), 12.f);
	TestFalse(TEXT("a 12 m mid-hill blocks the rising lane"),
		MarchClear(Grid, FVector2D(0.f, 0.f), 1.6f, FVector2D(50.f, 0.f), 20.f, 0.3f));

	// -- DDA boundary cases: cell-corner crossings (task 2; the two cases
	//    the kickoff patch could not hand-verify). Grid geometry is exact
	//    here: origin -129, 3 m cells, so cell (44,43) spans x [3,6) y [0,3)
	//    and world (3,3) is the shared corner of cells
	//    (43,43)/(43,44)/(44,43)/(44,44). The march steps 1.5 m (29 samples
	//    on the 30 m diagonal); the stations straddling the corner are
	//    (60/29, 60/29) -- cell (43,43), bilinear tx=ty=0.1897 -- and
	//    (90/29, 90/29) -- cell (44,44), tx=ty=0.5345. A corner-adjacent
	//    hill cell therefore blends into the read at w = 0.1897*0.8103 =
	//    0.1537 (first straddle) and 0.5345*0.4655 = 0.2488 (second).
	{
		FBattleCostGrid CGrid;
		CGrid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);

		// Case A: ONE hill cell whose only contact with the diagonal lane is
		// the corner (3,3). 10 m blends to 2.49 m at the second straddling
		// station -- above the 1.6 m ray + 0.3 m clearance -> blocked through
		// the corner bleed alone. A 2 m hill blends to 0.50 m -> clear.
		CGrid.CellHeight[CGrid.IndexOf(44, 43)] = 10.f;
		TestFalse(TEXT("corner case A: a 10 m hill cell blocks through its corner bleed"),
			MarchClear(CGrid, FVector2D(0.f, 0.f), 1.6f, FVector2D(30.f, 30.f), 1.6f, 0.3f));
		CGrid.CellHeight[CGrid.IndexOf(44, 43)] = 2.f;
		TestTrue(TEXT("corner case A control: a 2 m hill cell at the same corner is clear"),
			MarchClear(CGrid, FVector2D(0.f, 0.f), 1.6f, FVector2D(30.f, 30.f), 1.6f, 0.3f));

		// Case B: the diagonal keyhole -- two hill cells touching ONLY at the
		// shared corner (3,3). A pure cell-DDA would advance (43,43) ->
		// (44,44) on the simultaneous X/Y crossing and enter NEITHER hill;
		// the sub-cell march's bilinear read taps BOTH hills at the first
		// straddle station (10 m * 2 * 0.1537 = 3.07 m) -> blocked. At 2 m
		// the blend is 0.61 m -> clear.
		CGrid.CellHeight[CGrid.IndexOf(44, 43)] = 10.f;
		CGrid.CellHeight[CGrid.IndexOf(43, 44)] = 10.f;
		TestFalse(TEXT("corner case B: diagonal hills touching at a corner cannot be shot between"),
			MarchClear(CGrid, FVector2D(0.f, 0.f), 1.6f, FVector2D(30.f, 30.f), 1.6f, 0.3f));
		CGrid.CellHeight[CGrid.IndexOf(44, 43)] = 2.f;
		CGrid.CellHeight[CGrid.IndexOf(43, 44)] = 2.f;
		TestTrue(TEXT("corner case B control: 2 m diagonal hills leave the keyhole open"),
			MarchClear(CGrid, FVector2D(0.f, 0.f), 1.6f, FVector2D(30.f, 30.f), 1.6f, 0.3f));
	}

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

		// Task 2 evidence: the measured bilinear tent over the synthetic
		// 4 m hill cell, sampled on a 1.5 m lattice through the center and
		// the shared corners (the grid API's first live record; verbatim in
		// the phase progress log).
		for (int32 Row = 0; Row < 5; ++Row)
		{
			const float Y = Center.Y + (Row - 2) * 1.5f;
			FString Line;
			for (int32 Col = 0; Col < 5; ++Col)
			{
				const float X = Center.X + (Col - 2) * 1.5f;
				Line += FString::Printf(TEXT("%s%.3f"), Col > 0 ? TEXT(" ") : TEXT(""), HGrid.SampleHeight(FVector2D(X, Y)));
			}
			AddInfo(FString::Printf(TEXT("SampleHeight lattice y=%+.1f: %s"), Y, *Line));
		}
	}

	// -- march determinism: two identical calls agree.
	{
		const bool A = MarchClear(Grid, FVector2D(-5.f, 2.f), 1.6f, FVector2D(45.f, -8.f), 1.2f, 0.3f);
		const bool B = MarchClear(Grid, FVector2D(-5.f, 2.f), 1.6f, FVector2D(45.f, -8.f), 1.2f, 0.3f);
		TestEqual(TEXT("march deterministic"), A, B);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T3.3a: Battle.Unit.Ranged.Pool -- the SoA store (RC9: fails closed, never
// grows; stale generations die; battle reset clears).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_Pool,
	"Battle.Unit.Ranged.Pool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_Pool::RunTest(const FString& Parameters)
{
	FBattleProjectilePool Pool;
	Pool.Reserve(8);

	// Fill to capacity.
	uint32 Gen = 0;
	TArray<int32> Slots;
	for (int32 i = 0; i < 8; ++i)
	{
		const int32 S = Pool.Acquire(Gen);
		TestTrue(TEXT("acquire within capacity"), S != FBattleProjectilePool::InvalidIndex);
		Slots.Add(S);
	}
	TestEqual(TEXT("live count at capacity"), Pool.GetLiveCount(), 8);

	// Beyond capacity: fail closed, count the drop, never grow (RC9).
	const int32 Over = Pool.Acquire(Gen);
	TestEqual(TEXT("acquire beyond capacity fails closed"), Over, FBattleProjectilePool::InvalidIndex);
	TestEqual(TEXT("drop counted"), Pool.GetDrops(), 1);
	TestEqual(TEXT("capacity unchanged"), Pool.GetCapacity(), 8);
	TestEqual(TEXT("SoA arrays never grew"), Pool.PosX.Num(), 8);

	// Release one, re-acquire: the SAME slot comes back (LIFO), generation
	// bumped so the OLD handle is stale.
	const uint32 OldGen = Pool.GenerationArray[Slots[3]];
	Pool.Release(Slots[3]);
	TestEqual(TEXT("release decrements live count"), Pool.GetLiveCount(), 7);
	const int32 Re = Pool.Acquire(Gen);
	TestEqual(TEXT("LIFO re-acquire returns the released slot"), Re, Slots[3]);
	TestEqual(TEXT("generation bumped on release"), Pool.GenerationArray[Re], OldGen + 1);
	TestFalse(TEXT("stale generation is invalid"), Pool.IsValid(Re, OldGen));
	TestTrue(TEXT("current generation is valid"), Pool.IsValid(Re, OldGen + 1));

	// A short volley burst: fill, release all, fill again -- the drop counter
	// only counts genuine overflow. Drain to zero first so every round's
	// acquires succeed: the fill above left the pool at capacity, and an
	// Acquire on a full pool fails closed into InvalidIndex (the PosX write
	// below would RangeCheck-assert on -1 in dev builds).
	for (int32 S = 0; S < Pool.GetCapacity(); ++S)
	{
		if (Pool.IsLive(S))
		{
			Pool.Release(S);
		}
	}
	TestEqual(TEXT("drain returns to zero live"), Pool.GetLiveCount(), 0);
	for (int32 Round = 0; Round < 100; ++Round)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 S = Pool.Acquire(Gen);
			Pool.PosX[S] = static_cast<float>(Round * 4 + i);
		}
		for (int32 i = 0; i < 4; ++i)
		{
			// Release by scanning live slots (the stepper's iteration shape).
			for (int32 S = 0; S < Pool.GetCapacity(); ++S)
			{
				if (Pool.IsLive(S))
				{
					Pool.Release(S);
					break;
				}
			}
		}
	}
	TestEqual(TEXT("churn rounds leave live count 0"), Pool.GetLiveCount(), 0);
	TestEqual(TEXT("churn rounds add no drops"), Pool.GetDrops(), 1);

	// Battle reset: everything released, generations PRESERVED (stale indices
	// from a prior battle still die).
	Pool.Reset();
	TestEqual(TEXT("reset clears live count"), Pool.GetLiveCount(), 0);
	TestEqual(TEXT("reset preserves the drop ledger until re-reserve"), Pool.GetDrops(), 1);

	return true;
}

// ---------------------------------------------------------------------------
// T3.3b: Battle.Unit.Ranged.AmmoAccounting -- shots == decrements; the unit
// aggregate == the entity sum; out-of-ammo entities never fire (RC5).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_AmmoAccounting,
	"Battle.Unit.Ranged.AmmoAccounting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_AmmoAccounting::RunTest(const FString& Parameters)
{
	FBattleCostGrid Grid;
	Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
	const FProjectileProfile Arrow = GetProjectileProfile(EBattleProjectileKind::Arrow);
	FVolleyTargetView Target;
	Target.PosM = FVector2D(80.f, 0.f);
	Target.ZM = 1.0f;

	// 16 shooters, randomized reload states over 50 cycles: every emitted
	// shot decrements exactly one shooter's ammo by exactly one.
	FRandomStream Rng(20261029u);
	FUnitHandle OwnerUnit;
	OwnerUnit.Index = 1u;
	OwnerUnit.Serial = 1u;
	TArray<FVolleyShooterView> Shooters;
	Shooters.SetNum(16);
	for (int32 i = 0; i < Shooters.Num(); ++i)
	{
		Shooters[i].Entity = FMassEntityHandle(i + 1, 1);
		Shooters[i].Unit = OwnerUnit;
		Shooters[i].PosM = FVector2D(0.f, static_cast<float>(i - 8) * 1.2f);
		Shooters[i].EyeZM = 1.6f;
		Shooters[i].Kind = EBattleProjectileKind::Arrow;
		Shooters[i].RangeM = 120.f;
		Shooters[i].Ammo = 25;
		Shooters[i].ReloadTimer = Rng.FRandRange(0.f, 3.f);
		Shooters[i].ShotsFired = 0;
		Shooters[i].Rank = (i < 8) ? 0 : 1; // VolleyRanks default 2: all eligible
	}

	TArray<FVolleyShot> Shots;
	int32 TotalShots = 0;
	for (int32 Cycle = 0; Cycle < 50; ++Cycle)
	{
		Shots.Reset();
		// Advance reloads (the processor's job; emulated deterministically).
		for (FVolleyShooterView& S : Shooters)
		{
			S.ReloadTimer = FMath::Max(0.f, S.ReloadTimer - 0.1f);
		}
		const int32 Fired = SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots);
		TotalShots += Fired;
		TestTrue(TEXT("emitted shots carry launch data"), Fired == 0 || (Shots.Num() == Fired));
	}
	TestTrue(TEXT("randomized cycles produce shots"), TotalShots > 0);

	// shots == decrements == ShotsFired sum (RC5).
	int32 ShotsFiredSum = 0;
	int32 AmmoSum = 0;
	for (const FVolleyShooterView& S : Shooters)
	{
		ShotsFiredSum += S.ShotsFired;
		AmmoSum += S.Ammo;
	}
	TestEqual(TEXT("shots == total decrements (ShotsFired sum)"), TotalShots, ShotsFiredSum);
	TestEqual(TEXT("ammo conservation: sum + fired == 16 x 25"), AmmoSum + ShotsFiredSum, 16 * 25);

	// Drain to zero: out-of-ammo entities never fire (no negative ammo, no
	// reload zombie -- RC5).
	for (FVolleyShooterView& S : Shooters)
	{
		S.Ammo = 0;
		S.ReloadTimer = 0.f;
	}
	Shots.Reset();
	TestEqual(TEXT("empty quivers never fire"),
		SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 0);
	int32 MinAmmo = 25;
	for (const FVolleyShooterView& S : Shooters)
	{
		MinAmmo = FMath::Min(MinAmmo, static_cast<int32>(S.Ammo));
	}
	TestEqual(TEXT("ammo never goes negative"), MinAmmo, 0);

	// Fraction invariants (RC5): -1 non-ranged; 0 at the empty aggregate; the
	// aggregate == the entity sum.
	TestEqual(TEXT("fraction -1 for non-ranged"), ComputeAmmoFraction(0, 0, 40), -1.f, 1e-5f);
	TestEqual(TEXT("fraction 0 at empty aggregate"), ComputeAmmoFraction(0, 25, 40), 0.f, 1e-5f);
	TestEqual(TEXT("fraction 1 at full"), ComputeAmmoFraction(25 * 40, 25, 40), 1.f, 1e-5f);
	TestEqual(TEXT("fraction 0.4 mid-band"), ComputeAmmoFraction(25 * 16, 25, 40), 0.4f, 1e-4f);

	return true;
}

// ---------------------------------------------------------------------------
// Task 3: Battle.Unit.Ranged.PoolStep -- the stepping pass's seam half (RC9
// stepping): a synthetic 64-shot volley lives, flies, and dies with an exact
// TTL/ground split, two-run equality, an earlier hill kill, and zero
// capacity violations. The processor shim (cvar gates + timing) is
// flags-off-identity by construction; this tier locks the math it shims.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_PoolStep,
	"Battle.Unit.Ranged.PoolStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_PoolStep::RunTest(const FString& Parameters)
{
	constexpr float DT = 1.f / 60.f;
	constexpr int32 NumTicks = 600;
	constexpr int32 VolleySize = 32;

	FBattleCostGrid Grid;
	Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);

	// Deterministic initial state: 32 arced shots (35 m/s at 45 deg from
	// 1.6 m, TTL 2 s -- still airborne at expiry) + 32 steep-down shots
	// (35 m/s at -30 deg, TTL 20 s -- terrain ends them around tick 6).
	auto SpawnVolley = [VolleySize](FBattleProjectilePool& Pool, float TtlArced, float TtlDown)
	{
		uint32 Gen = 0;
		for (int32 i = 0; i < VolleySize; ++i)
		{
			const int32 Arc = Pool.Acquire(Gen);
			Pool.PosX[Arc] = 0.f;
			Pool.PosY[Arc] = static_cast<float>(i - VolleySize / 2) * 0.25f;
			Pool.PosZ[Arc] = 1.6f;
			const float C45 = FMath::Cos(FMath::DegreesToRadians(45.f));
			Pool.VelX[Arc] = 35.f * C45;
			Pool.VelY[Arc] = 0.f;
			Pool.VelZ[Arc] = 35.f * C45;
			Pool.TTL[Arc] = TtlArced;
			Pool.GravityScale[Arc] = 1.f;

			const int32 Down = Pool.Acquire(Gen);
			Pool.PosX[Down] = 0.f;
			Pool.PosY[Down] = static_cast<float>(i - VolleySize / 2) * 0.25f;
			Pool.PosZ[Down] = 1.6f;
			const float C30 = FMath::Cos(FMath::DegreesToRadians(30.f));
			Pool.VelX[Down] = 35.f * C30;
			Pool.VelY[Down] = 0.f;
			Pool.VelZ[Down] = -35.f * FMath::Sin(FMath::DegreesToRadians(30.f));
			Pool.TTL[Down] = TtlDown;
			Pool.GravityScale[Down] = 1.f;
		}
	};

	// -- The main volley: all 64 dead by tick 600, exact cause split, zero
	//    capacity violations, live count never grows.
	{
		FBattleProjectilePool Pool;
		Pool.Reserve(2 * VolleySize);
		SpawnVolley(Pool, 2.f, 20.f);
		TestEqual(TEXT("volley fills the pool"), Pool.GetLiveCount(), 2 * VolleySize);

		int32 LastLive = Pool.GetLiveCount();
		int32 FirstReleaseTick = -1;
		for (int32 Tick = 1; Tick <= NumTicks; ++Tick)
		{
			FBallisticsMath::StepPool(Pool, &Grid, DT);
			const int32 Live = Pool.GetLiveCount();
			TestTrue(TEXT("live count never grows"), Live <= LastLive);
			TestEqual(TEXT("capacity never changes"), Pool.GetCapacity(), 2 * VolleySize);
			if (Live < LastLive && FirstReleaseTick < 0)
			{
				FirstReleaseTick = Tick;
			}
			LastLive = Live;
			if (Live == 0)
			{
				break;
			}
		}
		TestEqual(TEXT("all 64 dead by TTL or ground"), Pool.GetLiveCount(), 0);
		TestEqual(TEXT("zero drops"), Pool.GetDrops(), 0);
		TestTrue(TEXT("steep shots die early (first release by tick 10)"), FirstReleaseTick > 0 && FirstReleaseTick <= 10);
	}

	// Exact cause split: TTL-short arced volley alone.
	{
		FBattleProjectilePool Pool;
		Pool.Reserve(VolleySize);
		uint32 Gen = 0;
		for (int32 i = 0; i < VolleySize; ++i)
		{
			const int32 S = Pool.Acquire(Gen);
			Pool.PosX[S] = 0.f; Pool.PosY[S] = 0.f; Pool.PosZ[S] = 1.6f;
			const float C45 = FMath::Cos(FMath::DegreesToRadians(45.f));
			Pool.VelX[S] = 35.f * C45; Pool.VelY[S] = 0.f; Pool.VelZ[S] = 35.f * C45;
			Pool.TTL[S] = 2.f; Pool.GravityScale[S] = 1.f;
		}
		int32 DrainTick = -1;
		for (int32 Tick = 1; Tick <= NumTicks; ++Tick)
		{
			FBallisticsMath::StepPool(Pool, &Grid, DT);
			if (Pool.GetLiveCount() == 0) { DrainTick = Tick; break; }
		}
		TestTrue(TEXT("arced volley drains at its 2 s TTL (within float slack)"),
			DrainTick > 0 && DrainTick <= 130);
		// Death at ~tick 120 is decades before the ~5.1 s ground time, so the
		// only possible cause is TTL expiry; no drops either.
		TestEqual(TEXT("arced volley adds no drops"), Pool.GetDrops(), 0);
	}

	// Steep-down volley alone: every death is a ground hit.
	{
		FBattleProjectilePool Pool;
		Pool.Reserve(VolleySize);
		uint32 Gen = 0;
		const float C30 = FMath::Cos(FMath::DegreesToRadians(30.f));
		const float S30 = FMath::Sin(FMath::DegreesToRadians(30.f));
		for (int32 i = 0; i < VolleySize; ++i)
		{
			const int32 S = Pool.Acquire(Gen);
			Pool.PosX[S] = 0.f; Pool.PosY[S] = 0.f; Pool.PosZ[S] = 1.6f;
			Pool.VelX[S] = 35.f * C30; Pool.VelY[S] = 0.f; Pool.VelZ[S] = -35.f * S30;
			Pool.TTL[S] = 20.f; Pool.GravityScale[S] = 1.f;
		}
		int32 GroundTicks = 0;
		for (int32 Tick = 0; Tick < NumTicks; ++Tick)
		{
			if (Pool.GetLiveCount() == 0) { break; }
			const FBallisticsMath::FPoolStepCounts Counts = FBallisticsMath::StepPool(Pool, &Grid, DT);
			GroundTicks += Counts.GroundHits;
		}
		TestEqual(TEXT("steep volley drains to zero"), Pool.GetLiveCount(), 0);
		TestEqual(TEXT("every steep death is a ground hit"), GroundTicks, VolleySize);
	}

	// Two-run equality: identical initials -> identical per-tick live counts
	// (the pool-iteration-order determinism, RC8's stepping half at unit tier).
	{
		FBattleProjectilePool PoolA;
		PoolA.Reserve(2 * VolleySize);
		SpawnVolley(PoolA, 2.f, 20.f);
		FBattleProjectilePool PoolB;
		PoolB.Reserve(2 * VolleySize);
		SpawnVolley(PoolB, 2.f, 20.f);
		bool bIdentical = true;
		for (int32 Tick = 0; Tick < NumTicks; ++Tick)
		{
			FBallisticsMath::StepPool(PoolA, &Grid, DT);
			FBallisticsMath::StepPool(PoolB, &Grid, DT);
			if (PoolA.GetLiveCount() != PoolB.GetLiveCount())
			{
				bIdentical = false;
				break;
			}
		}
		TestTrue(TEXT("two-run per-tick live counts identical"), bIdentical);
		TestEqual(TEXT("both runs end empty"), PoolA.GetLiveCount() + PoolB.GetLiveCount(), 0);
	}

	// Hill twin: a 5 m hill under the steep-down lane kills on tick 1
	// (terrain above the descent), the flat twin on tick ~6 -- ground reads
	// the heightfield, not a hardcoded 0.
	{
		FBattleCostGrid HGrid;
		HGrid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		StampHill(HGrid, FVector2D(0.5f, -1.f), FVector2D(2.5f, 1.f), 5.f);
		FBattleProjectilePool PoolHill;
		PoolHill.Reserve(VolleySize);
		FBattleProjectilePool PoolFlat;
		PoolFlat.Reserve(VolleySize);
		auto SpawnDown = [VolleySize](FBattleProjectilePool& Pool)
		{
			uint32 Gen = 0;
			const float C30 = FMath::Cos(FMath::DegreesToRadians(30.f));
			const float S30 = FMath::Sin(FMath::DegreesToRadians(30.f));
			for (int32 i = 0; i < VolleySize; ++i)
			{
				const int32 S = Pool.Acquire(Gen);
				Pool.PosX[S] = 0.f; Pool.PosY[S] = 0.f; Pool.PosZ[S] = 1.6f;
				Pool.VelX[S] = 35.f * C30; Pool.VelY[S] = 0.f; Pool.VelZ[S] = -35.f * S30;
				Pool.TTL[S] = 20.f; Pool.GravityScale[S] = 1.f;
			}
		};
		SpawnDown(PoolHill);
		SpawnDown(PoolFlat);
		int32 HillFirstRelease = -1;
		int32 FlatFirstRelease = -1;
		for (int32 Tick = 1; Tick <= NumTicks; ++Tick)
		{
			FBallisticsMath::StepPool(PoolHill, &HGrid, DT);
			FBallisticsMath::StepPool(PoolFlat, &Grid, DT);
			if (HillFirstRelease < 0 && PoolHill.GetLiveCount() < VolleySize) { HillFirstRelease = Tick; }
			if (FlatFirstRelease < 0 && PoolFlat.GetLiveCount() < VolleySize) { FlatFirstRelease = Tick; }
			if (HillFirstRelease > 0 && FlatFirstRelease > 0) { break; }
		}
		TestEqual(TEXT("hill twin kills on the first tick"), HillFirstRelease, 1);
		TestTrue(TEXT("flat twin dies later than the hill twin"),
			FlatFirstRelease > HillFirstRelease && FlatFirstRelease <= 10);
	}

	// Empty-pool identity: the pass on a reserved-but-empty pool is a no-op
	// (the seam half of the R-R8 early-out; the shim's flags-off gate sits on
	// top of this).
	{
		FBattleProjectilePool Pool;
		Pool.Reserve(16);
		const FBallisticsMath::FPoolStepCounts Counts = FBallisticsMath::StepPool(Pool, &Grid, DT);
		TestEqual(TEXT("empty pool: no ground hits"), Counts.GroundHits, 0);
		TestEqual(TEXT("empty pool: no TTL expiries"), Counts.TtlExpired, 0);
		TestEqual(TEXT("empty pool: still empty"), Pool.GetLiveCount(), 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T3.3c: Battle.Unit.Ranged.VolleySelection -- eligibility matrix: rank
// limits, range window, LOS fire rules (arced frontage vs flat lane, RC2/RC3),
// friendly-fire holds (A7), reload staggering (RC1).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_VolleySelection,
	"Battle.Unit.Ranged.VolleySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_VolleySelection::RunTest(const FString& Parameters)
{
	const FProjectileProfile Arrow = GetProjectileProfile(EBattleProjectileKind::Arrow);
	const FProjectileProfile Bolt = GetProjectileProfile(EBattleProjectileKind::Bolt);
	FVolleyTargetView Target;
	Target.PosM = FVector2D(80.f, 0.f);
	Target.ZM = 1.0f;

	const auto MakeShooter = [&](int32 Index, int32 Rank) -> FVolleyShooterView
	{
		FVolleyShooterView S;
		S.Entity = FMassEntityHandle(Index + 1, 1);
		FUnitHandle U;
		U.Index = 1u;
		U.Serial = 1u;
		S.Unit = U;
		S.PosM = FVector2D(0.f, 0.f);
		S.EyeZM = 1.6f;
		S.Kind = EBattleProjectileKind::Arrow;
		S.RangeM = 120.f;
		S.Ammo = 25;
		S.ReloadTimer = 0.f;
		S.ShotsFired = 0;
		S.Rank = Rank;
		return S;
	};

	// -- rank limits (A10): arced fires ranks 0..VolleyRanks-1; flat fires
	//    rank 0 only.
	{
		FBattleCostGrid Grid;
		Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		TArray<FVolleyShooterView> Shooters = { MakeShooter(0, 0), MakeShooter(1, 1), MakeShooter(2, 2) };
		TArray<FVolleyShot> Shots;
		// VolleyRanks default = 2: ranks 0,1 fire (arced).
		TestEqual(TEXT("arced fires the front 2 ranks"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 2);
		// Flat: rank 0 only.
		Shooters = { MakeShooter(0, 0), MakeShooter(1, 1) };
		Shots.Reset();
		TestEqual(TEXT("flat fires the front rank only"),
			SelectVolleyShots(Shooters, Target, Bolt, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 1);
	}

	// -- range window: below MinRangeM (default 8) holds; beyond weapon range
	//    holds; in between fires.
	{
		FBattleCostGrid Grid;
		Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		FVolleyTargetView Near = Target;
		Near.PosM = FVector2D(6.f, 0.f); // inside MinRangeM
		TArray<FVolleyShooterView> Shooters = { MakeShooter(0, 0) };
		TArray<FVolleyShot> Shots;
		TestEqual(TEXT("too close holds"),
			SelectVolleyShots(Shooters, Near, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 0);
		FVolleyTargetView Far = Target;
		Far.PosM = FVector2D(121.f, 0.f); // beyond the 120 m EDU range
		TestEqual(TEXT("too far holds"),
			SelectVolleyShots(Shooters, Far, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 0);
		TestEqual(TEXT("in window fires"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 1);
	}

	// -- the trajectory-class FIRE RULE over terrain (RC2 + RC3): a hill
	//    mid-lane holds the flat shot (whole-lane march) but NOT the arced
	//    shot (frontage-only march -- arrows go over).
	{
		FBattleCostGrid Grid;
		Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		StampHill(Grid, FVector2D(38.f, -3.f), FVector2D(44.f, 3.f), 5.f);
		TArray<FVolleyShooterView> Shooters = { MakeShooter(0, 0) };
		TArray<FVolleyShot> Shots;
		TestEqual(TEXT("arced fires over a mid-lane hill (frontage clear)"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 1);
		Shooters = { MakeShooter(0, 0) }; // the arced shot armed the reload timer
		Shots.Reset();
		TestEqual(TEXT("flat holds on a mid-lane hill (lane blocked)"),
			SelectVolleyShots(Shooters, Target, Bolt, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 0);
	}

	// -- friendly fire (A7): flat holds with a friendly body in the lane
	//    corridor; arced holds when friends stand at the impact zone.
	{
		FBattleCostGrid Grid;
		Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		TArray<FVolleyShooterView> Shooters = { MakeShooter(0, 0) };
		TArray<FVolleyShot> Shots;

		// Flat: a friendly at the lane midpoint (corridor half-width ~1.3 m).
		TArray<FVector2D> LaneBlockers = { FVector2D(40.f, 0.5f) };
		TestEqual(TEXT("flat holds with a friendly in the lane"),
			SelectVolleyShots(Shooters, Target, Bolt, Grid, LaneBlockers, 1.f, 0, Shots), 0);
		// The same friendly far off the lane does not hold fire.
		LaneBlockers = { FVector2D(40.f, 12.f) };
		TestEqual(TEXT("flat fires past an off-lane friendly"),
			SelectVolleyShots(Shooters, Target, Bolt, Grid, LaneBlockers, 1.f, 0, Shots), 1);

		// Arced: a friendly at the impact zone holds (tolerance 0); off-zone
		// does not. Fresh shooter: the flat-fire case above armed the reload.
		Shooters = { MakeShooter(0, 0) };
		Shots.Reset();
		TArray<FVector2D> ZoneBlockers = { FVector2D(80.f, 0.5f) };
		TestEqual(TEXT("arced holds with a friendly at the impact zone"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, ZoneBlockers, 1.f, 0, Shots), 0);
		ZoneBlockers = { FVector2D(80.f, 25.f) };
		TestEqual(TEXT("arced fires with friendlies far from the zone"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, ZoneBlockers, 1.f, 0, Shots), 1);
	}

	// -- reload staggering (RC1): a spent shooter sits out the reload cycle.
	{
		FBattleCostGrid Grid;
		Grid.InitFlat(86, 86, FVector2D::ZeroVector, 3.0f);
		TArray<FVolleyShooterView> Shooters = { MakeShooter(0, 0) };
		TArray<FVolleyShot> Shots;
		TestEqual(TEXT("first cycle fires"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 1);
		TestEqual(TEXT("reload timer set from the cvar"),
			Shooters[0].ReloadTimer, FRangedCVars::ReloadInterval(), 1e-4f);
		Shots.Reset();
		TestEqual(TEXT("reloading shooter sits out"),
			SelectVolleyShots(Shooters, Target, Arrow, Grid, TArray<FVector2D>(), 1.f, 0, Shots), 0);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T3.3d: Battle.Unit.Ranged.SkirmishMath -- the RC7 truth table: break
// trigger, hysteresis resume, hold-fire suppresses fire but not retreat.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRanged_SkirmishMath,
	"Battle.Unit.Ranged.SkirmishMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRanged_SkirmishMath::RunTest(const FString& Parameters)
{
	const float Break = 25.f;
	const float Resume = 32.5f; // 1.3x

	// Inside the break distance: retreat regardless of the fire toggle.
	TestEqual(TEXT("inside break: retreat"), EvaluateSkirmish(20.f, true, true, false, Break, Resume), ESkirmishDecision::Retreat);
	TestEqual(TEXT("inside break with hold-fire: still retreat"), EvaluateSkirmish(20.f, true, false, false, Break, Resume), ESkirmishDecision::Retreat);

	// Past break but inside the resume band while retreating: keep retreating
	// (hysteresis).
	TestEqual(TEXT("hysteresis holds the retreat"), EvaluateSkirmish(30.f, true, true, true, Break, Resume), ESkirmishDecision::Retreat);

	// Past resume: fire when ready + fire-at-will; hold otherwise.
	TestEqual(TEXT("clear + ready + willing: fire"), EvaluateSkirmish(40.f, true, true, true, Break, Resume), ESkirmishDecision::Fire);
	TestEqual(TEXT("clear + hold-fire: hold"), EvaluateSkirmish(40.f, true, false, true, Break, Resume), ESkirmishDecision::Hold);
	TestEqual(TEXT("clear + not reloaded: hold"), EvaluateSkirmish(40.f, false, true, true, Break, Resume), ESkirmishDecision::Hold);

	// Not retreating and past break: no hysteresis latch needed.
	TestEqual(TEXT("past break, not retreating: fire"), EvaluateSkirmish(30.f, true, true, false, Break, Resume), ESkirmishDecision::Fire);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
