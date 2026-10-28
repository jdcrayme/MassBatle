// BattleMelee.cpp -- melee resolution seam (task 3; constants golden-locked
// by T2.1, see FMeleeMathConstants in the header).
#include "BattleMelee.h"
#include "BattleEngagement.h"
#include "HAL/IConsoleManager.h"

namespace FMeleeMath
{
	float HitChance(float Attack, float Defense)
	{
		Attack = FMath::Max(Attack, 0.f);
		Defense = FMath::Max(Defense, 0.f);
		float P = Attack / FMath::Max(Attack + Defense, FMeleeMathConstants::ContestedEpsilon);
		P = FMath::Clamp(P, FCombatCVars::HitChanceFloor(), FCombatCVars::HitChanceCap());
		return P;
	}

	namespace
	{
		/** MID-BUCKET center angle of a sector, normalized to [-180, 180):
		 *  sector k owns [k, k+1) buckets of 360/Count degrees (the floor
		 *  quantization in SectorIndexFromAngle), so its center is (k + 0.5)
		 *  steps. Must stay consistent with FCombatSolver::SectorCenterAngle
		 *  (the one-value-two-consumers contract, spec §4.5). */
		float SectorCenterDeg(int32 SectorIndexFromFacing, int32 SectorCount)
		{
			const int32 Count = FMath::Max(1, SectorCount);
			const int32 Wrapped = ((SectorIndexFromFacing % Count) + Count) % Count;
			const float Center = (static_cast<float>(Wrapped) + 0.5f) * (360.f / static_cast<float>(Count));
			float Normalized = FMath::Fmod(Center + 180.f, 360.f) - 180.f;
			if (Normalized < -180.f)
			{
				Normalized += 360.f;
			}
			return Normalized;
		}
	}

	float SkillArcMult(int32 SectorIndexFromFacing, int32 SectorCount)
	{
		const float AbsDeg = FMath::Abs(SectorCenterDeg(SectorIndexFromFacing, SectorCount));
		if (AbsDeg <= FMeleeMathConstants::FrontalArcDeg)
		{
			return FMeleeMathConstants::SkillMultFrontal;
		}
		if (AbsDeg >= FMeleeMathConstants::RearArcDeg)
		{
			return FMeleeMathConstants::SkillMultRear;
		}
		return FMeleeMathConstants::SkillMultFlank;
	}

	float ShieldArcWindow(int32 SectorIndexFromFacing, int32 SectorCount)
	{
		const float AbsDeg = FMath::Abs(SectorCenterDeg(SectorIndexFromFacing, SectorCount));
		return AbsDeg <= FMeleeMathConstants::FrontalArcDeg ? 1.f : 0.f;
	}

	float ArmorMultiplier(bool bArmorPiercing)
	{
		return bArmorPiercing ? FMeleeMathConstants::ArmorMultiplierAP : FMeleeMathConstants::ArmorMultiplierNormal;
	}

	float StaggeredPhaseOffset(uint32 EntityHandleIndex, float AttackInterval)
	{
		// Integer hash scatter (multiply-xor), deterministic per handle.
		uint32 H = EntityHandleIndex * 2654435761u;
		H ^= H >> 16;
		const float Frac = static_cast<float>(H & 0xFFFFu) / 65535.f;
		return Frac * FMath::Max(0.f, AttackInterval);
	}

	float CombatRoll01(uint32 EntityIndex, uint32 EntitySerial, uint32 RollCount)
	{
		// Same multiply-xor family as StaggeredPhaseOffset (the Knuth
		// golden-ratio multiplier has ONE home here). Inputs: the striker's
		// handle + the pair's own roll count (A13: no wall-clock, no
		// GFrameCounter -- two runs of one seed agree).
		const uint32 H = (EntityIndex * 2654435761u)
			^ (RollCount * 40503u)
			^ (EntitySerial * 2685821657u);
		return static_cast<float>(H & 0xFFFFFFu) / 16777215.f;
	}

	float RecoveryWindow(float AttackInterval)
	{
		return FMeleeMathConstants::RecoveryFraction * FMath::Max(0.f, AttackInterval);
	}

	float ChargeBonusRemaining(float BaseCharge, float ImpactAgeSeconds, float DecayWindow)
	{
		if (DecayWindow <= 0.f || ImpactAgeSeconds >= DecayWindow)
		{
			return 0.f;
		}
		return FMath::Max(0.f, BaseCharge * (1.f - ImpactAgeSeconds / DecayWindow));
	}

	float KnockbackImpulse(float ImpactSpeedMps, float SpeedThresholdMps)
	{
		const float Above = FMath::Max(0.f, ImpactSpeedMps - SpeedThresholdMps);
		return FMath::Min(Above * FMeleeMathConstants::KnockbackPerSpeed, FMeleeMathConstants::KnockbackMaxMps);
	}

	void DrainCompositePool(float& RiderHP, int16& MountHP, float Damage)
	{
		const float In = FMath::Max(0.f, Damage);
		const float MountDamage = FMath::Min(static_cast<float>(MountHP), In);
		MountHP = static_cast<int16>(static_cast<float>(MountHP) - MountDamage);
		RiderHP = FMath::Max(0.f, RiderHP - (In - MountDamage));
	}

	float BaseChargeBonus()
	{
		return FMeleeMathConstants::BaseChargeBonus;
	}
}
