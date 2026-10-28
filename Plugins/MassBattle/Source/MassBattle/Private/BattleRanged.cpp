// BattleRanged.cpp -- the Battle.Ranged.* CVAR block (spec ranged-stack
// §4.11) + the ranged seam bodies. Task 0 ships the CVAR block live and the
// seam functions as STUBS (the Phase 2 task-0 pattern: CI-visible gates,
// empty math); the seam task replaces the stubs with the implementations
// locked by T3.3. Defaults are A6 placeholders (ADR-30).
#include "BattleRanged.h"
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

// -- Seam stubs (task 0; replaced by the seam task, T3.3-locked) --

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
	// STUB: the volley task implements eligibility + solving + dispersion.
	(void)InOutShooters;
	(void)Target;
	(void)Profile;
	(void)Grid;
	(void)FriendlyLaneBlockersM;
	(void)AccuracyMult;
	(void)FriendlyImpactTolerance;
	(void)OutShots;
	return 0;
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
	// STUB: the skirmish task implements the hysteresis decision (RC7).
	(void)NearestEnemyDistM;
	(void)bReloadReady;
	(void)bFireAtWill;
	(void)bRetreating;
	(void)BreakDistM;
	(void)ResumeDistM;
	return ESkirmishDecision::Hold;
}
