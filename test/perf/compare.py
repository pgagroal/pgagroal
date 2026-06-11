#!/usr/bin/env python3
#
# Copyright (C) 2025 The pgagroal community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Aggregate and compare pgbench A/B results (base vs PR, measured on the same
# runner). Each input file is one measurement: {workload: {tps, p99_ms}}. Several
# files per side are aggregated by median, then PR is compared to base per
# workload.
#
# Exit codes (the central distinction — see PR #918):
#   0  valid comparison (even if a regression is flagged, in advisory mode)
#   1  valid comparison with a regression AND --fail-on-regression (blocking mode)
#   2  DATA-INTEGRITY failure: missing/empty inputs, missing workload, or a
#      non-positive/None tps or p99 — i.e. we could not actually measure.
#
# "Fail loudly when you cannot measure; be advisory only when you measured an
# apparent regression."
#
# Usage:
#   compare.py --base b1.json [b2.json ...] --pr p1.json [p2.json ...]
#              [--tps-drop PCT] [--p99-rise PCT] [--fail-on-regression]

import argparse
import json
import statistics
import sys

WORKLOADS = ["read_only", "read_write", "connect"]
INTEGRITY_EXIT = 2
REGRESSION_EXIT = 1


class IntegrityError(Exception):
    pass


def load_side(label, paths):
    """Load + validate one side's measurement files. Raises IntegrityError on any
    missing file, malformed JSON, missing workload, or non-positive metric."""
    if not paths:
        raise IntegrityError(f"{label}: no measurement files provided")
    runs = []
    for p in paths:
        try:
            with open(p) as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            raise IntegrityError(f"{label}: cannot read/parse {p}: {e}")
        for wl in WORKLOADS:
            if wl not in data:
                raise IntegrityError(f"{label}: {p} missing workload '{wl}'")
            for metric in ("tps", "p99_ms"):
                v = data[wl].get(metric)
                if not isinstance(v, (int, float)) or isinstance(v, bool):
                    raise IntegrityError(f"{label}: {p} {wl}.{metric} is not numeric ({v!r})")
                if v <= 0:
                    raise IntegrityError(f"{label}: {p} {wl}.{metric} is non-positive ({v})")
        runs.append(data)
    return runs


def median_metric(runs, wl, metric):
    return statistics.median(r[wl][metric] for r in runs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", nargs="+", required=True)
    ap.add_argument("--pr", nargs="+", required=True)
    ap.add_argument("--tps-drop", type=float, default=5.0)
    ap.add_argument("--p99-rise", type=float, default=10.0)
    ap.add_argument("--fail-on-regression", action="store_true")
    args = ap.parse_args()

    try:
        base_runs = load_side("base", args.base)
        pr_runs = load_side("pr", args.pr)
    except IntegrityError as e:
        print(f"PERF INTEGRITY FAILURE: {e}", file=sys.stderr)
        # Also emit a Markdown note so the summary explains the failure.
        print(f"### Performance A/B — could not measure\n\n**Integrity failure:** {e}\n")
        return INTEGRITY_EXIT

    lines = [
        "### Performance A/B — PR vs base (same runner, median of "
        f"{len(base_runs)} base / {len(pr_runs)} PR runs)",
        "",
        "| Workload | TPS (base → PR) | ΔTPS | p99 ms (base → PR) | Δp99 | |",
        "|---|---|---|---|---|:--:|",
    ]
    flagged = False
    for wl in WORKLOADS:
        bt = median_metric(base_runs, wl, "tps")
        pt = median_metric(pr_runs, wl, "tps")
        bp = median_metric(base_runs, wl, "p99_ms")
        pp = median_metric(pr_runs, wl, "p99_ms")
        td = (pt - bt) / bt * 100.0
        pd = (pp - bp) / bp * 100.0
        row_flag = td < -args.tps_drop or pd > args.p99_rise
        flagged = flagged or row_flag
        mark = "⚠️" if row_flag else "✅"
        lines.append(
            f"| {wl} | {bt:.0f} → {pt:.0f} | {td:+.1f}% "
            f"| {bp:.2f} → {pp:.2f} | {pd:+.1f}% | {mark} |"
        )

    lines.append("")
    if flagged:
        lines.append(f"⚠️ **Apparent regression** (TPS drop > {args.tps_drop:g}% or p99 rise > {args.p99_rise:g}%).")
    else:
        lines.append(f"✅ Within thresholds (TPS drop > {args.tps_drop:g}%, p99 rise > {args.p99_rise:g}%).")
    mode = "blocking" if args.fail_on_regression else "advisory (informational while variance is calibrated)"
    lines.append(f"\n_Mode: {mode}. Measured PR-vs-base back-to-back on the same runner._")

    print("\n".join(lines))

    if flagged and args.fail_on_regression:
        return REGRESSION_EXIT
    return 0


if __name__ == "__main__":
    sys.exit(main())
