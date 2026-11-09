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
#include <logging.h>
#include <network.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <pool.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <tls.h>
#include <tracker.h>
#include <utils.h>
#include <configuration.h>

/* system */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

static int find_best_rule(char* username, char* database);
static bool remove_connection(char* username, char* database);
static void connection_details(int slot);
static bool do_prefill(char* username, char* database, int size);
static bool is_alias_of_limit(char* database, int limit_index);
static int get_connection_count_for_limit_rule(int rule_index, char* username);
static char* resolve_database_name(char* database, int best_rule);
static void check_graceful_shutdown_trigger(void);
static bool increase_connections(int best_rule);
static bool fetch_descriptor(int slot);
static bool holds_descriptor(int slot);

int pgagroal_known_fds[MAX_NUMBER_OF_CONNECTIONS];
long long pgagroal_fd_generation = 0;
int pgagroal_fd_base = -1;

static void
wait_queue_wakeup_handler(int signum)
{
   (void)signum;
}
static void
wait_queue_alarm_handler(int signum)
{
   (void)signum;
}

/* The wait queue wakes workers with SIGUSR2 and expires them with SIGALRM
 * (ITIMER_REAL). Both dispositions must be these no-op handlers for the
 * WHOLE lifetime of a worker process, not just while a wait is in progress:
 * a wake-up kill can legitimately arrive after the waiter has already left
 * its protected window (e.g. it was claimed between publishing its entry and
 * reaching sigsuspend, so it never suspended, or it self-woke on the
 * capacity re-check), and the default disposition of SIGUSR2 would terminate
 * a worker that is serving a client. Outside a wait, SIGUSR2/SIGALRM have no
 * other user in a worker (main's shutdown SIGALRM does not survive the
 * worker's own event loop setup), and a stray signal must never kill a
 * serving worker. Workers call pgagroal_wait_queue_signal_init() once at
 * startup; the dispositions are never restored afterwards. */
void
pgagroal_wait_queue_signal_init(void)
{
   struct sigaction sa;

   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = wait_queue_wakeup_handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   sigaction(SIGUSR2, &sa, NULL);

   sa.sa_handler = wait_queue_alarm_handler;
   sigaction(SIGALRM, &sa, NULL);
}

/* The wait queue deadline: wait_queue_timeout when configured, otherwise the
 * legacy blocking_timeout default. */
static pgagroal_time_t
wait_queue_effective_timeout(struct main_configuration* config)
{
   return pgagroal_time_is_valid(config->wait_queue_timeout) ? config->wait_queue_timeout : config->blocking_timeout;
}

static int
wait_queue_get_free_entry(struct wait_queue* wq)
{
   int hint = atomic_load(&wq->free_hint);
   for (int i = 0; i < wq->capacity; i++)
   {
      int idx = (hint + i) % wq->capacity;
      signed char expected = WAIT_QUEUE_ENTRY_FREE;
      /* FREE -> RESERVED: claim the slot without publishing it yet */
      if (atomic_compare_exchange_strong(&wq->entries[idx].state,
                                         &expected,
                                         WAIT_QUEUE_ENTRY_RESERVED))
      {
         atomic_store(&wq->free_hint, (idx + 1) % wq->capacity);
         return idx;
      }
   }
   return WAIT_QUEUE_NULL_INDEX;
}

static void
wait_queue_enqueue(struct wait_queue* wq, int idx, int best_rule, const char* username, const char* database)
{
   struct wait_queue_entry* entry = &wq->entries[idx];

   /* Initialize ALL fields BEFORE making the entry visible as WAITING */
   entry->pid = getpid();
   entry->best_rule = best_rule;
   memset(entry->username, 0, MAX_USERNAME_LENGTH);
   memcpy(entry->username, username, MIN(strnlen(username, MAX_USERNAME_LENGTH - 1), MAX_USERNAME_LENGTH - 1));
   memset(entry->database, 0, MAX_DATABASE_LENGTH);
   memcpy(entry->database, database, MIN(strnlen(database, MAX_DATABASE_LENGTH - 1), MAX_DATABASE_LENGTH - 1));
   entry->enqueue_time = time(NULL);
   entry->slot = -1;
   entry->prev = WAIT_QUEUE_NULL_INDEX;
   entry->next = WAIT_QUEUE_NULL_INDEX;

   /* Acquire lock */
   while (atomic_exchange(&wq->lock, 1) == 1)
   {
      SLEEP(1000000L);
   }

   /* Link at tail */
   entry->prev = wq->tail;
   if (wq->tail != WAIT_QUEUE_NULL_INDEX)
   {
      wq->entries[wq->tail].next = idx;
   }
   else
   {
      wq->head = idx;
   }
   wq->tail = idx;
   atomic_store(&wq->count, atomic_load(&wq->count) + 1);

   /* Publish: RESERVED -> WAITING. Only NOW can other processes see it. */
   atomic_store(&entry->state, WAIT_QUEUE_ENTRY_WAITING);
   atomic_store(&wq->lock, 0);

   struct main_configuration* config = (struct main_configuration*)shmem;
   pgagroal_log_debug("wait_queue: waiting (entry %d pid %d user %s database %s size %d max_wait %ds)",
                      idx, entry->pid, entry->username, entry->database,
                      atomic_load(&wq->count),
                      (int)pgagroal_time_convert(wait_queue_effective_timeout(config), FORMAT_TIME_S));
}

static pid_t
wait_queue_claim_oldest_waiter(struct wait_queue* wq, int queue_timeout_s, int rule)
{
   time_t now = time(NULL);
   pid_t timeout_pids[MAX_NUMBER_OF_CONNECTIONS];
   int timeout_count = 0;
   pid_t claimed_pid = -1;

   /* Acquire lock */
   while (atomic_exchange(&wq->lock, 1) == 1)
   {
      SLEEP(1000000L);
   }

   /* Phase 1: Clean up expired entries from the head (FIFO guarantees O(1) amortized) */
   int current_idx = wq->head;
   while (current_idx != WAIT_QUEUE_NULL_INDEX)
   {
      struct wait_queue_entry* current = &wq->entries[current_idx];
      double diff = difftime(now, current->enqueue_time);

      if (diff >= (double)queue_timeout_s)
      {
         int next_idx = current->next;

         /* Unlink */
         if (current->prev != WAIT_QUEUE_NULL_INDEX)
            wq->entries[current->prev].next = next_idx;
         else
            wq->head = next_idx;

         if (next_idx != WAIT_QUEUE_NULL_INDEX)
         {
            wq->entries[next_idx].prev = current->prev;
         }
         else
         {
            wq->tail = current->prev;
         }
         atomic_store(&wq->count, atomic_load(&wq->count) - 1);
         current->prev = WAIT_QUEUE_NULL_INDEX;
         current->next = WAIT_QUEUE_NULL_INDEX;

         /* WAITING -> TIMEOUT */
         signed char expected = WAIT_QUEUE_ENTRY_WAITING;
         if (atomic_compare_exchange_strong(&current->state,
                                            &expected,
                                            WAIT_QUEUE_ENTRY_TIMEOUT))
         {
            if (timeout_count < MAX_NUMBER_OF_CONNECTIONS)
            {
               timeout_pids[timeout_count++] = current->pid;
            }
         }
         current_idx = next_idx;
      }
      else
      {
         break; /* Not expired, and FIFO means nothing later is either */
      }
   }

   /* Phase 2: Claim the oldest waiter that can use the freed slot's limit
    * rule. The reuse scan in pgagroal_get_connection() only reuses a parked
    * slot when its limit_rule matches the acquirer's best_rule, so waking a
    * waiter of a different rule would only send it back to the queue tail;
    * scanning past non-matching entries keeps them queued in FIFO order. The
    * claim only dequeues the entry and marks it CLAIMED; the caller signals
    * the waiter after the returned connection has gone through the normal
    * parking/return path, and the waiter then re-runs the regular
    * acquisition logic in pgagroal_get_connection(). */
   {
      int scan_idx = wq->head;

      while (scan_idx != WAIT_QUEUE_NULL_INDEX)
      {
         struct wait_queue_entry* claimed = &wq->entries[scan_idx];
         signed char expected = WAIT_QUEUE_ENTRY_WAITING;

         if (claimed->best_rule == rule &&
             atomic_compare_exchange_strong(&claimed->state,
                                            &expected,
                                            WAIT_QUEUE_ENTRY_CLAIMED))
         {
            int next_idx = claimed->next;

            /* Unlink */
            if (claimed->prev != WAIT_QUEUE_NULL_INDEX)
               wq->entries[claimed->prev].next = next_idx;
            else
               wq->head = next_idx;

            if (next_idx != WAIT_QUEUE_NULL_INDEX)
            {
               wq->entries[next_idx].prev = claimed->prev;
            }
            else
            {
               wq->tail = claimed->prev;
            }
            atomic_store(&wq->count, atomic_load(&wq->count) - 1);
            claimed->prev = WAIT_QUEUE_NULL_INDEX;
            claimed->next = WAIT_QUEUE_NULL_INDEX;

            claimed_pid = claimed->pid;
            pgagroal_prometheus_wait_queue_handoff();
            pgagroal_log_debug("wait_queue: claimed (entry %d pid %d rule %d queue %d) on slot return",
                               scan_idx, claimed_pid, rule, atomic_load(&wq->count));
            break;
         }

         scan_idx = claimed->next;
      }
   }

   /* Release lock BEFORE sending signals */
   atomic_store(&wq->lock, 0);

   /* Signal timed-out workers outside the critical section */
   for (int i = 0; i < timeout_count; i++)
   {
      if (timeout_pids[i] > 0)
      {
         /* Use SIGALRM to wake them up so they can exit immediately */
         pgagroal_log_debug("wait_queue: timeout (pid %d after %ds)",
                            timeout_pids[i], queue_timeout_s);
         kill(timeout_pids[i], SIGALRM);
         pgagroal_prometheus_wait_queue_timeout();
      }
   }
   return claimed_pid;
}

