# PR-11: Dictionary-keyed level authoring + the demo-map TeamId eyeball (D-2, A-1 remainder)

| | |
|---|---|
| **Finds** | Audit phase-3 A-1 (code half landed: `ADemoUnitActor::OnConstruction` now syncs `TeamId` from `Team`; the visuals' adopt path reads `Spec->TeamId`) + D-2: level-authored units still reference EDU rows by INDEX (`ADemoUnitActor` inherits `AUnitActor::EduRowIndex`, an int property), and the demo map's placed units are a binary .umap nobody can audit statically |
| **Blocks on** | PIE on the reference machine (the map must be re-saved by the editor); the actor property change wants an eyeball pass |
| **Owner** | The property shape (dictionary string vs int+validation); the map re-authoring is mechanical either way |

## Motivation

The phase-3 audit patched every RUNTIME fixture to dictionary-keyed EDU lookups (B-3), but the
LEVEL authoring surface still binds units to rows by index: an author types `EduRowIndex = 2`
into the details panel. Re-order the golden EDU (T6.1 will) and every placed unit on every map
silently becomes a different troop type. The same authoring surface also had A-1's team trap
(visual `Team` vs sim `TeamId`); the code-side sync landed with the audit, but the five units
already placed on `L_MassBattle_Demo01` were authored BEFORE the sync -- their `TeamId` values
must be verified in PIE, and a .umap cannot be inspected from a static patch.

## Proposed change

1. `ADemoUnitActor` (or `AUnitActor`): add an editor-visible `UnitDictionary` (FName) property.
   When set, `SpawnEntities` resolves it through `BattleExec::FindEduRowIndexByDictionary`
   (public since the audit's patch 20) and stamps `Spec.EduRowIndex`; the int property stays as
   the fallback for CI/headless maps. Validate loudly on an unknown name (the B-3 posture:
   fail, never silently re-type).
2. Re-author `L_MassBattle_Demo01`'s five placed units: fill `UnitDictionary`
   (e.g. `Peasant_Levy` / `Sergeant_Spearmen`), verify each unit's `Team` matches its intended
   side, and let the audit's OnConstruction sync stamp `TeamId` from it.
3. PIE eyeball: spawn the demo, order the player units to attack; confirm (a) the enemy-tinted
   units are the ones the ranged rules treat as enemies (watch `RangedSkip Reason=Blockers`
   flip sides), (b) `Battle.Ranged.Dump` fractions render on both sides.

## Files

- Edit: `Source/MassBattleGame/DemoUnitActor.h/.cpp` (the dictionary property + resolution)
- Edit: `Plugins/MassBattle/Source/MassBattle/Private/UnitActor.cpp` (SpawnEntities consumes it)
- Edit: `Content/MassBattle/Demo/Maps/L_MassBattle_Demo01.umap` (re-save; binary)

## Evidence to gather on the UE machine

- PIE: the M4 sample + the level units firing/holding per their factions; a screenshot of a
  two-side volley (the PR-8 residual eyeball doubles as this pass).
- Unit tier green (the property is editor-visible only; the automation maps place no units).
- One `SoakVolley=1` nightly (the adopt path's TeamId read is exercised by exec-fixture units).

## Rollback

The dictionary property is additive with the int fallback; deleting it restores today's
authoring. The map re-save is the only irreversible step (git-revertable like any .umap).

## Status

PENDING (filed by the phase-3 audit, 2026-10-30). The A-1 code half (OnConstruction sync +
adopt-by-TeamId) already landed with the audit's patch 20.
