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

	// Template-level shared fragment of the per-unit type: the ADR-14 anchor is
	// necessarily a NULL handle here -- a template cannot know a future unit
	// handle, so all config-spawned entities of one config share this one
	// null-anchored instance. Per ADR-17: Route B is config-integrity
	// validation only (T0.3); these entities must NEVER be used as battle
	// units (real units spawn via UBattleSpawnLibrary / Route A, which creates
	// a properly anchored per-unit instance).
	FUnitStatsSharedFragment DefaultStats;
	DefaultStats.OwnerUnit = FUnitHandle();
	BuildContext.AddSharedFragment(FSharedStruct::Make(DefaultStats));
}
