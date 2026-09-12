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

#ifndef PGAGROAL_SERVER_H
#define PGAGROAL_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>

#include <stdlib.h>
#include <openssl/ssl.h>

/**
 * Get the primary server
 * @param server The resulting server identifier
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_get_primary(int* server);

/**
 * Update the server state
 * @param slot The slot
 * @param socket The descriptor
 * @param ssl The SSL connection
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_update_server_state(int slot, int socket, SSL* ssl);

/**
 * Print the state of the servers
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_status(void);

/**
 * Failover
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_failover(int slot);

/**
 * Force failover
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_force_failover(int server);

/**
 * Clear server
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_clear(char* server);

/**
 * Switch server
 * @param server The server
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_switch(char* server);

/**
 * Check server system identifiers for duplicates
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_check_server_identifiers(bool strict);

/**
 * Connect to a server, authenticate, and execute a simple query
 * @param server_idx The server index
 * @param user The username for the connection
 * @param database The database name for the connection
 * @param query The SQL query string
 * @param timeout The timeout in seconds for server responses
 * @param auth_type The authentication type used, can be NULL
 * @param fd_out The resulting file descriptor for reading the response
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_query_execute(int server_idx, char* user, char* database, char* query, int timeout, int* auth_type, int* fd_out);

/**
 * Get live PostgreSQL connectivity, role, and replication lag for one configured server.
 *
 * @param server       The server index into config->servers[]
 * @param status       Output: "Running" or "Down"  (static string, do not free)
 * @param primary      Output: "Yes", "No", or "Unknown" (static string, do not free)
 * @param behind_bytes Output: replication lag in bytes (>= 0 for standbys; -1 if N/A or unavailable)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_get_connectivity_info(int server, char** status, char** primary, int64_t* behind_bytes);

/**
 * Get the primary's replication slot / connected-standby state.
 *
 * @param server        The primary's server index into config->servers[]
 * @param slot_names    Output: slot name per row
 * @param client_addrs  Output: connected client address per row
 * @param max_rows      Capacity of slot_names/client_addrs (rows beyond this are dropped)
 * @param num_rows      Output: actual number of rows returned
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_get_replication_slots_status(int server,
                                             char slot_names[][MISC_LENGTH],
                                             char client_addrs[][MISC_LENGTH],
                                             int max_rows, int* num_rows);

/**
 * Get the WAL receiver status and streaming details for a configured standby server.
 *
 * @param server        The server index into config->servers[]
 * @param rep_status    Output: the replication status (e.g., "streaming")
 * @param status_size   The maximum size of the rep_status buffer
 * @param slot_name     Output: the replication slot name
 * @param slot_size     The maximum size of the slot_name buffer
 * @param sender_host   Output: the primary host the standby is streaming from
 * @param host_size     The maximum size of the sender_host buffer
 * @param sender_port   Output: the primary port the standby is streaming from
 * @param port_size     The maximum size of the sender_port buffer
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_server_get_wal_receiver_status(int server,
                                        char* rep_status, size_t status_size,
                                        char* slot_name, size_t slot_size,
                                        char* sender_host, size_t host_size,
                                        char* sender_port, size_t port_size);
/**
 * Check if the server is a primary server
 * @param server The server index
 * @return true if primary, false otherwise
 */
bool
pgagroal_server_is_primary(int server);

#ifdef __cplusplus
}
#endif

#endif
