// MoraleTickProcessor.cpp -- the game-thread morale pass (PR-12; spec
// morale-and-gas sections 4.8, 4.10; brief A2/A7/A11/A13/A14).
#include "MoraleTickProcessor.h"
#include "BattleRegistry.h"
#include "BattleMorale.h"
#include "BattleMoraleEffects.h"
#include "UnitActor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleEngagement.h"
#include "BattleMelee.h"
#include "BattleDebugSubsystem.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

namespace
{
	// A16-class placeholders (disclosed; the T6.1 audit owns the swap):
	/** Height-advantage normalizer (m): (anchor Z differential) / this,
	 *  clamped to [-1, 1] -- spec 4.6's HeightNormM slot. */
	constexpr float HeightNormM = 10.f;
	/** Flee-move target distance from the unit anchor, meters (the order
	 *  anchor mirrors the threat direction this far out). */
	constexpr float FleeDistanceM = 40.f;
	/** The WinningLocally window's loss normalizer (deaths in-window to
	 *  -1): each ~4 recent deaths reads as "losing locally". The kills half
	 *  has no game-thread feed v1 (KillWindow stays 0 -- disclosed). */
	constexpr float WinLossWindowNorm = 4.f;
	/** The DeathWindow's half-life (the decaying recent-loss window, spec
	 *  4.2's "same decay form"). */
	constexpr float DeathWindowHalfLifeS = 8.f;
}

UMoraleTickProcessor::UMoraleTickProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MoraleNominalQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("TallyProcessor"));
	ExecutionOrder.ExecuteBefore.Add(TEXT("RangedFireProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	bRequiresGameThreadExecution = true; // registry + ASCs + formation writes: game thread BY DESIGN (spec section 2)
}

void UMoraleTickProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// The nominal query (see header): the 5.8 scheduler skips zero-query
	// processors, and every other Battle-group pass registers at least one.
	// The fold itself is registry-driven (the F4 discipline -- geometry from
	// records/envelopes, never per-entity scans).
	MoraleNominalQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	MoraleNominalQuery.RegisterWithProcessor(*this);
}

void UMoraleTickProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || !FMoraleCVars::Enable())
	{
		return; // flags-off: byte-identical baseline (no GE, no pass state, hash block 10 folds zeros)
	}

	UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
	const double StartMs = FPlatformTime::ToMilliseconds(FPlatformTime::Cycles());
	ON_SCOPE_EXIT
	{
		if (Debug)
		{
			Debug->RecordMoraleMs(FPlatformTime::ToMilliseconds(FPlatformTime::Cycles()) - StartMs);
		}
	};

	// Cadence accumulator (registry-stored, the CombatClusterCooldown
	// pattern): the per-frame cost is one compare. All work rides the tick.
	// EMPTY-WORLD FIRES STAY PRIMED (task 1.S3, 2026-11-02): a fire with no
	// units sets the cooldown back to 0 instead of consuming a period. The
	// old consume-on-empty made the cadence depend on WHERE the battle reset
	// landed within the frame: ResetBattleState zeroes the accumulator
	// mid-frame, the same frame's pass then fired on the emptied world and
	// re-armed 0.35, and the next fixture inherited a one-tick-shifted
	// phase. Measured: the two-run gate's runs fired the morale passes at
	// 1+21k vs 21k (det_trace_s3f.log), a tick-identical kill at t=693 fell
	// into different pass windows, and the morale cascade (rout vs no-rout)
	// forked the kill streams at t=700 -- the DeterminismTwoRun residue.
	// With the primed form the first fire lands on the first POPULATED tick
	// after any reset, symmetric regardless of pipeline order.
	const float DT = Context.GetDeltaTimeSeconds();
	float Cooldown = Registry->GetMoraleTickCooldown() - DT;
	if (Cooldown > 0.f)
	{
		Registry->SetMoraleTickCooldown(Cooldown);
		return;
	}
	if (Registry->GetUnitCount() == 0)
	{
		Registry->SetMoraleTickCooldown(0.f); // stay primed: fire on the first populated tick
		return;
	}
	const float TickDt = FMath::Max(FMoraleCVars::TickPeriod(), KINDA_SMALL_NUMBER);
	Registry->SetMoraleTickCooldown(TickDt);
	TickMorale(EntityManager, Context, Registry, TickDt);
}

