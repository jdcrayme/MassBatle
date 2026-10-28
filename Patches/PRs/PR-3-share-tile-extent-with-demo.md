# PR-3: Expose the movement-tile extent to the demo clamp

| | |
|---|---|
| **Finds** | Audit B-7 (root): the demo re-derives the cost-grid tile extent as a local literal |
| **Blocks on** | Plugin public-API change + a UE-built test cycle (the plugin's func tier asserts order behavior) |
| **Owner** | Owner call on the API shape (accessor on `UBattleCostGridSubsystem` vs a constant in `BattleCostGrid.h`) |

## Motivation

`ADemoPlayerController::ClampToGridHelper` (after the audit's dedup, the demo's ONE clamp) carries
decomposed local constants `TileHalfExtentCm = 12800` / `EdgeMarginCm = 300`. Those numbers are a
copy of the plugin's grid geometry: the cost grid is a 256 m flat tile at the origin
(`BattleCostGrid`, referenced across the movement planners). If the owner ever resizes the tile
(or a later phase introduces multi-tile maps), the demo silently clamps orders into the wrong
box while the plugin plans paths in the right one -- orders would dead-end at an invisible wall
of the demo's own invention.

Data the plugin owns must not be re-typed by its consumer. This is the smallest instance of the
audit's B-3/B-7 family, and the only one whose fix touches a plugin public header (hence a UE
test cycle rather than a static-audit patch).

## Proposed change

1. `BattleCostGrid.h`: export the tile half-extent as a named constexpr beside the grid data
   (e.g. `namespace BattleGridGeo { constexpr float TileHalfExtentCm = 12800.f; }`) -- it is
   already implicit in the grid's dimensions; this only names it.
2. Optional accessor `UBattleCostGridSubsystem::GetTileHalfExtentCm()` if the owner prefers
   instance-level (future multi-tile) rather than module-level truth.
3. Demo: `ClampToGridHelper` reads the constant (or accessor); the local decomposition keeps only
   the genuinely demo-owned `EdgeMarginCm` (the 3 m order-inside-tile margin is a UX decision).

## Files

- Edit: `Plugins/MassBattle/Source/MassBattle/Public/BattleCostGrid.h` (+ `.cpp` if accessor)
- Edit: `Source/MassBattleGame/DemoPlayerController.cpp` (`ClampToGridHelper` body)

## Evidence to gather on the UE machine

- Unit tier green (the planner/cost-grid tests pin the grid geometry)
- Func tier: OrderSmoke + S1 green (orders clamped to the tile edge still plan correctly)
- PIE demo: click at the far map corner issues a Move clamped inside the tile (order logged
  inside 125 m), unchanged from today

## Rollback

Two-line revert; no schema or data impact.
