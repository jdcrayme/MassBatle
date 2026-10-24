#include "BattleFuncTestActor.h"
#include "BattleFragments.h"
#include "UnitActor.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassEntityView.h"
#include "MassBattleModule.h"
#include "BattleDeathPath.h"
#include "BattleExecCommands.h"
#include "BattleStateHash.h"
#include "MassSimulationSubsystem.h"
#include "MassSimulationSettings.h"
#include "HAL/IConsoleManager.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

namespace
{
	UUnitRegistrySubsystem* GetRegistry(UWorld* World)
	{
		return World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	}

	FMassEntityManager* GetEntityManager(UWorld* World)
	{
		UMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
		return ES ? &ES->GetMutableEntityManager() : nullptr;
	}

	const FUnitStatsSharedFragment* GetSharedStats(UWorld* World, FUnitHandle Unit)
	{
		UUnitRegistrySubsystem* Registry = GetRegistry(World);
		FMassEntityManager* EM = GetEntityManager(World);
		if (!Registry || !EM)
		{
			return nullptr;
		}
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Unit);
		if (!Roster || Roster->Num() == 0)
		{
			return nullptr;
		}
		return EM->GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*Roster)[0]);
	}
}

AUnitSyncLatencyTest::AUnitSyncLatencyTest()
{
	TimeLimit = 30.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitSyncLatencyTest::BeginTest()
{
	// Manager-equivalent start, minus the manager-only extras (observation
	// point camera, log handling): enable ticking, mark running, let Tick call
	// StartTest once IsReady(). On-machine trace: the native PrepareTest path
	// left the test stopped when driven outside the manager.
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;               // public on AFunctionalTest
	Result = EFunctionalTestResult::Running;
	// bIsReady stays ctor-false; base Tick then calls IsReady() (virtual,
	// overridden true) -> StartTest() on the next tick.
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency BeginTest running=1 tickEnabled=%d"),
	       IsActorTickEnabled() ? 1 : 0);
}

void AUnitSyncLatencyTest::CompleteTest(EFunctionalTestResult TestResult, const FString& Message)
{
	Result = TestResult;
	bIsRunning = false;
	SetActorTickEnabled(false); // stop zombie instances ticking after completion
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency Complete Result=%s Msg=%s"),
	       *LexToString(TestResult), *Message);
}

void AUnitSyncLatencyTest::StartTest()
{
	Super::StartTest();

	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no world"));
		return;
	}

	// Clean any actors from a previous run in the same session.
	DestroyTestUnits();

	// 2 units x 40 entities from the parsed EDU table (exit-criteria shape).
	const int32 Rows[2] = { 0, 1 };
	TObjectPtr<AUnitActor>* Targets[2] = { &UnitA, &UnitB };
	for (int32 i = 0; i < 2; ++i)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		AUnitActor* Actor = World->SpawnActor<AUnitActor>(FVector(1000.f * (i + 1), 0, 100.f), FRotator::ZeroRotator, Params);
		if (!Actor)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("failed to spawn unit actor"));
			return;
		}
		Actor->EduRowIndex = Rows[i];
		Actor->SoldierCount = 40;

		FBattleSpawnResult SpawnResult;
		FString Error;
		if (!Actor->SpawnEntities(SpawnResult, Error))
		{
			CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency Spawned Unit=%u:%u Row=%d Entities=%d"),
		       SpawnResult.Unit.Index, SpawnResult.Unit.Serial, Rows[i], SpawnResult.NumEntities);
		*Targets[i] = Actor;
	}

	// Pipeline probe: is the Mass simulation subsystem present + ticking here?
	if (const UMassSimulationSubsystem* Sim = World->GetSubsystem<UMassSimulationSubsystem>())
	{
		const IConsoleVariable* TickingCvar = IConsoleManager::Get().FindConsoleVariable(TEXT("mass.SimulationTickingEnabled"));
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency MassSim present tickingCvar=%s"),
		       TickingCvar ? (TickingCvar->GetInt() ? TEXT("1") : TEXT("0")) : TEXT("unset"));
	}
	else
	{
		UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT S0_SyncLatency MassSim MISSING in world"));
	}

	// T0.4 drive: set attribute on A only (no GE asset; SetNumericAttributeBase
	// fires the same delegate path). B stays untouched (T0.8 isolation).
	const FGameplayAttribute Attr = UUnitAttributeSet::GetMeleeAttackModAttribute();
	UnitA->GetAbilitySystemComponent()->SetNumericAttributeBase(Attr, 0.5f);

	bAttributeSet = true;
	TicksSinceSet = 0;
}

void AUnitSyncLatencyTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TicksSinceSet < 0)
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency Tick running=%d set=%d"),
		       bIsRunning ? 1 : 0, bAttributeSet ? 1 : 0);
	}

	if (!bAttributeSet || IsRunning() == false)
	{
		return;
	}

	++TicksSinceSet;
	// arch §5.1: one frame of latency is accepted for buffs. The attribute is
	// set during StartTest (this actor's PrePhysics tick, frame N); the Mass
	// PrePhysics phase for frame N already ran, so the earliest UnitSync write
	// lands in frame N+1 -- the tick in frame N+1 must see it (tick-counted
	// assertion, never wall time). On-machine: UnitSync ExecuteCalls=1 lands
	// exactly one frame after the set (trace kept in Execute).
	if (TicksSinceSet < 2)
	{
		return;
	}

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = GetRegistry(World);
	if (!Registry)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no registry"));
		return;
	}

	// T0.4: attribute change visible in FUnitStatsSharedFragment within 1 frame.
	const FUnitStatsSharedFragment* StatsA = GetSharedStats(World, UnitA->GetUnitHandle());
	const FUnitStatsSharedFragment* StatsB = GetSharedStats(World, UnitB->GetUnitHandle());
	if (!StatsA || !StatsB)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("shared fragments missing on roster heads"));
		return;
	}

	bool bOk = true;
	if (!FMath::IsNearlyEqual(StatsA->MeleeAttackMult, 1.5f, 1e-4f))
	{
		AddError(FString::Printf(TEXT("T0.4 FAIL after %d tick(s): unit A MeleeAttackMult=%.4f (expected 1.5)"),
			TicksSinceSet, StatsA->MeleeAttackMult));
		bOk = false;
	}
	// T0.8 second half: a change on unit A is not observable on unit B in the same frame.
	if (!FMath::IsNearlyEqual(StatsB->MeleeAttackMult, 1.0f, 1e-4f))
	{
		AddError(FString::Printf(TEXT("T0.8 FAIL: unit B MeleeAttackMult=%.4f leaked from unit A"), StatsB->MeleeAttackMult));
		bOk = false;
	}
	if (TicksSinceSet > 2)
	{
		AddError(FString::Printf(TEXT("T0.4 latency: sync took %d ticks (> 1 frame after set frame)"), TicksSinceSet));
		bOk = false;
	}
	// UnitSync consumed the dirty flag.
	if (Registry->IsStatsDirty(UnitA->GetUnitHandle()))
	{
		AddError(TEXT("UnitSync did not consume unit A's dirty flag"));
		bOk = false;
	}

	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_SyncLatency Ticks=%d A=%.4f B=%.4f"),
	       TicksSinceSet, StatsA->MeleeAttackMult, StatsB->MeleeAttackMult);

	CompleteTest(bOk ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Error,
		bOk ? TEXT("sync within 1 tick, unit B isolated") : TEXT("sync assertions failed"));

	// Leave the world clean for other tests.
	DestroyTestUnits();
}

void AUnitSyncLatencyTest::DestroyTestUnits()
{
	UWorld* World = GetWorld();
	for (AUnitActor* Actor : TActorRange<AUnitActor>(World))
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	UnitA = nullptr;
	UnitB = nullptr;
	bAttributeSet = false;
	TicksSinceSet = -1;
}

// ---------------------------------------------------------------------------
// S0_DeathPath (T0.5)
// ---------------------------------------------------------------------------

AUnitDeathPathTest::AUnitDeathPathTest()
{
	TimeLimit = 30.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitDeathPathTest::CompleteTest(EFunctionalTestResult TestResult, const FString& Message)
{
	Result = TestResult;
	bIsRunning = false;
	SetActorTickEnabled(false); // stop zombie instances ticking after completion
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Complete Result=%s Msg=%s"),
	       *LexToString(TestResult), *Message);
}

void AUnitDeathPathTest::BeginTest()
{
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath BeginTest running=1"));
}

void AUnitDeathPathTest::StartTest()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no world"));
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Unit = World->SpawnActor<AUnitActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Unit->EduRowIndex = 1; // Sergeant Spearmen
	Unit->SoldierCount = 8;
	FBattleSpawnResult SpawnResult;
	FString Error;
	if (!Unit || !Unit->SpawnEntities(SpawnResult, Error))
	{
		CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
		return;
	}
	RosterCountAtSpawn = SpawnResult.NumEntities;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Spawned Unit=%u:%u Entities=%d"),
	       SpawnResult.Unit.Index, SpawnResult.Unit.Serial, SpawnResult.NumEntities);
}

