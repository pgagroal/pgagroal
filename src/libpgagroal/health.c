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

/* pgagroal */
#include <pgagroal.h>
#include <health.h>
#include <logging.h>
#include <network.h>
#include <server.h>
#include <shmem.h>
#include <utils.h>
#include <message.h>

/* system */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>

static void health_check_loop(void);
static int server_probe(int server_idx, bool* up, int* auth_type);

/**
 * Entry point for the health check worker
 */
void
pgagroal_health_check(int argc, char** argv)
{
   pid_t pid;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < 100; i++)
   {
      pid = fork();
      if (pid != -1)
      {
         break;
      }
      SLEEP(10000000L);
   }

   if (pid == -1)
   {
      pgagroal_log_error("Unable to fork health check process");
      return;
   }
   else if (pid == 0)
   {
      pgagroal_start_logging();

      /* Restore default signal handlers so we can be terminated by main process */
      signal(SIGTERM, SIG_DFL);
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      pgagroal_set_proc_title(argc, argv, "health check worker", NULL);
      health_check_loop();
      exit(0);
   }
   else
   {
      config->health_check_pid = pid;
   }
}

/**
 * Stops the health check worker
 */
void
pgagroal_health_check_stop(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->health_check_pid != 0)
   {
      kill(config->health_check_pid, SIGTERM);

      for (int i = 0; i < 50; i++)
      {
         if (kill(config->health_check_pid, 0))
         {
            break;
         }
         SLEEP(100000000L);
      }

      waitpid(config->health_check_pid, NULL, WNOHANG);
      config->health_check_pid = 0;
   }
}

/**
 * Main loop for the health check worker
 */
static void
health_check_loop(void)
{
   struct main_configuration* config;
   pgagroal_time_t period;
   bool up;
   int status;
   int previous_state[NUMBER_OF_SERVERS];
   int32_t t;

   config = (struct main_configuration*)shmem;

   period = config->health_check_period;

   pgagroal_log_info("Health check started");

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      previous_state[i] = -2; /* Initial value representing 'never checked' */
   }

   while (config->keep_running)
   {
      /* Sleep for the configured period, but check keep_running every second */
      t = pgagroal_time_convert(period, FORMAT_TIME_S);
      for (int32_t i = 0; i < t && config->keep_running; i++)
      {
         sleep(1);
      }

      if (!config->keep_running)
      {
         break;
      }

      pgagroal_log_debug("Health check run");

      for (int i = 0; i < config->number_of_servers; i++)
      {
         int auth = HEALTH_CHECK_AUTH_UNKNOWN;
         up = false;
         status = server_probe(i, &up, &auth);

         /* status != 0 means connection or protocol error, but we treat it as 'not up' for retries */
         if (status != 0)
         {
            up = false;
            atomic_store(&config->servers[i].auth_type, HEALTH_CHECK_AUTH_ERROR);
         }
         else
         {
            atomic_store(&config->servers[i].auth_type, auth);
         }

         if (up)
         {
            config->servers[i].failures = 0;
            if (previous_state[i] != SERVER_HEALTH_UP)
            {
               pgagroal_log_info("Health: Server %d is UP", i);
               previous_state[i] = SERVER_HEALTH_UP;
            }
            atomic_store(&config->servers[i].health_state, SERVER_HEALTH_UP);
         }
         else
         {
            config->servers[i].failures++;
            if (config->servers[i].failures >= HEALTH_CHECK_MAX_RETRIES)
            {
               if (previous_state[i] != SERVER_HEALTH_DOWN)
               {
                  pgagroal_log_warn("Health: Server %d is DOWN", i);
                  previous_state[i] = SERVER_HEALTH_DOWN;
               }
               atomic_store(&config->servers[i].health_state, SERVER_HEALTH_DOWN);
            }
         }
      }
   }

   pgagroal_log_info("Health check stopped");
}

static int
server_probe(int server_idx, bool* up, int* auth_type)
{
   struct main_configuration* config;
   int fd = -1;
   struct message* msg = NULL;
   int status;
   bool query_success = false;
   bool query_ready = false;
   int offset_q;
   char type_q;
   int len_q;
   char buffer[8];

   config = (struct main_configuration*)shmem;

   *up = false;
   *auth_type = HEALTH_CHECK_AUTH_UNKNOWN;

   pgagroal_log_debug("Health: Probing server %d (%s:%d) as user %s",
                      server_idx, config->servers[server_idx].host,
                      config->servers[server_idx].port, config->health_check_user);

   if (pgagroal_server_query_execute(server_idx, config->health_check_user, config->health_check_user, "SELECT 1", auth_type, &fd) != 0)
   {
      pgagroal_log_debug("Health: Failed to connect to server %d", server_idx);
      return 1;
   }

   /* Wait for query response */
   while (true)
   {
      status = pgagroal_read_timeout_message(NULL, fd, 5, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
      {
         pgagroal_log_debug("Health: Failed to read query response (status %d)", status);
         goto error;
      }

      offset_q = 0;
      while (offset_q < msg->length)
      {
         type_q = pgagroal_read_byte(msg->data + offset_q);
         len_q = pgagroal_read_int32(msg->data + offset_q + 1);

         pgagroal_log_debug("Health Trace Query: loop msg kind=%c len=%d offset=%d total=%zd", type_q, len_q, offset_q, msg->length);

         if (type_q == 'T' || type_q == 'C' || type_q == 'D')
         {
            query_success = true;
         }
         else if (type_q == 'E')
         {
            query_success = false;
         }
         else if (type_q == 'Z')
         {
            query_ready = true;
            break;
         }

         offset_q += 1 + len_q;
         if (offset_q >= msg->length)
         {
            break;
         }
      }

      pgagroal_clear_message(msg);
      msg = NULL;

      if (query_ready)
      {
         break;
      }
   }

   /* Send Terminate */
   buffer[0] = 'X';
   pgagroal_write_int32(buffer + 1, 4);
   pgagroal_write_socket(NULL, fd, buffer, 5);

   pgagroal_disconnect(fd);

   *up = query_success;
   return 0;

error:
   if (msg != NULL)
   {
      pgagroal_clear_message(msg);
   }
   if (fd != -1)
   {
      pgagroal_disconnect(fd);
   }
   return 1;
}
