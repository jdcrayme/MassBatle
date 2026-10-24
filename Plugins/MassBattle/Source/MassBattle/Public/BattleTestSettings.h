// BattleTestSettings.h -- the test-mode contract (roadmap B.0, T0.9).
// One switch, grouped as Battle.Test.* CVars, asserted against the engine's
// ACTIVE settings (never against the CVars merely being set):
//   Battle.Test.FixedStep          1 -> FApp fixed time step + 1/60 delta
//   Battle.Test.DisableTimeSlicing 1 -> asserts no processor time-slicing
//                                     (5.8 has NO global cvar: FExecutionLimiter
//                                     is opt-in per call, audit §11 -- the
//                                     assertion is that the contract cvar is on
//                                     and no Battle processor constructs a limiter)
//   Battle.Test.ForceFullDetail    1 -> asserts variable-tick scheduling off
//                                     (no MassLOD trait on battle entities;
//                                     UMassLODTrait::bEnableVariableTicking
//                                     defaults false and Phase 0 never adds it)
#pragma once

#include "CoreMinimal.h"
#include "BattleTestSettings.generated.h"

class FAutomationTestBase;

UCLASS()
class MASSBATTLE_API UBattleTestSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Check the ACTIVE engine state against the test-mode contract. Violations
	 *  are AddError'd on Test (when non-null) and always returned via
	 *  OutViolations. Returns true when clean. */
	static bool AssertTestMode(FAutomationTestBase* Test = nullptr, TArray<FString>* OutViolations = nullptr);

	/** Apply the contract (called by CI launch scripts / test setup): fixed
	 *  step on at 1/60, contract CVars on. Returns true when applied. */
	static bool ApplyTestMode();
};
