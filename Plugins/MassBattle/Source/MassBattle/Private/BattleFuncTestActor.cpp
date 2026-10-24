#include "BattleFuncTestActor.h"
#include "BattleFragments.h"
#include "UnitActor.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "MassBattleModule.h"
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
