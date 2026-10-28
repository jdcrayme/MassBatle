// BattleMelee.h -- melee resolution pure math on the B.0 seam (brief A1; phase
// ground rule 1). Free functions / plain structs, deterministic, meters, no
// RNG and no wall clock: the resolution processor draws from the registered
// `Combat.Roll` stream (F7/A13) and calls these for the math.
//
// A6 posture: the community model's STRUCTURE ships (contested curve, arc
// bands, AP halving, staggered cycle, charge decay, bounded knockback); the
// CONSTANTS are placeholders, golden-locked by T2.1 and swapped only by the
// Phase-6 T6.1 audit with diff review (spec ADR-26). Every constant lives in
// FMeleeMathConstants below so the golden table names what it locks.
#pragma once

#include "CoreMinimal.h"

/** T2.1-golden-locked placeholders (A6). Renaming/removing a constant is a
 *  conscious-formula-review event (the golden test names them). */
namespace FMeleeMathConstants
{
        // Contested curve: P = Attack / (Attack + Defense).
        constexpr float ContestedEpsilon = 1e-6f;
        // Arc bands (angular windows, sector-count independent): skill applies on
        // all arcs (reduced off frontal), the shield only within the frontal arc.
        constexpr float FrontalArcDeg = 45.f;   // |center| <= this = frontal
        constexpr float RearArcDeg = 135.f;     // |center| >= this = rear
        constexpr float SkillMultFrontal = 1.f;
        constexpr float SkillMultFlank = 0.75f;
        constexpr float SkillMultRear = 0.5f;
        // Armor-piercing halves armor (spec §4.8).
        constexpr float ArmorMultiplierAP = 0.5f;
        constexpr float ArmorMultiplierNormal = 1.f;
        // Attack cycle: the full period is AttackInterval; the Recovering window
        // is this fraction of it (strike -> recover -> repeat within one period).
        constexpr float RecoveryFraction = 0.4f;
        // Charge bonus at impact (EDU stat_charge mapping is a T6.1 item; the
        // structure is BaseCharge decaying linearly over ChargeDecayWindow).
        constexpr float BaseChargeBonus = 4.f;
        // Knockback: linear in impact speed above the fresh-contact threshold,
        // bounded (deterministic; no knockdown RNG v1, spec §4.9).
        constexpr float KnockbackPerSpeed = 0.6f;  // (m/s)/(m/s above threshold)
        constexpr float KnockbackMaxMps = 2.5f;
        // Charge: minimum accumulated run-up (m) before a fast contact counts
        // as a charge impact (a standing nudge at speed is not a charge, EC7).
        constexpr float MinChargeRunUpM = 1.f;
        // Composite HP: landed hits drain the mount pool first, then the rider
        // (documented simplification: the ORDER does not affect the total; no
        // riderless mounts v1, spec §10).
}

/** Defense arc bands (EC6). Input: the defender-relative attack sector index
 *  (0 = dead ahead, from the quantized ContactAngle via
 *  FCombatSolver::SectorIndexFromAngle -- ONE value, TWO consumers, §4.5:
 *  the bands compare against the sector, they never re-derive the angle). */
namespace FMeleeMath
{
        /** Contested hit chance (spec §4.8, arch §9.1): P = Attack / (Attack +
         *  Defense), clamped to [HitChanceFloor, HitChanceCap] (CVAR block).
         *  Negative inputs clamp at 0 first (stats are non-negative by
         *  construction; the guard keeps the curve total). */
        MASSBATTLE_API float HitChance(float Attack, float Defense);

        /** Defense-skill arc multiplier: frontal 1.0, flank 0.75, rear 0.5. */
        MASSBATTLE_API float SkillArcMult(int32 SectorIndexFromFacing, int32 SectorCount);

        /** Shield-arc window: 1.0 on frontal sectors, 0.0 otherwise (EC6: shield
         *  contributes on frontal arcs only). */
        MASSBATTLE_API float ShieldArcWindow(int32 SectorIndexFromFacing, int32 SectorCount);

        /** Armor multiplier: 0.5 against AP weapons, 1.0 otherwise. */
        MASSBATTLE_API float ArmorMultiplier(bool bArmorPiercing);

        /** Attack-cycle phase offset for one entity: deterministic handle-hashed
         *  scatter in [0, AttackInterval) so combat is never serialized (ADR-05).
         *  Repeat-call equal for the same handle. */
        MASSBATTLE_API float StaggeredPhaseOffset(uint32 EntityHandleIndex, float AttackInterval);

        /** Deterministic melee roll in [0, 1) for one strike (A13: the worker
         *  cannot touch the game-thread RNG registry; seed-derived, no
         *  wall-clock, run-invariant -- the tick input is the pair's OWN roll
         *  count, never GFrameCounter). Same hash family as
         *  StaggeredPhaseOffset (one home for the Knuth multipliers, B-2). */
        MASSBATTLE_API float CombatRoll01(uint32 EntityIndex, uint32 EntitySerial, uint32 RollCount);

        /** Recovering-window length for one cycle (fraction of the interval). */
        MASSBATTLE_API float RecoveryWindow(float AttackInterval);

        /** Charge-bonus value remaining: BaseCharge decaying linearly over the
         *  window; <= 0 once expired (EC7: decays to normal melee within the
         *  charge window). */
        MASSBATTLE_API float ChargeBonusRemaining(float BaseCharge, float ImpactAgeSeconds, float DecayWindow);

        /** Knockback impulse magnitude at impact, m/s: linear in impact speed
         *  above the fresh-contact threshold, bounded by KnockbackMaxMps. The
         *  DIRECTION is the charge heading (processor-owned). */
        MASSBATTLE_API float KnockbackImpulse(float ImpactSpeedMps, float SpeedThresholdMps);

        /** Composite HP drain (spec §4.8): one landed hit's damage drains the
         *  mount pool first, then the rider; entity dies when the total is 0. */
        MASSBATTLE_API void DrainCompositePool(float& RiderHP, int16& MountHP, float Damage);

        /** Base charge bonus at impact (placeholder constant, golden-locked). */
        MASSBATTLE_API float BaseChargeBonus();
}
