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
#include <connection.h>
#include <logging.h>
#include <network.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <pool.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <tracker.h>
#include <utils.h>
#include <configuration.h>

/* system */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int find_best_rule(char* username, char* database);
static bool remove_connection(char* username, char* database);
static void connection_details(int slot);
static bool do_prefill(char* username, char* database, int size);
static bool is_alias_of_limit(char* database, int limit_index);
static int get_connection_count_for_limit_rule(int rule_index, char* username);
static char* resolve_database_name(char* database, int best_rule);
static void check_graceful_shutdown_trigger(void);

int
pgagroal_get_connection(char* username, char* database, bool reuse, bool transaction_mode, int* slot, SSL** ssl)
{
   bool do_init;
   bool has_lock;
   int connections;
   signed char not_init;
   signed char free;
   int server;
   int fd;
   time_t start_time;
   int best_rule;
   int retries;
   int ret;
   char* real_database;

   struct main_configuration* config;
   struct main_prometheus* prometheus;

   config = (struct main_configuration*)shmem;
   prometheus = (struct main_prometheus*)prometheus_shmem;

   pgagroal_prometheus_connection_get();

   best_rule = find_best_rule(username, database);
   retries = 0;
   start_time = time(NULL);
   pgagroal_prometheus_connection_awaiting(best_rule);

start:

   *slot = -1;
   *ssl = NULL;
   do_init = false;
   has_lock = false;

   connections = atomic_fetch_add(&config->active_connections, 1);
   has_lock = true;
   if (connections >= config->max_connections)
   {
      goto retry;
   }

   if (reuse)
   {
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         free = STATE_FREE;

         if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_IN_USE))
         {
            bool can_reuse = false;

            // Check if same rule and username
            if (best_rule == config->connections[i].limit_rule &&
                !strcmp((const char*)(&config->connections[i].username), username))
            {
               real_database = resolve_database_name(database, best_rule);
               // Check exact database match
               if (!strcmp((const char*)(&config->connections[i].database), real_database))
               {
                  can_reuse = true;
               }
               // Check if both are aliases of the same real database
            }

            if (can_reuse)
            {
               *slot = i;
            }
            else
            {
               atomic_store(&config->states[i], STATE_FREE);
            }
         }
      }
   }

   if (*slot == -1 && !transaction_mode)
   {
      if (best_rule >= 0)
      {
         int rule_count = get_connection_count_for_limit_rule(best_rule, username);
         if (rule_count >= config->limits[best_rule].max_size)
         {
            goto retry;
         }
      }

      /* Ok, try and create a new connection */
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         not_init = STATE_NOTINIT;

         if (atomic_compare_exchange_strong(&config->states[i], &not_init, STATE_INIT))
         {
            *slot = i;
            do_init = true;
         }
      }
   }

   if (*slot != -1)
   {
      config->connections[*slot].limit_rule = best_rule;
      config->connections[*slot].pid = getpid();

      if (do_init)
      {
         /* We need to find the server for the connection */
         if (pgagroal_get_primary(&server))
         {
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            if (!fork())
            {
               pgagroal_flush(FLUSH_GRACEFULLY, "*");
               exit(0);
            }

            goto error;
         }

         pgagroal_log_debug("connect: server %d", server);

         if (config->servers[server].host[0] == '/')
         {
            char pgsql[MISC_LENGTH];

            memset(&pgsql, 0, sizeof(pgsql));
            snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
            ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], &fd);
         }
         else
         {
            ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, &fd, config->keep_alive, config->nodelay);
         }

         if (ret)
         {
            pgagroal_log_error("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            pgagroal_prometheus_server_error(server);

            if (!fork())
            {
               pgagroal_flush_server(server);
            }

            if (config->failover)
            {
               pgagroal_server_force_failover(server);
               pgagroal_prometheus_failed_servers();
               goto retry;
            }

            goto error;
         }

         pgagroal_log_debug("connect: %s:%d using slot %d fd %d", config->servers[server].host, config->servers[server].port, *slot, fd);

         config->connections[*slot].server = server;

         memset(&config->connections[*slot].username, 0, MAX_USERNAME_LENGTH);
         memcpy(&config->connections[*slot].username, username, MIN(strlen(username), MAX_USERNAME_LENGTH - 1));
         real_database = resolve_database_name(database, best_rule);
         memset(&config->connections[*slot].database, 0, MAX_DATABASE_LENGTH);
         memcpy(&config->connections[*slot].database, real_database, MIN(strlen(real_database), MAX_DATABASE_LENGTH - 1));

         pgagroal_log_debug("Connection setup: client_db='%s' -> postgres_db='%s'", database, real_database);
         config->connections[*slot].has_security = SECURITY_INVALID;
         config->connections[*slot].fd = fd;

         atomic_store(&config->states[*slot], STATE_IN_USE);
      }
      else
      {
         bool kill = false;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[*slot].fd))
         {
            if (!transaction_mode)
            {
               kill = true;
            }
            else
            {
               atomic_store(&config->states[*slot], STATE_FREE);
               goto retry;
            }
         }

         if (!kill && config->validation == VALIDATION_FOREGROUND)
         {
            kill = !pgagroal_connection_isvalid(config->connections[*slot].fd);
         }

         if (kill)
         {
            int status;

            pgagroal_log_debug("pgagroal_get_connection: Slot %d FD %d - Error", *slot, config->connections[*slot].fd);
            pgagroal_tracking_event_slot(TRACKER_BAD_CONNECTION, *slot);
            status = pgagroal_kill_connection(*slot, *ssl);

            pgagroal_prefill_if_can(true, false);

            if (status == 0)
            {
               goto retry2;
            }
            else
            {
               goto timeout;
            }
         }
      }

      if (config->connections[*slot].start_time == -1)
      {
         config->connections[*slot].start_time = time(NULL);
      }

      config->connections[*slot].timestamp = time(NULL);

      if (config->common.metrics > 0)
      {
         atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
      }
      pgagroal_prometheus_connection_success();
      pgagroal_tracking_event_slot(TRACKER_GET_CONNECTION_SUCCESS, *slot);
      pgagroal_prometheus_connection_unawaiting(best_rule);
      return 0;
   }
   else
   {
retry:
      if (best_rule >= 0)
      {
         atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
      }
      if (has_lock)
      {
         atomic_fetch_sub(&config->active_connections, 1);
      }
retry2:
      if (pgagroal_time_is_valid(config->blocking_timeout))
      {
         /* Sleep for 500ms */
         SLEEP(500000000L)

         double diff = difftime(time(NULL), start_time);
         if (diff >= (double)pgagroal_time_convert(config->blocking_timeout, FORMAT_TIME_S))
         {
            goto timeout;
         }

         if (best_rule == -1)
         {
            remove_connection(username, database);
         }

         goto start;
      }
      else
      {
         if (!transaction_mode)
         {
            if (best_rule == -1)
            {
               if (remove_connection(username, database))
               {
                  if (retries < config->max_retries)
                  {
                     retries++;
                     goto start;
                  }
               }
            }
            else
            {
               if (retries < config->max_retries)
               {
                  retries++;
                  goto start;
               }
            }
         }
         else
         /* Sleep for 1000 nanos */
         {
            SLEEP_AND_GOTO(1000L, start)
         }
      }
   }

