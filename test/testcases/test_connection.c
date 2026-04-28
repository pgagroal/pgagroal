/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <tsclient.h>
#include <mctf.h>

// simple
MCTF_TEST(test_pgagroal_connection)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, database, true, 0, 0, 0);
   MCTF_ASSERT(found, cleanup, "success status not found");
cleanup:
   MCTF_FINISH();
}

// baseline
MCTF_TEST(test_pgagroal_connection_load)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, database, true, 6, 0, 1000);
   MCTF_ASSERT(found, cleanup, "success status not found");
cleanup:
   MCTF_FINISH();
}

// regression for #875: blocked clients in the retry path must acquire
// freed slots under concurrent contention. Pre-fix, this scenario was
// observed to time out with "pool is full" despite 2-5 free slots being
// available throughout the blocking_timeout window.
//
// 8 clients, 8 threads, 100 select-only transactions each. The
// multi-threaded form (-j 8) is required: single-threaded pgbench
// connects sequentially and does not exercise the concurrent contention
// on the retry path that #875 covers.
//
// Saturation only actually manifests when the test runs against a
// configuration whose effective per-rule cap is smaller than the client
// count (8) - test/conf/17 sets max_connections = 6 for that purpose.
// On configurations with larger caps the load still passes through
// pgagroal cleanly, so the test is a useful regression guard against
// any regression that breaks the saturation path generally.
MCTF_TEST(test_pgagroal_connection_saturation)
{
   int found = 0;

   found = !pgagroal_tsclient_execute_pgbench(user, database, true, 8, 8, 100);
   MCTF_ASSERT(found, cleanup,
               "all clients should acquire connections within blocking_timeout (regression for #875)");

cleanup:
   MCTF_FINISH();
}
