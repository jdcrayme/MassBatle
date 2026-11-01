// DemoGameMode.cpp -- see header.
#include "DemoGameMode.h"
#include "DemoUnitActor.h"
#include "DemoCameraPawn.h"
#include "DemoPlayerController.h"
#include "DemoHUD.h"
#include "DemoUnitVisualSubsystem.h"

#include "BattleRegistry.h"
#include "BattleGarrisonSubsystem.h"
#include "BattleCostGridSubsystem.h"
#include "BattleExecCommands.h"
#include "BattleMovementFragments.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "BattleRanged.h"
#include "BattleArrowVisualComponent.h"
#include "DemoGarrisonProp.h"
#include "Components/SceneComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassEntitySubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

ADemoGameMode::ADemoGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ADemoCameraPawn::StaticClass();
	PlayerControllerClass = ADemoPlayerController::StaticClass();
	HUDClass = ADemoHUD::StaticClass();
}

void ADemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Phase 2 (task 10, owner demo): the combat pipeline is SESSION-ON in the
	// demo -- level-placed units must fight when ordered without console
	// setup (the M3 auto-sample only covers the empty-map case). The PLUGIN
	// defaults stay 0: the T1.8 movement-soak identity argument needs the
	// pipeline out of the CI baseline until the fold-in nightly.
	BattleExec::EnableDemoCombatPipeline();
	// PR-1 (demo-shell spec sections 4/7): ranged + morale join the
	// session-on posture on the DEMO MAP only (the SpawnRout pattern,
	// ECVF_SetByCode; plugin defaults untouched). GATED like the field
	// fixture fallback: the func maps ride this game mode too
	// (L_MassBattle_T0_Functional logs "Game class is DemoGameMode"), and
	// the func baseline is morale-OFF -- the PR-12 probe measured
	// PairLifecycle regressing under morale session-on (reproduced in the
	// PR-1 func run before this gate landed).
	if (GetWorld()->GetMapName().Contains(TEXT("Demo")))
	{
		BattleExec::EnableRangedPipeline();
		BattleExec::EnableMoralePipeline();
		// PR-1 shoring (the owner PIE report 2026-10-31, root-caused by the
		// Demo.AttackTest anchor telemetry): on a LONG march the straggler
		// pace reads the progressive-fall-in tear (the intentional spawn
		// shape, mean slot error ~11.6 m >> DressBreak 1.8) and floors the
		// anchor at the 0.25x walk pace for the WHOLE march -- an 81 m
		// attack approach takes ~4 minutes, the units never reach contact,
		// and the engagement records stay empty (Engagements=0 in the
		// owner's DumpPairs). The phase-2 alpha-guard treaty covers only
		// the tail morph zone. The demo session runs the pace OFF: the
		// anchor marches at walk speed and the fall-in blend owns
		// convergence. Saved + restored in EndPlay (the func maps ride this
		// game mode -- the PairLifecycle travel-leak lesson).
		if (IConsoleVariable* Pace = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.StragglerPace")))
		{
			SavedStragglerPace = Pace->GetInt();
			Pace->Set(0, ECVF_SetByCode);
		}
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoPipelines combat+ranged+morale session-on (PR-1; straggler pace off for the demo session)"));
	}
	else
	{
		UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoPipelines combat session-on (the func-tier baseline; ranged/morale stay default)"));
	}

	// PR-8: the gray-box arrow feed -- one ISM batched from the projectile
	// pool each frame, attached when the ranged flags are on. Flag-gated so a
	// Battle.Ranged.Enable=0 session stays visually identical to pre-ranged.
	UWorld* World = GetWorld();
	if (FRangedCVars::Enable() && World)
	{
		if (AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass()))
		{
			USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("ArrowHostRoot"));
			Root->SetMobility(EComponentMobility::Movable);
			Root->RegisterComponent();
			Host->SetRootComponent(Root);
			ArrowVisuals = NewObject<UBattleArrowVisualComponent>(Host, TEXT("ArrowVisuals"));
			ArrowVisuals->SetupAttachment(Root);
			ArrowVisuals->RegisterComponent();
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoArrows visual feed attached (PR-8 gray-box)"));
		}
	}

	// Scenario authoring is level-driven: every ADemoUnitActor placed in the
	// map defines one unit (position = anchor, yaw = facing, EduRowIndex /
	// SoldierCount / NumRanks / FormationSpacing editable in the details
	// panel, Team for the demo faction split).
	for (TActorIterator<ADemoUnitActor> It(GetWorld()); It; ++It)
	{
		LevelUnits.Add(*It);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode discovered %d level units"), LevelUnits.Num());

	// PR-1: the map-assigned scenario asset is the first authoring channel
	// (data-driven composition). Empty-asset maps on the demo map fall
	// through to the M7 field fixture below.
	if (LevelUnits.Num() == 0)
	{
		if (ScenarioAsset)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode spawning scenario asset %s (%d rows)"),
				*ScenarioAsset->GetName(), ScenarioAsset->Spawns.Num());
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnScenarioAsset);
		}
		// PR-1: the no-level-units fallback on the demo map is the M7 FIELD
		// fixture -- the composed demo-shell battle (the spec demo-shell
		// section 2 order of battle + the section 3 beat sequencer). The
		// M3/M4/M5 console fixtures stay available for CI (Battle.*.Spawn*
		// commands) but the demo map no longer boots through their
		// sequential-reset chain. Gated to the demo map exactly as before:
		// the automation maps have no placed units either, and a stray sample
		// would poison their world-state asserts (measured 2026-10-27: 56
		// extra live entities on L_MassBattle_T0_Functional).
		else if (GetWorld()->GetMapName().Contains(TEXT("Demo")))
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode no level units -- registering the M7 field fixture (the composed demo-shell battle)"));
			GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnFieldFixture);
		}
	}

	// Entity creation must land on the game thread with Mass idle (the soak
	// controller waits out the first ticks for the same reason).
	GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
}

void ADemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	// PR-1: the session-on flags are DEMO-MAP-LIFETIME state. The func tier
	// boots the default (demo) map before traveling to the functional maps,
	// and ECVF_SetByCode values persist across travel in one process -- the
	// PR-1 func run measured PairLifecycle regressing under the leaked
	// morale flag (exactly the PR-12 probe shape; attributed by the stash
	// run). Restore the shipped default on map end so the automation
	// baseline stays flags-off. The combat family leaks by pre-existing
	// design (the phase-2 baseline documents the movement suite green at
	// Enable=1); the ranged family's shipped defaults are already on.
	if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Morale.Enable")))
	{
		C->Set(0, ECVF_SetByCode);
	}
	// The demo-session movement override (see BeginPlay): restore the
	// shipped straggler-pace value so the func tier's march-discipline
	// baselines (S5_MarchRefront, S4_WidthOverride) stay intact.
	if (IConsoleVariable* Pace = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.StragglerPace")))
	{
		Pace->Set(SavedStragglerPace, ECVF_SetByCode);
	}
}

void ADemoGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// PR-1: the beat sequencer (the spec demo-shell section 3 arc; beats 0-3
	// in this carve-out).
	TickSequencer(DeltaSeconds);
	// Phase 2 task 10: adopt registry units the visuals don't know (the M3
	// sample, console SpawnClash) on a 1 s cadence -- cheap at demo scale.
	if (UDemoUnitVisualSubsystem* Visuals = GetWorld() ? GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr)
	{
		if (const int32 Adopted = Visuals->AdoptUnknownRegistryUnits(
			GetWorld()->GetSubsystem<UUnitRegistrySubsystem>()); Adopted > 0)
		{
			UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoGameMode adopted %d registry units into the visuals"), Adopted);
		}
	}
}

const ADemoGameMode::FDemoBeat* ADemoGameMode::GetBeatTable(int32& OutCount)
{
	// The spec demo-shell section 3 table, the full arc (PR-4). Beats 0-3
	// carry ABSOLUTE seconds (the state triggers below fire them early when
	// their metric resolves; the times are the spec's fallbacks). Beats 4-6
	// carry CLASH-RELATIVE offsets (the table's t+55/t+75/t+90); beat 7 is
	// state-only (one side out of steady units) and never fires by time.
	static const FDemoBeat Beats[] = {
		{  0.f, TEXT("Deploy") },
		{  8.f, TEXT("Advance") },
		{ 18.f, TEXT("Volley") },
		{ 30.f, TEXT("Clash") },
		{ 55.f, TEXT("Break") },
		{ 75.f, TEXT("Rally") },
		{ 90.f, TEXT("Garrison") },
		{  0.f, TEXT("Resolution") },
	};
	OutCount = UE_ARRAY_COUNT(Beats);
	return Beats;
}

void ADemoGameMode::TickSequencer(float DeltaSeconds)
{
	if (!bSequencerRunning)
	{
		return;
	}
	SequencerTime += DeltaSeconds;
	int32 Count = 0;
	const FDemoBeat* Beats = GetBeatTable(Count);
	if (!Beats)
	{
		return;
	}
	const int32 Next = CurrentBeat + 1;
	if (Next >= Count)
	{
		return; // the arc ended at the resolution
	}
	// Due time: beats 0-3 absolute; 4-6 clash-relative (t = the beat-3 fire);
	// 7 never by time. The state triggers fire beats 2/3 ahead of their
	// fallbacks; beat 2 keeps ONLY the fallback (no public shots metric
	// exists -- disclosed PR-4).
	bool bDue = false;
	if (Next <= 3)
	{
		bDue = SequencerTime >= Beats[Next].AtSeconds;
		if (!bDue && Next == 3)
		{
			bDue = PlayerEngaged();
		}
	}
	else if (Next <= 6)
	{
		bDue = ClashTime >= 0.f && SequencerTime >= ClashTime + Beats[Next].AtSeconds;
	}
	if (bDue)
	{
		FireBeat(Next);
	}
	// Beat 7's state trigger (the spec: one side has no steady units alive).
	// Polled from the Break beat on -- before that nothing can have routed.
	if (CurrentBeat >= 4 && CurrentBeat < 7)
	{
		CheckResolution();
	}
}

bool ADemoGameMode::PlayerEngaged() const
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return false;
	}
	// The player roles' live handles (P1..P5; C1 rides with the beat-4
	// charge, it is not the clash the label acknowledges).
	static const TCHAR* PlayerRoles[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4"), TEXT("P5") };
	for (const TCHAR* RoleName : PlayerRoles)
	{
		const FUnitHandle* H = FieldUnitByRole.Find(RoleName);
		if (H && H->IsValid() && Registry->UnitHasLiveEngagement(*H))
		{
			return true;
		}
	}
	return false;
}

void ADemoGameMode::FireGeneralKill()
{
	// Beat 4 + 10 s: the officer-death shock (D.13). The general entity is
	// the enemy team record's OfficerEntity (officer slot 0); the kill rides
	// BattleExec::KillEntity (the spec's scripted verb) with the record's
	// roster index resolved here (the exec form takes <UnitIndex> <RosterIndex>).
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!World || !Registry)
	{
		return;
	}
	const FTeamGeneralRecord* General = Registry->FindTeamGeneral(/*enemy team*/ 1);
	if (!General || !General->Unit.IsValid() || !General->OfficerEntity.IsValid())
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoBeatOrders Beat=4 GeneralKill MISS (no enemy general record)"));
		return;
	}
	int32 RosterIndex = INDEX_NONE;
	if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(General->Unit))
	{
		for (int32 i = 0; i < Roster->Num(); ++i)
		{
			if ((*Roster)[i] == General->OfficerEntity)
			{
				RosterIndex = i;
				break;
			}
		}
	}
	if (RosterIndex == INDEX_NONE)
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoBeatOrders Beat=4 GeneralKill MISS (general entity not in roster)"));
		return;
	}
	TArray<FString> Args;
	Args.Add(FString::Printf(TEXT("%u"), General->Unit.Index));
	Args.Add(FString::FromInt(RosterIndex));
	BattleExec::KillEntity(Args, World);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoBeatOrders Beat=4 GeneralKill Unit=%u RosterIndex=%d"), General->Unit.Index, RosterIndex);
}

void ADemoGameMode::FireBeat6Egress()
{
	// The beat-6 half-beat (D.14's second half): the garrisoned unit egresses.
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry || !Beat6GarrisonHandle.IsValid())
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoBeatOrders Beat=6.5 Egress MISS (nothing garrisoned by the beat)"));
		return;
	}
	const bool bIssued = Registry->IssueExitGarrisonOrder(Beat6GarrisonHandle);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoBeatOrders Beat=6.5 Egress Unit=%u Issued=%d"),
		Beat6GarrisonHandle.Index, bIssued ? 1 : 0);
}

