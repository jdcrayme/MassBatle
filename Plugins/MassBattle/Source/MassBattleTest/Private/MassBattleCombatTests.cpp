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
#include "BattleEngagement.h"
#include "BattleMelee.h"
#include "BattleSpatialHash.h"
#include "BattleUnitContact.h"
#include "BattleStateHash.h"
#include "BattleExecCommands.h"
#include "BattleFragments.h"
#include "BattleCombatFragments.h"
#include "BattleRangedFragments.h"
#include "BattleRanged.h"
#include "BattleProjectilePool.h"
#include "BattleProjectileSubsystem.h"
#include "BattleDebugSubsystem.h"
#include "RangedFireProcessor.h"
#include "ProjectileStepProcessor.h"
#include "BattleRngRegistry.h"
#include "BattleEduParser.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "BattleMovementFragments.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	UWorld* MakeCombatTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	using namespace FCombatSolver; // the assign mirrors are seam-namespace types

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
	TestTrue(TEXT("coverage store"), Coverage.Store(D1, 0.75f, 0u, Tick) != INDEX_NONE);
	const int32 Fresh = Coverage.Find(D1, Tick);
	TestTrue(TEXT("coverage fresh hit"), Fresh != INDEX_NONE && FMath::IsNearlyEqual(Coverage.Values[Fresh], 0.75f));
	TestEqual(TEXT("coverage stale tick misses"), Coverage.Find(D1, Tick + 1), INDEX_NONE);
	Coverage.Store(D1, 0.5f, 0u, Tick + 1);
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
// PR-4 (audit D-4 step 2): the assign pass's intent table on the seam --
// auto-engage nearest-by-rank, demo-fix3 defensive reaction, order-exclusion.
// Pure: orders + live record keys -> intents, no world.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_AssignIntents,
	"Battle.Unit.Combat.AssignIntents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_AssignIntents::RunTest(const FString& Parameters)
{
	// Handles with distinct (index, serial) ranks: A=1:1, B=2:1, C=3:1.
	const FUnitHandle A(1, 1);
	const FUnitHandle B(2, 1);
	const FUnitHandle C(3, 1);
	TestTrue(TEXT("probe handles"), A.IsValid() && B.IsValid() && C.IsValid());

	// -- Targetless AttackMove auto-engages the lowest-rank live-record enemy. --
	{
		TMap<FUnitHandle, FUnitOrder> Orders;
		Orders.Add(A, FUnitOrder{ /*Type*/ FUnitOrder::EType::AttackMove });
		TArray<FEngagementKey> Records;
		Records.Add({ A, C }); // higher rank first: the pick must still be the LOWEST
		Records.Add({ A, B });
		TMap<FUnitHandle, FCombatSolver::FAssignIntent> Intents;
		FCombatSolver::ResolveUnitIntents(Orders, Records, Intents);
		const FCombatSolver::FAssignIntent* Resolved = Intents.Find(A);
		TestNotNull(TEXT("auto-engage intent"), Resolved);
		if (Resolved)
		{
			TestEqual(TEXT("auto-engage target = lowest rank"), Resolved->TargetUnit, B);
			TestEqual(TEXT("auto-engage flag"), Resolved->bAutoEngage, 1);
			TestTrue(TEXT("auto-engage has intent"), Resolved->bHasIntent);
		}
	}

	// -- demo-fix3: an UNORDERED unit under fire defends against the lowest
	//    aggressor rank; a Move-ordered unit under fire gets NO intent. --
	{
		TMap<FUnitHandle, FUnitOrder> Orders;
		FUnitOrder Retreat;
		Retreat.Type = FUnitOrder::EType::Move;
		Orders.Add(B, Retreat);
		TArray<FEngagementKey> Records;
		Records.Add({ C, B }); // higher-rank aggressor
		Records.Add({ A, B }); // lower-rank aggressor
		TMap<FUnitHandle, FCombatSolver::FAssignIntent> Intents;
		FCombatSolver::ResolveUnitIntents(Orders, Records, Intents);
		TestFalse(TEXT("Move-ordered unit has no defensive intent"), Intents.Contains(B));
	}

	{
		TArray<FEngagementKey> Records;
		Records.Add({ C, B });
		Records.Add({ A, B });
		TMap<FUnitHandle, FCombatSolver::FAssignIntent> Intents;
		FCombatSolver::ResolveUnitIntents(TMap<FUnitHandle, FUnitOrder>(), Records, Intents);
		const FCombatSolver::FAssignIntent* Resolved = Intents.Find(B);
		TestNotNull(TEXT("defensive intent"), Resolved);
		if (Resolved)
		{
			TestEqual(TEXT("defends lowest aggressor"), Resolved->TargetUnit, A);
			TestEqual(TEXT("defensive is not auto-engage"), Resolved->bAutoEngage, 0);
		}
	}

	// -- Designated AttackMove / AttackUnit never enter the table; the table
	//    RESETS between calls. (The record's DEFENDER must also be ordered
	//    away here, or demo-fix3 would give it a defensive intent.) --
	{
		TMap<FUnitHandle, FUnitOrder> Orders;
		FUnitOrder Designated;
		Designated.Type = FUnitOrder::EType::AttackMove;
		Designated.TargetUnit = C;
		Orders.Add(A, Designated);
		FUnitOrder Away;
		Away.Type = FUnitOrder::EType::Move;
		Orders.Add(B, Away);
		TArray<FEngagementKey> Records;
		Records.Add({ A, B });
		TMap<FUnitHandle, FCombatSolver::FAssignIntent> Intents;
		FCombatSolver::ResolveUnitIntents(Orders, Records, Intents);
		TestEqual(TEXT("designated AttackMove stays with the order path"), Intents.Num(), 0);

		// Reset semantics: a call with an unordered defender under fire fills
		// the table; the next call with no records must clear it.
		FCombatSolver::ResolveUnitIntents(TMap<FUnitHandle, FUnitOrder>(), Records, Intents);
		TestTrue(TEXT("idle unit under fire defends"), Intents.Contains(B));
		FCombatSolver::ResolveUnitIntents(Orders, TArray<FEngagementKey>(), Intents);
		TestEqual(TEXT("table resets with the record set"), Intents.Num(), 0);
	}

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
	// v2 (spec ranged-stack §3.5): non-default toggles + the once-only
	// out-of-ammo flip must survive the round trip.
	TestTrue(TEXT("fire toggles set"), Registry->SetUnitFireToggles(UnitA.Unit, false, true));
	TestTrue(TEXT("out-of-ammo flip"), Registry->FlipUnitOutOfAmmo(UnitA.Unit));
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
	TestEqual(TEXT("schema version"), Save.SchemaVersion, 2u);
	TestEqual(TEXT("units captured"), Save.Units.Num(), 2);
	TestEqual(TEXT("engagement captured"), Save.Engagements.Num(), 1);
	TestTrue(TEXT("rng streams captured"), Save.RngStreams.Num() >= 2);
	TestEqual(TEXT("root seed captured"), Save.RngRootSeed, 424242);
	{
		const FUnitSaveEntry* EntryA = Save.Units.FindByPredicate(
			[&](const FUnitSaveEntry& E) { return E.Index == UnitA.Unit.Index; });
		if (TestNotNull(TEXT("unit A entry"), EntryA))
		{
			TestEqual(TEXT("captured FireAtWill"), EntryA->bFireAtWill, 0);
			TestEqual(TEXT("captured Skirmish"), EntryA->bSkirmish, 1);
			TestEqual(TEXT("captured OutOfAmmo"), EntryA->bOutOfAmmo, 1);
		}
	}

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
		// v2: toggles + out-of-ammo survive capture/clear/restore (spec §3.5).
		TestFalse(TEXT("FireAtWill back"), RestoredA->bFireAtWill);
		TestTrue(TEXT("Skirmish back"), RestoredA->bSkirmish);
		TestTrue(TEXT("OutOfAmmo back"), RestoredA->bOutOfAmmo);
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

	// Legacy v1 payload: the toggles are absent -> restore takes the defaults
	// (FireAtWill on, Skirmish/OutOfAmmo off, denominators 0; spec §3.5).
	Save.SchemaVersion = 1;
	TestTrue(TEXT("legacy restore ok"), Registry->RestoreBattleState(Save));
	{
		const FUnitRuntimeState* LegacyA = Registry->FindUnitState(UnitA.Unit);
		if (TestNotNull(TEXT("unit A state back (legacy)"), LegacyA))
		{
			TestTrue(TEXT("legacy FireAtWill defaults on"), LegacyA->bFireAtWill);
			TestFalse(TEXT("legacy Skirmish defaults off"), LegacyA->bSkirmish);
			TestFalse(TEXT("legacy OutOfAmmo defaults off"), LegacyA->bOutOfAmmo);
			TestEqual(TEXT("legacy denominators default 0"), LegacyA->RangedInitialAmmo + LegacyA->InitialRosterSize, 0);
		}
	}
	TestEqual(TEXT("bookkeeping hash independent of toggles"),
		Registry->ComputeBookkeepingHash(), HashBefore);

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

	// Phase 3 (spec ranged-stack §3.1): ranged rows carry FRangedStateFragment +
	// FBowTag on a SEPARATE composition (arch §3.2); bow and crossbow share it
	// (Kind is a fragment value). The growth is MEASURED here and itemized in
	// the phase report (F6 disclosed re-sign); the invariants asserted are:
	// ranged fragments present on ranged rosters, absent on melee rosters,
	// identical-run determinism, and a non-negative growth delta.
	int32 ArchetypesRanged = -1;
	bool bRangedFragmentsCorrect = true;
	{
		UWorld* World = MakeCombatTestWorld();
		const FBattleSpawnResult MeleeA = SpawnRow(World, Rows[0]);
		const FBattleSpawnResult RangedA = SpawnRow(World, Rows[2]);
		const FBattleSpawnResult RangedB = SpawnRow(World, Rows[3]);
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
#if WITH_MASSENTITY_DEBUG
		ArchetypesRanged = EntityManager.DebugGetArchetypesCount();
#endif
		const auto CheckRoster = [&](const FBattleSpawnResult& Result, bool bExpectRanged)
		{
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Result.Unit);
			if (!TestNotNull(TEXT("roster"), Roster) || Roster->Num() == 0)
			{
				return;
			}
			for (const FMassEntityHandle& Entity : *Roster)
			{
				const FMassEntityView View(EntityManager, Entity);
				const bool bHasRanged = View.GetFragmentDataPtr<FRangedStateFragment>() != nullptr;
				bRangedFragmentsCorrect = bRangedFragmentsCorrect && (bHasRanged == bExpectRanged);
			}
		};
		CheckRoster(MeleeA, /*bExpectRanged*/ false);
		CheckRoster(RangedA, /*bExpectRanged*/ true);
		CheckRoster(RangedB, /*bExpectRanged*/ true);
#if WITH_MASSENTITY_DEBUG
		TestTrue(TEXT("ranged rows grow the archetype count (composition added)"), ArchetypesRanged >= ArchetypesA);
		AddInfo(FString::Printf(TEXT("archetypes: melee-only=%d with-ranged=%d (growth %d, one ranged composition expected; itemized in the phase report)"),
				ArchetypesA, ArchetypesRanged, ArchetypesRanged - ArchetypesA));
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
	TestTrue(TEXT("ranged fragments: present on ranged rosters, absent on melee rosters"), bRangedFragmentsCorrect);
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

	// Phase 3 (spec ranged-stack §3.4): ranged terms on a ranged-row roster +
	// the pool block. The archer row carries the fragment; the melee roster
	// above proves the zero-fill stability (its hash already round-trips).
	if (Rows.Num() > 2)
	{
		const FBattleSpawnResult UnitB = SpawnRow(World, Rows[2]); // Peasant Archers
		TestTrue(TEXT("ranged row spawned"), UnitB.Unit.IsValid());
		const TArray<FMassEntityHandle>* RosterB = Registry->FindRoster(UnitB.Unit);
		if (TestNotNull(TEXT("ranged roster"), RosterB) && RosterB->Num() > 0)
		{
			const uint64 HashR = FBattleStateHash::Compute(World);
			TestEqual(TEXT("ranged hash stable"), FBattleStateHash::Compute(World), HashR);

			FMassEntityView ViewB(EntityManager, (*RosterB)[0]);
			FRangedStateFragment& Ranged = ViewB.GetFragmentData<FRangedStateFragment>();

			// Ammo mutation.
			const int16 OldAmmo = Ranged.AmmoCount;
			Ranged.AmmoCount = static_cast<int16>(OldAmmo - 1);
			TestNotEqual(TEXT("ammo mutation changes hash"), FBattleStateHash::Compute(World), HashR);
			Ranged.AmmoCount = OldAmmo;
			// Reload mutation (above the 1e-2 quantum).
			const float OldReload = Ranged.ReloadTimer;
			Ranged.ReloadTimer = OldReload + 0.5f;
			TestNotEqual(TEXT("reload mutation changes hash"), FBattleStateHash::Compute(World), HashR);
			Ranged.ReloadTimer = OldReload;
			// Shots fired mutation.
			const uint16 OldShots = Ranged.ShotsFired;
			Ranged.ShotsFired = OldShots + 1;
			TestNotEqual(TEXT("shots fired mutation changes hash"), FBattleStateHash::Compute(World), HashR);
			Ranged.ShotsFired = OldShots;
			TestEqual(TEXT("ranged reverts restore hash"), FBattleStateHash::Compute(World), HashR);

			// The pool block: an acquired entry mutates the hash; release does too.
			UBattleProjectileSubsystem* Projectiles = World->GetSubsystem<UBattleProjectileSubsystem>();
			if (TestNotNull(TEXT("projectile subsystem"), Projectiles))
			{
				Projectiles->ReserveForBattle();
				uint32 Gen = 0;
				const int32 Slot = Projectiles->GetMutablePool().Acquire(Gen);
				TestTrue(TEXT("pool slot acquired"), Slot != FBattleProjectilePool::InvalidIndex);
				Projectiles->GetMutablePool().PosX[Slot] = 42.5f;
				Projectiles->GetMutablePool().PosZ[Slot] = 3.25f;
				Projectiles->GetMutablePool().TTL[Slot] = 1.75f;
				TestNotEqual(TEXT("pool entry changes hash"), FBattleStateHash::Compute(World), HashR);
				Projectiles->GetMutablePool().PosX[Slot] = 43.0f;
				TestNotEqual(TEXT("pool position mutation changes hash"), FBattleStateHash::Compute(World), HashR);
				Projectiles->GetMutablePool().Release(Slot);
				TestEqual(TEXT("pool release restores hash"), FBattleStateHash::Compute(World), HashR);
			}
		}
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Phase 3 task 1: the registry ranged data spine (spec ranged-stack §3.2,
// §4.10) -- spawn stamp defaults, the toggle surface + bad-handle rejects,
// the live ammo fraction, the once-only out-of-ammo flip, and the
// RangedDump evidence line. Also probes the archetype-count stability
// across ranged-capable spawns (F6 watch).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_RangedRegistrySpine,
	"Battle.Unit.Ranged.RegistrySpine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_RangedRegistrySpine::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	UMassEntitySubsystem* EntitySubsystem = World->GetSubsystem<UMassEntitySubsystem>();
	TestNotNull(TEXT("registry"), Registry);
	if (!Registry || !EntitySubsystem)
	{
		World->DestroyWorld(false);
		return false;
	}

	TArray<FEduUnit> Rows;
	if (!LoadCombatFixture(Rows) || Rows.Num() < 4)
	{
		AddError(TEXT("EDU fixture unavailable"));
		World->DestroyWorld(false);
		return false;
	}

	// Archetype-count stability (F6 watch): one world per composition mix,
	// identical mixes yield identical counts; the ranged fragment rides
	// ranged-capable compositions only. Measured here per tier; the 8-count
	// alive/dead assertion is the scenario/soak gate (Tasks 6/7).
	int32 ArchetypesTwoRows = -1;
	{
		const FBattleSpawnResult Melee = SpawnRow(World, Rows[0]);
		const FBattleSpawnResult Archers = SpawnRow(World, Rows[2]);
		TestTrue(TEXT("rows spawned"), Melee.Unit.IsValid() && Archers.Unit.IsValid());
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
#if WITH_MASSENTITY_DEBUG
		ArchetypesTwoRows = EntityManager.DebugGetArchetypesCount();
#endif
		AddInfo(FString::Printf(TEXT("archetypes: two-row world=%d"), ArchetypesTwoRows));

		// -- Spawn stamp defaults (A8): missile class -> skirmish ON --
		bool bFireAtWill = false;
		bool bSkirmish = false;
		bool bOutOfAmmo = false;
		Registry->GetUnitFireToggles(Archers.Unit, bFireAtWill, bSkirmish, bOutOfAmmo);
		TestTrue(TEXT("archer FireAtWill default on"), bFireAtWill);
		TestTrue(TEXT("archer Skirmish default on (missile class)"), bSkirmish);
		TestFalse(TEXT("archer OutOfAmmo default off"), bOutOfAmmo);
		Registry->GetUnitFireToggles(Melee.Unit, bFireAtWill, bSkirmish, bOutOfAmmo);
		TestFalse(TEXT("melee Skirmish default off"), bSkirmish);

		// The RangedDump evidence pass (spec 4.10): one line per unit,
		// Ranged=1 Skirmish=1 on the archer row, Ranged=0 on the levy.
		BattleExec::RangedDump({}, World);

		// -- Live ammo fraction (RC5) --
		const TArray<FMassEntityHandle>* RosterA = Registry->FindRoster(Archers.Unit);
		if (!TestNotNull(TEXT("archer roster"), RosterA) || RosterA->Num() == 0)
		{
			World->DestroyWorld(false);
			return false;
		}
		const int32 N = RosterA->Num();
		const int32 InitialAmmo = 25; // fixture stat_pri ammo for the archer row
		TestEqual(TEXT("fraction 1 at full quiver"), Registry->GetUnitAmmoFraction(Archers.Unit), 1.0f, 1e-4f);
		TestEqual(TEXT("melee fraction reads -1"), Registry->GetUnitAmmoFraction(Melee.Unit), -1.f);

		// One decrement moves the fraction by exactly one round.
		FMassEntityView(EntityManager, (*RosterA)[0]).GetFragmentData<FRangedStateFragment>().AmmoCount--;
		TestEqual(TEXT("fraction moves by one decrement"), Registry->GetUnitAmmoFraction(Archers.Unit),
			static_cast<float>(N * InitialAmmo - 1) / static_cast<float>(N * InitialAmmo), 1e-4f);

		// -- The once-only out-of-ammo flip (A14) at aggregate zero --
		for (const FMassEntityHandle& Entity : *RosterA)
		{
			FMassEntityView(EntityManager, Entity).GetFragmentData<FRangedStateFragment>().AmmoCount = 0;
		}
		TestEqual(TEXT("fraction 0 at empty aggregate"), Registry->GetUnitAmmoFraction(Archers.Unit), 0.0f, 1e-4f);
		TestTrue(TEXT("flip at zero returns true"), Registry->FlipUnitOutOfAmmo(Archers.Unit));
		TestFalse(TEXT("second flip is a no-op"), Registry->FlipUnitOutOfAmmo(Archers.Unit));
		Registry->GetUnitFireToggles(Archers.Unit, bFireAtWill, bSkirmish, bOutOfAmmo);
		TestTrue(TEXT("OutOfAmmo latched"), bOutOfAmmo);

		// -- Toggle surface + bad-handle rejects (A11 lines) --
		TestTrue(TEXT("toggles set"), Registry->SetUnitFireToggles(Archers.Unit, false, false));
		Registry->GetUnitFireToggles(Archers.Unit, bFireAtWill, bSkirmish, bOutOfAmmo);
		TestFalse(TEXT("FireAtWill cleared"), bFireAtWill);
		TestFalse(TEXT("Skirmish cleared"), bSkirmish);
		TestTrue(TEXT("OutOfAmmo survives toggles"), bOutOfAmmo);
		BattleExec::RangedDump({ FString::FromInt(Archers.Unit.Index) }, World); // the toggled unit's line: FireAtWill=0
		FUnitHandle Bad;
		Bad.Index = 987u;
		Bad.Serial = 1u;
		TestFalse(TEXT("toggles reject bad handle"), Registry->SetUnitFireToggles(Bad, true, true));
		TestFalse(TEXT("flip rejects bad handle"), Registry->FlipUnitOutOfAmmo(Bad));
		BattleExec::RangedToggle({ TEXT("999"), TEXT("FireAtWill"), TEXT("0") }, World);
	}

	// Identical re-spawn -> identical archetype count (the SpawnComposition
	// discipline, now with a ranged row in the mix).
	{
		World->DestroyWorld(false);
		World = MakeCombatTestWorld();
		SpawnRow(World, Rows[0]);
		SpawnRow(World, Rows[2]);
#if WITH_MASSENTITY_DEBUG
		const int32 ArchetypesAgain = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().DebugGetArchetypesCount();
		TestEqual(TEXT("archetype count stable across identical ranged spawns"), ArchetypesAgain, ArchetypesTwoRows);
#endif
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Phase 3 task 4: the live fire pass (RC1/RC4/RC5/RC7 halves at unit tier --
// the processor core is the exported RunRangedFirePass, driven here without
// a Mass tick). Covers: eligibility -> shots -> pool entries -> decrements
// (RC1/RC5), the reload cycle, the arced impact-zone friendly hold (A7),
// the once-only out-of-ammo flip, flags-off identity, and the skirmish
// retreat / resume / player-cancel triple (RC7, A8 serial guard).
// ---------------------------------------------------------------------------

namespace
{
	/** Combat spawn with position/facing/team (the fire fixtures' shape). */
	FUnitHandle SpawnRowAt(UWorld* World, const FEduUnit& EduRow, const FVector2D& PosM, float YawDeg, uint8 TeamId)
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(EduRow, Stats, Error))
		{
			return FUnitHandle();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = 0;
		Spec.SoldierCount = FMath::Max(1, Stats.SoldierCount);
		Spec.UnitName = Stats.TypeName;
		Spec.SpawnPos = PosM * 100.f;
		Spec.SpawnYaw = YawDeg;
		Spec.NumRanks = 2;
		Spec.TeamId = TeamId;
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats).Unit;
	}

	/** Builds the combat spatial hash from every live unit's roster (the
	 *  BuildSpatialHashProcessor's shape, driven manually for the test tier). */
	void BuildTestHash(UWorld* World, UUnitRegistrySubsystem* Registry)
	{
		UBattleSpatialHashSubsystem* Hash = World->GetSubsystem<UBattleSpatialHashSubsystem>();
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		TArray<FBattleHashEntry> Entries;
		TArray<FVector2D> PositionsM;
		for (const FUnitHandle& Handle : Registry->GetSortedHandles())
		{
			const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
			if (!Roster)
			{
				continue;
			}
			for (const FMassEntityHandle& Entity : *Roster)
			{
				FBattleHashEntry Entry;
				Entry.Entity = Entity;
				Entry.Unit = Handle;
				const FTransformFragment* Transform = FMassEntityView(EM, Entity).GetFragmentDataPtr<FTransformFragment>();
				const FVector P = Transform ? Transform->GetTransform().GetTranslation() : FVector::ZeroVector;
				Entries.Add(Entry);
				PositionsM.Add(FVector2D(P.X, P.Y) * 0.01f);
			}
		}
		Hash->Reserve(FMath::Max(512, Entries.Num()));
		Hash->RebuildWithPositions(MoveTemp(Entries), MoveTemp(PositionsM));
	}

	/** Session-on ranged flags with restore (the EnableDemoCombatPipeline
	 *  pattern; the test restores whatever it changed). */
	struct FRangedFlagScope
	{
		IConsoleVariable* Enable;
		IConsoleVariable* Fire;
		IConsoleVariable* Skirmish;
		int32 OldEnable;
		int32 OldFire;
		int32 OldSkirmish;

		explicit FRangedFlagScope(bool bSkirmishOn = true)
		{
			Enable = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.Enable"));
			Fire = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.Fire"));
			Skirmish = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.Skirmish"));
			OldEnable = Enable ? Enable->GetInt() : 0;
			OldFire = Fire ? Fire->GetInt() : 0;
			OldSkirmish = Skirmish ? Skirmish->GetInt() : 0;
			if (Enable) { Enable->Set(1, ECVF_SetByCode); }
			if (Fire) { Fire->Set(1, ECVF_SetByCode); }
			if (Skirmish && bSkirmishOn) { Skirmish->Set(1, ECVF_SetByCode); }
		}
		~FRangedFlagScope()
		{
			if (Enable) { Enable->Set(OldEnable, ECVF_SetByCode); }
			if (Fire) { Fire->Set(OldFire, ECVF_SetByCode); }
			if (Skirmish) { Skirmish->Set(OldSkirmish, ECVF_SetByCode); }
		}
	};

	void ZeroReloadTimers(UWorld* World, UUnitRegistrySubsystem* Registry, const FUnitHandle& Handle)
	{
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle))
		{
			for (const FMassEntityHandle& Entity : *Roster)
			{
				if (FRangedStateFragment* Ranged = FMassEntityView(EM, Entity).GetFragmentDataPtr<FRangedStateFragment>())
				{
					Ranged->ReloadTimer = 0.f;
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_RangedFirePass,
	"Battle.Unit.Ranged.FirePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_RangedFirePass::RunTest(const FString& Parameters)
{
	constexpr float DT = 1.f / 60.f;
	FRangedFlagScope Flags;

	// ---- Flow A: the fire pipeline (RC1/RC5) + out-of-ammo + flags-off. --
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UBattleProjectileSubsystem* Projectiles = World->GetSubsystem<UBattleProjectileSubsystem>();
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			AddError(TEXT("EDU fixture unavailable"));
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(-40.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(40.f, 0.f), 180.f, 1);
		TestTrue(TEXT("flow A spawned"), Archers.IsValid() && Enemy.IsValid());
		TestTrue(TEXT("flow A attack order"), Registry->IssueAttackUnitOrder(Archers, Enemy));
		ZeroReloadTimers(World, Registry, Archers);
		BuildTestHash(World, Registry);

		// First volley: every shooter eligible, every shot accounted (RC1/RC5).
		RunRangedFirePass(World, DT);
		const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Archers);
		TestNotNull(TEXT("archer roster"), Roster);
		int32 ShotsFiredSum = 0;
		int32 AmmoSum = 0;
		if (Roster)
		{
			for (const FMassEntityHandle& Entity : *Roster)
			{
				const FRangedStateFragment* Ranged = FMassEntityView(EM, Entity).GetFragmentDataPtr<FRangedStateFragment>();
				if (Ranged)
				{
					ShotsFiredSum += Ranged->ShotsFired;
					AmmoSum += Ranged->AmmoCount;
					TestTrue(TEXT("reload armed after firing"), Ranged->ReloadTimer > 0.f);
				}
			}
		}
		const int32 RosterSize = Roster ? Roster->Num() : 0;
		TestEqual(TEXT("shots == roster (one volley, one shot each)"), ShotsFiredSum, RosterSize);
		TestEqual(TEXT("ammo decremented once per shot"), AmmoSum, RosterSize * 25 - RosterSize);
		TestEqual(TEXT("pool holds the volley"), Projectiles->GetPool().GetLiveCount(), RosterSize);
		TestEqual(TEXT("stamped attack term is the row's RangedAttack"), Projectiles->GetPool().RangedAttack[0], 6.f, 1e-4f);

		// Immediate second pass: the reload cycle holds (0 shots, eligible held).
		RunRangedFirePass(World, DT);
		TestEqual(TEXT("reload cycle suppresses the second volley"), Projectiles->GetPool().GetLiveCount(), RosterSize);

		// Drain to out-of-ammo: the flip fires exactly once (RC5/A14).
		if (Roster)
		{
			for (const FMassEntityHandle& Entity : *Roster)
			{
				if (FRangedStateFragment* Ranged = FMassEntityView(EM, Entity).GetFragmentDataPtr<FRangedStateFragment>())
				{
					Ranged->AmmoCount = 1;
					Ranged->ReloadTimer = 0.f;
				}
			}
		}
		RunRangedFirePass(World, DT);
		const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
		TestNotNull(TEXT("archer state"), State);
		TestTrue(TEXT("out-of-ammo flipped"), State != nullptr && State->bOutOfAmmo);
		RunRangedFirePass(World, DT);
		TestEqual(TEXT("spent unit never fires again"), Projectiles->GetPool().GetLiveCount(), 2 * RosterSize);

		// Flags-off identity: the pass is a no-op with Enable=0 (B.8 item 6);
		// the flag comes back ON inside this block -- flows B/C run after it.
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Ranged.Enable")))
		{
			C->Set(0, ECVF_SetByCode);
			const uint32 PoolLiveBefore = Projectiles->GetPool().GetLiveCount();
			RunRangedFirePass(World, DT);
			TestEqual(TEXT("flags-off pass is a no-op"), Projectiles->GetPool().GetLiveCount(), PoolLiveBefore);
			C->Set(1, ECVF_SetByCode);
		}

		World->DestroyWorld(false);
	}

	// ---- Flow B: the arced impact-zone friendly hold (A7). ----
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(-40.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(40.f, 0.f), 180.f, 1);
		SpawnRowAt(World, Rows[0], FVector2D(37.f, 0.f), 0.f, 0); // friendly INSIDE the impact zone
		TestTrue(TEXT("flow B attack order"), Registry->IssueAttackUnitOrder(Archers, Enemy));
		ZeroReloadTimers(World, Registry, Archers);
		BuildTestHash(World, Registry);

		RunRangedFirePass(World, DT);
		TestTrue(TEXT("no spawn skipped the fixture"), Archers.IsValid());
		TestEqual(TEXT("arced hold: no shots with friends in the zone"), World->GetSubsystem<UBattleProjectileSubsystem>()->GetPool().GetLiveCount(), 0);
		// The Holds counter proves it was a RULE hold, not a bug-hold (R-R6):
		// all 40 eligible shooters held.
		UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
		TestEqual(TEXT("rule-holds counted for the whole unit"), Debug ? Debug->GetRangedHolds() : -1, 40);
		World->DestroyWorld(false);
	}

	// ---- Flow C: skirmish retreat / resume / player cancel (RC7). ----
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(0.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(15.f, 0.f), 180.f, 1);
		TestTrue(TEXT("flow C spawned"), Archers.IsValid() && Enemy.IsValid());
		// Skirmish defaults ON for the missile class (A8); assert the stamp.
		{
			bool bFAW = false, bSkirmish = false, bOoA = false;
			Registry->GetUnitFireToggles(Archers, bFAW, bSkirmish, bOoA);
			TestTrue(TEXT("skirmish stamped on at spawn"), bSkirmish);
		}

		// Inside the break distance + idle -> the auto-retreat move issues.
		RunRangedFirePass(World, DT);
		{
			const FUnitOrder* Order = Registry->FindOrder(Archers);
			const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
			TestNotNull(TEXT("retreat order issued"), Order);
			TestEqual(TEXT("retreat is a Move"), Order ? static_cast<int32>(Order->Type) : -1, static_cast<int32>(FUnitOrder::EType::Move));
			if (Order)
			{
				// Directly away from the threat: (0,0) - (15,0) -> anchor (-50, 0) m.
				const int32 AnchorX = FMath::RoundToInt(Order->AnchorPos.X);
				TestEqual(TEXT("retreat anchor is 50 m away, opposite the threat"), AnchorX, -5000);
			}
			TestTrue(TEXT("retreating latched"), State != nullptr && State->bRetreating);
		}

		// Re-run while retreating: no re-issue churn (the order stands).
		RunRangedFirePass(World, DT);
		{
			const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
			TestTrue(TEXT("retreat still ours"), State != nullptr && State->bRetreating);
		}

		// The threat leaves past the resume distance -> stop + resume fire.
		if (const TArray<FMassEntityHandle>* EnemyRoster = Registry->FindRoster(Enemy))
		{
			if (FUnitFormationSharedFragment* Formation =
				EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*EnemyRoster)[0]))
			{
				Formation->AnchorPos = FVector2D(10000.f, 0.f); // 100 m east
			}
		}
		RunRangedFirePass(World, DT);
		{
			const FUnitOrder* Order = Registry->FindOrder(Archers);
			const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
			TestEqual(TEXT("resume stops the retreat"), Order ? static_cast<int32>(Order->Type) : -1, static_cast<int32>(FUnitOrder::EType::Stop));
			TestFalse(TEXT("retreating cleared at resume"), State ? State->bRetreating : true);
		}

		// Threat returns -> retreat again; a player Move then cancels it
		// (the RC7 order-serial guard) and is never overridden.
		if (const TArray<FMassEntityHandle>* EnemyRoster = Registry->FindRoster(Enemy))
		{
			if (FUnitFormationSharedFragment* Formation =
				EM.GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*EnemyRoster)[0]))
			{
				Formation->AnchorPos = FVector2D(1500.f, 0.f); // 15 m east again
			}
		}
		RunRangedFirePass(World, DT);
		{
			const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
			TestTrue(TEXT("second retreat latched"), State != nullptr && State->bRetreating);
		}
		const FVector2D PlayerAnchorCm(2000.f, 3000.f);
		TestTrue(TEXT("player move issued"), Registry->IssueMoveOrder(Archers, PlayerAnchorCm, 0.f));
		RunRangedFirePass(World, DT);
		{
			const FUnitOrder* Order = Registry->FindOrder(Archers);
			const FUnitRuntimeState* State = Registry->FindUnitState(Archers);
			const FVector2D Actual = Order ? Order->AnchorPos : FVector2D(-1.f, -1.f);
			TestEqual(TEXT("player order survives the fire pass (X)"), Actual.X, PlayerAnchorCm.X, 1e-2);
			TestEqual(TEXT("player order survives the fire pass (Y)"), Actual.Y, PlayerAnchorCm.Y, 1e-2);
			TestFalse(TEXT("retreat cancelled by the player order"), State ? State->bRetreating : true);
		}

		World->DestroyWorld(false);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Phase 3 task 5: impact resolution (RC6) -- arrows kill through the melee
