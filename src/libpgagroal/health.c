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
static bool server_probe(int server_idx);

void
pgagroal_start_health_check(void)
{
   pid_t pid;
   char title[MISC_LENGTH];
   char* args[] = {title, NULL};

   memset(&title, 0, sizeof(title));
   snprintf(&title[0], sizeof(title), "pgagroal: health-check");

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("Unable to fork health check process");
      return;
   }
   else if (pid == 0)
   {
      pgagroal_set_proc_title(1, args, "health check", NULL);
      health_check_loop();
      exit(0);
   }
}

static void
health_check_loop(void)
{
   struct main_configuration* config;
   unsigned int period;

   config = (struct main_configuration*)shmem;

   pgagroal_log_info("Health check started");

   period = config->health_check_period;
   if (period <= 0)
   {
      period = 5;
      pgagroal_log_warn("Health: Period was %d, forcing to %d seconds", config->health_check_period, period);
   }
   else
   {
      pgagroal_log_debug("Health: running every %d seconds", period);
   }

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
         bool up = server_probe(i);

         if (!up)
         {
            pgagroal_log_debug("Health: Server %d is DOWN", i);
         }
         else
         {
            pgagroal_log_debug("Health: Server %d is UP", i);
         }
      }
   }
}

static bool
server_probe(int server_idx)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct server* server = &config->servers[server_idx];
   int fd = -1;
   char buffer[1024];
   int offset = 0;
   struct message* msg = NULL;
   int status;
   size_t start_packet_size;
   char* password = NULL;

   pgagroal_log_debug("Health: Probing server %d as %s", server_idx, config->health_check_user);

   if (pgagroal_connect(server->host, server->port, &fd, true, false) != 0)
   {
      pgagroal_log_debug("Health: Failed to connect to server %d (%s:%d)", server_idx, server->host, server->port);
      return false;
   }

   /* Construct Startup Packet (Protocol v3.0) */
   start_packet_size = 4 + 4 + 5 + strlen(config->health_check_user) + 1 + 9 + strlen("postgres") + 1 + 1;

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
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "postgres");
   offset += strlen("postgres") + 1;

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
   int auth_type = ntohl(*(int*)(msg->data + 5));

   if (auth_type == 0) /* AUTH_REQ_OK (Trust) */
   {
      pgagroal_log_debug("Health: Server %d is UP (Trust)", server_idx);
      pgagroal_clear_message(msg);
      msg = NULL;

      buffer[0] = 'X';
      pgagroal_write_int32(buffer + 1, 4);
      pgagroal_write_socket(NULL, fd, buffer, 5);

      pgagroal_disconnect(fd);
      return true;
   }
   else if (auth_type == 5) /* MD5 */
   {
      pgagroal_log_debug("Health: Server requires MD5 auth");

      password = pgagroal_get_user_password(config->health_check_user);
      if (password == NULL)
      {
         pgagroal_log_warn("Health: Password for %s not found", config->health_check_user);
         goto error;
      }

      if (pgagroal_md5_client_auth(msg, config->health_check_user, password, fd, NULL) == 0)
      {
         pgagroal_log_debug("Health: Server %d is UP (MD5)", server_idx);
         pgagroal_clear_message(msg);
         msg = NULL;
         pgagroal_disconnect(fd);
         return true;
      }

      pgagroal_log_warn("Health: MD5 authentication failed");
      goto error;
   }
   else if (auth_type == 10) /* SASL / SCRAM-SHA-256 */
   {
      pgagroal_log_debug("Health: Server requires SCRAM auth");

      password = pgagroal_get_user_password(config->health_check_user);
      if (password == NULL)
      {
         pgagroal_log_warn("Health: Password for %s not found", config->health_check_user);
         goto error;
      }

      if (pgagroal_scram_client_auth(config->health_check_user, password, fd, NULL) == 0)
      {
         pgagroal_log_debug("Health: Server %d is UP (SCRAM)", server_idx);
         pgagroal_clear_message(msg);
         msg = NULL;
         pgagroal_disconnect(fd);
         return true;
      }

      pgagroal_log_warn("Health: SCRAM authentication failed");
      goto error;
   }
   else
   {
      pgagroal_log_warn("Health check failed: Server requires Auth (Type %d), but we only support TRUST", auth_type);
      goto error;
   }

   char* query_string = "SELECT 1";
   int query_len = strlen(query_string);

   memset(buffer, 0, sizeof(buffer));
   buffer[0] = 'Q';
   pgagroal_write_int32(buffer + 1, 4 + query_len + 1);
   memcpy(buffer + 5, query_string, query_len);
   buffer[5 + query_len] = 0;

   int msg_size = 1 + 4 + query_len + 1;

   if (pgagroal_write_socket(NULL, fd, buffer, msg_size) != msg_size)
   {
      pgagroal_log_debug("Health: Failed to write query");
      goto error;
   }

   bool query_success = false;

   while (true)
   {
      status = pgagroal_read_socket_message(fd, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
         break;

      char type = msg->kind;
      pgagroal_clear_message(msg);
      msg = NULL;

      if (type == 'T' || type == 'C')
      {
         query_success = true;
      }
      else if (type == 'E')
      {
         query_success = false;
         pgagroal_log_debug("Health: Query returned Error");
      }
      else if (type == 'Z')
      {
         break;
      }
   }

   buffer[0] = 'X';
   pgagroal_write_int32(buffer + 1, 4);
   pgagroal_write_socket(NULL, fd, buffer, 5);

   pgagroal_disconnect(fd);

   return query_success;

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
   return false;
}