timeout:
   if (config->common.metrics > 0)
   {
      atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   }
   pgagroal_prometheus_connection_timeout();
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_TIMEOUT, username, database);
   pgagroal_prometheus_connection_unawaiting(best_rule);
   return 1;

error:
   if (best_rule >= 0)
   {
      atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
   }
   atomic_fetch_sub(&config->active_connections, 1);
   if (config->common.metrics > 0)
   {
      atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   }
   pgagroal_prometheus_connection_error();
   pgagroal_prometheus_connection_unawaiting(best_rule);
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_ERROR, username, database);

   return 2;
}

int
pgagroal_return_connection(int slot, SSL* ssl, bool transaction_mode)
{
   int state;
   struct main_configuration* config;
   time_t now;
   signed char in_use;
   signed char age_check;
   int transfer_fd = -1;

   config = (struct main_configuration*)shmem;

   /* Kill the connection, if it lives longer than max_connection_age */
   if (pgagroal_time_is_valid(config->max_connection_age))
   {
      now = time(NULL);
      in_use = STATE_IN_USE;
      age_check = STATE_MAX_CONNECTION_AGE;
      if (atomic_compare_exchange_strong(&config->states[slot], &in_use, age_check))
      {
         double age = difftime(now, config->connections[slot].start_time);
         if ((age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S) && !config->connections[slot].tx_mode) ||
             !atomic_compare_exchange_strong(&config->states[slot], &age_check, STATE_IN_USE))
         {
            pgagroal_prometheus_connection_max_connection_age();
            pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, slot);
            goto kill_connection;
         }
      }
   }

   /* Verify the socket for the slot */
   if (!transaction_mode && !pgagroal_socket_isvalid(config->connections[slot].fd))
   {
      pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d - Error", slot, config->connections[slot].fd);
      config->connections[slot].has_security = SECURITY_INVALID;
   }

   /* Can we cache this connection ? */
   if (config->connections[slot].has_security != SECURITY_INVALID &&
       (config->connections[slot].has_security != SECURITY_SCRAM256 ||
        (config->connections[slot].has_security == SECURITY_SCRAM256 &&
         (config->authquery || pgagroal_user_known(config->connections[slot].username)))) &&
       ssl == NULL)
   {
      state = atomic_load(&config->states[slot]);

      /* Return the connection, if not GRACEFULLY */
      if (state == STATE_IN_USE)
      {
         pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d", slot, config->connections[slot].fd);

         if (!transaction_mode)
         {
            if (pgagroal_write_discard_all(ssl, config->connections[slot].fd))
            {
               goto kill_connection;
            }
         }

         pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_SUCCESS, slot);

         config->connections[slot].timestamp = time(NULL);

         if (config->connections[slot].new)
         {
            if (pgagroal_connection_get(&transfer_fd))
            {
               goto kill_connection;
            }

            if (pgagroal_connection_id_write(transfer_fd, CONNECTION_TRANSFER))
            {
               goto kill_connection;
            }

            if (pgagroal_connection_transfer_write(transfer_fd, slot))
            {
               goto kill_connection;
            }

            pgagroal_disconnect(transfer_fd);
            transfer_fd = -1;
         }

         if (pgagroal_connection_get(&transfer_fd))
         {
            goto kill_connection;
         }

         if (pgagroal_connection_id_write(transfer_fd, CONNECTION_RETURN))
         {
            goto kill_connection;
         }

         if (pgagroal_connection_slot_write(transfer_fd, slot))
         {
            goto kill_connection;
         }

         pgagroal_disconnect(transfer_fd);
         transfer_fd = -1;

         if (config->connections[slot].limit_rule >= 0)
         {
            atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
         }

         config->connections[slot].new = false;
         config->connections[slot].pid = -1;
         config->connections[slot].tx_mode = transaction_mode;
         memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));
         atomic_store(&config->states[slot], STATE_FREE);
         atomic_fetch_sub(&config->active_connections, 1);

         pgagroal_log_debug("Connection returned: slot=%d, active_connections=%d, gracefully=%s",
                            slot, atomic_load(&config->active_connections), config->gracefully ? "true" : "false");

         // Check for graceful shutdown after successful connection return
         check_graceful_shutdown_trigger();

         pgagroal_prometheus_connection_return();

         return 0;
      }
      else if (state == STATE_GRACEFULLY)
      {
         pgagroal_write_terminate(ssl, config->connections[slot].fd);

         // Check for graceful shutdown after graceful connection termination
         check_graceful_shutdown_trigger();
      }
   }

