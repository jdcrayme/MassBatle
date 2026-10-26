// BattleGarrison.cpp -- CVAR block + garrison seam math (B.0 pure functions,
// meters, deterministic, no RNG). Task 2 fixtures/stamping + task 3 path /
// assignment / queue / Z math. Processors stay thin shims around these.
#include "BattleGarrison.h"
#include "BattleMovementFragments.h"
#include "BattleCostGrid.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleGarrisonEnable(
		TEXT("Battle.Garrison.Enable"),
		1,
		TEXT("Enable garrison orders + the locomotion garrison FSM (1 from task 5; 0 restores the pre-garrison behavior for baseline equivalence)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleGarrisonInteriorSpeedFactor(
		TEXT("Battle.Garrison.InteriorSpeedFactor"),
		0.5f,
		TEXT("Interior speed clamp factor: commanded speed x this during Ingress/Egress (A3)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleGarrisonQueueRetry(
		TEXT("Battle.Garrison.QueueRetry"),
		0.5f,
		TEXT("Queue-phase booking retry cadence, seconds (A4)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleGarrisonMaxSlotDisplacement(
		TEXT("Battle.Garrison.MaxSlotDisplacement"),
		3.0f,
		TEXT("Garrison reflow displacement bound, meters (the k-file bound's analog)."),
		ECVF_Default);

	TAutoConsoleVariable<float> GBattleGarrisonMaxZRate(
		TEXT("Battle.Garrison.MaxZRate"),
		2.0f,
		TEXT("Z servo rate bound, m/s (A8)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> GBattleGarrisonSoakOrders(
		TEXT("Battle.Garrison.SoakOrders"),
		0,
		TEXT("1 = the T1.8 soak controller's seeded order mix includes garrison targets. 0 (default) = byte-identical T1.8 baseline."),
		ECVF_Default);

	// Gray-box fixture constants (task 2; F15). One entrance each, declared
	// ClearWidthM = 4 (R-G2). Local frame: +X right, +Y outward/front.
	constexpr float WallTopZ = 6.f;        // m (spec fixture table)
	constexpr float TrenchZ = -0.75f;      // m
	constexpr float SlotSpacingM = 1.2f;   // matches the fixture FileSpacing
	constexpr float PathSampleM = 1.f;     // internal-path sampling step
	constexpr float QueueSpacingM = 12.f;  // hold-point recede step (one unit body)

	FVector2D Rotate(const FVector2D& P, float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		const float C = FMath::Cos(Rad);
		const float S = FMath::Sin(Rad);
		return FVector2D(P.X * C - P.Y * S, P.X * S + P.Y * C);
	}

	/** Samples one leg [A,B] (Z from Za to Zb) into Out at PathSampleM steps,
	 *  skipping A itself (the previous leg's end); appends exact B. */
	void AppendLeg(TArray<FBattlePathPoint3>& Out, const FVector2D& A, float Za,
		const FVector2D& B, float Zb)
	{
		const FVector2D Delta = B - A;
		const float Len = Delta.Size();
		FVector2D Tangent(1.f, 0.f);
		if (Len > KINDA_SMALL_NUMBER)
		{
			Tangent = Delta / Len;
		}
		const int32 Steps = FMath::Max(1, FMath::FloorToInt32(Len / PathSampleM));
		for (int32 i = 1; i <= Steps; ++i)
		{
			const bool bLast = i == Steps;
			const float T = static_cast<float>(i) / static_cast<float>(Steps);
			FBattlePathPoint3 P;
			P.Pos = bLast ? B : A + Delta * T;
			P.Z = FMath::Lerp(Za, Zb, T);
			P.Tangent = Tangent;
			Out.Add(P);
		}
	}

	void ReserializeS(TArray<FBattlePathPoint3>& Path)
	{
		float S = 0.f;
		for (int32 i = 0; i < Path.Num(); ++i)
		{
			if (i > 0)
			{
				S += (Path[i].Pos - Path[i - 1].Pos).Size();
			}
			Path[i].S = S;
		}
	}
}

namespace FGarrisonCVars
{
	int32 Enable() { return GBattleGarrisonEnable.GetValueOnGameThread(); }
	float InteriorSpeedFactor() { return GBattleGarrisonInteriorSpeedFactor.GetValueOnGameThread(); }
	float QueueRetry() { return GBattleGarrisonQueueRetry.GetValueOnGameThread(); }
	float MaxSlotDisplacement() { return GBattleGarrisonMaxSlotDisplacement.GetValueOnAnyThread(); }
	float MaxZRate() { return GBattleGarrisonMaxZRate.GetValueOnAnyThread(); }
	int32 SoakOrders() { return GBattleGarrisonSoakOrders.GetValueOnGameThread(); }
}

namespace FGarrisonLayout
{
	bool MakeFixture(EGarrisonStructureType Type, const FVector2D& PosMeters, float YawDeg,
		FGarrisonStructureRecord& OutRecord)
	{
		FGarrisonStructureRecord Rec;
		Rec.Type = Type;
		Rec.Entrances.AddDefaulted(1);
		FGarrisonEntrance& E = Rec.Entrances[0];
		E.ClearWidthM = 4.f;

		// Local geometry per type. Building: door on the -Y wall; wall/trench:
		// stairs/ramp at the -X end (the entrance choke is the stair/ramp base).
		int32 SlotCount = 0;
		float SlotYaw = 0.f;
		float SlotZ = 0.f;
		FVector2D SlotOrigin = FVector2D::ZeroVector; // slot 0 local pos
		FVector2D SlotDir = FVector2D(1.f, 0.f);      // slot line direction
		FBox2D LocalFootprint(EForceInit::ForceInit);
		FVector2D LocalDoor = FVector2D::ZeroVector;
		FVector2D LocalQueue = FVector2D::ZeroVector;
		FVector2D LocalAssembly = FVector2D::ZeroVector;
		FVector2D LocalNode = FVector2D::ZeroVector;

		switch (Type)
		{
		case EGarrisonStructureType::Building:
		{
			Rec.MaxUnits = 2;
			LocalFootprint = FBox2D(FVector2D(-24.f, -10.f), FVector2D(24.f, 10.f));
			LocalDoor = FVector2D(0.f, -10.f);
			LocalQueue = FVector2D(0.f, -16.f);
			LocalAssembly = FVector2D(0.f, -20.f);
			LocalNode = FVector2D(0.f, -6.f);
			// Two rows of 40 along the long walls: front row on the door side
			// facing its wall, back row along the far wall facing back.
			SlotCount = 80;
			SlotOrigin = FVector2D(-23.4f, -8.5f);
			SlotDir = FVector2D(1.f, 0.f);
			SlotYaw = 180.f;
			SlotZ = 0.f;
			break;
		}
		case EGarrisonStructureType::Wall:
		{
			Rec.MaxUnits = 1;
			LocalFootprint = FBox2D(FVector2D(-32.f, -2.f), FVector2D(32.f, 2.f));
			LocalDoor = FVector2D(-38.f, 0.f); // stair base
			LocalQueue = FVector2D(-44.f, 0.f);
			LocalAssembly = FVector2D(-48.f, 0.f);
			LocalNode = FVector2D(-30.f, 0.f); // on the walkway
			SlotCount = 48;
			SlotOrigin = FVector2D(-27.f, 0.f);
			SlotDir = FVector2D(1.f, 0.f);
			SlotYaw = 90.f; // face outward (+Y)
			SlotZ = WallTopZ;
			break;
		}
		case EGarrisonStructureType::Trench:
		{
			Rec.MaxUnits = 1;
			LocalFootprint = FBox2D(FVector2D(-32.f, -2.f), FVector2D(32.f, 2.f));
			LocalDoor = FVector2D(-38.f, 0.f); // ramp base
			LocalQueue = FVector2D(-44.f, 0.f);
			LocalAssembly = FVector2D(-48.f, 0.f);
			LocalNode = FVector2D(-30.f, 0.f); // channel floor
			SlotCount = 48;
			SlotOrigin = FVector2D(-27.f, 0.f);
			SlotDir = FVector2D(1.f, 0.f);
			SlotYaw = 90.f;
			SlotZ = TrenchZ;
			break;
		}
		default:
			return false;
		}
		E.DoorPos = PosMeters + Rotate(LocalDoor, YawDeg);
		E.QueuePos = PosMeters + Rotate(LocalQueue, YawDeg);
		E.AssemblyPos = PosMeters + Rotate(LocalAssembly, YawDeg);
		E.DistributionPos = PosMeters + Rotate(LocalNode, YawDeg);
		E.DistributionZ = SlotZ;

		// World footprint from the rotated corners (exact for cardinal rotations,
		// the fixture contract; arbitrary yaw gets the conservative AABB).
		Rec.Footprint = FBox2D(EForceInit::ForceInit);
		const FVector2D Corners[4] = {
			Rotate(LocalFootprint.Min, YawDeg),
			Rotate(LocalFootprint.Max, YawDeg),
			Rotate(FVector2D(LocalFootprint.Min.X, LocalFootprint.Max.Y), YawDeg),
			Rotate(FVector2D(LocalFootprint.Max.X, LocalFootprint.Min.Y), YawDeg)
		};
		for (const FVector2D& Corner : Corners)
		{
			Rec.Footprint = Rec.Footprint + (PosMeters + Corner);
		}

		// Slots: building folds a second row back along the far wall (+17 m),
		// facing reversed; the wall/trench lines are single-row.
		Rec.Slots.SetNum(SlotCount);
		for (int32 i = 0; i < SlotCount; ++i)
		{
			const bool bBackRow = Type == EGarrisonStructureType::Building && i >= 40;
			const int32 InRow = bBackRow ? i - 40 : i;
			const FVector2D Local = SlotOrigin + SlotDir * (SlotSpacingM * InRow)
				+ (bBackRow ? FVector2D(0.f, 17.f) : FVector2D::ZeroVector);
			FGarrisonSlot& Slot = Rec.Slots[i];
			Slot.Pos = PosMeters + Rotate(Local, YawDeg);
			Slot.Z = SlotZ;
			Slot.YawDeg = bBackRow ? FMath::Fmod(SlotYaw + 180.f, 360.f) : SlotYaw;
			Slot.EntranceIndex = 0;
		}
		OutRecord = MoveTemp(Rec);
		return true;
	}

	int32 StampStructure(const FGarrisonStructureRecord& Record, FBattleCostGrid& Grid)
	{
		int32 Stamped = Grid.StampImpassable(Record.Footprint.Min, Record.Footprint.Max);
		for (const FGarrisonEntrance& E : Record.Entrances)
		{
			// Corridor box: QueuePos -> DoorPos, ClearWidthM wide.
			const FVector2D Along((E.DoorPos - E.QueuePos).GetSafeNormal());
			const FVector2D Perp(-Along.Y, Along.X);
			const float HalfLen = (E.DoorPos - E.QueuePos).Size() * 0.5f;
			const FVector2D Center(E.QueuePos + (E.DoorPos - E.QueuePos) * 0.5f);
			const FVector2D Ex = Along.GetAbs() * HalfLen + Perp.GetAbs() * (E.ClearWidthM * 0.5f);
			Stamped += Grid.StampCost(Center - Ex, Center + Ex, GarrisonCorridorCost);
		}
		return Stamped;
	}

	bool BuildInternalPath(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		bool bEgress, TArray<FBattlePathPoint3>& OutPath)
	{
		OutPath.Reset();
		if (!Record.Entrances.IsValidIndex(EntranceIndex))
		{
			return false;
		}
		const FGarrisonEntrance& E = Record.Entrances[EntranceIndex];
		OutPath.SetNum(1);
		if (bEgress)
		{
			// node -> Door -> Assembly (spec §4.5); Z reverses down the
			// stair/ramp leg, the ground leg stays 0.
			OutPath[0].Pos = E.DistributionPos;
			OutPath[0].Z = E.DistributionZ;
			AppendLeg(OutPath, E.DistributionPos, E.DistributionZ, E.DoorPos, 0.f);
			AppendLeg(OutPath, E.DoorPos, 0.f, E.AssemblyPos, 0.f);
		}
		else
		{
			// QueuePos -> DoorPos -> distribution node (spec §3.1).
			OutPath[0].Pos = E.QueuePos;
			OutPath[0].Z = 0.f;
			AppendLeg(OutPath, E.QueuePos, 0.f, E.DoorPos, 0.f);
			AppendLeg(OutPath, E.DoorPos, 0.f, E.DistributionPos, E.DistributionZ);
		}
		if (OutPath.Num() >= 2)
		{
			OutPath[0].Tangent = (OutPath[1].Pos - OutPath[0].Pos).GetSafeNormal();
		}
		ReserializeS(OutPath);
		return OutPath.Num() >= 2;
	}

	void AssignSlots(const TArray<FVector2D>& SoldierPositionsM, const TArray<FVector2D>& SlotPositionsM,
		const TArray<int32>& PreviousAssignment, float MaxSlotDisplacementM, TArray<int32>& OutAssignment)
	{
		const int32 NumSoldiers = SoldierPositionsM.Num();
		const int32 NumSlots = SlotPositionsM.Num();
		OutAssignment.Init(INDEX_NONE, NumSoldiers);
		if (NumSlots == 0)
		{
			return;
		}
		TArray<bool> Vacant;
		Vacant.Init(true, NumSlots);

		// Deterministic greedy bijection (the k-file bound's analog): soldiers
		// bid in roster order; ties to the lower slot index; pass 0 skips
		// candidates farther than MaxSlotDisplacementM from the soldier's
		// previous slot, pass 1 lets leftovers take any vacancy.
		for (int32 Bound = 0; Bound < 2; ++Bound)
		{
			for (int32 i = 0; i < NumSoldiers; ++i)
			{
				if (OutAssignment[i] != INDEX_NONE)
				{
					continue;
				}
				float Best = TNumericLimits<float>::Max();
				int32 BestSlot = INDEX_NONE;
				for (int32 j = 0; j < NumSlots; ++j)
				{
					if (!Vacant[j])
					{
						continue;
					}
					if (Bound == 0 && PreviousAssignment.IsValidIndex(i)
						&& PreviousAssignment[i] != INDEX_NONE
						&& PreviousAssignment[i] < NumSlots
						&& FVector2D::Distance(SlotPositionsM[j], SlotPositionsM[PreviousAssignment[i]]) > MaxSlotDisplacementM)
					{
						continue;
					}
					const float D = FVector2D::DistSquared(SoldierPositionsM[i], SlotPositionsM[j]);
					if (D < Best)
					{
						Best = D;
						BestSlot = j;
					}
				}
				if (BestSlot != INDEX_NONE)
				{
					Vacant[BestSlot] = false;
					OutAssignment[i] = BestSlot;
				}
			}
		}
	}

	void QueuePoints(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		int32 MaxQueued, TArray<FVector2D>& OutPointsM)
	{
		OutPointsM.Reset();
		if (!Record.Entrances.IsValidIndex(EntranceIndex) || MaxQueued <= 0)
		{
			return;
		}
		const FGarrisonEntrance& E = Record.Entrances[EntranceIndex];
		// Hold points recede straight away from the door; index 0 = QueuePos.
		const FVector2D Away = (E.QueuePos - E.DoorPos).GetSafeNormal();
		for (int32 i = 0; i < MaxQueued; ++i)
		{
			OutPointsM.Add(E.QueuePos + Away * (QueueSpacingM * i));
		}
	}

	float ZAtS(const TArray<FBattlePathPoint3>& Path, float S)
	{
		if (Path.Num() == 0)
		{
			return 0.f;
		}
		if (S <= Path[0].S)
		{
			return Path[0].Z;
		}
		for (int32 i = 1; i < Path.Num(); ++i)
		{
			if (S <= Path[i].S)
			{
				const float Span = Path[i].S - Path[i - 1].S;
				const float T = Span > KINDA_SMALL_NUMBER ? (S - Path[i - 1].S) / Span : 0.f;
				return FMath::Lerp(Path[i - 1].Z, Path[i].Z, T);
			}
		}
		return Path[Path.Num() - 1].Z;
	}

	float ZAtSFragment(const FBattlePathPolyline& PathCm, float SCm)
	{
		if (PathCm.NumPoints <= 0)
		{
			return 0.f;
		}
		if (SCm <= PathCm.Points[0].S)
		{
			return PathCm.Points[0].Z;
		}
		for (int32 i = 1; i < PathCm.NumPoints; ++i)
		{
			if (SCm <= PathCm.Points[i].S)
			{
				const float Span = PathCm.Points[i].S - PathCm.Points[i - 1].S;
				const float T = Span > KINDA_SMALL_NUMBER ? (SCm - PathCm.Points[i - 1].S) / Span : 0.f;
				return FMath::Lerp(PathCm.Points[i - 1].Z, PathCm.Points[i].Z, T);
			}
		}
		return PathCm.Points[PathCm.NumPoints - 1].Z;
	}

	int32 ColumnFilesFor(float ClearWidthM, float FileSpacingM, int32 NumFilesUpperBound)
	{
		if (FileSpacingM <= KINDA_SMALL_NUMBER)
		{
			return 1;
		}
		return FMath::Clamp(FMath::FloorToInt32(ClearWidthM / FileSpacingM), 1, FMath::Max(1, NumFilesUpperBound));
	}
}
