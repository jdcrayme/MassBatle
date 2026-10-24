#include "BattlePathPlanner.h"
#include "Algo/Reverse.h"

void FBattlePlannedPath::RebuildCumulativeS()
{
	CumulativeS.SetNumUninitialized(Points.Num());
	CumulativeS[0] = 0.f;
	float Total = 0.f;
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		Total += FVector2D::Distance(Points[i - 1], Points[i]);
		CumulativeS[i] = Total;
	}
	TotalLength = Total;
}

namespace
{
	// Fixed neighbor order makes heap insertion order -- and therefore the
	// exact tie-break sequence -- identical run to run (ground rule 5).
	static constexpr int32 NeighborDX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static constexpr int32 NeighborDY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	// Octile heuristic (admissible for 8-connected uniform-cost grids; the
	// slope term is ignored -- stays admissible).
	float Heuristic(int32 FromX, int32 FromY, int32 ToX, int32 ToY, float CellSize)
	{
		const float DX = FMath::Abs(static_cast<float>(FromX - ToX));
		const float DY = FMath::Abs(static_cast<float>(FromY - ToY));
		const float Min = FMath::Min(DX, DY);
		const float Max = FMath::Max(DX, DY);
		return CellSize * (Max - Min + Min * UE_SQRT_2);
	}

	/** Deterministic open-set: min-heap keyed strictly on (F, H, Index). */
	struct FHeapNode
	{
		float F = 0.f;
		float H = 0.f;
		int32 CellIndex = INDEX_NONE;
	};

	// Strict less: equal F and H falls back to the cell index, so the pop
	// order is a total order -- identical across runs (no float-equality ties
	// escape into insertion-order dependence).
	bool HeapLess(const FHeapNode& A, const FHeapNode& B)
	{
		if (A.F != B.F)
		{
			return A.F < B.F;
		}
		if (A.H != B.H)
		{
			return A.H < B.H;
		}
		return A.CellIndex < B.CellIndex;
	}

	void HeapPush(TArray<FHeapNode>& Heap, const FHeapNode& Node)
	{
		Heap.Add(Node);
		int32 i = Heap.Num() - 1;
		while (i > 0)
		{
			const int32 Parent = (i - 1) / 2;
			if (HeapLess(Heap[i], Heap[Parent]))
			{
				Swap(Heap[i], Heap[Parent]);
				i = Parent;
			}
			else
			{
				break;
			}
		}
	}

	FHeapNode HeapPop(TArray<FHeapNode>& Heap)
	{
		const FHeapNode Top = Heap[0];
		Heap[0] = Heap.Last();
		Heap.Pop();
		const int32 Count = Heap.Num();
		int32 i = 0;
		while (true)
		{
			const int32 Left = 2 * i + 1;
			const int32 Right = 2 * i + 2;
			int32 Best = i;
			if (Left < Count && HeapLess(Heap[Left], Heap[Best]))
			{
				Best = Left;
			}
			if (Right < Count && HeapLess(Heap[Right], Heap[Best]))
			{
				Best = Right;
			}
			if (Best == i)
			{
				break;
			}
			Swap(Heap[i], Heap[Best]);
			i = Best;
		}
		return Top;
	}
}

