// BattleMorale.cpp -- the Battle.Morale.* CVAR block (spec morale-and-gas
// section 4.12) + the morale seam bodies. Task 0 ships the CVAR block with
// STUB seam bodies (the Phase 3 0011 pattern); the seam task replaces them,
// T4.1/T4.2-locked. Defaults are A6 placeholders (ADR-37).
#include "BattleMorale.h"
#include "HAL/IConsoleManager.h"

// -- Battle.Morale.* CVAR block (spec section 4.12; defaults are A6 placeholders) --
namespace
{
	TAutoConsoleVariable<int32> GCMoraleEnable(
		TEXT("Battle.Morale.Enable"),
		0,
		TEXT("Master morale flag (tick GE + FSM + effects). Flipped to default 1 at "
			 "task 6's FSM landing end-to-end (ground rule 6; B.8 item 6). 0 = "
			 "pre-morale behavior: no GE applied, no catalog, the pass early-outs."),
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
		4.f,
		TEXT("Absolute morale below which a unit wavers (MC5)."),
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
		6.f,
		TEXT("Morale required to attempt a rally roll (MC6)."),
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
	float ImpetuousChargeRadiusM() { return GCMoraleImpetuousChargeRadius.GetValueOnAnyThread(); }
	bool SoakRout() { return GCMoraleSoakRout.GetValueOnAnyThread() != 0; }
}

// -- Seam bodies: task-0 STUBS (the 0011 pattern). The seam task replaces
// every one of these with the T4.1/T4.2-locked implementations; the stubs are
// deliberately inert so flags-off behavior is byte-identical by construction.

float EvaluateMorale(const FMoraleTickInputs& Inputs)
{
	// STUB: returns 0, reads nothing (the stack lands with the seam task).
	(void)Inputs;
	return 0.f;
}

float EvaluateDecay(float Value, float Dt, float HalfLifeS)
{
	// STUB: identity (the closed-form half-life lands with the seam task).
	(void)Dt;
	(void)HalfLifeS;
	return Value;
}

void AccumulateExertion(float& InOutExertion, float Dt, bool bRunning, bool bFighting, float FatigueRateScale)
{
	// STUB: no accumulation until the seam task.
	(void)Dt;
	(void)bRunning;
	(void)bFighting;
	(void)FatigueRateScale;
	(void)InOutExertion;
}

EBattleFatigueState EvaluateFatigueState(float ExertionIntegral)
{
	// STUB: Fresh regardless of the integral.
	(void)ExertionIntegral;
	return EBattleFatigueState::Fresh;
}

FFatigueMultipliers GetFatigueMultipliers(EBattleFatigueState State)
{
	// STUB: identity row for every state (the golden table lands with the
	// seam task, T4.2).
	(void)State;
	return FFatigueMultipliers();
}

EBattleRoutState EvaluateRoutTransition(EBattleRoutState Current, float Morale,
	float WaveringThreshold, float BreakThreshold, float WaveringHysteresis,
	uint8 RoutCount, int32 MaxRallies, EBattleDiscipline Discipline)
{
	// STUB: no transitions (the FSM lands with the seam task, T4.1).
	(void)Morale;
	(void)WaveringThreshold;
	(void)BreakThreshold;
	(void)WaveringHysteresis;
	(void)RoutCount;
	(void)MaxRallies;
	(void)Discipline;
	return Current;
}

float EvaluateRallyChance(float Morale, float RallyThreshold, EBattleDiscipline Discipline,
	uint8 RoutCount, bool bGeneralAuraActive)
{
	// STUB: zero chance (the rally curve lands with the seam task).
	(void)Morale;
	(void)RallyThreshold;
	(void)Discipline;
	(void)RoutCount;
	(void)bGeneralAuraActive;
	return 0.f;
}

bool EvaluateRally(float Morale, float RallyThreshold, EBattleDiscipline Discipline,
	uint8 RoutCount, bool bGeneralAuraActive, float Roll01)
{
	// STUB: never rallies (the decision lands with the seam task).
	(void)Morale;
	(void)RallyThreshold;
	(void)Discipline;
	(void)RoutCount;
	(void)bGeneralAuraActive;
	(void)Roll01;
	return false;
}
