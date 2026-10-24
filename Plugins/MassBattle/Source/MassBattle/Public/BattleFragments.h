// BattleFragments.h -- Phase 0 fragment/tag set (arch §3.1).
// Rules honored here: hot fragments are blittable-friendly (no FString, no
// UObject*, no TArray); shared fragments carry the ADR-14 uniqueness anchor as
// their FIRST field; FDeadTag is a 5.8 sparse tag per ADR-15.
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h" // MassCore: FMassFragment / FMassSharedFragment / FMassTag / FMassSparseTag
#include "BattleTypes.h"
#include "BattleFragments.generated.h"

/** Entity -> unit link (arch §2.4). Written once at spawn fixup. */
USTRUCT()
struct FOwnerUnitFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() FUnitHandle Owner;
	UPROPERTY() int32 RosterIndex = INDEX_NONE;
};

/** Table-row provenance + representation IDs. Mesh/weapon IDs are indices, not
 *  object references (arch §3.1). */
USTRUCT()
struct FLoadoutFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() int32 EduRowIndex = INDEX_NONE;
	UPROPERTY() int32 MeshId = INDEX_NONE;
	UPROPERTY() int32 WeaponId = INDEX_NONE;
};

/** Immutable EDU-mapped base stats (arch §3.1: "spawn, immutable"). Multipliers
 *  derived from these live in FUnitStatsSharedFragment, computed by UnitSync. */
USTRUCT()
struct FBaseStatsFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() float BaseHealth = 1.f;
	UPROPERTY() float MeleeAttack = 0.f;
	UPROPERTY() float MeleeDefense = 0.f;
	UPROPERTY() float RangedAttack = 0.f;
	UPROPERTY() float Armor = 0.f;
	UPROPERTY() float MoraleBase = 0.f;
	UPROPERTY() float ChargeDistance = 0.f;
};

/** Per-entity HP pool + pending damage (written by combat resolution, Phase 2+;
 *  Phase 0 only initializes CurrentHP at spawn). */
USTRUCT()
struct FHealthFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() float CurrentHP = 1.f;
	UPROPERTY() float QueuedDamage = 0.f;
};

/**
 * Per-unit broadcast channel (arch §3.3, §10.3). One instance per unit.
 *
 * ADR-14 (review E1): Mass dedups shared fragments by content hash, so the
 * FUnitHandle anchor MUST be the first field and creation MUST go through a
 * handle-seeded hash (HashCombine(Index, Serial)) -- otherwise two units with
 * identical stats silently share one instance and cross-contaminate on the
 * UnitSync in-place write. T0.8 asserts N identical-stat units yield N distinct
 * instances.
 */
USTRUCT()
struct FUnitStatsSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	// ADR-14 uniqueness anchor -- keep first.
	UPROPERTY() FUnitHandle OwnerUnit;

	UPROPERTY() float SpeedMult = 1.f;
	UPROPERTY() float MeleeAttackMult = 1.f;
	UPROPERTY() float MeleeDefenseMult = 1.f;
	UPROPERTY() float RangedAttackMult = 1.f;
	UPROPERTY() float BlockMult = 1.f;
	UPROPERTY() float FatigueAttackMult = 1.f;
	UPROPERTY() float FatigueDefenseMult = 1.f;
	UPROPERTY() float ArmorAdd = 0.f;
	UPROPERTY() float AccuracyMult = 1.f;
	UPROPERTY() float MoraleBonus = 0.f;
};

// ---- Capability tags (zero-memory archetype filters, arch §3.2) ----

USTRUCT() struct FInfantryTag : public FMassTag { GENERATED_BODY() };
USTRUCT() struct FCavalryTag : public FMassTag { GENERATED_BODY() };
USTRUCT() struct FMeleeTag : public FMassTag { GENERATED_BODY() };
USTRUCT() struct FBowTag : public FMassTag { GENERATED_BODY() };
USTRUCT() struct FOfficerTag : public FMassTag { GENERATED_BODY() };

/**
 * Death marker. ADR-15 final state (Phase 0 Task 9 on-machine probe --
 * reality wins): the sparse-tag adoption was REVERTED. 5.8.4 sparse tags are
 * stored per archetype CHUNK (MassSparseElementsStorage.h: "Sparse tags live
 * there since they don't contain any data per instance" -- a per-chunk bitset),
 * so AddTag on one entity marks every same-chunk entity as dead; and the
 * observer registry rejects sparse elements entirely (audit C10/C11). Per-entity
 * death marking therefore requires the regular tag: killing an entity DOES
 * archetype-flip (the roadmap's original wording), amortized fine at Phase 0
 * scale; revisit if 5.8.x patches per-instance sparse tags.
 */
USTRUCT()
struct FDeadTag : public FMassTag
{
	GENERATED_BODY()
};
