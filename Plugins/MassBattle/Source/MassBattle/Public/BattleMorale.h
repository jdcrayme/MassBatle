// BattleMorale.h -- Phase 4a morale seam (spec morale-and-gas sections 4.1-4.5,
// 4.12; brief A1): the contribution stack, the decay forms, the fatigue model
// + multiplier table, the rout state machine, and the rally roll -- free
// functions / plain structs, deterministic, seed-derived RNG only (the A13
// discipline: hash-family inputs, never streams, never wall-clock, never
// GFrameCounter; rally rolls arrive here as plain Roll01 values the caller
// draws through BattleCanonicalEntityKey/Count).
//
// The seam task ships the bodies (T4.1/T4.2-locked by the
// Battle.Unit.Morale.* suites; the task-0 stubs they replaced are in the
// patch history). The processors and the exec calc stay thin shims over
// these functions (B.0).
//
// A16 posture (ADR-37): FMoraleWeights / FFatigueConstants below are
// golden-locked placeholders honoring the community model's STRUCTURE; the
// Phase-6 T6.1 audit swaps ported constants with diff review, never silently.
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"

/** CVAR block accessors (definitions + defaults in BattleMorale.cpp; the
 *  FRangedCVars pattern). Game-thread processors are the only runtime
 *  consumers; accessors read GetValueOnAnyThread() per the worker-read
 *  contract (engine-api Part B, the audit A-1 verification note). */
namespace FMoraleCVars
{
	MASSBATTLE_API bool Enable();                   // Battle.Morale.Enable (0 until the FSM lands end-to-end, B.8 item 6)
	MASSBATTLE_API float TickPeriod();              // Battle.Morale.TickPeriod (arch section 12 band 0.25-0.5)
	MASSBATTLE_API bool DeterministicTick();        // Battle.Morale.DeterministicTick (gates/soak pin the cadence)
	MASSBATTLE_API float WaveringThreshold();       // Battle.Morale.WaveringThreshold (absolute morale)
	MASSBATTLE_API float BreakThreshold();          // Battle.Morale.BreakThreshold (absolute morale)
	MASSBATTLE_API float WaveringHysteresis();      // Battle.Morale.WaveringHysteresis (recovery margin)
	MASSBATTLE_API float RallyThreshold();          // Battle.Morale.RallyThreshold
	MASSBATTLE_API float RallyBase();               // Battle.Morale.RallyBase
	MASSBATTLE_API float RallyDecay();              // Battle.Morale.RallyDecay (per break)
	MASSBATTLE_API int32 MaxRallies();              // Battle.Morale.MaxRallies (Shattered at this RoutCount)
	MASSBATTLE_API float SpikePerCasualty();        // Battle.Morale.SpikePerCasualty (fresh-casualty term)
	MASSBATTLE_API float SpikeHalfLifeS();          // Battle.Morale.SpikeHalfLifeS
	MASSBATTLE_API float OfficerSpikeMultiplier();  // Battle.Morale.OfficerSpikeMultiplier (MC8)
	MASSBATTLE_API float GeneralShockBase();        // Battle.Morale.GeneralShockBase (team-wide term)
	MASSBATTLE_API float GeneralShockHalfLifeS();   // Battle.Morale.GeneralShockHalfLifeS
	MASSBATTLE_API float CascadeRadiusM();          // Battle.Morale.CascadeRadiusM (routing-friends term)
	MASSBATTLE_API float GeneralAuraRadiusM();      // Battle.Morale.GeneralAuraRadiusM
	MASSBATTLE_API float GeneralAuraBase();         // Battle.Morale.GeneralAuraBase

	// Task 4b.2 (B-6): the terror per-token shaping (the falloff shape is
	// GeneralAura's; the foot/mounted split is per-token Base/Radius).
	MASSBATTLE_API float TerrorAuraFootBase();       // Battle.Morale.TerrorAuraFootBase
	MASSBATTLE_API float TerrorAuraFootRadiusM();    // Battle.Morale.TerrorAuraFootRadiusM
	MASSBATTLE_API float TerrorAuraMountedBase();    // Battle.Morale.TerrorAuraMountedBase
	MASSBATTLE_API float TerrorAuraMountedRadiusM(); // Battle.Morale.TerrorAuraMountedRadiusM
	MASSBATTLE_API float ImpetuousChargeRadiusM();  // Battle.Morale.ImpetuousChargeRadiusM (MC9)
	MASSBATTLE_API bool SoakRout();                 // Battle.Morale.SoakRout (nightly variant; 0 = baseline identity)
}

