#include "BattleRegistry.h"
#include "BattleExternalSubsystemTraits.h"
#include "BattleFragments.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleRangedFragments.h"
#include "BattleRngRegistry.h"
#include "BattleSave.h"
#include "MassBattleModule.h"
#include "BattleExecCommands.h"
#include "HAL/IConsoleManager.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassCommandBuffer.h"

namespace
{
	// Battle-scale scratch caps (EC9: preallocated at Initialize, grown at
	// battle start only). Tally/coverage are keyed per-DEFENDER entity; the
	// 2x2400-entity target leaves headroom in 8192 slots.
	constexpr int32 MaxEngagementRecords = 4096;
	constexpr int32 CombatScratchCapacity = 8192;
}

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
	// Phase 2 combat state (task 2): the record store + scratch tables are
	// preallocated once per battle; passes Reset() them, never reallocate.
	Engagements.Reset();
	Engagements.Reserve(MaxEngagementRecords);
	TallyScratch.Initialize(CombatScratchCapacity);
	CoverageScratch.Initialize(CombatScratchCapacity);
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
	if (UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr)
	{
		Garrison->ReleaseUnit(Handle);
	}
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
		// Phase 3 (spec §3.2): the fraction denominator's roster half stamps
		// at the one publish (the spawn fixup; restore never routes through
		// here) -- the live roster shrinks with deaths, the denominator does
		// not.
		State->InitialRosterSize = State->Roster.Num();
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
		// Phase 2 (F2): the single registry pair-cleanup hook. The dead
		// entity's scratch entries drop here so the next fresh-tally pass
		// never counts a corpse; inbound pair fragments release through the
		// assign pass's top-of-pass validation (same-tick handoff, EC5).
		NotifyCombatEntityDeathInternal(Entity);
		// F14: a unit dying out mid-occupation releases its garrison booking +
		// queue hold here (the single correct place per the brief's fact).
		if (State->Roster.Num() == 0)
		{
			if (UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr)
			{
				Garrison->ReleaseUnit(Unit);
			}
		}
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

void UUnitRegistrySubsystem::SetUnitRangedSpawnState(FUnitHandle Handle, bool bInSkirmish, int32 InitialAmmo)
{
	// Phase 3 (spec ranged-stack §3.2, A8): spawn-time stamp -- skirmish
	// default + the fraction denominator's ammo half. Called from
	// UBattleSpawnLibrary::SpawnUnit right after SetRoster. Melee rows pass
	// AmmoPerMan 0 -> the fraction reads -1 (non-ranged).
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (FUnitRuntimeState* State = Units.Find(Handle))
	{
		State->bSkirmish = bInSkirmish;
		State->RangedInitialAmmo = FMath::Max(0, InitialAmmo);
	}
}

bool UUnitRegistrySubsystem::SetUnitFireToggles(FUnitHandle Handle, bool bFireAtWill, bool bSkirmish)
{
	// The A11 rejection pattern (OrderAttackUnit precedent): a bad handle
	// logs an EXEC_RESULT line and returns false. One write scope for the
	// validate-and-write half (the M7 TOCTOU lesson).
	if (!Handle.IsValid())
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedToggle FAIL BadHandle Handle=%u:%u"),
			Handle.Index, Handle.Serial);
		return false;
	}
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT RangedToggle FAIL StaleHandle Handle=%u:%u"),
			Handle.Index, Handle.Serial);
		return false;
	}
	State->bFireAtWill = bFireAtWill;
	State->bSkirmish = bSkirmish;
	return true;
}

void UUnitRegistrySubsystem::GetUnitFireToggles(FUnitHandle Handle, bool& bOutFireAtWill, bool& bOutSkirmish, bool& bOutOutOfAmmo) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	bOutFireAtWill = State ? State->bFireAtWill : true;
	bOutSkirmish = State ? State->bSkirmish : false;
	bOutOutOfAmmo = State ? State->bOutOfAmmo : false;
}

