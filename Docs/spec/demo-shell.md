# Demo Shell -- Design Spec (the M7 main demo + player interface)

**Status:** Living spec (owner-authored; implementing agents read cited sections only)
**Engine pin:** UE 5.8.4 (ADR-13)
**Scope:** the composed main demo (`Content/MassBattle/Demo/Maps/L_MassBattle_Demo01.umap`), the demo-module shell that drives it (`Source/MassBattleGame/`), and one new EDU data row (§2.3). The plugin code is consumed through its public API only (the ADR-18 pattern); no sim-module code changes are in scope.
**Acceptance:** the D-gate table in `Docs/spec/roadmap-and-testing.md` Part A ("Demo acceptance gates") -- owner-run, manual PIE, with the per-gate triage rule tying each gate to the automated suites that back it. Gate IDs are append-only.

---

## 1. Problem and goal

The demo currently ships as two half-demos. The authored map path (`ADemoGameMode::TrySpawnLevelUnits`) enables only the combat pipeline: it places no missile units, no general, and never enables morale, so the ranged, morale, and garrison layers are invisible on the main map. The console-fixture path (M3 mob -> M4 volley -> M5 rout, registered only on zero-level-unit maps) runs sequential CI fixtures that each reset the battle. Nothing demonstrates the composed experience, and nothing happens until the player orders it.

**Goal:** one field where M2 (formed clash), M4 (arrow volleys), M5 (rout cascade), the cavalry charge, and the garrison story happen as consequences of each other -- driven by a scripted beat arc that plays itself, under a player interface that exposes every command verb the sim ships. The M3/M4/M5 console fixtures remain CI evidence; the demo map stops depending on them.

**Non-goals:** representation (Phase 5 / M6; stays gray-box per the PR-8 and M5 postures), UMG migration (Phase 5), player-activatable abilities (no catalog exists -- no fake buttons), shift-order queueing and run/walk (§7), perf claims (the soak maps own those).

## 2. The field

One authored battle, west (player, Team 0) vs east (enemy, Team 1). Every unit is a map-placed `ADemoUnitActor`, **dictionary-keyed** (`UnitDictionary`, the PR-11 rule -- `EduRowIndex` stays the CI fallback), with `NumRanks` / `SoldierCount` / `Team` authored per unit. Structures are `ADemoGarrisonProp`s. Positions below are targets in meters (x west->east, y south->north); the editor pass finalizes them against the placement previews (`ADemoUnitActor` footprint lines, `ADemoGarrisonProp::DrawPlacementPreview`) and re-saves the map (the PR-11 posture: a .umap cannot be authored statically).

### 2.1 Order of battle (~384 entities)

| ID | Dictionary | Count | Ranks | Pos (m) | Yaw | Notes |
|----|------------|-------|-------|---------|-----|-------|
| P1 | Peasant_Levy | 40 | 4 | (-40, -30) | 0 | left wing infantry |
| P2 | Sergeant_Spearmen | 40 | 4 | (-40, 0) | 0 | center |
| P3 | Sergeant_Spearmen | 40 | 4 | (-40, 30) | 0 | right wing; trench in front |
| C1 | Feudal_Knights | 24 | 3 | (-52, -52) | 0 | wing cavalry -- the charge showcase (D.19, beat 4) |
| P4 | Peasant_Archers | 40 | 3 | (-58, 15) | 0 | fire support behind the line; arced shots over own heads exercise the friendly-fire rule (D.9) |
| P5 | Feudal_Knights | 12 | 2 | (-70, 0) | 0 | **the mounted general**: `OfficerCount 2`, `bIsGeneral` -- aura anchor, ~30 m behind center |
| E1 | Peasant_Levy | 40 | 4 | (40, 15) | 180 | enemy left |
| E2 | Sergeant_Spearmen | 40 | 4 | (40, -15) | 180 | enemy right |
| E3 | Sergeant_Spearmen | 32 | 4 | (58, 0) | 180 | second line / reserve |
| E4 | Mercenary_Crossbowmen | 40 | 3 | (58, 25) | 180 | flat-trajectory fire support |
| C2 | Feudal_Knights | 24 | 3 | (52, 48) | 180 | enemy wing cavalry (east of the village) |
| E5 | Feudal_Knights | 12 | 2 | (72, 0) | 180 | **enemy mounted general**: `OfficerCount 2`, `bIsGeneral` -- the beat-4 kill target |

