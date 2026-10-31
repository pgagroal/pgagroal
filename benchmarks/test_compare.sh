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
# Tests of "pgagroal-bench compare" with made-up results. No database needed.
# Builds pgagroal-bench like bench.sh does (Release, in build-bench).
#
# Run: benchmarks/test_compare.sh   (bash 3.2 compatible)

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$HERE/.." && pwd)"
BUILD_DIR="${BENCH_BUILD_DIR:-$PROJECT_DIR/build-bench}"
BENCH_BIN="$BUILD_DIR/benchmarks/pgagroal-bench"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== building pgagroal-bench into $BUILD_DIR"
if ! cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DDOCS=FALSE -Dbenchmark=ON >"$TMP/build.log" 2>&1 ||
   ! cmake --build "$BUILD_DIR" --target pgagroal-bench >>"$TMP/build.log" 2>&1; then
   tail -n 30 "$TMP/build.log"
   echo "error: cannot build pgagroal-bench"
   exit 1
fi

failures=0
runs=0
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

compare() {
   OUT="$("$BENCH_BIN" compare "$@" 2>&1)"
   RC=$?
}

# The output line of the read_write workload for a metric.
row() {
   echo "$OUT" | awk -v m="$1" '$1 == "read_write" && $2 == m { print; exit }'
}

ends_with() {
   [[ "$1" == *"$2" ]]
}

has() {
   [[ "$OUT" == *"$1"* ]]
}

count() {
   echo "$OUT" | grep -cF -- "$1"
}

# meta.json: the baseline's by default, KEY=JSON pairs change or add fields.
meta() {
   local fields=(case='"read_write"' branch='"master"' commit='"aaaaaaa"' build_type='"Release"'
      os='"Darwin"' cpus=10 dirty=false)
   local kv key i found out=""

   for kv in "$@"; do
      key="${kv%%=*}"
      found=false
      for i in "${!fields[@]}"; do
         if [[ "${fields[$i]%%=*}" == "$key" ]]; then
            fields[$i]="$kv"
            found=true
         fi
      done
      [[ "$found" == "true" ]] || fields+=("$kv")
   done

   for kv in "${fields[@]}"; do
      out="$out${out:+, }\"${kv%%=*}\": ${kv#*=}"
   done
   echo "{$out}"
}

# The candidate's meta.json: another branch and commit.
cand_meta() {
   meta branch='"my-branch"' commit='"bbbbbbb"' "$@"
}

# One iteration with read_write's tps or p99_ms set.
it() {
   local tps=1000 p99=4.0

   [[ "$1" == "tps" ]] && tps="$2"
   [[ "$1" == "p99_ms" ]] && p99="$2"
   echo "{\"read_write\": {\"tps\": $tps, \"p99_ms\": $p99}}"
}

GOOD="$(it - -)"
MULTI='{"read_only": {"tps": 5000, "p99_ms": 1.0}, "read_write": {"tps": 1000, "p99_ms": 4.0}}'

# Create a run directory and set RUN to it. First argument: the meta.json
# content, or "none". The others: one iteration file each.
make_run() {
   local m="$1" i=0 it

   shift
   runs=$((runs + 1))
   RUN="$TMP/run$runs"
   mkdir -p "$RUN"
   [[ "$m" == "none" ]] || echo "$m" >"$RUN/meta.json"
   for it in "$@"; do
      i=$((i + 1))
      echo "$it" >"$RUN/iter-$i.json"
   done
}

# A run with one iteration per tps value.
tps_run() {
   local m="$1" v its=()

   shift
   for v in "$@"; do
      its+=("$(it tps "$v")")
   done
   make_run "$m" "${its[@]}"
}

make_run "$(meta)" "$GOOD" "$GOOD" "$GOOD"
BASE="$RUN"

# 1. identical runs -> exit 0, no change, header names both branches
make_run "$(cand_meta)" "$GOOD" "$GOOD" "$GOOD"
compare "$BASE" "$RUN"
check "identical: exit 0" '[[ $RC -eq 0 ]]'
check "identical: tps no change" 'ends_with "$(row tps)" "no change"'
check "identical: p99 no change" 'ends_with "$(row p99_ms)" "no change"'
check "identical: header" 'has "master @ aaaaaaa" && has "my-branch @ bbbbbbb"'
check "identical: run counts" 'has "3 baseline / 3 candidate"'

# 2. TPS is higher-is-better
make_run "$(cand_meta)" "$(it tps 1200)"
compare "$BASE" "$RUN"
check "tps +20%: faster" 'ends_with "$(row tps)" "1.20x faster"'
make_run "$(cand_meta)" "$(it tps 800)"
compare "$BASE" "$RUN"
check "tps -20%: slower" 'ends_with "$(row tps)" "1.25x slower"'