float UUnitRegistrySubsystem::GetUnitAmmoFraction(FUnitHandle Handle) const
{
	// RC5 aggregate: live sum / (RangedInitialAmmo x InitialRosterSize). The
	// registry is the fraction's authority (arch §10.1, A14); the GAS
	// AmmoPool attribute mirrors it for UI only (UnitSync, Task 4).
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	if (!State || State->RangedInitialAmmo <= 0 || State->InitialRosterSize <= 0)
	{
		return -1.f;
	}
	UMassEntitySubsystem* EntitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!EntitySubsystem)
	{
		return -1.f;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	int32 AmmoSum = 0;
	bool bAnyRanged = false;
	for (const FMassEntityHandle& Entity : State->Roster)
	{
		// Dead entities left the roster at HandleUnitDeath; unresolvable
		// handles (registry-only restored state) yield null fragment data
		// and simply do not contribute.
		const FRangedStateFragment* Ranged =
			FMassEntityView(EntityManager, Entity).GetFragmentDataPtr<FRangedStateFragment>();
		if (Ranged)
		{
			bAnyRanged = true;
			AmmoSum += Ranged->AmmoCount;
		}
	}
	if (!bAnyRanged)
	{
		return -1.f;
	}
	return static_cast<float>(AmmoSum) / static_cast<float>(State->RangedInitialAmmo * State->InitialRosterSize);
}

bool UUnitRegistrySubsystem::FlipUnitOutOfAmmo(FUnitHandle Handle)
{
	// A14: flips exactly once at aggregate zero, never un-flips (no resupply
	// v1). Idempotent afterward: repeat calls report false, not an error.
	if (!Handle.IsValid())
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OutOfAmmo FAIL BadHandle Handle=%u:%u"),
			Handle.Index, Handle.Serial);
		return false;
	}
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OutOfAmmo FAIL StaleHandle Handle=%u:%u"),
			Handle.Index, Handle.Serial);
		return false;
	}
	if (State->bOutOfAmmo)
	{
		return false;
	}
	State->bOutOfAmmo = true;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OutOfAmmo Unit=%u:%u Roster=%d InitialAmmo=%d"),
		Handle.Index, Handle.Serial, State->Roster.Num(), State->RangedInitialAmmo);
	return true;
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

bool UUnitRegistrySubsystem::IssueMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg,
	float CommandedSpeedMps, int32 CommandedFiles)
{
	// Audit M7: the order API mutates the registry under an access-detector
	// write scope (all callers are game-thread today -- discipline, not a
	// live race -- and the validate-and-fetch now share one scope, closing
	// the IsHandleValid/Find TOCTOU).
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	// Handle.IsValid() only bit-checks (IsHandleValid would nest a read
	// scope inside this write scope -- detector violation); the map Find
	// below IS the stale-handle validation (handle == map key).
	if (!Handle.IsValid())
	{
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		return false;
	}
	// Audit M11: an identical re-issue is a no-op -- no serial bump, no
	// full A*+Smooth+Resample replan on the game thread. A client issuing
	// the same order every tick replans once, not every tick.
	const FUnitOrder& Prev = State->CurrentOrder;
	if (Prev.Type == FUnitOrder::EType::Move
		&& FVector2D::DistSquared(Prev.AnchorPos, AnchorPosCm) <= FMath::Square(100.f)
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(Prev.FacingYaw, FacingYawDeg)) < 1.f
		&& Prev.CommandedSpeed == CommandedSpeedMps
		&& Prev.CommandedFiles == CommandedFiles)
	{
		return true;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order.Type = FUnitOrder::EType::Move;
	Order.AnchorPos = AnchorPosCm;
	Order.FacingYaw = FacingYawDeg;
	Order.CommandedSpeed = CommandedSpeedMps;
	Order.CommandedFiles = CommandedFiles;
	++State->OrderSerial;
	return true;
}

bool UUnitRegistrySubsystem::IssueStopOrder(FUnitHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector); // audit M7
	if (!Handle.IsValid())
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
	Order.CommandedFiles = 0;
	Order.StructureId = 0;
	++State->OrderSerial;
	return true;
}

// -- Phase garrison order surface (spec §4.1) --