void ADemoGameMode::CheckResolution()
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	// "No steady units alive": per team, count the bound roles whose handle
	// is valid, the roster still has soldiers, and the rout state is Steady.
	int32 SteadyByTeam[2] = { 0, 0 };
	for (const TPair<FName, FUnitHandle>& It : FieldUnitByRole)
	{
		if (!It.Value.IsValid())
		{
			continue;
		}
		const FUnitRuntimeState* State = Registry->FindUnitState(It.Value);
		const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(It.Value);
		if (!State || !Spec || Registry->GetRosterSize(It.Value) <= 0)
		{
			continue;
		}
		if (State->RoutState == EBattleRoutState::Steady && Spec->TeamId < 2)
		{
			++SteadyByTeam[Spec->TeamId];
		}
	}
	if (SteadyByTeam[0] > 0 && SteadyByTeam[1] > 0)
	{
		return; // both sides still hold the field
	}
	FString Verdict;
	if (SteadyByTeam[0] == 0 && SteadyByTeam[1] == 0)
	{
		Verdict = TEXT("Mutual collapse -- no steady units remain");
	}
	else if (SteadyByTeam[0] == 0)
	{
		Verdict = TEXT("Defeat -- your line has broken");
	}
	else
	{
		Verdict = TEXT("Victory -- the enemy breaks!");
	}
	VerdictLine = Verdict;
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoVerdict SteadyPlayer=%d SteadyEnemy=%d Verdict=%s"),
		SteadyByTeam[0], SteadyByTeam[1], *Verdict);
	FireBeat(7);
}

void ADemoGameMode::ResolveProbeFire(uint8 TeamId, int32 KillsPerUnit)
{
	// The probe's fire: up to KillsPerUnit soldier kills per surviving unit
	// of the team (roster indices ascend from the soldiers; officers sit
	// after them -- E5's general was RosterIndex=12 of a 14-entity roster).
	// The morale pass owns everything after the deaths land.
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!World || !Registry)
	{
		return;
	}
	int32 Probed = 0;
	int32 Killed = 0;
	for (const TPair<FName, FUnitHandle>& It : FieldUnitByRole)
	{
		const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(It.Value);
		if (!Spec || Spec->TeamId != TeamId || !Registry->IsHandleValid(It.Value))
		{
			continue;
		}
		const int32 Roster = Registry->GetRosterSize(It.Value);
		if (Roster <= 0)
		{
			continue;
		}
		// Keep one entity alive: a fully-dead unit trivially satisfies "no
		// steady units" but the rout cascade (the shape D.11 asserts) needs
		// survivors to flee.
		const int32 KillCount = FMath::Min(KillsPerUnit, FMath::Max(0, Roster - 1));
		for (int32 i = 0; i < KillCount; ++i)
		{
			TArray<FString> Args;
			Args.Add(FString::Printf(TEXT("%u"), It.Value.Index));
			Args.Add(FString::FromInt(i));
			BattleExec::KillEntity(Args, World);
			++Killed;
		}
		++Probed;
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoResolveProbe fired team=%u units=%d kills=%d"), TeamId, Probed, Killed);
}