static void
wait_queue_dequeue_if_present(struct wait_queue* wq, int idx)
{
   while (atomic_exchange(&wq->lock, 1) == 1)
   {
      SLEEP(1000000L);
   }

   struct wait_queue_entry* entry = &wq->entries[idx];

   /* Unlink whenever the entry is still linked, regardless of its state.
    * WAITING entries that lost the claim race and self-timed-out (TIMEOUT)
    * entries both belong to us here; a CLAIMED entry is always already
    * unlinked by the claimer. Deferring the unlink to the owner closes the
    * window where a freed entry could be recycled while still linked. */
   if (entry->prev != WAIT_QUEUE_NULL_INDEX || entry->next != WAIT_QUEUE_NULL_INDEX || wq->head == idx)
   {
      if (entry->prev != WAIT_QUEUE_NULL_INDEX)
      {
         wq->entries[entry->prev].next = entry->next;
      }
      else
      {
         wq->head = entry->next;
      }

      if (entry->next != WAIT_QUEUE_NULL_INDEX)
      {
         wq->entries[entry->next].prev = entry->prev;
      }
      else
      {
         wq->tail = entry->prev;
      }

      atomic_store(&wq->count, atomic_load(&wq->count) - 1);
      entry->prev = WAIT_QUEUE_NULL_INDEX;
      entry->next = WAIT_QUEUE_NULL_INDEX;
   }
   atomic_store(&wq->lock, 0);
}

