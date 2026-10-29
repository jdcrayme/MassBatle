// BattleFuncTestActor.h -- S-scenario functional test actors (roadmap B.3).
// S0_SyncLatency (T0.4 + T0.8 spawn-level isolation): spawn 2 units x 40,
// set a GAS attribute on unit A, assert the shared fragment reflects the new
// value within 1 tick (tick-count assertion, never wall time) and unit B is
// untouched. Runs in the -game func tier where Mass phases tick (risk §3.2.1).
#pragma once

#include "CoreMinimal.h"

class UUnitRegistrySubsystem;
class UMassEntitySubsystem;
#include "FunctionalTest.h"
#include "Mass/EntityHandle.h"
#include "BattleGarrisonTypes.h"
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

	// Audit H1/M14: per-tick mid-march formation coherence. Entities outside
	// their unit's TRAVELING body envelope (+5 m) while PathS is strictly
	// inside the path are violations; two overshoot distances are judged:
	// AHEAD of the front along the facing (the H1 terminus-streaming
	// signature -- entities beelining for path-end slots; bound 1.0 m) and
	// ANY direction (ejections/left-behind entities; bound 6.0 m -- the
	// 90-degree wheel start legitimately leaves the old flank up to ~5.2 m
	// behind the rotating envelope until the re-dress catches up, while real
	// shedding or the H1 dissolution overshoots by an order of magnitude).
	int32 MaxMidMarchViolations = 0;
	float MaxAheadOvershootM = 0.f;
	float MaxAnyOvershootM = 0.f;
	int32 MidMarchSamples = 0;
	// Diagnostics: per-phase maxima + the first violation's coordinates.
	int32 MaxViolationsMarch = 0;
	int32 MaxViolationsWheel = 0;
	FString FirstViolationDesc;

	// Phase 2 phase-A gate: max servo-target error observed during the wheel
	// (asserted <= 0.8 m only when a phase-A flag is on; logged otherwise --
	// the flag-off baseline legitimately sheds the ends in tight wheels).
	float MaxWheelSlotErrorM = 0.f;
	bool bPhaseAFlagsOn = false;

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

	// Audit H3: the mover's speed-limit samples while its anchor is inside
	// the contact band -- the ~55% pass-through factor must actually bind
	// (the old code multiplied a dead local after the Min3).
	float MaxContactLimitCm = 0.f;
	int32 ContactSamples = 0;
	// Audit M15: escalations during the contact window get an explicit,
	// disclosed budget instead of being exempt from every assert.
	int32 EscalationsAtPassStart = 0;

	// Phase 2 phase-B gate: the MOVER's dress metric recovers <= 0.5 m within
	// 4 s (240 ticks) of clearing the standee (asserted when a phase-B flag is
	// on; logged otherwise). MinSpeedLimitDuringTearCm documents the P4(a)
	// throttle dip (the contact factor already dips speed at baseline, so the
	// straggler term's contribution stays a diagnostic here).
	float MaxSlotErrorAfterPassM = 0.f;
	int32 MoverSlotErrorRecoveredTicks = -1;
	float MinSpeedLimitDuringTearCm = FLT_MAX;
	bool bPhaseBFlagsOn = false;

	/** Cavalry mover (registry handle; spawned directly from the inline EDU
	 *  row, no AUnitActor shell). */
	FUnitHandle MoverHandle;

	UPROPERTY() TObjectPtr<AUnitActor> Standee;
};

