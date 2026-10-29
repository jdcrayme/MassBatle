// BattleDebugSubsystem.h -- debug overlay + B.4 metrics (roadmap B.2). Phase 1
// (decision A3) adds the first DRAWABLE states: path polyline, slot-error
// magnitude, unstuck ladder level -- minimal DrawDebug-class lines via the
// thread-safe line queue below (workers enqueue, the game-thread Tick drains
// and draws). Everything else stays hook-only per the Phase 0 A1 decision.
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Subsystems/WorldSubsystem.h"
#include "BattleTypes.h"
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

/** Overlay shape kinds (task 10 debug pass). Segment is the phase-1
 *  primitive; Circle/Rect expand into segments in the game-thread drain
 *  (workers enqueue ONE primitive, the draw cost lands on the game thread). */
enum class EBattleOverlayShape : uint8
{
	Segment = 0,
	Circle = 1,   // From = center, Radius = radius (planar, ground Z)
	Rect = 2      // From = center, Radius = half-extent, YawDeg = facing
};

/** One queued overlay primitive (world space, centimeters). LadderLevel > 0
 *  draws a small box marker at the midpoint (A3 unstuck hook). */
struct FBattleOverlayLine
{
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	FColor Color = FColor::White;
	int32 LadderLevel = 0;
	EBattleOverlayShape Shape = EBattleOverlayShape::Segment;
	float Radius = 0.f;   // Circle radius / Rect half-extent (cm)
	float YawDeg = 0.f;   // Rect facing
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

	/** Phase 2 (task 5): last spatial-hash rebuild cost (BuildSpatialHash). */
	void RecordCombatHashMs(double Ms) { CombatHashMs = Ms; }
	double GetCombatHashMs() const { return CombatHashMs; }

	/** Phase 2 (task 9, T2.9): the game-thread pairing costs (tally + assign).
	 *  Recorded by the tally/assign processors; dumped in BattleMetrics. */
	void RecordTallyMs(double Ms) { TallyMs = Ms; }
	void RecordAssignMs(double Ms) { AssignMs = Ms; }
	double GetTallyMs() const { return TallyMs; }
	double GetAssignMs() const { return AssignMs; }

	/** Phase 3 (task 3): the stepping pass cost + release-cause counters
	 *  (game-thread writer; InFlight/Drops read from the pool at dump time). */
	void RecordProjectileStep(double Ms, int32 GroundHits, int32 TtlExpired)
	{
		ProjectileStepMs = Ms;
		ProjectileGroundHits += GroundHits;
		ProjectileTtlExpired += TtlExpired;
	}
	double GetProjectileStepMs() const { return ProjectileStepMs; }
	int32 GetProjectileGroundHits() const { return ProjectileGroundHits; }
	int32 GetProjectileTtlExpired() const { return ProjectileTtlExpired; }

	/** Phase 3 (task 4): the fire pass cost + emission counters (game-thread
	 *  writer; Holds = cheap-eligible shooters that did not emit -- the
	 *  R-R6 rule-vs-bug discriminator). */
	void RecordRangedFire(double Ms, int32 Shots, int32 Holds)
	{
		RangedFireMs = Ms;
		RangedShots += Shots;
		RangedHolds += Holds;
	}
	double GetRangedFireMs() const { return RangedFireMs; }
	int32 GetRangedShots() const { return RangedShots; }
	int32 GetRangedHolds() const { return RangedHolds; }

	/** Phase 3 (task 5): impact counters (RC4 observability -- the friendly
	 *  half proves the team-blind impact-point rule). */
	void RecordRangedImpact() { ++RangedImpacts; }
	void RecordRangedFriendlyHit() { ++RangedFriendlyHits; }
	int32 GetRangedImpacts() const { return RangedImpacts; }
	int32 GetRangedFriendlyHits() const { return RangedFriendlyHits; }

	/** Phase 3 (task 7, T3.6): the in-flight peak, sampled by both ranged
	 *  passes each tick (game thread; the metric's home is here, the pool
	 *  stays pure data). */
	void RecordInFlight(int32 LiveCount) { InFlightPeak = FMath::Max(InFlightPeak, LiveCount); }
	int32 GetInFlightPeak() const { return InFlightPeak; }

	/** T3.6 probe (the T2.9 pattern): dump the ranged perf verdict at N ticks
	 *  after arming; disarms after the dump. */
	void ArmRangedPerfProbe(int32 InTicks) { RangedPerfProbeTicks = InTicks; }

	/** T2.9 perf probe: dump the metrics line at N ticks after arming (the
	 *  Exec fixtures run before the sim advances; the probe samples the
	 *  steady state). Disarms after the dump. */
	void ArmPerfProbe(int32 InTicks) { PerfProbeTicks = InTicks; }

