// Battle.* console command implementations. Task 4 adds the registry probes;
// later tasks fill in their own functions and register their commands.
// Delegate signature note: FConsoleCommandWithWorldAndArgsDelegate is
// void(const TArray<FString>& Args, UWorld* World) -- args FIRST.
#include "BattleExecCommands.h"
#include "BattleRegistry.h"
#include "MassBattleModule.h"

#include "Engine/World.h"

namespace
{
	UUnitRegistrySubsystem* GetRegistry(UWorld* World)
	{
		return World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	}
}

namespace BattleExec
{
	void DumpRegistry(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = GetRegistry(World);
		if (!Registry)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT DumpRegistry FAIL no world/registry"));
			return;
		}

		const TArray<FUnitHandle> Handles = Registry->GetSortedHandles();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpRegistry Begin Units=%d FreeList=%d"),
		       Handles.Num(), Registry->GetFreeListSize());
		for (const FUnitHandle& Handle : Handles)
		{
			const FUnitRuntimeState* State = Registry->FindUnitState(Handle);
			const FUnitSpawnSpec* Spec = Registry->FindUnitSpec(Handle);
			const FBattleAttributeSnapshot* Attrs = Registry->FindCachedAttributes(Handle);
			UE_LOG(LogMassBattle, Display,
			       TEXT("EXEC_RESULT DumpRegistry Unit=%u:%u roster=%d dirty=%d row=%d name=%s morale=%.2f"),
			       Handle.Index, Handle.Serial,
			       State ? State->Roster.Num() : -1,
			       Registry->IsStatsDirty(Handle) ? 1 : 0,
			       Spec ? Spec->EduRowIndex : INDEX_NONE,
			       Spec ? *Spec->UnitName : TEXT("<none>"),
			       Attrs ? Attrs->Morale : 0.f);
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT DumpRegistry End"));
	}

	void StateHash(const TArray<FString>& Args, UWorld* World)
	{
		UUnitRegistrySubsystem* Registry = GetRegistry(World);
		if (!Registry)
		{
			UE_LOG(LogMassBattle, Error, TEXT("EXEC_RESULT StateHash FAIL no world/registry"));
			return;
		}
		const uint32 Hash = Registry->ComputeBookkeepingHash();
		// Task 10 replaces the placeholder with the full CityHash64 sim-state hash.
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT StateHash Hash=%08x Units=%d"),
		       Hash, Registry->GetUnitCount());
	}

	// -- Implemented by their owning tasks; stubs log a TODO until then. --

	void SpawnRoster(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT SpawnRoster FAIL not-implemented (Task 6)"));
	}

	void BuildT0Configs(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT BuildT0Configs FAIL not-implemented (Task 6)"));
	}

	void SetAttr(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT SetAttr FAIL not-implemented (Task 8)"));
	}

	void KillEntity(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT KillEntity FAIL not-implemented (Task 9)"));
	}

	void DumpSharedFrag(const TArray<FString>& Args, UWorld* World)
	{
		UE_LOG(LogMassBattle, Warning, TEXT("EXEC_RESULT DumpSharedFrag FAIL not-implemented (Task 6)"));
	}
}

// ---- Command registration (Task 4 portion) ----

static FAutoConsoleCommandWithWorldAndArgs GBattleDumpRegistry(
	TEXT("Battle.DumpRegistry"),
	TEXT("Dumps registry units + rosters; emits EXEC_RESULT DumpRegistry lines"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::DumpRegistry));

static FAutoConsoleCommandWithWorldAndArgs GBattleStateHash(
	TEXT("Battle.StateHash"),
	TEXT("Emits EXEC_RESULT StateHash with the registry bookkeeping hash"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BattleExec::StateHash));
