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
#include <deque.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <pool.h>
#include <security.h>
#include <server.h>
#include <utils.h>
#include <value.h>

/* system */
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int failover(int old_primary);
static int process_server_parameters(int server, struct deque* server_parameters);
static int check_recovery_status(SSL* ssl, int socket, bool* recovery_state);

int
pgagroal_get_primary(int* server)
{
   int primary;
   signed char server_state;
   struct main_configuration* config;

   primary = -1;
   config = (struct main_configuration*)shmem;

   /* Find PRIMARY */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state == SERVER_PRIMARY)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) primary", i, config->servers[i].name);
         primary = i;
      }
   }

   /* Find NOTINIT_PRIMARY */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state == SERVER_NOTINIT_PRIMARY)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) noninit_primary", i, config->servers[i].name);
         primary = i;
      }
   }

   /* Find the first valid server */
   for (int i = 0; primary == -1 && i < config->number_of_servers; i++)
   {
      server_state = atomic_load(&config->servers[i].state);
      if (server_state != SERVER_FAILOVER && server_state != SERVER_FAILED)
      {
         pgagroal_log_trace("pgagroal_get_primary: server (%d) name (%s) any (%d)", i, config->servers[i].name, server_state);
         primary = i;
      }
   }

   if (primary == -1)
   {
      goto error;
   }

   *server = primary;

   return 0;

error:

   *server = -1;

   return 1;
}

static int
check_recovery_status(SSL* ssl, int socket, bool* recovery_state)
{
   int status;
   size_t size = 40;
   char is_recovery[size];
   struct message qmsg;
   struct message* tmsg = NULL;
   struct message* dmsg = NULL;
   int16_t column_count;
   int32_t column_length;
   signed char state;

   *recovery_state = false; 

   memset(&qmsg, 0, sizeof(struct message));
   memset(&is_recovery, 0, size);

   /* XXX: Consider streaming replication info for more accurate recovery detection */
   pgagroal_write_byte(&is_recovery, 'Q');
   pgagroal_write_int32(&(is_recovery[1]), size - 1);
   pgagroal_write_string(&(is_recovery[5]), "SELECT * FROM pg_is_in_recovery();");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &is_recovery;

   status = pgagroal_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgagroal_log_debug("check_recovery_status: Failed to write query message");
      goto error;
   }

   status = pgagroal_read_block_message(ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      pgagroal_log_debug("check_recovery_status: Failed to read response message");
      goto error;
   }

   /* Extract DataRow message ('D') from the response */
   if (pgagroal_extract_message('D', tmsg, &dmsg))
   {
      pgagroal_log_debug("check_recovery_status: Failed to extract DataRow message from response");
      goto error;
   }

   if (!dmsg || !dmsg->data || dmsg->length < 11)
   {
      pgagroal_log_debug("check_recovery_status: Invalid DataRow message structure");
      goto error;
   }

   /* Parse DataRow message structure:
    * Offset 0: 'D' (message type)
    * Offset 1-4: message length (int32)
    * Offset 5-6: column count (int16)
    * Offset 7-10: column 1 length (int32)
    * Offset 11: column 1 data ('t' or 'f')
    */
   column_count = pgagroal_read_int16(dmsg->data + 5);
   if (column_count != 1)
   {
      pgagroal_log_debug("check_recovery_status: Expected 1 column, got %d", column_count);
      goto error;
   }

   column_length = pgagroal_read_int32(dmsg->data + 7);
   if (column_length != 1)
   {
      pgagroal_log_debug("check_recovery_status: Expected column length 1, got %d", column_length);
      goto error;
   }

   state = pgagroal_read_byte(dmsg->data + 11);
   /* Convert 't'/'f' to boolean: 't' means in recovery (true), 'f' means not in recovery (false) */
   *recovery_state = (state == 't');

   pgagroal_clear_message(dmsg);
   pgagroal_clear_message(tmsg);

   return 0;

error:
   if (dmsg)
   {
      pgagroal_clear_message(dmsg);
   }
   if (tmsg)
   {
      pgagroal_clear_message(tmsg);
   }

   return 1;
}

int
pgagroal_update_server_state(int slot, int socket, SSL* ssl)
{
   int server;
   bool in_recovery = false;
   struct deque* server_parameters = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   server = config->connections[slot].server;

   /* Check recovery status using shared helper function */
   if (check_recovery_status(ssl, socket, &in_recovery))
   {
      goto error;
   }

   if (!in_recovery)
   {
      atomic_store(&config->servers[server].state, SERVER_PRIMARY);
   }
   else
   {
      atomic_store(&config->servers[server].state, SERVER_REPLICA);
   }

   if (pgagroal_extract_server_parameters(slot, &server_parameters))
   {
      pgagroal_log_trace("Unable to extract server_parameters for %s", config->servers[server].name);
      goto error;
   }

   if (process_server_parameters(server, server_parameters))
   {
      pgagroal_log_trace("uanble to process server_parameters for %s", config->servers[server].name);
      goto error;
   }

   pgagroal_deque_destroy(server_parameters);

   return 0;

error:
   pgagroal_log_trace("pgagroal_update_server_state: slot (%d) failed", slot);

   pgagroal_deque_destroy(server_parameters);

   return 1;
}

