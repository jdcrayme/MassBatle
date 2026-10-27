// BattleRegistry.h -- UUnitRegistrySubsystem (arch §2.3, §2.4).
// The translation layer: unit actors and Mass entities communicate ONLY through
// this subsystem. Phase 0 is game-thread-write only; Phase 1 adds scoped read
// access for worker processors (UE_MT_SCOPED_READ_ACCESS(AccessDetector)) --
// the detector and the external-subsystem traits are baked in now so the
// discipline exists before the first worker reads it.
// Phase 2 (task 2, spec §3.2): registry-owned engagement records + preallocated
// combat scratch, the attack order surface (A4), and the F2 pair-cleanup hook
// on HandleUnitDeath. Game-thread written; worker passes read through the
// access detector (assign pass, task 6).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Misc/MTAccessDetector.h"
#include "BattleTypes.h"
#include "BattleRegistry.generated.h"

struct FBattleSaveData;

/** Engagement-record mode (spec §3.2, arch §6.3). Flank keeps the frontal
 *  pressure formula; Mob uses the A14-floored mob formula. */
enum class EEngagementMode : uint8
{
	Frontal = 0,
	Flank = 1,
	Mob = 2
};

/** Registry-owned per-(attacker unit, defender unit) engagement record (spec
 *  §3.2). Recomputed at 4 Hz by the cluster pass (task 5); the assign pass
 *  (task 6) reads it. Plain struct -- Iris-ready (B.8 item 7). */
struct FEngagementRecord
{
	EEngagementMode Mode = EEngagementMode::Frontal;
	/** Segment parameters of the defender line assigned to the attacker
	 *  (meters, seam convention; flanks wrap the end). */
	float FrontageStart = 0.f;
	float FrontageEnd = 0.f;
	/** Frontal = DefenderAlive x FormedSlots; Mob = Min(AttackerAlive,
	 *  Max(DefenderAlive x MobPressureFactor, MaxMobSlots)) -- the A14 floor. */
	int32 DesiredPressure = 0;
	/** Tallied fresh each tick by the tally pass; never persisted across
	 *  ticks (arch §7.2: no state to desync). */
	int32 AssignedAttackers = 0;
	/** 1 = released (disengage / mass release); pruned at the cluster pass. */
	uint8 bDead = 0;
};

/** (attacker unit, defender unit) record key. Ordered: the record is stored
 *  once, from the attacker's perspective (the solver looks it up that way). */
struct FEngagementKey
{
	FUnitHandle Attacker;
	FUnitHandle Defender;

	bool operator==(const FEngagementKey& Other) const
	{
		return Attacker == Other.Attacker && Defender == Other.Defender;
	}
};

inline uint32 GetTypeHash(const FEngagementKey& Key)
{
	return HashCombine(::GetTypeHash(Key.Attacker), ::GetTypeHash(Key.Defender));
}

/**
 * Open-address tally table keyed by defender entity handle (spec §3.2 scratch;
 * EC9: preallocated per battle, Reset() zeroes without deallocating, counts
 * are derived fresh each tick). Power-of-two capacity, linear probing.
 */
struct FCombatTallyTable
{
	void Initialize(int32 InCapacity)
	{
		const int32 Pow2 = FMath::RoundUpToPowerOfTwo(FMath::Max(16, InCapacity));
		Keys.SetNumUninitialized(Pow2);
		Counts.Init(0, Pow2);
		Reset();
	}

	/** Clears all entries: invalid keys + zero counts. Keeps the allocation. */
	void Reset()
	{
		for (int32 i = 0; i < Keys.Num(); ++i)
		{
			Keys[i] = FMassEntityHandle();
			Counts[i] = 0;
		}
	}

