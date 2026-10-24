// BattleDeathPath.h -- the up-path (arch §5.2, §4 row 11).
//
// Final shape after the ADR-15 on-machine probe (Phase 0 Task 9, audit
// C10/C11): FDeadTag is a REGULAR tag (5.8 sparse tags are chunk-granular and
// observer-incompatible -- see BattleFragments.h), so the death notification
// is a true observer that fires DURING the kill flush (the E5 audited point):
//
//   kill: Defer(AddTag<FDeadTag>) + flush
//   -> UBattleDeathObserver (observes FDeadTag AddElement at flush): defers
//      the death signal
//   -> UBattleDeathSignalReceiver (game thread, signal-driven): roster update
//      + GAS event -> Defer(DestroyEntity) AFTER the signal (corpse policy is
//      Phase 5; the receiver runs while the entity is still alive).
#pragma once

#include "CoreMinimal.h"
#include "MassObserverProcessor.h"
#include "MassSignalProcessorBase.h"
#include "BattleDeathPath.generated.h"

namespace UE::Mass::Signals
{
	// Project signal name (kept next to its producer/consumer pair).
	inline const FName& BattleUnitDeath()
	{
		static const FName Name(TEXT("Battle.UnitDeath"));
		return Name;
	}
}

/** Pipeline row 11 (death): observes FDeadTag (regular tag) AddElement at the
 *  kill flush, defers the death signal. The destroy is deferred by the
 *  receiver so the entity is still alive when the signal is consumed. */
UCLASS()
class MASSBATTLE_API UBattleDeathObserver : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UBattleDeathObserver(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void Register() override;
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** 5.8 observer pattern: the notification carries no entities -- Execute
	 *  runs this query (all currently FDeadTag'd entities) at flush time. */
	FMassEntityQuery EntitySignalQuery;
};

/** Receives "Battle.UnitDeath" (game thread) while the entity is still alive,
 *  updates the registry roster, raises "Battle.Unit.Died" on the unit actor's
 *  ability system, then defers the destroy. */
UCLASS()
class MASSBATTLE_API UBattleDeathSignalReceiver : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UBattleDeathSignalReceiver(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals) override;
};
