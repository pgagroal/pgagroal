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
#include <worker.h>

/* system */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

static int performance_initialize(void*, void**, size_t*);
static void performance_start(struct event_loop* loop, struct worker_io*);
static void performance_client(struct io_watcher* watcher);
static void performance_server(struct io_watcher* watcher);
static void performance_stop(struct event_loop* loop, struct worker_io*);
static void performance_destroy(void*, size_t);
static void performance_periodic(void);

static bool saw_x = false;

struct pipeline
performance_pipeline(void)
{
   struct pipeline pipeline;

   pipeline.initialize = &performance_initialize;
   pipeline.start = &performance_start;
   pipeline.client = &performance_client;
   pipeline.server = &performance_server;
   pipeline.stop = &performance_stop;
   pipeline.destroy = &performance_destroy;
   pipeline.periodic = &performance_periodic;

   return pipeline;
}

static int
performance_initialize(void* shmem __attribute__((unused)), void** pipeline_shmem __attribute__((unused)), size_t* pipeline_shmem_size __attribute__((unused)))
{
   return 0;
}

static void
performance_start(struct event_loop* loop __attribute__((unused)), struct worker_io* w)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      if (i != w->slot && !config->connections[i].new && config->connections[i].fd > 0)
      {
         pgagroal_disconnect(config->connections[i].fd);
      }
   }

   return;
}

static void
performance_stop(struct event_loop* loop __attribute__((unused)), struct worker_io* w __attribute__((unused)))
{
}

static void
performance_destroy(void* pipeline_shmem __attribute__((unused)), size_t pipeline_shmem_size __attribute__((unused)))
{
}

static void
performance_periodic(void)
{
}

static void
performance_client(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = (struct main_configuration*)shmem;

   wi = (struct worker_io*)watcher;

   status = pgagroal_recv_message(watcher, &msg);
   PGAGROAL_LOG_POSTGRES(msg);

   if (likely(status == MESSAGE_STATUS_OK))
   {
      if (likely(msg->kind != 'X'))
      {
         status = pgagroal_send_message(watcher, msg);

         if (unlikely(status != MESSAGE_STATUS_OK))
         {
            goto server_error;
         }
      }
      else if (msg->kind == 'X')
      {
         saw_x = true;
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

   errno = 0;

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
}

static void
performance_server(struct io_watcher* watcher)
{
   int status = MESSAGE_STATUS_ERROR;
   bool fatal = false;
   struct worker_io* wi = NULL;
   struct message* msg = NULL;
   struct main_configuration* config = (struct main_configuration*)shmem;

   wi = (struct worker_io*)watcher;

   status = pgagroal_recv_message(watcher, &msg);

   if (likely(status == MESSAGE_STATUS_OK))
   {
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
            pgagroal_log_warn("[C] Server Fatal (slot %d database %s user %s): %s (socket %d status %d)",
                              wi->slot, config->connections[wi->slot].database, config->connections[wi->slot].username,
                              strerror(errno), wi->client_fd, status);
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
}
