#include "FormationSlotResolveProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleCombatFragments.h"
#include "BattleSlotAssigner.h"
#include "BattleUnitLocomotion.h"
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

	// Formation-flow rule 1 (owner proposal 2026-10-28): directional gap
	// closure. After every greedy (re)assignment, vacant slots fill from
	// their directly-behind occupant (fill front) or directly-right occupant
	// (fill left), so holes drain to the rear-right corner instead of
	// persisting mid-formation while the unit marches. 0 keeps the greedy
	// result byte-for-byte. Rule 2 (stuck re-homing) is gated servo-side by
	// Battle.Movement.StuckReawardLevel -- the A/B matrix:
	//   GapCascade=0, StuckReawardLevel=0 -> legacy byte-for-byte
	//   GapCascade=1, StuckReawardLevel=0 -> rule 1 only (gap closure)
	//   GapCascade=0, StuckReawardLevel>0 -> rule 2 only (stuck re-homing)
	//   GapCascade=1, StuckReawardLevel>0 -> full formation flow
	TAutoConsoleVariable<int32> GBattleMovementGapCascade(
		TEXT("Battle.Movement.GapCascade"),
		1,
		TEXT("1 = directional gap closure after slot assignment: fill front / fill left, holes drain to the rear-right (owner formation-flow rule 1)."),
		ECVF_Default);

	// Formation flow v2 (owner observation 2026-10-28): the distance at which a
	// HELD slot claim goes stale -- the entity is "nowhere near it" and the
	// claim is dropped so the vacancy can diffuse (DiffuseVacancies: evict,
	// cascade, re-home). v1's cascade never fired in a full unit because the
	// greedy bijection leaves no INDEX_NONE slots: the holes the owner observed
	// were VIRTUAL (claimed-but-far), invisible to the rank-gap trigger and to
	// CloseGaps alike. This radius converts them into real vacancies. Calibrated
	// well beyond dressing jitter (SlotReachEps 0.35) and the unit-level
	// DressBreak (1.8): 3.0 m is unambiguously "not in the slot" while a
	// mid-catchup entity walking its slot stays under it. 0 disables -- v1
	// behavior (CloseGaps only) byte-for-byte.
	TAutoConsoleVariable<float> GBattleMovementStaleClaimRadius(
		TEXT("Battle.Movement.StaleClaimRadius"),
		3.f,
		TEXT("Distance from its held slot at which an entity's claim goes stale and the vacancy diffuses (formation-flow v2; 0 = off)."),
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

	// Phase 2 B3 (F-7): march stickiness -- on a dressed march the 0.5 s
	// interval reflow + predictive swaps are churn, not information. While a
	// path is active and the block is dressed (MaxSlotErrorM under the
	// shared DressBreak threshold) they are skipped; rank-gap and layout
	// re-decision triggers still re-slot immediately.
	TAutoConsoleVariable<int32> GBattleMovementMarchStickiness(
		TEXT("Battle.Movement.MarchStickiness"),
		0,
		TEXT("1 = skip the interval reflow + predictive swaps while a path is active and the block is dressed (phase 2 B3)."),
		ECVF_Default);

	// Phase 2 C master switch (F-1/F-2): TW progressive fall-in. Gates C2
	// (sort-matching final assignment at layout re-decisions), C3 (the
	// march->final target blend with the per-soldier handoff latch), C4's
	// arrival-deletion (UnitLocomotion side) and C5 (generalized rest-facing,
	// Integrate side).
	// OWNER DEFAULT FLIP (2026-10-28, formation-flow v2): 0 kept the pre-phase-2
	// pipeline byte-for-byte for baseline A/B; the owner's symptom report
	// (tail entities dragged back, overshoot at the moving slot, late fall-back
	// to finals) IS the flag-off pipeline -- the binary fan-in peel flips
	// targets across a 0.3 m deadband and the servo settle basin (the damped
	// final meter that kills the micro-orbit) rides this same switch. The
	// phase-2 C machinery is built and measured; it now ships ON. Restore the
	// legacy tier with `Battle.Movement.ProgressiveFallIn 0`.
	TAutoConsoleVariable<int32> GBattleMovementProgressiveFallIn(
		TEXT("Battle.Movement.ProgressiveFallIn"),
		1,
		TEXT("1 = TW progressive fall-in: final slots from order time, target blend march->final, early handoff, no arrival re-decision (phase 2 C)."),
		ECVF_Default);

	// Remaining-path length at which the whole block starts blending into the
	// final layout, m. Floored at 2x formation depth at the call site (deep
	// formations get proportionally longer morphs; plan section 4.1).
	// ERRATA (measured, phase 2): the plan's 12 m default only budgets the
	// DEPTH term; a facing flip at arrival (the P1 repro, 90 deg) needs the
	// FRONTAGE rotation to resolve during the approach -- corner walks of
	// ~9 m at crowd-impeded lateral speed want ~17 s of march, so the default
	// rides at 24 m. Owner-tunable.
	TAutoConsoleVariable<float> GBattleMovementMorphDistance(
		TEXT("Battle.Movement.MorphDistance"),
		24.f,
		TEXT("Remaining path length where the fall-in blend engages, m (phase 2 C3)."),
		ECVF_Default);

	// Per-soldier early handoff distance to the final slot, m (plan: 2x
	// SlotReachEps). Within it a soldier locks his target to the final slot
	// and settles while the rest still march.
	TAutoConsoleVariable<float> GBattleMovementHandoffDist(
		TEXT("Battle.Movement.HandoffDist"),
		0.7f,
		TEXT("Per-soldier early handoff distance to the final slot, m (phase 2 C3)."),
		ECVF_Default);

	// Phase 2 C5 (F-8) ERRATA home (IntegrateProcessor's per-tick lookup
	// documents this spot): the per-man settle-facing rate, rad/s. The
	// shared FacingDeltaRate (0.35 rad/s) is the BLOCK refront cap -- a man
	// turning 180 deg needs 9 s at 20 deg/s and the settle window times
	// out (S7 measured offYaw 27/40 at 5 s); ~1 s per pivot is the
	// measured fit. The registration was missing through the 2026-10-28
	// demo session (486 failed FindConsoleObject warnings; the console
	// could not set the rate and the lookup silently took the fallback).
	TAutoConsoleVariable<float> GBattleMovementSettleYawRate(
		TEXT("Battle.Movement.SettleYawRate"),
		2.f,
		TEXT("Rest-facing turn rate for field entities, rad/s (phase 2 C5; the 0.35 block refront rate is not a man turn)."),
		ECVF_Default);
}

