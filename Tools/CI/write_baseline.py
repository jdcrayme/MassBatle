#!/usr/bin/env python3
"""write_baseline.py -- append/update a nightly baseline JSON.

Usage:
  write_baseline.py --gate nightly-smoke --smoke-log <gauntlet log path> \
                    [--spawn-us <number>]

Writes Baselines/perf/<machine-tag>/<gate>.json with the reference-machine spec
embedded (R10). Duration metric is derived from the Gauntlet log's first/last
timestamps; the spawn metric is filled when Task 6 starts producing it (null =
pending until then). Python 3 stdlib only. ASCII + LF output.
"""

import argparse
import datetime
import json
import os
import re
import sys

MACHINE_TAG = "ref-i7-9700F-1660Ti"
MACHINE_SPEC = {
    "cpu": "Intel Core i7-9700F 8C/8T",
    "ram_gb": 16,
    "gpu": "NVIDIA GeForce GTX 1660 Ti",
    "os": "Windows 10 Home 19045",
}

LINE_TS = re.compile(r"^\[(\d+)\.(\d+)\.(\d+)-(\d+)\.(\d+)\.(\d+):(\d{3})\]")


def smoke_duration_seconds(log_path):
    """Wall-clock seconds spanned by the log's frame timestamps (uses the log's
    own first/last [mm] on the machine-local timeline)."""
    first = last = None
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE_TS.match(line)
            if not m:
                continue
            secs = (int(m.group(4)) * 3600.0 + int(m.group(5)) * 60.0
                    + int(m.group(6)) + int(m.group(7)) / 1000.0)
            if first is None:
                first = secs
            last = secs
    if first is None or last is None or last < first:
        return None
    return round(last - first, 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gate", required=True)
    ap.add_argument("--smoke-log", default=None)
    ap.add_argument("--spawn-us", type=float, default=None)
    ap.add_argument("--baselines-dir", default=None,
                    help="defaults to <repo>/Baselines/perf relative to this script")
    ap.add_argument("--out", default=None,
                    help="explicit output path; REQUIRED for nightly candidate "
                         "writes (audit H2: the candidate must never overwrite "
                         "the committed reference before the compare passes)")
    ap.add_argument("--seed-from", default=None,
                    help="seed the candidate's metric set from this reference "
                         "JSON so metrics the nightly does not measure (e.g. "
                         "entity_spawn_80_us) survive promotion")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    base_dir = args.baselines_dir or os.path.join(repo, "Baselines", "perf")
    tag_dir = os.path.join(base_dir, MACHINE_TAG)
    os.makedirs(tag_dir, exist_ok=True)
    out = args.out or os.path.join(tag_dir, "%s.json" % args.gate)
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    doc = None
    seed_path = args.seed_from
    if seed_path and os.path.exists(seed_path):
        try:
            with open(seed_path, "r", encoding="ascii") as f:
                doc = json.load(f)
        except ValueError:
            doc = None
    if doc is None and os.path.exists(out):
        try:
            with open(out, "r", encoding="ascii") as f:
                doc = json.load(f)
        except ValueError:
            doc = None
    if doc is None:
        doc = {
            "machine_tag": MACHINE_TAG,
            "machine": MACHINE_SPEC,
            "engineCL": 58902052,
            "date": datetime.datetime.now().isoformat(timespec="seconds"),
            "metrics": {
                "smoke_test_duration_s": {"value": None, "unit": "s", "tolerance": 0.5},
                "entity_spawn_80_us": {"value": None, "unit": "us", "tolerance": 0.5},
            },
        }

    doc["date"] = datetime.datetime.now().isoformat(timespec="seconds")

    if args.smoke_log:
        dur = smoke_duration_seconds(args.smoke_log)
        if dur is not None:
            doc["metrics"].setdefault(
                "smoke_test_duration_s", {"value": None, "unit": "s", "tolerance": 0.5})
            doc["metrics"]["smoke_test_duration_s"]["value"] = dur
    if args.spawn_us is not None:
        doc["metrics"].setdefault(
            "entity_spawn_80_us", {"value": None, "unit": "us", "tolerance": 0.5})
        doc["metrics"]["entity_spawn_80_us"]["value"] = args.spawn_us

    with open(out, "w", encoding="ascii", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")

    print("[baseline] wrote %s" % out)
    print("[baseline] metrics: %s" % json.dumps(
        {k: v.get("value") for k, v in doc["metrics"].items()}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
