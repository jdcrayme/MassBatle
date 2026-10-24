#include "UnitLocomotionProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// B.8 item 6: feature flag so the half-built locomotion stage opts out of
	// whatever perf/soak baselines measure while it is in flight.
	TAutoConsoleVariable<int32> GBattleMovementLocomotion(
		TEXT("Battle.Movement.Locomotion"),
		1,
		TEXT("Enable UnitLocomotion processing (arch §4 row 1). 0 opts the stage out."),
		ECVF_Default);
}

UUnitLocomotionProcessor::UUnitLocomotionProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LocomotionQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitSyncProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry + planner state are game-thread (arch §4 row 1)
}

void UUnitLocomotionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	LocomotionQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadWrite);
	LocomotionQuery.AddSubsystemRequirement<UUnitRegistrySubsystem>(EMassFragmentAccess::ReadOnly);
}

void UUnitLocomotionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// One-time pipeline-ordering trace (phase-1 Task 2 verify; Display so the
	// func-tier boot log carries it without log-level changes -- not hot path).
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine UnitLocomotion first-execute"));
	}

	if (GBattleMovementLocomotion.GetValueOnGameThread() == 0)
	{
		return;
	}

	// Task 5 lands the §8.2 math here (Min3 speed limit, two-step wheel cap,
	// anchor progression along the planner path).
	LocomotionQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
	});
}
