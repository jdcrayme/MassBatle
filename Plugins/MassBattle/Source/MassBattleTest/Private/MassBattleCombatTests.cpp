#if WITH_DEV_AUTOMATION_TESTS
// MassBattleCombatTests.cpp -- Phase 2 combat gates (brief task 0 created the
// fail-by-design skeletons; task 2 adds the PASSING data-spine tests here:
// record lifecycle, attack orders + A9 guard, save round-trip, spawn
// composition (zero new tags), combat hash terms. T2.1-T2.3 stay skeletons
// until tasks 3-4 fill them.
#include "Misc/AutomationTest.h"

#include "BattleRegistry.h"
#include "BattleSave.h"
#include "BattleSpawnLibrary.h"
#include "BattleStateHash.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleRngRegistry.h"
#include "BattleEduParser.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	UWorld* MakeCombatTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	/** Registry-valid spec (RegisterUnit rejects INDEX_NONE/<=0). */
	FUnitSpawnSpec MakeCombatSpec(int32 Row = 0, int32 Count = 8)
	{
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = Row;
		Spec.SoldierCount = Count;
		Spec.UnitName = TEXT("CombatTestUnit");
		return Spec;
	}

	/** Parses the EDU fixture (same source SpawnRoster uses). */
	bool LoadCombatFixture(TArray<FEduUnit>& OutRows)
	{
		const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString EduText;
		FEduParseError Err;
		return FFileHelper::LoadFileToString(EduText, *FixturePath)
			&& FBattleEduParser::Parse(EduText, OutRows, Err);
	}

	/** Maps one fixture row to typed stats, then spawns (SpawnUnit takes the
	 *  typed row + a valid spec, not the parsed field bag). */
	FBattleSpawnResult SpawnRow(UWorld* World, const FEduUnit& EduRow)
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(EduRow, Stats, Error))
		{
			return FBattleSpawnResult();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = 0; // fixture order; display only at this tier
		Spec.SoldierCount = FMath::Max(1, Stats.SoldierCount);
		Spec.UnitName = Stats.TypeName;
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats);
	}
}

