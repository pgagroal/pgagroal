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

#include <pgagroal.h>
#include <art.h>
#include <deque.h>
#include <logging.h>
#include <network.h>
#include <prometheus_client.h>
#include <security.h>
#include <utils.h>
#include <value.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define PROMETHEUS_LABEL_LENGTH 1024

static int parse_body_to_bridge(time_t timestamp, char* body, struct prometheus_bridge* bridge);
static int metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric);
static int metric_set_name(struct prometheus_metric* metric, char* name);
static int metric_set_help(struct prometheus_metric* metric, char* help);
static int metric_set_type(struct prometheus_metric* metric, char* type);
static bool attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute);
static int attributes_find_create(struct deque* definitions, struct deque* input, struct prometheus_attributes** attributes, bool* is_new);
static int add_attribute(struct deque* attributes, char* key, char* value);
static int add_value(struct deque* values, time_t timestamp, char* value);
static int add_line(struct main_configuration* config, struct prometheus_metric* metric, char* line, time_t timestamp);
static int parse_metric_name_from_line(char* line, char* metric_name, size_t size);
static int fetch_metrics_body(const char* host, int port, bool secure, char** body);
static int write_all(SSL* ssl, int fd, const char* buffer, size_t size);
static int read_all(SSL* ssl, int fd, char** response, size_t* response_size);
static int decode_http_response_body(const char* response, size_t response_size, char** body);
static int decode_chunked_body(const char* chunked, size_t chunked_size, char** body, size_t* body_size);

static void prometheus_metric_destroy_cb(uintptr_t data);
static char* deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_attributes_destroy_cb(uintptr_t data);
static char* prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_value_destroy_cb(uintptr_t data);
static char* prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_attribute_destroy_cb(uintptr_t data);
static char* prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent);

int
pgagroal_prometheus_client_create_bridge(struct prometheus_bridge** bridge)
{
   struct prometheus_bridge* b = NULL;

   if (bridge == NULL)
   {
      return 1;
   }

   *bridge = NULL;

   b = (struct prometheus_bridge*)malloc(sizeof(struct prometheus_bridge));
   if (b == NULL)
   {
      pgagroal_log_error("Failed to allocate bridge");
      goto error;
   }

   memset(b, 0, sizeof(struct prometheus_bridge));

   if (pgagroal_art_create(&b->metrics))
   {
      pgagroal_log_error("Failed to create ART");
      goto error;
   }

   *bridge = b;

   return 0;

error:
   if (b != NULL)
   {
      pgagroal_art_destroy(b->metrics);
      free(b);
   }

   return 1;
}

int
pgagroal_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge)
{
   if (bridge != NULL)
   {
      pgagroal_art_destroy(bridge->metrics);
   }

   free(bridge);

   return 0;
}

int
pgagroal_prometheus_client_get(int endpoint, struct prometheus_bridge* bridge)
{
   time_t timestamp;
   struct main_configuration* config = NULL;
   bool secure = false;
   char* body_copy = NULL;
   char* host = NULL;

   (void)endpoint;

   config = (struct main_configuration*)shmem;
   if (config == NULL || bridge == NULL)
   {
      goto error;
   }

   if (config->common.metrics <= 0)
   {
      pgagroal_log_error("Metrics listener is not enabled");
      goto error;
   }

   host = config->common.host;

   secure = strlen(config->common.metrics_cert_file) > 0 && strlen(config->common.metrics_key_file) > 0;

   pgagroal_log_debug("Endpoint %s://%s:%d/metrics", secure ? "https" : "http", host, config->common.metrics);

   if (fetch_metrics_body(host, config->common.metrics, secure, &body_copy))
   {
      pgagroal_log_error("Failed to fetch /metrics from %s:%d", host, config->common.metrics);
      goto error;
   }

   timestamp = time(NULL);
   if (parse_body_to_bridge(timestamp, body_copy, bridge))
   {
      goto error;
   }

   free(body_copy);

   return 0;

error:

   free(body_copy);

   return 1;
}

