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
#include <pool.h>
#include <security.h>
#include <server.h>
#include <message.h>
#include <network.h>
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
#include <arpa/inet.h>

static int failover(int old_primary);
static int process_server_parameters(int server, struct deque* server_parameters);
static int query_system_identifier(int server_idx, char* identifier, size_t id_size);

/**
 * Check server system identifiers for duplicates.
 *
 * Behavior is controlled by the startup_validation config parameter:
 *   off: skip entirely
 *   try: run check if health_check_user is set, otherwise log INFO and continue
 *   on:  require health_check_user and fail on any query error or duplicate
 */
int
pgagroal_check_server_identifiers(void)
{
   struct main_configuration* config;
   char identifiers[NUMBER_OF_SERVERS][64];
   int num_servers;

   config = (struct main_configuration*)shmem;
   num_servers = config->number_of_servers;

   if (config->startup_validation == STARTUP_VALIDATION_OFF)
   {
      pgagroal_log_debug("Startup validation: off, skipping identifier check");
      return 0;
   }

   if (num_servers < 2)
   {
      return 0;
   }

   if (strlen(config->health_check_user) == 0)
   {
      if (config->startup_validation == STARTUP_VALIDATION_ON)
      {
         pgagroal_log_fatal("startup_validation is set to 'on' but health_check_user is not configured");
         return 1;
      }

      /* try mode: no health_check_user, just inform and continue */
      pgagroal_log_info("Startup validation: health_check_user not set, skipping server identifier check");
      return 0;
   }

   pgagroal_log_info("Checking server system identifiers");

   for (int i = 0; i < num_servers; i++)
   {
      memset(identifiers[i], 0, sizeof(identifiers[i]));

      if (query_system_identifier(i, identifiers[i], sizeof(identifiers[i])))
      {
         if (config->startup_validation == STARTUP_VALIDATION_ON)
         {
            pgagroal_log_fatal("Could not query system_identifier for server [%s] (%s:%d)",
                               config->servers[i].name, config->servers[i].host, config->servers[i].port);
            return 1;
         }

         pgagroal_log_warn("Could not query system_identifier for server [%s] (%s:%d)",
                           config->servers[i].name, config->servers[i].host, config->servers[i].port);
      }
   }

   for (int i = 0; i < num_servers; i++)
   {
      if (!strlen(identifiers[i]))
      {
         continue;
      }

      for (int j = i + 1; j < num_servers; j++)
      {
         if (!strlen(identifiers[j]))
         {
            continue;
         }

         if (!strcmp(identifiers[i], identifiers[j]))
         {
            pgagroal_log_fatal("Servers [%s] (%s:%d) and [%s] (%s:%d) have the same system_identifier (%s) "
                               "- they point to the same PostgreSQL cluster",
                               config->servers[i].name, config->servers[i].host, config->servers[i].port,
                               config->servers[j].name, config->servers[j].host, config->servers[j].port,
                               identifiers[i]);
            return 1;
         }
      }
   }

   return 0;
}

