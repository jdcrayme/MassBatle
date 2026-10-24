import io

# BattleDeathPath.h: back to observer processor (regular tag supports observers)
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Public\BattleDeathPath.h"
s = io.open(p, encoding="utf-8").read()

old_head = s[s.find("// BattleDeathPath.h -- the up-path"):s.find("#pragma once")]
new_head = """// BattleDeathPath.h -- the up-path (arch §5.2, §4 row 11).
//
// Final shape after the ADR-15 on-machine probe (Phase 0 Task 9, audit
// C10/C11): FDeadTag is a REGULAR tag (5.8 sparse tags are chunk-granular and
// observer-incompatible -- see BattleFragments.h), so the death notification
// is a true observer that fires DURING the kill flush (the E5 audited point):
//
//   kill: Defer(AddTag<FDeadTag>) + flush
//   -> UBattleDeathObserver (observes FDeadTag AddElement at flush): defers
//      the death signal
//   -> UBattleDeathSignalReceiver (game thread, signal-driven): roster update
//      + GAS event -> Defer(DestroyEntity) AFTER the signal (corpse policy is
//      Phase 5; the receiver runs while the entity is still alive).
"""
s = new_head + s[s.find("#pragma once"):]

old_cls = """/** Pipeline row 11 (death scan): per-tick discovery of newly-dead entities
 *  (sparse FDeadTag present, not yet handled). Defers the death signal and
 *  marks them handled. Runs PrePhysics so the signal lands the same frame and
 *  the destroy (deferred by the receiver) flushes at that phase's end. */
UCLASS()
class MASSBATTLE_API UBattleDeathObserver : public UMassProcessor
{
	GENERATED_BODY()

public:
	UBattleDeathObserver(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery DeathScanQuery;
};"""
new_cls = """/** Pipeline row 11 (death): observes FDeadTag (regular tag) AddElement at the
 *  kill flush, defers the death signal. The destroy is deferred by the
 *  receiver so the entity is still alive when the signal is consumed. */
UCLASS()
class MASSBATTLE_API UBattleDeathObserver : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UBattleDeathObserver(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};"""
assert old_cls in s, "observer class block not found"
s = s.replace(old_cls, new_cls)
s = s.replace('#include "MassProcessor.h"', '#include "MassObserverProcessor.h"')
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

# BattleDeathPath.cpp: restore observer implementation
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleDeathPath.cpp"
s = io.open(p, encoding="utf-8").read()
start = s.find("// ------------------------------------------------------------------ death scan")
end = s.find("// ------------------------------------------------------------------ receiver")
assert start != -1 and end != -1
observer_new = """// ------------------------------------------------------------------ observer

UBattleDeathObserver::UBattleDeathObserver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 5.8 observer registration (audit §4): ObservedTypes + ObservedOperations.
	// Works because FDeadTag is a REGULAR tag (ADR-15 probe: sparse elements
	// are observer-incompatible AND chunk-granular -- audit C10/C11).
	ObservedTypes = { FDeadTag::StaticStruct() };
	ObservedOperations = EMassObservedOperationFlags::AddElement;

	bAutoRegisterWithProcessingPhases = false; // observers are registry-driven, not per-tick
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true;
}

void UBattleDeathObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// No per-tick query: observers execute on flush notifications (E5 audited
	// point: DURING the command-buffer flush that carried the deferred add).
}

void UBattleDeathObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Context.GetWorld());
	for (int32 i = 0; i < Context.GetNumEntities(); ++i)
	{
		const FMassEntityHandle Entity = Context.GetEntity(i);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathObserver Entity=%u:%u -> signal"), Entity.Index, Entity.SerialNumber);
		if (SignalSubsystem)
		{
			SignalSubsystem->SignalEntityDeferred(Context, UE::Mass::Signals::BattleUnitDeath(), Entity);
		}
		// NOTE: the destroy is deferred by the RECEIVER (after roster/GAS work)
		// so the entity is still alive when the signal is consumed next frame.
	}
}

"""
s = s[:start] + observer_new + s[end:]
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("observer restored")

# Registry: drop the now-unused death-handled set
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Public\BattleRegistry.h"
s = io.open(p, encoding="utf-8").read()
old = """
	/** DeathScan dedup: has this entity's death been signaled already?
	 *  (Sparse tag persists until destroy; without the handled set the scan
	 *  would re-signal every tick. Entries are bounded by deaths per battle.) */
	void MarkDeathHandled(FMassEntityHandle Entity);
	bool IsDeathHandled(FMassEntityHandle Entity) const;"""
assert old in s
s = s.replace(old, "")
old = """
	/** Entities whose death has been signaled (DeathScan dedup). */
	TSet<FMassEntityHandle> HandledDeaths;"""
assert old in s
s = s.replace(old, "")
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleRegistry.cpp"
s = io.open(p, encoding="utf-8").read()
old = """void UUnitRegistrySubsystem::MarkDeathHandled(FMassEntityHandle Entity)
{
	UE_MT_SCOPED_WRITE_ACCESS(AccessDetector);
	HandledDeaths.Add(Entity);
}

bool UUnitRegistrySubsystem::IsDeathHandled(FMassEntityHandle Entity) const
{
	UE_MT_SCOPED_READ_ACCESS(AccessDetector);
	return HandledDeaths.Contains(Entity);
}

"""
assert old in s
s = s.replace(old, "")
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("registry cleaned")
