#include "BattleTestSettings.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"

namespace
{
	bool GetContractCvar(const TCHAR* Name)
	{
		const IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(Name);
		return Cvar && Cvar->GetInt() != 0;
	}

	void SetContractCvar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Cvar->Set(Value, ECVF_SetByCode);
		}
	}
}

static int32 BattleTestFixedStep = 0;
static int32 BattleTestDisableTimeSlicing = 0;
static int32 BattleTestForceFullDetail = 0;

static FAutoConsoleVariableRef GBattleTestFixedStep(
	TEXT("Battle.Test.FixedStep"),
	BattleTestFixedStep,
	TEXT("1 = test-mode contract: FApp fixed time step (1/60) asserted active (T0.9)"),
	ECVF_Default);

static FAutoConsoleVariableRef GBattleTestDisableTimeSlicing(
	TEXT("Battle.Test.DisableTimeSlicing"),
	BattleTestDisableTimeSlicing,
	TEXT("1 = test-mode contract: processor time-slicing must be off (5.8: FExecutionLimiter is opt-in per call; Battle processors never construct one)"),
	ECVF_Default);

static FAutoConsoleVariableRef GBattleTestForceFullDetail(
	TEXT("Battle.Test.ForceFullDetail"),
	BattleTestForceFullDetail,
	TEXT("1 = test-mode contract: SimLOD variable-tick off (Phase 0 entities carry no MassLOD trait)"),
	ECVF_Default);

bool UBattleTestSettings::ApplyTestMode()
{
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / 60.0); // audit C4: SetFixedTimeStepValue does not exist in 5.8
	SetContractCvar(TEXT("Battle.Test.FixedStep"), 1);
	SetContractCvar(TEXT("Battle.Test.DisableTimeSlicing"), 1);
	SetContractCvar(TEXT("Battle.Test.ForceFullDetail"), 1);
	return true;
}

bool UBattleTestSettings::AssertTestMode(FAutomationTestBase* Test, TArray<FString>* OutViolations)
{
	bool bClean = true;
	const auto Fail = [Test, OutViolations, &bClean](const FString& What)
	{
		const FString Line = TEXT("T0.9 test-mode violation: ") + What;
		if (Test)
		{
			Test->AddError(Line);
		}
		if (OutViolations)
		{
			OutViolations->Add(Line);
		}
		bClean = false;
	};

	// 1. Fixed step: read the ACTIVE FApp state (not the CVars).
	if (GetContractCvar(TEXT("Battle.Test.FixedStep")))
	{
		if (!FApp::UseFixedTimeStep())
		{
			Fail(TEXT("Battle.Test.FixedStep is on but FApp::UseFixedTimeStep() is false"));
		}
		const double Expected = 1.0 / 60.0;
		if (FMath::Abs(FApp::GetFixedDeltaTime() - Expected) > 1e-6)
		{
			Fail(FString::Printf(TEXT("fixed delta %.6f != 1/60"), FApp::GetFixedDeltaTime()));
		}
	}

	// 2. Time slicing: 5.8 has no global toggle (audit §11). The contract cvar
	// must be set, and the structural guarantee is compile-time: Battle
	// processors never construct UE::Mass::FExecutionLimiter (reviewed per B.0).
	if (!GetContractCvar(TEXT("Battle.Test.DisableTimeSlicing")))
	{
		Fail(TEXT("Battle.Test.DisableTimeSlicing is not set"));
	}

	// 3. Full detail: contract cvar on; Phase 0 entities carry no MassLOD trait
	// (variable tick defaults off engine-side, UMassLODTrait::bEnableVariableTicking).
	if (!GetContractCvar(TEXT("Battle.Test.ForceFullDetail")))
	{
		Fail(TEXT("Battle.Test.ForceFullDetail is not set"));
	}

	return bClean;
}