bool UUnitRegistrySubsystem::IssueGarrisonOrder(FUnitHandle Handle, uint32 StructureId)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector); // audit M7 (IsHandleValid would
	// nest a read scope in the write scope; the Find below validates instead)
	if (!Handle.IsValid() || StructureId == 0)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT GarrisonOrder REJECT BadHandleOrStructure Unit=%u:%u Structure=%u"),
			Handle.Index, Handle.Serial, StructureId);
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		return false;
	}
	// A9 cavalry guard clause: the EDU class tag lives on the roster entities
	// (F11 composition); walls and trenches are infantry-only v1 as well.
	if (UMassEntitySubsystem* Entities = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		if (State->Roster.Num() > 0)
		{
			const FMassEntityView View(Entities->GetMutableEntityManager(), State->Roster[0]);
			if (View.HasTag<FCavalryTag>())
			{
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT GarrisonOrder REJECT CAVALRY Unit=%u:%u Structure=%u"),
					Handle.Index, Handle.Serial, StructureId);
				return false;
			}
		}
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order.Type = FUnitOrder::EType::Garrison;
	Order.AnchorPos = FVector2D::ZeroVector;
	Order.FacingYaw = 0.f;
	Order.CommandedSpeed = 0.f;
	Order.CommandedFiles = 0;
	Order.StructureId = StructureId;
	++State->OrderSerial;
	return true;
}

bool UUnitRegistrySubsystem::IssueExitGarrisonOrder(FUnitHandle Handle)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector); // audit M7
	if (!Handle.IsValid())
	{
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State)
	{
		return false;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order.Type = FUnitOrder::EType::ExitGarrison;
	Order.AnchorPos = FVector2D::ZeroVector;
	Order.FacingYaw = 0.f;
	Order.CommandedSpeed = 0.f;
	Order.CommandedFiles = 0;
	Order.StructureId = 0;
	++State->OrderSerial;
	return true;
}

void UUnitRegistrySubsystem::ParkPendingAfterEgress(FUnitHandle Handle, const FUnitOrder& Order)
{
	if (!IsHandleValid(Handle))
	{
		return;
	}
	if (FUnitRuntimeState* State = Units.Find(Handle))
	{
		State->PendingAfterEgress = Order;
	}
}

bool UUnitRegistrySubsystem::ConsumePendingAfterEgress(FUnitHandle Handle, FUnitOrder& OutParked)
{
	if (!IsHandleValid(Handle))
	{
		return false;
	}
	FUnitRuntimeState* State = Units.Find(Handle);
	if (!State || State->PendingAfterEgress.Type == FUnitOrder::EType::None)
	{
		return false;
	}
	OutParked = State->PendingAfterEgress;
	State->PendingAfterEgress = FUnitOrder();
	State->CurrentOrder = OutParked;
	++State->OrderSerial;
	return true;
}

const FUnitOrder* UUnitRegistrySubsystem::FindOrder(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector); // audit M7
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? &State->CurrentOrder : nullptr;
}

uint32 UUnitRegistrySubsystem::GetOrderSerial(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector); // audit M7
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

// -- Phase 2 attack order surface (brief A4, task 2) --

bool UUnitRegistrySubsystem::IssueAttackUnitOrder(FUnitHandle Handle, FUnitHandle TargetHandle)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	FUnitRuntimeState* State = Handle.IsValid() ? Units.Find(Handle) : nullptr;
	const FUnitRuntimeState* TargetState = TargetHandle.IsValid() ? Units.Find(TargetHandle) : nullptr;
	const TCHAR* Reject = nullptr;
	if (!State)
	{
		Reject = TEXT("BadHandle");
	}
	else if (!TargetState)
	{
		Reject = TEXT("BadTarget");
	}
	else if (TargetState->CurrentOrder.Type == FUnitOrder::EType::Garrison)
	{
		Reject = TEXT("TargetGarrisonOrder");
	}
	else if (State->CurrentOrder.Type == FUnitOrder::EType::Garrison)
	{
		Reject = TEXT("AttackerGarrisonOrder");
	}
	if (!Reject)
	{
		// A9 (F11): a unit committed to the garrison flow (booked or queuing)
		// is neither a valid melee target nor an attack initiator.
		if (UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr)
		{
			if (Garrison->IsUnitCommittedToGarrison(TargetHandle))
			{
				Reject = TEXT("TargetGarrisonCommitted");
			}
			else if (Garrison->IsUnitCommittedToGarrison(Handle))
			{
				Reject = TEXT("AttackerGarrisonCommitted");
			}
		}
	}
	if (Reject)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT AttackOrder REJECT %s Unit=%u:%u Target=%u:%u"),
		       Reject, Handle.Index, Handle.Serial, TargetHandle.Index, TargetHandle.Serial);
		return false;
	}
	// Identical re-issue is a no-op (M11 pattern): no serial bump, no churn.
	const FUnitOrder& Prev = State->CurrentOrder;
	if (Prev.Type == FUnitOrder::EType::AttackUnit && Prev.TargetUnit == TargetHandle)
	{
		return true;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order = FUnitOrder();
	Order.Type = FUnitOrder::EType::AttackUnit;
	Order.TargetUnit = TargetHandle;
	++State->OrderSerial;
	return true;
}

