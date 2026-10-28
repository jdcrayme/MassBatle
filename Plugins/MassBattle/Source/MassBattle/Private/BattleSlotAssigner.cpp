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

	void CloseGaps(TArray<int32>& InOutAssignment, int32 NumSlots, int32 NumFiles)
	{
		const int32 NumEntities = InOutAssignment.Num();
		if (NumSlots <= 0 || NumFiles <= 0 || NumEntities == 0)
		{
			return;
		}

		// Inverse map slot -> entity, mutated in place so later fills observe
		// earlier ones (the within-pass cascade).
		TArray<int32> SlotOwner;
		SlotOwner.Init(INDEX_NONE, NumSlots);
		for (int32 E = 0; E < NumEntities; ++E)
		{
			const int32 S = InOutAssignment[E];
			if (S >= 0 && S < NumSlots)
			{
				SlotOwner[S] = E;
			}
		}

		// Bounded fixpoint (replica-verified): a single pass can leave a
		// hole whose behind slot was VACANT at scan time but got laterally
		// filled later in the same pass -- that hole becomes
		// behind-fillable and needs one more look. Every productive fill
		// strictly decreases the owners' total slot index, so passes
		// terminate; the cap bounds the worst case and any residual hole
		// drains on the next reflow (0.5 s cadence -- the Brownian
		// relaxation the owner described).
		for (int32 Guard = 0; Guard < 4; ++Guard)
		{
			bool bChanged = false;
			// Front-to-back, file-ascending IS slot-index ascending
			// (rank-major, file-ascending layout). Both fill sources sit
			// at HIGHER indices: the behind source S+NumFiles and the
			// right source S+1 (same rank, so only when file+1 <
			// NumFiles). A vacancy created by a fill is therefore
			// processed later this same pass, and within a pass each
			// entity moves at most once (a filled entity sits at a LOWER
			// index than any slot still to be processed, so nothing
			// targets it again). Holes walk toward the rear / high-file
			// drain corner.
			for (int32 S = 0; S < NumSlots; ++S)
			{
				if (SlotOwner[S] != INDEX_NONE)
				{
					continue;
				}
				const int32 Behind = S + NumFiles; // same file, next rank: fill FRONT (rule 1a)
				const int32 Right = S + 1;         // next file, same rank: fill LEFT (rule 1b)
				const bool bRightSameRank = (S % NumFiles) + 1 < NumFiles;
				int32 Taker = INDEX_NONE;
				int32 Vacated = INDEX_NONE;
				if (Behind < NumSlots && SlotOwner[Behind] != INDEX_NONE)
				{
					Taker = SlotOwner[Behind];
					Vacated = Behind;
				}
				else if (bRightSameRank && Right < NumSlots && SlotOwner[Right] != INDEX_NONE)
				{
					Taker = SlotOwner[Right];
					Vacated = Right;
				}
				if (Taker != INDEX_NONE)
				{
					SlotOwner[S] = Taker;
					SlotOwner[Vacated] = INDEX_NONE;
					InOutAssignment[Taker] = S;
					bChanged = true;
				}
			}
			if (!bChanged)
			{
				break;
			}
		}
	}

	void EvictToReachable(TArray<int32>& InOutAssignment, const TArray<FVector2D>& EntityPositions,
		const TArray<FVector2D>& SlotPositions, int32 NumSlots, int32 NumFiles, const TArray<uint8>& StuckFlags)
	{
		const int32 NumEntities = InOutAssignment.Num();
		if (NumSlots <= 0 || NumFiles <= 0 || NumEntities == 0)
		{
			return;
		}

		TArray<int32> SlotOwner;
		SlotOwner.Init(INDEX_NONE, NumSlots);
		for (int32 E = 0; E < NumEntities; ++E)
		{
			const int32 S = InOutAssignment[E];
			if (S >= 0 && S < NumSlots)
			{
				SlotOwner[S] = E;
			}
		}

		// Roster order -- deterministic; a later eviction observes the
		// earlier ones' result.
		for (int32 E = 0; E < NumEntities; ++E)
		{
			if (E >= StuckFlags.Num() || StuckFlags[E] == 0 || !EntityPositions.IsValidIndex(E))
			{
				continue;
			}
			const int32 OldSlot = InOutAssignment[E];
			if (OldSlot < 0 || OldSlot >= NumSlots)
			{
				continue; // no owned slot -> no sink; the greedy vacant-bid path already covers it
			}

			// Claim the slot nearest the BODY, occupancy ignored (the stuck
			// entity stands outside the block; its nearest slot is a
			// reachable perimeter slot). Ties: lowest slot index.
			float BestDistSq = FLT_MAX;
			int32 NewSlot = INDEX_NONE;
			for (int32 S = 0; S < NumSlots; ++S)
			{
				const float DistSq = FVector2D::DistSquared(EntityPositions[E], SlotPositions[S]);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					NewSlot = S;
				}
			}
			if (NewSlot == INDEX_NONE || NewSlot == OldSlot)
			{
				continue; // already owns the nearest slot: nothing to fix
			}
			// 2-opt-style churn guard (the predictive swaps' doctrine):
			// evict only for a REAL reachability win -- the claimed slot
			// at less than half the old slot's squared distance. Marginally
			// nearer slots never justify displacing a settled owner.
			if (!(BestDistSq * 2.f < FVector2D::DistSquared(EntityPositions[E], SlotPositions[OldSlot])))
			{
				continue;
			}

			// Monotone staircase NewSlot -> OldSlot: file axis first (toward
			// the sink's file), then the rank axis. Every hop lands on an
			// existing slot or the eviction is skipped (a partial rear rank
			// can make an intermediate step nonexistent).
			const int32 SinkR = OldSlot / NumFiles;
			const int32 SinkF = OldSlot % NumFiles;
			TArray<int32> Path; // slots AFTER NewSlot, ending AT OldSlot
			{
				int32 R = NewSlot / NumFiles;
				int32 F = NewSlot % NumFiles;
				bool bValid = true;
				while ((R != SinkR || F != SinkF) && bValid)
				{
					if (F != SinkF)
					{
						F += (SinkF > F) ? 1 : -1;
					}
					else
					{
						R += (SinkR > R) ? 1 : -1;
					}
					const int32 Idx = R * NumFiles + F;
					if (Idx < 0 || Idx >= NumSlots)
					{
						bValid = false;
					}
					else
					{
						Path.Add(Idx);
					}
				}
				if (!bValid)
				{
					continue; // staircase leaves the slot set: keep the current assignment
				}
			}

			// E takes the claimed slot; the displaced owner chain walks the
			// staircase one slot each and terminates on E's vacated old slot
			// (the sink) -- or earlier, absorbed by a mid-path vacancy, which
			// then migrates to the sink for CloseGaps to drain.
			int32 Carry = SlotOwner[NewSlot];
			SlotOwner[NewSlot] = E;
			InOutAssignment[E] = NewSlot;
			SlotOwner[OldSlot] = INDEX_NONE; // E vacated -- the sink
			for (int32 P = 0; P < Path.Num(); ++P)
			{
				if (Carry == INDEX_NONE)
				{
					break; // mid-path vacancy absorbed the chain
				}
				const int32 Next = SlotOwner[Path[P]];
				InOutAssignment[Carry] = Path[P];
				SlotOwner[Path[P]] = Carry;
				Carry = Next;
			}
		}
	}

	void DiffuseVacancies(TArray<int32>& InOutAssignment, const TArray<FVector2D>& EntityPositions,
		const TArray<FVector2D>& SlotPositions, int32 NumSlots, int32 NumFiles,
		float StaleRadiusM, const TArray<uint8>& SkipFlags)
	{
		const int32 NumEntities = InOutAssignment.Num();
		if (NumSlots <= 0 || NumFiles <= 0 || NumEntities == 0 || StaleRadiusM <= 0.f)
		{
			return; // degenerate layout or the policy is off
		}

		// Inverse map slot -> entity (the CloseGaps contract), rebuilt
		// after each mutation phase so later passes observe earlier ones.
		TArray<int32> SlotOwner;
		SlotOwner.Init(INDEX_NONE, NumSlots);
		for (int32 E = 0; E < NumEntities; ++E)
		{
			const int32 S = InOutAssignment[E];
			if (S >= 0 && S < NumSlots)
			{
				SlotOwner[S] = E;
			}
		}

		// Pass 1 -- evict stale claims. This is the trigger the v1 patch
		// never had: an entity farther than StaleRadiusM from the slot it
		// HOLDS does not occupy it in any physical sense, but the greedy
		// bijection still counts it -- no INDEX_NONE appears, the rank-gap
		// detector stays quiet, CloseGaps finds nothing to close. Dropping
		// the claim converts the VIRTUAL hole (claimed-but-far) into a real
		// vacancy the cascade can drain. Roster order; distance ties are
		// impossible (threshold comparison).
		const float StaleSq = StaleRadiusM * StaleRadiusM;
		for (int32 E = 0; E < NumEntities; ++E)
		{
			const int32 S = InOutAssignment[E];
			if (S < 0 || S >= NumSlots)
			{
				continue; // already a hole-walker; the re-home pass covers it
			}
			if (SkipFlags.IsValidIndex(E) && SkipFlags[E] != 0)
			{
				continue; // combat ring owns the motion; the distance is not staleness
			}
			if (!EntityPositions.IsValidIndex(E) || !SlotPositions.IsValidIndex(S))
			{
				continue;
			}
			if (FVector2D::DistSquared(EntityPositions[E], SlotPositions[S]) > StaleSq)
			{
				SlotOwner[S] = INDEX_NONE;
				InOutAssignment[E] = INDEX_NONE;
			}
		}

		// Pass 2 -- the CloseGaps cascade over the opened slots: the
		// directly-behind (else directly-right) neighbor claims each hole
		// and moves in; holes walk one step per fill toward the rear-right
		// drain corner. In a full unit each eviction opened exactly one
		// hole, and the cascade drags it to the rear where the evicted
		// entity physically is -- the vacancy diffuses to its owner.
		CloseGaps(InOutAssignment, NumSlots, NumFiles);

		// Rebuild the inverse map: the cascade mutated the assignment.
		SlotOwner.Init(INDEX_NONE, NumSlots);
		for (int32 E = 0; E < NumEntities; ++E)
		{
			const int32 S = InOutAssignment[E];
			if (S >= 0 && S < NumSlots)
			{
				SlotOwner[S] = E;
			}
		}

		// Pass 3 -- re-home: evicted (and never-assigned) entities walk
		// into the remaining holes nearest their BODIES. Roster order,
		// lowest slot index on distance ties (the greedy contract).
		// Surplus entities (roster > slots) find no hole and stay
		// INDEX_NONE -- the bijection is never forced.
		for (int32 E = 0; E < NumEntities; ++E)
		{
			if (InOutAssignment[E] != INDEX_NONE || !EntityPositions.IsValidIndex(E))
			{
				continue;
			}
			float BestDistSq = FLT_MAX;
			int32 BestSlot = INDEX_NONE;
			for (int32 S = 0; S < NumSlots; ++S)
			{
				if (SlotOwner[S] != INDEX_NONE || !SlotPositions.IsValidIndex(S))
				{
					continue;
				}
				const float DistSq = FVector2D::DistSquared(EntityPositions[E], SlotPositions[S]);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestSlot = S;
				}
			}
			if (BestSlot != INDEX_NONE)
			{
				SlotOwner[BestSlot] = E;
				InOutAssignment[E] = BestSlot;
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
