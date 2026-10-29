// RangedFireProcessor.cpp -- the fire pass (brief task 4; spec §4.4-§4.8).
#include "RangedFireProcessor.h"
#include "BattleCostGridSubsystem.h"
#include "BattleDebugSubsystem.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleProjectilePool.h"
#include "BattleProjectileSubsystem.h"
#include "BattleRanged.h"
#include "BattleRangedFragments.h"
#include "BattleRegistry.h"
#include "BattleSpatialHash.h"
#include "MassBattleModule.h"
#include "UnitActor.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"

URangedFireProcessor::URangedFireProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	// A4: after AssignPairs (pairing owns this tick's intent stamping), before
	// FormationSlotResolve.
	ExecutionOrder.ExecuteAfter.Add(TEXT("AssignPairsProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("FormationSlotResolveProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // five subsystem consumers (ADR-28)
}

void URangedFireProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	RunRangedFirePass(Context.GetWorld(), Context.GetDeltaTimeSeconds());
}

bool URangedFireProcessor::ShouldAllowQueryBasedPruning(const bool bRuntimeMode) const
{
	return false; // no owned queries -- works the registry + the pool (see ProjectileStepProcessor)
}

// -- The pass core --

namespace
{
	// Game-thread-only pass scratch (EC9: allocated once, Reset per pass --
	// the steady volley never grows an allocation; the game thread runs one
	// world tick at a time so a single scratch set is race-free).
	TArray<FVolleyShooterView> GShooters;
	TArray<FVolleyShot> GShots;
	TArray<FVector2D> GBlockers;
	TArray<FBattleHashEntry> GHashEntries;
	TArray<FVector2D> GHashPositions;
	TMap<FUnitHandle, FVector2D> GAnchorsM;
	TMap<FUnitHandle, FUnitHandle> GEnemyOf;

	/** Formation anchor of a unit, meters (its first roster entity's shared
	 *  fragment); false when the unit has no live formation chunk. */
	bool FindUnitAnchorM(FMassEntityManager& EM, const TArray<FMassEntityHandle>& Roster, FVector2D& OutAnchorM)
	{
		if (Roster.Num() == 0)
		{
			return false;
		}
		const FUnitFormationSharedFragment* Formation =
			EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>(Roster[0]);
		if (!Formation)
		{
			return false;
		}
		OutAnchorM = FVector2D(Formation->AnchorPos) * 0.01f;
		return true;
	}

	/** A14 UI mirror: the registry owns the fraction's authority; the unit
	 *  actor's AmmoPool attribute mirrors it for HUD reads. No actor (unit
	 *  tier worlds) -> no-op. */
	void MirrorAmmoPoolToGas(UWorld* InWorld, const FUnitHandle& InHandle, float Fraction)
	{
		for (TActorIterator<AUnitActor> It(InWorld); It; ++It)
		{
			AUnitActor* Actor = *It;
			if (Actor->GetUnitHandle() != InHandle)
			{
				continue;
			}
			if (UAbilitySystemComponent* ASC = Actor->GetAbilitySystemComponent())
			{
				const FGameplayAttribute Attribute = UUnitAttributeSet::FindAttributeByName(TEXT("AmmoPool"));
				if (Attribute.IsValid())
				{
					ASC->SetNumericAttributeBase(Attribute, FMath::Clamp(Fraction, 0.f, 1.f));
				}
			}
			break;
		}
	}
}

