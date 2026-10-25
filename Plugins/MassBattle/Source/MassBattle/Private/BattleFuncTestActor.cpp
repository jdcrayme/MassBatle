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
#include "BattleMovementFragments.h"
#include "BattleTypes.h"
#include "BattleDebugSubsystem.h"
#include "BattleTestSettings.h"
#include "MassCommonFragments.h"
#include "BattleStateHash.h"
#include "MassSimulationSubsystem.h"
#include "MassSimulationSettings.h"
#include "HAL/IConsoleManager.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

namespace
{
	UUnitRegistrySubsystem* GetFuncTestRegistry(UWorld* World)
	{
		return World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	}

	FMassEntityManager* GetFuncTestEntityManager(UWorld* World)
	{
		UMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
		return ES ? &ES->GetMutableEntityManager() : nullptr;
	}

	const FUnitStatsSharedFragment* GetFuncTestSharedStats(UWorld* World, FUnitHandle Unit)
	{
		UUnitRegistrySubsystem* Registry = GetFuncTestRegistry(World);
		FMassEntityManager* EM = GetFuncTestEntityManager(World);
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
	UUnitRegistrySubsystem* Registry = GetFuncTestRegistry(World);
	if (!Registry)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no registry"));
		return;
	}

	// T0.4: attribute change visible in FUnitStatsSharedFragment within 1 frame.
	const FUnitStatsSharedFragment* StatsA = GetFuncTestSharedStats(World, UnitA->GetUnitHandle());
	const FUnitStatsSharedFragment* StatsB = GetFuncTestSharedStats(World, UnitB->GetUnitHandle());
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
// S1_MarchWheel (T1.5)
// ---------------------------------------------------------------------------
namespace
{
	// Helpers shared by the movement scenarios.
	void GatherEntityPositions(UWorld* World, const FUnitHandle& Handle, TArray<FVector2D>& OutPosM)
	{
		OutPosM.Reset();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
		if (!Registry || !ES)
		{
			return;
		}
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster)
		{
			return;
		}
		FMassEntityManager& EM = ES->GetMutableEntityManager();
		for (const FMassEntityHandle& E : *Roster)
		{
			const FTransformFragment* T = FMassEntityView(EM, E).GetFragmentDataPtr<FTransformFragment>();
			if (T)
			{
				const FVector P = T->GetTransform().GetTranslation();
				OutPosM.Add(FVector2D(P.X, P.Y) * 0.01f);
			}
		}
	}

	FUnitFormationSharedFragment* FindFormation(UWorld* World, const FUnitHandle& Handle)
	{
		UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		if (!ES || !Registry)
		{
			return nullptr;
		}
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
		if (!Roster || Roster->Num() == 0)
		{
			return nullptr;
		}
		return ES->GetMutableEntityManager().GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]);
	}
}

AUnitMarchWheelTest::AUnitMarchWheelTest()
{
	TimeLimit = 300.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitMarchWheelTest::BeginTest()
{
	// Manager-equivalent start (phase-0 pattern): enable ticking, mark
	// running; the base Tick calls StartTest once IsReady().
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
}

void AUnitMarchWheelTest::CompleteTest(EFunctionalTestResult TestResult, const FString& Message)
{
	// Manager-less completion (phase-0 pattern): record directly; FinishTest
	// would error against the absent FunctionalTestingManager.
	Result = TestResult;
	bIsRunning = false;
	SetActorTickEnabled(false);
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Complete Result=%s Msg=%s"),
	       *LexToString(TestResult), *Message);
}

