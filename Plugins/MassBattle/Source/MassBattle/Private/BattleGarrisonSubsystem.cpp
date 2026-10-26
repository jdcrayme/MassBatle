// BattleGarrisonSubsystem.cpp -- see header. Booking/lease/queue state is pure
// bookkeeping: reconstructible from spec + orders (B.8 item 7, Iris-ready), no
// raw pointers out, no RNG.
#include "BattleGarrisonSubsystem.h"
#include "BattleGarrison.h"
#include "BattleCostGridSubsystem.h"
#include "MassBattleModule.h"

#include "Engine/World.h"

void UBattleGarrisonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UBattleGarrisonSubsystem::Deinitialize()
{
	Structures.Reset();
	Bookings.Reset();
	EntryLeaseHolders.Reset();
	QueueWaits.Reset();
	Super::Deinitialize();
}

uint32 UBattleGarrisonSubsystem::RegisterStructure(const FGarrisonStructureRecord& Record)
{
	// Validity gate: a structure without an entrance or posts cannot garrison.
	if (Record.Entrances.Num() == 0 || Record.Slots.Num() == 0)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT Garrison.Register FAIL no-entrances-or-slots"));
		return 0;
	}
	if (UBattleCostGridSubsystem* CostGrid = GetWorld() ? GetWorld()->GetSubsystem<UBattleCostGridSubsystem>() : nullptr)
	{
		FGarrisonLayout::StampStructure(Record, CostGrid->GetMutableGrid());
	}
	const uint32 Id = NextStructureId++;
	FGarrisonStructureRecord& Copy = Structures.Add(Id, Record);
	Copy.EntryPaths.SetNum(Record.Entrances.Num());
	for (int32 E = 0; E < Record.Entrances.Num(); ++E)
	{
		// Internal paths are static structure data (ADR-19): built once at
		// registration, immutable after (ground rule 6).
		FGarrisonLayout::BuildInternalPath(Copy, E, /*bEgress*/ false, Copy.EntryPaths[E]);
	}
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT Garrison.Register OK Structure=%u Type=%d MaxUnits=%d Slots=%d Entrances=%d"),
		Id, static_cast<int32>(Record.Type), Record.MaxUnits, Record.Slots.Num(), Record.Entrances.Num());
	return Id;
}

const FGarrisonStructureRecord* UBattleGarrisonSubsystem::FindStructure(uint32 StructureId) const
{
	return Structures.Find(StructureId);
}

bool UBattleGarrisonSubsystem::TryBook(FUnitHandle Unit, uint32 StructureId, uint8 EntranceIndex,
	int32 SlotCount, FGarrisonBooking& OutBooking)
{
	if (!Unit.IsValid() || SlotCount <= 0 || Bookings.Contains(Unit))
	{
		return false;
	}
	const FGarrisonStructureRecord* Record = Structures.Find(StructureId);
	if (!Record || !Record->Entrances.IsValidIndex(EntranceIndex))
	{
		return false;
	}
	if (GetBookedCount(StructureId) >= Record->MaxUnits)
	{
		return false; // ETW B2: full structure refuses further garrisons
	}

	// The entrance's slot block: records group slots by EntranceIndex, so the
	// block is one contiguous index range. Deterministic first-fit over it.
	int32 BlockStart = INDEX_NONE;
	int32 BlockEnd = INDEX_NONE; // exclusive
	for (int32 i = 0; i < Record->Slots.Num(); ++i)
	{
		if (Record->Slots[i].EntranceIndex == EntranceIndex)
		{
			if (BlockStart == INDEX_NONE)
			{
				BlockStart = i;
			}
			BlockEnd = i + 1;
		}
		else if (BlockStart != INDEX_NONE)
		{
			break; // past the block
		}
	}
	if (BlockStart == INDEX_NONE || BlockEnd - BlockStart < SlotCount)
	{
		return false;
	}

	// Occupancy bitmap of the block from live bookings.
	TArray<bool> Occupied;
	Occupied.Init(false, BlockEnd - BlockStart);
	int32 BookedUnits = 0;
	for (const TPair<FUnitHandle, FGarrisonBooking>& Pair : Bookings)
	{
		if (Pair.Value.StructureId != StructureId)
		{
			continue;
		}
		++BookedUnits;
		for (int32 i = Pair.Value.SlotBase; i < Pair.Value.SlotBase + Pair.Value.SlotCount; ++i)
		{
			if (i >= BlockStart && i < BlockEnd)
			{
				Occupied[i - BlockStart] = true;
			}
		}
	}
	if (BookedUnits >= Record->MaxUnits)
	{
		return false;
	}

	// First-fit gap of exactly SlotCount within the block.
	for (int32 Start = 0; Start + SlotCount <= Occupied.Num(); ++Start)
	{
		bool bFree = true;
		for (int32 i = Start; i < Start + SlotCount; ++i)
		{
			if (Occupied[i])
			{
				Start = i; // skip past this occupied cell on the next outer step
				bFree = false;
				break;
			}
		}
		if (bFree)
		{
			FGarrisonBooking Booking;
			Booking.Unit = Unit;
			Booking.StructureId = StructureId;
			Booking.EntranceIndex = EntranceIndex;
			Booking.SlotBase = BlockStart + Start;
			Booking.SlotCount = SlotCount;
			Bookings.Add(Unit, Booking);
			OutBooking = Booking;
			return true;
		}
	}
	return false;
}

