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
#include <pipeline.h>

#include <tsclient.h>
#include <mctf.h>

/* pgbench -C opens a new connection for every transaction, so backends are
 * created and handed back to main all the time. Flushing the idle pool first
 * makes each round start with new backends (#1076). */
MCTF_TEST(test_pgagroal_connect_per_transaction, 300)
{
   int ret = 0;
   struct main_configuration* config = (struct main_configuration*)shmem;

   /* The transaction pipeline keeps its own copy of every backend descriptor  */
   if (config != NULL && config->pipeline == PIPELINE_TRANSACTION)
   {
      MCTF_SKIP("not supported by the transaction pipeline");
   }

   for (int i = 1; i <= 5; i++)
   {
      ret = pgagroal_tsclient_execute_cli("flush idle");
      MCTF_ASSERT(ret == 0, cleanup, "flush failed in round %d", i);

      ret = pgagroal_tsclient_execute_pgbench_connect(user, database, true, 8, 4, 50);
      MCTF_ASSERT(ret == 0, cleanup, "pgbench -C failed in round %d", i);
   }

cleanup:
   MCTF_FINISH();
}