bool UUnitRegistrySubsystem::IssueAttackMoveOrder(FUnitHandle Handle, const FVector2D& AnchorPosCm, float FacingYawDeg,
	float CommandedSpeedMps, FUnitHandle TargetHandle)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	FUnitRuntimeState* State = Handle.IsValid() ? Units.Find(Handle) : nullptr;
	const TCHAR* Reject = nullptr;
	if (!State)
	{
		Reject = TEXT("BadHandle");
	}
	else if (State->CurrentOrder.Type == FUnitOrder::EType::Garrison)
	{
		Reject = TEXT("AttackerGarrisonOrder");
	}
	else if (TargetHandle.IsValid() && Units.Find(TargetHandle) &&
		Units.Find(TargetHandle)->CurrentOrder.Type == FUnitOrder::EType::Garrison)
	{
		Reject = TEXT("TargetGarrisonOrder");
	}
	if (!Reject && State)
	{
		if (UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr)
		{
			if (Garrison->IsUnitCommittedToGarrison(Handle) ||
				(TargetHandle.IsValid() && Garrison->IsUnitCommittedToGarrison(TargetHandle)))
			{
				Reject = TEXT("GarrisonCommitted");
			}
		}
	}
	if (Reject)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT AttackMove REJECT %s Unit=%u:%u Target=%u:%u"),
		       Reject, Handle.Index, Handle.Serial, TargetHandle.Index, TargetHandle.Serial);
		return false;
	}
	// Identical re-issue is a no-op (M11 pattern).
	const FUnitOrder& Prev = State->CurrentOrder;
	if (Prev.Type == FUnitOrder::EType::AttackMove
		&& FVector2D::DistSquared(Prev.AnchorPos, AnchorPosCm) <= FMath::Square(100.f)
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(Prev.FacingYaw, FacingYawDeg)) < 1.f
		&& Prev.CommandedSpeed == CommandedSpeedMps
		&& Prev.TargetUnit == TargetHandle)
	{
		return true;
	}
	FUnitOrder& Order = State->CurrentOrder;
	Order = FUnitOrder();
	Order.Type = FUnitOrder::EType::AttackMove;
	Order.AnchorPos = AnchorPosCm;
	Order.FacingYaw = FacingYawDeg;
	Order.CommandedSpeed = CommandedSpeedMps;
	Order.TargetUnit = TargetHandle;
	++State->OrderSerial;
	return true;
}

// -- Phase 2 engagement records + scratch (spec §3.2) --

FEngagementRecord* UUnitRegistrySubsystem::CreateOrRefreshEngagement(FUnitHandle Attacker, FUnitHandle Defender)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (!Attacker.IsValid() || !Defender.IsValid())
	{
		return nullptr;
	}
	if (Engagements.Num() >= MaxEngagementRecords && !Engagements.Contains({Attacker, Defender}))
	{
		return nullptr; // store full: the cluster pass treats this as a cap hit
	}
	FEngagementRecord& Record = Engagements.FindOrAdd({Attacker, Defender});
	Record.bDead = 0; // a refresh revives a dead-flagged record (re-contact)
	return &Record;
}

const FEngagementRecord* UUnitRegistrySubsystem::FindEngagement(FUnitHandle Attacker, FUnitHandle Defender) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return Engagements.Find({Attacker, Defender});
}

FEngagementRecord* UUnitRegistrySubsystem::FindEngagementMutable(FUnitHandle Attacker, FUnitHandle Defender)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	return Engagements.Find({Attacker, Defender});
}

int32 UUnitRegistrySubsystem::FlagUnitEngagementsDead(FUnitHandle Unit)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	int32 Flagged = 0;
	for (TPair<FEngagementKey, FEngagementRecord>& Pair : Engagements)
	{
		if (Pair.Key.Attacker == Unit || Pair.Key.Defender == Unit)
		{
			if (Pair.Value.bDead == 0)
			{
				Pair.Value.bDead = 1;
				++Flagged;
			}
		}
	}
	return Flagged;
}

