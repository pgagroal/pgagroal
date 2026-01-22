/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef EV_H
#define EV_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgagroal */
#include <pgagroal.h>

/* system */
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if HAVE_LINUX
#include <liburing.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#endif /* HAVE_LINUX */

#define EXPERIMENTAL_FEATURE_ZERO_COPY_ENABLED      0
#define EXPERIMENTAL_FEATURE_FAST_POLL_ENABLED      0
#define EXPERIMENTAL_FEATURE_USE_HUGE_ENABLED       0
#define EXPERIMENTAL_FEATURE_RECV_MULTISHOT_ENABLED 0
#define EXPERIMENTAL_FEATURE_IOVECS                 0
#define PGAGROAL_CONTEXT_MAIN                       0
#define PGAGROAL_CONTEXT_VAULT                      1

#define ALIGNMENT                                   sysconf(_SC_PAGESIZE)
#define MAX_EVENTS                                  32
#define INITIAL_BUFFER_COUNT                        1
#if HAVE_LINUX
#define PGAGROAL_NSIG _NSIG
#else
/* https://man.freebsd.org/cgi/man.cgi?query=signal&sektion=3&format=html */
#define PGAGROAL_NSIG 33
#endif

/* Constants used to define the supported event backends */
typedef enum ev_backend {
   PGAGROAL_EVENT_BACKEND_INVALID = -2,
   PGAGROAL_EVENT_BACKEND_EMPTY = -1,
   PGAGROAL_EVENT_BACKEND_AUTO = 0,
   PGAGROAL_EVENT_BACKEND_IO_URING,
   PGAGROAL_EVENT_BACKEND_EPOLL,
   PGAGROAL_EVENT_BACKEND_KQUEUE,
} ev_backend_t;

#if HAVE_LINUX
#define DEFAULT_EVENT_BACKEND PGAGROAL_EVENT_BACKEND_IO_URING
#else
#define DEFAULT_EVENT_BACKEND PGAGROAL_EVENT_BACKEND_KQUEUE
#endif

/* Enumerates the types of events in the event loop */
enum event_type {
   PGAGROAL_EVENT_TYPE_INVALID = 0,
   PGAGROAL_EVENT_TYPE_MAIN,
   PGAGROAL_EVENT_TYPE_WORKER,
   PGAGROAL_EVENT_TYPE_SIGNAL,
   PGAGROAL_EVENT_TYPE_PERIODIC,
};

/* Defines return codes for event operations */
enum ev_return_codes {
   PGAGROAL_EVENT_RC_OK = 0,
   PGAGROAL_EVENT_RC_ERROR = 1,
   PGAGROAL_EVENT_RC_FATAL = 2,
   PGAGROAL_EVENT_RC_CONN_CLOSED,
};

struct event_loop;

/**
 * @struct event_watcher
 * @brief General watcher for the event loop
 */
typedef struct event_watcher
{
   enum event_type type; /**<Type of the watcher. */
} event_watcher_t;

/**
 * @struct io_watcher
 * @brief I/O watcher for the event loop
 *
 * Monitors file descriptors for I/O readiness events (i.e., send or receive)
 */
struct io_watcher
{
   event_watcher_t event_watcher; /**< First member: Pointer to the event watcher in the loop */
   union
   {
      struct
      {
         int client_fd; /**< Main loop client file descriptor */
         int listen_fd; /**< Main loop accept (listen) file descriptor */
      } main;           /**< Struct that holds the file descriptors for the main loop */
      struct
      {
         int rcv_fd; /**< File descriptor for receiving messages */
         int snd_fd; /**< File descriptor for sending messages */
      } worker;      /**< Struct that holds the file descriptors for the worker */
      int __fds[2];
   } fds;                                  /**< Set of file descriptors used for I/O */
   bool ssl;                               /**< Indicates if SSL/TLS is used on this connection. */
   struct message* msg;                    /**< Per-watcher message buffer to avoid global state races */
   void (*cb)(struct io_watcher* watcher); /**< Event callback. */
};

/**
 * @struct signal_watcher
 * @brief Signal watcher for the event loop
 *
 * Monitors and handles specific signals received by the process
 */