int
pgagroal_server_status(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (strlen(config->servers[i].name) > 0)
      {
         pgagroal_log_debug("pgagroal_server_status:    #: %d", i);
         pgagroal_log_debug("                        Name: %s", config->servers[i].name);
         pgagroal_log_debug("                        Host: %s", config->servers[i].host);
         pgagroal_log_debug("                        Port: %d", config->servers[i].port);
         switch (atomic_load(&config->servers[i].state))
         {
            case SERVER_NOTINIT:
               pgagroal_log_debug("                        State: NOTINIT");
               break;
            case SERVER_NOTINIT_PRIMARY:
               pgagroal_log_debug("                        State: NOTINIT_PRIMARY");
               break;
            case SERVER_PRIMARY:
               pgagroal_log_debug("                        State: PRIMARY");
               break;
            case SERVER_REPLICA:
               pgagroal_log_debug("                        State: REPLICA");
               break;
            case SERVER_FAILOVER:
               pgagroal_log_debug("                        State: FAILOVER");
               break;
            case SERVER_FAILED:
               pgagroal_log_debug("                        State: FAILED");
               break;
            default:
               pgagroal_log_debug("                        State: %d", atomic_load(&config->servers[i].state));
               break;
         }
      }
   }

   return 0;
}

int
pgagroal_server_failover(int slot)
{
   signed char primary;
   signed char old_primary;
   int ret = 1;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   primary = SERVER_PRIMARY;

   old_primary = config->connections[slot].server;

   if (atomic_compare_exchange_strong(&config->servers[old_primary].state, &primary, SERVER_FAILOVER))
   {
      ret = failover(old_primary);

      if (!fork())
      {
         pgagroal_flush_server(old_primary);
      }
   }

   return ret;
}

int
pgagroal_server_force_failover(int server)
{
   signed char cur_state;
   signed char prev_state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   cur_state = atomic_load(&config->servers[server].state);

   if (cur_state != SERVER_FAILOVER && cur_state != SERVER_FAILED)
   {
      prev_state = atomic_exchange(&config->servers[server].state, SERVER_FAILOVER);

      if (prev_state == SERVER_NOTINIT || prev_state == SERVER_NOTINIT_PRIMARY || prev_state == SERVER_PRIMARY || prev_state == SERVER_REPLICA)
      {
         return failover(server);
      }
      else if (prev_state == SERVER_FAILED)
      {
         atomic_store(&config->servers[server].state, SERVER_FAILED);
      }
   }

   return 1;
}

int
pgagroal_server_clear(char* server)
{
   signed char state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, server))
      {
         state = atomic_load(&config->servers[i].state);

         if (state == SERVER_FAILED)
         {
            atomic_store(&config->servers[i].state, SERVER_NOTINIT);
         }

         return 0;
      }
   }

   return 1;
}

int
pgagroal_server_switch(char* server)
{
   int old_primary = -1;
   int new_primary = -1;
   signed char state;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal: Attempting to switch to server '%s'", server);

   // Find current primary server
   for (int i = 0; i < config->number_of_servers; i++)
   {
      state = atomic_load(&config->servers[i].state);
      if (state == SERVER_PRIMARY)
      {
         old_primary = i;
         break;
      }
   }
   // Find target server by name
   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (!strcmp(config->servers[i].name, server))
      {
         new_primary = i;
         break;
      }
   }

   if (old_primary != -1 && new_primary != -1)
   {
      if (old_primary == new_primary)
      {
         pgagroal_log_info("pgagroal: Server '%s' is already the primary - no switch needed", server);
         return 0;
      }
      else
      {
         pgagroal_log_info("pgagroal: Switching primary from '%s' to '%s'",
                           config->servers[old_primary].name,
                           config->servers[new_primary].name);
         atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
         atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
         return 0;
      }
   }
   else if (old_primary == -1 && new_primary != -1)
   {
      pgagroal_log_info("pgagroal: Setting '%s' as primary server (no previous primary found)",
                        config->servers[new_primary].name);
      atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
      return 0;
   }
   else if (old_primary != -1 && new_primary == -1)
   {
      pgagroal_log_warn("pgagroal: Switch to server '%s' failed: server not found in configuration (current primary: '%s')",
                        server, config->servers[old_primary].name);
      return 1;
   }
   else
   {
      pgagroal_log_warn("pgagroal: Switch to server '%s' failed: no current primary server found and target server not found in configuration", server);
      return 1;
   }
}

