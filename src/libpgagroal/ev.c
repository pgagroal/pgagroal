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
#include <ev.h>
#include <logging.h>
#include <message.h>
#include <memory.h>
#include <network.h>
#include <pgagroal.h>
#include <shmem.h>

/* system */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if HAVE_LINUX
#include <liburing.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#endif /* HAVE_LINUX */

static int (*loop_init)(void);
static int (*loop_start)(void);
static int (*loop_fork)(void);
static int (*loop_destroy)(void);

static int (*io_start)(struct io_watcher*);
static int (*io_stop)(struct io_watcher*);

static void signal_handler(int signum, siginfo_t* info, void* p);

static int (*periodic_init)(struct periodic_watcher*, int);
static int (*periodic_start)(struct periodic_watcher*);
static int (*periodic_stop)(struct periodic_watcher*);

#if HAVE_LINUX

static int ev_io_uring_init(void);
static int ev_io_uring_destroy(void);
static int ev_io_uring_loop(void);
static int ev_io_uring_fork(void);
static int ev_io_uring_handler(struct io_uring_cqe*);
#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
static int ev_io_uring_setup_buffers(void);
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

static int ev_io_uring_io_start(struct io_watcher*);
static int ev_io_uring_io_stop(struct io_watcher*);

static int ev_io_uring_periodic_init(struct periodic_watcher*, int);
static int ev_io_uring_periodic_start(struct periodic_watcher*);
static int ev_io_uring_periodic_stop(struct periodic_watcher*);

static int ev_epoll_init(void);
static int ev_epoll_destroy(void);
static int ev_epoll_loop(void);
static int ev_epoll_fork(void);
static int ev_epoll_handler(void*);

static int ev_epoll_io_start(struct io_watcher*);
static int ev_epoll_io_stop(struct io_watcher*);
static int ev_epoll_io_handler(struct io_watcher*);

static int ev_epoll_periodic_init(struct periodic_watcher*, int);
static int ev_epoll_periodic_start(struct periodic_watcher*);
static int ev_epoll_periodic_stop(struct periodic_watcher*);
static int ev_epoll_periodic_handler(struct periodic_watcher*);

#else

static int ev_kqueue_init(void);
static int ev_kqueue_destroy(void);
static int ev_kqueue_loop(void);
static int ev_kqueue_fork(void);
static int ev_kqueue_handler(struct kevent*);

static int ev_kqueue_io_start(struct io_watcher*);
static int ev_kqueue_io_stop(struct io_watcher*);
static int ev_kqueue_io_handler(struct kevent*);

static int ev_kqueue_periodic_init(struct periodic_watcher*, int);
static int ev_kqueue_periodic_start(struct periodic_watcher*);
static int ev_kqueue_periodic_stop(struct periodic_watcher*);
static int ev_kqueue_periodic_handler(struct kevent*);

static int ev_kqueue_signal_start(struct signal_watcher*);
static int ev_kqueue_signal_stop(struct signal_watcher*);
static int ev_kqueue_signal_handler(struct kevent*);

#endif /* HAVE_LINUX */

static void init_watcher_message(struct io_watcher* watcher);

/* context globals */

static struct event_loop* loop = NULL;
static _Atomic(struct signal_watcher*) signal_watchers[PGAGROAL_NSIG] = {0};

#if HAVE_LINUX

static struct io_uring_params params; /* io_uring argument params */
static int ring_size;                 /* io_uring sqe ring_size */

static int epoll_flags; /* Flags for epoll instance creation */

#else

static int kqueue_flags; /* Flags for kqueue instance creation */

#endif /* HAVE_LINUX */

static int execution_context = PGAGROAL_CONTEXT_MAIN;

void
pgagroal_event_set_context(int context)
{
   execution_context = context;
}

static int
setup_ops(void)
{
   int backend_type = PGAGROAL_EVENT_BACKEND_AUTO;

   // Determine backend type based on execution context
   if (execution_context == PGAGROAL_CONTEXT_VAULT)
   {
      struct vault_configuration* vault_config = (struct vault_configuration*)shmem;
      if (vault_config)
      {
         backend_type = vault_config->ev_backend;
      }
   }
   else
   {
      struct main_configuration* main_config = (struct main_configuration*)shmem;
      if (main_config)
      {
         backend_type = main_config->ev_backend;
      }
   }

   if (backend_type == PGAGROAL_EVENT_BACKEND_AUTO)
   {
      backend_type = DEFAULT_EVENT_BACKEND;
   }

#if HAVE_LINUX
   if (backend_type == PGAGROAL_EVENT_BACKEND_IO_URING)
   {
      loop_init = ev_io_uring_init;
      loop_fork = ev_io_uring_fork;
      loop_destroy = ev_io_uring_destroy;
      loop_start = ev_io_uring_loop;
      io_start = ev_io_uring_io_start;
      io_stop = ev_io_uring_io_stop;
      periodic_init = ev_io_uring_periodic_init;
      periodic_start = ev_io_uring_periodic_start;
      periodic_stop = ev_io_uring_periodic_stop;
      return PGAGROAL_EVENT_RC_OK;
   }
   else if (backend_type == PGAGROAL_EVENT_BACKEND_EPOLL)
   {
      loop_init = ev_epoll_init;
      loop_fork = ev_epoll_fork;
      loop_destroy = ev_epoll_destroy;
      loop_start = ev_epoll_loop;
      io_start = ev_epoll_io_start;
      io_stop = ev_epoll_io_stop;
      periodic_init = ev_epoll_periodic_init;
      periodic_start = ev_epoll_periodic_start;
      periodic_stop = ev_epoll_periodic_stop;
      return PGAGROAL_EVENT_RC_OK;
   }
#else
   if (backend_type == PGAGROAL_EVENT_BACKEND_KQUEUE)
   {
      loop_init = ev_kqueue_init;
      loop_fork = ev_kqueue_fork;
      loop_destroy = ev_kqueue_destroy;
      loop_start = ev_kqueue_loop;
      io_start = ev_kqueue_io_start;
      io_stop = ev_kqueue_io_stop;
      periodic_init = ev_kqueue_periodic_init;
      periodic_start = ev_kqueue_periodic_start;
      periodic_stop = ev_kqueue_periodic_stop;
      return PGAGROAL_EVENT_RC_OK;
   }
#endif /* HAVE_LINUX */
   return PGAGROAL_EVENT_RC_ERROR;
}

