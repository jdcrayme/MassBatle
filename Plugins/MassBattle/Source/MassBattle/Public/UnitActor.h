// UnitActor.h -- the GAS side of the translation layer (arch §2.1, §5).
// One actor per unit; owns attributes, effects, abilities, orders. The actor
// owns an FUnitSpawnSpec and performs the batched entity spawn on demand
// (game-thread, outside Mass processing -- §4.5 guard).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"
#include "BattleTypes.h"
#include "BattleSpawnLibrary.h"
#include "UnitActor.generated.h"

class UUnitRegistrySubsystem;
struct FOnAttributeChangeData;

/** Closed GAS attribute set (arch §10.1). Buff/debuff effects target the
 *  *Mod attributes only (ADR-09). 5.8 note (audit C9): the convenience
 *  ATTRIBUTE_ACCESSORS macro no longer ships -- use ATTRIBUTE_ACCESSORS_BASIC. */
UCLASS()
class MASSBATTLE_API UUnitAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	// -- meta (fed by the contribution stack, Phase 4) --
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData Morale;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData Fatigue;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData Experience;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData ArmourUpgrade;
	// -- the only effect targets (ADR-09) --
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData MeleeAttackMod;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData MeleeDefenseMod;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData RangedAttackMod;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData SpeedMod;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData BlockChanceMod;
	// -- command/aggregates --
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData OrderLatency;
	UPROPERTY(VisibleAnywhere, Category = "Battle") FGameplayAttributeData AmmoPool;

	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, Morale)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, Fatigue)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, Experience)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, ArmourUpgrade)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, MeleeAttackMod)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, MeleeDefenseMod)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, RangedAttackMod)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, SpeedMod)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, BlockChanceMod)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, OrderLatency)
	ATTRIBUTE_ACCESSORS_BASIC(UUnitAttributeSet, AmmoPool)

	/** The closed set as attribute handles (T0.4 completeness assertions). */
	static const TArray<FGameplayAttribute>& GetAllAttributes();

	/** Look up a closed-set attribute by property name ("MeleeAttackMod"). Invalid if unknown. */
	static FGameplayAttribute FindAttributeByName(const FString& PropertyName);
};

/**
 * Unit actor: authority + interface (arch §5.1 down-path registration).
 * PostInitializeComponents wires InitAbilityActorInfo. The attribute-change
 * delegates (snapshot write + registry dirty flag, one frame of latency
 * accepted; UnitSync consumes the flags) are bound in SpawnEntities, where the
 * registry handle the handlers report on becomes meaningful -- on-machine,
 * bindings made in PostInitializeComponents did not survive to change time.
 */
UCLASS()
class MASSBATTLE_API AUnitActor : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AUnitActor();

	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }

	/** Spawn the entity roster (Task 6 spawn path) from SpawnSpec + row stats. */
	bool SpawnEntities(FBattleSpawnResult& OutResult, FString& OutError);

	/** Registry handle once spawned; invalid before. */
	FUnitHandle GetUnitHandle() const { return UnitHandle; }

	/** Count of "Battle.Unit.Died" gameplay events observed on this unit's ASC. */
	int32 GetDeathEventCount() const { return DeathEventCount; }

	/** EDU row this unit instantiates (index into the fixture table). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle") int32 EduRowIndex = INDEX_NONE;

	/** Roster size override; 0 = use the row's own soldier count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle") int32 SoldierCount = 0;

	/** Ranks in the formed layout (files = ceil(SoldierCount / NumRanks));
	 *  0 = the spec default. Level-authored formation control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Formation", meta = (ClampMin = "0", ClampMax = "16"))
	int32 NumRanks = 0;

	/** File/rank spacing in meters; 0 = the spec default (1.2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle|Formation", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float FormationSpacing = 0.f;

	/** Side id (Phase 3, A7/A8): the ranged friend/enemy discriminator; 0
	 *  default. Melee stays intent/record-driven and never reads it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle", meta = (ClampMin = "0", ClampMax = "7"))
	uint8 TeamId = 0;

	UPROPERTY(VisibleAnywhere, Category = "Battle") TObjectPtr<UAbilitySystemComponent> AbilitySystem;
	UPROPERTY(VisibleAnywhere, Category = "Battle") TObjectPtr<UUnitAttributeSet> AttributeSet;

protected:
	void OnAttributeChanged(const FOnAttributeChangeData& Data);

	FUnitSpawnSpec Spec;
	FUnitHandle UnitHandle;

	/** EDU-row -> typed stats cache (parsed rows are immutable). */
	TMap<int32, FBattleRowStats> RowStatsCache;

	/** Attribute-change delegates bound exactly once (see SpawnEntities). */
	bool bDelegatesBound = false;
	bool bDeathEventBound = false;
	int32 DeathEventCount = 0;
	FDelegateHandle DeathEventHandle;

	void OnDeathEvent(FGameplayTag EventTag, const FGameplayEventData* Payload);
};