// ---------------------------------------------------------------------------
// Task 2: engagement-record lifecycle (create/refresh/cap/dead/mass release)
// + the preallocated scratch tables.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_RecordLifecycle,
	"Battle.Unit.Combat.RecordLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_RecordLifecycle::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	FUnitHandle A = Registry->RegisterUnit(MakeCombatSpec());
	FUnitHandle B = Registry->RegisterUnit(MakeCombatSpec(1));
	FUnitHandle C = Registry->RegisterUnit(MakeCombatSpec());
	TestTrue(TEXT("handles"), A.IsValid() && B.IsValid() && C.IsValid());

	// Create + refresh: one record per key, refreshed in place.
	FEngagementRecord* Rec = Registry->CreateOrRefreshEngagement(A, B);
	TestNotNull(TEXT("record created"), Rec);
	if (Rec)
	{
		Rec->Mode = EEngagementMode::Mob;
		Rec->DesiredPressure = 11;
	}
	FEngagementRecord* Refreshed = Registry->CreateOrRefreshEngagement(A, B);
	TestNotNull(TEXT("refresh returns the record"), Refreshed);
	TestEqual(TEXT("refresh keeps the store at one entry"), Registry->GetEngagementCount(), 1);
	TestEqual(TEXT("refresh keeps the fields"), Refreshed ? Refreshed->DesiredPressure : 0, 11);

	// Directional keys: (A,B) != (B,A).
	Registry->CreateOrRefreshEngagement(B, A);
	TestEqual(TEXT("reverse key is a distinct record"), Registry->GetEngagementCount(), 2);

	// Sorted keys are deterministic (attacker index, then defender index).
	const TArray<FEngagementKey> Keys = Registry->GetSortedEngagementKeys();
	TestEqual(TEXT("sorted key count"), Keys.Num(), 2);
	if (Keys.Num() == 2)
	{
		TestTrue(TEXT("sorted by attacker first"), Keys[0].Attacker.Index < Keys[1].Attacker.Index);
	}

	// Dead-flag + mass release + prune. Flagging C kills every record where C
	// is attacker or defender; (A,B)/(B,A) survive; prune removes flagged.
	Registry->CreateOrRefreshEngagement(C, A);
	Registry->CreateOrRefreshEngagement(B, C);
	const int32 Flagged = Registry->FlagUnitEngagementsDead(C);
	TestEqual(TEXT("flagged records involving C"), Flagged, 2);
	TestTrue(TEXT("(A,B) not flagged"), Registry->FindEngagement(A, B) && Registry->FindEngagement(A, B)->bDead == 0);
	TestTrue(TEXT("(C,A) flagged"), Registry->FindEngagement(C, A) && Registry->FindEngagement(C, A)->bDead == 1);
	const int32 Pruned = Registry->PruneDeadEngagements();
	TestEqual(TEXT("pruned count"), Pruned, Flagged);
	TestNull(TEXT("(C,A) gone after prune"), Registry->FindEngagement(C, A));
	TestNotNull(TEXT("(A,B) survives prune"), Registry->FindEngagement(A, B));

	// Cap: fill to capacity, then a NEW key must be rejected (store grows at
	// battle start only, EC9). Existing keys keep refreshing.
	{
		for (int32 i = 0; i < 8192; ++i)
		{
			FUnitHandle Attacker, Defender;
			Attacker.Index = static_cast<uint32>(10 + i / 64);
			Attacker.Serial = 1;
			Defender.Index = static_cast<uint32>(100 + i % 64);
			Defender.Serial = 1;
			if (Registry->CreateOrRefreshEngagement(Attacker, Defender) == nullptr)
			{
				break; // store hit its cap
			}
		}
		TestEqual(TEXT("store capped at MaxEngagementRecords"), Registry->GetEngagementCount(), 4096);
		TestTrue(TEXT("existing key still refreshable at cap"),
			Registry->CreateOrRefreshEngagement(A, B) != nullptr);
		FUnitHandle OverflowAttacker, OverflowDefender;
		OverflowAttacker.Index = 999999;
		OverflowAttacker.Serial = 1;
		OverflowDefender.Index = 999998;
		OverflowDefender.Serial = 1;
		TestNull(TEXT("new key rejected at cap"), Registry->CreateOrRefreshEngagement(OverflowAttacker, OverflowDefender));
	}

	// Scratch tables: open-address tally (insert/count/reset) + stamped
	// coverage cache (stale tick misses).
	FMassEntityHandle D1, D2;
	D1.Index = 500; D1.SerialNumber = 1;
	D2.Index = 501; D2.SerialNumber = 1;
	FCombatTallyTable& Tally = Registry->GetTallyScratch();
	const int32 SlotA = Tally.FindOrAdd(D1);
	const int32 SlotB = Tally.FindOrAdd(D2);
	const int32 SlotA2 = Tally.FindOrAdd(D1);
	TestTrue(TEXT("tally slots stable"), SlotA != INDEX_NONE && SlotA == SlotA2 && SlotB != INDEX_NONE && SlotB != SlotA);
	Tally.Counts[SlotA] += 1;
	Tally.Counts[SlotA] += 1;
	Tally.Counts[SlotB] += 1;
	TestEqual(TEXT("tally counts"), Tally.GetCount(D1), 2);
	Tally.Reset();
	TestEqual(TEXT("tally reset"), Tally.GetCount(D1), 0);

	FCombatCoverageTable& Coverage = Registry->GetCoverageScratch();
	const uint32 Tick = 1234;
	TestTrue(TEXT("coverage store"), Coverage.Store(D1, 0.75f, Tick) != INDEX_NONE);
	const int32 Fresh = Coverage.Find(D1, Tick);
	TestTrue(TEXT("coverage fresh hit"), Fresh != INDEX_NONE && FMath::IsNearlyEqual(Coverage.Values[Fresh], 0.75f));
	TestEqual(TEXT("coverage stale tick misses"), Coverage.Find(D1, Tick + 1), INDEX_NONE);
	Coverage.Store(D1, 0.5f, Tick + 1);
	const int32 Updated = Coverage.Find(D1, Tick + 1);
	TestTrue(TEXT("coverage update"), Updated != INDEX_NONE && FMath::IsNearlyEqual(Coverage.Values[Updated], 0.5f));

	// F2 hook: scratch entries for a dead entity are dropped.
	Registry->NotifyCombatEntityDeath(D2);
	TestEqual(TEXT("dead entity tally dropped"), Tally.GetCount(D2), 0);
	TestEqual(TEXT("dead entity coverage dropped"), Coverage.Find(D2, Tick + 1), INDEX_NONE);

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Task 2: attack order surface (A4) -- serial bump, no-op re-issue, A9 guard.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_OrderAttack,
	"Battle.Unit.Combat.OrderAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_OrderAttack::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	const FUnitHandle A = Registry->RegisterUnit(MakeCombatSpec());
	const FUnitHandle B = Registry->RegisterUnit(MakeCombatSpec(1));
	const FUnitHandle Ghost = Registry->RegisterUnit(MakeCombatSpec());
	Registry->UnregisterUnit(Ghost); // a stale handle for the reject path
	TestTrue(TEXT("handles"), A.IsValid() && B.IsValid());

	// AttackUnit: serial bump + payload.
	const uint32 SerialBefore = Registry->GetOrderSerial(A);
	TestTrue(TEXT("attack unit issued"), Registry->IssueAttackUnitOrder(A, B));
	TestEqual(TEXT("serial bumped"), Registry->GetOrderSerial(A), SerialBefore + 1);
	const FUnitOrder* Order = Registry->FindOrder(A);
	TestNotNull(TEXT("order present"), Order);
	if (Order)
	{
		TestEqual(TEXT("type is AttackUnit"), static_cast<int32>(Order->Type), static_cast<int32>(FUnitOrder::EType::AttackUnit));
		TestTrue(TEXT("target payload"), Order->TargetUnit == B);
	}

	// Identical re-issue: no serial bump (M11 pattern).
	const uint32 SerialAfterFirst = Registry->GetOrderSerial(A);
	TestTrue(TEXT("identical re-issue ok"), Registry->IssueAttackUnitOrder(A, B));
	TestEqual(TEXT("re-issue does not bump"), Registry->GetOrderSerial(A), SerialAfterFirst);

	// Rejects: bad handles.
	TestFalse(TEXT("bad attacker"), Registry->IssueAttackUnitOrder(FUnitHandle(), B));
	TestFalse(TEXT("stale target"), Registry->IssueAttackUnitOrder(A, Ghost));

	// A9 guard: a garrison-committed target rejects (garrisoned units are not
	// valid melee targets, F11) with an EXEC_RESULT REJECT line (logged).
	// Staged through the real API: an empty-roster unit passes the cavalry
	// check and carries the Garrison order.
	TestTrue(TEXT("stage: target garrison-ordered"), Registry->IssueGarrisonOrder(B, 999));
	const uint32 SerialBeforeReject = Registry->GetOrderSerial(A);
	TestFalse(TEXT("garrisoned target rejects"), Registry->IssueAttackUnitOrder(A, B));
	TestEqual(TEXT("reject does not bump"), Registry->GetOrderSerial(A), SerialBeforeReject);
	TestFalse(TEXT("garrisoned target rejects attack-move"),
		Registry->IssueAttackMoveOrder(A, FVector2D(1.f, 1.f), 0.f, 0.f, B));

	// AttackMove: payload + serial; disengage = re-task with Move (spec §4.7)
	// -- the order type simply changes.
	const FVector2D Anchor(1000.f, 2000.f);
	TestTrue(TEXT("attack move issued"), Registry->IssueAttackMoveOrder(A, Anchor, 90.f, 1.5f));
	Order = Registry->FindOrder(A);
	if (Order)
	{
		TestEqual(TEXT("type is AttackMove"), static_cast<int32>(Order->Type), static_cast<int32>(FUnitOrder::EType::AttackMove));
		TestEqual(TEXT("anchor payload"), Order->AnchorPos, Anchor);
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Task 2: save/load skeleton (A8) -- registry-level round-trip: capture ->
// clear -> restore -> bookkeeping hash equal, schema version present, RNG
// stream states restored.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_SaveRoundTrip,
	"Battle.Unit.Combat.SaveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_SaveRoundTrip::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UBattleRngSubsystem* Rng = World->GetSubsystem<UBattleRngSubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}
	Rng->Seed(424242);

	// Build state: two spawned units, orders, engagement records, RNG draws.
	TArray<FEduUnit> Rows;
	if (!LoadCombatFixture(Rows) || Rows.Num() < 2)
	{
		AddError(TEXT("EDU fixture unavailable"));
		World->DestroyWorld(false);
		return false;
	}
	const FBattleSpawnResult UnitA = SpawnRow(World, Rows[0]);
	const FBattleSpawnResult UnitB = SpawnRow(World, Rows[1]);
	TestTrue(TEXT("units spawned"), UnitA.Unit.IsValid() && UnitB.Unit.IsValid());
	TestTrue(TEXT("attack order"), Registry->IssueAttackUnitOrder(UnitA.Unit, UnitB.Unit));
	if (FEngagementRecord* Rec = Registry->CreateOrRefreshEngagement(UnitA.Unit, UnitB.Unit))
	{
		Rec->Mode = EEngagementMode::Mob;
		Rec->DesiredPressure = 9;
	}
	Rng->GetStream(TEXT("Combat.Roll")).RandRange(0, 100000);
	Rng->GetStream(TEXT("Combat.Assign")).RandRange(0, 100000);

	const uint32 HashBefore = Registry->ComputeBookkeepingHash();
	FBattleSaveData Save;
	Registry->CaptureBattleState(Save);
	TestEqual(TEXT("schema version"), Save.SchemaVersion, 1u);
	TestEqual(TEXT("units captured"), Save.Units.Num(), 2);
	TestEqual(TEXT("engagement captured"), Save.Engagements.Num(), 1);
	TestTrue(TEXT("rng streams captured"), Save.RngStreams.Num() >= 2);
	TestEqual(TEXT("root seed captured"), Save.RngRootSeed, 424242);

	// Clear: unregister everything + prune records. The hash must CHANGE
	// (state actually destroyed, not vacuously restored over itself).
	for (const FUnitHandle& Handle : Registry->GetSortedHandles())
	{
		Registry->UnregisterUnit(Handle);
	}
	Registry->FlagUnitEngagementsDead(UnitA.Unit);
	Registry->PruneDeadEngagements();
	TestEqual(TEXT("cleared"), Registry->GetUnitCount(), 0);
	TestNotEqual(TEXT("hash changed after clear"), Registry->ComputeBookkeepingHash(), HashBefore);

	// Restore: bookkeeping hash equal again (registry-level round-trip).
	TestTrue(TEXT("restore ok"), Registry->RestoreBattleState(Save));
	TestEqual(TEXT("hash restored"), Registry->ComputeBookkeepingHash(), HashBefore);
	const FUnitRuntimeState* RestoredA = Registry->FindUnitState(UnitA.Unit);
	TestNotNull(TEXT("unit A state back"), RestoredA);
	if (RestoredA)
	{
		TestEqual(TEXT("roster size back"), RestoredA->Roster.Num(), UnitA.NumEntities);
		TestEqual(TEXT("order type back"), static_cast<int32>(RestoredA->CurrentOrder.Type),
			static_cast<int32>(FUnitOrder::EType::AttackUnit));
	}
	const FEngagementRecord* RestoredRec = Registry->FindEngagement(UnitA.Unit, UnitB.Unit);
	TestNotNull(TEXT("engagement back"), RestoredRec);
	if (RestoredRec)
	{
		TestEqual(TEXT("engagement pressure back"), RestoredRec->DesiredPressure, 9);
		TestEqual(TEXT("assigned attackers never persist"), RestoredRec->AssignedAttackers, 0);
	}

	// RNG: the stream continues from the captured state (same draw sequence).
	const TArray<FBattleRngStreamState> CapturedStates = Rng->GetRegistry().SnapshotStreams();
	TestEqual(TEXT("stream count kept"), Rng->GetRegistry().GetStreamCount(), Save.RngStreams.Num());
	int32 Identical = 0;
	for (const FBattleRngStreamState& State : CapturedStates)
	{
		for (const FRngStreamSaveEntry& Entry : Save.RngStreams)
		{
			if (Entry.StreamName == State.Name && Entry.CurrentSeed == State.CurrentSeed)
			{
				++Identical;
			}
		}
	}
	TestEqual(TEXT("every stream restored at its captured position"), Identical, Save.RngStreams.Num());

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Task 2: spawn composition (F6) -- combat fragments ride every battle entity
// (zero new tags), fragment mutation does NOT flip archetypes, identical
// spawns yield identical archetype counts.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_SpawnComposition,
	"Battle.Unit.Combat.SpawnComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_SpawnComposition::RunTest(const FString& Parameters)
{
	TArray<FEduUnit> Rows;
	if (!LoadCombatFixture(Rows) || Rows.Num() < 2)
	{
		AddError(TEXT("EDU fixture unavailable"));
		return false;
	}

	int32 ArchetypesA = -1;
	int32 ArchetypesB = -1;
	bool bFragmentsPresent = true;

	{
		UWorld* World = MakeCombatTestWorld();
		const FBattleSpawnResult UnitA = SpawnRow(World, Rows[0]);
		SpawnRow(World, Rows[1]);
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
#if WITH_MASSENTITY_DEBUG
		ArchetypesA = EntityManager.DebugGetArchetypesCount();
#endif
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(UnitA.Unit);
		if (!TestNotNull(TEXT("roster"), Roster) || Roster->Num() == 0)
		{
			World->DestroyWorld(false);
			return false;
		}
		for (const FMassEntityHandle& Entity : *Roster)
		{
			const FMassEntityView View(EntityManager, Entity);
			bFragmentsPresent = bFragmentsPresent
				&& View.GetFragmentDataPtr<FCombatIntentFragment>() != nullptr
				&& View.GetFragmentDataPtr<FCombatPairFragment>() != nullptr
				&& View.GetFragmentDataPtr<FChargeFragment>() != nullptr
				&& View.GetFragmentDataPtr<FHealthFragment>() != nullptr;
		}
		// Fragment MUTATION must not flip archetypes (only tags do, F6).
		FMassEntityView First(EntityManager, (*Roster)[0]);
		First.GetFragmentData<FCombatPairFragment>().State = ECombatPairState::Striking;
#if WITH_MASSENTITY_DEBUG
		TestEqual(TEXT("fragment mutation does not flip archetypes"),
			EntityManager.DebugGetArchetypesCount(), ArchetypesA);
#endif
		World->DestroyWorld(false);
	}
	// Second identical run: same archetype count (composition determinism).
	{
		UWorld* World = MakeCombatTestWorld();
		SpawnRow(World, Rows[0]);
		SpawnRow(World, Rows[1]);
#if WITH_MASSENTITY_DEBUG
		ArchetypesB = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().DebugGetArchetypesCount();
#endif
		World->DestroyWorld(false);
	}

#if WITH_MASSENTITY_DEBUG
	TestEqual(TEXT("identical spawns -> identical archetype count"), ArchetypesB, ArchetypesA);
	AddInfo(FString::Printf(TEXT("archetypes with combat fragments (2 infantry units): %d"), ArchetypesA));
#else
	AddInfo(TEXT("shipping build: archetype count folds 0 (debug API absent)"));
#endif
	TestTrue(TEXT("combat fragments on every spawned entity"), bFragmentsPresent);
	return true;
}

// ---------------------------------------------------------------------------
// Task 2: combat hash terms (spec §3.4) -- stability + mutation sensitivity
// (pair target / state / timer, charge fields, mount HP) with revert.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_HashTerms,
	"Battle.Unit.Combat.HashTerms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_HashTerms::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry)
	{
		World->DestroyWorld(false);
		return false;
	}

	TArray<FEduUnit> Rows;
	if (!LoadCombatFixture(Rows) || Rows.Num() < 2)
	{
		AddError(TEXT("EDU fixture unavailable"));
		World->DestroyWorld(false);
		return false;
	}
	const FBattleSpawnResult UnitA = SpawnRow(World, Rows[0]);
	TestTrue(TEXT("spawned"), UnitA.Unit.IsValid());

	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(UnitA.Unit);
	if (!TestNotNull(TEXT("roster"), Roster) || Roster->Num() == 0)
	{
		World->DestroyWorld(false);
		return false;
	}

	const uint64 Hash0 = FBattleStateHash::Compute(World);
	const uint64 Hash0Again = FBattleStateHash::Compute(World);
	TestEqual(TEXT("hash stable across recomputes"), Hash0Again, Hash0);

	FMassEntityView View(EntityManager, (*Roster)[0]);

	// Pair mutation: target.
	{
		FCombatPairFragment& Pair = View.GetFragmentData<FCombatPairFragment>();
		FMassEntityHandle Old = Pair.Target;
		Pair.Target = (*Roster)[FMath::Min(1, Roster->Num() - 1)];
		TestNotEqual(TEXT("pair target mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Pair.Target = Old;
		TestEqual(TEXT("pair target revert restores hash"), FBattleStateHash::Compute(World), Hash0);
	}
	// Pair mutation: state + contact angle + attack timer.
	{
		FCombatPairFragment& Pair = View.GetFragmentData<FCombatPairFragment>();
		const ECombatPairState OldState = Pair.State;
		Pair.State = ECombatPairState::Striking;
		TestNotEqual(TEXT("pair state mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Pair.State = OldState;
		const uint8 OldAngle = Pair.ContactAngle;
		Pair.ContactAngle = 137;
		TestNotEqual(TEXT("contact angle mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Pair.ContactAngle = OldAngle;
		const float OldTimer = Pair.AttackTimer;
		Pair.AttackTimer = 0.37f;
		TestNotEqual(TEXT("attack timer mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Pair.AttackTimer = OldTimer;
		TestEqual(TEXT("pair reverts restore hash"), FBattleStateHash::Compute(World), Hash0);
	}
	// Charge fields.
	{
		FChargeFragment& Charge = View.GetFragmentData<FChargeFragment>();
		const float OldDist = Charge.DistanceTraveled;
		Charge.DistanceTraveled = 12.345f;
		TestNotEqual(TEXT("charge distance mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Charge.DistanceTraveled = OldDist;
		const float OldImpact = Charge.ImpactSpeed;
		Charge.ImpactSpeed = 5.5f;
		TestNotEqual(TEXT("impact speed mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Charge.ImpactSpeed = OldImpact;
		const uint8 OldConnected = Charge.bConnected;
		Charge.bConnected = 1;
		TestNotEqual(TEXT("charge connect mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Charge.bConnected = OldConnected;
	}
	// Mount HP.
	{
		FHealthFragment& Health = View.GetFragmentData<FHealthFragment>();
		const int16 OldMount = Health.MountCurHP;
		Health.MountCurHP = 3;
		TestNotEqual(TEXT("mount HP mutation changes hash"), FBattleStateHash::Compute(World), Hash0);
		Health.MountCurHP = OldMount;
		TestEqual(TEXT("all reverts restore hash"), FBattleStateHash::Compute(World), Hash0);
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// T2.1-T2.3 unit gates (tasks 3-4 own the bodies)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitChanceGoldenSkeleton,
	"Battle.Unit.Combat.HitChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitChanceGoldenSkeleton::RunTest(const FString& Parameters)
{
	// T2.1 (task 3): hit-chance formula + arc bands + AP + clamps locked to a
	// golden table; any diff = conscious formula review.
	AddError(TEXT("skeleton: T2.1 golden lands with task 3 (BattleMelee seam)"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBudgetCoverageSkeleton,
	"Battle.Unit.Combat.BudgetCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBudgetCoverageSkeleton::RunTest(const FString& Parameters)
{
	// T2.2 (task 4): exposed arcs filled before covered; budget bounds [1,
	// MaxMob] never violated over 1k random assign/remove cycles.
	AddError(TEXT("skeleton: T2.2 invariants land with task 4 (BattleEngagement seam)"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTallyAssignSkeleton,
	"Battle.Unit.Combat.TallyAssign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTallyAssignSkeleton::RunTest(const FString& Parameters)
{
	// T2.3 (task 4): budgets respected on synthetic populations; determinism
	// flag yields index-sorted reproducibility (A13).
	AddError(TEXT("skeleton: T2.3 synthetic assign lands with task 4"));
	return false;
}

// ---------------------------------------------------------------------------
// T2.4-T2.8 func gates (tasks 7-8 own the scenario actors; the real tests
// become the harness's complex-test pattern with latent commands)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FS4ShieldWallSkeleton,
	"Battle.Func.S4_ShieldWall",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FS4ShieldWallSkeleton::RunTest(const FString& Parameters)
{
	// T2.4 (task 8): formed defender holds budget at FormedSlots while
	// coverage holds; the melt CURVE shape is asserted as coverage drops.
	AddError(TEXT("skeleton: S4_ShieldWall scenario lands with task 8"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FS5MobSkeleton,
	"Battle.Func.S5_Mob",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FS5MobSkeleton::RunTest(const FString& Parameters)
{
	// T2.5 (task 8, = M3 geometry-only): 2 knights vs 40 peasants; >= 4
	// exposed-arc attackers; pulled down in the time band; waiters at the
	// ring, no carousel.
	AddError(TEXT("skeleton: S5_Mob scenario lands with task 8"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChargeMechanicsSkeleton,
	"Battle.Func.ChargeMechanics",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FChargeMechanicsSkeleton::RunTest(const FString& Parameters)
{
	// T2.6 (task 7): bonus only on fresh fast contact; knockback applied;
	// decays to melee within the window.
	AddError(TEXT("skeleton: ChargeMechanics scenario lands with task 7"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairLifecycleSkeleton,
	"Battle.Func.PairLifecycle",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FPairLifecycleSkeleton::RunTest(const FString& Parameters)
{
	// T2.7 (task 8 full assert): hysteresis breakup; vacated-slot handoff
	// <= 1 tick; disengage mass release.
	AddError(TEXT("skeleton: PairLifecycle scenario lands with task 8"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeterminismTwoRunSkeleton,
	"Battle.Func.DeterminismTwoRun",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FDeterminismTwoRunSkeleton::RunTest(const FString& Parameters)
{
	// T2.8 (task 9): 5-min mixed clash, same seed twice, per-tick state-hash
	// equality (R9 scope: same binary, same machine).
	AddError(TEXT("skeleton: DeterminismTwoRun lands with task 9"));
	return false;
}

// ---------------------------------------------------------------------------
// T2.9 perf gate placeholder (task 9): the pairing pass budget.
// Runs under the Battle.Perf filter (nightly/Gauntlet territory per B.0) --
// matched by neither CI tier's filter string, so it changes no tier's count.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfGate2Skeleton,
	"Battle.Perf.Gate2",
	(EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter))

bool FPerfGate2Skeleton::RunTest(const FString& Parameters)
{
	// T2.9 (task 9): tally + assign < 0.5 ms game thread @ 1k in contact;
	// baseline JSON checked in, CI compares.
	AddError(TEXT("skeleton: perf gate lands with task 9"));
	return false;
}

#endif // WITH_DEV_AUTOMATION_TESTS
