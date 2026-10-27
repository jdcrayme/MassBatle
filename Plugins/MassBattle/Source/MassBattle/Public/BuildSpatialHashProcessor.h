// BuildSpatialHashProcessor.h -- arch §4 row 2 (task 5): game-thread rebuild
// of the spatial hash from end-of-last-tick transforms, before the cluster
// pass. Gated by Battle.Combat.SpatialHash (default 0 = flags-off baseline
// untouched; B.8 item 6).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "BuildSpatialHashProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UBuildSpatialHashProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UBuildSpatialHashProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** All live battle entities: transform + owner + loadout (reach class);
	 *  garrisoned units excluded (A9/F11: not combat participants this phase). */
	FMassEntityQuery HashQuery;
};
