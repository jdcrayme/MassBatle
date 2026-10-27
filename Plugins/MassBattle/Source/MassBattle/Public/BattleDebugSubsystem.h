// BattleDebugSubsystem.h -- debug overlay + B.4 metrics (roadmap B.2). Phase 1
// (decision A3) adds the first DRAWABLE states: path polyline, slot-error
// magnitude, unstuck ladder level -- minimal DrawDebug-class lines via the
// thread-safe line queue below (workers enqueue, the game-thread Tick drains
// and draws). Everything else stays hook-only per the Phase 0 A1 decision.
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Subsystems/WorldSubsystem.h"
#include "BattleDebugSubsystem.generated.h"

/** Per-frame metrics in the B.4 instrumented set (drawn in Phase 2; dumped
 *  via Battle.DumpMetrics now). */
USTRUCT()
struct FBattleFrameMetrics
{
	GENERATED_BODY()

	UPROPERTY() int32 Frame = 0;
	UPROPERTY() int32 ArchetypeCount = 0;
	UPROPERTY() int32 UnitCount = 0;
	UPROPERTY() double UnitSyncMs = 0.0;
	UPROPERTY() double RegistryHash32 = 0.0; // placeholder carrier; Task 10 widens
};

/** One queued overlay line (world space, centimeters). LadderLevel > 0 draws
 *  a small box marker at the midpoint (A3 unstuck hook). */
struct FBattleOverlayLine
{
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	FColor Color = FColor::White;
	int32 LadderLevel = 0;
};

UCLASS()
class MASSBATTLE_API UBattleDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- USubsystem --
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// -- FTickableGameObject (UTickableWorldSubsystem) --
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Called by UnitSync (thin shim) to record its per-frame cost. */
	void RecordUnitSyncMs(double Ms);

	/** Thread-safe overlay enqueue (worker processors call this; A3 states
	 *  only: slot-error magnitude, path polyline, unstuck ladder). */
	void EnqueueOverlayLine(const FBattleOverlayLine& Line);

	/** B.4 metrics line, EXEC_RESULT-formatted, for log-based evidence. */
	void DumpMetrics();

	const FBattleFrameMetrics& GetLastMetrics() const { return LastMetrics; }

	/** Unstuck-ladder escalations since the last reset (T1.5 asserts zero).
	 *  Thread-safe: the servo increments from workers. */
	void RecordLadderEscalation() { LadderEscalations++; }
	int32 GetLadderEscalations() const { return LadderEscalations; }

	/** Pass-geometry MORPH decisions since the last reset (audit H2/H5: S3
	 *  asserts the no-thrash contract through this counter). Game thread only
	 *  (UnitLocomotion decides), so a plain int; ResetDiagnostics is GT too. */
	void RecordMorphDecision() { ++MorphDecisions; }
	int32 GetMorphDecisions() const { return MorphDecisions; }

	// -- Garrison metrics (B.8 item 4; task 9) --

	/** One call per unit reaching Settled (UnitLocomotion, game thread):
	 *  filing time = order -> settled, queue wait = queue enter -> ingress
	 *  (both in ticks; diagnostic only -- never hashed). */
	void RecordGarrisonSettled(int32 FilingTicks, int32 QueueWaitTicks);
	/** `Battle.Garrison.Metrics` EXEC line: filings, averages, occupancy. */
	void DumpGarrisonMetrics(int32 BookedUnits, int32 QueueWaiting);

	int32 GetGarrisonFilings() const { return GarrisonFilings; }
	int32 GetGarrisonFilingTicksAvg() const { return GarrisonFilings > 0 ? GarrisonFilingTicksTotal / GarrisonFilings : 0; }
	int32 GetGarrisonQueueWaitTicksAvg() const { return GarrisonQueueWaits > 0 ? GarrisonQueueWaitTicksTotal / GarrisonQueueWaits : 0; }

	/** Test hook: zero the diagnostic counters (scenario start). */
	void ResetDiagnostics();

private:
	FBattleFrameMetrics LastMetrics;

	/** T1.5/T1.8 diagnostics; seq_cst std::atomic (workers write; the extra
	 *  ordering is irrelevant for a diagnostic counter but harmless). */
	std::atomic<int32> LadderEscalations{0};

	/** Morph decisions (game-thread writer; see RecordMorphDecision). */
	int32 MorphDecisions = 0;

	// Garrison metric accumulators (B.4 instrumented set, game thread only).
	int32 GarrisonFilings = 0;
	int32 GarrisonFilingTicksTotal = 0;
	int32 GarrisonQueueWaits = 0;
	int32 GarrisonQueueWaitTicksTotal = 0;

	FCriticalSection OverlayMutex;
	TArray<FBattleOverlayLine> PendingOverlayLines;
};
