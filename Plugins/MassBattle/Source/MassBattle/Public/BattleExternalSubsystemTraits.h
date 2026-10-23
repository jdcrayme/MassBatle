// BattleExternalSubsystemTraits.h -- Mass thread-safety contract for the
// registry (separate-header pattern, like MassGameplayExternalTraits.h).
// GameThreadOnly=false lets Phase 1 worker processors declare read access;
// ThreadSafeWrite=false keeps writes exclusive (game-thread-only writes, arch
// §2.3). Audit C3: members are GameThreadOnly/ThreadSafeWrite, header lives in
// MassCore ("Mass/ExternalSubsystemTraits.h").
#pragma once

#include "CoreMinimal.h"
#include "Mass/ExternalSubsystemTraits.h"
#include "BattleRegistry.h"

template<>
struct TMassExternalSubsystemTraits<UUnitRegistrySubsystem> final
{
	enum
	{
		GameThreadOnly = false,
		ThreadSafeWrite = false
	};
};
