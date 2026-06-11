#!/bin/bash
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
# Alternating A/B measurement to reduce ordering/warming bias: measure
# base, PR, PR, base. Before each measured run the pgbench database is reset
# (re-initialised) so no run sees a database warmed or mutated by a prior run.
# Each run writes one JSON file into <outdir>; compare.py then aggregates by
# median per side.
#
# Any measurement or reset failure aborts with a non-zero exit (integrity).
#
# Usage: run_ab.sh <base_bindir> <pr_bindir> <outdir>
# Env: PGUSER, PGDATABASE, PGPASSWORD, PGHOST, PGPORT, PERF_SCALE, plus the
#      measure.sh / run_bench.sh variables.

set -uo pipefail

BASE_BIN="${1:?base bindir}"
PR_BIN="${2:?pr bindir}"
OUTDIR="${3:?outdir}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$OUTDIR"

: "${PGUSER:?PGUSER required}"
: "${PGDATABASE:?PGDATABASE required}"
SCALE="${PERF_SCALE:-10}"

reset_db() {
   PGPASSWORD="${PGPASSWORD:-}" pgbench -i -q -s "$SCALE" \
      -h "${PGHOST:-localhost}" -p "${PGPORT:-5432}" -U "$PGUSER" "$PGDATABASE" >/dev/null 2>&1
}

i=0
for spec in "base:$BASE_BIN" "pr:$PR_BIN" "pr:$PR_BIN" "base:$BASE_BIN"; do
   label="${spec%%:*}"
   bin="${spec#*:}"
   i=$((i + 1))
   echo "::group::measure ${label} (run ${i})"
   reset_db || { echo "PERF MEASURE FAILURE: pgbench DB reset failed before ${label}-${i}" >&2; exit 1; }
   "$HERE/measure.sh" "$bin" "${label}-${i}" "$OUTDIR/${label}-${i}.json" || exit 1
   echo "::endgroup::"
done

echo "collected base runs: $(ls "$OUTDIR"/base-*.json 2>/dev/null | wc -l | tr -d ' '), pr runs: $(ls "$OUTDIR"/pr-*.json 2>/dev/null | wc -l | tr -d ' ')"
