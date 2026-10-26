#if WITH_DEV_AUTOMATION_TESTS
// Battle.Func.* harness -- bridges the S-scenario AFunctionalTest actors into
// the automation framework (roadmap B.0: functional tier runs headless via
// UnrealEditor-Cmd -game -ExecCmds="Automation RunTests Battle.Func"
// -TestExit="Automation Test Queue Empty"). ClientContext = the -game process.
// Pattern per engine FunctionalTesting/Tests/ClientFuncTestPerforming.cpp:
// complex automation test + latent commands.
#include "Misc/AutomationTest.h"

#include "BattleFuncTestActor.h"
#include "BattleTestSettings.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "FunctionalTestingModule.h"

namespace
{
	constexpr const TCHAR* FuncMapName = TEXT("L_MassBattle_T0_Functional");

	UWorld* GetAnyGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (UWorld* World = Context.World())
				{
					if (World->IsGameWorld() && !World->IsPlayInEditor())
					{
						return World;
					}
				}
			}
		}
		return nullptr;
	}

	/** Opens the functional map and polls until the game world reports it. */
	class FOpenBattleFuncMap : public IAutomationLatentCommand
	{
	public:
		FOpenBattleFuncMap(FAutomationTestBase* InOwner) : Owner(InOwner) {}

		virtual bool Update() override
		{
			UWorld* World = GetAnyGameWorld();
			if (!World)
			{
				return false;
			}
			if (!bOpened)
			{
				UGameplayStatics::OpenLevel(World, FuncMapName);
				bOpened = true;
				Deadline = FPlatformTime::Seconds() + 30.0;
				return false;
			}
			if (World->GetMapName().Contains(FuncMapName))
			{
				return true;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Owner->AddError(FString::Printf(TEXT("Timed out opening %s (current: %s)"), FuncMapName, *World->GetMapName()));
				return true;
			}
			return false;
		}

	private:
		FAutomationTestBase* Owner = nullptr;
		bool bOpened = false;
		double Deadline = 0.0;
	};

	/** Finds/spawns the named S-scenario actor class, begins it, polls completion. */
	class FRunBattleFuncTest : public IAutomationLatentCommand
	{
	public:
		FRunBattleFuncTest(FAutomationTestBase* InOwner, const TCHAR* InTestName, const TCHAR* InActorClassName)
			: Owner(InOwner), TestName(InTestName), ActorClassName(InActorClassName)
		{
		}

		virtual bool Update() override
		{
			UWorld* World = GetAnyGameWorld();
			if (!World || !World->GetMapName().Contains(FuncMapName))
			{
				return false; // wait for the map
			}
			if (!bStarted)
			{
				// Deterministic factory registry (reflection lookups proved
				// unreliable in -game processes -- see FBattleFuncTestRegistry).
				AFunctionalTest* Found = nullptr;
				for (TActorIterator<AFunctionalTest> It(World); It; ++It)
				{
					if (It->GetName().Contains(TestName))
					{
						Found = *It;
						break;
					}
				}
				if (Found)
				{
					TestActor = Found;
				}
				else
				{
					if (FBattleFuncTestRegistry::FFactory* Factory = FBattleFuncTestRegistry::Get().Find(ActorClassName))
					{
						FActorSpawnParameters Params;
						Params.ObjectFlags |= RF_Transient;
						TestActor = (*Factory)(World);
					}
				}
				if (!TestActor)
				{
					Owner->AddError(FString::Printf(TEXT("actor %s not resolvable"), *ActorClassName));
					return true;
				}
				if (!TestActor)
				{
					Owner->AddError(TEXT("could not spawn the functional test actor"));
					return true;
				}
				Deadline = FPlatformTime::Seconds() + 60.0;
				bStarted = true;
				// Manager-equivalent start via the subclass hook (PrepareTest is
				// protected; the native path leaves the test stopped when driven
				// outside the manager -- on-machine verified).
				if (AUnitSyncLatencyTest* Sync = Cast<AUnitSyncLatencyTest>(TestActor))
				{
					Sync->BeginTest();
				}
				else if (AUnitDeathPathTest* Death = Cast<AUnitDeathPathTest>(TestActor))
				{
					Death->BeginTest();
				}
				else if (AUnitExitCriteriaTest* Exit = Cast<AUnitExitCriteriaTest>(TestActor))
				{
					Exit->BeginTest();
				}
				else if (AUnitMarchWheelTest* S1 = Cast<AUnitMarchWheelTest>(TestActor))
				{
					S1->BeginTest();
				}
				else if (AUnitOrderSmokeTest* Smoke = Cast<AUnitOrderSmokeTest>(TestActor))
				{
					Smoke->BeginTest();
				}
				else if (AUnitPassThroughTest* S2 = Cast<AUnitPassThroughTest>(TestActor))
				{
					S2->BeginTest();
				}
				else if (AUnitSqueezeMorphTest* S3 = Cast<AUnitSqueezeMorphTest>(TestActor))
				{
					S3->BeginTest();
				}
				else if (AUnitWidthOverrideTest* S4 = Cast<AUnitWidthOverrideTest>(TestActor))
				{
					S4->BeginTest();
				}
				else if (AUnitMarchRefrontTest* S5 = Cast<AUnitMarchRefrontTest>(TestActor))
				{
					S5->BeginTest();
				}
				else if (AUnitInPlaceReformTest* S6 = Cast<AUnitInPlaceReformTest>(TestActor))
				{
					S6->BeginTest();
				}
				else if (AGarrisonScenarioBase* Garrison = Cast<AGarrisonScenarioBase>(TestActor))
				{
					Garrison->BeginTest();
				}
				else if (AUnitMultiUnitGarrisonTest* S8b = Cast<AUnitMultiUnitGarrisonTest>(TestActor))
				{
					S8b->BeginTest();
				}
				// T0.9: apply + assert the test-mode contract at scenario start
				// (roadmap B.0: assert ACTIVE engine settings, not CVars being
				// set; the -game process does not seed the contract on its own).
				// World is passed (audit M2) so the Mass-side probes run.
				UBattleTestSettings::ApplyTestMode();
				UBattleTestSettings::AssertTestMode(Owner, nullptr, World);
				return false;
			}

			if (TestActor->IsRunning())
			{
				if (FPlatformTime::Seconds() > Deadline)
				{
					Owner->AddError(FString::Printf(TEXT("%s timed out"), *TestName));
					TestActor->Result = EFunctionalTestResult::Error;
					TestActor->bIsRunning = false;
					return true;
				}
				return false; // keep polling
			}

			if (TestActor->Result != EFunctionalTestResult::Succeeded)
			{
				Owner->AddError(FString::Printf(TEXT("%s finished with result %d"),
					*TestName, static_cast<int32>(TestActor->Result)));
			}
			return true;
		}

	private:
		FAutomationTestBase* Owner = nullptr;
		FString TestName;
		FString ActorClassName;
		TObjectPtr<AFunctionalTest> TestActor;
		bool bStarted = false;
		double Deadline = 0.0;
	};

	/** Base for Battle.Func complex tests. */
	class FBattleFuncTestBase : public FAutomationTestBase
	{
	public:
		FBattleFuncTestBase(const FString& InName, const bool bInComplexTask)
			: FAutomationTestBase(InName, bInComplexTask)
		{
		}
		virtual EAutomationTestFlags GetTestFlags() const override
		{
			return EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
		}
		virtual FString GetTestSourceFileName() const override { return __FILE__; }
		virtual int32 GetTestSourceFileLine() const override { return __LINE__; }
	protected:
		virtual bool RunTest(const FString& Parameters) override = 0;
	};
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS0_SyncLatency, FBattleFuncTestBase,
	"Battle.Func.S0_SyncLatency", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS0_SyncLatency::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	// Fixed single-parameter test (the S0 scenario actor is spawned at runtime,
	// not authored on the map).
	OutBeautifiedNames.Add(TEXT("S0_SyncLatency"));
	OutTestCommands.Add(TEXT(""));
}