// machinery. Flow A: a volley against a formed line lands in the golden-rate
// band, deaths land in the FRONTAL arc rows (DeathsByArc), and the deferred
// FDeadTag count equals the arc ledger. Flow B: a friendly line behind the
// target takes the team-blind stray hits (RC4's impact-point half). Flow C:
// aimed shots walk the composite-HP ladder (mount drain -> kill through the
// F2 death path) with no statistics involved.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_RangedImpacts,
	"Battle.Unit.Ranged.Impacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_RangedImpacts::RunTest(const FString& Parameters)
{
	constexpr float DT = 1.f / 60.f;
	FRangedFlagScope Flags;

	// ---- Flow A: the volley kill band + the frontal arc rows. ----
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			AddError(TEXT("EDU fixture unavailable"));
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(-40.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(40.f, 0.f), 180.f, 1);
		TestTrue(TEXT("flow A spawned"), Archers.IsValid() && Enemy.IsValid());
		TestTrue(TEXT("flow A attack order"), Registry->IssueAttackUnitOrder(Archers, Enemy));
		ZeroReloadTimers(World, Registry, Archers);
		BuildTestHash(World, Registry);
		RunRangedFirePass(World, DT);
		UBattleProjectileSubsystem* Projectiles = World->GetSubsystem<UBattleProjectileSubsystem>();
		TestEqual(TEXT("flow A volley in flight"), Projectiles->GetPool().GetLiveCount(), 40);

		for (int32 Tick = 0; Tick < 300; ++Tick)
		{
			RunProjectileStepPass(World, DT);
		}
		TestEqual(TEXT("all projectiles resolved"), Projectiles->GetPool().GetLiveCount(), 0);
		const int32 Impacts = Debug->GetRangedImpacts();
		TestTrue(TEXT("the line was hit"), Impacts > 0);

		EM.FlushCommands(); // the deferred FDeadTag batch lands; the observer fires
		const TArray<FMassEntityHandle>* EnemyRoster = Registry->FindRoster(Enemy);
		TestNotNull(TEXT("enemy roster"), EnemyRoster);
		int32 Dead = 0;
		if (EnemyRoster)
		{
			for (const FMassEntityHandle& Entity : *EnemyRoster)
			{
				if (FMassEntityView(EM, Entity).HasTag<FDeadTag>())
				{
					++Dead;
				}
			}
		}
		// The golden-rate band: frontal levy vs arrow volley. P derives from
		// the VICTIM's actual stats (D = defense + shield frontal + armor,
		// the levy fragment reads defense 2 / shield 0 / armor 0) times the
		// shooter's lethality. Tolerance guards cross-machine float drift on
		// the DETERMINISTIC roll sequence (same seed -> same rolls; only the
		// numeric P moves across compilers).
		float VictimDefense = 0.f;
		float VictimShield = 0.f;
		float VictimArmor = 0.f;
		float ArcherLethality = 1.f;
		if (EnemyRoster && EnemyRoster->Num() > 0)
		{
			if (const FBaseStatsFragment* Base = FMassEntityView(EM, (*EnemyRoster)[0]).GetFragmentDataPtr<FBaseStatsFragment>())
			{
				VictimDefense = Base->MeleeDefense;
				VictimShield = Base->Shield;
				VictimArmor = Base->Armor;
			}
		}
		if (const TArray<FMassEntityHandle>* ShooterRoster = Registry->FindRoster(Archers))
		{
			if (const FBaseStatsFragment* Base = FMassEntityView(EM, (*ShooterRoster)[0]).GetFragmentDataPtr<FBaseStatsFragment>())
			{
				ArcherLethality = Base->Lethality;
			}
		}
		const float P = FMeleeMath::HitChance(6.f,
			VictimDefense + VictimShield + VictimArmor) * ArcherLethality;
		const float Expected = static_cast<float>(Impacts) * P;
		const int32 BandMin = FMath::Max(1, FMath::FloorToInt(0.45f * Expected));
		const int32 BandMax = FMath::CeilToInt(1.55f * Expected) + 1;
		TestTrue(FString::Printf(TEXT("kills %d in the golden band [%d, %d] of %d impacts (P=%.3f incl. lethality %.2f)"),
			Dead, BandMin, BandMax, Impacts, P, ArcherLethality), Dead >= BandMin && Dead <= BandMax);
		// Arc ledger == the deferred kills (the ranged rows joined DeathsByArc).
		TestEqual(TEXT("DeathsByArc ledger equals the dead"), Dead,
			Debug->GetDeathsByArc(0) + Debug->GetDeathsByArc(1) + Debug->GetDeathsByArc(2));
		TestTrue(TEXT("frontal rows populated (arrows from the levy's front)"), Debug->GetDeathsByArc(0) > 0);
		World->DestroyWorld(false);
	}

	// ---- Flow B: team-blind friendly strays behind the target. ----
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UBattleDebugSubsystem* Debug = World->GetSubsystem<UBattleDebugSubsystem>();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(-40.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(40.f, 0.f), 180.f, 1);
		SpawnRowAt(World, Rows[0], FVector2D(46.f, 0.f), 0.f, 0); // friendly line BEHIND the target
		TestTrue(TEXT("flow B spawned"), Archers.IsValid() && Enemy.IsValid());
		TestTrue(TEXT("flow B attack order"), Registry->IssueAttackUnitOrder(Archers, Enemy));
		ZeroReloadTimers(World, Registry, Archers);
		BuildTestHash(World, Registry);
		RunRangedFirePass(World, DT);
		for (int32 Tick = 0; Tick < 300; ++Tick)
		{
			RunProjectileStepPass(World, DT);
		}
		// RC4's impact-point half, made deterministic: one aimed over-flight
		// shot climbs over the enemy line (Z 2.1 at x=40, above the 1.8 m
		// bodies) and descends onto the friendly line behind (Z ~1.5 at
		// x=46). The team-blind pick takes the friendly body.
		{
			UBattleProjectileSubsystem* Projectiles = World->GetSubsystem<UBattleProjectileSubsystem>();
			uint32 Gen = 0;
			FBattleProjectilePool& Pool = Projectiles->GetMutablePool();
			const int32 Slot = Pool.Acquire(Gen);
			TestTrue(TEXT("stray slot acquired"), Slot != FBattleProjectilePool::InvalidIndex);
			if (Slot != FBattleProjectilePool::InvalidIndex)
			{
				Pool.PosX[Slot] = 30.f;
				Pool.PosY[Slot] = 0.f;
				Pool.PosZ[Slot] = 2.5f;
				Pool.VelX[Slot] = 35.f;
				Pool.VelY[Slot] = 0.f;
				Pool.VelZ[Slot] = 0.f;
				Pool.GravityScale[Slot] = 1.f;
				Pool.TTL[Slot] = 5.f;
				Pool.Kind[Slot] = static_cast<uint8>(EBattleProjectileKind::Arrow);
				Pool.GenerationArray[Slot] = Gen;
				Pool.OwnerUnit[Slot] = Archers;
				Pool.ShooterEntity[Slot] = (*Registry->FindRoster(Archers))[1];
				Pool.RangedAttack[Slot] = 100.f;
				Pool.ShooterRollCount[Slot] = 0;
				Pool.Lethality[Slot] = 1.f;
			}
			for (int32 Tick = 0; Tick < 120; ++Tick)
			{
				RunProjectileStepPass(World, DT);
			}
		}
		TestTrue(TEXT("friendly strays demonstrable"), Debug->GetRangedFriendlyHits() > 0);
		World->DestroyWorld(false);
	}

	// ---- Flow C: the composite-HP ladder on aimed shots (no statistics). ----
	{
		UWorld* World = MakeCombatTestWorld();
		UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
		UMassEntitySubsystem* Entities = World->GetSubsystem<UMassEntitySubsystem>();
		UBattleProjectileSubsystem* Projectiles = World->GetSubsystem<UBattleProjectileSubsystem>();
		FMassEntityManager& EM = Entities->GetMutableEntityManager();
		TArray<FEduUnit> Rows;
		if (!LoadCombatFixture(Rows) || Rows.Num() < 3)
		{
			World->DestroyWorld(false);
			return false;
		}
		const FUnitHandle Archers = SpawnRowAt(World, Rows[2], FVector2D(-40.f, 0.f), 0.f, 0);
		const FUnitHandle Enemy = SpawnRowAt(World, Rows[0], FVector2D(40.f, 0.f), 180.f, 1);
		TestTrue(TEXT("flow C spawned"), Archers.IsValid() && Enemy.IsValid());
		BuildTestHash(World, Registry);
		Projectiles->ReserveForBattle();
		const TArray<FMassEntityHandle>* EnemyRoster = Registry->FindRoster(Enemy);
		if (!TestNotNull(TEXT("enemy roster"), EnemyRoster) || EnemyRoster->Num() == 0)
		{
			World->DestroyWorld(false);
			return false;
		}
		const FMassEntityHandle Victim = (*EnemyRoster)[0];
		const FTransformFragment* VictimTransform =
			FMassEntityView(EM, Victim).GetFragmentDataPtr<FTransformFragment>();
		FHealthFragment* VictimHealth = FMassEntityView(EM, Victim).GetFragmentDataPtr<FHealthFragment>();
		if (!TestNotNull(TEXT("victim transform"), VictimTransform) || !TestNotNull(TEXT("victim health"), VictimHealth))
		{
			World->DestroyWorld(false);
			return false;
		}
		const FVector VictimPos = VictimTransform->GetTransform().GetTranslation();
		VictimHealth->MountCurHP = 2; // the composite ladder: two drains, then kill

		auto FireAimedShot = [&]() -> void
		{
			uint32 Gen = 0;
			FBattleProjectilePool& Pool = Projectiles->GetMutablePool();
			const int32 Slot = Pool.Acquire(Gen);
			if (Slot == FBattleProjectilePool::InvalidIndex)
			{
				return;
			}
			Pool.PosX[Slot] = VictimPos.X * 0.01f - 10.f;
			Pool.PosY[Slot] = VictimPos.Y * 0.01f;
			Pool.PosZ[Slot] = 1.0f;
			Pool.VelX[Slot] = 35.f;
			Pool.VelY[Slot] = 0.f;
			Pool.VelZ[Slot] = 0.f;
			Pool.GravityScale[Slot] = 1.f;
			Pool.TTL[Slot] = 5.f;
			Pool.Kind[Slot] = static_cast<uint8>(EBattleProjectileKind::Arrow);
			Pool.GenerationArray[Slot] = Gen;
			Pool.OwnerUnit[Slot] = Archers;
			Pool.ShooterEntity[Slot] = (*Registry->FindRoster(Archers))[1];
			Pool.RangedAttack[Slot] = 100.f; // near-certain land: P = 100/103
			Pool.ShooterRollCount[Slot] = 0;
			Pool.Lethality[Slot] = 1.f;
		};

		// Shot 1: the mount pool takes the hit (alive, drained to 1).
		FireAimedShot();
		for (int32 Tick = 0; Tick < 60; ++Tick) { RunProjectileStepPass(World, DT); }
		TestEqual(TEXT("first hit drains the mount pool"), VictimHealth->MountCurHP, 1);
		TestFalse(TEXT("drained victim still stands"), FMassEntityView(EM, Victim).HasTag<FDeadTag>());

		// Shot 2: the pool drains to 0 -- the rider still stands.
		FireAimedShot();
		for (int32 Tick = 0; Tick < 60; ++Tick) { RunProjectileStepPass(World, DT); }
		TestEqual(TEXT("pool at zero, no kill yet"), VictimHealth->MountCurHP, 0);
		TestFalse(TEXT("zero-pool victim still stands"), FMassEntityView(EM, Victim).HasTag<FDeadTag>());

		// Shot 3: a landed hit at an empty pool kills through the F2 path.
		FireAimedShot();
		for (int32 Tick = 0; Tick < 60; ++Tick) { RunProjectileStepPass(World, DT); }
		EM.FlushCommands();
		TestTrue(TEXT("kill deferred through the death path"), FMassEntityView(EM, Victim).HasTag<FDeadTag>());

		World->DestroyWorld(false);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T2.1-T2.3 unit gates (tasks 3-4 own the bodies)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// T2.1 (task 3): the resolution seam golden table. Any diff against these
// CHECKED-IN literals is a conscious formula review (A6/ADR-26). Covers: the
// contested curve + clamps, the sector quantizer + arc bands (frontal/flank/
// rear/boundaries), AP halving, cycle stagger determinism, charge decay, and
// the bounded knockback.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitChanceGolden,
	"Battle.Unit.Combat.HitChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitChanceGolden::RunTest(const FString& Parameters)
{
	// -- Contested curve: P = A / (A + D), clamped [Floor, Cap] = [0.05, 0.95].
	TestEqual(TEXT("golden (6,4) = 0.6"), FMeleeMath::HitChance(6.f, 4.f), 0.6f);
	TestEqual(TEXT("golden (1,1) = 0.5"), FMeleeMath::HitChance(1.f, 1.f), 0.5f);
	TestEqual(TEXT("golden (3,1) = 0.75"), FMeleeMath::HitChance(3.f, 1.f), 0.75f);
	TestEqual(TEXT("golden (1,3) = 0.25"), FMeleeMath::HitChance(1.f, 3.f), 0.25f);
	// Clamps: 0 attack bottoms at the floor; overwhelming attack caps.
	TestEqual(TEXT("floor at 0 attack"), FMeleeMath::HitChance(0.f, 10.f), 0.05f);
	TestEqual(TEXT("cap at 20v1 (0.95238 -> 0.95)"), FMeleeMath::HitChance(20.f, 1.f), 0.95f);
	TestEqual(TEXT("cap at 5v0"), FMeleeMath::HitChance(5.f, 0.f), 0.95f);
	// Both-zero: the curve denominator guard -> 0 -> floor.
	TestEqual(TEXT("both zero -> floor"), FMeleeMath::HitChance(0.f, 0.f), 0.05f);

	// -- Sector quantizer (floor buckets of 256/SectorCount; boundaries owned
	//    by the higher bucket). SectorCount 8: bucket = angle / 32.
	TestEqual(TEXT("angle 0 -> sector 0"), FCombatSolver::SectorIndexFromAngle(0, 8), 0);
	TestEqual(TEXT("angle 31 -> sector 0"), FCombatSolver::SectorIndexFromAngle(31, 8), 0);
	TestEqual(TEXT("boundary 32 -> sector 1"), FCombatSolver::SectorIndexFromAngle(32, 8), 1);
	TestEqual(TEXT("angle 128 -> sector 4"), FCombatSolver::SectorIndexFromAngle(128, 8), 4);
	TestEqual(TEXT("angle 255 -> sector 7"), FCombatSolver::SectorIndexFromAngle(255, 8), 7);
	// Round-trip: a sector center always quantizes back to its sector.
	for (int32 Sector = 0; Sector < 8; ++Sector)
	{
		const uint8 Center = FCombatSolver::SectorCenterAngle(Sector, 8);
		TestEqual(FString::Printf(TEXT("sector %d center round-trips"), Sector),
			FCombatSolver::SectorIndexFromAngle(Center, 8), Sector);
	}

	// -- Arc bands (8 sectors; centers 22.5/67.5/112.5/157.5/202.5/247.5/292.5/
	//    337.5 deg): frontal = {0,7}, flank = {1,2,5,6}, rear = {3,4}.
	struct FBandRow { int32 Sector; float Skill; float Shield; };
	const FBandRow Bands[] = {
		{ 0, 1.00f, 1.f }, // frontal
		{ 1, 0.75f, 0.f }, // flank
		{ 2, 0.75f, 0.f }, // flank
		{ 3, 0.50f, 0.f }, // rear
		{ 4, 0.50f, 0.f }, // rear
		{ 5, 0.75f, 0.f }, // flank
		{ 6, 0.75f, 0.f }, // flank
		{ 7, 1.00f, 1.f }, // frontal
	};
	for (const FBandRow& Row : Bands)
	{
		TestEqual(FString::Printf(TEXT("golden skill band sector %d"), Row.Sector),
			FMeleeMath::SkillArcMult(Row.Sector, 8), Row.Skill);
		TestEqual(FString::Printf(TEXT("golden shield band sector %d"), Row.Sector),
			FMeleeMath::ShieldArcWindow(Row.Sector, 8), Row.Shield);
	}
	// Sector-count independence of the angular windows: 12 sectors, the dead-
	// ahead sector stays frontal, the opposite sector stays rear.
	TestEqual(TEXT("12 sectors: sector 0 frontal"), FMeleeMath::SkillArcMult(0, 12), 1.f);
	TestEqual(TEXT("12 sectors: opposite sector rear"), FMeleeMath::SkillArcMult(6, 12), 0.5f);
	TestEqual(TEXT("12 sectors: shield only frontal"), FMeleeMath::ShieldArcWindow(6, 12), 0.f);

	// -- AP halving.
	TestEqual(TEXT("armor normal"), FMeleeMath::ArmorMultiplier(false), 1.f);
	TestEqual(TEXT("armor AP halves"), FMeleeMath::ArmorMultiplier(true), 0.5f);

	// -- Cycle stagger: deterministic, in [0, Interval), scattered.
	const float Off7 = FMeleeMath::StaggeredPhaseOffset(7u, 1.5f);
	TestEqual(TEXT("golden offset(7, 1.5)"), Off7, 0.00189975f);
	TestEqual(TEXT("golden offset(1, 1.5)"), FMeleeMath::StaggeredPhaseOffset(1u, 1.5f), 1.35660334f);
	TestEqual(TEXT("golden offset(2, 1.5)"), FMeleeMath::StaggeredPhaseOffset(2u, 1.5f), 1.21318379f);
	TestEqual(TEXT("offset(0) = 0"), FMeleeMath::StaggeredPhaseOffset(0u, 1.5f), 0.f);
	TestTrue(TEXT("repeat-call equality"),
		FMath::IsNearlyEqual(FMeleeMath::StaggeredPhaseOffset(1234u, 2.f), FMeleeMath::StaggeredPhaseOffset(1234u, 2.f)));
	bool bInRange = true;
	bool bScattered = !FMath::IsNearlyEqual(FMeleeMath::StaggeredPhaseOffset(1u, 1.5f), FMeleeMath::StaggeredPhaseOffset(2u, 1.5f));
	for (uint32 H = 0; H < 256u; ++H)
	{
		const float Off = FMeleeMath::StaggeredPhaseOffset(H, 1.5f);
		bInRange = bInRange && Off >= 0.f && Off < 1.5f;
	}
	TestTrue(TEXT("offsets within [0, interval)"), bInRange);
	TestTrue(TEXT("offsets scatter across handles"), bScattered);
	// Recovery window: 0.4 fraction of the interval.
	TestEqual(TEXT("recovery window"), FMeleeMath::RecoveryWindow(1.5f), 0.6f);

	// -- Charge decay (BaseCharge 4, window 5 s, golden-locked constants).
	TestEqual(TEXT("charge full at impact"), FMeleeMath::ChargeBonusRemaining(FMeleeMath::BaseChargeBonus(), 0.f, 5.f), 4.f);
	TestEqual(TEXT("charge half-decayed"), FMeleeMath::ChargeBonusRemaining(4.f, 2.5f, 5.f), 2.f);
	TestEqual(TEXT("charge expired at window"), FMeleeMath::ChargeBonusRemaining(4.f, 5.f, 5.f), 0.f);
	TestEqual(TEXT("charge stays 0 after window"), FMeleeMath::ChargeBonusRemaining(4.f, 7.f, 5.f), 0.f);

	// -- Knockback: linear above the threshold, bounded.
	TestEqual(TEXT("knockback zero below threshold"), FMeleeMath::KnockbackImpulse(3.5f, 4.f), 0.f);
	TestEqual(TEXT("knockback zero at threshold"), FMeleeMath::KnockbackImpulse(4.f, 4.f), 0.f);
	TestEqual(TEXT("golden knockback at 6 m/s"), FMeleeMath::KnockbackImpulse(6.f, 4.f), 1.2f);
	TestEqual(TEXT("knockback clamped"), FMeleeMath::KnockbackImpulse(10.f, 4.f), 2.5f);

	// Composite HP note (2026-10-28 audit): the float-damage seam
	// DrainCompositePool was dead code -- the resolution processor drains the
	// mount mirror inline (integer hits, DefenderMountRemaining-- then the
	// existing death tag path) and no runtime path ever called it. The seam
	// function, its golden block here, and the T2.6 charge/kill func tests
	// (which exercise the REAL inline path end-to-end) agree on the same
	// mount-first order.
	return true;
}

// ---------------------------------------------------------------------------
// T2.2 (task 4): budget/coverage invariants -- bounds [1, MaxMob(+spear)],
// exposed arcs filled before covered, over 1k randomized assign/remove cycles.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBudgetCoverage,
	"Battle.Unit.Combat.BudgetCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBudgetCoverage::RunTest(const FString& Parameters)
{
	// -- Budget bound reading (spec §4.5): [1, MaxMobSlots] non-spear;
	// MaxMobSlots + SecondRankBonus for spear; coverage 1 = FormedSlots.
	{
		const FCombatDefenderView D{ FVector2D::ZeroVector, 1.2f, FUnitHandle(), FMassEntityHandle() };
		const FCombatAttackerView Melee{ FUnitHandle(), static_cast<uint8>(EBattleWeaponReach::Normal) };
		const FCombatAttackerView Spear{ FUnitHandle(), static_cast<uint8>(EBattleWeaponReach::Spear) };
		TestEqual(TEXT("coverage 1 -> FormedSlots"), FCombatSolver::SlotBudget(D, 1.f, Melee), FCombatCVars::FormedSlots());
		TestEqual(TEXT("coverage 0 -> MaxMobSlots"), FCombatSolver::SlotBudget(D, 0.f, Melee), FCombatCVars::MaxMobSlots());
		TestEqual(TEXT("spear bonus"), FCombatSolver::SlotBudget(D, 1.f, Spear), FCombatCVars::FormedSlots() + FCombatCVars::SecondRankBonus());
		FRandomStream Rng(777);
		bool bBounds = true;
		for (int32 i = 0; i < 1000; ++i)
		{
			const float Coverage = Rng.FRand();
			const int32 B = FCombatSolver::SlotBudget(D, Coverage, Melee);
			bBounds = bBounds && B >= 1 && B <= FCombatCVars::MaxMobSlots();
		}
		TestTrue(TEXT("1k random coverages stay in [1, MaxMobSlots]"), bBounds);
	}

	// -- Exposed-before-covered (spec §4.5), over randomized sector contexts.
	FRandomStream Rng(4242);
	bool bExposedFirst = true;
	bool bBoundsAngle = true;
	for (int32 Iter = 0; Iter < 1000; ++Iter)
	{
		FCombatSolver::FSectorPickContext Ctx;
		Ctx.TotalBudget = 1 + Rng.RandRange(0, 8);
		uint16 Covered = 0;
		for (int32 Sector = 0; Sector < 8; ++Sector)
		{
			if (Rng.FRand() < 0.4f)
			{
				Covered |= (1u << Sector);
			}
			if (Rng.FRand() < 0.3f)
			{
				Ctx.AssignedPerSector[Sector] = Rng.RandRange(0, 2);
			}
		}
		Ctx.CoveredMask = Covered;
		const uint8 Angle = FCombatSolver::PickExposedAngle(8, Ctx);
		bBoundsAngle = bBoundsAngle && Angle <= 255;
		if (Angle == 255)
		{
			continue; // everything at share
		}
		const int32 Chosen = FCombatSolver::SectorIndexFromAngle(Angle, 8);
		const bool bChosenExposed = (Covered & (1u << Chosen)) == 0;
		// Invariant: if the pick is COVERED, then NO exposed sector has room.
		if (!bChosenExposed)
		{
			const int32 Share = FMath::DivideAndRoundUp(Ctx.TotalBudget, 8);
			for (int32 Sector = 0; Sector < 8 && bExposedFirst; ++Sector)
			{
				if (!(Covered & (1u << Sector)) && Ctx.AssignedPerSector[Sector] < Share)
				{
					bExposedFirst = false; // an exposed sector had room: violation
				}
			}
		}
	}
	TestTrue(TEXT("1k randomized picks: exposed arcs fill before covered"), bExposedFirst);
	TestTrue(TEXT("1k randomized picks: angle always valid or 255"), bBoundsAngle);

	// -- Full assign cycles: randomized populations, budgets respected, tally
	// agreement.
	FCombatTallyTable Tally;
	Tally.Initialize(1024);
	TArray<int32> SectorScratch;
	SectorScratch.Reserve(64 * 16);
	bool bCyclesClean = true;
	for (int32 Iter = 0; Iter < 1000 && bCyclesClean; ++Iter)
	{
		const int32 NumDefenders = 1 + Rng.RandRange(0, 6);
		const int32 NumSeekers = Rng.RandRange(0, 24);
		TArray<FAssignDefenderState> Defenders;
		TArray<FAssignSeekerState> Seekers;
		TArray<FAssignRecordView> Records;
		for (int32 D = 0; D < NumDefenders; ++D)
		{
			FAssignDefenderState Def;
			Def.Entity.Index = static_cast<uint32>(1000 + D);
			Def.Entity.SerialNumber = 1;
			Def.Unit.Index = 50;
			Def.Unit.Serial = 1;
			Def.PosM = FVector2D(D * 1.5f, 0.f);
			Def.Spacing = 1.2f;
			Def.Coverage = Rng.FRand();
			Def.CoveredMask = static_cast<uint16>(Rng.RandRange(0, 255));
			Defenders.Add(Def);
			FAssignRecordView Rec;
			Rec.DesiredPressure = Rng.RandRange(0, 10);
			Rec.Mode = 2; // mob: laned filter off
			Records.Add(Rec);
		}
		for (int32 SIdx = 0; SIdx < NumSeekers; ++SIdx)
		{
			FAssignSeekerState S;
			S.Entity.Index = static_cast<uint32>(2000 + SIdx);
			S.Entity.SerialNumber = 1;
			S.OwnerUnit.Index = 60;
			S.OwnerUnit.Serial = 1;
			S.IntentTarget.Index = 50;
			S.IntentTarget.Serial = 1;
			S.PosM = FVector2D(Rng.FRand() * 20.f - 10.f, Rng.FRand() * 20.f - 10.f);
			S.ReachClass = static_cast<uint8>(Rng.RandRange(0, 1));
			S.RecordIndex = 0; // one shared record: the unit-level cap binds
			Seekers.Add(S);
		}
		Tally.Reset();
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		Ctx.SeekRadiusM = 30.f;
		FCombatSolver::RunAssignPass(Seekers, Defenders, Records, Tally, Ctx);

		int32 AssignedTotal = 0;
		for (const FAssignDefenderState& Def : Defenders)
		{
			const FCombatDefenderView View{ Def.PosM, Def.Spacing, Def.Unit, Def.Entity };
			const FCombatAttackerView Att{ Seekers.Num() > 0 ? Seekers[0].OwnerUnit : FUnitHandle(),
				static_cast<uint8>(EBattleWeaponReach::Normal) };
			const int32 Budget = FCombatSolver::SlotBudget(View, Def.Coverage, Att);
			if (Tally.GetCount(Def.Entity) > Budget)
			{
				bCyclesClean = false;
			}
			AssignedTotal += Tally.GetCount(Def.Entity);
		}
		if (Records[0].AssignedAttackers > Records[0].DesiredPressure)
		{
			bCyclesClean = false;
		}
		if (Records[0].AssignedAttackers != AssignedTotal)
		{
			bCyclesClean = false; // tally and record must agree
		}
	}
	TestTrue(TEXT("1k assign/remove cycles: budgets + caps + tally agreement hold"), bCyclesClean);

	return true;
}

// ---------------------------------------------------------------------------
// T2.3 (task 4): the assign step over synthetic populations -- budgets
// respected, lifecycle rules (validate/release/hysteresis/disengage), and
// the A13 determinism flag's index-sorted reproducibility.
// ---------------------------------------------------------------------------

namespace
{
	/** Builds a small formed-line population: 5 defenders (unit B) vs
	 *  NumSeekers attackers (unit A), one frontal record. Attackers approach
	 *  from -X, so the frontal sectors (0, 7) are the exposed ones. */
	void BuildSyntheticClash(TArray<FAssignSeekerState>& OutSeekers, TArray<FAssignDefenderState>& OutDefenders,
		TArray<FAssignRecordView>& OutRecords, int32 NumSeekers)
	{
		OutDefenders.Reset();
		OutSeekers.Reset();
		OutRecords.Reset();
		for (int32 D = 0; D < 5; ++D)
		{
			FAssignDefenderState Def;
			Def.Entity.Index = static_cast<uint32>(100 + D);
			Def.Entity.SerialNumber = 1;
			Def.Unit.Index = 20;
			Def.Unit.Serial = 1;
			Def.PosM = FVector2D(10.f, D * 1.2f - 2.4f);
			Def.Spacing = 1.2f;
			Def.Coverage = 1.f; // formed line: full coverage
			// Neighbors cover every sector except the frontal two.
			Def.CoveredMask = 0b01111110;
			Def.LaneIndex = static_cast<uint8>(D % 2);
			OutDefenders.Add(Def);
		}
		for (int32 SIdx = 0; SIdx < NumSeekers; ++SIdx)
		{
			FAssignSeekerState S;
			S.Entity.Index = static_cast<uint32>(300 + SIdx);
			S.Entity.SerialNumber = 1;
			S.OwnerUnit.Index = 10;
			S.OwnerUnit.Serial = 1;
			S.IntentTarget.Index = 20;
			S.IntentTarget.Serial = 1;
			// AT RING DISTANCE (EC1: pairs form at contact, not at the seek
			// radius -- the ring gate rejects 4 m standoffs; measured
			// 2026-10-27 when the gate landed).
			S.PosM = FVector2D(9.0f, static_cast<float>(SIdx % 5) * 1.2f - 2.4f);
			S.ReachClass = static_cast<uint8>(EBattleWeaponReach::Normal);
			S.LaneIndex = static_cast<uint8>(SIdx % 2);
			S.RecordIndex = 0; // the single shared frontal record
			OutSeekers.Add(S);
		}
		FAssignRecordView Rec;
		Rec.DesiredPressure = 10; // frontal: 5 defenders x FormedSlots(2)
		Rec.Mode = 0;
		OutRecords.Add(Rec);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTallyAssign,
	"Battle.Unit.Combat.TallyAssign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTallyAssign::RunTest(const FString& Parameters)
{
	FCombatTallyTable Tally;
	Tally.Initialize(256);
	TArray<int32> SectorScratch;
	SectorScratch.Reserve(16 * 16);

	// -- Determinism mode (null stream): identical outputs across runs.
	TArray<FAssignSeekerState> Run1;
	TArray<FAssignDefenderState> Defenders;
	TArray<FAssignRecordView> Records;
	BuildSyntheticClash(Run1, Defenders, Records, 12);
	{
		TArray<FAssignSeekerState> Run2 = Run1;
		TArray<FAssignRecordView> Records2 = Records;
		FCombatTallyTable Tally2;
		Tally2.Initialize(256);
		TArray<int32> Scratch2;
		Scratch2.Reserve(16 * 16);
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		FCombatSolver::RunAssignPass(Run1, Defenders, Records, Tally, Ctx);
		Ctx.SectorScratch = &Scratch2;
		FCombatSolver::RunAssignPass(Run2, Defenders, Records2, Tally2, Ctx);
		bool bIdentical = true;
		for (int32 i = 0; i < Run1.Num(); ++i)
		{
			bIdentical = bIdentical
				&& Run1[i].Target == Run2[i].Target
				&& Run1[i].ContactAngle == Run2[i].ContactAngle
				&& Run1[i].State == Run2[i].State
				&& Run1[i].BackoffTicks == Run2[i].BackoffTicks;
		}
		TestTrue(TEXT("determinism mode: two runs identical (A13)"), bIdentical);
		TestEqual(TEXT("record tallies agree"), Records[0].AssignedAttackers, Records2[0].AssignedAttackers);
	}

	// -- Budgets respected: formed line with coverage 1 -> FormedSlots per
	// defender; the unit-level cap binds; exposed sectors fill first.
	{
		int32 MaxPerDefender = 0;
		for (const FAssignDefenderState& Def : Defenders)
		{
			MaxPerDefender = FMath::Max(MaxPerDefender, Tally.GetCount(Def.Entity));
		}
		const FCombatDefenderView View{ Defenders[0].PosM, Defenders[0].Spacing, Defenders[0].Unit, Defenders[0].Entity };
		const FCombatAttackerView Att{ Run1[0].OwnerUnit, static_cast<uint8>(EBattleWeaponReach::Normal) };
		TestEqual(TEXT("formed-line budget = FormedSlots"), FCombatSolver::SlotBudget(View, 1.f, Att), FCombatCVars::FormedSlots());
		TestTrue(TEXT("no defender over budget"), MaxPerDefender <= FCombatCVars::FormedSlots());
		// Unit-level cap: 5 defenders x 2 = 10 assigned of 12 seekers.
		TestEqual(TEXT("unit-level cap binds at pressure"), Records[0].AssignedAttackers, 10);
		int32 Pressing = 0;
		int32 Seeking = 0;
		for (const FAssignSeekerState& S : Run1)
		{
			Pressing += (S.State == static_cast<uint8>(ECombatPairState::Pressing)) ? 1 : 0;
			Seeking += (S.State == static_cast<uint8>(ECombatPairState::Seeking)) ? 1 : 0;
		}
		// Assigned seekers enter Pressing too (state = at the ring); the 2
		// over-cap seekers press behind them. Nothing stays Seeking.
		TestEqual(TEXT("12 seekers at the ring (10 paired + 2 over cap)"), Pressing, 12);
		TestEqual(TEXT("none left seeking"), Seeking, 0);
		bool bFrontalOnly = true;
		for (const FAssignSeekerState& S : Run1)
		{
			if (S.Target.IsValid())
			{
				const int32 Sector = FCombatSolver::SectorIndexFromAngle(S.ContactAngle, 8);
				bFrontalOnly = bFrontalOnly && (Sector == 0 || Sector == 7);
			}
		}
		TestTrue(TEXT("formed line: exposed frontal sectors fill first"), bFrontalOnly);
	}

	// -- Release rules: invalid target, handoff, hysteresis, disengage.
	{
		Tally.Reset(); // per-tick state: earlier blocks' counts must not leak
		TArray<FAssignSeekerState> Seekers;
		TArray<FAssignRecordView> Records2;
		BuildSyntheticClash(Seekers, Defenders, Records2, 4);
		Seekers[0].Target = Defenders[3].Entity;
		Seekers[0].State = static_cast<uint8>(ECombatPairState::Pressing);
		Defenders[3].bValidTarget = 0;
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		FCombatSolver::RunAssignPass(Seekers, Defenders, Records2, Tally, Ctx);
		// EC5: the release is immediate AND the released attacker re-pairs a
		// LIVE defender within the same pass (the advance IS the handoff).
		TestTrue(TEXT("invalid target released + re-paired same pass"),
			Seekers[0].Target.IsValid() && Seekers[0].Target != Defenders[3].Entity);
	}
	{
		// Distance breakup: beyond break radius AND PairAge > hysteresis.
		Tally.Reset();
		TArray<FAssignSeekerState> Seekers;
		TArray<FAssignRecordView> Records2;
		BuildSyntheticClash(Seekers, Defenders, Records2, 2);
		Seekers[0].Target = Defenders[0].Entity;
		Seekers[0].State = static_cast<uint8>(ECombatPairState::Pressing);
		Seekers[0].PosM = Defenders[0].PosM + FVector2D(20.f, 0.f);
		Seekers[0].PairAge = 0.1f; // below the 0.4 s hysteresis
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		FCombatSolver::RunAssignPass(Seekers, Defenders, Records2, Tally, Ctx);
		TestTrue(TEXT("breakup needs hysteresis (young pair survives distance)"), Seekers[0].Target.IsValid());
		Seekers[0].PairAge = 0.5f;
		FCombatSolver::RunAssignPass(Seekers, Defenders, Records2, Tally, Ctx);
		TestFalse(TEXT("breakup fires past hysteresis"), Seekers[0].Target.IsValid());
	}
	{
		// Disengage: dead record -> mass release in the same pass.
		Tally.Reset();
		TArray<FAssignSeekerState> Seekers;
		TArray<FAssignRecordView> Records2;
		BuildSyntheticClash(Seekers, Defenders, Records2, 4);
		for (int32 i = 0; i < Seekers.Num(); ++i)
		{
			Seekers[i].Target = Defenders[i % 5].Entity;
			Seekers[i].State = static_cast<uint8>(ECombatPairState::Pressing);
			Seekers[i].RecordIndex = 0;
		}
		Records2[0].bDead = 1; // unit disengaged
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		FCombatSolver::RunAssignPass(Seekers, Defenders, Records2, Tally, Ctx);
		bool bAllReleased = true;
		for (const FAssignSeekerState& S : Seekers)
		{
			bAllReleased = bAllReleased && !S.Target.IsValid();
		}
		TestTrue(TEXT("disengage mass-releases inbound pairs same pass"), bAllReleased);
	}

	// -- Shuffled mode: same seed -> reproducible; budgets still hold.
	{
		Tally.Reset();
		TArray<FAssignSeekerState> RunA;
		TArray<FAssignRecordView> RecordsA;
		BuildSyntheticClash(RunA, Defenders, RecordsA, 12);
		TArray<FAssignSeekerState> RunB = RunA;
		TArray<FAssignRecordView> RecordsB = RecordsA;
		FRandomStream StreamA(31337);
		FRandomStream StreamB(31337);
		FAssignContext Ctx;
		Ctx.SectorScratch = &SectorScratch;
		Ctx.ShuffleStream = &StreamA;
		FCombatSolver::RunAssignPass(RunA, Defenders, RecordsA, Tally, Ctx);
		Tally.Reset(); // each run starts from fresh per-tick state
		Ctx.ShuffleStream = &StreamB;
		FCombatSolver::RunAssignPass(RunB, Defenders, RecordsB, Tally, Ctx);
		bool bIdentical = true;
		for (int32 i = 0; i < RunA.Num(); ++i)
		{
			bIdentical = bIdentical && RunA[i].Target == RunB[i].Target && RunA[i].ContactAngle == RunB[i].ContactAngle;
		}
		TestTrue(TEXT("shuffled mode, same seed: reproducible"), bIdentical);
	}

	return true;
}

// ---------------------------------------------------------------------------
// T2.4-T2.8 func gates (tasks 7-8 own the scenario actors; the real tests
// become the harness's complex-test pattern with latent commands)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// T2.9 perf gate: shipped by task 9 as the Battle.Combat.PerfStress fixture +
// the measured AssignMs/TallyMs metrics against the checked-in baseline JSON.
// (The permanently-failing Battle.Perf.Gate2 skeleton that preceded it was
// removed by the 2026-10-28 audit: it failed any run matching Battle.Perf
// forever, by design of nothing -- the gate had already landed.)
// ---------------------------------------------------------------------------



// ---------------------------------------------------------------------------
// Task 5: spatial hash -- insert/query/miss, rebuild determinism, and the
// EC9 zero-allocation proxy (steady-state rebuilds keep array capacity).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_SpatialHash,
	"Battle.Unit.Combat.SpatialHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_SpatialHash::RunTest(const FString& Parameters)
{
	UWorld* World = MakeCombatTestWorld();
	UBattleSpatialHashSubsystem* Hash = World->GetSubsystem<UBattleSpatialHashSubsystem>();
	TestNotNull(TEXT("hash subsystem"), Hash);
	if (!Hash)
	{
		World->DestroyWorld(false);
		return false;
	}

	// -- Insert + query + miss --
	{
		TArray<FBattleHashEntry> Entries;
		TArray<FVector2D> Positions;
		for (int32 i = 0; i < 16; ++i)
		{
			FBattleHashEntry Entry;
			Entry.Entity.Index = static_cast<uint32>(1 + i);
			Entry.Entity.SerialNumber = 1;
			Entry.Unit.Index = 10;
			Entry.Unit.Serial = 1;
			Entries.Add(Entry);
			Positions.Add(FVector2D(static_cast<float>(i) * 0.5f, 0.f)); // 8 m line
		}
		Hash->RebuildWithPositions(MoveTemp(Entries), MoveTemp(Positions));

		TArray<FBattleHashEntry> Out;
		Hash->QueryCircle(FVector2D(2.f, 0.f), 1.0f, Out);
		TestEqual(TEXT("circle query hits the entries in range"), Out.Num(), 5); // x in [1, 3] -> 0.5 steps
		Out.Reset();
		Hash->QueryCircle(FVector2D(2.f, 0.f), 0.1f, Out);
		TestEqual(TEXT("tight query hits one"), Out.Num(), 1);
		Out.Reset();
		Hash->QueryCircle(FVector2D(100.f, 100.f), 5.f, Out);
		TestEqual(TEXT("miss far away"), Out.Num(), 0);

		// Within-cell iteration is handle-sorted (spec §4.1): the sorted
		// entry array is globally handle-ordered.
		const TArray<FBattleHashEntry>& Sorted = Hash->GetSortedEntries();
		bool bHandleSorted = Sorted.Num() == 16;
		for (int32 i = 1; i < Sorted.Num() && bHandleSorted; ++i)
		{
			bHandleSorted = Sorted[i - 1].Entity.Index < Sorted[i].Entity.Index;
		}
		TestTrue(TEXT("entries iterate handle-sorted"), bHandleSorted);
	}

	// -- Rebuild determinism: two identical rebuilds -> identical queries --
	{
		auto BuildInput = []()
		{
			TArray<FBattleHashEntry> Entries;
			TArray<FVector2D> Positions;
			for (int32 i = 0; i < 64; ++i)
			{
				FBattleHashEntry Entry;
				Entry.Entity.Index = static_cast<uint32>(i * 7 % 64 + 1); // shuffled insertion order
				Entry.Entity.SerialNumber = 1;
				Entries.Add(Entry);
				Positions.Add(FVector2D(static_cast<float>(i % 8) * 3.f, static_cast<float>(i / 8) * 3.f));
			}
			return TPair<TArray<FBattleHashEntry>, TArray<FVector2D>>(MoveTemp(Entries), MoveTemp(Positions));
		};
		auto Fold = [&Hash]()
		{
			uint32 FoldValue = 0;
			for (const FBattleHashEntry& E : Hash->GetSortedEntries())
			{
				FoldValue = HashCombine(FoldValue, E.Entity.Index);
			}
			for (const FVector2D& P : Hash->GetEntryPositionsM())
			{
				FoldValue = HashCombine(FoldValue, GetTypeHash(P.X));
				FoldValue = HashCombine(FoldValue, GetTypeHash(P.Y));
			}
			return FoldValue;
		};
		TPair<TArray<FBattleHashEntry>, TArray<FVector2D>> In1 = BuildInput();
		Hash->RebuildWithPositions(MoveTemp(In1.Key), MoveTemp(In1.Value));
		const uint32 Fold1 = Fold();
		TPair<TArray<FBattleHashEntry>, TArray<FVector2D>> In2 = BuildInput();
		Hash->RebuildWithPositions(MoveTemp(In2.Key), MoveTemp(In2.Value));
		const uint32 Fold2 = Fold();
		TestEqual(TEXT("rebuild determinism across identical runs"), Fold2, Fold1);
	}

	// -- EC9 zero-allocation proxy: steady-state rebuilds keep capacity --
	{
		TArray<FBattleHashEntry> Entries;
		TArray<FVector2D> Positions;
		for (int32 i = 0; i < 256; ++i)
		{
			FBattleHashEntry Entry;
			Entry.Entity.Index = static_cast<uint32>(i + 1);
			Entry.Entity.SerialNumber = 1;
			Entries.Add(Entry);
			Positions.Add(FVector2D(static_cast<float>(i % 16) * 3.f, static_cast<float>(i / 16) * 3.f));
		}
		Hash->RebuildWithPositions(MoveTemp(Entries), MoveTemp(Positions));
		const int32 CapacityBefore = Hash->GetSortedEntries().Max();
		const int32 PositionCapacityBefore = Hash->GetEntryPositionsM().Max();
		for (int32 TickIndex = 0; TickIndex < 100; ++TickIndex)
		{
			TArray<FBattleHashEntry> TickEntries;
			TArray<FVector2D> TickPositions;
			TickEntries.Reserve(256);
			TickPositions.Reserve(256);
			for (int32 i = 0; i < 256; ++i)
			{
				FBattleHashEntry Entry;
				Entry.Entity.Index = static_cast<uint32>(i + 1);
				Entry.Entity.SerialNumber = 1;
				TickEntries.Add(Entry);
				TickPositions.Add(FVector2D(static_cast<float>(i % 16) * 3.f + 0.01f * TickIndex, static_cast<float>(i / 16) * 3.f));
			}
			Hash->RebuildWithPositions(MoveTemp(TickEntries), MoveTemp(TickPositions));
		}
		TestEqual(TEXT("steady-state rebuilds: entry capacity stable (EC9 proxy)"),
			Hash->GetSortedEntries().Max(), CapacityBefore);
		TestEqual(TEXT("steady-state rebuilds: position capacity stable (EC9 proxy)"),
			Hash->GetEntryPositionsM().Max(), PositionCapacityBefore);
	}

	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// Task 5: cluster-record seam fixtures (frontal / flank / mob / none) over
// synthetic unit bodies (the processor is a thin shim; the func tier proves
// the wiring end-to-end in task 6).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombat_ClusterRecords,
	"Battle.Unit.Combat.ClusterRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombat_ClusterRecords::RunTest(const FString& Parameters)
{
	// Two formed units 40 m wide x 3 ranks, facing +X, 2 m apart front-to-front.
	FCombatSolver::FUnitClusterNode A, B;
	A.Unit = FUnitHandle{10, 1};
	B.Unit = FUnitHandle{20, 1};
	A.AnchorM = FVector2D(0.f, 0.f);
	B.AnchorM = FVector2D(4.f, 0.f);
	A.YawDeg = 0.f;      // A faces +X (toward B)
	B.YawDeg = 180.f;    // B faces -X (toward A)
	A.HalfAlongM = 20.f; // frontage half-extent (along the SIDE axis)
	A.HalfAcrossM = 1.8f;
	B.HalfAlongM = 20.f;
	B.HalfAcrossM = 1.8f;
	A.AliveCount = 40;
	B.AliveCount = 40;

	// -- Frontal: A attacks B head-on (inside the frontage interior).
	{
		FCombatSolver::FEngagementRecordBuild Out;
		FCombatSolver::BuildEngagementRecord(A, B, /*DirectionMask*/ 0, Out);
		TestEqual(TEXT("frontal mode"), Out.Mode, static_cast<uint8>(0));
		TestEqual(TEXT("frontal pressure = defenderAlive x FormedSlots"), Out.DesiredPressure,
			40 * FCombatCVars::FormedSlots());
		// Frontage segment: attacker opposite the line center, clipped inside.
		TestTrue(TEXT("frontage covers the crossing"),
			Out.FrontageStart <= 0.f && Out.FrontageEnd >= 0.f);
	}
	// -- Flank: attacker beyond the defended end (t = 25 > HalfAlong 20).
	{
		FCombatSolver::FUnitClusterNode Flanker = A;
		Flanker.AnchorM = FVector2D(4.f, 25.f); // past B's lateral end
		FCombatSolver::FEngagementRecordBuild Out;
		FCombatSolver::BuildEngagementRecord(Flanker, B, 0, Out);
		TestEqual(TEXT("flank mode"), Out.Mode, static_cast<uint8>(1));
		// Flank keeps the frontal pressure formula (spec §3.2).
		TestEqual(TEXT("flank pressure"), Out.DesiredPressure, 40 * FCombatCVars::FormedSlots());
	}
	// -- Mob: the defender's attackers span >= 3 quadrants.
	{
		FCombatSolver::FEngagementRecordBuild Out;
		const uint8 Mask = 1 | 4 | 8; // +X, +Y, -Y
		FCombatSolver::BuildEngagementRecord(A, B, Mask, Out);
		TestEqual(TEXT("mob mode at 3 quadrants"), Out.Mode, static_cast<uint8>(2));
		// A14 floor: Min(attackerAlive, Max(defenderAlive x factor, MaxMobSlots)).
		const int32 Expected = FMath::Min(40,
			FMath::Max(40 * FCombatCVars::MobPressureFactor(), FCombatCVars::MaxMobSlots()));
		TestEqual(TEXT("mob pressure with the A14 floor"), Out.DesiredPressure, Expected);
	}
	// -- A14 floor resolution: 1 defender alive -> MaxMobSlots, not 3.
	{
		FCombatSolver::FUnitClusterNode Lone = B;
		Lone.AliveCount = 1;
		FCombatSolver::FEngagementRecordBuild Out;
		FCombatSolver::BuildEngagementRecord(A, Lone, 1 | 4 | 8, Out);
		TestEqual(TEXT("isolated knight floors at MaxMobSlots"), Out.DesiredPressure, FCombatCVars::MaxMobSlots());
	}
	// -- Cluster graph: two disjoint pairs vs one merged chain.
	{
		TArray<FIntPoint> Pairs;
		Pairs.Add(FIntPoint(0, 1));
		Pairs.Add(FIntPoint(2, 3));
		Pairs.Add(FIntPoint(1, 2)); // chain merges everything
		TArray<int32> Ids;
		FCombatSolver::ClusterGraph(4, Pairs, Ids);
		bool bOneCluster = Ids.Num() == 4;
		for (int32 i = 0; i < 4 && bOneCluster; ++i)
		{
			bOneCluster = Ids[i] == Ids[0];
		}
		TestTrue(TEXT("chained contacts merge into one cluster"), bOneCluster);
		Pairs.Reset();
		Pairs.Add(FIntPoint(0, 1));
		Pairs.Add(FIntPoint(2, 3));
		FCombatSolver::ClusterGraph(4, Pairs, Ids);
		TestTrue(TEXT("disjoint pairs stay separate clusters"), Ids[0] != Ids[2]);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