void ADemoGameMode::FireBeat(int32 BeatIndex)
{
	int32 Count = 0;
	const FDemoBeat* Beats = GetBeatTable(Count);
	if (!Beats || BeatIndex < 0 || BeatIndex >= Count)
	{
		return;
	}
	CurrentBeat = BeatIndex;
	BeatLabel = Beats[BeatIndex].Label;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoBeat %d %s (t=%.1f)"),
		BeatIndex, *BeatLabel.ToString(), SequencerTime);

	// Beat 7: the resolution (the state trigger drove it) -- the verdict line
	// is set; the arc ends (the label + verdict stay displayed).
	if (BeatIndex == 7)
	{
		bSequencerRunning = false;
		return;
	}

	// Beat 3 (Clash): stamp the clash time -- beats 4-6 are scheduled from it
	// (the spec table's t+55/t+75/t+90).
	if (BeatIndex == 3)
	{
		ClashTime = SequencerTime;
		return;
	}

	// Beat 4 (Break): the scripted mounted charge (D.19) -- C1 attack-moves
	// the enemy left rear (through E1's lane; the attack-move engages what it
	// meets en route). Ten seconds later the sequencer kills the enemy
	// general entity (D.13, the officer-death shock). The morale beats' CLOCK
	// is soft (section 7: the T6.1 threshold co-audit owns the tuning); the
	// SHAPE (wavering -> breaking -> routing -> cascade -> rally) is what is
	// asserted.
	if (BeatIndex == 4)
	{
		if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
		{
			const ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetWorld()->GetFirstPlayerController());
			const FUnitHandle* C1 = FieldUnitByRole.Find(TEXT("C1"));
			if (C1 && C1->IsValid() && !(PC && PC->WasPlayerOrdered(*C1)))
			{
				// E1's lane start was (40,15); its REAR is +X (away from the
				// player line). The attack-move rides through the flank.
				const bool bIssued = Registry->IssueAttackMoveOrder(*C1, FVector2D(60.f, 15.f) * 100.f, 0.f);
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoBeatOrders Beat=4 Charge C1=%u Issued=%d"), C1->Index, bIssued ? 1 : 0);
			}
			GetWorldTimerManager().SetTimer(GeneralKillTimer,
				FTimerDelegate::CreateUObject(this, &ADemoGameMode::FireGeneralKill),
				/*10 s after the Break label*/ 10.f, false);
		}
		return;
	}

	// Beat 5 (Rally): P5 (the mounted general, the player aura anchor) rides
	// to beside a routing friendly -- the rally (D.12) is the sim's aura at
	// the new anchor; the demo only repositions it. No routing friendly (yet)
	// falls back to the field center.
	if (BeatIndex == 5)
	{
		if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
		{
			const ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetWorld()->GetFirstPlayerController());
			UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>();
			const FUnitHandle* P5 = FieldUnitByRole.Find(TEXT("P5"));
			FVector2D DestM(0.f, 0.f);
			const FUnitHandle* Router = nullptr;
			for (const TPair<FName, FUnitHandle>& It : FieldUnitByRole)
			{
				const FUnitRuntimeState* State = Registry->FindUnitState(It.Value);
				const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(It.Value);
				if (State && Spec && Spec->TeamId == 0
					&& State->RoutState == EBattleRoutState::Routing && It.Value.IsValid())
				{
					Router = &It.Value;
					break;
				}
			}
			if (Router && Visuals)
			{
				// The rally point: 15 m south of the router's body center.
				FDemoUnitBodyInfo Info;
				if (Visuals->GetUnitBodyInfo(*Router, Info))
				{
					DestM = (Info.BodyCenterCm + FVector2D(0.f, -1500.f)) * 0.01f;
				}
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoBeatOrders Beat=5 Rally Router=%s Dest=(%.0f,%.0f)m"),
				Router ? TEXT("yes") : TEXT("none"), DestM.X, DestM.Y);
			if (P5 && P5->IsValid() && !(PC && PC->WasPlayerOrdered(*P5)))
			{
				Registry->IssueMoveOrder(*P5, DestM * 100.f, 0.f);
			}
		}
		return;
	}

	// Beat 6 (Garrison): the least-touched surviving player unit garrisons
	// the village building (D.14's first half); the half-beat at +45 s orders
	// the egress. "Least-touched" prefers a unit the player never ordered
	// (the WasPlayerOrdered set); "surviving" = valid handle + soldiers left.
	// Infantry only (the registry rejects cavalry garrisons) and never the
	// general.
	if (BeatIndex == 6)
	{
		if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
		{
			const ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetWorld()->GetFirstPlayerController());
			const uint32 VillageId = ADemoPlayerController::FindStructureAtWorld(
				GetWorld(), FVector2D(8.f, 52.f) * 100.f);
			static const TCHAR* GarrisonRoles[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4") };
			FUnitHandle Chosen;
			auto Surviving = [Registry](const FUnitHandle& H)
			{
				return H.IsValid() && Registry->GetRosterSize(H) > 0;
			};
			// First pass: untouched survivors; second: any survivor.
			for (int32 Pass = 0; Pass < 2 && !Chosen.IsValid(); ++Pass)
			{
				for (const TCHAR* RoleName : GarrisonRoles)
				{
					const FUnitHandle* H = FieldUnitByRole.Find(RoleName);
					if (H && Surviving(*H) && (Pass == 1 || !(PC && PC->WasPlayerOrdered(*H))))
					{
						Chosen = *H;
						break;
					}
				}
			}
			if (Chosen.IsValid() && VillageId != 0)
			{
				Beat6GarrisonHandle = Chosen;
				const bool bIssued = Registry->IssueGarrisonOrder(Chosen, VillageId);
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoBeatOrders Beat=6 Garrison Unit=%u Structure=%u Issued=%d"),
					Chosen.Index, VillageId, bIssued ? 1 : 0);
			}
			else
			{
				UE_LOG(LogDemoBattle, Display,
					TEXT("EXEC_RESULT DemoBeatOrders Beat=6 Garrison MISS (no survivor or no structure=%u)"), VillageId);
			}
			// The half-beat: the egress order 45 s later (the march + filing
			// needs the time; the m7_commandbar_boot measured the full
			// settle at ~40 s from a standing start).
			GetWorldTimerManager().SetTimer(Beat6EgressTimer,
				FTimerDelegate::CreateUObject(this, &ADemoGameMode::FireBeat6Egress),
				45.f, false);
		}
		return;
	}

	// Beat 1: the enemy line advances on the player center (the spec section
	// 3 table). Scripted orders ride the same registry surface the player
	// uses and skip player-touched units, so a player order mid-beat simply
	// wins -- the determinism story holds (no sim-side scripting).
	// PR-2 shoring: LANE goals -- the shared (-10,0) goal converged all three
	// columns onto one point and their 12 m frontages touched mid-march (the
	// friendly-contact defect the cluster team-filter now also guards). The
	// wings advance in their lanes; E3 walks up as the second line.
	if (BeatIndex == 1)
	{
		if (UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr)
		{
			const ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetWorld()->GetFirstPlayerController());
			static const TCHAR* AdvanceRoles[] = { TEXT("E1"), TEXT("E2"), TEXT("E3") };
			// Through the player line (the lanes carry the columns INTO
			// contact; stopping short left the advance bloodless).
			static const FVector2D AdvanceGoals[] = { FVector2D(-48.f, 12.f), FVector2D(-48.f, -12.f), FVector2D(20.f, 0.f) };
			int32 Issued = 0;
			int32 Skipped = 0;
			for (int32 i = 0; i < 3; ++i)
			{
				const FUnitHandle* H = FieldUnitByRole.Find(AdvanceRoles[i]);
				if (!H || !H->IsValid() || (PC && PC->WasPlayerOrdered(*H)))
				{
					++Skipped;
					continue;
				}
				if (Registry->IssueAttackMoveOrder(*H, AdvanceGoals[i] * 100.f, 180.f))
				{
					++Issued;
				}
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoBeatOrders Beat=1 Issued=%d Skipped=%d"), Issued, Skipped);
		}
	}
}

