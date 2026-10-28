// BattleProjectileSubsystem.cpp -- the pool's game-thread lifetime owner
// (spec ranged-stack §3.3). Reserve reads the cvar ONCE per battle; the
// pool itself never allocates after that (RC9).
#include "BattleProjectileSubsystem.h"
#include "BattleRanged.h"

void UBattleProjectileSubsystem::ReserveForBattle()
{
	Pool.Reserve(FMath::Max(16, FRangedCVars::PoolCapacity()));
}

void UBattleProjectileSubsystem::ResetForBattle()
{
	Pool.Reset();
}
