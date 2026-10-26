// DemoCameraPawn.cpp -- see header.
#include "DemoCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

ADemoCameraPawn::ADemoCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(40.f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Sphere);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Sphere);
	SpringArm->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	SpringArm->TargetArmLength = TargetArmLength;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	SpringArm->bUsePawnControlRotation = false;
	// Inherit the root actor's yaw: the rig rotates with the pawn so pan stays
	// camera-relative (bInheritPitch/Yaw/Roll = false would pin the view yaw).
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void ADemoCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateTargets(DeltaSeconds);
	ApplyTargets(DeltaSeconds);

	PendingPan = FVector2D::ZeroVector;
	PendingZoom = 0.f;
	PendingRotate = 0.f;
}

void ADemoCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// Start from the actual spawn transform (PlayerStart): no yank to the world
	// origin, and the rig yaw carries the level-authored start orientation.
	TargetLocation = GetActorLocation();
	TargetYawDeg = GetActorRotation().Yaw;
	// Kick off fully zoomed out (BP-tunable through MaxArmLength; C++
	// constructors run before BP defaults apply, so resolve here).
	TargetArmLength = MaxArmLength;
	SpringArm->TargetArmLength = MaxArmLength;
	ApplyTargets(0.f);
}

void ADemoCameraPawn::UpdateTargets(float DeltaSeconds)
{
	if (!GetWorld())
	{
		return;
	}

	// Rotate: yaw the whole rig (the spring arm inherits yaw, so the view and
	// the pan frame rotate together).
	TargetYawDeg += PendingRotate * RotateSpeedDeg * DeltaSeconds;

	// Zoom: multiplicative exponential on arm length (positive = zoom IN).
	if (PendingZoom != 0.f)
	{
		TargetArmLength *= FMath::Pow(ZoomSpeed, -PendingZoom);
		TargetArmLength = FMath::Clamp(TargetArmLength, MinArmLength, MaxArmLength);
	}

	// Pan: camera-relative on the ground plane. Forward = flattened camera
	// forward; Right = forward rotated to the camera's right (FRotationMatrix
	// Y column: (-sin, cos)), so W/A/S/D follow what the player sees.
	const float Rad = FMath::DegreesToRadians(TargetYawDeg);
	const FVector ForwardDir(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
	const FVector RightDir(-ForwardDir.Y, ForwardDir.X, 0.f);
	const float PanScale = TargetArmLength / 4000.f;
	TargetLocation += (ForwardDir * PendingPan.X + RightDir * PendingPan.Y)
		* MoveSpeed * PanScale * DeltaSeconds;
	TargetLocation.Z = 0.f; // flat tile
}

void ADemoCameraPawn::ApplyTargets(float DeltaSeconds)
{
	SetActorLocation(FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaSeconds, 10.f));
	SetActorRotation(FRotator(0.f, TargetYawDeg, 0.f));
	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArmLength, DeltaSeconds, 10.f);
}
