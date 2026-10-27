// ChargeProcessor.h -- arch §4 row 9 melee half (task 7; spec §4.9): the
// worker-side charge state machine. Run-up tracking, fresh-contact impact
// detection seeding ChargeBonusRemaining, linear decay, and the unit-level
// charge flag the servo's displacement blend consumes for knockback.
// (RangedFire, the other half of row 9, is Phase 3.)
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "ChargeProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UChargeProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UChargeProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery ChargeQuery;
};