Mounted generals are the classic TW look and make the morale beats *faster to stage* (the aura anchor repositions quickly), but the aura/officer machinery is row-agnostic -- `bIsGeneral` + officers is all it reads.

### 2.2 Structures

| Type | Target pos (m) | Role |
|------|----------------|------|
| Building | (8, 52) | the contested village objective (beat 6) |
| Building | (52, 80) | village second slot, north -- the building fixture is 48x20 m, so the two buildings need ~25 m separation minimum (the boot probe caught the first spacing overlapping) |
| Wall | (52, -52) | covers the enemy right; forces cost-grid pathing around it |
| Trench | (-26, 30) | in front of P3; the shield-wall story fights from it (beat 3) |

The three existing props are re-positioned from flank decoration into the fight; footprint sizes come from the plugin's `FGarrisonLayout::MakeFixture` (building 2 units / wall + trench 1), so final placement yields to the entrance lines the previews draw -- and to the one-time boot click probe, which fails loudly when any two registered footprints (plus the 3 m click margin) collide.

### 2.3 The cavalry row (the one new EDU data item)

The sim already maps everything cavalry needs -- adding a row is fixture data, not a schema change:

- `category cavalry` is the `bCavalry` tag source (`BattleSpawnLibrary.cpp` -- `OutStats.bCavalry = Category == "cavalry"`); `stat_health`'s second value is the mount pool (`MountHP`, cavalry rows only); `stat_charge_dist` maps to `ChargeDistance` (already read for every row). `FCavalryTag` rides the spawn tags.
- **New row:** dictionary `Feudal_Knights`, category `cavalry`, class per the M2TW heavy-cavalry convention, `stat_health` with a mount value, `stat_charge_dist` set. Constants are A6 placeholders pending the T6.1 audit, like every other row.
- **Appended at the END of the golden fixture** -- row indices of the existing four rows stay stable (the `EduRowIndex` CI fallback and the PR-11 ordering rule).
- **Golden re-sign:** the T0.2 canonical round-trip is re-signed with diff review (the B.7 discipline), plus a unit test asserting `FromEdu` maps the row (tag + `MountHP` + `ChargeDistance` + `Kind None`) -- the B.8 unit-test rule for the new capability.
- `Battle.Func.S2_PassThrough` may adopt the row for its cavalry proxy (optional, CI-side).

## 3. The beat arc (the sequencer)

A demo-module sequencer in `ADemoGameMode`: a table of beats `{AtSeconds (or state trigger), HUD label, scripted orders}`. Every scripted order is a **plain registry order** (`IssueAttackUnitOrder` / `IssueAttackMoveOrder` / `IssueMoveOrder` / `IssueGarrisonOrder`) or a typed `BattleExec` call -- the same surface the player's input uses. This keeps the determinism story intact (no wall-clock coupling inside the sim) and makes player override free: any player-issued order to a unit marks it player-touched and later scripted orders skip it. Beats still advance (labels are broadcast), so the arc never stalls.

| # | Trigger | Label | What fires |
|---|---------|-------|------------|
| 0 | t=0 | Deploy | both lines stand; banner + HUD settle |
| 1 | t=8 | Advance | E1, E2, E3 attack-move to (-10, 0); E4 holds (fire support) |
| 2 | first volley (poll shots metric; time fallback) | Volley | nothing to order -- P4/E4 fire at will when in range; the label acknowledges it |
| 3 | first engagement record touching a player unit (time fallback) | Clash | melee grind; P3 fights from the trench |
| 4 | t+55 | Break | morale plays out (casualty-driven); C1 attack-moves the enemy left rear -- the scripted mounted charge (D.19); at t+65 the sequencer kills E5's general entity via `BattleExec::KillEntity` -- the officer-death shock (D.13) |
| 5 | t+75 | Rally | P5 rides to field center beside a routing friendly -- aura ring, rally (D.12) |
| 6 | t+90 | Garrison | the least-touched surviving player unit garrisons the village building; a later half-beat orders the egress (D.14) |
| 7 | state: one side has no steady units alive | Resolution | HUD verdict line; the arc ends |

