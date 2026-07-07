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

#include <string.h>

/* Upper bound on max_connections for the retry-path saturation test: above
 * this, oversubscribing the pool would spawn more backends than PostgreSQL's
 * default max_connections and the CI runner can sustain. */
#define MAX_SATURATION_CONNECTIONS 16

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

/*
 * Regression test for issue #875: the retry path in pgagroal_get_connection()
 * failed to acquire backends freed by other clients under concurrent
 * contention. Clients that exceeded max_connections entered the blocking
 * retry loop but never observed the slots released by the first batch, so
 * they timed out at blocking_timeout even though free slots were available.
 *
 * The test oversubscribes the pool: it drives (max_connections + 2) concurrent
 * holds. The first max_connections clients take every slot; the remaining two
 * must wait for that batch to commit and then acquire the freed slots within
 * blocking_timeout. On the pre-#875 code the two waiters time out and psql
 * exits non-zero, so the helper returns failure. With the fix all clients
 * succeed.
 *
 * Parameters are derived from the live configuration so the test is meaningful
 * against every test/conf profile exercised by run-configs. Configurations
 * whose blocking_timeout is too small to absorb one hold cycle are skipped:
 * there the waiters are expected to time out regardless of #875, so the test
 * could not isolate the regression.
 */
MCTF_TEST(test_pgagroal_pool_saturation_retry)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int hold_seconds = 2;
   int client_count = 0;
   int ok = 0;

   if (config == NULL || config->max_connections <= 0)
   {
      MCTF_SKIP("configuration not available; cannot size the saturation load");
   }

   /* Oversubscribing means spawning max_connections + 2 real psql sessions and
    * backends. On a large pool that exceeds PostgreSQL's own max_connections
    * and the runner's resource limits, so restrict the test to small pools
    * (e.g. conf/17, max_connections = 8) where the retry path is exercised
    * cheaply and deterministically. */
   if (config->max_connections > MAX_SATURATION_CONNECTIONS)
   {
      MCTF_SKIP("max_connections (%d) too large to oversubscribe safely; retry-path test targets small pools",
                config->max_connections);
   }

   /* Need enough headroom for the waiters to ride out one hold cycle and still
    * acquire a freed slot before blocking_timeout fires. */
   if (config->blocking_timeout.s <= (int64_t)hold_seconds + 2)
   {
      MCTF_SKIP("blocking_timeout (%lds) too small to exercise the retry path",
                (long)config->blocking_timeout.s);
   }

   client_count = config->max_connections + 2;

   ok = !pgagroal_tsclient_execute_concurrent_holds(user, database, client_count, hold_seconds);
   MCTF_ASSERT(ok, cleanup,
               "%d concurrent clients (max_connections + 2) did not all acquire freed slots within blocking_timeout (retry-path regression #875)",
               client_count);
cleanup:
   MCTF_FINISH();
}

/*
 * Regression test for issue #848: the per-rule max_size cap must be a hard cap.
 * Before #903 the sub-cap was a count-then-create check (TOCTOU): concurrent
 * acquirers each read a stale per-rule count and then each created a backend,
 * driving the rule's live backend count past max_size. The fix reserves
 * against the cap atomically, so live backends for a rule never exceed
 * max_size.
 *
 * The test oversubscribes a rule: it drives (2 * max_size) concurrent holds.
 * The first max_size clients take every backend slot for the rule; the surplus
 * block on the cap. While they hold, it samples pgagroal's own per-rule live
 * backend counter (the pgagroal_limit type="backend" series exposed in #905)
 * and asserts the peak never exceeds max_size. Sampling pgagroal's authoritative
 * counter avoids the pg_stat_activity teardown-lag over-count that made the
 * first attempt unreliable in CI.
 *
 * Parameters are derived from the live configuration. Configurations without
 * the metrics endpoint enabled, or without a per-rule limit for this
 * user/database, are skipped: there the cap counter cannot be observed.
 */
MCTF_TEST(test_pgagroal_pool_max_size_cap)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int max_size = 0;
   int found_rule = 0;
   int client_count = 0;
   int peak = -1;

   if (config == NULL)
   {
      MCTF_SKIP("configuration not available");
   }
   if (config->common.metrics <= 0)
   {
      MCTF_SKIP("metrics endpoint not enabled; cannot read the per-rule backend counter");
   }

   for (int i = 0; i < config->number_of_limits; i++)
   {
      if (!strcmp((const char*)config->limits[i].username, user) &&
          !strcmp((const char*)config->limits[i].database, database))
      {
         max_size = config->limits[i].max_size;
         found_rule = 1;
         break;
      }
   }

   if (!found_rule || max_size <= 0)
   {
      MCTF_SKIP("no per-rule max_size limit for %s/%s", user, database);
   }

   client_count = max_size * 2;

   peak = pgagroal_tsclient_limit_backend_peak(user, database, client_count, 3);

   MCTF_ASSERT(peak >= 0, cleanup,
               "failed to sample the pgagroal_limit type=\"backend\" series for %s/%s", user, database);
   MCTF_ASSERT(peak <= max_size, cleanup,
               "per-rule live backends peaked at %d, exceeding max_size %d (cap regression #848)",
               peak, max_size);
cleanup:
   MCTF_FINISH();
}