const ADemoGameMode::FDemoFieldUnit* ADemoGameMode::GetFieldTable(int32& OutCount)
{
	// The spec demo-shell section 2.1 order of battle, ~384 entities. ONE
	// home: SpawnFieldFixture spawns it; BindSequencerToLevelUnits matches
	// the level-placed actors against it (dictionary + spawn position).
	static const FDemoFieldUnit FieldUnits[] = {
		{ TEXT("Peasant_Levy"),          40, 4, FVector2D(-40.f, -30.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P1") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D(-40.f,   0.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P2") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D(-40.f,  30.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P3") },
		{ TEXT("Feudal_Knights"),        24, 3, FVector2D(-52.f, -52.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("C1") },
		{ TEXT("Peasant_Archers"),       40, 3, FVector2D(-58.f,  15.f),   0.f, 0, 0, false, EDemoTeam::Player, TEXT("P4") },
		{ TEXT("Feudal_Knights"),        12, 2, FVector2D(-70.f,   0.f),   0.f, 0, 2, true,  EDemoTeam::Player, TEXT("P5") },
		{ TEXT("Peasant_Levy"),          40, 4, FVector2D( 40.f,  15.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E1") },
		{ TEXT("Sergeant_Spearmen"),     40, 4, FVector2D( 40.f, -15.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E2") },
		{ TEXT("Sergeant_Spearmen"),     32, 4, FVector2D( 58.f,   0.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E3") },
		{ TEXT("Mercenary_Crossbowmen"), 40, 3, FVector2D( 58.f,  25.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("E4") },
		{ TEXT("Feudal_Knights"),        24, 3, FVector2D( 52.f,  48.f), 180.f, 1, 0, false, EDemoTeam::Enemy,  TEXT("C2") },
		{ TEXT("Feudal_Knights"),        12, 2, FVector2D( 72.f,   0.f), 180.f, 1, 2, true,  EDemoTeam::Enemy,  TEXT("E5") },
	};
	OutCount = UE_ARRAY_COUNT(FieldUnits);
	return FieldUnits;
}

void ADemoGameMode::BindSequencerToLevelUnits()
{
	UWorld* World = GetWorld();
	UDemoUnitVisualSubsystem* Visuals = World ? World->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!World || !Visuals)
	{
		return;
	}
	int32 TableCount = 0;
	const FDemoFieldUnit* Table = GetFieldTable(TableCount);
	int32 Bound = 0;
	// Role = the table row matching the placed actor's dictionary AND spawn
	// position (the map pass authors actors at exactly the table positions;
	// 5 m tolerance absorbs float round-trip). The spawned handle rides the
	// same actor the level path registered.
	for (TActorIterator<ADemoUnitActor> It(World); It; ++It)
	{
		ADemoUnitActor* Actor = *It;
		const FUnitHandle* BoundHandle = nullptr;
		for (const TPair<FUnitHandle, TObjectPtr<ADemoUnitActor>>& Kvp : HandleByActor)
		{
			if (Kvp.Value == Actor) { BoundHandle = &Kvp.Key; break; }
		}
		if (!BoundHandle || !BoundHandle->IsValid())
		{
			continue;
		}
		const FVector Pos = Actor->GetActorLocation();
		for (int32 i = 0; i < TableCount; ++i)
		{
			const FDemoFieldUnit& Row = Table[i];
			const float DX = Pos.X * 0.01f - Row.PosM.X;
			const float DY = Pos.Y * 0.01f - Row.PosM.Y;
			if (FMath::Abs(DX) < 5.f && FMath::Abs(DY) < 5.f
				&& Actor->UnitDictionary == Row.Dictionary)
			{
				FieldUnitByRole.Add(Row.Role, *BoundHandle);
				++Bound;
				break;
			}
		}
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoBind roles=%d of %d (the sequencer rides the level units)"), Bound, TableCount);
	if (Bound == TableCount)
	{
		bSequencerRunning = true;
		SequencerTime = 0.f;
		FireBeat(0);
	}
}

void ADemoGameMode::EnsureRowStatsCache()
{
	if (bRowStatsCacheBuilt)
	{
		return;
	}
	bRowStatsCacheBuilt = true;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!BattleExec::LoadGoldenEduRows(Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoRowStats FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		return;
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		FBattleRowStats Stats;
		FString Error;
		if (FBattleRowStats::FromEdu(Rows[i], Stats, Error))
		{
			RowStatsByRowIdx.Add(i, Stats);
		}
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoRowStats rows=%d (the banner stats cache)"), RowStatsByRowIdx.Num());
}

const FBattleRowStats* ADemoGameMode::FindRowStatsForUnit(FUnitHandle Handle)
{
	EnsureRowStatsCache();
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return nullptr;
	}
	const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
	if (!Spec)
	{
		return nullptr;
	}
	return RowStatsByRowIdx.Find(Spec->EduRowIndex);
}

void ADemoGameMode::AttackTestFire(int32 UnitIdx, int32 TargetIdx)
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	const FUnitHandle Attacker(static_cast<uint32>(UnitIdx), 1);
	const FUnitHandle Target(static_cast<uint32>(TargetIdx), 1);
	const bool bIssued = Registry->IssueAttackUnitOrder(Attacker, Target);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoAttackTest Fire %u->%u issued=%d"), Attacker.Index, Target.Index, bIssued ? 1 : 0);
	if (bIssued)
	{
		for (int32 Poll = 1; Poll <= 15; ++Poll)
		{
			GetWorldTimerManager().SetTimer(PollHandles[Poll - 1],
				FTimerDelegate::CreateUObject(this, &ADemoGameMode::AttackTestPoll, UnitIdx, TargetIdx, Poll),
				2.f * Poll, false);
		}
	}
}

void ADemoGameMode::AttackTestPoll(int32 UnitIdx, int32 TargetIdx, int32 Poll)
{
	UUnitRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !Mass)
	{
		return;
	}
	const FUnitHandle Attacker(static_cast<uint32>(UnitIdx), 1);
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Attacker);
	FVector2D Anchor = FVector2D::ZeroVector;
	FVector2D Blob = FVector2D::ZeroVector;
	float Fatigue = -1.f;
	if (Roster && Roster->Num() > 0)
	{
		if (const FUnitFormationSharedFragment* F =
			Mass->GetMutableEntityManager().GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
		{
			Anchor = F->AnchorPos;
		}
	}
	if (const FBattleAttributeSnapshot* Cached = Registry->FindCachedAttributes(Attacker))
	{
		Fatigue = Cached->Fatigue;
	}
	// The visual blob center (the trimmed entity centroid) + the delta from
	// the sim anchor: the number that sizes the entity-vs-banner tear the
	// owner reported (PR-2 shoring).
	if (UDemoUnitVisualSubsystem* Visuals = GetWorld()->GetSubsystem<UDemoUnitVisualSubsystem>())
	{
		FDemoUnitBodyInfo Info;
		if (Visuals->GetUnitBodyInfo(Attacker, Info))
		{
			Blob = Info.VisualCentroidCm;
		}
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoAttackTest Poll=%d Unit=%u Anchor=(%.1f,%.1f)m Blob=(%.1f,%.1f)m Tear=%.1fm Fatigue=%.3f"),
		Poll, Attacker.Index, Anchor.X * 0.01f, Anchor.Y * 0.01f, Blob.X * 0.01f, Blob.Y * 0.01f,
		FVector2D::Distance(Anchor, Blob) * 0.01f, Fatigue);
}

void ADemoGameMode::SpawnFieldFixture()
{
	UWorld* World = GetWorld();
	if (!World || bFieldSpawned)
	{
		return;
	}
	// Spawn needs Mass idle (the level-unit path waits out the same grace).
	UMassEntitySubsystem* Mass = World->GetSubsystem<UMassEntitySubsystem>();
	if (!World->HasBegunPlay() || Mass == nullptr
		|| Mass->GetMutableEntityManager().IsProcessing())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnFieldFixture);
		return;
	}
	UBattleCostGridSubsystem* CostGrid = World->GetSubsystem<UBattleCostGridSubsystem>();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UDemoUnitVisualSubsystem* Visuals = World->GetSubsystem<UDemoUnitVisualSubsystem>();
	if (!CostGrid || !Registry || !Visuals)
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL no subsystems"));
		return;
	}

	// The session-on posture (the SpawnRout pattern) -- the fixture runs the
	// full stack regardless of the shipping defaults (PR-1).
	BattleExec::EnableDemoCombatPipeline();
	BattleExec::EnableRangedPipeline();
	BattleExec::EnableMoralePipeline();
	CostGrid->ResetToFlat();
	Registry->ResetBattleState();

	// Structures first: their BeginPlay registers the records + stamps the
	// cost grid, so the units below route around them from the first plan.
	// Positions are the spec demo-shell section 2.2 targets (meters).
	struct FFieldStructure
	{
		FVector2D PosM;
		float YawDeg;
		EGarrisonStructureType Type;
	};
	static const FFieldStructure Structures[] = {
		{ FVector2D(8.f, 52.f),    0.f,  EGarrisonStructureType::Building },  // the village objective (beat 6)
		{ FVector2D(52.f, 80.f),   0.f,  EGarrisonStructureType::Building },  // village second slot (north; the building
		                                                                       // fixture is 48x20 m -- the boot probe
		                                                                       // caught the first spacing overlapping)
		{ FVector2D(52.f, -52.f), 90.f, EGarrisonStructureType::Wall },      // covers the enemy right
		{ FVector2D(-26.f, 30.f),  0.f,  EGarrisonStructureType::Trench },   // in front of P3
	};
	int32 StructuresSpawned = 0;
	for (const FFieldStructure& S : Structures)
	{
		const FTransform Xform(FRotator(0.f, S.YawDeg, 0.f),
			FVector(S.PosM.X * 100.f, S.PosM.Y * 100.f, 0.f));
		ADemoGarrisonProp* Prop = World->SpawnActorDeferred<ADemoGarrisonProp>(
			ADemoGarrisonProp::StaticClass(), Xform);
		if (!Prop)
		{
			continue;
		}
		Prop->SetStructureType(S.Type);
		Prop->FinishSpawning(Xform);
		++StructuresSpawned;
	}

	// Units: dictionary-keyed (the PR-11 rule), generals stamped
	// (OfficerCount 2 + bIsGeneral). The spec demo-shell section 2.1 order
	// of battle, ~384 entities -- the ONE-HOME table (shared with the
	// level-unit binding).
	int32 FieldCount = 0;
	const FDemoFieldUnit* FieldUnits = GetFieldTable(FieldCount);

	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!BattleExec::LoadGoldenEduRows(Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		return;
	}

	int32 Spawned = 0;
	for (int32 FieldIdx = 0; FieldIdx < FieldCount; ++FieldIdx)
	{
		const FDemoFieldUnit& U = FieldUnits[FieldIdx];
		const int32 EduRow = BattleExec::FindEduRowIndexByDictionary(Rows, U.Dictionary);
		if (EduRow == INDEX_NONE)
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL unknown EDU dictionary '%s'"),
				U.Dictionary);
			return;
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = EduRow;
		Spec.SoldierCount = U.Count;
		Spec.UnitName = U.Role.ToString(); // the HUD/card surface reads the role
		Spec.SpawnPos = U.PosM * 100.f;
		Spec.SpawnYaw = U.YawDeg;
		Spec.NumRanks = U.NumRanks;
		Spec.TeamId = U.TeamId;
		Spec.OfficerCount = U.OfficerCount;
		Spec.bIsGeneral = U.bIsGeneral;
		FBattleSpawnResult Result;
		FString Error;
		if (!UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[EduRow], Result, Error)
			|| !Result.Unit.IsValid())
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoField FAIL spawn %s: %s"),
				*U.Role.ToString(), *Error);
			return;
		}
		Visuals->RegisterUnit(Result.Unit, U.Team);
		FieldUnitByRole.Add(U.Role, Result.Unit);
		++Spawned;
	}

	bFieldSpawned = true;
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoField OK rows=%d of %d structures=%d (the demo-shell spec section 2 field, morale session-on)"),
		Spawned, FieldCount, StructuresSpawned);

	// The beat sequencer (the spec section 3 table; PR-1 runs beats 0-3).
	bSequencerRunning = true;
	SequencerTime = 0.f;
	FireBeat(0);
}

