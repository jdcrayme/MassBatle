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

	void AssignFinal(const TArray<FVector2D>& EntityPositions, const FVector2D& FinalAnchor,
		float FinalFacingDeg, int32 NumSlots, int32 NumFiles, TArray<int32>& OutSlotOf)
	{
		const int32 NumEntities = EntityPositions.Num();
		OutSlotOf.Init(INDEX_NONE, NumEntities);
		if (NumSlots <= 0 || NumFiles <= 0 || NumEntities == 0)
		{
			return;
		}

		// Order axes in the final facing frame. F = facing direction (rank
		// depth axis, front is +F); L = the lateral axis in FILE-ASCENDING
		// order -- the same rotation AnchorFormula applies to its layout
		// offset (offset Y = file-ascending), so projections and layout agree
		// bit-for-bit on which side is "left".
		const float Rad = FMath::DegreesToRadians(FinalFacingDeg);
		const FVector2D F(FMath::Cos(Rad), FMath::Sin(Rad));
		const FVector2D L(-F.Y, F.X);

		// Entities: front-most first (largest F projection), tie-break lateral
		// ascending (matching file-ascending slot order), then roster index.
		// Local buffer: AssignFinal runs at event rate (layout re-decisions),
		// not per frame -- no scratch amortization needed.
		TArray<int32> EntOrder;
		EntOrder.Reserve(NumEntities);
		for (int32 i = 0; i < NumEntities; ++i)
		{
			EntOrder.Add(i);
		}
		EntOrder.Sort([&](int32 A, int32 B)
		{
			const FVector2D DA = EntityPositions[A] - FinalAnchor;
			const FVector2D DB = EntityPositions[B] - FinalAnchor;
			const float FA = FVector2D::DotProduct(DA, F);
			const float FB = FVector2D::DotProduct(DB, F);
			if (!FMath::IsNearlyEqual(FA, FB, 1e-4f))
			{
				return FA > FB;
			}
			const float LA = FVector2D::DotProduct(DA, L);
			const float LB = FVector2D::DotProduct(DB, L);
			if (!FMath::IsNearlyEqual(LA, LB, 1e-4f))
			{
				return LA < LB;
			}
			return A < B;
		});

		// Slots in canonical order: rank-major, file-ascending IS sorted by
		// (facing-projection descending, lateral ascending) -- rank r sits r
		// spacings behind the front along F, files ascend along L. Pairing the
		// i-th front-most entity with the i-th front-most slot leaves any
		// surplus (casualty holes) at the REAR.
		for (int32 i = 0; i < NumEntities; ++i)
		{
			OutSlotOf[EntOrder[i]] = FMath::Min(i, NumSlots - 1);
		}
	}

	void FindCrossingPairs(const TArray<FVector2D>& Positions, const TArray<FVector2D>& Velocities,
		const TArray<int32>& Assignment, const TArray<FVector2D>& SlotPositions,
		float HorizonSeconds, float ThresholdMeters, TArray<FIntPoint>& OutPairs)
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
				// 2-opt guard: swap only if each entity is strictly nearer the
				// other's slot -- prevents swap/reswap thrash at meeting points.
				if (Assignment[I] >= 0 && Assignment[J] >= 0 &&
					SlotPositions.IsValidIndex(Assignment[I]) && SlotPositions.IsValidIndex(Assignment[J]))
				{
					const float OwnI = FVector2D::DistSquared(Positions[I], SlotPositions[Assignment[I]]);
					const float OwnJ = FVector2D::DistSquared(Positions[J], SlotPositions[Assignment[J]]);
					const float SwapI = FVector2D::DistSquared(Positions[I], SlotPositions[Assignment[J]]);
					const float SwapJ = FVector2D::DistSquared(Positions[J], SlotPositions[Assignment[I]]);
					if (!(SwapI < OwnI && SwapJ < OwnJ))
					{
						continue;
					}
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

	void ApplyPredictiveSwaps(TArray<int32>& InOutAssignment, const TArray<FIntPoint>& Pairs,
		const TArray<FVector2D>& Positions, const TArray<FVector2D>& SlotPositions)
	{
		// Audit M3: the detection-time 2-opt guard was evaluated against the
		// assignment as it stood when FindCrossingPairs ran. Applied chains
		// like (1,2),(2,3) mutated the assignment between pairs, so a later
		// swap could execute without its guard promise holding -- a
		// swap-created trek bypassing the k-file bound. Re-check the guard
		// against the CURRENT assignment at apply time and use each entity
		// at most once per pass.
		TSet<int32> Used;
		for (const FIntPoint& Pair : Pairs)
		{
			const int32 I = Pair.X;
			const int32 J = Pair.Y;
			if (!InOutAssignment.IsValidIndex(I) || !InOutAssignment.IsValidIndex(J)
				|| Used.Contains(I) || Used.Contains(J))
			{
				continue;
			}
			const int32 SlotI = InOutAssignment[I];
			const int32 SlotJ = InOutAssignment[J];
			if (SlotI < 0 || SlotJ < 0 || SlotI == SlotJ
				|| !SlotPositions.IsValidIndex(SlotI) || !SlotPositions.IsValidIndex(SlotJ)
				|| !Positions.IsValidIndex(I) || !Positions.IsValidIndex(J))
			{
				continue;
			}
			const float OwnI = FVector2D::DistSquared(Positions[I], SlotPositions[SlotI]);
			const float OwnJ = FVector2D::DistSquared(Positions[J], SlotPositions[SlotJ]);
			const float SwapI = FVector2D::DistSquared(Positions[I], SlotPositions[SlotJ]);
			const float SwapJ = FVector2D::DistSquared(Positions[J], SlotPositions[SlotI]);
			if (SwapI < OwnI && SwapJ < OwnJ)
			{
				InOutAssignment[I] = SlotJ;
				InOutAssignment[J] = SlotI;
				Used.Add(I);
				Used.Add(J);
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
