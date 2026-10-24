#include "UnitSyncProcessor.h"
#include "BattleFragments.h"
#include "BattleRegistry.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

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
	SyncQuery.ForEachEntityChunk(Context, [&ChunksMatched](FMassExecutionContext& Context)
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

		// Dirty check against the registry; write in place; skip clean units early.
		if (!Registry->IsStatsDirty(Stats.OwnerUnit))
		{
			return;
		}

		const FBattleAttributeSnapshot* Snapshot = Registry->FindCachedAttributes(Stats.OwnerUnit);
		if (Snapshot)
		{
			// Phase 0 derivation (arch §10.3): base x modifier; *Mod attributes
			// default 0 so multipliers start at 1.
			Stats.SpeedMult = 1.f + Snapshot->SpeedMod;
			Stats.MeleeAttackMult = 1.f + Snapshot->MeleeAttackMod;
			Stats.MeleeDefenseMult = 1.f + Snapshot->MeleeDefenseMod;
			Stats.RangedAttackMult = 1.f + Snapshot->RangedAttackMod;
			Stats.BlockMult = 1.f + Snapshot->BlockChanceMod;
			Stats.FatigueAttackMult = 1.f + Snapshot->Fatigue * 0.01f;
			Stats.FatigueDefenseMult = 1.f + Snapshot->Fatigue * 0.01f;
			Stats.ArmorAdd = Snapshot->ArmourUpgrade;
			Stats.AccuracyMult = 1.f + Snapshot->Experience * 0.05f;
			Stats.MoraleBonus = Snapshot->Morale;
		}
		Registry->ClearStatsDirty(Stats.OwnerUnit);
	});

	if (ChunksMatched > 0)
	{
		UE_LOG(LogMassBattle, VeryVerbose, TEXT("UnitSync ChunksMatched=%d"), ChunksMatched);
	}
}
