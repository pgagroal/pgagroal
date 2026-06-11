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
# Start pgagroal from a built binary dir against the already-running local
# PostgreSQL, run one benchmark measurement through it, and stop it. Startup and
# teardown are deterministic: pgagroal is always stopped via an EXIT trap (even
# if the workload fails), the port is freed before starting, and an unreachable
# pgagroal fails the run with its log.
#
# Usage: measure.sh <pgagroal_bindir> <label> <out.json>
#   bindir = .../build/src ; label names the per-run log (/tmp/pgagroal-<label>.log)
# Env: PGA_PORT, PGHOST, PGPORT, PGUSER, PGDATABASE, PERF_DURATION, PERF_CLIENTS,
#      PGAGROAL_RUN_AS (run pgagroal as this user when current uid is 0).

set -uo pipefail

BINDIR="$(cd "${1:?bindir}" && pwd)"
LABEL="${2:?label}"
OUT="${3:?out.json}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PGA_PORT="${PGA_PORT:-6432}"
BACKEND_HOST="${PGHOST:-localhost}"
BACKEND_PORT="${PGPORT:-5432}"
DUR="${PERF_DURATION:-20}"
CLIENTS="${PERF_CLIENTS:-16}"
LOG="/tmp/pgagroal-${LABEL}.log"

fail() { echo "PERF MEASURE FAILURE: $*" >&2; [ -f "$LOG" ] && sed 's/^/  log| /' "$LOG" >&2; exit 1; }

# pgagroal refuses root; run as PGAGROAL_RUN_AS when we are root, else directly.
run_pgagroal() {
   if [ -n "${PGAGROAL_RUN_AS:-}" ] && [ "$(id -u)" = "0" ]; then
      sudo -u "$PGAGROAL_RUN_AS" env "LD_LIBRARY_PATH=$BINDIR" "$@"
   else
      LD_LIBRARY_PATH="$BINDIR" "$@"
   fi
}

CFG="$(mktemp -d)"
chmod 755 "$CFG"

cat > "$CFG/pgagroal.conf" <<EOF
[pgagroal]
host = localhost
port = $PGA_PORT
log_type = file
log_path = $LOG
log_level = warn
max_connections = 100
pipeline = performance
unix_socket_dir = /tmp/

[primary]
host = $BACKEND_HOST
port = $BACKEND_PORT
EOF
printf 'host all all all all\n' > "$CFG/pgagroal_hba.conf"
chmod 644 "$CFG"/*.conf

stop_pgagroal() {
   run_pgagroal "$BINDIR/pgagroal-cli" -c "$CFG/pgagroal.conf" shutdown >/dev/null 2>&1 || true
   # Belt and braces: kill anything still bound to our config, then wait for the port.
   pkill -f "$CFG/pgagroal.conf" 2>/dev/null || true
   for _ in $(seq 1 10); do pg_isready -h localhost -p "$PGA_PORT" >/dev/null 2>&1 || break; sleep 1; done
}
trap 'stop_pgagroal; rm -rf "$CFG"' EXIT

# Ensure the port is free before we start (a previous run may not have released it).
if pg_isready -h localhost -p "$PGA_PORT" >/dev/null 2>&1; then
   fail "port $PGA_PORT already in use before starting pgagroal ($LABEL)"
fi

# Do not pre-create the log: pgagroal runs as the unprivileged PGAGROAL_RUN_AS
# user and must own the file it writes (a root-created log is not writable by it).
# Remove any stale file from a prior run so pgagroal creates it fresh.
rm -f "$LOG" 2>/dev/null || true
run_pgagroal "$BINDIR/pgagroal" -c "$CFG/pgagroal.conf" -a "$CFG/pgagroal_hba.conf" -d \
   || fail "pgagroal failed to launch ($LABEL)"

reachable=0
for _ in $(seq 1 30); do
   if pg_isready -h localhost -p "$PGA_PORT" >/dev/null 2>&1; then reachable=1; break; fi
   sleep 1
done
[ "$reachable" = "1" ] || fail "pgagroal did not become reachable on port $PGA_PORT ($LABEL)"

"$HERE/run_bench.sh" localhost "$PGA_PORT" "$PGUSER" "$PGDATABASE" "$DUR" "$CLIENTS" "$OUT"
