#include "ServoProcessor.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleMovementFragments.h"
#include "BattleServo.h"
#include "BattleUnitContact.h"
#include "BattleMelee.h"
#include "BattleEngagement.h"
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

	// Phase 2 A1 (F-4): slot-velocity feed-forward in the servo. 0 keeps the
	// pure position pursuit byte-for-byte.
	// OWNER DEFAULT FLIP (2026-10-28, formation-flow v2): the pure pursuit
	// trails any moving slot by Tau*v and closes a drag at only the quickstep
	// margin -- the owner's tail-drag symptom. The FF form (Reynolds velocity
	// matching) is the measured fix and cooperates with the seek boost below.
	// Ships ON; restore with `Battle.Movement.FeedForward 0`.
	TAutoConsoleVariable<int32> GBattleMovementFeedForward(
		TEXT("Battle.Movement.FeedForward"),
		1,
		TEXT("1 = servo adds the slot-velocity feed-forward term (V_slot + (Target-Pos)/Tau). Phase 2 A1."),
		ECVF_Default);

	// Phase 2 A2 (F-3): the servo ceiling is the MEMBER's own max speed
	// (WalkSpeed x SpeedMult), not the anchor's pace -- in a wheel the anchor
	// slows and every entity's ceiling must NOT drop with it, because outer
	// files have to move faster than the anchor to track their sweeping slots.
	// OWNER DEFAULT FLIP (2026-10-28, formation-flow v2): with the default
	// clamp the ceiling rides Formation.SpeedLimit -- INCLUDING the straggler
	// throttle, so a throttled recovery slows the very men it waits for
	// (0.25 x 0.25 closing; the coupling the owner's "significantly higher"
	// ask targets). The member clamp decouples the ceiling from the anchor
	// pace. Ships ON; restore with `Battle.Movement.MemberClamp 0`.
	TAutoConsoleVariable<int32> GBattleMovementMemberClamp(
		TEXT("Battle.Movement.MemberClamp"),
		1,
		TEXT("1 = servo clamps at the member's own max speed (arch §8.3 MaxSpeed = member max). Phase 2 A2."),
		ECVF_Default);

	// Formation-flow rule 2 (owner proposal 2026-10-28): the unstuck-ladder
	// level at which an entity's slot is considered PHYSICALLY UNREACHABLE and
	// the slot reflow may re-home it at its nearest reachable slot (the
	// EvictToReachable chain in the assigner). 0 disables -- marks stay 0 and
	// the resolve stage is byte-identical to the legacy pipeline. 2 = ~3 s
	// slow + far from slot (one escalation period past the sidestep step).
	TAutoConsoleVariable<int32> GBattleMovementStuckReawardLevel(
		TEXT("Battle.Movement.StuckReawardLevel"),
		2,
		TEXT("Unstuck-ladder level that marks a slot unreachable for the formation-flow reflow (0 = off; 1..4; default 2)."),
		ECVF_Default);

	// Formation-flow v2 (owner observation 2026-10-28): error-gated servo seek
	// boost. The §12 quickstep (CatchupBoost 1.25) leaves a closing rate of
	// only 0.25x against a marching anchor -- a dragged entity trails for
	// tens of seconds (the owner's tail-drag symptom) and outer files cannot
	// track a sweeping wheel (the arc symptom). Beyond SeekErrorGate meters
	// of slot error the servo clamp rises to SeekSpeedScale x the ceiling:
	// "entity slot seeking speed significantly higher than the unit movement
	// speed." 1.8 x walk = 2.52 m/s against the 1.4 m/s anchor: ~1.1 m/s
	// closing. <= 1.25 restores the constant quickstep.
	TAutoConsoleVariable<float> GBattleMovementSeekSpeedScale(
		TEXT("Battle.Movement.SeekSpeedScale"),
		1.8f,
		TEXT("Servo clamp multiplier while slot error exceeds SeekErrorGate (formation-flow v2; <= 1.25 = the constant quickstep)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleMovementSeekErrorGate(
		TEXT("Battle.Movement.SeekErrorGate"),
		1.f,
		TEXT("Slot error (m) that arms the seek-speed boost; below it the quickstep ceiling holds so dressed blocks do not churn."),
		ECVF_Default);
}

