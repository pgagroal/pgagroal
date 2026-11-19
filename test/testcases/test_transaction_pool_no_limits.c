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

/* The pool must serve transactions even when it starts empty */
MCTF_TEST(test_transaction_pool_grows_from_empty, 300)
{
   int ret = 0;
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (config == NULL || config->pipeline != PIPELINE_TRANSACTION)
   {
      MCTF_SKIP("requires the transaction pipeline");
   }

   ret = pgagroal_tsclient_execute_pgbench(user, database, true, 1, 1, 10);
   MCTF_ASSERT(ret == 0, cleanup, "single-client transactions failed on an initially empty pool");

   ret = pgagroal_tsclient_execute_concurrent_holds(user, database, 4, 1);
   MCTF_ASSERT(ret == 0, cleanup, "concurrent holds failed while the pool was growing");

   ret = pgagroal_tsclient_execute_pgbench(user, database, true, 4, 2, 20);
   MCTF_ASSERT(ret == 0, cleanup, "steady-state transactions failed after the pool had grown");

cleanup:
   MCTF_FINISH();
}

/* The pool may grow up to the cap while clients keep transacting */
MCTF_TEST(test_transaction_pool_growth_respects_cap, 300)
{
   int ret = 0;
   int cap = 0;
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (config == NULL || config->pipeline != PIPELINE_TRANSACTION)
   {
      MCTF_SKIP("requires the transaction pipeline");
   }

   if (config->number_of_limits > 0)
   {
      for (int i = 0; i < config->number_of_limits; i++)
      {
         if (cap == 0 || config->limits[i].max_size < cap)
         {
            cap = config->limits[i].max_size;
         }
      }
   }
   else
   {
      cap = config->max_connections;
   }

   ret = pgagroal_tsclient_execute_concurrent_holds(user, database, cap, 1);
   MCTF_ASSERT(ret == 0, cleanup, "%d concurrent holds failed up to the cap %d", cap, cap);

cleanup:
   MCTF_FINISH();
}