static int
failover(int old_primary)
{
   signed char state;
   char old_primary_port[6];
   int new_primary;
   char new_primary_port[6];
   int status;
   pid_t pid;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   new_primary = -1;

   for (int i = 0; new_primary == -1 && i < config->number_of_servers; i++)
   {
      state = atomic_load(&config->servers[i].state);
      if (state == SERVER_NOTINIT || state == SERVER_NOTINIT_PRIMARY || state == SERVER_REPLICA)
      {
         new_primary = i;
      }
   }

   if (new_primary == -1)
   {
      pgagroal_log_error("Failover: New primary could not be found");
      atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
      goto error;
   }

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("Failover: Unable to execute failover script");
      atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
      goto error;
   }
   else if (pid > 0)
   {
      waitpid(pid, &status, 0);

      if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
      {
         pgagroal_log_info("Failover: New primary is %s (%s:%d)", config->servers[new_primary].name, config->servers[new_primary].host, config->servers[new_primary].port);
         atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
         atomic_store(&config->servers[new_primary].state, SERVER_PRIMARY);
      }
      else
      {
         if (WIFEXITED(status))
         {
            pgagroal_log_error("Failover: Error from failover script (exit %d)", WEXITSTATUS(status));
         }
         else
         {
            pgagroal_log_error("Failover: Error from failover script (status %d)", status);
         }

         atomic_store(&config->servers[old_primary].state, SERVER_FAILED);
         atomic_store(&config->servers[new_primary].state, SERVER_FAILED);
      }
   }
   else
   {
      memset(&old_primary_port, 0, sizeof(old_primary_port));
      memset(&new_primary_port, 0, sizeof(new_primary_port));

      sprintf(&old_primary_port[0], "%d", config->servers[old_primary].port);
      sprintf(&new_primary_port[0], "%d", config->servers[new_primary].port);

      execl(config->failover_script, "pgagroal_failover",
            config->servers[old_primary].host, old_primary_port,
            config->servers[new_primary].host, new_primary_port,
            (char*)NULL);
   }

   return 0;

error:

   return 1;
}

static int
process_server_parameters(int server, struct deque* server_parameters)
{
   int status = 0;
   int major = 0;
   int minor = 0;
   struct deque_iterator* iter = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   config->servers[server].version = 0;
   config->servers[server].minor_version = 0;

   pgagroal_deque_iterator_create(server_parameters, &iter);
   while (pgagroal_deque_iterator_next(iter))
   {
      pgagroal_log_trace("%s/process server_parameter '%s'", config->servers[server].name, iter->tag);
      char* value = pgagroal_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
      free(value);
      if (!strcmp("server_version", iter->tag))
      {
         char* server_version = pgagroal_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         if (sscanf(server_version, "%d.%d", &major, &minor) == 2)
         {
            config->servers[server].version = major;
            config->servers[server].minor_version = minor;
         }
         else
         {
            pgagroal_log_error("Unable to parse server_version '%s' for %s",
                               server_version, config->servers[server].name);
            status = 1;
         }
         free(server_version);
         pgagroal_log_trace("%s/processed version: %d", config->servers[server].name, config->servers[server].version);
         pgagroal_log_trace("%s/processed minor_version: %d", config->servers[server].name, config->servers[server].minor_version);
      }
   }

   pgagroal_deque_iterator_destroy(iter);
   return status;
}

int
pgagroal_test_server_connectivity(int server, bool* is_available)
{
   int fd = -1;
   int ret = 0;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->number_of_servers)
   {
      goto error;
   }

   *is_available = false;

   /* Try to connect to the server */
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

   if (ret == 0 && fd >= 0)
   {
      *is_available = true;
      pgagroal_disconnect(fd);
      return 0;
   }

error:
   if (fd >= 0)
   {
      pgagroal_disconnect(fd);
   }

   *is_available = false;
   return 1;
}

int
pgagroal_get_server_status(int server, char** status_string)
{
   bool is_available = false;
   bool in_recovery = false;
   int slot = -1;
   SSL* ssl = NULL;
   struct main_configuration* config = NULL;
   char* database = "postgres";

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->number_of_servers)
   {
      goto error;
   }

   *status_string = NULL;

   /* First check basic connectivity - if connection fails or server is unavailable, return "Down" */
   if (pgagroal_test_server_connectivity(server, &is_available) || !is_available)
   {
      *status_string = strdup("Down");
      return 0;
   }

   /* Try to authenticate and get a connection slot */
   if (pgagroal_prefill_auth(config->superuser.username, config->superuser.password, database, &slot, &ssl))
   {
      pgagroal_log_debug("pgagroal_get_server_status: Authentication failed for server %d", server);
      *status_string = strdup("Running");
      return 0;
   }

   if (slot < 0 || slot >= config->max_connections)
   {
      *status_string = strdup("Running");
      return 0;
   }

   /* Check recovery status using shared helper function */
   if (check_recovery_status(ssl, config->connections[slot].fd, &in_recovery))
   {
      goto running_fallback;
   }

   /* Convert boolean recovery state to status string */
   if (in_recovery)
   {
      *status_string = strdup("Recovering");
   }
   else
   {
      *status_string = strdup("Running");
   }

   /* Return the connection to the pool */
   pgagroal_return_connection(slot, ssl, false);

   return 0;

running_fallback:
   /* If we can't check recovery status, assume Running if server is reachable */
   *status_string = strdup("Running");
   if (slot >= 0 && slot < config->max_connections)
   {
      pgagroal_return_connection(slot, ssl, false);
   }

   return 0;

error:
   *status_string = strdup("Down");
   if (slot >= 0 && slot < config->max_connections)
   {
      pgagroal_return_connection(slot, ssl, false);
   }

   return 0;
}
