// BattleCombatFragments.h -- Phase 2 combat data spine (spec
// engagement-and-melee §3.1, brief task 0). Blittable rules per arch §3.1:
// handles, fixed scalars, enums -- no FString / UObject* / TArray. ZERO new
// tags this phase (F6): pair/charge state are fragment fields, so the
// archetype count target stays 6 -- composition grows, count does not.
// Unit conventions: seam math is meters, fragments carry centimeters/degrees;
// the assign pass quantizes once at the boundary.
#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"
#include "Mass/EntityHandle.h"
#include "BattleTypes.h"
#include "BattleCombatFragments.generated.h"

/** Pair-state machine (spec §4.4-§4.6). Movement mapping per brief A12:
 *  Seeking -> CrossField toward the press point; Pressing -> PressRing;
 *  Striking/Recovering -> hold at contact (HoldSlot weights); Charging is
 *  carried by FChargeFragment + the Charge movement state. */
UENUM(BlueprintType)
enum class ECombatPairState : uint8
{
	Seeking,     /**< no target yet: advance toward the engagement */
	Pressing,    /**< unslotted waiter: hold at the contact ring (ContactAngle) */
	Striking,    /**< attack timer elapsed: the resolution pass rolls this tick */
	Recovering   /**< post-attack cooldown window */
};

/** Order surface (brief A4) + unit-FSM intent (arch §6.2): WHICH enemy UNIT
 *  this entity's owner is engaging. Written game-thread at order issue;
 *  the assign pass (§4.4) turns unit intent into entity pairs -- the fragment
 *  never names the paired ENTITY (that is FCombatPairFragment.Target). */
USTRUCT()
struct FCombatIntentFragment : public FMassFragment
{
	GENERATED_BODY()

	/** AttackUnit target; the auto-engage target for AttackMove. */
	UPROPERTY() FUnitHandle TargetUnit;
	/** Attack lane: one lane per attacking file (spec §4.2); laned targeting
	 *  scans the defender slot table within this lane's slice only. */
	UPROPERTY() uint8 LaneIndex = 0;
	/** 1 = AttackMove (auto-engage a valid enemy unit); 0 = AttackUnit only. */
	UPROPERTY() uint8 bAutoEngage = 0;
};

/** Per-entity pair state (spec §3.1). The unit-level picture is the
 *  registry-owned FEngagementRecord; per attacker-defender adjacency is
 *  per-entity (the ADR-06 data-model consequence: budgets are exposure-driven
 *  geometry, so the data that varies per adjacency lives on the entities).
 *  ContactAngle is ONE value with TWO consumers (arch §7.3): contact-ring
 *  steering and directional defense lookup -- quantized ONCE at assign time,
 *  the defense arc bands compare against it, never re-derive. */
USTRUCT()
struct FCombatPairFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Paired defender entity; invalid = unpaired (Seeking / Pressing). */
	UPROPERTY() FMassEntityHandle Target;
	/** Quantized approach sector around the defender, 0-255 -> full circle
	 *  (spec §4.5); SectorCount buckets are derived, never stored. */
	UPROPERTY() uint8 ContactAngle = 0;
	UPROPERTY() ECombatPairState State = ECombatPairState::Seeking;
	/** Seconds to the next strike. Staggered at assign (handle-hashed phase
	 *  offset, §4.4); advanced worker-side by the resolution pass. */
	UPROPERTY() float AttackTimer = 0.f;
	/** Seconds since the pair formed (distance-breakup hysteresis gate, §4.7). */
	UPROPERTY() float PairAge = 0.f;
	/** Charge bonus remaining (§4.9): seeded by the charge impact, decays over
	 *  ChargeDecayWindow, applies to attack rolls while positive. */
	UPROPERTY() float ChargeBonusRemaining = 0.f;
	/** Press-ring servo point, CENTIMETERS (A12): the assign pass computes it
	 *  each tick from the target transform + ContactAngle + ring radius; the
	 *  servo uses it as the combat target while the entity is paired or
	 *  pressing. Derived from hashed terms (target pos + angle) -- never
	 *  hashed itself (spec §3.4's term list). */
	UPROPERTY() FVector2D RingPosCm = FVector2D::ZeroVector;
	/** 1 = RingPosCm carries this tick's press point (combat override active). */
	UPROPERTY() uint8 bRingValid = 0;
	/** Failed-seek backoff, ticks (1-3, jittered; spec §4.4). Transient pacing
	 *  -- not a hashed term (spec §3.4's list). */
	UPROPERTY() uint8 BackoffTicks = 0;
	/** Defender stat mirror (spec §4.8 directional defense): stamped by the
	 *  assign pass at PAIR CREATION from the defender's immutable stats -- the
	 *  worker-side resolution pass reads these instead of crossing chunks
	 *  (ADR-05: attacker-authoritative, defender holds no combat authority). */
	UPROPERTY() float DefenderDefense = 0.f;
	UPROPERTY() float DefenderShield = 0.f;
	UPROPERTY() float DefenderArmor = 0.f;
	/** Remaining mount pool of the paired defender (hits before the rider
	 *  pool; humans have none -- spec §4.8 composite model). */
	UPROPERTY() int16 DefenderMountRemaining = 0;
};

/** Charge state machine per-entity fields (spec §4.9, worker-written by the
 *  ChargeProcessor). Charging entities bypass pairing until impact seeds
 *  their first pair with ChargeBonusRemaining. */
USTRUCT()
struct FChargeFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Distance covered while charging (meters, seam convention). */
	UPROPERTY() float DistanceTraveled = 0.f;
	/** Speed at impact (m/s); the fresh-contact gate reads this against
	 *  ChargeSpeedThreshold. */
	UPROPERTY() float ImpactSpeed = 0.f;
	/** 1 after impact seeded the first pair (the bypass latch). */
	UPROPERTY() uint8 bConnected = 0;
	/** Ticks spent charging without connecting (fixed DT; the run-up EXPIRES
	 *  after the decay window so a never-connecting fast mover cannot hold
	 *  the unit's charge-through displacement open indefinitely -- measured
	 *  2026-10-27: one stuck charger shoved the enemy formation 25 m). */
	UPROPERTY() uint16 ChargeTicks = 0;
	/** 1 once the connect impulse reached the victim's transform (one-shot;
	 *  the assign pass applies it game-thread before the worker phase). */
	UPROPERTY() uint8 bKnockbackApplied = 0;
};
