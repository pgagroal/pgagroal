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
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pgagroal.h>
#include <configuration.h>
#include <pipeline.h>
#include <tsclient.h>
#include <utils.h>
#include <mctf.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Upper bound on the client count for the wake-ups oversubscription test:
 * above this, oversubscribing the pool would spawn more backends than
 * PostgreSQL's default max_connections and the CI runner can sustain. */
#define MAX_WAIT_QUEUE_CONNECTIONS 16
#define HOLD_SECONDS               2

/* Upper bound on the holder count for the saturation (timeout) test: the
 * holders need one real backend each, and the CI runner's PostgreSQL allows
 * far fewer connections than pgagroal's largest test pools expose. Keeping
 * the holder batch below that budget prevents mass holder failures that
 * would make the saturation verdict meaningless. */
#define MAX_HOLD_CLIENTS 64

static bool
wait_queue_value_line(const char* line, const char* metric, int* value)
{
   /* Accepts only the bare sample line "name value" (no HELP/TYPE marker) */
   size_t len = strlen(metric);

   if (strncmp(line, metric, len) != 0)
   {
      return false;
   }

   if (line[len] != ' ')
   {
      return false;
   }

   if (value != NULL)
   {
      *value = atoi(line + len + 1);
   }
   return true;
}

MCTF_TEST(test_pgagroal_wait_queue_wakeups)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int client_count;
   int ok = 0;

   if (config == NULL || config->max_connections <= 0)
   {
      MCTF_SKIP("configuration not available for wait queue oversubscription");
   }

   client_count = config->max_connections + 2 < MAX_WAIT_QUEUE_CONNECTIONS ? config->max_connections + 2 : MAX_WAIT_QUEUE_CONNECTIONS;

   if (config->blocking_timeout.s < (int64_t)HOLD_SECONDS + 2)
   {
      MCTF_SKIP("blocking_timeout (%lds) too small to ride out one hold cycle",
                (long)config->blocking_timeout.s);
   }

   for (int round = 0; round < 2; round++)
   {
      ok = !pgagroal_tsclient_execute_concurrent_holds(user, database,
                                                       client_count,
                                                       HOLD_SECONDS);
      MCTF_ASSERT(ok, cleanup,
                  "round %d: %d concurrent clients did not all acquire "
                  "freed slots within blocking_timeout (wait queue wake-up path)",
                  round, client_count);
   }

cleanup:
   MCTF_FINISH();
}

/* Build a psql invocation like the tsclient hold helper: bounded by
 * timeout(1). Returns heap memory to free. */
static char*
wait_queue_build_psql(char* sql, int watchdog)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   const char* password = NULL;
   char* cmd = NULL;

   password = getenv("PGPASSWORD");
   if (password == NULL)
   {
      password = getenv("PG_USER_PASSWORD");
   }

   if (password != NULL && strlen(password) > 0)
   {
      cmd = pgagroal_append(cmd, "PGPASSWORD=");
      cmd = pgagroal_append(cmd, (char*)password);
      cmd = pgagroal_append_char(cmd, ' ');
   }

   cmd = pgagroal_append(cmd, "timeout -k 5 ");
   cmd = pgagroal_append_int(cmd, watchdog);
   cmd = pgagroal_append(cmd, " psql -h ");
   cmd = pgagroal_append(cmd, config->common.host);
   cmd = pgagroal_append(cmd, " -p ");
   cmd = pgagroal_append_int(cmd, config->common.port);
   cmd = pgagroal_append(cmd, " -U ");
   cmd = pgagroal_append(cmd, user);
   cmd = pgagroal_append(cmd, " -d ");
   cmd = pgagroal_append(cmd, database);
   cmd = pgagroal_append(cmd, " -v ON_ERROR_STOP=1 -X -A -t -c \"");
   cmd = pgagroal_append(cmd, sql);
   cmd = pgagroal_append(cmd, "\" 2>&1 < /dev/null");

   return cmd;
}

/* Number of slots both this user/database can occupy: the per-rule max_size
 * when a limit exists for user/database, otherwise max_connections. */
static int
wait_queue_hold_capacity(void)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->number_of_limits; i++)
   {
      if (pgagroal_strcmp((const char*)config->limits[i].username, user) &&
          pgagroal_strcmp((const char*)config->limits[i].database, database))
      {
         return config->limits[i].max_size;
      }
   }

   return config->max_connections;
}

static void*
wait_queue_holder_thread(void* arg)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int holders = wait_queue_hold_capacity();
   int* rc = (int*)arg;

   if (holders > MAX_HOLD_CLIENTS)
   {
      holders = MAX_HOLD_CLIENTS;
   }

   *rc = pgagroal_tsclient_execute_concurrent_holds(user, database, holders,
                                                    config->blocking_timeout.s + 5);
   return NULL;
}

/*
 * Timeout and reaping: while max_connections holders keep every slot busy for
 * longer than blocking_timeout, a surplus client must enter the wait queue and
 * time out at blocking_timeout (psql exits non-zero). Once the holders release
 * and the timed-out entries have been reaped from the queue head, a follow-up
 * client must acquire a slot normally -- asserting the queue bookkeeping and
 * slot counters survived the timeout path.
 */
