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
#include <ev.h>
#include <logging.h>
#include <management.h>
#include <message.h>
#include <network.h>
#include <pipeline.h>
#include <prometheus.h>
#include <server.h>
#include <shmem.h>
#include <utils.h>
#include <worker.h>

/* system */
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

static int session_initialize(void*, void**, size_t*);
static void session_start(struct event_loop* loop, struct worker_io*);
static void session_client(struct io_watcher* watcher);
static void session_server(struct io_watcher* watcher);
static void session_stop(struct event_loop* loop, struct worker_io*);
static void session_destroy(void*, size_t);
static void session_periodic(void);

static bool in_tx;
static int next_client_message;
static int next_server_message;
static bool saw_x = false;

#define CLIENT_INIT   0
#define CLIENT_IDLE   1
#define CLIENT_ACTIVE 2
#define CLIENT_CHECK  3

struct client_session
{
   atomic_schar state; /**< The state */
   time_t timestamp;   /**< The last used timestamp */
};

static void client_active(int);
static void client_inactive(int);

struct pipeline
session_pipeline(void)
{
   struct pipeline pipeline;

   pipeline.initialize = &session_initialize;
   pipeline.start = &session_start;
   pipeline.client = &session_client;
   pipeline.server = &session_server;
   pipeline.stop = &session_stop;
   pipeline.destroy = &session_destroy;
   pipeline.periodic = &session_periodic;

   return pipeline;
}

static int
session_initialize(void* shmem, void** pipeline_shmem, size_t* pipeline_shmem_size)
{
   void* session_shmem = NULL;
   size_t session_shmem_size;
   struct client_session* client;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   *pipeline_shmem = NULL;
   *pipeline_shmem_size = 0;

   if (config->disconnect_client > 0)
   {
      session_shmem_size = config->max_connections * sizeof(struct client_session);
      if (pgagroal_create_shared_memory(session_shmem_size, config->common.hugepage, &session_shmem))
      {
         return 1;
      }
      memset(session_shmem, 0, session_shmem_size);

      for (int i = 0; i < config->max_connections; i++)
      {
         client = session_shmem + (i * sizeof(struct client_session));

         atomic_init(&client->state, CLIENT_INIT);
         client->timestamp = time(NULL);
      }

      *pipeline_shmem = session_shmem;
      *pipeline_shmem_size = session_shmem_size;
   }

   return 0;
}

static void
session_start(struct event_loop* loop __attribute__((unused)), struct worker_io* w)
{
   struct client_session* client;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   in_tx = false;
   next_client_message = 0;
   next_server_message = 0;

   for (int i = 0; i < config->max_connections; i++)
   {
      if (i != w->slot && !config->connections[i].new && config->connections[i].fd > 0)
      {
         pgagroal_disconnect(config->connections[i].fd);
      }
   }

   if (pipeline_shmem != NULL)
   {
      client = pipeline_shmem + (w->slot * sizeof(struct client_session));

      atomic_store(&client->state, CLIENT_IDLE);
      client->timestamp = time(NULL);
   }

   return;
}

static void
session_stop(struct event_loop* loop __attribute__((unused)), struct worker_io* w)
{
   struct client_session* client;

   if (pipeline_shmem != NULL)
   {
      client = pipeline_shmem + (w->slot * sizeof(struct client_session));

      atomic_store(&client->state, CLIENT_INIT);
      client->timestamp = time(NULL);
   }
}

static void
session_destroy(void* pipeline_shmem, size_t pipeline_shmem_size)
{
   if (pipeline_shmem != NULL)
   {
      pgagroal_destroy_shared_memory(pipeline_shmem, pipeline_shmem_size);
   }
}

