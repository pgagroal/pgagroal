#!/bin/bash
#
# Copyright (C) 2026 The pgagroal community
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
# Tests of bench.sh. Main rule: if it can't measure, it fails, stores
# nothing and leaves no pgagroal running.
#
# Part 1: no database needed.
# Part 2: your own PostgreSQL (PGHOST, PGPORT, PGUSER; database
#         pgagroal_bench, whose data is replaced). Skipped if not reachable.
# Part 3: PostgreSQL in a container. Needs podman or docker and the image
#         from test/check.sh setup. Skipped otherwise.
#
# Runs that must succeed use only 2 clients: we test the harness here, not
# pgagroal under load.
#
# Run: benchmarks/test_bench.sh   (bash 3.2 compatible)

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH="$HERE/bench.sh"

PORT=2399
PIDFILE=/tmp/pgagroal-bench/pgagroal.pid

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export BENCH_RESULTS_DIR="$TMP/results"
export BENCH_PORT="$PORT"
export BENCH_WARMUP=1

failures=0
OUT=""
RC=0

check() {
   if eval "$2"; then
      echo "ok    $1"
   else
      echo "FAIL  $1"
      echo "$OUT" | sed 's/^/      | /'
      failures=$((failures + 1))
   fi
}

bench() {
   OUT="$("$BENCH" "$@" 2>&1)"
   RC=$?
}

# A failed run stores nothing, not even a .partial directory.
no_results() {
   [[ -z "$(find "$BENCH_RESULTS_DIR" -mindepth 2 2>/dev/null)" ]]
}

# Stopped: main is gone and nothing listens on the port (not even a worker).
pgagroal_stopped() {
   local pid

   [[ -z "$(lsof -t -iTCP:"$PORT" -sTCP:LISTEN 2>/dev/null)" ]] || return 1
   pid="$(cat "$PIDFILE" 2>/dev/null)"
   [[ -z "$pid" ]] || ! kill -0 "$pid" 2>/dev/null
}

# A fake pgbench that fails the measured run (the one with -l).
make_failing_pgbench() {
   local real

   [[ -x "$TMP/bin/pgbench" ]] && return 0
   real="$(command -v pgbench)"
   mkdir -p "$TMP/bin"
   cat >"$TMP/bin/pgbench" <<EOF
#!/bin/bash
for a in "\$@"; do
   [[ "\$a" == "-l" ]] && { echo "stub pgbench: simulated failure" >&2; exit 1; }
done
exec "$real" "\$@"
EOF
   chmod +x "$TMP/bin/pgbench"
}

echo "== part 1: arguments, cases, compare (no server needed)"

bench
check "no subcommand: usage, exit 0" '[[ $RC -eq 0 && "$OUT" == *"Usage:"* ]]'
bench frobnicate
check "unknown subcommand: exit 1" '[[ $RC -eq 1 && "$OUT" == *"unknown subcommand"* ]]'
bench run --nope
check "unknown option: exit 1" '[[ $RC -eq 1 && "$OUT" == *"unknown option"* ]]'
bench run -c
check "option without value: exit 1" '[[ $RC -eq 1 && "$OUT" == *"needs a value"* ]]'
for opt in -i -s -d --clients; do
   bench run "$opt" 0
   check "run $opt 0: rejected" '[[ $RC -eq 1 && "$OUT" == *"positive integer"* ]]'
done
OUT="$(BENCH_EV_BACKEND=select "$BENCH" run 2>&1)"
RC=$?
check "unknown event backend: rejected" '[[ $RC -eq 1 && "$OUT" == *"BENCH_EV_BACKEND"* ]]'
bench run -c no_such_case
check "unknown case: rejected" '[[ $RC -eq 1 && "$OUT" == *"no such case"* ]]'
OUT="$(BENCH_POSTGRES=container CONTAINER_ENGINE=false "$BENCH" run -c read_only 2>&1)"
RC=$?
check "container engine unusable: rejected with a hint" '[[ $RC -eq 1 && "$OUT" == *"CONTAINER_ENGINE"* ]]'
OUT="$(BENCH_POSTGRES=elsewhere "$BENCH" run 2>&1)"
RC=$?
check "unknown BENCH_POSTGRES: rejected" '[[ $RC -eq 1 && "$OUT" == *"BENCH_POSTGRES"* ]]'
check "argument errors store nothing" 'no_results'

bench list
check "list: every case with its description" \
   '[[ $RC -eq 0 && "$OUT" == *"read_only "* && "$OUT" == *"read_write "* ]]'

