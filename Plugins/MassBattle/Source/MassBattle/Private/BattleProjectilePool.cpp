// BattleProjectilePool.cpp -- the pooled projectile store (spec
// ranged-stack §3.3, ADR-27). All allocation happens in Reserve; Acquire and
// Release are O(1) stack operations with zero allocation.
#include "BattleProjectilePool.h"

void FBattleProjectilePool::Reserve(int32 InCapacity)
{
	Capacity = FMath::Max(0, InCapacity);
	LiveCount = 0;
	Drops = 0;

	PosX.Init(0.f, Capacity);
	PosY.Init(0.f, Capacity);
	PosZ.Init(0.f, Capacity);
	VelX.Init(0.f, Capacity);
	VelY.Init(0.f, Capacity);
	VelZ.Init(0.f, Capacity);
	GravityScale.Init(1.f, Capacity);
	TTL.Init(0.f, Capacity);
	Kind.Init(0, Capacity);
	Live.Init(0, Capacity);
	GenerationArray.Init(0, Capacity);
	OwnerUnit.Init(FUnitHandle(), Capacity);
	ShooterEntity.Init(FMassEntityHandle(), Capacity);
	RangedAttack.Init(0.f, Capacity);

	// LIFO free list: slot order is deterministic (index order on a fresh
	// pool -- the state hash's pool block depends on it, spec §3.4).
	FreeIndices.SetNum(Capacity);
	for (int32 i = 0; i < Capacity; ++i)
	{
		FreeIndices[Capacity - 1 - i] = i;
	}
}

int32 FBattleProjectilePool::Acquire(uint32& OutGeneration)
{
	if (FreeIndices.Num() == 0)
	{
		++Drops; // fail closed (RC9): the pool NEVER grows mid-volley
		OutGeneration = 0;
		return InvalidIndex;
	}
	const int32 Index = FreeIndices.Pop();
	Live[Index] = 1;
	++LiveCount;
	OutGeneration = GenerationArray[Index];
	return Index;
}

void FBattleProjectilePool::Release(int32 Index)
{
	if (!IsLive(Index))
	{
		return;
	}
	Live[Index] = 0;
	--LiveCount;
	++GenerationArray[Index]; // stale indices die here
	FreeIndices.Push(Index);
}

void FBattleProjectilePool::Reset()
{
	for (int32 i = 0; i < Capacity; ++i)
	{
		Live[i] = 0;
	}
	// Re-seed the free list in index order (fresh-pool determinism).
	FreeIndices.SetNum(Capacity);
	for (int32 i = 0; i < Capacity; ++i)
	{
		FreeIndices[Capacity - 1 - i] = i;
	}
	LiveCount = 0;
}
