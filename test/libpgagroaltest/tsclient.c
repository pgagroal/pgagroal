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
#include <json.h>
#include <logging.h>
#include <network.h>
#include <shmem.h>
#include <tsclient.h>
#include <utils.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char project_directory[BUFFER_SIZE];

struct hold_thread_args
{
   char* command;
   int   exit_status;
};

static char* get_configuration_path();
static char* get_databases_path();
static char* get_log_file_path();
static void* hold_thread_main(void* arg);
static int scrape_limit_backend(int metrics_port, char* host, char* user, char* database);
static int compute_client_watchdog(struct main_configuration* config, int hold_seconds);

int
pgagroal_tsclient_init(char* base_dir)
{
   int ret;
   size_t size;
   char* configuration_path = NULL;
   char* limit_path = NULL;

   memset(project_directory, 0, sizeof(project_directory));
   memcpy(project_directory, base_dir, strlen(base_dir));

   configuration_path = get_configuration_path();
   // Create the shared memory for the configuration
   size = sizeof(struct main_configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      goto error;
   }
   pgagroal_init_configuration(shmem);
   // Try reading configuration from the configuration path
   if (configuration_path != NULL)
   {
      ret = pgagroal_read_configuration(shmem, configuration_path, false);
      if (ret)
      {
         goto error;
      }
   }
   else
   {
      goto error;
   }

   /* Also load the LIMIT configuration so limit-aware tests can see the rules
    * in shmem (pgagroal_read_configuration only reads pgagroal.conf). Best
    * effort: a configuration directory without a databases file just leaves
    * number_of_limits at 0, exactly as before. */
   limit_path = get_databases_path();
   if (limit_path != NULL)
   {
      pgagroal_read_limit_configuration(shmem, limit_path);
      free(limit_path);
   }

   pgagroal_start_logging();

   free(configuration_path);
   return 0;
error:
   free(configuration_path);
   return 1;
}

int
pgagroal_tsclient_destroy()
{
   size_t size;

   size = sizeof(struct main_configuration);
   pgagroal_stop_logging();
   return pgagroal_destroy_shared_memory(shmem, size);
}

int
pgagroal_tsclient_execute_pgbench(char* user, char* database, bool select_only, int client_count, int thread_count, int transaction_count)
{
   char* command = NULL;
   char* log_file_path = NULL;
   struct main_configuration* config = NULL;
   int ret = EXIT_FAILURE;
   const char* password = NULL;
   char* command_with_password = NULL;

   config = (struct main_configuration*)shmem;
   log_file_path = get_log_file_path();

   /* Get password from environment variables */
   /* Priority: PGPASSWORD > PG_USER_PASSWORD > PG_UTF8_USER_PASSWORD */
   password = getenv("PGPASSWORD");
   if (password == NULL)
   {
      password = getenv("PG_USER_PASSWORD");
   }
   if (password == NULL)
   {
      password = getenv("PG_UTF8_USER_PASSWORD");
   }

   command = pgagroal_append(NULL, "pgbench ");

   // add options
   if (select_only)
   {
      command = pgagroal_append(command, "-S ");
   }

   if (client_count)
   {
      command = pgagroal_append(command, "-c ");
      command = pgagroal_append_int(command, client_count);
      command = pgagroal_append_char(command, ' ');
   }
   if (thread_count)
   {
      command = pgagroal_append(command, "-j ");
      command = pgagroal_append_int(command, thread_count);
      command = pgagroal_append_char(command, ' ');
   }
   if (transaction_count)
   {
      command = pgagroal_append(command, "-t ");
      command = pgagroal_append_int(command, transaction_count);
      command = pgagroal_append_char(command, ' ');
   }

   // add host details
   command = pgagroal_append(command, "-h ");
   command = pgagroal_append(command, config->common.host);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-p ");
   command = pgagroal_append_int(command, config->common.port);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-U ");
   command = pgagroal_append(command, user);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-d ");
   command = pgagroal_append(command, database);

   command = pgagroal_append(command, " >> "); // append to the file
   command = pgagroal_append(command, log_file_path);
   command = pgagroal_append(command, " 2>&1");
   command = pgagroal_append(command, " < /dev/null");

   /* Prepend PGPASSWORD to command if password is available */
   if (password != NULL && strlen(password) > 0)
   {
      size_t cmd_len = strlen(command);
      size_t pwd_len = strlen(password);
      /* Format: "PGPASSWORD=%s %s\0" = 11 + pwd_len + 1 + cmd_len + 1 */
      size_t total_len = 11 + pwd_len + 1 + cmd_len + 1;

      command_with_password = (char*)calloc(total_len, sizeof(char));
      if (command_with_password != NULL)
      {
         snprintf(command_with_password, total_len, "PGPASSWORD=%s %s", password, command);
         ret = system(command_with_password);
         free(command_with_password);
      }
      else
      {
         /* Memory allocation failed - fallback to command without password (will fail immediately due to < /dev/null) */
         ret = system(command);
      }
   }
   else
   {
      /* No password found - pgbench will fail immediately (due to < /dev/null) */
      ret = system(command);
   }

   free(command);
   free(log_file_path);

   return ret;
}

