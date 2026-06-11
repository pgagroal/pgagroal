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
# Fast unit tests for compare.py. Run: python3 test/perf/test_compare.py
# Exercises the central distinction: integrity failures (exit 2) vs valid
# comparisons (exit 0, or 1 with --fail-on-regression).

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, "compare.py")

GOOD = {
    "read_only": {"tps": 1000, "p99_ms": 2.0},
    "read_write": {"tps": 500, "p99_ms": 4.0},
    "connect": {"tps": 200, "p99_ms": 9.0},
}

_tmp = tempfile.mkdtemp()
_n = 0
failures = []


def write(obj):
    global _n
    _n += 1
    if isinstance(obj, str):
        path = os.path.join(_tmp, f"f{_n}.json")
        with open(path, "w") as f:
            f.write(obj)
    else:
        path = os.path.join(_tmp, f"f{_n}.json")
        with open(path, "w") as f:
            json.dump(obj, f)
    return path


def run(args):
    p = subprocess.run([sys.executable, COMPARE] + args, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def check(name, cond):
    print(f"{'ok  ' if cond else 'FAIL'}  {name}")
    if not cond:
        failures.append(name)


def deepcopy(d):
    return json.loads(json.dumps(d))


# 1. normal, no regression -> exit 0, within thresholds
rc, out = run(["--base", write(GOOD), "--pr", write(GOOD)])
check("normal: exit 0", rc == 0)
check("normal: within thresholds", "Within thresholds" in out)

# 2. regression (tps -20% on read_write), advisory -> exit 0, flagged
pr = deepcopy(GOOD); pr["read_write"]["tps"] = 400
rc, out = run(["--base", write(GOOD), "--pr", write(pr)])
check("regression advisory: exit 0", rc == 0)
check("regression advisory: flagged", "Apparent regression" in out)

# 3. regression + --fail-on-regression -> exit 1
rc, out = run(["--base", write(GOOD), "--pr", write(pr), "--fail-on-regression"])
check("regression blocking: exit 1", rc == 1)

# 4. p99 regression (+30%) -> flagged
pr2 = deepcopy(GOOD); pr2["connect"]["p99_ms"] = 12.0
rc, out = run(["--base", write(GOOD), "--pr", write(pr2)])
check("p99 regression: flagged", "Apparent regression" in out and rc == 0)

# 5. null value -> integrity exit 2
bad = deepcopy(GOOD); bad["read_only"]["tps"] = None
rc, out = run(["--base", write(GOOD), "--pr", write(bad)])
check("null tps: integrity exit 2", rc == 2)
check("null tps: integrity message", "INTEGRITY" in out)

# 6. zero value -> integrity exit 2
bad = deepcopy(GOOD); bad["read_write"]["p99_ms"] = 0
rc, out = run(["--base", write(GOOD), "--pr", write(bad)])
check("zero p99: integrity exit 2", rc == 2)

# 7. missing workload -> integrity exit 2
bad = deepcopy(GOOD); del bad["connect"]
rc, out = run(["--base", write(GOOD), "--pr", write(bad)])
check("missing workload: integrity exit 2", rc == 2)

# 8. malformed JSON -> integrity exit 2
rc, out = run(["--base", write(GOOD), "--pr", write("{not json")])
check("malformed json: integrity exit 2", rc == 2)

# 9. bad CLI args (no --pr) -> non-zero (argparse exit 2)
rc, out = run(["--base", write(GOOD)])
check("missing --pr: non-zero", rc != 0)

# 10. median across runs: base tps medians 1000/500/200; pr two runs whose
# medians equal base -> within thresholds (proves median, not mean/first)
b1 = deepcopy(GOOD)
b2 = deepcopy(GOOD); b2["read_only"]["tps"] = 1200  # median of (1000,1200)=1100
pr_a = deepcopy(GOOD); pr_a["read_only"]["tps"] = 1050
pr_b = deepcopy(GOOD); pr_b["read_only"]["tps"] = 1150  # median 1100 == base median
rc, out = run(["--base", write(b1), write(b2), "--pr", write(pr_a), write(pr_b)])
check("median aggregation: exit 0 within thresholds", rc == 0 and "Within thresholds" in out)
check("median aggregation: reports 2 base / 2 PR runs", "2 base / 2 PR" in out)

print()
if failures:
    print(f"FAILED ({len(failures)}): {failures}")
    sys.exit(1)
print("all compare.py tests passed")
