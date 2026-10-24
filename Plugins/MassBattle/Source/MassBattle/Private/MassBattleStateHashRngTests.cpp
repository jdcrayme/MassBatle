// Battle.Unit.StateHash.* / Battle.Unit.Rng.* / Battle.Unit.TestMode.* --
// T0.6 (state-hash stability), T0.7 (RNG registry discipline), T0.9
// (test-mode contract assertion + violation detection).
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "BattleFragments.h"
#include "Misc/App.h"
#include "HAL/IConsoleManager.h"

#include "BattleStateHash.h"
#include "BattleRngRegistry.h"
#include "BattleTestSettings.h"
#include "BattleRegistry.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "MassEntitySubsystem.h"

namespace
{
	UWorld* MakeStateTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	bool LoadFixtureForState(TArray<FEduUnit>& OutRows)
	{
		const FString Path = FPaths::ProjectDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}
		FEduParseError Err;
		return FBattleEduParser::Parse(Text, OutRows, Err);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateHash_Stability,
	"Battle.Unit.StateHash.Stability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStateHash_Stability::RunTest(const FString& Parameters)
{
	UWorld* World = MakeStateTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("entities"), ES))
	{
		World->DestroyWorld(false);
		return false;
	}

	TArray<FEduUnit> Rows;
	if (!TestTrue(TEXT("fixture"), LoadFixtureForState(Rows)))
	{
		World->DestroyWorld(false);
		return false;
	}

	// Empty-world hash is stable call-to-call.
	const uint64 Empty1 = FBattleStateHash::Compute(World);
	const uint64 Empty2 = FBattleStateHash::Compute(World);
	TestEqual(TEXT("empty world hash stable"), Empty1, Empty2);
	TestNotEqual(TEXT("empty world hash nonzero"), Empty1, static_cast<uint64>(0));

	// Spawn 2 units -> stable while nothing mutates.
	for (int32 RowIdx = 0; RowIdx < 2; ++RowIdx)
	{
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = RowIdx;
		Spec.SoldierCount = 10;
		Spec.UnitName = FString::Printf(TEXT("SH_%d"), RowIdx);
		FBattleSpawnResult Result;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("spawn row %d"), RowIdx),
			UBattleSpawnLibrary::SpawnUnitFromEduRow(World, Spec, Rows[RowIdx], Result, Error)))
		{
			AddError(Error);
			World->DestroyWorld(false);
			return false;
		}
	}
	const uint64 Baseline = FBattleStateHash::Compute(World);
	TestEqual(TEXT("spawned-state hash stable (identical state -> identical hash)"),
		FBattleStateHash::Compute(World), Baseline);

	// Seeded mutation #1: mutate a shared-fragment value (the UnitSync output
	// channel) -> hash changes.
	const TArray<FMassEntityHandle>* RosterA = Registry->FindRoster(Registry->GetSortedHandles()[0]);
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	FUnitStatsSharedFragment* Stats = EM.GetSharedFragmentDataPtr<FUnitStatsSharedFragment>((*RosterA)[0]);
	if (!TestNotNull(TEXT("shared stats"), Stats))
	{
		World->DestroyWorld(false);
		return false;
	}
	const float SavedMelee = Stats->MeleeAttackMult;
	Stats->MeleeAttackMult = SavedMelee + 0.25f;
	const uint64 AfterMutation = FBattleStateHash::Compute(World);
	TestNotEqual(TEXT("shared-fragment mutation changes the hash"), AfterMutation, Baseline);
	Stats->MeleeAttackMult = SavedMelee;
	TestEqual(TEXT("reverting restores the hash"), FBattleStateHash::Compute(World), Baseline);

	// Seeded mutation #2: quantization sanity -- a sub-quantum change (1e-3,
	// below the 1e-2 grid) must NOT change the hash (T0.6 quantization rule).
	Stats->MeleeAttackMult = SavedMelee + 0.001f;
	TestEqual(TEXT("sub-quantum change does not change the hash"),
		FBattleStateHash::Compute(World), Baseline);
	Stats->MeleeAttackMult = SavedMelee;

	// Seeded mutation #3: HP damage (per-entity quantized state).
	{
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Registry->GetSortedHandles()[0]);
		FHealthFragment* Health = FMassEntityView(EM, (*Roster)[0]).GetFragmentDataPtr<FHealthFragment>();
		if (TestNotNull(TEXT("health fragment"), Health))
		{
			Health->CurrentHP -= 0.5f;
			TestNotEqual(TEXT("HP mutation changes the hash"), FBattleStateHash::Compute(World), Baseline);
			Health->CurrentHP += 0.5f;
			TestEqual(TEXT("HP restore restores the hash"), FBattleStateHash::Compute(World), Baseline);
		}
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRng_Discipline,
	"Battle.Unit.Rng.Discipline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRng_Discipline::RunTest(const FString& Parameters)
{
	// T0.7: two registries with equal roots produce identical 10k-draw
	// sequences; different roots diverge; suites stay green under 5 seeds.
	auto Draw10k = [](const FBattleRngRegistry& Registry, FName Stream)
	{
		const FRandomStream& S = const_cast<FBattleRngRegistry&>(Registry).GetStream(Stream);
		uint64 Acc = 1469598103934665603ull; // FNV-1a fold of the sequence
		for (int32 i = 0; i < 10000; ++i)
		{
			const uint32 V = static_cast<uint32>(S.RandRange(0, 1000000));
			Acc ^= V;
			Acc *= 1099511628211ull;
		}
		return Acc;
	};

	for (int32 Seed : {1, 42, 1337, 90210, 20261024})
	{
		FBattleRngRegistry A;
		FBattleRngRegistry B;
		A.Seed(Seed);
		B.Seed(Seed);

		TestEqual(TEXT("derived seeds match"), FBattleRngRegistry::DeriveSeed(Seed, TEXT("Pairing")),
			FBattleRngRegistry::DeriveSeed(Seed, TEXT("Pairing")));
		TestNotEqual(TEXT("different streams derive different seeds"),
			FBattleRngRegistry::DeriveSeed(Seed, TEXT("Pairing")),
			FBattleRngRegistry::DeriveSeed(Seed, TEXT("Morale")));

		const uint64 SeqA = Draw10k(A, TEXT("Pairing"));
		const uint64 SeqB = Draw10k(B, TEXT("Pairing"));
		if (!TestEqual(FString::Printf(TEXT("equal roots -> identical 10k draws (seed %d)"), Seed), SeqA, SeqB))
		{
			return false;
		}

		FBattleRngRegistry C;
		C.Seed(Seed + 1);
		TestNotEqual(FString::Printf(TEXT("different root diverges (seed %d)"), Seed),
			SeqA, Draw10k(C, TEXT("Pairing")));
	}

	// Stream laziness: getting the same named stream twice returns the SAME
	// stream (continuing sequence), a fresh registry has none.
	FBattleRngRegistry R;
	R.Seed(7);
	FRandomStream& S1 = R.GetStream(TEXT("Combat"));
	S1.RandRange(0, 100);
	FRandomStream& S2 = R.GetStream(TEXT("Combat"));
	TestEqual(TEXT("named stream is persistent"), S1.GetCurrentSeed(), S2.GetCurrentSeed());
	TestEqual(TEXT("one stream materialized"), R.GetStreamCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestMode_AssertAndViolation,
	"Battle.Unit.TestMode.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTestMode_AssertAndViolation::RunTest(const FString& Parameters)
{
	// T0.9: the helper reads ACTIVE engine settings and catches deliberate
	// violations (roadmap: assert active settings, not CVars being set).
	// Violations are probed through a null sink + out-array so this test's own
	// result is not polluted by the deliberate breaches.
	UBattleTestSettings::ApplyTestMode();
	TestTrue(TEXT("clean state passes AssertTestMode"), UBattleTestSettings::AssertTestMode());
	TestTrue(TEXT("FApp fixed step active"), FApp::UseFixedTimeStep());
	TestEqual(TEXT("fixed delta is 1/60"), FApp::GetFixedDeltaTime(), 1.0 / 60.0);

	// Deliberate violation #1: fixed step off -> AssertTestMode must fail.
	const bool bSavedUseFixed = FApp::UseFixedTimeStep();
	const double SavedDelta = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(false);
	{
		TArray<FString> Violations;
		const bool bClean = UBattleTestSettings::AssertTestMode(nullptr, &Violations);
		TestFalse(TEXT("fixed-step violation detected"), bClean);
		TestTrue(TEXT("violation names the fixed step"), Violations.ContainsByPredicate(
			[](const FString& V) { return V.Contains(TEXT("UseFixedTimeStep")); }));
	}
	FApp::SetUseFixedTimeStep(bSavedUseFixed);
	FApp::SetFixedDeltaTime(SavedDelta);

	// Deliberate violation #2: contract cvar off -> detected.
	IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Test.DisableTimeSlicing"));
	if (TestNotNull(TEXT("Battle.Test.DisableTimeSlicing cvar exists"), Cvar))
	{
		const int32 Saved = Cvar->GetInt();
		Cvar->Set(0, ECVF_SetByCode);
		TArray<FString> Violations;
		const bool bClean = UBattleTestSettings::AssertTestMode(nullptr, &Violations);
		TestFalse(TEXT("time-slicing contract violation detected"), bClean);
		TestTrue(TEXT("violation names time slicing"), Violations.ContainsByPredicate(
			[](const FString& V) { return V.Contains(TEXT("DisableTimeSlicing")); }));
		Cvar->Set(Saved, ECVF_SetByCode);
	}

	// Restored: clean again.
	TestTrue(TEXT("restored state passes again"), UBattleTestSettings::AssertTestMode());
	return true;
}