int
pgagroal_get_connection(char* username, char* database, bool reuse, bool transaction_mode, int* slot, SSL** ssl)
{
   bool do_init;
   bool has_lock;
   int connections;
   signed char not_init;
   signed char free;
   int server;
   int fd;
   time_t start_time;
   int best_rule;
   int retries;
   long retry_delay;
   int ret;
   char* real_database;

   struct main_configuration* config;
   struct main_prometheus* prometheus;

   config = (struct main_configuration*)shmem;
   prometheus = (struct main_prometheus*)prometheus_shmem;

   pgagroal_prometheus_connection_get();

   best_rule = find_best_rule(username, database);
   retries = 0;
   retry_delay = 0; /* seeds the back-off at 1ms on the first blocking retry; persists across goto start */
   start_time = time(NULL);
   pgagroal_prometheus_connection_awaiting(best_rule);

start:

   *slot = -1;
   *ssl = NULL;
   do_init = false;
   has_lock = false;

   /* Fast-path bail when the pool is likely saturated. The counter is
    * read non-mutating; the slot array (below) is the source of truth.
    * Counter inflation cannot occur because we do not increment until
    * a slot has actually been acquired.
    */
   connections = atomic_load(&config->active_connections);
   if (connections >= config->max_connections)
   {
      goto retry;
   }

   if (reuse)
   {
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         free = STATE_FREE;

         if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_IN_USE))
         {
            bool can_reuse = false;

            // Check if same rule and username
            if (best_rule == config->connections[i].limit_rule &&
                pgagroal_strcmp((const char*)(&config->connections[i].username), username))
            {
               real_database = resolve_database_name(database, best_rule);
               // Check exact database match
               if (pgagroal_strcmp((const char*)(&config->connections[i].database), real_database))
               {
                  can_reuse = true;
               }
               // Check if both are aliases of the same real database
            }

            /* We don't hold the descriptor if main received it after we were forked */
            if (can_reuse && !transaction_mode && !holds_descriptor(i) && !fetch_descriptor(i))
            {
               can_reuse = false;
            }

            if (can_reuse)
            {
               if (increase_connections(best_rule))
               {
                  *slot = i;
                  has_lock = true;
               }
               else
               {
                  atomic_store(&config->states[i], STATE_FREE);
                  goto retry;
               }
            }
            else
            {
               atomic_store(&config->states[i], STATE_FREE);
            }
         }
      }
   }

   if (*slot == -1 && !transaction_mode)
   {
      if (best_rule >= 0)
      {
         /* Atomically reserve a backend against the per-rule max_size cap
          * before creating one. This mirrors the global max_connections gate
          * above (fetch-add, then test the prior value, rolling back on
          * overshoot) and replaces the previous count-then-create check, which
          * was a TOCTOU: concurrent acquirers each read a stale sub-cap count
          * and then each created a backend, driving the rule past max_size
          * (issue #848). The reservation is the serialization point, so live
          * backends for the rule never exceed max_size. */
         unsigned short reserved = atomic_fetch_add(&config->limits[best_rule].backend_connections, 1);
         if (reserved >= config->limits[best_rule].max_size)
         {
            atomic_fetch_sub(&config->limits[best_rule].backend_connections, 1);
            goto retry;
         }
      }

      /* Ok, try and create a new connection */
      for (int i = 0; *slot == -1 && i < config->max_connections; i++)
      {
         not_init = STATE_NOTINIT;

         if (atomic_compare_exchange_strong(&config->states[i], &not_init, STATE_INIT))
         {
            if (increase_connections(best_rule))
            {
               *slot = i;
               do_init = true;
               has_lock = true;
            }
            else
            {
               atomic_store(&config->states[i], STATE_NOTINIT);
               /* The global max_connections gate lost the race after we had
                * already reserved a backend against the per-rule max_size cap
                * (#848). Release that reservation here; the retry: label only
                * unwinds active_connections, so without this the reservation
                * would leak and throttle the rule below max_size permanently. */
               if (best_rule >= 0)
               {
                  atomic_fetch_sub(&config->limits[best_rule].backend_connections, 1);
               }
               goto retry;
            }
         }
      }

      if (*slot == -1 && best_rule >= 0)
      {
         /* Held a rule reservation but no NOTINIT slot remained (lost the race
          * for the last global slot); release it so the reservation is not
          * leaked, then fall through to the blocking-timeout retry path. */
         atomic_fetch_sub(&config->limits[best_rule].backend_connections, 1);
      }
   }

   if (*slot != -1)
   {
      config->connections[*slot].limit_rule = best_rule;
      config->connections[*slot].pid = getpid();

      if (do_init)
      {
         /* We need to find the server for the connection */
         if (pgagroal_get_primary(&server))
         {
            if (best_rule >= 0)
            {
               /* The backend was reserved but never established; release it. */
               atomic_fetch_sub(&config->limits[best_rule].backend_connections, 1);
            }
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            if (!fork())
            {
               pgagroal_flush(FLUSH_GRACEFULLY, "*");
               exit(0);
            }

            goto error;
         }

         pgagroal_log_debug("connect: server %d", server);

         if (config->servers[server].host[0] == '/')
         {
            char pgsql[MISC_LENGTH];

            memset(&pgsql, 0, sizeof(pgsql));
            pgagroal_snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
            ret = pgagroal_connect_unix_socket(config->servers[server].host, &pgsql[0], &fd);
         }
         else
         {
            ret = pgagroal_connect(config->servers[server].host, config->servers[server].port, &fd, config->keep_alive, config->nodelay);
         }

         if (ret)
         {
            pgagroal_log_error("pgagroal: No connection to %s:%d", config->servers[server].host, config->servers[server].port);
            if (best_rule >= 0)
            {
               /* The backend was reserved but never established; release it. */
               atomic_fetch_sub(&config->limits[best_rule].backend_connections, 1);
            }
            config->connections[*slot].limit_rule = -1;
            config->connections[*slot].pid = -1;
            atomic_store(&config->states[*slot], STATE_NOTINIT);

            pgagroal_prometheus_server_error(server);

            if (!fork())
            {
               pgagroal_flush_server(server);
            }

            if (config->failover)
            {
               pgagroal_server_force_failover(server);
               pgagroal_prometheus_failed_servers();
               goto retry;
            }

            goto error;
         }

         pgagroal_log_debug("connect: %s:%d using slot %d fd %d", config->servers[server].host, config->servers[server].port, *slot, fd);

         config->connections[*slot].server = server;

         memset(&config->connections[*slot].username, 0, MAX_USERNAME_LENGTH);
         memcpy(&config->connections[*slot].username, username, MIN(strlen(username), MAX_USERNAME_LENGTH - 1));
         real_database = resolve_database_name(database, best_rule);
         memset(&config->connections[*slot].database, 0, MAX_DATABASE_LENGTH);
         memcpy(&config->connections[*slot].database, real_database, MIN(strlen(real_database), MAX_DATABASE_LENGTH - 1));

         pgagroal_log_debug("Connection setup: client_db='%s' -> postgres_db='%s'", database, real_database);
         config->connections[*slot].has_security = SECURITY_INVALID;
         config->connections[*slot].fd = fd;

         atomic_store(&config->states[*slot], STATE_IN_USE);
      }
      else
      {
         bool kill = false;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[*slot].fd))
         {
            if (!transaction_mode)
            {
               kill = true;
            }
            else
            {
               atomic_store(&config->states[*slot], STATE_FREE);
               goto retry;
            }
         }

         if (!kill && config->validation == VALIDATION_FOREGROUND)
         {
            kill = !pgagroal_connection_isvalid(config->connections[*slot].fd);
         }

         if (kill)
         {
            int status;

            pgagroal_log_debug("pgagroal_get_connection: Slot %d FD %d - Error", *slot, config->connections[*slot].fd);
            pgagroal_tracking_event_slot(TRACKER_BAD_CONNECTION, *slot);
            status = pgagroal_kill_connection(*slot, *ssl);

            pgagroal_prefill_if_can(true, false);

            if (status == 0)
            {
               goto retry2;
            }
            else
            {
               goto timeout;
            }
         }
      }

      if (config->connections[*slot].start_time == -1)
      {
         config->connections[*slot].start_time = time(NULL);
      }

      config->connections[*slot].timestamp = time(NULL);

      if (config->common.metrics > 0)
      {
         atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
      }
      pgagroal_prometheus_connection_success();
      pgagroal_tracking_event_slot(TRACKER_GET_CONNECTION_SUCCESS, *slot);
      pgagroal_prometheus_connection_unawaiting(best_rule);
      return 0;
   }
   else
   {
retry:
      if (has_lock)
      {
         if (best_rule >= 0)
         {
            atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
         }
         atomic_fetch_sub(&config->active_connections, 1);
      }
retry2:
      if (pgagroal_time_is_valid(wait_queue_effective_timeout(config)))
      {
         struct wait_queue* wq = (struct wait_queue*)wait_queue_shmem;
         int timeout_s = (int)pgagroal_time_convert(wait_queue_effective_timeout(config), FORMAT_TIME_S);
         int entry_idx = WAIT_QUEUE_NULL_INDEX;

         /* The wait queue is a wake-up protocol, not a slot handoff: a return
          * claims the oldest waiter, completes its normal parking/return path,
          * then signals the waiter. On wake the waiter re-runs the normal
          * acquisition path above (slot/user/database matching, descriptor
          * fetch, security resume, counters), so no slot state is ever
          * transferred through the queue. */
         if (wq != NULL && difftime(time(NULL), start_time) < (double)timeout_s)
         {
            /* Re-validate the pool before queueing. The fast-path check that
             * brought us here read a stale snapshot: a concurrent return may
             * already have freed capacity (the return decrements
             * active_connections and publishes STATE_FREE before its claim
             * scan runs). Sleeping in the queue in that window means nobody
             * wakes us until the NEXT return, which under long-held
             * connections may be the full deadline away -- reported as
             * "connection pool is full" while slots are actually free.
             * Re-check and retry the acquisition instead, with the standard
             * back-off so a pool whose free slots are unusable for this
             * user/database (parked for someone else) does not spin hot. */
            if (atomic_load(&config->active_connections) < config->max_connections)
            {
               retry_delay = pgagroal_pool_next_retry_delay(retry_delay, config->connection_retry_delay);
               SLEEP(retry_delay);
               goto start;
            }
            entry_idx = wait_queue_get_free_entry(wq);
         }

         if (entry_idx != WAIT_QUEUE_NULL_INDEX)
         {
            /* SIGUSR2/SIGALRM dispositions are the permanent no-op handlers
             * installed by pgagroal_wait_queue_signal_init() at worker start;
             * only the blocking is per-wait here. This closes the race where
             * a wake-up kill arrives after the waiter has left its protected
             * window: a late SIGUSR2 is absorbed by the no-op handler instead
             * of killing a worker that is already serving a client. */

            /* Block both signals so we can control when they're delivered */
            sigset_t mask, old_mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGUSR2);
            sigaddset(&mask, SIGALRM);
            sigprocmask(SIG_BLOCK, &mask, &old_mask);

            wait_queue_enqueue(wq, entry_idx, best_rule, username, database);

            /* Watchdog cadence: re-validate the pool at the same interval the
             * blocking retry path polls at, instead of sleeping the whole
             * remaining deadline. Signals still deliver instant wake-ups; the
             * periodic re-check only catches what the protocol can miss
             * (capacity freed between our fast-path check and the enqueue, a
             * wake-up lost to a dead pid, or a capacity-freeing path that
             * does not claim). */
            double recheck_s = (double)config->connection_retry_delay / 1000.0;
            if (recheck_s < 0.001)
            {
               recheck_s = 0.001;
            }

            /* Wait loop: exit only on CLAIMED, self-recovery or TIMEOUT */
            signed char state;
            bool self_wake = false;
            while ((state = atomic_load(&wq->entries[entry_idx].state)) == WAIT_QUEUE_ENTRY_WAITING)
            {
               /* Check our own timeout first */
               double elapsed = difftime(time(NULL), wq->entries[entry_idx].enqueue_time);
               if (elapsed >= (double)timeout_s)
               {
                  /* Try to self-transition WAITING -> TIMEOUT.
                  * If it fails, the claimer's CAS already removed us. */
                  signed char expected = WAIT_QUEUE_ENTRY_WAITING;
                  if (atomic_compare_exchange_strong(&wq->entries[entry_idx].state,
                                                     &expected,
                                                     WAIT_QUEUE_ENTRY_TIMEOUT))
                  {
                     state = WAIT_QUEUE_ENTRY_TIMEOUT;
                     break;
                  }
                  /* Re-check state (we may now be CLAIMED) */
                  continue;
               }

               /* Watchdog: capacity is visible, so re-run the acquisition
                * path instead of sleeping on the queue. The claimer may have
                * already CAS'd us to CLAIMED (the return decrements the
                * counter before claiming); the CAS below loses that race and
                * the loop re-reads the state, exiting through the CLAIMED
                * path. */
               if (atomic_load(&config->active_connections) < config->max_connections)
               {
                  signed char expected = WAIT_QUEUE_ENTRY_WAITING;
                  if (atomic_compare_exchange_strong(&wq->entries[entry_idx].state,
                                                     &expected,
                                                     WAIT_QUEUE_ENTRY_TIMEOUT))
                  {
                     state = WAIT_QUEUE_ENTRY_TIMEOUT;
                     self_wake = true;
                     break;
                  }
                  continue;
               }

               /* Arm a one-shot alarm for the sooner of the remaining
                * deadline and the watchdog interval, so that a return worker
                * that dies between claiming us and signaling can never hang
                * us forever, and so the watchdog above runs periodically */
               double remaining = (double)timeout_s - elapsed;
               double nap = remaining < recheck_s ? remaining : recheck_s;
               struct itimerval itv;
               itv.it_interval.tv_sec = 0;
               itv.it_interval.tv_usec = 0;
               itv.it_value.tv_sec = (time_t)nap;
               itv.it_value.tv_usec = (suseconds_t)((nap - (time_t)nap) * 1000000.0);
               setitimer(ITIMER_REAL, &itv, NULL);

               /* Sleep until signaled. old_mask is our suspend mask: it does
                * not contain SIGUSR2/SIGALRM (blocked above), and every other
                * signal keeps its previous mask state, so SIGTERM/SIGINT and
                * friends keep working while we wait. */
               sigsuspend(&old_mask);
            }

            /* Cancel any pending alarm */
            struct itimerval itv_zero = {{0, 0}, {0, 0}};
            setitimer(ITIMER_REAL, &itv_zero, NULL);

            /* Unlink if still linked (lost the claim race or self-timed out),
            * then return the entry to the FREE pool */
            wait_queue_dequeue_if_present(wq, entry_idx);
            atomic_store(&wq->entries[entry_idx].state, WAIT_QUEUE_ENTRY_FREE);

            /* sigwait() consumes a pending signal without running any handler.
             * Standard (non-realtime) signals coalesce, so at most one SIGUSR2
             * and one SIGALRM can be pending here; sigwait() is only called
             * once sigpending() showed the signal, so it never blocks */
            {
               sigset_t drain_set;
               sigset_t pending_set;
               sigset_t one;
               int drained_sig;

               sigemptyset(&drain_set);
               sigaddset(&drain_set, SIGUSR2);
               sigaddset(&drain_set, SIGALRM);

               if (sigpending(&pending_set) == 0)
               {
                  if (sigismember(&pending_set, SIGUSR2))
                  {
                     sigemptyset(&one);
                     sigaddset(&one, SIGUSR2);
                     sigwait(&one, &drained_sig);
                  }
                  if (sigismember(&pending_set, SIGALRM))
                  {
                     sigemptyset(&one);
                     sigaddset(&one, SIGALRM);
                     sigwait(&one, &drained_sig);
                  }
               }
            }

            /* Restore the signal mask; the no-op dispositions stay installed */
            sigprocmask(SIG_SETMASK, &old_mask, NULL);

            if (state == WAIT_QUEUE_ENTRY_CLAIMED)
            {
               /* A return worker dequeued us and is (or was) completing the
                * normal return of a connection. Re-run the standard
                * acquisition logic above; if every slot is taken again we
                * simply re-enter the queue, bounded by the start_time
                * deadline checked at the top of this block. */
               pgagroal_log_debug("wait_queue: exit (entry %d pid %d state CLAIMED)", entry_idx, getpid());
               goto start;
            }

            if (self_wake)
            {
               /* The watchdog saw free capacity: leave the queue and re-run
                * the acquisition path immediately. This is not a timeout and
                * is not counted as one. */
               pgagroal_log_debug("wait_queue: exit (entry %d pid %d state SELF_WAKE)", entry_idx, getpid());
               goto start;
            }

            /* TIMEOUT (or unexpected state) */
            pgagroal_log_debug("wait_queue: exit (entry %d pid %d state %s)", entry_idx, getpid(), state == WAIT_QUEUE_ENTRY_TIMEOUT ? "TIMEOUT" : "unexpected");
            goto timeout;
         }
         else
         {
            /* FALLBACK: wait queue is full - use original spin-wait
             *
             * Back-off that doubles each retry (1ms, 2ms, 4ms, ... up to the
             * connection_retry_delay cap), in place of the former fixed 500ms poll.
             * The total wait is still bounded by the queue deadline
             * (wait_queue_timeout / blocking_timeout), re-checked below each retry (#813). */
            retry_delay = pgagroal_pool_next_retry_delay(retry_delay, config->connection_retry_delay);
            SLEEP(retry_delay);

            double diff = difftime(time(NULL), start_time);
            if (diff >= (double)timeout_s)
            {
               goto timeout;
            }

            if (best_rule == -1)
            {
               remove_connection(username, database);
            }
            goto start;
         }
      }
      else
      {
         /* FALLBACK: blocking_timeout disabled - original logic */
         if (!transaction_mode)
         {
            if (best_rule == -1)
            {
               if (remove_connection(username, database))
               {
                  if (retries < config->max_retries)
                  {
                     retries++;
                     goto start;
                  }
               }
            }
            else
            {
               if (retries < config->max_retries)
               {
                  retries++;
                  goto start;
               }
            }
         }
         else
         /* Sleep for 1000 nanos */
         {
            SLEEP_AND_GOTO(1000L, start)
         }
      }
   }

