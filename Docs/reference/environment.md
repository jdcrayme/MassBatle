# Environment Reference — MassBattle

| | |
|---|---|
| **Status** | Living reference — canonical home for install/project/VCS facts (**one fact, one home**, `Docs/AGENTS.md` §7). |
| **Verified** | On-machine 2026-10-23 (pre-phase-0) and 2026-10-24 (Phase 0). |
| **Correction rule** | When reality contradicts an entry: correct it **here** + progress-log line. New environment facts land here, not in phase briefs. |

---

## 1. Hardware

The named reference machine for all perf baselines (roadmap R10): **`Docs/spec/reference-machine.md`** — i7-9700F 8C/8T, 16 GB RAM, GTX 1660 Ti, Windows 10 Home 19045. Every perf baseline JSON must embed that spec.

## 2. Engine install & pin

| Fact | Value |
|------|-------|
| Engine | **UE 5.8.4** (CL 58902052, branch `++UE5+Release-5.8`, promoted build) at `E:\Unreal\UE_5.8` |
| Upgrade policy | ADR-13: 5.8 is the **terminal UE5 release**; engine-API facts live in `Docs/reference/engine-api-5.8.md` (Part A doubles as the upgrade-diff checklist) |
| OS | Windows 10 Home, 10.0.19045 (Build 19045), x64 |

## 3. Project layout & the module-naming rule

| Fact | Value |
|------|-------|
| Project | `E:\Unreal Projects\MassBattle\MassBattle.uproject` |
| Game module | **`MassBattleGame`** (`Source/MassBattleGame/`) — **stays empty** |
| Plugin module | **`MassBattle`** (`Plugins/MassBattle/Source/MassBattle/`) — **all game C++ lives here** |
| Targets | `MassBattle` / `MassBattleEditor` |

**Why:** UBT 5.8 compiles the game module and project plugins into ONE "Project" rules assembly and requires each module's Build.cs class name to equal the module name (`RulesAssembly.cs`). A plugin module can therefore never share a name with the game module — the original `MassBattle`/`MassBattle` collision fails UBT with exit 8 (CS0101/CS0111). The game module was renamed in pre-phase-0; do not undo this.

## 4. Plugin enablement reality (5.8)

- Enabled in `.uproject`: **MassGameplay, MassAI, GameplayAbilities, MassBattle** (+ the MCP/editor toolset plugins for the live MCP setup).
- **`MassEntity` and `FunctionalTesting` are NOT plugins in 5.8** — core engine modules (`Engine/Source/Runtime/MassEntity`, `Engine/Source/Developer/FunctionalTesting`; only `FunctionalTestingEditor` is a plugin). Enabling either by name fails UBT with "Unable to find plugin". Never re-add them to `.uproject`/`.uplugin`.
- Deferred (owner call): `TargetAllowList: ["Editor"]` pruning of the MCP toolset plugins — cook bloat + the GameFeatureData cook failure was a symptom (see `Docs/phases/phase-0/audit.md` §3/§7).
- **Stale rules assembly after engine-plugin installs (bit 2026-10-28)**: `Engine/Plugins/Fab` was installed into the engine AFTER the precompiled `Engine/Intermediate/Build/BuildRules/UE5Rules.dll`, so `FabToolset` module rules exist nowhere and UBT aborts any build whose makefile cache gets invalidated (`Expecting to find a type ... 'FabToolset' ... RulesError`). The cache masks it until a source file is added. Recovery that worked: delete `UE5Rules.dll`/`UE5Rules.pdb`/`UE5RulesManifest.json`, TEMPORARILY move `Engine/Build/InstalledBuild.txt` aside, run `dotnet UnrealBuildTool.dll -Mode=QueryTargets -project=<proj>` (regenerates UE5Rules + MarketplaceRules incl. Fab), restore the marker. `Tools/CI/run_tests.bat` never builds — always run the Jenkinsfile's `Build.bat MassBattleEditor Win64 Development` step first (running the bat alone executes stale DLLs and fails confusingly).

## 5. Mass module map (5.8)

