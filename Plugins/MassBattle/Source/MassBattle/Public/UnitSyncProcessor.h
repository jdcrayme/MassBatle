// UnitSyncProcessor.h -- pipeline row 0 (arch §4): registry dirty flags ->
// per-unit shared stat fragments. Game thread, PrePhysics, custom "Battle"
// group. 5.8 signatures per api-audit-5.8.md §2 (ctor-registered query,
// ConfigureQueries(const TSharedRef<FMassEntityManager>&), AllWorldModes).
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "UnitSyncProcessor.generated.h"

UCLASS()
class MASSBATTLE_API UUnitSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UUnitSyncProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery SyncQuery;
};
