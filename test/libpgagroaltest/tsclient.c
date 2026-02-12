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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

char project_directory[BUFFER_SIZE];

static char* get_configuration_path();
static char* get_log_file_path();

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
         /* Fallback: try to setenv (may not work in all cases) */
         setenv("PGPASSWORD", password, 1);
         ret = system(command);
      }
   }
   else
   {
      /* No password found - pgbench will prompt (or fail if non-interactive) */
      ret = system(command);
   }

   if (command != NULL && ret == 0)
   {
      ret = EXIT_SUCCESS;
   }

   free(log_file_path);
   free(command);
   return ret;
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