/** Fatigue state (spec section 4.3; the six-band M2TW ladder). Derived fresh
 *  from ExertionIntegral each evaluation -- never persisted; the INTEGRAL is
 *  the persisted state (registry) and the state's EFFECTS ride the
 *  FatigueState GE catalog (section 4.9), never this enum directly. */
enum class EBattleFatigueState : uint8
{
	Fresh = 0,
	Active = 1,
	QuiteTired = 2,
	Tired = 3,
	VeryTired = 4,
	Exhausted = 5
};

/** The contribution stack's named weights (ONE home; A16). Signs are baked in:
 *  penalties are negative. Structure honors the community model (a burst of
 *  casualties hurts more than the same losses slowly; the general's death is
 *  the sharpest shock; high ground and the general's presence steady). */
namespace FMoraleWeights
{
	/** Per unit of casualty FRACTION (0..1) -- the grinding term. */
	constexpr float CasualtyFraction = -8.f;
	/** Per unit of fresh-casualty spike (the decaying burst, section 4.2). */
	constexpr float FreshCasualtySpike = -0.5f;
	/** Local engagement odds differential (-1..1; -1 = badly losing locally). */
	constexpr float LocalOdds = -2.f;
	/** Per flanked/rear sector bit set in the exposure mask (section 4.6). */
	constexpr float ArcExposurePerSector = -0.75f;
	/** Per routing friendly inside the cascade radius (MC7). */
	constexpr float RoutingFriend = -1.f;
	/** The general aura term (already distance-scaled at the input, 0..base). */
	constexpr float GeneralAura = 1.f;
	/** The team-wide general-death shock (decays, section 4.2). */
	constexpr float GeneralDeathShock = -0.5f;
	/** Height advantage (-1..1). */
	constexpr float HeightAdvantage = 0.5f;
	/** Winning-locally differential (-1..1; decaying kill window). */
	constexpr float WinningLocally = 1.f;
	/** Task 4b.2 (B-5): the terror aura -- the GeneralAura MIRROR against
	 *  enemy envelopes (drain-only; already distance-scaled at the input).
	 *  The magnitude mirrors the aura's +1 at the same falloff shape. */
	constexpr float TerrorAura = -1.f;
}

/** Fatigue mechanism constants (ONE home; A16): exertion rates (per second)
 *  and the state bands on the integral. The integral is MONOTONE this phase
 *  (no rest model -- spec section 10 records the seam); FatigueRateScale is
 *  the hardy/very_hardy slot the 4b parse consumes. */
namespace FFatigueConstants
{
	/** Running exertion rate (units of integral per second). */
	constexpr float RunRate = 0.6f;
	/** Fighting exertion rate (per second while the unit holds live pairs). */
	constexpr float FightRate = 0.8f;
	/** State bands on the integral: [0,10) Fresh, [10,25) Active, [25,45)
	 *  QuiteTired, [45,70) Tired, [70,100) VeryTired, >=100 Exhausted. */
	constexpr float BandActive = 10.f;
	constexpr float BandQuiteTired = 25.f;
	constexpr float BandTired = 45.f;
	constexpr float BandVeryTired = 70.f;
	constexpr float BandExhausted = 100.f;
}

/** Discipline scaling tables (ONE home; spec sections 4.4-4.5). BreakOffset
 *  ADDS to the break threshold: a unit breaks when morale drops below
 *  BreakThreshold + BreakOffset -- Low breaks half a point earlier,
 *  Disciplined holds half a point longer, Impetuous fights like Normal (it is
 *  brave in the charge, not in the rout -- MC9). T6.1 co-audit sizing: the
 *  offsets must stay SMALL against the EDU base-morale spread (2..9) -- the
 *  placeholder +-2.0 put the levy row (base 2, Low) one casualty from
 *  routing at its own base. The additive form survives the 0-anchored
 *  default threshold (a multiplicative resistance would be degenerate there;
 *  corrected at seam review, disclosed). RallyFactor multiplies the rally
 *  chance. Discipline NEVER scales the weights (MC2). Indexed by
 *  EBattleDiscipline. */
namespace FMoraleDiscipline
{
	constexpr float BreakOffset[4] = { 0.5f, 0.f, -0.5f, 0.f };
	constexpr float RallyFactor[4] = { 0.7f, 1.f, 1.3f, 0.8f };
}

/** The exec calc's sim-owned input block (spec section 4.1): assembled by the
 *  morale pass per unit per tick from registry state + cluster geometry;
 *  consumed by EvaluateMorale INSIDE the exec calc (A14: one evaluation per
 *  tick, the pass never evaluates the stack). Plain mirror -- the seam never
 *  touches fragments, worlds, or actors. */
