// BattleProjectileSubsystem.cpp -- the pool's game-thread lifetime owner
// (spec ranged-stack §3.3). Reserve reads the cvar ONCE per battle; the
// pool itself never allocates after that (RC9).
#include "BattleProjectileSubsystem.h"
#include "BattleRanged.h"

namespace
{
	/** Floor for the cvar read: a pool smaller than this cannot serve even a
	 *  pair of volleys; the cvar is the tuning knob, this is the sanity bound. */
	constexpr int32 MinPoolReserve = 16;
}

void UBattleProjectileSubsystem::ReserveForBattle()
{
	Pool.Reserve(FMath::Max(MinPoolReserve, FRangedCVars::PoolCapacity()));
}

void UBattleProjectileSubsystem::ResetForBattle()
{
	Pool.Reset();
}