UServoProcessor::UServoProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ServoQuery(*this)
	, SnapshotQuery(*this)
	, UnitBodyQuery(*this)
	, CavalryUnitQuery(*this)
	, ZeroVelocityQuery(*this)
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
	// Audit M1 step 3: the forced slot swap reads/writes slot indices.
	ServoQuery.AddRequirement<FSlotAssignmentFragment>(EMassFragmentAccess::ReadWrite);
	// Audit M8 (all movement queries): death-window corpses excluded.
	ServoQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	ServoQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	// Phase 2 A2: the member clamp reads the unit's SpeedMult (stats fragment
	// travels on every battle entity; chunk grouping is per unit already).
	ServoQuery.AddSharedRequirement<FUnitStatsSharedFragment>(EMassFragmentAccess::ReadOnly);
	// Phase 2 A12: combat pair states drive the movement state + target (the
	// ONLY existing-processor consumption point, brief A12).
	ServoQuery.AddRequirement<FCombatPairFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FCombatIntentFragment>(EMassFragmentAccess::ReadOnly);
	ServoQuery.AddRequirement<FChargeFragment>(EMassFragmentAccess::ReadOnly);

	// Frame snapshot: every battle entity's position/velocity/state, all
	// read-only (safe to run to completion before the servo chunks). Phase 1
	// scale (8 units x ~40) -- flat array, no spatial hash (fact 1.11).
	SnapshotQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	SnapshotQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // audit M8

	// Unit bodies: one chunk per unit (shared-formation grouped). Audit M8: no corpses.
	// Phase 2 task 7: movement state + velocity ride along for the charge
	// knockback (a charging unit displaces what it rides over, spec §4.9).
	UnitBodyQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FMovementStateFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	UnitBodyQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);

	// Same grouping, cavalry rosters only (§8.6 mobility-advantage input).
	CavalryUnitQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	CavalryUnitQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	CavalryUnitQuery.AddTagRequirement<FCavalryTag>(EMassFragmentPresence::All);
	CavalryUnitQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // audit M8

	// Audit M6 companion query: zeroes velocity when the servo stage is gated
	// out (battle entities only -- FOwnerUnitFragment scopes it).
	ZeroVelocityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	ZeroVelocityQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	ZeroVelocityQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None); // audit M8
}

void UServoProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GBattleMovementServo.GetValueOnAnyThread() == 0)
	{
		// Audit M6: a gated-out servo must still zero planar velocity, or the
		// Integrate stage (if left on) coasts every previously-moving entity
		// at constant speed forever. The B.8 opt-out must not corrupt the sim.
		ZeroVelocityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
		{
			auto Velocities = Context.GetMutableFragmentView<FMassVelocityFragment>();
			for (int32 i = 0; i < Context.GetNumEntities(); ++i)
			{
				Velocities[i].Value = FVector::ZeroVector;
			}
		});
		return;
	}

	const float DT = Context.GetDeltaTimeSeconds();
	UBattleDebugSubsystem* Debug = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	// Cached once (audit H4): the pointer is stable for the process; a
	// FindConsoleVariable per chunk per frame is pure lookup waste.
	if (!WalkSpeedCvar)
	{
		WalkSpeedCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.WalkSpeed"));
	}
	if (!FallInCvar)
	{
		FallInCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.ProgressiveFallIn"));
	}

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
	Snapshot.Reserve(320); // Phase-1 fixture scale (audit H4: capacity reuse, not per-frame growth)
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
			Entry.RadiusM = FCombatSolver::Contact::BodyRadiusM; // fact 1.9; cavalry widens in Task 9
			Snapshot.Add(Entry);
		}
	});

	// §8.6 mobility input (audit H3): the set of cavalry-owned units. Units are
	// class-uniform (one EDU row per unit), so chunk membership is the unit's
	// class.
	TSet<FUnitHandle> CavalryUnits;
	CavalryUnitQuery.ForEachEntityChunk(Context, [&CavalryUnits](FMassExecutionContext& Context)
	{
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		if (Owners.Num() > 0)
		{
			CavalryUnits.Add(Owners[0].Owner);
		}
	});

	const float MaxAccel = GBattleMovementMaxAccel.GetValueOnAnyThread();
	const bool bFeedForward = GBattleMovementFeedForward.GetValueOnAnyThread() != 0;
	const bool bMemberClamp = GBattleMovementMemberClamp.GetValueOnAnyThread() != 0;
	const bool bFallInServo = FallInCvar && FallInCvar->GetInt() != 0;
	// Formation-flow rule 2 feed: the ladder threshold that marks a slot as
	// physically unreachable for the next slot reflow (read once per Execute,
	// captured by value like the flags above).
	const int32 StuckReawardLevel = GBattleMovementStuckReawardLevel.GetValueOnAnyThread();
	// Formation-flow v2 feeds: the error-gated seek boost (owner ask: seek
	// speed significantly higher than the unit movement speed).
	const float SeekSpeedScale = GBattleMovementSeekSpeedScale.GetValueOnAnyThread();
	const float SeekErrorGateM = GBattleMovementSeekErrorGate.GetValueOnAnyThread();

	// ---- Unit-vs-unit OBB contact (arch §8.6, Phase 1 scale: 8 units) ----
	// One body per unit from the formation fragment; pairs resolved to
	// per-entity shove velocities for the PassThrough policy.
	TArray<FUnitOBB> UnitBodies;
	UnitBodies.Reserve(16);
	TArray<FUnitHandle> BodyUnits;
	BodyUnits.Reserve(16);
	TArray<bool> BodyMoving; // §8.6: only a moving unit has right-of-way to shove
	TArray<bool> BodyCharging; // task 7: any entity in the Charge movement state
	TArray<float> BodySpeedM; // mean live speed (knockback magnitude input)
	UnitBodyQuery.ForEachEntityChunk(Context, [&UnitBodies, &BodyUnits, &BodyMoving, &BodyCharging, &BodySpeedM](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment* FormationPtr = Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		if (!FormationPtr || FormationPtr->NumSlots <= 0 || FormationPtr->NumFiles <= 0 || Owners.Num() == 0)
		{
			return;
		}
		if (FormationPtr->GarrisonMode != EGarrisonMode::None)
		{
			return; // R-G3: garrisoned units are neither shove sources nor targets
		}
		const FUnitFormationSharedFragment& Formation = *FormationPtr;
		// Audit L9: one body construction site for all callers (MakeUnitBody).
		const FUnitOBB Body = FUnitContact::MakeUnitBody(Formation.AnchorPos, Formation.FacingYaw,
			Formation.NumSlots, Formation.NumFiles, Formation.RankSpacing, Formation.FileSpacing);
		UnitBodies.Add(Body);
		BodyUnits.Add(Owners[0].Owner);
		BodyMoving.Add(Formation.SpeedLimit > 10.f);
		const auto MoveStates = Context.GetFragmentView<FMovementStateFragment>();
		const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();
		bool bCharging = false;
		float SpeedSum = 0.f;
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			bCharging = bCharging || MoveStates[i].State == EBattleMovementState::Charge;
			SpeedSum += FVector(Velocities[i].Value.X, Velocities[i].Value.Y, 0.f).Size() * 0.01f;
		}
		BodyCharging.Add(bCharging);
		BodySpeedM.Add(Context.GetNumEntities() > 0 ? SpeedSum / Context.GetNumEntities() : 0.f);
	});

	// Per-snapshot-entity shove displacement (m/s) + blend weight.
	// Audit H4: capacity reserved so Init reuses the allocation per frame.
	TArray<FVector2D> ShoveVel;
	ShoveVel.Reserve(Snapshot.Num());
	ShoveVel.Init(FVector2D::ZeroVector, Snapshot.Num());
	TArray<float> ShoveW;
	ShoveW.Reserve(Snapshot.Num());
	ShoveW.Init(0.f, Snapshot.Num());
	// Audit H3: Block-policy pairs. Equal-footing contact shoves nobody --
	// instead the mover's entities take the blocking unit's entities as
	// avoidance neighbors below (crowd/deflect at the contact instead of
	// servoing straight through). This is the mechanism behind the S3
	// pass-mouth queue. Built before the chunk fan-out; read-only in it.
	TMap<FUnitHandle, TArray<FUnitHandle>> BlockPartners;
	BlockPartners.Reserve(16);
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
			// §8.6 policy resolution (audit H3 -- ResolvePolicy is now the
			// single decision point). A is the mover (right-of-way: the
			// doctrine that only a moving unit displaces others); friendly is
			// true for the Phase 1 same-team fixture set (hostile contact is
			// Phase 2); mobility advantage = cavalry mover vs non-cavalry.
			// Phase 2 task 7 (spec §4.9): a CHARGING mover rides THROUGH and
			// displaces what it hits -- the knockback consumes the existing
			// displacement blend at a bounded impulse along the charge heading.
			const bool bAdvantage = CavalryUnits.Contains(BodyUnits[A]) && !CavalryUnits.Contains(BodyUnits[B]);
			FUnitContact::EContactPolicy Policy = FUnitContact::ResolvePolicy(
				/*bFriendly*/ true, bAdvantage, /*bRightOfWay*/ true);
			const bool bChargingMover = BodyCharging[A];
			if (bChargingMover)
			{
				Policy = FUnitContact::EContactPolicy::PassThrough;
			}
			if (Policy != FUnitContact::EContactPolicy::PassThrough)
			{
				BlockPartners.FindOrAdd(BodyUnits[A]).AddUnique(BodyUnits[B]);
				continue;
			}
			const FUnitOBB& Mover = UnitBodies[A];
			// Cavalry-over-infantry mass ratio 2:1; equal classes 1:1.
			const float MassRatio = bAdvantage ? 2.0f : 1.0f;
			const float Blend = FUnitContact::ShoveBlend(MassRatio, Depth);

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

	const IConsoleVariable* WalkCvar = WalkSpeedCvar;
	ServoQuery.ForEachEntityChunk(Context, [DT, MaxAccel, bFeedForward, bMemberClamp, bFallInServo, StuckReawardLevel, SeekSpeedScale, SeekErrorGateM, Debug, &Snapshot, &ShoveVel, &ShoveW, &HandleToSnapshot, &BlockPartners, WalkCvar](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const FUnitStatsSharedFragment* Stats = Context.GetSharedFragmentPtr<FUnitStatsSharedFragment>();
		const auto Targets = Context.GetFragmentView<FSlotTargetFragment>();
		const auto Transforms = Context.GetFragmentView<FTransformFragment>();
		const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();
		const auto CombatPairs = Context.GetFragmentView<FCombatPairFragment>();
		const auto CombatIntents = Context.GetFragmentView<FCombatIntentFragment>();
		const auto ChargeStates = Context.GetFragmentView<FChargeFragment>();
		auto States = Context.GetMutableFragmentView<FMovementStateFragment>();
		auto ServoStates = Context.GetMutableFragmentView<FServoStateFragment>();
		auto Velocities = Context.GetMutableFragmentView<FMassVelocityFragment>();

		// Unit speed limit arrives in cm/s (fragment); servo math is m/s.
		// A unit WITHOUT an order (SpeedLimit 0) holds its ground -- but its
		// entities still re-dress (wake recovery, slot fills): they get the
		// walk speed as the servo ceiling. Marching units clamp at their
		// section 8.2 limit -- EXCEPT under Battle.Movement.MemberClamp
		// (phase 2 A2, F-3): the ceiling is then the member's own max speed
		// (arch §8.3 MaxSpeed means member max, not the anchor's pace), so a
		// wheel's outer files may quickstep while the anchor slows. The
		// no-order fallback keeps its walk-speed ceiling in both paths.
		float MaxSpeedM = Formation.SpeedLimit * 0.01f;
		if (bMemberClamp && Stats)
		{
			MaxSpeedM = (WalkCvar ? WalkCvar->GetFloat() : 1.4f) * Stats->SpeedMult;
		}
		if (MaxSpeedM <= 0.f)
		{
			MaxSpeedM = WalkCvar ? WalkCvar->GetFloat() : 1.4f;
		}

		// Audit H3: this unit's Block partners (avoid-them instead of shove-
		// through). Chunk = one unit, so one lookup.
		const TArray<FUnitHandle>* MyBlockPartners = BlockPartners.Find(Formation.OwnerUnit);

		// Audit H4: one capacity-reused neighbor buffer per chunk (chunks are
		// single-threaded internally; Reset keeps the allocation) instead of a
		// heap allocation per entity per frame.
		TArray<FAvoidanceNeighbor> Neighbors;
		Neighbors.Reserve(128);

		// Avoidance gather cull radius (audit H4): neighbors beyond this can
		// matter only through the 1.5 s horizon at the fastest commanded
		// speeds; inside the radius the deflection math is unchanged.
		constexpr float CullRadiusSq = FMath::Square(12.f);

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FVector P = Transforms[i].GetTransform().GetTranslation();
			const FVector2D PosM(P.X * 0.01f, P.Y * 0.01f);
			const FVector2D SlotPosM = FVector2D(Targets[i].TargetPos) * 0.01f;
			const FVector2D OldVelM(Velocities[i].Value.X * 0.01f, Velocities[i].Value.Y * 0.01f);
			EBattleMovementState State = States[i].State;

			// Phase 2 A12: combat pair states OWN the movement state + target
			// while the entity carries a live press ring (the assign pass
			// tracks the target each tick). Seeking -> CrossField toward the
			// press point; Pressing -> PressRing; Striking/Recovering -> hold
			// at contact (HoldSlot weights; compression owns crowding). The
			// charge override (state only; task 7 seeds the impact) comes last.
			FVector2D ServoTargetM = SlotPosM;
			bool bCombatOverride = false;
			if (CombatPairs[i].bRingValid != 0
				&& (CombatIntents[i].TargetUnit.IsValid() || CombatIntents[i].bAutoEngage != 0))
			{
				ServoTargetM = FVector2D(CombatPairs[i].RingPosCm) * 0.01f;
				bCombatOverride = true;
				switch (CombatPairs[i].State)
				{
				case ECombatPairState::Seeking:
					State = EBattleMovementState::CrossField;
					break;
				case ECombatPairState::Pressing:
					State = EBattleMovementState::PressRing;
					break;
				case ECombatPairState::Striking:
				case ECombatPairState::Recovering:
				default:
					State = EBattleMovementState::HoldSlot;
					break;
				}
			}
			if (ChargeStates[i].DistanceTraveled > 0.f && ChargeStates[i].bConnected == 0)
			{
				State = EBattleMovementState::Charge; // no avoidance (phase-1 weight)
				bCombatOverride = true;
			}

			// Formation-flow v2 (owner ask 2026-10-28: "entity slot seeking
			// speed needs to be significantly higher than the unit movement
			// speed"): error-gated seek boost. A dressed entity keeps the
			// §12 quickstep ceiling (1.25x); an entity whose slot error
			// exceeds the gate may run up to SeekSpeedScale x -- the closing
			// rate against a marching anchor rises from 0.25x to
			// (SeekScale - 1)x, which is what actually drains a drag.
			// Measured against the SLOT target (Targets[i].TargetPos), not
			// the combat-override target: ring approach speeds are combat
			// policy, not formation recovery. The same ceiling bounds the
			// avoidance/deflection/shove clamps below (the audit M4 doctrine:
			// nothing walks the limit upward).
			const float SlotErrM = FVector2D::Distance(PosM, SlotPosM);
			const float ServoBoost = (SeekSpeedScale > FUnitServoConstants::CatchupBoost && SlotErrM > SeekErrorGateM)
				? SeekSpeedScale
				: FUnitServoConstants::CatchupBoost;
			const float ServoCeilM = MaxSpeedM * ServoBoost;

			// §8.3 servo (velocity control, ADR-07). Phase 2 A1: the
			// feed-forward form adds the slot's own velocity (Reynolds
			// velocity matching on a moving target) so sweeping/wheeling
			// slots are tracked instead of perpetually trailed.
			FVector2D DesiredVel;
			if (bFeedForward)
			{
				const FVector2D PrevSlotPosM = Targets[i].bPrevValid
					? FVector2D(Targets[i].PrevTargetPos) * 0.01f
					: SlotPosM; // first write: no slot motion yet
				DesiredVel = FUnitServo::ComputeServoVelocityFF(PosM, ServoTargetM,
					bCombatOverride ? ServoTargetM : PrevSlotPosM,
					FUnitServoConstants::Tau, MaxSpeedM, ServoBoost, DT);
			}
			else
			{
				DesiredVel = FUnitServo::ComputeServoVelocity(PosM, ServoTargetM,
					FUnitServoConstants::Tau, MaxSpeedM, ServoBoost);
			}

			// Phase 2 C3 settle (the plan's §4.1 flow: settled = stop and turn
			// to the slot facing). Inside the slot's arrival basin the
			// commanded speed is CAPPED, not zeroed: the pure pursuit form
			// still commands ~1 m/s at SlotReachEps so entities micro-orbit
			// the slot forever -- velocity heading never drops below the rest
			// eps and per-entity facing never settles (S7 measured offYaw
			// 38-40). A hard stop instead parks entities up to eps short of
			// center and trips the separation gates (funcon S6 measured
			// 0.293 m). The cap keeps the pursuit's exact-center convergence
			// (v -> pursuit -> 0) with a damped final meter.
			if (bFallInServo && !bCombatOverride) // the press ring is not a settle basin
			{
				const float DistM = FVector2D::Distance(PosM, ServoTargetM);
				constexpr float BasinR = 2.f * FUnitServoConstants::SlotReachEps;
				if (DistM < BasinR)
				{
					constexpr float BasinCapM = 0.5f; // m/s, the damped close
					if (DesiredVel.SizeSquared() > BasinCapM * BasinCapM)
					{
						DesiredVel = DesiredVel.GetSafeNormal() * BasinCapM;
					}
				}
			}

			// A1 per-state avoidance weights (§4.5): HoldSlot none;
			// ManeuverToSlot medium teammates-only; CrossField full.
			float Weight = 0.f;
			if (State == EBattleMovementState::ManeuverToSlot || State == EBattleMovementState::PressRing)
			{
				Weight = 0.5f; // PressRing: RVO within the mob radius only (§8.3)
			}
			else if (State == EBattleMovementState::CrossField)
			{
				Weight = 1.0f;
			}
			// Charge / HoldSlot / Flee: none (charge has phase-1 no-avoidance).

			const uint32 MyHandle = Context.GetEntity(i).Index;
			const FUnitHandle MyOwner = Owners[i].Owner;
			if (Weight > 0.f)
			{
				Neighbors.Reset();
				for (const FSnapshotEntry& Entry : Snapshot)
				{
					if (Entry.HandleIndex == MyHandle)
					{
						continue;
					}
					// Distance cull (audit H4): ~102k full-pair evaluations at
					// Phase-1 scale become a cheap squared-distance reject for
					// the overwhelming majority.
					if (FVector2D::DistSquared(PosM, Entry.Pos) > CullRadiusSq)
					{
						continue;
					}
					// ManeuverToSlot: teammates, plus §8.6 Block-policy
					// partners (audit H3) -- the blocking unit's entities are
					// exactly the obstacle this entity must crowd against
					// instead of shoving through or interpenetrating.
					if (Entry.Owner != MyOwner)
					{
						const bool bBlockPartner = MyBlockPartners &&
							MyBlockPartners->Contains(Entry.Owner);
						if (State != EBattleMovementState::CrossField && !bBlockPartner)
						{
							continue;
						}
					}
					FAvoidanceNeighbor& N = Neighbors.AddDefaulted_GetRef();
					N.Pos = Entry.Pos;
					N.Vel = Entry.Vel;
					N.Radius = Entry.RadiusM;
					N.HandleIndex = Entry.HandleIndex;
				}
				const float WeightMul = FUnstuckLadder::AvoidanceWeightMultiplier(ServoStates[i].LadderLevel);
				FVector2D Deflection = FUnitServo::ComputeAvoidanceDeflection(
					PosM, DesiredVel, FCombatSolver::Contact::BodyRadiusM, MyHandle, Neighbors, Weight * WeightMul);
				// Audit M4: the accumulated push is unbounded by construction
				// (full Combined per overlapping neighbor); clamp it so the
				// blended velocity can never ratchet past the §8.2 unit limit.
				Deflection = Deflection.GetClampedToMaxSize(ServoCeilM);
				DesiredVel += Deflection;

				// Step 2 sidestep impulse: perpendicular to the servo output.
				if (FUnstuckLadder::WantsSidestep(ServoStates[i].LadderLevel) &&
					ServoStates[i].SidestepDir.SizeSquared() > KINDA_SMALL_NUMBER)
				{
					DesiredVel += ServoStates[i].SidestepDir * MaxSpeedM * 0.5f;
				}
				// Audit M4 (rate + total): ClampAcceleration bounds the CHANGE,
				// not the magnitude -- clamp the commanded velocity itself so
				// avoidance + sidestep cannot walk the limit upward.
				DesiredVel = DesiredVel.GetClampedToMaxSize(ServoCeilM);
			}

			// §8.6 Block realized as a follow constraint (audit H3): the
			// lateral deflection above steers around a blocked path but never
			// brakes the forward press -- without this cap the servo would
			// grind the mover straight through the blocking formation. Against
			// each Block partner roughly ahead within the brake radius, the
			// forward speed is capped at the blocker's forward speed plus a
			// separation spring: entities QUEUE behind the contact (the S3
			// pass-mouth queue) instead of shoving through. PassThrough movers
			// have no partners and are untouched.
			if (MyBlockPartners)
			{
				const FVector2D MoveDir = DesiredVel.GetSafeNormal();
				if (MoveDir.SizeSquared() > KINDA_SMALL_NUMBER)
				{
					for (const FSnapshotEntry& Entry : Snapshot)
					{
						if (!MyBlockPartners->Contains(Entry.Owner))
						{
							continue;
						}
						const FVector2D Rel = Entry.Pos - PosM;
						const float Dist = Rel.Size();
						constexpr float CombinedRadius = 2.f * FCombatSolver::Contact::BodyRadiusM; // fact 1.9
						if (Dist > CombinedRadius + 2.2f)
						{
							continue;
						}
						if (FVector2D::DotProduct(Rel, MoveDir) <= 0.f)
						{
							continue; // partner behind or abreast: not a follow target
						}
						const float BlockerAheadSpeed = FMath::Max(0.f, FVector2D::DotProduct(Entry.Vel, MoveDir));
						const float Gap = Dist - CombinedRadius;
						constexpr float QueueGap = 0.5f;   // m, held station behind the blocker
						constexpr float SpringPerS = 2.0f; // m/s per m of gap error
						const float AllowedForward = FMath::Max(0.f,
							BlockerAheadSpeed + (Gap - QueueGap) * SpringPerS);
						const float Forward = FVector2D::DotProduct(DesiredVel, MoveDir);
						if (Forward > AllowedForward)
						{
							DesiredVel = MoveDir * AllowedForward;
						}
					}
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
			// Audit M4: the shove blend targets a fixed 1.5 m/s displacement
			// regardless of the unit's own limit -- clamp the post-blend
			// result to the servo ceiling.
			NewVelM = NewVelM.GetClampedToMaxSize(ServoCeilM);
			Velocities[i].Value = FVector(NewVelM.X, NewVelM.Y, 0.f) * 100.f;

			// Ladder bookkeeping (§8.5): slow + away from slot escalates.
			const float SpeedM = NewVelM.Size();
			const float DistToSlotM = FVector2D::Distance(PosM, ServoTargetM);
			const bool bNearSlot = DistToSlotM < FUnitServoConstants::SlotReachEps;
			const bool bSlow = SpeedM < FUnitServoConstants::StuckSpeedEps;
			float Timer = ServoStates[i].LowSpeedTime;
			const int32 OldLevel = ServoStates[i].LadderLevel;
			const int32 NewLevel = FUnstuckLadder::Advance(OldLevel, bSlow, bNearSlot,
				DT, Timer, FUnitServoConstants::EscalateAfterSeconds);
			if (NewLevel != OldLevel)
			{
				// Audit M10: count only ESCALATIONS. Advance() returns 0 on
				// recovery -- counting every de-escalation made the soak's
				// "escalations" metric uninterpretable.
				if (NewLevel > OldLevel && Debug)
				{
					Debug->RecordLadderEscalation(); // T1.5/T1.8 zero-escalation gate
				}
				// Every step logged (§8.5) -- one line per transition, not per frame.
				UE_LOG(LogMassBattle, Display,
					TEXT("EXEC_RESULT UnstuckLadder Entity=%u:%u Unit=%u:%u Level=%d DistToSlot=%.2f"),
					Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
					MyOwner.Index, MyOwner.Serial, NewLevel, DistToSlotM);
				if (Debug && NewLevel > OldLevel)
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

				// Audit M1, ladder step 3: forced slot swap with a blocking
				// teammate (brief Task 8). Deterministic: the first same-unit
				// entity within 1 m holding a DIFFERENT valid slot trades
				// indices, so the two trade destinations instead of shoving
				// at one crossing point.
				if (FUnstuckLadder::WantsSlotSwap(NewLevel) && !FUnstuckLadder::WantsSlotSwap(OldLevel))
				{
					auto Assignments = Context.GetMutableFragmentView<FSlotAssignmentFragment>();
					for (int32 j = 0; j < Context.GetNumEntities(); ++j)
					{
						if (j == i || Owners[j].Owner != MyOwner
							|| Assignments[j].SlotIndex < 0 || Assignments[j].SlotIndex == Assignments[i].SlotIndex)
						{
							continue;
						}
						const FVector Q = Transforms[j].GetTransform().GetTranslation();
						if (FVector2D::DistSquared(PosM, FVector2D(Q.X, Q.Y) * 0.01f) < FMath::Square(1.f))
						{
							UE_LOG(LogMassBattle, Display,
								TEXT("EXEC_RESULT UnstuckSlotSwap Entity=%u:%u Unit=%u:%u With=%u:%u SlotA=%d SlotB=%d"),
								Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
								MyOwner.Index, MyOwner.Serial,
								Context.GetEntity(j).Index, Context.GetEntity(j).SerialNumber,
								Assignments[i].SlotIndex, Assignments[j].SlotIndex);
							Swap(Assignments[i].SlotIndex, Assignments[j].SlotIndex);
							break;
						}
					}
				}
				// Audit M1, ladder step 4: escalate to the unit-AI layer --
				// the minimal handoff flag + counter (the consumer is a Phase
				// 2 ready hook; the flag is reset by a full recovery below).
				if (NewLevel == 4 && ServoStates[i].bUnitAIHandoff == 0)
				{
					ServoStates[i].bUnitAIHandoff = 1;
					if (Debug)
					{
						Debug->RecordUnitAIHandoff();
					}
					UE_LOG(LogMassBattle, Display,
						TEXT("EXEC_RESULT UnstuckHandoff Entity=%u:%u Unit=%u:%u (terminal level 4)"),
						Context.GetEntity(i).Index, Context.GetEntity(i).SerialNumber,
						MyOwner.Index, MyOwner.Serial);
				}
			}
			ServoStates[i].LowSpeedTime = Timer;
			ServoStates[i].LadderLevel = NewLevel;

			// Formation-flow rule 2 (owner proposal 2026-10-28): surface the
			// reachability ladder to the slot reflow. FormationSlotResolve
			// reads StuckMark when a reflow fires and re-homes marked entities
			// at their nearest reachable slot (EvictToReachable). Written every
			// frame (self-correcting against cvar changes and any state drift);
			// recovery to level 0 clears it. Diagnostic byte, never hashed
			// (bFinalLocked pattern).
			{
				auto AssignFrag = Context.GetMutableFragmentView<FSlotAssignmentFragment>();
				AssignFrag[i].StuckMark = (StuckReawardLevel > 0 && NewLevel >= StuckReawardLevel) ? 1 : 0;
			}
			if (bCombatOverride)
			{
				States[i].State = State; // the pair state owns the movement state
			}
			if (NewLevel == 0)
			{
				ServoStates[i].bUnitAIHandoff = 0; // recovered: handoff withdrawn
			}

			// Servo-local transitions around HoldSlot. Doctrine (§8.3): HoldSlot
			// runs NO avoidance -- so never enter it while still touching a
			// teammate (an overlap frozen there can never resolve; rotation
			// re-dress transients must shed contact under ManeuverToSlot's
			// teammate avoidance first), and leave it if contact appears.
			if (!bCombatOverride)
			{
				bool bTouchingTeammate = false;
				if (State == EBattleMovementState::ManeuverToSlot || State == EBattleMovementState::HoldSlot)
				{
					const float TouchSq = FMath::Square(2.f * FCombatSolver::Contact::BodyRadiusM); // fact 1.9
					for (const FSnapshotEntry& Entry : Snapshot)
					{
						if (Entry.HandleIndex == MyHandle || Entry.Owner != MyOwner)
						{
							continue;
						}
						if (FVector2D::DistSquared(PosM, Entry.Pos) < TouchSq)
						{
							bTouchingTeammate = true;
							break;
						}
					}
				}
				if (State == EBattleMovementState::ManeuverToSlot && bNearSlot && !bTouchingTeammate)
				{
					States[i].State = EBattleMovementState::HoldSlot;
				}
				else if (State == EBattleMovementState::HoldSlot && bTouchingTeammate)
				{
					States[i].State = EBattleMovementState::ManeuverToSlot;
				}
			}
		}
	});
}