UFormationSlotResolveProcessor::UFormationSlotResolveProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ResolveQuery(*this)
	, ShowOverlayCvar(nullptr)
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
	// Task 10 debug pass: the slot-circle colors ride the combat pair state
	// (written by the assign pass, game thread -- read-only here, worker-safe).
	ResolveQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadOnly);
	// Audit M8: death-window corpses are excluded from the movement chain --
	// they must not be re-slotted or servoed toward targets while the
	// destroy is in flight (the Phase 0 M1 standard).
	ResolveQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
}

void UFormationSlotResolveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GBattleMovementSlotResolve.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();
	// Audit M9: the overlay CVAR is read once per Execute (pointer cached --
	// stable for the process) so workers do not construct overlay lines and
	// take the debug mutex per entity per frame while the overlay is off.
	if (!ShowOverlayCvar)
	{
		ShowOverlayCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Debug.ShowOverlay"));
	}
	const bool bOverlay = ShowOverlayCvar && ShowOverlayCvar->GetInt() != 0;

	// Phase 2 B3: the shared DressBreak threshold (defined beside the pace
	// term in UnitLocomotionProcessor.cpp; cached pointer, audit M9 pattern).
	if (!DressBreakCvar)
	{
		DressBreakCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.DressBreak"));
	}
	const float DressBreakM = DressBreakCvar ? DressBreakCvar->GetFloat() : 1.8f;

	// Phase 2 C inputs (read once per Execute, captured by value like DT).
	const bool bFallIn = GBattleMovementProgressiveFallIn.GetValueOnAnyThread() != 0;
	const float MorphDistCvarM = GBattleMovementMorphDistance.GetValueOnAnyThread();
	const float HandoffDistM = GBattleMovementHandoffDist.GetValueOnAnyThread();

	ResolveQuery.ForEachEntityChunk(Context, [DT, bOverlay, DressBreakM, bFallIn, MorphDistCvarM, HandoffDistM](FMassExecutionContext& Context)
	{
		// Audit H4: per-thread scratch reused across chunks and frames
		// (Reset/Init keep capacity) -- zero steady-state heap traffic from
		// the slot-resolve stage at Phase-1 scale or at the 2x2400 target.
		static thread_local TArray<FVector2D> TLEntityPosM, TLPostPosM, TLFinalSlotPosM, TLSlotPosM, TLFanInVelM;
		static thread_local TArray<float> TLPostYaw, TLPostZCm, TLSlotYaw;
		static thread_local TArray<int32> TLAssignment, TLNewAssignment;
		static thread_local TArray<bool> TLSlotOccupied;
		static thread_local TArray<uint8> TLStuckFlags;
		static thread_local TArray<uint8> TLStaleSkipFlags;
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
				TArrayView<FSlotAssignmentFragment> HoldSlots = Context.GetMutableFragmentView<FSlotAssignmentFragment>();
				for (int32 i = 0; i < NumEntities; ++i)
				{
					HoldSlots[i].SlotIndex = NewAssignment[i] >= 0 ? NewAssignment[i] + SlotBase : INDEX_NONE;
					HoldSlots[i].Generation = Formation.Generation; // audit L12: the mirror is now live
				}
				Assignment = MoveTemp(NewAssignment);
				Formation.AssignedGeneration = Formation.Generation;
			}
			Formation.ReflowCooldown = Scheduler.Cooldown;

			// Write move targets + slot-error overlay (A3). Audit M9: the
			// enqueue is gated on the overlay toggle -- the mutex per entity
			// per frame was pure contention against the game-thread drain.
			UBattleDebugSubsystem* DebugHold = bOverlay && Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
			TArrayView<FSlotTargetFragment> HoldTargets = Context.GetMutableFragmentView<FSlotTargetFragment>();
			float HoldMaxSlotErrorM = 0.f;
			float HoldSlotErrorSum = 0.f;
			int32 HoldSlotErrorCount = 0;
			for (int32 i = 0; i < NumEntities; ++i)
			{
				const int32 Slot = Assignment[i];
				if (Slot < 0 || Slot >= Count)
				{
					continue;
				}
				// Phase 2 A1 feed-forward: stamp the previous target BEFORE the
				// overwrite so the servo can form the slot's own velocity. First
				// write: Prev = the new target (no slot motion on the first frame).
				const FVector2D NewTargetPos(FVector2D(PostPosM[Slot]) * 100.f);
				HoldTargets[i].PrevTargetPos = HoldTargets[i].bPrevValid ? HoldTargets[i].TargetPos : NewTargetPos;
				HoldTargets[i].bPrevValid = 1;
				HoldTargets[i].TargetPos = NewTargetPos;
				HoldTargets[i].TargetYaw = PostYaw[Slot];
				HoldTargets[i].TargetZ = PostZCm[Slot];

				// Phase 2 B1: the same reduction rides the garrison branch.
				{
					const float ErrorM = FVector2D::Distance(EntityPosM[i], PostPosM[Slot]);
					HoldMaxSlotErrorM = FMath::Max(HoldMaxSlotErrorM, ErrorM);
					HoldSlotErrorSum += ErrorM;
					++HoldSlotErrorCount;
				}

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
			Formation.MaxSlotErrorM = HoldMaxSlotErrorM;
			Formation.MeanSlotErrorM = HoldSlotErrorCount > 0 ? HoldSlotErrorSum / HoldSlotErrorCount : 0.f;
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
		// Phase 2 C: under the master switch the frame stays armed across the
		// path drop (C4 keeps it valid post-arrival), so targets are
		// continuous across arrival -- the no-path anchor-formula layout at
		// scale 1 coincides with the finals (AnchorPos == FinalAnchorPos,
		// FacingYaw == FinalFacingYaw at arrival). Flag-off keeps the old
		// bHasPath requirement byte-for-byte.
		const bool bFanIn = !bFiling && Formation.FanInDestValid != 0
			&& (bFallIn || bHasPath);
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

		// Phase 2 C3: unit-level blend weight from the remaining path, floored
		// at 2x formation depth (deep formations morph over proportionally
		// longer distances; plan section 4.1). Far away alpha ~= 0: exactly
		// today's coherent path-frame block. Computed BEFORE the assignment
		// block -- the blend-engagement edge triggers the scheduled final
		// assignment there.
		float AlphaUnit = 1.f;
		if (bFallIn && bHasPath)
		{
			const float RemainingM = FMath::Max(0.f, PathM.TotalLength - Formation.PathS * 0.01f);
			const int32 NumRanksLayout = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation.NumSlots) / Formation.NumFiles));
			AlphaUnit = FUnitLocomotion::FallInBlendAlpha(RemainingM,
				FUnitLocomotion::FallInMorphDistance(MorphDistCvarM, NumRanksLayout, RankSpacingM,
					Formation.NumFiles, FileSpacingM));
		}

		// ---- Batched reflow (arch §8.4): 0.5 s cadence or rank-gap trigger ----
		TArray<int32>& Assignment = TLAssignment;
		Assignment.Reset(NumEntities);
		TArray<bool>& SlotOccupied = TLSlotOccupied;
		SlotOccupied.Init(false, Formation.NumSlots);
		// Formation-flow rule 2 trigger: servo-ladder stuck marks (written by
		// ServoProcessor; one frame of lag at this 0.5 s cadence). A marked
		// entity's slot is physically unreachable -- the reflow must fire and
		// re-home it at the perimeter instead of letting it orbit the block.
		// Formation-flow v2 skip flags: an entity whose motion is owned by a
		// combat ring is exempt from STALE-CLAIM eviction -- its distance to
		// the held slot is combat displacement, not a formation hole, and
		// evicting it every 0.5 s would churn the assignment for no gain (the
		// ring, not the slot, owns the walk).
		TArray<uint8>& StuckFlags = TLStuckFlags;
		StuckFlags.Init(0, NumEntities);
		TArray<uint8>& StaleSkipFlags = TLStaleSkipFlags;
		StaleSkipFlags.Init(0, NumEntities);
		bool bStuckPresent = false;
		const auto CombatPairView = Context.GetFragmentView<FCombatPairFragment>();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			const FSlotAssignmentFragment& SlotFrag = Context.GetFragmentView<FSlotAssignmentFragment>()[i];
			Assignment.Add(SlotFrag.SlotIndex);
			StuckFlags[i] = SlotFrag.StuckMark;
			bStuckPresent = bStuckPresent || SlotFrag.StuckMark != 0;
			StaleSkipFlags[i] = (CombatPairView.Num() == NumEntities && CombatPairView[i].bRingValid != 0) ? 1 : 0;
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
		// Phase 2 C: the SCHEDULED final assignment (Pottinger's scheduled
		// slot positioning). While the unit marches (alpha == 0) the greedy
		// march assignment owns the block -- march-coherent, churn-free. The
		// first tick where the blend engages runs AssignFinal ONCE: from there
		// every soldier walks a crossing-free final approach. With no path at
		// all (in-place reform, post-arrival) the live anchor frame IS the
		// final frame: AssignFinal owns assignment there -- but ONLY on layout
		// re-decisions and rank gaps. ERRATA vs plan C2 (measured): routing
		// the 0.5 s INTERVAL reflow to AssignFinal post-arrival re-sorts the
		// settling crowd every half second (targets flip, nobody dresses --
		// S5's worst entity moved at full servo speed forever); assignment
		// stability after the scheduled swap is the whole TW contract.
		const bool bBlendEngaged = bFallIn && bHasPath && AlphaUnit > KINDA_SMALL_NUMBER;
		const bool bScheduledFinalDue = bBlendEngaged && Formation.bFinalAssignDone == 0;
		const bool bFinalOwnedNoPath = bFallIn && !bFiling && bFanIn && !bHasPath;
		// Phase 2 B3 march stickiness (F-7): while a path is active and the
		// block is dressed, the interval reflow + predictive swaps are churn --
		// skip them. The cooldown still decays, so a later tear reflows on the
		// first due tick; rank gaps, layout re-decisions and stuck entities
		// (formation-flow rule 2: a blocked-out entity must not wait out the
		// sticky gate) bypass it.
		const bool bMarchSticky = GBattleMovementMarchStickiness.GetValueOnAnyThread() != 0
			&& FUnitLocomotion::MarchReflowSticky(bHasPath, Formation.MaxSlotErrorM, DressBreakM);
		bool bIntervalReflowDue = false;
		if ((bMarchSticky || bFinalOwnedNoPath) && !bRankGap && !bStuckPresent)
		{
			// Stickiness (B3) / finals-owned (C): decay without consuming --
			// a later tear or rank gap reflows on the first due tick.
			Scheduler.Cooldown = FMath::Max(0.f, Scheduler.Cooldown - DT);
		}
		else if (!bFiling && (bLayoutRedecision || bScheduledFinalDue))
		{
			// Re-decision / scheduled final assignment: fire WITHOUT touching
			// the interval cadence. Flag-off byte-compatibility demands the
			// legacy short-circuit exactly -- the pre-phase-2 condition never
			// called ConsumeIfDue on these paths, and shifting the cadence by
			// up to half a second flips cadence-sensitive gates (S3 measured:
			// grind-through at the pass mouth).
			bIntervalReflowDue = true;
		}
		else
		{
			bIntervalReflowDue = Scheduler.ConsumeIfDue(DT, bRankGap);
		}
		if (!bFiling && (bLayoutRedecision || bScheduledFinalDue || bIntervalReflowDue))
		{
			TArray<int32>& NewAssignment = TLNewAssignment;
			const bool bUseFinalAssignment = bFinalOwnedNoPath
				? (bLayoutRedecision || bIntervalReflowDue)
				: (bFallIn && bFanIn && bHasPath && bScheduledFinalDue);
			if (bUseFinalAssignment)
			{
				// Crossing-free sort matching onto the FINAL layout (the
				// blend is engaged, or no path exists and the live anchor
				// frame IS the final frame).
				FSlotAssigner::AssignFinal(EntityPosM, Formation.FinalAnchorPos * 0.01f,
					Formation.FinalFacingYaw, Formation.NumSlots, Formation.NumFiles, NewAssignment);
				if (bHasPath)
				{
					Formation.bFinalAssignDone = 1; // once per traversal
				}
			}
			else
			{
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

				// ---- Formation flow (owner proposal 2026-10-28) ----
				// Rule 2 (before rule 1: the eviction's vacated sink is
				// exactly the vacancy CloseGaps should drain). Stuck-marked
				// entities claim their physically nearest slot regardless of
				// occupancy; the displaced owners chain one slot each toward
				// the vacated interior slot. A blocked-out entity re-homes at
				// the perimeter in ONE reflow instead of orbiting the block.
				// Gated servo-side by Battle.Movement.StuckReawardLevel --
				// no marks, no call.
				if (bStuckPresent)
				{
					FSlotAssigner::EvictToReachable(NewAssignment, EntityPosM, SlotPosM,
						Formation.NumSlots, Formation.NumFiles, StuckFlags);
				}
				// Formation-flow v2 (owner observation: "the slot is claimed
				// even though the entity is nowhere near it"): DISTANCE-
				// triggered vacancy diffusion. The v1 triggers never saw the
				// holes the owner observed -- CloseGaps keys on INDEX_NONE
				// (a full bijection has none) and EvictToReachable keys on
				// the SLOW-ladder (an orbiting entity moves at full servo
				// speed and never escalates). DiffuseVacancies evicts claims
				// held from beyond Battle.Movement.StaleClaimRadius, lets
				// the cascade pull the neighbors in, and re-homes the
				// evicted at the hole nearest their body -- one reflow, no
				// orbit. Gates: NOT on the layout re-decision frame (the
				// layout just changed; distances are transition noise the
				// unbounded Assign already sorted), NOT inside the C3 blend
				// (the morph tear is the intended shape change -- the blend
				// owns convergence, same doctrine as the pace throttle's
				// AlphaNow gate), and only while the fall-in machinery owns
				// the approach (bFallIn) or on standing frames (no path:
				// travel slots ARE the anchor slots, the binary peel is
				// inert and staleness is real).
				if (!bBlendEngaged && !bLayoutRedecision && (bFallIn || !bHasPath))
				{
					FSlotAssigner::DiffuseVacancies(NewAssignment, EntityPosM, SlotPosM,
						Formation.NumSlots, Formation.NumFiles,
						GBattleMovementStaleClaimRadius.GetValueOnAnyThread(), StaleSkipFlags);
				}
				// Rule 1: directional gap closure -- fill front, then fill
				// left; holes drain to the rear-right corner (AssignFinal's
				// surplus corner, so the finals sort and the cadence agree).
				// Pure permutation on the greedy bijection.
				if (GBattleMovementGapCascade.GetValueOnAnyThread() != 0)
				{
					FSlotAssigner::CloseGaps(NewAssignment, Formation.NumSlots, Formation.NumFiles);
				}
			}

			// Adopt (audit L12: stamp the layout generation the slot was
			// assigned under -- the field was written at spawn and never
			// touched again before). Phase 2 C3: handoff latches die with the
			// old assignment.
			TArrayView<FSlotAssignmentFragment> SlotsView = Context.GetMutableFragmentView<FSlotAssignmentFragment>();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				SlotsView[i].SlotIndex = NewAssignment[i];
				SlotsView[i].Generation = Formation.Generation;
				SlotsView[i].bFinalLocked = 0;
			}
			Assignment = MoveTemp(NewAssignment);
			Formation.AssignedGeneration = Formation.Generation;
		}
		Formation.ReflowCooldown = Scheduler.Cooldown;

		// ---- Write move targets + slot-error overlay (A3; M9-gated) ----
		UBattleDebugSubsystem* Debug = bOverlay && Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
		TArrayView<FSlotTargetFragment> Targets = Context.GetMutableFragmentView<FSlotTargetFragment>();
		TArrayView<FSlotAssignmentFragment> AssignFrags = Context.GetMutableFragmentView<FSlotAssignmentFragment>();
		const float FanInHystM = GBattleMovementFanInHysteresis.GetValueOnAnyThread();
		// Phase 2 B1: running max over the written targets (zero allocation).
		// Measured against the target actually written (peel/blend included) --
		// this is the number the straggler pace term, march stickiness, the
		// debug HUD and the soak assert all consume.
		float MaxSlotError = 0.f;
		float SlotErrorSum = 0.f;
		int32 SlotErrorCount = 0;
		for (int32 i = 0; i < NumEntities; ++i)
		{
			const int32 Slot = Assignment[i];
			if (Slot < 0 || Slot >= Formation.NumSlots)
			{
				continue;
			}
			// TW fan-in target selection. Two regimes:
			//   flag OFF: the binary peel -- the destination slot wins when it
			//     is the closer walk for THIS entity (deadband against
			//     flip-flop). Pre-phase-2 behavior, byte-for-byte.
			//   flag ON (C3): the progressive blend -- target = lerp(march,
			//     final, alpha), alpha from the remaining path (whole block)
			//     with the per-soldier handoff latch (early TW settle).
			// PrevTargetPos was stamped pre-selection above: the feed-forward
			// tracks the MOVING blended target, which is correct -- that is
			// the slot the soldier walks to.
			FVector2D TargetPosM = SlotPosM[Slot];
			float TargetYawDeg = SlotYaw[Slot];
			if (bFallIn && bFanIn && HandoffDistM > 0.f)
			{
				const float DistFinalM = FVector2D::Distance(EntityPosM[i], FinalSlotPosM[Slot]);
				AssignFrags[i].bFinalLocked = FUnitLocomotion::FallInHandoffLatch(
					AssignFrags[i].bFinalLocked != 0, DistFinalM, HandoffDistM) ? 1 : 0;
				const float Alpha = AssignFrags[i].bFinalLocked ? 1.f : AlphaUnit;
				TargetPosM = FMath::Lerp(SlotPosM[Slot], FinalSlotPosM[Slot], Alpha);
				// Facing blends shortest-arc from the (path-tangent) slot yaw
				// to the final facing -- settled entities face the order.
				TargetYawDeg = SlotYaw[Slot]
					+ FMath::FindDeltaAngleDegrees(SlotYaw[Slot], Formation.FinalFacingYaw) * Alpha;
			}
			else if (bFanIn && FanInHystM > 0.f)
			{
				const float DistTravelM = FVector2D::Distance(EntityPosM[i], SlotPosM[Slot]);
				const float DistFinalM = FVector2D::Distance(EntityPosM[i], FinalSlotPosM[Slot]);
				if (DistFinalM + FanInHystM < DistTravelM)
				{
					TargetPosM = FinalSlotPosM[Slot];
					TargetYawDeg = Formation.FinalFacingYaw;
				}
			}
			// Phase 2 A1 feed-forward: stamp the previous target BEFORE the
			// overwrite (pre-blend, per the plan -- the servo tracks the
			// moving blended target, which is the slot the soldier walks to).
			// First write: Prev = the new target (no slot motion frame one).
			const FVector2D NewTargetPos(FVector2D(TargetPosM) * 100.f);
			Targets[i].PrevTargetPos = Targets[i].bPrevValid ? Targets[i].TargetPos : NewTargetPos;
			Targets[i].bPrevValid = 1;
			Targets[i].TargetPos = NewTargetPos;
			Targets[i].TargetYaw = TargetYawDeg;
			// Phase 2 B1: fold this entity's error into the unit metric.
			{
				const float ErrorM = FVector2D::Distance(EntityPosM[i], TargetPosM);
				MaxSlotError = FMath::Max(MaxSlotError, ErrorM);
				SlotErrorSum += ErrorM;
				++SlotErrorCount;
			}
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
		// Task 10 debug pass (owner request; B.2 pairing/state rows): per-slot
		// final circles (violet, where the unit converges) + per-entity
		// movement-slot circles (state-colored: white moving, cyan seeking,
		// yellow pressing, red striking, orange recovering) + the entity-to-
		// slot line. All under the overlay master switch; volume note: ~10
		// segments per entity per frame at full volume -- an opt-in tool.
		if (Debug)
		{
			constexpr float Z = 20.f; // field paths are flat; ground Z
			// Final-slot circles (violet; per slot -- holes read as missing
			// circles, which IS the casualty picture). Gated by bFanIn: the
			// finals array is sized/filled only there (the old ghost-cross
			// gate; indexing it otherwise is an empty-array read -- measured
			// 2026-10-28 boot crash).
			if (bFanIn)
			{
				for (int32 Slot = 0; Slot < Formation.NumSlots; ++Slot)
				{
					const FVector2D G = FinalSlotPosM[Slot] * 100.f;
					FBattleOverlayLine Final;
					Final.Shape = EBattleOverlayShape::Circle;
					Final.From = FVector(G.X, G.Y, Z);
					Final.Radius = 40.f;
					Final.Color = FColor(170, 0, 255);
					Debug->EnqueueOverlayLine(Final);
				}
			}
			// Movement circles + entity-to-slot lines (per entity).
			const auto Transforms = Context.GetFragmentView<FTransformFragment>();
			const auto CombatPairs = Context.GetFragmentView<FCombatPairFragment>();
			const auto StateColor = [](ECombatPairState State) -> FColor
			{
				switch (State)
				{
				case ECombatPairState::Seeking: return FColor(0, 200, 220);
				case ECombatPairState::Pressing: return FColor(240, 220, 0);
				case ECombatPairState::Striking: return FColor(255, 40, 40);
				case ECombatPairState::Recovering: return FColor(255, 150, 40);
				default: return FColor(220, 220, 220); // not in combat: white
				}
			};
			for (int32 i = 0; i < Context.GetNumEntities(); ++i)
			{
				const FVector P = Transforms[i].GetTransform().GetTranslation();
				const FVector2D Slot = Targets[i].TargetPos;
				const FColor Color = StateColor(CombatPairs[i].State);
				FBattleOverlayLine Circle;
				Circle.Shape = EBattleOverlayShape::Circle;
				Circle.From = FVector(Slot.X, Slot.Y, Z);
				Circle.Radius = 40.f;
				Circle.Color = Color;
				Debug->EnqueueOverlayLine(Circle);
				FBattleOverlayLine ToSlot;
				ToSlot.From = FVector(P.X, P.Y, Z);
				ToSlot.To = FVector(Slot.X, Slot.Y, Z);
				ToSlot.Color = FColor(Color.R, Color.G, Color.B, 140);
				Debug->EnqueueOverlayLine(ToSlot);
			}
		}
		// Phase 2 B1: publish the unit's dress metric (game thread reads it
		// next frame for the straggler pace term; one frame of latency, same
		// channel contract as ReflowCooldown).
		Formation.MaxSlotErrorM = MaxSlotError;
		Formation.MeanSlotErrorM = SlotErrorCount > 0 ? SlotErrorSum / SlotErrorCount : 0.f;
	});
}
