// OrderLatencyReleaseProcessor.h -- Task 4b.1: releases due queued player
// orders on tick boundaries (B-4: sorted-handle order, through the registry's
// existing issue functions). The queue itself is registry state (B-3); this
// processor only advances the sim tick + sweeps. The F4-6 lesson: the 5.8
// scheduler skips zero-query processors, so the nominal-query pattern (the
// morale pass's) keeps it in the pipeline.
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "OrderLatencyReleaseProcessor.generated.h"

UCLASS()
class UOrderLatencyReleaseProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UOrderLatencyReleaseProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	// Nominal (the F4-6 pattern): the sweep is registry-driven -- the query
	// only keeps the processor in the pipeline.
	FMassEntityQuery ReleaseQuery;
};
