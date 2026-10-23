# MassBattle Pre-Phase-0 Handoff Report

**Date:** 2026-10-23 · **Agent run:** autonomous pre-phase-0 setup per `BRIEF.md` v1.0
**Engine:** UE 5.8.4 (CL 58902052, `++UE5+Release-5.8`, promoted) at `E:\Unreal\UE_5.8`
**Project:** `E:\Unreal Projects\MassBattle\MassBattle.uproject`
**Status:** **All tasks 0–6 complete. No open blockers.** DoD checklist at the bottom, every box verifiably checked.

---

## 1. What was created (paths)

### Plugin (exactly per BRIEF Appendix A except where §3 below forced changes)
- `Plugins/MassBattle/MassBattle.uplugin` — module `MassBattle` (Runtime/Default); plugin deps: MassGameplay only (see §3.4 for the MassEntity removal)
- `Plugins/MassBattle/Source/MassBattle/MassBattle.Build.cs` — deps: Core + CoreUObject, Engine, MassEntity, MassSpawner, MassSignals, MassCommon, GameplayAbilities
- `Plugins/MassBattle/Source/MassBattle/Public/MassBattleModule.h` + `Private/MassBattleModule.cpp` — `IMPLEMENT_MODULE(FMassBattleModule, MassBattle)`
- `Plugins/MassBattle/Source/MassBattle/Private/MassBattleSmokeTest.cpp` — `Battle.Smoke.PluginLoaded` (EditorContext|SmokeFilter)
- Build artifacts (gitignored): `Plugins/MassBattle/Binaries/Win64/UnrealEditor-MassBattle.dll`

### Levels (Content)
- `Content/MassBattle/Demo/Maps/L_MassBattle_Demo01.umap` — SkyLight, DirectionalLight, SkyAtmosphere, PlayerStart, WorldSettings, Ground (`/Engine/BasicShapes/Plane` at origin, scale ×256 → **256 m × 256 m**, verified bounds ±12,800 cm), plus one auto-respawned `MassDebugVisualizer_0` (see §5.9)
- `Content/MassBattle/Test/Maps/L_MassBattle_T0_Sandbox.umap` — identical minimal set, **no** Mass visualizer
- `Content/MassBattle/Test/Scratch/` — created, left empty (pipeline proof asset deleted)

### Data & config
- `Data/EDU/golden_edu_v1.txt` — ASCII, LF-only, 2 unit types (Peasant Levy, Sergeant Spearmen), 13 brief-listed fields each, placeholder stats per brief
- `Config/DefaultEngine.ini` — `EditorStartupMap` + `GameDefaultMap` = `/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01` (only this changed)
- `Tools/CI/README.md` — three-line R8 stub
- `.gitattributes` — added `Data/**/*.txt text eol=lf` (see §3.8)

### Docs / agent memory
- `Docs/agent/progress.md` — full step-by-step log with verification results per step
- `Docs/agent/mcp-inventory.md` — 49 toolsets, relevant ones with qualified names, AgentSkills list, MCP quirks
- `Docs/reference-machine.md` — i7-9700F 8C/8T, 16 GB RAM, GTX 1660 Ti, Win10 Home 19045 [roadmap R10]
- This file.

### Project file changes forced by reality (deviations — details in §3)
- `MassBattle.uproject` — plugins appended: MassGameplay, MassAI, GameplayAbilities, MassBattle (33 entries); game module renamed `MassBattle` → **`MassBattleGame`**
- `Source/MassBattle*` → `Source/MassBattleGame/` (`MassBattleGame.{h,cpp,Build.cs}`), both `*.Target.cs` → `ExtraModuleNames.Add("MassBattleGame")` (target names unchanged)

---

## 2. Corrections to the BRIEF facts digest (§1) — trust these over the brief

