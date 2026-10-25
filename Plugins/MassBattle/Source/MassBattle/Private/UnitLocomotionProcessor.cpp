#include "UnitLocomotionProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleRegistry.h"
#include "BattleCostGridSubsystem.h"
#include "BattlePathPlanner.h"
#include "BattlePathFrameLayout.h"
#include "BattleUnitLocomotion.h"
#include "BattleUnitContact.h"
#include "BattlePassGeometry.h"
#include "BattleCompression.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
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
	TAutoConsoleVariable<float> GBattleMovementFacingDeltaRate(
		TEXT("Battle.Movement.FacingDeltaRate"),
		1.0f,
		TEXT("Commanded facing-change rate, rad/s (arch §8.2 facing cap)."),
		ECVF_Default);

	// §8.2 hysteresis fraction on the speed limit.
	TAutoConsoleVariable<float> GBattleMovementHysteresis(
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
	if (!Registry || !CostGrid)
	{
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();

	// ---- Unit bodies (for the §8.2 facing lock) ----
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
	const float Hysteresis = GBattleMovementHysteresis.GetValueOnGameThread();

	// ---- Per-unit locomotion (§8.2: the unit is the locomotor) ----
	LocomotionQuery.ForEachEntityChunk(Context, [&, DT, WalkSpeed, FacingDeltaRate, Hysteresis, CostGrid](FMassExecutionContext& Context)
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

		if (Serial != Formation.PlannedOrderSerial)
		{
			Formation.PlannedOrderSerial = Serial;
			Formation.PathS = 0.f;
			if (Order->Type == FUnitOrder::EType::Move)
			{
				FBattlePlannedPath Plan;
				// Ground rule 2: anchor in cm, planner in meters.
				if (FBattlePathPlanner::FindPath(CostGrid->GetGrid(), Formation.AnchorPos * 0.01f,
					Order->AnchorPos * 0.01f, 0.f, Plan))
				{
					FBattlePathPlanner::Smooth(CostGrid->GetGrid(), 1.0f, Plan);
					FBattlePlannedPath Resampled;
					FBattlePathPlanner::Resample(Plan, 2.0f, Resampled);
					PolylineFromPlan(Resampled);
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT PlanPath Unit=%u:%u Length=%.1fm Points=%d"),
						Unit.Index, Unit.Serial, Resampled.TotalLength, Resampled.Points.Num());
				}
				else
				{
					// Unreachable anchor: hold position (no silent teleport).
					Formation.Path.NumPoints = 0;
					UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT PlanPath Unit=%u:%u UNREACHABLE -- holding"),
						Unit.Index, Unit.Serial);
				}
			}
			else // Stop
			{
				Formation.Path.NumPoints = 0;
			}
			++Formation.Generation;
			// Order intent: entities re-enter their slots (Move) or hold (Stop).
			auto MoveStates = Context.GetMutableFragmentView<FMovementStateFragment>();
			for (int32 i = 0; i < Context.GetNumEntities(); ++i)
			{
				MoveStates[i].State = (Order->Type == FUnitOrder::EType::Move)
					? EBattleMovementState::ManeuverToSlot
					: EBattleMovementState::HoldSlot;
			}
		}

		// ---- §8.2 speed limit (E2 two-step wheel cap + Min3) ----
		FBattlePlannedPath PathM;
		const bool bHasPath = FPathFrameLayout::FromFragment(Formation.Path, PathM);

		// Member speeds: EffectiveSpeed(i) = WalkSpeed * SpeedMult (Phase 1:
		// uniform per unit -- per-member fatigue variation arrives Phase 4).
		const float MemberSpeed = WalkSpeed * Stats->SpeedMult;
		FUnitLocomotion::FSpeedLimitInputs LimitIn;
		LimitIn.CommandedSpeed = (Order->Type == FUnitOrder::EType::Move)
			? (Order->CommandedSpeed > 0.f ? Order->CommandedSpeed : WalkSpeed)
			: 0.f;
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
		const float RadMy = FMath::DegreesToRadians(Formation.FacingYaw);
		const FVector2D FrontMy(FMath::Cos(RadMy), FMath::Sin(RadMy));
		FUnitOBB MyBody;
		MyBody.Center = Formation.AnchorPos * 0.01f - FrontMy * HalfDepthM; // centroid
		MyBody.YawDeg = Formation.FacingYaw;
		MyBody.HalfAlongFacing = HalfDepthM;
		MyBody.HalfAcrossFacing = HalfFrontageM;
		bool bInterpenetrating = false;
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

		// ---- Narrow-pass decision (arch §8.6, T1.7): scan the path ahead;
		//      decide squeeze vs morph once per pass (commit rule = lock). ----
		if (bHasPath && !Formation.PassLocked && Order->Type == FUnitOrder::EType::Move)
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
		// demand while squeezing and recovers at ~10%/s afterwards.
		if (bHasPath)
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

		// §8.6 pass-through: the anchor proceeds at ~55% speed while
		// interpenetrating another unit (no facing change -- the §8.2 lock
		// above already holds during contact).
		if (bInterpenetrating && LimitIn.CommandedSpeed > 0.f)
		{
			LimitIn.CommandedSpeed *= FUnitContact::PassThroughSpeedFactor;
		}

		// ---- Anchor progression + facing cap ----
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
				const float PathYaw = FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X));
				if (!bInterpenetrating)
				{
					Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
						Formation.FacingYaw, PathYaw, FacingDeltaRate, DT);
				}
			}
		}
		else if (!bHasPath && Order->Type == FUnitOrder::EType::Move &&
			FVector2D::DistSquared(Formation.AnchorPos, Order->AnchorPos) > 1.f)
		{
			// No path (unreachable): no anchor motion. Facing still tracks the
			// order intent so the formation presents correctly.
			if (!bInterpenetrating)
			{
				Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
					Formation.FacingYaw, Order->FacingYaw, FacingDeltaRate, DT);
			}
		}
		else if (!bHasPath && !bInterpenetrating && Order->Type == FUnitOrder::EType::Stop)
		{
			// Stopped with no path: settle facing to the order facing if any.
			if (Order->FacingYaw != 0.f)
			{
				Formation.FacingYaw = FUnitLocomotion::AdvanceFacing(
					Formation.FacingYaw, Order->FacingYaw, FacingDeltaRate, DT);
			}
		}
	});
}
