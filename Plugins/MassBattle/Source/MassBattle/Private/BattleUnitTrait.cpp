#include "BattleUnitTrait.h"
#include "BattleFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "StructUtils/SharedStruct.h"

void UBattleUnitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Same composition as the Route-A C++ archetype (UBattleSpawnLibrary) so
	// config-spawned and library-spawned entities are interchangeable (T0.3).
	BuildContext.AddFragment<FOwnerUnitFragment>();
	BuildContext.AddFragment<FLoadoutFragment>();

	FBaseStatsFragment& Base = BuildContext.AddFragment_GetRef<FBaseStatsFragment>();
	Base.BaseHealth = BaseHealth;
	Base.MeleeAttack = MeleeAttack;
	Base.MeleeDefense = MeleeDefense;
	Base.RangedAttack = RangedAttack;
	Base.Armor = Armor;
	Base.MoraleBase = MoraleBase;
	Base.ChargeDistance = ChargeDistance;

	BuildContext.AddFragment<FHealthFragment>();

	if (bInfantry) { BuildContext.AddTag<FInfantryTag>(); }
	if (bCavalry)  { BuildContext.AddTag<FCavalryTag>(); }
	if (bMelee)    { BuildContext.AddTag<FMeleeTag>(); }
	if (bBow)      { BuildContext.AddTag<FBowTag>(); }

	// Template-level shared fragment of the per-unit type: ADR-14 anchor is a
	// null handle here (template default); the spawn path (registry/spawn
	// library) replaces it with the real per-unit instance.
	FUnitStatsSharedFragment DefaultStats;
	DefaultStats.OwnerUnit = FUnitHandle();
	BuildContext.AddSharedFragment(FSharedStruct::Make(DefaultStats));
}
