// ChargeProcessor.cpp -- arch §4 row 9 melee half (task 7; spec §4.9).
#include "ChargeProcessor.h"
#include "BattleCombatFragments.h"
#include "BattleFragments.h"
#include "BattleMelee.h"
#include "BattleEngagement.h"
#include "BattleMovementFragments.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GCCombatCharge(
		TEXT("Battle.Combat.Charge"),
		1,
		TEXT("Run the worker-side charge state machine (arch §4 row 9). Phase 2 task 7."),
		ECVF_Default);
}

UChargeProcessor::UChargeProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ChargeQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("IntegrateProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("CombatResolutionProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses.
	bRequiresGameThreadExecution = false;
}

void UChargeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ChargeQuery.AddRequirement<FChargeFragment>(EMassFragmentAccess::ReadWrite);
	ChargeQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadWrite);
	ChargeQuery.AddRequirement<FCombatIntentFragment>(EMassFragmentAccess::ReadOnly);
	ChargeQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ChargeQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	ChargeQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	ChargeQuery.RegisterWithProcessor(*this);
}

void UChargeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GCCombatCharge.GetValueOnAnyThread() == 0 || FCombatCVars::Enable() == 0)
	{
		return;
	}
	UWorld* World = Context.GetWorld();
	UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	const float DT = Context.GetDeltaTimeSeconds();
	const float Threshold = FCombatCVars::ChargeSpeedThreshold();

	ChargeQuery.ForEachEntityChunk(Context, [DT, Threshold, Debug](FMassExecutionContext& Context)
	{
		auto Charges = Context.GetMutableFragmentView<FChargeFragment>();
		auto Pairs = Context.GetMutableFragmentView<FCombatPairFragment>();
		const auto Intents = Context.GetFragmentView<FCombatIntentFragment>();
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FChargeFragment& Charge = Charges[i];
			FCombatPairFragment& Pair = Pairs[i];
			const bool bHasIntent = Intents[i].TargetUnit.IsValid() || Intents[i].bAutoEngage != 0;
			const FVector Vel = Velocities[i].Value;
			const float SpeedM = FVector(Vel.X, Vel.Y, 0.f).Size() * 0.01f;

			// -- Run-up: fast movement with combat intent accumulates charge
			// distance; dropping below the threshold cancels (a walk break is
			// not a charge, EC7's fresh-contact gate). The run-up also EXPIRES
			// after the decay window without connecting: a never-connecting
			// fast mover must not hold the unit's charge-through displacement
			// open indefinitely (the 2026-10-27 smoke cascade).
			if (bHasIntent && SpeedM >= Threshold && Charge.bConnected == 0)
			{
				Charge.DistanceTraveled += SpeedM * DT;
				++Charge.ChargeTicks;
				const float ChargeAgeS = static_cast<float>(Charge.ChargeTicks) * DT;
				if (ChargeAgeS > FCombatCVars::ChargeDecayWindow())
				{
					Charge.DistanceTraveled = 0.f;
					Charge.ChargeTicks = 0;
				}
			}
			else if (SpeedM < Threshold)
			{
				Charge.DistanceTraveled = 0.f;
				Charge.ChargeTicks = 0;
			}

			// -- Impact: fresh contact at speed seeds the first pair's bonus
			// (EC7: only fresh contact at speed; the pair existing BEFORE the
			// run-up means the contact is not fresh -- the distance gate
			// covers it, since a standing melee never accumulates).
			if (Pair.Target.IsValid() && Charge.bConnected == 0
				&& Charge.DistanceTraveled > FMeleeMathConstants::MinChargeRunUpM && SpeedM >= Threshold)
			{
				Charge.bConnected = 1;
				Charge.ImpactSpeed = SpeedM;
				Pair.ChargeBonusRemaining = FMeleeMath::BaseChargeBonus();
				if (Debug)
				{
					Debug->RecordChargeConnect();
				}
				UE_LOG(LogMassBattle, Display,
					TEXT("EXEC_RESULT ChargeConnect Entity=%u:%u Target=%u:%u Speed=%.2f Run=%.1fm Bonus=%.1f"),
					Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
					Pair.Target.Index, Pair.Target.SerialNumber, SpeedM, Charge.DistanceTraveled,
					Pair.ChargeBonusRemaining);
			}

			// -- Decay: linear over the window (worker-side, deterministic).
			if (Pair.ChargeBonusRemaining > 0.f)
			{
				Pair.ChargeBonusRemaining = FMath::Max(0.f,
					Pair.ChargeBonusRemaining - FMeleeMath::BaseChargeBonus() * DT / FMath::Max(0.01f, FCombatCVars::ChargeDecayWindow()));
			}
		}
	});
}
