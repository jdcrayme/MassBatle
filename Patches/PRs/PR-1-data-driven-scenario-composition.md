# PR-1: Data-driven demo scenario composition

| | |
|---|---|
| **Finds** | Audit D-2 (root of B-3): demo scenario composition lives only in C++ and binary uassets |
| **Blocks on** | Editor asset authoring (MCP), PIE verification cycle on the reference machine |
| **Owner** | Owner decision on the scenario-data format (DataTable vs JSON vs data asset) |

## Motivation

The demo has THREE parallel scenario-authoring channels, each with a different blast radius:

1. `ADemoUnitActor` props placed on the map (binary `.uasset` -- cannot be code-reviewed, merged
   visually, or diffed)
2. `Battle.Combat.SpawnClash` fixture tables (now data-shaped in C++ after the audit's B-3 fix,
   but still a recompile to retune)
3. The M3 mob sample wired from `DemoGameMode` when the map is empty

A scenario is game data. Editing a spawn position or swapping a unit type should be a data
change: reviewable in a diff, hot-reloadable where possible, and authorable by someone who is
not in the C++ module. This PR unifies the three channels behind one data format.

## Proposed change

1. New `UDataTable` row type `FDemoScenarioSpawn { Dictionary (FName), Count, Pos, Yaw, Team }`
   in the plugin's public types (the demo module consumes it; the plugin already owns the
   registry/spec surface).
2. `DemoGameMode` reads a `UDemoScenarioAsset` (`UDataAsset` holding an array of spawn rows +
   optional per-side order script) assigned in the map. Empty-asset maps keep today's M3 sample
   fallback.
3. `SpawnClash` fixtures become three checked-in scenario assets next to the golden EDU
   (`Content/MassBattle/Data/Scenarios/`), selected by name; the C++ table added by the audit
   becomes the fallback if the asset is missing (backwards-compat for CI).
4. The map-placed `ADemoUnitActor` path stays (level-driven authoring is legitimate); the gap
   this PR closes is only that fixtures and the M3 sample stop being C++.

## Files

- New: `Plugins/MassBattle/Source/MassBattle/Public/BattleScenarioAsset.h` (row struct + asset)
- New: `Source/MassBattleGame/DemoScenarioConsumer` wiring in `DemoGameMode`
- Edit: `BattleExecCommands.cpp` (SpawnClash asset lookup before the C++ fallback table)
- New assets: `Content/MassBattle/Data/Scenarios/{DA_Mob,DA_Wall,DA_Charge}.uasset` (author via MCP)

## Evidence to gather on the UE machine

- PIE on the demo map: identical behavior to the pre-PR session (mob sample still spawns,
  SpawnClash wall/mob/charge unchanged from the C++ tables -- handle logs compared)
- Func tier: S5_Mob, CombatSmoke green (they exercise the mob composition)
- An editor-side retune (move a spawn 5 m) requires NO recompile

## Rollback

The C++ fallback tables are retained; reverting the asset assignment restores the exact prior
behavior. The PR is additive at every step.