timeout:
   if (config->common.metrics > 0)
   {
      atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   }
   pgagroal_prometheus_connection_timeout();
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_TIMEOUT, username, database);
   pgagroal_prometheus_connection_unawaiting(best_rule);
   return 1;

error:
   if (best_rule >= 0)
   {
      atomic_fetch_sub(&config->limits[best_rule].active_connections, 1);
   }
   atomic_fetch_sub(&config->active_connections, 1);
   if (config->common.metrics > 0)
   {
      atomic_store(&prometheus->client_wait_time, difftime(time(NULL), start_time));
   }
   pgagroal_prometheus_connection_error();
   pgagroal_prometheus_connection_unawaiting(best_rule);
   pgagroal_tracking_event_basic(TRACKER_GET_CONNECTION_ERROR, username, database);

   return 2;
}

long
pgagroal_pool_next_retry_delay(long current_ns, int cap_ms)
{
   long cap_ns;
   long next;

   /* Keep the helper total even if handed an out-of-range cap; configuration
    * validation is the primary guard, this is defence in depth. */
   if (cap_ms < MIN_CONNECTION_RETRY_DELAY)
   {
      cap_ms = MIN_CONNECTION_RETRY_DELAY;
   }
   else if (cap_ms > MAX_CONNECTION_RETRY_DELAY)
   {
      cap_ms = MAX_CONNECTION_RETRY_DELAY;
   }

   cap_ns = (long)cap_ms * 1000000L;

   if (current_ns <= 0)
   {
      next = 1000000L; /* 1ms seed */
   }
   else
   {
      next = current_ns * 2;
   }

   if (next > cap_ns)
   {
      next = cap_ns;
   }

   return next;
}

int
pgagroal_return_connection(int slot, SSL* ssl, bool transaction_mode)
{
   int state;
   struct main_configuration* config;
   time_t now;
   signed char in_use;
   signed char age_check;
   int transfer_fd = -1;
   struct tls* t = NULL;
   bool tls_owned = false;

   config = (struct main_configuration*)shmem;

   /* Park a backend whose record layer we own: its TLS 1.3 state is serializable.
    * Confined to non-transaction mode -- only use_pooled_connection resumes it. */
   t = pgagroal_tls_from_ssl(ssl);
   tls_owned = (t != NULL && t->owned && !transaction_mode);

   /* Kill the connection, if it lives longer than max_connection_age */
   if (pgagroal_time_is_valid(config->max_connection_age))
   {
      now = time(NULL);
      in_use = STATE_IN_USE;
      age_check = STATE_MAX_CONNECTION_AGE;
      if (atomic_compare_exchange_strong(&config->states[slot], &in_use, age_check))
      {
         double age = difftime(now, config->connections[slot].start_time);
         if ((age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S) && !config->connections[slot].tx_mode) ||
             !atomic_compare_exchange_strong(&config->states[slot], &age_check, STATE_IN_USE))
         {
            pgagroal_prometheus_connection_max_connection_age();
            pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, slot);
            goto kill_connection;
         }
      }
   }

   /* Verify the socket for the slot */
   if (!transaction_mode && !pgagroal_socket_isvalid(config->connections[slot].fd))
   {
      pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d - Error", slot, config->connections[slot].fd);
      config->connections[slot].has_security = SECURITY_INVALID;
   }

   /* Can we cache this connection ? */
   if (config->connections[slot].has_security != SECURITY_INVALID &&
       (config->connections[slot].has_security != SECURITY_SCRAM256 ||
        (config->connections[slot].has_security == SECURITY_SCRAM256 &&
         (config->authquery || pgagroal_user_known(config->connections[slot].username)))) &&
       (ssl == NULL || tls_owned))
   {
      state = atomic_load(&config->states[slot]);

      /* Return the connection, if not GRACEFULLY */
      if (state == STATE_IN_USE)
      {
         pgagroal_log_debug("pgagroal_return_connection: Slot %d FD %d", slot, config->connections[slot].fd);

         if (!transaction_mode)
         {
            /* Session pooling: reset the backend before reuse.
             * Empty query or a prior ignore-mode failure disables the reset. */
            if (config->server_reset_query[0] != '\0' && !config->connections[slot].reset_query_failed)
            {
               if (pgagroal_write_reset_query(ssl, config->connections[slot].fd))
               {
                  switch (config->server_reset_query_behavior_on_failure)
                  {
                     case SERVER_RESET_QUERY_BEHAVIOR_ON_FAILURE_IGNORE:
                        pgagroal_log_warn("server_reset_query failed for slot %d (fd %d) — "
                                          "returning connection dirty. Future resets on this "
                                          "slot are suppressed. Fix server_reset_query and "
                                          "reload to re-enable.",
                                          slot, config->connections[slot].fd);
                        config->connections[slot].reset_query_failed = true;
                        break;

                     default:
                        /* SERVER_RESET_QUERY_ON_BEHAVIOR_FAILURE_DISCARD and _TRY both kill the
                         * connection; any unrecognised future value also falls here safely. */
                        goto kill_connection;
                  }
               }
            }
         }

         /* Park the serialized context for a later checkout to resume. Buffered
          * ciphertext would be lost on worker exit, so only park when rbuf is empty. */
         if (tls_owned)
         {
            if (t->rbuf_len != 0)
            {
               pgagroal_log_debug("pgagroal_return_connection: Slot %d cannot park (%zu bytes buffered)", slot, t->rbuf_len);
               goto kill_connection;
            }
            if (pgagroal_tls_record_export(&t->record, (unsigned char*)config->connections[slot].tls_context,
                                           sizeof(config->connections[slot].tls_context),
                                           &config->connections[slot].tls_context_length))
            {
               pgagroal_log_debug("pgagroal_return_connection: Slot %d TLS context export failed", slot);
               goto kill_connection;
            }
            pgagroal_log_debug("pgagroal_return_connection: Slot %d parked TLS context (%zu bytes)", slot, config->connections[slot].tls_context_length);
         }

         pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_SUCCESS, slot);

         config->connections[slot].timestamp = time(NULL);

         if (config->connections[slot].new)
         {
            /* The slot can't be reused until main has the descriptor */
            atomic_store(&config->connections[slot].generation, FD_GENERATION_PENDING);

            if (pgagroal_connection_get(&transfer_fd))
            {
               goto kill_connection;
            }

            if (pgagroal_connection_id_write(transfer_fd, CONNECTION_TRANSFER))
            {
               goto kill_connection;
            }

            if (pgagroal_connection_transfer_write(transfer_fd, slot))
            {
               goto kill_connection;
            }

            pgagroal_disconnect(transfer_fd);
            transfer_fd = -1;
         }

         if (pgagroal_connection_get(&transfer_fd))
         {
            goto kill_connection;
         }

         if (pgagroal_connection_id_write(transfer_fd, CONNECTION_RETURN))
         {
            goto kill_connection;
         }

         if (pgagroal_connection_slot_write(transfer_fd, slot))
         {
            goto kill_connection;
         }

         pgagroal_disconnect(transfer_fd);
         transfer_fd = -1;

         if (config->connections[slot].limit_rule >= 0)
         {
            atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
         }

         /* The rule of the parked slot, read before the slot becomes visible
          * as FREE: a racing acquirer can rewrite it the moment the state
          * store below lands. */
         int parked_rule = config->connections[slot].limit_rule;

         config->connections[slot].new = false;
         config->connections[slot].pid = -1;
         config->connections[slot].tx_mode = transaction_mode;
         memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));
         atomic_store(&config->states[slot], STATE_FREE);
         atomic_fetch_sub(&config->active_connections, 1);

         /* Wait queue wake-up: the slot is FREE now, so claim the oldest
          * waiter that can reuse it (same limit rule) and signal it. The
          * claim is issued only after the return has fully completed:
          * claiming earlier and waking only on this success path left an
          * orphan window where any failure between claim and wake (TLS
          * parking, the transfer hops) left the waiter dequeued, CLAIMED and
          * never signaled, sleeping until its own deadline. The wake-up is
          * still only a hint -- the waiter re-runs the normal acquisition
          * path in pgagroal_get_connection(); nothing is transferred through
          * the queue. If the wake-up is lost (e.g. the waiter exited),
          * nothing happens; the waiter's own watchdog or deadline, or the
          * next returning connection, covers it. */
         {
            struct wait_queue* wq = (struct wait_queue*)wait_queue_shmem;

            if (wq != NULL && !config->gracefully)
            {
               pid_t woken_pid = wait_queue_claim_oldest_waiter(
                  wq,
                  (int)pgagroal_time_convert(wait_queue_effective_timeout(config), FORMAT_TIME_S),
                  parked_rule);

               if (woken_pid > 0)
               {
                  pgagroal_log_debug("wait_queue: wake (slot %d pid %d)", slot, woken_pid);
                  kill(woken_pid, SIGUSR2);
               }
            }
         }

         pgagroal_log_debug("Connection returned: slot=%d, active_connections=%d, gracefully=%s",
                            slot, atomic_load(&config->active_connections), config->gracefully ? "true" : "false");

         // Check for graceful shutdown after successful connection return
         check_graceful_shutdown_trigger();

         pgagroal_prometheus_connection_return();

         return 0;
      }
      else if (state == STATE_GRACEFULLY)
      {
         pgagroal_write_terminate(ssl, config->connections[slot].fd);

         // Check for graceful shutdown after graceful connection termination
         check_graceful_shutdown_trigger();
      }
   }

