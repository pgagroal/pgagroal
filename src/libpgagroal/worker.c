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
#include <memory.h>
#include <message.h>
#include <network.h>
#include <pipeline.h>
#include <pool.h>
#include <prometheus.h>
#include <security.h>
#include <tracker.h>
#include <worker.h>
#include <utils.h>

/* system */
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <openssl/ssl.h>

volatile int exit_code = WORKER_FAILURE;

static void signal_callback(void);

void
pgagroal_worker(int client_fd, char* address, char** argv)
{
   struct event_loop* loop = NULL;
   struct signal_info signal_watcher;
   struct worker_io client_io;
   struct worker_io server_io;
   time_t start_time;
   bool started = false;
   int auth_status;
   struct main_configuration* config;
   struct pipeline p;
   bool tx_pool = false;
   int32_t slot = -1;
   int transfer_fd = -1;
   SSL* client_ssl = NULL;
   SSL* server_ssl = NULL;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   memset(&client_io, 0, sizeof(struct worker_io));
   memset(&server_io, 0, sizeof(struct worker_io));

   client_io.slot = -1;
   server_io.slot = -1;

   start_time = time(NULL);

   pgagroal_tracking_event_basic(TRACKER_CLIENT_START, NULL, NULL);
   pgagroal_tracking_event_socket(TRACKER_SOCKET_ASSOCIATE_CLIENT, client_fd);
   pgagroal_set_proc_title(1, argv, "authenticating", NULL);

   pgagroal_prometheus_client_wait_add();
   /* Authentication */
   auth_status = pgagroal_authenticate(client_fd, address, &slot, &client_ssl, &server_ssl);
   if (auth_status == AUTH_SUCCESS)
   {
      pgagroal_log_debug("pgagroal_worker: Slot %d (%d -> %d)", slot, client_fd, config->connections[slot].fd);

      pgagroal_tracking_event_socket(TRACKER_SOCKET_ASSOCIATE_SERVER, config->connections[slot].fd);

      if (config->common.log_connections)
      {
         pgagroal_log_info("connect: user=%s database=%s address=%s", config->connections[slot].username,
                           config->connections[slot].database, address);
      }

      pgagroal_prometheus_client_wait_sub();
      pgagroal_prometheus_client_active_add();

      pgagroal_pool_status();

      // do we have to update the process title?
      switch (config->update_process_title)
      {
         case UPDATE_PROCESS_TITLE_MINIMAL:
         case UPDATE_PROCESS_TITLE_STRICT:
            // pgagroal_set_proc_title will check the policy
            pgagroal_set_proc_title(1, argv, config->connections[slot].username, config->connections[slot].database);
            break;
         case UPDATE_PROCESS_TITLE_VERBOSE:
            pgagroal_set_connection_proc_title(1, argv, &config->connections[slot]);
            break;
      }

      if (config->pipeline == PIPELINE_PERFORMANCE)
      {
         p = performance_pipeline();
      }
      else if (config->pipeline == PIPELINE_SESSION)
      {
         p = session_pipeline();
      }
      else if (config->pipeline == PIPELINE_TRANSACTION)
      {
         p = transaction_pipeline();
         tx_pool = true;
      }
      else
      {
         pgagroal_log_error("pgagroal_worker: Unknown pipeline %d", config->pipeline);
         p = session_pipeline();
      }

      /* client io_watcher receives from client and sends to server */
      pgagroal_event_worker_init(&client_io.io, client_fd, config->connections[slot].fd, p.client);
      client_io.client_fd = client_fd;
      client_io.server_fd = config->connections[slot].fd;
      client_io.slot = slot;
      client_io.client_ssl = client_ssl;
      client_io.server_ssl = server_ssl;
      client_io.io.ssl = (client_ssl != NULL);

      if (config->pipeline != PIPELINE_TRANSACTION)
      {
         /* server io_watcher receives from server and sends to client */
         pgagroal_event_worker_init(&server_io.io, config->connections[slot].fd, client_fd, p.server);
         server_io.client_fd = client_fd;
         server_io.server_fd = config->connections[slot].fd;
         server_io.slot = slot;
         server_io.client_ssl = client_ssl;
         server_io.server_ssl = server_ssl;
         server_io.io.ssl = (server_ssl != NULL);
      }

      loop = pgagroal_event_loop_init();
      if (!loop)
      {
         pgagroal_log_fatal("pgagroal_worker: Failed to create loop");
         exit(1);
      }

      pgagroal_signal_init(&signal_watcher.sig_w, signal_callback, SIGQUIT);
      signal_watcher.slot = slot;
      pgagroal_signal_start(&signal_watcher.sig_w);

      p.start(loop, &client_io);
      started = true;

      pgagroal_io_start(&client_io.io);
      if (config->pipeline != PIPELINE_TRANSACTION)
      {
         pgagroal_io_start(&server_io.io);
      }

      pgagroal_event_loop_run();

      if (config->pipeline == PIPELINE_TRANSACTION)
      {
         /* The slot may have been updated */
         slot = client_io.slot;
      }

      pgagroal_prometheus_client_active_sub();
   }
   else
   {
      if (config->common.log_connections)
      {
         pgagroal_log_info("connect: address=%s", address);
      }
      pgagroal_prometheus_client_wait_sub();
   }

   if (config->common.log_disconnections)
   {
      if (auth_status == AUTH_SUCCESS)
      {
         pgagroal_log_info("disconnect: user=%s database=%s address=%s", config->connections[slot].username,
                           config->connections[slot].database, address);
      }
      else
      {
         pgagroal_log_info("disconnect: address=%s", address);
      }
   }

   /* Return to pool */
   if (slot != -1)
   {
      if (started)
      {
         p.stop(loop, &client_io);
         pgagroal_prometheus_session_time(difftime(time(NULL), start_time));
         pgagroal_event_loop_destroy();
      }

      if ((auth_status == AUTH_SUCCESS || auth_status == AUTH_BAD_PASSWORD) &&
          (exit_code == WORKER_SUCCESS || exit_code == WORKER_CLIENT_FAILURE ||
           (exit_code == WORKER_FAILURE && config->connections[slot].has_security != SECURITY_INVALID)))
      {
         if (config->pipeline != PIPELINE_TRANSACTION)
         {
            pgagroal_tracking_event_socket(TRACKER_SOCKET_DISASSOCIATE_SERVER, config->connections[slot].fd);
            pgagroal_tracking_event_slot(TRACKER_WORKER_RETURN1, slot);
            pgagroal_return_connection(slot, server_ssl, tx_pool);
         }
      }
      else if (exit_code == WORKER_SERVER_FAILURE || exit_code == WORKER_SERVER_FATAL || exit_code == WORKER_SHUTDOWN || exit_code == WORKER_FAILOVER ||
               (exit_code == WORKER_FAILURE && config->connections[slot].has_security == SECURITY_INVALID))
      {
         pgagroal_tracking_event_socket(TRACKER_SOCKET_DISASSOCIATE_SERVER, config->connections[slot].fd);
         pgagroal_tracking_event_slot(TRACKER_WORKER_KILL1, slot);
         pgagroal_kill_connection(slot, server_ssl);
      }
      else
      {
         if (pgagroal_socket_isvalid(config->connections[slot].fd) &&
             pgagroal_connection_isvalid(config->connections[slot].fd) &&
             config->connections[slot].has_security != SECURITY_INVALID)
         {
            pgagroal_tracking_event_socket(TRACKER_SOCKET_DISASSOCIATE_SERVER, config->connections[slot].fd);
            pgagroal_tracking_event_slot(TRACKER_WORKER_RETURN2, slot);
            pgagroal_return_connection(slot, server_ssl, tx_pool);
         }
         else
         {
            pgagroal_tracking_event_socket(TRACKER_SOCKET_DISASSOCIATE_SERVER, config->connections[slot].fd);
            pgagroal_tracking_event_slot(TRACKER_WORKER_KILL2, slot);
            pgagroal_kill_connection(slot, server_ssl);
         }
      }
   }

   if (pgagroal_connection_get(&transfer_fd))
   {
      pgagroal_log_error("pgagroal_workers: Unable to get a transfer connection");
   }
   else
   {
      if (pgagroal_connection_id_write(transfer_fd, CONNECTION_CLIENT_DONE))
      {
         pgagroal_log_error("pgagroal_workers: Unable to write to a transfer connection");
      }

      if (pgagroal_connection_pid_write(transfer_fd, getpid()))
      {
         pgagroal_log_error("pgagroal_workers: Unable to write to a transfer connection");
      }

      pgagroal_disconnect(transfer_fd);
   }

   if (client_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(client_ssl);
      res = SSL_shutdown(client_ssl);
      if (res == 0)
      {
         SSL_shutdown(client_ssl);
      }
      SSL_free(client_ssl);
      SSL_CTX_free(ctx);
   }

   pgagroal_log_debug("client disconnect: %d", client_fd);
   pgagroal_tracking_event_socket(TRACKER_SOCKET_DISASSOCIATE_CLIENT, client_fd);
   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_client_sockets_sub();
   pgagroal_prometheus_query_count_specified_reset(slot);

   pgagroal_pool_status();
   pgagroal_log_debug("After client: PID %d Slot %d (%d)", getpid(), slot, exit_code);

   /* pgagroal_event_loop_destroy(); */
   free(address);

   pgagroal_tracking_event_basic(TRACKER_CLIENT_STOP, NULL, NULL);

   if (client_io.io.msg)
   {
      free(client_io.io.msg->data);
      free(client_io.io.msg);
      client_io.io.msg = NULL;
   }
   if (server_io.io.msg)
   {
      free(server_io.io.msg->data);
      free(server_io.io.msg);
      server_io.io.msg = NULL;
   }

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(exit_code);
}

static void
signal_callback(void)
{
   exit_code = WORKER_SHUTDOWN;
   pgagroal_event_loop_break();
}
