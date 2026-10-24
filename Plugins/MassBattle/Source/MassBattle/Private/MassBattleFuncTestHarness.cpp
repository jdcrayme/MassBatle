// Battle.Func.* harness -- bridges the S-scenario AFunctionalTest actors into
// the automation framework (roadmap B.0: functional tier runs headless via
// UnrealEditor-Cmd -game -ExecCmds="Automation RunTests Battle.Func"
// -TestExit="Automation Test Queue Empty"). ClientContext = the -game process.
// Pattern per engine FunctionalTesting/Tests/ClientFuncTestPerforming.cpp:
// complex automation test + latent commands.
#include "Misc/AutomationTest.h"

#include "BattleFuncTestActor.h"
#include "EngineUtils.h"
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

	/** Finds/spawns the named AFunctionalTest actor, starts it, polls completion. */
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
	ADD_LATENT_AUTOMATION_COMMAND(FRunBattleFuncTest(this, TEXT("S0_SyncLatency")));
	return true;
}
