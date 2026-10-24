import io

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleDeathPath.cpp"
s = io.open(p, encoding="utf-8").read()

old = """// ------------------------------------------------------------------ observer

UBattleDeathObserver::UBattleDeathObserver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 5.8 observer registration (audit §4): ObservedTypes + ObservedOperations;
	// the single ObservedType/Operation pair is deprecated.
	ObservedTypes = { FDeadTag::StaticStruct() };
	ObservedOperations = EMassObservedOperationFlags::AddElement;

	bAutoRegisterWithProcessingPhases = false; // observers are registry-driven, not per-tick
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true;
}

void UBattleDeathObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// No per-tick query: observers execute on flush notifications with the
	// affected entities in the execution context (FMassObserverExecutionContext
	// via Context.GetAuxData()).
}

void UBattleDeathObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Flush contract (audit §3 / review E5): observers fire DURING the command
	// buffer flush that carried the deferred FDeadTag add. The signal is
	// deferred from here (batched observer semantics, 5.7+); the destroy is
	// queued after the signal so every consumer sees a tagged entity first.
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Context.GetWorld());
	for (int32 i = 0; i < Context.GetNumEntities(); ++i)
	{
		const FMassEntityHandle Entity = Context.GetEntity(i);
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathObserver Entity=%u:%u"), Entity.Index, Entity.SerialNumber);
		if (SignalSubsystem)
		{
			SignalSubsystem->SignalEntityDeferred(Context, UE::Mass::Signals::BattleUnitDeath(), Entity);
		}
		Context.Defer().DestroyEntity(Entity);
	}
}"""

new = """// ------------------------------------------------------------------ death scan

UBattleDeathObserver::UBattleDeathObserver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DeathScanQuery(*this)
{
	// ADR-15 probe finding (audit C10): sparse elements cannot be observed via
	// UMassObserverProcessor in 5.8.4 (registry ensure). This per-tick scan is
	// the sanctioned replacement; the E5 audited point becomes "the next
	// PrePhysics phase after the kill flush".
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry handled-set is game-thread
}

void UBattleDeathObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// Sparse requirement: entities carrying the sparse FDeadTag. Presence All
	// on a sparse tag does NOT change archetypes -- this is the ADR-15 query
	// reading of "removed from live queries" (live queries declare None).
	DeathScanQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::All);
	DeathScanQuery.AddConstSharedRequirement<FUnitStatsSharedFragment>();
	DeathScanQuery.AddSubsystemRequirement<UUnitRegistrySubsystem>(EMassFragmentAccess::ReadOnly);
}

void UBattleDeathObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Context.GetWorld());
	DeathScanQuery.ForEachEntityChunk(Context, [SignalSubsystem](FMassExecutionContext& Context)
	{
		UUnitRegistrySubsystem* Registry = Context.GetWorld()->GetSubsystem<UUnitRegistrySubsystem>();
		if (!Registry || !SignalSubsystem)
		{
			return;
		}
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			if (Registry->IsDeathHandled(Entity))
			{
				continue;
			}
			Registry->MarkDeathHandled(Entity);
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathScan Entity=%u:%u -> signal"), Entity.Index, Entity.SerialNumber);
			// Destroy is deferred by the RECEIVER, after the roster/GAS work,
			// so the receiver's query still sees a live entity.
			SignalSubsystem->SignalEntityDeferred(Context, UE::Mass::Signals::BattleUnitDeath(), Entity);
		}
	});
}"""

assert old in s, "observer block not found"
s = s.replace(old, new)

old2 = """	EntityQuery.ForEachEntityChunk(Context, [Registry](FMassExecutionContext& Context)
	{
		const TConstArrayView<FOwnerUnitFragment> Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FUnitHandle Unit = Owners[i].Owner;
			const int32 RosterIndex = Owners[i].RosterIndex;

			// Registry roster update (authoritative bookkeeping, arch §5.2).
			Registry->HandleUnitDeath(Unit, Context.GetEntity(i), RosterIndex);

			// Up-path completion: gameplay event on the unit actor.
			UWorld* World = Context.GetWorld();
			for (TActorIterator<AUnitActor> It(World); It; ++It)
			{
				if (It->GetUnitHandle() == Unit)
				{
					FGameplayEventData Payload;
					Payload.EventTag = UnitDiedTag();
					Payload.Target = *It;
					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(*It, UnitDiedTag(), Payload);
					UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathSignal Unit=%u:%u RosterIndex=%d GAS=1"),
					       Unit.Index, Unit.Serial, RosterIndex);
					break;
				}
			}
		}
	});"""

new2 = """	EntityQuery.ForEachEntityChunk(Context, [Registry](FMassExecutionContext& Context)
	{
		const TConstArrayView<FOwnerUnitFragment> Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			const FUnitHandle Unit = Owners[i].Owner;
			const int32 RosterIndex = Owners[i].RosterIndex;

			// Registry roster update (authoritative bookkeeping, arch §5.2).
			Registry->HandleUnitDeath(Unit, Entity, RosterIndex);

			// Up-path completion: gameplay event on the unit actor.
			UWorld* World = Context.GetWorld();
			bool bGasSent = false;
			for (TActorIterator<AUnitActor> It(World); It; ++It)
			{
				if (It->GetUnitHandle() == Unit)
				{
					FGameplayEventData Payload;
					Payload.EventTag = UnitDiedTag();
					Payload.Target = *It;
					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(*It, UnitDiedTag(), Payload);
					bGasSent = true;
					break;
				}
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathSignal Entity=%u:%u Unit=%u:%u RosterIndex=%d GAS=%d"),
			       Entity.Index, Entity.SerialNumber, Unit.Index, Unit.Serial, RosterIndex, bGasSent ? 1 : 0);

			// Destroy AFTER the signal (corpse policy is Phase 5); the receiver
			// runs while the entity is still alive.
			Context.Defer().DestroyEntity(Entity);
		}
	});"""

assert old2 in s, "receiver block not found"
s = s.replace(old2, new2)

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("death path redesigned")
