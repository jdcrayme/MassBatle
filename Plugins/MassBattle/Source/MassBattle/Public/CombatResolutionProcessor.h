// CombatResolutionProcessor.h -- arch §4 row 10 (task 7; spec §4.8): the
// worker-side attacker-authoritative resolution pass. Attack timers advance,
// contested rolls land damage through the pair's defender mirror, kills defer
// FDeadTag via the command buffer into the EXISTING Phase 0 death path.
// Gated by Battle.Combat.Resolve (default 1 once landed; B.8 item 6).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CombatResolutionProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UCombatResolutionProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCombatResolutionProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** Paired attackers: timers + rolls + the kill chain. */
	FMassEntityQuery ResolutionQuery;
};