void UMoraleTickProcessor::TickMorale(FMassEntityManager& EM, FMassExecutionContext& Context,
	UUnitRegistrySubsystem* Registry, float TickDt)
{
	UWorld* World = Context.GetWorld();
	UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	if (Debug)
	{
		Debug->RecordMoraleTick(); // the soak variant's FINAL-line counter (PR-15)
	}

	// ---- Unit bodies (the fold's geometry source): anchor/facing from the
	// formation shared fragment, team from the spawn spec, Z from the anchor
	// entity's transform. ----
	TArray<FUnitHandle> Handles = Registry->GetSortedHandles(); // one alloc per tick (small, bounded)
	const int32 UnitCount = Handles.Num();
	TickBodies.Reset(UnitCount);
	BodyIndexScratch.Reset();
	for (int32 i = 0; i < UnitCount; ++i)
	{
		const FUnitHandle Handle = Handles[i];
		const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
		const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
		if (!State || !Spec || State->Roster.Num() == 0)
		{
			continue;
		}
		FUnitBody Body;
		Body.Unit = Handle;
		Body.TeamId = Spec->TeamId;
		Body.LiveCount = State->Roster.Num();
		if (const FUnitFormationSharedFragment* Formation =
			EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>(State->Roster[0]))
		{
			Body.AnchorM = FVector2D(Formation->AnchorPos) * 0.01f;
			Body.YawDeg = Formation->FacingYaw;
		}
		if (const FTransformFragment* Transform =
			EM.GetFragmentDataPtr<FTransformFragment>(State->Roster[0]))
		{
			Body.AnchorZM = Transform->GetTransform().GetTranslation().Z * 0.01f;
		}
		Body.bValid = true;
		BodyIndexScratch.Add(Handle, TickBodies.Num());
		TickBodies.Add(Body);
	}

	// ---- Decay pushes (spec 4.2) + exertion accumulation (4.3) at the tick
	// (both closed-form over TickDt; the per-frame cost is the accumulator). ----
	for (const FUnitBody& Body : TickBodies)
	{
		FUnitRuntimeState* State = Registry->FindMutableUnitState(Body.Unit);
		if (!State)
		{
			continue;
		}
		State->FreshCasualtySpike = EvaluateDecay(State->FreshCasualtySpike, TickDt, FMoraleCVars::SpikeHalfLifeS());
		State->DeathWindow = EvaluateDecay(State->DeathWindow, TickDt, DeathWindowHalfLifeS);
		State->KillWindow = EvaluateDecay(State->KillWindow, TickDt, DeathWindowHalfLifeS);
		const bool bFighting = Registry->UnitHasLiveEngagement(Body.Unit);
		const bool bRunning = State->CurrentOrder.Type == FUnitOrder::EType::Move;
		// Task 4b.3 (B-7): the climate rate scale rides the registry spec
		// (hardy 0.85 / very_hardy 0.7 / else 1 -- the F7 slot wired).
		const FUnitSpawnSpec* BodySpec = Registry->FindUnitSpec(Body.Unit);
		const float RateScale = ClimateFatigueRateScale(BodySpec ? BodySpec->ClimateToken : FString());
		AccumulateExertion(State->ExertionIntegral, TickDt, bRunning, bFighting, RateScale);
	}
	Registry->DecayMoraleTeamShocks(TickDt);

	// ---- Input folds (spec 4.6): records + envelope geometry per unit. ----
	const TArray<FEngagementKey> SortedRecords = Registry->GetSortedEngagementKeys();
	for (const FUnitBody& Body : TickBodies)
	{
		FoldUnitInputs(EM, Registry, Body, SortedRecords);
	}

	// ---- Unit actors (one sweep per tick) + apply-on-first-sight (GE
	// application is actor-side; covers fixtures that spawn without the
	// processor having seen the unit before, and re-applies after resets). ----
	ActorsScratch.Reset();
	for (TActorIterator<AUnitActor> It(World); It; ++It)
	{
		AUnitActor* Actor = *It;
		if (Actor && Actor->GetUnitHandle().IsValid())
		{
			ActorsScratch.Add(Actor->GetUnitHandle(), Actor);
			Actor->EnsureMoraleTickApplied();
		}
	}

	// ---- The deterministic pin (A2/R-M1): force the period executions on
	// the fixed sim cadence. The pin's specs carry Period = NO_PERIOD (the
	// Task 3 verdict), so these are the ONLY executions -- no double-fire. ----
	const bool bPinned = FMoraleCVars::DeterministicTick();
	if (bPinned)
	{
		for (const FUnitBody& Body : TickBodies)
		{
			if (const TWeakObjectPtr<AUnitActor>* Actor = ActorsScratch.Find(Body.Unit))
			{
				if (AUnitActor* Strong = Actor->Get())
				{
					Strong->ForceMoraleTickExecution();
				}
			}
		}
	}

	// ---- Mirror + fatigue band + FSM + rally + auto-charge, sorted-handle
	// order (A13); the flee flag rides the formation fragment for the servo's
	// Flee state stamp. ----
	const float NowSimS = World ? World->GetTimeSeconds() : 0.f;
	float TickAgeMax = 0.f;
	int32 AuraActiveCount = 0;
	int32 TerrorActiveCount = 0;
	for (const FUnitBody& Body : TickBodies)
	{
		FUnitRuntimeState* State = Registry->FindMutableUnitState(Body.Unit);
		if (!State)
		{
			continue;
		}
		const FBattleAttributeSnapshot* Snap = Registry->FindCachedAttributes(Body.Unit);
		if (Snap)
		{
			// The sim mirror (hash/save terms; A14: the GE-written attribute
			// read back through the snapshot path).
			State->LastMoraleValue = Snap->Morale;
			State->LastFatigueValue = Snap->Fatigue;
		}
		if (State->LastMoraleTickSimTime >= 0.f)
		{
			TickAgeMax = FMath::Max(TickAgeMax, NowSimS - State->LastMoraleTickSimTime);
		}
		if (const FMoraleTickInputs* Inputs = Registry->FindMoraleInputs(Body.Unit))
		{
			if (Inputs->GeneralAuraBonus > 0.f)
			{
				++AuraActiveCount;
			}
			if (Inputs->TerrorAuraBonus > 0.f)
			{
				++TerrorActiveCount;
			}
			// DET trace (task 1.S3): the per-unit morale surface -- value +
			// every weighted input, run-tagged, one line per unit per morale
			// pass (the pass cadence is ~21 game ticks, so the volume is
			// bounded). The 2026-11-02 fork: run 1's unit 3 crossed the break
			// at the 4th kill while run 2's did not -- this line names WHICH
			// input diverged first (game thread; reliable file order).
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT DetMor run=%d t=%d U=%u:%u M=%.3f spike=%.3f shock=%.3f ex=%.3f odds=%.2f arc=%d rf=%d aura=%.2f win=%.2f cas=%.3f rout=%d live=%d"),
				BattleDet::RunTag(), BattleDet::Tick(),
				Body.Unit.Index, Body.Unit.Serial,
				State->LastMoraleValue,
				State->FreshCasualtySpike,
				Registry->GetTeamGeneralDeathShock(Body.TeamId),
				State->ExertionIntegral,
				Inputs->LocalOdds,
				FMath::CountBits(static_cast<uint32>(Inputs->ArcExposureMask)),
				Inputs->RoutingFriendsNearby,
				Inputs->GeneralAuraBonus,
				Inputs->WinningLocally,
				Inputs->CasualtyFraction,
				static_cast<int32>(State->RoutState),
				Body.LiveCount);
		}

		// Servo flee flag (the Flee movement state's source; A12: a movement
		// flag, never morale state -- the servo reads the flag, not morale).
		if (State->Roster.Num() > 0)
		{
			if (FUnitFormationSharedFragment* Formation =
				EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>(State->Roster[0]))
			{
				Formation->bFleeMove = State->bRoutFleeing ? 1 : 0;
			}
		}

		AUnitActor* Actor = ActorsScratch.FindRef(Body.Unit).Get();

		// Fatigue catalog band crossing (A8; aggregation event, never timer).
		if (Actor)
		{
			Actor->ApplyFatigueBand(EvaluateFatigueState(State->ExertionIntegral));
		}

		// ---- The rout FSM (spec 4.4, MC5). ----
		const EBattleRoutState Prev = State->RoutState;
		const EBattleRoutState Next = EvaluateRoutTransition(Prev, State->LastMoraleValue,
			FMoraleCVars::WaveringThreshold(), FMoraleCVars::BreakThreshold(), FMoraleCVars::WaveringHysteresis(),
			State->RoutCount, FMoraleCVars::MaxRallies(), State->Discipline);
		if (Next != Prev)
		{
			State->RoutState = Next;
			switch (Next)
			{
			case EBattleRoutState::Wavering:
				if (Prev == EBattleRoutState::Routing)
				{
					// Climbed out while routing (the roll is the normal route
					// back -- this is the rare morale-recovery path). Halt.
					Registry->ApplyUnitRoutDecision(Body.Unit, static_cast<int32>(EBattleRoutDecision::RallyStop),
						FVector2D::ZeroVector, 0.f);
					UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRout Unit=%u:%u State=Wavering Recovery Morale=%.2f"),
						Body.Unit.Index, Body.Unit.Serial, State->LastMoraleValue);
				}
				else
				{
					UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRout Unit=%u:%u State=Wavering Morale=%.2f"),
						Body.Unit.Index, Body.Unit.Serial, State->LastMoraleValue);
				}
				break;
			case EBattleRoutState::Routing:
			{
				State->RoutCount++;
				State->RallyAttempts = 0;
				if (Debug) { Debug->RecordRout(); }
				// The break: mass release both directions (A7) + the flee Move
				// directly away from the nearest threat (facing away).
				Registry->FlagUnitEngagementsDead(Body.Unit);
				FVector2D AwayM(Body.AnchorM);
				float FacingDeg = Body.YawDeg;
				if (State->bThreatValid)
				{
					const FVector2D FromThreatM = Body.AnchorM - FVector2D(State->LastThreatAnchorCm) * 0.01f;
					if (FromThreatM.SizeSquared() > KINDA_SMALL_NUMBER)
					{
						AwayM = Body.AnchorM + FromThreatM.GetSafeNormal() * FleeDistanceM;
						FacingDeg = FMath::RadiansToDegrees(FMath::Atan2(FromThreatM.Y, FromThreatM.X));
					}
				}
				Registry->ApplyUnitRoutDecision(Body.Unit, static_cast<int32>(EBattleRoutDecision::Flee),
					AwayM * 100.f, FacingDeg);
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRout Unit=%u:%u State=Routing Morale=%.2f RoutCount=%d"),
					Body.Unit.Index, Body.Unit.Serial, State->LastMoraleValue, State->RoutCount);
				break;
			}
			case EBattleRoutState::Shattered:
				if (Debug) { Debug->RecordShattered(); }
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRout Unit=%u:%u State=Shattered RoutCount=%d"),
					Body.Unit.Index, Body.Unit.Serial, State->RoutCount);
				break;
			case EBattleRoutState::Steady:
			default:
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRout Unit=%u:%u State=Steady Recovery Morale=%.2f"),
					Body.Unit.Index, Body.Unit.Serial, State->LastMoraleValue);
				break;
			}
		}

		// ---- Rally rolls while Routing (spec 4.5, MC6; the roll draws the
		// canonical UNIT key + the attempt count -- A13). ----
		if (State->RoutState == EBattleRoutState::Routing)
		{
			const FMoraleTickInputs* Inputs = Registry->FindMoraleInputs(Body.Unit);
			const bool bAura = Inputs && Inputs->GeneralAuraBonus > 0.f;
			const float Roll = FMeleeMath::CombatRoll01(Body.Unit.Index, Body.Unit.Serial, State->RallyAttempts);
			++State->RallyAttempts;
			if (EvaluateRally(State->LastMoraleValue, FMoraleCVars::RallyThreshold(), FMoraleCVars::RallyBase(),
				FMoraleCVars::RallyDecay(), State->Discipline, State->RoutCount, bAura, Roll))
			{
				State->RoutState = EBattleRoutState::Wavering;
				State->RallyAttempts = 0;
				Registry->ApplyUnitRoutDecision(Body.Unit, static_cast<int32>(EBattleRoutDecision::RallyStop),
					FVector2D::ZeroVector, 0.f);
				if (Debug) { Debug->RecordRally(); }
				UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MoraleRally Unit=%u:%u Roll=%.3f Morale=%.2f RoutCount=%d"),
					Body.Unit.Index, Body.Unit.Serial, Roll, State->LastMoraleValue, State->RoutCount);
			}
		}

		// ---- Impetuous auto-charge (A11/MC9): suppressed by GuardMode,
		// routing, and player orders (the serial guard). The player-bump
		// clear runs REGARDLESS of the FSM state -- a wavering/routing unit's
		// stale auto-charge bookkeeping must not survive its player order. ----
		{
			const uint32 Serial = Registry->GetOrderSerial(Body.Unit);
			if (State->bAutoCharging && State->AutoChargeSerial != Serial)
			{
				State->bAutoCharging = false; // a player order superseded the charge
			}
		}
		if (State->Discipline == EBattleDiscipline::Impetuous && !State->bGuardMode
			&& State->RoutState == EBattleRoutState::Steady)
		{
			const uint32 Serial = Registry->GetOrderSerial(Body.Unit);
			const FUnitOrder* Order = Registry->FindOrder(Body.Unit);
			const bool bIdle = !Order || Order->Type == FUnitOrder::EType::None || Order->Type == FUnitOrder::EType::Stop;
			const bool bOwnCharge = State->bAutoCharging && State->AutoChargeSerial == Serial;
			if ((bIdle || bOwnCharge) && State->bThreatValid)
			{
				// Nearest different-team envelope inside the trigger radius.
				float BestDistSq = FMath::Square(FMoraleCVars::ImpetuousChargeRadiusM());
				FUnitHandle BestEnemy;
				for (const FUnitBody& Other : TickBodies)
				{
					if (Other.TeamId == Body.TeamId || !Other.bValid)
					{
						continue;
					}
					const float DistSq = FVector2D::DistSquared(Body.AnchorM, Other.AnchorM);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestEnemy = Other.Unit;
					}
				}
				if (BestEnemy.IsValid()
					&& Registry->IssueAttackUnitOrder(Body.Unit, BestEnemy))
				{
					State->bAutoCharging = true;
					State->AutoChargeSerial = Registry->GetOrderSerial(Body.Unit);
					UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT ImpetuousCharge Unit=%u:%u Target=%u:%u Dist=%.1fm"),
						Body.Unit.Index, Body.Unit.Serial, BestEnemy.Index, BestEnemy.Serial,
						FMath::Sqrt(BestDistSq));
				}
			}
		}
	}

	if (Debug)
	{
		Debug->RecordMoraleTickAge(TickAgeMax);
		Debug->SetAuraActiveCount(AuraActiveCount);
		Debug->SetTerrorActiveCount(TerrorActiveCount);
		DrawMoraleOverlay(Context, Registry, TickBodies, Debug);
	}
}