kill_connection:

   pgagroal_disconnect(transfer_fd);

   pgagroal_tracking_event_slot(TRACKER_RETURN_CONNECTION_KILL, slot);

   return pgagroal_kill_connection(slot, ssl);
}

int
pgagroal_kill_connection(int slot, SSL* ssl)
{
   SSL_CTX* ctx;
   int ssl_shutdown;
   int result = 0;
   int fd;
   int transfer_fd;
   int killed_rule;
   bool freed_capacity;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_kill_connection: Slot %d FD %d State %d PID %d",
                      slot, config->connections[slot].fd, atomic_load(&config->states[slot]),
                      config->connections[slot].pid);

   pgagroal_tracking_event_slot(TRACKER_KILL_CONNECTION, slot);

   /* Read before the teardown below clears the fields: the rule the killed
    * backend belonged to, and whether the kill frees capacity at all (an
    * active checkout dropped, or a rule-bound pooled backend removed). */
   killed_rule = config->connections[slot].limit_rule;
   freed_capacity = (config->connections[slot].pid != -1 || killed_rule >= 0);

   fd = config->connections[slot].fd;
   if (fd != -1)
   {
      if (pgagroal_connection_get(&transfer_fd))
      {
         result = 1;
      }

      if (pgagroal_connection_id_write(transfer_fd, CONNECTION_KILL))
      {
         result = 1;
      }

      if (pgagroal_connection_slot_write(transfer_fd, slot))
      {
         result = 1;
      }

      /* Tells main which of its descriptors for the slot to close */
      if (pgagroal_connection_generation_write(transfer_fd, atomic_load(&config->connections[slot].generation)))
      {
         result = 1;
      }

      pgagroal_disconnect(transfer_fd);

      if (ssl != NULL)
      {
         struct tls* t = pgagroal_tls_from_ssl(ssl);

         ctx = SSL_get_SSL_CTX(ssl);

         if (t != NULL)
         {
            /* Socket-decoupled wrapper owns the SSL and its BIOs */
            pgagroal_tls_free(t);
            SSL_CTX_free(ctx);
         }
         else
         {
            ssl_shutdown = SSL_shutdown(ssl);
            if (ssl_shutdown == 0)
            {
               SSL_shutdown(ssl);
            }
            SSL_free(ssl);
            SSL_CTX_free(ctx);
         }
      }

      if (!pgagroal_socket_has_error(fd))
      {
         if (pgagroal_socket_isvalid(config->connections[slot].fd))
         {
            pgagroal_write_terminate(NULL, config->connections[slot].fd);
         }
         pgagroal_disconnect(fd);
      }
   }
   else
   {
      result = 1;
   }

   if (config->connections[slot].limit_rule >= 0)
   {
      /* Release the rule's hard-cap reservation (issue #848). Unlike the
       * per-rule active_connections below, backend_connections tracks live
       * backends rather than checkouts, so it must be released on every kill
       * of a rule-bound backend -- including idle/flush kills of pooled
       * connections, where pid == -1 and the checkout counter was already
       * decremented on return. */
      atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].backend_connections, 1);
   }

   if (config->connections[slot].pid != -1)
   {
      if (config->connections[slot].limit_rule >= 0)
      {
         atomic_fetch_sub(&config->limits[config->connections[slot].limit_rule].active_connections, 1);
      }

      atomic_fetch_sub(&config->active_connections, 1);

      // Check for graceful shutdown after killing connection
      check_graceful_shutdown_trigger();
   }

   memset(&config->connections[slot].username, 0, sizeof(config->connections[slot].username));
   memset(&config->connections[slot].database, 0, sizeof(config->connections[slot].database));
   memset(&config->connections[slot].appname, 0, sizeof(config->connections[slot].appname));

   config->connections[slot].new = true;
   config->connections[slot].server = -1;
   config->connections[slot].tx_mode = false;
   config->connections[slot].reset_query_failed = false;

   config->connections[slot].has_security = SECURITY_INVALID;
   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      config->connections[slot].security_lengths[i] = 0;
      memset(&config->connections[slot].security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   /* The parked TLS context holds session secrets; cleanse before recycling the slot. */
   pgagroal_cleanse(config->connections[slot].tls_context, sizeof(config->connections[slot].tls_context));
   config->connections[slot].tls_context_length = 0;

   config->connections[slot].backend_pid = 0;
   config->connections[slot].backend_secret = 0;

   config->connections[slot].limit_rule = -1;
   config->connections[slot].start_time = -1;
   config->connections[slot].timestamp = -1;
   config->connections[slot].fd = -1;
   config->connections[slot].pid = -1;
   atomic_store(&config->connections[slot].generation, 0);

   atomic_store(&config->states[slot], STATE_NOTINIT);

   /* Wait queue wake-up: destroying a backend frees capacity -- an active
    * checkout dropped, or a rule-bound pooled backend removed, and the slot
    * is NOTINIT so any waiter can build a fresh connection in it. Without
    * this, the idle-timeout, max-connection-age, validation, flush and
    * CLI-kill paths never wake queued waiters: with a saturated pool they
    * sleep to their full deadline even though capacity became available. The
    * claim is issued after the slot teardown above, so the woken waiter
    * races for a slot that is actually available. */
   if (freed_capacity && !config->gracefully)
   {
      struct wait_queue* wq = (struct wait_queue*)wait_queue_shmem;

      if (wq != NULL)
      {
         pid_t woken_pid = wait_queue_claim_oldest_waiter(
            wq,
            (int)pgagroal_time_convert(wait_queue_effective_timeout(config), FORMAT_TIME_S),
            killed_rule);

         if (woken_pid > 0)
         {
            pgagroal_log_debug("wait_queue: wake (kill slot %d pid %d)", slot, woken_pid);
            kill(woken_pid, SIGUSR2);
         }
      }
   }

   pgagroal_prometheus_connection_kill();

   return result;
}

