import io

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\MassBattleFuncTestHarness.cpp"
s = io.open(p, encoding="utf-8").read()

# 1. parameterize the existing S0_SyncLatency runner call
old2 = """	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_SyncLatency")));
	return true;
}"""
new2 = """	ADD_LATENT_AUTOMATION_COMMAND(FOpenBattleFuncMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_SyncLatency"), TEXT("AUnitSyncLatencyTest")));
	return true;
}"""
assert old2 in s, "sync run call not found"
s = s.replace(old2, new2)

# 2. generalize the runner class
old3 = """	/** Finds/spawns the named AFunctionalTest actor, starts it, polls completion. */
	class FRunBattleFuncTest : public IAutomationLatentCommand
	{
	public:
		FRunBattleFuncTest(FAutomationTestBase* InOwner, const TCHAR* InTestActorName)
			: Owner(InOwner), TestActorName(InTestActorName)
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
				for (AUnitSyncLatencyTest* Test : TActorRange<AUnitSyncLatencyTest>(World))
				{
					TestActor = Test;
					break;
				}
				if (!TestActor)
				{
					FActorSpawnParameters Params;
					Params.ObjectFlags |= RF_Transient;
					TestActor = World->SpawnActor<AUnitSyncLatencyTest>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
				}
				if (!TestActor)
				{
					Owner->AddError(TEXT("could not spawn the functional test actor"));
					return true;
				}
				Deadline = FPlatformTime::Seconds() + 45.0;
				bStarted = true;
				// The FunctionalTestingManager entry point is PrepareTest(): it
				// sets bIsRunning + enables ticking; Tick then calls StartTest
				// once IsReady. Calling StartTest directly leaves the actor
				// unticked (on-machine verified both ways).
				TestActor->BeginTest();
				return false;
			}

			if (TestActor->IsRunning())
			{
				if (FPlatformTime::Seconds() > Deadline)
				{
					Owner->AddError(FString::Printf(TEXT("%s timed out"), *TestActorName));
					TestActor->CompleteTest(EFunctionalTestResult::Error, TEXT("harness timeout"));
					return true;
				}
				return false; // keep polling
			}

			if (TestActor->Result != EFunctionalTestResult::Succeeded)
			{
				Owner->AddError(FString::Printf(TEXT("%s finished with result %d"),
					*TestActorName, static_cast<int32>(TestActor->Result)));
			}
			return true;
		}

	private:
		FAutomationTestBase* Owner = nullptr;
		FString TestActorName;
		TObjectPtr<AUnitSyncLatencyTest> TestActor;
		bool bStarted = false;
		double Deadline = 0.0;
	};"""
new3 = """	/** Finds/spawns the named S-scenario actor class, begins it, polls completion. */
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
				UClass* ActorClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MassBattle.%s"), *ActorClassName));
				if (!ActorClass)
				{
					Owner->AddError(FString::Printf(TEXT("actor class %s not found"), *ActorClassName));
					return true;
				}
				AFunctionalTest* Found = nullptr;
				for (TActorIterator<AFunctionalTest> It(World); It; ++It)
				{
					if (It->GetClass() == ActorClass)
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
					FActorSpawnParameters Params;
					Params.ObjectFlags |= RF_Transient;
					TestActor = World->SpawnActor<AFunctionalTest>(ActorClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
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
				// T0.9: assert the test-mode contract at scenario start (roadmap
				// B.0: assert ACTIVE engine settings, not CVars being set).
				UBattleTestSettings::AssertTestMode(*Owner);
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
	};"""
assert old3 in s, "runner class not found"
s = s.replace(old3, new3)

# 3. includes for the new actor types + test settings
s = s.replace('#include "BattleFuncTestActor.h"',
              '#include "BattleFuncTestActor.h"\n#include "BattleTestSettings.h"')

# 4. append the two new automation registrations
s += '''
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
'''

io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("harness generalized + 3 tests registered")
