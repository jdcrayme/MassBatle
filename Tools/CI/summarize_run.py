#!/usr/bin/env python3
"""summarize_run.py -- parse an Unreal automation log into last_run.json.

Parses per-test lines of the form (LogAutomationController):
    Test Completed. Result={<state>} Name={<name>} Path={<path>}
and the end-of-run marker (LogAutomationCommandLine / LogGauntlet):
    **** TEST COMPLETE. EXIT CODE: <n> ****

Exit code 0 only if the run completed AND every test state is "Success".
Python 3 stdlib only. ASCII + LF output.
"""

import argparse
import datetime
import json
import re
import socket
import sys

TEST_LINE = re.compile(r"Test Completed\. Result=\{([^}]*)\} Name=\{([^}]*)\} Path=\{([^}]*)\}")
DONE_LINE = re.compile(r"\*{4} TEST COMPLETE\. EXIT CODE: (-?\d+) \*{4}")

MACHINE_TAG = "ref-i7-9700F-1660Ti"
MACHINE_SPEC = {
    "cpu": "Intel Core i7-9700F 8C/8T",
    "ram_gb": 16,
    "gpu": "NVIDIA GeForce GTX 1660 Ti",
    "os": "Windows 10 Home 19045",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tier", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--report", default=None, help="optional -ReportExportPath index.json")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    try:
        with open(args.log, "r", encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    except OSError as e:
        print("[summarize] FATAL: cannot read log: %s" % e)
        return 2

    tests = []
    for m in TEST_LINE.finditer(log_text):
        state, name, path = m.group(1).strip(), m.group(2).strip(), m.group(3).strip()
        tests.append({"name": name, "state": state, "path": path})

    done = DONE_LINE.search(log_text)
    completed = done is not None
    exit_code = int(done.group(1)) if done else None

    failed = [t for t in tests if t["state"] != "Success"]
    passed = [t for t in tests if t["state"] == "Success"]

    # -ReportExportPath index.json is the authoritative machine-readable summary
    # when present (UE 5.8 renamed the arg from -ReportOutputPath). The file has
    # top-level counts (succeeded/failed/notRun/succeededWithWarnings) and a
    # UTF-8 BOM; per-test rows come from the log parse.
    report_used = False
    report_counts = None
    if args.report:
        try:
            with open(args.report, "r", encoding="utf-8-sig") as f:
                report = json.load(f)
            report_counts = {k: report.get(k) for k in
                             ("succeeded", "succeededWithWarnings", "failed", "notRun", "totalDuration")}
            entries = report.get("Tests") or []
            known = {t["name"] for t in tests}
            for e in entries:
                if not isinstance(e, dict):
                    continue
                name = e.get("TestDisplayName") or e.get("Name")
                if name and name not in known:
                    tests.append({"name": name, "state": str(e.get("State", "Unknown")), "path": e.get("FullTestPath", "")})
                    if tests[-1]["state"] != "Success":
                        failed.append(tests[-1])
                    else:
                        passed.append(tests[-1])
            report_used = True
        except (OSError, ValueError):
            print("[summarize] note: report file unreadable, relying on log parse")

    ok = completed and exit_code == 0 and len(tests) > 0 and not failed
    if report_counts is not None:
        # cross-check: log-parse pass count vs engine-reported succeeded count
        if report_counts.get("succeeded") == len(passed) and (report_counts.get("failed") or 0) == len(failed):
            print("[summarize] report counts agree with log parse (succeeded=%s failed=%s)"
                  % (report_counts.get("succeeded"), report_counts.get("failed")))
        else:
            # Audit L5: a Success-in-log / Fail-in-report discrepancy used to
            # only warn -- the disagreement is itself a failure.
            print("[summarize] ERROR: report counts %s disagree with log parse passed=%d failed=%d"
                  % (report_counts, len(passed), len(failed)))
            ok = False

    summary = {
        "tier": args.tier,
        "date": datetime.datetime.now().isoformat(timespec="seconds"),
        "host": socket.gethostname(),
        "machine_tag": MACHINE_TAG,
        "machine": MACHINE_SPEC,
        "engine": "UE 5.8.4 CL 58902052",
        "completed": completed,
        "exit_code": exit_code,
        "passed": len(passed),
        "failed": len(failed),
        "total": len(tests),
        "report_used": report_used,
        "report_counts": report_counts,
        "failures": [{"name": t["name"], "state": t["state"]} for t in failed],
        "ok": ok,
    }
    with open(args.out, "w", encoding="ascii", newline="\n") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")

    print("[summarize] tier=%s passed=%d failed=%d total=%d completed=%s exit=%s ok=%s"
          % (args.tier, len(passed), len(failed), len(tests), completed, exit_code, ok))
    for t in failed:
        print("[summarize]   FAILED: %s (%s)" % (t["name"], t["state"]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
