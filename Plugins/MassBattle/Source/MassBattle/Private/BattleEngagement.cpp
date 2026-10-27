// BattleEngagement.cpp -- CVAR block (spec §4.11) + the solver seam (task 4).
#include "BattleEngagement.h"
#include "BattleCombatFragments.h"
#include "BattleRegistry.h" // FCombatTallyTable (plain scratch struct)
#include "BattleMelee.h"
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

namespace FCombatSolver
{
	int32 SectorIndexFromAngle(uint8 ContactAngle, int32 SectorCount)
	{
		const int32 Count = FMath::Max(1, SectorCount);
		return (static_cast<int32>(ContactAngle) * Count) / 256;
	}

	uint8 SectorCenterAngle(int32 SectorIndex, int32 SectorCount)
	{
		const int32 Count = FMath::Max(1, SectorCount);
		const int32 Center = ((2 * SectorIndex + 1) * 256) / (2 * Count);
		return static_cast<uint8>(Center & 0xFF);
	}

	int32 SlotBudget(const FCombatDefenderView& Defender, float Coverage, const FCombatAttackerView& Attacker)
	{
		const float Lerped = FMath::Lerp(static_cast<float>(FCombatCVars::MaxMobSlots()),
			static_cast<float>(FCombatCVars::FormedSlots()), FMath::Clamp(Coverage, 0.f, 1.f));
		int32 Budget = FMath::RoundToInt(Lerped);
		if (Attacker.ReachClass >= static_cast<uint8>(EBattleWeaponReach::Spear))
		{
			Budget += FCombatCVars::SecondRankBonus();
		}
		return FMath::Max(1, Budget);
	}

	float ComputeCoverage(const FCombatDefenderView& Defender, const TArray<FVector2D>& SameUnitNeighborPositionsM)
	{
		// Same-unit living neighbors within Spacing * 1.25 (self excluded by
		// the caller -- the coverage of a lone entity is 0).
		const float Radius = FMath::Max(0.f, Defender.Spacing) * 1.25f;
		const float RadiusSq = Radius * Radius;
		int32 Neighbors = 0;
		for (const FVector2D& NeighborPos : SameUnitNeighborPositionsM)
		{
			if (FVector2D::DistSquared(Defender.Pos, NeighborPos) <= RadiusSq)
			{
				++Neighbors;
			}
		}
		const float Normalizer = static_cast<float>(FMath::Max(1, FCombatCVars::NeighborsForFullCoverage()));
		return FMath::Min(1.f, static_cast<float>(Neighbors) / Normalizer);
	}

	uint8 PickExposedAngle(int32 SectorCount, const FSectorPickContext& Context)
	{
		const int32 Count = FMath::Clamp(SectorCount, 1, 16);
		if (Context.TotalBudget <= 0)
		{
			return 255;
		}
		// Even share of the budget per sector; a sector is "full" at its share.
		const int32 Share = FMath::DivideAndRoundUp(Context.TotalBudget, Count);

		int32 BestExposed = INDEX_NONE;
		int32 BestExposedCount = MAX_int32;
		int32 BestCovered = INDEX_NONE;
		int32 BestCoveredCount = MAX_int32;
		for (int32 Sector = 0; Sector < Count; ++Sector)
		{
			const bool bCovered = (Context.CoveredMask & (1u << Sector)) != 0;
			const int32 Assigned = Context.AssignedPerSector[Sector];
			if (Assigned >= Share)
			{
				continue; // sector at share
			}
			if (!bCovered)
			{
				if (Assigned < BestExposedCount)
				{
					BestExposedCount = Assigned;
					BestExposed = Sector;
				}
			}
			else if (Assigned < BestCoveredCount)
			{
				BestCoveredCount = Assigned;
				BestCovered = Sector;
			}
		}
		// Exposed sectors fill BEFORE covered arcs open (spec §4.5; T2.2).
		const int32 Chosen = BestExposed != INDEX_NONE ? BestExposed : BestCovered;
		return Chosen != INDEX_NONE ? SectorCenterAngle(Chosen, Count) : 255;
	}

	int32 DesiredPressure(int32 Mode, int32 AttackerAlive, int32 DefenderAlive)
	{
		if (DefenderAlive <= 0 || AttackerAlive <= 0)
		{
			return 0;
		}
		if (Mode == 2) // Mob (EEngagementMode)
		{
			const int32 Floored = FMath::Max(DefenderAlive * FCombatCVars::MobPressureFactor(),
				FCombatCVars::MaxMobSlots());
			return FMath::Min(AttackerAlive, Floored);
		}
		// Frontal / Flank share the frontal formula (spec §3.2).
		return DefenderAlive * FCombatCVars::FormedSlots();
	}