struct signal_watcher
{
   event_watcher_t event_watcher; /**< First member. Pointer to the event watcher in the loop */
   int signum;                    /**< Signal number to watch for. */
   void (*cb)(void);              /**< Event callback. */
};

/**
 * @struct periodic_watcher
 * @brief Periodic timer watcher for the event loop
 *
 * Triggers callbacks at regular intervals specified in milliseconds
 */
struct periodic_watcher
{
   event_watcher_t event_watcher; /**< First member. Pointer to the event watcher in the loop */
#if HAVE_LINUX
   struct __kernel_timespec ts; /**< Timespec struct for io_uring loop. */
   int fd;                      /**< File descriptor for epoll-based periodic watcher. */
#else
   int interval; /**< Interval for kqueue timer. */
#endif               /* HAVE_LINUX */
   void (*cb)(void); /**< Event callback. */
};

/**
 * @struct event_loop
 * @brief Main event loop structure.
 *
 * Handles the execution and coordination of events using the specified
 * backend.
 */
struct event_loop
{
   atomic_bool running;                 /**< Flag indicating if the event loop is running. */
   sigset_t sigset;                     /**< Signal set used for handling signals in the event loop. */
   event_watcher_t* events[MAX_EVENTS]; /**< List of events */
   int events_nr;                       /**< Size of list of events */

   struct
   {
      struct io_uring_buf_ring* br; /**< Buffer ring used internally by io_uring */
      void* buf;                    /**< Pointer to the actual buffer being used */
      bool pending_send;            /**< A send is still pending */
      int cnt;                      /**< The number of buffers */
   } br;                            /**< The buffer ring struct */

#if HAVE_LINUX
   struct io_uring ring_rcv; /**< io_uring ring for receive operations */
   struct io_uring ring_snd; /**< io_uring ring for send operations (separate to avoid CQE mixing) */
   int bid;                  /**< Next buffer id */
#if EXPERIMENTAL_FEATURE_IOVECS
   /* XXX: Test with iovecs for send/recv io_uring */
   int iovecs_nr;
   struct iovec* iovecs;
#endif          /* EXPERIMENTAL_FEATURE_IOVECS */
   int epollfd; /**< File descriptor for the epoll instance (used with epoll backend). */
#else
   int kqueuefd; /**< File descriptor for the kqueue instance (used with kqueue backend). */
#endif           /* HAVE_LINUX */
   void* buffer; /**< Pointer to a buffer used to read in bytes. */
};

/**
 * Callbacks for each type of watcher
 */
typedef void (*io_cb)(struct io_watcher* watcher);
typedef void (*signal_cb)(void);
typedef void (*periodic_cb)(void);

/**
 * Initialize a new event loop
 * @param config Pointer to the configuration struct
 * @return Pointer to the initialized event loop
 */
struct event_loop*
pgagroal_event_loop_init(void);

/**
 * Start the main event loop
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int
pgagroal_event_loop_run(void);

/**
 * Break the event loop, stopping its execution
 * @param loop Pointer to the event loop struct
 */
void
pgagroal_event_loop_break(void);

/**
 * Destroy the event loop, freeing only the strictly necessary resources that
 * need to be freed.
 *
 * @param loop Pointer to the event loop struct
 * @return Return code
 */
int
pgagroal_event_loop_destroy(void);

/**
 * Closes the file descriptors used by the loop of the parent process.
 *
 * @param loop Pointer to the loop that should be freed by the child process
 * @return Return code
 */
int
pgagroal_event_loop_fork(void);

/**
 * Check if the event loop is currently running
 * @param loop Pointer to the event loop struct
 * @return True if the loop is running, false otherwise
 */
bool
pgagroal_event_loop_is_running(void);

/**
 * Initialize the watcher for accept event
 * @param w Pointer to the io event watcher struct
 * @param fd File descriptor being watched
 * @param cb Callback executed when event completes
 * @return Return code
 */
int
pgagroal_event_accept_init(struct io_watcher* watcher, int fd, io_cb cb);

/**
 * Initialize the watcher for receive events
 * @param watcher Pointer to the io event watcher struct
 * @param rcv_fd File descriptor being received
 * @param snd_fd File descriptor being send
 * @param cb Callback executed when event completes
 * @return Return code
 */
