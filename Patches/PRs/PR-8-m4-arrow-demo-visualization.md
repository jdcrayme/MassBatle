# PR-8: M4 arrow-volley demo -- visible arrows (ISM pool, gray-box)

| | |
|---|---|
| **Finds** | Roadmap M4 ("Arrow volleys -- proves the ranged stack") needs a visible arrow mass to be a demo at all; the sim renders arrows only as debug-overlay segments (Phase 3's gray-box posture, spec section 1) |
| **Blocks on** | Editor asset authoring (an arrow ISM mesh + material via MCP), PIE verification on the reference machine; Phase 5 owns the full representation bands (arch section 11.5: volleys = one Niagara batch per volley, arrow-stick decals pooled) |
| **Owner** | Mesh/material choice for the gray-box arrow (a capsule or low-poly shaft is in keeping with B.1's capsules-first rule) |

## Motivation

M4 is the phase's milestone demo, and "arrow storm" is the M2TW signature the whole project chases. The pool's SoA layout is already the render feed (spec section 10: "the pool's SoA layout IS the render feed"), but a visible arrow needs an instanced mesh component nobody can author from a static patch -- an asset, a component registration, and a PIE eyeball pass.

## Proposed change

1. A `UBattleArrowVisualComponent` on the demo side (`Source/MassBattleGame/`, the owner-decided demo home): one `UInstancedStaticMeshComponent` fed from `UBattleProjectileSubsystem` each frame (batch transform update from the pool's SoA, one draw call; capacity-clamped with the oldest-first skip).
2. Gray-box arrow asset: `/Game/MassBattle/Demo/SM_Arrow` (low-poly shaft + head, unlit material, ~20 cm) authored via MCP.
3. Volley polish hook: per spec section 10/Phase 5, ONE Niagara batch per volley replaces the ISM feed when Phase 5 lands -- this component is the interim seam, not the destination.
4. Demo fixtures `Battle.Ranged.SpawnVolley {line|skirmish}` (PR-6) drive it; a camera preset on the demo map that frames the volley arc.

## Files

- New: `Source/MassBattleGame/BattleArrowVisualComponent.h/.cpp` (demo module; consumes the plugin's public pool API only -- the ADR-18 pattern)
- New asset: `Content/MassBattle/Demo/SM_Arrow.uasset` (+ material)
- Edit: `DemoGameMode` (component attach when the ranged flags are on)

## Evidence to gather on the UE machine

- PIE on the demo map: `Battle.Ranged.SpawnVolley line` -> a visible arcing arrow mass, ~1,000 instances without a frame hitch (the T3.6 fixture doubles as the visual stress)
- Draw-call count unchanged by arrow count (one ISM)
- Screenshot tier sanity (B.1 tolerance): arrows present, animating along arcs (positions update per frame)

## Rollback

The component is demo-module-only and flag-gated; deleting the component registration leaves the sim untouched (the pool never knew about rendering).

## Status (2026-10-29)

EXECUTED (gray-box). Code: UBattleArrowVisualComponent + flag-gated
attach (`46d0c1c`); assets SM_Arrow (+X forward, 21.5 cm, bounds
verified) + M_ArrowGray unlit (`3b3a528`). PIE evidence: the feed
attached (`DemoArrows visual feed attached`), and the ISM's StaticMesh
= SM_Arrow in the live PIE world -- the component only assigns the mesh
once the pool has live entries. Residual: the arrow-mass screenshot is
not obtainable through the MCP capture surface (it renders the editor
world, not the PIE viewport); the eyeball pass is owner-scoped. Phase 5
owns the Niagara-per-volley replacement.
