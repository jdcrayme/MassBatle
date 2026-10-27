// AssignPairsProcessor.h -- arch §4 row 5 (task 6): the single game-thread
// assign pass. Bridges fragments/records/orders into the seam's mirrors, runs
// FCombatSolver::RunAssignPass, writes pair fragments (target, angle, state,
// staggered timer, ring point) + order intents. Gated by Battle.Combat.Assign
// (default 0; B.8 item 6).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "AssignPairsProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UAssignPairsProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UAssignPairsProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** Seeker candidates: entities with combat intent fragments (order-stamped
	 *  in this pass's bridge). */
	FMassEntityQuery SeekerQuery;
	/** Candidate defenders: all live, non-garrisoned battle entities. */
	FMassEntityQuery DefenderQuery;
};