int
pgagroal_server_query_execute(int server_idx, char* user, char* database, char* query, int* auth_type, int* fd_out)
{
   struct main_configuration* config;
   struct server* srv;
   int fd = -1;
   char buffer[1024];
   struct message* msg = NULL;
   struct message* startup_msg = NULL;
   int status;
   char* password = NULL;
   int query_len;
   int msg_size;
   int auth_type_msg;
   bool ready = false;
   int offset_p;
   char kind;
   int len;

   config = (struct main_configuration*)shmem;
   srv = &config->servers[server_idx];

   if (pgagroal_connect(srv->host, srv->port, &fd, true, false) != 0)
   {
      pgagroal_log_debug("server_query: Failed to connect to server %d (%s:%d)",
                         server_idx, srv->host, srv->port);
      return 1;
   }

   /* Construct and send Startup Packet */
   if (pgagroal_create_startup_message(user, database, &(startup_msg)) != MESSAGE_STATUS_OK)
   {
      pgagroal_log_debug("server_query: Failed to create startup message");
      goto error;
   }

   if (pgagroal_write_message(NULL, fd, startup_msg) != MESSAGE_STATUS_OK)
   {
      pgagroal_log_debug("server_query: Failed to write startup packet");
      pgagroal_free_message(startup_msg);
      startup_msg = NULL;
      goto error;
   }

   pgagroal_free_message(startup_msg);
   startup_msg = NULL;

   /* Authentication Phase */
   status = pgagroal_read_timeout_message(NULL, fd, 5, &msg);
   if (status != MESSAGE_STATUS_OK || msg == NULL || msg->kind != 'R')
   {
      pgagroal_log_debug("server_query: Expected 'R' but got %c (status %d)",
                         msg ? msg->kind : '?', status);
      goto error;
   }

   auth_type_msg = ntohl(*(int*)(msg->data + 5));
   if (auth_type_msg == 0) /* Trust */
   {
      if (auth_type != NULL)
      {
         *auth_type = HEALTH_CHECK_AUTH_TRUST;
      }
   }
   else if (auth_type_msg == 5) /* MD5 */
   {
      if (auth_type != NULL)
      {
         *auth_type = HEALTH_CHECK_AUTH_MD5;
      }
      password = pgagroal_get_user_password(user);
      if (password == NULL)
      {
         pgagroal_log_debug("server_query: Password for %s not found", user);
         goto error;
      }
      if (pgagroal_md5_client_auth(msg, user, password, fd, NULL, &msg) != 0)
      {
         pgagroal_log_debug("server_query: MD5 auth failed for server %d", server_idx);
         goto error;
      }
   }
   else if (auth_type_msg == 10) /* SASL */
   {
      if (auth_type != NULL)
      {
         *auth_type = HEALTH_CHECK_AUTH_SCRAM;
      }
      password = pgagroal_get_user_password(user);
      if (password == NULL)
      {
         pgagroal_log_debug("server_query: Password for %s not found", user);
         goto error;
      }
      if (pgagroal_scram_client_auth(user, password, fd, NULL, &msg) != 0)
      {
         pgagroal_log_debug("server_query: SCRAM auth failed for server %d", server_idx);
         goto error;
      }
   }
   else
   {
      pgagroal_log_debug("server_query: Unsupported auth type %d for server %d", auth_type_msg, server_idx);
      goto error;
   }

   /* Wait for ReadyForQuery */
   while (true)
   {
      if (msg == NULL)
      {
         status = pgagroal_read_timeout_message(NULL, fd, 5, &msg);
         if (status != MESSAGE_STATUS_OK || msg == NULL)
         {
            pgagroal_log_debug("server_query: Failed to read from server (status %d)", status);
            goto error;
         }
      }

      offset_p = 0;
      while (offset_p < msg->length)
      {
         kind = pgagroal_read_byte(msg->data + offset_p);
         len = pgagroal_read_int32(msg->data + offset_p + 1);

         if (kind == 'Z')
         {
            ready = true;
            break;
         }
         else if (kind == 'E')
         {
            pgagroal_log_debug("server_query: Received ErrorResponse during session initialization");
            goto error;
         }

         offset_p += 1 + len;
         if (offset_p >= msg->length)
         {
            break;
         }
      }

      pgagroal_clear_message(msg);
      msg = NULL;

      if (ready)
      {
         break;
      }
   }

   /* Send query */
   query_len = strlen(query);
   msg_size = 1 + 4 + query_len + 1;

   if ((size_t)msg_size > sizeof(buffer))
   {
      pgagroal_log_debug("server_query: Query too large (%d bytes)", query_len);
      goto error;
   }

   memset(buffer, 0, sizeof(buffer));
   buffer[0] = 'Q';
   pgagroal_write_int32(buffer + 1, 4 + query_len + 1);
   memcpy(buffer + 5, query, query_len);
   buffer[5 + query_len] = 0;

   if (pgagroal_write_socket(NULL, fd, buffer, msg_size) != msg_size)
   {
      pgagroal_log_debug("server_query: Failed to write query");
      goto error;
   }

   *fd_out = fd;
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
query_system_identifier(int server_idx, char* identifier, size_t id_size)
{
   struct main_configuration* config;
   struct server* srv;
   int fd = -1;
   struct message* msg = NULL;
   int status;
   bool query_ready = false;
   int offset_q;
   char buffer[8];

   config = (struct main_configuration*)shmem;
   srv = &config->servers[server_idx];

   if (pgagroal_server_query_execute(server_idx,
                                     config->health_check_user,
                                     config->health_check_user,
                                     "SELECT system_identifier FROM pg_control_system()",
                                     NULL, &fd) != 0)
   {
      return 1;
   }

   /* Parse query response and extract system_identifier from DataRow */
   while (true)
   {
      status = pgagroal_read_timeout_message(NULL, fd, 5, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
      {
         goto error;
      }

      offset_q = 0;
      while (offset_q < msg->length)
      {
         char type_q = pgagroal_read_byte(msg->data + offset_q);
         int len_q = pgagroal_read_int32(msg->data + offset_q + 1);

         if (type_q == 'D')
         {
            /* DataRow: 'D' | int32 len | int16 num_cols | int32 col_len | data */
            int dr_offset = offset_q + 5; /* skip 'D' + len */
            int num_cols = pgagroal_read_int16(msg->data + dr_offset);
            dr_offset += 2;

            if (num_cols >= 1)
            {
               int col_len = pgagroal_read_int32(msg->data + dr_offset);
               dr_offset += 4;

               if (col_len > 0 && (size_t)col_len < id_size)
               {
                  memcpy(identifier, msg->data + dr_offset, col_len);
                  identifier[col_len] = '\0';
               }
            }
         }
         else if (type_q == 'Z')
         {
            query_ready = true;
            break;
         }
         else if (type_q == 'E')
         {
            pgagroal_log_error("cannot query pg_control_system() on server %d", server_idx);
            goto error;
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

   pgagroal_log_debug("system_identifier: Server [%s] (%s:%d) = %s",
                      srv->name, srv->host, srv->port, identifier);

   return strlen(identifier) > 0 ? 0 : 1;

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

int
pgagroal_update_server_state(int slot, int socket, SSL* ssl)
{
   int status;
   int server;
   size_t size = 40;
   signed char state;
   char is_recovery[size];
   struct message qmsg;
   struct message* tmsg = NULL;
   struct deque* server_parameters = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   server = config->connections[slot].server;

   memset(&qmsg, 0, sizeof(struct message));
   memset(&is_recovery, 0, size);

   pgagroal_write_byte(&is_recovery, 'Q');
   pgagroal_write_int32(&(is_recovery[1]), size - 1);
   pgagroal_write_string(&(is_recovery[5]), "SELECT * FROM pg_is_in_recovery();");

   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = &is_recovery;

   status = pgagroal_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgagroal_read_block_message(ssl, socket, &tmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   /* Read directly from the D message fragment */
   state = pgagroal_read_byte(tmsg->data + 54);

   pgagroal_clear_message(tmsg);

   if (state == 'f')
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
   pgagroal_clear_message(tmsg);

   return 0;

error:
   pgagroal_log_trace("pgagroal_update_server_state: slot (%d) status (%d)", slot, status);

   pgagroal_deque_destroy(server_parameters);
   pgagroal_clear_message(tmsg);

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

static void
notify_standbys(int old_primary, int new_primary)
{
   pid_t pid;
   int status;
   struct main_configuration* config = (struct main_configuration*)shmem;

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("Notify: Unable to fork notify script");
      return;
   }
   else if (pid > 0)
   {
      waitpid(pid, &status, 0);

      if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
      {
         pgagroal_log_info("Notify: Standbys notified successfully");
      }
      else
      {
         pgagroal_log_error("Notify: Error from notify script");
      }
   }
   else
   {
      int max_args = 5 + (config->number_of_servers * 2) + 1;
      char** args = malloc(max_args * sizeof(char*));

      if (!args)
      {
         pgagroal_log_error("Notify: Out of memory");
         exit(1);
      }

      int idx = 0;
      bool has_standbys = false;

      args[idx++] = "pgagroal_failover_notify_standbys";

      char* old_port = malloc(6);
      snprintf(old_port, 6, "%d", config->servers[old_primary].port);
      args[idx++] = config->servers[old_primary].host;
      args[idx++] = old_port;

      char* new_port = malloc(6);
      snprintf(new_port, 6, "%d", config->servers[new_primary].port);
      args[idx++] = config->servers[new_primary].host;
      args[idx++] = new_port;

      for (int i = 0; i < config->number_of_servers; i++)
      {
         if (i == old_primary || i == new_primary)
            continue;

         signed char state = atomic_load(&config->servers[i].state);
         if (state == SERVER_REPLICA || state == SERVER_NOTINIT || state == SERVER_NOTINIT_PRIMARY)
         {
            has_standbys = true;

            char* port = malloc(6);
            snprintf(port, 6, "%d", config->servers[i].port);
            args[idx++] = config->servers[i].host;
            args[idx++] = port;
         }
      }

      args[idx] = NULL;

      if (!has_standbys)
      {
         pgagroal_log_warn("Notify: No standbys to notify");
         exit(0);
      }

      execv(config->failover_notify_script, args);

      pgagroal_log_error("Notify: execv() failed");
      exit(1);
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

         if (config->failover_notify_script[0] != '\0')
         {
            notify_standbys(old_primary, new_primary);
         }
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
