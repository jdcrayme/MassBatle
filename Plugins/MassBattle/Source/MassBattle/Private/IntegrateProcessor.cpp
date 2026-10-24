#include "IntegrateProcessor.h"
#include "BattleMovementFragments.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementIntegrate(
		TEXT("Battle.Movement.Integrate"),
		1,
		TEXT("Enable Integrate processing (arch §4 row 8). 0 opts the stage out."),
		ECVF_Default);
}

UIntegrateProcessor::UIntegrateProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, IntegrateQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("ServoProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 8).
	bRequiresGameThreadExecution = false;
}

void UIntegrateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	IntegrateQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UIntegrateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine Integrate first-execute"));
	}

	if (GBattleMovementIntegrate.GetValueOnAnyThread() == 0)
	{
		return;
	}

	// Task 8 lands transform integration here (simple velocity * DT model).
	IntegrateQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
	});
}
