#include "IntegrateProcessor.h"
#include "BattleFragments.h"
#include "BattleMovementFragments.h"
#include "BattleGarrison.h"
#include "BattleServo.h"
#include "BattleUnitLocomotion.h"
#include "MassBattleModule.h"

#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> GBattleMovementIntegrate(
		TEXT("Battle.Movement.Integrate"),
		1,
		TEXT("Enable Integrate processing (arch §4 row 8). 0 opts the stage out."),
		ECVF_Default);

	// Commanded facing-change rate (rad/s) -- shared with the §8.2 locomotion
	// cap; read through the manager here (the definition lives with the other
	// Battle.Movement CVARs; engine-api note: TAutoConsoleVariable in one TU,
	// FindConsoleVariable in readers, the ServoProcessor pattern).
	float GetFacingDeltaRate()
	{
		if (const IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.FacingDeltaRate")))
		{
			return Cvar->GetFloat();
		}
		return 0.35f;
	}

	// Phase 2 C5 (F-8): the R-G5 rest-facing generalizes to field entities
	// under the fall-in master switch (defined beside the blend in
	// FormationSlotResolveProcessor.cpp) -- within SlotReachEps and slow, an
	// entity rotates to its slot yaw (the TW settle look) instead of keeping
	// the walk heading. The garrison Z servo stays gated on garrison mode.
	int32 IntegrateFallInEnabled()
	{
		if (const IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.ProgressiveFallIn")))
		{
			return Cvar->GetInt();
		}
		return 0;
	}

	// Settle-facing rate (rad/s) for the rest-facing rotation. ERRATA vs plan
	// C5 ("at the facing rate"): the shared FacingDeltaRate (0.35 rad/s) is
	// the BLOCK refront cap -- at 20 deg/s a man turning 180 deg needs 9 s and
	// the settle window times out (S7 measured: offYaw 27/40 at 5 s). An
	// individual pivot is ~1 s in M2TW; 2 rad/s is the measured-fit default.
	float GetSettleYawRate()
	{
		if (const IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(TEXT("Battle.Movement.SettleYawRate")))
		{
			return Cvar->GetFloat();
		}
		return 2.f;
	}
}

UIntegrateProcessor::UIntegrateProcessor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, IntegrateQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	SetProcessingPhase(EMassProcessingPhase::PrePhysics);
	ExecutionOrder.ExecuteInGroup = TEXT("Battle");
	ExecutionOrder.ExecuteAfter.Add(TEXT("ServoProcessor"));
	ExecutionFlags = static_cast<uint8>(EProcessorExecutionFlags::AllWorldModes);
	// Worker-safe: fragment-only accesses (arch §4 row 8).
	bRequiresGameThreadExecution = false;
}

void UIntegrateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	IntegrateQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	// Garrison Z servo + rest-facing (spec §6/§4.4): per-entity slot targets
	// carry TargetZ/TargetYaw; the shared fragment carries the garrison mode.
	// Both travel on every battle entity, so the query shape is unchanged in
	// practice (F11: no archetype impact -- fragment PRESENCE, not new types).
	IntegrateQuery.AddRequirement<FSlotTargetFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddSharedRequirement<FUnitFormationSharedFragment>(EMassFragmentAccess::ReadOnly);
	// Audit M12: battle entities only. The old shape (velocity + transform)
	// z-pinned and yaw-snapped ANY Mass entity in the world. Audit M8: no
	// death-window corpses either.
	IntegrateQuery.AddRequirement<FOwnerUnitFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddTagRequirement<FDeadTag>(EMassFragmentPresence::None);
}

void UIntegrateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bTracedOnce = false;
	if (!bTracedOnce)
	{
		bTracedOnce = true;
		UE_LOG(LogMassBattle, Display, TEXT("EXEC_RESULT MovementSpine Integrate first-execute"));
	}

	if (GBattleMovementIntegrate.GetValueOnAnyThread() == 0)
	{
		return;
	}

	// Transform integration modeled on UMassSimpleMovementProcessor
	// (engine-api §14): position += velocity * DT. Planar sim (X/Y); Z is a
	// carried value: garrisoned entities run the A8 bounded Z servo toward
	// their slot/path elevation, everyone else pins to the heightfield (flat
	// 0 in Phase 1). Entity yaw follows the velocity heading while moving;
	// garrisoned entities AT their post rotate to the post facing (R-G5).
	const float DT = Context.GetDeltaTimeSeconds();
	const float FacingDeltaRate = GetFacingDeltaRate();
	const float SettleYawRate = GetSettleYawRate();
	const bool bRestFacingAll = IntegrateFallInEnabled() != 0;
	IntegrateQuery.ForEachEntityChunk(Context, [DT, FacingDeltaRate, SettleYawRate, bRestFacingAll](FMassExecutionContext& Context)
	{
		const FUnitFormationSharedFragment& Formation = *Context.GetSharedFragmentPtr<FUnitFormationSharedFragment>();
		const bool bGarrisoned = Formation.GarrisonMode != EGarrisonMode::None;
		// Rest-facing runs for garrisoned entities (R-G5, always) and -- under
		// the phase 2 C master switch -- for field entities too (F-8).
		const bool bRestFacing = bGarrisoned || bRestFacingAll;
		auto Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Context.GetFragmentView<FMassVelocityFragment>();
		const auto Targets = Context.GetFragmentView<FSlotTargetFragment>();

		const float MaxZRateCm = FGarrisonCVars::MaxZRate() * 100.f; // m/s -> cm/s
		const float SlotReachCm = FUnitServoConstants::SlotReachEps * 100.f;

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			FTransform& Transform = Transforms[i].GetMutableTransform();
			const FVector Vel = Velocities[i].Value;
			FVector Translation = Transform.GetTranslation() + Vel * DT;

			float NewZ = 0.f;
			if (bGarrisoned)
			{
				// A8 bounded-rate Z servo toward the (per-entity) target Z.
				const float CurZ = Transform.GetTranslation().Z;
				const float Delta = Targets[i].TargetZ - CurZ;
				NewZ = CurZ + FMath::Clamp(Delta, -MaxZRateCm * DT, MaxZRateCm * DT);
			}
			Transform.SetTranslation(FVector(Translation.X, Translation.Y, NewZ));

			if (FMath::Abs(Vel.X) + FMath::Abs(Vel.Y) > KINDA_SMALL_NUMBER)
			{
				const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Vel.Y, Vel.X));
				Transform.SetRotation(FQuat(FRotator(0.f, YawDeg, 0.f)));
			}
			else if (bRestFacing)
			{
				// R-G5 rest-facing (generalized, phase 2 C5): at the slot
				// (within SlotReachEps), rotate to the slot yaw at the
				// commanded facing rate (deterministic).
				const FVector P = Transform.GetTranslation();
				if (FVector2D::DistSquared(FVector2D(P.X, P.Y), Targets[i].TargetPos) <= FMath::Square(SlotReachCm))
				{
					const float CurYaw = Transform.Rotator().Yaw;
					const float NewYaw = FUnitLocomotion::AdvanceFacing(CurYaw, Targets[i].TargetYaw, SettleYawRate, DT);
					if (!FMath::IsNearlyEqual(CurYaw, NewYaw, 1e-4f))
					{
						Transform.SetRotation(FQuat(FRotator(0.f, NewYaw, 0.f)));
					}
				}
			}
		}
	});
}
