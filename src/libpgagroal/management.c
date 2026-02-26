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
#include <aes.h>
#include <bzip2_compression.h>
#include <configuration.h>
#include <gzip_compression.h>
#include <json.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <message.h>
#include <memory.h>
#include <network.h>
#include <pool.h>
#include <utils.h>
#include <value.h>
#include <zstandard_compression.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

static int read_uint8(char* prefix, SSL* ssl, int socket, uint8_t* i);
static int read_string(char* prefix, SSL* ssl, int socket, char** str);
static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_uint8(char* prefix, SSL* ssl, int socket, uint8_t i);
static int write_string(char* prefix, SSL* ssl, int socket, char* str);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);

int
pgagroal_management_request_flush(SSL* ssl, int socket, int32_t mode, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_FLUSH, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_MODE, (uintptr_t)mode, ValueInt32);
   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_enabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_ENABLEDB, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_config_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* payload)
{
   struct json* response = NULL;
   struct main_configuration* config;
   struct json* aliases_array = NULL;
   struct json* entry = NULL;
   struct json* alias_list = NULL;

   config = (struct main_configuration*)shmem;

   if (pgagroal_management_create_response(payload, -1, &response))
   {
      goto error;
   }

   if (pgagroal_json_create(&aliases_array))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_limits; i++)
   {
      entry = NULL;
      if (pgagroal_json_create(&entry))
      {
         goto error;
      }

      pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_USERNAME, (uintptr_t)config->limits[i].username, ValueString);
      pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_MAX_SIZE, (uintptr_t)config->limits[i].max_size, ValueInt64);
      pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_INITIAL_SIZE, (uintptr_t)config->limits[i].initial_size, ValueInt64);
      pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_MIN_SIZE, (uintptr_t)config->limits[i].min_size, ValueInt64);

      if (config->limits[i].aliases_count > 0)
      {
         alias_list = NULL;
         if (pgagroal_json_create(&alias_list))
         {
            goto error;
         }

         for (int j = 0; j < config->limits[i].aliases_count; j++)
         {
            pgagroal_json_append(alias_list, (uintptr_t)config->limits[i].aliases[j], ValueString);
         }
         pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_ALIASES, (uintptr_t)alias_list, ValueJSON);
      }
      else
      {
         if (pgagroal_json_create(&alias_list))
         {
            goto error;
         }
         pgagroal_json_put(entry, CONFIGURATION_ARGUMENT_LIMIT_ALIASES, (uintptr_t)alias_list, ValueJSON);
      }

      pgagroal_json_put(aliases_array, config->limits[i].database, (uintptr_t)entry, ValueJSON);
   }

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_DATABASES, (uintptr_t)aliases_array, ValueJSON);

   if (pgagroal_management_response_ok(ssl, socket, 0, 0, compression, encryption, payload))
   {
      goto error;
   }

   pgagroal_disconnect(socket);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);

error:
   pgagroal_disconnect(socket);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(1);
}

int
pgagroal_management_request_conf_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CONFIG_ALIAS, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_get_password(SSL* ssl, int socket, char* username, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_GET_PASSWORD, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_USERNAME, (uintptr_t)username, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_disabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_DISABLEDB, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_gracefully(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_GRACEFULLY, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_SHUTDOWN, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_cancel_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CANCEL_SHUTDOWN, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_STATUS, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_DETAILS, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_PING, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_clear(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CLEAR, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_clear_server(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CLEAR_SERVER, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_switch_to(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_SWITCH_TO, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_RELOAD, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CONFIG_LS, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_conf_get(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CONFIG_GET, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_request_conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (pgagroal_management_create_header(MANAGEMENT_CONFIG_SET, compression, encryption, output_format, &j))
   {
      goto error;
   }

   if (pgagroal_management_create_request(j, &request))
   {
      goto error;
   }

   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_CONFIG_KEY, (uintptr_t)config_key, ValueString);
   pgagroal_json_put(request, MANAGEMENT_ARGUMENT_CONFIG_VALUE, (uintptr_t)config_value, ValueString);

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, j))
   {
      goto error;
   }

   pgagroal_json_destroy(j);

   return 0;

error:

   pgagroal_json_destroy(j);

   return 1;
}

int
pgagroal_management_create_header(int32_t command, uint8_t compression, uint8_t encryption, int32_t output_format, struct json** json)
{
   time_t t;
   char timestamp[128];
   struct tm* time_info;
   struct json* j = NULL;
   struct json* header = NULL;

   *json = NULL;

   if (pgagroal_json_create(&j))
   {
      goto error;
   }

   if (pgagroal_json_create(&header))
   {
      goto error;
   }

   time(&t);
   time_info = localtime(&t);
   strftime(&timestamp[0], sizeof(timestamp), "%Y%m%d%H%M%S", time_info);

   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_COMMAND, (uintptr_t)command, ValueInt32);
   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_CLIENT_VERSION, (uintptr_t)PGAGROAL_VERSION, ValueString);
   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_OUTPUT, (uintptr_t)output_format, ValueUInt8);
   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_TIMESTAMP, (uintptr_t)timestamp, ValueString);
   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)compression, ValueUInt8);
   pgagroal_json_put(header, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)encryption, ValueUInt8);

   pgagroal_json_put(j, MANAGEMENT_CATEGORY_HEADER, (uintptr_t)header, ValueJSON);

   *json = j;

   return 0;

