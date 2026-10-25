// Phase 0 / Task 1 (R8): Gauntlet smoke controller -- proves the packaged-nightly
// loop end-to-end (wait N seconds, assert no log errors, EndTest).
//
// Gating note (BRIEF §5 Task 1.5): UHT forbids UCLASS inside preprocessor blocks
// ("'UCLASS' must not be inside preprocessor blocks"), and the engine's own
// Gauntlet controllers (UGauntletTestControllerBootTest et al.) ship ungated in
// the Runtime Gauntlet module -- so the appropriate gate here is "none", exactly
// like the engine's. Gauntlet nightlies run Test/Development packaged builds;
// a Shipping build simply never passes -gauntlet= and the controller idles.
#pragma once

#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "BattleSmokeController.generated.h"

/** Counts log lines at Error/Fatal verbosity seen while attached to GLog. */
class FBattleSmokeErrorCollector : public FOutputDevice
{
public:
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal)
		{
			++ErrorCount;
		}
	}

	int32 ErrorCount = 0;
};

/**
 * Minimal Gauntlet controller: -gauntlet=BattleSmoke
 * Waits battlesmoke.wait=<seconds> (default 30), failing fast if any Error/Fatal
 * line is logged after init. Emits "GAUNTLET BattleSmoke" lines for grep-able
 * evidence in the nightly log, mirroring the project's EXEC_RESULT convention.
 */
UCLASS()
class UBattleSmokeController : public UGauntletTestController
{
	GENERATED_BODY()

public:
	virtual void OnInit() override;
	virtual void OnTick(float TimeDelta) override;

	virtual ~UBattleSmokeController() override;

private:
	void DetachCollector();
	void FinishTest(int32 ExitCode);

	float WaitSeconds = 30.0f;
	bool bEnded = false;
	FBattleSmokeErrorCollector ErrorCollector;
};
