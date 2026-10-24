#include "BattleTestSettings.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassLODFragments.h" // FMassVariableTickChunkFragment (MassLOD, audit M2 probe)

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

bool UBattleTestSettings::AssertTestMode(FAutomationTestBase* Test, TArray<FString>* OutViolations, UWorld* World)
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

	// 4. Engine-side Mass state (audit M2: read ACTIVE settings, not contract
	// cvars). Requires a world; callers that have one pass it (S-scenario
	// harness, T0.9 unit test).
	if (World)
	{
		// Simulation ticking must be on under test -- the off value silently
		// freezes the processor pipeline and makes tick-counted asserts lie.
		if (const IConsoleVariable* SimTicking = IConsoleManager::Get().FindConsoleVariable(TEXT("mass.SimulationTickingEnabled")))
		{
			if (SimTicking->GetInt() == 0)
			{
				Fail(TEXT("mass.SimulationTickingEnabled is 0 in an active test world"));
			}
		}
		// Query parallelism is part of the audited scheduling state (audit §11):
		// chunk execution stays deterministic for pure reads while it is on --
		// pin it so a flipped default cannot silently change scheduling.
		if (const IConsoleVariable* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("mass.AllowQueryParallelFor")))
		{
			if (Parallel->GetInt() == 0)
			{
				Fail(TEXT("mass.AllowQueryParallelFor is 0 -- audited test-mode scheduling state changed"));
			}
		}
		// Variable tick off = no battle archetype may carry the variable-tick
		// chunk fragment (it is only added by UMassLODTrait, which Phase 0-1
		// battle entities never carry -- this probe fails if that changes).
		if (UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>())
		{
			FMassEntityManager& EM = EntitySubsystem->GetMutableEntityManager();
			FMassExecutionContext Ctx(EM);
			FMassEntityQuery VarTickQuery(Ctx.GetSharedEntityManager());
			VarTickQuery.AddChunkRequirement<FMassVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
			int32 VarTickEntities = 0;
			VarTickQuery.ForEachEntityChunk(Ctx, [&VarTickEntities](FMassExecutionContext& ChunkCtx)
			{
				VarTickEntities += ChunkCtx.GetNumEntities();
			});
			if (VarTickEntities > 0)
			{
				Fail(FString::Printf(TEXT("%d entities carry FMassVariableTickChunkFragment -- variable-tick scheduling active under test"), VarTickEntities));
			}
		}
	}

	return bClean;
}
