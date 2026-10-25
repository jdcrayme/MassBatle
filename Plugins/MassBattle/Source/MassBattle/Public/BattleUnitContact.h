// BattleUnitContact.h -- unit-vs-unit OBB contact + policies (arch §8.6,
// gate T1.6 support). Pure functions on the seam, METERS. The unit body is an
// OBB around the formation (frontage x depth) at the anchor. Phase 1 scale:
// 8 units -> 28 pairs, roster data, no spatial hash (fact 1.11).
#pragma once

#include "CoreMinimal.h"

struct FUnitOBB
{
	FVector2D Center = FVector2D::ZeroVector;  // m (formation anchor)
	/** Extent ALONG the facing: rank depth (ranks * rankSpacing / 2). */
	float HalfAlongFacing = 0.f;
	/** Extent ACROSS the facing: frontage (files * fileSpacing / 2) -- the
	 *  formation's long axis is perpendicular to the facing (arch §8.6: ranks
	 *  stack behind the anchor, files spread lateral). */
	float HalfAcrossFacing = 0.f;
	float YawDeg = 0.f;
};

namespace FUnitContact
{
	/** §8.6 contact policies. */
	enum class EContactPolicy : uint8
	{
		Block,       // formations hold and crowd (equal footing)
		Yield,       // the weaker/slower side gives way
		PassThrough  // shove-through (cavalry over friendly infantry)
	};

	/** SAT overlap depth of two OBBs (m); 0 when separated. */
	float OverlapDepth(const FUnitOBB& A, const FUnitOBB& B);

	/** True when the point lies inside the OBB (shove-zone test). */
	bool ContainsPoint(const FUnitOBB& OBB, const FVector2D& P);

	/** True while two unit bodies interpenetrate -- the §8.2 hard rule keys
	 *  off this: NO facing change while interpenetrating another unit. */
	bool Interpenetrating(const FUnitOBB& A, const FUnitOBB& B);

	/** Policy resolution by friendliness, mass, mobility, right-of-way:
	 *  hostile -> Block (melee contact is Phase 2 territory); friendly +
	 *  mobility advantage + right-of-way -> PassThrough; friendly + mobility
	 *  advantage without right-of-way -> Yield; friendly equal -> Block. */
	EContactPolicy ResolvePolicy(bool bFriendly, bool bMobilityAdvantage, bool bRightOfWay);

	/** §8.6 shove blend: w = clamp(MassRatio * OverlapDepth, 0, 1). */
	float ShoveBlend(float MassRatio, float OverlapDepthM);

	/** Displacement speed handed to shoved entities (m/s), blended by w. */
	constexpr float ShoveSpeed = 1.5f;

	/** §8.6 pass-through tuning: the cavalry anchor proceeds at ~55% speed. */
	constexpr float PassThroughSpeedFactor = 0.55f;
	/** ...and the compression floor lowers 60% -> 40% (deeper packing). */
	constexpr float PassThroughCompressionFloor = 0.4f;
}