| Home | Modules |
|------|---------|
| Core `Engine/Source/Runtime/Mass/` | MassCore, MassDeveloper, MassEngine, **MassSignals** |
| Core `Engine/Source/Runtime/MassEntity` | MassEntity (top-level, not under `Mass/`) |
| MassGameplay plugin | MassCommon, MassActors, MassEQS, **MassSpawner**, MassSmartObjects, MassSimulation, MassLOD, MassMovement, MassCharacterTrajectory, MassReplication, MassRepresentation, … |
| MassAI plugin | MassNavigation, MassNavMeshNavigation, MassZoneGraphNavigation, MassAIBehavior, … |

- `UMassEntityConfigAsset` = `/Script/MassSpawner.MassEntityConfigAsset` (module MassSpawner — **not** `/Script/MassEntity.*`).
- **MassCore needs explicit linking** in Build.cs (base-struct UClass ctors → LNK2019 without it).
- Since 5.5: `FInstancedStruct` & co. live in CoreUObject (`StructUtils/InstancedStruct.h`; StructUtils plugin deprecated).

## 6. VCS state & conventions

- git on `main`; **git-lfs 2.13.3 active** for `*.uasset` / `*.umap` / `*.uproject` (+ media types) — works, keep it.
- `.gitattributes`: `Data/**/*.txt text eol=lf` (repo-wide `* text=auto` would materialize CRLF and break the LF parser contract).
- Data files: **ASCII + LF only; em-dashes forbidden** (use `--`).
- Commits: one per task, message prefix `phaseN:`; amend only unpushed local commits.

## 7. Content, config & data conventions

- Maps: `/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01` (editor startup + game default), `/Game/MassBattle/Test/Maps/L_MassBattle_T0_Sandbox`, `/Game/MassBattle/Test/Maps/L_MassBattle_T0_Functional` (S-scenario map; needs a cook-list entry before any staged-build func tier).
- `Data/EDU/golden_edu_v1.txt` — ASCII, LF-only, **2 unit types × 14 fields**; it is the **schema source of truth** for the EDU parser (parse what the fixture contains; do not invent field names).
- Config rules learned in Phase 0: the AssetManager **GameFeatureData rule belongs in `DefaultGame.ini`** (`UAssetManagerSettings` is `config=Game`; `bIsEditorOnly=False`) — NOT `DefaultEngine.ini`. Zen: `[Zen.AutoLaunch] LimitProcessLifetime=false` in `DefaultEngine.ini`.
- Baselines: `Baselines/perf/<machine-tag>/<gate>.json`, schema `{ machine, engineCL, date, metrics{…} }`; nightly flow documented in `Baselines/perf/README.md` + `Tools/CI/JENKINS.md` (repo, outside `Docs/`).

## 8. Facts-correction ledger (brief-era claims reality overruled)

| Original claim (source) | Reality (verified on machine) |
|---|---|
| "Editor open, MCP server running" at session start (pre-phase-0 §1.1) | Editor closed, port dead; MCP auto-starts on launch |
| "VCS: none yet — you create git in Task 0" | Git already existed (`main`, standard UE ignore/attributes) |
| "no LFS" | git-lfs 2.13.3 installed and in use |
| "Enable MassEntity + FunctionalTesting plugins" (pre-phase-0 Task 1) | Neither is a plugin in 5.8 — core modules; enabling fails UBT |
| "Plane scaled ×2560 → 256 m × 256 m" (pre-phase-0 Task 3) | Engine plane base is 100 cm → ×2560 = 2560 m; honored the 256 m intent with ×256, bounds-verified |
| "2 unit types × 13 fields" (briefs) | Fixture has **14** field keys; off-by-one propagated through 3 files before being caught |
| Python `unreal` fallbacks via MCP (pre-phase-0 §1.4) | Unreachable — `ProgrammaticToolset` sandbox import whitelist |

## 9. Provenance

Merged 2026-10-25 from: pre-phase-0 report §2/§3 · phase-0 report §3 · phase-0 progress log discrepancy notes · `Docs/spec/reference-machine.md` (hardware stays there — this file links, not copies). Future environment corrections land **here**, not in phase briefs.