// ---------------------------------------------------------------------------
// S3_PassQueue (T1.7, audit H5 re-scope to the roadmap B.3 wording): TWO units
// cross ONE wall gap (8 m authored -> ~4.5 m on the 3 m grid -> morph). The
// follower queues behind the leader while the leader occupies the corridor.
// Asserts: the follower holds a station near the pass mouth during the
// leader's occupancy; morph commits exactly once per unit (PassMorphCount <= 1
// -- no re-morph thrash); clean re-deploy past the exit (frontage restored --
// the audit H2 invariant, NOT the locked-3-files state the old test sold).
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

	UPROPERTY() TObjectPtr<AUnitActor> Leader;    // first through the gap (morph)
	UPROPERTY() TObjectPtr<AUnitActor> Follower;  // queues at the mouth, then crosses
	int32 FilesAtSpawn = 0;

	// -- queue-pressure tracking (sampled while the leader's anchor is inside
	//    the wall band) --
	int32 QueueObservedTicks = 0;             // follower held station behind the leader's tail
	int32 MaxFollowerFrontXCm = 0;            // deepest follower front advance past the wall line (cm)
	int32 MaxFollowerFrontBeyondTailCm = 0;   // deepest push into the leader's body past its tail (cm)
	int32 MaxLeaderMorphs = 0;                // PassMorphCount high-water per unit
	int32 MaxFollowerMorphs = 0;
	int32 MinLeaderFilesSeen = INT32_MAX;     // smallest committed frontage observed (morph evidence)
};

// ---------------------------------------------------------------------------
// S4_WidthOverride (A2 width control): a 40-man unit marches with a commanded
// file count (10 -> ranks 4), then re-fronts back to the spawn layout. Asserts:
// NumFiles applied on order consumption; the re-laid formation is coherent
// (body bounds) and separated; repeatable in both directions.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitWidthOverrideTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitWidthOverrideTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Settle, Assert, Refront, Settle2, Assert2, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;

	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	int32 FilesAtSpawn = 0;
};

// ---------------------------------------------------------------------------
// S5_MarchRefront (phase 2 re-scope): a 40-man unit marches 80 m under a
// commanded frontage (files 10, facing 135 deg) WITH the fall-in master
// switch on -- the pre-phase-2 assertions (EarlyRefront/OffTangent) encoded
// the superseded rigid-then-refront contract and are replaced by: per-entity
// monotone approach to the final slots, a bounded anchor-arrival -> settled
// time, settled facing ~= commanded, files applied, coherent, zero
// escalations.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitMarchRefrontTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitMarchRefrontTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Settle, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 MarchCoherenceViolations = 0;
	int32 FacingSettledTicks = 0;

	// Phase 2 monotone-approach probe (per roster entity, final slots at the
	// ordered anchor/facing): after first coming within the handoff distance,
	// distance-to-final-slot must never grow > 0.25 m.
	TArray<float> MinDistFinalM;
	TArray<bool> HandedOff;
	TArray<int32> LastSlot;      // assignment-change detector for the monotone probe
	TArray<int32> ExcursionTicks; // persistence counter for the monotone probe
	int32 MonotonicityViolations = 0;
	FString FirstMonotonicityDesc;
	int32 AnchorArrivalTick = -1;
	int32 AllSettledTick = -1;

	UPROPERTY() TObjectPtr<AUnitActor> Unit;
};

// ---------------------------------------------------------------------------
// S6_InPlaceReform (A2 in-place reform): a standing unit is re-ordered in
// place (anchor unchanged, facing +135 deg, files 20 -> 8). Asserts: files
// apply at order consumption; facing + layout achieve the commanded state in
// bounded time; the anchor never drifts; coherent; zero escalations.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitInPlaceReformTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitInPlaceReformTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, Order, Reform, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 FacingSettledTicks = 0;
	bool bFilesChecked = false;

	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	FVector2D AnchorBefore = FVector2D::ZeroVector;
};

// ---------------------------------------------------------------------------
// S7_FallIn (phase 2 P1, the reported repro): a 40-man unit marches 60 m with
// a commanded arrival facing 90 deg off the path and CommandedFiles = 10.
// TW progressive fall-in under Battle.Movement.ProgressiveFallIn: every
// soldier owns his final slot from order time. Asserts: (a) monotone
// approach -- once within 0.7 m of his final slot, no entity's distance to it
// grows > 0.25 m; (b) anchor-arrival -> all-settled <= 3 s; (c) zero unstuck
// escalations; (d) final geometry: every entity within 0.35 m of its final
// slot and its yaw within 5 deg of the commanded facing.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitFallInTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitFallInTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, March, Settle, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;

	// Per-entity final-slot tracking (roster order; sized at first sample).
	TArray<float> MinDistFinalM;
	TArray<bool> HandedOff;
	TArray<int32> LastSlot;      // assignment-change detector for the monotone probe
	TArray<int32> ExcursionTicks; // persistence counter for the monotone probe
	int32 MonotonicityViolations = 0;
	FString FirstMonotonicityDesc;
	int32 AnchorArrivalTick = -1;
	int32 SettledTicks = 0;
	bool bSettledInBudget = false; // all-settled within 180 ticks of arrival

	UPROPERTY() TObjectPtr<AUnitActor> Unit;
};