	namespace
	{
		/** Binary search over the handle-sorted defender table. */
		const FAssignDefenderState* FindDefenderSorted(const TArray<FAssignDefenderState>& Defenders, FMassEntityHandle Entity)
		{
			int32 Lo = 0;
			int32 Hi = Defenders.Num() - 1;
			while (Lo <= Hi)
			{
				const int32 Mid = (Lo + Hi) / 2;
				if (Defenders[Mid].Entity == Entity)
				{
					return &Defenders[Mid];
				}
				if (Defenders[Mid].Entity.Index < Entity.Index ||
					(Defenders[Mid].Entity.Index == Entity.Index && Defenders[Mid].Entity.SerialNumber < Entity.SerialNumber))
				{
					Lo = Mid + 1;
				}
				else
				{
					Hi = Mid - 1;
				}
			}
			return nullptr;
		}

		float RingRadiusFor(const FAssignSeekerState& Seeker, const FAssignContext& Context)
		{
			const uint8 ClassIdx = FMath::Min<uint8>(Seeker.ReachClass, 7);
			return Context.DefenderRadiusM + Context.AttackerRadiusM + Context.ReachByClassM[ClassIdx];
		}

		/** Per-defender sector scratch view (Defenders.Num() x 16). */
		int32* SectorRow(TArray<int32>* Scratch, int32 DefenderIndex)
		{
			return Scratch && Scratch->Num() > DefenderIndex * 16 ? &(*Scratch)[DefenderIndex * 16] : nullptr;
		}
	}