struct event_loop*
pgagroal_event_loop_init(void)
{
   static bool context_is_set = false;

   loop = calloc(1, sizeof(struct event_loop));
   if (loop == NULL)
   {
      pgagroal_log_fatal("calloc error: %s", strerror(errno));
      return NULL;
   }
   sigemptyset(&loop->sigset);

   if (!context_is_set)
   {
#if HAVE_LINUX
      /* io_uring context */

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
      ring_size = 128;
      params.cq_entries = 1024;
#else
      ring_size = 64;
      params.cq_entries = 128;
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

      params.flags = 0;
      params.flags |= IORING_SETUP_CQSIZE; /* needed if I'm using cq_entries above */
      params.flags |= IORING_SETUP_DEFER_TASKRUN;
      params.flags |= IORING_SETUP_SINGLE_ISSUER;

#if EXPERIMENTAL_FEATURE_FAST_POLL_ENABLED
      params.flags |= IORING_FEAT_FAST_POLL;
#endif /* EXPERIMENTAL_FEATURE_FAST_POLL_ENABLED */
#if EXPERIMENTAL_FEATURE_USE_HUGE_ENABLED
      /* XXX: Maybe this could be interesting if we cache the rings and the buffers? */
      params.flags |= IORING_SETUP_NO_MMAP;
#endif /* EXPERIMENTAL_FEATURE_USE_HUGE_ENABLED */

      /* epoll context */
      epoll_flags = 0;
#else
      /* kqueue context */
      kqueue_flags = 0;
#endif /* HAVE_LINUX */

      if (setup_ops())
      {
         pgagroal_log_fatal("Failed to set event backend operations");
         goto error;
      }

      if (loop_init())
      {
         pgagroal_log_fatal("Failed to initiate loop");
         goto error;
      }

      context_is_set = true;
   }
   else if (loop_init())
   {
      pgagroal_log_fatal("Failed to initiate loop");
      goto error;
   }

   return loop;

error:
   free(loop);
   loop = NULL;

   return NULL;
}

int
pgagroal_event_loop_run(void)
{
   return loop_start();
}

