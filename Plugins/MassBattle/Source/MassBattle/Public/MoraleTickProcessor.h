// MoraleTickProcessor.h -- the game-thread morale pass (PR-12; spec
// morale-and-gas sections 4.8, 4.10). Group "Battle", after TallyProcessor,
// before RangedFireProcessor. Gated by Battle.Morale.Enable (default 0 until
// the FSM lands end-to-end, ground rule 6; flags-off = byte-identical
// baseline: no GE applied, no pass state, the hash block folds zeros).
//
// Cadence: the per-frame Execute only advances the registry-stored
// accumulator; ALL work runs at the tick boundary (decay/exertion advance by
// the exact TickPeriod -- both are closed-form, so per-frame sampling would
// change nothing but cost). At the tick:
//   1. fold the per-unit input blocks (records + envelope geometry at the
//      cluster cadence's data, never per-entity scans -- spec 4.6),
//   2. under the deterministic pin, force every unit's MoraleTick GE
//      execution (the Task 3 verdict: the pin's specs carry Period =
//      NO_PERIOD, so the forced calls are the ONLY executions),
//   3. mirror + fatigue band + FSM + rally in sorted-handle order
//      (canonical-key rolls, A13), the rout/rally applies through the
//      registry (A7), and the impetuous auto-charge (A11).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "BattleTypes.h"
#include "MoraleTickProcessor.generated.h"

class UUnitRegistrySubsystem;
class UBattleDebugSubsystem;
class AUnitActor;
struct FEngagementKey;

UCLASS()
class MASSBATTLE_API UMoraleTickProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMoraleTickProcessor(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** The scheduler requires >= 1 registered query (5.8 skips zero-query
	 *  processors with 'no registered queries' -- measured on-machine,
	 *  F4-6). The pass folds over registry units + unit actors, not entity
	 *  chunks; this nominal query declares the battle-entity interest and
	 *  is never iterated (Execute never calls ForEachEntityChunk). */
	FMassEntityQuery MoraleNominalQuery;

	/** One unit body mirror for the fold (anchor/facing/team from the
	 *  formation shared fragment + the spawn spec; the FUnitClusterNode
	 *  shape). Member scratch: reset-not-realloc per tick (EC9; the
	 *  processor instance is per-world, game thread only). */
	struct FUnitBody
	{
		FUnitHandle Unit;
		uint8 TeamId = 0;
		FVector2D AnchorM = FVector2D::ZeroVector;
		float YawDeg = 0.f;
		float AnchorZM = 0.f;
		int32 LiveCount = 0;
		bool bValid = false;
	};

	void TickMorale(FMassEntityManager& EntityManager, FMassExecutionContext& Context,
		UUnitRegistrySubsystem* Registry, float TickDt);
	void FoldUnitInputs(FMassEntityManager& EntityManager, UUnitRegistrySubsystem* Registry,
		const FUnitBody& Body, const TArray<FEngagementKey>& SortedRecords);

	TArray<FUnitBody> TickBodies;
	TMap<FUnitHandle, int32> BodyIndexScratch;
	TMap<FUnitHandle, TWeakObjectPtr<AUnitActor>> ActorsScratch;
};
