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
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

char project_directory[BUFFER_SIZE];

struct hold_thread_args
{
   char* command;
   int   exit_status;
};

static char* get_configuration_path();
static char* get_log_file_path();
static void* hold_thread_main(void* arg);

int
pgagroal_tsclient_init(char* base_dir)
{
   int ret;
   size_t size;
   char* configuration_path = NULL;

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

      cmd = pgagroal_append(cmd, "psql -h ");
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
