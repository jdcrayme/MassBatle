// BattleSlotAssigner.h -- locality-aware slot assignment (arch §8.4, gate
// T1.1). Pure functions on the seam, METERS.
//   - Assign: greedy per-file locality fill; entities bid for the nearest
//     vacant slot (deterministic: roster order, ties to the lower slot index).
//     Guarantees a bijection; a k-file displacement bound constrains survivors
//     against treks across the formation.
//   - FindCrossingPairs + ApplyPredictiveSwaps: same-unit pairs whose straight
//     servo projections cross within the horizon swap slot indices (single
//     deterministic pass -- terminates by construction).
//   - FReflowScheduler: batched reflow cadence (§12: ~0.5 s) with the rank-gap
//     immediate trigger -- never per-casualty instant reflow.
#pragma once

#include "CoreMinimal.h"

namespace FSlotAssigner
{
	/** Greedy locality assignment. EntityPositions[i] bids for the nearest
	 *  vacant entry of SlotPositions. PreviousAssignment (may be empty on
	 *  first dress): candidates farther than MaxFileDisplacement files from
	 *  the entity's previous file are skipped unless nothing else is vacant.
	 *  OutAssignment[EntityIdx] = SlotIdx -- a bijection over the slot set. */
	void Assign(const TArray<FVector2D>& EntityPositions, const TArray<FVector2D>& SlotPositions,
		int32 NumFiles, const TArray<int32>& PreviousAssignment, int32 MaxFileDisplacement,
		TArray<int32>& OutAssignment);

	/** Pairs (i, j), i < j, whose constant-velocity projections over
	 *  HorizonSeconds come within ThresholdMeters of each other (covers both
	 *  angled crossings and head-on/parallel convergence, which a pure
	 *  intersection test cannot see). Deterministic order. */
	void FindCrossingPairs(const TArray<FVector2D>& Positions, const TArray<FVector2D>& Velocities,
		const TArray<int32>& Assignment, float HorizonSeconds, float ThresholdMeters, TArray<FIntPoint>& OutPairs);

	/** Swap slot indices for every reported pair (each slot used once per
	 *  pass; pairs processed in the given order -- single pass terminates). */
	void ApplyPredictiveSwaps(TArray<int32>& InOutAssignment, const TArray<FIntPoint>& Pairs);
}

/** Batched reflow cadence (arch §8.4): reflow at most every IntervalSeconds,
 *  except a detected rank gap triggers immediately. Idempotent within an
 *  interval: Advance consumes the decision exactly once. */
struct FReflowScheduler
{
	float IntervalSeconds = 0.5f; // §12 reflow interval
	float Cooldown = 0.f;

	/** True when a reflow may run this tick (cooldown expired or rank gap);
	 *  consuming resets the cooldown. RankGapDetected short-circuits the timer. */
	bool ConsumeIfDue(float DT, bool bRankGapDetected);
};
