// BattleFuncTestActor.h -- S-scenario functional test actors (roadmap B.3).
// S0_SyncLatency (T0.4 + T0.8 spawn-level isolation): spawn 2 units x 40,
// set a GAS attribute on unit A, assert the shared fragment reflects the new
// value within 1 tick (tick-count assertion, never wall time) and unit B is
// untouched. Runs in the -game func tier where Mass phases tick (risk §3.2.1).
#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "Mass/EntityHandle.h"
#include "BattleFuncTestActor.generated.h"

class AUnitActor;

UCLASS()
class MASSBATTLETEST_API AUnitSyncLatencyTest : public AFunctionalTest
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
// S0_DeathPath (T0.5): kill TWO entities in the same frame via the KillEntity
// bridge (deferred REGULAR tag per ADR-15 final state + explicit flush at the
// E5-audited point). Asserts immediately after the flush -- never vacuously on
// an already-destroyed entity (audit M1): FDeadTag + FDeadSignaledTag present
// and dead/live query counts correct. Then: roster 8->6, TWO GAS events (two
// kills in one frame used to re-signal the first corpse -- audit H3), deferred
// destroys complete, and survivor RosterIndex fragments are restamped to their
// live roster positions (audit M5).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitDeathPathTest : public AFunctionalTest
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
	FMassEntityHandle SecondDeadEntityHandle;
	int32 RosterCountAtSpawn = 0;
	int32 SettleTicks = 0;
	int32 KillTick = -1;
	bool bKillRequested = false;
};

// ---------------------------------------------------------------------------
// S0_ExitCriteria (Task 13): the roadmap Phase 0 exit scenario end-to-end.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitExitCriteriaTest : public AFunctionalTest
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
// S1_MarchWheel (T1.5) + OrderSmoke (A2): phase-1 movement scenarios.
// S1: 8 units march 30 m, then wheel 90 degrees onto a lateral anchor.
// Asserts (B.3 S1 / T1.5 verbatim): zero unstuck escalations; separation
// above threshold; no flank shedding; arrival at the ordered anchors.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitMarchWheelTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitMarchWheelTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Wheel, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;

	UPROPERTY() TArray<TObjectPtr<AUnitActor>> Units;
};

// ---------------------------------------------------------------------------
// A2 smoke: issue one Move order through the registry API; the unit must
// reach the ordered anchor (brief Task 10 item 2).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitOrderSmokeTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitOrderSmokeTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	int32 TicksInPhase = 0;
};

// ---------------------------------------------------------------------------
// S2_PassThrough (T1.6): unit A marches THROUGH standing unit B. Asserts
// (B.3 S2): no pairwise stalls (zero unstuck escalations as the stall proxy +
// pass completes); pass time bounded (tick cap); B's wake re-dresses within a
// bounded settle after the pass (coherence + separation).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitPassThroughTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitPassThroughTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Settle, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 EscalationsAtPassEnd = 0;

	UPROPERTY() TObjectPtr<AUnitActor> Mover;
	UPROPERTY() TObjectPtr<AUnitActor> Standee;
};

// ---------------------------------------------------------------------------
// S3_SqueezeMorph (T1.7): two units cross a stamped wall -- one through an
// 8 m gap (morph to column), one through a 16 m gap (squeeze). Asserts:
// threshold decision correct both sides; the decision locks (PassLocked,
// commits once -- no re-decision thrash); clean re-deploy past the exit.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitSqueezeMorphTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitSqueezeMorphTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Settle, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;

	UPROPERTY() TObjectPtr<AUnitActor> Narrow;  // 8 m gap -> morph
	UPROPERTY() TObjectPtr<AUnitActor> Wide;    // 16 m gap -> squeeze
	int32 NarrowFilesAtSpawn = 0;
};

// ---------------------------------------------------------------------------
// S-scenario actor factory registry -- deterministic, reflection-free class
// lookup for the automation harness (FindObject with /Script/ paths and
// TObjectIterator both failed to resolve fresh C++ classes in -game processes
// on this machine; a static registry registered beside each actor definition
// cannot miss).
// ---------------------------------------------------------------------------
struct MASSBATTLETEST_API FBattleFuncTestRegistry
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
