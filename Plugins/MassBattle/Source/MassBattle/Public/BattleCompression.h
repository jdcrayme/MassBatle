// BattleCompression.h -- formation compression model (arch §8.2 + §12, gate
// T1.4). Pure functions on the seam, METERS.
//   slot = anchor + R(facing) * LayoutFn(slot) * SpacingScale
// SpacingScale floor = 60% of nominal spacing (§12); compression (scale drop)
// is immediate -- crowding is urgent; recovery is rate-limited ~10%/s toward
// the requested scale. This asymmetry is what makes step inputs non-oscillating.
#pragma once

#include "CoreMinimal.h"

namespace FUnitCompression
{
	/** §12 tuning: compression floor at 60% of nominal spacing. */
	constexpr float CompressionFloor = 0.6f;
	/** §8.2: recovery ~10%/s (SpacingScale units per second). */
	constexpr float RecoveryPerSecond = 0.1f;

	/** Advance SpacingScale toward TargetScale for DT seconds. Drops are
	 *  immediate (clamped at FloorScale); rises are rate-limited. */
	float AdvanceSpacingScale(float CurrentScale, float TargetScale, float DT,
		float FloorScale = CompressionFloor, float RecoveryPerSec = RecoveryPerSecond);

	/** The compressed slot position: anchor + R(facing) * LayoutOffset * Scale. */
	FVector2D CompressedSlot(const FVector2D& Anchor, float FacingYawDeg,
		const FVector2D& LayoutOffset, float SpacingScale);
}
