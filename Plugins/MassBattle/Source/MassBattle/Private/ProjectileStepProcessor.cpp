// ProjectileStepProcessor.cpp -- the stepping shim (brief task 3).
#include "ProjectileStepProcessor.h"
#include "BattleBallistics.h"
#include "BattleCostGridSubsystem.h"
#include "BattleDebugSubsystem.h"
#include "BattleProjectileSubsystem.h"
#include "BattleRanged.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"

UProjectileStepProcessor::UProjectileStepProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	// FIRST in the group (A4): before UnitSync, so the pass reads end-of-last-
	// tick transforms in the still-current spatial hash (F11's contract).
	ExecutionOrder.ExecuteBefore.Add(TEXT("UnitSyncProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // pool + subsystems are game-thread (ADR-28)
}

void UProjectileStepProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UBattleProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	if (!Projectiles || !FRangedCVars::Enable() || !FRangedCVars::Step())
	{
		return; // flags-off identity: the pool stays untouched (byte-identical baseline)
	}
	FBattleProjectilePool& Pool = Projectiles->GetMutablePool();
	if (Pool.GetLiveCount() == 0)
	{
		return; // R-R8: nothing in flight -- the pass costs two cvar reads
	}

	const double StartTime = FPlatformTime::Seconds();
	UBattleCostGridSubsystem* Grids = World ? World->GetSubsystem<UBattleCostGridSubsystem>() : nullptr;
	const FBallisticsMath::FPoolStepCounts Counts = FBallisticsMath::StepPool(
		Pool, Grids ? &Grids->GetGrid() : nullptr, Context.GetDeltaTimeSeconds());
	const double StepMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// Impact STUB (log-only): v1 a "hit" is a ground/TTL release with no
	// resolution; Task 5 swaps this for the transient melee-machinery roll.
	if (UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr)
	{
		Debug->RecordProjectileStep(StepMs, Counts.GroundHits, Counts.TtlExpired);
	}
	UE_LOG(LogMassBattle, Verbose,
		TEXT("ProjectileStep InFlight=%d GroundHits=%d TtlExpired=%d StepMs=%.3f"),
		Pool.GetLiveCount(), Counts.GroundHits, Counts.TtlExpired, StepMs);
}

bool UProjectileStepProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false; // no owned queries -- nothing to prune (see header)
}
