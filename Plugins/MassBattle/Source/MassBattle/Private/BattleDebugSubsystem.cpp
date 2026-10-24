#include "BattleDebugSubsystem.h"
#include "BattleRegistry.h"
#include "BattleExecCommands.h"
#include "MassBattleModule.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// A1: the on-screen toggle is a HOOK only -- nothing draws in Phase 0.
	int32 BattleDebugShowOverlay = 0;
	int32 BattleDebugEnabled = 0;

	FAutoConsoleVariableRef GBattleDebugShowOverlay(
		TEXT("Battle.Debug.ShowOverlay"),
		BattleDebugShowOverlay,
		TEXT("1 = overlay hook armed (drawing arrives Phase 2 per A1)"),
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
	if (UMassEntitySubsystem* ES = World->GetSubsystem<UMassEntitySubsystem>())
	{
		ArchetypeCount = ES->GetMutableEntityManager().DebugGetArchetypesCount();
	}
	LastMetrics.ArchetypeCount = ArchetypeCount;

	UE_LOG(LogMassBattle, Display,
	       TEXT("EXEC_RESULT BattleMetrics Frame=%d Archetypes=%d Units=%d UnitSyncMs=%.3f BookkeepingHash=%u OverlayHook=%d"),
	       LastMetrics.Frame, ArchetypeCount, LastMetrics.UnitCount,
	       LastMetrics.UnitSyncMs, static_cast<uint32>(LastMetrics.RegistryHash32), BattleDebugShowOverlay);
}
