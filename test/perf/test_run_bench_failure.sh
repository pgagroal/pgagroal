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
# Failure-path test for run_bench.sh: a failing pgbench must make the script
# exit non-zero and write NO output (an inability to measure is an error, not a
# null result). Uses a stub pgbench; skips where timeout(1) is unavailable
# (e.g. stock macOS), since run_bench.sh relies on it.

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v timeout >/dev/null 2>&1; then
   echo "SKIP: timeout(1) not available"
   exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Stub pgbench that always fails.
cat > "$TMP/pgbench" <<'STUB'
#!/bin/bash
echo "stub pgbench: simulated failure" >&2
exit 1
STUB
chmod +x "$TMP/pgbench"

OUT="$TMP/out.json"
PATH="$TMP:$PATH" bash "$HERE/run_bench.sh" localhost 6432 u d 1 1 "$OUT" >/dev/null 2>&1
rc=$?

fail=0
if [ "$rc" -eq 0 ]; then
   echo "FAIL: run_bench.sh exited 0 despite a failing pgbench"
   fail=1
fi
if [ -f "$OUT" ]; then
   echo "FAIL: run_bench.sh wrote output ($OUT) despite a failing pgbench"
   fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: run_bench.sh fails loudly and writes no output on pgbench failure"
exit "$fail"