bool FBattlePathPlanner::FindPath(const FBattleCostGrid& Grid, const FVector2D& StartMeters, const FVector2D& GoalMeters,
	float SlopeWeight, FBattlePlannedPath& OutPath)
{
	OutPath.Points.Reset();
	OutPath.CumulativeS.Reset();
	OutPath.TotalLength = 0.f;
	if (Grid.DimX <= 0 || Grid.DimY <= 0)
	{
		return false;
	}

	int32 StartX, StartY, GoalX, GoalY;
	Grid.WorldToCell(StartMeters, StartX, StartY);
	Grid.WorldToCell(GoalMeters, GoalX, GoalY);
	if (!Grid.IsInside(StartX, StartY) || !Grid.IsInside(GoalX, GoalY))
	{
		return false;
	}
	if (Grid.IsImpassable(StartX, StartY) || Grid.IsImpassable(GoalX, GoalY))
	{
		return false;
	}
	if (StartX == GoalX && StartY == GoalY)
	{
		OutPath.Points = { StartMeters, GoalMeters };
		OutPath.RebuildCumulativeS();
		return true;
	}

	const int32 NumCells = Grid.NumCells();
	const int32 StartIndex = Grid.IndexOf(StartX, StartY);
	const int32 GoalIndex = Grid.IndexOf(GoalX, GoalY);

	TArray<float> GScore;
	GScore.Init(FLT_MAX, NumCells);
	TArray<int32> CameFrom;
	CameFrom.Init(INDEX_NONE, NumCells);

	TArray<FHeapNode> Open;
	Open.Reserve(256);
	GScore[StartIndex] = 0.f;
	FHeapNode StartNode;
	StartNode.H = Heuristic(StartX, StartY, GoalX, GoalY, Grid.CellSize);
	StartNode.F = StartNode.H;
	StartNode.CellIndex = StartIndex;
	HeapPush(Open, StartNode);

	TArray<uint8> Closed;
	Closed.Init(0, NumCells);

	bool bReached = false;
	while (Open.Num() > 0)
	{
		const FHeapNode Current = HeapPop(Open);
		const int32 CurIndex = Current.CellIndex;
		if (Closed[CurIndex])
		{
			continue;
		}
		Closed[CurIndex] = 1;
		if (CurIndex == GoalIndex)
		{
			bReached = true;
			break;
		}
		const int32 CurX = CurIndex % Grid.DimX;
		const int32 CurY = CurIndex / Grid.DimX;

		for (int32 N = 0; N < 8; ++N)
		{
			const int32 NX = CurX + NeighborDX[N];
			const int32 NY = CurY + NeighborDY[N];
			if (!Grid.IsInside(NX, NY) || Grid.IsImpassable(NX, NY))
			{
				continue;
			}
			// No corner cutting: a diagonal needs both orthogonal legs passable.
			if (NeighborDX[N] != 0 && NeighborDY[N] != 0 &&
				(Grid.IsImpassable(CurX + NeighborDX[N], CurY) || Grid.IsImpassable(CurX, CurY + NeighborDY[N])))
			{
				continue;
			}
			const int32 NIndex = Grid.IndexOf(NX, NY);
			if (Closed[NIndex])
			{
				continue;
			}
			const float StepDist = ((NeighborDX[N] != 0) && (NeighborDY[N] != 0)) ? Grid.CellSize * UE_SQRT_2 : Grid.CellSize;
			// Slope-aware cost: distance plus weighted height difference.
			const float StepCost = StepDist + SlopeWeight * FMath::Abs(Grid.GetHeight(NX, NY) - Grid.GetHeight(CurX, CurY));
			const float TentativeG = GScore[CurIndex] + StepCost * (0.01f * Grid.GetCost(NX, NY) + 1.f);
			if (TentativeG < GScore[NIndex])
			{
				GScore[NIndex] = TentativeG;
				CameFrom[NIndex] = CurIndex;
				FHeapNode Node;
				Node.H = Heuristic(NX, NY, GoalX, GoalY, Grid.CellSize);
				Node.F = TentativeG + Node.H;
				Node.CellIndex = NIndex;
				HeapPush(Open, Node);
			}
		}
	}

	if (!bReached)
	{
		return false;
	}

	// Walk the chain backwards (goal -> start), then reverse.
	TArray<FVector2D> Reversed;
	for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = CameFrom[Index])
	{
		const int32 X = Index % Grid.DimX;
		const int32 Y = Index / Grid.DimX;
		Reversed.Add(Grid.CellCenter(X, Y));
		if (Index == StartIndex)
		{
			break;
		}
	}
	Algo::Reverse(Reversed);
	OutPath.Points = MoveTemp(Reversed);
	// Snap the endpoints to the exact request (cell centers are interior data).
	OutPath.Points[0] = StartMeters;
	OutPath.Points.Last() = GoalMeters;
	OutPath.RebuildCumulativeS();
	return true;
}