int
pgagroal_tsclient_init_pgbench(char* user, char* database, int scale)
{
   char* command = NULL;
   char* log_file_path = NULL;
   struct main_configuration* config = NULL;
   int ret = EXIT_FAILURE;
   const char* password = NULL;
   char* command_with_password = NULL;

   config = (struct main_configuration*)shmem;
   log_file_path = get_log_file_path();

   /* Get password from environment variables */
   /* Priority: PGPASSWORD > PG_USER_PASSWORD > PG_UTF8_USER_PASSWORD */
   password = getenv("PGPASSWORD");
   if (password == NULL)
   {
      password = getenv("PG_USER_PASSWORD");
   }
   if (password == NULL)
   {
      password = getenv("PG_UTF8_USER_PASSWORD");
   }

   command = pgagroal_append(NULL, "pgbench -i ");

   if (scale > 0)
   {
      command = pgagroal_append(command, "-s ");
      command = pgagroal_append_int(command, scale);
      command = pgagroal_append_char(command, ' ');
   }

   // add host details
   command = pgagroal_append(command, "-h ");
   command = pgagroal_append(command, config->common.host);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-p ");
   command = pgagroal_append_int(command, config->common.port);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-U ");
   command = pgagroal_append(command, user);
   command = pgagroal_append_char(command, ' ');

   command = pgagroal_append(command, "-d ");
   command = pgagroal_append(command, database);

   command = pgagroal_append(command, " >> "); // append to the file
   command = pgagroal_append(command, log_file_path);
   command = pgagroal_append(command, " 2>&1");
   command = pgagroal_append(command, " < /dev/null");

   /* Prepend PGPASSWORD to command if password is available */
   if (password != NULL && strlen(password) > 0)
   {
      size_t cmd_len = strlen(command);
      size_t pwd_len = strlen(password);
      /* Format: "PGPASSWORD=%s %s\0" = 11 + pwd_len + 1 + cmd_len + 1 */
      size_t total_len = 11 + pwd_len + 1 + cmd_len + 1;

      command_with_password = (char*)calloc(total_len, sizeof(char));
      if (command_with_password != NULL)
      {
         snprintf(command_with_password, total_len, "PGPASSWORD=%s %s", password, command);
         ret = system(command_with_password);
         free(command_with_password);
      }
      else
      {
         /* Memory allocation failed - fallback to command without password (will fail immediately due to < /dev/null) */
         ret = system(command);
      }
   }
   else
   {
      /* No password found - pgbench will fail immediately (due to < /dev/null) */
      ret = system(command);
   }

   free(command);
   free(log_file_path);

   return ret;
}

