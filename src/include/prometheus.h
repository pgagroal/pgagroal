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

#ifndef PGAGROAL_PROMETHEUS_H
#define PGAGROAL_PROMETHEUS_H

#include <openssl/crypto.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <ev.h>
#include <stdlib.h>

// Certificate type constants
#define PGAGROAL_CERT_TYPE_MAIN    "main"
#define PGAGROAL_CERT_TYPE_METRICS "metrics"
#define PGAGROAL_CERT_TYPE_SERVER  "server"

// Server name constants
#define PGAGROAL_SERVER_NAME_MAIN    "pgagroal"
#define PGAGROAL_SERVER_NAME_METRICS "metrics"

/*
 * Value to disable the Prometheus cache,
 * it is equivalent to set `metrics_cache`
 * to 0 (seconds).
 */
#define PGAGROAL_PROMETHEUS_CACHE_DISABLED 0

/**
 * Max size of the cache (in bytes).
 * If the cache request exceeds this size
 * the caching should be aborted in some way.
 */
#define PROMETHEUS_MAX_CACHE_SIZE (1024 * 1024)

/**
 * The default cache size in the case
 * the user did not set any particular
 * configuration option.
 */
#define PROMETHEUS_DEFAULT_CACHE_SIZE (256 * 1024)

/**
 * Create a prometheus instance
 * @param client_ssl The client SSL structure
 * @param fd The client descriptor
 */
void
pgagroal_prometheus(SSL* client_ssl, int fd);

/**
 * Create a prometheus instance for vault
 * @param client_ssl The client SSL structure
 * @param fd The client descriptor
 */
void
pgagroal_vault_prometheus(SSL* client_ssl, int fd);

/**
 * Initialize prometheus shmem
 */
int
pgagroal_init_prometheus(size_t* p_size, void** p_shmem);

/**
 * Initialize prometheus shmem for vault
 */
int
pgagroal_vault_init_prometheus(size_t* p_size, void** p_shmem);

/**
 * Add session time information
 * @param time The time
 */
void
pgagroal_prometheus_session_time(double time);

/**
 * Connection error
 */
void
pgagroal_prometheus_connection_error(void);

/**
 * Connection kill
 */
void
pgagroal_prometheus_connection_kill(void);

/**
 * Connection remove
 */
void
pgagroal_prometheus_connection_remove(void);

/**
 * Connection timeout
 */
void
pgagroal_prometheus_connection_timeout(void);

/**
 * Connection return
 */
void
pgagroal_prometheus_connection_return(void);

/**
 * Connection invalid
 */
void
pgagroal_prometheus_connection_invalid(void);

/**
 * Connection get
 */
void
pgagroal_prometheus_connection_get(void);

/**
 * Connection idle timeout
 */
void
pgagroal_prometheus_connection_idletimeout(void);

/**
 * Connection max connection age
 */
void
pgagroal_prometheus_connection_max_connection_age(void);

/**
 * Connection awaiting due to `blocking_timeout`.
 * Tracks the total awaiting connections and also the
 * per-limit ones.
 *
 * <b>
 * Every call to this function should be paired
 * by the same number of calls
 * to `pgagroal_prometheus_connection_unawaiting()`
 * </b>
 *
 * @param limit_index if greater or equal to zero
 * tracks the awaiting connection for the limits entry
 * (i.e., per user and database)
 */
void
pgagroal_prometheus_connection_awaiting(int limit_index);

/**
 * An awaiting connection, i.e., one holded by `blocking_timeout`
 * that is no more on hold and can restart its workflo.
 *
 *
 * <b>
 * Every call to this function should be after
 * the call
 * to `pgagroal_prometheus_connection_awaiting()`
 * </b>
 *
 * The function decreases the total counter of the awaiting connections as
 * well as the per-limit ones.
 *
 * @param limit_entry if greater or equal to zero
 * it untracks the corresponding limit entry
 */
void
pgagroal_prometheus_connection_unawaiting(int limit_index);

/**
 * Connection flush
 */
void
pgagroal_prometheus_connection_flush(void);

/**
 * Connection success
 */
void
pgagroal_prometheus_connection_success(void);

/**
 * Increase AUTH_SUCCESS for a user
 */
void
pgagroal_prometheus_auth_user_success(void);

/**
 * Increase AUTH_BAD_PASSWORD for a user
 */
void
pgagroal_prometheus_auth_user_bad_password(void);

/**
 * Increase AUTH_ERROR for a user
 */
void
pgagroal_prometheus_auth_user_error(void);

/**
 * Increase client_wait by 1
 */
void
pgagroal_prometheus_client_wait_add(void);

/**
 * Decrease client_wait by 1
 */
void
pgagroal_prometheus_client_wait_sub(void);

/**
 * Increase client_active by 1
 */
void
pgagroal_prometheus_client_active_add(void);

/**
 * Decrease client_active by 1
 */
void
pgagroal_prometheus_client_active_sub(void);

/**
 * Increase query_count by 1
 */
void
pgagroal_prometheus_query_count_add(void);

/**
 * Increase query_count for the specified connection by 1
 * @param slot The connection slot
 */
void
pgagroal_prometheus_query_count_specified_add(int slot);

/**
 * Reset query_count for the specified connection
 * @param slot The connection slot
 */
void
pgagroal_prometheus_query_count_specified_reset(int slot);

/**
 * Increase tx_count by 1
 */
void
pgagroal_prometheus_tx_count_add(void);

/**
 * Increase network_sent
 * @param s The size
 */
void
pgagroal_prometheus_network_sent_add(ssize_t s);

/**
 * Increase network_received
 * @param s The size
 */
void
pgagroal_prometheus_network_received_add(ssize_t s);

/**
 * Increase client_sockets by 1
 */
void
pgagroal_prometheus_client_sockets_add(void);

/**
 * Decrease client_sockets by 1
 */
void
pgagroal_prometheus_client_sockets_sub(void);

/**
 * Increase self_sockets by 1
 */
void
pgagroal_prometheus_self_sockets_add(void);

/**
 * Decrease self_sockets by 1
 */
void
pgagroal_prometheus_self_sockets_sub(void);

/**
 * Reset the counters and histograms
 */
void
pgagroal_prometheus_clear(void);

/**
 * Increase SERVER_ERROR for a server
 * @param server The server
 */
void
pgagroal_prometheus_server_error(int server);

/**
 * Count failed servers
 */
void
pgagroal_prometheus_failed_servers(void);

/**
 * Add a logging count
 * @param logging The logging type
 */
void
pgagroal_prometheus_logging(int logging);

/**
 * Allocates, for the first time, the Prometheus cache.
 *
 * The cache structure, as well as its dynamically sized payload,
 * are created as shared memory chunks.
 *
 * Assumes the shared memory for the configuration is already set.
 *
 * The cache will be allocated as soon as this method is invoked,
 * even if the cache has not been configured at all!
 *
 * If the memory cannot be allocated, the function issues errors
 * in the logs and disables the caching machinaery.
 *
 * @param p_size a pointer to where to store the size of
 * allocated chunk of memory
 * @param p_shmem the pointer to the pointer at which the allocated chunk
 * of shared memory is going to be inserted
 *
 * @return 0 on success
 */
int
pgagroal_init_prometheus_cache(size_t* p_size, void** p_shmem);

/**
 * Update certificate metrics for main pgagroal
 * @param config The main configuration
 * @return 0 on success, 1 on error
 */
int
pgagroal_update_main_certificate_metrics(struct main_configuration* config);

#ifdef __cplusplus
}
#endif

#endif
