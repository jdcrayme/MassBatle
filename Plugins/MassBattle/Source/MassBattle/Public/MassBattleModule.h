#pragma once
#include "Modules/ModuleManager.h"

// Project-wide log category (EXEC_RESULT evidence lines, EXEC commands, dumps).
MASSBATTLE_API DECLARE_LOG_CATEGORY_EXTERN(LogMassBattle, Log, All);

class FMassBattleModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
