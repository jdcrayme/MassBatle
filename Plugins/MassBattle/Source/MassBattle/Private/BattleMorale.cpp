// BattleMorale.cpp -- the Battle.Morale.* CVAR block (spec morale-and-gas
// section 4.12) + the morale seam bodies (T4.1/T4.2-locked by the
// Battle.Unit.Morale.* suites). Defaults are A6 placeholders (ADR-37).
#include "BattleMorale.h"
#include "HAL/IConsoleManager.h"

// -- Battle.Morale.* CVAR block (spec section 4.12; defaults are A6 placeholders) --
namespace
{
	TAutoConsoleVariable<int32> GCMoraleEnable(
		TEXT("Battle.Morale.Enable"),
		1,
		TEXT("Master morale flag (tick GE + FSM + effects). Default 1 per the "
			 "T6.1 threshold co-audit (Docs/phases/phase-4/t61-morale-threshold-"
			 "audit.md): the PR-12 evidence block (levy base 2 vs WaveringThreshold "
			 "4.0 -- spawn-wavering, one-casualty routing) was the THRESHOLD "
			 "placement, not the flag; the audited family (Wavering 1.0, "
			 "Rally 1.5, break offsets +-0.5) keeps every fixture row steady at "
			 "spawn. The PR-12 session-on posture (T4 scenarios set the flag) is "
			 "redundant now but harmless."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleTickPeriod(
		TEXT("Battle.Morale.TickPeriod"),
		0.35f,
		TEXT("The MoraleTick GE period in seconds (arch section 12 band 0.25-0.5). "
			 "Under DeterministicTick=1 the sim pins the cadence to this value on "
			 "the fixed tick grid."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCMoraleDeterministicTick(
		TEXT("Battle.Morale.DeterministicTick"),
		0,
		TEXT("Pin the tick to the sim cadence (a game-thread processor forces the GE "
			 "period execution on the fixed tick grid). Every determinism-gated "
			 "scenario and the two-run gate set this to 1 (the T4.7 carve-out, "
			 "spec section 4.8)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleWavering(
		TEXT("Battle.Morale.WaveringThreshold"),
		1.f,
		TEXT("Absolute morale below which a unit wavers (MC5). T6.1 co-audit: "
			 "one point above the M2TW break-at-zero anchor, BELOW the fixture's "
			 "lowest base morale (levy 2) so nothing wavers at spawn -- the "
			 "placeholder 4.0 sat above half the roster."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleBreak(
		TEXT("Battle.Morale.BreakThreshold"),
		0.f,
		TEXT("Absolute morale below which a wavering unit breaks (scaled by "
			 "discipline break resistance, spec section 4.4)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleWaveringHysteresis(
		TEXT("Battle.Morale.WaveringHysteresis"),
		1.f,
		TEXT("Recovery margin above the wavering threshold (anti-thrash, R-M5)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleRallyThreshold(
		TEXT("Battle.Morale.RallyThreshold"),
		1.5f,
		TEXT("Morale required to attempt a rally roll (MC6). T6.1 co-audit: "
			 "reachable by the fixture's LOWEST base (levy 2) under the general "
			 "aura (+2.0) and unreachable without it (a routed levy sits below "
			 "0.5) -- the placeholder 6.0 made levy and peasant archers "
			 "permanently unrallyable."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleRallyBase(
		TEXT("Battle.Morale.RallyBase"),
		0.4f,
		TEXT("Base rally chance per roll (before decay + discipline scaling)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleRallyDecay(
		TEXT("Battle.Morale.RallyDecay"),
		0.5f,
		TEXT("Per-break multiplier on the rally chance (RoutCount decay)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCMoraleMaxRallies(
		TEXT("Battle.Morale.MaxRallies"),
		2,
		TEXT("Breaks beyond which a unit is Shattered (never rallies, MC6)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleSpikePerCasualty(
		TEXT("Battle.Morale.SpikePerCasualty"),
		1.f,
		TEXT("Fresh-casualty spike amplitude per death (section 4.2)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleSpikeHalfLife(
		TEXT("Battle.Morale.SpikeHalfLifeS"),
		4.f,
		TEXT("Fresh-casualty spike half-life in seconds."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleOfficerSpike(
		TEXT("Battle.Morale.OfficerSpikeMultiplier"),
		3.f,
		TEXT("Spike amplification when the casualty is an officer (MC8)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleGeneralShockBase(
		TEXT("Battle.Morale.GeneralShockBase"),
		6.f,
		TEXT("Team-wide general-death shock amplitude (section 4.2)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleGeneralShockHalfLife(
		TEXT("Battle.Morale.GeneralShockHalfLifeS"),
		20.f,
		TEXT("General-death shock half-life in seconds (the M2TW shape: sharpest "
			 "right after the death, improving gradually)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleCascadeRadius(
		TEXT("Battle.Morale.CascadeRadiusM"),
		45.f,
		TEXT("Routing-friends cascade radius on unit envelope centers (MC7)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleGeneralAuraRadius(
		TEXT("Battle.Morale.GeneralAuraRadiusM"),
		60.f,
		TEXT("General aura radius on unit envelope centers (section 4.6)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleGeneralAuraBase(
		TEXT("Battle.Morale.GeneralAuraBase"),
		2.f,
		TEXT("General aura magnitude at zero distance (falls linearly to 0 at the "
			 "radius)."),
		ECVF_Default);

	// Task 4b.2 (B-5/B-6): the terror per-token shaping. Base defaults 0 =
	// the flags-off identity (the fold contributes zero until enabled; the
	// weight row is the seam constant TerrorAura = -1, the aura's mirror).
	TAutoConsoleVariable<float> GCMoraleTerrorAuraFootBase(
		TEXT("Battle.Morale.TerrorAuraFootBase"),
		0.f,
		TEXT("Terror drain magnitude at zero distance for frighten_foot rows "
			 "(falls linearly to 0 at the radius). 0 = off (the identity)."),
		ECVF_Default);
	TAutoConsoleVariable<float> GCMoraleTerrorAuraFootRadius(
		TEXT("Battle.Morale.TerrorAuraFootRadiusM"),
		30.f,
		TEXT("Terror drain radius (m) for frighten_foot rows."),
		ECVF_Default);
	TAutoConsoleVariable<float> GCMoraleTerrorAuraMountedBase(
		TEXT("Battle.Morale.TerrorAuraMountedBase"),
		0.f,
		TEXT("Terror drain magnitude at zero distance for frighten_mounted rows."),
		ECVF_Default);
	TAutoConsoleVariable<float> GCMoraleTerrorAuraMountedRadius(
		TEXT("Battle.Morale.TerrorAuraMountedRadiusM"),
		45.f,
		TEXT("Terror drain radius (m) for frighten_mounted rows."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCMoraleImpetuousChargeRadius(
		TEXT("Battle.Morale.ImpetuousChargeRadiusM"),
		30.f,
		TEXT("Impetuous auto-charge trigger distance (MC9; suppressed by GuardMode "
			 "and player orders)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCMoraleSoakRout(
		TEXT("Battle.Morale.SoakRout"),
		0,
		TEXT("Nightly soak variant flag: the spawn mix gains a general + impetuous "
			 "rows. 0 = byte-identical baseline (the SoakVolley pattern)."),
		ECVF_Default);
}

namespace FMoraleCVars
{
	bool Enable() { return GCMoraleEnable.GetValueOnAnyThread() != 0; }
	float TickPeriod() { return GCMoraleTickPeriod.GetValueOnAnyThread(); }
	bool DeterministicTick() { return GCMoraleDeterministicTick.GetValueOnAnyThread() != 0; }
	float WaveringThreshold() { return GCMoraleWavering.GetValueOnAnyThread(); }
	float BreakThreshold() { return GCMoraleBreak.GetValueOnAnyThread(); }
	float WaveringHysteresis() { return GCMoraleWaveringHysteresis.GetValueOnAnyThread(); }
	float RallyThreshold() { return GCMoraleRallyThreshold.GetValueOnAnyThread(); }
	float RallyBase() { return GCMoraleRallyBase.GetValueOnAnyThread(); }
	float RallyDecay() { return GCMoraleRallyDecay.GetValueOnAnyThread(); }
	int32 MaxRallies() { return GCMoraleMaxRallies.GetValueOnAnyThread(); }
	float SpikePerCasualty() { return GCMoraleSpikePerCasualty.GetValueOnAnyThread(); }
	float SpikeHalfLifeS() { return GCMoraleSpikeHalfLife.GetValueOnAnyThread(); }
	float OfficerSpikeMultiplier() { return GCMoraleOfficerSpike.GetValueOnAnyThread(); }
	float GeneralShockBase() { return GCMoraleGeneralShockBase.GetValueOnAnyThread(); }
	float GeneralShockHalfLifeS() { return GCMoraleGeneralShockHalfLife.GetValueOnAnyThread(); }
	float CascadeRadiusM() { return GCMoraleCascadeRadius.GetValueOnAnyThread(); }
	float GeneralAuraRadiusM() { return GCMoraleGeneralAuraRadius.GetValueOnAnyThread(); }
	float GeneralAuraBase() { return GCMoraleGeneralAuraBase.GetValueOnAnyThread(); }
	float TerrorAuraFootBase() { return GCMoraleTerrorAuraFootBase.GetValueOnAnyThread(); }
	float TerrorAuraFootRadiusM() { return GCMoraleTerrorAuraFootRadius.GetValueOnAnyThread(); }
	float TerrorAuraMountedBase() { return GCMoraleTerrorAuraMountedBase.GetValueOnAnyThread(); }
	float TerrorAuraMountedRadiusM() { return GCMoraleTerrorAuraMountedRadius.GetValueOnAnyThread(); }
	float ImpetuousChargeRadiusM() { return GCMoraleImpetuousChargeRadius.GetValueOnAnyThread(); }
	bool SoakRout() { return GCMoraleSoakRout.GetValueOnAnyThread() != 0; }
}

// -- Seam bodies (the seam task; T4.1/T4.2-locked by the Battle.Unit.Morale.*
// suites). Pure functions over plain inputs: no world, no fragments, no
// wall-clock, no streams. Constants are the A16 golden-locked placeholders.

float EvaluateMorale(const FMoraleTickInputs& Inputs)
{
	// Spec section 4.1 (MC2): the weighted sum. Signs live in the weights
	// (FMoraleWeights); discipline never scales them (it scales the
	// thresholds + the rally factor). ArcExposureMask contributes per SET
	// BIT (the engaged-sector count), not per mask value.
	const int32 ExposedSectors = FMath::CountBits(static_cast<uint32>(Inputs.ArcExposureMask));
	return Inputs.BaseMorale
		+ FMoraleWeights::CasualtyFraction * Inputs.CasualtyFraction
		+ FMoraleWeights::FreshCasualtySpike * Inputs.FreshCasualtySpike
		+ FMoraleWeights::LocalOdds * Inputs.LocalOdds
		+ FMoraleWeights::ArcExposurePerSector * static_cast<float>(ExposedSectors)
		+ FMoraleWeights::RoutingFriend * static_cast<float>(Inputs.RoutingFriendsNearby)
		+ FMoraleWeights::GeneralAura * Inputs.GeneralAuraBonus
		+ FMoraleWeights::GeneralDeathShock * Inputs.GeneralDeathShock
		+ FMoraleWeights::HeightAdvantage * Inputs.HeightAdvantage
		+ FMoraleWeights::WinningLocally * Inputs.WinningLocally
		+ FMoraleWeights::TerrorAura * Inputs.TerrorAuraBonus;
}

// -- Task 4b.3 (B-7): the climate rate scale (the FatigueRateScale slot's
// EDU mapping; the T4b.3 golden locks the mapping). stat_heat is the
// Phase 5 slot (the brief's B-7; the spec section 10 row records it). --
float ClimateFatigueRateScale(const FString& ClimateToken)
{
	if (ClimateToken == TEXT("hardy"))
	{
		return 0.85f;
	}
	if (ClimateToken == TEXT("very_hardy"))
	{
		return 0.7f;
	}
	return 1.f;
}

float EvaluateDecay(float Value, float Dt, float HalfLifeS)
{
	// Spec section 4.2: the shared closed form for BOTH decay terms (the
	// fresh-casualty spike + the general-death shock) -- exponential
	// half-life, evaluated as 2^(-Dt/HalfLife) so the half-life property is
	// exact at Dt == HalfLife (the T4.1 golden anchors on it). HalfLife <=
	// 0 is a no-op decay (the caller's parameter contract).
	if (HalfLifeS <= 0.f || Dt <= 0.f)
	{
		return Value;
	}
	return Value * FMath::Pow(0.5f, Dt / HalfLifeS);
}

void AccumulateExertion(float& InOutExertion, float Dt, bool bRunning, bool bFighting, float FatigueRateScale)
{
	// Spec section 4.3: the rates sum when both activities hold (running
	// while fighting is the cavalry-charge posture); walking/idle adds
	// nothing v1. MONOTONE by construction (rates + scale are non-negative;
	// there is no rest model this phase -- spec section 10 records the seam).
	const float Scale = FMath::Max(0.f, FatigueRateScale);
	const float Rate = (bRunning ? FFatigueConstants::RunRate : 0.f)
		+ (bFighting ? FFatigueConstants::FightRate : 0.f);
	InOutExertion += Rate * Scale * FMath::Max(0.f, Dt);
}

EBattleFatigueState EvaluateFatigueState(float ExertionIntegral)
{
	// Spec section 4.3: the six-band ladder on the integral (FFatigueConstants).
	if (ExertionIntegral < FFatigueConstants::BandActive)
	{
		return EBattleFatigueState::Fresh;
	}
	if (ExertionIntegral < FFatigueConstants::BandQuiteTired)
	{
		return EBattleFatigueState::Active;
	}
	if (ExertionIntegral < FFatigueConstants::BandTired)
	{
		return EBattleFatigueState::QuiteTired;
	}
	if (ExertionIntegral < FFatigueConstants::BandVeryTired)
	{
		return EBattleFatigueState::Tired;
	}
	if (ExertionIntegral < FFatigueConstants::BandExhausted)
	{
		return EBattleFatigueState::VeryTired;
	}
	return EBattleFatigueState::Exhausted;
}

FFatigueMultipliers GetFatigueMultipliers(EBattleFatigueState State)
{
	// Spec section 4.3 (T4.2 golden): the community model's shape -- the
	// first step off Fresh costs only RELOAD (the M2TW observation: 'a unit
	// falling from fresh to active only reduces the reload speed of any
	// ranged attacks it has by 5% and nothing else'), melee stats degrade
	// from QuiteTired, speed from QuiteTired. A16 placeholders; the catalog
	// writes these minus 1 into the *Mod attributes (ADR-36).
	switch (State)
	{
	case EBattleFatigueState::Fresh:
	default:
		return FFatigueMultipliers{ 1.f, 1.f, 1.f, 1.f };
	case EBattleFatigueState::Active:
		return FFatigueMultipliers{ 1.f, 1.f, 1.f, 0.95f };
	case EBattleFatigueState::QuiteTired:
		return FFatigueMultipliers{ 0.9f, 0.92f, 0.95f, 0.85f };
	case EBattleFatigueState::Tired:
		return FFatigueMultipliers{ 0.8f, 0.85f, 0.9f, 0.75f };
	case EBattleFatigueState::VeryTired:
		return FFatigueMultipliers{ 0.7f, 0.75f, 0.85f, 0.65f };
	case EBattleFatigueState::Exhausted:
		return FFatigueMultipliers{ 0.55f, 0.65f, 0.75f, 0.55f };
	}
}

EBattleRoutState EvaluateRoutTransition(EBattleRoutState Current, float Morale,
	float WaveringThreshold, float BreakThreshold, float WaveringHysteresis,
	uint8 RoutCount, int32 MaxRallies, EBattleDiscipline Discipline)
{
	// Spec section 4.4 (MC5). The break threshold shifts by the discipline's
	// break offset (an impetuous unit breaks as easily as a normal one; a
	// disciplined one holds longer -- MC2's rule that discipline never
	// touches the weights). Shattered is TERMINAL and latches from Routing
	// at the rally-exhaustion limit; the caller owns RoutCount++ on the
	// Wavering->Routing edge (the pass applies the break).
	if (Current == EBattleRoutState::Shattered)
	{
		return EBattleRoutState::Shattered;
	}
	if (Current == EBattleRoutState::Routing)
	{
		return (static_cast<int32>(RoutCount) >= MaxRallies)
			? EBattleRoutState::Shattered
			: EBattleRoutState::Routing;
	}
	const int32 DisciplineIndex = static_cast<int32>(Discipline);
	const float EffectiveBreak = BreakThreshold + FMoraleDiscipline::BreakOffset[
		FMath::Clamp(DisciplineIndex, 0, 3)];
	if (Morale < EffectiveBreak)
	{
		return EBattleRoutState::Routing;
	}
	if (Morale < WaveringThreshold)
	{
		return EBattleRoutState::Wavering;
	}
	// Recovery needs the hysteresis margin above the wavering threshold
	// (R-M5: no break->recover->break thrash at the boundary).
	if (Current == EBattleRoutState::Wavering && Morale < WaveringThreshold + WaveringHysteresis)
	{
		return EBattleRoutState::Wavering;
	}
	return EBattleRoutState::Steady;
}

float EvaluateRallyChance(float Morale, float RallyThreshold, float RallyBase, float RallyDecay,
	EBattleDiscipline Discipline, uint8 RoutCount, bool bGeneralAuraActive)
{
	// Spec section 4.5 (MC6): the roll requires BOTH the morale recovery AND
	// a living friendly general in aura range (T4.3's with/without sub-runs);
	// the chance decays per break and scales by the discipline's rally
	// factor (impetuous rallies worst, disciplined best).
	if (!bGeneralAuraActive || Morale < RallyThreshold)
	{
		return 0.f;
	}
	const int32 DisciplineIndex = static_cast<int32>(Discipline);
	const float DisciplineFactor = FMoraleDiscipline::RallyFactor[FMath::Clamp(DisciplineIndex, 0, 3)];
	return RallyBase * FMath::Pow(RallyDecay, static_cast<float>(RoutCount)) * DisciplineFactor;
}

bool EvaluateRally(float Morale, float RallyThreshold, float RallyBase, float RallyDecay,
	EBattleDiscipline Discipline, uint8 RoutCount, bool bGeneralAuraActive, float Roll01)
{
	// Spec section 4.5: the decision is the chance vs the caller-drawn
	// Roll01 (strictly below -- a 0 chance never rallies, a 1 chance fails
	// only on Roll01 == chance exactly, measure-zero).
	const float Chance = EvaluateRallyChance(Morale, RallyThreshold, RallyBase, RallyDecay,
		Discipline, RoutCount, bGeneralAuraActive);
	return Roll01 < Chance;
}
