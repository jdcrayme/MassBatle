// MassBattleOrderTests.cpp -- Battle.Unit.Order.* -- Task 4b.1: the
// OrderLatency queue (T4b.1). The release sweep is registry-owned
// (ReleaseQueuedOrders), so the suite drives tick boundaries directly.
#include "Misc/AutomationTest.h"
#include "BattleRegistry.h"
#include "BattleTypes.h"
#include "BattleSpawnLibrary.h"
#include "BattleEduParser.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

namespace
{
	UWorld* MakeOrderTestWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, false);
	}

	bool LoadOrderFixture(TArray<FEduUnit>& OutRows)
	{
		const FString FixturePath = FPaths::ProjectContentDir() / TEXT("Data/EDU/golden_edu_v1.txt");
		FString EduText;
		FEduParseError Err;
		return FFileHelper::LoadFileToString(EduText, *FixturePath)
			&& FBattleEduParser::Parse(EduText, OutRows, Err);
	}

	FBattleSpawnResult SpawnOrderRow(UWorld* World, const FEduUnit& EduRow)
	{
		FBattleRowStats Stats;
		FString Error;
		if (!FBattleRowStats::FromEdu(EduRow, Stats, Error))
		{
			return FBattleSpawnResult();
		}
		FUnitSpawnSpec Spec;
		Spec.EduRowIndex = 0;
		Spec.SoldierCount = FMath::Max(1, Stats.SoldierCount);
		Spec.UnitName = Stats.TypeName;
		return UBattleSpawnLibrary::SpawnUnit(World, Spec, Stats);
	}

	void SetLatencyTicks(int32 Ticks)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Order.LatencyTicks")))
		{
			C->Set(Ticks, ECVF_SetByCode);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrder_Queue,
	"Battle.Unit.Order.Queue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOrder_Queue::RunTest(const FString& Parameters)
{
	UWorld* World = MakeOrderTestWorld();
	UUnitRegistrySubsystem* Registry = World->GetSubsystem<UUnitRegistrySubsystem>();
	if (!TestNotNull(TEXT("registry"), Registry))
	{
		World->DestroyWorld(false);
		return false;
	}
	TArray<FEduUnit> Rows;
	if (!LoadOrderFixture(Rows) || Rows.Num() < 1)
	{
		AddError(TEXT("EDU fixture unavailable"));
		World->DestroyWorld(false);
		return false;
	}
	const FBattleSpawnResult Unit = SpawnOrderRow(World, Rows[0]);
	if (!TestTrue(TEXT("spawned"), Unit.Unit.IsValid()))
	{
		World->DestroyWorld(false);
		return false;
	}

	// -- Identity (ground rule 3): latency 0 applies DIRECTLY. --
	SetLatencyTicks(0);
	FUnitOrder MoveOrder;
	MoveOrder.Type = FUnitOrder::EType::Move;
	MoveOrder.AnchorPos = FVector2D(5000.f, 1000.f);
	MoveOrder.FacingYaw = 45.f;
	const uint32 SerialBefore = Registry->GetOrderSerial(Unit.Unit);
	TestTrue(TEXT("latency0 applies"), Registry->QueuePlayerOrder(Unit.Unit, MoveOrder));
	TestEqual(TEXT("latency0 serial bumped once"), Registry->GetOrderSerial(Unit.Unit), SerialBefore + 1);
	const FUnitOrder* Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("latency0 order live"), Current && Current->Type == FUnitOrder::EType::Move
		&& Current->AnchorPos == MoveOrder.AnchorPos);

	// -- Release timing (B-4): latency 2 queues; the order lands on the
	// release tick boundary and NOT before. --
	SetLatencyTicks(2);
	FUnitOrder MoveA = MoveOrder;
	MoveA.AnchorPos = FVector2D(8000.f, 0.f);
	Registry->AdvanceSimTick(); // tick 1 (the issue happens on tick 1)
	const uint32 SerialAtIssue = Registry->GetOrderSerial(Unit.Unit);
	TestTrue(TEXT("queued"), Registry->QueuePlayerOrder(Unit.Unit, MoveA));
	// B-4: the serial bumps AT ISSUE.
	TestEqual(TEXT("serial-at-issue"), Registry->GetOrderSerial(Unit.Unit), SerialAtIssue + 1);
	// The current order is UNCHANGED until release (the old Move still owns).
	Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("current unchanged while queued"), Current && Current->AnchorPos == FVector2D(5000.f, 1000.f));
	Registry->ReleaseQueuedOrders(); // tick 1: release tick is 1+2=3 -- not due
	Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("not released early"), Current && Current->AnchorPos == FVector2D(5000.f, 1000.f));
	Registry->AdvanceSimTick(); // tick 2
	Registry->ReleaseQueuedOrders();
	Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("still not due"), Current && Current->AnchorPos == FVector2D(5000.f, 1000.f));
	Registry->AdvanceSimTick(); // tick 3 = the release boundary
	Registry->ReleaseQueuedOrders();
	Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("released on the boundary"), Current && Current->AnchorPos == FVector2D(8000.f, 0.f));

	// -- Last order wins (B-4): the second player order replaces the queued
	// one; exactly one apply lands at the boundary. --
	FUnitOrder MoveB = MoveOrder;
	MoveB.AnchorPos = FVector2D(-8000.f, 0.f);
	FUnitOrder MoveC = MoveOrder;
	MoveC.AnchorPos = FVector2D(0.f, 9000.f);
	Registry->AdvanceSimTick(); // tick 4
	TestTrue(TEXT("queued A"), Registry->QueuePlayerOrder(Unit.Unit, MoveA));
	TestTrue(TEXT("queued B replaces A"), Registry->QueuePlayerOrder(Unit.Unit, MoveB));
	TestTrue(TEXT("queued C replaces B"), Registry->QueuePlayerOrder(Unit.Unit, MoveC));
	Registry->AdvanceSimTick(); // tick 5
	Registry->AdvanceSimTick(); // tick 6
	Registry->AdvanceSimTick(); // tick 7 = C's release (issued on 4, latency 2 -> 6... released at <=7)
	Registry->ReleaseQueuedOrders();
	Current = Registry->FindOrder(Unit.Unit);
	TestTrue(TEXT("last order wins"), Current && Current->AnchorPos == FVector2D(0.f, 9000.f));

	// -- Auto-order interplay (B-2/R-C1): an AUTO order issued directly does
	// not queue (it applies immediately, superseding the pending player
	// order's current state), and the pending player order still lands at
	// its boundary (the player's intent wins at release). --
	Registry->ResetBattleState();
	const FBattleSpawnResult A = SpawnOrderRow(World, Rows[0]);
	const FBattleSpawnResult B = SpawnOrderRow(World, Rows[Rows.Num() > 1 ? 1 : 0]);
	if (!TestTrue(TEXT("resawned"), A.Unit.IsValid() && B.Unit.IsValid()))
	{
		World->DestroyWorld(false);
		return false;
	}
	SetLatencyTicks(3);
	Registry->AdvanceSimTick(); // tick 1
	FUnitOrder PlayerMove = MoveOrder;
	PlayerMove.AnchorPos = FVector2D(20000.f, 0.f);
	TestTrue(TEXT("player move queued"), Registry->QueuePlayerOrder(A.Unit, PlayerMove));
	// The AUTO order goes DIRECT (B-2: reflexes never delay).
	TestTrue(TEXT("auto attack applies immediately"), Registry->IssueAttackUnitOrder(A.Unit, B.Unit));
	Current = Registry->FindOrder(A.Unit);
	TestTrue(TEXT("auto owns the current order"), Current && Current->Type == FUnitOrder::EType::AttackUnit);
	// Walk to the release boundary (issued tick 1, latency 3 -> tick 4).
	for (int32 i = 0; i < 4; ++i)
	{
		Registry->AdvanceSimTick();
		Registry->ReleaseQueuedOrders();
	}
	Current = Registry->FindOrder(A.Unit);
	TestTrue(TEXT("player order supersedes at release"), Current && Current->Type == FUnitOrder::EType::Move
		&& Current->AnchorPos == FVector2D(20000.f, 0.f));

	// -- Reset-ordering immunity (F10/R-C2): a reset clears the queue AND
	// the tick counter; two identical battles release at identical ticks
	// regardless of an extra throwaway queued order between them. --
	SetLatencyTicks(2);
	auto RunMiniBattle = [&]() -> int32
	{
		Registry->ResetBattleState();
		const FBattleSpawnResult U = SpawnOrderRow(World, Rows[0]);
		if (!U.Unit.IsValid())
		{
			return -1;
		}
		Registry->AdvanceSimTick(); // tick 1 (post-reset the counter restarts)
		FUnitOrder M = MoveOrder;
		M.AnchorPos = FVector2D(12000.f, 0.f);
		if (!Registry->QueuePlayerOrder(U.Unit, M))
		{
			return -2;
		}
		int32 ReleaseTick = 0;
		for (int32 T = 2; T <= 8; ++T)
		{
			Registry->AdvanceSimTick();
			Registry->ReleaseQueuedOrders();
			const FUnitOrder* C = Registry->FindOrder(U.Unit);
			if (C && C->AnchorPos == FVector2D(12000.f, 0.f))
			{
				ReleaseTick = T;
				break;
			}
		}
		return ReleaseTick;
	};
	const int32 FirstRelease = RunMiniBattle();
	// The throwaway between the battles: a sacrificial unit's queued order
	// (seeding process history the old accumulator disease would leak).
	{
		const FBattleSpawnResult Sac = SpawnOrderRow(World, Rows[0]);
		FUnitOrder SacMove = MoveOrder;
		SacMove.AnchorPos = FVector2D(-12000.f, 0.f);
		Registry->QueuePlayerOrder(Sac.Unit, SacMove);
	}
	const int32 SecondRelease = RunMiniBattle();
	TestEqual(TEXT("reset-ordering immunity: identical release ticks"), SecondRelease, FirstRelease);
	AddInfo(FString::Printf(TEXT("Order.Queue release ticks: first=%d second=%d"), FirstRelease, SecondRelease));

	SetLatencyTicks(0);
	World->DestroyWorld(false);
	return true;
}