bool UBattleGarrisonSubsystem::ReleaseBooking(FUnitHandle Unit)
{
	return Bookings.Remove(Unit) > 0;
}

const FGarrisonBooking* UBattleGarrisonSubsystem::FindBooking(FUnitHandle Unit) const
{
	return Bookings.Find(Unit);
}

int32 UBattleGarrisonSubsystem::GetBookedCount(uint32 StructureId) const
{
	int32 Count = 0;
	for (const TPair<FUnitHandle, FGarrisonBooking>& Pair : Bookings)
	{
		if (Pair.Value.StructureId == StructureId)
		{
			++Count;
		}
	}
	return Count;
}

int32 UBattleGarrisonSubsystem::GetQueueWaitCount(uint32 StructureId) const
{
	int32 Count = 0;
	for (const TPair<FUnitHandle, FGarrisonQueueWait>& Pair : QueueWaits)
	{
		if (Pair.Value.StructureId == StructureId)
		{
			++Count;
		}
	}
	return Count;
}

bool UBattleGarrisonSubsystem::TryAcquireEntryLease(uint32 StructureId, uint8 EntranceIndex, FUnitHandle Unit)
{
	if (!Unit.IsValid() || !Structures.Contains(StructureId))
	{
		return false;
	}
	const uint64 Key = LeaseKey(StructureId, EntranceIndex);
	if (const FUnitHandle* Holder = EntryLeaseHolders.Find(Key))
	{
		return *Holder == Unit; // re-acquire by the holder is idempotent
	}
	EntryLeaseHolders.Add(Key, Unit);
	return true;
}

void UBattleGarrisonSubsystem::ReleaseEntryLease(uint32 StructureId, uint8 EntranceIndex, FUnitHandle Unit)
{
	const uint64 Key = LeaseKey(StructureId, EntranceIndex);
	if (const FUnitHandle* Holder = EntryLeaseHolders.Find(Key))
	{
		if (*Holder == Unit)
		{
			EntryLeaseHolders.Remove(Key);
		}
	}
}

bool UBattleGarrisonSubsystem::IsEntryLeaseFree(uint32 StructureId, uint8 EntranceIndex) const
{
	return !EntryLeaseHolders.Contains(LeaseKey(StructureId, EntranceIndex));
}

int32 UBattleGarrisonSubsystem::RegisterQueueWait(FUnitHandle Unit, uint32 StructureId, uint8 EntranceIndex)
{
	if (!Unit.IsValid() || QueueWaits.Contains(Unit))
	{
		return INDEX_NONE;
	}
	// Smallest free hold point at this entrance (deterministic; spec §5).
	TArray<bool> Taken;
	Taken.Init(false, 16); // v1 saturation bound: 16 waiting units per entrance
	for (const TPair<FUnitHandle, FGarrisonQueueWait>& Pair : QueueWaits)
	{
		if (Pair.Value.StructureId == StructureId && Pair.Value.EntranceIndex == EntranceIndex
			&& Pair.Value.QueueSlot != INDEX_NONE && Pair.Value.QueueSlot < Taken.Num())
		{
			Taken[Pair.Value.QueueSlot] = true;
		}
	}
	for (int32 i = 0; i < Taken.Num(); ++i)
	{
		if (!Taken[i])
		{
			FGarrisonQueueWait Wait;
			Wait.StructureId = StructureId;
			Wait.EntranceIndex = EntranceIndex;
			Wait.QueueSlot = i;
			QueueWaits.Add(Unit, Wait);
			return i;
		}
	}
	return INDEX_NONE;
}

void UBattleGarrisonSubsystem::UnregisterQueueWait(FUnitHandle Unit)
{
	QueueWaits.Remove(Unit);
}

const FGarrisonQueueWait* UBattleGarrisonSubsystem::FindQueueWait(FUnitHandle Unit) const
{
	return QueueWaits.Find(Unit);
}

void UBattleGarrisonSubsystem::DumpStructures() const
{
	for (const TPair<uint32, FGarrisonStructureRecord>& Pair : Structures)
	{
		const FGarrisonStructureRecord& Rec = Pair.Value;
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT Garrison.Structure Id=%u Type=%d MaxUnits=%d Booked=%d Waiting=%d Slots=%d Entrances=%d"),
			Pair.Key, static_cast<int32>(Rec.Type), Rec.MaxUnits, GetBookedCount(Pair.Key),
			GetQueueWaitCount(Pair.Key), Rec.Slots.Num(), Rec.Entrances.Num());
	}
	if (Structures.Num() == 0)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT Garrison.Structure none"));
	}
	for (const TPair<FUnitHandle, FGarrisonBooking>& Pair : Bookings)
	{
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT Garrison.Booking Unit=%u:%u Structure=%u Entrance=%d Range=[%d,%d)"),
			Pair.Key.Index, Pair.Key.Serial, Pair.Value.StructureId,
			static_cast<int32>(Pair.Value.EntranceIndex), Pair.Value.SlotBase,
			Pair.Value.SlotBase + Pair.Value.SlotCount);
	}
}
