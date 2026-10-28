// BattleProjectileSubsystem.h -- Phase 3 game-thread home for the pooled
// projectile store (spec ranged-stack §3.3, ADR-27/28). The pool struct is
// world-free seam math; this subsystem owns its lifetime: capacity is
// reserved at battle start ONLY (never mid-volley, RC9) from the
// Battle.Ranged.PoolCapacity cvar, and ResetBattleState clears through
// ResetForBattle. The fire/step PROCESSORS land with the Phase 3 pipeline
// task on the UE machine; this wrapper ships with the data spine so the
// state hash's pool block (spec §3.4) has its read surface from day one.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BattleProjectilePool.h"
#include "BattleProjectileSubsystem.generated.h"

UCLASS()
class MASSBATTLE_API UBattleProjectileSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Reserves the pool at the cvar capacity (battle start / test setup).
	 *  Safe to call repeatedly -- a re-reserve resets everything. */
	void ReserveForBattle();

	/** Battle/test reset: everything released, generations preserved. */
	void ResetForBattle();

	const FBattleProjectilePool& GetPool() const { return Pool; }
	FBattleProjectilePool& GetMutablePool() { return Pool; }

private:
	FBattleProjectilePool Pool;
};
