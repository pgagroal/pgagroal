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
#include <connection.h>
#include <ev.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <pipeline.h>
#include <pool.h>
#include <prometheus.h>
#include <server.h>
#include <shmem.h>
#include <tracker.h>
#include <worker.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

static int transaction_initialize(void*, void**, size_t*);
static void transaction_start(struct event_loop* loop, struct worker_io*);
static void transaction_client(struct io_watcher* watcher);
static void transaction_server(struct io_watcher* watcher);
static void transaction_stop(struct event_loop* loop, struct worker_io*);
static void transaction_destroy(void*, size_t);
static void transaction_periodic(void);

static void start_mgt(struct event_loop* loop);
static void shutdown_mgt(struct event_loop* loop);
static void accept_cb(struct io_watcher* watcher);

static int slot;
static char username[MAX_USERNAME_LENGTH];
static char database[MAX_DATABASE_LENGTH];
static char appname[MAX_APPLICATION_NAME];
static bool in_tx;
static int next_client_message;
static int next_server_message;
static int unix_socket = -1;
static int deallocate;
static bool fatal;
static int fds[MAX_NUMBER_OF_CONNECTIONS];
static bool saw_x = false;
static struct io_watcher io_mgt;
static struct worker_io server_io;
static bool io_watcher_active = false;

struct pipeline
transaction_pipeline(void)
{
   struct pipeline pipeline;

   pipeline.initialize = &transaction_initialize;
   pipeline.start = &transaction_start;
   pipeline.client = &transaction_client;
   pipeline.server = &transaction_server;
   pipeline.stop = &transaction_stop;
   pipeline.destroy = &transaction_destroy;
   pipeline.periodic = &transaction_periodic;

   return pipeline;
}

static int
transaction_initialize(void* shmem __attribute__((unused)), void** pipeline_shmem __attribute__((unused)), size_t* pipeline_shmem_size __attribute__((unused)))
{
   return 0;
}

static void
transaction_start(struct event_loop* loop, struct worker_io* w)
{
   char p[MISC_LENGTH];
   bool is_new;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   slot = -1;
   memcpy(&username[0], config->connections[w->slot].username, MAX_USERNAME_LENGTH);
   memcpy(&database[0], config->connections[w->slot].database, MAX_DATABASE_LENGTH);
   memcpy(&appname[0], config->connections[w->slot].appname, MAX_APPLICATION_NAME);
   in_tx = false;
   next_client_message = 0;
   next_server_message = 0;
   deallocate = false;

   memset(&p, 0, sizeof(p));
   snprintf(&p[0], sizeof(p), ".s.pgagroal.%d", getpid());

   if (pgagroal_bind_unix_socket(config->unix_socket_dir, &p[0], &unix_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &p[0]);
      goto error;
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      fds[i] = config->connections[i].fd;
   }

   start_mgt(loop);

   pgagroal_tracking_event_slot(TRACKER_TX_RETURN_CONNECTION_START, w->slot);

   is_new = config->connections[w->slot].new;
   pgagroal_return_connection(w->slot, w->server_ssl, true);

   w->server_fd = -1;
   w->slot = -1;

   if (is_new)
   {
      /* Sleep for 5ms */
      SLEEP(5000000L)
   }

   return;

error:

   exit_code = WORKER_FAILURE;
   pgagroal_event_loop_break();
   return;
}

static void
transaction_stop(struct event_loop* loop, struct worker_io* w)
{
   if (slot != -1)
   {
      struct main_configuration* config = NULL;

      config = (struct main_configuration*)shmem;

      /* We are either in 'X' or the client terminated (consider cancel query) */
      if (in_tx)
      {
         /* ROLLBACK */
         pgagroal_write_rollback(w->server_ssl, config->connections[slot].fd);
      }

      if (io_watcher_active)
      {
         pgagroal_io_stop(&server_io.io);
         io_watcher_active = false;
      }
      pgagroal_tracking_event_slot(TRACKER_TX_RETURN_CONNECTION_STOP, w->slot);
      pgagroal_return_connection(slot, w->server_ssl, true);
      slot = -1;
   }

   shutdown_mgt(loop);
}

static void
transaction_destroy(void* pipeline_shmem __attribute__((unused)), size_t pipeline_shmem_size __attribute__((unused)))
{
}

static void
transaction_periodic(void)
{
}

