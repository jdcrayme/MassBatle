#include "UnitSyncProcessor.h"
#include "BattleFragments.h"
#include "BattleRegistry.h"
#include "BattleDebugSubsystem.h"
#include "BattleMorale.h"
#include "UnitActor.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "EngineUtils.h"

UUnitSyncProcessor::UUnitSyncProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SyncQuery(*this) // 5.6+: ctor registration (audit §2; RegisterWithProcessor exists but is not the pattern)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle"); // custom group; engine groups in UE::Mass::ProcessorGroupNames
	// CDO default is Server|Standalone (audit §2) -- editor-world structure probes need EditorWorld:
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry access is game-thread (arch §4 row 0)
}

void UUnitSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	SyncQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	SyncQuery.AddSharedRequirement<FUnitStatsSharedFragment>(EMassFragmentAccess::ReadWrite);
	SyncQuery.AddSubsystemRequirement<UUnitRegistrySubsystem>(EMassFragmentAccess::ReadOnly);
}

void UUnitSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// Pipeline probe (Task 8 diagnostics): first-call + every-1000th-call trace
	// (audit M6: demoted from Display -- Verbose keeps it grep-able when
	// explicitly enabled without per-frame log traffic).
	static int64 ExecuteCount = 0;
	++ExecuteCount;
	if (ExecuteCount == 1 || ExecuteCount % 1000 == 0)
	{
		UE_LOG(LogMassBattle, Verbose, TEXT("EXEC_RESULT UnitSync ExecuteCalls=%lld"), ExecuteCount);
	}

	// B.4 metric: per-frame processor ms into the debug subsystem (Task 12).
	const double SyncStartMs = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
	ON_SCOPE_EXIT
	{
		if (UWorld* World = Context.GetWorld())
		{
			if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
			{
				Debug->RecordUnitSyncMs(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles()) - SyncStartMs);
			}
		}
	};

	int32 ChunksMatched = 0;

	// Phase 4a (A15/D-4): UnitSync is the SINGLE GAS-mirror writer for
	// aggregate attributes -- the AmmoPool mirror moved here from the ranged
	// fire pass (audit D-4). One actor sweep per Execute (the fire pass's
	// per-pass cache pattern); the chunk loop mirrors per unit chunk.
	TMap<FUnitHandle, TWeakObjectPtr<AUnitActor>> ActorsByUnit;
	for (TActorIterator<AUnitActor> It(Context.GetWorld()); It; ++It)
	{
		if (It->GetUnitHandle().IsValid())
		{
			ActorsByUnit.Add(It->GetUnitHandle(), TWeakObjectPtr<AUnitActor>(*It));
		}
	}

	SyncQuery.ForEachEntityChunk(Context, [&ChunksMatched, &ActorsByUnit](FMassExecutionContext& Context)
	{
		UUnitRegistrySubsystem* Registry = Context.GetWorld()->GetSubsystem<UUnitRegistrySubsystem>();
		if (!Registry)
		{
			return;
		}

		FUnitStatsSharedFragment& Stats = Context.GetMutableSharedFragment<FUnitStatsSharedFragment>();
		++ChunksMatched;

		// Audit M6: per-chunk/per-frame logging was a Task 8 diagnostic that
		// formats a string for every chunk every frame -- hot-path spam (and a
		// B.7 "no hot-path allocations" violation) at scale. VeryVerbose keeps
		// the diagnostic available without steady-state cost.
		UE_LOG(LogMassBattle, VeryVerbose, TEXT("UnitSync Chunk anchor=%u:%u dirty=%d melee=%.3f"),
		       Stats.OwnerUnit.Index, Stats.OwnerUnit.Serial,
		       Registry->IsStatsDirty(Stats.OwnerUnit) ? 1 : 0, Stats.MeleeAttackMult);

		// The D-4 mirror (A15): the registry's ammo fraction mirrors to the
		// GAS AmmoPool attribute here (idempotent per chunk; the delegate
		// fires only on value change). Runs for EVERY chunk -- the fragment
		// ammo sum changes without the attribute-snapshot dirty flag once
		// the fire pass stopped mirroring (the single-writer point).
		if (const TWeakObjectPtr<AUnitActor>* Actor = ActorsByUnit.Find(Stats.OwnerUnit))
		{
			if (AUnitActor* Strong = Actor->Get())
			{
				if (UAbilitySystemComponent* ASC = Strong->GetAbilitySystemComponent())
				{
					const float Fraction = Registry->GetUnitAmmoFraction(Stats.OwnerUnit);
					if (Fraction >= 0.f)
					{
						const FGameplayAttribute Attribute = UUnitAttributeSet::FindAttributeByName(TEXT("AmmoPool"));
						if (Attribute.IsValid())
						{
							ASC->SetNumericAttributeBase(Attribute, FMath::Clamp(Fraction, 0.f, 1.f));
						}
					}
				}
			}
		}

		// Dirty check against the registry; write in place; skip clean units early.
		if (!Registry->IsStatsDirty(Stats.OwnerUnit))
		{
			return;
		}

		const FBattleAttributeSnapshot* Snapshot = Registry->FindCachedAttributes(Stats.OwnerUnit);
		if (Snapshot)
		{
			// Derivation (arch §10.3): base x modifier; *Mod attributes
			// default 0 so multipliers start at 1.
			Stats.SpeedMult = 1.f + Snapshot->SpeedMod;
			Stats.MeleeAttackMult = 1.f + Snapshot->MeleeAttackMod;
			Stats.MeleeDefenseMult = 1.f + Snapshot->MeleeDefenseMod;
			Stats.RangedAttackMult = 1.f + Snapshot->RangedAttackMod;
			Stats.BlockMult = 1.f + Snapshot->BlockChanceMod;
			// Phase 4a corrections (A8/A15/ADR-36, disclosed in the phase
			// report; both placeholder couplings verified consumer-free at
			// HEAD -- the R-M9 grep posture):
			// - FatigueAttackMult/DefenseMult: the Phase 0 placeholder
			//   `1 + Fatigue*0.01` is retired -- the FatigueState catalog
			//   writes fatigue through the *Mod aggregation (the melee math
			//   never consumed these two fields; they stay at the identity
			//   they were spawned with).
			Stats.FatigueAttackMult = 1.f;
			Stats.FatigueDefenseMult = 1.f;
			Stats.ArmorAdd = Snapshot->ArmourUpgrade;
			Stats.AccuracyMult = 1.f + Snapshot->Experience * 0.05f;
			// - MoraleBonus: the Phase 0 placeholder fed the ABSOLUTE morale
			//   level in as a bonus; the field means the AURA sum now (the
			//   general-aura contribution the morale pass folds -- the stack
			//   already carries it inside Morale; no consumer existed).
			Stats.MoraleBonus = 0.f;
			if (const FMoraleTickInputs* Inputs = Registry->FindMoraleInputs(Stats.OwnerUnit))
			{
				Stats.MoraleBonus = Inputs->GeneralAuraBonus;
			}
		}

		Registry->ClearStatsDirty(Stats.OwnerUnit);
	});

	if (ChunksMatched > 0)
	{
		UE_LOG(LogMassBattle, VeryVerbose, TEXT("UnitSync ChunksMatched=%d"), ChunksMatched);
	}
}
