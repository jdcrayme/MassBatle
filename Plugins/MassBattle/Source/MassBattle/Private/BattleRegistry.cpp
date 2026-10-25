#include "BattleRegistry.h"
#include "BattleExternalSubsystemTraits.h"
#include "MassBattleModule.h"
#include "BattleExecCommands.h"
#include "HAL/IConsoleManager.h"

void UUnitRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	Units.Reset();
	SpecsByIndex.Reset();
	NextGenerationByIndex.Reset();
	FreeIndices.Reset();
	NextIndex = 1;
	DirtyStats.Reset();
	UE_LOG(LogMassBattle, Log, TEXT("UnitRegistry initialized"));
}

void UUnitRegistrySubsystem::Deinitialize()
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	Units.Reset();
	SpecsByIndex.Reset();
	NextGenerationByIndex.Reset();
	DirtyStats.Reset();
	Super::Deinitialize();
}

FUnitHandle UUnitRegistrySubsystem::RegisterUnit(const FUnitSpawnSpec& Spec)
{
	if (Spec.EduRowIndex == INDEX_NONE || Spec.SoldierCount <= 0)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("RegisterUnit: rejecting invalid spec (row=%d count=%d)"),
		       Spec.EduRowIndex, Spec.SoldierCount);
		return FUnitHandle();
	}

	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);

	uint32 Index;
	if (FreeIndices.Num() > 0)
	{
		Index = FreeIndices.Last();
		FreeIndices.Pop();
	}
	else
	{
		Index = NextIndex++;
	}

	// Per-slot generation counter: ++ on every allocation, so handles from a
	// previous tenant of this slot are stale (T0.1). Serial 0 = null handle.
	const int32 Generation = NextGenerationByIndex.FindOrAdd(Index) + 1;
	NextGenerationByIndex[Index] = Generation;

	FUnitHandle Handle;
	Handle.Index = Index;
	Handle.Serial = static_cast<uint32>(Generation);

	FUnitRuntimeState& State = Units.Add(Handle);
	State.Generation = Generation;

	FUnitSpawnSpec& StoredSpec = SpecsByIndex.Add(Index);
	StoredSpec = Spec;

	return Handle;
}

bool UUnitRegistrySubsystem::UnregisterUnit(FUnitHandle Handle)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}

	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);

	FUnitRuntimeState State;
	if (!Units.RemoveAndCopyValue(Handle, State))
	{
		return false;
	}

	State.Roster.Reset();
	SpecsByIndex.Remove(Handle.Index);
	DirtyStats.Remove(Handle);
	FreeIndices.Push(Handle.Index);
	return true;
}

bool UUnitRegistrySubsystem::IsHandleValid(FUnitHandle Handle) const
{
	if (!Handle.IsValid())
	{
		return false;
	}
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return Units.Contains(Handle);
}

const FUnitSpawnSpec* UUnitRegistrySubsystem::FindUnitSpec(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	// index-based map: a stale handle must not alias the new tenant's spec
	if (!Handle.IsValid() || !Units.Contains(Handle))
	{
		return nullptr;
	}
	return SpecsByIndex.Find(Handle.Index);
}

void UUnitRegistrySubsystem::SetRoster(FUnitHandle Handle, TArray<FMassEntityHandle>&& Roster)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (FUnitRuntimeState* State = Units.Find(Handle))
	{
		State->Roster = MoveTemp(Roster);
	}
}

const TArray<FMassEntityHandle>* UUnitRegistrySubsystem::FindRoster(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? &State->Roster : nullptr;
}

void UUnitRegistrySubsystem::HandleUnitDeath(FUnitHandle Unit, FMassEntityHandle Entity, int32 RosterIndex)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (FUnitRuntimeState* State = Units.Find(Unit))
	{
		const int32 Removed = State->Roster.RemoveSingle(Entity);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RosterDeath Unit=%u:%u Entity=%u:%u RosterIndex=%d Removed=%d Size=%d"),
		       Unit.Index, Unit.Serial, Entity.Index, Entity.SerialNumber, RosterIndex, Removed, State->Roster.Num());
	}
}

