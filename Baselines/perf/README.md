# Perf baseline storage (R8 / R10)

Versioned baseline JSONs for performance gates. One file per machine tag and
gate; committed to git so regressions are diffable in review.

    Baselines/perf/<machine-tag>/<gate>.json

- `<machine-tag>` — short hardware tag, e.g. `ref-i7-9700F-1660Ti`
  (the reference machine, roadmap R10, spec in `Docs/reference-machine.md`).
- `<gate>` — what produced the numbers: `gate-unit` (per-commit tier wall time),
  `nightly-smoke` (packaged boot + Gauntlet smoke), later `perf-gates` (B.6).

## Schema

```json
{
  "machine_tag": "ref-i7-9700F-1660Ti",
  "machine": {
    "cpu": "Intel Core i7-9700F 8C/8T",
    "ram_gb": 16,
    "gpu": "NVIDIA GeForce GTX 1660 Ti",
    "os": "Windows 10 Home 19045"
  },
  "engineCL": 58902052,
  "date": "2026-10-24T02:00:00",
  "metrics": {
    "smoke_test_duration_s": { "value": 41.2, "unit": "s", "tolerance": 0.5 },
    "entity_spawn_80_us":    { "value": null, "unit": "us", "tolerance": 0.5 }
  }
}
```

Rules:

- Every file **embeds the reference-machine spec** (R10) — a baseline without
  hardware context is meaningless.
- `value: null` means the metric is named but not yet produced (Phase 0 keeps
  the loop exercised before real gates land in Phase 2, T2.9); the compare
  script reports it as PENDING and skips it.
- `tolerance` is a fraction (0.5 = +50% allowed). Per-metric tolerance wins
  over the compare script's `--tolerance` default (0.25).
- Compare with `Tools/CI/compare_baseline.py --baseline <this file>
  --candidate <new file>`. Nonzero exit = regression beyond tolerance.
- Wall-clock metrics on the reference machine are noisy; nightly values are
  recorded in the nightly git commit for history, but only the newest file per
  `<machine-tag>/<gate>` is authoritative for the compare.
