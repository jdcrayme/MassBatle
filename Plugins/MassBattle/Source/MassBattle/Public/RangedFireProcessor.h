// RangedFireProcessor.h -- Phase 3 game-thread fire pass (spec ranged-stack
// §4.4-§4.8, ADR-28; brief task 4). Thin shim per A1: eligibility/volley
// math is the seam's (SelectVolleyShots); the skirmish DECISION is the
// seam's (EvaluateSkirmish); this pass gathers shooter views in roster
// order, resolves the intent/record target (A11), gathers the friendly
// blockers through the spatial hash (A7's sanctioned circle queries),
// writes the pool + ammo/reload writeback, flips the out-of-ammo aggregate
// (A14), and drives the auto-retreat through the registry (RC7 serial
// guard). After AssignPairs, before FormationSlotResolve (A4).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RangedFireProcessor.generated.h"

UCLASS()
class MASSBATTLE_API URangedFireProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URangedFireProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	virtual bool ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const override;
};

/** One tick of the ranged fire pass. The cvar gates (Enable && Fire &&
 *  Skirmish) are applied INSIDE (the exec fixtures drive the exported
 *  entry without a processor tick); DeltaSeconds advances the reload
 *  timers. Game thread only (five subsystem consumers, ADR-28). */
MASSBATTLE_API void RunRangedFirePass(UWorld* World, float DeltaSeconds);
