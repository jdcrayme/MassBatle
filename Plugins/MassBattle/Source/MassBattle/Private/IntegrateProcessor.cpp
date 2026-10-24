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

	// Transform integration modeled on UMassSimpleMovementProcessor
	// (engine-api §14): position += velocity * DT. Planar sim: Z pinned to 0;
	// entity yaw follows the velocity heading while moving (per-rank facing
	// emerges from the slot targets as the formation re-dresses).
	const float DT = Context.GetDeltaTimeSeconds();
	IntegrateQuery.ForEachEntityChunk(Context, [DT](FMassExecutionContext& Context)
	{
		auto Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FTransform& Transform = Transforms[i].GetMutableTransform();
			const FVector Vel = Velocities[i].Value;
			Transform.SetTranslation(Transform.GetTranslation() + Vel * DT);
			Transform.SetTranslation(FVector(Transform.GetTranslation().X, Transform.GetTranslation().Y, 0.f));
			if (FMath::Abs(Vel.X) + FMath::Abs(Vel.Y) > KINDA_SMALL_NUMBER)
			{
				const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Vel.Y, Vel.X));
				Transform.SetRotation(FQuat(FRotator(0.f, YawDeg, 0.f)));
			}
		}
	});
}
