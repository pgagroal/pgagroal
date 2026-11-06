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
#include <configuration.h>
#include <pipeline.h>
#include <tsclient.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Largest number of concurrent clients opened by the other testcases. The
 * backend discarded here is gone for the rest of the configuration's run
 * because the transaction pipeline cannot create a backend after startup
 * (#344), so prefill must leave at least one spare above this. */
#define SUITE_PEAK_CLIENTS 6

static bool
has_spare_prefilled_backends(struct main_configuration* config, char* user, char* database)
{
   for (int i = 0; i < config->number_of_limits; i++)
   {
      if (pgagroal_strcmp((const char*)config->limits[i].username, user) &&
          pgagroal_strcmp((const char*)config->limits[i].database, database))
      {
         return config->limits[i].initial_size > SUITE_PEAK_CLIENTS;
      }
   }

   return false;
}

static char*
build_psql_command(struct main_configuration* config, char* user, char* database, char* sql)
{
   char* command = NULL;

   command = pgagroal_append(command, "psql -h ");
   command = pgagroal_append(command, config->common.host);
   command = pgagroal_append(command, " -p ");
   command = pgagroal_append_int(command, config->common.port);
   command = pgagroal_append(command, " -U ");
   command = pgagroal_append(command, user);
   command = pgagroal_append(command, " -d ");
   command = pgagroal_append(command, database);
   command = pgagroal_append(command, " -X -q -A -t -v ON_ERROR_STOP=1 -c \"");
   command = pgagroal_append(command, sql);
   command = pgagroal_append(command, "\"");

   return command;
}

/*
 * #989: with pipeline = transaction, a client that disconnects while a result
 * is still streaming must not leave its backend in the pool. Otherwise the
 * next client can receive the abandoned query's remaining messages before its
 * own result.
 */
MCTF_TEST(test_pgagroal_transaction_disconnect_discards_stale_backend, 60)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char* stream = NULL;
   char* pipeline = NULL;
   char* check = NULL;
   FILE* output = NULL;
   char line[64];
   int rc = 0;

   MCTF_ASSERT_PTR_NONNULL(config, cleanup, "shmem is not initialized");

   if (config->pipeline != PIPELINE_TRANSACTION)
   {
      MCTF_SKIP("transaction pipeline only");
   }

   /* The streaming client takes one backend and the check client must still
    * have a different one after the streaming backend is discarded. Creating
    * a backend after startup is not supported by the transaction pipeline
    * (#344), so both backends must come from prefill. The discarded backend
    * is not replaced, which is why a spare above the suite's peak load is
    * required (test/conf/01 and 02 prefill seven). */
   if (!has_spare_prefilled_backends(config, user, database))
   {
      MCTF_SKIP("transaction pipeline needs prefilled backends with a spare");
   }

   stream = build_psql_command(config, user, database, "SELECT pg_sleep(10)");
   MCTF_ASSERT_PTR_NONNULL(stream, cleanup, "could not build the streaming command");

   /* Disconnect while the backend is still executing the query: the worker
    * must not hand this backend to the next client. */
   pipeline = pgagroal_append(pipeline, "( ");
   pipeline = pgagroal_append(pipeline, stream);
   pipeline = pgagroal_append(pipeline,
                              " >/dev/null 2>&1 & pid=$!; sleep 2; "
                              "kill -9 $pid 2>/dev/null || true; "
                              "wait $pid 2>/dev/null || true )");

   MCTF_ASSERT_INT_EQ(system(pipeline), 0, cleanup, "streaming client failed");

   /* Let the worker notice the client disconnect and release the backend. */
   sleep(1);

   check = build_psql_command(config, user, database, "SELECT 1");
   MCTF_ASSERT_PTR_NONNULL(check, cleanup, "could not build the check command");

   output = popen(check, "r");
   MCTF_ASSERT_PTR_NONNULL(output, cleanup, "could not start the check client");
   MCTF_ASSERT_PTR_NONNULL(fgets(line, sizeof(line), output), cleanup,
                           "check client did not return a row");

   rc = pclose(output);
   output = NULL;

   MCTF_ASSERT_INT_EQ(rc, 0, cleanup, "check client failed");
   MCTF_ASSERT_STR_EQ(line, "1\n", cleanup,
                      "check client received a stale response from the abandoned backend");

cleanup:
   if (output != NULL)
   {
      pclose(output);
   }
   free(stream);
   free(pipeline);
   free(check);
   MCTF_FINISH();
}
