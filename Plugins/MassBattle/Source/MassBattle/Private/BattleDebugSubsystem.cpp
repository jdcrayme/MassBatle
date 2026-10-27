#include "BattleDebugSubsystem.h"
#include "BattleRegistry.h"
#include "BattleMovementFragments.h"
#include "BattleExecCommands.h"
#include "MassBattleModule.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// A1/A3: the on-screen toggle gates the first drawable states (phase 1:
	// path polyline, slot-error magnitude, unstuck ladder level).
	int32 BattleDebugShowOverlay = 0;
	int32 BattleDebugEnabled = 0;

	FAutoConsoleVariableRef GBattleDebugShowOverlay(
		TEXT("Battle.Debug.ShowOverlay"),
		BattleDebugShowOverlay,
		TEXT("1 = draw the A3 overlay states (path polyline, slot error, unstuck ladder)"),
		ECVF_Default);

	FAutoConsoleVariableRef GBattleDebugEnabled(
		TEXT("Battle.Debug.Enabled"),
		BattleDebugEnabled,
		TEXT("1 = debug subsystem records B.4 metrics"),
		ECVF_Default);
}

void UBattleDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastMetrics = FBattleFrameMetrics();
}

void UBattleDebugSubsystem::ResetDiagnostics()
{
	CombatRolls = 0;
	CombatRollsLanded = 0;
	ChargeConnects = 0;
	KnockbacksApplied = 0;
	DeathsByArc[0] = DeathsByArc[1] = DeathsByArc[2] = 0;
	LadderEscalations = 0;
	MorphDecisions = 0;
	UnitAIHandoffs = 0;
	GarrisonFilings = 0;
	GarrisonFilingTicksTotal = 0;
	GarrisonQueueWaits = 0;
	GarrisonQueueWaitTicksTotal = 0;
}

void UBattleDebugSubsystem::RecordGarrisonSettled(int32 FilingTicks, int32 QueueWaitTicks)
{
	++GarrisonFilings;
	GarrisonFilingTicksTotal += FMath::Max(0, FilingTicks);
	if (QueueWaitTicks > 0)
	{
		++GarrisonQueueWaits;
		GarrisonQueueWaitTicksTotal += QueueWaitTicks;
	}
}

void UBattleDebugSubsystem::DumpGarrisonMetrics(int32 BookedUnits, int32 QueueWaiting)
{
	UE_LOG(LogMassBattle, Display,
		TEXT("EXEC_RESULT GarrisonMetrics Filings=%d FilingTicksAvg=%d QueueWaits=%d QueueWaitTicksAvg=%d BookedUnits=%d Waiting=%d"),
		GarrisonFilings, GetGarrisonFilingTicksAvg(), GarrisonQueueWaits,
		GetGarrisonQueueWaitTicksAvg(), BookedUnits, QueueWaiting);
}

void UBattleDebugSubsystem::Tick(float DeltaTime)
{

	// T2.9 perf probe (task 9): the steady-state metrics dump.
	if (PerfProbeTicks > 0 && --PerfProbeTicks == 0)
	{
		DumpMetrics();
		PerfProbeTicks = -1;
	}
	Super::Tick(DeltaTime);

	TArray<FBattleOverlayLine> Lines;
	{
		FScopeLock Lock(&OverlayMutex);
		Lines = MoveTemp(PendingOverlayLines);
		PendingOverlayLines.Reset();
	}
	if (!BattleDebugShowOverlay || Lines.Num() == 0 || !GetWorld())
	{
		return;
	}
	for (const FBattleOverlayLine& Line : Lines)
	{
		DrawDebugLine(GetWorld(), Line.From, Line.To, Line.Color, false, 0.f, 0, 2.f);
		if (Line.LadderLevel > 0)
		{
			const FVector Mid = (Line.From + Line.To) * 0.5f;
			DrawDebugString(GetWorld(), Mid, FString::Printf(TEXT("L%d"), Line.LadderLevel), nullptr, Line.Color, 0.f, true);
		}
	}
}

TStatId UBattleDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBattleDebugSubsystem, STATGROUP_Tickables);
}

void UBattleDebugSubsystem::EnqueueOverlayLine(const FBattleOverlayLine& Line)
{
	FScopeLock Lock(&OverlayMutex);
	PendingOverlayLines.Add(Line);
}

void UBattleDebugSubsystem::RecordUnitSyncMs(double Ms)
{
	if (!BattleDebugEnabled)
	{
		return;
	}
	if (LastMetrics.Frame != GFrameNumber)
	{
		LastMetrics = FBattleFrameMetrics();
		LastMetrics.Frame = GFrameNumber;
		if (UUnitRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UUnitRegistrySubsystem>())
		{
			LastMetrics.UnitCount = Registry->GetUnitCount();
			LastMetrics.RegistryHash32 = Registry->ComputeBookkeepingHash();
		}
	}
	LastMetrics.UnitSyncMs += Ms;
	// Archetype count is filled by DumpMetrics (needs the entity manager).
}

