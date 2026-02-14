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
#include <shmem.h>
#include <utils.h>
#include <message.h>
#include <security.h>
#include <aes.h>

/* system */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

static void health_check_loop(void);
static int server_probe(int server_idx, bool* up);

void
pgagroal_health_check(char** argv)
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
      for (int i = 3; i < sysconf(_SC_OPEN_MAX); i++)
      {
         close(i);
      }

      pgagroal_set_proc_title(1, argv, "health check worker", NULL);
      health_check_loop();
      exit(0);
   }
   else
   {
      config->health_check_pid = pid;
   }
}

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
      config->health_check_pid = 0;
   }
}

static void
health_check_loop(void)
{
   struct main_configuration* config;
   unsigned int period;
   bool up;
   int status;

   config = (struct main_configuration*)shmem;

   pgagroal_log_info("Health check started");

   period = config->health_check_period;

   while (config->keep_running)
   {
      sleep(period);

      if (!config->keep_running)
      {
         break;
      }

      pgagroal_log_debug("Health check run");

      for (int i = 0; i < config->number_of_servers; i++)
      {
         up = false;
         status = server_probe(i, &up);

         if (status != 0)
         {
            atomic_store(&config->servers[i].health_state, SERVER_HEALTH_UNKNOWN);
         }

         if (up)
         {
            config->servers[i].failures = 0;
            atomic_store(&config->servers[i].health_state, SERVER_HEALTH_UP);
            pgagroal_log_debug("Health: Server %d is UP", i);
         }
         else
         {
            config->servers[i].failures++;
            if (config->servers[i].failures >= HEALTH_CHECK_MAX_RETRIES)
            {
               atomic_store(&config->servers[i].health_state, SERVER_HEALTH_DOWN);
               pgagroal_log_debug("Health: Server %d is DOWN", i);
            }
         }
      }
   }
}

