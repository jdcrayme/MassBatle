#include "FormationSlotResolveProcessor.h"
#include "BattleMovementFragments.h"
#include "BattleSlotAssigner.h"
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

		// Slot world positions (meters): path-frame when a path exists, the
		// §8.2 anchor-formula degenerate layout otherwise.
		FBattlePlannedPath PathM;
		const bool bHasPath = FPathFrameLayout::FromFragment(Formation.Path, PathM);
		const float FileSpacingM = Formation.FileSpacing * 0.01f;
		const float RankSpacingM = Formation.RankSpacing * 0.01f;

		TArray<FVector2D> SlotPosM;
		TArray<float> SlotYaw;
		SlotPosM.SetNumUninitialized(Formation.NumSlots);
		SlotYaw.SetNumUninitialized(Formation.NumSlots);
		for (int32 Slot = 0; Slot < Formation.NumSlots; ++Slot)
		{
			const int32 Rank = Slot / Formation.NumFiles;
			const int32 File = Slot % Formation.NumFiles;
			FVector2D Pos;
			float YawDeg;
			if (bHasPath)
			{
				(void)FPathFrameLayout::Project(PathM, PathM.TotalLength, Rank, File, Formation.NumFiles,
					RankSpacingM, FileSpacingM, Pos, YawDeg);
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
		if (Scheduler.ConsumeIfDue(DT, bRankGap))
		{
			TArray<int32> NewAssignment;
			FSlotAssigner::Assign(EntityPosM, SlotPosM, Formation.NumFiles, Assignment,
				GBattleMovementMaxFileDisplacement.GetValueOnAnyThread(), NewAssignment);

			// Predictive swaps (optional policy, arch §8.4): crossing servo
			// projections within ~1 s swap slot indices.
			if (GBattleMovementPredictiveSwaps.GetValueOnAnyThread() != 0)
			{
				TArray<FVector2D> VelM;
				VelM.Reserve(NumEntities);
				for (int32 i = 0; i < NumEntities; ++i)
				{
					const FVector Vel = Context.GetFragmentView<FMassVelocityFragment>()[i].Value;
					VelM.Add(FVector2D(Vel.X, Vel.Y) * 0.01f);
				}
				TArray<FIntPoint> Pairs;
				FSlotAssigner::FindCrossingPairs(EntityPosM, VelM, NewAssignment, 1.0f, 0.5f, Pairs);
				FSlotAssigner::ApplyPredictiveSwaps(NewAssignment, Pairs);
			}

			// Adopt.
			for (int32 i = 0; i < NumEntities; ++i)
			{
				Context.GetMutableFragmentView<FSlotAssignmentFragment>()[i].SlotIndex = NewAssignment[i];
			}
			Assignment = MoveTemp(NewAssignment);
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
