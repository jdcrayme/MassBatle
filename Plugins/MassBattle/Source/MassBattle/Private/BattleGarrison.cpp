// BattleGarrison.cpp -- CVAR block + seam-math bodies. Math lands with its
// owning task (gate-first): task 0 ships the CVARs + failure stubs; task 2
// fills MakeFixture/StampStructure; task 3 fills the rest.
#include "BattleGarrison.h"
#include "BattleMovementFragments.h"
#include "BattleCostGrid.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleGarrisonEnable(
		TEXT("Battle.Garrison.Enable"),
		0,
		TEXT("Enable garrison orders + the locomotion garrison FSM. 0 (default until task 5) leaves the T1.8 baseline untouched (B.8 item 6)."),
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
		(void)Type; (void)PosMeters; (void)YawDeg; (void)OutRecord;
		return false; // task 2
	}

	int32 StampStructure(const FGarrisonStructureRecord& Record, FBattleCostGrid& Grid)
	{
		(void)Record; (void)Grid;
		return 0; // task 2
	}

	bool BuildInternalPath(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		bool bEgress, TArray<FBattlePathPoint3>& OutPath)
	{
		(void)Record; (void)EntranceIndex; (void)bEgress; (void)OutPath;
		return false; // task 3
	}

	void AssignSlots(const TArray<FVector2D>& SoldierPositionsM, const TArray<FVector2D>& SlotPositionsM,
		const TArray<int32>& PreviousAssignment, float MaxSlotDisplacementM, TArray<int32>& OutAssignment)
	{
		(void)SoldierPositionsM; (void)SlotPositionsM; (void)PreviousAssignment; (void)MaxSlotDisplacementM;
		OutAssignment.Reset(); // task 3
	}

	void QueuePoints(const FGarrisonStructureRecord& Record, int32 EntranceIndex,
		int32 MaxQueued, TArray<FVector2D>& OutPointsM)
	{
		(void)Record; (void)EntranceIndex; (void)MaxQueued; (void)OutPointsM;
		OutPointsM.Reset(); // task 3
	}

	float ZAtS(const TArray<FBattlePathPoint3>& Path, float S)
	{
		(void)Path; (void)S;
		return 0.f; // task 3
	}

	float ZAtSFragment(const FBattlePathPolyline& PathCm, float SCm)
	{
		(void)PathCm; (void)SCm;
		return 0.f; // task 3
	}

	int32 ColumnFilesFor(float ClearWidthM, float FileSpacingM, int32 NumFilesUpperBound)
	{
		(void)ClearWidthM; (void)FileSpacingM; (void)NumFilesUpperBound;
		return 0; // task 3
	}
}
