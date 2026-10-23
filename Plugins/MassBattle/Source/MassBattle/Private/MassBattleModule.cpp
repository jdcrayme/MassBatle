#include "MassBattleModule.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogMassBattle);

void FMassBattleModule::StartupModule()
{
	UE_LOG(LogMassBattle, Log, TEXT("MassBattle module started"));
}

void FMassBattleModule::ShutdownModule()
{
	UE_LOG(LogMassBattle, Log, TEXT("MassBattle module shut down"));
}

IMPLEMENT_MODULE(FMassBattleModule, MassBattle)
