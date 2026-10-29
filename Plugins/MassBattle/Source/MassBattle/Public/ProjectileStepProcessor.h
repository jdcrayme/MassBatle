// ProjectileStepProcessor.h -- Phase 3 game-thread stepping + impact pass
// (spec ranged-stack §4.9, ADR-28; brief tasks 3+5). Thin shim per A1: the
// cvar gates, the live-count early-out, and the StepMs timing live here; the
// integration is the seam's StepPool and the impact resolution reuses the
// melee machinery (FMeleeMath + the F2 death path) exactly as a melee
// strike. FIRST in the "Battle" group (A4) so impact queries read LAST
// tick's spatial hash (F11's rebuild contract).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "ProjectileStepProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UProjectileStepProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UProjectileStepProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const override;
};

/** One tick of the stepping + impact pass (the processor's whole body,
 *  exported so the exec fixtures and the unit tier drive it without a Mass
 *  tick). Gates inside: Enable && Step. Game thread only. */
MASSBATTLE_API void RunProjectileStepPass(UWorld* World, float DeltaSeconds);