| # | Brief said (§1) | Reality on this machine (UE 5.8.4) |
|---|---|---|
| 1 | "editor open, MCP server running" at session start | Editor **closed**, port 8000 dead. Recovery: MCP auto-starts on editor launch — Saved config `bAutoStartServer=True`, `ServerPortNumber=8000`, `bEnableToolSearch=True`. |
| 2 | "VCS: None yet — you create git in Task 0" | Git **already existed** (main; `Repo setup`, `Initial Commit`) with standard UE `.gitignore` + `.gitattributes`. |
| 3 | "no LFS" | **git-lfs 2.13.3 installed and in use**; `.gitattributes` tracks `*.uasset/*.umap/*.uproject` (+fbx/png/jpg/wav/mp3/mp4) via LFS. `.umap` files committed as LFS objects — works, keep it. |
| 4 | Enable plugins MassEntity, MassGameplay, MassAI, GameplayAbilities, FunctionalTesting, MassBattle | **`MassEntity` and `FunctionalTesting` plugins do not exist in 5.8.** MassEntity is a core engine module (`Engine/Source/Runtime/MassEntity`); FunctionalTesting is a core Developer module (`Engine/Source/Developer/FunctionalTesting`; only `FunctionalTestingEditor` is a plugin). Enabling either by name fails UBT: "Unable to find plugin". Final enabled set: MassGameplay, MassAI, GameplayAbilities, MassBattle. |
| 5 | M1-style layout (MassEntity as plugin) | 5.8 core Mass modules: `Engine/Source/Runtime/Mass/` = **MassCore, MassDeveloper, MassEngine, MassSignals**; `Engine/Source/Runtime/MassEntity`. Plugin modules verified: MassGameplay = MassCommon, MassActors, MassEQS, **MassSpawner**, MassSmartObjects, MassSimulation, MassLOD, MassMovement, MassCharacterTrajectory, MassReplication, MassRepresentation, MassGameplayDebug/Editor, MassMovementEditor, MassGameplayExternalTraits, MassGameplayTestSuite; MassAI = MassNavigation, MassNavMeshNavigation, MassZoneGraphNavigation, MassNavigationEditor, MassAIBehavior(/Editor), MassAIReplication, MassAIDebug, MassAITestSuite. M1/M4 verified otherwise. |
| 6 | — (gap, not contradiction) | `UMassEntityConfigAsset` lives in module **MassSpawner** in 5.8: class path `/Script/MassSpawner.MassEntityConfigAsset` (M3 didn't name the module; it is *not* `/Script/MassEntity.*`). |
| 7 | Task 3: "Plane scaled ×2560 (→ 256 m × 256 m)" | ×2560 on the 100 cm engine plane = **2560 m**. Honored the stated 256 m intent: scale ×256, bounds-verified. |
| 8 | §1.4 Python fallbacks (`unreal.LevelEditorSubsystem` etc.) | **Not reachable through MCP**: `ProgrammaticToolset.execute_tool_script` is a sandbox that only imports json/re/math/copy/datetime/time — `import unreal` is impossible. Level creation/save must go through asset/scene tools (see §4). The MCP toolsets cover the same ground. |

Task-by-task corrections already logged in `Docs/agent/progress.md`; the table above is the distilled version.

---

## 3. Deviations from the brief and why

1. **Editor launch order changed.** Brief assumed editor already open. To honor restart batching (§1.6) with the editor closed, all file-side work (Task 0 non-MCP steps + Task 1) was done first, then UBT, then a **single** editor launch; the MCP inventory (Task 0 steps 3–4) ran immediately after that launch. Net editor restarts for the whole run: **1**.
2. **Game module renamed `MassBattle` → `MassBattleGame`** (blocker: UBT exit 8, CS0101/CS0111). UBT 5.8 compiles game + project plugins into ONE "Project" rules assembly and requires each module's Build.cs class name to equal the module name (`RulesAssembly.cs`: "Currently, we expect the user's rules object type name to be the same as the module name"). A plugin module named `MassBattle` therefore cannot coexist with a game module named `MassBattle`. The game module was renamed (5 pre-existing files, `git mv`) because the brief mandates Appendix A plugin files verbatim and the smoke test must test the *plugin* module. **Phase 0 note:** game C++ goes in `Source/MassBattleGame/`; plugin C++ in `Plugins/MassBattle/Source/MassBattle/`.
3. **Removed `MassEntity` + `FunctionalTesting` from `.uproject`** and `MassEntity` from `.uplugin` plugin deps (see §2.4). Build.cs *module* deps unchanged — `MassEntity`, `MassSpawner`, `MassSignals`, `MassCommon` are module names and all resolve.
4. **Level construction route** (§1.4 fallback unavailable): `AssetTools.duplicate("/Engine/Maps/Templates/Template_Default", …)` → `save_assets` → `load_level` → remove 12 template extras → add Ground plane → `save_assets`. Two gotchas encoded here for future agents: (a) duplication leaves the package **dirty** and `load_level` refuses dirty packages — save before load; (b) Template_Default ships 17 actors including a Water-plugin `BuoyancyManager` and subsystem renderers — prune them for deterministic test maps.
5. **EDU header em-dash replaced with `--`.** The brief's verbatim header text contains "—" (U+2014), which violates the brief's own ASCII-only rule for `.txt` data (§1.6); ASCII wins.
6. **`.gitattributes` gained `Data/**/*.txt text eol=lf`.** Repo default `* text=auto` + Windows autocrlf would materialize CRLF on future checkouts and silently break the LF parser contract. Committed by amending the Task 4 commit (local only, unpushed).
7. **MCP inventory written after Task 2** instead of before Task 1 — direct consequence of deviation 1; brief accepted this shape ("surface is project-dependent — your Task 0 inventory is the truth").

---

## 4. Tool quirks discovered (§1/§2 got wrong or omitted — gold for future runs)

1. **No native MCP client in this agent harness.** The ZCode session exposes no `mcp__unreal-mcp__*` tools; everything ran through a curl JSON-RPC helper against `http://127.0.0.1:8000/mcp`: POST `initialize` → capture `Mcp-Session-Id` response header → POST `notifications/initialized` → then `tools/list` / `tools/call` with the `Mcp-Session-Id` header. Keep serial (C3).
2. **`ProgrammaticToolset` contract:** script must define `run()` returning a dict; call tools via `execute_tool("<FullyQualified.Toolset.ToolName>", json_string)` (returns dict with `returnValue`); exceptions abort and are returned as text; **output schemas are not auto-included** in `describe_toolset` reads unless you look — check `outputSchema` to parse results. Import whitelist: json, re, math, copy, datetime, time. No `unreal`.
3. **`LogsToolset.GetLogEntries` defaults `category` to `"LogsToolset"`** — pass `"category": ""` to search the whole session log, else it errors "Log category '…' not found". This is the tool for log-based evidence (used to prove module load).
4. **`SceneTools` has no new_level / save_all_dirty_levels / save_level.** Save levels via `AssetTools.save_assets(["/Game/…/Map"])`; verify cleanliness via `AssetTools.is_dirty`. `load_level` returns null on success — confirm with `get_current_level`.
5. **`DataAssetTools.create(folder_path, asset_name, asset_type={"refPath": "/Script/MassSpawner.MassEntityConfigAsset"})`** creates the Mass config asset. `ObjectTools.search_subclasses(base_class={"refPath":"/Script/Engine.DataAsset"}, class_name="MassEntity")` is how the module was found — use that pattern whenever a class's `/Script/…` home is unknown.
6. **AutomationTestToolset workflow** (worked first try, preferred over console/CLI): `DiscoverTests` (once per session, ~seconds) → `ListTests(nameFilter, tagFilter)` → `RunTests(testNames)` (returns the completed JSON summary synchronously) → `GetTestStatus`/`GetTestResults`. No editor restart needed.
7. **Actor/class references** are `{"refPath": "…"}` soft paths everywhere (e.g. `/Script/Engine.SkyLight`).
8. **`MassDebugVisualizer` auto-respawn:** after deleting the template's `MassVisualizer_0` in Demo01, a `MassDebugVisualizer_0` appeared on reload (MassGameplay's subsystem visualizer spawns into loaded maps when Mass is enabled). It is in Demo01's actor list and absent from T0_Sandbox's — don't be surprised; deleting it again just respawns it on load.
9. **Timeouts:** default per-call curl budget used was ~110 s; `RunTests` for one smoke test returned in well under a second of editor time. Big operations (test discovery) are the slow ones.

