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
	MASSBATTLE_API void DumpRegistry(const TArray<FString>& Args, UWorld* World);
	MASSBATTLE_API void StateHash(const TArray<FString>& Args, UWorld* World);

	// -- Task 6 (spawn path) --
	MASSBATTLE_API void SpawnRoster(const TArray<FString>& Args, UWorld* World);
	MASSBATTLE_API void BuildT0Configs(const TArray<FString>& Args, UWorld* World);

	// -- Task 12 (debug metrics) --
	MASSBATTLE_API void DumpMetrics(const TArray<FString>& Args, UWorld* World);

	// -- Task 8/9 (sync + death probes) --
	MASSBATTLE_API void SetAttr(const TArray<FString>& Args, UWorld* World);
	MASSBATTLE_API void KillEntity(const TArray<FString>& Args, UWorld* World);
	MASSBATTLE_API void DumpSharedFrag(const TArray<FString>& Args, UWorld* World);

	// -- Task 10 (RNG discipline probe; restored by the audit fix pass) --
	MASSBATTLE_API void RngSelfTest(const TArray<FString>& Args, UWorld* World);

	// -- Phase 1 Task 3 (A5 cost-grid stamp override; meters) --
	MASSBATTLE_API void StampImpassable(const TArray<FString>& Args, UWorld* World);

	// -- Phase 1 Task 10 (A2 command surface) --
	MASSBATTLE_API void OrderMove(const TArray<FString>& Args, UWorld* World);
	MASSBATTLE_API void OrderStop(const TArray<FString>& Args, UWorld* World);
}
