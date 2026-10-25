#include "ServoProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleServo.h"
#include "BattleUnitContact.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementServo(
		TEXT("Battle.Movement.Servo"),
		1,
		TEXT("Enable Servo processing (arch §4 row 7). 0 opts the stage out."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleMovementMaxAccel(
		TEXT("Battle.Movement.MaxAccel"),
		FUnitServoConstants::MaxAccel,
		TEXT("Servo acceleration clamp, m/s^2 (arch §8.3)."),
		ECVF_Default);
}

UServoProcessor::UServoProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ServoQuery(*this)
	, SnapshotQuery(*this)
	, UnitBodyQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("FormationSlotResolveProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 7).
	bRequiresGameThreadExecution = false;
}

void UServoProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	ServoQuery.AddRequirement<FSlotTargetFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddRequirement<FServoStateFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	ServoQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);

	// Frame snapshot: every battle entity's position/velocity/state, all
	// read-only (safe to run to completion before the servo chunks). Phase 1
	// scale (8 units x ~40) -- flat array, no spatial hash (fact 1.11).
	SnapshotQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);

	// Unit bodies: one chunk per unit (shared-formation grouped).
	UnitBodyQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
}

void UServoProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine Servo first-execute"));
	}

	if (GBattleMovementServo.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();
	UBattleDebugSubsystem* Debug = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;

	// ---- Frame snapshot (all battle entities) ----
	struct FSnapshotEntry
	{
		FVector2D Pos;
		FVector2D Vel;
		FUnitHandle Owner;
		uint32 HandleIndex;
		float RadiusM;
	};
	TArray<FSnapshotEntry> Snapshot;
	SnapshotQuery.ForEachEntityChunk(Context, [&Snapshot](FMassExecutionContext& Context)
	{
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FSnapshotEntry Entry;
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			Entry.Pos = FVector2D(P.X, P.Y) * 0.01f;
			Entry.Vel = FVector2D(Velocities[i].Value.X, Velocities[i].Value.Y) * 0.01f;
			Entry.Owner = Owners[i].Owner;
			Entry.HandleIndex = Context.GetEntity(i).Index;
			Entry.RadiusM = 0.4f; // FAgentRadiusFragment 40 cm (fact 1.9); cavalry widens in Task 9
			Snapshot.Add(Entry);
		}
	});

	const float MaxAccel = GBattleMovementMaxAccel.GetValueOnAnyThread();

	// ---- Unit-vs-unit OBB contact (arch §8.6, Phase 1 scale: 8 units) ----
	// One body per unit from the formation fragment; pairs resolved to
	// per-entity shove velocities for the PassThrough policy.
	TArray<FUnitOBB> UnitBodies;
	UnitBodies.Reserve(16);
	TArray<FUnitHandle> BodyUnits;
	BodyUnits.Reserve(16);
	TArray<bool> BodyMoving; // §8.6: only a moving unit has right-of-way to shove
	UnitBodyQuery.ForEachEntityChunk(Context, [&UnitBodies, &BodyUnits, &BodyMoving](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment* FormationPtr = Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		if (!FormationPtr || FormationPtr->NumSlots <= 0 || FormationPtr->NumFiles <= 0 || Owners.Num() == 0)
		{
			return;
		}
		const FUnitFormationSharedFragment& Formation = *FormationPtr;
		const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation.NumSlots) / Formation.NumFiles));
		FUnitOBB Body;
		Body.Center = Formation.AnchorPos * 0.01f; // cm -> m
		Body.YawDeg = Formation.FacingYaw;
		Body.HalfAlongFacing = (NumRanks * Formation.RankSpacing * 0.5f) * 0.01f;
		Body.HalfAcrossFacing = (Formation.NumFiles * Formation.FileSpacing * 0.5f) * 0.01f;
		UnitBodies.Add(Body);
		BodyUnits.Add(Owners[0].Owner);
		BodyMoving.Add(Formation.SpeedLimit > 10.f);
	});

	// Per-snapshot-entity shove displacement (m/s) + blend weight.
	TArray<FVector2D> ShoveVel;
	ShoveVel.Init(FVector2D::ZeroVector, Snapshot.Num());
	TArray<float> ShoveW;
	ShoveW.Init(0.f, Snapshot.Num());
	for (int32 A = 0; A < UnitBodies.Num(); ++A)
	{
		if (!BodyMoving[A])
		{
			continue; // stationary formations never displace others (HoldSlot crowds via servo+compression)
		}
		for (int32 B = 0; B < UnitBodies.Num(); ++B)
		{
			if (A == B || BodyUnits[A] == BodyUnits[B])
			{
				continue;
			}
			const float Depth = FUnitContact::OverlapDepth(UnitBodies[A], UnitBodies[B]);
			if (Depth <= 0.f)
			{
				continue;
			}
			// Same-team fixtures in Phase 1: the mover (PassThrough side,
			// documented policy at Phase 1 fixtures) shoves the static unit's
			// entities aside; cavalry-vs-infantry mass ratio 2:1.
			const FUnitOBB& Mover = UnitBodies[A];
			const float Blend = FUnitContact::ShoveBlend(2.0f /* cavalry:infantry mass ratio */, Depth);

			// Infantry of the weaker unit inside the mover's OBB get shoved.
			for (int32 S = 0; S < Snapshot.Num(); ++S)
			{
				if (Snapshot[S].Owner != BodyUnits[B])
				{
					continue;
				}
				if (!FUnitContact::ContainsPoint(Mover, Snapshot[S].Pos))
				{
					continue;
				}
				// Push perpendicular to the mover's facing (aside the path),
				// away from the mover center line.
				const float Rad = FMath::DegreesToRadians(Mover.YawDeg);
				FVector2D Side(-FMath::Sin(Rad), FMath::Cos(Rad));
				if (FVector2D::DotProduct(Side, Snapshot[S].Pos - Mover.Center) < 0.f)
				{
					Side = -Side;
				}
				ShoveVel[S] = Side * FUnitContact::ShoveSpeed;
				ShoveW[S] = FMath::Max(ShoveW[S], Blend);
			}
		}
	}

	// Handle -> snapshot row (the servo chunks iterate a different order).
	TMap<uint32, int32> HandleToSnapshot;
	HandleToSnapshot.Reserve(Snapshot.Num());
	for (int32 S = 0; S < Snapshot.Num(); ++S)
	{
		HandleToSnapshot.Add(Snapshot[S].HandleIndex, S);
	}

	ServoQuery.ForEachEntityChunk(Context, [DT, MaxAccel, Debug, &Snapshot, &ShoveVel, &ShoveW, &HandleToSnapshot](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const auto Targets = Context.GetFragmentView<FSlotTargetFragment>();
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		auto States = Context.GetMutableFragmentView<FMovementStateFragment>();
		auto ServoStates = Context.GetMutableFragmentView<FServoStateFragment>();
		auto Velocities = Context.GetMutableFragmentView<FMassVelocityFragment>();

		// Unit speed limit arrives in cm/s (fragment); servo math is m/s.
		const float MaxSpeedM = Formation.SpeedLimit * 0.01f;

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			const FVector2D PosM(P.X * 0.01f, P.Y * 0.01f);
			const FVector2D SlotPosM = FVector2D(Targets[i].TargetPos) * 0.01f;
			const FVector2D OldVelM(Velocities[i].Value.X * 0.01f, Velocities[i].Value.Y * 0.01f);
			const EBattleMovementState State = States[i].State;

			// §8.3 servo (velocity control, ADR-07).
			FVector2D DesiredVel = FUnitServo::ComputeServoVelocity(PosM, SlotPosM,
				FUnitServoConstants::Tau, MaxSpeedM, FUnitServoConstants::CatchupBoost);

			// A1 per-state avoidance weights (§4.5): HoldSlot none;
			// ManeuverToSlot medium teammates-only; CrossField full.
			float Weight = 0.f;
			if (State == EBattleMovementState::ManeuverToSlot)
			{
				Weight = 0.5f;
			}
			else if (State == EBattleMovementState::CrossField)
			{
				Weight = 1.0f;
			}

			const uint32 MyHandle = Context.GetEntity(i).Index;
			const FUnitHandle MyOwner = Owners[i].Owner;
			if (Weight > 0.f)
			{
				TArray<FAvoidanceNeighbor> Neighbors;
				Neighbors.Reserve(128);
				for (const FSnapshotEntry& Entry : Snapshot)
				{
					if (Entry.HandleIndex == MyHandle)
					{
						continue;
					}
					// ManeuverToSlot: teammates only (§4.5).
					if (State == EBattleMovementState::ManeuverToSlot && Entry.Owner != MyOwner)
					{
						continue;
					}
					FAvoidanceNeighbor N;
					N.Pos = Entry.Pos;
					N.Vel = Entry.Vel;
					N.Radius = Entry.RadiusM;
					N.HandleIndex = Entry.HandleIndex;
					Neighbors.Add(N);
				}
				const float WeightMul = FUnstuckLadder::AvoidanceWeightMultiplier(ServoStates[i].LadderLevel);
				const FVector2D Deflection = FUnitServo::ComputeAvoidanceDeflection(
					PosM, DesiredVel, 0.4f, MyHandle, Neighbors, Weight * WeightMul);
				DesiredVel += Deflection;

				// Step 2 sidestep impulse: perpendicular to the servo output.
				if (FUnstuckLadder::WantsSidestep(ServoStates[i].LadderLevel) &&
					ServoStates[i].SidestepDir.SizeSquared() > KINDA_SMALL_NUMBER)
				{
					DesiredVel += ServoStates[i].SidestepDir * MaxSpeedM * 0.5f;
				}
			}

			// §8.6 pass-through shove: displacement velocity blended over the
			// servo output (w = clamp(MassRatio * OverlapDepth)); the servo
			// recovers the wake for free once contact ends.
			FVector2D NewVelM = FUnitServo::ClampAcceleration(OldVelM, DesiredVel, MaxAccel, DT);
			if (const int32* SnapIdx = HandleToSnapshot.Find(Context.GetEntity(i).Index))
			{
				const float W = ShoveW[*SnapIdx];
				if (W > KINDA_SMALL_NUMBER)
				{
					NewVelM = FMath::Lerp(NewVelM, ShoveVel[*SnapIdx], W);
					ServoStates[i].DisplacementWeight = W;
				}
				else
				{
					ServoStates[i].DisplacementWeight = 0.f;
				}
			}
			Velocities[i].Value = FVector(NewVelM.X, NewVelM.Y, 0.f) * 100.f;

			// Ladder bookkeeping (§8.5): slow + away from slot escalates.
			const float SpeedM = NewVelM.Size();
			const float DistToSlotM = FVector2D::Distance(PosM, SlotPosM);
			const bool bNearSlot = DistToSlotM < FUnitServoConstants::SlotReachEps;
			const bool bSlow = SpeedM < FUnitServoConstants::StuckSpeedEps;
			float Timer = ServoStates[i].LowSpeedTime;
			const int32 NewLevel = FUnstuckLadder::Advance(ServoStates[i].LadderLevel, bSlow, bNearSlot,
				DT, Timer, FUnitServoConstants::EscalateAfterSeconds);
			if (NewLevel != ServoStates[i].LadderLevel)
			{
				if (Debug)
				{
					Debug->RecordLadderEscalation(); // T1.5/T1.8 zero-escalation gate
				}
				// Every step logged (§8.5) -- one line per transition, not per frame.
				UE_LOG(LogMassBattle, Display,
					TEXT("EXEC_RESULT UnstuckLadder Entity=%u:%u Unit=%u:%u Level=%d DistToSlot=%.2f"),
					Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
					MyOwner.Index, MyOwner.Serial, NewLevel, DistToSlotM);
				if (Debug)
				{
					FBattleOverlayLine Line;
					Line.From = P;
					Line.To = FVector(SlotPosM.X * 100.f, SlotPosM.Y * 100.f, P.Z);
					// Step colors (A3): 1 yellow, 2 orange, 3 red, 4 magenta.
					static const FColor LevelColors[5] = { FColor::Green, FColor::Yellow, FColor::Orange, FColor::Red, FColor::Magenta };
					Line.Color = LevelColors[FMath::Clamp(NewLevel, 0, 4)];
					Line.LadderLevel = NewLevel;
					Debug->EnqueueOverlayLine(Line);
				}
				// Step 2: fix the sidestep direction perpendicular to the servo.
				if (FUnstuckLadder::WantsSidestep(NewLevel))
				{
					const FVector2D ServoDir = (SlotPosM - PosM).GetSafeNormal();
					FVector2D Perp(-ServoDir.Y, ServoDir.X);
					if (Perp.SizeSquared() < KINDA_SMALL_NUMBER)
					{
						Perp = FVector2D(0.f, 1.f);
					}
					ServoStates[i].SidestepDir = Perp;
				}
			}
			ServoStates[i].LowSpeedTime = Timer;
			ServoStates[i].LadderLevel = NewLevel;

			// Servo-local transition: ManeuverToSlot closes into HoldSlot.
			if (State == EBattleMovementState::ManeuverToSlot && bNearSlot)
			{
				States[i].State = EBattleMovementState::HoldSlot;
			}
		}
	});
}
