// BattleExecCommands.h -- implementations behind the Battle.* console commands
// (BRIEF §4.11 ExecBridge pattern). Every command emits at least one
// "EXEC_RESULT <Name> ..." line: the MCP/log-based evidence channel.
// Commands must be safe in the editor world (structure-only probes); anything
// needing sim ticks runs as a functional test instead.
#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace BattleExec
{
	// -- Task 4 (registry probes) --
	void DumpRegistry(const TArray<FString>& Args, UWorld* World);
	void StateHash(const TArray<FString>& Args, UWorld* World);

	// -- Task 6 (spawn path) --
	void SpawnRoster(const TArray<FString>& Args, UWorld* World);
	void BuildT0Configs(const TArray<FString>& Args, UWorld* World);

	// -- Task 8/9 (sync + death probes) --
	void SetAttr(const TArray<FString>& Args, UWorld* World);
	void KillEntity(const TArray<FString>& Args, UWorld* World);
	void DumpSharedFrag(const TArray<FString>& Args, UWorld* World);
}
