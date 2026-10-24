import io

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleFuncTestActor.cpp"
s = io.open(p, encoding="utf-8").read()

# Capture the dead entity handle at kill time; tag-check on the handle at
# tick 1; destroy-proof on the handle from tick 3.
old = """	if (!bKillRequested)
	{
		// Let the world settle one tick after spawn, then kill roster[5] via
		// the KillEntity bridge (deferred sparse tag + explicit flush at the
		// E5-audited flush point: the death observer fires during this flush).
		if (GetWorld()->GetTimeSeconds() < 0.25)
		{
			return;
		}
		bKillRequested = true;
		TArray<FString> Args;
		Args.Add(FString::Printf(TEXT("%u"), Handle.Index));
		Args.Add(TEXT("5"));
		BattleExec::KillEntity(Args, World);
		KillTick = 0;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Kill requested at tick 0"));
		return;
	}

	++KillTick;

	// T0.5 timing (scan-path, E5 contract): the kill flush lands the sparse tag
	// immediately; the DeadScan->signal->receiver chain completes within the
	// next tick; the receiver-deferred destroy flushes at that phase's end.
	const FMassEntityHandle DeadEntity = (*Roster)[5];
	const FMassEntityView DeadView(EM, DeadEntity);

	if (KillTick == 1)
	{
		// (a) sparse tag present -> removed from live queries (ADR-15; the
		// "counts 0 in queries with FDeadTag None" reading of this fact).
		if (!DeadView.HasTag<FDeadTag>())
		{
			AddError(TEXT("T0.5 FAIL: dead entity lacks FDeadTag after the audited flush"));
			CompleteTest(EFunctionalTestResult::Error, TEXT("tag missing"));
			return;
		}
	}

	if (KillTick >= 2)
	{
		// (b)+(c) roster updated + GAS event observed by now.
		const int32 RosterSize = Registry->GetRosterSize(Handle);
		const int32 GasCount = Unit->GetDeathEventCount();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Tick=%d Tagged=%d Roster=%d GAS=%d"),
		       KillTick, DeadView.HasTag<FDeadTag>() ? 1 : 0, RosterSize, GasCount);
		if (RosterSize != 7 || GasCount != 1)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: roster=%d gas=%d (want 7/1)"), RosterSize, GasCount));
			CompleteTest(EFunctionalTestResult::Error, TEXT("roster/GAS failed"));
			return;
		}
	}

	if (KillTick >= 3)
	{
		// (d) queued destroy completed (corpse policy is Phase 5).
		const bool bStillValid = DeadEntity.IsValid() && EM.IsEntityValid(DeadEntity);
		if (bStillValid)
		{
			AddError(TEXT("T0.5 FAIL: deferred destroy did not complete"));
			CompleteTest(EFunctionalTestResult::Error, TEXT("destroy pending"));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Destroyed=1 at tick %d"), KillTick);
		Unit->Destroy();
		CompleteTest(EFunctionalTestResult::Succeeded, TEXT("death path complete"));
	}

	if (KillTick > 4)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("death path stalled"));
	}
}"""
new = """	if (!bKillRequested)
	{
		// Let the world settle one tick after spawn, then kill roster[5] via
		// the KillEntity bridge (deferred tag + explicit flush at the E5-audited
		// flush point: the death observer fires during this flush).
		if (GetWorld()->GetTimeSeconds() < 0.25)
		{
			return;
		}
		bKillRequested = true;
		DeadEntityHandle = (*Roster)[5]; // capture BEFORE the roster mutates
		TArray<FString> Args;
		Args.Add(FString::Printf(TEXT("%u"), Handle.Index));
		Args.Add(TEXT("5"));
		BattleExec::KillEntity(Args, World);
		KillTick = 0;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Kill requested at tick 0 Entity=%u:%u"),
		       DeadEntityHandle.Index, DeadEntityHandle.SerialNumber);
		return;
	}

	++KillTick;

	// T0.5 timing (observer path, E5 contract): the kill flush fires the
	// observer immediately; the signal -> receiver chain completes within the
	// next tick; the receiver-deferred destroy flushes at that phase's end.
	const FMassEntityView DeadView(EM, DeadEntityHandle);

	if (KillTick == 1)
	{
		// (a) tag present on the captured handle -> removed from live queries
		// (regular tag per ADR-15 final state: archetype flip, entity gone from
		// live query results).
		if (!DeadView.HasTag<FDeadTag>())
		{
			AddError(TEXT("T0.5 FAIL: dead entity lacks FDeadTag after the audited flush"));
			CompleteTest(EFunctionalTestResult::Error, TEXT("tag missing"));
			return;
		}
	}

	if (KillTick >= 2)
	{
		// (b)+(c) roster updated + GAS event observed by now.
		const int32 RosterSize = Registry->GetRosterSize(Handle);
		const int32 GasCount = Unit->GetDeathEventCount();
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Tick=%d Tagged=%d Roster=%d GAS=%d"),
		       KillTick, DeadView.HasTag<FDeadTag>() ? 1 : 0, RosterSize, GasCount);
		if (RosterSize != 7 || GasCount != 1)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: roster=%d gas=%d (want 7/1)"), RosterSize, GasCount));
			CompleteTest(EFunctionalTestResult::Error, TEXT("roster/GAS failed"));
			return;
		}
	}

	if (KillTick >= 3)
	{
		// (d) queued destroy completed (corpse policy is Phase 5).
		const bool bStillValid = DeadEntityHandle.IsValid() && EM.IsEntityValid(DeadEntityHandle);
		if (bStillValid)
		{
			AddError(TEXT("T0.5 FAIL: deferred destroy did not complete"));
			CompleteTest(EFunctionalTestResult::Error, TEXT("destroy pending"));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Destroyed=1 at tick %d"), KillTick);
		Unit->Destroy();
		CompleteTest(EFunctionalTestResult::Succeeded, TEXT("death path complete"));
	}

	if (KillTick > 4)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("death path stalled"));
	}
}"""
assert old in s, "death tick block not found"
s = s.replace(old, new)