void ADemoGameMode::SpawnScenarioAsset()
{
	UWorld* World = GetWorld();
	if (!World || !ScenarioAsset || bScenarioSpawned)
	{
		return;
	}
	// Spawn needs Mass idle (the level-unit path waits out the same grace).
	if (!World->HasBegunPlay() || World->GetSubsystem<UMassEntitySubsystem>() == nullptr)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::SpawnScenarioAsset);
		return;
	}

	// The golden EDU is the unit-type catalog; rows match by dictionary name
	// (B-3). Same fixture file the plugin's SpawnClash reads.
	const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
	FString EduText;
	TArray<FEduUnit> Rows;
	FEduParseError Err;
	if (!FFileHelper::LoadFileToString(EduText, *FixturePath)
		|| !FBattleEduParser::Parse(EduText, Rows, Err))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL EDU fixture (%s)"),
			Err.bFailed ? *Err.Message : TEXT("unreadable"));
		bScenarioSpawned = true;
		return;
	}

	TArray<FUnitHandle> Spawned;
	for (const FDemoScenarioSpawn& RowDef : ScenarioAsset->Spawns)
	{
		// ONE home for the dictionary lookup (the plugin's BattleExec helper;
		// this lambda was its third re-typed copy -- audit B-3).
		const int32 EduRow = BattleExec::FindEduRowIndexByDictionary(Rows, RowDef.Dictionary.ToString());
		if (EduRow == INDEX_NONE)
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL unknown EDU dictionary '%s'"),
				*RowDef.Dictionary.ToString());
			return;
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = EduRow;
		Spec.SoldierCount = RowDef.Count;
		Spec.SpawnPos = RowDef.PosM * 100.f;
		Spec.SpawnYaw = RowDef.YawDeg;
		Spec.NumRanks = RowDef.NumRanks;
		Spec.TeamId = RowDef.TeamId;
		FBattleSpawnResult Result;
		FString Error;
		if (!UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[EduRow], Result, Error)
			|| !Result.Unit.IsValid())
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoScenario FAIL spawn '%s': %s"),
				*RowDef.Dictionary.ToString(), *Error);
			return;
		}
		Spawned.Add(Result.Unit);
	}

	switch (ScenarioAsset->AutoOrder)
	{
	case EDemoScenarioAutoOrder::LastAttackMoveOrigin:
		if (UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>())
		{
			Registry->IssueAttackMoveOrder(Spawned.Last(), FVector2D(0.f, 0.f), 0.f);
		}
		break;
	case EDemoScenarioAutoOrder::LastAttacksFirst:
		if (UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>())
		{
			Registry->IssueAttackUnitOrder(Spawned.Last(), Spawned[0]);
		}
		break;
	default:
		break;
	}

	bScenarioSpawned = true;
	FString Ids;
	for (const FUnitHandle& H : Spawned)
	{
		Ids += FString::Printf(TEXT(" %u:%u"), H.Index, H.Serial);
	}
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoScenario OK asset=%s rows=%d units:%s"),
		*ScenarioAsset->GetName(), Spawned.Num(), *Ids);
}

