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

#include <pgagroal.h>

#include <tsclient.h>
#include <mctf.h>

/*
 * Regression test for issue #946: in pipeline = transaction, a pgagroal-cli
 * management command failed with "Connection reset by peer" whenever a client
 * was connected, because the transaction worker bound MAIN_UDS and stole the
 * management socket (introduced by #824 / 9dd22c7, fixed in #947).
 *
 * The test keeps one client connected through a held transaction and runs a
 * `pgagroal-cli status` while it is connected; the command must succeed. This
 * runs against every test configuration; under the transaction-pipeline
 * configs (test/conf/01, test/conf/02) it exercises the regression, and under
 * the session/performance configs it is a benign smoke check.
 */
MCTF_TEST(test_pgagroal_cli_transaction_client_connected)
{
   int rc = 0;

   rc = pgagroal_tsclient_cli_during_hold(user, database, "status", 3);
   MCTF_ASSERT(rc == 0, cleanup,
               "pgagroal-cli failed while a client was connected (transaction-mode #946 regression)");
cleanup:
   MCTF_FINISH();
}
