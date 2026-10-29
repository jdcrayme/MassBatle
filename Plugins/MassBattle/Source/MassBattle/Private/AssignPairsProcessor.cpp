// AssignPairsProcessor.cpp -- arch §4 row 5 (task 6; spec §4.4 bridge).
#include "AssignPairsProcessor.h"
#include "BattleRegistry.h"
#include "BattleRngRegistry.h"
#include "BattleEngagement.h"
#include "BattleMelee.h"
#include "BattleCombatFragments.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleExecCommands.h"
#include "BattleDebugSubsystem.h"
#include "HAL/PlatformTime.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
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
	// ReadWrite: the write-back consumes the one-shot knockback latch through
	// FCombatSolver::ApplyChargeKnockback (PR-4; the call site stays here --
	// see the seam function's comment).
	SeekerQuery.AddRequirement<FChargeFragment>(EMassFragmentAccess::ReadWrite);
	SeekerQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	SeekerQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	SeekerQuery.RegisterWithProcessor(*this);

	DefenderQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.AddRequirement<FBaseStatsFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
	DefenderQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	DefenderQuery.RegisterWithProcessor(*this);
}

void UAssignPairsProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleRngSubsystem* Rng = World ? World->GetSubsystem<UBattleRngSubsystem>() : nullptr;
	UMassEntitySubsystem* Entities = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UBattleDebugSubsystem* DebugSub = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
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
		const auto Stats = Context.GetFragmentView<FBaseStatsFragment>();
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
			Def.Defense = Stats[i].MeleeDefense;
			Def.Shield = Stats[i].Shield;
			Def.Armor = Stats[i].Armor;
			Def.MountHP = 0; // mount pools ride the health fragment (cavalry rows, T6.1)
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

	// ---- Order intents per unit (A4 + demo-fix3): the table build lives on
	// the seam (PR-4, audit D-4 step 2) -- pure and unit-tested; the processor
	// only consumes it below. LiveRecords mirrors RecordIndexMap's key set
	// (the filtered live records), in the registry's sorted-key order. ----
	TMap<FUnitHandle, FUnitOrder> Orders;
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		if (const FUnitOrder* Order = Registry->FindOrder(Handle))
		{
			Orders.Add(Handle, *Order);
		}
	}
	TArray<FEngagementKey> LiveRecords;
	LiveRecords.Reserve(RecordIndexMap.Num());
	for (const TPair<FEngagementKey, int32>& Rec : RecordIndexMap)
	{
		LiveRecords.Add(Rec.Key);
	}
	TMap<FUnitHandle, FCombatSolver::FAssignIntent> UnitIntents;
	FCombatSolver::ResolveUnitIntents(Orders, LiveRecords, UnitIntents);

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
			// AttackMove's target comes from the AUTO-ENGAGE resolution (the
			// nearest live-record enemy), not the raw order -- the raw order's
			// TargetUnit is empty until a player designates one (measured
			// 2026-10-27: the seekers read the raw order and never paired).
			if (bAutoEngage && !OrderTarget.IsValid())
			{
				if (const FCombatSolver::FAssignIntent* Resolved = UnitIntents.Find(OwnerUnit))
				{
					OrderTarget = Resolved->TargetUnit;
				}
			}
		}
		else if (!Order || Order->Type == FUnitOrder::EType::None)
		{
			// demo-fix3 (owner report 2026-10-28): idle unit under attack -- read
			// the defensive intent resolved above; the entities pair through the
			// (defender -> aggressor) record exactly like an ordered attacker's.
			if (const FCombatSolver::FAssignIntent* Resolved = UnitIntents.Find(OwnerUnit))
			{
				OrderTarget = Resolved->TargetUnit;
				bAutoEngage = Resolved->bAutoEngage;
			}
		}
		else
		{
			// Disengage (spec §4.7): re-tasking with Move/Stop while engaged
			// flags the unit's engagement records dead AND clears the unit's
			// pair fragments HERE -- a non-seeker entity never reaches the
			// write-back, so without this its stale pair target would persist
			// forever (measured 2026-10-27: the disengage never landed).
			Registry->FlagUnitEngagementsDead(OwnerUnit);
			auto DisengagePairs = Context.GetMutableFragmentView<FCombatPairFragment>();
			for (int32 i = 0; i < Context.GetNumEntities(); ++i)
			{
				if (DisengagePairs[i].Target.IsValid())
				{
					DisengagePairs[i].Target = FMassEntityHandle();
					DisengagePairs[i].State = ECombatPairState::Seeking;
					DisengagePairs[i].bRingValid = 0;
					DisengagePairs[i].BackoffTicks = 0;
				}
			}
		}
		// demo-fix3: combat intent = a live attack order OR the defensive
		// resolution (an idle unit under attack).
		const bool bUnitHasIntent = bUnitHasOrder || OrderTarget.IsValid();

		TArray<FCombatSolver::FAssignSeekerState>& ChunkBridges = BridgesByChunk.AddDefaulted_GetRef();
		TArray<int32>& ChunkRows = ChunkIndices.AddDefaulted_GetRef();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			// Stamp the intent fragments from the order surface (A4) or the
			// demo-fix3 defensive resolution (idle unit under attack).
			Intents[i].TargetUnit = bUnitHasIntent ? OrderTarget : FUnitHandle();
			Intents[i].bAutoEngage = bUnitHasIntent ? bAutoEngage : 0;
			if (!bUnitHasIntent)
			{
				// demo-fix3: intent GONE (defensive records died -- the attacker
				// disengaged or died) must release the stale pair HERE: a no-intent
				// entity never reaches the seam's release path, and the resolution
				// pass rolls across whatever pair fragment survives -- without this
				// a disengaged-from defender would keep killing the retreating
				// attacker forever (the disengage branch above covers re-tasked
				// orders; this covers the intent loss of a NEVER-ordered unit).
				if (Pairs[i].Target.IsValid())
				{
					Pairs[i].Target = FMassEntityHandle();
					Pairs[i].State = ECombatPairState::Seeking;
					Pairs[i].bRingValid = 0;
					Pairs[i].BackoffTicks = 0;
				}
				continue; // no combat intent: not a seeker this tick
			}
			FCombatSolver::FAssignSeekerState S;
			S.RosterIndex = Owners[i].RosterIndex;
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
	const double AssignStart = FPlatformTime::Seconds();
	FCombatSolver::RunAssignPass(Seekers, Defenders, Records, Tally, AssignContext);
	if (DebugSub)
	{
		DebugSub->RecordAssignMs((FPlatformTime::Seconds() - AssignStart) * 1000.0);
	}

	// ---- Write back: pair fragments + ring points (A12 press ring). ----
	{
		const bool bDebugOverlay = DebugSub && DebugSub->IsOverlayShown();
		int32 SeekerCursor = 0; // into the seam-mutated Seekers (chunk order preserved)
		SeekerQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
		{
			auto Pairs = Context.GetMutableFragmentView<FCombatPairFragment>();
			const auto Transforms = Context.GetFragmentView<FTransformFragment>();
			const auto Owners = Context.GetFragmentView<FOwnerUnitFragment>();

			auto Charges = Context.GetMutableFragmentView<FChargeFragment>();
			const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();
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
				const FMassEntityHandle PrevTarget = Out.Target;
				Out.Target = S.Target;
				// Defender stat mirror: stamped at PAIR CREATION only (the
				// resolution pass drains the mount mirror across ticks; a
				// refresh must not restore it).
				if (S.Target.IsValid() && S.Target != PrevTarget)
				{
					if (const FCombatSolver::FAssignDefenderState* Def = FCombatSolver::FindDefender(Defenders, S.Target))
					{
						Out.DefenderDefense = Def->Defense;
						Out.DefenderShield = Def->Shield;
						Out.DefenderArmor = Def->Armor;
						Out.DefenderMountRemaining = static_cast<int16>(Def->MountHP);
					}
				}
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
				const float RingRadiusM = FCombatSolver::RingRadiusFor(S.ReachClass, AssignContext);
				const FCombatSolver::FAssignDefenderState* RingDef = nullptr;
				float RingAngleDeg = 0.f;
				bool bRing = false;
				if (S.Target.IsValid())
				{
					RingDef = FCombatSolver::FindDefender(Defenders, S.Target);
					RingAngleDeg = FCombatSolver::DegreesFromContactAngle(S.ContactAngle);
					bRing = RingDef != nullptr;
				}
				else
				{
					// Waiters press toward the nearest intent defender's ring,
					// REGARDLESS of record state (pre-demo-fix3 flow; the
					// record-gated variant shipped 2026-10-28 and was reverted
					// same day -- func-tier evidence below).
					//
					// The gate (press ONLY while the unit's engagement record is
					// live, to stop pre-contact sprinting) deadlocks pairing:
					// records form at anchor-OBB contact, but the approach closes
					// at ~6 m/s against a 4 Hz cluster pass, so by the FIRST
					// record the formations have already interpenetrated. Waiter
					// ring targets derive from the CURRENT nearest defender each
					// tick -- past overlap the nearest defender can lie BEHIND an
					// entity, its ring point flips to the far side, and the press
					// walks entities away from / along the fight instead of into
					// it. Measured (CombatSmoke, gated): paired=0 for the full
					// 2400-tick budget with a live record, valid intents, and
					// pressure 24/24; one pair lands only on lucky geometry.
					// Bisection: the gate alone reproduces the stall (engaged-
					// march halt off); ungating alone restores pairing (halt on).
					//
					// The demo complaint that motivated the gate (sprint-ahead
					// then waiting on the anchor's contact while the straggler
					// throttle crawled) is addressed separately by
					// Battle.Movement.MemberClamp (formation-flow v2): the entity
					// servo ceiling no longer rides the throttled anchor pace,
					// which was the delay's feedback loop. A record-gated press
					// that ALSO converges needs non-overlapping contact (a
					// close-to-contact march deceleration zone) -- owner design
					// space, not a press-side patch.
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

					// Task 10 debug pass (B.2 pairing rows): the red attack line to
					// the paired entity (the write-back has the transforms + the
					// fresh pair in one place).
					if (bDebugOverlay && Out.Target.IsValid())
					{
						FMassEntityView TargetView(Entities->GetMutableEntityManager(), Out.Target);
						if (const FTransformFragment* TargetTransform = TargetView.GetFragmentDataPtr<FTransformFragment>())
						{
							const FVector TargetP = TargetTransform->GetTransform().GetTranslation();
							FBattleOverlayLine AttackLine;
							AttackLine.From = FVector(MyPosM.X * 100.f, MyPosM.Y * 100.f, 30.f);
							AttackLine.To = FVector(TargetP.X, TargetP.Y, 30.f);
							AttackLine.Color = FColor(255, 30, 30);
							DebugSub->EnqueueOverlayLine(AttackLine);
						}
					}

					// Charge knockback (spec §4.9): the math lives on the seam
					// (FCombatSolver::ApplyChargeKnockback, PR-4 audit D-4 step
					// 1) but the CALL SITE stays inside the write-back: the
					// waiter-ring math above reads live transforms and knockback
					// victims are themselves seekers (demo-fix3), so a later
					// step changes the same pass's ring geometry (A/B'd
					// 2026-10-29 -- see the seam function's comment).
					float ImpulseM = 0.f;
					FVector2D Heading(0.f, 0.f);
					if (FCombatSolver::ApplyChargeKnockback(Charges[i], S.Target, Velocities[i].Value,
						FCombatCVars::ChargeSpeedThreshold(), Entities->GetMutableEntityManager(),
						ImpulseM, Heading))
					{
						if (DebugSub)
						{
							DebugSub->RecordKnockback();
						}
						if (ImpulseM > 0.f)
						{
							UE_LOG(LogMassBattle, Display,
								TEXT("EXEC_RESULT ChargeKnockback Attacker=%u:%u Victim=%u:%u Impulse=%.2fm Heading=(%.2f,%.2f)"),
								S.Entity.Index, S.Entity.SerialNumber, S.Target.Index, S.Target.SerialNumber,
								ImpulseM, Heading.X, Heading.Y);
						}
					}
				}
			});
	}
}