static int
write_all(SSL* ssl, int fd, const char* buffer, size_t size)
{
   size_t offset = 0;

   while (offset < size)
   {
      int written = pgagroal_write_socket(ssl, fd, (char*)buffer + offset, size - offset);
      if (written <= 0)
      {
         return 1;
      }
      offset += (size_t)written;
   }

   return 0;
}

static int
read_all(SSL* ssl, int fd, char** response, size_t* response_size)
{
   char buffer[8192];
   char* out = NULL;
   size_t used = 0;
   size_t capacity = 0;

   if (response == NULL || response_size == NULL)
   {
      return 1;
   }

   *response = NULL;
   *response_size = 0;

   for (;;)
   {
      int n = pgagroal_read_socket(ssl, fd, buffer, sizeof(buffer));
      if (n == 0)
      {
         break;
      }

      if (n < 0)
      {
         if (errno == EINTR)
         {
            errno = 0;
            continue;
         }
         free(out);
         return 1;
      }

      if (used + (size_t)n + 1 > capacity)
      {
         size_t new_capacity = capacity == 0 ? 16384 : capacity;
         while (used + (size_t)n + 1 > new_capacity)
         {
            new_capacity *= 2;
         }

         char* resized = realloc(out, new_capacity);
         if (resized == NULL)
         {
            free(out);
            return 1;
         }

         out = resized;
         capacity = new_capacity;
      }

      memcpy(out + used, buffer, (size_t)n);
      used += (size_t)n;
   }

   if (out == NULL)
   {
      out = strdup("");
      if (out == NULL)
      {
         return 1;
      }
   }

   out[used] = '\0';
   *response = out;
   *response_size = used;

   return 0;
}

static int
decode_chunked_body(const char* chunked, size_t chunked_size, char** body, size_t* body_size)
{
   size_t offset = 0;
   size_t output_size = 0;
   size_t output_capacity = chunked_size + 1;
   char* output = malloc(output_capacity);

   if (output == NULL)
   {
      return 1;
   }

   while (offset < chunked_size)
   {
      size_t line_end = offset;
      while (line_end + 1 < chunked_size && !(chunked[line_end] == '\r' && chunked[line_end + 1] == '\n'))
      {
         line_end++;
      }

      if (line_end + 1 >= chunked_size)
      {
         free(output);
         return 1;
      }

      char length_buffer[32];
      size_t length_size = line_end - offset;
      if (length_size == 0 || length_size >= sizeof(length_buffer))
      {
         free(output);
         return 1;
      }

      memcpy(length_buffer, chunked + offset, length_size);
      length_buffer[length_size] = '\0';

      char* endptr = NULL;
      unsigned long chunk_length = strtoul(length_buffer, &endptr, 16);
      if (endptr == length_buffer)
      {
         free(output);
         return 1;
      }

      offset = line_end + 2;

      if (chunk_length == 0)
      {
         break;
      }

      if (offset + chunk_length + 2 > chunked_size)
      {
         free(output);
         return 1;
      }

      if (output_size + chunk_length + 1 > output_capacity)
      {
         size_t new_capacity = output_capacity;
         while (output_size + chunk_length + 1 > new_capacity)
         {
            new_capacity *= 2;
         }

         char* resized = realloc(output, new_capacity);
         if (resized == NULL)
         {
            free(output);
            return 1;
         }

         output = resized;
         output_capacity = new_capacity;
      }

      memcpy(output + output_size, chunked + offset, chunk_length);
      output_size += chunk_length;
      offset += chunk_length;

      if (!(chunked[offset] == '\r' && chunked[offset + 1] == '\n'))
      {
         free(output);
         return 1;
      }

      offset += 2;
   }

   output[output_size] = '\0';
   *body = output;
   *body_size = output_size;

   return 0;
}