void
pgagroal_pool_close_inherited(int server_fd)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /* connections[i].fd can be another process' descriptor number, so only
    * close what main held when this process was forked */
   for (int i = 0; i < config->max_connections; i++)
   {
      if (pgagroal_known_fds[i] > 0 && pgagroal_known_fds[i] != server_fd)
      {
         pgagroal_disconnect(pgagroal_known_fds[i]);
      }
      pgagroal_known_fds[i] = 0;
   }
}

void
pgagroal_idle_timeout(void)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char idle_check;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);
   prefill = false;

   pgagroal_log_debug("pgagroal_idle_timeout");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      idle_check = STATE_IDLE_CHECK;

      if (atomic_compare_exchange_strong(&config->states[i], &free, idle_check))
      {
         if (!holds_descriptor(i))
         {
            atomic_store(&config->states[i], STATE_FREE);
            continue;
         }

         double diff = difftime(now, config->connections[i].timestamp);
         if (diff >= (double)pgagroal_time_convert(config->idle_timeout, FORMAT_TIME_S) && !config->connections[i].tx_mode)
         {
            pgagroal_prometheus_connection_idletimeout();
            pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &idle_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_idletimeout();
               pgagroal_tracking_event_slot(TRACKER_IDLE_TIMEOUT, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_max_connection_age(void)
{
   bool prefill;
   time_t now;
   signed char free;
   signed char age_check;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);
   prefill = false;

   pgagroal_log_debug("pgagroal_max_connection_age");

   /* Here we run backwards in order to keep hot connections in the beginning */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      age_check = STATE_MAX_CONNECTION_AGE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, age_check))
      {
         if (!holds_descriptor(i))
         {
            atomic_store(&config->states[i], STATE_FREE);
            continue;
         }

         double age = difftime(now, config->connections[i].start_time);
         if (age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S) && !config->connections[i].tx_mode)
         {
            pgagroal_prometheus_connection_max_connection_age();
            pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &age_check, STATE_FREE))
            {
               pgagroal_prometheus_connection_max_connection_age();
               pgagroal_tracking_event_slot(TRACKER_MAX_CONNECTION_AGE, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_validation(void)
{
   bool prefill = true;
   time_t now;
   signed char free;
   signed char validation;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;
   now = time(NULL);

   pgagroal_log_debug("pgagroal_validation");

   /* We run backwards */
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      validation = STATE_VALIDATION;

      if (atomic_compare_exchange_strong(&config->states[i], &free, validation))
      {
         if (!holds_descriptor(i))
         {
            atomic_store(&config->states[i], STATE_FREE);
            continue;
         }

         bool kill = false;
         double diff, age;

         /* Verify the socket for the slot */
         if (!pgagroal_socket_isvalid(config->connections[i].fd))
         {
            kill = true;
         }

         /* While we have the connection in validation may as well check for idle_timeout */
         if (!kill && pgagroal_time_is_valid(config->idle_timeout))
         {
            diff = difftime(now, config->connections[i].timestamp);
            if (diff >= (double)pgagroal_time_convert(config->idle_timeout, FORMAT_TIME_S))
            {
               kill = true;
            }
         }

         /* Also check for max_connection_age */
         if (!kill && pgagroal_time_is_valid(config->max_connection_age))
         {
            age = difftime(now, config->connections[i].start_time);
            if (age >= (double)pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S))
            {
               kill = true;
            }
         }

         /* Ok, send SELECT 1 */
         if (!kill)
         {
            kill = !pgagroal_connection_isvalid(config->connections[i].fd);
         }

         if (kill)
         {
            pgagroal_prometheus_connection_invalid();
            pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
            prefill = true;
         }
         else
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &validation, STATE_FREE))
            {
               pgagroal_prometheus_connection_invalid();
               pgagroal_tracking_event_slot(TRACKER_INVALID_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_flush(int mode, char* database)
{
   bool prefill;
   signed char free;
   signed char in_use;
   bool do_kill;
   signed char server_state;
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   prefill = false;

   pgagroal_log_debug("pgagroal_flush");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      in_use = STATE_IN_USE;
      do_kill = false;

      if (config->connections[i].server != -1)
      {
         server_state = atomic_load(&config->servers[config->connections[i].server].state);
         if (server_state == SERVER_FAILED)
         {
            do_kill = true;
         }
      }

      if (!do_kill)
      {
         bool consider = false;

         if (pgagroal_strcmp(database, "all") || pgagroal_strcmp(config->connections[i].database, database))
         {
            consider = true;
         }

         if (consider)
         {
            if (atomic_compare_exchange_strong(&config->states[i], &free, STATE_FLUSH))
            {
               if (!holds_descriptor(i))
               {
                  atomic_store(&config->states[i], STATE_FREE);
                  continue;
               }

               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
            }
            else if (mode == FLUSH_ALL || mode == FLUSH_GRACEFULLY)
            {
               if (atomic_compare_exchange_strong(&config->states[i], &in_use, STATE_FLUSH))
               {
                  if (mode == FLUSH_ALL)
                  {
                     kill(config->connections[i].pid, SIGQUIT);
                     pgagroal_prometheus_connection_flush();
                     pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
                     pgagroal_kill_connection(i, NULL);
                     prefill = true;
                  }
                  else if (mode == FLUSH_GRACEFULLY)
                  {
                     atomic_store(&config->states[i], STATE_GRACEFULLY);
                  }
               }
               else if (mode == FLUSH_ALL)
               {
                  signed char graceful = STATE_GRACEFULLY;
                  if (atomic_compare_exchange_strong(&config->states[i], &graceful, STATE_FLUSH))
                  {
                     kill(config->connections[i].pid, SIGQUIT);
                     pgagroal_prometheus_connection_flush();
                     pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
                     pgagroal_kill_connection(i, NULL);
                     prefill = true;
                  }
               }
            }
         }
      }
      else
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               if (!holds_descriptor(i))
               {
                  break;
               }
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               prefill = true;
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_MAX_CONNECTION_AGE:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (prefill)
   {
      pgagroal_prefill_if_can(true, false);
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();
}

void
pgagroal_flush_server(signed char server)
{
   struct main_configuration* config;
   int primary = -1;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_flush_server %s", config->servers[server].name);
   for (int i = 0; i < config->max_connections; i++)
   {
      if (config->connections[i].server == server)
      {
         switch (atomic_load(&config->states[i]))
         {
            case STATE_NOTINIT:
            case STATE_INIT:
               /* Do nothing */
               break;
            case STATE_FREE:
               if (!holds_descriptor(i))
               {
                  break;
               }
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               if (pgagroal_socket_isvalid(config->connections[i].fd))
               {
                  pgagroal_write_terminate(NULL, config->connections[i].fd);
               }
               pgagroal_prometheus_connection_flush();
               pgagroal_tracking_event_slot(TRACKER_FLUSH, i);
               pgagroal_kill_connection(i, NULL);
               break;
            case STATE_IN_USE:
            case STATE_GRACEFULLY:
            case STATE_FLUSH:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            case STATE_IDLE_CHECK:
            case STATE_MAX_CONNECTION_AGE:
            case STATE_VALIDATION:
            case STATE_REMOVE:
               atomic_store(&config->states[i], STATE_GRACEFULLY);
               break;
            default:
               break;
         }
      }
   }

   if (pgagroal_get_primary(&primary))
   {
      pgagroal_log_debug("No primary defined");
   }
   else
   {
      if (server != (unsigned char)primary && primary != -1)
      {
         pgagroal_prefill_if_can(true, true);
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

void
pgagroal_request_flush(SSL* ssl __attribute__((unused)), int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   time_t start_time;
   time_t end_time;
   struct json* req = NULL;
   int mode = 0;
   char* database = NULL;

   start_time = time(NULL);

   req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   mode = (int)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_MODE);
   database = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_DATABASE);

   if (database != NULL)
   {
      pgagroal_flush(mode, database);
   }
   else
   {
      pgagroal_flush(mode, "*");
   }

   end_time = time(NULL);

   pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);

   pgagroal_json_destroy(payload);

   exit(0);
}

void
pgagroal_prefill(bool initial)
{
   struct main_configuration* config;

   pgagroal_start_logging();
   pgagroal_memory_init();

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_prefill");

   for (int i = 0; i < config->number_of_limits; i++)
   {
      int size;

      if (initial)
      {
         size = config->limits[i].initial_size;
      }
      else
      {
         size = config->limits[i].min_size;
      }

      if (size > 0)
      {
         // do not perform prefill if
         // - the database is a reserved word
         // - the username is reserved
         if (pgagroal_is_database_reserved(config->limits[i].database) || pgagroal_is_username_reserved(config->limits[i].username))
         {
            pgagroal_log_warn("Cannot perform prefill for database '%s' user '%s' at limit entry %d (row %d)",
                              config->limits[i].database,
                              config->limits[i].username,
                              i + 1,
                              config->limits[i].lineno);
            continue;
         }

         if (!pgagroal_strcmp("all", config->limits[i].database) && !pgagroal_strcmp("all", config->limits[i].username))
         {
            int user = -1;

            for (int j = 0; j < config->number_of_users && user == -1; j++)
            {
               if (pgagroal_strcmp(config->limits[i].username, config->users[j].username))
               {
                  user = j;
               }
            }

            if (user != -1)
            {
               while (do_prefill(config->users[user].username, config->limits[i].database, size))
               {
                  int32_t slot = -1;
                  SSL* ssl = NULL;

                  if (pgagroal_prefill_auth(config->users[user].username, config->users[user].password,
                                            config->limits[i].database, &slot, &ssl) != AUTH_SUCCESS)
                  {
                     pgagroal_log_warn("Invalid data for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);

                     if (slot != -1)
                     {
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                     }

                     break;
                  }

                  if (slot != -1)
                  {
                     if (config->connections[slot].has_security != SECURITY_INVALID)
                     {
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_RETURN, slot);
                        pgagroal_return_connection(slot, ssl, false);
                     }
                     else
                     {
                        pgagroal_log_warn("Unsupported security model during prefill for user '%s' using limit entry (%d)", config->limits[i].username, i + 1);
                        if (config->connections[slot].fd != -1)
                        {
                           if (pgagroal_socket_isvalid(config->connections[slot].fd))
                           {
                              pgagroal_write_terminate(NULL, config->connections[slot].fd);
                           }
                        }
                        pgagroal_tracking_event_slot(TRACKER_PREFILL_KILL, slot);
                        pgagroal_kill_connection(slot, ssl);
                        break;
                     }
                  }
               }
            }
            else
            {
               pgagroal_log_warn("Unknown user '%s' for limit entry (%d)", config->limits[i].username, i + 1);
            }
         }
         else
         {
            pgagroal_log_warn("Limit entry %d with invalid definition at row %d",
                              i + 1,
                              config->limits[i].lineno);
         }
      }
   }

   pgagroal_pool_status();
   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(0);
}

int
pgagroal_pool_init(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /* States */
   for (int i = 0; i < MAX_NUMBER_OF_CONNECTIONS; i++)
   {
      atomic_init(&config->states[i], STATE_NOTINIT);
   }

   /* Connections */
   for (int i = 0; i < config->max_connections; i++)
   {
      config->connections[i].new = true;
      config->connections[i].tx_mode = false;
      config->connections[i].reset_query_failed = false;
      config->connections[i].server = -1;
      config->connections[i].has_security = SECURITY_INVALID;
      config->connections[i].limit_rule = -1;
      config->connections[i].start_time = -1;
      config->connections[i].timestamp = -1;
      config->connections[i].fd = -1;
      config->connections[i].pid = -1;
      atomic_store(&config->connections[i].generation, 0);
   }

   return 0;
}

int
pgagroal_pool_shutdown(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->max_connections; i++)
   {
      int state = atomic_load(&config->states[i]);

      if (state != STATE_NOTINIT)
      {
         if (state == STATE_FREE)
         {
            if (pgagroal_socket_isvalid(config->connections[i].fd))
            {
               pgagroal_write_terminate(NULL, config->connections[i].fd);
            }
         }
         pgagroal_disconnect(config->connections[i].fd);

         if (config->connections[i].pid != -1)
         {
            kill(config->connections[i].pid, SIGQUIT);
         }

         atomic_store(&config->states[i], STATE_NOTINIT);
      }
   }

   return 0;
}

int
pgagroal_pool_status(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal_pool_status: %d/%d", atomic_load(&config->active_connections), config->max_connections);

   for (int i = 0; i < config->max_connections; i++)
   {
      connection_details(i);
   }

#ifdef DEBUG
   assert(atomic_load(&config->active_connections) <= config->max_connections);
#endif

   return 0;
}

static int
find_best_rule(char* username, char* database)
{
   int best_rule;
   struct main_configuration* config;

   best_rule = -1;
   config = (struct main_configuration*)shmem;

   if (config->number_of_limits > 0)
   {
      for (int i = 0; i < config->number_of_limits; i++)
      {
         bool database_match = false;

         // Check exact database name match or "all"
         if (pgagroal_strcmp("all", config->limits[i].database) || pgagroal_strcmp(database, config->limits[i].database))
         {
            database_match = true;
         }
         else
         {
            // Check if database matches any alias for this limit entry
            for (int j = 0; j < config->limits[i].aliases_count; j++)
            {
               if (pgagroal_strcmp(database, config->limits[i].aliases[j]))
               {
                  database_match = true;
                  pgagroal_log_debug("Database '%s' matched alias '%s' for rule %d",
                                     database, config->limits[i].aliases[j], i);
                  break;
               }
            }
         }

         // Check username match
         bool username_match = (pgagroal_strcmp("all", config->limits[i].username) || pgagroal_strcmp(username, config->limits[i].username));

         /* There is a match */
         if (username_match && database_match)
         {
            if (best_rule == -1)
            {
               best_rule = i;
            }
            else
            {
               if (pgagroal_strcmp(username, config->limits[best_rule].username) &&
                   (pgagroal_strcmp(database, config->limits[best_rule].database) ||
                    is_alias_of_limit(database, best_rule)))
               {
                  /* We have a precise rule already */
               }
               else if (pgagroal_strcmp("all", config->limits[best_rule].username))
               {
                  /* User is better */
                  if (!pgagroal_strcmp("all", config->limits[i].username))
                  {
                     best_rule = i;
                  }
               }
               else if (pgagroal_strcmp("all", config->limits[best_rule].database))
               {
                  /* Database is better */
                  if (!pgagroal_strcmp("all", config->limits[i].database))
                  {
                     best_rule = i;
                  }
               }
            }
         }
      }
   }

   if (best_rule != -1)
   {
      pgagroal_log_debug("find_best_rule: user=%s, database=%s -> rule %d (real_database=%s)",
                         username, database, best_rule, config->limits[best_rule].database);
   }

   return best_rule;
}

static bool
is_alias_of_limit(char* database, int limit_index)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (limit_index < 0 || limit_index >= config->number_of_limits)
   {
      return false;
   }

   for (int i = 0; i < config->limits[limit_index].aliases_count; i++)
   {
      if (pgagroal_strcmp(database, config->limits[limit_index].aliases[i]))
      {
         return true;
      }
   }

   return false;
}

static bool
increase_connections(int best_rule)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int connections;

   connections = atomic_fetch_add(&config->active_connections, 1);
   if (connections >= config->max_connections)
   {
      /* Cap race: another thread acquired between the atomic_load fast-path
       * bail in pgagroal_get_connection() and this fetch_add. Roll the
       * global counter back so it does not drift upward as a metric. The
       * post-decrement value is not claimed to be below max_connections
       * (another thread may increment in between); the actual cap is
       * enforced by the slot array (config->states[] has exactly
       * max_connections entries), so a missed rollback at worst leaves
       * the metric off by N rather than producing more backends than the
       * cap.
       */
      pgagroal_log_debug("increase_connections: cap race (active=%d, max=%d); rolling back counter",
                         connections + 1, config->max_connections);
      atomic_fetch_sub(&config->active_connections, 1);
      return false;
   }

   if (best_rule >= 0)
   {
      atomic_fetch_add(&config->limits[best_rule].active_connections, 1);
   }

   return true;
}

static int
get_connection_count_for_limit_rule(int rule_index, char* username)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int count = 0;

   if (rule_index < 0 || rule_index >= config->number_of_limits)
   {
      return 0;
   }

   for (int i = 0; i < config->max_connections; i++)
   {
      if (atomic_load(&config->states[i]) != STATE_NOTINIT &&
          pgagroal_strcmp((const char*)(&config->connections[i].username), username))
      {
         // Check if this connection is for the main database name
         if (pgagroal_strcmp((const char*)(&config->connections[i].database), config->limits[rule_index].database))
         {
            count++;
         }
         else
         {
            // Check if this connection is for any alias of this database
            for (int j = 0; j < config->limits[rule_index].aliases_count; j++)
            {
               if (pgagroal_strcmp((const char*)(&config->connections[i].database), config->limits[rule_index].aliases[j]))
               {
                  count++;
                  break;
               }
            }
         }
      }
   }

   pgagroal_log_debug("Connection count for rule %d (user=%s): %d connections",
                      rule_index, username, count);

   return count;
}