void AUnitMarchWheelTest::StartTest()
{
	Super::StartTest();

	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no world"));
		return;
	}

	// Test-mode contract (fact 1.5): applied + asserted at scenario start.
	UBattleTestSettings::ApplyTestMode();
	TArray<FString> Violations;
	if (!UBattleTestSettings::AssertTestMode(nullptr, &Violations, World))
	{
		CompleteTest(EFunctionalTestResult::Error,
			FString::Printf(TEXT("test-mode contract violated: %s"), *FString::Join(Violations, TEXT("; "))));
		return;
	}
	if (UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>())
	{
		Debug->ResetDiagnostics();
	}

	// 8 units, two ranks of four, 12 m apart, facing north (+Y at yaw 90).
	const int32 Rows[2] = { 0, 1 };
	Units.Reset();
	for (int32 i = 0; i < 8; ++i)
	{
		const int32 Col = i % 4;
		const int32 Row = i / 4;
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		AUnitActor* Actor = World->SpawnActor<AUnitActor>(
			FVector(-4500.f + Col * 3000.f, Row * 3000.f, 100.f), FRotator(0.f, 90.f, 0.f), Params);
		if (!Actor)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("failed to spawn unit actor"));
			return;
		}
		Actor->EduRowIndex = Rows[i % 2];
		Actor->SoldierCount = 40;
		FBattleSpawnResult SpawnResult;
		FString Error;
		if (!Actor->SpawnEntities(SpawnResult, Error))
		{
			CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
			return;
		}
		Units.Add(Actor);
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Spawned Units=8 Entities=320"));
	for (int32 i = 0; i < Units.Num(); ++i)
	{
		if (const FUnitFormationSharedFragment* F = FindFormation(World, Units[i]->GetUnitHandle()))
		{
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT S1SpawnCheck U%d handle=%u:%u anchor=(%.1f,%.1f)m files=%d slots=%d rankSp=%.1f"),
				i, Units[i]->GetUnitHandle().Index, Units[i]->GetUnitHandle().Serial,
				F->AnchorPos.X * 0.01f, F->AnchorPos.Y * 0.01f, F->NumFiles, F->NumSlots, F->RankSpacing * 0.01f);
		}
	}
	Phase = EPhase::March;
	TicksInPhase = 0;
}

void AUnitMarchWheelTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsRunning() || Phase == EPhase::Done)
	{
		return;
	}
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	UBattleDebugSubsystem* Debug = World ? World->GetSubsystem<UBattleDebugSubsystem>() : nullptr;
	if (!Registry || !Debug)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("subsystems missing"));
		return;
	}

	++TicksInPhase;
	const int32 PhaseCap = 2000; // ~66 s at 30 Hz -- generous vs the ~22 s march

	auto AllAnchorsArrived = [this, &Registry]() -> bool
	{
		for (const TObjectPtr<AUnitActor>& Actor : Units)
		{
			const FUnitFormationSharedFragment* Formation = FindFormation(GetWorld(), Actor->GetUnitHandle());
			const FUnitOrder* Order = Registry->FindOrder(Actor->GetUnitHandle());
			if (!Formation || !Order ||
				FVector2D::DistSquared(Formation->AnchorPos, Order->AnchorPos) > FMath::Square(150.f))
			{
				return false;
			}
		}
		return true;
	};

	if (Phase == EPhase::March)
	{
		if (TicksInPhase == 1)
		{
			for (const TObjectPtr<AUnitActor>& Actor : Units)
			{
				const FUnitFormationSharedFragment* Formation = FindFormation(GetWorld(), Actor->GetUnitHandle());
				if (!Formation)
				{
					CompleteTest(EFunctionalTestResult::Error, TEXT("formation fragment missing at march"));
					return;
				}
				// March 30 m north; final facing north (+Y, yaw 90).
				Registry->IssueMoveOrder(Actor->GetUnitHandle(),
					Formation->AnchorPos + FVector2D(0.f, 3000.f), 90.f);
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Phase=March orders issued"));
			return;
		}

		if (AllAnchorsArrived())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Phase=March done ticks=%d"), TicksInPhase);
			Phase = EPhase::Wheel;
			TicksInPhase = 0;
		}
		else if (TicksInPhase > PhaseCap)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("march phase timed out"));
			return;
		}
	}
	else if (Phase == EPhase::Wheel)
	{
		if (TicksInPhase == 1)
		{
			// 90-degree wheel: anchor moves 24 m east, final facing east (+X).
			for (const TObjectPtr<AUnitActor>& Actor : Units)
			{
				const FUnitFormationSharedFragment* Formation = FindFormation(GetWorld(), Actor->GetUnitHandle());
				if (!Formation)
				{
					continue;
				}
				Registry->IssueMoveOrder(Actor->GetUnitHandle(),
					Formation->AnchorPos + FVector2D(2400.f, 0.f), 0.f);
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Phase=Wheel orders issued"));
			return;
		}

		if (AllAnchorsArrived())
		{
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S1_MarchWheel Phase=Wheel done ticks=%d"), TicksInPhase);
			Phase = EPhase::Assert;
			TicksInPhase = 0;
		}
		else if (TicksInPhase > PhaseCap)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("wheel phase timed out"));
			return;
		}
	}
	else if (Phase == EPhase::Assert)
	{
		// Bounded settle: entities re-dress after the anchor arrives; wait
		// until the wake recovers (min separation >= threshold) or the budget
		// expires -- the assert judges the FORMED state, reached within T.
		TArray<TArray<FVector2D>> AllPos;
		AllPos.SetNum(Units.Num());
		auto ComputeMinSeparation = [this, &AllPos, World](float& OutMin, int32& OutNearOrigin) -> void
		{
			OutMin = FLT_MAX;
			OutNearOrigin = 0;
			for (int32 U = 0; U < Units.Num(); ++U)
			{
				GatherEntityPositions(World, Units[U]->GetUnitHandle(), AllPos[U]);
			}
			for (int32 U = 0; U < AllPos.Num(); ++U)
			{
				for (int32 i = 0; i < AllPos[U].Num(); ++i)
				{
					if (AllPos[U][i].SizeSquared() < 0.0001f)
					{
						++OutNearOrigin;
					}
					for (int32 V = U; V < AllPos.Num(); ++V)
					{
						const int32 JStart = (V == U) ? i + 1 : 0;
						for (int32 j = JStart; j < AllPos[V].Num(); ++j)
						{
							OutMin = FMath::Min(OutMin, FVector2D::DistSquared(AllPos[U][i], AllPos[V][j]));
						}
					}
				}
			}
			OutMin = FMath::Sqrt(OutMin);
		};
		float SepNow = 0.f;
		int32 NearOriginNow = 0;
		ComputeMinSeparation(SepNow, NearOriginNow);
		// Formation coherence: count entities outside their unit body + 3 m.
		auto CountFlankViolations = [this, &AllPos, World]() -> int32
		{
			int32 Violations = 0;
			for (int32 U = 0; U < Units.Num(); ++U)
			{
				const FUnitFormationSharedFragment* Formation = FindFormation(World, Units[U]->GetUnitHandle());
				if (!Formation || !AllPos[U].Num())
				{
					continue;
				}
				const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation->NumSlots) / Formation->NumFiles));
				const float Rad = FMath::DegreesToRadians(Formation->FacingYaw);
				const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
				const float LimitAlong = (NumRanks * Formation->RankSpacing * 0.5f) * 0.01f + 3.f;
				const float LimitSide = (Formation->NumFiles * Formation->FileSpacing * 0.5f) * 0.01f + 3.f;
				for (const FVector2D& P : AllPos[U])
				{
					const FVector2D Delta = (P - Formation->AnchorPos * 0.01f);
					if (FMath::Abs(FVector2D::DotProduct(Delta, Front)) > LimitAlong
						|| FMath::Abs(FVector2D::DotProduct(Delta, FVector2D(-Front.Y, Front.X))) > LimitSide)
					{
						++Violations;
					}
				}
			}
			return Violations;
		};
		const int32 ViolationsNow = CountFlankViolations();
		if ((SepNow < 0.3f || ViolationsNow > 0) && TicksInPhase < 900)
		{
			return; // still re-dressing; bounded wait
		}

		bool bOk = true;

		// T1.5a: zero unstuck escalations across the whole scenario.
		const int32 Escalations = Debug->GetLadderEscalations();
		if (Escalations != 0)
		{
			AddError(FString::Printf(TEXT("T1.5 FAIL: %d unstuck escalations (expected 0)"), Escalations));
			bOk = false;
		}

		// T1.5b: separation -- min pairwise distance across all entities
		// (recomputed on the settled state; AllPos filled above).
		int32 TotalEntities = 0;
		for (int32 U = 0; U < AllPos.Num(); ++U)
		{
			TotalEntities += AllPos[U].Num();
		}
		float MinDistM = 0.f;
		int32 NearOriginCount = 0;
		ComputeMinSeparation(MinDistM, NearOriginCount);
		if (TotalEntities != 320)
		{
			AddError(FString::Printf(TEXT("T1.5 FAIL: expected 320 live entities, got %d"), TotalEntities));
			bOk = false;
		}
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT S1Settle ticks=%d minSep=%.4fm nearOrigin=%d"),
			TicksInPhase, MinDistM, NearOriginCount);
		if (MinDistM < 0.3f)
		{
			AddError(FString::Printf(TEXT("T1.5 FAIL: separation %.3f m < 0.3 m threshold"), MinDistM));
			bOk = false;
		}

		// T1.5c: no flank shedding -- every entity within its unit body
		// inflated by 3 m, and the unit actually wheeled (facing ~ east).
		for (int32 U = 0; U < Units.Num(); ++U)
		{
			const FUnitFormationSharedFragment* Formation = FindFormation(World, Units[U]->GetUnitHandle());
			if (!Formation)
			{
				continue;
			}
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, Formation->FacingYaw)) > 15.f)
			{
				AddError(FString::Printf(TEXT("T1.5 FAIL: unit %d facing %.1f (expected ~0 after east wheel)"),
					U, Formation->FacingYaw));
				bOk = false;
			}
			const int32 NumRanks = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Formation->NumSlots) / Formation->NumFiles));
			const float Rad = FMath::DegreesToRadians(Formation->FacingYaw);
			const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
			const float SlackM = 3.f;
			for (const FVector2D& P : AllPos[U])
			{
				const FVector2D Delta = (P - Formation->AnchorPos * 0.01f);
				const float Along = FMath::Abs(FVector2D::DotProduct(Delta, Front));
				const float Side = FMath::Abs(FVector2D::DotProduct(Delta, FVector2D(-Front.Y, Front.X)));
				const float LimitAlong = (NumRanks * Formation->RankSpacing * 0.5f) * 0.01f + SlackM;
				const float LimitSide = (Formation->NumFiles * Formation->FileSpacing * 0.5f) * 0.01f + SlackM;
				if (Along > LimitAlong || Side > LimitSide)
				{
					AddError(FString::Printf(TEXT("T1.5 FAIL: flank shed (entity %.1f m along / %.1f m side of anchor)"),
						Along, Side));
					bOk = false;
					break;
				}
			}
		}

		// Position evidence: unit 0's anchor + first entities (diagnose drift).
		if (const FUnitFormationSharedFragment* F0 = FindFormation(World, Units[0]->GetUnitHandle()))
		{
			UE_LOG(LogMassBattle, Display,
				TEXT("EXEC_RESULT S1Dump U0 anchor=(%.1f,%.1f)m facing=%.1f entities0-3=(%s)"),
				F0->AnchorPos.X * 0.01f, F0->AnchorPos.Y * 0.01f, F0->FacingYaw,
				*FString::Printf(TEXT("%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f"),
					AllPos[0].Num() > 0 ? AllPos[0][0].X : -1.f, AllPos[0].Num() > 0 ? AllPos[0][0].Y : -1.f,
					AllPos[0].Num() > 1 ? AllPos[0][1].X : -1.f, AllPos[0].Num() > 1 ? AllPos[0][1].Y : -1.f,
					AllPos[0].Num() > 2 ? AllPos[0][2].X : -1.f, AllPos[0].Num() > 2 ? AllPos[0][2].Y : -1.f,
					AllPos[0].Num() > 3 ? AllPos[0][3].X : -1.f, AllPos[0].Num() > 3 ? AllPos[0][3].Y : -1.f));
		}
		UE_LOG(LogMassBattle, Display,
			TEXT("EXEC_RESULT S1_MarchWheel Assert escalations=%d minSeparation=%.3fm ok=%d"),
			Escalations, MinDistM, bOk ? 1 : 0);
		Phase = EPhase::Done;
		CompleteTest(bOk ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed,
			bOk ? TEXT("S1 march+wheel green") : TEXT("S1 assertions failed"));
	}
}

