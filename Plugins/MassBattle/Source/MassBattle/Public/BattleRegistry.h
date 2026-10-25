// BattleRegistry.h -- UUnitRegistrySubsystem (arch §2.3, §2.4).
// The translation layer: unit actors and Mass entities communicate ONLY through
// this subsystem. Phase 0 is game-thread-write only; Phase 1 adds scoped read
// access for worker processors (UE_MT_SCOPED_READ_ACCESS(AccessDetector)) --
// the detector and the external-subsystem traits are baked in now so the
// discipline exists before the first worker reads it.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Misc/MTAccessDetector.h"
#include "BattleTypes.h"
#include "BattleRegistry.generated.h"

/**
 * Handle registry + roster bookkeeping + dirty-flag fan-out.
 *
 * Handle discipline (T0.1): Index is slot-based (free-list reuse); Serial is a
 * per-slot generation counter that increments on every (re)allocation, so a
 * stale handle (old Serial) is rejected. Handles are value types; Serial 0 is
 * invalid.
 *
 * Dirty flags: MarkStatsDirty is idempotent per frame (set semantics); the
 * UnitSync processor reads-and-clears them (Task 8).
 */
UCLASS()
class MASSBATTLE_API UUnitRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- USubsystem --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -- unit lifecycle (game thread) --

	/** Allocates a unit slot. Returns an invalid handle if the spec is bad. */
	FUnitHandle RegisterUnit(const FUnitSpawnSpec& Spec);

	/** Frees the unit's slot (bumps generation). Rejects stale handles. */
	bool UnregisterUnit(FUnitHandle Handle);

	bool IsHandleValid(FUnitHandle Handle) const;

	/** Spec the unit was registered with (spawn path re-reads it on demand). */
	const FUnitSpawnSpec* FindUnitSpec(FUnitHandle Handle) const;

	// -- roster bookkeeping --

	/** Replaces the unit's roster (spawn fixup publishes once). */
	void SetRoster(FUnitHandle Handle, TArray<FMassEntityHandle>&& Roster);

	/** Entity->unit side reads this to find the roster. Null when stale. */
	const TArray<FMassEntityHandle>* FindRoster(FUnitHandle Handle) const;

	/** Death-path bookkeeping (arch §5.3): remove one entity from the unit's
	 *  roster. Called by the death-signal receiver on the game thread. */
	void HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex);


	/** Current roster size (0 when stale). */
	int32 GetRosterSize(FUnitHandle Handle) const;

	// -- attribute snapshot & dirty flags (arch §5.1) --

	void UpdateCachedAttributes(FUnitHandle Handle, const FBattleAttributeSnapshot& Snapshot);
	const FBattleAttributeSnapshot* FindCachedAttributes(FUnitHandle Handle) const;

	/** Idempotent: marking twice in one frame leaves one dirty entry. */
	void MarkStatsDirty(FUnitHandle Handle);
	void ClearStatsDirty(FUnitHandle Handle);
	bool IsStatsDirty(FUnitHandle Handle) const;

	// -- A2 thin order API (decision A2; game thread) --

	/** Move to the anchor (cm) with final facing (deg); speed m/s, 0 = walk. */
	bool IssueMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg, float CommandedSpeedMps = 0.f);
	/** Halt: clears the path intent; the formation holds where it stands. */
	bool IssueStopOrder(FUnitHandle Handle);
	/** Current order + its serial (locomotion change detector). */
	const FUnitOrder* FindOrder(FUnitHandle Handle) const;
	uint32 GetOrderSerial(FUnitHandle Handle) const;

	// -- introspection (debug overlay / state hash) --

	int32 GetUnitCount() const;
	const FUnitRuntimeState* FindUnitState(FUnitHandle Handle) const;

	/** Gather live handles in a deterministic order (sorted by Index). */
	TArray<FUnitHandle> GetSortedHandles() const;

	/** Stable fold of registry bookkeeping (serials + roster sizes + free-list
	 *  depth; dirty flags excluded as non-sim state, audit M4). The full
	 *  sim-state hash is FBattleStateHash (Task 10), which folds this in. */
	uint32 ComputeBookkeepingHash() const;

	/** Number of free slots held for reuse (leak diagnostics). */
	int32 GetFreeListSize() const { return FreeIndices.Num(); }

private:
	// Phase 1+ worker reads: UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	UE_MT_DECLARE_RW_ACCESS_DETECTOR(AccessDetector);

	/** Live units only; freed slots move to FreeIndices. */
	TMap<FUnitHandle, FUnitRuntimeState> Units;

	/** Specs by index (registry-side copy of what the unit was spawned from). */
	TMap<uint32, FUnitSpawnSpec> SpecsByIndex;

	/** LIFO free-slot reuse (T0.1: reuse is safe because Serial++ on free). */
	TArray<uint32> FreeIndices;

	/** Per-slot generation counter; persists across free/reuse so stale
	 *  handles (old Serial) never alias a new tenant. */
	TMap<uint32, int32> NextGenerationByIndex;


	/** Next never-used slot index (0 reserved for the null handle). */
	uint32 NextIndex = 1;

	/** Units whose attribute snapshot changed and UnitSync has not consumed. */
	TSet<FUnitHandle> DirtyStats;
};
