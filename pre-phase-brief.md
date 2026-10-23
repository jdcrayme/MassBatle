# MassBattle — Pre-Phase 0 Environment Setup Brief

**Version:** 1.0 · 2026-10-23
**Consumer:** autonomous coding agent with file-system + shell access, connected to the live Unreal Editor via Epic's official **Unreal MCP** (plugin `ModelContextProtocol`, toolsets via `AllToolsets`).
**Mission:** stand up the MassBattle plugin skeleton and the project scaffolding for the demo and automated-testing levels. Then **stop**. Phase 0 implementation (fragments, processors, registry logic, GAS attributes) is a later brief.
**Spec sources (read-only, never edit):** `Docs/tw-battle-system-architecture.md` (arch), `Docs/tw-battle-system-roadmap-and-testing.md` (roadmap), `Docs/tw-battle-system-plan-review-findings.md` (findings).

---

## 0. How to use this brief (read first)

1. Read **§1 (facts digest)** — it replaces web research. Do **not** search the web; these facts were verified against UE 5.8 official docs on 2026-10-23. If reality contradicts §1, record the discrepancy in the report and trust reality.
2. Execute **Tasks 0–6 in order**. Each task has: *Goal → Steps → Acceptance check → Commit*. Do not claim a task done without running its acceptance check.
3. All `<Project>` placeholders resolve in Task 0 (read the `.uproject` filename and game module name). Substitute everywhere.
4. Token discipline: **do not read the three spec docs end-to-end.** They total ~50k tokens. Open only the sections cited in a task. Everything you need for pre-phase-0 is in this brief.
5. Artifacts you produce live in `Docs/agent/` (create it in Task 0): `progress.md` (running log), `mcp-inventory.md`, `prephase0-report.md`. These are the institutional memory for every future agent run — write them well.

---

## 1. Environment facts digest (pre-verified — do not re-research)

### 1.1 Engine & project

| Fact | Value |
|------|-------|
| Engine pin | **UE 5.8.x** (ADR-13; terminal UE5 release). Re-verify build number in Task 0. |
| Project state | Empty C++-capable project, editor open, MCP server running at `http://127.0.0.1:8000/mcp` |
| MCP stack | Epic official: `ModelContextProtocol` plugin + `AllToolsets` plugin (30+ toolsets, hundreds of tools) |
| Units / axes | Centimeters, Z-up, X-forward. Rotations in degrees. Package paths `/Game/...`, engine shapes `/Engine/BasicShapes/Plane|Cube|Sphere` |
| VCS | None yet — you create git in Task 0 (no LFS; no heavy assets exist) |

### 1.2 UE 5.8 Mass API facts (verified against Epic API docs / release notes, 2026-10-23)

| # | Fact | Consequence |
|---|------|-------------|
| M1 | `UMassSpawnerSubsystem` lives in module **`MassSpawner`**, plugin **MassGameplay** (`/Engine/Plugins/Runtime/MassGameplay/Source/MassSpawner/`) | `MassSpawner` goes in Build.cs deps |
| M2 | 5.8 spawn surface is **template-based**: `SpawnEntities(const FMassEntityTemplate&, uint32, TArray<FMassEntityHandle>&)` and `SpawnEntities(FMassEntityTemplateID, uint32, FConstStructView, TSubclassOf<UMassProcessor>, TArray<FMassEntityHandle>&)`. **`SpawnEntitiesFromConfig` no longer exists.** | Any pre-5.8 sample code using config-named spawning is wrong (findings E4) |
| M3 | Entity templates resolve via `FMassEntityTemplateRegistry` (`GetTemplateRegistryInstance()` / `GetMutableTemplateRegistryInstance()`); `UMassEntityConfigAsset` resolves to a template handle | Phase 0 concern; noted so you don't "fix" docs incorrectly |
| M4 | **Mass Signals is a core engine module (`Runtime/MassSignals`) in 5.8** — no plugin to enable | Build.cs dep `"MassSignals"` only |
| M5 | 5.8 has **sparse/virtual fragments** (add/remove optional fragments+tags without archetype changes) | Roadmap open decision 7 (Phase 0 decides `FDeadTag` strategy). No action now |
| M6 | 5.8 split out **MassCore**, allows off-game-thread entity creation, overhauled processor execution/dependency resolution | Nondeterminism sources under test → why T0.9 exists. No action now |
| M7 | Batch element API renamed: `AddElementToEntities` → `BatchAddElementToEntities`; command-buffer element commands unified | Old community snippets are stale — prefer engine docs over MassSample snippets for 5.8 |
| M8 | Shared fragments dedupe **by content hash** (`GetOrCreateSharedFragmentByHash`) | ADR-14: per-unit shared fragments must embed `FUnitHandle` + handle-seeded hash. Phase 0 code concern; never copy shared-fragment patterns that rely on value-identity for unique-per-unit data |

