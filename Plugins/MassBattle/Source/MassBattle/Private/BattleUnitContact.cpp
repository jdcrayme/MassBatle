#include "BattleUnitContact.h"

namespace
{
	/** OBB half-extent projections on the given unit axis. */
	float ProjectExtent(const FUnitOBB& O, const FVector2D& Axis)
	{
		const float Rad = FMath::DegreesToRadians(O.YawDeg);
		const FVector2D Front(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D Side(-Front.Y, Front.X);
		return O.HalfFrontage * FMath::Abs(FVector2D::DotProduct(Front, Axis))
			+ O.HalfDepth * FMath::Abs(FVector2D::DotProduct(Side, Axis));
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
			return FMath::Min(A.HalfFrontage + B.HalfFrontage, A.HalfDepth + B.HalfDepth);
		}
		const FVector2D Axis1 = Delta.GetSafeNormal();
		const FVector2D Axis2(-Axis1.Y, Axis1.X);

		float Depth = FLT_MAX;
		for (const FVector2D* Axis : { &Axis1, &Axis2 })
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
		return FMath::Abs(FVector2D::DotProduct(Delta, Front)) <= O.HalfFrontage
			&& FMath::Abs(FVector2D::DotProduct(Delta, Side)) <= O.HalfDepth;
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
