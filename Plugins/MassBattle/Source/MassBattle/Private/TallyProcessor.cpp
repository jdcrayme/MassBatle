// TallyProcessor.cpp -- arch §4 row 4 (task 6; spec §4.3).
#include "TallyProcessor.h"
#include "BattleRegistry.h"
#include "BattleSpatialHash.h"
#include "BattleEngagement.h"
#include "BattleCombatFragments.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GCCombatTally(
		TEXT("Battle.Combat.Tally"),
		0,
		TEXT("Run the per-tick tally + coverage pass (arch §4 row 4). Phase 2 task 6."),
		ECVF_Default);
}

UTallyProcessor::UTallyProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PairQuery(*this)
	, CoverageQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("EngagementClustersProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("AssignPairsProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry scratch writes (spec §3.2)
}

void UTallyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	PairQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadOnly);
	PairQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	PairQuery.RegisterWithProcessor(*this);

	CoverageQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	CoverageQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	CoverageQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	CoverageQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	CoverageQuery.RegisterWithProcessor(*this);
}

void UTallyProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleSpatialHashSubsystem* Hash = World ? World->GetSubsystem<UBattleSpatialHashSubsystem>() : nullptr;
	if (!Registry || GCCombatTally.GetValueOnGameThread() == 0)
	{
		return; // flags-off: scratch stays reset (byte-identical baseline)
	}

	// Fresh each tick (arch §7.2): reset, count, stamp. No state to desync.
	FCombatTallyTable& Tally = Registry->GetTallyScratch();
	FCombatCoverageTable& Coverage = Registry->GetCoverageScratch();
	Tally.Reset();
	Coverage.Reset();
	const uint32 Tick = static_cast<uint32>(GFrameCounter);

	// Pass 1: count attackers per defender (paired entities only).
	PairQuery.ForEachEntityChunk(Context, [&Tally](FMassExecutionContext& Context)
	{
		const auto Pairs = Context.GetFragmentView<FCombatPairFragment>();
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			if (!Pairs[i].Target.IsValid())
			{
				continue;
			}
			if (const int32 Slot = Tally.FindOrAdd(Pairs[i].Target); Slot != INDEX_NONE)
			{
				++Tally.Counts[Slot];
			}
		}
	});

	// Pass 2: coverage + covered-sector masks for defenders with inbound pairs
	// (the perf scope cut, spec §4.3). Same-unit living neighbors come from
	// the spatial hash circle query (a permitted use: coverage ONLY).
	if (!Hash)
	{
		return;
	}
	CoverageQuery.ForEachEntityChunk(Context, [&Tally, &Coverage, Hash, Tick](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		if (Formation.GarrisonMode != EGarrisonMode::None)
		{
			return; // A9: garrisoned units are not combat defenders
		}
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		// The coverage radius rides the formation's CURRENT spacing (the
		// compression scale included): coverage measures the formation's
		// integrity at its own scale -- a compressed huddle must not read as
		// fully covered (the EC2 melt needs real gaps to register;
		// measured 2026-10-27: the raw spacing read coverage 1.0 at 8 alive).
		const float SpacingM = Formation.FileSpacing * Formation.SpacingScale * 0.01f;
		const float SearchRadius = FMath::Max(0.1f, SpacingM) * 1.25f;
		const int32 SectorCount = FMath::Clamp(FCombatCVars::SectorCount(), 1, 16);
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			if (Tally.GetCount(Entity) <= 0)
			{
				continue; // no inbound pair this tick: no coverage stamp
			}
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			const FVector2D PosM(P.X * 0.01f, P.Y * 0.01f);
			const FUnitHandle MyUnit = Owners[i].Owner;

			// Same-unit living neighbors within Spacing * 1.25 (self excluded).
			TArray<FBattleHashEntry> Neighbors;
			TArray<FVector2D> NeighborQueryPositionsM;
			Hash->QueryCircle(PosM, SearchRadius, Neighbors, &NeighborQueryPositionsM);
			TArray<FVector2D> NeighborPositionsM;
			uint8 CoveredMask = 0;
			const float Rad = FMath::DegreesToRadians(Formation.FacingYaw);
			const FVector2D Forward(FMath::Cos(Rad), FMath::Sin(Rad));
			const FVector2D Side(-FMath::Sin(Rad), FMath::Cos(Rad));
			for (int32 N = 0; N < Neighbors.Num(); ++N)
			{
				const FBattleHashEntry& Neighbor = Neighbors[N];
				if (Neighbor.Unit != MyUnit)
				{
					continue;
				}
				const FVector2D NPos = NeighborQueryPositionsM[N];
				NeighborPositionsM.Add(NPos);
				// Covered sector: the neighbor's direction in the defender's
				// frame (facing-relative, clockwise, 0-255 -- the ContactAngle
				// convention), quantized into a mask bit.
				const FVector2D Rel = NPos - PosM;
				const float Angle = FMath::Atan2(FVector2D::DotProduct(Rel, Side), FVector2D::DotProduct(Rel, Forward));
				float Deg = FMath::RadiansToDegrees(Angle);
				if (Deg < 0.f)
				{
					Deg += 360.f;
				}
				const uint8 Quantized = static_cast<uint8>(FMath::Clamp(Deg * (255.f / 360.f), 0.f, 255.f));
				CoveredMask |= static_cast<uint8>(1u << FCombatSolver::SectorIndexFromAngle(Quantized, SectorCount));
			}
			const FCombatDefenderView View{ PosM, SpacingM, MyUnit, Entity };
			const float CoverageValue = FCombatSolver::ComputeCoverage(View, NeighborPositionsM);
			Coverage.Store(Entity, CoverageValue, CoveredMask, Tick);
		}
	});
}