int32 UUnitRegistrySubsystem::GetRosterSize(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? State->Roster.Num() : 0;
}

void UUnitRegistrySubsystem::UpdateCachedAttributes(FUnitHandle Handle, const FBattleAttributeSnapshot& Snapshot)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (FUnitRuntimeState* State = Units.Find(Handle))
	{
		State->CachedAttributes = Snapshot;
	}
}

const FBattleAttributeSnapshot* UUnitRegistrySubsystem::FindCachedAttributes(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? &State->CachedAttributes : nullptr;
}

void UUnitRegistrySubsystem::MarkStatsDirty(FUnitHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	DirtyStats.Add(Handle); // set semantics: idempotent per frame (T0.1)
}

void UUnitRegistrySubsystem::ClearStatsDirty(FUnitHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	DirtyStats.Remove(Handle);
}

bool UUnitRegistrySubsystem::IsStatsDirty(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return DirtyStats.Contains(Handle);
}

// -- A2 thin order API (decision A2) --

bool UUnitRegistrySubsystem::IssueMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg, float CommandedSpeedMps)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		return false;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order.Type = FUnitOrder::EType::Move;
	Order.AnchorPos = AnchorPosCm;
	Order.FacingYaw = FacingYawDeg;
	Order.CommandedSpeed = CommandedSpeedMps;
	++State->OrderSerial;
	return true;
}

bool UUnitRegistrySubsystem::IssueStopOrder(FUnitHandle Handle)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		return false;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order.Type = FUnitOrder::EType::Stop;
	Order.AnchorPos = FVector2D::ZeroVector;
	Order.FacingYaw = 0.f;
	Order.CommandedSpeed = 0.f;
	++State->OrderSerial;
	return true;
}

const FUnitOrder* UUnitRegistrySubsystem::FindOrder(FUnitHandle Handle) const
{
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? &State->CurrentOrder : nullptr;
}

uint32 UUnitRegistrySubsystem::GetOrderSerial(FUnitHandle Handle) const
{
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? State->OrderSerial : 0;
}

int32 UUnitRegistrySubsystem::GetUnitCount() const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return Units.Num();
}

const FUnitRuntimeState* UUnitRegistrySubsystem::FindUnitState(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return Units.Find(Handle);
}

TArray<FUnitHandle> UUnitRegistrySubsystem::GetSortedHandles() const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	TArray<FUnitHandle> Handles;
	Handles.Reserve(Units.Num());
	for (const TPair<FUnitHandle, FUnitRuntimeState>& Pair : Units)
	{
		Handles.Add(Pair.Key);
	}
	Handles.Sort([](const FUnitHandle& A, const FUnitHandle& B) { return A.Index < B.Index; });
	return Handles;
}

uint32 UUnitRegistrySubsystem::ComputeBookkeepingHash() const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	// Deterministic fold over sorted handles: serial + roster size. Dirty flags
	// are deliberately EXCLUDED (audit M4): they are transient pipeline
	// scheduling state, not sim state -- a MarkStatsDirty with no data change
	// must not perturb the sim-state hash (T2.8-style two-run comparisons).
	uint32 Hash = 0x9E3779B9; // golden-ratio seed
	for (const FUnitHandle& Handle : GetSortedHandles())
	{
		const FUnitRuntimeState* State = Units.Find(Handle);
		const int32 RosterSize = State ? State->Roster.Num() : -1;
		Hash = HashCombine(Hash, ::GetTypeHash(Handle));
		Hash = HashCombine(Hash, ::GetTypeHash(RosterSize));
	}
	Hash = HashCombine(Hash, ::GetTypeHash(FreeIndices.Num()));
	return Hash;
}
