#include "FormationSlotResolveProcessor.h"
#include "BattleMovementFragments.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementSlotResolve(
		TEXT("Battle.Movement.SlotResolve"),
		1,
		TEXT("Enable FormationSlotResolve processing (arch §4 row 6). 0 opts the stage out."),
		ECVF_Default);
}

UFormationSlotResolveProcessor::UFormationSlotResolveProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ResolveQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitLocomotionProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 6).
	bRequiresGameThreadExecution = false;
}

void UFormationSlotResolveProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ResolveQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	ResolveQuery.AddRequirement<FSlotAssignmentFragment>(EMassFragmentAccess::ReadOnly);
	ResolveQuery.AddRequirement<FSlotTargetFragment>(EMassFragmentAccess::ReadWrite);
}

void UFormationSlotResolveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine FormationSlotResolve first-execute"));
	}

	if (GBattleMovementSlotResolve.GetValueOnAnyThread() == 0)
	{
		return;
	}

	// Task 7 wires this to the slot assignment + path-frame layout math
	// (FPathFrameLayout::Project, Task 4).
	ResolveQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
	});
}