bool FS0_SyncLatency::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_SyncLatency"), TEXT("AUnitSyncLatencyTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS0_DeathPath, FBattleFuncTestBase,
	"Battle.Func.S0_DeathPath", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS0_DeathPath::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S0_DeathPath"));
	OutTestCommands.Add(TEXT(""));
}

bool FS0_DeathPath::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_DeathPath"), TEXT("AUnitDeathPathTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS0_ExitCriteria, FBattleFuncTestBase,
	"Battle.Func.S0_ExitCriteria", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS0_ExitCriteria::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S0_ExitCriteria"));
	OutTestCommands.Add(TEXT(""));
}

bool FS0_ExitCriteria::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_ExitCriteria"), TEXT("AUnitExitCriteriaTest")));
	return true;
}

// ---------------------------------------------------------------------------
// Phase 1 movement scenarios (brief §5 Tasks 10-11).
// ---------------------------------------------------------------------------

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FOrderSmoke, FBattleFuncTestBase,
	"Battle.Func.OrderSmoke", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FOrderSmoke::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("OrderSmoke"));
	OutTestCommands.Add(TEXT(""));
}

bool FOrderSmoke::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("OrderSmoke"), TEXT("AUnitOrderSmokeTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS1_MarchWheel, FBattleFuncTestBase,
	"Battle.Func.S1_MarchWheel", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS1_MarchWheel::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S1_MarchWheel"));
	OutTestCommands.Add(TEXT(""));
}