### 1.3 Epic Unreal MCP operating facts

| # | Fact |
|---|------|
| C1 | Tool-search mode: `tools/list` exposes 3 meta-tools — `list_toolsets`, `describe_toolset`, `call_tool`. Real tools are reached through them. Use **fully-qualified toolset names** verbatim (`editor_toolset.toolsets.scene.SceneTools`) as `toolset_name`, and the **short tool name** (`find_actors`) inside `call_tool`. |
| C2 | Discovery walk every fresh session: `list_toolsets` → `describe_toolset` (read real schemas — never guess parameter names) → `call_tool`. Cache for the session; re-list only after plugin enable / `ModelContextProtocol.RefreshTools` / editor restart. |
| C3 | All tool calls execute **serially on the game thread**. **Never issue overlapping/parallel MCP calls** — one call, await result, next call. Editor UI freezing during a call is normal. |
| C4 | For 5+ homogeneous operations (bulk folder creation, multi-actor spawn), prefer ONE `ProgrammaticToolset.execute_tool_script` call (Python executed server-side) over N round-trips. |
| C5 | Relevant toolsets expected: Scene, Actor, MaterialInstance, Object, **Testing** (automation runs), **Programmatic**, AgentSkill. Surface is project-dependent — your Task 0 inventory is the truth. |
| C6 | Editor console commands: `ModelContextProtocol.StartServer [port]`, `StopServer`, `RefreshTools`, `GenerateClientConfig <ClaudeCode|Cursor|VSCode|Gemini|Codex|All>`. |
| C7 | **Live Coding does not propagate new UFUNCTIONs/toolsets** — new C++ requires a full editor restart (or building with the editor closed). After restarts/rebuilds run `RefreshTools` and re-list. |
| C8 | MCP edits are not reliably undoable. **Save before and after bulk changes and after every milestone** (`LevelEditorSubsystem.save_all_dirty_levels()`). |
| C9 | Modal editor dialogs block the server. If a call hangs forever, a dialog is up — report it rather than spinning. Default per-call timeouts are ~120 s; big saves can approach it. |
| C10 | Project-registered agent skills may exist: `AgentSkillToolset.ListSkills` — check once in Task 0; a matching skill's instructions override generic defaults. |

### 1.4 Python editor-scripting fallbacks (work regardless of toolset gaps)

```python
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.new_level("/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01", False)  # (asset_path, is_partitioned_world)
les.load_level("/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01")
les.save_all_dirty_levels()
# world access:
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
# content folders:
unreal.EditorAssetLibrary.make_directory("/Game/MassBattle/Test/Maps")
unreal.EditorAssetLibrary.does_asset_exist("/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01")
```
`EditorLevelLibrary` is deprecated — use the subsystems above.

### 1.5 Automation facts

| # | Fact |
|---|------|
| A1 | Test macros need **one context flag + one filter flag** or the test is invisible: `EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter`. |
| A2 | Editor run: Tools → Test Automation (Sessions Frontend), or console `Automation RunTests <Filter>`. The MCP **Testing** toolset likely wraps this — `describe_toolset` to find the exact tool; that is the preferred path (no editor restart). |
| A3 | CLI/headless: `UnrealEditor-Cmd.exe "<proj>.uproject" -ExecCmds="Automation RunTests <Filter>; Quit" -ReportExportPath="<dir>" -unattended -nopause -nullrhi`. 5.8 docs also show the form `Automation RunTest` — if one verb errors, use the other. |
| A4 | Roadmap naming convention (arch §B.0): tests live under `Battle.*`. This scaffold adds `Battle.Smoke.*`; Phase 0 adds `Battle.Unit.*`, `Battle.Data.*`, `Battle.Func.*`. |

