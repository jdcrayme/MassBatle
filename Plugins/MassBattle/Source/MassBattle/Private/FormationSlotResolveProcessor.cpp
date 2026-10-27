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

	// TW-style fan-in peel (arch §8.1 amendment): while a field path exists,
	// an entity targets its destination-layout slot whenever that is the
	// closer walk than its traveling slot -- entities stream directly into
	// their final positions during the approach instead of holding the rigid
	// block until anchor arrival and re-dressing afterwards. This deadband
	// (m) keeps the decision boundary from flip-flopping; <= 0 disables.
	TAutoConsoleVariable<float> GBattleMovementFanInHysteresis(
		TEXT("Battle.Movement.FanInHysteresis"),
		0.3f,
		TEXT("Fan-in peel hysteresis deadband, m; the destination slot must be closer by this much (<= 0 disables the peel)."),
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
		// Audit H4: per-thread scratch reused across chunks and frames
		// (Reset/Init keep capacity) -- zero steady-state heap traffic from
		// the slot-resolve stage at Phase-1 scale or at the 2x2400 target.
		static thread_local TArray<FVector2D> TLEntityPosM, TLPostPosM, TLFinalSlotPosM, TLSlotPosM, TLFanInVelM;
		static thread_local TArray<float> TLPostYaw, TLPostZCm, TLSlotYaw;
		static thread_local TArray<int32> TLAssignment, TLNewAssignment;
		static thread_local TArray<bool> TLSlotOccupied;
		static thread_local FBattlePlannedPath TLPathM;

		FUnitFormationSharedFragment& Formation = Context.GetMutableSharedFragment<FUnitFormationSharedFragment>();
		const int32 NumEntities = Context.GetNumEntities();
		if (Formation.NumSlots <= 0 || Formation.NumFiles <= 0)
		{
			return; // layout never initialized
		}

		// ---- Assemble assigner inputs (meters at the fragment boundary) ----
		TArray<FVector2D>& EntityPosM = TLEntityPosM;
		EntityPosM.Reset(NumEntities);
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
			TArray<FVector2D>& PostPosM = TLPostPosM;
			TArray<float>& PostYaw = TLPostYaw;
			TArray<float>& PostZCm = TLPostZCm;
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
			TArray<int32>& Assignment = TLAssignment;
			Assignment.Reset(NumEntities);
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
		FBattlePlannedPath& PathM = TLPathM;
		const bool bHasPath = FPathFrameLayout::FromFragment(Formation.Path, PathM);
		const bool bFiling = Formation.GarrisonMode != EGarrisonMode::None
			&& (Formation.GarrisonPhase == EGarrisonPhase::Ingress
				|| Formation.GarrisonPhase == EGarrisonPhase::Egress);
		const float FileSpacingM = Formation.FileSpacing * 0.01f;
		const float RankSpacingM = Formation.RankSpacing * 0.01f;

		TArray<FVector2D>& SlotPosM = TLSlotPosM;
		TArray<float>& SlotYaw = TLSlotYaw;
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

		// ---- TW fan-in destination layout (arch §8.1 amendment) ----
		// Static anchor-formula layout at the ordered destination/facing,
		// computed only when UnitLocomotion armed the frame for this field
		// move. Assignment keeps matching on the TRAVELING layout (block-
		// coherent slot identity, stable predictive swaps); the peel applies
		// per entity at target-write time below. Open-order spacing (scale 1):
		// the destination is a formed block, not a squeezed one.
		const bool bFanIn = bHasPath && Formation.FanInDestValid != 0;
		TArray<FVector2D>& FinalSlotPosM = TLFinalSlotPosM;
		if (bFanIn)
		{
			FinalSlotPosM.SetNumUninitialized(Formation.NumSlots);
			for (int32 Slot = 0; Slot < Formation.NumSlots; ++Slot)
			{
				const int32 Rank = Slot / Formation.NumFiles;
				const int32 File = Slot % Formation.NumFiles;
				FinalSlotPosM[Slot] = FPathFrameLayout::AnchorFormula(
					Formation.FinalAnchorPos * 0.01f, Formation.FinalFacingYaw,
					Rank, File, Formation.NumFiles, RankSpacingM, FileSpacingM);
			}
		}

		// ---- Batched reflow (arch §8.4): 0.5 s cadence or rank-gap trigger ----
		TArray<int32>& Assignment = TLAssignment;
		Assignment.Reset(NumEntities);
		TArray<bool>& SlotOccupied = TLSlotOccupied;
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
			TArray<int32>& NewAssignment = TLNewAssignment;
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
				TArray<FVector2D>& VelM = TLFanInVelM;
				VelM.Reset(NumEntities);
				for (int32 i = 0; i < NumEntities; ++i)
				{
					const FVector Vel = Context.GetFragmentView<FMassVelocityFragment>()[i].Value;
					VelM.Add(FVector2D(Vel.X, Vel.Y) * 0.01f);
				}
				TArray<FIntPoint> Pairs;
				FSlotAssigner::FindCrossingPairs(EntityPosM, VelM, NewAssignment, SlotPosM, 1.0f, 0.5f, Pairs);
				FSlotAssigner::ApplyPredictiveSwaps(NewAssignment, Pairs, EntityPosM, SlotPosM);
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
		const float FanInHystM = GBattleMovementFanInHysteresis.GetValueOnAnyThread();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			const int32 Slot = Assignment[i];
			if (Slot < 0 || Slot >= Formation.NumSlots)
			{
				continue;
			}
			// TW fan-in peel: the destination slot wins when it is the closer
			// walk for THIS entity (straight-line, the TW rule). The deadband
			// keeps the boundary from flip-flopping -- once an entity steps
			// toward the final slot, that side only gets closer. Peeled
			// entities already face the ordered arrival facing; the servo's
			// per-state teammate avoidance covers the crossing streams, and
			// the arrival facing snap in UnitLocomotion makes the post-arrival
			// anchor frame coincide with the peeled targets.
			FVector2D TargetPosM = SlotPosM[Slot];
			float TargetYawDeg = SlotYaw[Slot];
			if (bFanIn && FanInHystM > 0.f)
			{
				const float DistTravelM = FVector2D::Distance(EntityPosM[i], SlotPosM[Slot]);
				const float DistFinalM = FVector2D::Distance(EntityPosM[i], FinalSlotPosM[Slot]);
				if (DistFinalM + FanInHystM < DistTravelM)
				{
					TargetPosM = FinalSlotPosM[Slot];
					TargetYawDeg = Formation.FinalFacingYaw;
				}
			}
			Targets[i].TargetPos = FVector2D(TargetPosM) * 100.f;
			Targets[i].TargetYaw = TargetYawDeg;
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
				// Magnitude-graded color: green near slot -> red far (measured
				// against the target actually written, peel included).
				const float ErrorM = FVector2D::Distance(EntityPosM[i], TargetPosM);
				const uint8 Magnitude = static_cast<uint8>(FMath::Clamp(ErrorM * 25.5f, 0.f, 255.f));
				Line.Color = FColor(255, 255 - Magnitude, 0);
				Debug->EnqueueOverlayLine(Line);
			}
		}
	});
}
