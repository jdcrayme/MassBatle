// IntegrateProcessor.h -- pipeline row 8 (arch §4): velocities -> transforms.
// Worker-safe, PrePhysics, "Battle" group, after ServoProcessor. Own project
// integration modeled on UMassSimpleMovementProcessor (engine-api §14) -- no
// engine movement processors on battle entities (phase-1 ground rule 3 / A1).
// Task 2 ships the no-op skeleton; integration lands in Task 8.
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "IntegrateProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UIntegrateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UIntegrateProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery IntegrateQuery;
};
