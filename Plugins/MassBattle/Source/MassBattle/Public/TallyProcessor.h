// TallyProcessor.h -- arch §4 row 4 (task 6): per-tick per-defender tally +
// coverage stamping. Everything derived fresh each tick -- no state to desync
// (arch §7.2). Gated by Battle.Combat.Tally (default 0; B.8 item 6).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "TallyProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UTallyProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UTallyProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** Live pairs: count attackers per defender into the registry's tally
	 *  scratch (open-address, reset-not-realloc, EC9). */
	FMassEntityQuery PairQuery;
	/** All live battle entities: stamp coverage + covered-sector mask for
	 *  defenders with at least one inbound pair (spec §4.3's perf scope cut;
	 *  circle queries against the spatial hash -- a permitted use). */
	FMassEntityQuery CoverageQuery;
};
