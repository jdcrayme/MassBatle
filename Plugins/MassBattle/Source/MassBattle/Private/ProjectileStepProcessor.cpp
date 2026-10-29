// ProjectileStepProcessor.cpp -- the stepping + impact pass (brief tasks 3+5;
// spec ranged-stack 4.6/4.9). The impact half is the transient one-roll
// context (RC6): swept-segment midpoint circle query (R-R2) -> team-blind
// nearest body -> incoming angle -> the shared sector quantizer -> melee arc
// bands -> HitChance -> the A13 hash roll -> mount-pool drain or the FDeadTag
// defer (F2's death path, identical to melee). The math is the melee seam's;
// this file orchestrates fragments + subsystems (game thread, ADR-28).
#include "ProjectileStepProcessor.h"
#include "BattleBallistics.h"
#include "BattleCombatFragments.h"
#include "BattleCostGridSubsystem.h"
#include "BattleDebugSubsystem.h"
#include "BattleEngagement.h"
#include "BattleFragments.h"
#include "BattleMelee.h"
#include "BattleMovementFragments.h"
#include "BattleProjectileSubsystem.h"
#include "BattleRanged.h"
#include "BattleRangedFragments.h"
#include "BattleRegistry.h"
#include "BattleSpatialHash.h"
#include "MassBattleModule.h"

#include "Engine/World.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"

UProjectileStepProcessor::UProjectileStepProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	// FIRST in the group (A4): before UnitSync, so the pass reads end-of-last-
	// tick transforms in the still-current spatial hash (F11's contract).
	ExecutionOrder.ExecuteBefore.Add(TEXT("UnitSyncProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // pool + subsystems are game-thread (ADR-28)
}

bool UProjectileStepProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false; // no owned queries -- nothing to prune (see header)
}

void UProjectileStepProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	RunProjectileStepPass(Context.GetWorld(), Context.GetDeltaTimeSeconds());
}

// -- The pass core --

namespace
{
	// Game-thread-only pass scratch (EC9: allocated once, Reset per pass).
	TArray<float> GPreX;
	TArray<float> GPreY;
	TArray<float> GPreZ;
	TArray<FBattleHashEntry> GHitEntries;
	TArray<FVector2D> GHitPositions;
	// Bodies killed since their pool batch emptied (see the early-out): the
	// deferred FDeadTag is invisible until flush, and the last-tick hash
	// keeps corpses addressable -- this ledger is the no-double-kill guard.
	TSet<FMassEntityHandle> GKilledLedger;

	// Body geometry for the height gates (the B-1 family: named constants;
	// values mirror the hash entry's 40 cm body radius and the ~1.8 m body).
	constexpr float BodyRadiusM = 0.4f;
	constexpr float BodyTopM = 1.8f;

	/** Point-to-segment distance, 2D (the swept-segment corridor test). */
	float SegmentDist2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq <= 1e-8f)
		{
			return FVector2D::Distance(Point, A);
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LenSq, 0.f, 1.f);
		return FVector2D::Distance(Point, A + AB * T);
	}
}