int
pgagroal_event_loop_fork(void)
{
   int rc;

   if (sigprocmask(SIG_UNBLOCK, &loop->sigset, NULL) == -1)
   {
      pgagroal_log_fatal("sigprocmask error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }

   /* no need to empty sigset */
   rc = loop_fork();

   return rc;
}

int
pgagroal_event_loop_destroy(void)
{
   int rc = PGAGROAL_EVENT_RC_OK;

   if (unlikely(!loop))
   {
      return 0;
   }

   rc = loop_destroy();

#if HAVE_LINUX
   for (int i = 0; i < loop->events_nr; i++)
   {
      event_watcher_t* w = loop->events[i];

      if (w && w->type == PGAGROAL_EVENT_TYPE_PERIODIC)
      {
         struct periodic_watcher* p = (struct periodic_watcher*)w;
         if (p->fd != -1)
         {
            pgagroal_disconnect(p->fd);
            p->fd = -1;
         }
      }
   }
#endif

   free(loop);
   loop = NULL;

   return rc;
}

void
pgagroal_event_loop_start(void)
{
   atomic_store(&loop->running, true);
}

void
pgagroal_event_loop_break(void)
{
   /* This function can be called even from interrupt handler, so we cannot
    * guarantee the loop exists at all times */
   if (unlikely(!loop))
   {
      return;
   }

   atomic_store(&loop->running, false);
}

bool
pgagroal_event_loop_is_running(void)
{
   return atomic_load(&loop->running);
}

int
pgagroal_event_accept_init(struct io_watcher* watcher, int listen_fd, io_cb cb)
{
   watcher->event_watcher.type = PGAGROAL_EVENT_TYPE_MAIN;
   watcher->fds.main.listen_fd = listen_fd;
   watcher->fds.main.client_fd = -1;
   watcher->cb = cb;
   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_event_worker_init(struct io_watcher* watcher, int rcv_fd, int snd_fd, io_cb cb)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   watcher->event_watcher.type = PGAGROAL_EVENT_TYPE_WORKER;
   watcher->fds.worker.rcv_fd = rcv_fd;
   watcher->fds.worker.snd_fd = snd_fd;
   watcher->cb = cb;

   if (config->ev_backend == PGAGROAL_EVENT_BACKEND_IO_URING)
   {
      init_watcher_message(watcher);
   }

   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_io_start(struct io_watcher* watcher)
{
   assert(loop != NULL && watcher != NULL);

   if (loop->events_nr >= MAX_EVENTS)
   {
      pgagroal_log_warn("pgagroal_io_start: MAX_EVENTS (%d) reached - cannot register new watcher (fd rcv=%d, snd=%d)",
                        MAX_EVENTS, watcher->fds.worker.rcv_fd, watcher->fds.worker.snd_fd);
      return PGAGROAL_EVENT_RC_FATAL;
   }

   loop->events[loop->events_nr] = (event_watcher_t*)watcher;
   loop->events_nr++;

   return io_start(watcher);
}

int
pgagroal_io_stop(struct io_watcher* watcher)
{
   int i;

   assert(loop != NULL && watcher != NULL);

   for (i = 0; i < loop->events_nr; i++)
   {
      if (watcher == (struct io_watcher*)loop->events[i])
      {
         break;
      }
   }

   if (i >= loop->events_nr)
   {
      pgagroal_log_warn("pgagroal_io_stop: watcher not found in events list (fd rcv=%d, snd=%d, events_nr=%d) - possible double-stop",
                        watcher->fds.worker.rcv_fd, watcher->fds.worker.snd_fd, loop->events_nr);
      return PGAGROAL_EVENT_RC_ERROR;
   }

   int rc = io_stop(watcher);
   if (rc != PGAGROAL_EVENT_RC_OK)
   {
      pgagroal_log_error("pgagroal_io_stop: io_stop failed %d", rc);
      return rc;
   }

   loop->events_nr--;
   if (i != loop->events_nr)
   {
      loop->events[i] = loop->events[loop->events_nr];
   }

   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_periodic_init(struct periodic_watcher* watcher, periodic_cb cb, int msec)
{
   watcher->event_watcher.type = PGAGROAL_EVENT_TYPE_PERIODIC;
   watcher->cb = cb;
   if (periodic_init(watcher, msec))
   {
      pgagroal_log_fatal("Failed to initiate timer event");
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_periodic_start(struct periodic_watcher* watcher)
{
   assert(loop != NULL && watcher != NULL);

   if (loop->events_nr >= MAX_EVENTS)
   {
      pgagroal_log_warn("pgagroal_periodic_start: MAX_EVENTS (%d) reached - cannot register periodic watcher",
                        MAX_EVENTS);
      return PGAGROAL_EVENT_RC_FATAL;
   }

   loop->events[loop->events_nr] = (event_watcher_t*)watcher;
   loop->events_nr++;

   return periodic_start(watcher);
}

int __attribute__((unused))
pgagroal_periodic_stop(struct periodic_watcher* watcher)
{
   int i;

   assert(loop != NULL && watcher != NULL);

   for (i = 0; i < loop->events_nr; i++)
   {
      if (watcher == (struct periodic_watcher*)loop->events[i])
      {
         break;
      }
   }

   if (i >= loop->events_nr)
   {
      return PGAGROAL_EVENT_RC_ERROR;
   }

   int rc = periodic_stop(watcher);
   if (rc != PGAGROAL_EVENT_RC_OK)
   {
      pgagroal_log_error("pgagroal_periodic_stop: periodic_stop failed %d", rc);
      return rc;
   }

   loop->events_nr--;
   if (i != loop->events_nr)
   {
      loop->events[i] = loop->events[loop->events_nr];
   }

   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_event_prep_submit_send(struct io_watcher* watcher, struct message* msg)
{
   int sent_bytes = 0;
#if HAVE_LINUX
   struct io_uring_sqe* sqe = NULL;
   struct io_uring_cqe* cqe = NULL;
   int send_flags = 0;
   int ret;
   int cqe_res;

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
   int bid = loop->bid;
   if (bid < 0 || bid >= 2)
   {
      pgagroal_log_fatal("invalid buffer id: %d", bid);
      return -1;
   }
   void* data = loop->br.buf + bid * DEFAULT_BUFFER_SIZE;
   msg->data = data;
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

   ssize_t total_sent = 0;
   ssize_t to_send = msg->length;

   /*
    * Use the dedicated send_ring for sends.
    * This avoids CQE mixing issues where recv completions arrive on the
    * main ring while we're waiting for a send completion. With a separate
    * ring, we're guaranteed to only receive send CQEs here.
    */
   while (total_sent < to_send)
   {
      sqe = io_uring_get_sqe(&loop->ring_snd);
      if (!sqe)
      {
         pgagroal_log_error("io_uring: no SQE available for send on send_ring");
         return -1;
      }

#if EXPERIMENTAL_FEATURE_ZERO_COPY_ENABLED
      /* XXX: Implement zero copy send (this has been shown to speed up a little some
       * workloads, but the implementation is still problematic). */
      io_uring_prep_send_zc(sqe, watcher->fds.worker.snd_fd,
                            (char*)msg->data + total_sent,
                            to_send - total_sent,
                            send_flags, 0);
#else
      send_flags |= MSG_NOSIGNAL;
      io_uring_prep_send(sqe, watcher->fds.worker.snd_fd,
                         (char*)msg->data + total_sent,
                         to_send - total_sent,
                         send_flags);
#endif /* EXPERIMENTAL_FEATURE_ZERO_COPY_ENABLED */

      io_uring_sqe_set_data(sqe, NULL);

      ret = io_uring_submit(&loop->ring_snd);
      if (ret < 0)
      {
         pgagroal_log_error("io_uring send submit error: %s", strerror(-ret));
         return -1;
      }

      ret = io_uring_wait_cqe(&loop->ring_snd, &cqe);
      if (ret < 0)
      {
         pgagroal_log_error("io_uring send wait error: %s", strerror(-ret));
         return -1;
      }

      /* Read cqe->res before calling io_uring_cqe_seen() to prevent the
       * completion from being reused before we read the result. */
      cqe_res = cqe->res;
      io_uring_cqe_seen(&loop->ring_snd, cqe);

      if (cqe_res < 0)
      {
         pgagroal_log_debug("io_uring send error fd=%d: %s",
                            watcher->fds.worker.snd_fd, strerror(-cqe_res));
         return cqe_res;
      }

      if (cqe_res == 0)
      {
         /* Connection closed */
         pgagroal_log_debug("io_uring send closed fd=%d after %zd/%zd bytes",
                            watcher->fds.worker.snd_fd, total_sent, to_send);
         break;
      }

      total_sent += cqe_res;
   }

   if (total_sent > INT_MAX)
   {
      pgagroal_log_error("io_uring send overflow: %zd", total_sent);
      sent_bytes = -EOVERFLOW;
   }
   else
   {
      sent_bytes = (int)total_sent;
   }

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
   io_uring_buf_ring_add(loop->br.br,
                         data,
                         DEFAULT_BUFFER_SIZE,
                         bid,
                         DEFAULT_BUFFER_SIZE,
                         1);
   io_uring_buf_ring_advance(loop->br.br, 1);
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

#endif /* HAVE_LINUX */
   return sent_bytes;
}

int
pgagroal_wait_recv(void)
{
   int recv_bytes = 0;
#if HAVE_LINUX
   struct io_uring_cqe* rcv_cqe = NULL;
   int rc = io_uring_wait_cqe(&loop->ring_rcv, &rcv_cqe);
   if (rc < 0)
   {
      pgagroal_log_error("io_uring recv wait error: %s", strerror(-rc));
      return rc;
   }
   if (!rcv_cqe)
   {
      pgagroal_log_error("io_uring recv wait error: missing CQE");
      return -EIO;
   }
   recv_bytes = rcv_cqe->res;
   io_uring_cqe_seen(&loop->ring_rcv, rcv_cqe);
#endif
   return recv_bytes;
}

#if HAVE_LINUX

static inline void __attribute__((unused))
ev_io_uring_rearm_receive(struct event_loop* loop, struct io_watcher* watcher)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring_rcv);
   io_uring_sqe_set_data(sqe, watcher);
   io_uring_prep_recv_multishot(sqe, watcher->fds.worker.rcv_fd, NULL, 0, 0);
}

static int
ev_io_uring_init(void)
{
   int rc;
   struct io_uring_params send_params = {0};

   /* Initialize the main ring for receives */
   rc = io_uring_queue_init_params(ring_size, &loop->ring_rcv, &params);
   if (rc)
   {
      pgagroal_log_fatal("io_uring_queue_init_params (recv ring) error: %s", strerror(-rc));
      return rc;
   }

   rc = io_uring_ring_dontfork(&loop->ring_rcv);
   if (rc)
   {
      pgagroal_log_fatal("io_uring_ring_dontfork (recv ring) error: %s", strerror(-rc));
      io_uring_queue_exit(&loop->ring_rcv);
      return rc;
   }

   /* Initialize a separate ring for sends to avoid CQE mixing issues.
    * When waiting for a send CQE on a shared ring, recv CQEs may arrive first,
    * causing either lost data, stack overflow (if processed), or state corruption.
    * Using a separate ring guarantees we only get send CQEs when waiting for sends. */
   rc = io_uring_queue_init_params(64, &loop->ring_snd, &send_params);
   if (rc)
   {
      pgagroal_log_fatal("io_uring_queue_init_params (send ring) error: %s", strerror(-rc));
      io_uring_queue_exit(&loop->ring_rcv);
      return rc;
   }

   rc = io_uring_ring_dontfork(&loop->ring_snd);
   if (rc)
   {
      pgagroal_log_fatal("io_uring_ring_dontfork (send ring) error: %s", strerror(-rc));
      io_uring_queue_exit(&loop->ring_rcv);
      io_uring_queue_exit(&loop->ring_snd);
      return rc;
   }

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
   rc = ev_io_uring_setup_buffers();
   if (rc)
   {
      pgagroal_log_fatal("ev_io_uring_setup_buffers error");
      return rc;
   }
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_io_uring_destroy(void)
{
#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
   if (loop->br.buf != NULL)
   {
      free(loop->br.buf);
      loop->br.buf = NULL;
   }
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */
   io_uring_queue_exit(&loop->ring_rcv);
   io_uring_queue_exit(&loop->ring_snd);
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_io_uring_io_start(struct io_watcher* watcher)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring_rcv);
   struct message* msg = NULL;

   io_uring_sqe_set_data(sqe, watcher);
   switch (watcher->event_watcher.type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         io_uring_prep_multishot_accept(sqe, watcher->fds.main.listen_fd, NULL, NULL, 0);
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
         io_uring_prep_recv_multishot(sqe, watcher->fds.worker.rcv_fd, NULL, 0, 0); /* msg must be NULL */
         sqe->buf_group = 0;
         sqe->flags |= IOSQE_BUFFER_SELECT;
#else
         msg = pgagroal_get_watcher_message(watcher);
         /* Use MESSAGE_PARSE_BUFFER_SIZE to leave headroom and prevent buffer
          * overflow when parsing message headers near the end of received data */
         io_uring_prep_recv(sqe, watcher->fds.worker.rcv_fd, msg->data, MESSAGE_PARSE_BUFFER_SIZE, 0);
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", watcher->event_watcher.type);
         exit(1);
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_io_uring_io_stop(struct io_watcher* target)
{
   int rc = PGAGROAL_EVENT_RC_OK;
   struct io_uring_sqe* sqe;
   struct io_uring_cqe* cqe;
   struct __kernel_timespec ts = {.tv_sec = 2, .tv_nsec = 0};

   /* When io_stop is called it may never return to a loop
    * where sqes are submitted. Flush these sqes so the get call
    * doesn't return NULL. */
   for (int retries = 0; retries < 100; retries++)
   {
      sqe = io_uring_get_sqe(&loop->ring_rcv);
      if (sqe)
      {
         break;
      }
      pgagroal_log_warn("sqe is full");
      io_uring_submit(&loop->ring_rcv);
   }
   if (!sqe)
   {
      pgagroal_log_error("io_uring: no SQE available for cancel");
      return PGAGROAL_EVENT_RC_ERROR;
   }

   io_uring_prep_cancel(sqe, (void*)target, 0);

   io_uring_submit_and_wait_timeout(&loop->ring_rcv, &cqe, 0, &ts, NULL);

   return rc;
}

static int
ev_io_uring_periodic_init(struct periodic_watcher* watcher, int msec)
{
   watcher->ts = (struct __kernel_timespec){
      .tv_sec = msec / 1000,
      .tv_nsec = (msec % 1000) * 1000000};
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_io_uring_periodic_start(struct periodic_watcher* watcher)
{
   struct io_uring_sqe* sqe = io_uring_get_sqe(&loop->ring_rcv);
   io_uring_sqe_set_data(sqe, watcher);
   io_uring_prep_timeout(sqe, &watcher->ts, 0, IORING_TIMEOUT_MULTISHOT);
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_io_uring_periodic_stop(struct periodic_watcher* watcher)
{
   struct io_uring_sqe* sqe;
   sqe = io_uring_get_sqe(&loop->ring_rcv);
   io_uring_prep_cancel64(sqe, (uint64_t)watcher, 0);
   return PGAGROAL_EVENT_RC_OK;
}

static int __attribute__((unused))
ev_io_uring_flush(void)
{
   int rc = PGAGROAL_EVENT_RC_ERROR;
   unsigned int head;
   struct __kernel_timespec ts = {
      .tv_sec = 0,
      .tv_nsec = 100000LL, /* seems best with 100000LL ns for most loads */
   };

   struct io_uring_cqe* cqe;
   struct io_uring_sqe* sqe;
   int to_wait = 0;
   int events = 0;

retry:
   sqe = io_uring_get_sqe(&loop->ring_rcv);
   if (!sqe)
   {
      pgagroal_log_warn("sqe is full, retrying...");
      io_uring_submit(&loop->ring_rcv);
      goto retry;
   }

   for (int i = 0; i < loop->events_nr; i++)
   {
      io_uring_prep_cancel(sqe, (void*)(loop->events[i]), 0);
      /* XXX: if used, delete event */
      to_wait++;
   }

   io_uring_submit_and_wait_timeout(&loop->ring_rcv, &cqe, to_wait, &ts, NULL);

   io_uring_for_each_cqe(&loop->ring_rcv, head, cqe)
   {
#ifdef DEBUG
      rc = cqe->res;
      if (rc < 0)
      {
         /* -EINVAL shouldn't happen */
         pgagroal_log_trace("io_uring_prep_cancel rc: %s", strerror(-rc));
      }
#endif
      events++;
   }
   if (events)
   {
      io_uring_cq_advance(&loop->ring_rcv, events);
   }
   return rc;
}

/*
 * Based on: https://git.kernel.dk/cgit/liburing/tree/examples/proxy.c
 * (C) 2024 Jens Axboe <axboe@kernel.dk>
 */
static int
ev_io_uring_loop(void)
{
   int rc = PGAGROAL_EVENT_RC_ERROR;
   int events;
   int to_wait = 1; /* at first, wait for any 1 event */
   unsigned int head;
   struct io_uring_cqe* cqe = NULL;
   struct __kernel_timespec* ts = NULL;
   struct __kernel_timespec idle_ts = {
      .tv_sec = 0,
      .tv_nsec = 100000LL, /* seems best with 100000LL ns for most loads */
   };

   pgagroal_event_loop_start();
   while (pgagroal_event_loop_is_running())
   {
      ts = &idle_ts;

      io_uring_submit_and_wait_timeout(&loop->ring_rcv, &cqe, to_wait, ts, NULL);

      if (*loop->ring_rcv.cq.koverflow)
      {
         pgagroal_log_fatal("io_uring overflow %u", *loop->ring_rcv.cq.koverflow);
         pgagroal_event_loop_break();
         return PGAGROAL_EVENT_RC_FATAL;
      }
      if (*loop->ring_rcv.sq.kflags & IORING_SQ_CQ_OVERFLOW)
      {
         pgagroal_log_fatal("io_uring overflow");
         pgagroal_event_loop_break();
         return PGAGROAL_EVENT_RC_FATAL;
      }

      events = 0;
      io_uring_for_each_cqe(&loop->ring_rcv, head, cqe)
      {
         rc = ev_io_uring_handler(cqe);
         if (rc)
         {
            pgagroal_event_loop_break();
            break;
         }
         events++;
      }

      if (events)
      {
         io_uring_cq_advance(&loop->ring_rcv, events);
      }
   }

   return rc;
}

static int
ev_io_uring_fork(void)
{
   return 0;
}

static int
ev_io_uring_handler(struct io_uring_cqe* cqe)
{
   int rc = 0;
   event_watcher_t* watcher = io_uring_cqe_get_data(cqe);
   struct io_watcher* io;
   struct periodic_watcher* per;
   struct message* msg = NULL;

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
   loop->bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

   /* Cancelled requests will trigger the handler, but have NULL data. */
   if (!watcher)
   {
      rc = cqe->res;
      if (rc == -ENOENT)
      {
         pgagroal_log_trace("io_uring: cancelled operation not found");
         return PGAGROAL_EVENT_RC_OK;
      }
      else if (rc == -ECANCELED)
      {
         pgagroal_log_trace("io_uring: operation cancelled");
         return PGAGROAL_EVENT_RC_OK;
      }
      else if (rc < 0)
      {
         pgagroal_log_debug("io_uring: CQE with NULL watcher, res=%d: %s", rc, strerror(-rc));
      }
      return PGAGROAL_EVENT_RC_OK;
   }

   /* This type of thing is not ideal, ideally I should have
    * only event_watcher_t pointers returning in cqe->user_data */
   switch (watcher->type)
   {
      case PGAGROAL_EVENT_TYPE_PERIODIC:
         per = (struct periodic_watcher*)watcher;
         per->cb();
         break;
      case PGAGROAL_EVENT_TYPE_MAIN:
         io = (struct io_watcher*)watcher;
         io->fds.main.client_fd = cqe->res;
         io->cb(io);
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         io = (struct io_watcher*)watcher;
         msg = pgagroal_get_watcher_message(io);
         if (cqe->res <= 0)
         {
            if (cqe->res == 0)
            {
               pgagroal_log_debug("io_uring: connection closed fd=%d", io->fds.worker.rcv_fd);
            }
            else
            {
               pgagroal_log_debug("io_uring: recv error fd=%d: %s",
                                  io->fds.worker.rcv_fd, strerror(-cqe->res));
            }
            msg->length = 0;
            rc = PGAGROAL_EVENT_RC_CONN_CLOSED;
            io->cb(io);
            /* Do NOT rearm after connection close or error */
         }
         else
         {
            msg->length = cqe->res;
            rc = PGAGROAL_EVENT_RC_OK;
            io->cb(io);

            /* Only rearm if loop is still running and connection is good */
            if (pgagroal_event_loop_is_running())
            {
               ev_io_uring_io_start(io);
            }
         }

         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", watcher->type);
         return PGAGROAL_EVENT_RC_FATAL;
   }
   return rc;
}

#if EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED
static int
ev_io_uring_setup_buffers(void)
{
   int rc;
   int br_cnt = 1;
   int br_bgid = 0;
   int br_mask = DEFAULT_BUFFER_SIZE;
   int br_flags = 0;
   int bid = 0;

#if EXPERIMENTAL_FEATURE_USE_HUGE_ENABLED
   pgagroal_log_fatal("io_uring use_huge not implemented");
   exit(1);
#endif /* EXPERIMENTAL_FEATURE_USE_HUGE_ENABLED */

   loop->br.br = NULL;
   loop->br.buf = NULL;
   loop->br.pending_send = false;
   loop->br.cnt = 0;

   loop->br.br = io_uring_setup_buf_ring(&loop->ring_rcv, br_cnt, br_bgid, br_flags, &rc);
   if (!loop->br.br)
   {
      pgagroal_log_fatal("buffer ring register error %s", strerror(-rc));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   if (posix_memalign(&loop->br.buf, sysconf(_SC_PAGESIZE), 2 * DEFAULT_BUFFER_SIZE))
   {
      pgagroal_log_fatal("posix_memalign error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   io_uring_buf_ring_add(loop->br.br,
                         loop->br.buf,
                         DEFAULT_BUFFER_SIZE,
                         bid++,
                         br_mask,
                         loop->br.cnt++);

   io_uring_buf_ring_add(loop->br.br,
                         loop->br.buf + DEFAULT_BUFFER_SIZE,
                         DEFAULT_BUFFER_SIZE,
                         bid,
                         br_mask,
                         loop->br.cnt++);

   io_uring_buf_ring_advance(loop->br.br, loop->br.cnt);

   return PGAGROAL_EVENT_RC_OK;
}
#endif /* EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED */

int
ev_epoll_loop(void)
{
   int rc = PGAGROAL_EVENT_RC_OK;
   int nfds;
   struct epoll_event events[MAX_EVENTS];
#if HAVE_EPOLL_PWAIT2
   struct timespec timeout_ts = {
      .tv_sec = 0,
      .tv_nsec = 10000000LL,
   };
#else
   int timeout = 10LL; /* ms */
#endif /* HAVE_EPOLL_PWAIT2 */

   pgagroal_event_loop_start();
   while (pgagroal_event_loop_is_running())
   {
#if HAVE_EPOLL_PWAIT2
      nfds = epoll_pwait2(loop->epollfd, events, MAX_EVENTS, &timeout_ts,
                          &loop->sigset);
#else
      nfds = epoll_pwait(loop->epollfd, events, MAX_EVENTS, timeout, &loop->sigset);
#endif

      if (nfds == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         pgagroal_log_error("epoll_pwait error: %s", strerror(errno));
         rc = PGAGROAL_EVENT_RC_ERROR;
         pgagroal_event_loop_break();
         break;
      }

      for (int i = 0; i < nfds; i++)
      {
         rc = ev_epoll_handler((void*)events[i].data.u64);
         if (rc)
         {
            pgagroal_event_loop_break();
            break;
         }
      }
   }
   return rc;
}

static int
ev_epoll_init(void)
{
   loop->epollfd = epoll_create1(epoll_flags);
   if (loop->epollfd == -1)
   {
      pgagroal_log_fatal("epoll_init error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_fork(void)
{
   if (close(loop->epollfd) < 0)
   {
      pgagroal_log_error("close error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_destroy(void)
{
   if (close(loop->epollfd) < 0)
   {
      pgagroal_log_error("close error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_handler(void* watcher)
{
   enum event_type type = ((event_watcher_t*)watcher)->type;
   if (type == PGAGROAL_EVENT_TYPE_PERIODIC)
   {
      return ev_epoll_periodic_handler((struct periodic_watcher*)watcher);
   }
   return ev_epoll_io_handler((struct io_watcher*)watcher);
}

static int
ev_epoll_periodic_init(struct periodic_watcher* watcher, int msec)
{
   struct timespec now;
   struct itimerspec new_value;

   if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
   {
      pgagroal_log_error("clock_gettime: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }

   new_value.it_value.tv_sec = msec / 1000;
   new_value.it_value.tv_nsec = (msec % 1000) * 1000000;

   new_value.it_interval.tv_sec = msec / 1000;
   new_value.it_interval.tv_nsec = (msec % 1000) * 1000000;

   /* no need to set it to non-blocking due to TFD_NONBLOCK */
   watcher->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
   if (watcher->fd == -1)
   {
      pgagroal_log_error("timerfd_create: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }

   if (timerfd_settime(watcher->fd, 0, &new_value, NULL) == -1)
   {
      pgagroal_log_error("timerfd_settime");
      close(watcher->fd);
      watcher->fd = -1;
      return PGAGROAL_EVENT_RC_ERROR;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_periodic_start(struct periodic_watcher* watcher)
{
   struct epoll_event event;
   event.events = EPOLLIN;
   event.data.u64 = (uint64_t)watcher;
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, watcher->fd, &event) == -1)
   {
      pgagroal_log_fatal("epoll_ctl error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_periodic_stop(struct periodic_watcher* watcher)
{
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, watcher->fd, NULL) == -1)
   {
      pgagroal_log_error("epoll_ctl error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }

   pgagroal_disconnect(watcher->fd);
   watcher->fd = -1;

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_periodic_handler(struct periodic_watcher* watcher)
{
   uint64_t exp;
   int nread = read(watcher->fd, &exp, sizeof(uint64_t));
   if (nread != sizeof(uint64_t))
   {
      pgagroal_log_error("periodic_handler read: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }
   watcher->cb();
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_io_start(struct io_watcher* watcher)
{
   enum event_type type = watcher->event_watcher.type;
   struct epoll_event event;
   int fd;

   event.data.u64 = (uintptr_t)watcher;

   switch (type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         fd = watcher->fds.main.listen_fd;
         event.events = EPOLLIN;
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         fd = watcher->fds.worker.rcv_fd;
         /* XXX: lookup the possibility to add EPOLLET here */
         event.events = EPOLLIN;
         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", type);
         exit(1);
   }

   if (epoll_ctl(loop->epollfd, EPOLL_CTL_ADD, fd, &event) == -1)
   {
      if (errno == EEXIST)
      {
         /* FD already exists, modify it instead */
         pgagroal_log_debug("epoll_ctl: fd %d already exists, modifying instead", fd);
         if (epoll_ctl(loop->epollfd, EPOLL_CTL_MOD, fd, &event) == -1)
         {
            pgagroal_log_error("epoll_ctl error when modifying fd %d : %s", fd, strerror(errno));
            return PGAGROAL_EVENT_RC_FATAL;
         }
      }
      else
      {
         pgagroal_log_error("epoll_ctl error when adding fd %d : %s", fd, strerror(errno));
         return PGAGROAL_EVENT_RC_FATAL;
      }
   }

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_io_stop(struct io_watcher* watcher)
{
   enum event_type type = watcher->event_watcher.type;
   int fd;

   switch (type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         fd = watcher->fds.main.listen_fd;
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         fd = watcher->fds.worker.rcv_fd;
         break;
      default:
         /* reaching here is a bug, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", type);
         return PGAGROAL_EVENT_RC_FATAL;
   }
   if (epoll_ctl(loop->epollfd, EPOLL_CTL_DEL, fd, NULL) == -1)
   {
      if (errno == EBADF || errno == ENOENT || errno == EINVAL)
      {
         pgagroal_log_error("epoll_ctl error: %s", strerror(errno));
      }
      else
      {
         pgagroal_log_fatal("epoll_ctl error: %s", strerror(errno));
         return PGAGROAL_EVENT_RC_FATAL;
      }
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_epoll_io_handler(struct io_watcher* watcher)
{
   int client_fd = -1;
   enum event_type type = watcher->event_watcher.type;
   switch (type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         client_fd = accept(watcher->fds.main.listen_fd, NULL, NULL);
         if (client_fd == -1)
         {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
               pgagroal_log_error("accept error: %s", strerror(errno));
               return PGAGROAL_EVENT_RC_ERROR;
            }
         }
         else
         {
            watcher->fds.main.client_fd = client_fd;
            watcher->cb(watcher);
         }
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         watcher->cb(watcher);
         break;
      default:
         /* shouldn't happen, do not recover */
         pgagroal_log_fatal("BUG: Unknown event type: %d", type);
         return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

#else

int
ev_kqueue_loop(void)
{
   int rc = PGAGROAL_EVENT_RC_OK;
   int nfds;
   struct kevent events[MAX_EVENTS];
   struct timespec timeout;
   timeout.tv_sec = 0;
   timeout.tv_nsec = 10000000; /* 10 ms */

   pgagroal_event_loop_start();
   while (pgagroal_event_loop_is_running())
   {
      nfds = kevent(loop->kqueuefd, NULL, 0, events, MAX_EVENTS, &timeout);
      if (nfds == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }

         pgagroal_log_error("kevent error: %s", strerror(errno));
         rc = PGAGROAL_EVENT_RC_ERROR;
         pgagroal_event_loop_break();
         break;
      }
      for (int i = 0; i < nfds; i++)
      {
         rc = ev_kqueue_handler(&events[i]);
         if (rc)
         {
            pgagroal_event_loop_break();
            break;
         }
      }
   }
   return rc;
}

static int
ev_kqueue_init(void)
{
   loop->kqueuefd = kqueue();
   if (loop->kqueuefd == -1)
   {
      pgagroal_log_fatal("kqueue init error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_fork(void)
{
   close(loop->kqueuefd);
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_destroy(void)
{
   close(loop->kqueuefd);
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_handler(struct kevent* kev)
{
   switch (kev->filter)
   {
      case EVFILT_TIMER:
         return ev_kqueue_periodic_handler(kev);
      case EVFILT_READ:
      case EVFILT_WRITE:
         return ev_kqueue_io_handler(kev);
      default:
         /* shouldn't happen, do not recover */
         pgagroal_log_fatal("BUG: Unknown filter in handler");
         return PGAGROAL_EVENT_RC_FATAL;
   }
}

int __attribute__((unused))
ev_kqueue_signal_start(struct signal_watcher* watcher)
{
   struct kevent kev;

   EV_SET(&kev, watcher->signum, EVFILT_SIGNAL, EV_ADD, 0, 0, watcher);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_fatal("kevent error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int __attribute__((unused))
ev_kqueue_signal_stop(struct signal_watcher* watcher)
{
   struct kevent kev;

   EV_SET(&kev, watcher->signum, EVFILT_SIGNAL, EV_DELETE, 0, 0, watcher);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_fatal("kevent error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_FATAL;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int __attribute__((unused))
ev_kqueue_signal_handler(struct kevent* kev)
{
   int rc = 0;
   struct signal_watcher* watcher = (struct signal_watcher*)kev->udata;
   watcher->cb();
   return rc;
}

static int
ev_kqueue_periodic_init(struct periodic_watcher* watcher, int msec)
{
   watcher->interval = msec;
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_periodic_start(struct periodic_watcher* watcher)
{
   struct kevent kev;
   EV_SET(&kev, (uintptr_t)watcher, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_USECONDS,
          watcher->interval * 1000, watcher);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent timer add: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_periodic_stop(struct periodic_watcher* watcher)
{
   struct kevent kev;
   EV_SET(&kev, (uintptr_t)watcher, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      pgagroal_log_error("kevent timer delete: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_periodic_handler(struct kevent* kev)
{
   struct periodic_watcher* watcher = (struct periodic_watcher*)kev->udata;
   watcher->cb();
   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_io_start(struct io_watcher* watcher)
{
   enum event_type type = watcher->event_watcher.type;
   struct kevent kev;
   int filter;
   int fd;

   switch (type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         filter = EVFILT_READ;
         fd = watcher->fds.main.listen_fd;
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         filter = EVFILT_READ;
         fd = watcher->fds.worker.rcv_fd;
         break;
      default:
         /* shouldn't happen, do not recover */
         pgagroal_log_fatal("Unknown event type: %d", type);
         return PGAGROAL_EVENT_RC_FATAL;
   }

   EV_SET(&kev, fd, filter, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, watcher);

   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      if (errno == EBADF)
      {
         /* File descriptor already closed */
         pgagroal_log_debug("kevent: fd already closed: %s", strerror(errno));
      }
      else
      {
         pgagroal_log_error("kevent error: %s", strerror(errno));
         return PGAGROAL_EVENT_RC_ERROR;
      }
   }

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_io_stop(struct io_watcher* watcher)
{
   struct kevent kev;
   int filter = EVFILT_READ;

   EV_SET(&kev, watcher->fds.__fds[0], filter, EV_DELETE, 0, 0, NULL);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      if (errno == EBADF || errno == ENOENT)
      {
         /* File descriptor already closed or event not found */
         pgagroal_log_debug("%s: kevent delete on closed/invalid fd[0]: %s", __func__, strerror(errno));
      }
      else
      {
         pgagroal_log_error("%s: kevent delete failed for fd[0]: %s", __func__, strerror(errno));
         return PGAGROAL_EVENT_RC_ERROR;
      }
   }

   EV_SET(&kev, watcher->fds.__fds[1], filter, EV_DELETE, 0, 0, NULL);
   if (kevent(loop->kqueuefd, &kev, 1, NULL, 0, NULL) == -1)
   {
      if (errno == EBADF || errno == ENOENT)
      {
         /* File descriptor already closed or event not found */
         pgagroal_log_debug("%s: kevent delete on closed/invalid fd[1]: %s", __func__, strerror(errno));
      }
      else
      {
         pgagroal_log_error("%s: kevent delete failed for fd[1]: %s", __func__, strerror(errno));
         return PGAGROAL_EVENT_RC_ERROR;
      }
   }

   return PGAGROAL_EVENT_RC_OK;
}

static int
ev_kqueue_io_handler(struct kevent* kev)
{
   struct io_watcher* watcher = (struct io_watcher*)kev->udata;
   enum event_type type = watcher->event_watcher.type;
   int rc = PGAGROAL_EVENT_RC_OK;

   switch (type)
   {
      case PGAGROAL_EVENT_TYPE_MAIN:
         watcher->fds.main.client_fd = accept(watcher->fds.main.listen_fd, NULL, NULL);
         if (watcher->fds.main.client_fd == -1)
         {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
               pgagroal_log_error("accept error: %s", strerror(errno));
               rc = PGAGROAL_EVENT_RC_ERROR;
            }
         }
         else
         {
            watcher->cb(watcher);
         }
         break;
      case PGAGROAL_EVENT_TYPE_WORKER:
         if (kev->flags & EV_EOF)
         {
            pgagroal_log_debug("Connection closed on fd %d", watcher->fds.worker.rcv_fd);
            rc = PGAGROAL_EVENT_RC_CONN_CLOSED;
         }
         else
         {
            watcher->cb(watcher);
         }
         break;
      default:
         pgagroal_log_fatal("unknown event type: %d", type);
         return PGAGROAL_EVENT_RC_FATAL;
   }
   return rc;
}

#endif /* HAVE_LINUX */

int
pgagroal_signal_init(struct signal_watcher* watcher, signal_cb cb, int signum)
{
   watcher->event_watcher.type = PGAGROAL_EVENT_TYPE_SIGNAL;
   watcher->signum = signum;
   watcher->cb = cb;
   return PGAGROAL_EVENT_RC_OK;
}

int
pgagroal_signal_start(struct signal_watcher* watcher)
{
   struct sigaction act;

   if (!loop)
   {
      pgagroal_log_error("signal_start: loop is NULL");
      return PGAGROAL_EVENT_RC_ERROR;
   }

   if (watcher->signum <= 0 || watcher->signum >= PGAGROAL_NSIG)
   {
      pgagroal_log_error("signal_start: invalid signum %d", watcher->signum);
      return PGAGROAL_EVENT_RC_ERROR;
   }

   atomic_store_explicit(&signal_watchers[watcher->signum], watcher, memory_order_release);

   sigemptyset(&act.sa_mask);
   act.sa_sigaction = &signal_handler;
   act.sa_flags = SA_SIGINFO | SA_RESTART;
   if (sigaction(watcher->signum, &act, NULL) == -1)
   {
      pgagroal_log_fatal("sigaction failed for signum %d", watcher->signum);
      return PGAGROAL_EVENT_RC_ERROR;
   }
   if (sigaddset(&loop->sigset, watcher->signum) == -1)
   {
      pgagroal_log_error("sigaddset error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }

   return PGAGROAL_EVENT_RC_OK;
}

int __attribute__((unused))
pgagroal_signal_stop(struct signal_watcher* target)
{
   int rc = PGAGROAL_EVENT_RC_OK;
   sigset_t tmp;

#ifdef DEBUG
   if (!target)
   {
      /* reaching here is a bug, do not recover */
      pgagroal_log_fatal("BUG: target is NULL");
      exit(1);
   }
#endif

   sigemptyset(&tmp);
   sigaddset(&tmp, target->signum);
   if (loop && sigdelset(&loop->sigset, target->signum) == -1)
   {
      pgagroal_log_error("sigdelset error: %s", strerror(errno));
      return PGAGROAL_EVENT_RC_ERROR;
   }
   if (target->signum > 0 && target->signum < PGAGROAL_NSIG)
   {
      atomic_store_explicit(&signal_watchers[target->signum], NULL, memory_order_release);
   }
#if !HAVE_LINUX
   /* XXX: FreeBSD catches SIGINT as soon as it is removed from
    * sigset. This could probably be improved */
   if (target->signum != SIGINT)
   {
#endif
      if (sigprocmask(SIG_UNBLOCK, &tmp, NULL) == -1)
      {
         pgagroal_log_fatal("sigprocmask error: %s", strerror(errno));
         return PGAGROAL_EVENT_RC_FATAL;
      }
#if !HAVE_LINUX
   }
#endif

   return rc;
}

static void
signal_handler(int signum, siginfo_t* si __attribute__((unused)), void* p __attribute__((unused)))
{
   if (signum < 0 || signum >= PGAGROAL_NSIG)
   {
      return;
   }

   struct signal_watcher* watcher = atomic_load_explicit(&signal_watchers[signum], memory_order_acquire);
   if (watcher && watcher->cb)
   {
      watcher->cb();
   }
}

static void
init_watcher_message(struct io_watcher* watcher)
{
   if (watcher->msg == NULL)
   {
      watcher->msg = calloc(1, sizeof(struct message));
      if (watcher->msg == NULL)
      {
         pgagroal_log_fatal("failed to allocate message");
         exit(1);
      }
      watcher->msg->data = calloc(1, DEFAULT_BUFFER_SIZE);
      if (watcher->msg->data == NULL)
      {
         pgagroal_log_fatal("failed to allocate message buffer");
         exit(1);
      }
      watcher->msg->length = 0;
      watcher->msg->kind = 0;
   }
}