# 3. p99 is lower-is-better
make_run "$(cand_meta)" "$(it p99_ms 2.0)"
compare "$BASE" "$RUN"
check "p99 halved: faster" 'ends_with "$(row p99_ms)" "2.00x faster"'
make_run "$(cand_meta)" "$(it p99_ms 8.0)"
compare "$BASE" "$RUN"
check "p99 doubled: slower" 'ends_with "$(row p99_ms)" "2.00x slower"'

# 4. the band
make_run "$(cand_meta)" "$(it tps 1050)"
CAND="$RUN"
compare "$BASE" "$CAND"
check "tps +5%, default band: no change" 'ends_with "$(row tps)" "no change"'
compare "$BASE" "$CAND" --band 3
check "tps +5%, band 3: faster" 'ends_with "$(row tps)" "1.05x faster"'
check "band 3: footer" 'has "below 3%"'
compare "$BASE" "$CAND" --band 0
check "band 0: rejected" '[[ $RC -ne 0 ]] && has "--band"'

# 5. median, not mean: one outlier iteration does not move the result
make_run "$(cand_meta)" "$(it tps 1000)" "$(it tps 1010)" "$(it tps 9000)"
compare "$BASE" "$RUN"
check "median ignores outlier" 'ends_with "$(row tps)" "no change"'

# 5b. the overlap rule: past the band, but the iterations' ranges overlap
tps_run "$(meta)" 1000 1100 1300
B="$RUN"
tps_run "$(cand_meta)" 1150 1250 1400
compare "$B" "$RUN"
check "ranges overlap: no change" 'ends_with "$(row tps)" "no change (ranges overlap)"'
check "ranges shown" '[[ "$(row tps)" == *"1000.0-1300.0"* && "$(row tps)" == *"1150.0-1400.0"* ]]'
tps_run "$(meta)" 1000 1100 1140
B="$RUN"
tps_run "$(cand_meta)" 1150 1250 1400
compare "$B" "$RUN"
check "ranges apart, past the band: faster" 'ends_with "$(row tps)" "1.14x faster"'
make_run "$(meta)" "$(it p99_ms 4.0)" "$(it p99_ms 4.4)"
B="$RUN"
make_run "$(cand_meta)" "$(it p99_ms 4.4)" "$(it p99_ms 6.0)"
compare "$B" "$RUN"
check "touching ranges count as overlap" 'ends_with "$(row p99_ms)" "no change (ranges overlap)"'
check "footer explains the overlap rule" 'has "or the min-max ranges overlap"'

# 6. several workloads, reported in the order measured
make_run "$(meta)" "$MULTI"
B="$RUN"
make_run "$(cand_meta)" "$MULTI"
compare "$B" "$RUN"
check "multi workload: exit 0" '[[ $RC -eq 0 ]]'
check "multi workload: order kept" \
   '[[ "$(echo "$OUT" | awk "/^read_(only|write) /{print \$1}" | tr "\n" " ")" == "read_only read_only read_write read_write " ]]'

# 7. warnings (not fatal)
make_run "$(cand_meta cpus=4)" "$GOOD"
compare "$BASE" "$RUN"
check "cpus differ: warned, exit 0" '[[ $RC -eq 0 ]] && has "CPU count differs"'
make_run "$(cand_meta ev_backend='"io_uring"')" "$GOOD"
B="$RUN"
make_run "$(cand_meta ev_backend='"epoll"')" "$GOOD"
compare "$B" "$RUN"
check "ev_backend differs: warned, exit 0" '[[ $RC -eq 0 ]] && has "event backend differs"'
make_run "$(cand_meta postgres='"external"')" "$GOOD"
B="$RUN"
make_run "$(cand_meta postgres='"container pgagroal-test-postgresql17-rocky10"')" "$GOOD"
compare "$B" "$RUN"
check "postgres setup differs: warned" '[[ $RC -eq 0 ]] && has "PostgreSQL setup differs"'
make_run "$(cand_meta build_type='"Debug"')" "$GOOD"
compare "$BASE" "$RUN"
check "debug build: warned" 'has "built as Debug"'
make_run "$(cand_meta dirty=true)" "$GOOD"
compare "$BASE" "$RUN"
check "dirty tree: warned" 'has "dirty working tree"'
make_run "$(cand_meta pgagroal_errors=3)" "$GOOD"
compare "$BASE" "$RUN"
check "pgagroal errors: warned, exit 0" '[[ $RC -eq 0 ]] && has "pgagroal logged 3 errors while measuring the candidate"'
make_run "$(cand_meta pgagroal_errors=0)" "$GOOD"
compare "$BASE" "$RUN"
check "no pgagroal errors: no warning" '! has "pgagroal logged"'

