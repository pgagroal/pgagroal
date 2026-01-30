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
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <server.h>
#include <status.h>
#include <utils.h>

static void status_details(bool details, struct json* response);

void
pgagroal_status(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* response = NULL;

   pgagroal_memory_init();
   pgagroal_start_logging();

   start_time = time(NULL);

   if (pgagroal_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   status_details(false, response);

   end_time = time(NULL);

   if (pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_NETWORK, compression, encryption, payload);
      pgagroal_log_error("Status: Error sending response");

      goto error;
   }

   elapsed = pgagroal_get_timestamp_string(start_time, end_time, &total_seconds);

   pgagroal_log_info("Status (Elapsed: %s)", elapsed);

   pgagroal_json_destroy(payload);

   pgagroal_disconnect(client_fd);

   pgagroal_stop_logging();
   pgagroal_memory_destroy();

   exit(0);

error:

   pgagroal_json_destroy(payload);

   pgagroal_disconnect(client_fd);

   pgagroal_stop_logging();
   pgagroal_memory_destroy();

   exit(1);
}

void
pgagroal_status_details(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* elapsed = NULL;
   time_t start_time;
   time_t end_time;
   int total_seconds;
   struct json* response = NULL;

   pgagroal_memory_init();
   pgagroal_start_logging();

   start_time = time(NULL);

   if (pgagroal_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   status_details(true, response);

   end_time = time(NULL);

   if (pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK, compression, encryption, payload);
      pgagroal_log_error("Status details: Error sending response");

      goto error;
   }

   elapsed = pgagroal_get_timestamp_string(start_time, end_time, &total_seconds);

   pgagroal_log_info("Status details (Elapsed: %s)", elapsed);

   pgagroal_json_destroy(payload);

   pgagroal_disconnect(client_fd);

   pgagroal_stop_logging();
   pgagroal_memory_destroy();

   exit(0);

error:

   pgagroal_json_destroy(payload);

   pgagroal_disconnect(client_fd);

   pgagroal_stop_logging();
   pgagroal_memory_destroy();

   exit(1);
}

static void
status_details(bool details, struct json* response)
{
   int active = 0;
   int total = 0;
   struct json* servers = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)(config->gracefully ? "Graceful shutdown" : "Running"), ValueString);

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);
      switch (state)
      {
         case STATE_IN_USE:
         case STATE_GRACEFULLY:
            active++;
            __attribute__((fallthrough));
         case STATE_INIT:
         case STATE_FREE:
         case STATE_FLUSH:
         case STATE_IDLE_CHECK:
         case STATE_MAX_CONNECTION_AGE:
         case STATE_VALIDATION:
         case STATE_REMOVE:
            total++;
            break;
         default:
            break;
      }
   }

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_ACTIVE_CONNECTIONS, (uintptr_t)active, ValueUInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_TOTAL_CONNECTIONS, (uintptr_t)total, ValueUInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_MAX_CONNECTIONS, (uintptr_t)config->max_connections, ValueUInt32);

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS, (uintptr_t)config->number_of_servers, ValueInt32);

   pgagroal_json_create(&servers);

   for (int i = 0; i < config->number_of_servers; i++)
   {
      struct json* js = NULL;
      char* server_status = NULL;

      if (strlen(config->servers[i].name) == 0)
      {
         continue;
      }

      /* Get server status (Running, Recovering, or Down) */
      if (pgagroal_get_server_status(i, &server_status))
      {
         server_status = strdup("Down");
      }

      pgagroal_json_create(&js);

      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_HOST, (uintptr_t)config->servers[i].host, ValueString);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_PORT, (uintptr_t)config->servers[i].port, ValueInt32);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)server_status, ValueString);

      pgagroal_json_put(servers, config->servers[i].name, (uintptr_t)js, ValueJSON);

      free(server_status);
   }

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_SERVERS, (uintptr_t)servers, ValueJSON);

   if (details)
   {
      int number_of_disabled = 0;
      struct json* limits = NULL;
      struct json* databases = NULL;
      struct json* connections = NULL;

      pgagroal_json_create(&limits);
      pgagroal_json_create(&connections);

      for (int i = 0; i < config->number_of_limits; i++)
      {
         struct json* js = NULL;

         pgagroal_json_create(&js);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)config->limits[i].database, ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_USERNAME, (uintptr_t)config->limits[i].username, ValueString);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ACTIVE_CONNECTIONS, (uintptr_t)atomic_load(&config->limits[i].active_connections), ValueUInt32);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_MAX_CONNECTIONS, (uintptr_t)config->limits[i].max_size, ValueUInt32);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_INITIAL_CONNECTIONS, (uintptr_t)config->limits[i].initial_size, ValueUInt32);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_MIN_CONNECTIONS, (uintptr_t)config->limits[i].min_size, ValueUInt32);

         pgagroal_json_append(limits, (uintptr_t)js, ValueJSON);
      }

      pgagroal_json_put(response, MANAGEMENT_ARGUMENT_LIMITS, (uintptr_t)limits, ValueJSON);

      pgagroal_json_create(&databases);

      for (int i = 0; i < NUMBER_OF_DISABLED; i++)
      {
         struct json* js = NULL;

         if (strlen(config->disabled[i]) > 0)
         {
            pgagroal_json_create(&js);

            pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)config->disabled[i], ValueString);
            pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)false, ValueBool);

            pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);

            number_of_disabled++;
         }
      }

      if (number_of_disabled == 0)
      {
         struct json* js = NULL;

         pgagroal_json_create(&js);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)"*", ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)!config->all_disabled, ValueBool);

         pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);
      }

      pgagroal_json_put(response, MANAGEMENT_ARGUMENT_DATABASES, (uintptr_t)databases, ValueJSON);

      for (int i = 0; i < config->max_connections; i++)
      {
         struct json* js = NULL;

         pgagroal_json_create(&js);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_START_TIME, (uintptr_t)config->connections[i].start_time, ValueInt64);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_TIMESTAMP, (uintptr_t)config->connections[i].timestamp, ValueInt64);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_PID, (uintptr_t)config->connections[i].pid, ValueInt32);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_FD, (uintptr_t)config->connections[i].fd, ValueInt32);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)config->connections[i].database, ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_USERNAME, (uintptr_t)config->connections[i].username, ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_APPNAME, (uintptr_t)config->connections[i].appname, ValueString);

         pgagroal_json_append(connections, (uintptr_t)js, ValueJSON);
      }

      pgagroal_json_put(response, MANAGEMENT_ARGUMENT_CONNECTIONS, (uintptr_t)connections, ValueJSON);
   }
}
