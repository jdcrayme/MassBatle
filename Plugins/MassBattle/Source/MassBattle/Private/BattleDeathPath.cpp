#include "BattleDeathPath.h"
#include "BattleDebugSubsystem.h"
#include "MassCommonFragments.h"
#include "BattleFragments.h"
#include "BattleGameplayTags.h"
#include "BattleMorale.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommands.h"
#include "MassEntityView.h"
#include "MassSignalSubsystem.h"
#include "UnitActor.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayTagContainer.h"
#include "EngineUtils.h"

// ------------------------------------------------------------------ observer

UBattleDeathObserver::UBattleDeathObserver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EntitySignalQuery(*this)
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

void UBattleDeathObserver::Register()
{
	Super::Register();
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathObserver Registered Types=%d Ops=%d"),
	       ObservedTypes.Num(), static_cast<int32>(ObservedOperations));
}

void UBattleDeathObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// The notification itself carries no entities (5.8: aux data is type-level
	// only) -- Execute runs this query over all currently-tagged entities.
	// FDeadSignaledTag None excludes entities whose signal was already emitted
	// in an earlier flush of the same frame: without it, a second kill's flush
	// would re-signal every dead-but-not-yet-destroyed entity (audit H3 --
	// double GAS events + double deferred destroys on multi-kill frames).
	EntitySignalQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::All);
	EntitySignalQuery.AddTagRequirement<FDeadSignaledTag>(EMassFragmentPresence::None);
	EntitySignalQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	EntitySignalQuery.AddSubsystemRequirement<UUnitRegistrySubsystem>(EMassFragmentAccess::ReadOnly);
}

void UBattleDeathObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassSignalSubsystem* SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(Context.GetWorld());
	int32 Signaled = 0;
	EntitySignalQuery.ForEachEntityChunk(Context, [SignalSubsystem, &Signaled](FMassExecutionContext& Context)
	{
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			UE_LOG(LogMassBattle, VeryVerbose, TEXT("DeathObserver Entity=%u:%u -> signal"),
			       Entity.Index, Entity.SerialNumber);
			if (SignalSubsystem)
			{
				SignalSubsystem->SignalEntityDeferred(Context, UE::Mass::Signals::BattleUnitDeath(), Entity);
				// Idempotence marker (audit H3): consumed by this query's None
				// requirement so each entity is signaled exactly once. The tag
				// goes to the grave with the deferred destroy.
				Context.Defer().AddTag<FDeadSignaledTag>(Entity);
			}
			++Signaled;
		}
	});
	if (Signaled > 0)
	{
		UE_LOG(LogMassBattle, Verbose, TEXT("EXEC_RESULT DeathObserver Signaled=%d"), Signaled);
	}
	// NOTE: the destroy is deferred by the RECEIVER (after roster/GAS work) so
	// the entity is still alive when the signal is consumed next frame.
}

// ------------------------------------------------------------------ receiver

UBattleDeathSignalReceiver::UBattleDeathSignalReceiver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoRegisterWithProcessingPhases = true; // signal processors run per frame over received signals
	SetProcessingPhase(EMassProcessingPhase::PostPhysics); // after sim writes, still same frame
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true;
}

void UBattleDeathSignalReceiver::InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& EntityManager)
{
	Super::InitializeInternal(Owner, EntityManager);
	if (UWorld* World = Owner.GetWorld())
	{
		if (UMassSignalSubsystem* SignalSubsystem = World->GetSubsystem<UMassSignalSubsystem>())
		{
			SubscribeToSignal(*SignalSubsystem, UE::Mass::Signals::BattleUnitDeath());
		}
	}
}

void UBattleDeathSignalReceiver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// Driven by EntitySignals in SignalEntities, not by composition.
	EntityQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
		// Phase 4a (spec morale-and-gas section 4.7): the officer branch reads
		// bOfficer from the base-stats fragment view (the chunk-idiomatic read;
		// the entity is still alive at signal time).
		EntityQuery.AddRequirement<FBaseStatsFragment>(EMassFragmentAccess::ReadOnly);
}

