# MCP Inventory — MassBattle sessions (pre-phase-0 + Phase 0 refresh 2026-10-24)

- **Server:** Epic official `ModelContextProtocol` plugin + `AllToolsets`, HTTP at `http://127.0.0.1:8000/mcp` (project `.mcp.json`; Saved config has `bAutoStartServer=True`, `ServerPortNumber=8000`, `bEnableToolSearch=True`). Auto-starts with the editor; port is dead while the editor is closed.
- **Phase 0 refresh (2026-10-24, editor launch #1): 70 toolsets** (pre-phase-0 saw 49; the count varies with enabled plugins — Gauntlet was enabled since; always re-run after restarts).
- **Session access from this agent:** no native `mcp__unreal-mcp__*` tools; all calls via curl JSON-RPC: POST `initialize` → capture `Mcp-Session-Id` header → POST `notifications/initialized` → then `tools/call` with the header. Serial, one call at a time.

## Tool-call shape (Phase 0 verified — supersedes the pre-phase-0 notes where they differ)

- `tools/list` exposes exactly **3 meta-tools**: `list_toolsets`, `describe_toolset` (arg `toolset_name`), `call_tool` (args `toolset_name`, `tool_name`, `arguments`).
- Calls go through `call_tool`; tool names WITHOUT the toolset prefix inside `tool_name`.
- Responses: `result.content[0].text` holds a JSON string `{"returnValue": "<json string>"}` — parse twice (returnValue may itself be JSON).
- Some tools require ALL schema properties present even when "optional" (ListTests demands `tagFilter` — pass `""`).

## Toolsets relevant to Phase 0

| Fully-qualified toolset | One-line capability | Phase 0 note |
|---|---|---|
| `AutomationTestToolset.AutomationTestToolset` | DiscoverTests → ListTests(nameFilter, **tagFilter required, "" ok**) → RunTests(testNames[]) → GetTestStatus/GetTestResults | Primary test-evidence channel; RunTests returns the completed summary synchronously (~<1 s for unit tests; DiscoverTests is the slow call). |
| `EditorToolset.LogsToolset` | GetLogEntries (regex over session log) | Evidence channel for module loads. **Default category is "LogsToolset" — pass `"category": ""` for whole-log search.** |
| `EditorToolset.EditorAppToolset` | Camera, selection, content browser, **StartPIE/StopPIE/IsPIERunning**, CaptureViewport, SearchCVars | **StartPIE exists** (with warmup option) — viable driver for functional tests if the automation path is flaky. **NO arbitrary console-command execution tool** (SearchCVars is query-only) — risk §3.2.5 resolved: EXEC_RESULT probes run headless via `UnrealEditor-Cmd -ExecCmds="..."` or from inside tests via `World->Exec`. |
| `editor_toolset.toolsets.scene.SceneTools` | Load level, get current level, find/spawn/remove actors. No new_level/save_level — save via AssetTools.save_assets. | |
| `editor_toolset.toolsets.asset.AssetTools` | find/load/save/delete/duplicate/move, create_folder, exists, is_dirty | duplicate leaves package dirty; save before load_level. |
| `editor_toolset.toolsets.object.ObjectTools` | list/get/set properties, get_class, search_subclasses | Always list_properties before set_properties. |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | create data assets (`/Script/MassSpawner.MassEntityConfigAsset` proven pre-phase-0) | Task 6 uses Battle.BuildT0Configs instead (A5). |
| `editor_toolset.toolsets.programmatic.ProgrammaticToolset` | sandboxed Python `run()` calling `execute_tool` | imports limited to json/re/math/copy/datetime/time; no `unreal`. |
| `ConfigSettingsToolset.ConfigSettingsToolset` | config section inspect/edit | project convention: direct .ini writes. |

Full registry: 70 toolsets (Niagara ×8, Sequencer ×8, PCG ×2, GAS ×3, StateTree, UMG, Physics, DataRegistry, GameFeatures, GameplayTags, LiveCoding, Plugin, SemanticSearch, ...).

## Operating quirks (cumulative)

1. `ProgrammaticToolset` forbids `import unreal` (sandbox). Level creation/save via asset tools only.
2. `LogsToolset.GetLogEntries` default `category` is `"LogsToolset"` — pass `""` for whole-log search.
3. Long calls: ~110 s practical budget; poll `GetTestStatus` instead of holding one request open.
4. **Phase 0:** UBT cannot run while the editor is open ("Live Coding is active" — even with Live Coding disabled, the editor holds the DLL); the cycle is: file edits → kill editor → UBT → relaunch → re-DiscoverTests.
5. **Phase 0:** the MCP session id changes on every editor restart; re-run initialize (helper `/tmp/mcp_init.sh init` in the run session).
6. `MassDebugVisualizer_0` auto-respawns in Demo01 (harmless; absent from T0_Sandbox).