namespace
{
	/** Standalone dead/live query counts (audit M1: the contracted
	 *  "removed from live queries" assert, run as an actual query). */
	void CountBattleQueries(FMassEntityManager& EM, int32& OutDeadCount, int32& OutLiveBattleCount)
	{
		OutDeadCount = 0;
		OutLiveBattleCount = 0;

		FMassExecutionContext Ctx(EM);
		const TSharedPtr<FMassEntityManager> SharedEM = Ctx.GetSharedEntityManager();

		FMassEntityQuery DeadQuery(SharedEM);
		DeadQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::All);
		DeadQuery.ForEachEntityChunk(Ctx, [&OutDeadCount](FMassExecutionContext& ChunkCtx)
		{
			OutDeadCount += ChunkCtx.GetNumEntities();
		});

		FMassExecutionContext LiveCtx(EM);
		FMassEntityQuery LiveQuery(SharedEM);
		LiveQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
		LiveQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
		LiveQuery.ForEachEntityChunk(LiveCtx, [&OutLiveBattleCount](FMassExecutionContext& ChunkCtx)
		{
			OutLiveBattleCount += ChunkCtx.GetNumEntities();
		});
	}
}

void AUnitDeathPathTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !ES || !Unit)
	{
		return;
	}
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	const FUnitHandle Handle = Unit->GetUnitHandle();
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
	if (!Roster)
	{
		return;
	}

	if (!bKillRequested)
	{
		// Let the world settle 2 full ticks after spawn (tick-counted; the old
		// GetTimeSeconds() < 0.25 wall-clock gate violated the B.0 contract).
		++SettleTicks;
		if (SettleTicks < 2)
		{
			return;
		}
		if (Roster->Num() != RosterCountAtSpawn)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("roster mutated before the kill"));
			return;
		}
		bKillRequested = true;
		DeadEntityHandle = (*Roster)[5];       // capture BEFORE the roster mutates
		SecondDeadEntityHandle = (*Roster)[6];

		// TWO kills in the SAME frame (audit H3 regression): each KillEntity
		// flushes its own tag add. Without the FDeadSignaledTag idempotence
		// marker, the second flush re-signaled the first corpse -- double GAS
		// event + double deferred destroy.
		TArray<FString> Args5;
		Args5.Add(FString::Printf(TEXT("%u"), Handle.Index));
		Args5.Add(TEXT("5"));
		BattleExec::KillEntity(Args5, World);
		TArray<FString> Args6;
		Args6.Add(FString::Printf(TEXT("%u"), Handle.Index));
		Args6.Add(TEXT("6"));
		BattleExec::KillEntity(Args6, World);
		KillTick = 0;

		// Immediate post-flush asserts (audit M1: never vacuously on an
		// already-destroyed entity -- both corpses are alive right here).
		bool bOk = true;
		for (const FMassEntityHandle Dead : {DeadEntityHandle, SecondDeadEntityHandle})
		{
			const FMassEntityView DeadView(EM, Dead);
			if (!DeadView.HasTag<FDeadTag>())
			{
				AddError(TEXT("T0.5 FAIL: dead entity lacks FDeadTag after the audited flush"));
				bOk = false;
			}
			if (!DeadView.HasTag<FDeadSignaledTag>())
			{
				AddError(TEXT("H3 FAIL: signaled corpse lacks the FDeadSignaledTag idempotence marker"));
				bOk = false;
			}
		}

		// Live-query counts (audit M1): exactly the two corpses are out.
		int32 DeadCount = 0;
		int32 LiveCount = 0;
		CountBattleQueries(EM, DeadCount, LiveCount);
		if (DeadCount != 2 || LiveCount != RosterCountAtSpawn - 2)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: live-query counts dead=%d live=%d (want 2/%d)"),
				DeadCount, LiveCount, RosterCountAtSpawn - 2));
			bOk = false;
		}
		if (!bOk)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("post-flush death asserts failed"));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Kill flushed dead=2 live=%d marker=2"), LiveCount);
		return;
	}

	++KillTick;

	if (KillTick >= 1)
	{
		// (b)+(c) roster updated (two removals) + TWO GAS events by now.
		const int32 RosterSize = Registry->GetRosterSize(Handle);
		const int32 GasCount = Unit->GetDeathEventCount();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Tick=%d Roster=%d GAS=%d"),
		       KillTick, RosterSize, GasCount);
		if (RosterSize != RosterCountAtSpawn - 2 || GasCount != 2)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: roster=%d gas=%d (want %d/2 -- exactly one signal per corpse, audit H3)"),
				RosterSize, GasCount, RosterCountAtSpawn - 2));
			CompleteTest(EFunctionalTestResult::Error, TEXT("roster/GAS failed"));
			return;
		}
	}

	if (KillTick >= 2)
	{
		// (d) queued destroys completed (corpse policy is Phase 5).
		const bool bFirstValid = DeadEntityHandle.IsValid() && EM.IsEntityValid(DeadEntityHandle);
		const bool bSecondValid = SecondDeadEntityHandle.IsValid() && EM.IsEntityValid(SecondDeadEntityHandle);
		if (bFirstValid || bSecondValid)
		{
			AddError(TEXT("T0.5 FAIL: deferred destroy did not complete"));
			CompleteTest(EFunctionalTestResult::Error, TEXT("destroy pending"));
			return;
		}

		// (e) audit M5: survivor fragments carry their live roster position.
		int32 IndexErrors = 0;
		const TArray<FMassEntityHandle>* LiveRoster = Registry->FindRoster(Handle);
		if (LiveRoster)
		{
			for (int32 i = 0; i < LiveRoster->Num(); ++i)
			{
				const FMassEntityView View(EM, (*LiveRoster)[i]);
				const FOwnerUnitFragment* OwnerFrag = View.GetFragmentDataPtr<FOwnerUnitFragment>();
				if (!OwnerFrag || OwnerFrag->RosterIndex != i)
				{
					++IndexErrors;
				}
			}
		}
		if (IndexErrors != 0)
		{
			AddError(FString::Printf(TEXT("M5 FAIL: %d survivor fragments carry stale RosterIndex"), IndexErrors));
			CompleteTest(EFunctionalTestResult::Error, TEXT("roster restamp failed"));
			return;
		}

		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Destroyed=2 Restamp=ok at tick %d"), KillTick);
		Unit->Destroy();
		CompleteTest(EFunctionalTestResult::Succeeded, TEXT("death path complete"));
	}

	if (KillTick > 4)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("death path stalled"));
	}
}

