// BattleRanged.cpp -- the Battle.Ranged.* CVAR block (spec ranged-stack
// §4.11) + the ranged seam bodies (volley selection, ammo accounting,
// skirmish decision -- T3.3-locked). The task-0 stubs are gone; the bodies
// consume the ballistics seam (BattleBallistics.h) and never touch worlds or
// fragments. Defaults are A6 placeholders (ADR-30).
#include "BattleRanged.h"
#include "BattleBallistics.h"
#include "HAL/IConsoleManager.h"

// -- Battle.Ranged.* CVAR block (spec §4.11; defaults are A6 placeholders) --
namespace
{
	TAutoConsoleVariable<int32> GCRangedEnable(
		TEXT("Battle.Ranged.Enable"),
		0,
		TEXT("Master ranged flag. 0 = pre-ranged behavior (byte-identical; B.8 item 6). "
			 "Flips to default 1 when the impact-resolution task lands end-to-end."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedStep(
		TEXT("Battle.Ranged.Step"),
		1,
		TEXT("Projectile stepping pass (integrator + ground/TTL release + impact queries)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedFire(
		TEXT("Battle.Ranged.Fire"),
		1,
		TEXT("Ranged fire pass (reload, volley selection, dispersion, ammo, skirmish)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedSkirmish(
		TEXT("Battle.Ranged.Skirmish"),
		1,
		TEXT("Skirmish auto-retreat evaluation (RC7; retreat is a registry move order)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedPoolCapacity(
		TEXT("Battle.Ranged.PoolCapacity"),
		4096,
		TEXT("Projectile pool capacity, reserved at battle start only. Acquire beyond "
			 "capacity fails closed (drops metric) -- the pool never grows mid-volley (RC9)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedVolleyRanks(
		TEXT("Battle.Ranged.VolleyRanks"),
		2,
		TEXT("Front ranks allowed to fire, arced profiles (flat = front rank only, fixed)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCRangedReloadInterval(
		TEXT("Battle.Ranged.ReloadInterval"),
		3.0f,
		TEXT("Base reload cycle, seconds (handle-hash staggered per entity, RC1)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCRangedMinRangeM(
		TEXT("Battle.Ranged.MinRangeM"),
		8.0f,
		TEXT("Minimum engagement distance, meters."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCRangedFrontageClearM(
		TEXT("Battle.Ranged.FrontageClearM"),
		8.0f,
		TEXT("Arced LOS check length ahead of the shooter (frontage clearance, RC3)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedFriendlyImpactTolerance(
		TEXT("Battle.Ranged.FriendlyImpactTolerance"),
		0,
		TEXT("Arced hold-fire rule: friendly bodies tolerated inside the estimated "
			 "impact zone before holding fire (A7; impacts stay team-blind regardless)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCRangedSkirmishBreakDistM(
		TEXT("Battle.Ranged.SkirmishBreakDistM"),
		25.0f,
		TEXT("Auto-retreat trigger distance, meters (RC7)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCRangedSkirmishResumeFactor(
		TEXT("Battle.Ranged.SkirmishResumeFactor"),
		1.3f,
		TEXT("Resume-fire distance as a factor of the break distance (hysteresis)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCRangedSoakVolley(
		TEXT("Battle.Ranged.SoakVolley"),
		0,
		TEXT("Soak volley-mix variant on the movement soak controller (nightly; "
			 "0 = byte-identical baseline, the R-C9 pattern)."),
		ECVF_Default);
}

namespace FRangedCVars
{
	bool Enable() { return GCRangedEnable.GetValueOnAnyThread() != 0; }
	bool Step() { return GCRangedStep.GetValueOnAnyThread() != 0; }
	bool Fire() { return GCRangedFire.GetValueOnAnyThread() != 0; }
	bool Skirmish() { return GCRangedSkirmish.GetValueOnAnyThread() != 0; }
	int32 PoolCapacity() { return GCRangedPoolCapacity.GetValueOnAnyThread(); }
	int32 VolleyRanks() { return GCRangedVolleyRanks.GetValueOnAnyThread(); }
	float ReloadInterval() { return GCRangedReloadInterval.GetValueOnAnyThread(); }
	float MinRangeM() { return GCRangedMinRangeM.GetValueOnAnyThread(); }
	float FrontageClearM() { return GCRangedFrontageClearM.GetValueOnAnyThread(); }
	int32 FriendlyImpactTolerance() { return GCRangedFriendlyImpactTolerance.GetValueOnAnyThread(); }
	float SkirmishBreakDistM() { return GCRangedSkirmishBreakDistM.GetValueOnAnyThread(); }
	float SkirmishResumeFactor() { return GCRangedSkirmishResumeFactor.GetValueOnAnyThread(); }
	bool SoakVolley() { return GCRangedSoakVolley.GetValueOnAnyThread() != 0; }
}

FProjectileProfile GetProjectileProfile(EBattleProjectileKind Kind)
{
	FProjectileProfile P;
	switch (Kind)
	{
	case EBattleProjectileKind::Arrow:
		P.MuzzleSpeedMps = 35.f;
		P.GravityScale = 1.f;
		P.bIndirect = true;
		P.DispersionDeg = 4.f;
		P.HitRadiusM = 0.5f;
		P.bArmorPiercing = false;
		break;
	case EBattleProjectileKind::Bolt:
		P.MuzzleSpeedMps = 60.f;
		P.GravityScale = 1.f;
		P.bIndirect = false;
		P.DispersionDeg = 2.f;
		P.HitRadiusM = 0.5f;
		P.bArmorPiercing = true;
		break;
	default:
		P.MuzzleSpeedMps = 0.f;
		P.bIndirect = false;
		break;
	}
	return P;
}

// -- Seam bodies (the task-0 stubs replaced; T3.3-locked) --

namespace
{
	/** Point-to-segment distance, 2D (the flat-lane corridor test, spec
	 *  §4.5). Pure math, no allocation. */
	float PointSegmentDist2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq <= 1e-8f)
		{
			return FVector2D::Distance(Point, A);
		}
		const float T = FVector2D::DotProduct(Point - A, AB) / LenSq;
		const float Clamped = FMath::Clamp(T, 0.f, 1.f);
		return FVector2D::Distance(Point, A + AB * Clamped);
	}

	constexpr float VolleyBodyRadiusM = 0.4f; // the 40 cm body radius (B-1 family: one named constant per seam)
}

int32 SelectVolleyShots(
	TArray<FVolleyShooterView>& InOutShooters,
	const FVolleyTargetView& Target,
	const FProjectileProfile& Profile,
	const FBattleCostGrid& Grid,
	const TArray<FVector2D>& FriendlyLaneBlockersM,
	float AccuracyMult,
	int32 FriendlyImpactTolerance,
	TArray<FVolleyShot>& OutShots)
{
	// Rank limit (A10): arced profiles fire from the front VolleyRanks ranks;
	// flat profiles from the front rank only (the fixed rule).
	const int32 MaxRank = Profile.bIndirect ? FMath::Max(1, FRangedCVars::VolleyRanks()) : 1;
	const float MinRangeM = FRangedCVars::MinRangeM();
	const float GravityMps2 = FBallisticsConstants::GravityMps2 * Profile.GravityScale;

	// Arced impact-zone estimate (A7): the dispersion cone scaled to the
	// horizontal distance, plus the hit radius. Friends inside -> hold when
	// the count exceeds the tolerance.
	const float HorizDist = FVector2D::Distance(InOutShooters.Num() > 0 ? InOutShooters[0].PosM : FVector2D::ZeroVector, Target.PosM);
	const float ConeRad = FMath::DegreesToRadians(FMath::Max(Profile.DispersionDeg, 0.01f));
	const float ImpactZoneRadiusM = (InOutShooters.Num() > 0 ? HorizDist : 0.f) * FMath::Tan(ConeRad) + Profile.HitRadiusM;
	int32 FriendsInZone = 0;
	for (const FVector2D& Friend : FriendlyLaneBlockersM)
	{
		if (FVector2D::Distance(Friend, Target.PosM) <= ImpactZoneRadiusM)
		{
			++FriendsInZone;
		}
	}
	const bool bArcedHold = Profile.bIndirect && FriendsInZone > FriendlyImpactTolerance;

	// Flat-lane corridor (A7): any friendly body inside the corridor holds
	// fire. Corridor half-width = 2 body radii + hit radius.
	const float CorridorHalfM = 2.f * VolleyBodyRadiusM + Profile.HitRadiusM;

	int32 ShotCount = 0;
	for (FVolleyShooterView& Shooter : InOutShooters)
	{
		// -- eligibility (RC1), cheapest tests first --
		if (Shooter.ReloadTimer > 0.f || Shooter.Ammo <= 0 || Shooter.Rank >= MaxRank)
		{
			continue;
		}
		const float Dist = FVector2D::Distance(Shooter.PosM, Target.PosM);
		if (Dist <= MinRangeM || Dist > Shooter.RangeM)
		{
			continue;
		}

		// -- LOS (RC3): arced checks the frontage segment only; flat checks
		//    the whole lane (the trajectory-class FIRE RULE, RC2).
		bool bClear = false;
		if (Profile.bIndirect)
		{
			const FVector2D Dir = (Target.PosM - Shooter.PosM).GetSafeNormal();
			const FVector2D FrontageEnd = Shooter.PosM + Dir * FRangedCVars::FrontageClearM();
			bClear = FBallisticsMath::MarchClear(Grid, Shooter.PosM, Shooter.EyeZM, FrontageEnd, Shooter.EyeZM, 0.3f);
		}
		else
		{
			bClear = FBallisticsMath::MarchClear(Grid, Shooter.PosM, Shooter.EyeZM, Target.PosM, Target.ZM, 0.3f);
			if (bClear)
			{
				for (const FVector2D& Friend : FriendlyLaneBlockersM)
				{
					if (PointSegmentDist2D(Friend, Shooter.PosM, Target.PosM) <= CorridorHalfM)
					{
						bClear = false;
						break;
					}
				}
			}
		}
		if (!bClear)
		{
			continue;
		}

		// -- arced friendly-zone hold (A7) --
		if (Profile.bIndirect && bArcedHold)
		{
			continue;
		}

		// -- solve + disperse (RC1/RC2) --
		const FBallisticSolution Solution = FBallisticsMath::SolveLaunch(
			Shooter.PosM, Shooter.EyeZM, Target.PosM, Target.ZM, Profile.MuzzleSpeedMps, GravityMps2);
		if (!Solution.bValid)
		{
			continue; // out of envelope: hold, never force-fire
		}
		const uint32 Seed = FBallisticsMath::ShotSeed(
			Shooter.Entity.Index, Shooter.Entity.SerialNumber, Shooter.ShotsFired);
		const float ConeDeg = Profile.DispersionDeg / FMath::Max(AccuracyMult, 0.05f);
		const FVector LaunchDir = FBallisticsMath::PerturbDirection(Solution.LaunchDir, ConeDeg, Seed);

		FVolleyShot Shot;
		Shot.Entity = Shooter.Entity;
		Shot.Unit = Shooter.Unit;
		Shot.Kind = Shooter.Kind;
		Shot.LaunchPosM = FVector(Shooter.PosM.X, Shooter.PosM.Y, Shooter.EyeZM);
		Shot.LaunchDir = LaunchDir;
		Shot.FlightTimeS = Solution.FlightTimeS;
		Shot.Seed = Seed;
		OutShots.Add(Shot);
		++ShotCount;

		// -- mutate the view: the accounting IS the shot (RC5) --
		--Shooter.Ammo;
		Shooter.ReloadTimer = FRangedCVars::ReloadInterval();
		++Shooter.ShotsFired;
	}
	return ShotCount;
}

float ComputeAmmoFraction(int32 AmmoSum, int32 InitialAmmo, int32 RosterSize)
{
	if (InitialAmmo <= 0 || RosterSize <= 0)
	{
		return -1.f; // non-ranged unit (RC5)
	}
	return static_cast<float>(AmmoSum) / (static_cast<float>(InitialAmmo) * static_cast<float>(RosterSize));
}

ESkirmishDecision EvaluateSkirmish(
	float NearestEnemyDistM, bool bReloadReady, bool bFireAtWill,
	bool bRetreating, float BreakDistM, float ResumeDistM)
{
	// RC7: retreat triggers inside the break distance regardless of the fire
	// toggle (hold-fire is not hold-ground); hysteresis holds the retreat
	// until the resume distance; fire resumes only past it.
	if (NearestEnemyDistM <= BreakDistM)
	{
		return ESkirmishDecision::Retreat;
	}
	if (bRetreating && NearestEnemyDistM < ResumeDistM)
	{
		return ESkirmishDecision::Retreat;
	}
	return (bReloadReady && bFireAtWill) ? ESkirmishDecision::Fire : ESkirmishDecision::Hold;
}
