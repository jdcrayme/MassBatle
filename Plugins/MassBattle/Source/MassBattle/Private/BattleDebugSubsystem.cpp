#include "BattleDebugSubsystem.h"
#include "BattleRegistry.h"
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
	LadderEscalations = 0;
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
	       TEXT("EXEC_RESULT BattleMetrics Frame=%d Archetypes=%d Units=%d UnitSyncMs=%.3f BookkeepingHash=%u OverlayHook=%d"),
	       LastMetrics.Frame, ArchetypeCount, LastMetrics.UnitCount,
	       LastMetrics.UnitSyncMs, static_cast<uint32>(LastMetrics.RegistryHash32), BattleDebugShowOverlay);
}