int32 UUnitRegistrySubsystem::PruneDeadEngagements()
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	int32 Removed = 0;
	for (auto It = Engagements.CreateIterator(); It; ++It)
	{
		if (It->Value.bDead != 0)
		{
			It.RemoveCurrent();
			++Removed;
		}
	}
	return Removed;
}

int32 UUnitRegistrySubsystem::FlagAllEngagementsDead()
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	int32 Flagged = 0;
	for (TPair<FEngagementKey, FEngagementRecord>& Pair : Engagements)
	{
		if (Pair.Value.bDead == 0)
		{
			Pair.Value.bDead = 1;
			++Flagged;
		}
	}
	return Flagged;
}

void UUnitRegistrySubsystem::SetEngagedCount(FUnitHandle Handle, int32 Count)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (FUnitRuntimeState* State = Units.Find(Handle))
	{
		State->EngagedCount = Count;
	}
}

int32 UUnitRegistrySubsystem::GetEngagedCount(FUnitHandle Handle) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	const FUnitRuntimeState* State = Units.Find(Handle);
	return State ? State->EngagedCount : 0;
}

TArray<FEngagementKey> UUnitRegistrySubsystem::GetSortedEngagementKeys() const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	TArray<FEngagementKey> Keys;
	Keys.Reserve(Engagements.Num());
	for (const TPair<FEngagementKey, FEngagementRecord>& Pair : Engagements)
	{
		Keys.Add(Pair.Key);
	}
	// Deterministic order: attacker index, then defender index (serials
	// tie-break; slots never share an index among live units).
	Keys.Sort([](const FEngagementKey& A, const FEngagementKey& B)
	{
		if (A.Attacker.Index != B.Attacker.Index) { return A.Attacker.Index < B.Attacker.Index; }
		if (A.Defender.Index != B.Defender.Index) { return A.Defender.Index < B.Defender.Index; }
		if (A.Attacker.Serial != B.Attacker.Serial) { return A.Attacker.Serial < B.Attacker.Serial; }
		return A.Defender.Serial < B.Defender.Serial;
	});
	return Keys;
}

void UUnitRegistrySubsystem::NotifyCombatEntityDeath(FMassEntityHandle Entity)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	NotifyCombatEntityDeathInternal(Entity);
}

void UUnitRegistrySubsystem::NotifyCombatEntityDeathInternal(FMassEntityHandle Entity)
{
	// The tally/coverage passes Reset() per tick and validate handles
	// top-of-pass, so a zeroed slot is all the cleanup a corpse needs
	// (spec §3.2: everything derived fresh each tick).
	for (int32 i = 0; i < TallyScratch.Keys.Num(); ++i)
	{
		if (TallyScratch.Keys[i] == Entity)
		{
			TallyScratch.Keys[i] = FMassEntityHandle();
			TallyScratch.Counts[i] = 0;
		}
	}
	for (int32 i = 0; i < CoverageScratch.Keys.Num(); ++i)
	{
		if (CoverageScratch.Keys[i] == Entity)
		{
			CoverageScratch.Keys[i] = FMassEntityHandle();
			CoverageScratch.Values[i] = 0.f;
			CoverageScratch.Stamps[i] = 0u;
		}
	}
}

void UUnitRegistrySubsystem::ResetBattleState()
{
	// Destroy the live rosters first (the actor teardown path does this for
	// actor-backed units; registry-only units need the explicit pass).
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	if (UMassEntitySubsystem* Entities = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		FMassEntityManager& EntityManager = Entities->GetMutableEntityManager();
		if (!EntityManager.IsProcessing())
		{
			for (const TPair<FUnitHandle, FUnitRuntimeState>& Pair : Units)
			{
				for (const FMassEntityHandle& Entity : Pair.Value.Roster)
				{
					if (EntityManager.IsEntityValid(Entity))
					{
						EntityManager.Defer().DestroyEntity(Entity);
					}
				}
			}
			EntityManager.FlushCommands();
		}
	}
	Units.Reset();
	SpecsByIndex.Reset();
	NextGenerationByIndex.Reset();
	FreeIndices.Reset();
	NextIndex = 1;
	DirtyStats.Reset();
	Engagements.Reset();
	Engagements.Reserve(MaxEngagementRecords);
	TallyScratch.Reset();
	CoverageScratch.Reset();
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT ResetBattleState ok"));
}

// -- Phase 2 save/load skeleton (A8, spec §3.5) --

