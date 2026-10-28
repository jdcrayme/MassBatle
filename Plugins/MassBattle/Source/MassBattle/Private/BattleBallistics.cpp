// BattleBallistics.cpp -- the ballistic seam bodies (spec ranged-stack
// §4.1-§4.3). All math closed-form or fixed-step; deterministic by
// construction (no streams, no wall-clock).
#include "BattleBallistics.h"
#include "Math/UnrealMathUtility.h"

namespace
{
	// Knuth multiplicative hash constants -- the SAME family as
	// FMeleeMath::StaggeredPhaseOffset / CombatRoll01 (BattleMelee.cpp; the
	// B-2 one-home posture). Duplicated here with the cross-reference so the
	// seam stays free of BattleMelee includes; a future audit may unify the
	// constant's home in one header both seams include.
	constexpr uint32 KnuthGold = 2654435761u;
	constexpr uint32 KnuthMix = 40503u;

	uint32 HashMix32(uint32 V)
	{
		V ^= V >> 16;
		V *= KnuthMix;
		V ^= V >> 13;
		return V;
	}

	uint32 Hash01ToUint(uint32 A, uint32 B, uint32 C)
	{
		uint32 H = KnuthGold;
		H = HashMix32(H ^ A);
		H = HashMix32(H ^ B);
		H = HashMix32(H ^ C);
		return H;
	}