static void
session_periodic(void)
{
   signed char state;
   signed char idle;
   bool do_kill;
   time_t now;
   int ret;
   signed char server;
   int socket;
   struct message* cancel_msg = NULL;
   struct client_session* client;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->disconnect_client > 0 && pipeline_shmem != NULL)
   {
      now = time(NULL);

      for (int i = 0; i < config->max_connections; i++)
      {
         client = pipeline_shmem + (i * sizeof(struct client_session));

         if (difftime(now, client->timestamp) > config->disconnect_client)
         {
            if (config->connections[i].pid != 0)
            {
               state = atomic_load(&client->state);
               do_kill = false;

               if (config->disconnect_client_force)
               {
                  do_kill = true;
               }
               else
               {
                  idle = CLIENT_IDLE;

                  if (atomic_compare_exchange_strong(&client->state, &idle, CLIENT_CHECK))
                  {
                     do_kill = true;
                  }
               }

               if (do_kill)
               {
                  pgagroal_create_cancel_request_message(config->connections[i].backend_pid, config->connections[i].backend_secret, &cancel_msg);

                  server = config->connections[i].server;

                  if (config->servers[server].host[0] == '/')
                  {
                     char pgsql[MISC_LENGTH];

                     memset(&pgsql, 0, sizeof(pgsql));
                     snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
                     ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], &socket);
                  }
                  else
                  {
                     ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, &socket, config->keep_alive, config->nodelay);
                  }

                  if (ret == 0)
                  {
                     pgagroal_log_debug("Cancel request for %s/%s using slot %d (pid %d secret %d)",
                                        config->connections[i].database, config->connections[i].username,
                                        i, config->connections[i].backend_pid, config->connections[i].backend_secret);

                     pgagroal_write_message(NULL, socket, cancel_msg);
                  }

                  pgagroal_disconnect(socket);

                  atomic_store(&config->states[i], STATE_GRACEFULLY);

                  pgagroal_log_info("Disconnect client %s/%s using slot %d (pid %d socket %d)",
                                    config->connections[i].database, config->connections[i].username,
                                    i, config->connections[i].pid, config->connections[i].fd);
                  kill(config->connections[i].pid, SIGQUIT);

                  pgagroal_free_message(cancel_msg);
                  cancel_msg = NULL;
               }
               else
               {
                  atomic_store(&client->state, state);
               }
            }
         }
      }
   }

   exit(0);
}