void UUnitRegistrySubsystem::CaptureBattleState(FBattleSaveData& OutData) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	OutData = FBattleSaveData();
	OutData.SchemaVersion = 2;

	// Units in sorted-handle order (the pass the hash folds).
	TArray<FUnitHandle> Handles;
	Handles.Reserve(Units.Num());
	for (const TPair<FUnitHandle, FUnitRuntimeState>& Pair : Units)
	{
		Handles.Add(Pair.Key);
	}
	Handles.Sort([](const FUnitHandle& A, const FUnitHandle& B) { return A.Index < B.Index; });

	for (const FUnitHandle& Handle : Handles)
	{
		const FUnitRuntimeState* State = Units.Find(Handle);
		if (!State)
		{
			continue;
		}
		const FUnitSpawnSpec* Spec = SpecsByIndex.Find(Handle.Index);
		FUnitSaveEntry& Entry = OutData.Units.AddDefaulted_GetRef();
		Entry.Index = Handle.Index;
		Entry.Serial = Handle.Serial;
		Entry.Generation = State->Generation;
		Entry.UnitName = Spec ? Spec->UnitName : FString();
		Entry.EduRowIndex = Spec ? Spec->EduRowIndex : INDEX_NONE;
		Entry.SoldierCount = Spec ? Spec->SoldierCount : 0;
		Entry.FormationSpacing = Spec ? Spec->FormationSpacing : 1.2f;
		Entry.SpawnPos = Spec ? Spec->SpawnPos : FVector2D::ZeroVector;
		Entry.SpawnYaw = Spec ? Spec->SpawnYaw : 0.f;
		Entry.NumRanks = Spec ? Spec->NumRanks : 2;
		Entry.Roster = State->Roster;
		const FBattleAttributeSnapshot& Attrs = State->CachedAttributes;
		Entry.Morale = Attrs.Morale;
		Entry.Fatigue = Attrs.Fatigue;
		Entry.Experience = Attrs.Experience;
		Entry.ArmourUpgrade = Attrs.ArmourUpgrade;
		Entry.MeleeAttackMod = Attrs.MeleeAttackMod;
		Entry.MeleeDefenseMod = Attrs.MeleeDefenseMod;
		Entry.RangedAttackMod = Attrs.RangedAttackMod;
		Entry.SpeedMod = Attrs.SpeedMod;
		Entry.BlockChanceMod = Attrs.BlockChanceMod;
		Entry.OrderLatencyAttr = Attrs.OrderLatency;
		Entry.AmmoPool = Attrs.AmmoPool;
		const FUnitOrder& Order = State->CurrentOrder;
		Entry.OrderType = static_cast<uint8>(Order.Type);
		Entry.OrderAnchorPos = Order.AnchorPos;
		Entry.OrderFacingYaw = Order.FacingYaw;
		Entry.OrderCommandedSpeed = Order.CommandedSpeed;
		Entry.OrderCommandedFiles = Order.CommandedFiles;
		Entry.OrderStructureId = Order.StructureId;
		Entry.OrderTargetUnitIndex = Order.TargetUnit.Index;
		Entry.OrderTargetUnitSerial = Order.TargetUnit.Serial;
		Entry.OrderSerial = State->OrderSerial;
		const FUnitOrder& Pending = State->PendingAfterEgress;
		Entry.PendingOrderType = static_cast<uint8>(Pending.Type);
		Entry.PendingAnchorPos = Pending.AnchorPos;
		Entry.PendingFacingYaw = Pending.FacingYaw;
		Entry.PendingCommandedSpeed = Pending.CommandedSpeed;
		Entry.PendingCommandedFiles = Pending.CommandedFiles;
		Entry.PendingStructureId = Pending.StructureId;
		Entry.PendingTargetUnitIndex = Pending.TargetUnit.Index;
		Entry.PendingTargetUnitSerial = Pending.TargetUnit.Serial;
		// v2 (spec ranged-stack §3.5): toggles + fraction denominators.
		Entry.bFireAtWill = State->bFireAtWill ? 1 : 0;
		Entry.bSkirmish = State->bSkirmish ? 1 : 0;
		Entry.bOutOfAmmo = State->bOutOfAmmo ? 1 : 0;
		Entry.RangedInitialAmmo = State->RangedInitialAmmo;
		Entry.InitialRosterSize = State->InitialRosterSize;
	}

	// Slot bookkeeping the round-trip must reproduce (free-list depth folds
	// into the bookkeeping hash). Flat key/value pairs in map order -- the
	// restore rebuilds a map, so order is irrelevant to equality.
	OutData.FreeIndices = FreeIndices;
	OutData.FreeSlotGenerationCounters.Reserve(NextGenerationByIndex.Num() * 2);
	for (const TPair<uint32, int32>& Pair : NextGenerationByIndex)
	{
		OutData.FreeSlotGenerationCounters.Add(Pair.Key);
		OutData.FreeSlotGenerationCounters.Add(Pair.Value);
	}
	OutData.NextIndexValue = NextIndex;

	// Engagement records (sorted keys; AssignedAttackers excluded -- derived
	// fresh each tick, spec §3.2).
	for (const FEngagementKey& Key : GetSortedEngagementKeys())
	{
		const FEngagementRecord* Record = Engagements.Find(Key);
		if (!Record)
		{
			continue;
		}
		FEngagementSaveEntry& Entry = OutData.Engagements.AddDefaulted_GetRef();
		Entry.AttackerIndex = Key.Attacker.Index;
		Entry.AttackerSerial = Key.Attacker.Serial;
		Entry.DefenderIndex = Key.Defender.Index;
		Entry.DefenderSerial = Key.Defender.Serial;
		Entry.Mode = static_cast<uint8>(Record->Mode);
		Entry.FrontageStart = Record->FrontageStart;
		Entry.FrontageEnd = Record->FrontageEnd;
		Entry.DesiredPressure = Record->DesiredPressure;
		Entry.bDead = Record->bDead;
	}

	// RNG stream states (seed-registered per B.5).
	if (UBattleRngSubsystem* Rng = GetWorld() ? GetWorld()->GetSubsystem<UBattleRngSubsystem>() : nullptr)
	{
		OutData.RngRootSeed = Rng->GetRegistry().GetRootSeed();
		for (const FBattleRngStreamState& StreamState : Rng->GetRegistry().SnapshotStreams())
		{
			FRngStreamSaveEntry& Entry = OutData.RngStreams.AddDefaulted_GetRef();
			Entry.StreamName = StreamState.Name;
			Entry.CurrentSeed = StreamState.CurrentSeed;
		}
	}
}

