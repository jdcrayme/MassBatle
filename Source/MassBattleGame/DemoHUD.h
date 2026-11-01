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
	/** PR-3 (demo-shell spec section 4.3): the M2TW army card row -- one card
	 *  per PLAYER unit (not just the selection): type glyph, alive count,
	 *  morale chip; selected cards highlight, routing cards gray. The player
	 *  controller hit tests the same DemoCommandBar geometry (click selects,
	 *  double-click isolates the type). */
	void DrawUnitCards();
	/** PR-3: the selected panel -- the stat readout (row stats + live values)
	 *  plus the stance buttons (Guard / Fire-at-will / Skirmish / Exit-garrison
	 *  + Wider / Narrower) with read-back state; out-of-ammo grays the fire
	 *  control. Same DemoCommandBar geometry as the controller's hit tests. */
	void DrawSelectedPanel();

private:
	void DrawControlsHint();
	/** PR-2: the world-layer banners (the M2TW convention) -- one card per
	 *  unit (team bar, type glyph, soldier bar, morale chip, ammo pips,
	 *  general star, toggle badges), collapsing to a small flag with zoom
	 *  and re-expanding on hover. Replaces the text labels. */
	void DrawUnitBanners();
	/** PR-2: the hover stats card (row stats + live values + order/toggles). */
	void DrawHoverCard(FUnitHandle Handle, const FBox2D& BannerRect);
	void DrawSelectionCircles();
	void DrawMarquee();
	void DrawOrderDragPreview();
	/** PR-1: the beat sequencer's current beat, top-center -- the D.1/D.17 read. */
	void DrawBeatLabel();

	/** The banner under the mouse this frame (hover display + the PC's click
	 *  hit tests read the subsystem's rects independently). */
	FUnitHandle HoveredBanner;

	void DrawWorldCircle(const FVector2D& CenterCm, float RadiusCm, const FColor& Color, int32 Segments = 32);
	void DrawWorldFacingArrow(const FVector2D& PosCm, float YawDeg, float LengthCm, const FColor& Color);

	FVector2D Project(const FVector2D& PosCm, float Zcm = 100.f) const;
};