## 5. Evidence (verbatim, condensed)

- **UBT Task 2:** `Build.bat MassBattleEditor Win64 Development -Project="E:\Unreal Projects\MassBattle\MassBattle.uproject" -WaitMutex` → `EXIT CODE: 0`, `Result: Succeeded` (16.67 s, 10 actions) after fixes; key lines: `[8/10] Link UnrealEditor-MassBattle.dll`, `[9/10] Link UnrealEditor-MassBattleGame.dll`. (Earlier attempts: exit 8 CS0101, exit 6 missing MassEntity plugin — see progress.md.)
- **Module loaded in editor** (`LogsToolset.GetLogEntries`): `LogModuleManager: InternalLoadLibrary: 'MassBattle' ('E:/Unreal Projects/MassBattle/Plugins/MassBattle/Binaries/Win64/UnrealEditor-MassBattle.dll')` and `LogMassBattle: MassBattle module started`.
- **Smoke test** (`AutomationTestToolset`): `ListTests` → `{"tests":["Battle.Smoke.PluginLoaded"],"total":1}`; `RunTests` → `{"tests":[{"name":"Battle.Smoke.PluginLoaded","state":"Success","duration":0.00977,"errors":[],"warnings":[]}],"passed":1,"failed":0,"skipped":0,"total":1}`.
- **Levels on disk:** `L_MassBattle_Demo01.umap` (36,648 B), `L_MassBattle_T0_Sandbox.umap` (36,712 B); both `is_dirty=false`; editor left on Demo01.
- **Pipeline proof:** create → `exists=true` → class `MassEntityConfigAsset` → delete → `exists=false`.
- **EDU fixture:** all bytes < 128; no CR; trailing LF; 2 unit types × 13 fields.