int
pgagroal_event_worker_init(struct io_watcher* watcher, int rcv_fd, int snd_fd, io_cb cb);

/**
 * Start the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int
pgagroal_io_start(struct io_watcher* watcher);

/**
 * Stop the watcher for an IO event in the event loop
 * @param loop Pointer to the event loop struct
 * @param w Pointer to the io event watcher struct
 * @return Return code
 */
int
pgagroal_io_stop(struct io_watcher* watcher);

/**
 * Initialize the watcher for periodic timeout events
 * @param watcher Pointer to the periodic event watcher struct
 * @param cb Callback executed on timeout
 * @param msec Interval in milliseconds for the periodic event
 * @return Return code
 */
int
pgagroal_periodic_init(struct periodic_watcher* watcher, periodic_cb cb, int msec);

/**
 * Start the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param watcher Pointer to the periodic event watcher struct
 * @return Return code
 */
int
pgagroal_periodic_start(struct periodic_watcher* watcher);

/**
 * Stop the watcher for a periodic timeout in the event loop
 * @param loop Pointer to the event loop struct
 * @param watcher Pointer to the periodic event watcher struct
 * @return Return code
 */
int
pgagroal_periodic_stop(struct periodic_watcher* watcher);

/**
 * Initialize the watcher for signal events
 * @param watcher Pointer to the signal event watcher struct
 * @param cb Callback executed when signal is received
 * @param signum Signal number to watch
 * @return Return code
 */
int
pgagroal_signal_init(struct signal_watcher* watcher, signal_cb cb, int signum);

/**
 * Start the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param watcher Pointer to the signal event watcher struct
 * @return Return code
 */
int
pgagroal_signal_start(struct signal_watcher* watcher);

/**
 * Stop the watcher for a signal in the event loop
 * @param loop Pointer to the event loop struct
 * @param watcher Pointer to the signal event watcher struct
 * @return Return code
 */
int
pgagroal_signal_stop(struct signal_watcher* watcher);

struct message;

/**
 * @brief Submit a send operation using io_uring.
 *
 * Prepares and submits an asynchronous send operation for the given message via io_uring.
 * The function sets up the submission queue entry (SQE), submits it, and waits for the completion.
 * The number of bytes sent is then returned.
 *
 * @param watcher Pointer to the I/O watcher structure.
 * @param msg Pointer to the message structure containing data to send.
 *
 * @return The number of bytes sent.
 */
int
pgagroal_event_prep_submit_send(struct io_watcher* watcher, struct message* msg);

/**
 * @brief Submit a send operation from outside the event loop using io_uring.
 *
 * Similar to pgagroal_event_prep_submit_send(), but intended for use outside of the event loop.
 * It prepares the send operation, submits it, waits for completion, and then marks the completion as seen.
 *
 * @param watcher Pointer to the I/O watcher structure.
 * @param msg Pointer to the message structure containing data to send.
 *
 * @return The number of bytes sent.
 */
int
pgagroal_event_prep_submit_send_outside_loop(struct io_watcher* watcher, struct message* msg);

/**
 * @brief Submit a receive operation from outside the event loop using io_uring.
 *
 * Similar to pgagroal_event_prep_submit_recv(), but intended for use outside of the event loop.
 *
 * @param watcher Pointer to the I/O watcher structure.
 * @param msg Pointer to the message structure where received data will be stored.
 *
 * @return The number of bytes received.
 */
int
pgagroal_event_prep_submit_recv_outside_loop(struct io_watcher* watcher, struct message* msg);

/**
 * @brief Wait for a receive operation to complete using io_uring.
 *
 * Blocks until a receive operation completes. Once complete, the number of bytes received
 * is returned and the completion event is marked as seen.
 *
 * @param watcher Pointer to the I/O watcher structure.
 * @param msg Pointer to the message structure to store the received data.
 *
 * @return The number of bytes received.
 */
int
pgagroal_wait_recv(void);

/**
 * Set the execution context for event loop initialization
 * @param context PGAGROAL_CONTEXT_MAIN or PGAGROAL_CONTEXT_VAULT
 */
void pgagroal_event_set_context(int context);

#ifdef __cplusplus
}
#endif

#endif /* EV_H */