mkdir -p "$TMP/cases"
printf 'CASE_PGBENCH_ARGS=(-S)\n' >"$TMP/cases/no_description.sh"
OUT="$(BENCH_CASES_DIR="$TMP/cases" "$BENCH" list 2>&1)"
RC=$?
check "case without CASE_DESCRIPTION: rejected" '[[ $RC -ne 0 && "$OUT" == *"CASE_DESCRIPTION is not set"* ]]'
rm -f "$TMP/cases/"*
printf 'CASE_DESCRIPTION=x\n' >"$TMP/cases/no_args.sh"
OUT="$(BENCH_CASES_DIR="$TMP/cases" "$BENCH" list 2>&1)"
RC=$?
check "case without CASE_PGBENCH_ARGS: rejected" '[[ $RC -ne 0 && "$OUT" == *"CASE_PGBENCH_ARGS is not set"* ]]'
rm -f "$TMP/cases/"*
printf 'CASE_DESCRIPTION=x\nCASE_PGBENCH_ARGS=()\n' >"$TMP/cases/bad-name.sh"
OUT="$(BENCH_CASES_DIR="$TMP/cases" "$BENCH" list 2>&1)"
RC=$?
check "case name with '-': rejected" '[[ $RC -ne 0 && "$OUT" == *"may only contain"* ]]'

bench compare onlyone
check "compare with one branch: rejected" '[[ $RC -eq 1 && "$OUT" == *"BASELINE and CANDIDATE"* ]]'
bench compare a b
check "compare without results: rejected" '[[ $RC -eq 1 && "$OUT" == *"no case has results"* ]]'
bench compare a b -c read_only
check "compare one case without results: rejected" '[[ $RC -eq 1 && "$OUT" == *"no result for case"* ]]'
bench compare a b -c no_such_case
check "compare unknown case: rejected" '[[ $RC -eq 1 && "$OUT" == *"no such case"* ]]'
bench compare a b --band 0
check "compare --band 0: rejected" '[[ $RC -eq 1 && "$OUT" == *"--band"* ]]'

echo
echo "== part 2: runs against PostgreSQL (external)"

export BENCH_POSTGRES=external
export PGHOST="${PGHOST:-localhost}"
export PGPORT="${PGPORT:-5432}"
export PGDATABASE="${PGDATABASE:-pgagroal_bench}"

if ! pg_isready -h "$PGHOST" -p "$PGPORT" >/dev/null 2>&1; then
   echo "SKIP: PostgreSQL is not reachable at $PGHOST:$PGPORT"
elif pg_isready -h localhost -p "$PORT" >/dev/null 2>&1; then
   echo "SKIP: port $PORT is in use"
else
   # The first run builds pgagroal.
   OUT="$(PGDATABASE=no_such_database_for_bench "$BENCH" run -i 1 -d 1 -s 1 2>&1)"
   RC=$?
   check "missing database: rejected with a hint" '[[ $RC -eq 1 && "$OUT" == *"createdb no_such_database_for_bench"* ]]'

   OUT="$(PGPORT=1 "$BENCH" run -i 1 -d 1 -s 1 2>&1)"
   RC=$?
   check "PostgreSQL not reachable: rejected" '[[ $RC -eq 1 && "$OUT" == *"not reachable"* ]]'

   OUT="$(BENCH_PORT="$PGPORT" "$BENCH" run -c read_only -i 1 -d 1 -s 1 2>&1)"
   RC=$?
   check "pgagroal port busy: rejected" '[[ $RC -eq 1 && "$OUT" == *"already in use"* ]]'
   check "failed runs store nothing" 'no_results'

   make_failing_pgbench
   # Every case fails; all must still be tried and reported.
   OUT="$(PATH="$TMP/bin:$PATH" "$BENCH" run -i 2 -d 1 -s 1 2>&1)"
   RC=$?
   check "pgbench fails: exit 1 with its output" '[[ $RC -eq 1 && "$OUT" == *"simulated failure"* && "$OUT" == *"no result was stored"* ]]'
   check "pgbench fails: every case is still tried" \
      '[[ "$OUT" == *"failed case(s): connect read_only read_write"* ]]'
   check "pgbench fails: nothing stored" 'no_results'
   check "pgbench fails: pgagroal stopped" 'pgagroal_stopped'

   # Ctrl-C sends SIGINT to the whole process group. set -m gives the run
   # its own group (a background job would ignore SIGINT otherwise).
   set -m
   "$BENCH" run -c read_only -i 1 -d 30 -s 1 --clients 2 >"$TMP/ctrlc.out" 2>&1 &
   pid=$!
   set +m
   for _ in $(seq 1 120); do
      pg_isready -h localhost -p "$PORT" >/dev/null 2>&1 && break
      sleep 0.5
   done
   sleep 3
   # If the run already ended, the checks below would test nothing.
   alive=false
   kill -0 "$pid" 2>/dev/null && alive=true
   start=$(date +%s)
   [[ "$alive" == "true" ]] && kill -INT -- "-$pid"
   wait "$pid"
   RC=$?
   took=$(($(date +%s) - start))
   OUT="$(cat "$TMP/ctrlc.out")"
   check "Ctrl-C: the run was still going when interrupted" '[[ "$alive" == "true" ]]'
   check "Ctrl-C: exits non-zero" '[[ $RC -ne 0 ]]'
   check "Ctrl-C: stops within 15 s (took ${took}s)" '[[ $took -le 15 ]]'
   check "Ctrl-C: nothing stored" 'no_results'
   check "Ctrl-C: pgagroal stopped" 'pgagroal_stopped'

   bench run -c read_only -i 2 -d 2 -s 1 --clients 2
   check "run: succeeds" '[[ $RC -eq 0 && "$OUT" == *"wrote "* ]]'
   run_dir="$(find "$BENCH_RESULTS_DIR" -mindepth 2 -maxdepth 2 -type d -name 'read_only.*' | head -1)"
   check "run: UTC timestamp in the directory name" '[[ "$run_dir" =~ read_only\.[0-9]{8}T[0-9]{6}Z$ ]]'
   check "run: meta.json and one file per iteration" \
      '[[ -f "$run_dir/meta.json" && -f "$run_dir/iter-1.json" && -f "$run_dir/iter-2.json" ]]'
   check "run: pgagroal errors recorded" \
      'grep -Eq "\"pgagroal_errors\": [0-9]+,?$" "$run_dir/meta.json"'
   check "run: no .partial left" '[[ -z "$(find "$BENCH_RESULTS_DIR" -name "*.partial")" ]]'
   check "run: pgagroal stopped" 'pgagroal_stopped'

   branch="$(basename "$(dirname "$run_dir")")"
   bench compare "$branch" "$branch"
   check "compare a run with itself: no change" \
      '[[ $RC -eq 0 && "$(echo "$OUT" | grep -c "^read_only .*no change$")" -eq 2 ]]'