kill_connection:

   pgagroal_disconnect(transfer_fd);

   pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_KILL, slot);

   return pgagroal_kill_connection(slot, ssl);
}

int
pgagroal_kill_connection(int slot, SSL* ssl)
{
   SSL_CTX* ctx;
   int ssl_shutdown;
   int result = 0;
   int fd;
   int transfer_fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_kill_connection: Slot %d FD %d State %d PID %d",
                      slot, config->connections[slot].fd, atomic_load(&config->states[slot]),
                      config->connections[slot].pid);

   pgagroal_tracking_event_slot(TRACKER_KILL_CONNECTION, slot);

   fd = config->connections[slot].fd;
   if (fd != -1)
   {
      if (pgagroal_connection_get(&transfer_fd))
      {
         result = 1;
      }

      if (pgagroal_connection_id_write(transfer_fd, CONNECTION_KILL))
      {
         result = 1;
      }

      if (pgagroal_connection_slot_write(transfer_fd, slot))
      {
         result = 1;
      }

      if (pgagroal_connection_socket_write(transfer_fd, fd))
      {
         result = 1;
      }

      pgagroal_disconnect(transfer_fd);

      if (ssl != NULL)
      {
         ctx = SSL_get_SSL_CTX(ssl);
         ssl_shutdown = SSL_shutdown(ssl);
         if (ssl_shutdown == 0)
         {
            SSL_shutdown(ssl);
         }
         SSL_free(ssl);
         SSL_CTX_free(ctx);
      }

      if (!pgagroal_socket_has_error(fd))
      {
         if (pgagroal_socket_isvalid(config->connections[slot].fd))
         {
            pgagroal_write_terminate(NULL, config->connections[slot].fd);
         }
         pgagroal_disconnect(fd);
      }
   }
   else
   {
      result = 1;
   }

   if (config->connections[slot].pid != -1)
   {
      if (config->connections[slot].limit_rule >= 0)
      {
         atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
      }

      atomic_fetch_sub(&config->active_connections, 1);

      // Check for graceful shutdown after killing connection
      check_graceful_shutdown_trigger();
   }

   memset(&config->connections[slot].username, 0, sizeof(config->connections[slot].username));
   memset(&config->connections[slot].database, 0, sizeof(config->connections[slot].database));
   memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));

   config->connections[slot].new = true;
   config->connections[slot].server = -1;
   config->connections[slot].tx_mode = false;

   config->connections[slot].has_security = SECURITY_INVALID;
   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      config->connections[slot].security_lengths[i] = 0;
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   config->connections[slot].backend_pid = 0;
   config->connections[slot].backend_secret = 0;

   config->connections[slot].limit_rule = -1;
   config->connections[slot].start_time = -1;
   config->connections[slot].timestamp = -1;
   config->connections[slot].fd = -1;
   config->connections[slot].pid = -1;

   atomic_store(&config->states[slot], STATE_NOTINIT);

   pgagroal_prometheus_connection_kill();

   return result;
}