## 6. Open blockers

None. (Two transient blockers were hit and resolved during Task 2 — see §3.2–3.3 of progress.md and `git log`.)

## 7. Definition of done — checklist

- [x] `git log` shows one commit per completed task (7 total: `5b1bb83` baseline, `1827055` plugin skeleton, `6919535` plugin compiled+loaded, `514d5ca` levels, `cebfc16` EDU fixture (amended, incl. .gitattributes LF rule), `9cca9bc` smoke test green, plus this report's `prephase0: complete`)
- [x] `.uproject` enables: MassGameplay, MassAI, GameplayAbilities, MassBattle — **correction vs brief:** MassEntity & FunctionalTesting don't exist as plugins in 5.8 (core modules), so enabling them is impossible and unnecessary; see §2
- [x] `Plugins/MassBattle` builds clean (UBT exit 0) and loads (log evidence above)
- [x] `Battle.Smoke.PluginLoaded` passes (1 test / 1 pass, state Success — evidence above)
- [x] `L_MassBattle_Demo01` + `L_MassBattle_T0_Sandbox` exist, load, saved (`is_dirty=false` both)
- [x] `Data/EDU/golden_edu_v1.txt` exists, ASCII, LF, 2 unit types
- [x] `Docs/agent/mcp-inventory.md`, `progress.md`, `prephase0-report.md`, `Docs/reference-machine.md`, `Tools/CI/README.md` all present and real
- [x] Editor left open on `L_MassBattle_Demo01`, zero unsaved changes (`get_current_level` + `is_dirty`×2 verified after final load), MCP server running (every call in this report went through it)

## 8. Where Phase 0 should start

1. Read order per brief Appendix B (arch §2–§5, roadmap Phase 0 + T0 gates, findings §4 E1/E4/E5 + §8.2).
2. Plugin C++ home: `Plugins/MassBattle/Source/MassBattle/` (module `MassBattle` — already linked against MassEntity, MassSpawner, MassSignals, MassCommon, GameplayAbilities; add MassSimulation/MassActors/etc. when needed per the Build.cs comment).
3. New test suites go under `Battle.Unit.*`, `Battle.Data.*`, `Battle.Func.*` (roadmap B.0); use the AutomationTestToolset workflow in §4.6 — the `Battle.Smoke.PluginLoaded` test in `MassBattleSmokeTest.cpp` is the template.
4. `UMassEntityConfigAsset` = `/Script/MassSpawner.MassEntityConfigAsset` in 5.8 (§2.6).
5. This workspace's MCP access pattern is documented in `Docs/agent/mcp-inventory.md` + §4.1 above; re-run `list_toolsets` after any editor restart (C2).
