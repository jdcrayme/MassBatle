// UnitLocomotionProcessor.h -- pipeline row 1 (arch §4): registry + cost grid
// -> anchor/facing/speed-limit/path into the shared formation fragment.
// Game thread (registry + planner state are game-thread), PrePhysics, custom
// "Battle" group, after UnitSyncProcessor. The unit is the locomotor (§8.1):
// entities never decide speed or heading. Task 2 ships the no-op skeleton;
// the §8.2 math lands in Task 5 (T1.3).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "UnitLocomotionProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UUnitLocomotionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UUnitLocomotionProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery LocomotionQuery;
	/** Unit bodies for the §8.2 interpenetration facing lock. */
	FMassEntityQuery UnitBodyQuery;
};