static int
decode_http_response_body(const char* response, size_t response_size, char** body)
{
   size_t header_end = 0;
   bool found = false;

   if (response == NULL || body == NULL)
   {
      return 1;
   }

   *body = NULL;

   for (size_t i = 0; i + 3 < response_size; i++)
   {
      if (response[i] == '\r' && response[i + 1] == '\n' && response[i + 2] == '\r' && response[i + 3] == '\n')
      {
         header_end = i + 4;
         found = true;
         break;
      }
   }

   if (!found || header_end > response_size)
   {
      return 1;
   }

   const char* header = response;
   size_t header_size = header_end;
   const char* payload = response + header_end;
   size_t payload_size = response_size - header_end;

   for (size_t i = 0; i + 26 <= header_size; i++)
   {
      if (strncasecmp(header + i, "Transfer-Encoding: chunked", 26) == 0)
      {
         size_t chunked_body_size = 0;
         return decode_chunked_body(payload, payload_size, body, &chunked_body_size);
      }
   }

   *body = strndup(payload, payload_size);

   return *body == NULL ? 1 : 0;
}

static int
fetch_metrics_body(const char* host, int port, bool secure, char** body)
{
   int fd = -1;
   SSL_CTX* ssl_ctx = NULL;
   SSL* ssl = NULL;
   char request[1024];
   char* response = NULL;
   size_t response_size = 0;
   int status = 1;

   if (host == NULL || body == NULL)
   {
      return 1;
   }

   *body = NULL;

   if (pgagroal_connect(host, port, &fd, false, true))
   {
      pgagroal_log_error("Unable to connect to metrics endpoint %s:%d", host, port);
      goto done;
   }

   if (secure)
   {
      if (pgagroal_create_ssl_ctx(true, &ssl_ctx))
      {
         pgagroal_log_error("Unable to create SSL context for metrics scraping");
         goto done;
      }

      SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

      ssl = SSL_new(ssl_ctx);
      if (ssl == NULL)
      {
         pgagroal_log_error("Unable to create SSL object for metrics scraping");
         goto done;
      }

      if (SSL_set_fd(ssl, fd) != 1 || SSL_connect(ssl) != 1)
      {
         pgagroal_log_error("Unable to establish TLS connection to metrics endpoint");
         goto done;
      }
   }

   pgagroal_snprintf(request, sizeof(request),
                     "GET /metrics HTTP/1.1\r\n"
                     "Host: %s:%d\r\n"
                     "Connection: close\r\n"
                     "Accept: text/plain\r\n"
                     "\r\n",
                     host,
                     port);

   if (write_all(ssl, fd, request, strlen(request)))
   {
      pgagroal_log_error("Unable to send /metrics request");
      goto done;
   }

   if (read_all(ssl, fd, &response, &response_size))
   {
      pgagroal_log_error("Unable to read /metrics response");
      goto done;
   }

   if (decode_http_response_body(response, response_size, body))
   {
      pgagroal_log_error("Unable to parse HTTP response body from /metrics endpoint");
      goto done;
   }

   status = 0;

done:
   free(response);

   if (ssl != NULL)
   {
      pgagroal_close_ssl(ssl);
   }

   if (ssl_ctx != NULL)
   {
      SSL_CTX_free(ssl_ctx);
   }

   if (fd != -1)
   {
      pgagroal_disconnect(fd);
   }

   return status;
}

static void
prometheus_metric_destroy_cb(uintptr_t data)
{
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (m != NULL)
   {
      free(m->name);
      free(m->help);
      free(m->type);

      pgagroal_deque_destroy(m->definitions);
   }

   free(m);
}

static char*
deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   struct deque* d = NULL;

   d = (struct deque*)data;

   return pgagroal_deque_to_string(d, format, tag, indent);
}

static char*
prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (pgagroal_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgagroal_art_insert(a, (char*)"Name", (uintptr_t)m->name, ValueString);
      pgagroal_art_insert(a, (char*)"Help", (uintptr_t)m->help, ValueString);
      pgagroal_art_insert(a, (char*)"Type", (uintptr_t)m->type, ValueString);
      pgagroal_art_insert_with_config(a, (char*)"Definitions", (uintptr_t)m->definitions, &vc);

      s = pgagroal_art_to_string(a, format, tag, indent);
   }

   pgagroal_art_destroy(a);

   return s;

