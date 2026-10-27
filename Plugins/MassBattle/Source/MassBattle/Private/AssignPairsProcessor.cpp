// AssignPairsProcessor.cpp -- arch §4 row 5 (task 6; spec §4.4 bridge).
#include "AssignPairsProcessor.h"
#include "BattleRegistry.h"
#include "BattleRngRegistry.h"
#include "BattleEngagement.h"
#include "BattleCombatFragments.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleExecCommands.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GCCombatAssign(
		TEXT("Battle.Combat.Assign"),
		0,
		TEXT("Run the per-tick assign pass (arch §4 row 5). Phase 2 task 6."),
		ECVF_Default);

	// B.5 determinism hook (runtime accessor of the compile-time posture):
	// 1 = index-sorted iteration + per-entity jitter instead of the seeded
	// shuffle. The shuffled mode is itself deterministic per seed, so T2.8's
	// two-run equality holds in both modes.
	TAutoConsoleVariable<int32> GCBattleIndexSortedIteration(
		TEXT("Battle.Test.IndexSortedIteration"),
		0,
		TEXT("B.5 determinism hook: 1 = assign-pass iteration is index-sorted (per-entity jitter) instead of the seeded Combat.Assign shuffle."),
		ECVF_Default);
}

UAssignPairsProcessor::UAssignPairsProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SeekerQuery(*this)
	, DefenderQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("TallyProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("FormationSlotResolveProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry + RNG reads (spec §4.4)
}

void UAssignPairsProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	SeekerQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadWrite);
	SeekerQuery.AddRequirement<FCombatIntentFragment>(EMassFragmentAccess::ReadWrite);
	SeekerQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.AddRequirement<FLoadoutFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	SeekerQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.RegisterWithProcessor(*this);

	DefenderQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	DefenderQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.RegisterWithProcessor(*this);
}

void UAssignPairsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleRngSubsystem* Rng = World ? World->GetSubsystem<UBattleRngSubsystem>() : nullptr;
	if (!Registry || GCCombatAssign.GetValueOnGameThread() == 0)
	{
		return; // flags-off: no pairs form, no combat targets move (byte-identical)
	}

	const float DT = Context.GetDeltaTimeSeconds();
	FCombatTallyTable& Tally = Registry->GetTallyScratch();
	FCombatCoverageTable& Coverage = Registry->GetCoverageScratch();
	const uint32 Tick = static_cast<uint32>(GFrameCounter);

	// ---- Defender mirrors (all live, non-garrisoned battle entities). ----
	// Lane = the entity's frontage bucket by GEOMETRY (deterministic, robust
	// to casualty reflow): ((pos - anchor) . side) / FileSpacing + NumFiles/2.
	TArray<FCombatSolver::FAssignDefenderState> Defenders;
	DefenderQuery.ForEachEntityChunk(Context, [&Defenders](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		if (Formation.GarrisonMode != EGarrisonMode::None || Formation.NumFiles <= 0)
		{
			return; // A9/F11
		}
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		Defenders.Reserve(Defenders.Num() + Context.GetNumEntities());
		const float Rad = FMath::DegreesToRadians(Formation.FacingYaw);
		const FVector2D Side(-FMath::Sin(Rad), FMath::Cos(Rad));
		const FVector2D AnchorM = FVector2D(Formation.AnchorPos) * 0.01f;
		const float SpacingM = Formation.FileSpacing * 0.01f;
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FCombatSolver::FAssignDefenderState Def;
			Def.Entity = Context.GetEntity(i);
			Def.Unit = Owners[i].Owner;
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			Def.PosM = FVector2D(P.X, P.Y) * 0.01f;
			Def.Spacing = SpacingM;
			Def.YawDeg = Formation.FacingYaw;
			const float AlongFrontage = FVector2D::DotProduct(Def.PosM - AnchorM, Side) / FMath::Max(0.1f, SpacingM)
				+ Formation.NumFiles * 0.5f;
			Def.LaneIndex = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(AlongFrontage), 0, 255));
			Def.bValidTarget = 1;
			Defenders.Add(Def);
		}
	});
	// Handle-sorted (the seam binary-searches this array).
	Defenders.Sort([](const FCombatSolver::FAssignDefenderState& A, const FCombatSolver::FAssignDefenderState& B)
	{
		if (A.Entity.Index != B.Entity.Index) { return A.Entity.Index < B.Entity.Index; }
		return A.Entity.SerialNumber < B.Entity.SerialNumber;
	});
	// Coverage + covered masks from the tally pass's fresh stamps (misses read
	// as coverage 0 -- a one-tick transient at first contact, logged design).
	for (FCombatSolver::FAssignDefenderState& Def : Defenders)
	{
		if (const int32 Slot = Coverage.Find(Def.Entity, Tick); Slot != INDEX_NONE)
		{
			Def.Coverage = Coverage.Values[Slot];
			Def.CoveredMask = Coverage.GetMask(Slot);
		}
	}

	// ---- Records: one mirror per live (attacker, defender) engagement. ----
	TArray<FCombatSolver::FAssignRecordView> Records;
	TMap<FEngagementKey, int32> RecordIndexMap;
	{
		const TArray<FEngagementKey> Keys = Registry->GetSortedEngagementKeys();
		Records.Reserve(Keys.Num());
		for (const FEngagementKey& Key : Keys)
		{
			const FEngagementRecord* Rec = Registry->FindEngagement(Key.Attacker, Key.Defender);
			if (!Rec || Rec->bDead != 0)
			{
				continue;
			}
			FCombatSolver::FAssignRecordView Mirror;
			Mirror.DesiredPressure = Rec->DesiredPressure;
			Mirror.AssignedAttackers = 0; // fresh tally (spec §3.2)
			Mirror.Mode = static_cast<uint8>(Rec->Mode);
			RecordIndexMap.Add(Key, Records.Add(Mirror));
		}
	}

	// ---- Order intents per unit (A4): the registry order surface resolved. ----
	TMap<FUnitHandle, FUnitOrder> Orders;
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		if (const FUnitOrder* Order = Registry->FindOrder(Handle))
		{
			Orders.Add(Handle, *Order);
		}
	}

	// ---- Seeker mirrors, per chunk (both query walks visit chunks in the
	// same deterministic order, so write-back consumes BridgesByChunk
	// sequentially). ----
	TArray<TArray<FCombatSolver::FAssignSeekerState>> BridgesByChunk;
	TArray<TArray<int32>> ChunkIndices; // per chunk: entity row within the chunk
	TMap<FUnitHandle, int32> UnitFileCount; // attacker unit -> NumFiles

	SeekerQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		auto Pairs = Context.GetMutableFragmentView<FCombatPairFragment>();
		auto Intents = Context.GetMutableFragmentView<FCombatIntentFragment>();
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Loadouts = Context.GetFragmentView<FLoadoutFragment>();
		const float Rad = FMath::DegreesToRadians(Formation.FacingYaw);
		const FVector2D Side(-FMath::Sin(Rad), FMath::Cos(Rad));
		const FVector2D AnchorM = FVector2D(Formation.AnchorPos) * 0.01f;
		const float SpacingM = Formation.FileSpacing * 0.01f;

		const FUnitHandle OwnerUnit = Owners[0].Owner;
		UnitFileCount.FindOrAdd(OwnerUnit) = Formation.NumFiles;
		const FUnitOrder* Order = Orders.Find(OwnerUnit);

		const bool bUnitHasOrder = Order
			&& (Order->Type == FUnitOrder::EType::AttackUnit || Order->Type == FUnitOrder::EType::AttackMove);
		FUnitHandle OrderTarget;
		uint8 bAutoEngage = 0;
		if (bUnitHasOrder)
		{
			OrderTarget = Order->TargetUnit;
			bAutoEngage = Order->Type == FUnitOrder::EType::AttackMove ? 1 : 0;
		}

		TArray<FCombatSolver::FAssignSeekerState>& ChunkBridges = BridgesByChunk.AddDefaulted_GetRef();
		TArray<int32>& ChunkRows = ChunkIndices.AddDefaulted_GetRef();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			// Stamp the intent fragments from the order surface (A4).
			Intents[i].TargetUnit = bUnitHasOrder ? OrderTarget : FUnitHandle();
			Intents[i].bAutoEngage = bUnitHasOrder ? bAutoEngage : 0;
			if (!bUnitHasOrder)
			{
				continue; // no combat intent: not a seeker this tick
			}
			FCombatSolver::FAssignSeekerState S;
			S.Entity = Context.GetEntity(i);
			S.OwnerUnit = OwnerUnit;
			S.IntentTarget = OrderTarget;
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			S.PosM = FVector2D(P.X, P.Y) * 0.01f;
			// Attacking file: the entity's frontage bucket (same geometry rule
			// as the defenders' lanes).
			const float AlongFrontage = FVector2D::DotProduct(S.PosM - AnchorM, Side) / FMath::Max(0.1f, SpacingM)
				+ Formation.NumFiles * 0.5f;
			S.LaneIndex = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(AlongFrontage), 0, 255));
			S.PairAge = Pairs[i].PairAge + DT;
			S.AttackTimer = Pairs[i].AttackTimer;
			S.Target = Pairs[i].Target;
			S.ContactAngle = Pairs[i].ContactAngle;
			S.State = static_cast<uint8>(Pairs[i].State);
			S.ReachClass = FMath::Clamp(Loadouts[i].ReachClass, static_cast<uint8>(0), static_cast<uint8>(7));
			S.BackoffTicks = Pairs[i].BackoffTicks;
			S.bCharging = 0; // charge bypass arrives with task 7's processor
			S.RecordIndex = INDEX_NONE;
			if (S.IntentTarget.IsValid())
			{
				if (const int32* RecIdx = RecordIndexMap.Find({ S.OwnerUnit, S.IntentTarget }))
				{
					S.RecordIndex = *RecIdx;
				}
			}
			ChunkBridges.Add(S);
			ChunkRows.Add(i);
		}
	});

	// Flatten seekers for the pass (chunk order preserved).
	TArray<FCombatSolver::FAssignSeekerState> Seekers;
	Seekers.Reserve(64);
	for (const TArray<FCombatSolver::FAssignSeekerState>& Chunk : BridgesByChunk)
	{
		Seekers.Append(Chunk);
	}

	// Lane proportionality: attacker file k of NumFilesA targets defender lane
	// round(k * NumFilesD / NumFilesA) when widths differ (the frontage
	// subdivides per attacking file, spec §4.2).
	{
		TMap<FUnitHandle, int32> DefenderFileCount;
		DefenderQuery.ForEachEntityChunk(Context, [&DefenderFileCount](FMassExecutionContext& Context)
		{
			const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
			const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
			if (Owners.Num() > 0)
			{
				DefenderFileCount.FindOrAdd(Owners[0].Owner) = Formation.NumFiles;
			}
		});
		for (FCombatSolver::FAssignSeekerState& S : Seekers)
		{
			const int32* NumA = UnitFileCount.Find(S.OwnerUnit);
			const int32* NumD = DefenderFileCount.Find(S.IntentTarget);
			if (NumA && NumD && *NumA > 0 && *NumD > 0 && *NumA != *NumD)
			{
				const float Mapped = static_cast<float>(S.LaneIndex) * static_cast<float>(*NumD) / static_cast<float>(*NumA);
				S.LaneIndex = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Mapped), 0, 255));
			}
		}
	}

	// ---- Run the seam pass. ----
	FCombatSolver::FAssignContext AssignContext;
	if (GCBattleIndexSortedIteration.GetValueOnGameThread() == 0 && Rng)
	{
		AssignContext.ShuffleStream = &Rng->GetStream(TEXT("Combat.Assign"));
	}
	TArray<int32> SectorScratch;
	SectorScratch.Reserve(Defenders.Num() * 16);
	AssignContext.SectorScratch = &SectorScratch;
	FCombatSolver::RunAssignPass(Seekers, Defenders, Records, Tally, AssignContext);

	// ---- Write back: pair fragments + ring points (A12 press ring). ----
	{
		int32 SeekerCursor = 0; // into the seam-mutated Seekers (chunk order preserved)
		SeekerQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
		{
			auto Pairs = Context.GetMutableFragmentView<FCombatPairFragment>();
			const auto Transforms = Context.GetFragmentView<FTransformFragment>();
			const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();

			// This chunk's slice of the SEAM-MUTATED Seekers array (the gather
			// appended per chunk in order; BridgesByChunk holds pre-pass
			// copies and must not be written back).
			const TArray<int32>& ChunkRows = SeekerCursor < ChunkIndices.Num() ? ChunkIndices[SeekerCursor] : TArray<int32>();
			const int32 ChunkSeekerCount = FMath::Min(ChunkRows.Num(), Seekers.Num() - SeekerCursor);
			++SeekerCursor;

			for (int32 Row = 0; Row < ChunkSeekerCount; ++Row)
			{
				const int32 i = ChunkRows[Row];
				const FCombatSolver::FAssignSeekerState& S = Seekers[SeekerCursor - 1 + Row];
				FCombatPairFragment& Out = Pairs[i];
				Out.Target = S.Target;
				Out.ContactAngle = S.ContactAngle;
				Out.State = static_cast<ECombatPairState>(S.State);
				Out.AttackTimer = S.AttackTimer;
				Out.PairAge = S.PairAge;
				Out.BackoffTicks = S.BackoffTicks;

				// Ring point: paired -> the assigned defender's ring at the
				// assigned approach angle (the ContactAngle convention:
				// clockwise from the DEFENDER's facing); waiter -> the nearest
				// intent defender's ring (press the line, spec §4.6).
				const FVector P = Transforms[i].GetTransform().GetTranslation();
				const FVector2D MyPosM(P.X * 0.01f, P.Y * 0.01f);
				const uint8 ClassIdx = FMath::Min<uint8>(S.ReachClass, 7);
				const float RingRadiusM = 0.4f + 0.4f + AssignContext.ReachByClassM[ClassIdx];
				const FCombatSolver::FAssignDefenderState* RingDef = nullptr;
				float RingAngleDeg = 0.f;
				bool bRing = false;
				if (S.Target.IsValid())
				{
					for (const FCombatSolver::FAssignDefenderState& Def : Defenders)
					{
						if (Def.Entity == S.Target)
						{
							RingDef = &Def;
							break;
						}
					}
					RingAngleDeg = static_cast<float>(S.ContactAngle) * (360.f / 255.f);
					bRing = RingDef != nullptr;
				}
				else
				{
					float BestDistSq = TNumericLimits<float>::Max();
					for (const FCombatSolver::FAssignDefenderState& Def : Defenders)
					{
						if (Def.Unit != S.IntentTarget || Def.bValidTarget == 0)
						{
							continue;
						}
						const float DistSq = FVector2D::DistSquared(MyPosM, Def.PosM);
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							RingDef = &Def;
						}
					}
					if (RingDef)
					{
						// Approach angle from the actual offset (waiters press
						// from where they stand; the assigned angle arrives
						// with the pair).
						const FVector2D Rel = MyPosM - RingDef->PosM;
						RingAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(Rel.Y, Rel.X)) - RingDef->YawDeg;
						bRing = true;
					}
				}
				if (bRing && RingDef)
				{
					const float DefenderRad = FMath::DegreesToRadians(RingDef->YawDeg + RingAngleDeg);
					const FVector2D Dir(FMath::Cos(DefenderRad), FMath::Sin(DefenderRad));
					Out.RingPosCm = (RingDef->PosM + Dir * RingRadiusM) * 100.f;
					Out.bRingValid = 1;
				}
				else
				{
					Out.bRingValid = 0;
				}
			}
		});
	}
}
