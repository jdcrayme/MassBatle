// BattleStateHash.h -- deterministic sim-state hash (roadmap B.5 hooks,
// T0.6). Pure function: no wall clock, no frame counters, no non-sim state
// (dirty flags are pipeline scheduling state and are excluded -- audit M4;
// the session-cumulative archetype count was retired on the same grounds --
// task 1.S3-c: process history is not sim state, measured 2026-11-02).
// Folds: registry bookkeeping (sorted handles, serials, roster sizes) +
// per-entity quantized state (HP at 1e-2, FDeadTag presence, transform at
// 1 cm / yaw at 0.1 deg -- phase-1 ground rule 7) + the live composition
// census (per-entity presence bitset over kCensusTypes) + per-unit
// shared-fragment values (stats quantized at 1e-2; formation anchor at 1 cm).
#pragma once

#include "CoreMinimal.h"
#include "BattleTypes.h"

struct MASSBATTLE_API FBattleStateHash
{
	/** Full sim-state hash of the world's battle data (CityHash64). When
	 *  OutSectionHashes is provided it receives one CityHash64 per SECTION,
	 *  each hashing that section's OWN bytes -- never the cumulative prefix
	 *  (task 1.S3-b: a prefix fold makes every later section hash cascade
	 *  from the first diverging one, so no later section carries independent
	 *  signal). Sections 1..10: 1 bookkeeping, 2 transforms+HP+dead,
	 *  3 composition census, 4 shared stats, 5 formation, 6 garrison,
	 *  7 combat pairs/charge/mount, 8 ranged, 9 projectile pool,
	 *  10 morale -- the RC8 divergence bisector. */
	static uint64 Compute(UWorld* World, TArray<uint64>* OutSectionHashes = nullptr);

	/** Quantize helper: value at 1e-2 precision, as int64 (stable across runs). */
	static int64 Quantize2(float Value);
};