### 1.6 Known traps

- **Serial calls only** (C3) — parallel MCP calls deadlock or fail. This overrides any general "batch tool calls" instinct.
- **Restart batching**: plugin-enable (.uproject) + new plugin source must ALL be in place before the single restart/compile in Task 2. Do not restart per change.
- **UBT vs editor**: build with editor closed (or Live Coding off) when adding a new module — a brand-new plugin module is not Live-Codable into a running editor.
- `IMPLEMENT_SIMPLE_AUTOMATION_TEST` name is a dotted path = filter namespace: `Battle.Smoke.PluginLoaded`.
- Keep `.txt`/`.md` data files ASCII-only; EDU files in `Data/EDU/` are non-asset files (Content is for .uasset/.umap only).

---

## 2. Operating protocol

**Division of labor.** File edits (.uplugin, .Build.cs, .cpp, .uproject, .ini, .txt, .gitignore) → direct file writes via your file tools. Anything requiring editor state (levels, assets, plugin loading, automation runs, screenshots) → MCP calls (or Python via `ProgrammaticToolset` / console). Anything requiring the editor to be closed (UBT build of a new module) → shell.

**After every completed step:** append one line to `Docs/agent/progress.md` (`[task.step] what was done → verification result`). Commit after every task per the commit plan. Never claim done without the acceptance check.

**Failure policy.** Retry a failed step up to 3 times with diagnosis between attempts (read the returned error/logs — most failures are schema mismatches, fixable by re-running `describe_toolset`). Still failing → record the blocker in `progress.md` with full error text, move to the next independent task, and list all blockers in the final report. If a step corrupts editor state, `git checkout -- <files>` for text assets and report; .umap/.uasset damage → delete and recreate the asset. Do not edit spec docs in `Docs/` to make reality fit.

---

## 3. Tasks

### Task 0 — Capability probe & VCS baseline

**Goal:** know the exact environment; make all subsequent work diffable and recoverable; satisfy roadmap R10's machine-record requirement cheaply.

