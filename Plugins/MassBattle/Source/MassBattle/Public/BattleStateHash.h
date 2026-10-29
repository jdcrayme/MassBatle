// BattleStateHash.h -- deterministic sim-state hash (roadmap B.5 hooks,
// T0.6). Pure function: no wall clock, no frame counters, no non-sim state
// (dirty flags are pipeline scheduling state and are excluded -- audit M4).
// Folds: registry bookkeeping (sorted handles, serials, roster sizes) +
// per-entity quantized state (HP at 1e-2, FDeadTag presence, transform at
// 1 cm / yaw at 0.1 deg -- phase-1 ground rule 7) + archetype count + per-unit
// shared-fragment values (stats quantized at 1e-2; formation anchor at 1 cm).
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"

struct MASSBATTLE_API FBattleStateHash
{
	/** Full sim-state hash of the world's battle data (CityHash64). When
	 *  OutSectionHashes is provided it receives one CityHash64 per SECTION
	 *  (bookkeeping, entity state, archetype count, shared/formation/combat
	 *  state, pool) -- the RC8 divergence bisector. */
	static uint64 Compute(UWorld* World, TArray<uint64>* OutSectionHashes = nullptr);

	/** Quantize helper: value at 1e-2 precision, as int64 (stable across runs). */
	static int64 Quantize2(float Value);
};
