# PR-10: Author + commit the ranged config assets (D-1)

| | |
|---|---|
| **Finds** | Audit phase-3 D-1: `BattleExec::BuildT0Configs` (BattleExecCommands.cpp) authors FOUR `M_Battle_*` config assets from its table; only two exist in `Content/MassBattle/Data/Units/` (`M_Battle_PeasantLevy`, `M_Battle_SergeantSpearmen`). Rows 2/3 (`M_Battle_PeasantArchers`, `M_Battle_MercenaryCrossbowmen`) were added to the authoring table by the phase-3 kickoff extension but the assets were never committed |
| **Blocks on** | The editor on the reference machine (the command is WITH_EDITOR; assets are binary .uasset), then a git commit of the binaries |
| **Owner** | Whether the config-asset channel (the A5 authoring route) is worth keeping at all -- see the alternative below |

## Motivation

The code and the asset tree currently disagree. `BuildT0Configs`'s table says four rows; the repo
holds two. Anyone re-running the command re-authors the missing two from the table's hard-coded
stats -- which duplicate the EDU fixture's values a THIRD time (they exist in
`golden_edu_v1.txt` and in `FBattleRowStats::FromEdu`). The audit patched the exec fixtures to
dictionary-keyed EDU lookups; the config-asset channel is the one remaining surface where row
stats live in C++ instead of data.

## Proposed change

1. On the UE machine: run `Battle.BuildT0Configs` in the editor world (it is `WITH_EDITOR`-only).
2. Verify the four assets exist and their `EduRowIndex` links resolve (rows 2/3 = the ranged rows;
   `Trait->Kind = Arrow/Bolt`, `RangedRangeM 120/160`, `AmmoPerMan 25/18` per the table).
3. Commit `Content/MassBattle/Data/Units/M_Battle_PeasantArchers.uasset` and
   `M_Battle_MercenaryCrossbowmen.uasset` (binary; the commit message should carry the
   `BuildT0Configs` revision that authored them).

### Alternative the owner should weigh (recommended)

Delete the rows from the authoring table instead, IF the config-asset channel has no consumer.
Check first: `Battle.Unit.Data.EntityConfigIntegrity` and any `UMassEntityConfigAsset` loads. If
nothing consumes the assets, the honest fix is to drop rows 2/3 from `BuildT0Configs` (the
C++ archetype route is the spawn path; the assets are the A5 authoring experiment) rather than
to commit binaries nobody reads. Either way the code and the tree must agree.

## Files

- Edit (option A): `Content/MassBattle/Data/Units/M_Battle_PeasantArchers.uasset` (new, binary)
- Edit (option A): `Content/MassBattle/Data/Units/M_Battle_MercenaryCrossbowmen.uasset` (new, binary)
- Edit (option B): `Plugins/MassBattle/Source/MassBattle/Private/BattleExecCommands.cpp`
  (`BuildT0Configs`'s `FConfigSpec Specs[]` back to two rows; the ranged `else if` branches go)

## Evidence to gather on the UE machine

- Option A: `EXEC_RESULT BuildT0Configs DONE Authored=4` + the two assets loadable in the editor
  (`LoadObject` probe or the content browser); unit tier green (the assets are outside the
  automation filter).
- Option B: `EXEC_RESULT BuildT0Configs DONE Authored=2` + `Battle.Unit.Data.EntityConfigIntegrity`
  green + a grep that no code loads `M_Battle_PeasantArchers`.

## Rollback

Pure additive (option A) or a two-row table edit (option B); no sim behavior depends on either.

## Status

PENDING (filed by the phase-3 audit, 2026-10-30).