error:

   pgagroal_art_destroy(a);

   return "Error";
}

static int
metric_find_create(struct prometheus_bridge* bridge, char* name,
                   struct prometheus_metric** metric)
{
   struct prometheus_metric* m = NULL;
   struct value_config vc = {.destroy_data = &prometheus_metric_destroy_cb,
                             .to_string = &prometheus_metric_string_cb};

   *metric = NULL;

   m = (struct prometheus_metric*)pgagroal_art_search(bridge->metrics, (char*)name);

   if (m == NULL)
   {
      struct deque* defs = NULL;

      m = (struct prometheus_metric*)malloc(sizeof(struct prometheus_metric));
      if (m == NULL)
      {
         goto error;
      }

      memset(m, 0, sizeof(struct prometheus_metric));

      if (pgagroal_deque_create(true, &defs))
      {
         free(m);
         goto error;
      }

      m->name = strdup(name);
      if (m->name == NULL)
      {
         pgagroal_deque_destroy(defs);
         free(m);
         goto error;
      }

      m->definitions = defs;

      if (pgagroal_art_insert_with_config(bridge->metrics, (char*)name,
                                          (uintptr_t)m, &vc))
      {
         prometheus_metric_destroy_cb((uintptr_t)m);
         goto error;
      }
   }

   *metric = m;

   return 0;

error:

   return 1;
}