fi

echo
echo "== part 3: PostgreSQL in a container"

PG_CONTAINER=pgagroal-bench-postgresql
ENGINE=()
if [[ -n "${CONTAINER_ENGINE:-}" ]]; then
   read -r -a ENGINE <<<"$CONTAINER_ENGINE"
elif command -v podman >/dev/null 2>&1; then
   ENGINE=(podman)
elif command -v docker >/dev/null 2>&1; then
   ENGINE=(docker)
fi
IMAGE="${BENCH_PG_IMAGE:-pgagroal-test-postgresql17-rocky10}"

container_gone() {
   ! "${ENGINE[@]}" container inspect "$PG_CONTAINER" >/dev/null 2>&1
}

if [[ ${#ENGINE[@]} -eq 0 ]] || ! "${ENGINE[@]}" info >/dev/null 2>&1; then
   echo "SKIP: no working podman or docker"
elif ! "${ENGINE[@]}" image inspect "$IMAGE" >/dev/null 2>&1; then
   echo "SKIP: image $IMAGE not found (test/check.sh setup builds it)"
elif "${ENGINE[@]}" container inspect "$PG_CONTAINER" >/dev/null 2>&1; then
   echo "SKIP: container $PG_CONTAINER exists (a benchmark may be running)"
else
   export BENCH_POSTGRES=container
   unset PGHOST PGPORT PGUSER PGDATABASE

   OUT="$(BENCH_PG_IMAGE=no-such-image "$BENCH" run -c read_only -i 1 -d 1 -s 1 2>&1)"
   RC=$?
   check "container: missing image rejected with a hint" '[[ $RC -eq 1 && "$OUT" == *"test/check.sh setup"* ]]'

   rm -rf "$BENCH_RESULTS_DIR"
   bench run -c read_only -i 2 -d 2 -s 1 --clients 2
   check "container: run succeeds" '[[ $RC -eq 0 && "$OUT" == *"starting PostgreSQL"* && "$OUT" == *"wrote "* ]]'
   run_dir="$(find "$BENCH_RESULTS_DIR" -mindepth 2 -maxdepth 2 -type d -name 'read_only.*' | head -1)"
   check "container: recorded in meta.json" \
      'grep -Fq "\"postgres\": \"container $IMAGE\"" "$run_dir/meta.json"'
   check "container: PostgreSQL 17 from the image" '[[ "$OUT" == *"postgresql=17."* ]]'
   check "container: removed after the run" 'container_gone'
   check "container: pgagroal stopped" 'pgagroal_stopped'

   # A failed run must remove the container too.
   make_failing_pgbench
   OUT="$(PATH="$TMP/bin:$PATH" "$BENCH" run -c read_only -i 1 -d 1 -s 1 2>&1)"
   RC=$?
   check "container: failed run exits 1" '[[ $RC -eq 1 && "$OUT" == *"simulated failure"* ]]'
   check "container: removed after a failed run" 'container_gone'

   set -m
   "$BENCH" run -c read_only -i 1 -d 30 -s 1 --clients 2 >"$TMP/ctrlc2.out" 2>&1 &
   pid=$!
   set +m
   for _ in $(seq 1 240); do
      pg_isready -h localhost -p "$PORT" >/dev/null 2>&1 && break
      sleep 0.5
   done
   sleep 3
   alive=false
   kill -0 "$pid" 2>/dev/null && alive=true
   [[ "$alive" == "true" ]] && kill -INT -- "-$pid"
   wait "$pid"
   RC=$?
   OUT="$(cat "$TMP/ctrlc2.out")"
   check "container: Ctrl-C while measuring" '[[ "$alive" == "true" && $RC -ne 0 ]]'
   check "container: removed after Ctrl-C" 'container_gone'
   check "container: pgagroal stopped after Ctrl-C" 'pgagroal_stopped'
fi

echo
if [[ "$failures" -ne 0 ]]; then
   echo "FAILED: $failures check(s)"
   exit 1
fi
echo "all bench.sh tests passed"