void RunRangedFirePass(UWorld* World, float DeltaSeconds)
{
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* Entities = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UBattleCostGridSubsystem* Grids = World ? World->GetSubsystem<UBattleCostGridSubsystem>() : nullptr;
	UBattleProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	UBattleSpatialHashSubsystem* Hash = World ? World->GetSubsystem<UBattleSpatialHashSubsystem>() : nullptr;
	UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	if (!Registry || !Entities || !Grids || !Projectiles)
	{
		return;
	}
	if (!FRangedCVars::Enable() || !FRangedCVars::Fire())
	{
		return; // flags-off identity: no reads beyond the cvars (byte-identical baseline)
	}
	if (Projectiles->GetPool().GetCapacity() == 0)
	{
		Projectiles->ReserveForBattle(); // exec-fixture safety: the battle-start reserve
	}

	const double StartTime = FPlatformTime::Seconds();
	FMassEntityManager& EM = Entities->GetMutableEntityManager();
	const FBattleCostGrid& Grid = Grids->GetGrid();
	FBattleProjectilePool& Pool = Projectiles->GetMutablePool();

	GAnchorsM.Reset();
	GEnemyOf.Reset();

	// Pass-level maps (sorted order -> deterministic first-wins): unit
	// anchors (the skirmish geometry + the volley aim point) and the
	// engagement-record enemy (the A11 target fallback).
	const TArray<FUnitHandle> Handles = Registry->GetSortedHandles();
	for (const FUnitHandle& Handle : Handles)
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		FVector2D AnchorM;
		if (Roster && FindUnitAnchorM(EM, *Roster, AnchorM))
		{
			GAnchorsM.Add(Handle, AnchorM);
		}
	}
	for (const FEngagementKey& Key : Registry->GetSortedEngagementKeys())
	{
		GEnemyOf.FindOrAdd(Key.Attacker, Key.Defender);
		GEnemyOf.FindOrAdd(Key.Defender, Key.Attacker);
	}

	int32 TotalShots = 0;
	int32 TotalHolds = 0;

	for (const FUnitHandle& Handle : Handles)
	{
		const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
		const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
		if (!State || !Spec || State->Roster.Num() == 0)
		{
			continue;
		}

		// -- Skirmish (RC7, A8): evaluated for toggled units whether or not
		//    they can fire (hold-fire is not hold-ground; out-of-ammo units
		//    still skirmish). Threat geometry = team-different unit anchors.
		if (State->bSkirmish && FRangedCVars::Skirmish())
		{
			const FVector2D* MyAnchor = GAnchorsM.Find(Handle);
			float NearestEnemyDistM = TNumericLimits<float>::Max();
			FVector2D EnemyAnchorM = FVector2D::ZeroVector;
			if (MyAnchor)
			{
				for (const TPair<FUnitHandle, FVector2D>& Pair : GAnchorsM)
				{
					const FUnitSpawnSpec* OtherSpec = Registry->FindUnitSpec(Pair.Key);
					if (!OtherSpec || OtherSpec->TeamId == Spec->TeamId)
					{
						continue;
					}
					const float Dist = FVector2D::Distance(*MyAnchor, Pair.Value);
					if (Dist < NearestEnemyDistM)
					{
						NearestEnemyDistM = Dist;
						EnemyAnchorM = Pair.Value;
					}
				}
			}
			if (NearestEnemyDistM < TNumericLimits<float>::Max())
			{
				// bReloadReady gates FIRE, not retreat (RC7): the aggregate.
				const bool bReloadReady = Registry->GetUnitAmmoFraction(Handle) > 0.f;
				const float BreakDist = FRangedCVars::SkirmishBreakDistM();
				const float ResumeDist = BreakDist * FRangedCVars::SkirmishResumeFactor();
				const ESkirmishDecision Decision = EvaluateSkirmish(
					NearestEnemyDistM, bReloadReady, State->bFireAtWill, State->bRetreating, BreakDist, ResumeDist);
				// Retreat DIRECTLY away from the nearest threat, facing held.
				const FVector2D AwayDir = (NearestEnemyDistM > 1e-3f)
					? (*MyAnchor - EnemyAnchorM) / FMath::Max(NearestEnemyDistM, 1e-3f)
					: FVector2D(0.f, 1.f);
				const FVector2D RetreatAnchorM = *MyAnchor + AwayDir * (BreakDist * 2.f);
				float FacingYaw = 0.f;
				if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle))
				{
					if (const FUnitFormationSharedFragment* Formation =
						EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
					{
						FacingYaw = Formation->FacingYaw;
					}
				}
				Registry->ApplyUnitSkirmishDecision(Handle, static_cast<int32>(Decision), RetreatAnchorM * 100.f, FacingYaw);
			}
		}

		if (!(State->RangedInitialAmmo > 0) || State->bOutOfAmmo)
		{
			continue; // non-ranged or spent: no fire (skirmish above still ran)
		}

		// -- Shooter views in roster order (RC1); reloads advance here so a
		//    target-less tick still ticks the weapon clocks. --
		GShooters.Reset();
		float AccuracyMult = 1.f;
		float RangedAttackMult = 1.f;
		int32 NumFiles = 0;
		{
			const TArray<FMassEntityHandle>& Roster = State->Roster;
			if (const FUnitStatsSharedFragment* Stats =
				EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>(Roster[0]))
			{
				AccuracyMult = Stats->AccuracyMult;
				RangedAttackMult = Stats->RangedAttackMult;
			}
			if (const FUnitFormationSharedFragment* Formation =
				EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>(Roster[0]))
			{
				NumFiles = FMath::Max(1, Formation->NumFiles);
			}
			for (const FMassEntityHandle& Entity : Roster)
			{
				// Non-const view: the reload advance writes the fragment here
				// (the weapon clock ticks every pass, target or not).
				FMassEntityView View(EM, Entity);
				FRangedStateFragment* Ranged = View.GetFragmentDataPtr<FRangedStateFragment>();
				const FTransformFragment* Transform = View.GetFragmentDataPtr<FTransformFragment>();
				if (!Ranged || !Transform || Ranged->Kind == EBattleProjectileKind::None)
				{
					continue;
				}
				Ranged->ReloadTimer = FMath::Max(0.f, Ranged->ReloadTimer - DeltaSeconds);
				const FVector P = Transform->GetTransform().GetTranslation();
				FVolleyShooterView Shooter;
				Shooter.Entity = Entity;
				Shooter.Unit = Handle;
				Shooter.PosM = FVector2D(P.X, P.Y) * 0.01f;
				Shooter.EyeZM = 1.6f; // A6 placeholder launch height
				Shooter.Kind = Ranged->Kind;
				Shooter.RangeM = Ranged->RangeM;
				Shooter.Ammo = Ranged->AmmoCount;
				Shooter.ReloadTimer = Ranged->ReloadTimer;
				Shooter.ShotsFired = Ranged->ShotsFired;
				if (const FSlotAssignmentFragment* Slot = View.GetFragmentDataPtr<FSlotAssignmentFragment>())
				{
					Shooter.Rank = Slot->SlotIndex / NumFiles;
				}
				GShooters.Add(Shooter);
			}
		}
		if (GShooters.Num() == 0)
		{
			continue;
		}

		// -- Target (A11): the order's attack unit first, then the record
		//    enemy; no target + FireAtWill means HOLD (no free fire).
		FUnitHandle TargetUnit;
		const FUnitOrder* Order = Registry->FindOrder(Handle);
		if (Order && (Order->Type == FUnitOrder::EType::AttackUnit || Order->Type == FUnitOrder::EType::AttackMove)
			&& Order->TargetUnit.IsValid() && Registry->IsHandleValid(Order->TargetUnit)
			&& Registry->GetRosterSize(Order->TargetUnit) > 0)
		{
			TargetUnit = Order->TargetUnit;
		}
		else if (const FUnitHandle* RecordEnemy = GEnemyOf.Find(Handle))
		{
			TargetUnit = *RecordEnemy;
		}
		const FVector2D* TargetAnchor = GAnchorsM.Find(TargetUnit);
		if (!TargetUnit.IsValid() || !TargetAnchor)
		{
			continue; // nothing to shoot at this tick (reloads advanced above)
		}

		const FProjectileProfile Profile = GetProjectileProfile(GShooters[0].Kind);
		FVolleyTargetView Target;
		Target.PosM = *TargetAnchor;
		Target.ZM = Grid.SampleHeight(*TargetAnchor) + 1.0f; // body-center aim
		Target.Unit = TargetUnit;

		// -- Friendly blockers through the spatial hash (A7's sanctioned
		//    circle queries; same team = friendly). Empty hash (combat
		//    SpatialHash flag off) reads no blockers: the rules need the
		//    combat flags on; the fixtures set them.
		GBlockers.Reset();
		if (Hash)
		{
			const float DistM = FVector2D::Distance(GShooters[0].PosM, Target.PosM);
			const float QueryRadiusM = Profile.bIndirect
				? DistM * FMath::Tan(FMath::DegreesToRadians(FMath::Max(Profile.DispersionDeg, 0.01f))) + Profile.HitRadiusM + 2.f
				: DistM * 0.5f + 2.f * 0.4f + Profile.HitRadiusM + 2.f;
			const FVector2D QueryCenterM = Profile.bIndirect
				? Target.PosM
				: (*TargetAnchor + GShooters[0].PosM) * 0.5f;
			GHashEntries.Reset();
			GHashPositions.Reset();
			Hash->QueryCircle(QueryCenterM, QueryRadiusM, GHashEntries, &GHashPositions);
			for (int32 i = 0; i < GHashEntries.Num(); ++i)
			{
				const FUnitSpawnSpec* EntrySpec = Registry->FindUnitSpec(GHashEntries[i].Unit);
				if (EntrySpec && EntrySpec->TeamId == Spec->TeamId)
				{
					GBlockers.Add(GHashPositions[i]);
				}
			}
		}

		// -- Eligible count for the Holds metric (the seam's cheap half,
		//    recomputed here; LOS/friendly holds are the seam's own). --
		int32 Eligible = 0;
		{
			const int32 MaxRank = Profile.bIndirect ? FMath::Max(1, FRangedCVars::VolleyRanks()) : 1;
			for (const FVolleyShooterView& Shooter : GShooters)
			{
				if (Shooter.ReloadTimer > 0.f || Shooter.Ammo <= 0 || Shooter.Rank >= MaxRank)
				{
					continue;
				}
				const float Dist = FVector2D::Distance(Shooter.PosM, Target.PosM);
				if (Dist <= FRangedCVars::MinRangeM() || Dist > Shooter.RangeM)
				{
					continue;
				}
				++Eligible;
			}
		}

		// -- The seam call (RC1-RC5; mutates views in place). --
		GShots.Reset();
		const int32 Fired = SelectVolleyShots(GShooters, Target, Profile, Grid, GBlockers, AccuracyMult,
			FRangedCVars::FriendlyImpactTolerance(), GShots);
		TotalShots += Fired;
		const int32 Holds = FMath::Max(0, Eligible - Fired);
		TotalHolds += Holds;
		if (Fired == 0)
		{
			continue;
		}

		// -- Writeback (accounting IS the shot, RC5) + pool writes. --
		for (const FVolleyShooterView& Shooter : GShooters)
		{
			FMassEntityView View(EM, Shooter.Entity);
			if (FRangedStateFragment* Ranged = View.GetFragmentDataPtr<FRangedStateFragment>())
			{
				if (Ranged->ShotsFired != Shooter.ShotsFired)
				{
					Ranged->AmmoCount = Shooter.Ammo;
					Ranged->ReloadTimer = Shooter.ReloadTimer;
					Ranged->ShotsFired = Shooter.ShotsFired;
				}
			}
		}
		for (const FVolleyShot& Shot : GShots)
		{
			uint32 Generation = 0;
			const int32 Slot = Pool.Acquire(Generation);
			if (Slot == FBattleProjectilePool::InvalidIndex)
			{
				continue; // failed closed; the pool counted the drop (RC9)
			}
			Pool.PosX[Slot] = Shot.LaunchPosM.X;
			Pool.PosY[Slot] = Shot.LaunchPosM.Y;
			Pool.PosZ[Slot] = Shot.LaunchPosM.Z;
			Pool.VelX[Slot] = Shot.LaunchDir.X * Profile.MuzzleSpeedMps;
			Pool.VelY[Slot] = Shot.LaunchDir.Y * Profile.MuzzleSpeedMps;
			Pool.VelZ[Slot] = Shot.LaunchDir.Z * Profile.MuzzleSpeedMps;
			Pool.GravityScale[Slot] = Profile.GravityScale;
			Pool.TTL[Slot] = Shot.FlightTimeS + 2.f; // safety net past impact
			Pool.Kind[Slot] = static_cast<uint8>(Shot.Kind);
			Pool.GenerationArray[Slot] = Generation;
			Pool.OwnerUnit[Slot] = Shot.Unit;
			Pool.ShooterEntity[Slot] = Shot.Entity;
			// The impact attack term, stamped at fire (spec 4.6):
			// RangedAttack x RangedAttackMult x AccuracyMult.
			float BaseRanged = 0.f;
			if (const FBaseStatsFragment* Base = FMassEntityView(EM, Shot.Entity).GetFragmentDataPtr<FBaseStatsFragment>())
			{
				BaseRanged = Base->RangedAttack;
			}
			Pool.RangedAttack[Slot] = BaseRanged * RangedAttackMult * AccuracyMult;
		}

		// B.2 trajectory rows (z-heighted segments; Task 8 completes the
		// overlay set) -- one per shot, bounded by the volley size.
		if (Debug)
		{
			FBattleOverlayLine Line;
			Line.Shape = EBattleOverlayShape::Segment;
			Line.Color = Profile.bIndirect ? FColor(255, 200, 64) : FColor(128, 200, 255);
			for (const FVolleyShot& Shot : GShots)
			{
				Line.From = Shot.LaunchPosM * 100.f;
				Line.To = (Shot.LaunchPosM + Shot.LaunchDir * FMath::Min(20.f,
					FVector2D::Distance(FVector2D(Shot.LaunchPosM.X, Shot.LaunchPosM.Y), Target.PosM))) * 100.f;
				Debug->EnqueueOverlayLine(Line);
			}
		}

		// The out-of-ammo aggregate (RC5/A14): flip once, mirror the fraction
		// to the UI attribute (authority stays registry-side).
		int32 AmmoSum = 0;
		for (const FVolleyShooterView& Shooter : GShooters)
		{
			AmmoSum += Shooter.Ammo;
		}
		if (AmmoSum == 0)
		{
			Registry->FlipUnitOutOfAmmo(Handle);
		}
		if (Debug)
		{
			MirrorAmmoPoolToGas(World, Handle, static_cast<float>(AmmoSum)
				/ static_cast<float>(FMath::Max(1, State->RangedInitialAmmo * State->InitialRosterSize)));
		}

		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT RangedFire Unit=%u:%u Shots=%d Holds=%d AmmoSum=%d PoolLive=%d"),
			Handle.Index, Handle.Serial, Fired, Holds, AmmoSum, Pool.GetLiveCount());
	}

	const double FireMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	if (Debug)
	{
		Debug->RecordRangedFire(FireMs, TotalShots, TotalHolds);
	}
}
