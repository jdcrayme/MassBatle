// BattleUnitTrait.h -- the entity-trait that stamps the Phase 0 unit
// composition onto a UMassEntityConfigAsset's template (Task 6, A5 route:
// configs are authored in C++ by Battle.BuildT0Configs through this trait's
// BuildTemplate; recorded in the ADR notes as the trait/BuildTemplate route --
// BuildContext.RequireFragment does not exist in 5.8, audit C6).
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "BattleUnitTrait.generated.h"

UCLASS(ClassGroup = "MassBattle", meta = (DisplayName = "Battle Unit Composition"))
class UBattleUnitTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** EDU row this config represents (index into the fixture table). */
	UPROPERTY(EditAnywhere, Category = "Battle") int32 EduRowIndex = INDEX_NONE;

	/** Immutable base stats stamped onto FBaseStatsFragment at build time. */
	UPROPERTY(EditAnywhere, Category = "Battle") float BaseHealth = 1.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float MeleeAttack = 0.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float MeleeDefense = 0.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float RangedAttack = 0.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float Armor = 0.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float MoraleBase = 0.f;
	UPROPERTY(EditAnywhere, Category = "Battle") float ChargeDistance = 0.f;

	/** Capability tags to apply (arch §3.2). */
	UPROPERTY(EditAnywhere, Category = "Battle") bool bInfantry = true;
	UPROPERTY(EditAnywhere, Category = "Battle") bool bCavalry = false;
	UPROPERTY(EditAnywhere, Category = "Battle") bool bMelee = true;
	UPROPERTY(EditAnywhere, Category = "Battle") bool bBow = false;

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
