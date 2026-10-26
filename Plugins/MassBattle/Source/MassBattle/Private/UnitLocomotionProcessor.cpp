#include "UnitLocomotionProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleGarrison.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleRegistry.h"
#include "BattleCostGridSubsystem.h"
#include "BattlePathPlanner.h"
#include "BattlePathFrameLayout.h"
#include "BattleUnitLocomotion.h"
#include "BattleUnitContact.h"
#include "BattlePassGeometry.h"
#include "BattleServo.h"
#include "BattleCompression.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// B.8 item 6: feature flag so the half-built locomotion stage opts out of
	// whatever perf/soak baselines measure while it is in flight.
	TAutoConsoleVariable<int32> GBattleMovementLocomotion(
		TEXT("Battle.Movement.Locomotion"),
		1,
		TEXT("Enable UnitLocomotion processing (arch §4 row 1). 0 opts the stage out."),
		ECVF_Default);

	// Default march speed when an order leaves CommandedSpeed at 0 (m/s).
	TAutoConsoleVariable<float> GBattleMovementWalkSpeed(
		TEXT("Battle.Movement.WalkSpeed"),
		1.4f,
		TEXT("Default commanded march speed, m/s."),
		ECVF_Default);

	// Commanded facing-change rate (rad/s) -- the §8.2 separate facing cap.
	// 0.35 rad/s: an in-place refront rotates the block slowly enough that the
	// outer files' slot targets sweep at ~walk speed (tracking the arc instead
	// of cutting chords through the formation -- faster rates jam the re-dress).
	TAutoConsoleVariable<float> GBattleMovementFacingDeltaRate(
		TEXT("Battle.Movement.FacingDeltaRate"),
		0.35f,
		TEXT("Commanded facing-change rate, rad/s (arch §8.2 facing cap)."),
		ECVF_Default);

	// §8.2 hysteresis fraction on the speed limit.
	TAutoConsoleVariable<float> GBattleMovementSpeedHysteresis(
		TEXT("Battle.Movement.SpeedHysteresis"),
		0.1f,
		TEXT("Speed-limit hysteresis fraction (rises need this margin)."),
		ECVF_Default);
}

UUnitLocomotionProcessor::UUnitLocomotionProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LocomotionQuery(*this)
	, UnitBodyQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("UnitSyncProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry + planner state are game-thread (arch §4 row 1)
}

void UUnitLocomotionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	LocomotionQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadWrite);
	LocomotionQuery.AddSharedRequirement<FUnitStatsSharedFragment>(EMassFragmentAccess::ReadOnly);
	LocomotionQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadWrite);
	LocomotionQuery.AddSubsystemRequirement<UUnitRegistrySubsystem>(EMassFragmentAccess::ReadOnly);

	// Unit bodies for the §8.2 hard rule (no facing change while
	// interpenetrating): one chunk per unit, read-only.
	UnitBodyQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
}

void UUnitLocomotionProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// One-time pipeline-ordering trace (phase-1 Task 2 verify; Display so the
	// func-tier boot log carries it without log-level changes -- not hot path).
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine UnitLocomotion first-execute"));
	}

	if (GBattleMovementLocomotion.GetValueOnGameThread() == 0)
	{
		return;
	}

	UUnitRegistrySubsystem* Registry = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleCostGridSubsystem* CostGrid = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleCostGridSubsystem>() : nullptr;
	UBattleGarrisonSubsystem* Garrison = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
	if (!Registry || !CostGrid || !Garrison)
	{
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();
	const bool bGarrisonEnabled = FGarrisonCVars::Enable() != 0;

	// B.8 item 3 overlay: the A3 toggle gates the garrison draws too (the
	// cvar lives in BattleDebugSubsystem.cpp; readers go through the manager).
	UBattleDebugSubsystem* Debug = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	const IConsoleVariable* ShowOverlayCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Debug.ShowOverlay"));
	const bool DebugOverlay = ShowOverlayCvar != nullptr && ShowOverlayCvar->GetInt() != 0;

	// ---- Unit bodies (for the §8.2 facing lock) ----
	// R-G3: garrisoned units are EXCLUDED -- their field OBB is meaningless
	// once distributed; interior crowding is servo+compression.
	TArray<FUnitOBB> UnitBodies;
	TArray<FUnitHandle> BodyUnits;
	UnitBodyQuery.ForEachEntityChunk(Context, [&UnitBodies, &BodyUnits](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment* Formation = Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		if (!Formation || Formation->NumSlots <= 0 || Formation->NumFiles <= 0 || Owners.Num() == 0)
		{
			return;
		}
		if (Formation->GarrisonMode != EGarrisonMode::None)
		{
			return; // R-G3 exclusion
		}
		const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation->NumSlots) / Formation->NumFiles));
		const float RadL = FMath::DegreesToRadians(Formation->FacingYaw);
		const FVector2D FrontL(FMath::Cos(RadL), FMath::Sin(RadL));
		const float HalfRanksM = static_cast<float>(NumRanks - 1) * Formation->RankSpacing * 0.5f * 0.01f;
		FUnitOBB Body;
		Body.Center = Formation->AnchorPos * 0.01f - FrontL * HalfRanksM; // centroid, not front rank
		Body.YawDeg = Formation->FacingYaw;
		Body.HalfAlongFacing = HalfRanksM + Formation->RankSpacing * 0.5f * 0.01f;
		Body.HalfAcrossFacing = (Formation->NumFiles * Formation->FileSpacing * 0.5f) * 0.01f;
		UnitBodies.Add(Body);
		BodyUnits.Add(Owners[0].Owner);
	});

	const float WalkSpeed = GBattleMovementWalkSpeed.GetValueOnGameThread();
	const float FacingDeltaRate = GBattleMovementFacingDeltaRate.GetValueOnGameThread();
	const float Hysteresis = GBattleMovementSpeedHysteresis.GetValueOnGameThread();

	// ---- Per-unit locomotion (§8.2: the unit is the locomotor) ----
	LocomotionQuery.ForEachEntityChunk(Context, [&, DT, WalkSpeed, FacingDeltaRate, Hysteresis, CostGrid, Garrison, Registry, bGarrisonEnabled](FMassExecutionContext& Context)
	{
		FUnitFormationSharedFragment& Formation = Context.GetMutableSharedFragment<FUnitFormationSharedFragment>();
		const FUnitStatsSharedFragment* Stats = Context.GetSharedFragmentPtr<FUnitStatsSharedFragment>();
		if (!Stats || Formation.NumSlots <= 0 || Formation.NumFiles <= 0)
		{
			return;
		}
		const FUnitHandle Unit = Formation.OwnerUnit;
		const FUnitOrder* Order = Registry->FindOrder(Unit);
		if (!Order)
		{
			return;
		}

		// ---- Order consumption: (re)plan on a new order serial ----
		const uint32 Serial = Registry->GetOrderSerial(Unit);
		const FUnitRuntimeState* RuntimeState = Registry->FindUnitState(Unit);
		if (!RuntimeState)
		{
			return;
		}

		auto PolylineFromPlan = [&Formation](const FBattlePlannedPath& Plan)
		{
			Formation.Path.NumPoints = 0;
			Formation.Path.TotalLength = 0.f;
			const int32 Count = FMath::Min(Plan.Points.Num(), MaxBattlePathPoints);
			Formation.Path.NumPoints = Count;
			float S = 0.f;
			for (int32 i = 0; i < Count; ++i)
			{
				S = Plan.CumulativeS[i];
				Formation.Path.Points[i].Pos = FVector2D(Plan.Points[i]) * 100.f; // m -> cm
				Formation.Path.Points[i].S = S * 100.f;
				Formation.Path.Points[i].Z = 0.f; // field paths stay flat (spec §3.1)
				FVector2D Tangent(1.f, 0.f);
				if (Count >= 2)
				{
					const int32 Next = FMath::Min(i + 1, Count - 1);
					const int32 Prev = FMath::Max(i - 1, 0);
					Tangent = (Plan.Points[Next] - Plan.Points[Prev]).GetSafeNormal();
				}
				Formation.Path.Points[i].Tangent = Tangent;
			}
			Formation.Path.TotalLength = S * 100.f;
		};

		// Garrison internal-path adoption (spec §4.3): same boundary rule as
		// PolylineFromPlan, carrying Z losslessly (m -> cm).
		auto PolylineFromGarrisonPath = [&Formation](const TArray<FBattlePathPoint3>& Src)
		{
			const int32 Count = FMath::Min(Src.Num(), MaxBattlePathPoints);
			Formation.Path.NumPoints = Count;
			for (int32 i = 0; i < Count; ++i)
			{
				Formation.Path.Points[i].Pos = Src[i].Pos * 100.f; // m -> cm
				Formation.Path.Points[i].S = Src[i].S * 100.f;
				Formation.Path.Points[i].Tangent = Src[i].Tangent;
				Formation.Path.Points[i].Z = Src[i].Z * 100.f;
			}
			Formation.Path.TotalLength = Count > 0 ? Src[Count - 1].S * 100.f : 0.f;
		};

		auto PlanFieldPathTo = [&, CostGrid](const FVector2D& GoalM)
		{
			FBattlePlannedPath Plan;
			const bool bFound = FBattlePathPlanner::FindPath(CostGrid->GetGrid(), Formation.AnchorPos * 0.01f, GoalM, 0.f, Plan);
			if (bFound && Plan.TotalLength > 1e-4f)
			{
				FBattlePathPlanner::Smooth(CostGrid->GetGrid(), 1.0f, Plan);
				FBattlePlannedPath Resampled;
				FBattlePathPlanner::Resample(Plan, 2.0f, Resampled);
				PolylineFromPlan(Resampled);
				return true;
			}
			Formation.Path.NumPoints = 0;
			return false;
		};

		// Deterministic entrance pick happens inline at order consumption
		// (spec §4.2): lowest entrance index whose lease is free, else lowest
		// index overall.

		// Garrison FSM transition helper (ground rule 4): one EXEC line per
		// transition, event-rate.
		auto GarrisonTransition = [&Formation, &Context](EGarrisonPhase NewPhase, const TCHAR* Reason)
		{
			Formation.GarrisonPhase = NewPhase;
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT Garrison Phase Unit=%u:%u Phase=%d Reason=%s"),
				Formation.OwnerUnit.Index, Formation.OwnerUnit.Serial, static_cast<int32>(NewPhase), Reason);
			// B.8 item 4: filing/queue-wait metrics, sampled at the settle
			// transition (diagnostic only -- never hashed).
			if (NewPhase == EGarrisonPhase::Settled)
			{
				if (UBattleDebugSubsystem* DebugSink = Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>())
				{
					DebugSink->RecordGarrisonSettled(
						GFrameNumber - Formation.GarrisonOrderTick,
						Formation.GarrisonQueueTick > 0 ? GFrameNumber - Formation.GarrisonQueueTick : 0);
				}
			}
		};

		// Cancel a garrison that has not entered the structure yet (order
		// change while Approaching/Queueing): release every hold.
		auto CancelPreIngressGarrison = [&]()
		{
			Garrison->ReleaseUnit(Unit);
			Formation.GarrisonMode = EGarrisonMode::None;
			Formation.GarrisonStructureId = 0;
			Formation.GarrisonEntranceIndex = 0;
			GarrisonTransition(EGarrisonPhase::None, TEXT("CANCEL"));
		};

		// Begin filing along the entrance's internal path (ingress or egress).
		auto BeginInteriorMove = [&](bool bEgress)
		{
			const FGarrisonStructureRecord* Record = Garrison->FindStructure(Formation.GarrisonStructureId);
			if (!Record || !Record->Entrances.IsValidIndex(Formation.GarrisonEntranceIndex))
			{
				CancelPreIngressGarrison();
				return false;
			}
			TArray<FBattlePathPoint3> Internal;
			if (!FGarrisonLayout::BuildInternalPath(*Record, Formation.GarrisonEntranceIndex, bEgress, Internal))
			{
				CancelPreIngressGarrison();
				return false;
			}
			const FGarrisonEntrance& Entrance = Record->Entrances[Formation.GarrisonEntranceIndex];
			// An exit ordered mid-ingress continues OUTWARD from the current
			// arc (the egress path mirrors the entry path up to the door; the
			// assembly vs queue legs differ slightly) -- adopting the path
			// with PathS = 0 would teleport the anchor back to the node.
			const bool bTurnAroundMidIngress = bEgress
				&& Formation.GarrisonPhase == EGarrisonPhase::Ingress
				&& Formation.Path.IsValid();
			const float MirrorS = bTurnAroundMidIngress
				? FMath::Clamp(Formation.Path.TotalLength - Formation.PathS, 0.f, Formation.Path.TotalLength)
				: 0.f;
			Formation.GarrisonMode = bEgress ? EGarrisonMode::Exiting : EGarrisonMode::Garrisoning;
			if (!bEgress)
			{
				Formation.FieldFilesSaved = Formation.NumFiles;
				Formation.NumFiles = FGarrisonLayout::ColumnFilesFor(Entrance.ClearWidthM, Formation.FileSpacing * 0.01f, Formation.NumFiles);
			}
			PolylineFromGarrisonPath(Internal);
			Formation.PathS = MirrorS;
			Formation.PassLocked = 1; // F7 pre-commit: the column decision is the entrance's declared width
			++Formation.Generation;
			GarrisonTransition(bEgress ? EGarrisonPhase::Egress : EGarrisonPhase::Ingress, bEgress ? TEXT("EGRESS") : TEXT("INGRESS"));
			return true;
		};

		// Start egress from an occupied structure (A7/§4.5). The entry lease
		// gates the doorway: busy -> stay put and retry from the FSM below
		// (mode flips to Exiting so the retry branch finds it).
		auto TryBeginEgress = [&]() -> bool
		{
			if (!Garrison->TryAcquireEntryLease(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, Unit))
			{
				Formation.GarrisonMode = EGarrisonMode::Exiting;
				GarrisonTransition(Formation.GarrisonPhase, TEXT("EGRESS-BUSY"));
				return false;
			}
			return BeginInteriorMove(/*bEgress*/ true);
		};

		// Booking + lease attempt at the queue point (the Approach-arrival
		// handler; also used when the approach plan is degenerate because the
		// unit already stands at the queue point). Anything short of booked +
		// leased queues (A4) with a deterministic hold-point offset.
		auto AttemptEntry = [&]()
		{
			const FGarrisonStructureRecord* Rec = Garrison->FindStructure(Formation.GarrisonStructureId);
			const int32 RosterSize = FMath::Max(1, Registry->GetRosterSize(Unit));
			FGarrisonBooking Booking;
			const bool bBooked = Garrison->TryBook(Unit, Formation.GarrisonStructureId,
				Formation.GarrisonEntranceIndex, RosterSize, Booking)
				|| Garrison->FindBooking(Unit) != nullptr;
			if (bBooked && Garrison->IsEntryLeaseFree(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex)
				&& Garrison->TryAcquireEntryLease(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, Unit))
			{
				Garrison->UnregisterQueueWait(Unit);
				BeginInteriorMove(/*bEgress*/ false);
			}
			else
			{
				if (Formation.GarrisonQueueTick == 0)
				{
					Formation.GarrisonQueueTick = GFrameNumber; // B.8 item 4 queue-wait start
				}
				const int32 QueueSlot = Garrison->RegisterQueueWait(Unit,
					Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex);
				GarrisonTransition(EGarrisonPhase::Queue, TEXT("FULL-OR-BUSY"));
				if (Rec && QueueSlot > 0)
				{
					TArray<FVector2D> HoldPoints;
					FGarrisonLayout::QueuePoints(*Rec, Formation.GarrisonEntranceIndex,
						QueueSlot + 1, HoldPoints);
					if (HoldPoints.IsValidIndex(QueueSlot))
					{
						PlanFieldPathTo(HoldPoints[QueueSlot]);
					}
				}
			}
		};

		// ---- Order-type dispatch on serial change ----
		if (Serial != Formation.PlannedOrderSerial)
		{
			Formation.PlannedOrderSerial = Serial;
			Formation.PathS = 0.f;
			Formation.PassLocked = 0; // new order = new pass (T1.7 commit rule is per traversal)

			const bool bGarrisonOrder = Order->Type == FUnitOrder::EType::Garrison
				|| Order->Type == FUnitOrder::EType::ExitGarrison;
			if (bGarrisonOrder && !bGarrisonEnabled)
			{
				// B.8 item 6: flag off -> garrison orders consume as no-ops and
				// the T1.8 baseline behavior is preserved byte-for-byte.
				Formation.Path.NumPoints = 0;
				++Formation.Generation;
				auto MoveStates0 = Context.GetMutableFragmentView<FMovementStateFragment>();
				for (int32 i = 0; i < Context.GetNumEntities(); ++i)
				{
					MoveStates0[i].State = EBattleMovementState::HoldSlot;
				}
			}
			else if (Order->Type == FUnitOrder::EType::Garrison)
			{
				const uint32 StructureId = Order->StructureId;
				const FGarrisonStructureRecord* Record = Garrison->FindStructure(StructureId);
				if (Formation.GarrisonStructureId == StructureId && Formation.GarrisonMode != EGarrisonMode::None)
				{
					// Spec §4.1 no-op family, extended to the whole in-bound
					// flow (Approach/Queue/Ingress): already heading into THIS
					// structure -- continue. Parking + egress here would churn
					// the choke on every re-order (the soak variant caught
					// units cycling ingress->egress forever under a 60 s
					// re-order cadence).
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT Garrison NoOp Unit=%u:%u Structure=%u (phase=%d)"),
						Unit.Index, Unit.Serial, StructureId, static_cast<int32>(Formation.GarrisonPhase));
				}
				else if (Formation.GarrisonPhase == EGarrisonPhase::Settled && Formation.GarrisonStructureId == StructureId)
				{
					// Spec §4.1: re-garrison into the same structure while
					// settled is a no-op.
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT Garrison NoOp Unit=%u:%u Structure=%u (already settled)"),
						Unit.Index, Unit.Serial, StructureId);
				}
				else if (!Record || Record->Entrances.Num() == 0)
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT Garrison Phase Unit=%u:%u Phase=0 Reason=BADSTRUCTURE (%u)"),
						Unit.Index, Unit.Serial, StructureId);
				}
				else if (Formation.GarrisonMode != EGarrisonMode::None)
				{
					// Spec §4.1: garrisoning a different structure while
					// garrisoned = egress, then re-approach (A7 parking).
					FUnitOrder Pending = *Order;
					Registry->ParkPendingAfterEgress(Unit, Pending);
					if (Formation.GarrisonPhase == EGarrisonPhase::Approach || Formation.GarrisonPhase == EGarrisonPhase::Queue)
					{
						CancelPreIngressGarrison();
						FUnitOrder Resumed;
						Registry->ConsumePendingAfterEgress(Unit, Resumed); // retaken next frame
					}
					else
					{
						// Occupy/Settled: file out; the booking rides until
						// egress completion (spec §4.5). Already Egressing ->
						// the current walk-out finishes and the parked order
						// resumes at the assembly point (no path re-adoption
						// churn -- restarting the interior path from the node
						// would teleport the anchor backwards every re-order).
						if (Formation.GarrisonPhase != EGarrisonPhase::Egress)
						{
							TryBeginEgress();
						}
					}
				}
				else
				{
					// Field unit: choose the entrance, book nothing yet, plan
					// the field approach to its queue point (A* unchanged,
					// ADR-19: registration stamped the route).
					int32 Entrance = 0;
					for (int32 E = 0; E < Record->Entrances.Num(); ++E)
					{
						if (Garrison->IsEntryLeaseFree(StructureId, static_cast<uint8>(E)))
						{
							Entrance = E;
							break;
						}
					}
					Formation.GarrisonMode = EGarrisonMode::Garrisoning;
					Formation.GarrisonStructureId = StructureId;
					Formation.GarrisonEntranceIndex = static_cast<uint8>(Entrance);
					Formation.GarrisonOrderTick = GFrameNumber;
					Formation.GarrisonQueueTick = 0;
					GarrisonTransition(EGarrisonPhase::Approach, TEXT("ORDER"));
					if (PlanFieldPathTo(Record->Entrances[Entrance].QueuePos))
					{
						++Formation.Generation;
						auto MoveStates = Context.GetMutableFragmentView<FMovementStateFragment>();
						for (int32 i = 0; i < Context.GetNumEntities(); ++i)
						{
							MoveStates[i].State = EBattleMovementState::ManeuverToSlot;
						}
					}
					else
					{
						// Degenerate (already at the queue point) or unreachable:
						// at the queue point this IS arrival -- attempt entry at
						// once; otherwise hold (no teleport) in Approach.
						if (FVector2D::DistSquared(Formation.AnchorPos * 0.01f,
								Record->Entrances[Entrance].QueuePos) <= FMath::Square(1.f))
						{
							++Formation.Generation;
							AttemptEntry();
						}
						else
						{
							GarrisonTransition(EGarrisonPhase::Approach, TEXT("UNREACHABLE"));
						}
					}
				}
			}
			else if (Order->Type == FUnitOrder::EType::ExitGarrison)
			{
				if (Formation.GarrisonMode == EGarrisonMode::None)
				{
					// Not garrisoned: consume as a no-op.
				}
				else if (Formation.GarrisonPhase == EGarrisonPhase::Approach
					|| Formation.GarrisonPhase == EGarrisonPhase::Queue)
				{
					CancelPreIngressGarrison();
				}
				else if (Formation.GarrisonPhase != EGarrisonPhase::Egress
					&& !TryBeginEgress())
				{
					// lease busy: mode=Exiting + retry branch takes over.
					// Already Egressing: consume as a no-op (the walk-out is
					// the exit -- restarting it would churn the choke).
				}
			}
			else if (Order->Type == FUnitOrder::EType::Move && Formation.GarrisonMode != EGarrisonMode::None)
			{
				// A7: a Move received while garrisoned is NOT a field move --
				// park it and file out first.
				FUnitOrder Pending = *Order;
				Registry->ParkPendingAfterEgress(Unit, Pending);
				if (Formation.GarrisonPhase == EGarrisonPhase::Approach
					|| Formation.GarrisonPhase == EGarrisonPhase::Queue)
				{
					CancelPreIngressGarrison();
					FUnitOrder Resumed;
					Registry->ConsumePendingAfterEgress(Unit, Resumed); // becomes the live order
				}
				else
				{
					if (!TryBeginEgress())
					{
						// lease busy: mode=Exiting + retry branch takes over
					}
				}
			}
			else if (Order->Type == FUnitOrder::EType::Move)
			{
				// A2 width control: re-front on a commanded file count before the
				// layout is consumed again. NumSlots is preserved (casualty reflow
				// semantics); the Generation bump below makes FormationSlotResolve
				// re-run the slot assigner for the new layout.
				if (const int32 NewFiles = FUnitLocomotion::ResolveCommandedFiles(
					Order->CommandedFiles, Formation.NumSlots, Formation.NumFiles);
					NewFiles > 0)
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT WidthOverride Unit=%u:%u Files %d->%d"),
						Unit.Index, Unit.Serial, Formation.NumFiles, NewFiles);
					Formation.NumFiles = NewFiles;
				}
				if (PlanFieldPathTo(Order->AnchorPos * 0.01f))
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT PlanPath Unit=%u:%u Length=%.1fm Points=%d"),
						Unit.Index, Unit.Serial, Formation.Path.TotalLength * 0.01f, Formation.Path.NumPoints);
				}
				else
				{
					// Unreachable anchor (or in-place reform when the goal
					// coincides): hold position -- no silent teleport.
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT PlanPath Unit=%u:%u IN-PLACE or UNREACHABLE -- holding"),
						Unit.Index, Unit.Serial);
				}
				++Formation.Generation;
				auto MoveStates = Context.GetMutableFragmentView<FMovementStateFragment>();
				for (int32 i = 0; i < Context.GetNumEntities(); ++i)
				{
					MoveStates[i].State = EBattleMovementState::ManeuverToSlot;
				}
			}
			else // Stop
			{
				Formation.Path.NumPoints = 0;
				++Formation.Generation;
				auto MoveStates = Context.GetMutableFragmentView<FMovementStateFragment>();
				for (int32 i = 0; i < Context.GetNumEntities(); ++i)
				{
					MoveStates[i].State = EBattleMovementState::HoldSlot;
				}
			}
		}

		// ---- Garrison FSM progression (event-rate; runs every frame but only
		//      acts on timers/arrivals) ----
		Formation.GarrisonRetryCooldown = FMath::Max(0.f, Formation.GarrisonRetryCooldown - DT);
		if (Formation.GarrisonPhase == EGarrisonPhase::Ingress
			|| Formation.GarrisonPhase == EGarrisonPhase::Egress)
		{
			// Interior elevation target rides the internal path (spec §6).
			Formation.GarrisonZ = FGarrisonLayout::ZAtSFragment(Formation.Path, Formation.PathS);
		}
		const FGarrisonStructureRecord* GarrisonRecord =
			Formation.GarrisonStructureId != 0 ? Garrison->FindStructure(Formation.GarrisonStructureId) : nullptr;

		if (Formation.GarrisonPhase == EGarrisonPhase::Approach && GarrisonRecord)
		{
			// Arrival is detected in the anchor-progression branch below; a
			// lost path (unreachable) holds here without retry churn.
		}
		else if (Formation.GarrisonPhase == EGarrisonPhase::Queue && GarrisonRecord)
		{
			// A4: hold at the queue hold point; retry booking + lease on the
			// reflow cadence (0.5 s default).
			if (Formation.GarrisonRetryCooldown <= 0.f)
			{
				Formation.GarrisonRetryCooldown = FMath::Max(0.05f, FGarrisonCVars::QueueRetry());
				const int32 RosterSize = FMath::Max(1, Registry->GetRosterSize(Unit));
				FGarrisonBooking Booking;
				const bool bBooked = Garrison->FindBooking(Unit) != nullptr
					|| Garrison->TryBook(Unit, Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, RosterSize, Booking);
				if (bBooked && Garrison->IsEntryLeaseFree(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex)
					&& Garrison->TryAcquireEntryLease(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, Unit))
				{
					Garrison->UnregisterQueueWait(Unit);
					BeginInteriorMove(/*bEgress*/ false);
				}
			}
		}
		else if (Formation.GarrisonPhase == EGarrisonPhase::Occupy && GarrisonRecord)
		{
			// Settled detection (spec §4.4): every live entity within
			// SlotReachEps of its assigned garrison post.
			UMassEntitySubsystem* Entities = Context.GetWorld()->GetSubsystem<UMassEntitySubsystem>();
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Unit);
			if (Entities && Roster && Roster->Num() > 0
				&& Formation.GarrisonSlotCount > 0)
			{
				FMassEntityManager& EM = Entities->GetMutableEntityManager();
				bool bAllSettled = true;
				for (const FMassEntityHandle& Entity : *Roster)
				{
					const FMassEntityView View(EM, Entity);
					const FSlotAssignmentFragment* Slot = View.GetFragmentDataPtr<FSlotAssignmentFragment>();
					const FTransformFragment* Transform = View.GetFragmentDataPtr<FTransformFragment>();
					if (!Slot || !Transform || Slot->SlotIndex < Formation.GarrisonSlotBase
						|| Slot->SlotIndex >= Formation.GarrisonSlotBase + Formation.GarrisonSlotCount)
					{
						bAllSettled = false;
						break;
					}
					const FGarrisonSlotFragment& Post =
						Formation.Slots[Slot->SlotIndex - Formation.GarrisonSlotBase];
					const FVector P = Transform->GetTransform().GetTranslation();
					const float DistCm = FVector2D::Distance(FVector2D(P.X, P.Y), Post.Pos);
					if (DistCm > FUnitServoConstants::SlotReachEps * 100.f)
					{
						bAllSettled = false;
						break;
					}
				}
				if (bAllSettled)
				{
					GarrisonTransition(EGarrisonPhase::Settled, TEXT("POSTS-HELD"));
				}
			}
		}
		else if (Formation.GarrisonMode == EGarrisonMode::Exiting
			&& (Formation.GarrisonPhase == EGarrisonPhase::Occupy
				|| Formation.GarrisonPhase == EGarrisonPhase::Settled))
		{
			// A parked/busy egress request retries on the queue cadence.
			if (Formation.GarrisonRetryCooldown <= 0.f)
			{
				Formation.GarrisonRetryCooldown = FMath::Max(0.05f, FGarrisonCVars::QueueRetry());
				if (TryBeginEgress())
				{
					// booking releases at egress completion (spec §4.5)
				}
			}
		}

		// ---- §8.2 speed limit (E2 two-step wheel cap + Min3) ----
		// Garrison phases override the order-derived commanded speed (A3):
		// Approach = walk, Queue/Occupy/Settled = hold, Ingress/Egress = walk
		// x InteriorSpeedFactor.
		float CommandedSpeedM = 0.f;
		if (Formation.GarrisonMode != EGarrisonMode::None)
		{
			switch (Formation.GarrisonPhase)
			{
			case EGarrisonPhase::Approach:
			case EGarrisonPhase::Queue: // walking to the queue hold point, else holding (no path)
				CommandedSpeedM = WalkSpeed;
				break;
			case EGarrisonPhase::Ingress:
			case EGarrisonPhase::Egress:
				CommandedSpeedM = WalkSpeed * FMath::Max(0.05f, FGarrisonCVars::InteriorSpeedFactor());
				break;
			default:
				CommandedSpeedM = 0.f; // Occupy / Settled hold
				break;
			}
		}
		else if (Order->Type == FUnitOrder::EType::Move)
		{
			CommandedSpeedM = Order->CommandedSpeed > 0.f ? Order->CommandedSpeed : WalkSpeed;
		}

		FBattlePlannedPath PathM;
		const bool bHasPath = FPathFrameLayout::FromFragment(Formation.Path, PathM);

		// Member speeds: EffectiveSpeed(i) = WalkSpeed * SpeedMult (Phase 1:
		// uniform per unit -- per-member fatigue variation arrives Phase 4).
		const float MemberSpeed = WalkSpeed * Stats->SpeedMult;
		FUnitLocomotion::FSpeedLimitInputs LimitIn;
		LimitIn.CommandedSpeed = CommandedSpeedM;
		LimitIn.MinMemberSpeed = MemberSpeed;
		LimitIn.MaxMemberSpeed = MemberSpeed;

		// Unit outer radius from the formed body; turn radius from path
		// curvature at the anchor (straight path -> huge -> caps inactive).
		const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation.NumSlots) / Formation.NumFiles));
		const float HalfFrontageM = (Formation.NumFiles * Formation.FileSpacing * 0.5f) * 0.01f;
		const float HalfDepthM = (NumRanks * Formation.RankSpacing * 0.5f) * 0.01f;
		LimitIn.OuterRadius = FMath::Sqrt(HalfFrontageM * HalfFrontageM + HalfDepthM * HalfDepthM);
		LimitIn.PathRadiusOfAnchor = bHasPath
			? FUnitLocomotion::PathRadiusAt(PathM, Formation.PathS * 0.01f, 3.0f)
			: BIG_NUMBER;
		LimitIn.FacingDeltaRate = FacingDeltaRate;

		float& CurrentLimit = Formation.SpeedLimit; // cm/s, persists frame to frame (hysteresis state)
		const float RawLimitM = FUnitLocomotion::ComputeUnitSpeedLimit(LimitIn);
		const float CurrentLimitM = CurrentLimit * 0.01f;
		const float NewLimitM = FUnitLocomotion::ApplyHysteresis(CurrentLimitM, RawLimitM, Hysteresis);
		CurrentLimit = NewLimitM * 100.f;

		// ---- §8.2 hard rule: no facing change while interpenetrating ----
		// R-G3: skipped entirely for garrisoned units (their field OBB is
		// meaningless and interior crowding is servo+compression).
		const bool bGarrisoned = Formation.GarrisonMode != EGarrisonMode::None;
		bool bInterpenetrating = false;
		if (!bGarrisoned)
		{
			const float RadMy = FMath::DegreesToRadians(Formation.FacingYaw);
			const FVector2D FrontMy(FMath::Cos(RadMy), FMath::Sin(RadMy));
			FUnitOBB MyBody;
			MyBody.Center = Formation.AnchorPos * 0.01f - FrontMy * HalfDepthM; // centroid
			MyBody.YawDeg = Formation.FacingYaw;
			MyBody.HalfAlongFacing = HalfDepthM;
			MyBody.HalfAcrossFacing = HalfFrontageM;
			for (int32 BodyIdx = 0; BodyIdx < UnitBodies.Num(); ++BodyIdx)
			{
				if (BodyUnits[BodyIdx] == Unit)
				{
					continue; // self
				}
				if (FUnitContact::Interpenetrating(MyBody, UnitBodies[BodyIdx]))
				{
					bInterpenetrating = true;
					break;
				}
			}
		}

		// ---- Narrow-pass decision (arch §8.6, T1.7): field paths only --
		//      garrison interior paths PRE-COMMIT the column (PassLocked = 1
		//      at adoption, F7); a grid-width scan is meaningless indoors. ----
		if (bHasPath && !Formation.PassLocked && !bGarrisoned && Order->Type == FUnitOrder::EType::Move)
		{
			const float ScanAheadM = 15.f;
			const float FrontM = Formation.PathS * 0.01f;
			float PassWidthM = FLT_MAX;
			FVector2D PathPos;
			FVector2D PathTan;
			for (float Ahead = 2.f; Ahead <= ScanAheadM; Ahead += 1.5f)
			{
				if (FPathFrameLayout::SampleAt(PathM, FrontM + Ahead, PathPos, PathTan))
				{
					const float W = FUnitPassGeometry::MeasureClearWidth(
						CostGrid->GetGrid(), PathPos, FVector2D(-PathTan.Y, PathTan.X), 40.f);
					PassWidthM = FMath::Min(PassWidthM, W);
				}
			}
			const float FormationWidthM = HalfFrontageM * 2.f; // files across the facing
			// Decide (and commit) only once a genuine narrowing is within the
			// scan window -- an all-clear scan must keep scanning, not lock a
			// no-op decision before the corridor is even visible.
			if (PassWidthM < FormationWidthM * 0.99f)
			{
				const FUnitPassGeometry::FPassDecision Decision = FUnitPassGeometry::DecideSqueezeOrMorph(
					PassWidthM, FormationWidthM, Formation.FileSpacing * 0.01f);
				if (Decision.bMorph && Decision.MorphFiles != Formation.NumFiles)
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT PassDecision Unit=%u:%u MORPH files %d->%d (pass %.1fm)"),
						Unit.Index, Unit.Serial, Formation.NumFiles, Decision.MorphFiles, PassWidthM);
					Formation.NumFiles = Decision.MorphFiles;
					++Formation.Generation; // layout re-parameterization re-slots via the normal reflow
				}
				else if (!Decision.bMorph)
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT PassDecision Unit=%u:%u SQUEEZE scale=%.2f (pass %.1fm)"),
						Unit.Index, Unit.Serial, Decision.SqueezeScale, PassWidthM);
				}
				Formation.PassLocked = 1; // commits once the lead rank nears the mouth
			}
		}

		// Compression model (§8.2): spacing scale advances toward the pass
		// demand while squeezing and recovers at ~10%/s afterwards. Garrison
		// interior phases skip the corridor scan (no meaningful grid widths).
		if (bHasPath && !bGarrisoned)
		{
			float TargetScale = 1.f;
			if (Formation.PassLocked)
			{
				// While inside the pass corridor, hold the locked squeeze scale.
				const float ScanAheadM = 8.f;
				FVector2D PathPos;
				FVector2D PathTan;
				if (FPathFrameLayout::SampleAt(PathM, Formation.PathS * 0.01f + ScanAheadM, PathPos, PathTan))
				{
					const float W = FUnitPassGeometry::MeasureClearWidth(
						CostGrid->GetGrid(), PathPos, FVector2D(-PathTan.Y, PathTan.X), 40.f);
					const float FormationWidthM = HalfFrontageM * 2.f;
					if (W < FormationWidthM * FUnitCompression::CompressionFloor * 0.99f)
					{
						TargetScale = FUnitPassGeometry::DecideSqueezeOrMorph(
							W, FormationWidthM, Formation.FileSpacing * 0.01f).SqueezeScale;
					}
				}
			}
			Formation.SpacingScale = FUnitCompression::AdvanceSpacingScale(
				Formation.SpacingScale, TargetScale, DT);
		}
		else
		{
			// No field path (arrived / stopped / garrison interior): recover
			// toward open-order spacing.
			Formation.SpacingScale = FUnitCompression::AdvanceSpacingScale(
				Formation.SpacingScale, 1.f, DT);
		}

		// §8.6 pass-through: the anchor proceeds at ~55% speed while
		// interpenetrating another unit (no facing change -- the §8.2 lock
		// above already holds during contact).
		if (bInterpenetrating && LimitIn.CommandedSpeed > 0.f)
		{
			LimitIn.CommandedSpeed *= FUnitContact::PassThroughSpeedFactor;
		}

		// ---- Anchor progression + facing cap (A2 refront-at-destination) ----
		if (bHasPath && CurrentLimit > 0.f)
		{
			// PathS is centimeters (fragment convention); advance in cm against
			// the cm length so SampleAt's /100 stays unit-consistent.
			Formation.PathS = FUnitLocomotion::AdvanceAnchor(
				Formation.PathS, CurrentLimit, DT, PathM.TotalLength * 100.f);
			FVector2D AnchorM;
			FVector2D Tangent;
			if (FPathFrameLayout::SampleAt(PathM, Formation.PathS * 0.01f, AnchorM, Tangent))
			{
				Formation.AnchorPos = AnchorM * 100.f;
				// En route the facing rides the path tangent (march normally);
				// the commanded facing is assumed AFTER arrival through the
				// no-path Move branch below -- rotating in place from a formed
				// block (the S6-proven re-dress), not mid-march.
				const float PathYaw = FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X));
				if (!bInterpenetrating)
				{
					Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
						Formation.FacingYaw, PathYaw, FacingDeltaRate, DT);
				}
				// ---- Arrivals: field Move vs garrison phases ----
				const float RemainingCm = FMath::Max(0.f, PathM.TotalLength * 100.f - Formation.PathS);
				if (!bGarrisoned && Order->Type == FUnitOrder::EType::Move && RemainingCm <= 1.f)
				{
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT ArriveRefront Unit=%u:%u Facing=%.1f Files=%d"),
						Unit.Index, Unit.Serial, Formation.FacingYaw, Formation.NumFiles);
					Formation.Path.NumPoints = 0;
					Formation.Path.TotalLength = 0.f;
					Formation.PathS = 0.f;
					++Formation.Generation;
				}
				else if (Formation.GarrisonPhase == EGarrisonPhase::Approach && RemainingCm <= 1.f)
				{
					// Reached the entrance queue point: try to book + take the
					// door; anything else queues (A4).
					Formation.Path.NumPoints = 0;
					Formation.Path.TotalLength = 0.f;
					Formation.PathS = 0.f;
					++Formation.Generation;
					AttemptEntry();
				}
				else if (Formation.GarrisonPhase == EGarrisonPhase::Queue && RemainingCm <= 1.f)
				{
					// Arrived at the queue hold point: drop the path and hold.
					Formation.Path.NumPoints = 0;
					Formation.Path.TotalLength = 0.f;
					Formation.PathS = 0.f;
					++Formation.Generation;
				}
				else if ((Formation.GarrisonPhase == EGarrisonPhase::Ingress
					|| Formation.GarrisonPhase == EGarrisonPhase::Egress) && RemainingCm <= 1.f)
				{
					// Interior path end: distribution node (ingress) or the
					// assembly point (egress).
					if (Formation.GarrisonPhase == EGarrisonPhase::Ingress)
					{
						// Occupy: copy the booked range into the fragment
						// mirror (A2) and let the slot-source branch spread.
						const FGarrisonBooking* BookingPtr = Garrison->FindBooking(Unit);
						Formation.Path.NumPoints = 0;
						Formation.Path.TotalLength = 0.f;
						Formation.PathS = 0.f;
						if (BookingPtr)
						{
							const int32 Count = FMath::Min(BookingPtr->SlotCount, MaxGarrisonSlotsPerUnit);
							Formation.GarrisonSlotBase = BookingPtr->SlotBase;
							Formation.GarrisonSlotCount = Count;
							if (GarrisonRecord)
							{
								for (int32 i = 0; i < Count; ++i)
								{
									const int32 SrcIdx = BookingPtr->SlotBase + i;
									if (!GarrisonRecord->Slots.IsValidIndex(SrcIdx))
									{
										break;
									}
									const FGarrisonSlot& Src = GarrisonRecord->Slots[SrcIdx];
									Formation.Slots[i].Pos = FVector2D(Src.Pos) * 100.f; // m -> cm
									Formation.Slots[i].Z = Src.Z * 100.f;
									Formation.Slots[i].YawDeg = Src.YawDeg;
								}
							}
						}
						else
						{
							Formation.GarrisonSlotCount = 0; // booking lost (death): hold at the node
						}
						Garrison->ReleaseEntryLease(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, Unit);
						Formation.GarrisonMode = EGarrisonMode::Garrisoned;
						++Formation.Generation;
						GarrisonTransition(EGarrisonPhase::Occupy, TEXT("NODE-REACHED"));
					}
					else
					{
						// Egress complete: re-form (B6), release everything.
						Formation.Path.NumPoints = 0;
						Formation.Path.TotalLength = 0.f;
						Formation.PathS = 0.f;
						Formation.NumFiles = FMath::Clamp(Formation.FieldFilesSaved, 1, FMath::Max(1, Formation.NumSlots));
						Formation.GarrisonSlotBase = 0;
						Formation.GarrisonSlotCount = 0;
						Formation.GarrisonMode = EGarrisonMode::None;
						Garrison->ReleaseEntryLease(Formation.GarrisonStructureId, Formation.GarrisonEntranceIndex, Unit);
						Garrison->ReleaseBooking(Unit);
						Garrison->UnregisterQueueWait(Unit);
						Formation.GarrisonStructureId = 0;
						Formation.GarrisonEntranceIndex = 0;
						++Formation.Generation;
						auto MoveStates = Context.GetMutableFragmentView<FMovementStateFragment>();
						for (int32 i = 0; i < Context.GetNumEntities(); ++i)
						{
							MoveStates[i].State = EBattleMovementState::ManeuverToSlot;
						}
						GarrisonTransition(EGarrisonPhase::None, TEXT("ASSEMBLED"));
						// A7: resume the parked order at the assembly point.
						FUnitOrder Resumed;
						if (Registry->ConsumePendingAfterEgress(Unit, Resumed))
						{
							UE_LOG(LogMassBattle, Display,
								TEXT("EXEC_RESULT Garrison Resume Unit=%u:%u Order=%d"),
								Unit.Index, Unit.Serial, static_cast<int32>(Resumed.Type));
						}
					}
				}
			}
		}
		else if (!bHasPath && !bInterpenetrating && !bGarrisoned && Order->Type == FUnitOrder::EType::Move)
		{
			// No path: unreachable (hold position) or an in-place reform order --
			// either way the facing settles to the ordered intent.
			Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
				Formation.FacingYaw, Order->FacingYaw, FacingDeltaRate, DT);
		}
		else if (!bHasPath && !bInterpenetrating && !bGarrisoned && Order->Type == FUnitOrder::EType::Stop)
		{
			// Stopped with no path: settle facing to the order facing if any.
			if (Order->FacingYaw != 0.f)
			{
				Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
					Formation.FacingYaw, Order->FacingYaw, FacingDeltaRate, DT);
			}
		}

		// ---- Per-unit garrison phase marker (B.8 item 3 overlay) ----
		if (bGarrisoned && DebugOverlay && Debug)
		{
			static const FColor PhaseColors[7] = {
				FColor::White,                             // None
				FColor(200, 200, 255),                     // Approach
				FColor::Yellow,                            // Queue
				FColor::Orange,                            // Ingress
				FColor::Cyan,                              // Occupy
				FColor::Green,                             // Settled
				FColor::Red                                // Egress
			};
			FBattleOverlayLine Marker;
			const FVector2D AnchorM = Formation.AnchorPos * 0.01f;
			Marker.From = FVector(AnchorM.X, AnchorM.Y, 0.f);
			Marker.To = FVector(AnchorM.X, AnchorM.Y, 300.f);
			Marker.Color = PhaseColors[FMath::Clamp(static_cast<int32>(Formation.GarrisonPhase), 0, 6)];
			Debug->EnqueueOverlayLine(Marker);
		}
	});

	// ---- Structure overlay (B.8 item 3; task 9): footprint, entrance legs,
	//      queue/assembly points, and slot posts (occupied vs free). ----
	if (DebugOverlay && Garrison->GetStructureCount() > 0)
	{
		if (UBattleDebugSubsystem* DebugSink = Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>())
		{
			constexpr float GroundZ = 20.f;
			auto Line = [DebugSink](const FVector2D& A, float AZ, const FVector2D& B, float BZ, const FColor& C)
			{
				FBattleOverlayLine L;
				L.From = FVector(A.X, A.Y, AZ);
				L.To = FVector(B.X, B.Y, BZ);
				L.Color = C;
				DebugSink->EnqueueOverlayLine(L);
			};
			for (const TPair<uint32, FGarrisonStructureRecord>& It : Garrison->GetAllStructures())
			{
				const FGarrisonStructureRecord& Rec = It.Value;
				const FVector2D Corners[4] = {
					Rec.Footprint.Min, FVector2D(Rec.Footprint.Max.X, Rec.Footprint.Min.Y),
					Rec.Footprint.Max, FVector2D(Rec.Footprint.Min.X, Rec.Footprint.Max.Y)
				};
				for (int32 C = 0; C < 4; ++C)
				{
					Line(Corners[C], GroundZ, Corners[(C + 1) % 4], GroundZ, FColor(120, 120, 40));
				}
				for (const FGarrisonEntrance& E : Rec.Entrances)
				{
					Line(E.QueuePos, GroundZ, E.DoorPos, GroundZ, FColor::Orange);
					Line(E.DoorPos, GroundZ, E.AssemblyPos, GroundZ, FColor(255, 80, 80));
				}
				for (int32 S = 0; S < Rec.Slots.Num(); ++S)
				{
					const FGarrisonSlot& Slot = Rec.Slots[S];
					const float ZCm = Slot.Z * 100.f + GroundZ;
					const float Half = 25.f;
					const FColor SlotColor = Garrison->IsSlotBooked(It.Key, S) ? FColor::Cyan : FColor(0, 90, 60);
					Line(FVector2D(Slot.Pos.X - Half, Slot.Pos.Y), ZCm, FVector2D(Slot.Pos.X + Half, Slot.Pos.Y), ZCm, SlotColor);
					Line(FVector2D(Slot.Pos.X, Slot.Pos.Y - Half), ZCm, FVector2D(Slot.Pos.X, Slot.Pos.Y + Half), ZCm, SlotColor);
				}
			}
		}
	}
}
