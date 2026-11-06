# Benchmarks

The benchmark harness runs [pgbench](https://www.postgresql.org/docs/current/pgbench.html) through
[**pgagroal**](https://github.com/pgagroal/pgagroal), stores the results per branch and compares two branches.
Use it to show that a change makes pgagroal faster, or at least not slower.

Benchmarks answer *how fast*; tests answer *does it work* (see [TEST.md](./TEST.md)).

## Dependencies

* What you need to build pgagroal (see [DISTRIBUTIONS.md](./DISTRIBUTIONS.md))
* `pgbench`, `psql` and `pg_isready`
* `lsof` and GNU `timeout` (on macOS: `brew install coreutils`)
* Docker or Podman, and the PostgreSQL image of the test suite. Build it once with:

```
<PATH_TO_PGAGROAL>/test/check.sh setup
```

Run the harness as a normal user: pgagroal refuses to run as `root`.

## Running benchmarks

Measure `master`, your branch, then `master` again, and compare:

```
git switch master
<PATH_TO_PGAGROAL>/benchmarks/bench.sh run

git switch my-branch
<PATH_TO_PGAGROAL>/benchmarks/bench.sh run

git switch master
<PATH_TO_PGAGROAL>/benchmarks/bench.sh run

<PATH_TO_PGAGROAL>/benchmarks/bench.sh compare master my-branch
```

For example, `master` against the fix for #1076 on Linux. `master`'s first run was slower than its second,
so `compare` reports `DRIFT`; against the second run there is no change:

```
case         : read_write
runs         : 5 baseline / 5 candidate (median)
build        : Release
machine      : 10 cores, Linux aarch64
ev_backend   : auto
postgresql   : 17.11
baseline     : master @ fe3bcf7 (pgagroal 2.2.0)
candidate    : 1076-fd-ownership @ 22b5603 (pgagroal 2.2.0)

DRIFT: master differs from its own previous run (20261016T032030Z):
  read_write tps 5465.2 -> 6023.9 (1.10x faster)
  read_write p99_ms 8.84 -> 7.25 (1.22x faster)
  Same code, different numbers: differences this small are not a result.

workload              metric      baseline              min-max   candidate              min-max  change
--------------------  --------  ----------  -------------------  ----------  -------------------  ------------
read_write            tps           6023.9        5941.5-6076.6      6079.6        5979.8-6103.2  no change
read_write            p99_ms          7.25            7.15-7.60        7.22            7.07-7.56  no change

Note: 1076-fd-ownership was measured once; measure it again to check for drift.

tps: higher is better. p99_ms: lower is better.
No change: the difference is below 10%, or the min-max ranges overlap.
```

| Command | |
|---|---|
| `run` | build and measure the current branch |
| `compare BASELINE CANDIDATE` | compare the newest results of two branches |
| `list` | show the cases |
| `clean` | remove the build and the temporary files (results are kept) |

| Option | |
|---|---|
| `-c`, `--case NAME` | only this case |
| `-i`, `--iterations N` | measured runs per case, default 5 |
| `-d`, `--duration N` | seconds per run, default 20 |
| `-s`, `--scale N` | `pgbench` scale, default 10 |
| `--clients N` | `pgbench` clients, default 16 |
| `--band PCT` | `compare`: smaller differences are "no change", default 10 |

| Environment variable | |
|---|---|
| `BENCH_POSTGRES` | `container` (default) or `external` (your own PostgreSQL, see below) |
| `CONTAINER_ENGINE` | e.g. `podman` or `sudo docker`; default `podman`, else `docker` |
| `BENCH_PORT` | pgagroal's port, default 2346 |
| `BENCH_PG_PORT` | the container's PostgreSQL port, default 6533 |
| `BENCH_EV_BACKEND` | `auto` (default), `io_uring`, `epoll` or `kqueue` |
| `BENCH_LOG_LEVEL` | pgagroal's log level, default `warn` |
| `BENCH_RESULTS_DIR` | where results go, default `benchmarks/results/` |
| `BENCH_BRANCH`, `BENCH_COMMIT` | store the result under this branch and commit |

## How a run works

1. Builds pgagroal as **Release** into `build-bench/`, with `-Dbenchmark=ON`. A test build adds sanitizers
   and `-O0`, so it would measure those, not pgagroal. The harness checks the real compile flags and refuses
   such a build.
2. Starts PostgreSQL 17 in a container from the test suite's image. It connects as `postgres`, the only user
   the image trusts (pgagroal's pass-through authentication cannot do `scram-sha-256`).
3. For each case, 5 times: fresh `pgbench` data, a new pgagroal, a short warm-up, then the measured run. It
   records TPS and the 99th percentile latency (p99).
4. Stores the results, and removes the container.

`bench.sh` runs pgagroal and `pgbench`. The comparison and a few checks (compile flags, free port, writing
`meta.json`) are done by `pgagroal-bench`, a small C program in `benchmarks/src/` that `-Dbenchmark=ON` builds.

If the harness cannot measure (`pgbench` fails, pgagroal does not start, ...), the case fails and stores
**nothing**. The other cases still run. Logs are in `/tmp/pgagroal-bench/log/`.

## Reading the output

**Only compare runs from the same machine.** The numbers depend on the CPU, the operating system and the load.

Each side shows the median of its runs and the lowest and highest value (min-max). A difference is reported only
when it is **at least 10%** and the two **min-max ranges don't overlap**. Otherwise it is `no change`: the
difference is noise. TPS is higher-is-better, p99 lower-is-better, and `faster` always means the candidate did
better.

**Drift.** Sometimes a whole run is faster or slower, for example because the machine was busy. Then even the
same code looks different. That's why you measure `master` twice: `compare` also compares each branch with its
own previous run. If they differ, it prints `DRIFT`, and a difference of that size between the branches is not
a result.

`compare` also warns when the machines differ, a run was not a Release build, the working tree had changes, or
pgagroal logged errors.

## Results

Results are written to `benchmarks/results/<branch>/<case>.<timestamp>/`: `meta.json` (branch, commit,
machine, settings) and one `iter-N.json` per run. They are **not** committed; paste the `compare` output into
your pull request instead.

## Using your own PostgreSQL

```
BENCH_POSTGRES=external PGHOST=localhost PGPORT=5432 PGUSER=myuser <PATH_TO_PGAGROAL>/benchmarks/bench.sh run
```

The harness uses the database `pgagroal_bench` (or `PGDATABASE`) and **replaces its data**, so create one just for
this (`createdb pgagroal_bench`). Use `trust`, `password` or `md5` authentication for that user.

## Adding a case

Add a file `benchmarks/cases/<name>.sh`; nothing else to change. The name may use letters, digits and `_`.

```
CASE_DESCRIPTION="Select-only transactions with prepared statements"
CASE_PGBENCH_ARGS=(-M prepared -S)
```

`CASE_PGBENCH_ARGS` are the `pgbench` options of the workload; the harness adds the connection, duration and
clients. An optional `CASE_NOTE` is printed under the comparison.

## Platforms

pgagroal mainly targets Linux (`io_uring`, `epoll`), so benchmark on Linux when you can. On macOS and FreeBSD
(`kqueue`) the harness works too, and `compare` notes that the numbers only show the direction of a change.

## Testing the harness

```
benchmarks/test_compare.sh
benchmarks/test_bench.sh
```

`test_compare.sh` checks `compare` with made-up results. `test_bench.sh` skips the parts it can't run (no PostgreSQL of your own, no container engine).

## Limitations

* Benchmarks are run by hand, not in CI: shared CI machines vary too much. (`test/perf` has an on-demand CI
  check that measures a pull request against `master`.)
* `pgbench`, pgagroal and PostgreSQL run on the same machine and share its CPUs.