static void
session_client(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = NULL;

   wi = (struct worker_io*)watcher;
   config = (struct main_configuration*)shmem;

   client_active(wi->slot);

   status = pgagroal_recv_message(watcher, &msg);

   if (likely(status == MESSAGE_STATUS_OK))
   {
      pgagroal_prometheus_network_sent_add(msg->length);

      if (likely(msg->kind != 'X'))
      {
         int offset = 0;

         while (offset < msg->length)
         {
            if (next_client_message == 0)
            {
               char kind = pgagroal_read_byte(msg->data + offset);
               int length = pgagroal_read_int32(msg->data + offset + 1);

               /* The Q and E message tell us the execute of the simple query and the prepared statement */
               if (kind == 'Q' || kind == 'E')
               {
                  pgagroal_prometheus_query_count_add();
                  pgagroal_prometheus_query_count_specified_add(wi->slot);
               }

               /* Calculate the offset to the next message */
               if (offset + length + 1 <= msg->length)
               {
                  next_client_message = 0;
                  offset += length + 1;
               }
               else
               {
                  next_client_message = length + 1 - (msg->length - offset);
                  offset = msg->length;
               }
            }
            else
            {
               offset = MIN(next_client_message, msg->length);
               next_client_message -= offset;
            }
         }

         status = pgagroal_send_message(watcher, msg);

         if (unlikely(status == MESSAGE_STATUS_ERROR))
         {
            if (config->failover)
            {
               pgagroal_server_failover(wi->slot);
               pgagroal_write_client_failover(wi->client_ssl, wi->client_fd);
               pgagroal_prometheus_failed_servers();

               goto failover;
            }
            else
            {
               goto server_error;
            }
         }
      }
      else if (msg->kind == 'X')
      {
         saw_x = true;
         pgagroal_event_loop_break();
      }
   }
   else if (status == MESSAGE_STATUS_ZERO)
   {
      goto client_done;
   }
   else
   {
      goto client_error;
   }

   client_inactive(wi->slot);

   return;

client_done:
   pgagroal_log_debug("[C] Client done (slot %d database %s user %s): %s (socket %d status %d)",
                      wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                      strerror(errno), wi->client_fd, status);
   errno = 0;

   client_inactive(wi->slot);

   if (saw_x)
   {
      exit_code = WORKER_SUCCESS;
   }
   else
   {
      exit_code = WORKER_CLIENT_FAILURE;
   }

   pgagroal_event_loop_break();
   return;

client_error:
   pgagroal_log_warn("[C] Client error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->client_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   client_inactive(wi->slot);

   exit_code = WORKER_CLIENT_FAILURE;

   pgagroal_event_loop_break();
   return;

server_error:
   pgagroal_log_warn("[C] Server error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->server_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   client_inactive(wi->slot);

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;

failover:

   client_inactive(wi->slot);

   exit_code = WORKER_FAILOVER;

   pgagroal_event_loop_break();
   return;
}

static void
session_server(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   bool fatal = false;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = (struct main_configuration*)shmem;

   wi = (struct worker_io*)watcher;

   client_active(wi->slot);

   status = pgagroal_recv_message(watcher, &msg);

   if (likely(status == MESSAGE_STATUS_OK))
   {
      pgagroal_prometheus_network_received_add(msg->length);

      int offset = 0;

      while (offset < msg->length)
      {
         if (next_server_message == 0)
         {
            char kind = pgagroal_read_byte(msg->data + offset);
            int length = pgagroal_read_int32(msg->data + offset + 1);

            /* The Z message tell us the transaction state */
            if (kind == 'Z')
            {
               char tx_state = pgagroal_read_byte(msg->data + offset + 5);

               if (tx_state != 'I' && !in_tx)
               {
                  pgagroal_prometheus_tx_count_add();
               }

               in_tx = tx_state != 'I';
            }

            /* Calculate the offset to the next message */
            if (offset + length + 1 <= msg->length)
            {
               next_server_message = 0;
               offset += length + 1;
            }
            else
            {
               next_server_message = length + 1 - (msg->length - offset);
               offset = msg->length;
            }
         }
         else
         {
            offset = MIN(next_server_message, msg->length);
            next_server_message -= offset;
         }
      }

      status = pgagroal_send_message(watcher, msg);

      if (unlikely(status != MESSAGE_STATUS_OK))
      {
         goto client_error;
      }

      if (unlikely(msg->kind == 'E'))
      {
         fatal = false;

         if (!strncmp(msg->data + 6, "FATAL", 5) || !strncmp(msg->data + 6, "PANIC", 5))
         {
            fatal = true;
         }

         if (fatal)
         {
            exit_code = WORKER_SERVER_FATAL;
            pgagroal_event_loop_break();
         }
      }
   }
   else if (status == MESSAGE_STATUS_ZERO)
   {
      goto server_done;
   }
   else
   {
      goto server_error;
   }

   client_inactive(wi->slot);

   return;

client_error:
   pgagroal_log_warn("[S] Client error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->client_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   client_inactive(wi->slot);

   exit_code = WORKER_CLIENT_FAILURE;

   pgagroal_event_loop_break();
   return;

server_done:
   pgagroal_log_debug("[S] Server done (slot %d database %s user %s): %s (socket %d status %d)",
                      wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                      strerror(errno), wi->server_fd, status);
   errno = 0;

   client_inactive(wi->slot);

   pgagroal_event_loop_break();
   return;

server_error:
   pgagroal_log_warn("[S] Server error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->server_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   client_inactive(wi->slot);

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;
}

static void
client_active(int slot)
{
   struct client_session* client;

   if (pipeline_shmem != NULL)
   {
      client = pipeline_shmem + (slot * sizeof(struct client_session));
      atomic_store(&client->state, CLIENT_ACTIVE);
      client->timestamp = time(NULL);
   }
}

static void
client_inactive(int slot)
{
   struct client_session* client;

   if (pipeline_shmem != NULL)
   {
      client = pipeline_shmem + (slot * sizeof(struct client_session));
      atomic_store(&client->state, CLIENT_IDLE);
      client->timestamp = time(NULL);
   }
}
