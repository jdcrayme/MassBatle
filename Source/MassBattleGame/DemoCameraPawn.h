// DemoCameraPawn.h -- RTS camera (adapted from the MassRTS TopDownCameraPawn
// pattern): spring-arm top-down rig with a command-accumulator API. The pawn
// binds no input; DemoPlayerController pushes Pan/Zoom/Rotate (+ edge scroll,
// probed from the viewport) each frame and the rig smooths toward the targets.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DemoCameraPawn.generated.h"

class USphereComponent;
class USpringArmComponent;
class UCameraComponent;

UCLASS(Blueprintable)
class MASSBATTLEGAME_API ADemoCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ADemoCameraPawn();

	// -- APawn --
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Command-accumulator API (called from the player controller's Tick). */
	void Pan(const FVector2D& WorldDir) { PendingPan += WorldDir; }
	void Zoom(float Delta) { PendingZoom += Delta; }
	void Rotate(float YawDeltaDeg) { PendingRotate += YawDeltaDeg; }

	/** Edge-scroll tuning (the controller probes the viewport each frame). */
	float GetEdgeScrollZone() const { return EdgeScrollZone; }
	float GetEdgeDeadZonePx() const { return EdgeDeadZonePx; }

private:
	void UpdateTargets(float DeltaSeconds);
	void ApplyTargets(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY(VisibleAnywhere, Category = "Camera") TObjectPtr<UCameraComponent> Camera;

	/** Accumulated commands (cleared each tick). */
	FVector2D PendingPan = FVector2D::ZeroVector;
	float PendingZoom = 0.f;
	float PendingRotate = 0.f;

	/** Smoothed state. */
	float TargetArmLength = 4000.f;
	float TargetYawDeg = 0.f;
	FVector TargetLocation = FVector::ZeroVector;

	/** Tunables. */
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float MoveSpeed = 3500.f;
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float RotateSpeedDeg = 120.f;
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float ZoomSpeed = 1.2f;
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float MinArmLength = 1200.f;
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float MaxArmLength = 12000.f;
	/** Fraction of each screen border that triggers edge scroll. */
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float EdgeScrollZone = 0.06f;
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float EdgeScrollSpeed = 2200.f;
	/** Dead zone (px) against exact-border jitter. */
	UPROPERTY(EditAnywhere, Category = "Camera|Tuning") float EdgeDeadZonePx = 4.f;
};
