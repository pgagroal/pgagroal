/*
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Red Hat licenses this file to you under the Apache License, version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * Bootstrapped on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the specific
 * language governing permissions and limitations under the License.
 */

#include <openssl/crypto.h>
#include <pgagroal.h>
#include <http_server.h>
#include <logging.h>
#include <message.h>
#include <shmem.h>
#include <utils.h>

#include <errno.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>

static void
fill_date_rfc1123(char* buf, size_t len)
{
   time_t rawtime;
   struct tm* timeinfo;

   time(&rawtime);
   timeinfo = gmtime(&rawtime);
   strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
}

int
pgagroal_http_server_ssl_accept(SSL* ssl, int fd)
{
   char buffer[PGAGROAL_HTTP_TLS_PROBE_SIZE] = {0};
   ssize_t peek_bytes;

   if (ssl == NULL)
   {
      return MESSAGE_STATUS_OK;
   }

   peek_bytes = recv(fd, buffer, sizeof(buffer), MSG_PEEK);
   if (peek_bytes <= 0)
   {
      if (peek_bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      {
         pgagroal_log_debug("http_server: peek timed out on fd %d", fd);
      }
      else
      {
         pgagroal_log_debug("http_server: peek failed or socket closed for fd %d", fd);
      }
      return MESSAGE_STATUS_ERROR;
   }

   if ((unsigned char)buffer[0] == PGAGROAL_HTTP_TLS_HANDSHAKE || (unsigned char)buffer[0] == PGAGROAL_HTTP_TLS_SSL2)
   {
      if (SSL_accept(ssl) <= 0)
      {
         pgagroal_log_error("http_server: SSL_accept failed for fd %d", fd);
         return MESSAGE_STATUS_ERROR;
      }

      return MESSAGE_STATUS_OK;
   }

   return MESSAGE_STATUS_ZERO;
}

int
pgagroal_http_server_parse(SSL* ssl, int fd, struct http_server_request** req)
{
   struct message* msg = NULL;
   struct configuration* config;
   struct http_server_request* r = NULL;
   int status;
   char* path_start;
   char* path_end;
   size_t path_len;

   if (req == NULL || shmem == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   config = (struct configuration*)shmem;
   *req = NULL;

   status = pgagroal_read_timeout_message(ssl, fd, pgagroal_time_convert(config->authentication_timeout, FORMAT_TIME_S), &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      return status;
   }

   if (msg == NULL || msg->data == NULL || msg->length < 4 || strncmp(msg->data, "GET ", 4) != 0)
   {
      pgagroal_log_debug("http_server: not a GET request or invalid HTTP payload");

      pgagroal_http_respond_400(ssl, fd);

      pgagroal_free_message(msg);
      return MESSAGE_STATUS_ERROR;
   }

   path_start = msg->data + 4;
   path_end = memchr(path_start, ' ', (size_t)msg->length - 4);
   if (path_end == NULL)
   {
      pgagroal_log_debug("http_server: malformed request line");
      pgagroal_free_message(msg);
      return MESSAGE_STATUS_ERROR;
   }

   path_len = (size_t)(path_end - path_start);
   r = (struct http_server_request*)malloc(sizeof(struct http_server_request));
   if (r == NULL)
   {
      pgagroal_log_fatal("http_server: couldn't allocate memory for request");
      pgagroal_free_message(msg);
      return MESSAGE_STATUS_ERROR;
   }

   memset(r->path, 0, sizeof(r->path));

   if (path_len >= sizeof(r->path))
   {
      path_len = sizeof(r->path) - 1;
   }

   memcpy(r->path, path_start, path_len);
   r->path[path_len] = '\0';

   pgagroal_free_message(msg);

   *req = r;

   return MESSAGE_STATUS_OK;
}

int
pgagroal_http_server_dispatch(SSL* ssl, int fd, struct http_server_request* req, struct http_route* routes, int n_routes)
{
   if (req == NULL)
   {
      return pgagroal_http_respond_400(ssl, fd);
   }

   for (int i = 0; i < n_routes; i++)
   {
      if (strcmp(req->path, routes[i].path) == 0)
      {
         return routes[i].handler(ssl, fd);
      }
   }

   pgagroal_log_debug("http_server: no route for path '%s'", req->path);

   return pgagroal_http_respond_404(ssl, fd);
}

void
pgagroal_http_request_free(struct http_server_request* req)
{
   if (req == NULL)
   {
      return;
   }

   free(req);
}

int
pgagroal_http_respond_200(SSL* ssl, int fd, const char* content_type, const char* payload)
{
   char header[1024];
   char time_buf[128];
   size_t payload_len = payload ? strlen(payload) : 0;
   struct message msg;

   fill_date_rfc1123(time_buf, sizeof(time_buf));

   snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            time_buf,
            content_type ? content_type : "text/plain; charset=utf-8",
            payload_len);

   memset(&msg, 0, sizeof(struct message));
   msg.data = header;
   msg.length = strlen(header);

   if (pgagroal_write_message(ssl, fd, &msg) != MESSAGE_STATUS_OK)
   {
      return MESSAGE_STATUS_ERROR;
   }

   if (payload_len > 0)
   {
      memset(&msg, 0, sizeof(struct message));
      msg.data = (void*)payload;
      msg.length = payload_len;

      if (pgagroal_write_message(ssl, fd, &msg) != MESSAGE_STATUS_OK)
      {
         return MESSAGE_STATUS_ERROR;
      }
   }

   return MESSAGE_STATUS_OK;
}

int
pgagroal_http_respond_redirect(SSL* ssl, int fd, const char* location)
{
   char header[1024];
   char time_buf[128];
   struct message msg;

   fill_date_rfc1123(time_buf, sizeof(time_buf));

   snprintf(header, sizeof(header),
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Date: %s\r\n"
            "Location: %s\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n",
            time_buf,
            location ? location : "/");

   memset(&msg, 0, sizeof(struct message));
   msg.data = header;
   msg.length = strlen(header);

   return pgagroal_write_message(ssl, fd, &msg);
}

int
pgagroal_http_respond_404(SSL* ssl, int fd)
{
   char* data = NULL;
   char time_buf[128] = {0};
   int status;
   struct message msg;

   fill_date_rfc1123(time_buf, sizeof(time_buf));

   data = pgagroal_append(data, "HTTP/1.1 404 Not Found\r\n");
   data = pgagroal_append(data, "Date: ");
   data = pgagroal_append(data, time_buf);
   data = pgagroal_append(data, "\r\n");
   data = pgagroal_append(data, "Content-Length: 0\r\n");
   data = pgagroal_append(data, "Connection: close\r\n\r\n");

   if (data == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   memset(&msg, 0, sizeof(struct message));
   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(ssl, fd, &msg);

   free(data);

   return status;
}

int
pgagroal_http_respond_400(SSL* ssl, int fd)
{
   char* data = NULL;
   char time_buf[128] = {0};
   int status;
   struct message msg;

   fill_date_rfc1123(time_buf, sizeof(time_buf));

   data = pgagroal_append(data, "HTTP/1.1 400 Bad Request\r\n");
   data = pgagroal_append(data, "Date: ");
   data = pgagroal_append(data, time_buf);
   data = pgagroal_append(data, "\r\n");
   data = pgagroal_append(data, "Content-Length: 0\r\n");
   data = pgagroal_append(data, "Connection: close\r\n\r\n");

   if (data == NULL)
   {
      return MESSAGE_STATUS_ERROR;
   }

   memset(&msg, 0, sizeof(struct message));
   msg.kind = 0;
   msg.length = strlen(data);
   msg.data = data;

   status = pgagroal_write_message(ssl, fd, &msg);

   free(data);

   return status;
}

int
pgagroal_http_respond_chunked_start(SSL* ssl, int fd, const char* content_type)
{
   char header[1024];
   char time_buf[128];
   struct message msg;

   fill_date_rfc1123(time_buf, sizeof(time_buf));

   snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Content-Type: %s\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: close\r\n"
            "\r\n",
            time_buf,
            content_type ? content_type : "text/plain; version=0.0.4; charset=utf-8");

   memset(&msg, 0, sizeof(struct message));
   msg.data = header;
   msg.length = strlen(header);

   return pgagroal_write_message(ssl, fd, &msg);
}

int
pgagroal_http_respond_chunked_write(SSL* ssl, int fd, const char* data)
{
   char chunk_hdr[32];
   size_t len = data ? strlen(data) : 0;
   struct message msg;

   if (len == 0)
   {
      return MESSAGE_STATUS_OK;
   }

   snprintf(chunk_hdr, sizeof(chunk_hdr), "%zX\r\n", len);

   memset(&msg, 0, sizeof(struct message));
   msg.data = chunk_hdr;
   msg.length = strlen(chunk_hdr);
   if (pgagroal_write_message(ssl, fd, &msg) != MESSAGE_STATUS_OK)
   {
      return MESSAGE_STATUS_ERROR;
   }

   msg.data = (void*)data;
   msg.length = len;
   if (pgagroal_write_message(ssl, fd, &msg) != MESSAGE_STATUS_OK)
   {
      return MESSAGE_STATUS_ERROR;
   }

   msg.data = "\r\n";
   msg.length = 2;
   return pgagroal_write_message(ssl, fd, &msg);
}

int
pgagroal_http_respond_chunked_end(SSL* ssl, int fd)
{
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   msg.data = "0\r\n\r\n";
   msg.length = 5;

   return pgagroal_write_message(ssl, fd, &msg);
}