static bool
remove_connection(char* username, char* database)
{
   signed char free;
   signed char remove;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_trace("remove_connection");
   for (int i = config->max_connections - 1; i >= 0; i--)
   {
      free = STATE_FREE;
      remove = STATE_REMOVE;

      if (atomic_compare_exchange_strong(&config->states[i], &free, remove))
      {
         if (!holds_descriptor(i))
         {
            atomic_store(&config->states[i], STATE_FREE);
            continue;
         }

         if (pgagroal_strcmp(username, config->connections[i].username) && pgagroal_strcmp(database, config->connections[i].database))
         {
            if (!atomic_compare_exchange_strong(&config->states[i], &remove, STATE_FREE))
            {
               pgagroal_prometheus_connection_remove();
               pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
               pgagroal_kill_connection(i, NULL);
            }
         }
         else
         {
            pgagroal_prometheus_connection_remove();
            pgagroal_tracking_event_slot(TRACKER_REMOVE_CONNECTION, i);
            pgagroal_kill_connection(i, NULL);
         }

         return true;
      }
   }

   return false;
}

static void
check_graceful_shutdown_trigger(void)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   int remaining_connections = atomic_load(&config->active_connections);

   if (remaining_connections == 0 && config->gracefully)
   {
      if (config->keep_running)
      {
         pgagroal_log_debug("pgagroal: graceful shutdown conditions met - connections=0, gracefully=true, keep_running=true - proceeding with shutdown");
      }
      else
      {
         pgagroal_log_debug("pgagroal: graceful shutdown skipped - connections=0, gracefully=true, but keep_running=false (race condition prevented)");
         return;
      }
   }

   if (remaining_connections == 0 && config->gracefully && config->keep_running)
   {
      // Double-check to handle race conditions with new connections
      usleep(1000); // 1ms delay to let any concurrent operations complete

      // Verify all three conditions are still met after brief delay
      int final_connections = atomic_load(&config->active_connections);
      if (final_connections == 0 && config->gracefully && config->keep_running)
      {
         pgagroal_log_debug("pgagroal: graceful shutdown confirmed - triggering shutdown");

         pid_t parent_pid = getppid();
         if (kill(parent_pid, SIGTRAP) == -1)
         {
            if (errno == ESRCH)
            {
               pgagroal_log_debug("Parent process already exited, worker shutting down");
            }
            else
            {
               pgagroal_log_debug("Failed to signal parent process (PID: %d): %s", parent_pid, strerror(errno));
            }
         }
         else
         {
            pgagroal_log_debug("Graceful shutdown signal sent to parent process (PID: %d)", parent_pid);
         }
      }
      else
      {
         int final_connections_check = atomic_load(&config->active_connections);
         pgagroal_log_debug("Graceful shutdown conditions - connections=%d, gracefully=%s, keep_running=%s",
                            final_connections_check,
                            config->gracefully ? "true" : "false",
                            config->keep_running ? "true" : "false");
      }
   }
}

