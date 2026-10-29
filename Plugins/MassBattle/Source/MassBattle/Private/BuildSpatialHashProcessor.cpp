// BuildSpatialHashProcessor.cpp -- arch §4 row 2 (task 5).
#include "BuildSpatialHashProcessor.h"
#include "BattleSpatialHash.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleDebugSubsystem.h"
#include "BattleExecCommands.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// B.8 item 6: default 0 until this task's flags-on evidence lands; off =
	// the hash is not rebuilt and combat bookkeeping sees no entries.
	TAutoConsoleVariable<int32> GCCombatSpatialHash(
		TEXT("Battle.Combat.SpatialHash"),
		0,
		TEXT("Rebuild the combat spatial hash each tick (arch §4 row 2). Phase 2 task 5."),
		ECVF_Default);
}

UBuildSpatialHashProcessor::UBuildSpatialHashProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, HashQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitLocomotionProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("EngagementClustersProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // subsystem rebuild is game-thread (arch §4 row 2)
}

void UBuildSpatialHashProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	HashQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	HashQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	HashQuery.AddRequirement<FLoadoutFragment>(EMassFragmentAccess::ReadOnly);
	HashQuery.AddRequirement<FBaseStatsFragment>(EMassFragmentAccess::ReadOnly); // A12: the RangedCapable stamp source
	HashQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // audit M8 pattern
	HashQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	HashQuery.RegisterWithProcessor(*this);
}

void UBuildSpatialHashProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GCCombatSpatialHash.GetValueOnGameThread() == 0)
	{
		return; // flags-off: no hash, no combat bookkeeping (byte-identical baseline)
	}
	UWorld* World = Context.GetWorld();
	UBattleSpatialHashSubsystem* Hash = World ? World->GetSubsystem<UBattleSpatialHashSubsystem>() : nullptr;
	if (!Hash)
	{
		return;
	}

	// Collect this tick's entries from end-of-last-tick transforms (the worker
	// integrate stage runs later in the tick -- arch §4 ordering).
	TArray<FBattleHashEntry> Entries;
	TArray<FVector2D> PositionsM;
	int32 MaxEntities = 0;
	HashQuery.ForEachEntityChunk(Context, [&Entries, &PositionsM, &MaxEntities](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		if (Formation.GarrisonMode != EGarrisonMode::None)
		{
			return; // A9/F11: garrisoned units are out of the combat hash
		}
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		const auto Loadouts = Context.GetFragmentView<FLoadoutFragment>();
		const auto BaseStats = Context.GetFragmentView<FBaseStatsFragment>();
		MaxEntities = FMath::Max(MaxEntities, Context.GetNumEntities());
		Entries.Reserve(Entries.Num() + Context.GetNumEntities());
		PositionsM.Reserve(PositionsM.Num() + Context.GetNumEntities());
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FBattleHashEntry Entry;
			Entry.Entity = Context.GetEntity(i);
			Entry.Unit = Owners[i].Owner;
			Entry.ReachClass = FMath::Clamp(Loadouts[i].ReachClass, static_cast<uint8>(0), static_cast<uint8>(7));
			// Phase 3 A12: the Flags byte's first defined bit -- stamped from
			// the loadout's base stats (spec ranged-stack 3.3); ranged targeting
			// priority + impact debug read it.
			if (BaseStats[i].RangedAttack > 0.f)
			{
				Entry.Flags |= static_cast<uint8>(ECombatHashFlags::RangedCapable);
			}
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			Entries.Add(Entry);
			PositionsM.Add(FVector2D(P.X, P.Y) * 0.01f);
		}
	});

	Hash->Reserve(FMath::Max(512, Entries.Num()));
	Hash->RebuildWithPositions(MoveTemp(Entries), MoveTemp(PositionsM));

	if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
	{
		Debug->RecordCombatHashMs(Hash->GetHashBuildMs());
	}
}
