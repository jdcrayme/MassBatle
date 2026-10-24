// BattleFuncTestActor.h -- S-scenario functional test actors (roadmap B.3).
// S0_SyncLatency (T0.4 + T0.8 spawn-level isolation): spawn 2 units x 40,
// set a GAS attribute on unit A, assert the shared fragment reflects the new
// value within 1 tick (tick-count assertion, never wall time) and unit B is
// untouched. Runs in the -game func tier where Mass phases tick (risk §3.2.1).
#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "MassEntityHandle.h"
#include "BattleFuncTestActor.generated.h"

class AUnitActor;

UCLASS()
class MASSBATTLE_API AUnitSyncLatencyTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitSyncLatencyTest();

	/** Harness entry point: AFunctionalTest::PrepareTest is protected; this
	 *  exposes the manager-style start (bIsRunning + tick enable), after which
	 *  Tick drives StartTest once IsReady. */
	void BeginTest();

	/** Manager-less completion: AFunctionalTest::FinishTest notifies the
	 *  FunctionalTestingManager ("SetFunctionalComplete with no active test"
	 *  errors when driven by the automation harness instead), so completion is
	 *  recorded directly (Result + bIsRunning) and logged for evidence. */
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);

	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

protected:
	void DestroyTestUnits();

	UPROPERTY() TObjectPtr<AUnitActor> UnitA;
	UPROPERTY() TObjectPtr<AUnitActor> UnitB;

	/** Tick counter since the attribute change (T0.4 asserts on ticks, not seconds). */
	int32 TicksSinceSet = -1;
	bool bAttributeSet = false;
};

// ---------------------------------------------------------------------------
// S0_DeathPath (T0.5): kill one entity via the KillEntity bridge (deferred
// sparse tag + explicit flush at the E5-audited point); assert tag visible,
// roster updated, GAS event observed, deferred destroy completes.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLE_API AUnitDeathPathTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitDeathPathTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	FMassEntityHandle DeadEntityHandle;
	int32 KillTick = -1;
	bool bKilled = false;
	bool bKillRequested = false;
};

// ---------------------------------------------------------------------------
// S0_ExitCriteria (Task 13): the roadmap Phase 0 exit scenario end-to-end.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLE_API AUnitExitCriteriaTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitExitCriteriaTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	UPROPERTY() TObjectPtr<AUnitActor> UnitA;
	UPROPERTY() TObjectPtr<AUnitActor> UnitB;
	enum class EPhase : int32 { Spawn, Sync, Death, VerifyHash, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	uint64 HashBeforeDeath = 0;
	FMassEntityHandle DeadEntityHandle;
};

// ---------------------------------------------------------------------------
// S-scenario actor factory registry -- deterministic, reflection-free class
// lookup for the automation harness (FindObject with /Script/ paths and
// TObjectIterator both failed to resolve fresh C++ classes in -game processes
// on this machine; a static registry registered beside each actor definition
// cannot miss).
// ---------------------------------------------------------------------------
struct MASSBATTLE_API FBattleFuncTestRegistry
{
	using FFactory = AFunctionalTest* (*)(UWorld* World);

	static TMap<FString, FFactory>& Get()
	{
		static TMap<FString, FFactory> Map;
		return Map;
	}

	struct FRegistrar
	{
		FRegistrar(const FString& Name, FFactory Factory)
		{
			Get().Add(Name, Factory);
		}
	};
};