static void
connection_details(int slot)
{
   int state;
   char time_buf[32];
   char start_buf[32];
   struct main_configuration* config;
   struct connection connection;

   config = (struct main_configuration*)shmem;

   connection = config->connections[slot];
   state = atomic_load(&config->states[slot]);

   memset(&time_buf, 0, sizeof(time_buf));
   ctime_r(&(connection.timestamp), &time_buf[0]);
   time_buf[strlen(time_buf) - 1] = 0;

   memset(&start_buf, 0, sizeof(start_buf));
   ctime_r(&(connection.start_time), &start_buf[0]);
   start_buf[strlen(start_buf) - 1] = 0;

   switch (state)
   {
      case STATE_NOTINIT:
         pgagroal_log_debug("pgagroal_pool_status: State: NOTINIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_INIT:
         pgagroal_log_debug("pgagroal_pool_status: State: INIT");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         break;
      case STATE_FREE:
         pgagroal_log_debug("pgagroal_pool_status: State: FREE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_IN_USE:
         pgagroal_log_debug("pgagroal_pool_status: State: IN_USE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_GRACEFULLY:
         pgagroal_log_debug("pgagroal_pool_status: State: GRACEFULLY");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_FLUSH:
         pgagroal_log_debug("pgagroal_pool_status: State: FLUSH");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_IDLE_CHECK:
         pgagroal_log_debug("pgagroal_pool_status: State: IDLE CHECK");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_MAX_CONNECTION_AGE:
         pgagroal_log_debug("pgagroal_pool_status: State: MAX CONNECTION AGE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_VALIDATION:
         pgagroal_log_debug("pgagroal_pool_status: State: VALIDATION");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      case STATE_REMOVE:
         pgagroal_log_debug("pgagroal_pool_status: State: REMOVE");
         pgagroal_log_debug("                      Slot: %d", slot);
         pgagroal_log_debug("                      Server: %d", connection.server);
         pgagroal_log_debug("                      User: %s", connection.username);
         pgagroal_log_debug("                      Database: %s", connection.database);
         pgagroal_log_debug("                      AppName: %s", connection.appname);
         pgagroal_log_debug("                      Rule: %d", connection.limit_rule);
         pgagroal_log_debug("                      Start: %s", &start_buf[0]);
         pgagroal_log_debug("                      Time: %s", &time_buf[0]);
         pgagroal_log_debug("                      FD: %d", connection.fd);
         pgagroal_log_trace("                      PID: %d", connection.pid);
         pgagroal_log_trace("                      Auth: %d", connection.has_security);
         for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
         {
            pgagroal_log_trace("                      Size: %zd", connection.security_lengths[i]);
            pgagroal_log_mem(&connection.security_messages[i], connection.security_lengths[i]);
         }
         pgagroal_log_trace("                      Backend PID: %d", connection.backend_pid);
#ifdef DEBUG
         pgagroal_log_trace("                      Backend Secret: %d", connection.backend_secret);
#endif
         break;
      default:
         pgagroal_log_debug("pgagroal_pool_status: State %d Slot %d FD %d", state, slot, connection.fd);
         break;
   }
}

static bool
do_prefill(char* username, char* database, int size)
{
   signed char state;
   int free = 0;
   int connections = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   //Find the limit rule for this database/username combination
   int rule_index = find_best_rule(username, database);
   if (rule_index != -1)
   {
      // Count connections for this rule (including aliases)
      connections = get_connection_count_for_limit_rule(rule_index, username);
   }
   else
   {
      // Fallback to old logic if no rule found
      for (int i = 0; i < config->max_connections; i++)
      {
         if (pgagroal_strcmp((const char*)(&config->connections[i].username), username) &&
             pgagroal_strcmp((const char*)(&config->connections[i].database), database))
         {
            connections++;
         }
      }
   }

   // Count free connections
   for (int i = 0; i < config->max_connections; i++)
   {
      state = atomic_load(&config->states[i]);
      if (state == STATE_NOTINIT)
      {
         free++;
      }
   }

   pgagroal_log_debug("do_prefill: user=%s, database=%s, current=%d, target=%d, free=%d",
                      username, database, connections, size, free);

   return connections < size && free > 0;
}

void
pgagroal_prefill_if_can(bool do_fork, bool initial)
{
   int primary;

   if (pgagroal_can_prefill())
   {
      if (pgagroal_get_primary(&primary))
      {
         pgagroal_log_warn("No primary detected, cannot try to prefill!");
         return;
      }

      if (do_fork)
      {
         if (!fork())
         {
            pgagroal_prefill(initial);
         }
      }
      else
      {
         pgagroal_prefill(initial);
      }
   }
}

static char*
resolve_database_name(char* database, int best_rule)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (best_rule < 0 || best_rule >= config->number_of_limits)
   {
      return database; // Return original if no rule found
   }

   // Check if this database name is an alias
   for (int j = 0; j < config->limits[best_rule].aliases_count; j++)
   {
      if (pgagroal_strcmp(database, config->limits[best_rule].aliases[j]))
      {
         return config->limits[best_rule].database; // Return real database name
      }
   }

   // Not an alias, return original name
   return database;
}

static bool
fetch_descriptor(int slot)
{
   int transfer_fd = -1;
   int32_t s = -1;
   int fd = -1;
   int target;
   long long expected;
   long long generation = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   target = config->connections[slot].fd;
   expected = atomic_load(&config->connections[slot].generation);

   if (target < 0 || expected == FD_GENERATION_PENDING)
   {
      goto error;
   }

   /* Ask main for its descriptor of the slot */
   if (pgagroal_connection_get(&transfer_fd) ||
       pgagroal_connection_id_write(transfer_fd, CONNECTION_GET_FD) ||
       pgagroal_connection_slot_write(transfer_fd, slot) ||
       pgagroal_connection_generation_read(transfer_fd, &generation))
   {
      goto error;
   }

   if (generation != expected || pgagroal_connection_transfer_read(transfer_fd, &s, &fd) || s != slot || fd < 0)
   {
      goto error;
   }

   pgagroal_disconnect(transfer_fd);
   transfer_fd = -1;

   /* Everything uses connections[slot].fd, so the descriptor must have that
    * number here too. An open target is our inherited copy of the slot's
    * previous backend, which dup2() replaces; anything else must not be touched. */
   if (fd != target)
   {
      if ((fcntl(target, F_GETFD) != -1 && pgagroal_known_fds[slot] != target) || dup2(fd, target) == -1)
      {
         goto error;
      }
      pgagroal_disconnect(fd);
   }

   pgagroal_log_debug("fetch_descriptor: Slot %d FD %d Generation %lld", slot, target, generation);

   return true;

error:

   pgagroal_log_debug("fetch_descriptor: Slot %d not available (FD %d Generation %lld)", slot, target, expected);

   if (fd >= 0 && fd != target)
   {
      pgagroal_disconnect(fd);
   }
   pgagroal_disconnect(transfer_fd);
   errno = 0;

   return false;
}

static bool
holds_descriptor(int slot)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /* connections[slot].fd is ours only if main received it before we were forked */
   return atomic_load(&config->connections[slot].generation) <= pgagroal_fd_generation;
}
