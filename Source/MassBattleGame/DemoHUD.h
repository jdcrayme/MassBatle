// DemoHUD.h -- canvas HUD for the demo (no UMG, MassRTS ARTSHUD pattern):
// marquee rectangle, selection circles, formation-drag preview (frontage line,
// facing arrows, per-unit footprints with Files x Ranks), unit labels and a
// controls hint.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DemoTypes.h"
#include "DemoHUD.generated.h"

UCLASS()
class MASSBATTLEGAME_API ADemoHUD : public AHUD
{
	GENERATED_BODY()

public:
	ADemoHUD();

	virtual void DrawHUD() override;

protected:
	/** Phase 2 task 10: one card per selected unit (team color, spec name,
	 *  alive roster) -- the MassRTS selection-summary UX, geometry-only. */
	void DrawUnitCards();

private:
	void DrawControlsHint();
	void DrawUnitLabels();
	void DrawSelectionCircles();
	void DrawMarquee();
	void DrawOrderDragPreview();

	void DrawWorldCircle(const FVector2D& CenterCm, float RadiusCm, const FColor& Color, int32 Segments = 32);
	void DrawWorldFacingArrow(const FVector2D& PosCm, float YawDeg, float LengthCm, const FColor& Color);

	FVector2D Project(const FVector2D& PosCm, float Zcm = 100.f) const;
};
