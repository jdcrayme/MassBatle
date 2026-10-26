// BattleGarrisonSubsystem.h -- UBattleGarrisonSubsystem (spec garrison-movement
// §3.1/§3.2): game-thread owner of the immutable structure records + the
// mutable booking state (capacity ledger, per-entrance entry leases, queue
// wait slots). Plain structs only -- the sim never touches a UObject through
// it (ADR-02); worker processors read exclusively the fragment mirrors (A2),
// so this subsystem is accessed on the game thread only (ground rule 2/3).
// Records are immutable after registration for the duration of a battle;
// booking state is the only mutable part (ground rule 6).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BattleGarrisonTypes.h"
#include "BattleGarrisonSubsystem.generated.h"

/** Queue-wait bookkeeping: which hold point a waiting unit occupies at an
 *  entrance (deterministic smallest-free-slot assignment, spec §5). */
struct FGarrisonQueueWait
{
	uint32 StructureId = 0;
	uint8 EntranceIndex = 0;
	int32 QueueSlot = INDEX_NONE;
};

UCLASS()
class MASSBATTLE_API UBattleGarrisonSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- USubsystem --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -- structure registry (immutable after registration, ground rule 6) --

	/** Copies the record, assigns StructureId (1-based, sequential), stamps
	 *  the cost grid (task 2) and returns the id (0 on rejection). */
	uint32 RegisterStructure(const FGarrisonStructureRecord& Record);
	const FGarrisonStructureRecord* FindStructure(uint32 StructureId) const;
	int32 GetStructureCount() const { return Structures.Num(); }
	/** Overlay/metrics iteration (game thread, read-only). */
	const TMap<uint32, FGarrisonStructureRecord>& GetAllStructures() const { return Structures; }
	/** True when any booking covers the slot (overlay occupied/free colors). */
	bool IsSlotBooked(uint32 StructureId, int32 SlotIndex) const;

	// -- booking ledger (spec §3.2; ADR-21 determinism) --

	/** Books a disjoint slot range for the unit within the entrance's slot
	 *  block if capacity allows (records group slots by entrance, so ranges
	 *  stay contiguous). Allocation is deterministic first-fit in slot order;
	 *  callers resolve multi-unit races by issuing from the sorted-handle
	 *  discipline (GetSortedHandles) on the game thread. Fails when the
	 *  structure is full (MaxUnits or no free contiguous range in the block)
	 *  or on a stale/duplicate handle. */
	bool TryBook(FUnitHandle Unit, uint32 StructureId, uint8 EntranceIndex, int32 SlotCount, FGarrisonBooking& OutBooking);
	/** Releases the unit's booking (egress completion, death, unregister). */
	bool ReleaseBooking(FUnitHandle Unit);
	/** Death/unregister cleanup (F14): drops the booking, any entry lease the
	 *  unit holds, and its queue wait in one deterministic pass. */
	void ReleaseUnit(FUnitHandle Unit);
	const FGarrisonBooking* FindBooking(FUnitHandle Unit) const;
	/** Units currently holding a booking on the structure. */
	int32 GetBookedCount(uint32 StructureId) const;
	/** Units currently WAITING (queue holds) on the structure. */
	int32 GetQueueWaitCount(uint32 StructureId) const;

	// -- entry leases (B3 choke filing: at most one unit Ingress/Egress per
	//    entrance at a time; others hold at queue points) --

	bool TryAcquireEntryLease(uint32 StructureId, uint8 EntranceIndex, FUnitHandle Unit);
	void ReleaseEntryLease(uint32 StructureId, uint8 EntranceIndex, FUnitHandle Unit);
	bool IsEntryLeaseFree(uint32 StructureId, uint8 EntranceIndex) const;

	// -- queue-wait slots (task 7: deterministic hold-point assignment) --

	/** Registers the unit as waiting at the entrance; returns the hold-point
	 *  index (smallest free slot; INDEX_NONE when the entrance is saturated). */
	int32 RegisterQueueWait(FUnitHandle Unit, uint32 StructureId, uint8 EntranceIndex);
	void UnregisterQueueWait(FUnitHandle Unit);
	const FGarrisonQueueWait* FindQueueWait(FUnitHandle Unit) const;

	// -- introspection (debug overlay / metrics / EXEC_RESULT evidence) --

	/** One EXEC_RESULT Garrison.Structure line per registered structure
	 *  (occupancy, capacity, leases, queue waits). */
	void DumpStructures() const;

private:
	/** Live records by id; registration order defines the 1-based ids. */
	TMap<uint32, FGarrisonStructureRecord> Structures;
	uint32 NextStructureId = 1;

	/** Bookings by unit handle (a unit holds at most one booking). */
	TMap<FUnitHandle, FGarrisonBooking> Bookings;

	/** Entry lease holder per (structureId, entrance); unset = free. */
	TMap<uint64, FUnitHandle> EntryLeaseHolders;

	/** Queue waits by unit handle. */
	TMap<FUnitHandle, FGarrisonQueueWait> QueueWaits;

	static uint64 LeaseKey(uint32 StructureId, uint8 EntranceIndex)
	{
		return (static_cast<uint64>(StructureId) << 8) | EntranceIndex;
	}
};