bool UUnitRegistrySubsystem::RestoreBattleState(const FBattleSaveData& InData)
{
	// v2 (spec ranged-stack §3.5) restores the ranged toggles + denominators;
	// v1 (legacy) restores to the defaults. Anything else rejects.
	if (InData.SchemaVersion != 1 && InData.SchemaVersion != 2)
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT SaveProbe FAIL schema mismatch (have %u, want 1 or 2)"), InData.SchemaVersion);
		return false;
	}
	const bool bV2 = InData.SchemaVersion >= 2;
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	Units.Reset();
	SpecsByIndex.Reset();
	DirtyStats.Reset();
	Engagements.Reset();
	Engagements.Reserve(MaxEngagementRecords);

	for (const FUnitSaveEntry& Entry : InData.Units)
	{
		FUnitHandle Handle;
		Handle.Index = Entry.Index;
		Handle.Serial = Entry.Serial;
		FUnitRuntimeState& State = Units.Add(Handle);
		State.Generation = Entry.Generation;
		State.Roster = Entry.Roster;
		State.OrderSerial = Entry.OrderSerial;
		FBattleAttributeSnapshot& Attrs = State.CachedAttributes;
		Attrs.Morale = Entry.Morale;
		Attrs.Fatigue = Entry.Fatigue;
		Attrs.Experience = Entry.Experience;
		Attrs.ArmourUpgrade = Entry.ArmourUpgrade;
		Attrs.MeleeAttackMod = Entry.MeleeAttackMod;
		Attrs.MeleeDefenseMod = Entry.MeleeDefenseMod;
		Attrs.RangedAttackMod = Entry.RangedAttackMod;
		Attrs.SpeedMod = Entry.SpeedMod;
		Attrs.BlockChanceMod = Entry.BlockChanceMod;
		Attrs.OrderLatency = Entry.OrderLatencyAttr;
		Attrs.AmmoPool = Entry.AmmoPool;
		FUnitOrder& Order = State.CurrentOrder;
		Order.Type = static_cast<FUnitOrder::EType>(Entry.OrderType);
		Order.AnchorPos = Entry.OrderAnchorPos;
		Order.FacingYaw = Entry.OrderFacingYaw;
		Order.CommandedSpeed = Entry.OrderCommandedSpeed;
		Order.CommandedFiles = Entry.OrderCommandedFiles;
		Order.StructureId = Entry.OrderStructureId;
		Order.TargetUnit.Index = Entry.OrderTargetUnitIndex;
		Order.TargetUnit.Serial = Entry.OrderTargetUnitSerial;
		FUnitOrder& Pending = State.PendingAfterEgress;
		Pending.Type = static_cast<FUnitOrder::EType>(Entry.PendingOrderType);
		Pending.AnchorPos = Entry.PendingAnchorPos;
		Pending.FacingYaw = Entry.PendingFacingYaw;
		Pending.CommandedSpeed = Entry.PendingCommandedSpeed;
		Pending.CommandedFiles = Entry.PendingCommandedFiles;
		Pending.StructureId = Entry.PendingStructureId;
		Pending.TargetUnit.Index = Entry.PendingTargetUnitIndex;
		Pending.TargetUnit.Serial = Entry.PendingTargetUnitSerial;
		// v2 ranged state (spec ranged-stack §3.5); a v1 payload takes the
		// defaults (toggles on/off/off, denominators 0 -- no v1 save can
		// contain a ranged row, the fixture predates stat_sec).
		State.bFireAtWill = !bV2 || Entry.bFireAtWill != 0;
		State.bSkirmish = bV2 && Entry.bSkirmish != 0;
		State.bOutOfAmmo = bV2 && Entry.bOutOfAmmo != 0;
		State.RangedInitialAmmo = bV2 ? Entry.RangedInitialAmmo : 0;
		State.InitialRosterSize = bV2 ? Entry.InitialRosterSize : 0;

		FUnitSpawnSpec& Spec = SpecsByIndex.Add(Handle.Index);
		Spec.EduRowIndex = Entry.EduRowIndex;
		Spec.SoldierCount = Entry.SoldierCount;
		Spec.UnitName = Entry.UnitName;
		Spec.FormationSpacing = Entry.FormationSpacing;
		Spec.SpawnPos = Entry.SpawnPos;
		Spec.SpawnYaw = Entry.SpawnYaw;
		Spec.NumRanks = Entry.NumRanks;
	}

	FreeIndices = InData.FreeIndices;
	NextGenerationByIndex.Reset();
	for (int32 i = 0; i + 1 < InData.FreeSlotGenerationCounters.Num(); i += 2)
	{
		NextGenerationByIndex.Add(InData.FreeSlotGenerationCounters[i], InData.FreeSlotGenerationCounters[i + 1]);
	}
	NextIndex = InData.NextIndexValue;

	for (const FEngagementSaveEntry& Entry : InData.Engagements)
	{
		FEngagementKey Key;
		Key.Attacker.Index = Entry.AttackerIndex;
		Key.Attacker.Serial = Entry.AttackerSerial;
		Key.Defender.Index = Entry.DefenderIndex;
		Key.Defender.Serial = Entry.DefenderSerial;
		FEngagementRecord& Record = Engagements.Add(Key);
		Record.Mode = static_cast<EEngagementMode>(Entry.Mode);
		Record.FrontageStart = Entry.FrontageStart;
		Record.FrontageEnd = Entry.FrontageEnd;
		Record.DesiredPressure = Entry.DesiredPressure;
		Record.bDead = Entry.bDead;
		Record.AssignedAttackers = 0; // fresh tallied each tick, never persisted
	}

	if (UBattleRngSubsystem* Rng = GetWorld() ? GetWorld()->GetSubsystem<UBattleRngSubsystem>() : nullptr)
	{
		TArray<FBattleRngStreamState> StreamStates;
		for (const FRngStreamSaveEntry& Entry : InData.RngStreams)
		{
			FBattleRngStreamState StreamState;
			StreamState.Name = Entry.StreamName;
			StreamState.CurrentSeed = Entry.CurrentSeed;
			StreamStates.Add(StreamState);
		}
		Rng->GetMutableRegistry().RestoreStreams(InData.RngRootSeed, StreamStates);
	}

	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT SaveProbe Restore Units=%d Engagements=%d RngStreams=%d Schema=%u"),
	       Units.Num(), Engagements.Num(), InData.RngStreams.Num(), InData.SchemaVersion);
	return true;
}
