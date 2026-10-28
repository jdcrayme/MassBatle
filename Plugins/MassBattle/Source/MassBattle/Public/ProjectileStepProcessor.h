// ProjectileStepProcessor.h -- Phase 3 game-thread stepping pass (spec
// ranged-stack §4.9, ADR-28; brief task 3). Thin shim per A1: the cvar gates
// (Enable && Step), the live-count early-out (R-R8), and the StepMs timing
// live here; the integration/TTL/ground-release math is the seam's StepPool.
// FIRST in the "Battle" group (A4) so impact queries read LAST tick's
// spatial hash (F11's rebuild contract). Impact resolution lands in Task 5 --
// this pass's ground/TTL releases are the log-only stub.
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
	/** The step pass works the SoA projectile pool, not entity chunks -- zero
	 *  owned queries, so query-based pruning is meaningless. Declining it also
	 *  keeps the dependency solver's archetype sweep from logging the
	 *  "no registered queries" misconfiguration warning (MassProcessor.cpp's
	 *  documented contract for query-less processors). */
	virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const override;
};