struct FMoraleTickInputs
{
	float BaseMorale = 0.f;          // EDU stat_mental[0]
	float CasualtyFraction = 0.f;    // dead / InitialRosterSize (0..1)
	float FreshCasualtySpike = 0.f;  // section 4.2 (sim-tracked, decaying)
	float LocalOdds = 0.f;           // section 4.6 (paired-attackers differential, -1..1)
	uint8 ArcExposureMask = 0;       // section 4.6 (engaged-sector bits; bit k = sector k inbound)
	int32 RoutingFriendsNearby = 0;  // section 4.6 (cascade count)
	float GeneralAuraBonus = 0.f;    // section 4.6 (0 when no living general in radius)
	float GeneralDeathShock = 0.f;   // section 4.2 (team-wide decay term)
	float HeightAdvantage = 0.f;     // -1..1 (envelope anchor Z differential, normalized)
	float WinningLocally = 0.f;      // -1..1 (decaying kill differential)
	/** Task 4b.2 (B-5): the terror drain (distance-scaled at the input,
	 *  0..the per-token base; the fold zeroes it without enemy frighteners
	 *  -- flags-off identity). */
	float TerrorAuraBonus = 0.f;
};

/** The fatigue multiplier row per state (T4.2 golden): the catalog's *Mod
 *  writes are these minus 1 (the multipliers resolve in UnitSync's fixed
 *  1 + Mod form -- ADR-36/section 4.9). All 1.0 at Fresh by definition. */
struct FFatigueMultipliers
{
	float AttackMult = 1.f;
	float DefenseMult = 1.f;
	float SpeedMult = 1.f;
	float ReloadMult = 1.f;
};

/** The stack (spec section 4.1, MC2): weighted sum of the ten terms over
 *  BaseMorale. */
MASSBATTLE_API float EvaluateMorale(const FMoraleTickInputs& Inputs);

/** Task 4b.3 (B-7): the climate token's FatigueRateScale mapping
 *  (hardy 0.85 / very_hardy 0.7 / else 1 -- the T4b.3 golden locks it). */
MASSBATTLE_API float ClimateFatigueRateScale(const FString& ClimateToken);

/** The shared decay form (spec section 4.2): exponential half-life decay,
 *  closed-form, golden-locked (T4.1). Both the fresh-casualty spike and the
 *  general-death shock consume it. */
MASSBATTLE_API float EvaluateDecay(float Value, float Dt, float HalfLifeS);

/** Exertion accumulation (spec section 4.3): running/fighting add their rates
 *  x Dt; walking/idle adds nothing v1. */
MASSBATTLE_API void AccumulateExertion(float& InOutExertion, float Dt, bool bRunning, bool bFighting,
	float FatigueRateScale = 1.f);

/** Integral -> state (spec section 4.3): the FFatigueConstants bands. */
MASSBATTLE_API EBattleFatigueState EvaluateFatigueState(float ExertionIntegral);

/** The multiplier table (T4.2 golden). */
MASSBATTLE_API FFatigueMultipliers GetFatigueMultipliers(EBattleFatigueState State);

/** Rout transition (spec section 4.4, MC5): thresholds are absolute morale;
 *  the break threshold scales by discipline break resistance; wavering
 *  recovers only above threshold + hysteresis; Shattered locks at MaxRallies. */
MASSBATTLE_API EBattleRoutState EvaluateRoutTransition(EBattleRoutState Current, float Morale,
	float WaveringThreshold, float BreakThreshold, float WaveringHysteresis,
	uint8 RoutCount, int32 MaxRallies, EBattleDiscipline Discipline);

/** Rally chance (spec section 4.5): requires morale >= threshold AND a living
 *  general aura; decays per break (RallyDecay^RoutCount); scales by
 *  discipline. The base/decay arrive as parameters (the EvaluateSkirmish
 *  precedent: callers read cvars, the seam stays pure and unit-locked). */
MASSBATTLE_API float EvaluateRallyChance(float Morale, float RallyThreshold, float RallyBase, float RallyDecay,
	EBattleDiscipline Discipline, uint8 RoutCount, bool bGeneralAuraActive);

/** The rally decision: the chance vs the caller-drawn Roll01 (the caller
 *  draws CombatRoll01 over the canonical unit key with RallyAttempts as the
 *  count input -- A13; the seam stays pure). */
MASSBATTLE_API bool EvaluateRally(float Morale, float RallyThreshold, float RallyBase, float RallyDecay,
	EBattleDiscipline Discipline, uint8 RoutCount, bool bGeneralAuraActive, float Roll01);