// ---------------------------------------------------------------------------
// OrderSmoke (A2)
// ---------------------------------------------------------------------------

AUnitOrderSmokeTest::AUnitOrderSmokeTest()
{
	TimeLimit = 120.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitOrderSmokeTest::BeginTest()
{
	// Manager-equivalent start (phase-0 pattern).
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
}

void AUnitOrderSmokeTest::CompleteTest(EFunctionalTestResult TestResult, const FString& Message)
{
	// Manager-less completion (phase-0 pattern).
	Result = TestResult;
	bIsRunning = false;
	SetActorTickEnabled(false);
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderSmoke Complete Result=%s Msg=%s"),
	       *LexToString(TestResult), *Message);
}

void AUnitOrderSmokeTest::StartTest()
{
	Super::StartTest();
	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no world"));
		return;
	}
	UBattleTestSettings::ApplyTestMode();
	TArray<FString> Violations;
	if (!UBattleTestSettings::AssertTestMode(nullptr, &Violations, World))
	{
		CompleteTest(EFunctionalTestResult::Error,
			FString::Printf(TEXT("test-mode contract violated: %s"), *FString::Join(Violations, TEXT("; "))));
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Unit = World->SpawnActor<AUnitActor>(FVector(0, 0, 100.f), FRotator::ZeroRotator, Params);
	if (!Unit)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("failed to spawn unit actor"));
		return;
	}
	Unit->EduRowIndex = 0;
	Unit->SoldierCount = 40;
	FBattleSpawnResult SpawnResult;
	FString Error;
	if (!Unit->SpawnEntities(SpawnResult, Error))
	{
		CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
		return;
	}

	// A2 surface: registry-owned order API (the ExecBridge command wraps the
	// same entry point; the API is the thing under test).
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	const FUnitFormationSharedFragment* Formation = FindFormation(World, Unit->GetUnitHandle());
	if (!Registry || !Formation)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("registry/formation missing"));
		return;
	}
	const FVector2D Goal(Formation->AnchorPos.X + 2000.f, Formation->AnchorPos.Y);
	if (!Registry->IssueMoveOrder(Unit->GetUnitHandle(), Goal, 0.f))
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("IssueMoveOrder rejected"));
		return;
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderSmoke Move issued Anchor=(%.1f,%.1f)m"),
	       Goal.X * 0.01f, Goal.Y * 0.01f);
	TicksInPhase = 0;
}

void AUnitOrderSmokeTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsRunning())
	{
		return;
	}
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("registry missing"));
		return;
	}
	++TicksInPhase;

	const FUnitFormationSharedFragment* Formation = FindFormation(World, Unit->GetUnitHandle());
	const FUnitOrder* Order = Registry->FindOrder(Unit->GetUnitHandle());
	if (!Formation || !Order)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("order/formation missing"));
		return;
	}

	if (FVector2D::DistSquared(Formation->AnchorPos, Order->AnchorPos) <= FMath::Square(100.f))
	{
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT OrderSmoke Arrived ticks=%d"), TicksInPhase);
		CompleteTest(EFunctionalTestResult::Succeeded, TEXT("unit reached the ordered anchor"));
		return;
	}
	if (TicksInPhase > 1500)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("order smoke timed out before arrival"));
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

	AFunctionalTest* MakeMarchWheelTest(UWorld* World)
	{
		return World->SpawnActor<AUnitMarchWheelTest>();
	}
	AFunctionalTest* MakeOrderSmokeTest(UWorld* World)
	{
		return World->SpawnActor<AUnitOrderSmokeTest>();
	}
	const FBattleFuncTestRegistry::FRegistrar GRegS1(TEXT("AUnitMarchWheelTest"), &MakeMarchWheelTest);
	const FBattleFuncTestRegistry::FRegistrar GRegSmoke(TEXT("AUnitOrderSmokeTest"), &MakeOrderSmokeTest);
}
