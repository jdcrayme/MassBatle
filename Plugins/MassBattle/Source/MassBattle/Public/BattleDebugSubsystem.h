// BattleDebugSubsystem.h -- debug overlay skeleton (roadmap B.2, slimmed per
// BRIEF §3.1 A1: HOOKS ONLY in Phase 0 -- CVars, dump endpoints, the B.4
// metrics format, MassInsights wiring note; all on-screen drawing arrives in
// Phase 2 where the first drawable state exists).
#pragma once

#include "CoreMinimal.h"
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

UCLASS()
class MASSBATTLE_API UBattleDebugSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Called by UnitSync (thin shim) to record its per-frame cost. */
	void RecordUnitSyncMs(double Ms);

	/** B.4 metrics line, EXEC_RESULT-formatted, for log-based evidence. */
	void DumpMetrics();

	const FBattleFrameMetrics& GetLastMetrics() const { return LastMetrics; }

private:
	FBattleFrameMetrics LastMetrics;
};