Beat-4/5 timings are placeholders pending the T6.1 morale-threshold co-audit (§7): until the threshold family is tuned, the morale beats' *shape* is asserted (wavering -> breaking -> routing -> cascade -> rally) and their *clock* is soft.

## 4. Player interface

### 4.1 Input grammar

All new keybinds follow the PR-2 posture: `IA_Demo_*` action assets added to `IMC_Demo` (editor-owned, retunable without recompile); bindings in code are behavior.

| Gesture | Verb | Sim surface | Status |
|---------|------|-------------|--------|
| LMB click / shift+click | select / toggle select | visuals selection set | have |
| LMB drag | marquee | same | have |
| double-LMB | select all of the same EDU type on screen | PC-side (group by dictionary) | new shell |
| Ctrl+1..9 / 1..9 | group store / recall | PC-side | new shell |
| RMB on terrain | move, keep formation, face the point | `IssueMoveOrder` | have |
| RMB drag | formation gesture: the drag line is the FRONT, divided across units (width via the A2 width API) | `IssueMoveOrder` | have |
| RMB on enemy | attack (guard honored) | `IssueAttackUnitOrder` | have |
| double-RMB on enemy | **charge**: attack with guard cleared -- chases, auto-charges, pursues | `IssueAttackUnitOrder` + `SetUnitGuardToggle(false)` | new wiring |
| Ctrl+RMB on ground | attack-move (engage en route) | `IssueAttackMoveOrder` | have |
| RMB on structure | garrison | `IssueGarrisonOrder` | have |
| X | exit garrison | `IssueExitGarrisonOrder` | new wiring |
| G | guard mode toggle | `SetUnitGuardToggle` | new wiring |
| F | fire-at-will toggle | `SetUnitFireToggles` | new wiring |
| R | skirmish toggle | `SetUnitFireToggles` | new wiring |
| H | halt | `IssueStopOrder` | have |
| F3 | debug overlay | `Battle.Debug.ShowOverlay` | have |
| shift+RMB | order queue | -- | **deferred**: the registry holds ONE current order per unit (§7) |
| Alt+RMB | run/walk | `SpeedMod` | **deferred**: no walk state in the sim |

Charge needs no sim work: `UChargeProcessor` seeds the charge bonus on run-up + fresh contact for any attack order, so double-RMB is an input alias whose only delta is the guard clear (the TW distinction between "attack" and "charge, pursue"). Cavalry charges the same path -- the row's `MountHP` + `ChargeDistance` make the impact read, the mechanics are shared.

### 4.2 World layer -- unit banners

One clickable floating card per unit, projected at the body center (`DemoUi::BodyLabelZCm`), replacing the text labels. Canvas HUD + screen-space rect hit tests (the `PickUnitAtScreen` projection math is reused); UMG is a Phase-5 migration, not now.

Always visible:

- team-colored border + type glyph (infantry / spear / missile / cavalry from the row capability tags)
- live soldier bar (roster vs spawn size)
- morale chip tinted by `RoutState` (steady / wavering / breaking / routing -- the color family `MoraleTickProcessor::DrawMoraleOverlay` already derives)
- ammo pips for missile rows (`ComputeAmmoFraction`; out-of-ammo state from `GetUnitFireToggles`)
- general star (`FindTeamGeneral`), garrisoned tag, selected ring (exists), toggle badges (guard / fire / skirmish)

Hover expands to the stats card: `FBattleRowStats` (attack / defense / charge / armor / shield / range / ammo / discipline) plus live GAS attrs (morale value, fatigue, experience chevrons), current order, toggle states.

Interaction: click = select; double-click = select all of that type; click an enemy banner = attack it with the current selection. Zoom LOD: below a zoom threshold banners collapse to a small flag and re-expand on hover (M2TW convention; keeps 10+ cards readable).

### 4.3 Command interface (bottom bar)

