// BattleEngagement.cpp -- CVAR block (spec §4.11) + task-0 solver stubs.
#include "BattleEngagement.h"

#include "HAL/IConsoleManager.h"

// -- Battle.Combat.* CVAR block (spec §4.11; defaults are A6 placeholders) --
namespace
{
	TAutoConsoleVariable<int32> GCCombatEnable(
		TEXT("Battle.Combat.Enable"),
		0,
		TEXT("Master combat flag. 0 = pre-combat behavior (byte-identical; B.8 item 6). "
			 "Flips to default 1 when the task-7 resolution pass lands end-to-end."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatClusterPeriod(
		TEXT("Battle.Combat.ClusterPeriod"),
		0.25f,
		TEXT("Engagement-record / cluster-graph recompute period, seconds (4 Hz)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatMaxMobSlots(
		TEXT("Battle.Combat.MaxMobSlots"),
		7,
		TEXT("Per-defender attack-slot budget ceiling (mob mode)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatFormedSlots(
		TEXT("Battle.Combat.FormedSlots"),
		2,
		TEXT("Per-defender attack-slot budget floor (formed line)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatSecondRankBonus(
		TEXT("Battle.Combat.SecondRankBonus"),
		1,
		TEXT("Spear-reach budget add (second rank strikes through the front rank)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatNeighborsForFullCoverage(
		TEXT("Battle.Combat.NeighborsForFullCoverage"),
		6,
		TEXT("Same-unit living neighbors that read as full coverage (1.0)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatSectorCount(
		TEXT("Battle.Combat.SectorCount"),
		8,
		TEXT("Exposed-angle quantization buckets around a defender (8-12 supported)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatMobPressureFactor(
		TEXT("Battle.Combat.MobPressureFactor"),
		3,
		TEXT("Mob DesiredPressure multiplier (defenderAlive x factor, floored at MaxMobSlots -- brief A14)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatPairHysteresis(
		TEXT("Battle.Combat.PairHysteresis"),
		0.4f,
		TEXT("Distance-breakup age gate, seconds (0.3-0.5 s band; R-C1 churn guard)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatAttackInterval(
		TEXT("Battle.Combat.AttackInterval"),
		1.5f,
		TEXT("Base attack-cycle interval, seconds (staggered per entity, spec §4.8)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatHitChanceFloor(
		TEXT("Battle.Combat.HitChanceFloor"),
		0.05f,
		TEXT("Contested-roll hit-chance floor (A6 placeholder)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatHitChanceCap(
		TEXT("Battle.Combat.HitChanceCap"),
		0.95f,
		TEXT("Contested-roll hit-chance cap (A6 placeholder)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatChargeSpeedThreshold(
		TEXT("Battle.Combat.ChargeSpeedThreshold"),
		4.0f,
		TEXT("Fresh-contact charge gate, m/s (EC7)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GCCombatChargeDecayWindow(
		TEXT("Battle.Combat.ChargeDecayWindow"),
		5.0f,
		TEXT("Charge-bonus decay window, seconds (EC7)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatSoakClash(
		TEXT("Battle.Combat.SoakClash"),
		0,
		TEXT("Soak clash-mix variant on the movement soak controller (nightly; "
			 "0 = byte-identical T1.8 baseline, R-C9)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GCCombatStateTree(
		TEXT("Battle.Combat.StateTree"),
		0,
		TEXT("I4 StateTree unit-FSM prototype flag (hard half-day timebox, brief A7)."),
		ECVF_Default);
}

namespace FCombatCVars
{
	int32 Enable() { return GCCombatEnable.GetValueOnGameThread(); }
	float ClusterPeriod() { return GCCombatClusterPeriod.GetValueOnGameThread(); }
	int32 MaxMobSlots() { return GCCombatMaxMobSlots.GetValueOnGameThread(); }
	int32 FormedSlots() { return GCCombatFormedSlots.GetValueOnGameThread(); }
	int32 SecondRankBonus() { return GCCombatSecondRankBonus.GetValueOnGameThread(); }
	int32 NeighborsForFullCoverage() { return GCCombatNeighborsForFullCoverage.GetValueOnGameThread(); }
	int32 SectorCount() { return GCCombatSectorCount.GetValueOnGameThread(); }
	int32 MobPressureFactor() { return GCCombatMobPressureFactor.GetValueOnGameThread(); }
	float PairHysteresis() { return GCCombatPairHysteresis.GetValueOnGameThread(); }
	float AttackInterval() { return GCCombatAttackInterval.GetValueOnGameThread(); }
	float HitChanceFloor() { return GCCombatHitChanceFloor.GetValueOnGameThread(); }
	float HitChanceCap() { return GCCombatHitChanceCap.GetValueOnGameThread(); }
	float ChargeSpeedThreshold() { return GCCombatChargeSpeedThreshold.GetValueOnGameThread(); }
	float ChargeDecayWindow() { return GCCombatChargeDecayWindow.GetValueOnGameThread(); }
	int32 SoakClash() { return GCCombatSoakClash.GetValueOnGameThread(); }
	int32 StateTree() { return GCCombatStateTree.GetValueOnGameThread(); }
}

// -- Task-0 solver stubs (bodies land in task 4; failure sentinels) --
namespace FCombatSolver
{
	int32 SlotBudget(const FCombatDefenderView& /*Defender*/, float /*Coverage*/, const FCombatAttackerView& /*Attacker*/)
	{
		return 0; // failure sentinel: 0 is never a valid budget (spec: >= 1)
	}

	float ComputeCoverage(const FCombatDefenderView& /*Defender*/)
	{
		return -1.f; // failure sentinel: valid coverage is [0,1]
	}

	uint8 PickExposedAngle(const FCombatDefenderView& /*Defender*/, float /*Coverage*/)
	{
		return 255; // failure sentinel: valid quantized angle is 0-254
	}

	int32 DesiredPressure(int32 /*Mode*/, int32 /*AttackerAlive*/, int32 /*DefenderAlive*/)
	{
		return 0; // failure sentinel: valid pressure is >= 1
	}
}