// ---------------------------------------------------------------------------
// Phase 2 task 6 combat smoke: two units ordered into contact form pairs and
// press the ring (the assign-pass wiring's end-to-end evidence; the T2 gates
// are task 7/8's fuller scenarios). Enables the combat cvars locally, restores
// on completion.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API ACombatSmokeTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	ACombatSmokeTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Spawn, Engage, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	UPROPERTY() TObjectPtr<AUnitActor> UnitA;
	UPROPERTY() TObjectPtr<AUnitActor> UnitB;
	int32 SavedCombatCvars[4] = {};
	void SetCombatCvars();
	void RestoreCombatCvars();
	void DestroySmokeUnits();
};

// ---------------------------------------------------------------------------
// Phase 2 task 7 charge mechanics (T2.6): cavalry-charge shape vs a standing
// line -- bonus only on fresh fast contact, bounded knockback observable in
// transform deltas, decay to melee within the window, kill chain evidenced.
// Walk speed rides a local cvar override (the fixture's 1.4 m/s walk cannot
// reach the 4 m/s fresh-contact gate).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AChargeMechanicsTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AChargeMechanicsTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Spawn, Charge, Decay, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	UPROPERTY() TObjectPtr<AUnitActor> UnitA;
	UPROPERTY() TObjectPtr<AUnitActor> UnitB;
	int32 SavedCvars[4] = {};      // the four combat gates (int cvars)
	float SavedWalkSpeed = 0.f;    // the float cvar rides separately (an int
	                               // cast truncates 1.4 -> 1.0 -- measured
	                               // 2026-10-27: poisoned every later march)
	bool bSawConnect = false;
	float MaxDefenderDisplacementM = 0.f;
	TArray<FVector2D> DefenderPositionsAtContact;
	void SetCvars();
	void RestoreCvars();
	void DestroyChargeUnits();
};


// ---------------------------------------------------------------------------
// Phase 2 task 8: the T2.4/T2.5/T2.7 scenario gates (the exit-criteria
// scenarios are PR-gate citizens) + the shared clash-spawn helper the
// Battle.Combat.SpawnClash Exec fixture reuses.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AS4ShieldWallTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AS4ShieldWallTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Spawn, Engage, Sample, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	UPROPERTY() TObjectPtr<AUnitActor> Attackers;
	UPROPERTY() TObjectPtr<AUnitActor> Defenders;
	int32 SavedCvars[4] = {};

	// Melt-curve samples: (defender alive, avg coverage, pressure per alive).
	struct FMeltSample
	{
		int32 Alive = 0;
		float AvgCoverage = 0.f;
		float PressurePerAlive = 0.f;
	};
	TArray<FMeltSample> Samples;
	int32 WallHeldSamples = 0;
	int32 MeltSamples = 0;
	float WallBudgetAtHighCoverage = 0.f;
	float MeltBudgetAtLowCoverage = 0.f;
	void SetCvars();
	void RestoreCvars();
};

UCLASS()
class MASSBATTLETEST_API AS5MobTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AS5MobTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Spawn, Engage, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	UPROPERTY() TObjectPtr<AUnitActor> Peasants;
	UPROPERTY() TObjectPtr<AUnitActor> KnightA;
	UPROPERTY() TObjectPtr<AUnitActor> KnightB;
	int32 SavedCvars[4] = {};
	int32 MaxExposedAttackers = 0;
	int32 MaxWaitersAtRing = 0;
	int32 MobStartTick = -1;
	int32 MobKillTick = -1;
	void SetCvars();
	void RestoreCvars();
};