	/** Index of Key's slot, inserting if absent. Returns INDEX_NONE when full
	 *  (capacity is a per-battle constant; the tally pass treats that as an
	 *  internal error and drops the increment -- never grows per tick). */
	int32 FindOrAdd(FMassEntityHandle Key)
	{
		if (!Key.IsValid() || Keys.Num() == 0)
		{
			return INDEX_NONE;
		}
		const uint32 Hash = GetTypeHash(Key);
		uint32 Slot = Hash & static_cast<uint32>(Keys.Num() - 1);
		for (int32 Probe = 0; Probe < Keys.Num(); ++Probe)
		{
			if (!Keys[Slot].IsValid())
			{
				Keys[Slot] = Key;
				Counts[Slot] = 0;
				return static_cast<int32>(Slot);
			}
			if (Keys[Slot] == Key)
			{
				return static_cast<int32>(Slot);
			}
			Slot = (Slot + 1) & static_cast<uint32>(Keys.Num() - 1);
		}
		return INDEX_NONE;
	}

	/** Index of Key's slot WITHOUT inserting; INDEX_NONE when absent. */
	int32 Find(FMassEntityHandle Key) const
	{
		if (!Key.IsValid() || Keys.Num() == 0)
		{
			return INDEX_NONE;
		}
		uint32 Slot = GetTypeHash(Key) & static_cast<uint32>(Keys.Num() - 1);
		for (int32 Probe = 0; Probe < Keys.Num(); ++Probe)
		{
			if (!Keys[Slot].IsValid())
			{
				return INDEX_NONE;
			}
			if (Keys[Slot] == Key)
			{
				return static_cast<int32>(Slot);
			}
			Slot = (Slot + 1) & static_cast<uint32>(Keys.Num() - 1);
		}
		return INDEX_NONE;
	}

	int32 GetCount(FMassEntityHandle Key) const
	{
		const int32 Idx = Find(Key);
		return Idx != INDEX_NONE ? Counts[Idx] : 0;
	}

	TArray<FMassEntityHandle> Keys;
	TArray<int32> Counts;
};

/**
 * Per-defender coverage cache with a valid-tick stamp (spec §3.2 scratch).
 * Same open-address shape; entries older than the current tick are stale and
 * recompute on read (the assign pass stamps via Store).
 */
struct FCombatCoverageTable
{
	void Initialize(int32 InCapacity)
	{
		const int32 Pow2 = FMath::RoundUpToPowerOfTwo(FMath::Max(16, InCapacity));
		Keys.SetNumUninitialized(Pow2);
		Values.Init(0.f, Pow2);
		Stamps.Init(0u, Pow2);
		Masks.Init(0u, Pow2);
		Reset();
	}

	void Reset()
	{
		for (int32 i = 0; i < Keys.Num(); ++i)
		{
			Keys[i] = FMassEntityHandle();
			Values[i] = 0.f;
			Stamps[i] = 0u;
			Masks[i] = 0u;
		}
	}

	/** Insert-or-update; returns the slot index. */
	int32 Store(FMassEntityHandle Key, float Value, uint8 CoveredMask, uint32 Tick)
	{
		if (!Key.IsValid() || Keys.Num() == 0)
		{
			return INDEX_NONE;
		}
		uint32 Slot = GetTypeHash(Key) & static_cast<uint32>(Keys.Num() - 1);
		for (int32 Probe = 0; Probe < Keys.Num(); ++Probe)
		{
			if (!Keys[Slot].IsValid() || Keys[Slot] == Key)
			{
				Keys[Slot] = Key;
				Values[Slot] = Value;
				Masks[Slot] = CoveredMask;
				Stamps[Slot] = Tick;
				return static_cast<int32>(Slot);
			}
			Slot = (Slot + 1) & static_cast<uint32>(Keys.Num() - 1);
		}
		return INDEX_NONE;
	}

	/** Covered-sector mask at a slot (valid only after a fresh Find). */
	uint8 GetMask(int32 Slot) const { return Masks.IsValidIndex(Slot) ? Masks[Slot] : 0u; }