error:

   pgagroal_json_destroy(header);
   pgagroal_json_destroy(j);

   *json = NULL;

   return 1;
}

int
pgagroal_management_create_request(struct json* json, struct json** request)
{
   struct json* r = NULL;

   *request = NULL;

   if (pgagroal_json_create(&r))
   {
      goto error;
   }

   pgagroal_json_put(json, MANAGEMENT_CATEGORY_REQUEST, (uintptr_t)r, ValueJSON);

   *request = r;

   return 0;

error:

   pgagroal_json_destroy(r);

   return 1;
}

int
pgagroal_management_create_outcome_success(struct json* json, time_t start_time, time_t end_time, struct json** outcome)
{
   int32_t total_seconds = 0;
   char* elapsed = NULL;
   struct json* r = NULL;

   *outcome = NULL;

   if (pgagroal_json_create(&r))
   {
      goto error;
   }

   elapsed = pgagroal_get_timestamp_string(start_time, end_time, &total_seconds);

   pgagroal_json_put(r, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)true, ValueBool);
   pgagroal_json_put(r, MANAGEMENT_ARGUMENT_TIME, (uintptr_t)elapsed, ValueString);

   pgagroal_json_put(json, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)r, ValueJSON);

   *outcome = r;

   free(elapsed);

   return 0;

error:

   free(elapsed);

   pgagroal_json_destroy(r);

   return 1;
}

int
pgagroal_management_create_outcome_failure(struct json* json, int32_t error, struct json** outcome)
{
   struct json* r = NULL;

   *outcome = NULL;

   if (pgagroal_json_create(&r))
   {
      goto error;
   }

   pgagroal_json_put(r, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)false, ValueBool);
   pgagroal_json_put(r, MANAGEMENT_ARGUMENT_ERROR, (uintptr_t)error, ValueInt32);

   pgagroal_json_put(json, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)r, ValueJSON);

   *outcome = r;

   return 0;

error:

   pgagroal_json_destroy(r);

   return 1;
}

int
pgagroal_management_create_response(struct json* json, int server, struct json** response)
{
   struct json* r = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *response = NULL;

   if (pgagroal_json_create(&r))
   {
      goto error;
   }

   pgagroal_json_put(json, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)r, ValueJSON);

   if (server >= 0)
   {
      pgagroal_json_put(r, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
   }

   pgagroal_json_put(r, MANAGEMENT_ARGUMENT_SERVER_VERSION, (uintptr_t)PGAGROAL_VERSION, ValueString);

   *response = r;

   return 0;

error:

   pgagroal_json_destroy(r);

   return 1;
}

