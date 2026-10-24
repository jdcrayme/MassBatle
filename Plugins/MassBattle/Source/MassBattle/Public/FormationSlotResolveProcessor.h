// FormationSlotResolveProcessor.h -- pipeline row 6 (arch §4): slot assignment
// + shared formation -> per-entity move targets. Worker-safe (no subsystem
// access; fragment data only), PrePhysics, "Battle" group, after
// UnitLocomotionProcessor. Consumes the slot assignment (Task 7) and writes
// FSlotTargetFragment via the path-frame layout (Task 4).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "FormationSlotResolveProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UFormationSlotResolveProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UFormationSlotResolveProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery ResolveQuery;
};
