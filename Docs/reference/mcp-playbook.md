# MCP Playbook — Epic Unreal MCP (`ModelContextProtocol` + `AllToolsets`)

| | |
|---|---|
| **Status** | Living reference — canonical home for every MCP operating fact (**one fact, one home**, `Docs/AGENTS.md` §7). |
| **Verified through** | Pre-phase-0 (2026-10-23) + Phase 0 refresh (2026-10-24, editor launch #1: 70 toolsets). |
| **Supersedes** | The MCP sections of the archived briefs (`Docs/phases/pre-phase-0/brief.md` §1.3–§1.5, `Docs/phases/phase-0/brief.md` §2) and the "tool quirks" sections of both handoff reports. |
| **Correction rule** | New quirk discovered mid-run → add it here (§8 ledger) + one progress-log line. Do not bury quirks in phase reports. |

---

## 1. Server & session lifecycle

- HTTP at `http://127.0.0.1:8000/mcp` (project `.mcp.json`; Saved config: `bAutoStartServer=True`, `ServerPortNumber=8000`, `bEnableToolSearch=True`). Auto-starts with the editor; the port is dead while the editor is closed.
- **No native MCP client in the agent harness.** All calls via curl JSON-RPC: POST `initialize` → capture the `Mcp-Session-Id` response header → POST `notifications/initialized` → then `tools/list` / `tools/call` with that header.
- **The session id changes on every editor restart** — re-run `initialize` each time.
- Toolset count varies with enabled plugins: 49 (pre-phase-0) → 70 (Phase 0; Gauntlet was enabled since). **Always re-run `list_toolsets` after any editor restart or plugin change.**
- Editor console commands: `ModelContextProtocol.StartServer [port]`, `StopServer`, `RefreshTools`, `GenerateClientConfig <ClaudeCode|Cursor|VSCode|Gemini|Codex|All>`.

## 2. Tool-call shape (Phase 0 verified)

- `tools/list` exposes exactly **3 meta-tools**: `list_toolsets`, `describe_toolset` (arg `toolset_name`), `call_tool` (args `toolset_name`, `tool_name`, `arguments`).
- Calls go through `call_tool`; tool names WITHOUT the toolset prefix inside `tool_name`; toolset names fully qualified verbatim (`editor_toolset.toolsets.scene.SceneTools`).
- Responses: `result.content[0].text` holds a JSON string `{"returnValue": "<json string>"}` — **parse twice** (returnValue may itself be JSON).
- Some tools require ALL schema properties present even when "optional" (`ListTests` demands `tagFilter` — pass `""`).
- Check `outputSchema` in `describe_toolset` reads — results are not always self-describing.

## 3. Operating rules (the ones that burn you)

1. **Serial only.** All calls execute serially on the game thread. Never issue overlapping/parallel MCP calls — one call, await result, next call. Editor UI freezing during a call is normal.
2. **Discovery walk every fresh session:** `list_toolsets` → `describe_toolset` (read real schemas — never guess parameter names) → `call_tool`. Cache for the session; re-list only after plugin enable / `RefreshTools` / editor restart.
3. **~110 s practical per-call budget.** Long operations (test discovery) are the slow ones; poll `GetTestStatus` instead of holding one request open.
4. **Modal editor dialogs block the server.** If a call hangs forever, a dialog is up — report it rather than spinning.
5. **Save discipline:** MCP edits are not reliably undoable. Save before and after bulk changes and after every milestone (`AssetTools.save_assets`).
6. **Live Coding does not propagate new UFUNCTIONs/toolsets** — new C++ requires a full editor restart (or building with the editor closed). After restarts/rebuilds run `RefreshTools` and re-list.
7. **Restart batching (project prime directive):** UBT cannot run while the editor is open ("Live Coding is active" — even with Live Coding disabled, the editor holds the DLL). The cycle is: all file-side edits → kill editor → UBT → relaunch → re-`list_toolsets` + re-`DiscoverTests`. Editor restarts are the scarcest resource; never restart for a single config tweak.
8. **AgentSkills:** check `AgentSkillToolset.ListSkills` once per project (21 engine-registered skills as of pre-phase-0, none MassBattle-specific). A matching skill's instructions override generic defaults.

## 4. Toolset inventory (relevant to this project)

| Fully-qualified toolset | One-line capability | Project notes |
|---|---|---|
| `AutomationTestToolset.AutomationTestToolset` | DiscoverTests → ListTests(nameFilter, **tagFilter required, "" ok**) → RunTests(testNames[]) → GetTestStatus/GetTestResults | Primary test-evidence channel; RunTests returns the completed summary synchronously (~<1 s for unit tests; DiscoverTests is the slow call). Preferred over console/CLI — no editor restart. |
| `EditorToolset.LogsToolset` | GetLogEntries (regex over session log) | Evidence channel for module loads and ExecBridge output. **Default category is `"LogsToolset"` — pass `"category": ""` for whole-log search.** |
| `EditorToolset.EditorAppToolset` | Camera, selection, content browser, **StartPIE/StopPIE/IsPIERunning**, CaptureViewport, SearchCVars | StartPIE exists (with warmup option) — viable driver for functional tests if the automation path is flaky. **NO arbitrary console-command execution** (SearchCVars is query-only). |
| `editor_toolset.toolsets.scene.SceneTools` | Load level, get current level, find/spawn/remove actors | No new_level/save_level — save via `AssetTools.save_assets`. |
| `editor_toolset.toolsets.asset.AssetTools` | find/load/save/delete/duplicate/move, create_folder, exists, is_dirty | duplicate leaves package dirty; save before load_level. |
| `editor_toolset.toolsets.object.ObjectTools` | list/get/set properties, get_class, search_subclasses | Always list_properties before set_properties. `search_subclasses` is how you find a class's `/Script/…` home. |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | create data assets | `{"refPath": "/Script/MassSpawner.MassEntityConfigAsset"}` proven. |
| `editor_toolset.toolsets.programmatic.ProgrammaticToolset` | sandboxed Python `run()` calling `execute_tool` | Import whitelist: json/re/math/copy/datetime/time. **No `unreal` module.** |
| `ConfigSettingsToolset.ConfigSettingsToolset` | config section inspect/edit | Project convention: direct `.ini` writes instead. |

Full registry: **70 toolsets** at last refresh (Niagara ×8, Sequencer ×8, PCG ×2, GAS ×3, StateTree, UMG, Physics, DataRegistry, GameFeatures, GameplayTags, LiveCoding, Plugin, SemanticSearch, …). The surface is project-dependent — your fresh `list_toolsets` output is the truth.

## 5. Daily-recipe quick reference

| Task | Recipe / gotcha |
|------|-----------------|
| Log-based evidence | `LogsToolset.GetLogEntries(category: "", pattern: …)` — whole-log search for `LogMassBattle`, `LogModuleManager` lines. |
| Level save/load | `AssetTools.save_assets(["/Game/…"])`; `load_level` refuses dirty packages — save before load; returns null on success, confirm via `get_current_level`. |
| Actor/class references | `{"refPath": "…"}` soft paths everywhere (e.g. `/Script/Engine.SkyLight`). |
| Mass config asset | `DataAssetTools.create(folder, name, {"refPath": "/Script/MassSpawner.MassEntityConfigAsset"})`. |
| Run tests | `DiscoverTests` (once/session) → `ListTests(nameFilter, tagFilter:"")` → `RunTests([names])` → sync JSON summary → `GetTestResults`. |
| Bulk operations | For 5+ homogeneous ops, ONE `ProgrammaticToolset.execute_tool_script` over N round-trips. Script must define `run()` returning a dict; exceptions abort and return as text. |
| Console commands | **No MCP console-execution toolset exists** (as of Phase 0). `-ExecCmds` AND `UWorld::Exec` do NOT reach `FAutoConsoleCommandWithWorldAndArgs` in editor/editor-cmd. Drive `BattleExec::` functions directly from tests, or run headless where automation parses `-ExecCmds` itself. |

## 6. Level construction (corrected route — the brief's Python fallback is unreachable)

`Docs/phases/pre-phase-0/brief.md` §1.4's `unreal.LevelEditorSubsystem` recipe is **NOT reachable** (sandbox import whitelist). The proven route:

1. `AssetTools.duplicate("/Engine/Maps/Templates/Template_Default", target)` → `save_assets` (duplicate leaves the package dirty; `load_level` refuses dirty packages).
2. `load_level` → prune template extras (Template_Default ships 17 actors incl. a Water-plugin `BuoyancyManager` and subsystem renderers — prune them for deterministic test maps).
3. Add ground (`/Engine/BasicShapes/Plane` — base is **100 cm**, verify scale intent with `get_actor_bounds`; a ×2560 "256 m" instruction actually means 2560 m) → `save_assets` → `is_dirty=false`.

`MassDebugVisualizer_0` auto-respawns in any loaded map when Mass is enabled — harmless; don't fight it.

## 7. Automation surfaces (paths that worked)

- **Preferred:** `AutomationTestToolset` (in-editor, no restart).
- Editor console: `Automation RunTests <Filter>`.
- Headless CLI (editor closed): `UnrealEditor-Cmd.exe "<proj>.uproject" -ExecCmds="Automation RunTests <Filter>; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath="<dir>" -unattended -nopause -nullrhi`.
- Test macros need **one context flag + one filter flag** or the test is invisible: `EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter`.
- `-ReportExportPath` writes `index.json` with UTF-8 BOM — parse `utf-8-sig` (details: `Docs/reference/engine-api-5.8.md` Part B §4.10, Part A §8).
- Test naming per roadmap B.0: `Battle.Unit.*`, `Battle.Func.*`, `Battle.Data.*`, `Battle.Perf.*`, `Battle.Soak.*`, `Battle.Smoke.*`.

## 8. Correction ledger (facts that changed, with dates)

| Was believed (source) | Reality (when verified) |
|---|---|
| `unreal` Python fallbacks work through MCP (pre-phase-0 §1.4) | `ProgrammaticToolset` sandbox forbids `import unreal` (pre-phase-0) |
| Console-command toolset assumed to exist (pre-phase-0 risk #5) | None exists; `SearchCVars` is query-only (Phase 0) |
| 49 toolsets (pre-phase-0) | 70 after Gauntlet enablement (Phase 0); count is plugin-dependent |
| `-ExecCmds` drives console commands | Does not reach `FAutoConsoleCommandWithWorldAndArgs`; automation parses it itself (Phase 0) |
| UBT runs any time | Blocked while editor open; restart cycle required (Phase 0) |
| MCP session persists | Session id changes every editor restart (Phase 0) |
| `ObjectTools.set_properties` takes a values OBJECT | The `values` param must be a JSON-encoded STRING; a passed object silently no-ops with `returnValue: False` (2026-10-29) |
| IMC mappings live in the top-level `Mappings` array | Dead in UE 5.8 (deprecated parallel array); `GetMappings()` reads `DefaultKeyMappings.Mappings` -- reflection writes MUST target `DefaultKeyMappings` (2026-10-29; a write to the dead array loads an empty context at runtime) |
| Runtime CDO edits flow into PIE game-mode instances | They do NOT (verified twice, incl. after an editor restart): native CDO and saved BP-CDO property edits both leave the PIE instance at its serialized defaults -- per-map configuration goes through the editor UI or a saved BP (2026-10-29) |
| `BlueprintTools.get_default_object` resolves any BP path | The Blueprint asset must be LOADED first (`AssetTools.load_asset`), then the object path works (2026-10-29) |
| `EditorAppToolset.CaptureViewport` captures PIE | It renders the EDITOR world from the given pose (editor billboards visible; PIE-only actors absent) -- PIE screenshots are unreachable via this surface (2026-10-29) |
| `AssetTools.write_file` covers arbitrary text assets | Extension-blocked allowlist (.csv/.html/.json/.md/.py/.txt) and the path must be under the allowed roots written ABSOLUTE; write mesh sources (e.g. OBJ) with external file tools, then `StaticMeshTools.import_file` (2026-10-29) |

## 9. Provenance

Merged 2026-10-25 from: `Docs/agent/mcp-inventory.md` (authoritative Phase 0 state) · pre-phase-0 brief §1.3–§1.5 (C1–C10, partially superseded — see §8) · `Docs/phases/phase-0/brief.md` §2 · pre-phase-0 report §4 · phase-0 report §4. Future MCP quirks land **here**, not in phase reports.