	/** Deterministic [0, 1) from a mixed hash. */
	float Hash01(uint32 H)
	{
		return static_cast<float>(H & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
	}
}

namespace FBallisticsMath
{

FBallisticSolution SolveLaunch(
	const FVector2D& FromM, float FromZM, const FVector2D& ToM, float ToZM,
	float SpeedMps, float GravityMps2)
{
	FBallisticSolution Out;

	const FVector2D Delta = ToM - FromM;
	const float d = Delta.Size();
	const float dh = ToZM - FromZM;

	if (d <= FBallisticsConstants::DistanceEpsilonM)
	{
		// Degenerate: straight up/down. A vertical launch can only reach a
		// target at or above the launch height; the caller never sets this up
		// (volleys have horizontal extent), but the seam stays total.
		if (dh > 0.f && SpeedMps > 0.f)
		{
			Out.LaunchDir = FVector(0.f, 0.f, 1.f);
			Out.FlightTimeS = dh / SpeedMps;
			Out.bValid = true;
		}
		return Out;
	}

	if (SpeedMps <= 0.f || GravityMps2 <= 0.f)
	{
		return Out; // invalid: no speed or no gravity is not a ballistic shot
	}

	const float v2 = SpeedMps * SpeedMps;
	const float Disc = v2 * v2 - GravityMps2 * (GravityMps2 * d * d + 2.f * dh * v2);
	if (Disc < 0.f)
	{
		return Out; // out of envelope (RC2: the volley pass holds fire)
	}

	const float TanTheta = (v2 - FMath::Sqrt(Disc)) / (GravityMps2 * d); // low arc
	const float Theta = FMath::Atan(TanTheta);
	const float CosTheta = FMath::Cos(Theta);
	const float SinTheta = FMath::Sin(Theta);

	const FVector2D Dir2D = Delta / d;
	Out.LaunchDir = FVector(Dir2D.X * CosTheta, Dir2D.Y * CosTheta, SinTheta);
	Out.FlightTimeS = d / (SpeedMps * CosTheta);
	Out.bValid = true;
	return Out;
}

FVector PerturbDirection(const FVector& Dir, float ConeHalfDeg, uint32 Seed)
{
	if (ConeHalfDeg <= FBallisticsConstants::ConeEpsilonDeg || !Dir.IsNormalized())
	{
		return Dir;
	}

	// Two independent deterministic draws -> a point in the cone's DISC
	// (radial sqrt + full angle): every result stays inside the cone by
	// construction (rejection-free, RC1's dispersion bound).
	const uint32 H1 = Hash01ToUint(Seed, 0x9E3779B9u, 0x85EBCA6Bu);
	const uint32 H2 = Hash01ToUint(Seed, 0xC2B2AE35u, 0x27D4EB2Fu);
	const float u1 = Hash01(H1);
	const float u2 = Hash01(H2);
	const float RadiusDeg = FMath::Sqrt(u1) * ConeHalfDeg;
	const float AzimuthRad = u2 * 2.f * PI;

	// Orthonormal basis around Dir (stable for any input direction).
	const FVector W = Dir;
	const FVector Reference = (FMath::Abs(W.Z) < 0.9f) ? FVector::UpVector : FVector::ForwardVector;
	const FVector U = FVector::CrossProduct(Reference, W).GetSafeNormal();
	const FVector V = FVector::CrossProduct(W, U);

	const float RadiusRad = FMath::DegreesToRadians(RadiusDeg);
	const float OffU = FMath::Cos(AzimuthRad) * RadiusRad;
	const float OffV = FMath::Sin(AzimuthRad) * RadiusRad;

	// Small-angle rotation of W toward (U*OffU + V*OffV), then renormalize:
	// exact at the axis, bounded within the cone for small angles.
	FVector Out = W + U * FMath::Tan(OffU) + V * FMath::Tan(OffV);
	return Out.GetSafeNormal();
}

uint32 ShotSeed(uint32 EntityIndex, uint32 EntitySerial, uint16 ShotsFired)
{
	return Hash01ToUint(EntityIndex, EntitySerial, static_cast<uint32>(ShotsFired));
}

void StepProjectile(FVector& InOutPosM, FVector& InOutVelMps, float GravityMps2, float DT)
{
	// Semi-implicit Euler: velocity first, position on the updated velocity
	// (the IntegrateProcessor family -- stable parabola at fixed DT).
	InOutVelMps.Z -= GravityMps2 * DT;
	InOutPosM += InOutVelMps * DT;
}

bool MarchClear(
	const FBattleCostGrid& Grid, const FVector2D& FromM, float FromEyeZM,
	const FVector2D& ToM, float ToZM, float ClearanceM)
{
	const FVector2D Delta = ToM - FromM;
	const float Dist = Delta.Size();
	if (Dist <= FBallisticsConstants::DistanceEpsilonM)
	{
		return true; // zero-length segment: nothing to occlude
	}

	// Sub-cell march (CellSize/2), deduped per crossed cell -- a DDA variant:
	// deterministic, visits every cell the segment passes through, and reads
	// the heightfield at most once per cell.
	const float StepM = FMath::Max(Grid.CellSize * 0.5f, 0.25f);
	const int32 NumSteps = FMath::CeilToInt(Dist / StepM);
	int32 LastX = INT32_MIN;
	int32 LastY = INT32_MIN;
	for (int32 i = 1; i <= NumSteps; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(NumSteps);
		const FVector2D Pos = FromM + Delta * T;
		int32 CellX = 0;
		int32 CellY = 0;
		Grid.WorldToCell(Pos, CellX, CellY);
		if (CellX == LastX && CellY == LastY)
		{
			continue; // same cell as the last sample
		}
		LastX = CellX;
		LastY = CellY;

		// Ray height at this station (linear interpolation eye -> target).
		const float RayZ = FromEyeZM + (ToZM - FromEyeZM) * T;
		const float TerrainZ = Grid.SampleHeight(Pos);

		// RC3's equal-height rule: occlusion requires terrain STRICTLY above
		// the ray plus clearance -- equal heights never block (flat ground is
		// always clear).
		if (TerrainZ > RayZ + ClearanceM)
		{
			return false;
		}
	}
	return true;
}

FBallisticsMath::FPoolStepCounts FBallisticsMath::StepPool(
	FBattleProjectilePool& Pool, const FBattleCostGrid* Grid, float DT)
{
	FPoolStepCounts Counts;
	for (int32 Slot = 0; Slot < Pool.GetCapacity(); ++Slot)
	{
		if (!Pool.IsLive(Slot))
		{
			continue;
		}
		FVector Pos(Pool.PosX[Slot], Pool.PosY[Slot], Pool.PosZ[Slot]);
		FVector Vel(Pool.VelX[Slot], Pool.VelY[Slot], Pool.VelZ[Slot]);
		StepProjectile(Pos, Vel, FBallisticsConstants::GravityMps2 * Pool.GravityScale[Slot], DT);
		Pool.PosX[Slot] = Pos.X;
		Pool.PosY[Slot] = Pos.Y;
		Pool.PosZ[Slot] = Pos.Z;
		Pool.VelX[Slot] = Vel.X;
		Pool.VelY[Slot] = Vel.Y;
		Pool.VelZ[Slot] = Vel.Z;
		Pool.TTL[Slot] -= DT;

		const float GroundZ = Grid ? Grid->SampleHeight(FVector2D(Pos.X, Pos.Y)) : 0.f;
		if (Pos.Z <= GroundZ)
		{
			// Task 5 replaces this terminal path's stub with the transient
			// roll context; v1 the release IS the resolution (log-only).
			++Counts.GroundHits;
			Pool.Release(Slot);
		}
		else if (Pool.TTL[Slot] <= 0.f)
		{
			++Counts.TtlExpired;
			Pool.Release(Slot);
		}
	}
	return Counts;
}

} // namespace FBallisticsMath
