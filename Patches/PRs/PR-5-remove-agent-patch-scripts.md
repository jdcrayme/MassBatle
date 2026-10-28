# PR-5: Delete the one-shot agent patch scripts under Tools/agent/

| | |
|---|---|
| **Finds** | Audit D-5: 15 development-session scripts committed at the repo's tools root |
| **Blocks on** | Owner repo-policy call only (deleting others' workflow tooling is out of audit scope) |
| **Execution** | Trivial: `git rm -r Tools/agent/` once approved |

## Motivation

`Tools/agent/` holds 15 Python scripts (`patch_audit*.py`, `patch_roadmap*.py`,
`patch_deathpath.py`, `append_death_tests.py`, ...) from past agent sessions. Their shape:

```python
p = r"E:\Unreal Projects\MassBattle\Docs\agent\api-audit-5.8.md"
s = io.open(p, encoding="utf-8").read()
... s.replace(old, new) with assert anchors ...
io.open(p, "w", ...).write(s)
```

They are:

- **One-shot**: string-replace patches against files as they existed at a moment in time; some
  target paths that no longer exist (`Docs/agent/api-audit-5.8.md` moved to
  `Docs/reference/engine-api-5.8.md` long ago)
- **Non-portable**: absolute `E:\` paths, Windows-only
- **Misleading**: a reader auditing "what touches the docs" greps into scripts that LOOK like a
  pipeline but are dead session residue; the git history preserves them perfectly
- **Redundant**: every durable fact they patched is already in the owning reference files
  (AGENTS §4 flow), which is why nothing breaks if they vanish

Git history is the right archive for one-shot patches: `git log -- Tools/agent/` retrieves any
of them verbatim.

## Proposed change

```
git rm -r Tools/agent/
```

plus a one-line note in `Tools/CI/README.md` (or wherever the owner wants the tools-map) that
session-local patch scripts are not committed; the repo's patch channel for reviewed change is
git itself.

## Files

- Delete: `Tools/agent/` (15 files, ~1.6k lines total; no build system references them -- the
  CI batch/python tooling under `Tools/CI/` is separate and untouched)

## Evidence to gather

- `Tools/CI/run_tests.bat` tiers unaffected (nothing in `Tools/CI/` references `Tools/agent/`)
- Jenkins `massbattle-gate` green on the deletion commit (verified by inspection: the Jenkinsfile
  drives `Tools/CI/` only)

## Rollback

`git revert` restores the directory byte-for-byte.
