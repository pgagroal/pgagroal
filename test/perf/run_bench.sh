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
# Run pgbench workloads through a running pgagroal and emit TPS + p99 latency as
# JSON: one measurement. A short warm-up precedes each recorded workload.
#
# This script FAILS LOUDLY (non-zero exit, nothing written to <out.json>) on any
# integrity fault — pgbench error, timeout, missing output, missing/empty latency
# log, or a non-numeric result. It must never emit null/placeholder values: an
# inability to measure is an error, not a result (see PR #918).
#
# Usage: run_bench.sh <host> <port> <user> <db> <duration_s> <clients> <out.json>

set -uo pipefail

HOST="${1:?host}"
PORT="${2:?port}"
USER="${3:?user}"
DB="${4:?db}"
DUR="${5:?duration_s}"
CLIENTS="${6:?clients}"
OUT="${7:?out.json}"

JOBS="$(nproc 2>/dev/null || echo 2)"
WARMUP="${PERF_WARMUP:-3}"
CAP=$((DUR + WARMUP + 60))     # hard timeout per pgbench invocation
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "PERF MEASURE FAILURE: $*" >&2; exit 1; }

# Echo one JSON member: "name": {tps, p99_ms}. Warm-up first (discarded), then
# the measured run. Any fault exits non-zero (caught by the caller's `|| exit`).
run_one()
{
   local name="$1"; shift
   local pfx="$TMP/$name"
   local out="$TMP/$name.out"
   local rc tps p99

   # Warm-up (pool fill, page cache); results discarded but failures still fatal.
   timeout "$CAP" pgbench -h "$HOST" -p "$PORT" -U "$USER" -d "$DB" \
      -T "$WARMUP" -c "$CLIENTS" -j "$JOBS" "$@" >/dev/null 2>&1
   rc=$?
   [ "$rc" -eq 124 ] && fail "$name warm-up timed out"
   [ "$rc" -ne 0 ] && fail "$name warm-up failed (pgbench exit $rc)"

   timeout "$CAP" pgbench -h "$HOST" -p "$PORT" -U "$USER" -d "$DB" \
      -T "$DUR" -c "$CLIENTS" -j "$JOBS" -l --log-prefix="$pfx" "$@" > "$out" 2>&1
   rc=$?
   [ "$rc" -eq 124 ] && { sed 's/^/  | /' "$out" >&2; fail "$name measured run timed out"; }
   [ "$rc" -ne 0 ] && { sed 's/^/  | /' "$out" >&2; fail "$name pgbench exited $rc"; }

   tps="$(grep -oE 'tps = [0-9.]+' "$out" | head -1 | awk '{print $3}')"
   [ -n "$tps" ] || { sed 's/^/  | /' "$out" >&2; fail "$name produced no TPS"; }

   ls "$pfx".* >/dev/null 2>&1 || fail "$name produced no latency log"
   p99="$(cat "$pfx".* | awk '{print $3}' | sort -n \
      | awk 'END {if (NR == 0) exit 1} {a[NR]=$1} END {printf "%.3f", a[int(NR*0.99)]/1000.0}')" \
      || fail "$name latency log was empty"

   printf '"%s": {"tps": %s, "p99_ms": %s}' "$name" "$tps" "$p99"
}

m_ro="$(run_one read_only -S)" || exit 1
m_rw="$(run_one read_write)" || exit 1
m_co="$(run_one connect -C -S)" || exit 1

printf '{%s, %s, %s}\n' "$m_ro" "$m_rw" "$m_co" > "$OUT"
cat "$OUT"