void RunProjectileStepPass(UWorld* World, float DeltaSeconds)
{
	UBattleProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	if (!Projectiles || !FRangedCVars::Enable() || !FRangedCVars::Step())
	{
		return; // flags-off identity: the pool stays untouched (byte-identical baseline)
	}
	FBattleProjectilePool& Pool = Projectiles->GetMutablePool();
	if (Pool.GetLiveCount() == 0)
	{
		// R-R8: nothing in flight. The killed-ledger clears here (a fully
		// resolved volley cannot double-count; a NEW volley starts clean).
		GKilledLedger.Reset();
		return; // the pass costs two cvar reads
	}

	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UBattleCostGridSubsystem* Grids = World->GetSubsystem<UBattleCostGridSubsystem>();
	UBattleSpatialHashSubsystem* Hash = World->GetSubsystem<UBattleSpatialHashSubsystem>();
	UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
	UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Registry || !Grids || !Entities)
	{
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	FMassEntityManager& EM = Entities->GetMutableEntityManager();
	const FBattleCostGrid& Grid = Grids->GetGrid();
	const int32 SectorCount = FMath::Clamp(FCombatCVars::SectorCount(), 1, 16);

	// 1. Snapshot pre-step positions (the swept segments' start points).
	const int32 Capacity = Pool.GetCapacity();
	if (GPreX.Num() < Capacity)
	{
		GPreX.SetNum(Capacity);
		GPreY.SetNum(Capacity);
		GPreZ.SetNum(Capacity);
	}
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (Pool.IsLive(Slot))
		{
			GPreX[Slot] = Pool.PosX[Slot];
			GPreY[Slot] = Pool.PosY[Slot];
			GPreZ[Slot] = Pool.PosZ[Slot];
		}
	}

	// 2. Integrate + release on ground/TTL (the seam; causes counted).
	const FBallisticsMath::FPoolStepCounts Counts =
		FBallisticsMath::StepPool(Pool, &Grid, DeltaSeconds);
	if (Debug)
	{
		Debug->RecordInFlight(Pool.GetLiveCount()); // T3.6 peak sampling
	}

	// 3. Impacts for the survivors (R-R2: ONE midpoint circle per projectile
	//    per tick; the height gate runs BEFORE the query -- most shots are
	//    above body height mid-flight). The hash still holds LAST tick's
	//    transforms (this pass is FIRST in the group, F11).
	int32 Impacts = 0;
	int32 FriendlyHits = 0;
	for (int32 Slot = 0; Slot < Capacity; ++Slot)
	{
		if (!Pool.IsLive(Slot))
		{
			continue;
		}
		const FVector2D Pre(GPreX[Slot], GPreY[Slot]);
		const FVector2D Post(Pool.PosX[Slot], Pool.PosY[Slot]);
		const float MidZ = (GPreZ[Slot] + Pool.PosZ[Slot]) * 0.5f;
		const FVector2D Mid = (Pre + Post) * 0.5f;
		const float HalfSeg = FVector2D::Distance(Pre, Post) * 0.5f;
		const float GroundMid = Grid.SampleHeight(Mid);
		if (MidZ > GroundMid + BodyTopM + 0.7f)
		{
			continue; // above every body: no query (the R-R2 early-out)
		}

		const FProjectileProfile Profile = GetProjectileProfile(static_cast<EBattleProjectileKind>(Pool.Kind[Slot]));
		const float CorridorM = HalfSeg + Profile.HitRadiusM + BodyRadiusM;
		GHitEntries.Reset();
		GHitPositions.Reset();
		if (Hash)
		{
			Hash->QueryCircle(Mid, CorridorM, GHitEntries, &GHitPositions);
		}

		// Team-blind nearest body along the swept segment (RC4) EXCEPT the
		// shooter's own unit: the rank-limited volley fires OVER its own
		// ranks by design (RC2's arced-over-friends rule; the solved arcs
		// clear them by a body's height), so own-unit bodies are never a
		// backstop. Other friendly units still take the strays -- flow B of
		// Battle.Unit.Ranged.Impacts pins that half.
		int32 Best = INDEX_NONE;
		float BestDistSq = TNumericLimits<float>::Max();
		float GroundV = 0.f;
		const FVector2D SegAB = Post - Pre;
		const float SegLenSq = FMath::Max(SegAB.SizeSquared(), 1e-8f);
		for (int32 i = 0; i < GHitEntries.Num(); ++i)
		{
			if (GHitEntries[i].Entity == Pool.ShooterEntity[Slot]
				|| GHitEntries[i].Unit == Pool.OwnerUnit[Slot])
			{
				continue;
			}
			// Corpses never roll (the melee resolution's own query rule): a
			// body killed THIS pass is still in the last-tick hash until the
			// death receiver destroys it -- and its FDeadTag is DEFERRED, so
			// the killed-this-pass ledger is the only honest visibility.
			if (GKilledLedger.Contains(GHitEntries[i].Entity)
				|| FMassEntityView(EM, GHitEntries[i].Entity).HasTag<FDeadTag>())
			{
				continue;
			}
			const float Dist = SegmentDist2D(GHitPositions[i], Pre, Post);
			if (Dist > Profile.HitRadiusM + BodyRadiusM)
			{
				continue;
			}
			// Height at the closest-approach point (not the midpoint: a body
			// beyond this tick's segment end reads its true flyover height).
			const float T = FMath::Clamp(FVector2D::DotProduct(GHitPositions[i] - Pre, SegAB) / SegLenSq, 0.f, 1.f);
			const float ZAtT = FMath::Lerp(GPreZ[Slot], Pool.PosZ[Slot], T);
			const float GroundI = Grid.SampleHeight(GHitPositions[i]);
			if (ZAtT > GroundI + BodyTopM)
			{
				continue; // passes overhead
			}
			const float DistSq = Dist * Dist;
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = i;
				GroundV = GroundI;
			}
		}
		if (Best == INDEX_NONE)
		{
			continue; // clean miss: flies on (released later by ground/TTL)
		}

		// -- The transient one-roll context (spec 4.6). --
		const FBattleHashEntry& VictimEntry = GHitEntries[Best];
		const FVector2D VictimPosM = GHitPositions[Best];
		FMassEntityView VictimView(EM, VictimEntry.Entity);
		const FBaseStatsFragment* VictimStats = VictimView.GetFragmentDataPtr<FBaseStatsFragment>();
		const FTransformFragment* VictimTransform = VictimView.GetFragmentDataPtr<FTransformFragment>();
		if (!VictimStats || !VictimTransform)
		{
			continue;
		}
		// Incoming angle in the victim's frame: 0 = dead ahead, 0-255 around
		// (the ContactAngle convention; the arc bands are |angle|-symmetric).
		const float VictimYaw = VictimTransform->GetTransform().Rotator().Yaw;
		const FVector2D Incoming = -FVector2D(Pool.VelX[Slot], Pool.VelY[Slot]).GetSafeNormal(); // toward the shooter
		const float AngleDeg = FMath::UnwindDegrees(
			FMath::RadiansToDegrees(FMath::Atan2(Incoming.Y, Incoming.X)) - VictimYaw);
		const uint8 ContactAngle = static_cast<uint8>(static_cast<int32>(
			FMath::RoundToDouble((AngleDeg < 0.f ? AngleDeg + 360.f : AngleDeg) * (255.0 / 360.0))) & 255);
		const int32 Sector = FCombatSolver::SectorIndexFromAngle(ContactAngle, SectorCount);

		const float Defense = VictimStats->MeleeDefense * FMeleeMath::SkillArcMult(Sector, SectorCount)
			+ VictimStats->Shield * FMeleeMath::ShieldArcWindow(Sector, SectorCount)
			+ VictimStats->Armor * FMeleeMath::ArmorMultiplier(Profile.bArmorPiercing);
		const float P = FMeleeMath::HitChance(Pool.RangedAttack[Slot], Defense);
		// Canonical roll identity (A13/RC8): the OWNER unit's handle + the
		// seam's folded count -- never the shooter's raw entity handle.
		const float Roll = FMeleeMath::CombatRoll01(
			Pool.OwnerUnit[Slot].Index, Pool.OwnerUnit[Slot].Serial, Pool.ShooterRollCount[Slot]);
		const bool bLanded = Roll < P * FMath::Max(0.f, Pool.Lethality[Slot]);
		++Impacts;
		if (Debug)
		{
			Debug->RecordCombatRoll(bLanded);
			Debug->RecordRangedImpact();
		}
		UE_LOG(LogMassBattle, Verbose,
			TEXT("RangedImpact Projectile=%d Victim=%u:%u A=%.1f D=%.1f P=%.2f Roll=%.3f Landed=%d Sector=%d"),
			Slot, VictimEntry.Entity.Index, VictimEntry.Entity.SerialNumber,
			Pool.RangedAttack[Slot], Defense, P, Roll, bLanded ? 1 : 0, Sector);

		// Team-blind hits still COUNT as friendly (the RC4 impact-point rule's
		// observability half).
		const FUnitSpawnSpec* VictimSpec = Registry->FindUnitSpec(VictimEntry.Unit);
		const FUnitSpawnSpec* ShooterSpec = Registry->FindUnitSpec(Pool.OwnerUnit[Slot]);
		const bool bFriendly = VictimSpec && ShooterSpec && VictimSpec->TeamId == ShooterSpec->TeamId;
		if (bFriendly)
		{
			++FriendlyHits;
			if (Debug)
			{
				Debug->RecordRangedFriendlyHit();
			}
		}
		// B.2 impact marker (Task 8): every hit lands a ring colored by the
		// VICTIM's team (team 0 yellow, team 1 cyan; a friendly hit is ALSO
		// flagged red-ring -- the team-blind impact-point rule's visual).
		if (Debug)
		{
			FBattleOverlayLine Marker;
			Marker.Shape = EBattleOverlayShape::Circle;
			Marker.From = FVector(VictimPosM.X, VictimPosM.Y, GroundV + 1.f) * 100.f;
			Marker.Radius = Profile.HitRadiusM * 100.f;
			Marker.Color = bFriendly ? FColor(255, 64, 64)
				: (VictimSpec && VictimSpec->TeamId == 0 ? FColor(255, 255, 0) : FColor(0, 255, 255));
			Debug->EnqueueOverlayLine(Marker);
		}

		if (bLanded)
		{
			// Composite HP (F2): the mount pool absorbs first; a landed hit at
			// 0 kills through the EXISTING death path -- FDeadTag via command
			// buffer -> observer -> signal -> HandleUnitDeath.
			FHealthFragment* Health = VictimView.GetFragmentDataPtr<FHealthFragment>();
			if (Health && Health->MountCurHP > 0)
			{
				--Health->MountCurHP;
			}
			else
			{
				EM.Defer().AddTag<FDeadTag>(VictimEntry.Entity);
				GKilledLedger.Add(VictimEntry.Entity);
				if (Debug)
				{
					Debug->RecordDeathByArc(Sector, SectorCount);
				}
				UE_LOG(LogMassBattle, Display,
					TEXT("EXEC_RESULT RangedKill Shooter=%u:%u Unit=%u:%u Victim=%u:%u Sector=%d Friendly=%d"),
					Pool.ShooterEntity[Slot].Index, Pool.ShooterEntity[Slot].SerialNumber,
					VictimEntry.Unit.Index, VictimEntry.Unit.Serial,
					VictimEntry.Entity.Index, VictimEntry.Entity.SerialNumber, Sector, bFriendly ? 1 : 0);
			}
		}

		Pool.Release(Slot); // the projectile is consumed either way
	}

	const double StepMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	if (Debug)
	{
		Debug->RecordProjectileStep(StepMs, Counts.GroundHits, Counts.TtlExpired);
	}
	UE_LOG(LogMassBattle, Verbose,
		TEXT("ProjectileStep InFlight=%d GroundHits=%d TtlExpired=%d Impacts=%d FriendlyHits=%d StepMs=%.3f"),
		Pool.GetLiveCount(), Counts.GroundHits, Counts.TtlExpired, Impacts, FriendlyHits, StepMs);
}
