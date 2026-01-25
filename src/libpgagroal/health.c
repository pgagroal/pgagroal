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

/* system */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

static void health_check_loop(void);
static void write_int32(char* buf, int value);
static bool pgagroal_server_probe(int server_idx);

void
pgagroal_start_health_check(int argc, char** argv)
{
   pid_t pid;

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("Unable to fork health check process");
      return;
   }
   else if (pid == 0)
   {
      /* Child process */
      pgagroal_set_proc_title(argc, argv, "health check", NULL);
      health_check_loop();
   }

   /* Parent process returns */
}

static void
health_check_loop(void)
{
   struct main_configuration* config;
   int period;

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
      /* Sleep for the defined period (usleep takes microseconds) */
      usleep(period * 1000000);

      if (!config->keep_running)
      {
         break;
      }

      pgagroal_log_debug("Health check run");

      for (int i = 0; i < config->number_of_servers; i++)
      {
         bool up = pgagroal_server_probe(i);

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

   pgagroal_log_info("Health check stopped");
   exit(0);
}

static void
write_int32(char* buf, int value)
{
   int n = htonl(value);
   memcpy(buf, &n, 4);
}

static bool
pgagroal_server_probe(int server_idx)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct server* server = &config->servers[server_idx];
   int fd = -1;
   char buffer[1024];
   int offset = 0;



   /* ---------------------------------------------------------
    * STEP 1: Connect
    * --------------------------------------------------------- */
   /* Use true for keep_alive, false for no_delay as default safe args */
   if (pgagroal_connect(server->host, server->port, &fd, true, false))
   {
      pgagroal_log_debug("Health: Failed to connect to server %d (%s:%d)", server_idx, server->host, server->port);
      return false;
   }

   /* ---------------------------------------------------------
    * STEP 2: Construct Startup Packet (Protocol v3.0)
    * --------------------------------------------------------- */
   /* Format: [Length (4)] [Version (4)] [user\0name\0] [database\0name\0] [\0] */

   memset(buffer, 0, sizeof(buffer));
   offset = 8; // Skip Length (4) and Version (4) for now

   /* Parameter: user */
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "user");
   offset += 5;
   /* Use the server's username or fallback to "postgres" - simplifying to postgres for now */
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "postgres");
   offset += strlen("postgres") + 1;

   /* Parameter: database */
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "database");
   offset += 9;
   pgagroal_snprintf(buffer + offset, sizeof(buffer) - offset, "postgres");
   offset += strlen("postgres") + 1;

   /* Terminator */
   buffer[offset] = 0;
   offset += 1;

   /* Now write the header */
   write_int32(buffer, offset);     // Total Length
   write_int32(buffer + 4, 196608); // Protocol Version 3.0 (0x00030000)

   /* Send it */
   if (pgagroal_write_socket(NULL, fd, buffer, offset) != 0)
   {
      pgagroal_disconnect(fd);
      return false;
   }

   /* ---------------------------------------------------------
    * STEP 3: Handle Authentication (Expect 'R')
    * --------------------------------------------------------- */
   /* Read the Message Type (1 byte) */
   if (pgagroal_read_socket(NULL, fd, buffer, 1) != 1)
   {
      pgagroal_disconnect(fd);
      return false;
   }
   char msg_type = buffer[0];

   /* Read Length (4 bytes) */
   if (pgagroal_read_socket(NULL, fd, buffer, 4) != 4)
   {
      pgagroal_disconnect(fd);
      return false;
   }

   if (msg_type == 'E')
   {
      /* Error Response from server */
      pgagroal_log_debug("Health: Server returned Error on startup");
      pgagroal_disconnect(fd);
      return false;
   }

   if (msg_type == 'R')
   {
      /* Authentication Request */
      /* Read the payload (Auth Type) */
      if (pgagroal_read_socket(NULL, fd, buffer, 4) != 4)
      {
         pgagroal_disconnect(fd);
         return false;
      }
      int auth_type = ntohl(*(int*)buffer);

      if (auth_type != 0)
      {
         pgagroal_log_warn("Health check failed: Server requires Auth (Type %d), but we only support TRUST", auth_type);
         pgagroal_disconnect(fd);
         return false;
      }

      /* If AuthOK (0), wait for ReadyForQuery ('Z') */
      while (1)
      {
         if (pgagroal_read_socket(NULL, fd, buffer, 1) != 1)
         {
            pgagroal_disconnect(fd);
            return false;
         }
         msg_type = buffer[0];

         /* Read length */
         if (pgagroal_read_socket(NULL, fd, buffer, 4) != 4)
         {
            pgagroal_disconnect(fd);
            return false;
         }
         int len = ntohl(*(int*)buffer) - 4;

         /* Skip payload */
         if (len > 0)
         {
            while (len > 0)
            {
               int to_read = (len > (int)sizeof(buffer)) ? (int)sizeof(buffer) : len;
               if (pgagroal_read_socket(NULL, fd, buffer, to_read) != to_read)
               {
                  pgagroal_disconnect(fd);
                  return false;
               }
               len -= to_read;
            }
         }

         if (msg_type == 'Z')
            break; // Ready!
         if (msg_type == 'E')
         {
            pgagroal_disconnect(fd);
            return false;
         }
      }
   }

   /* ---------------------------------------------------------
    * STEP 4: Send Query ('Q')
    * --------------------------------------------------------- */

   /* Reset buffer for the Query message */
   memset(buffer, 0, sizeof(buffer));

   /* Message Type 'Q' */
   buffer[0] = 'Q';

   /* Use the configured query, or default to "SELECT 1" */
   char* query_string = "SELECT 1";
   if (config->health_check_query[0] != 0)
   {
      query_string = config->health_check_query;
   }

   int query_len = strlen(query_string);

   write_int32(buffer + 1, 4 + query_len + 1);

   memcpy(buffer + 5, query_string, query_len);
   buffer[5 + query_len] = 0; // Null terminator

   int msg_size = 1 + 4 + query_len + 1;

   /* Send 'Q' packet */
   if (pgagroal_write_socket(NULL, fd, buffer, msg_size) != 0)
   {
      pgagroal_log_debug("Health: Failed to write query");
      pgagroal_disconnect(fd);
      return false;
   }

   /* ---------------------------------------------------------
    * STEP 5: Process Results (Expect 'T' or 'C', then 'Z')
    * --------------------------------------------------------- */
   bool query_success = false;

   while (true)
   {
      /* Read Message Type */
      if (pgagroal_read_socket(NULL, fd, buffer, 1) != 1)
         break;
      char type = buffer[0];

      /* Read Length */
      if (pgagroal_read_socket(NULL, fd, buffer, 4) != 4)
         break;
      int len = ntohl(*(int*)buffer) - 4;

      while (len > 0)
      {
         int chunk = (len > (int)sizeof(buffer)) ? (int)sizeof(buffer) : len;
         if (pgagroal_read_socket(NULL, fd, buffer, chunk) != chunk)
            goto done;
         len -= chunk;
      }

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
         break; // We are done
      }
   }

done:
   /* ---------------------------------------------------------
    * STEP 6: Cleanup
    * --------------------------------------------------------- */
   buffer[0] = 'X';
   write_int32(buffer + 1, 4);
   pgagroal_write_socket(NULL, fd, buffer, 5);

   pgagroal_disconnect(fd);

   return query_success;
}