void
pgagroal_idle_timeout(void)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char idle_check;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);
   prefill = false;

   pgagroal_log_debug("pgagroal_idle_timeout");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      idle_check = STATE_IDLE_CHECK;

      if (atomic_compare_exchange_strong(&config->states[i], &free, idle_check))
      {
         double diff = difftime(now, config->connections[i].timestamp);
         if (diff >= (double)pgagroal_time_convert(config->idle_timeout, FORMAT_TIME_S) && !config->connections[i].tx_mode)
         {
            pgagroal_prometheus_connection_idletimeout();
            pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &idle_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_idletimeout();
               pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_max_connection_age(void)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char age_check;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);
   prefill = false;

   pgagroal_log_debug("pgagroal_max_connection_age");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      age_check = STATE_MAX_CONNECTION_AGE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, age_check))
      {
         double age = difftime(now, config->connections[i].start_time);
         if (age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S) && !config->connections[i].tx_mode)
         {
            pgagroal_prometheus_connection_max_connection_age();
            pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &age_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_max_connection_age();
               pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_validation(void)
{
   bool prefill = true;
   time_t now;
   signed char free;
   signed char validation;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);

   pgagroal_log_debug("pgagroal_validation");

   /* We run backwards */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      validation = STATE_VALIDATION;

      if (atomic_compare_exchange_strong(&config->states[i], &free, validation))
      {
         bool kill = false;
         double diff, age;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[i].fd))
         {
            kill = true;
         }

         /* While we have the connection in validation may as well check for idle_timeout */
         if (!kill && pgagroal_time_is_valid(config->idle_timeout))
         {
            diff = difftime(now, config->connections[i].timestamp);
            if (diff >= (double)pgagroal_time_convert(config->idle_timeout, FORMAT_TIME_S))
            {
               kill = true;
            }
         }

         /* Also check for max_connection_age */
         if (!kill && pgagroal_time_is_valid(config->max_connection_age))
         {
            age = difftime(now, config->connections[i].start_time);
            if (age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S))
            {
               kill = true;
            }
         }

         /* Ok, send SELECT 1 */
         if (!kill)
         {
            kill = !pgagroal_connection_isvalid(config->connections[i].fd);
         }

         if (kill)
         {
            pgagroal_prometheus_connection_invalid();
            pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &validation, STATE_FREE))
            {
               pgagroal_prometheus_connection_invalid();
               pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_flush(int mode, char* database)
{
   bool prefill;
   signed char free;
   signed char in_use;
   bool do_kill;
   signed char server_state;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   prefill = false;

   pgagroal_log_debug("pgagroal_flush");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      in_use = STATE_IN_USE;
      do_kill = false;

      if (config->connections[i].server != -1)
      {
         server_state = atomic_load(&config->servers[config->connections[i].server].state);
         if (server_state == SERVER_FAILED)
         {
            do_kill = true;
         }
      }

      if (!do_kill)
      {
         bool consider = false;

         if (!strcmp(database, "*") || !strcmp(config->connections[i].database, database))
         {
            consider = true;
         }

         if (consider)
         {
            if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_FLUSH))
            {
               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
            else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
            {
               if (atomic_compare_exchange_strong(&config->states[i], &in_use, STATE_FLUSH))
               {
                  if (mode == FLUSH_ALL)
                  {
                     kill(config->connections[i].pid, SIGQUIT);
                     pgagroal_prometheus_connection_flush();
                     pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
                     pgagroal_kill_connection(i, NULL);
                     prefill = true;
                  }
                  else if (mode == FLUSH_GRACEFULLY)
                  {
                     atomic_store(&config->states[i], STATE_GRACEFULLY);
                  }
               }
            }
         }
      }
      else
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_MAX_CONNECTION_AGE:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();
}