void ADemoGameMode::VerifyGarrisonStructures()
{
	UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
	if (!Garrison || Garrison->GetStructureCount() == 0)
	{
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoClickProbe none (no garrison structures registered)"));
		return;
	}
	// Probe each structure's own footprint center through the SAME static the
	// RMB click path uses (ADemoPlayerController::FindStructureAtWorld) -- a
	// miss here means a click there would fail too.
	int32 Ok = 0;
	int32 Probed = 0;
	for (const TPair<uint32, FGarrisonStructureRecord>& It : Garrison->GetAllStructures())
	{
		const FVector2D CenterM = It.Value.Footprint.GetCenter();
		const uint32 Resolved = ADemoPlayerController::FindStructureAtWorld(GetWorld(), CenterM * 100.f);
		++Probed;
		Ok += Resolved == It.Key ? 1 : 0;
		UE_LOG(LogDemoBattle, Display,
			TEXT("EXEC_RESULT DemoClickProbe Structure=%u Center=(%.1f,%.1f)m Resolved=%u ok=%d"),
			It.Key, CenterM.X, CenterM.Y, Resolved, Resolved == It.Key ? 1 : 0);
	}
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoClickProbe summary probes=%d ok=%d"), Probed, Ok);
	if (Ok != Probed)
	{
		UE_LOG(LogDemoBattle, Error,
			TEXT("EXEC_RESULT DemoClickProbe FAIL %d of %d structures not clickable"), Probed - Ok, Probed);
	}
}

void ADemoGameMode::TrySpawnLevelUnits()
{
	if (bLevelUnitsSpawned)
	{
		return;
	}
	UWorld* World = GetWorld();
	UMassEntitySubsystem* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	UDemoUnitVisualSubsystem* Visuals = World ? World->GetSubsystem<UDemoUnitVisualSubsystem>() : nullptr;
	if (!World || !Mass || !Visuals)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
		return;
	}
	// Wait for begin play + the first Mass ticks (soak-controller grace).
	constexpr int32 GraceTicks = 10;
	if (!World->HasBegunPlay() || SpawnGraceTicks++ < GraceTicks || Mass->GetMutableEntityManager().IsProcessing())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ADemoGameMode::TrySpawnLevelUnits);
		return;
	}

	// Garrison structures are MAP actors (ADemoGarrisonProp placed in
	// L_MassBattle_Demo01); their BeginPlay registered the records + stamped
	// the cost grid at level start. One-time click-path probe, then units.
	VerifyGarrisonStructures();

	int32 Spawned = 0;
	for (const TObjectPtr<ADemoUnitActor>& Unit : LevelUnits)
	{
		if (Unit == nullptr)
		{
			continue;
		}
		FBattleSpawnResult Result;
		FString Error;
		if (Unit->SpawnEntities(Result, Error) && Result.Unit.IsValid())
		{
			Visuals->RegisterUnit(Result.Unit, Unit->Team);
			HandleByActor.Add(Result.Unit, Unit);
			++Spawned;
		}
		else
		{
			UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoSpawn FAIL Actor=%s: %s"), *Unit->GetName(), *Error);
		}
	}
	bLevelUnitsSpawned = true;
	UE_LOG(LogDemoBattle, Display, TEXT("EXEC_RESULT DemoSpawn spawned=%d of %d"), Spawned, LevelUnits.Num());
	// PR-4 early slice: the beat sequencer rides the level units (the map
	// pass authored them at the field-table positions; the binding matches
	// dictionary + position). The demo map now plays beats 0-3 by itself.
	BindSequencerToLevelUnits();
}

// -- Demo.SpawnField (PR-1) ----------------------------------------------------
// The headless/PIE boot command for the M7 field fixture: spawns the
// demo-shell spec section 2 order of battle + the garrison structures and
// starts the beat sequencer. Works on ANY map (the sandbox boot evidence
// path); the demo map runs it from the no-level-units fallback. The m5_boot
// precedent: -game -nullrhi -ExecCmds="Demo.SpawnField" produces the
// EXEC_RESULT evidence lines.

static FAutoConsoleCommandWithWorldAndArgs GDemoSpawnField(
	TEXT("Demo.SpawnField"),
	TEXT("M7 field fixture (PR-1): the demo-shell spec section 2 order of battle + the garrison structures + the beat sequencer; EXEC_RESULT DemoField"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr)
		{
			GM->SpawnFieldFixture();
		}
	}));