int
pgagroal_tsclient_execute_concurrent_holds(char* user, char* database, int client_count, int hold_seconds)
{
   struct main_configuration* config = NULL;
   pthread_t* threads = NULL;
   struct hold_thread_args* args = NULL;
   char* log_file_path = NULL;
   const char* password = NULL;
   int failures = 0;
   int spawned = 0;
   int watchdog;
   int rc;

   if (client_count <= 0 || hold_seconds < 0)
   {
      return 1;
   }

   config = (struct main_configuration*)shmem;
   log_file_path = get_log_file_path();

   /* Same precedence used by pgagroal_tsclient_execute_pgbench */
   password = getenv("PGPASSWORD");
   if (password == NULL)
   {
      password = getenv("PG_USER_PASSWORD");
   }
   if (password == NULL)
   {
      password = getenv("PG_UTF8_USER_PASSWORD");
   }

   threads = (pthread_t*)calloc(client_count, sizeof(pthread_t));
   args = (struct hold_thread_args*)calloc(client_count, sizeof(struct hold_thread_args));
   if (threads == NULL || args == NULL)
   {
      failures = client_count;
      goto cleanup;
   }

   /* Per-client watchdog: bound each psql invocation with timeout(1) so a
    * client that never returns cannot leave the run hung in pthread_join. See
    * compute_client_watchdog() for how the bound is derived. */
   watchdog = compute_client_watchdog(config, hold_seconds);

   for (int i = 0; i < client_count; i++)
   {
      char* cmd = NULL;
      char  sql[128];

      snprintf(sql, sizeof(sql), "BEGIN; SELECT pg_sleep(%d); COMMIT;", hold_seconds);

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
      cmd = pgagroal_append(cmd, "\" >> ");
      cmd = pgagroal_append(cmd, log_file_path);
      cmd = pgagroal_append(cmd, " 2>&1 < /dev/null");

      args[i].command = cmd;
      args[i].exit_status = -1;

      rc = pthread_create(&threads[i], NULL, hold_thread_main, &args[i]);
      if (rc != 0)
      {
         /* Could not spawn this client; count it as a failure and stop spawning */
         failures++;
         break;
      }
      spawned++;
   }

   for (int i = 0; i < spawned; i++)
   {
      pthread_join(threads[i], NULL);
      if (args[i].exit_status != 0)
      {
         failures++;
      }
   }

cleanup:
   if (args != NULL)
   {
      for (int i = 0; i < client_count; i++)
      {
         free(args[i].command);
      }
      free(args);
   }
   free(threads);
   free(log_file_path);

   return (failures == 0) ? 0 : 1;
}

int
pgagroal_tsclient_limit_backend_peak(char* user, char* database, int client_count, int hold_seconds)
{
   struct main_configuration* config = NULL;
   pthread_t* threads = NULL;
   struct hold_thread_args* args = NULL;
   char* log_file_path = NULL;
   const char* password = NULL;
   int metrics_port;
   int spawned = 0;
   int peak = -1;
   int watchdog;
   int rc;

   if (client_count <= 0 || hold_seconds <= 0)
   {
      return -1;
   }

   config = (struct main_configuration*)shmem;
   metrics_port = config->common.metrics;
   if (metrics_port <= 0)
   {
      /* Metrics endpoint disabled; the caller skips the assertion. */
      return -1;
   }

   log_file_path = get_log_file_path();

   /* Same precedence used by pgagroal_tsclient_execute_concurrent_holds */
   password = getenv("PGPASSWORD");
   if (password == NULL)
   {
      password = getenv("PG_USER_PASSWORD");
   }
   if (password == NULL)
   {
      password = getenv("PG_UTF8_USER_PASSWORD");
   }

   threads = (pthread_t*)calloc(client_count, sizeof(pthread_t));
   args = (struct hold_thread_args*)calloc(client_count, sizeof(struct hold_thread_args));
   if (threads == NULL || args == NULL)
   {
      goto cleanup;
   }

   /* Per-client watchdog. A psql session may legitimately wait up to
    * blocking_timeout for a slot and then hold for hold_seconds (and the rule
    * is oversubscribed here, so the surplus waits out a full hold wave before
    * acquiring a freed slot). Bound each invocation generously above that with
    * timeout(1) so a client that never returns -- e.g. a backend that stalls
    * under oversubscription -- is killed and its thread can be joined, instead
    * of leaving the run (and the CI job) hung indefinitely in pthread_join. */
   watchdog = compute_client_watchdog(config, hold_seconds);

   for (int i = 0; i < client_count; i++)
   {
      char* cmd = NULL;
      char  sql[128];

      snprintf(sql, sizeof(sql), "BEGIN; SELECT pg_sleep(%d); COMMIT;", hold_seconds);

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
      cmd = pgagroal_append(cmd, "\" >> ");
      cmd = pgagroal_append(cmd, log_file_path);
      cmd = pgagroal_append(cmd, " 2>&1 < /dev/null");

      args[i].command = cmd;
      args[i].exit_status = -1;

      rc = pthread_create(&threads[i], NULL, hold_thread_main, &args[i]);
      if (rc != 0)
      {
         break;
      }
      spawned++;
   }

   /* Sample the per-rule live-backend metric while the holds are active. The
    * first max_size clients take every backend slot and hold it for
    * hold_seconds; the surplus block on the per-rule cap. The peak of the
    * pgagroal_limit type="backend" series over the window is the live backend
    * count for the rule, which the #848 hard cap bounds at max_size. Sampling
    * spans roughly two hold cycles so both the first and the second wave are
    * observed. */
   if (spawned > 0)
   {
      struct timespec poll = {0, 200L * 1000L * 1000L};   /* 200ms */
      int samples = ((hold_seconds * 2 + 1) * 1000) / 200;

      for (int s = 0; s < samples; s++)
      {
         int v = scrape_limit_backend(metrics_port, config->common.host, user, database);
         if (v > peak)
         {
            peak = v;
         }
         nanosleep(&poll, NULL);
      }
   }

   for (int i = 0; i < spawned; i++)
   {
      pthread_join(threads[i], NULL);
   }

cleanup:
   if (args != NULL)
   {
      for (int i = 0; i < client_count; i++)
      {
         free(args[i].command);
      }
      free(args);
   }
   free(threads);
   free(log_file_path);

   return peak;
}

