// DemoGarrisonProp.cpp -- see header. The gray-box bodies are scaled engine
// basic cubes (B.1 doctrine: art never gates mechanics); the exact structure
// geometry lives in the plugin's FGarrisonLayout (one fact, one home).
#include "DemoGarrisonProp.h"
#include "DemoTypes.h"

#include "BattleGarrison.h"
#include "BattleGarrisonSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Local (pre-rotation) geometry mirrors -- coarse gray-box only; the
	// authoritative record geometry is FGarrisonLayout::MakeFixture.
	void GetBodyBoxes(EGarrisonStructureType Type,
		TArray<FBox>& OutBoxes)
	{
		OutBoxes.Reset();
		const FVector Half(50.f, 50.f, 50.f); // expand helper (cm)
		switch (Type)
		{
		case EGarrisonStructureType::Building:
		{
			// Perimeter walls of the 48 x 20 m footprint, 1.5 m high, with a
			// 4 m door gap on the -Y (door) side.
			const FVector2D Min(-2400.f, -1000.f);
			const FVector2D Max(2400.f, 1000.f);
			constexpr float T = 50.f;      // wall thickness, cm
			constexpr float H = 150.f;     // wall height, cm
			// North (+Y), East, West -- solid.
			OutBoxes.Add(FBox(FVector(Min.X, Max.Y - T, 0.f) - Half * 0.f, FVector(Max.X, Max.Y, H)));
			OutBoxes.Add(FBox(FVector(Min.X, Min.Y, 0.f), FVector(Min.X + T, Max.Y, H)));
			OutBoxes.Add(FBox(FVector(Max.X - T, Min.Y, 0.f), FVector(Max.X, Max.Y, H)));
			// South (-Y, door side): two segments leaving a 4 m gap at the middle.
			OutBoxes.Add(FBox(FVector(Min.X, Min.Y, 0.f), FVector(-200.f, Min.Y + T, H)));
			OutBoxes.Add(FBox(FVector(200.f, Min.Y, 0.f), FVector(Max.X, Min.Y + T, H)));
			break;
		}
		case EGarrisonStructureType::Wall:
		{
			// Full body 64 x 4 x 6 m (the walkway the units stand on).
			OutBoxes.Add(FBox(FVector(-3200.f, -200.f, 0.f), FVector(3200.f, 200.f, 600.f)));
			// Stair hint: a ramp box rising toward the -X entrance.
			OutBoxes.Add(FBox(FVector(-3800.f, -200.f, 0.f), FVector(-3200.f, 200.f, 300.f)));
			break;
		}
		case EGarrisonStructureType::Trench:
		{
			// Berm pair flanking the 4 m channel (the sunken floor is implied).
			OutBoxes.Add(FBox(FVector(-3200.f, 160.f, 0.f), FVector(3200.f, 280.f, 75.f)));
			OutBoxes.Add(FBox(FVector(-3200.f, -280.f, 0.f), FVector(3200.f, -160.f, 75.f)));
			// Ramp hint at the -X entrance.
			OutBoxes.Add(FBox(FVector(-3800.f, -200.f, 0.f), FVector(-3200.f, 200.f, 40.f)));
			break;
		}
		default:
			break;
		}
	}
}

ADemoGarrisonProp::ADemoGarrisonProp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADemoGarrisonProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBody();

	// Editor placement aid (persistent; matches the plugin overlay colors).
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		DrawPlacementPreview();
	}
}