**Steps:**
1. Identify `<Project>`: read the `.uproject` at the project root (name + `"Modules"` array → game module name). Record OS and the engine install path (probe common locations or `UnrealEditor` process command line; the UBT batch file is `<Engine>/Engine/Build/BatchFiles/Build.bat` on Windows, `Build.sh` elsewhere).
2. Confirm engine version is 5.8.x (read the engine's `Engine/Build/Build.version`). If it is **not** 5.8.x — STOP Task 1+ and record a blocker; §1.2 facts assume 5.8.
3. MCP inventory: `list_toolsets` → for each toolset relevant to this brief (Scene, Actor, Object/Asset, Testing, Programmatic, AgentSkill + anything named *Level*, *Editor*, *Blueprint*), record the fully-qualified name and a one-line capability summary (no need to describe every tool — `describe_toolset` again when needed). Write `Docs/agent/mcp-inventory.md` with the table + server URL + tool-search-mode confirmation.
4. Check `AgentSkillToolset.ListSkills` (C10) and note any project skills in the inventory.
5. Record the dev machine spec (CPU model/cores, RAM, GPU, OS) into `Docs/reference-machine.md` — this is the named reference machine for all future perf baselines [roadmap R10].
6. Create `Docs/agent/progress.md`. Then VCS: write `.gitignore` (standard UE: `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`, `.vs/`, `.vscode/`, `*.sln`, `*.VC.db`, `.idea/`, `Build/` inside plugin `Intermediate` etc.) and `git init && git add -A && git commit -m "prephase0: baseline (empty project + docs)"`.

**Acceptance:** `mcp-inventory.md` lists ≥ 5 toolsets with qualified names; `Build.version` says 5.8.x; git log shows the baseline commit; `reference-machine.md` filled.

### Task 1 — Plugin skeleton + plugin enablement (file-side, no restart yet)

**Goal:** all text changes that require an editor restart or recompile land in ONE batch (trap list §1.6).

**Steps:**
1. Create plugin files exactly as in **Appendix A** (`.uplugin`, `Build.cs`, module `.h/.cpp`, smoke test `.cpp`).
2. Edit `<Project>.uproject`: add to `"Plugins"` (keep existing entries, e.g. the MCP plugins):
   ```json
   { "Name": "MassEntity", "Enabled": true },
   { "Name": "MassGameplay", "Enabled": true },
   { "Name": "MassAI", "Enabled": true },
   { "Name": "GameplayAbilities", "Enabled": true },
   { "Name": "FunctionalTesting", "Enabled": true },
   { "Name": "MassBattle", "Enabled": true }
   ```
3. Sanity-scan your own output: JSON validity of both files, module name `MassBattle` consistent in all four places (uplugin, Build.cs target rules, .uproject, file names).

**Acceptance:** files on disk; JSON parses (lint it); `.gitignore` already excludes `Binaries/` + `Intermediate/` so the commit stays clean. Commit: `prephase0: MassBattle plugin skeleton + engine plugin enablement`.

### Task 2 — Restart, compile, verify pipeline

**Goal:** the plugin compiles and loads; the editor comes back with MCP alive.

**Steps:**
1. Close the editor (shell-terminate gracefully; ensure no unsaved prompts). If the user must do this, ask once and wait.
2. Build via UBT (editor closed):
   `<Engine>/Engine/Build/BatchFiles/Build.bat <Project>Editor Win64 Development -Project="<abs path>/<Project>.uproject" -WaitMutex`
   (Linux/macOS: `Build.sh <Project>Editor <Linux|Mac> Development ...`). Expect exit code 0.
3. Relaunch the editor on the project (shell: start `UnrealEditor.exe "<proj>.uproject"`), wait for full load, then confirm MCP: `list_toolsets` returns again (if the server was set to auto-start it will; else note the manual-start console command for the user in the report).
4. Run `ModelContextProtocol.RefreshTools` if toolsets changed, and re-list.

**Acceptance:** UBT exit 0 (paste the last build lines into `progress.md`); editor relaunches without error dialogs; `list_toolsets` healthy. Commit (if any files changed during generation): `prephase0: plugin compiled and loaded`.

### Task 3 — Content scaffold & levels

**Goal:** the two levels from the task description exist, open clean, and are saved; Mass asset pipeline proven once.

**Steps (prefer one `execute_tool_script` per level):**
1. Create content folders: `/Game/MassBattle/Demo/Maps`, `/Game/MassBattle/Test/Maps`, `/Game/MassBattle/Test/Scratch`.
2. Build **`L_MassBattle_Demo01`** (~256 m flat graybox, no World Partition):
   - `new_level("/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01", False)`
   - Add: SkyLight, DirectionalLight (default intensities fine), SkyAtmosphere, a flat ground = `/Engine/BasicShapes/Plane` scaled ×2560 (→ 256 m × 256 m) at origin, PlayerStart near origin. Nothing else — no terrain, no props.
3. Build **`L_MassBattle_T0_Sandbox`** identically (same minimal set). This is the future home of T0 functional tests (roadmap T0.4/T0.5) — empty and deterministic.
4. `save_all_dirty_levels()`.
5. Pipeline proof (throwaway): create a `UMassEntityConfigAsset` at `/Game/MassBattle/Test/Scratch/MassEntityConfig_Temp` (via the Asset/Create tools or Python `AssetToolsHelpers`), assert `does_asset_exist`, then **delete it**. This proves the Mass asset pipeline end-to-end without committing litter.

**Acceptance:** both maps load (`load_level` returns True) and exist on disk as `.umap`; scratch asset deleted; commit: `prephase0: demo + T0 sandbox levels`.

### Task 4 — Data & config scaffold

**Goal:** the data spine has its fixture; the editor opens where agents will work.

**Steps:**
1. Write `Data/EDU/golden_edu_v1.txt` — ASCII, LF line endings, exactly **2 unit types** in M2TW EDU field syntax (`type`, `dictionary`, `category`, `class`, `soldier`, `attributes`, `formation`, `stat_health`, `stat_pri`, `stat_pri_armour`, `stat_mental`, `stat_charge_dist`, `stat_cost`, `ownership`). Suggested: *Peasant Levy* (40 men, 1 HP, poor) and *Sergeant Spearmen* (40 men, spear reach, armored). Header comment: `// Phase 0 T0.2 fixture draft — validate field semantics against findings refs [27][28] before parser work; unknown fields must be rejected loudly.` Field values are placeholders pending the Phase 6 formula audit (roadmap) — do not overthink them.
2. Edit `Config/DefaultEngine.ini`: under `[/Script/EngineSettings.GameMapsSettings]` set `EditorStartupMap=/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01` and `GameDefaultMap=/Game/MassBattle/Demo/Maps/L_MassBattle_Demo01`. Change nothing else in config.
3. Create `Tools/CI/README.md`: three lines — "Phase 0 deliverable per roadmap R8 (automation host, Gauntlet nightly, baseline JSON storage). Pre-phase-0 records only the reference machine: see Docs/reference-machine.md."

**Acceptance:** EDU file present and ASCII-clean (verify encoding, not just existence); config keys present; commit: `prephase0: EDU fixture, default maps, CI stub`.

### Task 5 — Smoke test run

**Goal:** prove the automation harness end-to-end (per-task scope: "maps + one trivial passing test").

**Steps:**
1. Preferred: via MCP Testing toolset (describe it, then run filter `Battle.Smoke`). Fallback: editor console `Automation RunTests Battle.Smoke`. Last resort: the CLI form from §1.5 A3 (editor closed) — record which path you used.
2. Expect exactly **1 test, 1 pass**: `Battle.Smoke.PluginLoaded`. If the test is not listed → flags problem (A1) or module not loaded (back to Task 2 diagnosis).
3. Capture the result (export JSON via `-ReportExportPath` on CLI runs, or screenshot/copy the result line) into `Docs/agent/prephase0-report.md`.

**Acceptance:** pass recorded with the actual output line(s). Commit if report files changed: `prephase0: smoke test green`.

### Task 6 — Handoff report & stop

**Steps:**
1. Write `Docs/agent/prephase0-report.md`: what was created (paths), tool quirks discovered (anything §1/§2 got wrong — corrections are gold), deviations from the brief and why, open blockers, and the DoD checklist below with real checkboxes filled.
2. Append final status to `progress.md`. `git add -A && git commit -m "prephase0: complete"`.
3. **STOP.** Do not start Phase 0 (no fragments, no processors, no registry logic, no GAS attributes, no EDU parser). The next brief owns that.

---

## 4. Definition of done

- [ ] `git log` shows one commit per completed task (≥ 5 total)
- [ ] `.uproject` enables: MassEntity, MassGameplay, MassAI, GameplayAbilities, FunctionalTesting, MassBattle
- [ ] `Plugins/MassBattle` builds clean (UBT exit 0) and loads
- [ ] `Battle.Smoke.PluginLoaded` passes (evidence captured)
- [ ] `L_MassBattle_Demo01` + `L_MassBattle_T0_Sandbox` exist, load, saved
- [ ] `Data/EDU/golden_edu_v1.txt` exists, ASCII, 2 unit types
- [ ] `Docs/agent/mcp-inventory.md`, `progress.md`, `prephase0-report.md`, `Docs/reference-machine.md`, `Tools/CI/README.md` all present and real
- [ ] Editor left open on `L_MassBattle_Demo01`, zero unsaved changes, MCP server running

## 5. Explicitly out of scope (do not do these)

Any Mass code (fragments/tags/processors/subsystems/spawn logic) · GAS attribute sets or ability code · EDU parser implementation · MassEntityConfig assets committed to the repo (throwaway pipeline proof only) · CI host setup (R8 is Phase 0) · terrain/heightfield art or graybox props beyond the flat plane · World Partition · multiplayer/replication config · **any edit to the three spec docs in `Docs/`** · engine upgrades or plugin version changes.

---

## Appendix A — MassBattle plugin files (exact content)

**`Plugins/MassBattle/MassBattle.uplugin`**
```json
{
        "FileVersion": 3,
        "FriendlyName": "MassBattle",
        "Description": "Total War-style battle simulation on Mass entities + GAS unit actors. See Docs/tw-battle-system-architecture.md.",
        "Category": "Gameplay",
        "CanContainContent": false,
        "Installed": false,
        "Modules": [
                {
                        "Name": "MassBattle",
                        "Type": "Runtime",
                        "LoadingPhase": "Default"
                }
        ],
        "Plugins": [
                { "Name": "MassEntity", "Enabled": true },
                { "Name": "MassGameplay", "Enabled": true }
        ]
}
```

**`Plugins/MassBattle/Source/MassBattle/MassBattle.Build.cs`**
```csharp
using UnrealBuildTool;

public class MassBattle : ModuleRules
{
        public MassBattle(ReadOnlyTargetRules Target) : base(Target)
        {
                PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

                PublicDependencyModuleNames.AddRange(new string[]
                {
                        "Core"
                });

                PrivateDependencyModuleNames.AddRange(new string[]
                {
                        "CoreUObject",
                        "Engine",
                        // Mass stack (5.8): core + spawner + signals + common movement/transforms
                        "MassEntity",
                        "MassSpawner",   // UMassSpawnerSubsystem (module under MassGameplay plugin)
                        "MassSignals",   // core engine module in 5.8
                        "MassCommon",
                        // GAS arrives with Phase 0 unit actors; linking now avoids a config churn later
                        "GameplayAbilities"
                        // Future (add at the phase that needs them — see arch §3/§4):
                        // "MassSimulation", "MassActors", "MassLOD", "MassRepresentation",
                        // "MassNavigation", "MassAvoidance", "MassMovement", "MassAIBehavior", "MassStateTree"
                });
        }
}
```

**`Plugins/MassBattle/Source/MassBattle/Public/MassBattleModule.h`**
```cpp
#pragma once
#include "Modules/ModuleManager.h"

class FMassBattleModule : public IModuleInterface
{
public:
        virtual void StartupModule() override;
        virtual void ShutdownModule() override;
};
```

**`Plugins/MassBattle/Source/MassBattle/Private/MassBattleModule.cpp`**
```cpp
#include "MassBattleModule.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassBattle, Log, All);

void FMassBattleModule::StartupModule()
{
        UE_LOG(LogMassBattle, Log, TEXT("MassBattle module started"));
}

void FMassBattleModule::ShutdownModule()
{
        UE_LOG(LogMassBattle, Log, TEXT("MassBattle module shut down"));
}

IMPLEMENT_MODULE(FMassBattleModule, MassBattle)
```

**`Plugins/MassBattle/Source/MassBattle/Private/MassBattleSmokeTest.cpp`**
```cpp
// Pre-phase-0 scaffold proof: the automation harness works end to end.
// Phase 0 replaces this file's namespace siblings with Battle.Unit / Battle.Data / Battle.Func suites (roadmap B.0).
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMassBattleSmoke_PluginLoaded,
        "Battle.Smoke.PluginLoaded",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FMassBattleSmoke_PluginLoaded::RunTest(const FString& Parameters)
{
        TestTrue(TEXT("MassBattle runtime module is loaded in the editor process"),
                FModuleManager::Get().IsModuleLoaded("MassBattle"));
        return true;
}
```

## Appendix B — Doc map for later phases (do not read now)

| When | Read |
|------|------|
| Phase 0 kick-off | arch §2–§5 (registry, fragments, pipeline, lifecycle), roadmap Phase 0 + T0 gates, findings §4 E1/E4/E5 + §8.2 snippet |
| Movement work | arch §8, roadmap Phase 1, findings R2/R4 |
| Engagement work | arch §6–§7, roadmap Phase 2, findings §7.2 assign pass |
| Morale work | arch §10.5, findings E3 + §8.3 snippet |