MCTF_TEST(test_pgagroal_wait_queue_timeout, 300)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int64_t blocking;
   int watchdog;
   int hold_rc = -1;
   int rc;
   bool joined = false;
   pthread_t holder;
   char* doomed = NULL;
   char* final_cmd = NULL;

   if (config == NULL || config->max_connections <= 0)
   {
      MCTF_SKIP("configuration not available for wait queue oversubscription");
   }

   blocking = config->blocking_timeout.s;
   if (blocking < 3 || blocking > 120)
   {
      MCTF_SKIP("blocking_timeout (%lds) outside the testable range [3, 120]", (long)blocking);
   }

   watchdog = (int)blocking + 40;

   if (pthread_create(&holder, NULL, wait_queue_holder_thread, &hold_rc) != 0)
   {
      MCTF_SKIP("could not spawn the holder thread");
   }

   /* Give the holders time to take every slot */
   sleep(3);

   {
      int probe_window = (int)(blocking / 2);
      time_t probe_start;

      if (probe_window > 10)
      {
         probe_window = 10;
      }

      doomed = wait_queue_build_psql("SELECT 1", watchdog);
      probe_start = time(NULL);
      rc = 0;
      while (rc == 0 && difftime(time(NULL), probe_start) < (double)probe_window)
      {
         rc = system(doomed);
         if (rc != 0)
         {
            break;
         }
         sleep(2);
      }
   }

   MCTF_ASSERT(rc != 0, cleanup,
               "surplus client unexpectedly succeeded while the pool stayed saturated "
               "beyond blocking_timeout (%lds)",
               (long)blocking);

   /* The holders must all have succeeded and released */
   MCTF_ASSERT(pthread_join(holder, NULL) == 0, cleanup, "holder thread failed to join");
   joined = true;
   MCTF_ASSERT(hold_rc == 0, cleanup,
               "holder batch failed; the timeout-phase verdict is unreliable");

   /* After reaping: a fresh client acquires normally */
   final_cmd = wait_queue_build_psql("SELECT 1", watchdog);
   rc = system(final_cmd);
   MCTF_ASSERT(rc == 0, cleanup,
               "pool did not recover after wait queue timeouts: a follow-up client "
               "failed to acquire a slot");

cleanup:
   if (!joined)
   {
      pthread_cancel(holder);
      pthread_join(holder, NULL);
   }
   free(doomed);
   free(final_cmd);
   MCTF_FINISH();
}

/*
 * The wait queue must report on the metrics endpoint in spec-compliant form
 * (# HELP / # TYPE with a space, per #1062) and with parseable values.
 * Guards: metrics endpoint enabled.
 */
MCTF_TEST(test_pgagroal_wait_queue_metrics)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char tmp[] = "/tmp/pgagroal_wait_queue_metrics_XXXXXX";
   char line[8192];
   char* cmd = NULL;
   FILE* f = NULL;
   int fd;
   bool help_size = false;
   bool type_size = false;
   bool size_line = false;
   bool timeouts_line = false;
   bool handoffs_line = false;
   int size_value = -1;

   if (config == NULL || config->common.metrics <= 0)
   {
      MCTF_SKIP("metrics endpoint not enabled; cannot verify wait queue metrics");
   }

   fd = mkstemp(tmp);
   MCTF_ASSERT(fd >= 0, cleanup, "could not create temporary file for metrics scrape");
   close(fd);

   cmd = pgagroal_append(cmd, "curl -s -m 5 http://");
   cmd = pgagroal_append(cmd, config->common.host);
   cmd = pgagroal_append_char(cmd, ':');
   cmd = pgagroal_append_int(cmd, config->common.metrics);
   cmd = pgagroal_append(cmd, "/metrics -o ");
   cmd = pgagroal_append(cmd, tmp);
   MCTF_ASSERT(system(cmd) == 0, cleanup, "curl scrape of the metrics endpoint failed");

   f = fopen(tmp, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not read scraped metrics file");

   while (fgets(line, sizeof(line), f) != NULL)
   {
      help_size = help_size ||
                  strncmp(line, "# HELP pgagroal_wait_queue_size ",
                          strlen("# HELP pgagroal_wait_queue_size ")) == 0;
      type_size = type_size ||
                  strncmp(line, "# TYPE pgagroal_wait_queue_size gauge",
                          strlen("# TYPE pgagroal_wait_queue_size gauge")) == 0;
      size_line = size_line || wait_queue_value_line(line, "pgagroal_wait_queue_size", &size_value);
      timeouts_line = timeouts_line || wait_queue_value_line(line, "pgagroal_wait_queue_timeouts", NULL);
      handoffs_line = handoffs_line || wait_queue_value_line(line, "pgagroal_wait_queue_handoffs", NULL);
   }

   MCTF_ASSERT(help_size, cleanup, "missing # HELP for pgagroal_wait_queue_size");
   MCTF_ASSERT(type_size, cleanup, "missing # TYPE for pgagroal_wait_queue_size");
   MCTF_ASSERT(size_line, cleanup, "missing pgagroal_wait_queue_size sample");
   MCTF_ASSERT(timeouts_line, cleanup, "missing pgagroal_wait_queue_timeouts sample");
   MCTF_ASSERT(handoffs_line, cleanup, "missing pgagroal_wait_queue_handoffs sample");
   MCTF_ASSERT(size_value >= 0, cleanup,
               "pgagroal_wait_queue_size value (%d) is not a parseable gauge", size_value);

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   unlink(tmp);
   free(cmd);
   MCTF_FINISH();
}
