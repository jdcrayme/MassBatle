#if WITH_DEV_AUTOMATION_TESTS
// Battle.Unit.Garrison.* -- phase-garrison gate family TG.1-TG.5 (brief §6).
// Task 0 ships these as fail-by-design skeletons (gate-first discipline):
// each gate's real body lands in its owning task's commit (§5 task plan).
#include "Misc/AutomationTest.h"

namespace
{
	/** Shared skeleton body: fails with the gate id until the owning task
	 *  replaces it (the count of these failures IS the task-0 evidence). */
	bool GarrisonSkeleton(FAutomationTestBase& Test, const TCHAR* Gate)
	{
		Test.AddError(FString::Printf(TEXT("skeleton: %s lands with its owning task (brief §5)"), Gate));
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_Stamping,
	"Battle.Unit.Garrison.Stamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_Stamping::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.1 Battle.Unit.Garrison.Stamping (task 2)"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_InternalPath,
	"Battle.Unit.Garrison.InternalPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_InternalPath::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.2 Battle.Unit.Garrison.InternalPath (task 3)"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_SlotAssign,
	"Battle.Unit.Garrison.SlotAssign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_SlotAssign::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.3 Battle.Unit.Garrison.SlotAssign (task 3)"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_Booking,
	"Battle.Unit.Garrison.Booking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_Booking::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.4 Battle.Unit.Garrison.Booking (task 4)"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarrisonSkeleton_ZModel,
	"Battle.Unit.Garrison.ZModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarrisonSkeleton_ZModel::RunTest(const FString& Parameters)
{
	return GarrisonSkeleton(*this, TEXT("TG.5 Battle.Unit.Garrison.ZModel (task 3)"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
