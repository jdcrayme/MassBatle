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
adopt-by-TeamId) already landed with the audit's patch 20 (confirmed on `main` as `0653993`
at the 2026-10-30 series landing). The dictionary property, the map re-authoring, and the PIE
eyeball remain -- all need an editor session (no Unreal MCP was available at landing time).

**EXECUTED, 2026-10-30** (editor session over MCP): `ff542af` lands the code half -- an
editor-visible `UnitDictionary` (FName) on `AUnitActor` (the plugin base; the demo actor
inherits it), resolved in `SpawnEntities` through `BattleExec::FindEduRowIndexByDictionary`,
WINNING over the int when set, failing LOUDLY on an unknown name (B-3 posture); the int
stays as the CI/headless fallback. `L_MassBattle_Demo01`'s five placed units re-authored
(2x `Peasant_Levy` players, 3x `Sergeant_Spearmen` enemies) and re-saved (is_dirty=false).

Evidence (same session): unit tier 80/80 in-editor; PIE positive
`EXEC_RESULT DemoSpawn spawned=5 of 5` with `VisualRegister` Team=0 x3 / Team=1 x2 (all
five through the dictionary path); PIE negative -- a bogus `Not_A_Row` dictionary produced
`EXEC_RESULT DemoSpawn FAIL Actor=DemoUnitActor_2: UnitDictionary 'Not_A_Row' absent from
the EDU fixture` + `spawned=4 of 5`, then restored; the adopt path (SpawnVolley skirmish
boot, `Tools/CI/audit_volley.log`) registered the exec-spawned ARCHERS (EDU row 2) as
Team=0 and the levy target as Team=1 -- the pre-audit `EduRowIndex == 0` keying would have
tinted the archers Enemy -- plus `SkirmishRetreat Unit=1:1 Anchor=(-87.5,0.0)m Facing=0.0
Serial=2 FireTarget=2:1` (the phase-3 line shape) and 490 `RangedFire` lines. The
SoakVolley nightly (the writeup's last evidence item) stays with the staged CI schedule.