// -- Phase 4a (PR-14, B.2's morale rows): the gray-box overlay -- morale
// bars colored by rout state, the general's aura ring, flee arrows. The
// M9 pattern: the overlay cvar read once per tick; enqueue cost only
// when shown. --
void UMoraleTickProcessor::DrawMoraleOverlay(FMassExecutionContext& Context, UUnitRegistrySubsystem* Registry,
	const TArray<FUnitBody>& Bodies, UBattleDebugSubsystem* Debug)
{
	if (!Debug->IsOverlayShown())
	{
		return;
	}
	for (const FUnitBody& Body : Bodies)
	{
		const FUnitRuntimeState* State = Registry->FindUnitState(Body.Unit);
		if (!State || State->Roster.Num() == 0)
		{
			continue;
		}
		const FVector Anchor(Body.AnchorM.X * 100.f, Body.AnchorM.Y * 100.f, 60.f);

		// The morale bar: length 3 m x the morale fraction of the stack's
		// base span (the placeholder band 0..10); color by rout state.
		static const FColor StateColors[4] = {
			FColor(80, 220, 80),   // Steady: green
			FColor(240, 220, 60),  // Wavering: yellow
			FColor(240, 70, 50),   // Routing: red
			FColor(120, 20, 20)    // Shattered: dark red
		};
		const int32 StateIdx = FMath::Clamp(static_cast<int32>(State->RoutState), 0, 3);
		FBattleOverlayLine Bar;
		Bar.Shape = EBattleOverlayShape::Segment;
		Bar.From = Anchor;
		Bar.To = Anchor + FVector(FMath::Clamp(State->LastMoraleValue * 30.f, 0.f, 300.f), 0.f, 0.f);
		Bar.Color = StateColors[StateIdx];
		Debug->EnqueueOverlayLine(Bar);

		// The flee arrow: toward the flee order's anchor.
		if (State->bRoutFleeing)
		{
			FBattleOverlayLine Arrow;
			Arrow.Shape = EBattleOverlayShape::Segment;
			Arrow.From = Anchor;
			const FUnitOrder* Order = Registry->FindOrder(Body.Unit);
			const FVector2D Target = Order ? Order->AnchorPos : FVector2D(State->LastThreatAnchorCm);
			Arrow.To = FVector(Target.X, Target.Y, 60.f);
			Arrow.Color = FColor(255, 160, 40); // orange: the flee direction
			Debug->EnqueueOverlayLine(Arrow);
		}

		// The aura ring: at the general's unit only (the living anchor).
		if (const FTeamGeneralRecord* General = Registry->FindTeamGeneral(Body.TeamId);
			General && General->Unit == Body.Unit)
		{
			FBattleOverlayLine Ring;
			Ring.Shape = EBattleOverlayShape::Circle;
			Ring.From = Anchor;
			Ring.Radius = FMoraleCVars::GeneralAuraRadiusM() * 100.f;
			Ring.Color = FColor(90, 160, 255); // blue: the aura radius
			Debug->EnqueueOverlayLine(Ring);
		}

		// Task 4b.2: the terror ring (red) at frightener units -- the
		// GeneralAura ring's mirror (the radius is the unit's kind's).
		if (const FUnitSpawnSpec* SpecRow = Registry->FindUnitSpec(Body.Unit);
			SpecRow && (SpecRow->bFrightenFoot || SpecRow->bFrightenMounted))
		{
			FBattleOverlayLine Ring;
			Ring.Shape = EBattleOverlayShape::Circle;
			Ring.From = Anchor;
			Ring.Radius = (SpecRow->bFrightenMounted
				? FMoraleCVars::TerrorAuraMountedRadiusM()
				: FMoraleCVars::TerrorAuraFootRadiusM()) * 100.f;
			Ring.Color = FColor(255, 40, 40); // red: the terror radius
			Debug->EnqueueOverlayLine(Ring);
		}
	}
}