static int
metric_set_name(struct prometheus_metric* metric, char* name)
{
   if (metric == NULL || name == NULL)
   {
      return 1;
   }

   if (metric->name != NULL)
   {
      return 0;
   }

   metric->name = strdup(name);
   if (metric->name == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_help(struct prometheus_metric* metric, char* help)
{
   if (metric == NULL || help == NULL)
   {
      return 1;
   }

   if (metric->help != NULL)
   {
      free(metric->help);
      metric->help = NULL;
   }

   metric->help = strdup(help);

   if (metric->help == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_type(struct prometheus_metric* metric, char* type)
{
   if (metric == NULL || type == NULL)
   {
      return 1;
   }

   if (metric->type != NULL)
   {
      free(metric->type);
      metric->type = NULL;
   }

   metric->type = strdup(type);

   if (metric->type == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static bool
attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute)
{
   bool found = false;
   struct deque_iterator* attributes_iterator = NULL;

   if (!pgagroal_deque_empty(attributes))
   {
      if (pgagroal_deque_iterator_create(attributes, &attributes_iterator))
      {
         goto done;
      }

      while (!found && pgagroal_deque_iterator_next(attributes_iterator))
      {
         struct prometheus_attribute* a = (struct prometheus_attribute*)attributes_iterator->value->data;

         if (!strcmp(a->key, attribute->key) && !strcmp(a->value, attribute->value))
         {
            found = true;
         }
      }
   }

done:

   pgagroal_deque_iterator_destroy(attributes_iterator);

   return found;
}

static void
prometheus_attributes_destroy_cb(uintptr_t data)
{
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (m != NULL)
   {
      pgagroal_deque_destroy(m->attributes);
      pgagroal_deque_destroy(m->values);
   }

   free(m);
}

static char*
prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (pgagroal_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgagroal_art_insert_with_config(a, (char*)"Attributes", (uintptr_t)m->attributes, &vc);
      pgagroal_art_insert_with_config(a, (char*)"Values", (uintptr_t)m->values, &vc);

      s = pgagroal_art_to_string(a, format, tag, indent);
   }

   pgagroal_art_destroy(a);

   return s;

error:

   pgagroal_art_destroy(a);

   return "Error";
}

static int
attributes_find_create(struct deque* definitions, struct deque* input,
                       struct prometheus_attributes** attributes, bool* is_new)
{
   bool found = false;
   struct prometheus_attributes* m = NULL;
   struct deque_iterator* definition_iterator = NULL;
   struct deque_iterator* input_iterator = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attributes_destroy_cb,
                             .to_string = &prometheus_attributes_string_cb};

   *attributes = NULL;
   *is_new = false;

   if (!pgagroal_deque_empty(definitions))
   {
      if (pgagroal_deque_iterator_create(definitions, &definition_iterator))
      {
         goto error;
      }

      while (!found && pgagroal_deque_iterator_next(definition_iterator))
      {
         bool match = true;
         struct prometheus_attributes* a =
            (struct prometheus_attributes*)definition_iterator->value->data;

         if (pgagroal_deque_iterator_create(input, &input_iterator))
         {
            goto error;
         }

         while (match && pgagroal_deque_iterator_next(input_iterator))
         {
            struct prometheus_attribute* i = (struct prometheus_attribute*)input_iterator->value->data;

            if (!attributes_contains(a->attributes, i))
            {
               match = false;
            }
         }

         if (match)
         {
            *attributes = a;
            found = true;
         }

         pgagroal_deque_iterator_destroy(input_iterator);
         input_iterator = NULL;
      }
   }

   if (!found)
   {
      m = (struct prometheus_attributes*)malloc(sizeof(struct prometheus_attributes));
      if (m == NULL)
      {
         goto error;
      }

      memset(m, 0, sizeof(struct prometheus_attributes));

      if (pgagroal_deque_create(false, &m->values))
      {
         free(m);
         goto error;
      }

      m->attributes = input;

      if (pgagroal_deque_add_with_config(definitions, NULL, (uintptr_t)m, &vc))
      {
         prometheus_attributes_destroy_cb((uintptr_t)m);
         goto error;
      }

      *attributes = m;
      *is_new = true;
   }

   pgagroal_deque_iterator_destroy(definition_iterator);

   return 0;

error:

   pgagroal_deque_iterator_destroy(input_iterator);
   pgagroal_deque_iterator_destroy(definition_iterator);

   return 1;
}

static void
prometheus_value_destroy_cb(uintptr_t data)
{
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (m != NULL)
   {
      free(m->value);
   }

   free(m);
}

static char*
prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (pgagroal_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgagroal_art_insert(a, (char*)"Timestamp", (uintptr_t)m->timestamp, ValueInt64);
      pgagroal_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgagroal_art_to_string(a, format, tag, indent);
   }

   pgagroal_art_destroy(a);

   return s;

error:

   pgagroal_art_destroy(a);

   return "Error";
}

static int
add_value(struct deque* values, time_t timestamp, char* value)
{
   struct value_config vc = {.destroy_data = &prometheus_value_destroy_cb,
                             .to_string = &prometheus_value_string_cb};
   struct prometheus_value* val = NULL;

   val = (struct prometheus_value*)malloc(sizeof(struct prometheus_value));
   if (val == NULL)
   {
      goto error;
   }

   memset(val, 0, sizeof(struct prometheus_value));
   val->timestamp = timestamp;
   val->value = strdup(value);

   if (val->value == NULL)
   {
      goto error;
   }

   if (pgagroal_deque_size(values) >= 100)
   {
      struct prometheus_value* v = NULL;

      v = (struct prometheus_value*)pgagroal_deque_poll(values, NULL);
      prometheus_value_destroy_cb((uintptr_t)v);
   }

   if (pgagroal_deque_add_with_config(values, NULL, (uintptr_t)val, &vc))
   {
      goto error;
   }

   return 0;

error:

   if (val != NULL)
   {
      free(val->value);
      free(val);
   }

   return 1;
}

static void
prometheus_attribute_destroy_cb(uintptr_t data)
{
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (m != NULL)
   {
      free(m->key);
      free(m->value);
   }

   free(m);
}

static char*
prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (pgagroal_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgagroal_art_insert(a, (char*)"Key", (uintptr_t)m->key, ValueString);
      pgagroal_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgagroal_art_to_string(a, format, tag, indent);
   }

   pgagroal_art_destroy(a);

   return s;

error:

   pgagroal_art_destroy(a);

   return "Error";
}

static int
add_attribute(struct deque* attributes, char* key, char* value)
{
   struct prometheus_attribute* attr = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attribute_destroy_cb,
                             .to_string = &prometheus_attribute_string_cb};

   attr = (struct prometheus_attribute*)malloc(sizeof(struct prometheus_attribute));
   if (attr == NULL)
   {
      goto error;
   }
   memset(attr, 0, sizeof(struct prometheus_attribute));

   attr->key = strdup(key);
   if (attr->key == NULL)
   {
      goto error;
   }

   attr->value = strdup(value);
   if (attr->value == NULL)
   {
      goto error;
   }

   if (pgagroal_deque_add_with_config(attributes, NULL, (uintptr_t)attr, &vc))
   {
      goto error;
   }

   return 0;

error:

   if (attr != NULL)
   {
      free(attr->key);
      free(attr->value);
      free(attr);
   }

   return 1;
}

