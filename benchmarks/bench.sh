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
# Benchmark pgagroal with pgbench, store the results per branch and compare
# two branches. See doc/BENCHMARK.md.
#
# The comparison and a few checks are done by pgagroal-bench (C, built with
# -Dbenchmark=ON from benchmarks/src/).
#
# start_pgagroal, stop_pgagroal and measure are based on test/perf
# (measure.sh, run_bench.sh). They are copied, not shared, so the CI check in
# test/perf stays untouched.
#
# Keep it working with bash 3.2 (macOS): no mapfile, no associative arrays,
# and write an empty array as ${a[@]+"${a[@]}"} (set -u).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CASES_DIR="${BENCH_CASES_DIR:-$SCRIPT_DIR/cases}"
BUILD_DIR="${BENCH_BUILD_DIR:-$PROJECT_DIR/build-bench}"
BENCH_BIN="$BUILD_DIR/benchmarks/pgagroal-bench"
RESULTS_DIR="${BENCH_RESULTS_DIR:-$SCRIPT_DIR/results}"
ROOT_DIR="/tmp/pgagroal-bench"
LOG_DIR="$ROOT_DIR/log"
CONF_DIR="$ROOT_DIR/conf"
PGA_PIDFILE="$ROOT_DIR/pgagroal.pid"

ITERATIONS="${BENCH_ITERATIONS:-5}"
SCALE="${BENCH_SCALE:-10}"
DURATION="${BENCH_DURATION:-20}"
CLIENTS="${BENCH_CLIENTS:-16}"
WARMUP="${BENCH_WARMUP:-3}"
BAND="${BENCH_BAND:-10}"
PGA_PORT="${BENCH_PORT:-2346}"
PGA_EV_BACKEND="${BENCH_EV_BACKEND:-auto}"
PGA_LOG_LEVEL="${BENCH_LOG_LEVEL:-warn}"
CASE_FILTER=""

# PostgreSQL to benchmark against.
# container (default): started from test/check.sh's image and removed after
#   the run, so every machine uses the same PostgreSQL. We connect as
#   postgres, the only role the image trusts; pgagroal's pass-through
#   authentication cannot do the scram-sha-256 the other roles use.
# external: your own server, from PGHOST, PGPORT, PGUSER, PGDATABASE.
PG_MODE="${BENCH_POSTGRES:-container}"
PG_IMAGE="${BENCH_PG_IMAGE:-pgagroal-test-postgresql17-rocky10}"
PG_CONTAINER="pgagroal-bench-postgresql"
PG_CONTAINER_PORT="${BENCH_PG_PORT:-6533}"
PG_CONTAINER_STARTED=false

if [[ "$PG_MODE" == "container" ]]; then
   export PGHOST=localhost
   export PGPORT="$PG_CONTAINER_PORT"
   export PGUSER=postgres
   export PGDATABASE=pgagroal_bench
   unset PGPASSWORD
else
   export PGHOST="${PGHOST:-localhost}"
   export PGPORT="${PGPORT:-5432}"
   export PGUSER="${PGUSER:-$(id -un)}"
   export PGDATABASE="${PGDATABASE:-pgagroal_bench}"
fi

PGA_RUNNING=false

usage() {
   cat <<EOF
pgagroal benchmarks

Usage:
  $0 run [-c CASE] [-i N] [-s N] [-d N] [--clients N]
                                     Measure the current branch
  $0 compare BASELINE CANDIDATE [-c CASE] [--band PCT]
                                     Compare the newest results of two branches
  $0 list                            Show the available cases
  $0 clean                           Remove the build and runtime state

Options:
  -c, --case NAME        Only this case (default: all)
  -i, --iterations N     Measured iterations per case (default: $ITERATIONS)
  -s, --scale N          pgbench scale of the test database (default: $SCALE)
  -d, --duration N       Seconds per measured pgbench run (default: $DURATION)
      --clients N        pgbench clients (default: $CLIENTS)
      --band PCT         Differences below PCT% are "no change" (default: $BAND)

PostgreSQL runs in a container from test/check.sh's image (build it once
with: test/check.sh setup). Needs podman or docker. To use your own server:
BENCH_POSTGRES=external and PGHOST, PGPORT, PGUSER, PGDATABASE.