static int
server_probe(int server_idx, bool* up)
{
   struct main_configuration* config;
   struct server* server;
   int fd;
   char buffer[1024];
   int offset;
   struct message* msg;
   int status;
   size_t start_packet_size;
   char* password;
   char* query_string;
   int query_len;
   int msg_size;
   bool query_success;
   int auth_type;
   char type;
   int offset_h;
   int length_h;
   char kind_h;
   int offset_q;
   int length_q;
   bool query_ready;

   config = (struct main_configuration*)shmem;
   server = &config->servers[server_idx];
   fd = -1;
   offset = 0;
   msg = NULL;
   password = NULL;
   query_string = NULL;
   query_success = false;
   *up = false;

   pgagroal_log_debug("Health: Probing server %d (user: %s, database: %s)", server_idx, config->health_check_user, config->health_check_user);

   if (pgagroal_connect(server->host, server->port, &fd, true, false) != 0)
   {
      pgagroal_log_debug("Health: Failed to connect to server %d (%s:%d)", server_idx, server->host, server->port);
      return 1;
   }

   /* Construct Startup Packet (Protocol v3.0) */
   start_packet_size = 4 + 4 + 5 + strlen(config->health_check_user) + 1 + 9 + strlen(config->health_check_user) + 1 + 1;

   memset(buffer, 0, sizeof(buffer));
   pgagroal_write_int32(buffer, start_packet_size);
   pgagroal_write_int32(buffer + 4, 196608); /* Protocol Version 3.0 (0x00030000) */

   offset = 8;
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "user");
   offset += 5;
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "%s", config->health_check_user);
   offset += strlen(config->health_check_user) + 1;

   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "database");
   offset += 9;
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "%s", config->health_check_user);
   offset += strlen(config->health_check_user) + 1;

   buffer[offset] = 0;
   offset += 1;

   if (pgagroal_write_socket(NULL, fd, buffer, offset) != offset)
   {
      pgagroal_log_debug("Health: Failed to write startup packet");
      goto error;
   }

   status = pgagroal_read_socket_message(fd, &msg);

   if (status != MESSAGE_STATUS_OK || msg->kind != 'R')
   {
      pgagroal_log_debug("Health: Expected 'R' but got %c (status %d)", msg ? msg->kind : '?', status);
      goto error;
   }

   /* Auth Type is at offset 5 ('R'(1) + Length(4)) */
   auth_type = ntohl(*(int*)(msg->data + 5));

   if (auth_type == 0) /* AUTH_REQ_OK (Trust) */
   {
      pgagroal_log_debug("Health: Server %d requested Trust auth", server_idx);
      pgagroal_clear_message(msg);
      msg = NULL;
   }
   else if (auth_type == 5) /* MD5 */
   {
      pgagroal_log_debug("Health: Server %d requires MD5 auth", server_idx);

      password = pgagroal_get_user_password(config->health_check_user);
      if (password == NULL)
      {
         pgagroal_log_warn("Health: Password for %s not found", config->health_check_user);
         goto error;
      }

      if (pgagroal_md5_client_auth(msg, config->health_check_user, password, fd, NULL, &msg) == 0)
      {
         pgagroal_log_debug("Health: Server %d MD5 authentication successful", server_idx);
      }
      else
      {
         pgagroal_log_warn("Health: MD5 authentication failed");
         goto error;
      }
   }
   else if (auth_type == 10) /* SASL / SCRAM-SHA-256 */
   {
      pgagroal_log_debug("Health: Server %d requires SCRAM auth", server_idx);

      password = pgagroal_get_user_password(config->health_check_user);
      if (password == NULL)
      {
         pgagroal_log_warn("Health: Password for %s not found", config->health_check_user);
         goto error;
      }

      if (pgagroal_scram_client_auth(config->health_check_user, password, fd, NULL, &msg) == 0)
      {
         pgagroal_log_debug("Health: Server %d SCRAM authentication successful", server_idx);
      }
      else
      {
         pgagroal_log_warn("Health: SCRAM authentication failed");
         goto error;
      }
   }
   else
   {
      pgagroal_log_warn("Health check failed: Server requires Auth (Type %d), but we only support TRUST, MD5 and SCRAM", auth_type);
      goto error;
   }

   /* Wait for ReadyForQuery */
   while (true)
   {
      bool ready = false;

      if (msg == NULL)
      {
         status = pgagroal_read_socket_message(fd, &msg);
         if (status != MESSAGE_STATUS_OK || msg == NULL)
         {
            pgagroal_log_debug("Health: Failed to read ReadyForQuery (status %d)", status);
            goto error;
         }
      }

      offset_h = 0;
      while (offset_h < msg->length)
      {
         kind_h = pgagroal_read_byte(msg->data + offset_h);
         length_h = pgagroal_read_int32(msg->data + offset_h + 1);

         if (kind_h == 'Z')
         {
            ready = true;
            break;
         }
         else if (kind_h == 'E')
         {
            char* error = NULL;
            struct message* emsg = NULL;
            if (pgagroal_extract_message('E', msg, &emsg) == 0)
            {
               pgagroal_extract_error_message(emsg, &error);
               pgagroal_log_debug("Health: Received ErrorResponse: %s", error);
               free(error);
               pgagroal_free_message(emsg);
            }
         }

         offset_h += 1 + length_h;
      }

      pgagroal_clear_message(msg);
      msg = NULL;

      if (ready)
      {
         break;
      }
   }

   query_string = "SELECT 1";
   query_len = strlen(query_string);

   memset(buffer, 0, sizeof(buffer));
   buffer[0] = 'Q';
   pgagroal_write_int32(buffer + 1, 4 + query_len + 1);
   memcpy(buffer + 5, query_string, query_len);
   buffer[5 + query_len] = 0;

   msg_size = 1 + 4 + query_len + 1;

   if (pgagroal_write_socket(NULL, fd, buffer, msg_size) != msg_size)
   {
      pgagroal_log_debug("Health: Failed to write query");
      goto error;
   }

   while (true)
   {
      query_ready = false;
      status = pgagroal_read_socket_message(fd, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
      {
         pgagroal_log_debug("Health: Failed to read query response (status %d)", status);
         break;
      }

      offset_q = 0;
      while (offset_q < msg->length)
      {
         type = pgagroal_read_byte(msg->data + offset_q);
         length_q = pgagroal_read_int32(msg->data + offset_q + 1);

         if (type == 'T' || type == 'C')
         {
            query_success = true;
         }
         else if (type == 'E')
         {
            char* error = NULL;
            struct message* emsg = NULL;
            if (pgagroal_extract_message('E', msg, &emsg) == 0)
            {
               pgagroal_extract_error_message(emsg, &error);
               pgagroal_log_debug("Health: Query returned Error: %s", error);
               free(error);
               pgagroal_free_message(emsg);
            }
            query_success = false;
         }
         else if (type == 'Z')
         {
            query_ready = true;
            break;
         }

         offset_q += 1 + length_q;
      }

      pgagroal_clear_message(msg);
      msg = NULL;

      if (query_ready)
      {
         break;
      }
   }

   buffer[0] = 'X';
   pgagroal_write_int32(buffer + 1, 4);
   pgagroal_write_socket(NULL, fd, buffer, 5);

   pgagroal_disconnect(fd);

   *up = query_success;
   return 0;

error:
   if (msg)
   {
      pgagroal_clear_message(msg);
      msg = NULL;
   }
   if (fd != -1)
   {
      pgagroal_disconnect(fd);
   }
   return 1;
}