# same capture for the exit-criteria death phase
old2 = """		if (TicksInPhase == 1)
		{
			// T0.5 re-assert: kill one entity of A.
			TArray<FString> Args;
			Args.Add(FString::Printf(TEXT("%u"), UnitA->GetUnitHandle().Index));
			Args.Add(TEXT("5"));
			BattleExec::KillEntity(Args, World);
			return;
		}
		if (TicksInPhase >= 3)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(HA);
			const bool bRosterOk = (Registry->GetRosterSize(HA) == 39);
			const FMassEntityView DeadView(EM, (*Roster)[5]);
			const bool bTagged = DeadView.HasTag<FDeadTag>();
			const bool bGas = (UnitA->GetDeathEventCount() == 1);"""
new2 = """		if (TicksInPhase == 1)
		{
			// T0.5 re-assert: kill one entity of A (capture the handle first).
			const FUnitHandle HA0 = UnitA->GetUnitHandle();
			DeadEntityHandle = (*Registry->FindRoster(HA0))[5];
			TArray<FString> Args;
			Args.Add(FString::Printf(TEXT("%u"), HA0.Index));
			Args.Add(TEXT("5"));
			BattleExec::KillEntity(Args, World);
			return;
		}
		if (TicksInPhase >= 3)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const bool bRosterOk = (Registry->GetRosterSize(HA) == 39);
			const FMassEntityView DeadView(EM, DeadEntityHandle);
			const bool bTagged = DeadView.HasTag<FDeadTag>();
			const bool bGas = (UnitA->GetDeathEventCount() == 1);"""
assert old2 in s, "exit death block not found"
s = s.replace(old2, new2)
io.open(p, "w", encoding="utf-8", newline="\n").write(s)

# header: DeadEntityHandle members
p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Public\BattleFuncTestActor.h"
s = io.open(p, encoding="utf-8").read()
s = s.replace("""	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	int32 KillTick = -1;
	bool bKilled = false;
	bool bKillRequested = false;""",
"""	UPROPERTY() TObjectPtr<AUnitActor> Unit;
	FMassEntityHandle DeadEntityHandle;
	int32 KillTick = -1;
	bool bKilled = false;
	bool bKillRequested = false;""")
s = s.replace("""	enum class EPhase : int32 { Spawn, Sync, Death, VerifyHash, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	uint64 HashBeforeDeath = 0;""",
"""	enum class EPhase : int32 { Spawn, Sync, Death, VerifyHash, Done };
	EPhase Phase = EPhase::Spawn;
	int32 TicksInPhase = 0;
	uint64 HashBeforeDeath = 0;
	FMassEntityHandle DeadEntityHandle;""")
s = s.replace('#include "FunctionalTest.h"', '#include "FunctionalTest.h"\n#include "MassEntityHandle.h"')
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("death capture fixed")
