#!/usr/bin/env python3
"""compare_baseline.py -- fail on perf regression beyond tolerance.

Usage:
  compare_baseline.py --baseline Baselines/perf/ref-i7-9700F-1660Ti/gate-unit.json \
                      --candidate new.json [--tolerance 0.25]

Schema (Baselines/perf/README.md):
  { "machine": {...}, "machine_tag": "...", "engineCL": 58902052, "date": "ISO",
    "metrics": { "<name>": {"value": <number|null>, "unit": "s|ms|us",
                             "tolerance": <fraction, optional>} } }

A metric with value null is "pending" and is skipped (reported, not compared).
Regression = candidate > baseline * (1 + max(per-metric tolerance, --tolerance)).
Python 3 stdlib only.
"""

import argparse
import json
import sys


def load_metrics(path):
    with open(path, "r", encoding="ascii") as f:
        doc = json.load(f)
    return doc.get("metrics", {}), doc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--candidate", required=True)
    ap.add_argument("--tolerance", type=float, default=0.25)
    args = ap.parse_args()

    base_metrics, base_doc = load_metrics(args.baseline)
    cand_metrics, cand_doc = load_metrics(args.candidate)

    if base_doc.get("machine_tag") != cand_doc.get("machine_tag"):
        print("[compare] WARNING: machine tags differ (%s vs %s) -- comparison is cross-machine"
              % (base_doc.get("machine_tag"), cand_doc.get("machine_tag")))

    regressions = []
    improvements = []
    pending = []
    missing_baseline = []
    lines = ["%-28s %14s %14s %10s  %s" % ("metric", "baseline", "candidate", "tol", "verdict")]

    for name, cand in sorted(cand_metrics.items()):
        value = cand.get("value")
        if value is None:
            pending.append(name)
            lines.append("%-28s %14s %14s %10s  PENDING" % (name, "-", "-", "-"))
            continue
        base = base_metrics.get(name)
        if base is None or base.get("value") is None:
            missing_baseline.append(name)
            lines.append("%-28s %14s %14.4g %10s  NEW" % (name, "-", value, "-"))
            continue
        tol = max(float(base.get("tolerance", args.tolerance)), 0.0) or args.tolerance
        limit = base["value"] * (1.0 + tol)
        if value > limit:
            verdict = "REGRESSION"
            regressions.append(name)
        elif value < base["value"] * (1.0 - tol):
            verdict = "improved"
            improvements.append(name)
        else:
            verdict = "ok"
        lines.append("%-28s %14.4g %14.4g %10.1f%%  %s"
                     % (name, base["value"], value, tol * 100.0, verdict))

    for name in base_metrics:
        if name not in cand_metrics:
            lines.append("%-28s %14.4g %14s %10s  MISSING-IN-CANDIDATE"
                         % (name, base_metrics[name].get("value"), "-", "-"))

    print("\n".join(lines))
    print("[compare] regressions=%d improvements=%d pending=%d new=%d"
          % (len(regressions), len(improvements), len(pending), len(missing_baseline)))
    if regressions:
        print("[compare] FAIL: %s" % ", ".join(regressions))
        return 1
    print("[compare] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
