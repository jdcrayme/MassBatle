// BattleArrowVisualComponent.cpp -- see header.
#include "BattleArrowVisualComponent.h"
#include "BattleProjectileSubsystem.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

UBattleArrowVisualComponent::UBattleArrowVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	Mobility = EComponentMobility::Movable;
	ArrowMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/MassBattle/Demo/SM_Arrow.SM_Arrow")));
}

void UBattleArrowVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisFunction);

	const UWorld* World = GetWorld();
	const UBattleProjectileSubsystem* Projectiles =
		World ? World->GetSubsystem<UBattleProjectileSubsystem>() : nullptr;
	if (!Projectiles)
	{
		return;
	}
	const FBattleProjectilePool& Pool = Projectiles->GetPool();
	if (Pool.GetLiveCount() == 0)
	{
		if (GetInstanceCount() > 0)
		{
			ClearInstances();
		}
		return;
	}

	if (!GetStaticMesh())
	{
		// Quiet existence probe (see the PR-2 input fallback note): a missing
		// gray-box asset must not trip the engine's load-failure warning in
		// every session it is absent from.
		const FSoftObjectPath MeshPath = ArrowMesh.ToSoftObjectPath();
		if (MeshPath.IsValid() && FPackageName::DoesPackageExist(MeshPath.GetLongPackageName()))
		{
			if (UStaticMesh* Mesh = ArrowMesh.LoadSynchronous())
			{
				SetStaticMesh(Mesh);
			}
		}
		if (!GetStaticMesh())
		{
			return; // asset not authored yet -- the overlay segments stay the feed
		}
	}

	// Full rebuild per frame: the gray-box posture (spec section 1) at <=
	// 4k instances is cheaper than delta tracking, and Phase 5's Niagara
	// batching replaces this path wholesale.
	ClearInstances();
	int32 Added = 0;
	for (int32 i = 0; i < Pool.GetCapacity() && Added < MaxInstances; ++i)
	{
		if (Pool.Live[i] == 0)
		{
			continue;
		}
		const FVector PosCm(Pool.PosX[i] * 100.f, Pool.PosY[i] * 100.f, Pool.PosZ[i] * 100.f);
		const FVector Vel(Pool.VelX[i], Pool.VelY[i], Pool.VelZ[i]);
		// The mesh points +X; FVector::Rotation() aligns +X with the velocity
		// (arrows never hover -- a zero-velocity arrow still has its last
		// stamped step direction).
		const FRotator Rot = Vel.IsNearlyZero() ? FRotator::ZeroRotator : Vel.Rotation();
		AddInstance(FTransform(Rot, PosCm), /*bWorldSpace*/ true);
		++Added;
	}
}