More settings (BENCH_PORT, BENCH_EV_BACKEND, ...): doc/BENCHMARK.md

Results: $RESULTS_DIR/<branch>/<case>.<timestamp>/
Only compare runs from the same machine.

Typical use:
  git switch master    && $0 run
  git switch my-branch && $0 run
  git switch master    && $0 run      # again, to check for drift
  $0 compare master my-branch
EOF
}

die() {
   echo "error: $*" >&2
   exit 1
}

is_uint() {
   [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

# Branch names can contain '/'; make them one directory name.
sanitize() {
   printf '%s' "$1" | LC_ALL=C tr -c 'A-Za-z0-9._-' '-'
}

# A case is a file cases/<name>.sh.
case_names() {
   local f
   for f in "$CASES_DIR"/*.sh; do
      [[ -e "$f" ]] || continue
      basename "$f" .sh
   done
}

case_exists() {
   [[ -f "$CASES_DIR/$1.sh" ]]
}

# Load and check a case. It must set CASE_DESCRIPTION and CASE_PGBENCH_ARGS
# (an array); CASE_NOTE is optional and printed by compare. Call it in a
# subshell so one case's variables don't leak into the next.
load_case() {
   local name="$1"

   # Results are <case>.<timestamp>, so no '.' in names.
   [[ "$name" =~ ^[A-Za-z0-9_]+$ ]] || die "case '$name': name may only contain letters, digits and '_'"

   unset CASE_DESCRIPTION CASE_PGBENCH_ARGS CASE_NOTE
   # shellcheck source=/dev/null
   source "$CASES_DIR/$name.sh" || die "case '$name': cannot be sourced"

   [[ -n "${CASE_DESCRIPTION:-}" ]] || die "case '$name': CASE_DESCRIPTION is not set"
   declare -p CASE_PGBENCH_ARGS >/dev/null 2>&1 || die "case '$name': CASE_PGBENCH_ARGS is not set"
}

selected_cases() {
   if [[ -n "$CASE_FILTER" ]]; then
      case_exists "$CASE_FILTER" || die "no such case '$CASE_FILTER' (see: $0 list)"
      echo "$CASE_FILTER"
   else
      case_names
   fi
}

# Newest run of a case on a branch (or, with 2, the one before it).
# Timestamps are UTC, so sorting by name sorts by time.
newest_run() {
   local branch_dir="$RESULTS_DIR/$(sanitize "$1")"
   local case_name="$2"
   local nth="${3:-1}"
   local d newest="" previous=""

   for d in "$branch_dir/$case_name".*/; do
      [[ -d "$d" ]] || continue
      previous="$newest"
      newest="${d%/}"
   done
   if [[ "$nth" -eq 2 ]]; then
      echo "$previous"
   else
      echo "$newest"
   fi
}

# GNU timeout: "timeout" on Linux, "gtimeout" on macOS (Homebrew).
find_timeout() {
   if command -v timeout >/dev/null 2>&1; then
      TIMEOUT=timeout
   elif command -v gtimeout >/dev/null 2>&1; then
      TIMEOUT=gtimeout
   else
      die "timeout(1) not found (on macOS: brew install coreutils)"
   fi
}

preflight() {
   local tool

   [[ "$(id -u)" -ne 0 ]] || die "pgagroal refuses to run as root; run as a normal user"

   for tool in cmake pgbench psql pg_isready lsof; do
      command -v "$tool" >/dev/null 2>&1 || die "$tool not found in PATH"
   done
   find_timeout
   [[ "$PG_MODE" == "container" ]] && find_container_engine

   # getconf works on Linux and macOS (nproc doesn't exist on macOS).
   CPUS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
}

# Branch, commit and whether the tree is dirty (BENCH_BRANCH, BENCH_COMMIT,
# BENCH_DIRTY override them).
git_info() {
   # benchmarks/ and the build dir don't change pgagroal, so they don't make
   # the tree dirty. Other new files do: src/ is collected with FILE(GLOB).
   local excludes=(':(exclude)benchmarks')
   case "$BUILD_DIR" in
      "$PROJECT_DIR"/*) excludes+=(":(exclude)${BUILD_DIR#"$PROJECT_DIR"/}") ;;
   esac

   BRANCH="${BENCH_BRANCH:-$(git -C "$PROJECT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)}"
   COMMIT="${BENCH_COMMIT:-$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)}"
   if [[ -n "${BENCH_DIRTY:-}" ]]; then
      DIRTY="$BENCH_DIRTY"
   elif [[ -n "$(git -C "$PROJECT_DIR" status --porcelain -- . ${excludes[@]+"${excludes[@]}"} 2>/dev/null)" ]]; then
      DIRTY=true
   else
      DIRTY=false
   fi

   [[ "$BRANCH" == "HEAD" ]] && BRANCH="detached-$COMMIT"
   if [[ "$DIRTY" == "true" ]]; then
      echo "warning: working tree is dirty; the result will not match $COMMIT exactly"
   fi
}

find_container_engine() {
   if [[ -n "${CONTAINER_ENGINE:-}" ]]; then
      read -r -a ENGINE <<<"$CONTAINER_ENGINE"
   elif command -v podman >/dev/null 2>&1; then
      ENGINE=(podman)
   elif command -v docker >/dev/null 2>&1; then
      ENGINE=(docker)
   else
      die "neither podman nor docker found (or use BENCH_POSTGRES=external with your own PostgreSQL)"
   fi
}

# Can the container be started? Checked before the build, which takes a while.
check_container() {
   [[ "$PG_MODE" == "container" ]] || return 0

   # On Linux, check.sh uses "sudo docker"; say so instead of "no image".
   "${ENGINE[@]}" info >/dev/null 2>&1 \
      || die "cannot use '${ENGINE[*]}' (no access to its daemon?); set CONTAINER_ENGINE, e.g. CONTAINER_ENGINE=\"sudo docker\""
   "${ENGINE[@]}" image inspect "$PG_IMAGE" >/dev/null 2>&1 \
      || die "PostgreSQL image $PG_IMAGE not found; build it once with: test/check.sh setup"
}

# Start PostgreSQL in a container and create the database. The image first
# runs PostgreSQL briefly for setup, then starts the real server as PID 1;
# wait for that one, not the setup run.
start_postgresql() {
   local ready=false

   [[ "$PG_MODE" == "container" ]] || return 0

   "${ENGINE[@]}" rm -f "$PG_CONTAINER" >/dev/null 2>&1
   port_free "$PG_CONTAINER_PORT" || die "port $PG_CONTAINER_PORT is already in use (set BENCH_PG_PORT)"

   echo "==> starting PostgreSQL ($PG_IMAGE) on port $PG_CONTAINER_PORT"
   # The image requires all these variables (we only use postgres).
   # Its default log level, debug5, would slow PostgreSQL down.
   "${ENGINE[@]}" run -d --name "$PG_CONTAINER" -p "$PG_CONTAINER_PORT:5432" \
      -e PG_DATABASE=bench -e PG_USER_NAME=bench -e PG_USER_PASSWORD=bench \
      -e PG_REPL_USER_NAME=repl -e PG_REPL_PASSWORD=repl \
      -e PG_UTF8_USER_NAME=utf8 -e PG_UTF8_USER_PASSWORD=utf8 -e PG_UTF8_DATABASE=utf8 \
      -e PG_LOG_LEVEL=warning -e PG_MAX_CONNECTIONS=200 \
      "$PG_IMAGE" >"$LOG_DIR/container.log" 2>&1 \
      || { cat "$LOG_DIR/container.log" >&2; die "cannot start the PostgreSQL container"; }
   PG_CONTAINER_STARTED=true

   for _ in $(seq 1 120); do
      if [[ "$("${ENGINE[@]}" exec "$PG_CONTAINER" cat /proc/1/comm 2>/dev/null)" == "postgres" ]] \
         && pg_isready -h localhost -p "$PG_CONTAINER_PORT" >/dev/null 2>&1; then
         ready=true
         break
      fi
      sleep 0.5
   done
   if [[ "$ready" != "true" ]]; then
      "${ENGINE[@]}" logs "$PG_CONTAINER" 2>&1 | tail -n 20 | sed 's/^/  container| /' >&2
      die "PostgreSQL in the container did not become ready"
   fi

   psql -X -q -d postgres -c "CREATE DATABASE pgagroal_bench" >"$LOG_DIR/createdb.log" 2>&1 \
      || { cat "$LOG_DIR/createdb.log" >&2; die "cannot create the database pgagroal_bench"; }
}

stop_postgresql() {
   [[ "$PG_CONTAINER_STARTED" == "true" ]] || return 0
   PG_CONTAINER_STARTED=false
   "${ENGINE[@]}" rm -f "$PG_CONTAINER" >/dev/null 2>&1
}

check_backend() {
   pg_isready -h "$PGHOST" -p "$PGPORT" >/dev/null 2>&1 \
      || die "PostgreSQL is not reachable at $PGHOST:$PGPORT (set PGHOST/PGPORT)"

   PG_VERSION="$(psql -X -At -c 'SHOW server_version' 2>"$LOG_DIR/psql.log")" \
      || die "cannot connect to database '$PGDATABASE' as '$PGUSER' ($(head -1 "$LOG_DIR/psql.log")); create it with: createdb $PGDATABASE"
}

# Build Release in its own directory: the test build adds sanitizers and -O0,
# so it would measure those, not pgagroal. Only the given targets are built.
# Always run cmake: it finds src/ files with FILE(GLOB).
cmake_build() {
   local jobs="${CPUS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

   mkdir -p "$LOG_DIR" || die "cannot create $LOG_DIR"
   cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DDOCS=FALSE -Dbenchmark=ON \
      >"$LOG_DIR/cmake.log" 2>&1 || { tail -n 30 "$LOG_DIR/cmake.log" >&2; die "cmake failed; see $LOG_DIR/cmake.log"; }
   cmake --build "$BUILD_DIR" --target "$@" -j "$jobs" \
      >"$LOG_DIR/build.log" 2>&1 || { tail -n 30 "$LOG_DIR/build.log" >&2; die "build failed; see $LOG_DIR/build.log"; }
}

build() {
   echo "==> building (Release) into $BUILD_DIR"
   cmake_build pgagroal-bin pgagroal-cli-bin pgagroal-bench

   check_build_flags

   # pgagroal -V exits 1, so ignore its status.
   PGAGROAL_VERSION="$("$BUILD_DIR/src/pgagroal" -V 2>/dev/null | awk '{print $2}')"
}

# Check the real compile flags, not only the build type: with Clang the top
# CMakeLists.txt adds -O0 and coverage even to Release.
check_build_flags() {
   BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$BUILD_DIR/CMakeCache.txt")"
   [[ "$BUILD_TYPE" == "Release" ]] || die "$BUILD_DIR is a '$BUILD_TYPE' build, not Release"

   "$BENCH_BIN" check-build "$BUILD_DIR/compile_commands.json" "$PROJECT_DIR/src/" \
      || die "refusing to benchmark an instrumented build"
}

# Fresh pgbench data for every iteration, so no run sees changes from the last.
reset_db() {
   pgbench -i -q -s "$SCALE" >"$LOG_DIR/pgbench-init.log" 2>&1 \
      || { sed 's/^/  | /' "$LOG_DIR/pgbench-init.log" >&2; die "pgbench -i failed"; }
}

pga() {
   LD_LIBRARY_PATH="$BUILD_DIR/src" "$@"
}

fail_with_log() {
   echo "error: $1" >&2
   [[ -s "$LOG_DIR/pgagroal-start.log" ]] && sed 's/^/  start| /' "$LOG_DIR/pgagroal-start.log" >&2
   [[ -s "$PGA_LOG" ]] && sed 's/^/  log| /' "$PGA_LOG" >&2
   exit 1
}

# Start pgagroal for one iteration, with its own log file.
start_pgagroal() {
   PGA_LOG="$LOG_DIR/pgagroal-$1.log"
   mkdir -p "$CONF_DIR"
   cat >"$CONF_DIR/pgagroal.conf" <<EOF
[pgagroal]
host = localhost
port = $PGA_PORT
log_type = file
log_path = $PGA_LOG
log_level = $PGA_LOG_LEVEL
max_connections = 100
pipeline = performance
ev_backend = $PGA_EV_BACKEND
unix_socket_dir = $ROOT_DIR
pidfile = $PGA_PIDFILE

[primary]
host = $PGHOST
port = $PGPORT
EOF
   printf 'host all all all all\n' >"$CONF_DIR/pgagroal_hba.conf"

   port_free || die "port $PGA_PORT is already in use (set BENCH_PORT)"

   # The port is free, so an old PID file is stale (pgagroal refuses to start).
   rm -f "$PGA_LOG" "$PGA_PIDFILE"
   # Errors before pgagroal daemonizes go to stderr, not to its log.
   pga "$BUILD_DIR/src/pgagroal" -c "$CONF_DIR/pgagroal.conf" -a "$CONF_DIR/pgagroal_hba.conf" -d \
      >"$LOG_DIR/pgagroal-start.log" 2>&1 || fail_with_log "pgagroal failed to start"
   PGA_RUNNING=true

   for _ in $(seq 1 30); do
      pg_isready -h localhost -p "$PGA_PORT" >/dev/null 2>&1 && return 0
      sleep 1
   done
   fail_with_log "pgagroal did not become reachable on port $PGA_PORT"
}

# Can the port be bound? pg_isready is not enough: a leftover worker can
# hold the port without answering.
port_free() {
   "$BENCH_BIN" port-free "${1:-$PGA_PORT}"
}

wait_port_free() {
   for _ in $(seq 1 "$1"); do
      port_free && return 0
      sleep 0.5
   done
   return 1
}

# Stop pgagroal and make sure nothing of it is left. We use the PID file
# (pgagroal changes its process title, so pkill -f can't find it).
stop_pgagroal() {
   local pid holders

   [[ "$PGA_RUNNING" == "true" ]] || return 0
   PGA_RUNNING=false

   pid="$(cat "$PGA_PIDFILE" 2>/dev/null)"
   [[ "$pid" =~ ^[0-9]+$ ]] || pid=""

   pga "$BUILD_DIR/src/pgagroal-cli" -c "$CONF_DIR/pgagroal.conf" shutdown >/dev/null 2>&1
   for _ in $(seq 1 20); do
      [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null || break
      sleep 0.5
   done

   # Still running after 10 s: kill it. Stop it first so it can't fork.
   if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      echo "warning: pgagroal did not shut down; killing it (pid $pid)" >&2
      kill -STOP "$pid" 2>/dev/null
      kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null
   fi
   rm -f "$PGA_PIDFILE"

   # Workers have their own process groups and keep the listening socket,
   # so one that outlives main blocks the port. Kill what still listens.
   wait_port_free 10 && return 0
   holders="$(lsof -t -iTCP:"$PGA_PORT" -sTCP:LISTEN 2>/dev/null | sort -u | tr '\n' ' ')"
   if [[ -n "$holders" ]]; then
      echo "warning: pgagroal processes still listen on port $PGA_PORT after the shutdown; killing them (pids $holders)" >&2
      # shellcheck disable=SC2086
      kill -KILL $holders 2>/dev/null
   fi
   wait_port_free 10 || die "port $PGA_PORT is still in use after stopping pgagroal"
}

fail_pgbench() {
   local what="$1" rc="$2" out="$3"

   sed 's/^/  | /' "$out" >&2
   if [[ -s "$PGA_LOG" ]]; then
      echo "  last lines of $PGA_LOG:" >&2
      tail -n 40 "$PGA_LOG" | sed 's/^/  log| /' >&2
   fi
   [[ "$rc" -eq 124 ]] && die "$what timed out"
   die "$what failed (pgbench exit $rc)"
}

# One measurement: a warm-up (not recorded), then the measured run. Writes
# {"<case>": {"tps": ..., "p99_ms": ...}} to <out>. If it can't measure, it
# fails and writes nothing (the rule of test/perf).
measure() {
   local name="$1" out="$2"
   local work="$ROOT_DIR/work"
   local cap=$((DURATION + WARMUP + 60))
   local jobs="$CLIENTS"
   local rc tps p99

   [[ "$CPUS" -lt "$jobs" ]] && jobs="$CPUS"

   rm -rf "$work"
   mkdir -p "$work"

   # --foreground, or Ctrl-C would not reach pgbench.
   "$TIMEOUT" --foreground "$cap" pgbench -h localhost -p "$PGA_PORT" -T "$WARMUP" -c "$CLIENTS" -j "$jobs" \
      ${CASE_PGBENCH_ARGS[@]+"${CASE_PGBENCH_ARGS[@]}"} >"$work/warmup.out" 2>&1
   rc=$?
   [[ "$rc" -eq 0 ]] || fail_pgbench "$name warm-up" "$rc" "$work/warmup.out"

   "$TIMEOUT" --foreground "$cap" pgbench -h localhost -p "$PGA_PORT" -T "$DURATION" -c "$CLIENTS" -j "$jobs" \
      -l --log-prefix="$work/latency" \
      ${CASE_PGBENCH_ARGS[@]+"${CASE_PGBENCH_ARGS[@]}"} >"$work/run.out" 2>&1
   rc=$?
   [[ "$rc" -eq 0 ]] || fail_pgbench "$name measured run" "$rc" "$work/run.out"

   tps="$(grep -oE 'tps = [0-9.]+' "$work/run.out" | head -1 | awk '{print $3}')"
   [[ "$tps" =~ ^[0-9]+(\.[0-9]+)?$ ]] || { sed 's/^/  | /' "$work/run.out" >&2; die "$name produced no TPS"; }

   # Field 3 of pgbench's log is the latency in µs. p99 = value at ceil(0.99 * N).
   ls "$work"/latency.* >/dev/null 2>&1 || die "$name produced no latency log"
   p99="$(cat "$work"/latency.* | awk '{print $3}' | sort -n | awk '
      { a[NR] = $1 }
      END {
         if (NR == 0) exit 1
         i = int(NR * 0.99); if (i < NR * 0.99) i++; if (i < 1) i = 1
         printf "%.3f", a[i] / 1000.0
      }')" || die "$name latency log was empty"

   printf '{"%s": {"tps": %s, "p99_ms": %s}}\n' "$name" "$tps" "$p99" >"$out"
}

write_meta() {
   "$BENCH_BIN" meta "$2" \
      case="$1" branch="$BRANCH" commit="$COMMIT" dirty="$DIRTY" timestamp="$STAMP" \
      build_type="$BUILD_TYPE" os="$(uname -s)" arch="$(uname -m)" cpus="$CPUS" \
      pgagroal_version="$PGAGROAL_VERSION" pg_version="$PG_VERSION" \
      iterations="$ITERATIONS" duration="$DURATION" warmup="$WARMUP" \
      clients="$CLIENTS" scale="$SCALE" pgbench_args="${CASE_PGBENCH_ARGS[*]-}" \
      ev_backend="$PGA_EV_BACKEND" log_level="$PGA_LOG_LEVEL" note="${CASE_NOTE:-}" pgagroal_errors="$PGA_ERRORS" \
      postgres="$PG_DESCRIPTION"
}

# Run all iterations of a case (in a subshell). Results are written to a
# .partial directory and renamed only if every iteration worked.
run_case() {
   local name="$1"
   local branch_dir="$RESULTS_DIR/$(sanitize "$BRANCH")"
   local final="$branch_dir/$name.$STAMP"
   local i errors

   load_case "$name"
   [[ -e "$final" ]] && die "$final already exists"

   # Global: the EXIT trap runs after this function returns.
   RUN_PARTIAL="$branch_dir/.$name.$STAMP.partial"
   trap 'stop_pgagroal; [[ -n "${RUN_PARTIAL:-}" ]] && rm -rf "$RUN_PARTIAL"' EXIT
   mkdir -p "$RUN_PARTIAL" || die "cannot create $RUN_PARTIAL"

   # pgagroal can log errors and still finish the run; count them.
   PGA_ERRORS=0

   echo "==> $name: $CASE_DESCRIPTION"
   for ((i = 1; i <= ITERATIONS; i++)); do
      reset_db
      start_pgagroal "$name-$i"
      measure "$name" "$RUN_PARTIAL/iter-$i.json"
      stop_pgagroal
      errors="$(grep -c ' ERROR ' "$PGA_LOG" 2>/dev/null)"
      [[ "$errors" =~ ^[0-9]+$ ]] || errors=0
      PGA_ERRORS=$((PGA_ERRORS + errors))
      printf '    %d/%d  %s' "$i" "$ITERATIONS" "$(cat "$RUN_PARTIAL/iter-$i.json")"
      [[ "$errors" -gt 0 ]] && printf '  (pgagroal logged %d errors, see %s)' "$errors" "$PGA_LOG"
      echo
   done

   write_meta "$name" "$RUN_PARTIAL/meta.json" || die "cannot write meta.json"
   mv "$RUN_PARTIAL" "$final" || die "cannot move the results to $final"
   RUN_PARTIAL=""
   echo "    wrote $final"
}

do_run() {
   local name cases failed

   is_uint "$ITERATIONS" || die "--iterations must be a positive integer"
   is_uint "$SCALE" || die "--scale must be a positive integer"
   is_uint "$DURATION" || die "--duration must be a positive integer"
   is_uint "$CLIENTS" || die "--clients must be a positive integer"
   is_uint "$WARMUP" || die "BENCH_WARMUP must be a positive integer"
   is_uint "$PGA_PORT" || die "BENCH_PORT must be a positive integer"
   case "$PG_MODE" in
      container | external) ;;
      *) die "BENCH_POSTGRES must be container or external" ;;
   esac
   is_uint "$PG_CONTAINER_PORT" || die "BENCH_PG_PORT must be a positive integer"
   case "$PGA_EV_BACKEND" in
      auto | io_uring | epoll | kqueue) ;;
      *) die "BENCH_EV_BACKEND must be auto, io_uring, epoll or kqueue" ;;
   esac
   # Check the case before the build. (die in $( ) only exits the subshell.)
   cases="$(selected_cases)" || exit 1

   mkdir -p "$LOG_DIR" "$RESULTS_DIR" || die "cannot create $LOG_DIR or $RESULTS_DIR"

   preflight
   git_info
   check_container
   # Build first: start_postgresql needs pgagroal-bench.
   build
   # Remove the container on exit. (Subshells don't run this trap.)
   trap stop_postgresql EXIT
   start_postgresql
   check_backend

   if [[ "$PG_MODE" == "container" ]]; then
      PG_DESCRIPTION="container $PG_IMAGE"
   else
      PG_DESCRIPTION="external"
   fi
   STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
   echo "==> branch=$BRANCH commit=$COMMIT pgagroal=$PGAGROAL_VERSION postgresql=$PG_VERSION"
   echo "    iterations=$ITERATIONS duration=${DURATION}s clients=$CLIENTS scale=$SCALE cpus=$CPUS ev_backend=$PGA_EV_BACKEND"

   # A failed case doesn't stop the others.
   failed=""
   for name in $cases; do
      if ! (run_case "$name"); then
         echo "error: case '$name' failed; no result was stored for it (logs: $LOG_DIR)" >&2
         failed="$failed $name"
      fi
   done
   [[ -z "$failed" ]] || die "failed case(s):$failed"
}

do_list() {
   local name

   for name in $(case_names); do
      (
         load_case "$name"
         printf '%-16s %s\n' "$name" "$CASE_DESCRIPTION"
      ) || exit 1
   done
}

do_compare() {
   local baseline="$1"
   local candidate="$2"
   local name cases base_run cand_run run rc worst=0 compared=0 built=false
   local previous=()

   is_uint "$BAND" || die "--band must be a positive integer"
   cases="$(selected_cases)" || exit 1

   for name in $cases; do
      base_run="$(newest_run "$baseline" "$name")"
      cand_run="$(newest_run "$candidate" "$name")"

      if [[ -z "$base_run" || -z "$cand_run" ]]; then
         # Missing results are an error only for a case asked for with -c.
         [[ -n "$CASE_FILTER" ]] && die "no result for case '$name' on both '$baseline' and '$candidate'"
         continue
      fi

      # Each branch's previous run, for the drift check.
      previous=()
      run="$(newest_run "$baseline" "$name" 2)"
      [[ -n "$run" ]] && previous+=(--baseline-previous "$run")
      run="$(newest_run "$candidate" "$name" 2)"
      [[ -n "$run" ]] && previous+=(--candidate-previous "$run")

      # Build only when there is something to compare.
      if [[ "$built" != "true" ]]; then
         cmake_build pgagroal-bench
         built=true
      fi

      [[ "$compared" -gt 0 ]] && echo
      "$BENCH_BIN" compare "$base_run" "$cand_run" --band "$BAND" ${previous[@]+"${previous[@]}"}
      rc=$?
      [[ "$rc" -gt "$worst" ]] && worst="$rc"
      compared=$((compared + 1))
   done

   [[ "$compared" -gt 0 ]] || die "no case has results on both '$baseline' and '$candidate'"
   return "$worst"
}

do_clean() {
   # Also a container left by a killed run.
   if [[ "$PG_MODE" == "container" ]] && (find_container_engine) 2>/dev/null; then
      find_container_engine
      "${ENGINE[@]}" rm -f "$PG_CONTAINER" >/dev/null 2>&1
   fi
   rm -rf "$BUILD_DIR" "$ROOT_DIR"
   echo "removed $BUILD_DIR and $ROOT_DIR (results kept in $RESULTS_DIR)"
}

# A bare "-c" at the end would make "shift 2" fail and loop forever.
need_value() {
   [[ $# -ge 2 && -n "$2" ]] || die "option $1 needs a value"
}

SUBCOMMAND="${1:-}"
[[ $# -gt 0 ]] && shift

POSITIONAL=()
while [[ $# -gt 0 ]]; do
   case "$1" in
      -c | --case)
         need_value "$@"
         CASE_FILTER="$2"
         shift 2
         ;;
      -i | --iterations)
         need_value "$@"
         ITERATIONS="$2"
         shift 2
         ;;
      -s | --scale)
         need_value "$@"
         SCALE="$2"
         shift 2
         ;;
      -d | --duration)
         need_value "$@"
         DURATION="$2"
         shift 2
         ;;
      --clients)
         need_value "$@"
         CLIENTS="$2"
         shift 2
         ;;
      --band)
         need_value "$@"
         BAND="$2"
         shift 2
         ;;
      -h | --help)
         usage
         exit 0
         ;;
      -*)
         die "unknown option '$1'"
         ;;
      *)
         POSITIONAL+=("$1")
         shift
         ;;
   esac
done

case "$SUBCOMMAND" in
   run)
      do_run
      ;;
   compare)
      [[ ${#POSITIONAL[@]} -eq 2 ]] || die "compare needs BASELINE and CANDIDATE"
      do_compare "${POSITIONAL[0]}" "${POSITIONAL[1]}"
      ;;
   list)
      do_list
      ;;
   clean)
      do_clean
      ;;
   "" | -h | --help | help)
      usage
      ;;
   *)
      echo "error: unknown subcommand '$SUBCOMMAND'" >&2
      usage >&2
      exit 1
      ;;
esac