	/** B.2 combat overlay row (task 10): engagement-record debug (mode +
	 *  pressure vs assigned), queued as a colored record line. */
	void EnqueueEngagementLine(FUnitHandle Attacker, FUnitHandle Defender, int32 Mode,
		int32 DesiredPressure, int32 AssignedAttackers);
	bool IsOverlayShown() const;

	/** Task 10 debug pass: corpse ledger. MassBattle corpses are destroyed
	 *  right after the death signal (ADR-15 path), so the classic MassRTS
	 *  ground rectangle needs a debug-side ledger of the LAST transform,
	 *  captured here at the death signal (game thread, entity still alive).
	 *  Capped FIFO -- diagnostic state, never sim-authoritative. */
	void RecordCorpse(const FVector& GroundPosCm, float YawDeg);
	int32 GetCorpseCount() const { return CorpseMarks.Num(); }

	// -- Phase 2 combat diagnostics (task 7; worker-writers, atomics) --
	void RecordCombatRoll(bool bLanded) { ++CombatRolls; if (bLanded) { ++CombatRollsLanded; } }
	int32 GetCombatRolls() const { return CombatRolls; }
	int32 GetCombatRollsLanded() const { return CombatRollsLanded; }
	/** Death-by-arc bookkeeping (EC6): the defender-relative sector bucket. */
	void RecordDeathByArc(int32 Sector, int32 SectorCount);
	int32 GetDeathsByArc(int32 Band) const { return Band >= 0 && Band < 3 ? DeathsByArc[Band] : -1; } // 0 frontal 1 flank 2 rear
	void RecordChargeConnect() { ++ChargeConnects; }
	int32 GetChargeConnects() const { return ChargeConnects; }
	/** One-shot knockback impulses applied to victim transforms (assign pass). */
	void RecordKnockback() { ++KnockbacksApplied; }
	int32 GetKnockbacksApplied() const { return KnockbacksApplied; }

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

	/** Audit M1: ladder step-4 handoffs (unit-AI escalation signal). */
	void RecordUnitAIHandoff() { ++UnitAIHandoffs; }
	int32 GetUnitAIHandoffs() const { return UnitAIHandoffs; }

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

	/** Phase 2 combat pass costs (B.4; DumpMetrics line). */
	double CombatHashMs = 0.0;
	double TallyMs = 0.0;
	double AssignMs = 0.0;
	int32 PerfProbeTicks = -1;

	/** Phase 3 ranged pass costs + release causes (game thread only). */
	double ProjectileStepMs = 0.0;
	int32 ProjectileGroundHits = 0;
	int32 ProjectileTtlExpired = 0;
	double RangedFireMs = 0.0;
	int32 RangedShots = 0;
	int32 RangedHolds = 0;
	int32 RangedImpacts = 0;
	int32 RangedFriendlyHits = 0;
	int32 InFlightPeak = 0;
	int32 RangedPerfProbeTicks = -1;

	/** Task 10 corpse ledger (capped FIFO; the overlay draws yaw-aligned
	 *  rectangles). Game thread only (the death signal receiver). */
	struct FCorpseMark
	{
		FVector PosCm = FVector::ZeroVector;
		float YawDeg = 0.f;
	};
	TArray<FCorpseMark> CorpseMarks;
	std::atomic<int32> CombatRolls{0};
	std::atomic<int32> CombatRollsLanded{0};
	std::atomic<int32> ChargeConnects{0};
	std::atomic<int32> KnockbacksApplied{0};
	int32 DeathsByArc[3] = {}; // guarded by ArcMutex (worker writers)
	FCriticalSection ArcMutex;

	/** T1.5/T1.8 diagnostics; seq_cst std::atomic (workers write; the extra
	 *  ordering is irrelevant for a diagnostic counter but harmless). */
	std::atomic<int32> LadderEscalations{0};

	/** Morph decisions (game-thread writer; see RecordMorphDecision). */
	int32 MorphDecisions = 0;
	/** Ladder step-4 handoffs (worker writer via servo -- atomic). */
	std::atomic<int32> UnitAIHandoffs{0};

	// Garrison metric accumulators (B.4 instrumented set, game thread only).
	int32 GarrisonFilings = 0;
	int32 GarrisonFilingTicksTotal = 0;
	int32 GarrisonQueueWaits = 0;
	int32 GarrisonQueueWaitTicksTotal = 0;

	FCriticalSection OverlayMutex;
	TArray<FBattleOverlayLine> PendingOverlayLines;
};
