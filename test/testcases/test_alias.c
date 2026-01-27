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

// Test connecting with first database alias
MCTF_TEST(test_pgagroal_database_alias1)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, "pgalias1", true, 0, 0, 0);
   MCTF_ASSERT(found, cleanup, "Connection to database alias1 failed");
cleanup:
   MCTF_FINISH();
}

// Test connecting with second database alias
MCTF_TEST(test_pgagroal_database_alias2)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, "pgalias2", true, 0, 0, 0);
   MCTF_ASSERT(found, cleanup, "Connection to database alias2 failed");
cleanup:
   MCTF_FINISH();
}

// Test connecting with first database alias
MCTF_TEST(test_pgagroal_database_alias1_load)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, "pgalias1", true, 6, 0, 1000);
   MCTF_ASSERT(found, cleanup, "Connection to database alias1 failed");
cleanup:
   MCTF_FINISH();
}

// Test connecting with second database alias
MCTF_TEST(test_pgagroal_database_alias2_load)
{
   int found = 0;
   found = !pgagroal_tsclient_execute_pgbench(user, "pgalias2", true, 6, 0, 1000);
   MCTF_ASSERT(found, cleanup, "Connection to database alias2 failed");
cleanup:
   MCTF_FINISH();
}