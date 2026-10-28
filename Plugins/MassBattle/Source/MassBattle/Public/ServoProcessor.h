// ServoProcessor.h -- pipeline row 7 (arch §4): move targets + shared formation
// + movement states -> velocities. Worker-safe, PrePhysics, "Battle" group,
// after FormationSlotResolveProcessor. Implements the §8.3 velocity servo
// (V = clamp((SlotPos - Pos)/Tau, MaxSpeed * CatchupBoost)) with per-state
// avoidance weights per decision A1 -- NEVER engine force accumulation
// (ADR-07). Task 2 ships the no-op skeleton; math lands in Task 8.
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "ServoProcessor.generated.h"

class IConsoleVariable;

UCLASS()
class MASSBATTLE_API UServoProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UServoProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** Servo chunks (one per unit, shared-formation grouped). */
	FMassEntityQuery ServoQuery;
	/** All-battle-entity read-only snapshot feeding the avoidance term. */
	FMassEntityQuery SnapshotQuery;
	/** One chunk per unit (shared-formation grouped, RO): unit-body OBBs. */
	FMassEntityQuery UnitBodyQuery;
	/** Same grouping as UnitBodyQuery, restricted to CAVALRY rosters (audit
	 *  H3: the §8.6 mobility-advantage input to ResolvePolicy). */
	FMassEntityQuery CavalryUnitQuery;
	/** Battle entities only; zeroes velocity when the stage is gated out. */
	FMassEntityQuery ZeroVelocityQuery;
	/** Cached CVAR pointer (audit H4): stable for the process. */
	const IConsoleVariable* WalkSpeedCvar = nullptr;
	/** Cached pointer (B-6, audit M9 pattern): the servo is worker-run; a
	 *  per-Execute FindConsoleVariable (twice) is pure lookup waste. */
	const IConsoleVariable* FallInCvar = nullptr;
};
