#include "BattleSmokeController.h"

#include "GauntletModule.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

UBattleSmokeController::~UBattleSmokeController()
{
	DetachCollector();
}

void UBattleSmokeController::OnInit()
{
	FParse::Value(FCommandLine::Get(), TEXT("battlesmoke.wait="), WaitSeconds);

	GLog->AddOutputDevice(&ErrorCollector);
	UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleSmoke OnInit wait=%.1fs (collector attached)"), WaitSeconds);
	MarkHeartbeatActive(TEXT("BattleSmoke initialized"));
}

void UBattleSmokeController::OnTick(float TimeDelta)
{
	if (bEnded)
	{
		return;
	}

	if (ErrorCollector.ErrorCount > 0)
	{
		UE_LOG(LogGauntlet, Error, TEXT("GAUNTLET BattleSmoke FAIL -- %d error-level log line(s) after %.1fs"),
		       ErrorCollector.ErrorCount, GetTimeInCurrentState());
		FinishTest(-1);
		return;
	}

	if (GetTimeInCurrentState() >= WaitSeconds)
	{
		UE_LOG(LogGauntlet, Display, TEXT("GAUNTLET BattleSmoke PASS -- clean %.1fs boot, 0 errors"), WaitSeconds);
		FinishTest(0);
	}
}

void UBattleSmokeController::DetachCollector()
{
	GLog->RemoveOutputDevice(&ErrorCollector);
}

void UBattleSmokeController::FinishTest(int32 ExitCode)
{
	if (bEnded)
	{
		return;
	}
	bEnded = true;
	MarkHeartbeatActive(ExitCode == 0 ? TEXT("BattleSmoke passed") : TEXT("BattleSmoke failed"));
	DetachCollector();
	EndTest(ExitCode);
}
