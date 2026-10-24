#include "BattleSlotAssigner.h"

namespace
{
	/** Squared distance from point P to the 2D segment [A,B]. */
	float PointSegmentDistSq(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq < KINDA_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(P, A);
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f);
		return FVector2D::DistSquared(P, A + AB * T);
	}
}

namespace FSlotAssigner
{
	void Assign(const TArray<FVector2D>& EntityPositions, const TArray<FVector2D>& SlotPositions,
		int32 NumFiles, const TArray<int32>& PreviousAssignment, int32 MaxFileDisplacement,
		TArray<int32>& OutAssignment)
	{
		const int32 NumEntities = EntityPositions.Num();
		const int32 NumSlots = SlotPositions.Num();
		OutAssignment.Init(INDEX_NONE, NumEntities);
		if (NumSlots == 0)
		{
			return;
		}

		TArray<bool> SlotTaken;
		SlotTaken.Init(false, NumSlots);

		auto FileOf = [NumFiles](int32 Slot) { return NumFiles > 0 ? Slot % NumFiles : Slot; };

		// Entities bid in roster order -- deterministic.
		for (int32 E = 0; E < NumEntities; ++E)
		{
			// Pass 1: nearest vacant slot inside the k-file displacement bound.
			// Pass 2 (fallback): nearest vacant slot anywhere -- a bijection is
			// the hard guarantee; the bound is best-effort.
			for (int32 Pass = 0; Pass < 2 && OutAssignment[E] == INDEX_NONE; ++Pass)
			{
				float BestDist = FLT_MAX;
				int32 BestSlot = INDEX_NONE;
				for (int32 S = 0; S < NumSlots; ++S)
				{
					if (SlotTaken[S])
					{
						continue;
					}
					if (Pass == 0 && PreviousAssignment.IsValidIndex(E) && PreviousAssignment[E] != INDEX_NONE &&
						FMath::Abs(FileOf(S) - FileOf(PreviousAssignment[E])) > MaxFileDisplacement)
					{
						continue;
					}
					const float Dist = FVector2D::DistSquared(EntityPositions[E], SlotPositions[S]);
					// Strict < keeps the LOWEST slot index on distance ties.
					if (Dist < BestDist)
					{
						BestDist = Dist;
						BestSlot = S;
					}
				}
				if (BestSlot != INDEX_NONE)
				{
					OutAssignment[E] = BestSlot;
					SlotTaken[BestSlot] = true;
				}
			}
		}
	}

	void FindCrossingPairs(const TArray<FVector2D>& Positions, const TArray<FVector2D>& Velocities,
		const TArray<int32>& Assignment, float HorizonSeconds, float ThresholdMeters, TArray<FIntPoint>& OutPairs)
	{
		OutPairs.Reset();
		const float ThresholdSq = ThresholdMeters * ThresholdMeters;
		const int32 N = Positions.Num();
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 J = I + 1; J < N; ++J)
			{
				if (!Assignment.IsValidIndex(I) || !Assignment.IsValidIndex(J) ||
					Assignment[I] == Assignment[J])
				{
					continue;
				}
				const FVector2D A0 = Positions[I];
				const FVector2D A1 = A0 + Velocities[I] * HorizonSeconds;
				const FVector2D B0 = Positions[J];
				const FVector2D B1 = B0 + Velocities[J] * HorizonSeconds;

				// Segment-segment proximity: exact intersection OR any of the
				// four endpoint-to-other-segment distances under threshold.
				// (Head-on/parallel convergence never intersects in the strict
				// sense -- the threshold check is the real detector.)
				const FVector2D D1 = A1 - A0;
				const FVector2D D2 = B1 - B0;
				const float Denom = D1.X * D2.Y - D1.Y * D2.X;
				bool bClose = false;
				if (FMath::Abs(Denom) >= KINDA_SMALL_NUMBER)
				{
					const FVector2D AB = B0 - A0;
					const float T = (AB.X * D2.Y - AB.Y * D2.X) / Denom;
					const float U = (AB.X * D1.Y - AB.Y * D1.X) / Denom;
					bClose = T >= 0.f && T <= 1.f && U >= 0.f && U <= 1.f;
				}
				if (!bClose)
				{
					bClose = PointSegmentDistSq(A0, B0, B1) <= ThresholdSq
						|| PointSegmentDistSq(A1, B0, B1) <= ThresholdSq
						|| PointSegmentDistSq(B0, A0, A1) <= ThresholdSq
						|| PointSegmentDistSq(B1, A0, A1) <= ThresholdSq;
				}
				if (bClose)
				{
					OutPairs.Add(FIntPoint(I, J));
				}
			}
		}
	}

	void ApplyPredictiveSwaps(TArray<int32>& InOutAssignment, const TArray<FIntPoint>& Pairs)
	{
		for (const FIntPoint& Pair : Pairs)
		{
			if (InOutAssignment.IsValidIndex(Pair.X) && InOutAssignment.IsValidIndex(Pair.Y))
			{
				Swap(InOutAssignment[Pair.X], InOutAssignment[Pair.Y]);
			}
		}
	}
}

bool FReflowScheduler::ConsumeIfDue(float DT, bool bRankGapDetected)
{
	Cooldown = FMath::Max(0.f, Cooldown - DT);
	if (Cooldown > 0.f && !bRankGapDetected)
	{
		return false;
	}
	Cooldown = IntervalSeconds;
	return true;
}
