# Performance-regression check

An on-demand performance check that detects throughput/latency regressions, run
by `.github/workflows/perf.yml`.

## Triggering a run

The workflow does not run automatically. To benchmark a pull request, go to
**Actions → Performance → Run workflow** and enter the PR number; the PR head
is then measured A/B against the current `master`.

## Principle

**Fail loudly when it cannot measure; be advisory only when it measures an
apparent regression.** An inability to measure (pgbench error/timeout, pgagroal
that won't start, missing/`null`/zero results) is an *error* that fails the job —
never a placeholder result. Only a *valid* comparison whose deltas exceed the
thresholds is informational (while CI variance is being calibrated).

## Why A/B, alternating, on the same runner

CI runs on `ubuntu-latest` — shared runners with 20–50% run-to-run variance — so
an absolute baseline would be flaky. Instead both the PR and its base are built
and measured on the **same runner**, in **alternating order (base, PR, PR, base)**,
**re-initialising the pgbench database before each measured run** (so no run sees
a warmed/mutated DB), with a **warm-up** before each recorded result. Results are
aggregated by **median** per side. There is no static baseline to maintain.

## Workloads

`run_bench.sh` drives three pgbench workloads through pgagroal:
`read_only` (`-S`), `read_write` (TPC-B-like), `connect` (`-C -S`, reconnect per
transaction — stresses the pooler). Each is `timeout`-capped so the job can't hang.

## Pieces

| File | Role |
|------|------|
| `run_bench.sh` | one measurement: warm-up + measured run per workload → JSON; fails on any fault |
| `measure.sh` | start pgagroal from a build dir (non-root), readiness-or-fail-with-log, trap-stop, run `run_bench.sh` |
| `run_ab.sh` | alternating base/PR/PR/base measurement with DB reset before each run |
| `compare.py` | aggregate by median, validate, emit Markdown; integrity → exit 2, regression → advisory (exit 0) or blocking (exit 1 with `--fail-on-regression`) |
| `test_compare.py` | unit tests for `compare.py` (normal, thresholds, zeros, nulls, malformed, bad args, median) |
| `test_run_bench_failure.sh` | failure-path test: a failing pgbench → non-zero exit, no output |
| `../../.github/workflows/perf.yml` | orchestration; publishes to the job summary + uploads results as an artifact |

## Output & rollout

Results are written to **`$GITHUB_STEP_SUMMARY`** and uploaded as the
**`perf-results`** artifact (the per-run JSONs). No PR comment is posted — the
job builds and runs code from the PR head (often a fork), so it is kept on
read-only permissions. If automated PR comments are wanted later, add a
separate trusted `workflow_run` workflow that only reads the `perf-results`
artifact and posts the comment.

Thresholds are **informational** for now (`compare.py` is run without
`--fail-on-regression`). Promote to blocking once several CI runs establish the
expected variance.

## Running the harness tests locally

```sh
python3 test/perf/test_compare.py        # pure-Python, no deps
bash    test/perf/test_run_bench_failure.sh   # skips without timeout(1)
```