int
pgagroal_management_response_ok(SSL* ssl, int socket, time_t start_time, time_t end_time, uint8_t compression, uint8_t encryption, struct json* payload)
{
   struct json* outcome = NULL;

   if (pgagroal_management_create_outcome_success(payload, start_time, end_time, &outcome))
   {
      goto error;
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, payload))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_response_error(SSL* ssl, int socket, char* server, int32_t error, uint8_t compression, uint8_t encryption, struct json* payload)
{
   int srv = -1;
   struct json* response = NULL;
   struct json* outcome = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (pgagroal_management_create_outcome_failure(payload, error, &outcome))
   {
      goto error;
   }

   if (server != NULL && strlen(server) > 0)
   {
      if (pgagroal_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE) != 0)
      {
         response = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
      }
      else
      {
         for (int i = 0; i < config->number_of_servers; i++)
         {
            if (!strcmp(server, config->servers[i].name))
            {
               srv = i;
            }
         }

         if (pgagroal_management_create_response(payload, srv, &response))
         {
            goto error;
         }

         pgagroal_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
      }
   }

   if (pgagroal_management_write_json(ssl, socket, compression, encryption, payload))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgagroal_management_read_json(SSL* ssl, int socket, uint8_t* compression, uint8_t* encryption, struct json** json)
{
   uint8_t compress_method = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encrypt_method = MANAGEMENT_ENCRYPTION_NONE;
   char* s = NULL;
   struct json* r = NULL;

   unsigned char* transfer_buffer = NULL;
   unsigned char* decoded_buffer = NULL;
   unsigned char* decrypted_buffer = NULL;
   char* decompressed = NULL;
   size_t transfer_size = 0;
   size_t decoded_size = 0;
   size_t decrypted_size = 0;

   if (read_uint8("pgagroal-cli", ssl, socket, &compress_method))
   {
      goto error;
   }

   if (compression != NULL)
   {
      *compression = compress_method;
   }

   if (read_uint8("pgagroal-cli", ssl, socket, &encrypt_method))
   {
      goto error;
   }

   if (encryption != NULL)
   {
      *encryption = encrypt_method;
   }

   if (read_string("pgagroal-cli", ssl, socket, &s))
   {
      goto error;
   }

   if (compress_method || encrypt_method)
   {
      // First, perform decode
      if (pgagroal_base64_decode(s, strlen(s), (void**)&decoded_buffer, &decoded_size) != 0)
      {
         pgagroal_log_error("pgagroal_management_read_json: Decoding failedg");
         goto error;
      }
      free(s);
      s = NULL;
      transfer_buffer = decoded_buffer;
      transfer_size = decoded_size;
      decoded_buffer = NULL;

      // Second, perform dencrypt
      switch (encrypt_method)
      {
         case MANAGEMENT_ENCRYPTION_AES256:
            if (pgagroal_decrypt_buffer(transfer_buffer, transfer_size, &decrypted_buffer, &decrypted_size, MANAGEMENT_ENCRYPTION_AES256))
            {
               pgagroal_log_error("pgagroal_management_read_json: Failed to aes256 dencrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = decrypted_buffer;
            transfer_size = decrypted_size;
            decrypted_buffer = NULL;

            break;
         case MANAGEMENT_ENCRYPTION_AES192:
            if (pgagroal_decrypt_buffer(transfer_buffer, transfer_size, &decrypted_buffer, &decrypted_size, MANAGEMENT_ENCRYPTION_AES192))
            {
               pgagroal_log_error("pgagroal_management_read_json: Failed to aes192 dencrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = decrypted_buffer;
            transfer_size = decrypted_size;
            decrypted_buffer = NULL;

            break;
         case MANAGEMENT_ENCRYPTION_AES128:
            if (pgagroal_decrypt_buffer(transfer_buffer, transfer_size, &decrypted_buffer, &decrypted_size, MANAGEMENT_ENCRYPTION_AES128))
            {
               pgagroal_log_error("pgagroal_management_read_json: Failed to aes128 dencrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = decrypted_buffer;
            transfer_size = decrypted_size;
            decrypted_buffer = NULL;

            break;
         default:
            break;
      }

      // Third, perform decompress
      switch (compress_method)
      {
         case MANAGEMENT_COMPRESSION_GZIP:
            if (pgagroal_gunzip_string(transfer_buffer, transfer_size, &decompressed))
            {
               pgagroal_log_error("pgagroal_management_read_json: GZIP decompress failed");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = NULL;
            s = decompressed;
            decompressed = NULL;
            break;
         case MANAGEMENT_COMPRESSION_ZSTD:
            if (pgagroal_zstdd_string(transfer_buffer, transfer_size, &decompressed))
            {
               pgagroal_log_error("pgagroal_management_read_json: ZSTD decompress failed");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = NULL;
            s = decompressed;
            decompressed = NULL;
            break;
         case MANAGEMENT_COMPRESSION_LZ4:
            if (pgagroal_lz4d_string(transfer_buffer, transfer_size, &decompressed))
            {
               pgagroal_log_error("pgagroal_management_read_json: LZ4 decompress failed");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = NULL;
            s = decompressed;
            decompressed = NULL;
            break;
         case MANAGEMENT_COMPRESSION_BZIP2:
            if (pgagroal_bunzip2_string(transfer_buffer, transfer_size, &decompressed))
            {
               pgagroal_log_error("pgagroal_management_read_json: bzip2 decompress failed");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = NULL;
            s = decompressed;
            decompressed = NULL;
            break;
         default:
            s = (char*)transfer_buffer;
            transfer_buffer = NULL;
            break;
      }
   }

   if (pgagroal_json_parse_string(s, &r))
   {
      goto error;
   }

   *json = r;

   free(s);

   return 0;

error:

   pgagroal_json_destroy(r);

   if (s != NULL)
   {
      free(s);
   }
   if (transfer_buffer != NULL)
   {
      free(transfer_buffer);
   }
   if (decoded_buffer != NULL)
   {
      free(decoded_buffer);
   }
   if (decrypted_buffer != NULL)
   {
      free(decrypted_buffer);
   }
   if (decompressed != NULL)
   {
      free(decompressed);
   }

   return 1;
}

int
pgagroal_management_write_json(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* json)
{
   char* s = NULL;

   unsigned char* transfer_buffer = NULL;
   unsigned char* compressed_buffer = NULL;
   unsigned char* encrypted_buffer = NULL;
   char* encoded = NULL;
   size_t transfer_size = 0;
   size_t compressed_size = 0;
   size_t encrypted_size = 0;
   size_t encoded_size = 0;

   s = pgagroal_json_to_string(json, FORMAT_JSON_COMPACT, NULL, 0);

   if (write_uint8("pgagroal-cli", ssl, socket, compression))
   {
      goto error;
   }

   if (write_uint8("pgagroal-cli", ssl, socket, encryption))
   {
      goto error;
   }

   if (compression || encryption)
   {
      // First, perform compress
      switch (compression)
      {
         case MANAGEMENT_COMPRESSION_GZIP:
            if (pgagroal_gzip_string(s, &compressed_buffer, &compressed_size))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to gzip the string");
               goto error;
            }
            transfer_buffer = compressed_buffer;
            transfer_size = compressed_size;

            free(s);
            compressed_buffer = NULL;
            s = NULL;
            break;
         case MANAGEMENT_COMPRESSION_ZSTD:
            if (pgagroal_zstdc_string(s, &compressed_buffer, &compressed_size))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to zstd the string");
               goto error;
            }
            transfer_buffer = compressed_buffer;
            transfer_size = compressed_size;

            free(s);
            compressed_buffer = NULL;
            s = NULL;
            break;
         case MANAGEMENT_COMPRESSION_LZ4:
            if (pgagroal_lz4c_string(s, &compressed_buffer, &compressed_size))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to lz4 the string");
               goto error;
            }
            transfer_buffer = compressed_buffer;
            transfer_size = compressed_size;

            free(s);
            compressed_buffer = NULL;
            s = NULL;
            break;
         case MANAGEMENT_COMPRESSION_BZIP2:
            if (pgagroal_bzip2_string(s, &compressed_buffer, &compressed_size))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to bzip2 the string");
               goto error;
            }
            transfer_buffer = compressed_buffer;
            transfer_size = compressed_size;

            free(s);
            compressed_buffer = NULL;
            s = NULL;
            break;
         default:
            transfer_buffer = (unsigned char*)s;
            transfer_size = strlen(s);
            s = NULL;
            break;
      }

      // Second, perform encrypt
      switch (encryption)
      {
         case MANAGEMENT_ENCRYPTION_AES256:
            if (pgagroal_encrypt_buffer(transfer_buffer, transfer_size, &encrypted_buffer, &encrypted_size, MANAGEMENT_ENCRYPTION_AES256))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to aes256 encrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = encrypted_buffer;
            transfer_size = encrypted_size;
            encrypted_buffer = NULL;

            break;
         case MANAGEMENT_ENCRYPTION_AES192:
            if (pgagroal_encrypt_buffer(transfer_buffer, transfer_size, &encrypted_buffer, &encrypted_size, MANAGEMENT_ENCRYPTION_AES192))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to aes192 encrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = encrypted_buffer;
            transfer_size = encrypted_size;
            encrypted_buffer = NULL;

            break;
         case MANAGEMENT_ENCRYPTION_AES128:
            if (pgagroal_encrypt_buffer(transfer_buffer, transfer_size, &encrypted_buffer, &encrypted_size, MANAGEMENT_ENCRYPTION_AES128))
            {
               pgagroal_log_error("pgagroal_management_write_json: Failed to aes128 encrypt the string");
               goto error;
            }
            free(transfer_buffer);
            transfer_buffer = encrypted_buffer;
            transfer_size = encrypted_size;
            encrypted_buffer = NULL;

            break;
         default:
            break;
      }

      // Third, perform base64 encode
      if (pgagroal_base64_encode(transfer_buffer, transfer_size, &encoded, &encoded_size) != 0)
      {
         pgagroal_log_error("pgagroal_management_write_json: Encoding failed");
         goto error;
      }

      free(transfer_buffer);
      s = encoded;
      encoded = NULL;
   }

   if (write_string("pgagroal-cli", ssl, socket, s))
   {
      goto error;
   }

   free(s);

   return 0;

error:
   if (s != NULL)
   {
      free(s);
   }
   if (transfer_buffer != NULL)
   {
      free(transfer_buffer);
   }
   if (compressed_buffer != NULL)
   {
      free(compressed_buffer);
   }
   if (encrypted_buffer != NULL)
   {
      free(encrypted_buffer);
   }
   if (encoded != NULL)
   {
      free(encoded);
   }

   return 1;
}

static int
read_uint8(char* prefix, SSL* ssl, int socket, uint8_t* i)
{
   char buf1[1] = {0};

   *i = 0;

   if (read_complete(ssl, socket, &buf1[0], sizeof(buf1)))
   {
      pgagroal_log_warn("%s: read_byte: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgagroal_read_uint8(&buf1);

   return 0;

error:

   return 1;
}

static int
read_string(char* prefix, SSL* ssl, int socket, char** str)
{
   char* s = NULL;
   char buf4[4] = {0};
   uint32_t size;

   *str = NULL;

   if (read_complete(ssl, socket, &buf4[0], sizeof(buf4)))
   {
      pgagroal_log_warn("%s: read_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgagroal_read_uint32(&buf4);
   if (size > 0)
   {
      s = malloc(size + 1);

      if (s == NULL)
      {
         goto error;
      }

      memset(s, 0, size + 1);

      if (read_complete(ssl, socket, s, size))
      {
         pgagroal_log_warn("%s: read_string: %p %d %s", prefix, ssl, socket, strerror(errno));
         errno = 0;
         goto error;
      }

      *str = s;
   }

   return 0;

error:

   free(s);

   return 1;
}

static int
write_uint8(char* prefix, SSL* ssl, int socket, uint8_t i)
{
   char buf1[1] = {0};

   pgagroal_write_uint8(&buf1, i);
   if (write_complete(ssl, socket, &buf1, sizeof(buf1)))
   {
      pgagroal_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_string(char* prefix, SSL* ssl, int socket, char* str)
{
   char buf4[4] = {0};

   pgagroal_write_uint32(&buf4, str != NULL ? strlen(str) : 0);
   if (write_complete(ssl, socket, &buf4, sizeof(buf4)))
   {
      pgagroal_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (str != NULL)
   {
      if (write_complete(ssl, socket, str, strlen(str)))
      {
         pgagroal_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r == 0)
   {
      goto error;
   }
   else if (r < needs)
   {
      SLEEP(10000000L)

      pgagroal_log_trace("Got: %ld, needs: %ld", r, needs);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == (ssize_t)size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == (ssize_t)size)
         {
            return 0;
         }

         pgagroal_log_debug("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgagroal_log_debug("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgagroal_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgagroal_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}