// -- Demo.QuitAt (PR-4, PR-5) ----------------------------------------------------
// The boot-harness exit for long evidence windows: the full beat arc runs
// ~200 game-s (clash +55/+75/+90 then the resolution), and the one-command
// -ExecCmds slot is all a boot gets (multi-command chains are unreliable in
// -game processes -- measured twice). PR-5: the optional probe args arm the
// Demo.ResolveProbe attrition (the one-command D.18 arc boot), and the quit
// dumps the FPS chart first -- the D.18 frame-rate/hitch read over the arc.
static FAutoConsoleCommandWithWorldAndArgs GDemoQuitAt(
	TEXT("Demo.QuitAt"),
	TEXT("<seconds> [probeDelay=0] [probeTeam=1] -- dump the FPS chart + quit after the delay; probeDelay>0 arms the Demo.ResolveProbe attrition at it (EXEC_RESULT DemoQuitAt)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			const float Delay = Args.Num() > 0 ? FMath::Max(1.f, FCString::Atof(*Args[0])) : 30.f;
			const float ProbeDelay = Args.Num() > 1 ? FMath::Max(0.f, FCString::Atof(*Args[1])) : 0.f;
			const uint8 ProbeTeam = Args.Num() > 2 ? static_cast<uint8>(FCString::Atoi(*Args[2])) : 1;
			if (World)
			{
				if (ProbeDelay > 0.f)
				{
					if (ADemoGameMode* GM = Cast<ADemoGameMode>(World->GetAuthGameMode()))
					{
						FTimerHandle ProbeHandle;
						GM->GetWorldTimerManager().SetTimer(ProbeHandle,
							FTimerDelegate::CreateUObject(GM, &ADemoGameMode::ResolveProbeFire, ProbeTeam, /*kills per unit*/ 15),
							ProbeDelay, false);
					}
				}
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle,
					FTimerDelegate::CreateLambda([]()
					{
						if (GEngine)
						{
							for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
							{
								if (Ctx.World())
								{
									// The D.18 headless read: the arc TOTALS line
									// (Routs/OfficerDeaths/AuraActive). The pass
									// ms fields read zero here by design -- the
									// metric hooks register on the interactive/
									// soak path only (the m5_boot posture), and
									// t.FPSChart dumps nothing under -nullrhi
									// (RHI-tied; measured twice). The render-rate
									// half of D.18 stays the owner's PIE eyeball.
									BattleExec::DumpMetrics(TArray<FString>(), Ctx.World());
									GEngine->Exec(Ctx.World(), TEXT("Quit"));
									return;
								}
							}
						}
						FPlatformMisc::RequestExit(0);
					}),
					Delay, false);
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoQuitAt armed %.1f probeDelay=%.1f probeTeam=%u"),
				Delay, ProbeDelay, ProbeTeam);
		}));

// -- Demo.ResolveProbe (PR-4) ----------------------------------------------------
// The resolution-trigger evidence channel: with the placeholder morale
// thresholds (the T6.1 co-audit blocker) the full-arc boot stalemates and
// neither side collapses naturally. This probe attrites ONE team through the
// SANCTIONED KillEntity exec surface (up to 15 soldiers per unit) -- the M5
// rout fixture's honest driver: real deaths, real casualty spikes, the
// morale pass's own route transitions, and the beat-7 verdict off the
// genuinely-changed state. (A SetAttr Morale base write was tried first and
// moves nothing -- the exec calc recomputes morale; measured 2026-11-01.)
static FAutoConsoleCommandWithWorldAndArgs GDemoResolveProbe(
	TEXT("Demo.ResolveProbe"),
	TEXT("<delaySec> <teamId> [killsPerUnit=15] -- attrition-kill the team's soldiers via KillEntity at the delay (the beat-7 verdict evidence; EXEC_RESULT DemoResolveProbe)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			const float Delay = Args.Num() > 0 ? FMath::Max(1.f, FCString::Atof(*Args[0])) : 150.f;
			const uint8 TeamId = Args.Num() > 1 ? static_cast<uint8>(FCString::Atoi(*Args[1])) : 1;
			const int32 KillsPerUnit = Args.Num() > 2 ? FMath::Max(1, FCString::Atoi(*Args[2])) : 15;
			ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr;
			if (GM)
			{
				FTimerHandle ProbeHandle;
				GM->GetWorldTimerManager().SetTimer(ProbeHandle,
					FTimerDelegate::CreateUObject(GM, &ADemoGameMode::ResolveProbeFire, TeamId, KillsPerUnit),
					Delay, false);
			}
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoResolveProbe armed delay=%.1f team=%u killsPerUnit=%d"),
				Delay, TeamId, KillsPerUnit);
		}));

// -- Demo.AttackTest (PR-1 shoring) ---------------------------------------------
// The delayed real-attack repro for the owner report 2026-10-31 (RMB attack
// walks to the enemy and never engages): fires IssueAttackUnitOrder after
// [DelaySec], then polls engagement telemetry (head-entity anchor + cached
// fatigue) every 2 s x 15 -- the anchors answer the follow question, the
// fatigue/kill lines answer the engagement question. Usable in PIE (the
// console) and headless (-ExecCmds) alike; handles are 1-based unit indices.
static FAutoConsoleCommandWithWorldAndArgs GDemoAttackTest(
	TEXT("Demo.AttackTest"),
	TEXT("[unitIdx=1] [targetIdx=8] [delaySec=5] -- delayed AttackUnit + engagement telemetry (anchor trajectory + fatigue); EXEC_RESULT DemoAttackTest"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
	{
		if (ADemoGameMode* GM = World ? Cast<ADemoGameMode>(World->GetAuthGameMode()) : nullptr)
		{
			const int32 UnitIdx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
			const int32 TargetIdx = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 8;
			const float Delay = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 5.f;
			FTimerHandle FireHandle;
			GM->GetWorldTimerManager().SetTimer(FireHandle,
				FTimerDelegate::CreateUObject(GM, &ADemoGameMode::AttackTestFire, UnitIdx, TargetIdx),
				FMath::Max(1.f, Delay), false);
			UE_LOG(LogDemoBattle, Display,
				TEXT("EXEC_RESULT DemoAttackTest armed unit=%d target=%d delay=%.1f"), UnitIdx, TargetIdx, Delay);
		}
	}));