static int
add_line(struct main_configuration* config, struct prometheus_metric* metric, char* line, time_t timestamp)
{
   char* endpoint_attr = NULL;
   bool is_new = false;
   char* line_value = NULL;
   struct deque* line_attrs = NULL;
   struct prometheus_attributes* attributes = NULL;
   char* p = NULL;
   char* labels_end = NULL;
   char* value_start = NULL;
   char* value_end = NULL;
   char* host = NULL;

   if (line == NULL || metric == NULL || config == NULL)
   {
      goto error;
   }

   if (pgagroal_deque_create(false, &line_attrs))
   {
      goto error;
   }

   host = config->common.host;

   endpoint_attr = pgagroal_append(endpoint_attr, host);
   endpoint_attr = pgagroal_append_char(endpoint_attr, ':');
   endpoint_attr = pgagroal_append_int(endpoint_attr, config->common.metrics);

   if (add_attribute(line_attrs, (char*)"endpoint", endpoint_attr))
   {
      goto error;
   }

   p = line;

   if (strncmp(p, metric->name, strlen(metric->name)))
   {
      goto error;
   }

   p += strlen(metric->name);

   if (*p == '{')
   {
      bool in_quotes = false;
      bool escaped = false;

      p++;

      labels_end = p;
      while (*labels_end != '\0')
      {
         if (!escaped && *labels_end == '"')
         {
            in_quotes = !in_quotes;
         }
         else if (!in_quotes && *labels_end == '}')
         {
            break;
         }

         if (*labels_end == '\\' && !escaped)
         {
            escaped = true;
         }
         else
         {
            escaped = false;
         }

         labels_end++;
      }

      if (*labels_end != '}')
      {
         goto error;
      }

      while (p < labels_end)
      {
         char key[PROMETHEUS_LABEL_LENGTH] = {0};
         char value[PROMETHEUS_LABEL_LENGTH] = {0};
         size_t key_len = 0;
         size_t value_len = 0;

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end)
         {
            break;
         }

         while (p < labels_end && *p != '=' && !isspace((unsigned char)*p))
         {
            if (key_len + 1 >= sizeof(key))
            {
               goto error;
            }

            key[key_len++] = *p;
            p++;
         }

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end || *p != '=')
         {
            goto error;
         }
         p++;

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end || *p != '"')
         {
            goto error;
         }
         p++;

         while (p < labels_end)
         {
            if (*p == '"')
            {
               p++;
               break;
            }

            if (*p == '\\' && (p + 1) < labels_end)
            {
               p++;

               if (value_len + 1 >= sizeof(value))
               {
                  goto error;
               }

               switch (*p)
               {
                  case 'n':
                     value[value_len++] = '\n';
                     break;
                  case 't':
                     value[value_len++] = '\t';
                     break;
                  case 'r':
                     value[value_len++] = '\r';
                     break;
                  default:
                     value[value_len++] = *p;
                     break;
               }

               p++;
               continue;
            }

            if (value_len + 1 >= sizeof(value))
            {
               goto error;
            }

            value[value_len++] = *p;
            p++;
         }

         if (key_len == 0)
         {
            goto error;
         }

         if (add_attribute(line_attrs, key, value))
         {
            goto error;
         }

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p < labels_end)
         {
            if (*p != ',')
            {
               goto error;
            }
            p++;
         }
      }

      value_start = labels_end + 1;
   }
   else
   {
      value_start = p;
   }

   while (*value_start != '\0' && isspace((unsigned char)*value_start))
   {
      value_start++;
   }

   if (*value_start == '\0')
   {
      goto error;
   }

   value_end = value_start;
   while (*value_end != '\0' && !isspace((unsigned char)*value_end))
   {
      value_end++;
   }

   if (value_end == value_start)
   {
      goto error;
   }

   line_value = strndup(value_start, (size_t)(value_end - value_start));
   if (line_value == NULL)
   {
      goto error;
   }

   if (attributes_find_create(metric->definitions, line_attrs, &attributes, &is_new))
   {
      goto error;
   }

   if (add_value(attributes->values, timestamp, line_value))
   {
      goto error;
   }

   if (!is_new)
   {
      pgagroal_deque_destroy(line_attrs);
   }

   free(endpoint_attr);
   free(line_value);

   return 0;