static int
scrape_limit_backend(int metrics_port, char* host, char* user, char* database)
{
   char tmp[] = "/tmp/pgagroal_metrics_XXXXXX";
   char prefix[512];
   char line[8192];
   char* cmd = NULL;
   FILE* f = NULL;
   int fd;
   int value = -1;

   fd = mkstemp(tmp);
   if (fd < 0)
   {
      return -1;
   }
   close(fd);

   cmd = pgagroal_append(cmd, "curl -s -m 2 http://");
   cmd = pgagroal_append(cmd, host);
   cmd = pgagroal_append_char(cmd, ':');
   cmd = pgagroal_append_int(cmd, metrics_port);
   cmd = pgagroal_append(cmd, "/metrics -o ");
   cmd = pgagroal_append(cmd, tmp);

   if (system(cmd) != 0)
   {
      free(cmd);
      unlink(tmp);
      return -1;
   }
   free(cmd);

   /* The metric is one line per rule:
    * pgagroal_limit{user="U",database="D",type="backend"} N */
   snprintf(prefix, sizeof(prefix),
            "pgagroal_limit{user=\"%s\",database=\"%s\",type=\"backend\"", user, database);

   f = fopen(tmp, "r");
   if (f != NULL)
   {
      while (fgets(line, sizeof(line), f) != NULL)
      {
         if (strstr(line, prefix) != NULL)
         {
            char* p = strstr(line, "} ");
            if (p != NULL)
            {
               value = atoi(p + 2);
            }
            break;
         }
      }
      fclose(f);
   }

   unlink(tmp);
   return value;
}

/*
 * Compute a per-client watchdog (seconds) for the concurrent-hold helpers.
 * A client may legitimately wait up to blocking_timeout for a slot and then
 * hold for hold_seconds; under oversubscription the surplus rides out a full
 * hold wave before acquiring a freed slot. The bound is generously above that
 * worst-case legitimate runtime so it only fires on a genuinely stuck client.
 * blocking_timeout may be -1 (treated as infinite by pgagroal); there a bare
 * fixed ceiling is used so the run cannot hang forever.
 */
static int
compute_client_watchdog(struct main_configuration* config, int hold_seconds)
{
   int64_t blocking = (config != NULL) ? config->blocking_timeout.s : 0;

   if (blocking < 0)
   {
      return (hold_seconds * 2) + 60;
   }

   return (int)blocking + (hold_seconds * 2) + 10;
}

static void*
hold_thread_main(void* arg)
{
   struct hold_thread_args* a = (struct hold_thread_args*)arg;

   if (a == NULL || a->command == NULL)
   {
      return NULL;
   }
   a->exit_status = system(a->command);
   return NULL;
}

