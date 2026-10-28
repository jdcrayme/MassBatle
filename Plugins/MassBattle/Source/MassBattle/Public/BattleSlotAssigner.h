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
	MASSBATTLE_API void Assign(const TArray<FVector2D>& EntityPositions, const TArray<FVector2D>& SlotPositions,
		int32 NumFiles, const TArray<int32>& PreviousAssignment, int32 MaxFileDisplacement,
		TArray<int32>& OutAssignment);

	/** Phase 2 C2 (F-1/F-2): crossing-free final assignment by sort matching
	 *  -- the i-th front-most entity takes the i-th front-most slot. Order
	 *  axis = the FINAL facing; tie-breaks: lateral coordinate (file order),
	 *  then roster index (determinism). O(n log n); projection-monotone, so
	 *  no two soldiers' final approaches cross in the facing axis. Surplus
	 *  slots (casualty holes) are the REAR-most ones -- frontage holds, same
	 *  as the reflow semantics. OutSlotOf[EntityIdx] = SlotIdx. */
	MASSBATTLE_API void AssignFinal(const TArray<FVector2D>& EntityPositions, const FVector2D& FinalAnchor,
		float FinalFacingDeg, int32 NumSlots, int32 NumFiles, TArray<int32>& OutSlotOf);

	/** Pairs (i, j), i < j, whose constant-velocity projections over
	 *  HorizonSeconds come within ThresholdMeters of each other (covers both
	 *  angled crossings and head-on/parallel convergence, which a pure
	 *  intersection test cannot see) AND whose swap is a strict 2-opt
	 *  improvement -- each entity is nearer the other's slot than its own.
	 *  The 2-opt guard is what makes swap-then-reswap thrash impossible.
	 *  Deterministic order. */
	MASSBATTLE_API void FindCrossingPairs(const TArray<FVector2D>& Positions, const TArray<FVector2D>& Velocities,
		const TArray<int32>& Assignment, const TArray<FVector2D>& SlotPositions,
		float HorizonSeconds, float ThresholdMeters, TArray<FIntPoint>& OutPairs);

	/** Swap slot indices for every reported pair (each slot used once per
	 *  pass; pairs processed in the given order -- single pass terminates). */
	/** Applies detected pairs with the 2-opt guard RE-CHECKED against the
	 *  current assignment at apply time (audit M3) and each entity used at
	 *  most once per pass. */
	MASSBATTLE_API void ApplyPredictiveSwaps(TArray<int32>& InOutAssignment, const TArray<FIntPoint>& Pairs,
		const TArray<FVector2D>& Positions, const TArray<FVector2D>& SlotPositions);

	/** Formation flow, rule 1 (owner proposal 2026-10-28): directional gap
	 *  closure. Deterministic passes over vacant slots in rank-major,
	 *  file-ascending order (front first, low file first): a vacant slot is
	 *  claimed by its directly-behind occupant (fill front) or, if the file
	 *  is empty behind it, by its directly-right occupant (fill left, toward
	 *  file 0). Both sources sit at HIGHER slot indices, so a vacancy created
	 *  by a fill is visited later in the same pass: rank and file cascades
	 *  resolve within a pass and every entity moves at most one slot per
	 *  pass. A bounded fixpoint (<= 4 passes; a lateral fill arriving after a
	 *  slot's scan can make it behind-fillable) drains every ordinary hole
	 *  in one call. Holes migrate to the rear / high-file drain corner --
	 *  the same corner AssignFinal parks surplus slots in (self-consistent:
	 *  "fixing" the fill direction later would fight the final sort). Pure
	 *  permutation: the greedy bijection survives and INDEX_NONE entries
	 *  pass through. */
	MASSBATTLE_API void CloseGaps(TArray<int32>& InOutAssignment, int32 NumSlots, int32 NumFiles);

	/** Formation flow, rule 2 (owner proposal 2026-10-28): evict-to-reachable.
	 *  Entities flagged stuck (servo-ladder mark; slot physically unreachable)
	 *  claim the slot nearest their BODY regardless of occupancy -- a blocked-out
	 *  entity re-homes at the perimeter instead of orbiting the block. The
	 *  displaced owners shift one slot each along the monotone staircase
	 *  toward the stuck entity's vacated old slot (the sink -- the chain
	 *  terminates there by construction, or earlier at any mid-path vacancy,
	 *  which then migrates to the sink for CloseGaps to drain). A 2-opt-style
	 *  churn guard demands the claimed slot at less than half the old slot's
	 *  squared distance. Roster order, deterministic, pure permutation. */
	MASSBATTLE_API void EvictToReachable(TArray<int32>& InOutAssignment,
		const TArray<FVector2D>& EntityPositions, const TArray<FVector2D>& SlotPositions,
		int32 NumSlots, int32 NumFiles, const TArray<uint8>& StuckFlags);

	/** Formation flow v2 (owner observation 2026-10-28: "the slot is claimed
	 *  even though the entity is nowhere near it"): DISTANCE-triggered
	 *  vacancy diffusion. Rule 1 alone could never fire in a full unit -- the
	 *  greedy bijection leaves no INDEX_NONE slots to close, and the holes the
	 *  owner observes are VIRTUAL (claimed-but-far), invisible to the rank-gap
	 *  trigger and to CloseGaps alike. This pass makes them real in three
	 *  steps:
	 *    1. EVICT: an entity farther than StaleRadiusM from the slot it holds
	 *       does not occupy it in any physical sense -- the claim is dropped
	 *       and the slot opens for the neighbors (SkipFlags exempt entities
	 *       whose motion is owned by a combat ring: their slot distance is
	 *       combat, not staleness).
	 *    2. CASCADE: CloseGaps runs on the opened slots -- the directly-behind
	 *       or directly-right neighbor (whichever is closer to the slot) claims
	 *       it and moves in; the hole walks one step per fill toward the
	 *       rear-right drain corner. The owner's absence has become a REAL
	 *       vacancy and diffuses through the block.
	 *    3. RE-HOME: evicted (and never-assigned) entities walk into the
	 *       remaining holes nearest their BODIES -- in a full unit the cascade
	 *       has dragged the hole to the rear, so a tail-dragged entity is
	 *       re-awarded a REAR slot it can reach instead of orbiting the block
	 *       toward its old front slot. Surplus entities find no hole and stay
	 *       INDEX_NONE.
	 *  Eviction is self-hysteretic: after re-homing the entity is near its new
	 *  slot, so the next pass does not re-evict. Bijection-preserving (each
	 *  eviction opens exactly one hole; the cascade only permutes), roster
	 *  order, squared distances, lowest slot index on ties -- deterministic. */
	MASSBATTLE_API void DiffuseVacancies(TArray<int32>& InOutAssignment,
		const TArray<FVector2D>& EntityPositions, const TArray<FVector2D>& SlotPositions,
		int32 NumSlots, int32 NumFiles, float StaleRadiusM, const TArray<uint8>& SkipFlags);
}

/** Batched reflow cadence (arch §8.4): reflow at most every IntervalSeconds,
 *  except a detected rank gap triggers immediately. Idempotent within an
 *  interval: Advance consumes the decision exactly once. */
struct MASSBATTLE_API FReflowScheduler
{
	float IntervalSeconds = 0.5f; // §12 reflow interval
	float Cooldown = 0.f;

	/** True when a reflow may run this tick (cooldown expired or rank gap);
	 *  consuming resets the cooldown. RankGapDetected short-circuits the timer. */
	bool ConsumeIfDue(float DT, bool bRankGapDetected);
};
