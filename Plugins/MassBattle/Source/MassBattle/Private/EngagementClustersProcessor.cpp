// EngagementClustersProcessor.cpp -- the 4 Hz record machine (task 5; spec §4.2).
#include "EngagementClustersProcessor.h"
#include "BattleEngagement.h"
#include "BattleTypes.h"
#include "BattleFragments.h"
#include "BattleDebugSubsystem.h"
#include "BattleRegistry.h"
#include "BattleUnitContact.h"
#include "BattleMovementFragments.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// B.8 item 6: default 0 until the assign pass consumes records (task 6).
	TAutoConsoleVariable<int32> GCCombatClusters(
		TEXT("Battle.Combat.Clusters"),
		0,
		TEXT("Run the 4 Hz engagement-cluster / record pass (arch §4 row 3). Phase 2 task 5."),
		ECVF_Default);
}

UEngagementClustersProcessor::UEngagementClustersProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, UnitBodyQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("BuildSpatialHashProcessor"));
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitLocomotionProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry writes are game-thread (spec §3.2)
}

void UEngagementClustersProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	UnitBodyQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // audit M8
	UnitBodyQuery.RegisterWithProcessor(*this);
}

void UEngagementClustersProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || GCCombatClusters.GetValueOnGameThread() == 0)
	{
		return; // flags-off: records stay empty (byte-identical baseline)
	}

	// 4 Hz gate (spec §4.2; cooldown lives on the per-world registry).
	const float DT = Context.GetDeltaTimeSeconds();
	Registry->SetCombatClusterCooldown(Registry->GetCombatClusterCooldown() - DT);
	if (Registry->GetCombatClusterCooldown() > 0.f)
	{
		return;
	}
	Registry->SetCombatClusterCooldown(FCombatCVars::ClusterPeriod());

	// -- Gather unit bodies (FUnitClusterNode mirrors; garrisoned excluded). --
	TArray<FCombatSolver::FUnitClusterNode> Nodes;
	UnitBodyQuery.ForEachEntityChunk(Context, [&Nodes](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		if (Formation.GarrisonMode != EGarrisonMode::None || Formation.NumSlots <= 0 || Formation.NumFiles <= 0)
		{
			return; // A9/F11: garrisoned units initiate nothing and take nothing
		}
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		if (Owners.Num() == 0)
		{
			return;
		}
		FCombatSolver::FUnitClusterNode Node;
		Node.Unit = Owners[0].Owner;
		Node.AnchorM = FVector2D(Formation.AnchorPos) * 0.01f;
		Node.YawDeg = Formation.FacingYaw;
		Node.HalfAlongM = (Formation.NumFiles * Formation.FileSpacing) * 0.005f;
		Node.HalfAcrossM = (FMath::CeilToInt(Formation.NumSlots / (float)FMath::Max(1, Formation.NumFiles)) * Formation.RankSpacing) * 0.005f;
		Node.AliveCount = Context.GetNumEntities(); // live roster in this unit's chunks
		Nodes.Add(Node);
	});

	// -- Contact pairs: OBBs dilated by the engage distance (radius sum +
	// reach, one scalar; each OBB inflated by half so the sum matches).
	// B-1: the body radius + normal-reach literals live in the seam now --
	// contact detection and pairing must read the SAME numbers.
	static const FCombatSolver::FAssignContext ContactContext; // default reach table
	const float Dilation = (FCombatSolver::Contact::BodyRadiusM * 2.f
		+ ContactContext.ReachByClassM[static_cast<int32>(EBattleWeaponReach::Normal)]) * 0.5f;
	TArray<FIntPoint> ContactPairs;
	for (int32 A = 0; A < Nodes.Num(); ++A)
	{
		// Node bodies are ANCHOR-centered (the fight line is at the front
		// rank, which the formation anchor marks); extents in meters.
		FUnitOBB BodyA;
		BodyA.Center = Nodes[A].AnchorM;
		BodyA.YawDeg = Nodes[A].YawDeg;
		BodyA.HalfAlongFacing = Nodes[A].HalfAcrossM + Dilation; // rank depth
		BodyA.HalfAcrossFacing = Nodes[A].HalfAlongM + Dilation; // frontage
		for (int32 B = A + 1; B < Nodes.Num(); ++B)
		{
			if (Nodes[A].Unit == Nodes[B].Unit)
			{
				continue;
			}
			FUnitOBB BodyB;
			BodyB.Center = Nodes[B].AnchorM;
			BodyB.YawDeg = Nodes[B].YawDeg;
			BodyB.HalfAlongFacing = Nodes[B].HalfAcrossM + Dilation;
			BodyB.HalfAcrossFacing = Nodes[B].HalfAlongM + Dilation;
			if (FUnitContact::OverlapDepth(BodyA, BodyB) > 0.f)
			{
				ContactPairs.Add({A, B});
			}
		}
	}

	// -- Flood-fill clusters (metrics + the §10 per-cluster sharding seam). --
	TArray<int32> ClusterIds;
	FCombatSolver::ClusterGraph(Nodes.Num(), ContactPairs, ClusterIds);

	// -- Rebuild the records: flag all dead, refresh contacted pairs, prune. --
	Registry->FlagAllEngagementsDead();
	// Aggregate attacker DIRECTION quadrants per defender (the Mob test,
	// §4.2) -- in the DEFENDER'S FACING frame, ONE bit per attacker quadrant:
	// front-right=1, front-left=2, rear-right=4, rear-left=8. A frontal line
	// occupies the two FRONT quadrants (popcount 2 -> Frontal/Flank); a
	// surrounding mob spans >= 3 quadrants -> Mob. (Sign-bit ORs gave every
	// attacker two bits and misread the wall as Mob -- measured 2026-10-27.)
	TMap<FUnitHandle, uint8> DirectionMask;
	for (const FIntPoint& Pair : ContactPairs)
	{
		const FVector2D Rel = Nodes[Pair.X].AnchorM - Nodes[Pair.Y].AnchorM; // attacker relative to defender
		const float Rad = FMath::DegreesToRadians(Nodes[Pair.Y].YawDeg);
		const FVector2D Forward(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D Side(-FMath::Sin(Rad), FMath::Cos(Rad));
		const float Fx = FVector2D::DotProduct(Rel, Forward);
		const float Sy = FVector2D::DotProduct(Rel, Side);
		const int32 Quadrant = (Fx < 0.f ? 2 : 0) + (Sy < 0.f ? 1 : 0); // 0=FR 1=FL 2=RR 3=RL
		DirectionMask.FindOrAdd(Nodes[Pair.Y].Unit) |= static_cast<uint8>(1u << Quadrant);
	}
	// Contact without intent revives nothing: a disengaging unit's records
	// must STAY dead (the assign pass releases its inbound pairs; the 4 Hz
	// revival would otherwise resurrect them every tick and the disengage
	// never lands -- measured 2026-10-27). The orders are game-thread reads.
	TMap<FUnitHandle, bool> UnitWantsCombat;
	for (const FCombatSolver::FUnitClusterNode& Node : Nodes)
	{
		bool bWants = false;
		if (const FUnitOrder* Order = Registry->FindOrder(Node.Unit))
		{
			bWants = Order->Type == FUnitOrder::EType::AttackUnit || Order->Type == FUnitOrder::EType::AttackMove;
		}
		UnitWantsCombat.Add(Node.Unit, bWants);
	}
	int32 Emitted = 0;
	for (const FIntPoint& Pair : ContactPairs)
	{
		if (!UnitWantsCombat.FindOrAdd(Nodes[Pair.X].Unit) && !UnitWantsCombat.FindOrAdd(Nodes[Pair.Y].Unit))
		{
			continue; // neither side is attacking: no engagement records
		}
		for (int32 Dir = 0; Dir < 2; ++Dir)
		{
			const int32 AttackerIdx = Dir == 0 ? Pair.X : Pair.Y;
			const int32 DefenderIdx = Dir == 0 ? Pair.Y : Pair.X;
			const uint8 Mask = DirectionMask.FindOrAdd(Nodes[DefenderIdx].Unit);
			FCombatSolver::FEngagementRecordBuild Build;
			FCombatSolver::BuildEngagementRecord(Nodes[AttackerIdx], Nodes[DefenderIdx], Mask, Build);
			if (FEngagementRecord* Record = Registry->CreateOrRefreshEngagement(Build.Attacker, Build.Defender))
			{
				Record->Mode = static_cast<EEngagementMode>(Build.Mode);
				Record->FrontageStart = Build.FrontageStart;
				Record->FrontageEnd = Build.FrontageEnd;
				Record->DesiredPressure = Build.DesiredPressure;
				++Emitted;
			}
		}
	}
	Registry->PruneDeadEngagements();

	// -- Unit-level engagement bookkeeping (spec §3.2: FSM + metrics inputs). --
	TMap<FUnitHandle, int32> EngagedCount;
	for (const FEngagementKey& Key : Registry->GetSortedEngagementKeys())
	{
		if (const FEngagementRecord* Rec = Registry->FindEngagement(Key.Attacker, Key.Defender))
		{
			if (Rec->bDead == 0)
			{
				EngagedCount.FindOrAdd(Key.Attacker)++;
				EngagedCount.FindOrAdd(Key.Defender)++;
			}
		}
	}
	for (const FCombatSolver::FUnitClusterNode& Node : Nodes)
	{
		Registry->SetEngagedCount(Node.Unit, EngagedCount.FindOrAdd(Node.Unit));
	}

	int32 ClusterCount = 0;
	for (int32 i = 0; i < ClusterIds.Num(); ++i)
	{
		ClusterCount = FMath::Max(ClusterCount, ClusterIds[i] + 1);
	}
	UE_LOG(LogMassBattle, Verbose,
		TEXT("EngagementClusters units=%d contacts=%d clusters=%d records=%d"),
		Nodes.Num(), ContactPairs.Num(), ClusterCount, Emitted);

	// B.2 combat overlay row (task 10, F10 pattern): engagement-record debug
	// (frontage segments + mode + pressure vs assigned) behind the overlay
	// gate. Colors: Frontal red, Flank orange, Mob magenta.
	if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
		Debug && Debug->IsOverlayShown())
	{
		for (const FEngagementKey& Key : Registry->GetSortedEngagementKeys())
		{
			const FEngagementRecord* Record = Registry->FindEngagement(Key.Attacker, Key.Defender);
			if (!Record || Record->bDead != 0)
			{
				continue;
			}
			Debug->EnqueueEngagementLine(Key.Attacker, Key.Defender,
				static_cast<int32>(Record->Mode), Record->DesiredPressure, Record->AssignedAttackers);
		}
	}
}