static char*
get_configuration_path()
{
   char* configuration_path = NULL;
   int project_directory_length = strlen(project_directory);
   int configuration_trail_length = strlen(PGAGROAL_CONFIGURATION_TRAIL);

   configuration_path = (char*)calloc(project_directory_length + configuration_trail_length + 1, sizeof(char));

   memcpy(configuration_path, project_directory, project_directory_length);
   memcpy(configuration_path + project_directory_length, PGAGROAL_CONFIGURATION_TRAIL, configuration_trail_length);

   return configuration_path;
}

static char*
get_databases_path()
{
   char* databases_path = NULL;
   int project_directory_length = strlen(project_directory);
   int databases_trail_length = strlen(PGAGROAL_DATABASES_TRAIL);

   databases_path = (char*)calloc(project_directory_length + databases_trail_length + 1, sizeof(char));

   memcpy(databases_path, project_directory, project_directory_length);
   memcpy(databases_path + project_directory_length, PGAGROAL_DATABASES_TRAIL, databases_trail_length);

   return databases_path;
}

static char*
get_log_file_path()
{
   char* log_file_path = NULL;
   int project_directory_length = strlen(project_directory);
   int log_trail_length = strlen(PGBENCH_LOG_FILE_TRAIL);

   log_file_path = (char*)calloc(project_directory_length + log_trail_length + 1, sizeof(char));

   memcpy(log_file_path, project_directory, project_directory_length);
   memcpy(log_file_path + project_directory_length, PGBENCH_LOG_FILE_TRAIL, log_trail_length);

   return log_file_path;
}

int
pgagroal_tsclient_execute_cli(char* args)
{
   char* configuration_path = NULL;
   char* cmd = NULL;
   char* conf = NULL;
   char* bin_dir = NULL;
   int ret;

   /* Target the running instance's own config (exported by the test harness) so
    * the CLI reaches the same management endpoint; fall back to the tsclient
    * configuration path. */
   conf = getenv("PGAGROAL_TEST_CONF");
   if (conf == NULL)
   {
      configuration_path = get_configuration_path();
      conf = configuration_path;
   }
   if (conf == NULL)
   {
      return 1;
   }

   /* pgagroal-cli is not installed on PATH during tests; the harness exports its
    * directory (PGAGROAL_TEST_BIN). */
   bin_dir = getenv("PGAGROAL_TEST_BIN");

   if (bin_dir != NULL)
   {
      cmd = pgagroal_append(cmd, bin_dir);
      cmd = pgagroal_append_char(cmd, '/');
   }
   cmd = pgagroal_append(cmd, "pgagroal-cli -c ");
   cmd = pgagroal_append(cmd, conf);
   cmd = pgagroal_append_char(cmd, ' ');
   cmd = pgagroal_append(cmd, args);

   ret = system(cmd);

   free(cmd);
   free(configuration_path);

   return (ret == 0) ? 0 : 1;
}

int
pgagroal_tsclient_cli_during_hold(char* user, char* database, char* cli_args, int hold_seconds)
{
   pid_t hold;
   int cli_rc;
   int status;

   if (hold_seconds < 2)
   {
      hold_seconds = 3;
   }

   /* Hold one client connected in a child process so a transaction worker is
    * live while the CLI runs, reusing the existing hold machinery
    * (BEGIN; SELECT pg_sleep(n); COMMIT;). */
   hold = fork();
   if (hold < 0)
   {
      return 1;
   }
   if (hold == 0)
   {
      int rc = pgagroal_tsclient_execute_concurrent_holds(user, database, 1, hold_seconds);
      _exit(rc == 0 ? 0 : 1);
   }

   /* Let the held client connect, then run the management command while it is
    * still connected. */
   sleep(1);
   cli_rc = pgagroal_tsclient_execute_cli(cli_args);

   if (waitpid(hold, &status, 0) < 0)
   {
      return 1;
   }

   /* A false pass is worse than a failure: if the held client never ran, the
    * CLI would trivially succeed. Require both the hold and the CLI to succeed. */
   if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
   {
      return 1;
   }

   return cli_rc;
}