void ADemoGarrisonProp::RebuildBody()
{
	for (UStaticMeshComponent* Box : BodyComponents)
	{
		if (Box)
		{
			Box->DestroyComponent();
		}
	}
	BodyComponents.Reset();

	UStaticMesh* Cube = nullptr;
	if (UStaticMesh* Found = Cast<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")).TryLoad()))
	{
		Cube = Found;
	}

	TArray<FBox> Boxes;
	GetBodyBoxes(StructureType, Boxes);
	for (const FBox& Local : Boxes)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetupAttachment(RootComponent);
		Comp->SetStaticMesh(Cube);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); // sim collides via the cost grid, not physics
		Comp->SetGenerateOverlapEvents(false);
		const FVector Center = Local.GetCenter();
		const FVector Extent = Local.GetExtent();
		Comp->SetRelativeLocation(Center);
		Comp->SetRelativeScale3D(Extent / 50.f); // basic cube is 100 cm
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->RegisterComponent();
		BodyComponents.Add(Comp);
	}
}

void ADemoGarrisonProp::DrawPlacementPreview()
{
	FGarrisonStructureRecord Rec;
	if (!FGarrisonLayout::MakeFixture(StructureType,
		FVector2D(GetActorLocation()) * 0.01f, GetActorRotation().Yaw, Rec))
	{
		return;
	}
	const FColor Footprint(120, 120, 40);
	const FVector2D Min = Rec.Footprint.Min;
	const FVector2D Max = Rec.Footprint.Max;
	for (int32 C = 0; C < 4; ++C)
	{
		const FVector2D A(C == 1 || C == 2 ? Max.X : Min.X, C >= 2 ? Max.Y : Min.Y);
		const FVector2D B(C == 0 || C == 1 ? Max.X : Min.X, C == 2 || C == 3 ? Max.Y : Min.Y);
		DrawDebugLine(GetWorld(), FVector(A.X, A.Y, 20.f), FVector(B.X, B.Y, 20.f), Footprint, true);
	}
	for (const FGarrisonEntrance& E : Rec.Entrances)
	{
		DrawDebugLine(GetWorld(), FVector(E.QueuePos.X, E.QueuePos.Y, 20.f),
			FVector(E.DoorPos.X, E.DoorPos.Y, 20.f), FColor::Orange, true);
		DrawDebugLine(GetWorld(), FVector(E.DoorPos.X, E.DoorPos.Y, 20.f),
			FVector(E.AssemblyPos.X, E.AssemblyPos.Y, 20.f), FColor(255, 80, 80), true);
	}
	for (const FGarrisonSlot& Slot : Rec.Slots)
	{
		DrawDebugPoint(GetWorld(),
			FVector(Slot.Pos.X, Slot.Pos.Y, Slot.Z * 100.f + 20.f), 6.f,
			Slot.Z > 1.f ? FColor::Green : (Slot.Z < -0.1f ? FColor(0, 130, 200) : FColor::Cyan), true);
	}
}

void ADemoGarrisonProp::BeginPlay()
{
	Super::BeginPlay();

	if (!bRegisterOnBeginPlay)
	{
		return;
	}
	UBattleGarrisonSubsystem* Garrison = GetWorld() ? GetWorld()->GetSubsystem<UBattleGarrisonSubsystem>() : nullptr;
	if (!Garrison)
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoGarrisonProp FAIL no garrison subsystem"));
		return;
	}
	FGarrisonStructureRecord Rec;
	if (!FGarrisonLayout::MakeFixture(StructureType,
		FVector2D(GetActorLocation()) * 0.01f, GetActorRotation().Yaw, Rec))
	{
		UE_LOG(LogDemoBattle, Error, TEXT("EXEC_RESULT DemoGarrisonProp FAIL bad type %d"),
			static_cast<int32>(StructureType));
		return;
	}
	StructureId = Garrison->RegisterStructure(Rec);
	UE_LOG(LogDemoBattle, Display,
		TEXT("EXEC_RESULT DemoGarrisonProp %s Type=%d Pos=(%.0f,%.0f)m Rot=%.0f Structure=%u Slots=%d"),
		*GetName(), static_cast<int32>(StructureType),
		GetActorLocation().X * 0.01f, GetActorLocation().Y * 0.01f,
		GetActorRotation().Yaw, StructureId, Rec.Slots.Num());
}
