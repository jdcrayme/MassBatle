// CombatResolutionProcessor.cpp -- arch §4 row 10 (task 7; spec §4.8).
#include "CombatResolutionProcessor.h"
#include "BattleCombatFragments.h"
#include "BattleFragments.h"
#include "BattleMelee.h"
#include "BattleEngagement.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GCCombatResolve(
		TEXT("Battle.Combat.Resolve"),
		1,
		TEXT("Run the worker-side melee resolution pass (arch §4 row 10). Phase 2 task 7."),
		ECVF_Default);
}

UCombatResolutionProcessor::UCombatResolutionProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ResolutionQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("IntegrateProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 10).
	bRequiresGameThreadExecution = false;
}

void UCombatResolutionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ResolutionQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadWrite);
	ResolutionQuery.AddRequirement<FBaseStatsFragment>(EMassFragmentAccess::ReadOnly);
	ResolutionQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	ResolutionQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // corpses never roll
	ResolutionQuery.RegisterWithProcessor(*this);
}

void UCombatResolutionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GCCombatResolve.GetValueOnAnyThread() == 0 || FCombatCVars::Enable() == 0)
	{
		return;
	}
	UWorld* World = Context.GetWorld();
	UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	const float DT = Context.GetDeltaTimeSeconds();
	const int32 SectorCount = FMath::Clamp(FCombatCVars::SectorCount(), 1, 16);
	const float Interval = FMath::Max(0.01f, FCombatCVars::AttackInterval());
	const float Recovery = FMeleeMath::RecoveryWindow(Interval);

	ResolutionQuery.ForEachEntityChunk(Context, [DT, SectorCount, Interval, Recovery, Debug](FMassExecutionContext& Context)
	{
		auto Pairs = Context.GetMutableFragmentView<FCombatPairFragment>();
		const auto Stats = Context.GetFragmentView<FBaseStatsFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FCombatPairFragment& Pair = Pairs[i];
			if (!Pair.Target.IsValid())
			{
				continue; // unpaired: nothing to roll against (a waiter never
				// rolls -- the attacker-authoritative model rolls only across a
				// live pair, ADR-05)
			}

			// State machine (spec §4.8): Striking (roll at timer expiry) ->
			// Recovering (cooldown) -> repeat. Timers advance worker-side on
			// fixed DT under the test-mode contract (B.0).
			switch (Pair.State)
			{
			case ECombatPairState::Pressing:
			case ECombatPairState::Seeking:
				// Waiters hold their staggered timers; the strike fires when
				// the pair exists AND the timer has run out.
				Pair.AttackTimer -= DT;
				if (Pair.AttackTimer > 0.f)
				{
					break;
				}
				Pair.State = ECombatPairState::Striking;
				// fall through: strike this tick
			case ECombatPairState::Striking:
			{
				// -- Contested roll (spec §4.8; A6 placeholders, T2.1-locked
				// structure). Attack = base + decaying charge bonus.
				const float Attack = Stats[i].MeleeAttack + Pair.ChargeBonusRemaining;
				const int32 Sector = FCombatSolver::SectorIndexFromAngle(Pair.ContactAngle, SectorCount);
				const float Defense = Pair.DefenderDefense * FMeleeMath::SkillArcMult(Sector, SectorCount)
					+ Pair.DefenderShield * FMeleeMath::ShieldArcWindow(Sector, SectorCount)
					+ Pair.DefenderArmor * FMeleeMath::ArmorMultiplier(Stats[i].bArmorPiercing != 0);
				const float P = FMeleeMath::HitChance(Attack, Defense);

				// Roll: deterministic per-entity hash (A13: the worker cannot
				// touch the game-thread RNG registry; seed-derived, no
				// wall-clock; the tick input is the pair's OWN roll count --
				// GFrameCounter differs across two runs of the same seed and
				// would diverge T2.8). The hash lives in FMeleeMath beside
				// the stagger hash (B-2: one home for the Knuth multipliers).
				const float Roll = FMeleeMath::CombatRoll01(
					Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
					static_cast<uint32>(Pair.RollCount));
				++Pair.RollCount;
				const bool bLanded = Roll < P * FMath::Max(0.f, Stats[i].Lethality);

				if (Debug)
				{
					Debug->RecordCombatRoll(bLanded);
				}
				UE_LOG(LogMassBattle, Verbose,
					TEXT("CombatRoll Entity=%u:%u Target=%u:%u A=%.1f D=%.1f P=%.2f Roll=%.3f Landed=%d Charge=%.1f"),
					Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
					Pair.Target.Index, Pair.Target.SerialNumber, Attack, Defense, P, Roll, bLanded ? 1 : 0,
					Pair.ChargeBonusRemaining);

				if (bLanded)
				{
					// Composite HP (spec §4.8): the mount pool absorbs first;
					// at 0 the landed hit kills through the EXISTING Phase 0
					// death path (FDeadTag via command buffer -> observer ->
					// signal -> HandleUnitDeath, F2).
					if (Pair.DefenderMountRemaining > 0)
					{
						--Pair.DefenderMountRemaining;
					}
					else
					{
						Context.Defer().AddTag<FDeadTag>(Pair.Target);
						if (Debug)
						{
							// Death-by-arc bookkeeping (EC6 observability).
							Debug->RecordDeathByArc(Sector, SectorCount);
						}
						UE_LOG(LogMassBattle, Display,
							TEXT("EXEC_RESULT CombatKill Attacker=%u:%u Unit=%u:%u Target=%u:%u Sector=%d Charge=%.1f"),
							Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
							Owners[i].Owner.Index, Owners[i].Owner.Serial,
							Pair.Target.Index, Pair.Target.SerialNumber, Sector, Pair.ChargeBonusRemaining);
					}
				}

				// Recovering window (the charge bonus decays in the charge
				// processor -- one writer per field).
				Pair.State = ECombatPairState::Recovering;
				Pair.AttackTimer = Recovery;
				break;
			}
			case ECombatPairState::Recovering:
				Pair.AttackTimer -= DT;
				if (Pair.AttackTimer <= 0.f)
				{
					// Back to striking with the FULL interval (the cycle
					// period is AttackInterval: recovery + strike wait).
					Pair.State = ECombatPairState::Striking;
					Pair.AttackTimer = Interval - Recovery;
				}
				break;
			default:
				break;
			}
		}
	});
}