void UBattleDeathSignalReceiver::SignalEntities(FMassEntityManager& EntityManager, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	UUnitRegistrySubsystem* Registry = Context.GetWorld()->GetSubsystem<UUnitRegistrySubsystem>();
	if (!Registry)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [Registry](FMassExecutionContext& Context)
	{
		const TConstArrayView<FOwnerUnitFragment> Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		const TConstArrayView<FTransformFragment> Transforms = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FBaseStatsFragment> BaseStats = Context.GetFragmentView<FBaseStatsFragment>();
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			const FUnitHandle Unit = Owners[i].Owner;
			const int32 RosterIndex = Owners[i].RosterIndex;

			// Task 10 corpse ledger (the MassRTS-style ground rectangles): the
			// LAST transform, captured here while the entity is still alive --
			// the destroy defers right after this signal. Diagnostic state
			// only, never sim-authoritative.
			if (UBattleDebugSubsystem* Debug = Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>())
			{
				const FVector Pos = Transforms[i].GetTransform().GetTranslation();
				const float Yaw = Transforms[i].GetTransform().Rotator().Yaw;
				Debug->RecordCorpse(FVector(Pos.X, Pos.Y, 0.f), Yaw);
			}

			// Registry roster update (authoritative bookkeeping, arch §5.2).
			// RemoveSingle compacts the roster, so survivors after the removed
			// slot shift left; their FOwnerUnitFragment.RosterIndex is restamped
			// at the flush below (audit M5 -- the roster array is the live
			// authority and the fragment indices must follow it).
			Registry->HandleUnitDeath(Unit, Entity, RosterIndex);

			// PushCommand forwards the lambda into the command's FExecFunction
			// list; it runs at this buffer's flush (after the roster update).
			Context.Defer().PushCommand<FMassDeferredCreateCommand>([Registry, Unit](FMassEntityManager& EntityManager)
			{
				if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Unit))
				{
					for (int32 RosterIdx = 0; RosterIdx < Roster->Num(); ++RosterIdx)
					{
						const FMassEntityView View(EntityManager, (*Roster)[RosterIdx]);
						if (FOwnerUnitFragment* OwnerFrag = View.GetFragmentDataPtr<FOwnerUnitFragment>())
						{
							OwnerFrag->RosterIndex = RosterIdx;
						}
					}
				}
			});

			// Up-path completion: gameplay event on the unit actor.
			UWorld* World = Context.GetWorld();
			bool bGasSent = false;
			AUnitActor* UnitActor = nullptr;
			for (TActorIterator<AUnitActor> It(World); It; ++It)
			{
				if (It->GetUnitHandle() == Unit)
				{
					const FGameplayTag& DiedTag = TAG_Battle_Unit_Died.GetTag();
					ensureMsgf(DiedTag.IsValid(), TEXT("Battle.Unit.Died native tag must be registered (BattleGameplayTags.cpp)"));
					FGameplayEventData Payload;
					Payload.EventTag = DiedTag;
					Payload.Target = *It;
					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(*It, DiedTag, Payload);
					bGasSent = true;
					UnitActor = *It;
					break;
				}
			}

			// Phase 4a (spec morale-and-gas §4.7, MC8): the officer branch.
			// Reads the entity's bOfficer fragment value -- the entity is still
			// alive here (the destroy defers below) and the fragment is the
			// identity, immune to the roster compaction inside HandleUnitDeath.
			// Gated on Battle.Morale.Enable: flags-off stays byte-identical
			// (no spike, no events, no bookkeeping -- the R-R8 pattern).
			if (FMoraleCVars::Enable())
			{
				const bool bOfficer = BaseStats.IsValidIndex(i) && BaseStats[i].bOfficer != 0;
				// (a) the fresh-casualty spike: every death accumulates, officers
				//     amplified (the decay runs in the morale pass, PR-12).
				Registry->NotifyMoraleCasualty(Unit, bOfficer);
				if (bOfficer && UnitActor)
				{
					// (b) Battle.Unit.OfficerDied on the owning unit's ASC.
					const FGameplayTag& OfficerTag = TAG_Battle_Unit_OfficerDied.GetTag();
					ensureMsgf(OfficerTag.IsValid(), TEXT("Battle.Unit.OfficerDied native tag must be registered (BattleGameplayTags.cpp)"));
					FGameplayEventData OfficerPayload;
					OfficerPayload.EventTag = OfficerTag;
					OfficerPayload.Target = UnitActor;
					OfficerPayload.EventMagnitude = static_cast<float>(RosterIndex);
					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(UnitActor, OfficerTag, OfficerPayload);
				}
				// (c) the general: team shock + record invalidation + the
				//     team-wide event fan-out.
				uint8 Team = 0;
				if (Registry->NotifyGeneralEntityDeath(Entity, Team))
				{
					const FGameplayTag& GeneralTag = TAG_Battle_Team_GeneralDied.GetTag();
					ensureMsgf(GeneralTag.IsValid(), TEXT("Battle.Team.GeneralDied native tag must be registered (BattleGameplayTags.cpp)"));
					for (TActorIterator<AUnitActor> It(World); It; ++It)
					{
						const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(It->GetUnitHandle());
						if (Spec && Spec->TeamId == Team)
						{
							FGameplayEventData TeamPayload;
							TeamPayload.EventTag = GeneralTag;
							TeamPayload.Target = *It;
							UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(*It, GeneralTag, TeamPayload);
						}
					}
				}
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DeathSignal Entity=%u:%u Unit=%u:%u RosterIndex=%d GAS=%d"),
			       Entity.Index, Entity.SerialNumber, Unit.Index, Unit.Serial, RosterIndex, bGasSent ? 1 : 0);

			// Destroy AFTER the signal (corpse policy is Phase 5); the receiver
			// runs while the entity is still alive.
			Context.Defer().DestroyEntity(Entity);
		}
	});
}
