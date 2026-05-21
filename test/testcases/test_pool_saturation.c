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

/*
 * Smoke test for pgagroal_tsclient_execute_concurrent_holds(): two concurrent
 * psql sessions each open a backend through pgagroal and run
 * BEGIN; SELECT pg_sleep(1); COMMIT;. The client count is intentionally far
 * below any test configuration's max_connections so this test does not depend
 * on retry-path behaviour and only validates that the harness can drive
 * multiple parallel sessions end-to-end.
 *
 * The retry-path regression for issue #875 is exercised by a separate
 * testcase (test_pgagroal_pool_saturation_retry), which is added alongside
 * the fix in PR #878.
 */
MCTF_TEST(test_pgagroal_pool_concurrent_smoke)
{
   int found = 0;

   found = !pgagroal_tsclient_execute_concurrent_holds(user, database, 2, 1);
   MCTF_ASSERT(found, cleanup, "two concurrent holds did not both succeed");
cleanup:
   MCTF_FINISH();
}
