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
#include <health.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <pgagroal.h>
#include <server.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void health_check_loop(void);
static int verify_standby_replication(char previous_warned_slots[][MISC_LENGTH],
                                      int* previous_warned_count);
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

      /* Wait up to 2s for exit - the worker checks its flags every 1s via sleep(1) */
      for (int i = 0; i < 10; i++)
      {
         if (kill(config->health_check_pid, 0))
         {
            break;
         }
         SLEEP(200000000L);
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
   char previous_warned_slots[NUMBER_OF_SERVERS][MISC_LENGTH];
   int previous_warned_count = 0;
   int32_t t;

   config = (struct main_configuration*)shmem;

   period = config->health_check_period;

   pgagroal_log_info("Health check started");

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      previous_state[i] = -2; /* Initial value representing 'never checked' */
      atomic_store(&config->servers[i].replication_ok, true);
   }

   while (config->keep_running && config->health_check)
   {
      /* Sleep for the configured period, but check flags every second */
      period = config->health_check_period;
      t = pgagroal_time_convert(period, FORMAT_TIME_S);
      for (int32_t i = 0; i < t && config->keep_running && config->health_check; i++)
      {
         sleep(1);
      }

      if (!config->keep_running || !config->health_check)
      {
         break;
      }

      pgagroal_log_debug("Health check run");

      FOREACH_VALID_SERVER
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

         int current_state = atomic_load(&config->servers[i].state);
         if (current_state == SERVER_PRIMARY || current_state == SERVER_NOTINIT_PRIMARY)
         {
            atomic_store(&config->servers[i].streaming_state, SERVER_STREAMING_PRIMARY);
         }
         else if (up)
         {
            char rep_status[64] = {0};
            char slot_name[64] = {0};
            char sender_host[MISC_LENGTH] = {0};
            char sender_port[16] = {0};

            int wal_result = pgagroal_server_get_wal_receiver_status(i,
                                                                     rep_status, sizeof(rep_status),
                                                                     slot_name, sizeof(slot_name),
                                                                     sender_host, sizeof(sender_host),
                                                                     sender_port, sizeof(sender_port));
            bool is_streaming = pgagroal_strcmp(rep_status, "streaming");

            if (wal_result == 0)
            {
               if (is_streaming)
               {
                  atomic_store(&config->servers[i].streaming_state, SERVER_STREAMING_YES);
               }
               else
               {
                  atomic_store(&config->servers[i].streaming_state, SERVER_STREAMING_NO);
               }
               if (!pgagroal_strcmp(slot_name, ""))
               {
                  if (!pgagroal_strcmp(config->servers[i].replication_slot_name, slot_name))
                  {
                     memcpy(config->servers[i].replication_slot_name, slot_name, strlen(slot_name) + 1);
                  }
               }
               else
               {
                  if (config->servers[i].replication_slot_name[0] != '\0')
                  {
                     memset(config->servers[i].replication_slot_name, 0, MISC_LENGTH);
                  }
               }
            }
            else
            {
               pgagroal_log_debug("server %d failed to receive wal_result, setting streaming state to NO", i);
               atomic_store(&config->servers[i].streaming_state, SERVER_STREAMING_NO);
            }
         }
         else
         {
            pgagroal_log_debug("server %d not up defaulting streaming state to No", i);
            atomic_store(&config->servers[i].streaming_state, SERVER_STREAMING_NO);
         }
      }

      verify_standby_replication(previous_warned_slots, &previous_warned_count);
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

   config = (struct main_configuration*)shmem;

   *up = false;
   *auth_type = HEALTH_CHECK_AUTH_UNKNOWN;

   pgagroal_log_debug("Health: Probing server %d (%s:%d) as user %s",
                      server_idx, config->servers[server_idx].host,
                      config->servers[server_idx].port, config->health_check_user);

   if (pgagroal_server_query_execute(server_idx,
                                     config->health_check_user,
                                     config->health_check_user,
                                     "SELECT 1",
                                     MAX(1, (int)pgagroal_time_convert(config->health_check_timeout, FORMAT_TIME_S)),
                                     auth_type, &fd) != 0)
   {
      pgagroal_log_debug("Health: Failed to connect to server %d", server_idx);
      return 1;
   }

   /* Wait for query response */
   while (true)
   {
      status = pgagroal_read_timeout_message(NULL, fd,
                                             MAX(1, (int)pgagroal_time_convert(config->health_check_timeout, FORMAT_TIME_S)),
                                             &msg);
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

   (void)pgagroal_write_terminate(NULL, fd);

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

static int
verify_standby_replication(char previous_warned_slots[][MISC_LENGTH],
                           int* previous_warned_count)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   char primary_slot_names[NUMBER_OF_SERVERS][MISC_LENGTH];
   char primary_client_addrs[NUMBER_OF_SERVERS][MISC_LENGTH];
   int primary_rows = 0;
   int primary = -1;
   bool all_ok = true;
   char new_warned_slots[NUMBER_OF_SERVERS][MISC_LENGTH];
   int new_warned_count = 0;
   int row_idx = 0;
   bool already_warned = false;
   int warned_idx = 0;
   int new_warned_idx = 0;

   if (pgagroal_get_primary(&primary))
   {
      pgagroal_log_debug("Could not determine primary server, skipping replication verification");
      return HEALTH_CHECK_REPLICATION_VERIFY_SKIPPED;
   }

   if (pgagroal_server_get_replication_slots_status(primary, primary_slot_names, primary_client_addrs,
                                                    NUMBER_OF_SERVERS, &primary_rows))
   {
      pgagroal_log_debug("Failed to get replication slots status from primary '%s', skipping replication verification", config->servers[primary].name);
      return HEALTH_CHECK_REPLICATION_VERIFY_SKIPPED;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (config->servers[i].valid)
      {
         bool matched = false;
         if (i == primary || config->servers[i].replication_slot_name[0] == '\0')
         {
            continue;
         }

         for (row_idx = 0; row_idx < primary_rows; row_idx++)
         {
            if (pgagroal_strcmp(primary_slot_names[row_idx], config->servers[i].replication_slot_name))
            {
               matched = true;
               break;
            }
         }

         if (!matched)
         {
            all_ok = false;
            if (atomic_load(&config->servers[i].replication_ok))
            {
               pgagroal_log_error("Standby '%s' reports replication slot '%s', but primary '%s' has no matching active connection for that slot; possible misconfiguration",
                                  config->servers[i].name, config->servers[i].replication_slot_name, config->servers[primary].name);
            }
         }
         else if (!atomic_load(&config->servers[i].replication_ok))
         {
            pgagroal_log_info("Standby '%s' replication slot '%s' now matches primary '%s'; previously reported error has cleared",
                              config->servers[i].name, config->servers[i].replication_slot_name, config->servers[primary].name);
         }
         atomic_store(&config->servers[i].replication_ok, matched);
      }
   }

   for (row_idx = 0; row_idx < primary_rows; row_idx++)
   {
      bool known = false;

      if (pgagroal_strcmp(primary_slot_names[row_idx], ""))
      {
         continue;
      }

      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (config->servers[i].valid)
         {
            if (i == primary || pgagroal_strcmp(config->servers[i].replication_slot_name, ""))
            {
               continue;
            }
            if (pgagroal_strcmp(config->servers[i].replication_slot_name, primary_slot_names[row_idx]))
            {
               known = true;
               break;
            }
         }
      }

      if (!known)
      {
         already_warned = false;
         for (warned_idx = 0; warned_idx < *previous_warned_count; warned_idx++)
         {
            if (pgagroal_strcmp(previous_warned_slots[warned_idx], primary_slot_names[row_idx]))
            {
               already_warned = true;
               break;
            }
         }

         if (!already_warned)
         {
            pgagroal_log_warn("Primary '%s' has an active replication connection using slot '%s' from %s that does not match any configured standby",
                              config->servers[primary].name, primary_slot_names[row_idx], primary_client_addrs[row_idx]);
         }

         if (new_warned_count < NUMBER_OF_SERVERS)
         {
            memcpy(new_warned_slots[new_warned_count], primary_slot_names[row_idx], strlen(primary_slot_names[row_idx]) + 1);
            new_warned_count++;
         }
      }
   }

   for (warned_idx = 0; warned_idx < *previous_warned_count; warned_idx++)
   {
      bool still_unmatched = false;
      for (new_warned_idx = 0; new_warned_idx < new_warned_count; new_warned_idx++)
      {
         if (pgagroal_strcmp(previous_warned_slots[warned_idx], new_warned_slots[new_warned_idx]))
         {
            still_unmatched = true;
            break;
         }
      }

      if (!still_unmatched)
      {
         pgagroal_log_info("Primary '%s' no longer has an unrecognized replication connection using slot '%s'",
                           config->servers[primary].name, previous_warned_slots[warned_idx]);
      }
   }

   for (new_warned_idx = 0; new_warned_idx < new_warned_count; new_warned_idx++)
   {
      memcpy(previous_warned_slots[new_warned_idx], new_warned_slots[new_warned_idx], strlen(new_warned_slots[new_warned_idx]) + 1);
   }
   *previous_warned_count = new_warned_count;

   return all_ok ? HEALTH_CHECK_REPLICATION_VERIFY_OK : HEALTH_CHECK_REPLICATION_VERIFY_FAILED;
}