bool FS1_MarchWheel::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S1_MarchWheel"), TEXT("AUnitMarchWheelTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS2_PassThrough, FBattleFuncTestBase,
	"Battle.Func.S2_PassThrough", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS2_PassThrough::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S2_PassThrough"));
	OutTestCommands.Add(TEXT(""));
}

bool FS2_PassThrough::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S2_PassThrough"), TEXT("AUnitPassThroughTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS3_SqueezeMorph, FBattleFuncTestBase,
	"Battle.Func.S3_SqueezeMorph", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS3_SqueezeMorph::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S3_SqueezeMorph"));
	OutTestCommands.Add(TEXT(""));
}

bool FS3_SqueezeMorph::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S3_SqueezeMorph"), TEXT("AUnitSqueezeMorphTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS4_WidthOverride, FBattleFuncTestBase,
	"Battle.Func.S4_WidthOverride", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS4_WidthOverride::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S4_WidthOverride"));
	OutTestCommands.Add(TEXT(""));
}

bool FS4_WidthOverride::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S4_WidthOverride"), TEXT("AUnitWidthOverrideTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS5_MarchRefront, FBattleFuncTestBase,
	"Battle.Func.S5_MarchRefront", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS5_MarchRefront::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S5_MarchRefront"));
	OutTestCommands.Add(TEXT(""));
}

bool FS5_MarchRefront::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S5_MarchRefront"), TEXT("AUnitMarchRefrontTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS6_InPlaceReform, FBattleFuncTestBase,
	"Battle.Func.S6_InPlaceReform", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS6_InPlaceReform::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S6_InPlaceReform"));
	OutTestCommands.Add(TEXT(""));
}

bool FS6_InPlaceReform::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S6_InPlaceReform"), TEXT("AUnitInPlaceReformTest")));
	return true;
}

// ---------------------------------------------------------------------------
// Phase-garrison func gates TG.6-TG.9 (brief §6): the real scenario plumbing.
// Actors are found on the map by name or spawned through the deterministic
// factory registry (phase-0 pattern, same as every S-scenario above).
// ---------------------------------------------------------------------------

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS8_BuildingGarrison, FBattleFuncTestBase,
	"Battle.Func.S8_BuildingGarrison", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS8_BuildingGarrison::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S8_BuildingGarrison"));
	OutTestCommands.Add(TEXT(""));
}

bool FS8_BuildingGarrison::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S8_BuildingGarrison"), TEXT("AUnitBuildingGarrisonTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS9_TrenchGarrison, FBattleFuncTestBase,
	"Battle.Func.S9_TrenchGarrison", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS9_TrenchGarrison::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S9_TrenchGarrison"));
	OutTestCommands.Add(TEXT(""));
}

bool FS9_TrenchGarrison::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S9_TrenchGarrison"), TEXT("AUnitTrenchGarrisonTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS10_WallGarrison, FBattleFuncTestBase,
	"Battle.Func.S10_WallGarrison", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS10_WallGarrison::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S10_WallGarrison"));
	OutTestCommands.Add(TEXT(""));
}

bool FS10_WallGarrison::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S10_WallGarrison"), TEXT("AUnitWallGarrisonTest")));
	return true;
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FS8b_MultiUnitGarrison, FBattleFuncTestBase,
	"Battle.Func.S8b_MultiUnitGarrison", (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

void FS8b_MultiUnitGarrison::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("S8b_MultiUnitGarrison"));
	OutTestCommands.Add(TEXT(""));
}

bool FS8b_MultiUnitGarrison::RunTest(const FString& Parameters)
{
	IFunctionalTestingModule::Get().MarkPendingActivation();
	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S8b_MultiUnitGarrison"), TEXT("AUnitMultiUnitGarrisonTest")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
