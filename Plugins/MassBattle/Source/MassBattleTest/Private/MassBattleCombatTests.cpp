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

	// -- Composite HP: mount drains first, then the rider.
	{
		float Rider = 1.f;
		int16 Mount = 0;
		FMeleeMath::DrainCompositePool(Rider, Mount, 1.f);
		TestEqual(TEXT("foot: hit kills"), Rider, 0.f);
		Rider = 1.f; Mount = 1;
		FMeleeMath::DrainCompositePool(Rider, Mount, 1.f);
		TestEqual(TEXT("cavalry: first hit takes the mount"), Mount, static_cast<int16>(0));
		TestEqual(TEXT("cavalry: rider survives first hit"), Rider, 1.f);
		FMeleeMath::DrainCompositePool(Rider, Mount, 1.f);
		TestEqual(TEXT("cavalry: second hit kills"), Rider, 0.f);
		Rider = 1.f; Mount = 2;
		FMeleeMath::DrainCompositePool(Rider, Mount, 2.f);
		TestEqual(TEXT("cavalry: 2-damage hit drains both pools"), Rider, 1.f);
	}

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
