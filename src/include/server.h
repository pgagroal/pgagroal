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
 * Test server connectivity
 * @param server The server index
 * @param is_available Pointer to store availability status
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_test_server_connectivity(int server, bool* is_available);

/**
 * Get server status string (Running, Recovering, or Down)
 * @param server The server index
 * @param status_string Pointer to store status string (must be freed by caller)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_get_server_status(int server, char** status_string);

#ifdef __cplusplus
}
#endif

#endif