# 8. header details and notes
FULL="$(cand_meta arch='"arm64"' ev_backend='"kqueue"' pg_version='"15.8"' pgagroal_version='"2.1.0"')"
make_run "$FULL" "$GOOD"
compare "$BASE" "$RUN"
# The header describes the machine from the baseline, which has no ev_backend here.
check "header: baseline without ev_backend" '! has "ev_backend"'
check "header: pgagroal version" 'has "my-branch @ bbbbbbb (pgagroal 2.1.0)"'
make_run "$FULL" "$GOOD"
B="$RUN"
make_run "$FULL" "$GOOD"
compare "$B" "$RUN"
check "header: arch" 'has "10 cores, Darwin arm64"'
check "header: baseline ev_backend" 'has "ev_backend   : kqueue"'
check "header: postgresql" 'has "postgresql   : 15.8"'
check "note: not Linux" 'has "Note: Measured on Darwin, not Linux"'
make_run "$(cand_meta os='"Linux"')" "$GOOD"
compare "$RUN" "$RUN"
check "note: no platform note on Linux" '! has "not Linux"'
make_run "$(cand_meta os='"Linux"' note='"pgbench -C is unreliable on master."')" "$GOOD"
compare "$RUN" "$RUN"
check "note: case note printed once" '[[ $(count "Note: pgbench -C is unreliable on master.") -eq 1 ]]'
make_run "$(cand_meta os='"Linux"' note='""')" "$GOOD"
compare "$RUN" "$RUN"
# Only the drift hint.
check "note: empty note ignored" '! has "Note: ." && [[ $(count "Note:") -eq 1 ]]'

# 9. drift: each side against its own previous run
tps_run "$(meta)" 1000 1010 1020
NEWEST_BASE="$RUN"
tps_run "$(cand_meta)" 1000 1010 1020
compare "$NEWEST_BASE" "$RUN"
check "drift: measured once -> hint" 'has "master was measured once" && has "my-branch was measured once"'

tps_run "$(meta)" 1005 1015 1025
STABLE_PREV="$RUN"
tps_run "$(cand_meta)" 1000 1010 1020
compare "$NEWEST_BASE" "$RUN" --baseline-previous "$STABLE_PREV"
check "drift: agrees -> note" '[[ $RC -eq 0 ]] && has "master agrees with its previous run" && ! has "DRIFT"'

tps_run "$(meta)" 1300 1310 1320
DRIFTED_PREV="$RUN"
tps_run "$(cand_meta)" 1000 1010 1020
compare "$NEWEST_BASE" "$RUN" --baseline-previous "$DRIFTED_PREV"
check "drift: shifted -> warning, exit 0" '[[ $RC -eq 0 ]] && has "DRIFT: master differs from its own previous run"'
check "drift: names the metric and both values" 'has "read_write tps 1310.0 -> 1010.0 (1.30x slower)"'

tps_run "$(meta)" 900 1150 1400
OVERLAP_PREV="$RUN"
tps_run "$(cand_meta)" 1000
compare "$NEWEST_BASE" "$RUN" --baseline-previous "$OVERLAP_PREV"
check "drift: overlapping ranges are not drift" '! has "DRIFT"'

tps_run "$(cand_meta)" 1000
CAND="$RUN"
make_run "$(cand_meta)" "{bad"
compare "$NEWEST_BASE" "$CAND" --candidate-previous "$RUN"
check "drift: unreadable previous run -> note, comparison still valid" \
   '[[ $RC -eq 0 ]] && has "drift of my-branch not checked"'

compare "$NEWEST_BASE" "$NEWEST_BASE" --baseline-previous "$DRIFTED_PREV" --candidate-previous "$DRIFTED_PREV"
check "drift: same branch on both sides -> one warning" '[[ $(count "DRIFT:") -eq 1 ]]'

# 10. integrity failures -> exit 2
integrity() {
   compare "$BASE" "$RUN"
   check "$1: integrity exit 2" '[[ $RC -eq 2 ]] && has "INTEGRITY"'
}

RUN="$TMP/nope"
integrity "missing run dir"
make_run none "$GOOD"
integrity "missing meta.json"
make_run '{"case": "read_write", "commit": "b"}' "$GOOD"
integrity "meta without branch"
make_run "$(cand_meta)"
integrity "no iterations"
make_run "$(cand_meta)" "{not json"
integrity "malformed iteration"
make_run "$(cand_meta)" "$(it tps null)"
integrity "null tps"
make_run "$(cand_meta)" "$(it p99_ms 0)"
integrity "zero p99"
make_run "$(cand_meta)" "$(it tps true)"
integrity "bool tps"
make_run "$(cand_meta)" '{"read_write": {"tps": 1000}}'
integrity "missing metric"
make_run "$(cand_meta)" "$GOOD" "$MULTI"
integrity "workloads differ within a run"
make_run "$(cand_meta)" "$MULTI"
integrity "workloads differ between runs"
make_run "$(cand_meta case='"read_only"')" "$GOOD"
integrity "different cases"

echo
if [[ "$failures" -gt 0 ]]; then
   echo "FAILED: $failures"
   exit 1
fi
echo "all pgagroal-bench compare tests passed"