void UMoraleTickProcessor::FoldUnitInputs(FMassEntityManager& EM, UUnitRegistrySubsystem* Registry,
	const FUnitBody& Body, const TArray<FEngagementKey>& SortedRecords)
{
	FUnitRuntimeState* State = Registry->FindMutableUnitState(Body.Unit);
	FMoraleTickInputs* Inputs = Registry->FindMoraleInputsMutable(Body.Unit);
	if (!State || !Inputs)
	{
		return;
	}

	// Fresh block each tick (the seam's ten terms + the threat-anchor side
	// channel). The ONE stack evaluation happens downstream -- inside the
	// exec calc (A14).
	*Inputs = FMoraleTickInputs();

	// BaseMorale: the row's stat_mental[0], spawn-stamped on every entity's
	// base stats (uniform per unit; the anchor entity's copy stands in).
	if (State->Roster.Num() > 0)
	{
		if (const FBaseStatsFragment* Base = EM.GetFragmentDataPtr<FBaseStatsFragment>(State->Roster[0]))
		{
			Inputs->BaseMorale = Base->MoraleBase;
		}
	}
	Inputs->CasualtyFraction = State->InitialRosterSize > 0
		? static_cast<float>(State->InitialRosterSize - Body.LiveCount) / static_cast<float>(State->InitialRosterSize)
		: 0.f;
	Inputs->FreshCasualtySpike = State->FreshCasualtySpike;
	Inputs->GeneralDeathShock = Registry->GetTeamGeneralDeathShock(Body.TeamId);
	Inputs->WinningLocally = FMath::Clamp((State->KillWindow - State->DeathWindow) / WinLossWindowNorm, -1.f, 1.f);

	// Engagement folds: odds differential + inbound-sector mask + engaged
	// enemies' mean height. Records are the cluster pass's geometry (F4) --
	// one pass over the record list per unit, no per-entity scans. The mask
	// bit k = an engaged enemy sits in sector k around THIS unit (0 = dead
	// ahead, clockwise from the facing -- the ContactAngle convention).
	int32 AssignedAgainst = 0;
	int32 AssignedFor = 0;
	float EnemyZSum = 0.f;
	int32 EnemyZCount = 0;
	uint8 Mask = 0;
	for (const FEngagementKey& Key : SortedRecords)
	{
		const FEngagementRecord* Record = Registry->FindEngagement(Key.Attacker, Key.Defender);
		if (!Record || Record->bDead != 0 || Record->AssignedAttackers <= 0)
		{
			continue;
		}
		const bool bDefending = Key.Defender == Body.Unit;
		const bool bAttacking = Key.Attacker == Body.Unit;
		if (!bDefending && !bAttacking)
		{
			continue;
		}
		const int32* OtherIdx = BodyIndexScratch.Find(bDefending ? Key.Attacker : Key.Defender);
		if (!OtherIdx || !TickBodies.IsValidIndex(*OtherIdx))
		{
			continue;
		}
		const FUnitBody& Other = TickBodies[*OtherIdx];
		const FVector2D RelM = Other.AnchorM - Body.AnchorM;
		const float LocalAngleDeg = FMath::RadiansToDegrees(
			FMath::Atan2(RelM.Y, RelM.X) - FMath::DegreesToRadians(Body.YawDeg));
		const int32 Sector = FCombatSolver::SectorIndexFromAngle(
			FCombatSolver::ContactAngleFromDegrees(LocalAngleDeg), FCombatCVars::SectorCount());
		Mask |= static_cast<uint8>(1 << (Sector & 7));
		if (bDefending)
		{
			AssignedAgainst += Record->AssignedAttackers;
		}
		if (bAttacking)
		{
			AssignedFor += Record->AssignedAttackers;
			EnemyZSum += Other.AnchorZM;
			++EnemyZCount;
		}
	}
	Inputs->LocalOdds = (AssignedAgainst + AssignedFor) > 0
		? static_cast<float>(AssignedAgainst - AssignedFor) / static_cast<float>(FMath::Max(1, AssignedAgainst + AssignedFor))
		: 0.f;
	Inputs->ArcExposureMask = Mask;
	Inputs->HeightAdvantage = EnemyZCount > 0
		? FMath::Clamp((Body.AnchorZM - EnemyZSum / EnemyZCount) / HeightNormM, -1.f, 1.f)
		: 0.f;

	// Cascade (MC7): same-team routers/shattered inside the radius. The
	// general aura (MC6/4.6): a living friendly general's anchor, linear
	// falloff to 0 at the radius.
	const float CascadeRadiusSq = FMath::Square(FMoraleCVars::CascadeRadiusM());
	int32 RoutingFriends = 0;
	for (const FUnitBody& Other : TickBodies)
	{
		if (Other.Unit == Body.Unit || Other.TeamId != Body.TeamId || !Other.bValid)
		{
			continue;
		}
		const FUnitRuntimeState* OtherState = Registry->FindUnitState(Other.Unit);
		if (OtherState
			&& (OtherState->RoutState == EBattleRoutState::Routing || OtherState->RoutState == EBattleRoutState::Shattered)
			&& FVector2D::DistSquared(Body.AnchorM, Other.AnchorM) <= CascadeRadiusSq)
		{
			++RoutingFriends;
		}
	}
	Inputs->RoutingFriendsNearby = RoutingFriends;

	float AuraBonus = 0.f;
	if (const FTeamGeneralRecord* General = Registry->FindTeamGeneral(Body.TeamId))
	{
		if (const int32* GenIdx = BodyIndexScratch.Find(General->Unit))
		{
			const FUnitBody& GenBody = TickBodies[*GenIdx];
			const float DistM = FVector2D::Distance(Body.AnchorM, GenBody.AnchorM);
			const float Radius = FMath::Max(FMoraleCVars::GeneralAuraRadiusM(), KINDA_SMALL_NUMBER);
			if (DistM < Radius)
			{
				AuraBonus = FMoraleCVars::GeneralAuraBase() * (1.f - DistM / Radius);
			}
		}
	}
	Inputs->GeneralAuraBonus = AuraBonus;

	// Task 4b.2 (B-5/B-6): the terror fold -- the GeneralAura MIRROR against
	// enemy envelopes. The anchor is the NEAREST different-team frightener
	// unit's envelope; the falloff shape is the aura's
	// (Base * (1 - DistM / Radius)), with the per-token foot/mounted
	// Base/Radius from the cvar block. Bases default 0 = the flags-off
	// identity (the term contributes zero until enabled).
	float TerrorBonus = 0.f;
	float BestTerrorDistSq = BIG_NUMBER;
	const FUnitSpawnSpec* SelfSpec = Registry->FindUnitSpec(Body.Unit);
	for (const FUnitBody& Other : TickBodies)
	{
		if (Other.Unit == Body.Unit || Other.TeamId == Body.TeamId || !Other.bValid)
		{
			continue;
		}
		const FUnitSpawnSpec* OtherSpec = Registry->FindUnitSpec(Other.Unit);
		if (!OtherSpec || (!OtherSpec->bFrightenFoot && !OtherSpec->bFrightenMounted))
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(Body.AnchorM, Other.AnchorM);
		if (DistSq >= BestTerrorDistSq)
		{
			continue;
		}
		// The nearest frightener's KIND decides the shaping (a mounted
		// frightener outranges a foot one at the defaults).
		const float Base = OtherSpec->bFrightenMounted
			? FMoraleCVars::TerrorAuraMountedBase()
			: FMoraleCVars::TerrorAuraFootBase();
		const float Radius = FMath::Max(
			OtherSpec->bFrightenMounted
				? FMoraleCVars::TerrorAuraMountedRadiusM()
				: FMoraleCVars::TerrorAuraFootRadiusM(),
			KINDA_SMALL_NUMBER);
		const float DistM = FMath::Sqrt(DistSq);
		if (Base > 0.f && DistM < Radius)
		{
			BestTerrorDistSq = DistSq;
			TerrorBonus = Base * (1.f - DistM / Radius);
		}
	}
	Inputs->TerrorAuraBonus = TerrorBonus;

	// Threat-anchor side channel (the flee + auto-charge geometry): nearest
	// different-team envelope, any range (the consumers gate on radius).
	float BestDistSq = BIG_NUMBER;
	State->bThreatValid = false;
	for (const FUnitBody& Other : TickBodies)
	{
		if (Other.TeamId == Body.TeamId || !Other.bValid)
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(Body.AnchorM, Other.AnchorM);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			State->LastThreatAnchorCm = Other.AnchorM * 100.f;
			State->bThreatValid = true;
		}
	}
}
