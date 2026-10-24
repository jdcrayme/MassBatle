import io

APPEND = '''
// ---------------------------------------------------------------------------
// S0_DeathPath (T0.5)
// ---------------------------------------------------------------------------

AUnitDeathPathTest::AUnitDeathPathTest()
{
	TimeLimit = 30.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitDeathPathTest::BeginTest()
{
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath BeginTest running=1"));
}

void AUnitDeathPathTest::StartTest()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		CompleteTest(EFunctionalTestResult::Error, TEXT("no world"));
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Unit = World->SpawnActor<AUnitActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Unit->EduRowIndex = 1; // Sergeant Spearmen
	Unit->SoldierCount = 8;
	FBattleSpawnResult SpawnResult;
	FString Error;
	if (!Unit || !Unit->SpawnEntities(SpawnResult, Error))
	{
		CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
		return;
	}
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Spawned Unit=%u:%u Entities=%d"),
	       SpawnResult.Unit.Index, SpawnResult.Unit.Serial, SpawnResult.NumEntities);
}

void AUnitDeathPathTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	FMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !ES || !Unit)
	{
		return;
	}
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	const FUnitHandle Handle = Unit->GetUnitHandle();
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
	if (!Roster)
	{
		return;
	}

	if (!bKillRequested)
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

	if (KillTick == 1)
	{
		// T0.5 assertion point: one tick after the kill flush.
		const FMassEntityHandle DeadEntity = (*Roster)[5];
		const FMassEntityView View(EM, DeadEntity);

		bool bOk = true;
		// (a) sparse tag present -> removed from live queries (ADR-15; the
		// "counts 0 in queries with FDeadTag None" reading of this fact).
		if (!View.HasTag<FDeadTag>())
		{
			AddError(TEXT("T0.5 FAIL: dead entity lacks FDeadTag after the audited flush"));
			bOk = false;
		}
		// (b) roster updated by the signal receiver.
		if (Registry->GetRosterSize(Handle) != 7)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: roster size %d != 7"), Registry->GetRosterSize(Handle)));
			bOk = false;
		}
		// (c) GAS gameplay event observed on the unit actor.
		if (Unit->GetDeathEventCount() != 1)
		{
			AddError(FString::Printf(TEXT("T0.5 FAIL: death event count %d != 1"), Unit->GetDeathEventCount()));
			bOk = false;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_DeathPath Tick=%d Tagged=%d Roster=%d GAS=%d"),
		       KillTick, View.HasTag<FDeadTag>() ? 1 : 0, Registry->GetRosterSize(Handle), Unit->GetDeathEventCount());
		if (!bOk)
		{
			CompleteTest(EFunctionalTestResult::Error, TEXT("death path assertions failed"));
			return;
		}
	}

	if (KillTick >= 2)
	{
		// (d) queued destroy completed (corpse policy is Phase 5).
		const FMassEntityHandle DeadEntity = (*Roster)[5];
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
}

// ---------------------------------------------------------------------------
// S0_ExitCriteria (Task 13)
// ---------------------------------------------------------------------------

AUnitExitCriteriaTest::AUnitExitCriteriaTest()
{
	TimeLimit = 45.f;
	TimesUpResult = EFunctionalTestResult::Error;
}

void AUnitExitCriteriaTest::BeginTest()
{
	SetActorTickEnabled(true);
	PrimaryActorTick.bCanEverTick = true;
	bIsRunning = true;
	Result = EFunctionalTestResult::Running;
	UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria BeginTest"));
}

void AUnitExitCriteriaTest::StartTest()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;

	const int32 Rows[2] = {0, 1};
	AUnitActor* Targets[2] = {nullptr, nullptr};
	for (int32 i = 0; i < 2; ++i)
	{
		AUnitActor* Actor = World->SpawnActor<AUnitActor>(FVector(1000.f * (i + 1), 0, 100.f), FRotator::ZeroRotator, Params);
		Actor->EduRowIndex = Rows[i];
		Actor->SoldierCount = 40;
		FBattleSpawnResult SpawnResult;
		FString Error;
		if (!Actor || !Actor->SpawnEntities(SpawnResult, Error))
		{
			CompleteTest(EFunctionalTestResult::Error, FString::Printf(TEXT("spawn failed: %s"), *Error));
			return;
		}
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria Spawned Unit=%u:%u Row=%d Entities=%d"),
		       SpawnResult.Unit.Index, SpawnResult.Unit.Serial, Rows[i], SpawnResult.NumEntities);
		Targets[i] = Actor;
	}
	UnitA = Targets[0];
	UnitB = Targets[1];
	Phase = EPhase::Sync;
	TicksInPhase = 0;
}

void AUnitExitCriteriaTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	FMassEntitySubsystem* ES = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Registry || !ES || !UnitA || !UnitB)
	{
		return;
	}
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	++TicksInPhase;

	switch (Phase)
	{
	case EPhase::Sync:
	{
		if (TicksInPhase == 1)
		{
			// T0.4 re-assert: attribute change on A -> shared fragment in <= 1 frame.
			UnitA->GetAbilitySystemComponent()->SetNumericAttributeBase(UUnitAttributeSet::GetMeleeAttackModAttribute(), 0.5f);
			return;
		}
		if (TicksInPhase >= 2)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const FUnitHandle HB = UnitB->GetUnitHandle();
			const TArray<FMassEntityHandle>* RosterA = Registry->FindRoster(HA);
			const TArray<FMassEntityHandle>* RosterB = Registry->FindRoster(HB);
			const FUnitStatsSharedFragment* StatsA = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterA)[0]);
			const FUnitStatsSharedFragment* StatsB = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterB)[0]);
			bool bOk = StatsA && StatsB && FMath::IsNearlyEqual(StatsA->MeleeAttackMult, 1.5f, 1e-4f);
			bOk = bOk && FMath::IsNearlyEqual(StatsB->MeleeAttackMult, 1.0f, 1e-4f); // T0.8 re-assert
			if (!bOk)
			{
				AddError(FString::Printf(TEXT("exit-criteria sync FAIL: A=%.4f B=%.4f"),
					StatsA ? StatsA->MeleeAttackMult : -1.f, StatsB ? StatsB->MeleeAttackMult : -1.f));
				CompleteTest(EFunctionalTestResult::Error, TEXT("sync phase failed"));
				return;
			}
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria SyncTick=%d A=%.4f B=%.4f"),
			       TicksInPhase, StatsA->MeleeAttackMult, StatsB->MeleeAttackMult);
			HashBeforeDeath = FBattleStateHash::Compute(World);
			Phase = EPhase::Death;
			TicksInPhase = 0;
		}
		break;
	}
	case EPhase::Death:
	{
		if (TicksInPhase == 1)
		{
			// T0.5 re-assert: kill one entity of A.
			TArray<FString> Args;
			Args.Add(FString::Printf(TEXT("%u"), UnitA->GetUnitHandle().Index));
			Args.Add(TEXT("5"));
			BattleExec::KillEntity(Args, World);
			return;
		}
		if (TicksInPhase >= 2)
		{
			const FUnitHandle HA = UnitA->GetUnitHandle();
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(HA);
			const bool bRosterOk = (Registry->GetRosterSize(HA) == 39);
			const FMassEntityView DeadView(EM, (*Roster)[5]);
			const bool bTagged = DeadView.HasTag<FDeadTag>();
			const bool bGas = (UnitA->GetDeathEventCount() == 1);
			if (!bRosterOk || !bTagged || !bGas)
			{
				AddError(FString::Printf(TEXT("exit-criteria death FAIL: roster=%d tagged=%d gas=%d"),
					Registry->GetRosterSize(HA), bTagged ? 1 : 0, bGas ? 1 : 0));
				CompleteTest(EFunctionalTestResult::Error, TEXT("death phase failed"));
				return;
			}
			const uint64 HashAfterDeath = FBattleStateHash::Compute(World);
			UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT S0_ExitCriteria DeathTick=%d Roster=%d Tagged=1 GAS=1 HashBefore=%016llx HashAfter=%016llx"),
			       TicksInPhase, Registry->GetRosterSize(HA), HashBeforeDeath, HashAfterDeath);
			if (HashBeforeDeath == HashAfterDeath)
			{
				AddError(TEXT("state hash unchanged after a real state mutation"));
				CompleteTest(EFunctionalTestResult::Error, TEXT("hash phase failed"));
				return;
			}
			UnitA->Destroy();
			UnitB->Destroy();
			CompleteTest(EFunctionalTestResult::Succeeded, TEXT("exit criteria met"));
		}
		break;
	}
	default:
		break;
	}
}
'''

p = r"E:\Unreal Projects\MassBattle\Plugins\MassBattle\Source\MassBattle\Private\BattleFuncTestActor.cpp"
s = io.open(p, encoding="utf-8").read()
s += APPEND
# extra includes needed by the new code
s = s.replace('#include "MassBattleModule.h"',
              '#include "MassBattleModule.h"\n#include "BattleDeathPath.h"\n#include "BattleStateHash.h"')
io.open(p, "w", encoding="utf-8", newline="\n").write(s)
print("appended death + exit tests")
