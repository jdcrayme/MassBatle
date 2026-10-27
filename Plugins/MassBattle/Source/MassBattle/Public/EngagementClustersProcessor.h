// EngagementClustersProcessor.h -- arch §4 row 3 (task 5): the 4 Hz record
// machine. Dilates unit OBBs by the engage distance, finds enemy contacts,
// flood-fills clusters, and rebuilds the registry's engagement records
// (mode/frontage/pressure per spec §4.2). Gated by Battle.Combat.Clusters
// (default 0; B.8 item 6). Garrisoned units excluded (A9/F11).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EngagementClustersProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UEngagementClustersProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UEngagementClustersProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** One chunk per unit (shared-formation grouped; the ServoProcessor
	 *  UnitBodyQuery pattern). */
	FMassEntityQuery UnitBodyQuery;
};