	/** Coverage for Key if stamped at Tick; INDEX_NONE when absent/stale. */
	int32 Find(FMassEntityHandle Key, uint32 Tick) const
	{
		if (!Key.IsValid() || Keys.Num() == 0)
		{
			return INDEX_NONE;
		}
		uint32 Slot = GetTypeHash(Key) & static_cast<uint32>(Keys.Num() - 1);
		for (int32 Probe = 0; Probe < Keys.Num(); ++Probe)
		{
			if (!Keys[Slot].IsValid())
			{
				return INDEX_NONE;
			}
			if (Keys[Slot] == Key)
			{
				return Stamps[Slot] == Tick ? static_cast<int32>(Slot) : INDEX_NONE;
			}
			Slot = (Slot + 1) & static_cast<uint32>(Keys.Num() - 1);
		}
		return INDEX_NONE;
	}

	TArray<FMassEntityHandle> Keys;
	TArray<float> Values;
	TArray<uint32> Stamps;
	/** Covered-sector mask (same-unit living neighbors; bits +X/-X/+Y/-Y
	 *  quadrants are the cluster pass's; THIS mask is SectorCount buckets,
	 *  bit k = sector k occupied) -- stamped by the tally pass with Value. */
	TArray<uint8> Masks;
};

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
	 *  roster. Called by the death-signal receiver on the game thread.
	 *  Phase 2 (F2): also the single pair-cleanup hook -- drops the dead
	 *  entity's scratch entries (tally/coverage) so the next fresh-tally pass
	 *  never counts a corpse. */
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

	/** Move to the anchor (cm) with final facing (deg); speed m/s, 0 = walk.
	 *  CommandedFiles > 0 re-fronts the unit to that many files on the next
	 *  plan (Total War-style width control); 0 keeps the current layout. */
	bool IssueMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg,
		float CommandedSpeedMps = 0.f, int32 CommandedFiles = 0);
	/** Halt: clears the path intent; the formation holds where it stands. */
	bool IssueStopOrder(FUnitHandle Handle);
	// -- Phase garrison order surface (spec §4.1, task 4; game thread) --

	/** Garrison into a structure. Infantry-only at issue (A9 guard clause:
	 *  FCavalryTag roster -> reject + EXEC_RESULT line). */
	bool IssueGarrisonOrder(FUnitHandle Handle, uint32 StructureId);
	/** Begin egress (FSM consumes; no-op at the FSM when not garrisoned). */
	bool IssueExitGarrisonOrder(FUnitHandle Handle);
	/** A7 parking: a Move / re-Garrison received while garrisoned waits here
	 *  and resumes at the egress assembly point. */
	void ParkPendingAfterEgress(FUnitHandle Handle, const FUnitOrder& Order);
	/** A7 resume: pops the parked order into CurrentOrder (serial bump).
	 *  Returns false when nothing is parked. */
	bool ConsumePendingAfterEgress(FUnitHandle Handle, FUnitOrder& OutParked);
	/** Current order + its serial (locomotion change detector). */
	const FUnitOrder* FindOrder(FUnitHandle Handle) const;
	uint32 GetOrderSerial(FUnitHandle Handle) const;

	// -- Phase 2 attack order surface (brief A4, task 2; game thread) --

	/** Order the unit to attack a specific enemy unit. A9 guard: garrisoned /
	 *  garrison-committed attacker or target rejects with an EXEC_RESULT line
	 *  (garrisoned units are not valid melee targets and never seek, F11).
	 *  Serial bump on success; the intent fragment is stamped by the assign
	 *  pass's order-consumption block (task 6). */
	bool IssueAttackUnitOrder(FUnitHandle Handle, FUnitHandle TargetHandle);
	/** AttackMove: march to the anchor and auto-engage en route. An optional
	 *  TargetHandle pre-designates the first target. Same A9 guard. */
	bool IssueAttackMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg,
		float CommandedSpeedMps = 0.f, FUnitHandle TargetHandle = FUnitHandle());

	// -- Phase 2 engagement records + scratch (spec §3.2, task 2; game thread) --

	/** Insert-or-refresh the (Attacker, Defender) record. Returns nullptr when
	 *  the store is at capacity and the key is new (preallocated store grows
	 *  at battle start only, EC9). */
	FEngagementRecord* CreateOrRefreshEngagement(FUnitHandle Attacker, FUnitHandle Defender);
	const FEngagementRecord* FindEngagement(FUnitHandle Attacker, FUnitHandle Defender) const;
	FEngagementRecord* FindEngagementMutable(FUnitHandle Attacker, FUnitHandle Defender);

	/** Mass-release support (spec §4.7): flags every record where Unit is the
	 *  attacker OR defender dead; the assign pass releases inbound pairs the
	 *  same pass it observes the flags. Returns the flagged count. */
	int32 FlagUnitEngagementsDead(FUnitHandle Unit);

	/** Removes dead-flagged records (cluster pass, 4 Hz). Returns removed. */
	int32 PruneDeadEngagements();

	/** Cluster-pass rebuild prelude: flags EVERY record dead; CreateOrRefresh
	 *  revives the contacted pairs, Prune removes the rest. Returns flagged. */
	int32 FlagAllEngagementsDead();

	/** 4 Hz cluster-pass cadence (processor-owned, registry-stored: per-world
	 *  state; game thread only). */
	float GetCombatClusterCooldown() const { return CombatClusterCooldown; }
	void SetCombatClusterCooldown(float Seconds) { CombatClusterCooldown = Seconds; }

	/** Spec §3.2: per-unit engagement aggregates (FSM + metrics inputs). */
	void SetEngagedCount(FUnitHandle Handle, int32 Count);
	int32 GetEngagedCount(FUnitHandle Handle) const;

	/** Records in a deterministic (attacker index, defender index) order --
	 *  hash/debug/save consumers iterate this, never the map directly. */
	TArray<FEngagementKey> GetSortedEngagementKeys() const;
	int32 GetEngagementCount() const { return Engagements.Num(); }

	/** Combat scratch (preallocated at Initialize, reset each pass, EC9). */
	FCombatTallyTable& GetTallyScratch() { return TallyScratch; }
	FCombatCoverageTable& GetCoverageScratch() { return CoverageScratch; }
	/** F2 hook side: drop one dead entity's scratch entries. */
	void NotifyCombatEntityDeath(FMassEntityHandle Entity);

	/** Scope-free inner half (HandleUnitDeath already holds the write scope). */
	void NotifyCombatEntityDeathInternal(FMassEntityHandle Entity);

	// -- Phase 2 save/load skeleton (A8, R7; spec §3.5; game thread) --

	/** Full battle reset (test/diagnostic: the T2.8 two-run needs identical
	 *  slot serials across runs). Destroys every unit's entities, clears the
	 *  slot bookkeeping to the pristine state. Game thread, Mass idle. */
	void ResetBattleState();

	/** Snapshot the registry runtime state (rosters, orders, serials,
	 *  attributes, engagement records) + RNG stream states into the
	 *  schema-versioned save struct. Entities are NOT serialized (v1). */
	void CaptureBattleState(FBattleSaveData& OutData) const;
	/** Rebuild registry runtime state from the snapshot (capture -> clear ->
	 *  restore round-trip; entity rebuild-from-spec + payload overwrite is the
	 *  T6.3 maturity path). Returns false on a schema-version mismatch. */
	bool RestoreBattleState(const FBattleSaveData& InData);

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

	// -- Phase 2 combat state (spec §3.2; game thread only) --

	/** Engagement records keyed (attacker, defender). Reserved at Initialize
	 *  to the battle-scale cap; grows at battle start only (EC9). */
	TMap<FEngagementKey, FEngagementRecord> Engagements;

	/** Scratch tables (spec §3.2): open-address tally + stamped coverage
	 *  cache; preallocated once, Reset() per pass, never reallocated. */
	FCombatTallyTable TallyScratch;
	FCombatCoverageTable CoverageScratch;

	/** 4 Hz cluster-pass cadence accumulator (game thread only). */
	float CombatClusterCooldown = 0.f;
};
