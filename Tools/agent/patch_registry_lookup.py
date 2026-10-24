import io

# 1. registry header
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Public\BattleFuncTestActor.h"
s = io.open(p, encoding="utf-8").read()
s += '''
// ---------------------------------------------------------------------------
// S-scenario actor factory registry -- deterministic, reflection-free class
// lookup for the automation harness (FindObject with /Script/ paths and
// TObjectIterator both failed to resolve fresh C++ classes in -game processes
// on this machine; a static registry registered beside each actor definition
// cannot miss).
// ---------------------------------------------------------------------------
struct MASSBATTLE_API FBattleFuncTestRegistry
{
	using FFactory = AFunctionalTest* (*)(UWorld* World);

	static TMap<FString, FFactory>& Get()
	{
		static TMap<FString, FFactory> Map;
		return Map;
	}

	struct FRegistrar
	{
		FRegistrar(const FString& Name, FFactory Factory)
		{
			Get().Add(Name, Factory);
		}
	};
};
'''
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

# 2. actor cpp: register factories beside each actor
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleFuncTestActor.cpp"
s = io.open(p, encoding="utf-8").read()
s += '''
// ---------------------------------------------------------------------------
// Factory registrations (see FBattleFuncTestRegistry)
// ---------------------------------------------------------------------------
namespace
{
	AFunctionalTest* MakeSyncLatencyTest(UWorld* World)
	{
		return World->SpawnActor<AUnitSyncLatencyTest>();
	}
	AFunctionalTest* MakeDeathPathTest(UWorld* World)
	{
		return World->SpawnActor<AUnitDeathPathTest>();
	}
	AFunctionalTest* MakeExitCriteriaTest(UWorld* World)
	{
		return World->SpawnActor<AUnitExitCriteriaTest>();
	}

	const FBattleFuncTestRegistry::FRegistrar GRegSync(TEXT("AUnitSyncLatencyTest"), &MakeSyncLatencyTest);
	const FBattleFuncTestRegistry::FRegistrar GRegDeath(TEXT("AUnitDeathPathTest"), &MakeDeathPathTest);
	const FBattleFuncTestRegistry::FRegistrar GRegExit(TEXT("AUnitExitCriteriaTest"), &MakeExitCriteriaTest);
}
'''
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

# 3. harness: use the registry instead of reflection
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\MassBattleFuncTestHarness.cpp"
s = io.open(p, encoding="utf-8").read()
old = """				// Gauntlet-style class lookup: FindObject with /Script/ paths is
				// unreliable in -game processes (on-machine verified); iterate.
				UClass* ActorClass = nullptr;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->GetName() == ActorClassName && It->IsChildOf<AFunctionalTest>())
					{
						ActorClass = *It;
						break;
					}
				}
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
				}"""
new = """				// Deterministic factory registry (reflection lookups proved
				// unreliable in -game processes -- see FBattleFuncTestRegistry).
				FAutomationTestBase* Found = nullptr;
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
					TestActor = Cast<AFunctionalTest>(Found);
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
				}"""
assert old in s, "lookup block not found"
s = s.replace(old, new)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("registry lookup wired")
