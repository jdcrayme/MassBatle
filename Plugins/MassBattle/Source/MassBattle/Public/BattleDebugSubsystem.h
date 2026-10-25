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

	/** Test hook: zero the diagnostic counters (scenario start). */
	void ResetDiagnostics();

private:
	FBattleFrameMetrics LastMetrics;

	/** T1.5/T1.8 diagnostics; relaxed atomicity is fine (diagnostic only). */
	std::atomic<int32> LadderEscalations{0};

	FCriticalSection OverlayMutex;
	TArray<FBattleOverlayLine> PendingOverlayLines;
};