// ---------------------------------------------------------------------------
// S0_ExitCriteria (Task 13)
// ---------------------------------------------------------------------------

AUnitExitCriteriaTest::AUnitExitCriteriaTest()
{
	TimeLimit = 45.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitExitCriteriaTest::CompleteTest(EFunctionalTestResult TestResult, const FString& Message)
{
	Result = TestResult;
	bIsRunning = false;
	SetActorTickEnabled(false); // stop zombie instances ticking after completion
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria Complete Result=%s Msg=%s"),
	       *LexToString(TestResult), *Message);
}

void AUnitExitCriteriaTest::BeginTest()
{
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria BeginTest"));
}

void AUnitExitCriteriaTest::StartTest()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;

	const int32 Rows[2] = {0, 1};
	AUnitActor* Targets[2] = {nullptr, nullptr};
	for (int32 i = 0; i < 2; ++i)
	{
		AUnitActor* Actor = World->SpawnActor<AUnitActor>(FVector(1000.f * (i + 1), 0, 100.f), FRotator::ZeroRotator, Params);
		Actor->EduRowIndex = Rows[i];
		Actor->SoldierCount = 40;
		FBattleSpawnResult SpawnResult;
		FString Error;
		if (!Actor || !Actor->SpawnEntities(SpawnResult, Error))
		{
			CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria Spawned Unit=%u:%u Row=%d Entities=%d"),
		       SpawnResult.Unit.Index, SpawnResult.Unit.Serial, Rows[i], SpawnResult.NumEntities);
		Targets[i] = Actor;
	}
	UnitA = Targets[0];
	UnitB = Targets[1];
	Phase = EPhase::Sync;
	TicksInPhase = 0;
}

void AUnitExitCriteriaTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !ES || !UnitA || !UnitB)
	{
		return;
	}
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	++TicksInPhase;

	switch (Phase)
	{
	case EPhase::Sync:
	{
		if (TicksInPhase == 1)
		{
			// T0.4 re-assert: attribute change on A -> shared fragment in <= 1 frame.
			UnitA->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.5f);
			return;
		}
		if (TicksInPhase >= 2)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const FUnitHandle HB = UnitB->GetUnitHandle();
			const TArray<FMassEntityHandle>* RosterA = Registry->FindRoster(HA);
			const TArray<FMassEntityHandle>* RosterB = Registry->FindRoster(HB);
			const FUnitStatsSharedFragment* StatsA = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterA)[0]);
			const FUnitStatsSharedFragment* StatsB = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterB)[0]);
			bool bOk = StatsA && StatsB && FMath::IsNearlyEqual(StatsA->MeleeAttackMult, 1.5f, 1e-4f);
			bOk = bOk && FMath::IsNearlyEqual(StatsB->MeleeAttackMult, 1.0f, 1e-4f); // T0.8 re-assert
			if (!bOk)
			{
				AddError(FString::Printf(TEXT("exit-criteria sync FAIL: A=%.4f B=%.4f"),
					StatsA ? StatsA->MeleeAttackMult : -1.f, StatsB ? StatsB->MeleeAttackMult : -1.f));
				CompleteTest(EFunctionalTestResult::Error, TEXT("sync phase failed"));
				return;
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria SyncTick=%d A=%.4f B=%.4f"),
			       TicksInPhase, StatsA->MeleeAttackMult, StatsB->MeleeAttackMult);
			HashBeforeDeath = FBattleStateHash::Compute(World);
			Phase = EPhase::Death;
			TicksInPhase = 0;
		}
		break;
	}
	case EPhase::Death:
	{
		if (TicksInPhase == 1)
		{
			// T0.5 re-assert: kill one entity of A (capture the handle first).
			const FUnitHandle HA0 = UnitA->GetUnitHandle();
			DeadEntityHandle = (*Registry->FindRoster(HA0))[5];
			TArray<FString> Args;
			Args.Add(FString::Printf(TEXT("%u"), HA0.Index));
			Args.Add(TEXT("5"));
			BattleExec::KillEntity(Args, World);
			// Audit M1: assert the tag IMMEDIATELY after the audited flush --
			// the old deferred check passed vacuously once the corpse was
			// destroyed (`!bDeadValid || HasTag`).
			if (!FMassEntityView(EM, DeadEntityHandle).HasTag<FDeadTag>())
			{
				AddError(TEXT("exit-criteria death FAIL: corpse lacks FDeadTag right after the kill flush"));
				CompleteTest(EFunctionalTestResult::Error, TEXT("death phase failed"));
				return;
			}
			return;
		}
		if (TicksInPhase >= 3)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const bool bRosterOk = (Registry->GetRosterSize(HA) == 39);
			// Destroyed entities have no archetype: a destroyed handle proves the
			// full chain ran (tag -> signal -> roster -> destroy). The tag itself
			// was already asserted immediately post-flush above.
			const bool bDeadValid = DeadEntityHandle.IsValid() && EM.IsEntityValid(DeadEntityHandle);
			const bool bTagged = !bDeadValid || FMassEntityView(EM, DeadEntityHandle).HasTag<FDeadTag>();
			const bool bGas = (UnitA->GetDeathEventCount() == 1);
			if (!bRosterOk || !bTagged || !bGas)
			{
				AddError(FString::Printf(TEXT("exit-criteria death FAIL: roster=%d tagged=%d gas=%d"),
					Registry->GetRosterSize(HA), bTagged ? 1 : 0, bGas ? 1 : 0));
				CompleteTest(EFunctionalTestResult::Error, TEXT("death phase failed"));
				return;
			}
			const uint64 HashAfterDeath = FBattleStateHash::Compute(World);
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria DeathTick=%d Roster=%d Tagged=1 GAS=1 HashBefore=%016llx HashAfter=%016llx"),
			       TicksInPhase, Registry->GetRosterSize(HA), HashBeforeDeath, HashAfterDeath);
			if (HashBeforeDeath == HashAfterDeath)
			{
				AddError(TEXT("state hash unchanged after a real state mutation"));
				CompleteTest(EFunctionalTestResult::Error, TEXT("hash phase failed"));
				return;
			}
			UnitA->Destroy();
			UnitB->Destroy();
			CompleteTest(EFunctionalTestResult::Succeeded, TEXT("exit criteria met"));
		}
		break;
	}
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Factory registrations (see FBattleFuncTestRegistry)
// ---------------------------------------------------------------------------
namespace
{
	AFunctionalTest* MakeSyncLatencyTest(UWorld* World)
	{
		return World->SpawnActor<AUnitSyncLatencyTest>();
	}
	AFunctionalTest* MakeDeathPathTest(UWorld* World)
	{
		return World->SpawnActor<AUnitDeathPathTest>();
	}
	AFunctionalTest* MakeExitCriteriaTest(UWorld* World)
	{
		return World->SpawnActor<AUnitExitCriteriaTest>();
	}

	const FBattleFuncTestRegistry::FRegistrar GRegSync(TEXT("AUnitSyncLatencyTest"), &MakeSyncLatencyTest);
	const FBattleFuncTestRegistry::FRegistrar GRegDeath(TEXT("AUnitDeathPathTest"), &MakeDeathPathTest);
	const FBattleFuncTestRegistry::FRegistrar GRegExit(TEXT("AUnitExitCriteriaTest"), &MakeExitCriteriaTest);
}