bool UBattleDebugSubsystem::IsOverlayShown() const
{
	return BattleDebugShowOverlay != 0;
}

void UBattleDebugSubsystem::EnqueueEngagementLine(FUnitHandle Attacker, FUnitHandle Defender, int32 Mode,
	int32 DesiredPressure, int32 AssignedAttackers)
{
	// One queued line per engagement record: From/To span the two unit
	// anchors at ground level (the record debug is unit-level geometry);
	// the mode colors: Frontal red, Flank orange, Mob magenta.
	UWorld* World = GetWorld();
	UUnitRegistrySubsystem* Registry = World ? World->GetSubsystem<UUnitRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	auto UnitAnchor = [this, Registry](const FUnitHandle& Handle) -> FVector
	{
		UWorld* MyWorld = GetWorld();
		if (UMassEntitySubsystem* Entities = MyWorld ? MyWorld->GetSubsystem<UMassEntitySubsystem>() : nullptr)
		{
			if (const TArray<FMassEntityHandle>* Roster = Registry->FindRoster(Handle);
				Roster && Roster->Num() > 0)
			{
				if (const FUnitFormationSharedFragment* Formation =
					Entities->GetMutableEntityManager().GetSharedFragmentDataPtr<FUnitFormationSharedFragment>((*Roster)[0]))
				{
					const FVector2D Anchor = FVector2D(Formation->AnchorPos) * 0.01f;
					return FVector(Anchor.X, Anchor.Y, 20.f);
				}
			}
		}
		return FVector::ZeroVector;
	};
	static const FColor ModeColors[3] = { FColor::Red, FColor::Orange, FColor::Magenta };
	FBattleOverlayLine Line;
	Line.From = UnitAnchor(Attacker);
	Line.To = UnitAnchor(Defender);
	Line.Color = ModeColors[FMath::Clamp(Mode, 0, 2)];
	EnqueueOverlayLine(Line);
	UE_LOG(LogMassBattle, Verbose,
		TEXT("Overlay Engagement A=%u:%u D=%u:%u Mode=%d Pressure=%d Assigned=%d"),
		Attacker.Index, Attacker.Serial, Defender.Index, Defender.Serial, Mode, DesiredPressure, AssignedAttackers);
}

void UBattleDebugSubsystem::RecordDeathByArc(int32 Sector, int32 SectorCount)
{
	// Frontal/flank/rear bands over the sector index (the same angular windows
	// as the defense bands); game-thread-safe because the worker path defers
	// through the kill only -- THIS call happens worker-side, so atomic via
	// the counter lock (three ints under one critical section is fine at kill
	// rates; the counters are diagnostics).
	FScopeLock Lock(&ArcMutex);
	const float Center = (static_cast<float>(Sector) + 0.5f) * (360.f / static_cast<float>(FMath::Max(1, SectorCount)));
	float Normalized = FMath::Fmod(Center + 180.f, 360.f) - 180.f;
	const float AbsDeg = FMath::Abs(Normalized);
	const int32 Band = AbsDeg <= 45.f ? 0 : (AbsDeg >= 135.f ? 2 : 1);
	++DeathsByArc[Band];
}

void UBattleDebugSubsystem::DumpMetrics()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 ArchetypeCount = 0;
#if WITH_MASSENTITY_DEBUG
	if (UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>())
	{
		ArchetypeCount = ES->GetMutableEntityManager().DebugGetArchetypesCount();
	}
#endif
	LastMetrics.ArchetypeCount = ArchetypeCount;

	UE_LOG(LogMassBattle, Display,
	       TEXT("EXEC_RESULT BattleMetrics Frame=%d Archetypes=%d Units=%d UnitSyncMs=%.3f BookkeepingHash=%u OverlayHook=%d CombatHashMs=%.3f TallyMs=%.3f AssignMs=%.3f Rolls=%d/%d ChargeConnects=%d DeathsF/F/R=%d/%d/%d"),
	       LastMetrics.Frame, ArchetypeCount, LastMetrics.UnitCount,
	       LastMetrics.UnitSyncMs, static_cast<uint32>(LastMetrics.RegistryHash32), BattleDebugShowOverlay,
	       CombatHashMs, TallyMs, AssignMs, CombatRolls.load(), CombatRollsLanded.load(), ChargeConnects.load(),
	       DeathsByArc[0], DeathsByArc[1], DeathsByArc[2]);
}
