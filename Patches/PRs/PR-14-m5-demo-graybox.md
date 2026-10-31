# PR-14: M5 demo gray-box -- the rout visualization sample (SpawnRout on the demo map)

| | |
|---|---|
| **Finds** | Phase-4a brief Task 7 step 6 (spec morale-and-gas section 5, the M5 row): the overlay rows + the `Battle.Morale.SpawnRout` fixture are PR-12's; the DEMO-side sample (the game-mode hookup + the PIE eyeball) is editor-scoped |
| **Blocks on** | PR-12 + PR-13 executed; one editor session (MCP or manual) for the map-level authoring + the eyeball |
| **Owner** | The demo map's authored placements (a .umap cannot be edited statically -- the PR-11 posture) |

## Motivation

The M2TW feel signature of morale is visible: units waver, break, and stream off the field while their general's aura ring steadies the neighbors. The spine makes all of it observable in overlay terms (PR-12's rows: morale bars by rout state, aura rings, flee arrows); the demo map needs the fixture hookup + one placement set so a PIE session shows the full cycle. Gray-box only -- representation (routing banners, morale-tinted cards) is Phase 5 (the PR-8 precedent).

## Proposed change

1. `DemoGameMode` (or the existing demo sample path): the M5 sample boots `Battle.Morale.SpawnRout` -- a general unit + two adjacent infantry per side at engagement distance, dictionary-keyed rows (the B-3 lesson), teams set, one impetuous row for the auto-charge flavor.
2. The demo HUD: reuse the existing debug-overlay keybind surface to toggle the morale rows (`Battle.Debug.ShowOverlay` already gates them; no new IMC assets if the existing toggle suffices -- the PR-2 posture if a new key is wanted).
3. PIE eyeball (the owner-scoped item): the five-ish placed units spawn through the dictionary path, the morale bars render per unit colored by rout state, the aura ring sits at the general, a scripted volley onto one unit drives it wavering -> routing with the flee arrows, and the general walking over rallies it. Screenshot for the phase report (the PR-8 arrow-mass precedent).

## Files

- Edit: `Source/MassBattleGame/DemoGameMode.cpp` (the M5 sample), possibly `DemoHUD.cpp` (a morale readout line if the overlay bars are not enough)
- Edit (editor-scoped): `Content/MassBattle/Demo/Maps/L_MassBattle_Demo01.umap` (re-save with the M5 placements) -- static authoring CANNOT touch this; the PR-11 record is the workflow precedent

## Evidence to gather on the UE machine

- PIE: `EXEC_RESULT DemoSpawn spawned=N of N` through the dictionary path + the rout-cycle lines (`SkirmishRetreat`-shaped `RoutFlee`/`RallyApplied` lines from PR-12's apply surface)
- The screenshot (the PR-8 arrow-mass precedent)
- Headless boot: the demo map loads + the M5 sample runs in `-game` (the m4_boot precedent)

## Rollback

Demo-module only; the sim is untouched. The M5 sample is additive behind the existing demo boot paths.
