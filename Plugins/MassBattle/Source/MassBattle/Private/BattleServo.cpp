#include "BattleServo.h"

namespace FUnitServo
{
	FVector2D ComputeServoVelocity(const FVector2D& Pos, const FVector2D& SlotPos,
		float Tau, float MaxSpeed, float CatchupBoost)
	{
		const FVector2D Delta = SlotPos - Pos;
		FVector2D V = Delta / FMath::Max(Tau, KINDA_SMALL_NUMBER);
		const float Clamp = FMath::Max(0.f, MaxSpeed * CatchupBoost);
		if (V.SizeSquared() > Clamp * Clamp)
		{
			V = V.GetSafeNormal() * Clamp;
		}
		return V;
	}

	FVector2D ComputeServoVelocityFF(const FVector2D& Pos, const FVector2D& SlotPos,
		const FVector2D& PrevSlotPos, float Tau, float MaxSpeed, float CatchupBoost, float DT)
	{
		// Slot velocity from the resolve stage's own previous write (a moving
		// target); a degenerate DT contributes no feed-forward.
		const FVector2D SlotVel = DT > KINDA_SMALL_NUMBER
			? (SlotPos - PrevSlotPos) / DT
			: FVector2D::ZeroVector;
		const FVector2D Pursuit = (SlotPos - Pos) / FMath::Max(Tau, KINDA_SMALL_NUMBER);
		FVector2D V = SlotVel + Pursuit;
		const float Clamp = FMath::Max(0.f, MaxSpeed * CatchupBoost);
		if (V.SizeSquared() > Clamp * Clamp)
		{
			V = V.GetSafeNormal() * Clamp;
		}
		return V;
	}

	FVector2D ClampAcceleration(const FVector2D& OldVel, const FVector2D& NewVel, float MaxAccel, float DT)
	{
		const FVector2D Delta = NewVel - OldVel;
		const float MaxDelta = FMath::Max(0.f, MaxAccel) * DT;
		if (Delta.SizeSquared() > MaxDelta * MaxDelta)
		{
			return OldVel + Delta.GetSafeNormal() * MaxDelta;
		}
		return NewVel;
	}

	FVector2D ComputeAvoidanceDeflection(const FVector2D& MyPos, const FVector2D& MyDesiredVel,
		float MyRadius, uint32 MyCanonicalKey, const TArray<FAvoidanceNeighbor>& Neighbors,
		float Weight, float HorizonSeconds)
	{
		if (Weight <= 0.f || Neighbors.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}
		FVector2D Deflection = FVector2D::ZeroVector;
		const float DT = FMath::Max(HorizonSeconds, KINDA_SMALL_NUMBER);
		for (const FAvoidanceNeighbor& N : Neighbors)
		{
			const FVector2D RelPos = N.Pos - MyPos;
			const float DistSq = RelPos.SizeSquared();
			const float Combined = MyRadius + N.Radius;

			// Already overlapping: push directly apart (negated RelPos -- the
			// neighbor is AT the near side, so away from them is -RelPos).
			// For degenerate co-located pairs (no geometry to pick a side) split
			// along the desired-direction perpendicular by handle parity so the
			// pair separates instead of fleeing as one blob.
			if (DistSq < Combined * Combined)
			{
				FVector2D Away;
				if (DistSq > KINDA_SMALL_NUMBER)
				{
					Away = -RelPos.GetSafeNormal();
				}
				else
				{
					// Audit L10: the side comes from the handle COMPARISON, not
					// the parity of my index alone -- same-parity co-located
					// pairs fled as one blob under the old rule.
					Away = FVector2D(-MyDesiredVel.Y, MyDesiredVel.X).GetSafeNormal()
						* ((MyCanonicalKey < N.CanonicalKey) ? 1.f : -1.f); // canonical parity (RC8)
				}
				Deflection += Away * Combined;
				continue;
			}

			// Predicted conflict: closest approach of the relative position
			// (neighbor w.r.t. me) under the relative velocity within the
			// horizon.
			const FVector2D RelVel = N.Vel - MyDesiredVel;
			const float RVSq = RelVel.SizeSquared();
			if (RVSq < KINDA_SMALL_NUMBER)
			{
				continue; // not closing
			}
			const float Tc = FMath::Clamp(-FVector2D::DotProduct(RelPos, RelVel) / RVSq, 0.f, DT);
			const FVector2D Closest = RelPos + RelVel * Tc;
			const float MissSq = Closest.SizeSquared();
			if (MissSq >= Combined * Combined)
			{
				continue; // misses within the horizon
			}

			// Lateral deflection: dodge AWAY from the neighbor's side, i.e.
			// along the negative of RelPos's component perpendicular to the
			// approach axis. Head-on (no perpendicular component) picks a
			// deterministic side that is opposite for the two parties.
			const FVector2D Dir = RelVel / FMath::Sqrt(RVSq);
			FVector2D Perp = RelPos - FVector2D::DotProduct(RelPos, Dir) * Dir;
			if (Perp.SizeSquared() < KINDA_SMALL_NUMBER)
			{
				Perp = FVector2D(-Dir.Y, Dir.X);
			}
			Perp = -Perp.GetSafeNormal();
			// Urgency: how much the miss violates the combined radius, plus
			// how soon it happens.
			const float Urgency = (1.f - FMath::Sqrt(MissSq) / Combined) * (1.f - Tc / DT);
			// Asymmetric handle-ordered tie-break (A1): the lower index has
			// priority and deflects half; the higher index yields double.
			const float Asymmetry = (MyCanonicalKey > N.CanonicalKey) ? 2.f : 0.5f; // canonical asymmetry (RC8)
			Deflection += Perp * Urgency * Combined * Asymmetry;
		}
		return Deflection * Weight;
	}
}

namespace FUnstuckLadder
{
	int32 Advance(int32 CurrentLevel, bool bMovingSlowly, bool bNearSlot,
		float DT, float& InOutTimer, float EscalateAfterSeconds)
	{
		if (bNearSlot)
		{
			InOutTimer = 0.f;
			return 0; // recovered: ladder unwinds fully
		}
		if (!bMovingSlowly)
		{
			InOutTimer = 0.f;
			return CurrentLevel; // moving fine: hold the current step (may still be sidestepping)
		}
		InOutTimer += DT;
		if (InOutTimer >= EscalateAfterSeconds)
		{
			InOutTimer = 0.f;
			return FMath::Min(CurrentLevel + 1, 4); // level 4 = unit AI handoff, terminal
		}
		return CurrentLevel;
	}

	float AvoidanceWeightMultiplier(int32 Level)
	{
		return Level >= 1 ? 2.f : 1.f; // step 1: double the avoidance weight
	}

	bool WantsSidestep(int32 Level)
	{
		return Level == 2; // step 2: sidestep impulse perpendicular to the servo
	}

	bool WantsSlotSwap(int32 Level)
	{
		return Level >= 3; // step 3: forced slot swap with the blocking teammate
	}
}