- **Card row**: the existing 10-card row upgraded -- type glyph + count + morale chip; click selects, double-click isolates the type, grayed when routing.
- **Selected panel**: the stat readout plus stance buttons -- Guard, Fire-at-will, Skirmish, Exit-garrison -- each a one-line call to the setter family with read-back state from the getters; out-of-ammo grays the fire control.
- **Formation buttons**: Wider / Narrower re-issue the move order at the current anchor through the same A2 width path the drag gesture uses; rank depth derives from file count x roster (the TW behavior).
- **Minimap** (optional, last): terrain color + unit dots + camera frustum.
- **Destination flag marker** in world at the RMB point (cheap, high TW feel).

## 5. Build order (PR carve-outs)

| PR | Content | First gates it can pass |
|----|---------|------------------------|
| 1 | demo field data (unit/structure tables per §2), the cavalry EDU row + golden re-sign + `FromEdu` cavalry unit test (§2.3), pipeline enables on the level-unit path (combat + ranged + morale session-on, the `SpawnRout` pattern), sequencer skeleton (beats 0-3), HUD beat label | D.1, D.17 (partial), headless `m5_boot`-shaped boot evidence |
| 2 | banner cards: always-visible row + hover expand + click / double-click / enemy-click + zoom LOD | D.16, D.2-D.4 visuals |
| 3 | command bar: card row upgrade, selected panel, stance buttons, G/F/R/X keybinds (IA assets per PR-2) | D.15, D.14 (egress), D.7 |
| 4 | gestures: double-RMB charge, select-same-type, ctrl-groups, destination flag; beats 4-7 + verdict | D.5, D.6, D.11-D.14, D.17, D.19 |
| 5 | optional polish: minimap, banner LOD tuning, D.18 sweep | D.18 |

PR 1 is C++/data-side and headless-verifiable (`Tools/CI/m5_boot.log` precedent) except the golden re-sign, which rides the per-commit gate. The map placements in §2 need one editor session (MCP or manual) -- the owner-scoped piece, the PR-11/PR-14 posture exactly.

## 6. Traceability: gates to tests

Every D-gate names the automated suites that assert the same layer headlessly (the "Backed by" column in the roadmap table), per layer: movement (D.2-D.4 -> S1/S4_WidthOverride/S7_FallIn + the Slots/Spine unit tiers), engagement + charge (D.5-D.7, D.19 -> S4_ShieldWall/ChargeMechanics/S2_PassThrough + Combat), ranged + fire control + skirmish (D.9, D.10, D.15 -> S6_Volley/SkirmishBehavior + Ranged), morale (D.11-D.13 -> S7_RoutCascade/OfficerDeath + Morale), garrison (D.14 -> S8/S8b + Garrison), the EDU row (D.19 -> EduRoundTrip re-sign). The triage rule lives with the gate table (roadmap Part A): backing suites green + gate red = demo-shell wiring defect; a backing suite red = sim defect; no backing suite = test-profile gap, add the test first (B.8 rules 1-2). Gates marked "none (pure shell)" touch demo-module code only, by construction.

## 7. Constraints and open dependencies

- **Morale Enable flip is evidence-blocked** (PR-12: the levy base morale 2 vs the placeholder WaveringThreshold 4.0 -- PairLifecycle regressed on the probe). The demo enables morale session-on (`ECVF_SetByCode`, the `MoraleSpawnRout` pattern); beat 4/5 timing lands with the T6.1 threshold co-audit.
- **The .umap cannot be authored statically** (PR-11 posture): the §2 placements need an editor session; static tooling may not touch the map.
- **One order per unit**: the registry holds a single current order, so shift-queue is a sim change, deferred. The sequencer is built to not need it.
- **No walk/run state** in the sim (`SpeedMod` exists; a toggle would be a small sim addition) -- deferred.
- **No player-activatable ability catalog** (GAS abilities are the passive morale/fatigue machinery): no ability buttons. Special formations (wedge, schiltrom) have no sim support and get no buttons.
- **Cavalry is one new EDU row** (§2.3): the mapping already exists in the spawn library; no parser or schema change; the golden re-sign rides the T0.2 diff review.
- **Gray-box only**: capsules, boxes, overlay, canvas. Representation is Phase 5 (M6).