UCLASS()
class MASSBATTLETEST_API APairLifecycleTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	APairLifecycleTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Spawn, Pair, Handoff, Disengage, Distance, Assert, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	UPROPERTY() TObjectPtr<AUnitActor> Attackers;
	UPROPERTY() TObjectPtr<AUnitActor> Defenders;
	int32 SavedCvars[4] = {};
	int32 PairedBeforeKill = 0;
	int32 RePairTicks = -1;
	int32 DisengageReleaseTicks = -1;
	int32 DistanceBreakTicks = -1;
	int32 PairFormedTick = -1;
	void SetCvars();
	void RestoreCvars();
};

// ---------------------------------------------------------------------------
// Phase 2 task 9: T2.8 determinism two-run (R9 scope: same binary, same
// machine). The same clash twice from the same RNG root; the state hash
// sampled every 60 ticks; full reset between the runs; all hashes match.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API ADeterminismTwoRunTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	ADeterminismTwoRunTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	enum class EPhase : int32 { Run1, Run2, Done };
	EPhase Phase = EPhase::Done;
	int32 SavedCvars[4] = {};
	int32 SavedRanged[4] = {};
	int32 TicksDone = 0;
	bool bNeedSpawn = false;
	TArray<uint64> Run1Hashes;
	void SetCvars();
	void RestoreCvars();
	// Phase 3 (RC8): the mixed clash+volley fixture -- archers + infantry on
	// BOTH sides, spawned deterministically at each run's start (the phase-2
	// shape hashed an empty world: DestroyOtherUnitActors cleared the map
	// units and nothing re-spawned -- measured 2026-10-29).
	void SpawnMixedFixture();
	void DestroyFixture();
};

// ---------------------------------------------------------------------------
// Phase 3 task 0: T3.4/T3.5 gate SKELETONS (fail-by-design until the ranged
// pipeline tasks land; counted + listed in the phase-3 progress log -- the
// Phase 2 task-0 pattern, with the audit's C-6 lesson applied: no perf stub
// ships, T3.6 lands as a fixture + measured metrics with its task). The
// implementing tasks replace these actors with the real scenario machines
// (spec ranged-stack section 5).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AS6VolleyTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AS6VolleyTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	// The T3.4 phase machine (spec ranged-stack section 5): volley +
	// dispersion, the flat-lane friendly hold, the hill LOS variant, the
	// team-blind stray, and the ammo/out-of-ammo accounting -- one seeded
	// scenario, sampled assertions, evidence per phase.
	enum class EPhase : int32 { Spawn, Volley, FlatLane, Hill, Stray, Drain, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 PhaseBudget = 0;
	UPROPERTY() TObjectPtr<AUnitActor> Archers;
	UPROPERTY() TObjectPtr<AUnitActor> Levy;
	UPROPERTY() TObjectPtr<AUnitActor> Crossbows;
	UPROPERTY() TObjectPtr<AUnitActor> LevyD;
	UPROPERTY() TObjectPtr<AUnitActor> LaneFriendly;
	UPROPERTY() TObjectPtr<AUnitActor> StrayFriendly;
	int32 SavedCombat[5] = {};
	int32 SavedRanged[4] = {};
	float SavedReload = 0.f;
	int32 ShotsAtPhaseStart = 0;
	int32 CrossbowShotsMark = 0;
	bool bDispersionSampled = false;
	float MaxDeviationDeg = 0.f;
	void SetCvars();
	void RestoreCvars();
	void SpawnSide(TObjectPtr<AUnitActor>& OutActor, int32 Row, int32 Count, uint8 Team,
		const FVector& Pos, float Yaw, const TCHAR* Name);
	int32 UnitShotsFired(AUnitActor* Actor);
	bool ZeroReloadTimers(AUnitActor* Actor);
	FVector2D ComputeCorridorMid(UUnitRegistrySubsystem* Registry, UMassEntitySubsystem* Entities,
		const FUnitHandle& ShooterUnit, const FUnitHandle& TargetUnit, float T = 0.5f);
};

UCLASS()
class MASSBATTLETEST_API ASkirmishBehaviorTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	ASkirmishBehaviorTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }
	virtual void Destroyed() override;