error:

   pgagroal_deque_destroy(line_attrs);

   free(endpoint_attr);
   free(line_value);

   return 1;
}

static int
parse_metric_name_from_line(char* line, char* metric_name, size_t size)
{
   size_t idx = 0;
   char* p = line;

   if (line == NULL || metric_name == NULL || size == 0)
   {
      return 1;
   }

   while (*p != '\0' && isspace((unsigned char)*p))
   {
      p++;
   }

   if (*p == '\0')
   {
      return 1;
   }

   while (*p != '\0' && *p != '{' && !isspace((unsigned char)*p))
   {
      if (idx + 1 >= size)
      {
         return 1;
      }
      metric_name[idx++] = *p;
      p++;
   }

   metric_name[idx] = '\0';

   return idx == 0 ? 1 : 0;
}

static int
parse_body_to_bridge(time_t timestamp, char* body, struct prometheus_bridge* bridge)
{
   char* line = NULL;
   char* saveptr = NULL;
   char name[MISC_LENGTH] = {0};
   char help[MAX_PATH] = {0};
   char type[MISC_LENGTH] = {0};
   char sample_name[MISC_LENGTH] = {0};
   struct prometheus_metric* metric = NULL;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;
   if (config == NULL)
   {
      goto error;
   }

   line = strtok_r(body, "\n", &saveptr);

   while (line != NULL)
   {
      if (line[0] == '\0' || !strcmp(line, "\r"))
      {
         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      if (line[0] == '#')
      {
         if (!strncmp(&line[1], "HELP", 4))
         {
            memset(name, 0, sizeof(name));
            memset(help, 0, sizeof(help));

            if (sscanf(line + 6, "%127s %1023[^\n]", name, help) == 2)
            {
               if (metric_find_create(bridge, name, &metric))
               {
                  goto error;
               }

               if (metric_set_name(metric, name) || metric_set_help(metric, help))
               {
                  goto error;
               }
            }
         }
         else if (!strncmp(&line[1], "TYPE", 4))
         {
            memset(name, 0, sizeof(name));
            memset(type, 0, sizeof(type));

            if (sscanf(line + 6, "%127s %127[^\n]", name, type) == 2)
            {
               if (metric_find_create(bridge, name, &metric))
               {
                  goto error;
               }

               if (metric_set_type(metric, type))
               {
                  goto error;
               }
            }
         }

         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      memset(sample_name, 0, sizeof(sample_name));
      if (parse_metric_name_from_line(line, sample_name, sizeof(sample_name)))
      {
         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      if (metric_find_create(bridge, sample_name, &metric))
      {
         goto error;
      }

      if (add_line(config, metric, line, timestamp))
      {
         goto error;
      }

      line = strtok_r(NULL, "\n", &saveptr);
   }

   return 0;

error:
   pgagroal_art_destroy(bridge->metrics);
   bridge->metrics = NULL;

   return 1;
}