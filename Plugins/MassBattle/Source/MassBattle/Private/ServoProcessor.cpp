#include "ServoProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementServo(
		TEXT("Battle.Movement.Servo"),
		1,
		TEXT("Enable Servo processing (arch §4 row 7). 0 opts the stage out."),
		ECVF_Default);
}

UServoProcessor::UServoProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ServoQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("FormationSlotResolveProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 7).
	bRequiresGameThreadExecution = false;
}

void UServoProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ServoQuery.AddRequirement<FSlotTargetFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddRequirement<FServoStateFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
}

void UServoProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine Servo first-execute"));
	}

	if (GBattleMovementServo.GetValueOnAnyThread() == 0)
	{
		return;
	}

	// Task 8 lands the §8.3 servo + A1 per-state avoidance weights + §8.5
	// unstuck ladder here.
	ServoQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
	});
}