bool FBattlePathPlanner::SegmentIsClear(const FBattleCostGrid& Grid, const FVector2D& A, const FVector2D& B, float ClearanceMeters)
{
	const float Len = FVector2D::Distance(A, B);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Len / (Grid.CellSize * 0.5f)));
	for (int32 i = 0; i <= Steps; ++i)
	{
		const FVector2D P = FMath::Lerp(A, B, static_cast<float>(i) / Steps);
		int32 PX, PY;
		Grid.WorldToCell(P, PX, PY);
		// Every cell whose AABB comes within Clearance of P must be passable.
		const int32 Range = FMath::CeilToInt32(ClearanceMeters / Grid.CellSize);
		for (int32 DY = -Range; DY <= Range; ++DY)
		{
			for (int32 DX = -Range; DX <= Range; ++DX)
			{
				const int32 CX = PX + DX;
				const int32 CY = PY + DY;
				if (!Grid.IsInside(CX, CY) || Grid.IsImpassable(CX, CY))
				{
					// Distance from P to this cell's AABB.
					const FVector2D CellMin = Grid.Origin + FVector2D(CX, CY) * Grid.CellSize;
					const FVector2D CellMax = CellMin + FVector2D(Grid.CellSize);
					const FVector2D Clamped(FMath::Clamp(P.X, CellMin.X, CellMax.X), FMath::Clamp(P.Y, CellMin.Y, CellMax.Y));
					if (FVector2D::Distance(P, Clamped) <= ClearanceMeters)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

void FBattlePathPlanner::Smooth(const FBattleCostGrid& Grid, float ClearanceMeters, FBattlePlannedPath& InOutPath)
{
	if (!InOutPath.IsValid())
	{
		return;
	}
	TArray<FVector2D> Smoothed;
	Smoothed.Add(InOutPath.Points[0]);
	int32 Anchor = 0;
	while (Anchor < InOutPath.Points.Num() - 1)
	{
		// Greedy farthest visible point (ties resolved by scanning order --
		// deterministic).
		int32 Best = Anchor + 1;
		for (int32 Candidate = InOutPath.Points.Num() - 1; Candidate > Anchor + 1; --Candidate)
		{
			if (SegmentIsClear(Grid, InOutPath.Points[Anchor], InOutPath.Points[Candidate], ClearanceMeters))
			{
				Best = Candidate;
				break;
			}
		}
		Smoothed.Add(InOutPath.Points[Best]);
		Anchor = Best;
	}
	InOutPath.Points = MoveTemp(Smoothed);
	InOutPath.RebuildCumulativeS();
}

void FBattlePathPlanner::Resample(const FBattlePlannedPath& InPath, float MaxSpacingMeters, FBattlePlannedPath& OutPath)
{
	if (!InPath.IsValid() || MaxSpacingMeters <= KINDA_SMALL_NUMBER)
	{
		OutPath.Points.Reset();
		OutPath.CumulativeS.Reset();
		OutPath.TotalLength = 0.f;
		return;
	}
	// Copy the inputs first: OutPath is allowed to alias InPath.
	const TArray<FVector2D> SrcPoints = InPath.Points;

	OutPath.Points.Reset();
	OutPath.CumulativeS.Reset();
	OutPath.TotalLength = 0.f;
	OutPath.Points.Add(SrcPoints[0]);
	for (int32 i = 1; i < SrcPoints.Num(); ++i)
	{
		const FVector2D A = SrcPoints[i - 1];
		const FVector2D B = SrcPoints[i];
		const float Len = FVector2D::Distance(A, B);
		const int32 Splits = FMath::FloorToInt32(Len / MaxSpacingMeters);
		for (int32 k = 1; k <= Splits; ++k)
		{
			OutPath.Points.Add(FMath::Lerp(A, B, static_cast<float>(k) / (Splits + 1)));
		}
		OutPath.Points.Add(B);
	}
	OutPath.RebuildCumulativeS();
}