static void
transaction_client(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   SSL* s_ssl = NULL;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = NULL;

   wi = (struct worker_io*)watcher;
   config = (struct main_configuration*)shmem;

   /* We can't use the information from wi except from client_fd/client_ssl */
   if (slot == -1)
   {
      pgagroal_tracking_event_basic(TRACKER_TX_GET_CONNECTION, &username[0], &database[0]);
      if (pgagroal_get_connection(&username[0], &database[0], true, true, &slot, &s_ssl))
      {
         pgagroal_write_pool_full(wi->client_ssl, wi->client_fd);
         goto get_error;
      }

      wi->server_fd = fds[slot];
      wi->server_ssl = s_ssl;
      wi->slot = slot;

      pgagroal_event_worker_init(&wi->io, wi->client_fd, wi->server_fd, transaction_client);

      memcpy(&config->connections[slot].appname[0], &appname[0], MAX_APPLICATION_NAME);

      pgagroal_event_worker_init(&server_io.io, config->connections[slot].fd,
                                 wi->client_fd, transaction_server);
      server_io.client_fd = wi->client_fd;
      server_io.server_fd = config->connections[slot].fd;
      server_io.slot = slot;
      server_io.client_ssl = wi->client_ssl;
      server_io.server_ssl = wi->server_ssl;

      fatal = false;

      pgagroal_io_start(&server_io.io);
      io_watcher_active = true;
   }

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

               if (config->track_prepared_statements)
               {
                  /* The P message tell us the prepared statement */
                  if (kind == 'P')
                  {
                     char* ps = pgagroal_read_string(msg->data + offset + 5);
                     if (strcmp(ps, ""))
                     {
                        deallocate = true;
                     }
                  }
               }

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
               pgagroal_server_failover(slot);
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

   return;

client_done:
   pgagroal_log_debug("[C] Client done (slot %d database %s user %s): %s (socket %d status %d)",
                      wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                      strerror(errno), wi->client_fd, status);
   errno = 0;

   if (saw_x)
   {
      exit_code = WORKER_SUCCESS;
   }
   else
   {
      exit_code = WORKER_SERVER_FAILURE;
   }

   pgagroal_event_loop_break();
   return;

client_error:
   pgagroal_log_warn("[C] Client error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->client_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   exit_code = WORKER_CLIENT_FAILURE;

   pgagroal_event_loop_break();
   return;

server_error:
   pgagroal_log_warn("[C] Server error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->server_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;

failover:

   exit_code = WORKER_FAILOVER;

   pgagroal_event_loop_break();
   return;

get_error:
   pgagroal_log_warn("Failure during obtaining connection");

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;
}

static void
transaction_server(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = NULL;

   wi = (struct worker_io*)watcher;
   config = (struct main_configuration*)shmem;

   if (!pgagroal_socket_isvalid(wi->client_fd))
   {
      goto client_error;
   }

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
         if (!strncmp(msg->data + 6, "FATAL", 5) || !strncmp(msg->data + 6, "PANIC", 5))
         {
            fatal = true;
         }
      }

      /* Check for ReadyForQuery message (Z) to detect transaction completion */
      if (msg->kind == 'Z' && !in_tx && slot != -1)
      {
         /* Transaction completed - stop I/O watcher immediately if still active */
         if (io_watcher_active)
         {
            pgagroal_io_stop(&server_io.io);
            io_watcher_active = false;
         }

         if (!fatal)
         {
            if (deallocate)
            {
               pgagroal_write_deallocate_all(wi->server_ssl, wi->server_fd);
               deallocate = false;
            }

            pgagroal_tracking_event_slot(TRACKER_TX_RETURN_CONNECTION, slot);
            if (pgagroal_return_connection(slot, wi->server_ssl, true))
            {
               goto return_error;
            }

            slot = -1;
         }
         else
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

   return;

client_error:
   pgagroal_log_warn("[S] Client error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->client_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   exit_code = WORKER_CLIENT_FAILURE;

   pgagroal_event_loop_break();
   return;

server_done:
   pgagroal_log_debug("[S] Server done (slot %d database %s user %s): %s (socket %d status %d)",
                      wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                      strerror(errno), wi->server_fd, status);
   errno = 0;

   pgagroal_event_loop_break();
   return;

server_error:
   pgagroal_log_warn("[S] Server error (slot %d database %s user %s): %s (socket %d status %d)",
                     wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                     strerror(errno), wi->server_fd, status);
   pgagroal_log_message(msg);
   errno = 0;

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;

return_error:
   pgagroal_log_warn("Failure during connection return");

   exit_code = WORKER_SERVER_FAILURE;

   pgagroal_event_loop_break();
   return;
}

static void
start_mgt(struct event_loop* loop __attribute__((unused)))
{
   memset(&io_mgt, 0, sizeof(struct io_watcher));
   pgagroal_event_accept_init(&io_mgt, unix_socket, accept_cb);
   pgagroal_io_start(&io_mgt);
}

static void
shutdown_mgt(struct event_loop* loop __attribute__((unused)))
{
   char p[MISC_LENGTH];
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   memset(&p, 0, sizeof(p));
   snprintf(&p[0], sizeof(p), ".s.pgagroal.%d", getpid());

   pgagroal_io_stop(&io_mgt);
   pgagroal_disconnect(unix_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, &p[0]);
   errno = 0;

   if (server_io.io.msg)
   {
      free(server_io.io.msg->data);
      free(server_io.io.msg);
      server_io.io.msg = NULL;
   }
}

static void
accept_cb(struct io_watcher* watcher)
{
   int client_fd = -1;
   int id = -1;
   int32_t slot = -1;
   int fd = -1;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   client_fd = watcher->fds.main.client_fd;
   if (client_fd == -1)
   {
      pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      errno = 0;
      return;
   }

   /* Process management request */
   if (pgagroal_connection_id_read(client_fd, &id))
   {
      pgagroal_log_error("pgagroal: Management client: ID: %d", id);
      goto done;
   }

   if (id == CONNECTION_CLIENT_FD)
   {
      if (pgagroal_connection_transfer_read(client_fd, &slot, &fd))
      {
         pgagroal_log_error("pgagroal: Management client_fd: ID: %d Slot %d FD %d", id, slot, fd);
         goto done;
      }

      fds[slot] = fd;
   }
   else if (id == CONNECTION_REMOVE_FD)
   {
      if (pgagroal_connection_transfer_read(client_fd, &slot, &fd))
      {
         pgagroal_log_error("pgagroal: Management remove_fd: ID: %d Slot %d FD %d", id, slot, fd);
         goto done;
      }

      if (fds[slot] == fd && !config->connections[slot].new && config->connections[slot].fd > 0)
      {
         pgagroal_disconnect(fd);
         fds[slot] = 0;
      }
   }
   else
   {
      pgagroal_log_debug("pgagroal: Unsupported management id: %d", id);
   }

done:

   pgagroal_disconnect(client_fd);
}