private:
	// The T3.5 phase machine: break, fire-while-retreating, resume,
	// hysteresis stability, hold-fire-still-retreats, player-order override.
	enum class EPhase : int32 { Spawn, Break, Retreat, Resume, Hysteresis, HoldFire, Override, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 PhaseBudget = 0;
	UPROPERTY() TObjectPtr<AUnitActor> Archers;
	UPROPERTY() TObjectPtr<AUnitActor> Threat;
	int32 SavedCombat[5] = {};
	int32 SavedRanged[4] = {};
	float SavedReload = 0.f;
	int32 ShotsMark = 0;
	void SetCvars();
	void RestoreCvars();
	float AnchorDistM();
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

// ---------------------------------------------------------------------------
// Phase-garrison scenarios (TG.6-TG.9). AGarrisonScenarioBase owns the full
// ETW flow (order -> approach -> file in -> settle -> assert -> egress ->
// re-form -> assert); subclasses configure fixture type/geometry and the
// elevation/facing expectations (B4/B5).
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AGarrisonScenarioBase : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AGarrisonScenarioBase();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

protected:
	/** Subclass configuration (set in the ctor). */
	UPROPERTY() EGarrisonStructureType FixtureType = EGarrisonStructureType::Building;
	FVector2D FixturePosM = FVector2D::ZeroVector;
	float FixtureRotDeg = 0.f;
	FVector2D UnitSpawnPosCm = FVector2D::ZeroVector;
	float ExpectedSlotZM = 0.f;   // wall 6 / trench -0.75 / building 0
	float ZToleranceM = 0.4f;
	float OutwardYawDeg = 0.f;    // expected post facing (wall/trench)
	bool bYawIsBimodal = false;   // building: rows face 0 or 180

	enum class EPhase : int32 { Spawn, Order, Garrison, SettleWindow, AssertGarrison, Egress, EgressWindow, AssertEgress, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 MaxFilesSeenInIngress = 0;
	int32 EscalationsAtSettle = 0;
	int32 EscalationsAtEgress = 0;

	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	uint32 StructureId = 0;
};

/** S8_BuildingGarrison (TG.6): 40-man unit into the gray-box building. */
UCLASS()
class MASSBATTLETEST_API AUnitBuildingGarrisonTest : public AGarrisonScenarioBase
{
	GENERATED_BODY()
public:
	AUnitBuildingGarrisonTest();
};

/** S9_TrenchGarrison (TG.7): ramp filing, sunken posts (Z < 0), egress. */
UCLASS()
class MASSBATTLETEST_API AUnitTrenchGarrisonTest : public AGarrisonScenarioBase
{
	GENERATED_BODY()
public:
	AUnitTrenchGarrisonTest();
};

/** S10_WallGarrison (TG.8): stair climb (Z rise), wall-top posts, outward yaw. */
UCLASS()
class MASSBATTLETEST_API AUnitWallGarrisonTest : public AGarrisonScenarioBase
{
	GENERATED_BODY()
public:
	AUnitWallGarrisonTest();
};

// ---------------------------------------------------------------------------
// S8b_MultiUnitGarrison (TG.9, task 7): two units garrison one building; the
// second QUEUES with a bounded wait, then settles into its disjoint range;
// sequential egress files both out with zero strays and no lease deadlock;
// the booking ledger drains to empty.
// ---------------------------------------------------------------------------
UCLASS()
class MASSBATTLETEST_API AUnitMultiUnitGarrisonTest : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AUnitMultiUnitGarrisonTest();

	void BeginTest();
	void CompleteTest(EFunctionalTestResult TestResult, const FString& Message);
	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsReady_Implementation() override { return true; }

private:
	enum class EPhase : int32 { Spawn, Order, BothSettled, AssertOccupancy, ExitA, ExitB, AssertDrained, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	int32 BQueueTicks = 0;
	bool bObservedBQueued = false;
	bool OccupancyAssertOk = true;

	UPROPERTY() TObjectPtr<AUnitActor> UnitA;
	UPROPERTY() TObjectPtr<AUnitActor> UnitB;
	uint32 StructureId = 0;
};
