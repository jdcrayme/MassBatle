#include "BattleUnitContact.h"

namespace
{
	/** OBB half-extent projections on the given unit axis. */
	float ProjectExtent(const FUnitOBB& O, const FVector2D& Axis)
	{
		const float Rad = FMath::DegreesToRadians(O.YawDeg);
		const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D Side(-Front.Y, Front.X);
		return O.HalfAlongFacing * FMath::Abs(FVector2D::DotProduct(Front, Axis))
			+ O.HalfAcrossFacing * FMath::Abs(FVector2D::DotProduct(Side, Axis));
	}
}

namespace FUnitContact
{
	float OverlapDepth(const FUnitOBB& A, const FUnitOBB& B)
	{
		const FVector2D Delta = B.Center - A.Center;
		if (Delta.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			// Coincident centers: treat as maximal overlap along the tight axis.
			return FMath::Min(A.HalfAcrossFacing + B.HalfAcrossFacing, A.HalfAlongFacing + B.HalfAlongFacing);
		}
		// Audit M2: true 4-face-axis OBB SAT. The old 2-axis projection
		// (center delta + its perpendicular) is only exact for parallel and
		// exact-90-degree pairs: for oblique elongated formations it reports
		// deep overlap where the boxes barely touch, and even overlap where
		// they are separated -- which spuriously engaged the 8.2 facing lock
		// and the shove zone. Only the boxes' own face axes are guaranteed
		// separating axes.
		const float RadA = FMath::DegreesToRadians(A.YawDeg);
		const FVector2D AFront(FMath::Cos(RadA), FMath::Sin(RadA));
		const FVector2D ASide(-AFront.Y, AFront.X);
		const float RadB = FMath::DegreesToRadians(B.YawDeg);
		const FVector2D BFront(FMath::Cos(RadB), FMath::Sin(RadB));
		const FVector2D BSide(-BFront.Y, BFront.X);

		float Depth = FLT_MAX;
		for (const FVector2D* Axis : { &AFront, &ASide, &BFront, &BSide })
		{
			const float Dist = FMath::Abs(FVector2D::DotProduct(Delta, *Axis));
			const float Sum = ProjectExtent(A, *Axis) + ProjectExtent(B, *Axis);
			Depth = FMath::Min(Depth, Sum - Dist);
		}
		return Depth > 0.f ? Depth : 0.f;
	}

	bool ContainsPoint(const FUnitOBB& O, const FVector2D& P)
	{
		const FVector2D Delta = P - O.Center;
		const float Rad = FMath::DegreesToRadians(O.YawDeg);
		const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D Side(-Front.Y, Front.X);
		return FMath::Abs(FVector2D::DotProduct(Delta, Front)) <= O.HalfAlongFacing
			&& FMath::Abs(FVector2D::DotProduct(Delta, Side)) <= O.HalfAcrossFacing;
	}

	bool Interpenetrating(const FUnitOBB& A, const FUnitOBB& B)
	{
		return OverlapDepth(A, B) > 0.f;
	}

	EContactPolicy ResolvePolicy(bool bFriendly, bool bMobilityAdvantage, bool bRightOfWay)
	{
		if (!bFriendly)
		{
			return EContactPolicy::Block; // hostile contact resolution is Phase 2
		}
		if (bMobilityAdvantage)
		{
			return bRightOfWay ? EContactPolicy::PassThrough : EContactPolicy::Yield;
		}
		return EContactPolicy::Block;
	}

	float ShoveBlend(float MassRatio, float OverlapDepthM)
	{
		return FMath::Clamp(MassRatio * OverlapDepthM, 0.f, 1.f);
	}
}