void
pgagroal_flush_server(signed char server)
{
   struct main_configuration* config;
   int primary = -1;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_flush_server %s", config->servers[server].name);
   for (int i = 0; i < config->max_connections; i++)
   {
      if (config->connections[i].server == server)
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_MAX_CONNECTION_AGE:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (pgagroal_get_primary(&primary))
   {
      pgagroal_log_debug("No primary defined");
   }
   else
   {
      if (server != (unsigned char)primary && primary != -1)
      {
         pgagroal_prefill_if_can(true, true);
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_request_flush(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   time_t start_time;
   time_t end_time;
   struct json* req = NULL;
   int mode = 0;
   char* database = NULL;

   start_time = time(NULL);

   req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   mode = (int)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_MODE);
   database = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_DATABASE);

   if (database != NULL)
   {
      pgagroal_flush(mode, database);
   }
   else
   {
      pgagroal_flush(mode, "*");
   }

   end_time = time(NULL);

   pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);

   pgagroal_json_destroy(payload);

   exit(0);
}

void
pgagroal_prefill(bool initial)
{
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_prefill");

   for (int i = 0; i < config->number_of_limits; i++)
   {
      int size;

      if (initial)
      {
         size = config->limits[i].initial_size;
      }
      else
      {
         size = config->limits[i].min_size;
      }

      if (size > 0)
      {
         if (strcmp("all", config->limits[i].database) && strcmp("all", config->limits[i].username))
         {
            int user = -1;

            for (int j = 0; j < config->number_of_users && user == -1; j++)
            {
               if (!strcmp(config->limits[i].username, config->users[j].username))
               {
                  user = j;
               }
            }

            if (user != -1)
            {
               while (do_prefill(config->users[user].username, config->limits[i].database, size))
               {
                  int32_t slot = -1;
                  SSL* ssl = NULL;

                  if (pgagroal_prefill_auth(config->users[user].username, config->users[user].password,
                                            config->limits[i].database, &slot, &ssl) != AUTH_SUCCESS)
                  {
                     pgagroal_log_warn("Invalid data for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);

                     if (slot != -1)
                     {
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                     }

                     break;
                  }

                  if (slot != -1)
                  {
                     if (config->connections[slot].has_security != SECURITY_INVALID)
                     {
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_RETURN, slot);
                        pgagroal_return_connection(slot, ssl, false);
                     }
                     else
                     {
                        pgagroal_log_warn("Unsupported security model during prefill for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                        break;
                     }
                  }
               }
            }
            else
            {
               pgagroal_log_warn("Unknown user '%s' for limit entry (%d)", config->limits[i].username, i + 1);
            }
         }
         else
         {
            pgagroal_log_warn("Limit entry (%d) with invalid definition", i + 1);
         }
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

int
pgagroal_pool_init(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /* States */
   for (int i = 0; i < MAX_NUMBER_OF_CONNECTIONS; i++)
   {
      atomic_init(&config->states[i], STATE_NOTINIT);
   }

   /* Connections */
   for (int i = 0; i < config->max_connections; i++)
   {
      config->connections[i].new = true;
      config->connections[i].tx_mode = false;
      config->connections[i].server = -1;
      config->connections[i].has_security = SECURITY_INVALID;
      config->connections[i].limit_rule = -1;
      config->connections[i].start_time = -1;
      config->connections[i].timestamp = -1;
      config->connections[i].fd = -1;
      config->connections[i].pid = -1;
   }

   return 0;
}

int
pgagroal_pool_shutdown(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);

      if (state != STATE_NOTINIT)
      {
         if (state == STATE_FREE)
         {
            if (pgagroal_socket_isvalid(config->connections[i].fd))
            {
               pgagroal_write_terminate(NULL, config->connections[i].fd);
            }
         }
         pgagroal_disconnect(config->connections[i].fd);

         if (config->connections[i].pid != -1)
         {
            kill(config->connections[i].pid, SIGQUIT);
         }

         atomic_store(&config->states[i], STATE_NOTINIT);
      }
   }

   return 0;
}

int
pgagroal_pool_status(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_pool_status: %d/%d", atomic_load(&config->active_connections), config->max_connections);

   for (int i = 0; i < config->max_connections; i++)
   {
      connection_details(i);
   }

#ifdef DEBUG
   assert(atomic_load(&config->active_connections) <= config->max_connections);
#endif

   return 0;
}

static int
find_best_rule(char* username, char* database)
{
   int best_rule;
   struct main_configuration* config;

   best_rule = -1;
   config = (struct main_configuration*)shmem;

   if (config->number_of_limits > 0)
   {
      for (int i = 0; i < config->number_of_limits; i++)
      {
         bool database_match = false;

         // Check exact database name match or "all"
         if (!strcmp("all", config->limits[i].database) || !strcmp(database, config->limits[i].database))
         {
            database_match = true;
         }
         else
         {
            // Check if database matches any alias for this limit entry
            for (int j = 0; j < config->limits[i].aliases_count; j++)
            {
               if (!strcmp(database, config->limits[i].aliases[j]))
               {
                  database_match = true;
                  pgagroal_log_debug("Database '%s' matched alias '%s' for rule %d",
                                     database, config->limits[i].aliases[j], i);
                  break;
               }
            }
         }

         // Check username match
         bool username_match = (!strcmp("all", config->limits[i].username) || !strcmp(username, config->limits[i].username));

         /* There is a match */
         if (username_match && database_match)
         {
            if (best_rule == -1)
            {
               best_rule = i;
            }
            else
            {
               if (!strcmp(username, config->limits[best_rule].username) &&
                   (!strcmp(database, config->limits[best_rule].database) ||
                    is_alias_of_limit(database, best_rule)))
               {
                  /* We have a precise rule already */
               }
               else if (!strcmp("all", config->limits[best_rule].username))
               {
                  /* User is better */
                  if (strcmp("all", config->limits[i].username))
                  {
                     best_rule = i;
                  }
               }
               else if (!strcmp("all", config->limits[best_rule].database))
               {
                  /* Database is better */
                  if (strcmp("all", config->limits[i].database))
                  {
                     best_rule = i;
                  }
               }
            }
         }
      }
   }

   if (best_rule != -1)
   {
      pgagroal_log_debug("find_best_rule: user=%s, database=%s -> rule %d (real_database=%s)",
                         username, database, best_rule, config->limits[best_rule].database);
   }

   return best_rule;
}

static bool
is_alias_of_limit(char* database, int limit_index)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (limit_index < 0 || limit_index >= config->number_of_limits)
   {
      return false;
   }

   for (int i = 0; i < config->limits[limit_index].aliases_count; i++)
   {
      if (!strcmp(database, config->limits[limit_index].aliases[i]))
      {
         return true;
      }
   }

   return false;
}

static int
get_connection_count_for_limit_rule(int rule_index, char* username)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int count = 0;

   if (rule_index < 0 || rule_index >= config->number_of_limits)
   {
      return 0;
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      if (atomic_load(&config->states[i]) != STATE_NOTINIT &&
          !strcmp((const char*)(&config->connections[i].username), username))
      {
         // Check if this connection is for the main database name
         if (!strcmp((const char*)(&config->connections[i].database), config->limits[rule_index].database))
         {
            count++;
         }
         else
         {
            // Check if this connection is for any alias of this database
            for (int j = 0; j < config->limits[rule_index].aliases_count; j++)
            {
               if (!strcmp((const char*)(&config->connections[i].database), config->limits[rule_index].aliases[j]))
               {
                  count++;
                  break;
               }
            }
         }
      }
   }

   pgagroal_log_debug("Connection count for rule %d (user=%s): %d connections",
                      rule_index, username, count);

   return count;
}

static bool
remove_connection(char* username, char* database)
{
   signed char free;
   signed char remove;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_trace("remove_connection");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      remove = STATE_REMOVE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, remove))
      {
         if (!strcmp(username, config->connections[i].username) && !strcmp(database, config->connections[i].database))
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &remove, STATE_FREE))
            {
               pgagroal_prometheus_connection_remove();
               pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
            }
         }
         else
         {
            pgagroal_prometheus_connection_remove();
            pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
         }

         return true;
      }
   }

   return false;
}

