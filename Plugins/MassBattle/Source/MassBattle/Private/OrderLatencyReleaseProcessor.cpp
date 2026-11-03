// OrderLatencyReleaseProcessor.cpp -- Task 4b.1: the tick-boundary release
// sweep (see the header).
#include "OrderLatencyReleaseProcessor.h"
#include "BattleRegistry.h"
#include "BattleFragments.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"

UOrderLatencyReleaseProcessor::UOrderLatencyReleaseProcessor()
	: ReleaseQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	// Release BEFORE the locomotion consumes orders (the released order must
	// be visible to this frame's planner) and after the sync pass.
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitSyncProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("UnitLocomotionProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry writes (the issue functions)
}

void UOrderLatencyReleaseProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ReleaseQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	ReleaseQuery.RegisterWithProcessor(*this);
}

void UOrderLatencyReleaseProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UUnitRegistrySubsystem* Registry = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	// Advance FIRST (the release ticks are absolute against the counter),
	// then sweep. Both run every tick even on an empty world (the queue may
	// hold orders for units whose entities are mid-spawn; the counter must
	// not stall -- F10's reset-ordering immunity relies on the absolute
	// schedule).
	Registry->AdvanceSimTick();
	Registry->ReleaseQueuedOrders();
}
