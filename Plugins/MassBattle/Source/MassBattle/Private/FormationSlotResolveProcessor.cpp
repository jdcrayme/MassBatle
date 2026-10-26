#include "FormationSlotResolveProcessor.h"
#include "BattleMovementFragments.h"
#include "BattleSlotAssigner.h"
#include "BattleGarrison.h"
#include "BattlePathFrameLayout.h"
#include "BattleCompression.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementSlotResolve(
		TEXT("Battle.Movement.SlotResolve"),
		1,
		TEXT("Enable FormationSlotResolve processing (arch §4 row 6). 0 opts the stage out."),
		ECVF_Default);

	// Arch §8.4: predictive swaps are optional policy (same-unit pairs whose
	// servo paths cross within ~1 s swap slot indices).
	TAutoConsoleVariable<int32> GBattleMovementPredictiveSwaps(
		TEXT("Battle.Movement.PredictiveSwaps"),
		1,
		TEXT("1 = enable predictive slot swaps for crossing servo paths (arch §8.4)."),
		ECVF_Default);

	// k-file displacement bound for reflow (T1.1): survivors must not trek
	// across the formation; frontage-local fills only.
	TAutoConsoleVariable<int32> GBattleMovementMaxFileDisplacement(
		TEXT("Battle.Movement.MaxFileDisplacement"),
		2,
		TEXT("Max file columns an entity may shift during a reflow (k in the T1.1 bound)."),
		ECVF_Default);
}

UFormationSlotResolveProcessor::UFormationSlotResolveProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ResolveQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitLocomotionProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 6).
	bRequiresGameThreadExecution = false;
}

void UFormationSlotResolveProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ResolveQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadWrite);
	ResolveQuery.AddRequirement<FSlotAssignmentFragment>(EMassFragmentAccess::ReadWrite);
	ResolveQuery.AddRequirement<FSlotTargetFragment>(EMassFragmentAccess::ReadWrite);
	ResolveQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ResolveQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
}

void UFormationSlotResolveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine FormationSlotResolve first-execute"));
	}

	if (GBattleMovementSlotResolve.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();

	ResolveQuery.ForEachEntityChunk(Context, [DT](FMassExecutionContext& Context)
	{
		FUnitFormationSharedFragment& Formation = Context.GetMutableSharedFragment<FUnitFormationSharedFragment>();
		const int32 NumEntities = Context.GetNumEntities();
		if (Formation.NumSlots <= 0 || Formation.NumFiles <= 0)
		{
			return; // layout never initialized
		}

		// ---- Assemble assigner inputs (meters at the fragment boundary) ----
		TArray<FVector2D> EntityPosM;
		EntityPosM.Reserve(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			const FVector Pos = Context.GetFragmentView<FTransformFragment>()[i].GetTransform().GetTranslation();
			EntityPosM.Add(FVector2D(Pos.X, Pos.Y) * 0.01f);
		}

		// ---- Garrison slot-source branch (spec §4.4, A1/A2): when the unit
		//      holds posts (Occupy/Settled), slot targets come from the
		//      booked-range mirror, not the field layout. ----
		const bool bGarrisonHold = Formation.GarrisonMode == EGarrisonMode::Garrisoned
			&& (Formation.GarrisonPhase == EGarrisonPhase::Occupy || Formation.GarrisonPhase == EGarrisonPhase::Settled)
			&& Formation.GarrisonSlotCount > 0;
		if (bGarrisonHold)
		{
			const int32 Count = FMath::Min(Formation.GarrisonSlotCount, MaxGarrisonSlotsPerUnit);
			TArray<FVector2D> PostPosM;
			TArray<float> PostYaw;
			TArray<float> PostZCm;
			PostPosM.SetNumUninitialized(Count);
			PostYaw.SetNumUninitialized(Count);
			PostZCm.SetNumUninitialized(Count);
			for (int32 i = 0; i < Count; ++i)
			{
				const FGarrisonSlotFragment& Post = Formation.Slots[i];
				PostPosM[i] = Post.Pos * 0.01f; // cm -> m
				PostYaw[i] = Post.YawDeg;
				PostZCm[i] = Post.Z;
			}

			// Assignment over the booked range (deterministic greedy
			// bijection, the k-file bound's analog; R-G4: this is the ONLY
			// garrison phase where reflow runs). FSlotAssignmentFragment
			// keeps its role: SlotIndex indexes the BOOKED range
			// [GarrisonSlotBase, +Count) -- AssignSlots works on local
			// [0,Count) arrays, so map in and out.
			const int32 SlotBase = Formation.GarrisonSlotBase;
			TArray<int32> Assignment;
			Assignment.Reserve(NumEntities);
			for (int32 i = 0; i < NumEntities; ++i)
			{
				const int32 Slot = Context.GetFragmentView<FSlotAssignmentFragment>()[i].SlotIndex - SlotBase;
				Assignment.Add((Slot >= 0 && Slot < Count) ? Slot : INDEX_NONE);
			}

			FReflowScheduler Scheduler;
			Scheduler.IntervalSeconds = 0.5f;
			Scheduler.Cooldown = Formation.ReflowCooldown;
			const bool bLayoutRedecision = Formation.Generation != Formation.AssignedGeneration;
			if (bLayoutRedecision || Scheduler.ConsumeIfDue(DT, /*bRankGap*/ false))
			{
				TArray<int32> NewAssignment;
				FGarrisonLayout::AssignSlots(EntityPosM, PostPosM, Assignment,
					bLayoutRedecision ? BIG_NUMBER : FGarrisonCVars::MaxSlotDisplacement(), NewAssignment);
				for (int32 i = 0; i < NumEntities; ++i)
				{
					Context.GetMutableFragmentView<FSlotAssignmentFragment>()[i].SlotIndex =
						NewAssignment[i] >= 0 ? NewAssignment[i] + SlotBase : INDEX_NONE;
				}
				Assignment = MoveTemp(NewAssignment);
				Formation.AssignedGeneration = Formation.Generation;
			}
			Formation.ReflowCooldown = Scheduler.Cooldown;

			// Write move targets + slot-error overlay (A3).
			UBattleDebugSubsystem* DebugHold = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
			TArrayView<FSlotTargetFragment> HoldTargets = Context.GetMutableFragmentView<FSlotTargetFragment>();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				const int32 Slot = Assignment[i];
				if (Slot < 0 || Slot >= Count)
				{
					continue;
				}
				HoldTargets[i].TargetPos = FVector2D(PostPosM[Slot]) * 100.f;
				HoldTargets[i].TargetYaw = PostYaw[Slot];
				HoldTargets[i].TargetZ = PostZCm[Slot];

				if (DebugHold)
				{
					FBattleOverlayLine Line;
					Line.From = Context.GetFragmentView<FTransformFragment>()[i].GetTransform().GetTranslation();
					Line.To = FVector(HoldTargets[i].TargetPos.X, HoldTargets[i].TargetPos.Y, PostZCm[Slot]);
					const float ErrorM = FVector2D::Distance(EntityPosM[i], PostPosM[Slot]);
					const uint8 Magnitude = static_cast<uint8>(FMath::Clamp(ErrorM * 25.5f, 0.f, 255.f));
					Line.Color = FColor(0, 255 - Magnitude, 255); // cyan-graded: garrison slot error
					DebugHold->EnqueueOverlayLine(Line);
				}
			}
			return; // booked-range source replaces the field layout entirely
		}

		// Slot world positions (meters): path-frame when a path exists, the
		// §8.2 anchor-formula degenerate layout otherwise. En route the block
		// projects around the anchor's CURRENT arc position (PathS) -- the
		// formation travels coherently with the unit (arch §8.1 the unit is
		// the locomotor); projecting at the path end instead would send every
		// entity beelining for its final slot (the unit blobs apart mid-march).
		FBattlePlannedPath PathM;
		const bool bHasPath = FPathFrameLayout::FromFragment(Formation.Path, PathM);
		const bool bFiling = Formation.GarrisonMode != EGarrisonMode::None
			&& (Formation.GarrisonPhase == EGarrisonPhase::Ingress
				|| Formation.GarrisonPhase == EGarrisonPhase::Egress);
		const float FileSpacingM = Formation.FileSpacing * 0.01f;
		const float RankSpacingM = Formation.RankSpacing * 0.01f;

		TArray<FVector2D> SlotPosM;
		TArray<float> SlotYaw;
		SlotPosM.SetNumUninitialized(Formation.NumSlots);
		SlotYaw.SetNumUninitialized(Formation.NumSlots);
		FVector2D StartPosM = FVector2D::ZeroVector;
		FVector2D StartTan = FVector2D(1.f, 0.f);
		if (bHasPath)
		{
			(void)FPathFrameLayout::SampleAt(PathM, 0.f, StartPosM, StartTan);
		}
		for (int32 Slot = 0; Slot < Formation.NumSlots; ++Slot)
		{
			const int32 Rank = Slot / Formation.NumFiles;
			const int32 File = Slot % Formation.NumFiles;
			FVector2D Pos;
			float YawDeg;
			if (bHasPath)
			{
				const float RankArcM = Formation.PathS * 0.01f - Rank * RankSpacingM;
				if (RankArcM >= 0.f)
				{
					(void)FPathFrameLayout::Project(PathM, Formation.PathS * 0.01f, Rank, File, Formation.NumFiles,
						RankSpacingM, FileSpacingM * Formation.SpacingScale, Pos, YawDeg);
				}
				else
				{
					// Rank still behind the path start (early march): extrapolate
					// along the entry tangent so the block keeps its depth instead
					// of compressing onto the first path point.
					const FVector2D StartNormal(-StartTan.Y, StartTan.X);
					const float Lateral = (static_cast<float>(File) - (Formation.NumFiles - 1) * 0.5f)
						* FileSpacingM * Formation.SpacingScale;
					Pos = StartPosM + StartTan * RankArcM + StartNormal * Lateral;
					YawDeg = FMath::RadiansToDegrees(FMath::Atan2(StartTan.Y, StartTan.X));
				}
			}
			else
			{
				const FVector2D Offset(-static_cast<float>(Rank) * RankSpacingM,
					(static_cast<float>(File) - (Formation.NumFiles - 1) * 0.5f) * FileSpacingM);
				Pos = FUnitCompression::CompressedSlot(Formation.AnchorPos * 0.01f, Formation.FacingYaw, Offset, Formation.SpacingScale);
				YawDeg = Formation.FacingYaw;
			}
			SlotPosM[Slot] = Pos;
			SlotYaw[Slot] = YawDeg;
		}

		// ---- Batched reflow (arch §8.4): 0.5 s cadence or rank-gap trigger ----
		TArray<int32> Assignment;
		Assignment.Reserve(NumEntities);
		TArray<bool> SlotOccupied;
		SlotOccupied.Init(false, Formation.NumSlots);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			Assignment.Add(Context.GetFragmentView<FSlotAssignmentFragment>()[i].SlotIndex);
			if (Assignment[i] != INDEX_NONE && Assignment[i] < Formation.NumSlots)
			{
				SlotOccupied[Assignment[i]] = true;
			}
		}

		// Rank gap: an empty slot in some rank while a later rank has an
		// occupant (post-casualty hole the batch timer should not wait for).
		bool bRankGap = false;
		for (int32 Slot = 0; Slot < Formation.NumSlots && !bRankGap; ++Slot)
		{
			if (SlotOccupied[Slot])
			{
				continue;
			}
			const int32 GapRank = Slot / Formation.NumFiles;
			for (int32 Behind = (GapRank + 1) * Formation.NumFiles;
				Behind < FMath::Min(Formation.NumSlots, (GapRank + 2) * Formation.NumFiles); ++Behind)
			{
				if (SlotOccupied[Behind])
				{
					bRankGap = true;
					break;
				}
			}
		}

		FReflowScheduler Scheduler;
		Scheduler.IntervalSeconds = 0.5f;
		Scheduler.Cooldown = Formation.ReflowCooldown;
		// Layout re-decision (order consumed / morph / refront arrival bumps
		// Generation): re-assign immediately and UNBOUNDED -- each entity takes
		// its nearest slot in the new layout instead of a k-bounded shuffle that
		// would force treks across a rotated block (arch §8.4 re-decision path).
		// R-G4: reflow NEVER runs while filing a garrison interior path (the
		// PassLocked column filing owns the shape; spec §4.4).
		const bool bLayoutRedecision = Formation.Generation != Formation.AssignedGeneration;
		if (!bFiling && (bLayoutRedecision || Scheduler.ConsumeIfDue(DT, bRankGap)))
		{
			TArray<int32> NewAssignment;
			const int32 MaxDisplacement = bLayoutRedecision
				? Formation.NumSlots
				: GBattleMovementMaxFileDisplacement.GetValueOnAnyThread();
			FSlotAssigner::Assign(EntityPosM, SlotPosM, Formation.NumFiles, Assignment,
				MaxDisplacement, NewAssignment);

			// Predictive swaps (optional policy, arch §8.4): crossing servo
			// projections within ~1 s swap slot indices. Skipped while filing
			// (deterministic column shape through the choke).
			if (!bFiling && GBattleMovementPredictiveSwaps.GetValueOnAnyThread() != 0)
			{
				TArray<FVector2D> VelM;
				VelM.Reserve(NumEntities);
				for (int32 i = 0; i < NumEntities; ++i)
				{
					const FVector Vel = Context.GetFragmentView<FMassVelocityFragment>()[i].Value;
					VelM.Add(FVector2D(Vel.X, Vel.Y) * 0.01f);
				}
				TArray<FIntPoint> Pairs;
				FSlotAssigner::FindCrossingPairs(EntityPosM, VelM, NewAssignment, SlotPosM, 1.0f, 0.5f, Pairs);
				FSlotAssigner::ApplyPredictiveSwaps(NewAssignment, Pairs);
			}

			// Adopt.
			for (int32 i = 0; i < NumEntities; ++i)
			{
				Context.GetMutableFragmentView<FSlotAssignmentFragment>()[i].SlotIndex = NewAssignment[i];
			}
			Assignment = MoveTemp(NewAssignment);
			Formation.AssignedGeneration = Formation.Generation;
		}
		Formation.ReflowCooldown = Scheduler.Cooldown;

		// ---- Write move targets + slot-error overlay (A3) ----
		UBattleDebugSubsystem* Debug = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
		TArrayView<FSlotTargetFragment> Targets = Context.GetMutableFragmentView<FSlotTargetFragment>();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			const int32 Slot = Assignment[i];
			if (Slot < 0 || Slot >= Formation.NumSlots)
			{
				continue;
			}
			Targets[i].TargetPos = FVector2D(SlotPosM[Slot]) * 100.f;
			Targets[i].TargetYaw = SlotYaw[Slot];
			// Garrison filing (spec §6): TargetZ interpolates along the path
			// at the entity's rank arc; field movement stays at ground Z.
			Targets[i].TargetZ = bFiling && Formation.Path.IsValid()
				? FGarrisonLayout::ZAtSFragment(Formation.Path,
					FMath::Max(0.f, Formation.PathS - static_cast<float>(Slot / Formation.NumFiles) * Formation.RankSpacing))
				: 0.f;

			if (Debug)
			{
				FBattleOverlayLine Line;
				Line.From = Context.GetFragmentView<FTransformFragment>()[i].GetTransform().GetTranslation();
				Line.To = FVector(Targets[i].TargetPos.X, Targets[i].TargetPos.Y, Line.From.Z);
				// Magnitude-graded color: green near slot -> red far.
				const float ErrorM = FVector2D::Distance(EntityPosM[i], SlotPosM[Slot]);
				const uint8 Magnitude = static_cast<uint8>(FMath::Clamp(ErrorM * 25.5f, 0.f, 255.f));
				Line.Color = FColor(255, 255 - Magnitude, 0);
				Debug->EnqueueOverlayLine(Line);
			}
		}
	});
}