	void RunAssignPass(TArray<FAssignSeekerState>& Seekers,
		const TArray<FAssignDefenderState>& Defenders,
		TArray<FAssignRecordView>& Records,
		FCombatTallyTable& Tally,
		const FAssignContext& Context)
	{
		const int32 SectorCount = FMath::Clamp(FCombatCVars::SectorCount(), 1, 16);
		const float Hysteresis = FCombatCVars::PairHysteresis();
		const float Interval = FMath::Max(0.01f, FCombatCVars::AttackInterval());

		if (Context.SectorScratch)
		{
			Context.SectorScratch->SetNumUninitialized(Defenders.Num() * 16);
			for (int32 i = 0; i < Context.SectorScratch->Num(); ++i)
			{
				(*Context.SectorScratch)[i] = 0;
			}
		}

		// A13: seeker iteration shuffled per tick from `Combat.Assign`; the B.5
		// determinism mode (null stream) iterates index-sorted with per-entity
		// jitter on backoff draws instead of RNG.
		TArray<int32, TInlineAllocator<64>> Order;
		Order.Reserve(Seekers.Num());
		for (int32 i = 0; i < Seekers.Num(); ++i)
		{
			Order.Add(i);
		}
		if (Context.ShuffleStream)
		{
			for (int32 i = Order.Num() - 1; i > 0; --i)
			{
				Swap(Order[i], Order[Context.ShuffleStream->RandRange(0, i)]);
			}
		}

		for (const int32 SeekerIdx : Order)
		{
			FAssignSeekerState& S = Seekers[SeekerIdx];

			// Charging entities bypass pairing until impact seeds their first
			// pair (spec §4.7); the charge processor owns the seeding.
			if (S.bCharging)
			{
				continue;
			}

			// Failed-seek backoff (1-3 ticks, jittered): press while waiting.
			if (S.BackoffTicks > 0)
			{
				--S.BackoffTicks;
				continue;
			}

			// -- Top-of-pass validation / release (immediate; later seekers in
			// the same pass see vacated slots -- the <= 1 tick handoff, EC5). --
			if (S.Target.IsValid())
			{
				const FAssignDefenderState* D = FindDefenderSorted(Defenders, S.Target);
				if (!D || D->bValidTarget == 0)
				{
					// Target died / stale handle / invalid -> release.
					S.Target = FMassEntityHandle();
					S.State = static_cast<uint8>(ECombatPairState::Seeking);
				}
			}
			if (S.Target.IsValid() &&
				(S.RecordIndex == INDEX_NONE || !Records.IsValidIndex(S.RecordIndex) || Records[S.RecordIndex].bDead != 0))
			{
				// Unit disengage: the record flagged dead -> mass release of
				// inbound pairs in the same pass (spec §4.7).
				S.Target = FMassEntityHandle();
				S.State = static_cast<uint8>(ECombatPairState::Seeking);
			}
			if (S.Target.IsValid())
			{
				const FAssignDefenderState* D = FindDefenderSorted(Defenders, S.Target);
				const float Ring = RingRadiusFor(S, Context);
				const float BreakRadius = Ring * Context.BreakRadiusFactor;
				const float Dist = D ? FVector2D::Distance(S.PosM, D->PosM) : 0.f;
				if (Dist > BreakRadius && S.PairAge > Hysteresis)
				{
					// Distance breakup fires only beyond the break radius AND
					// the hysteresis age (R-C1 churn guard).
					S.Target = FMassEntityHandle();
					S.State = static_cast<uint8>(ECombatPairState::Seeking);
				}
			}
			if (S.Target.IsValid())
			{
				// Keep: refresh nothing (timers advance worker-side).
				continue;
			}

			// -- Unpaired seeker: needs an intent + a live record. A dead
			// record (disengaged unit) gates re-seeking too -- the intent is
			// gone, not just the pair (spec §4.7).
			if (!S.IntentTarget.IsValid() || S.RecordIndex == INDEX_NONE
				|| !Records.IsValidIndex(S.RecordIndex) || Records[S.RecordIndex].bDead != 0)
			{
				S.State = static_cast<uint8>(ECombatPairState::Seeking);
				continue;
			}
			FAssignRecordView& Record = Records[S.RecordIndex];

			// Unit-level cap: no pressure budget left -> press the line anyway.
			if (Record.AssignedAttackers >= Record.DesiredPressure)
			{
				S.State = static_cast<uint8>(ECombatPairState::Pressing);
				continue;
			}

			// -- Candidate selection: mob = circle-query scan (nearest with an
			// exposed-sector preference); laned = in-lane scan (data, not
			// proximity; the processor's defender table pre-filters the unit).
			const FAssignDefenderState* Chosen = nullptr;
			float ChosenDistSq = 0.f;
			uint8 ChosenAngle = 255;
			for (int32 DIdx = 0; DIdx < Defenders.Num(); ++DIdx)
			{
				const FAssignDefenderState& D = Defenders[DIdx];
				if (D.Unit != S.IntentTarget || D.bValidTarget == 0)
				{
					continue;
				}
				const float DistSq = FVector2D::DistSquared(S.PosM, D.PosM);
				if (DistSq > FMath::Square(Context.SeekRadiusM))
				{
					continue;
				}
				if (Record.Mode != 2 && D.LaneIndex != S.LaneIndex)
				{
					continue; // laned scan stays in the file's slice
				}
				// Per-defender budget check BEFORE angle picking.
				const FCombatDefenderView DefenderView{ D.PosM, D.Spacing, D.Unit, D.Entity };
				const FCombatAttackerView AttackerView{ S.OwnerUnit, S.ReachClass };
				const int32 Budget = SlotBudget(DefenderView, D.Coverage, AttackerView);
				if (Tally.GetCount(D.Entity) >= Budget)
				{
					continue;
				}
				// Exposed-sector pick (covered mask from the coverage cache).
				FSectorPickContext PickContext;
				PickContext.CoveredMask = D.CoveredMask;
				PickContext.TotalBudget = Budget;
				if (int32* Row = SectorRow(Context.SectorScratch, DIdx))
				{
					FMemory::Memcpy(PickContext.AssignedPerSector, Row, sizeof(int32) * 16);
				}
				const uint8 Angle = PickExposedAngle(SectorCount, PickContext);
				if (Angle == 255)
				{
					continue; // every sector at share this pass
				}
				if (!Chosen || DistSq < ChosenDistSq)
				{
					Chosen = &D;
					ChosenDistSq = DistSq;
					ChosenAngle = Angle;
					if (Record.Mode != 2)
					{
						break; // laned: first in-slice candidate wins (slot order)
					}
				}
			}

			if (!Chosen)
			{
				// Failed seek: back off 1-3 ticks (jittered) and press.
				S.State = static_cast<uint8>(ECombatPairState::Pressing);
				S.BackoffTicks = Context.ShuffleStream
					? static_cast<uint8>(Context.ShuffleStream->RandRange(1, 3))
					: static_cast<uint8>(1 + (S.Entity.Index % 3));
				continue;
			}

			// -- Assign. --
			const int32 TallySlot = Tally.FindOrAdd(Chosen->Entity);
			if (TallySlot == INDEX_NONE)
			{
				continue; // tally table full: drop the increment (never grows)
			}
			++Tally.Counts[TallySlot];
			++Record.AssignedAttackers;
			if (int32* Row = SectorRow(Context.SectorScratch, Chosen - Defenders.GetData()))
			{
				++Row[FCombatSolver::SectorIndexFromAngle(ChosenAngle, SectorCount)];
			}
			S.Target = Chosen->Entity;
			S.ContactAngle = ChosenAngle;
			S.State = static_cast<uint8>(ECombatPairState::Pressing);
			S.AttackTimer = FMeleeMath::StaggeredPhaseOffset(S.Entity.Index, Interval);
		}
	}
}