static void
check_graceful_shutdown_trigger(void)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   int remaining_connections = atomic_load(&config->active_connections);

   if (remaining_connections == 0 && config->gracefully)
   {
      if (config->keep_running)
      {
         pgagroal_log_debug("pgagroal: graceful shutdown conditions met - connections=0, gracefully=true, keep_running=true - proceeding with shutdown");
      }
      else
      {
         pgagroal_log_debug("pgagroal: graceful shutdown skipped - connections=0, gracefully=true, but keep_running=false (race condition prevented)");
         return;
      }
   }

   if (remaining_connections == 0 && config->gracefully && config->keep_running)
   {
      // Double-check to handle race conditions with new connections
      usleep(1000); // 1ms delay to let any concurrent operations complete

      // Verify all three conditions are still met after brief delay
      int final_connections = atomic_load(&config->active_connections);
      if (final_connections == 0 && config->gracefully && config->keep_running)
      {
         pgagroal_log_debug("pgagroal: graceful shutdown confirmed - triggering shutdown");

         pid_t parent_pid = getppid();
         if (kill(parent_pid, SIGTRAP) == -1)
         {
            if (errno == ESRCH)
            {
               pgagroal_log_debug("Parent process already exited, worker shutting down");
            }
            else
            {
               pgagroal_log_debug("Failed to signal parent process (PID: %d): %s", parent_pid, strerror(errno));
            }
         }
         else
         {
            pgagroal_log_debug("Graceful shutdown signal sent to parent process (PID: %d)", parent_pid);
         }
      }
      else
      {
         int final_connections_check = atomic_load(&config->active_connections);
         pgagroal_log_debug("Graceful shutdown conditions - connections=%d, gracefully=%s, keep_running=%s",
                            final_connections_check,
                            config->gracefully ? "true" : "false",
                            config->keep_running ? "true" : "false");
      }
   }
}

static void
connection_details(int slot)
{
   int state;
   char time_buf[32];
   char start_buf[32];
   struct main_configuration* config;
   struct connection connection;

   config = (struct main_configuration*)shmem;

   connection = config->connections[slot];
   state = atomic_load(&config->states[slot]);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&(connection.timestamp), &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   memset(&start_buf, 0, sizeof(start_buf));
   ctime_r(&(connection.start_time), &start_buf[0]);
   start_buf[strlen(start_buf) - 1] = 0;

   switch (state)
   {
      case STATE_NOTINIT:
         pgagroal_log_debug("pgagroal_pool_status: State: NOTINIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_INIT:
         pgagroal_log_debug("pgagroal_pool_status: State: INIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_FREE:
         pgagroal_log_debug("pgagroal_pool_status: State: FREE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_IN_USE:
         pgagroal_log_debug("pgagroal_pool_status: State: IN_USE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_GRACEFULLY:
         pgagroal_log_debug("pgagroal_pool_status: State: GRACEFULLY");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_FLUSH:
         pgagroal_log_debug("pgagroal_pool_status: State: FLUSH");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_IDLE_CHECK:
         pgagroal_log_debug("pgagroal_pool_status: State: IDLE CHECK");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_MAX_CONNECTION_AGE:
         pgagroal_log_debug("pgagroal_pool_status: State: MAX CONNECTION AGE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_VALIDATION:
         pgagroal_log_debug("pgagroal_pool_status: State: VALIDATION");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      case STATE_REMOVE:
         pgagroal_log_debug("pgagroal_pool_status: State: REMOVE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
         break;
      default:
         pgagroal_log_debug("pgagroal_pool_status: State %d Slot %d FD %d", state, slot, connection.fd);
         break;
   }
}

static bool
do_prefill(char* username, char* database, int size)
{
   signed char state;
   int free = 0;
   int connections = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   //Find the limit rule for this database/username combination
   int rule_index = find_best_rule(username, database);
   if (rule_index != -1)
   {
      // Count connections for this rule (including aliases)
      connections = get_connection_count_for_limit_rule(rule_index, username);
   }
   else
   {
      // Fallback to old logic if no rule found
      for (int i = 0; i < config->max_connections; i++)
      {
         if (!strcmp((const char*)(&config->connections[i].username), username) &&
             !strcmp((const char*)(&config->connections[i].database), database))
         {
            connections++;
         }
      }
   }

   // Count free connections
   for (int i = 0; i < config->max_connections; i++)
   {
      state = atomic_load(&config->states[i]);
      if (state == STATE_NOTINIT)
      {
         free++;
      }
   }

   pgagroal_log_debug("do_prefill: user=%s, database=%s, current=%d, target=%d, free=%d",
                      username, database, connections, size, free);

   return connections < size && free > 0;
}

void
pgagroal_prefill_if_can(bool do_fork, bool initial)
{
   int primary;

   if (pgagroal_can_prefill())
   {
      if (pgagroal_get_primary(&primary))
      {
         pgagroal_log_warn("No primary detected, cannot try to prefill!");
         return;
      }

      if (do_fork)
      {
         if (!fork())
         {
            pgagroal_prefill(initial);
         }
      }
      else
      {
         pgagroal_prefill(initial);
      }
   }
}

static char*
resolve_database_name(char* database, int best_rule)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (best_rule < 0 || best_rule >= config->number_of_limits)
   {
      return database; // Return original if no rule found
   }

   // Check if this database name is an alias
   for (int j = 0; j < config->limits[best_rule].aliases_count; j++)
   {
      if (!strcmp(database, config->limits[best_rule].aliases[j]))
      {
         return config->limits[best_rule].database; // Return real database name
      }
   }

   // Not an alias, return original name
   return database;
}