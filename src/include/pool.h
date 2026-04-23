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

#ifndef PGAGROAL_POOL_H
#define PGAGROAL_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>
#include <openssl/ssl.h>

/**
 * Get a connection
 * @param username The user name
 * @param database The database
 * @param reuse Should a slot be reused
 * @param transaction_mode Obtain a connection in transaction mode
 * @param slot The resulting slot
 * @param ssl The resulting SSL (can be NULL)
 * @return 0 upon success, 1 if pool is full, otherwise 2
 */
int
pgagroal_get_connection(char* username, char* database, bool reuse, bool transaction_mode, int* slot, SSL** ssl);

/**
 * Compute the next back-off delay for the blocking acquisition retry path.
 *
 * Exponential back-off: the delay doubles each retry, clamped to the cap. The
 * cap is `connection_retry_delay` (milliseconds). Pure function (no shared
 * state) so the sequence can be unit-tested in isolation.
 *
 * @param current_ns The current delay in nanoseconds (>= 0). 0 yields the 1ms seed.
 * @param cap_ms The back-off cap in milliseconds (the configured connection_retry_delay)
 * @return The next delay in nanoseconds, never exceeding cap_ms converted to nanoseconds
 */
long
pgagroal_pool_next_retry_delay(long current_ns, int cap_ms);

/**
 * Return a connection
 * @param slot The slot
 * @param ssl The SSL connection (can be NULL)
 * @param transaction_mode Is the connection returned in transaction mode
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_return_connection(int slot, SSL* ssl, bool transaction_mode);

/**
 * Kill a connection
 * @param slot The slot
 * @param ssl The SSL connection (can be NULL)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_kill_connection(int slot, SSL* ssl);

/**
 * Perform idle timeout
 */
void
pgagroal_idle_timeout(void);

/**
 * Perform max connection age check
 */
void
pgagroal_max_connection_age(void);

/**
 * Perform connection validation
 */
void
pgagroal_validation(void);

/**
 * Flush the pool (JSON)
 * @param mode The mode
 * @param database The database
 */
void
pgagroal_flush(int mode, char* database);

/**
 * Flush the pool for a specific server
 * @param server The server
 */
void
pgagroal_flush_server(signed char server);

/**
 * Flush the pool (JSON)
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgagroal_request_flush(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * @param mode One of PAUSE_MODE_GRACEFULLY / PAUSE_MODE_ALL
 * @param server Server name, or "*" / NULL for all servers
 */
void
pgagroal_pause(int mode, char* server);

/**
 * Resume a paused server or entire pool
 * @param server Server name, or "*" / NULL for all servers
 */
void
pgagroal_resume(char* server);

/**
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgagroal_request_pause(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Resume handler for management child process.
 * Resumes the server/pool, sends the response, and exits.
 * @param ssl The SSL connection
 * @param client_fd The client
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 */
void
pgagroal_request_resume(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Prefill the pool
 * @param initial Use initial size
 */
void
pgagroal_prefill(bool initial);

/**
 * Initialize the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_init(void);

/**
 * Shutdown the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_shutdown(void);

/**
 * Print the status of the pool
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_pool_status(void);

/**
 * This function wraps around the logic to call `pgagroal_prefill()`.
 * In order to avoid code repetition, this function can be used safely
 * wherever there is the possibility to activate the prefill. The function
 * does check if the configuration allows for a prefill, and in such case
 * tries to `fork(2)` and executes the prefill.
 * Also, the function checks for the presence of a primary with
 * `pgagroal_get_primary()` and refuses to do a prefill if there
 * is no primary at all.
 *
 * @param do_fork Run the prefill in a separate process
 * @param initial true if the prefill has to be done with the INITIAL
 *        value of the pgagroal_database.conf file, false if it has
 *        to be done with the MINIMAL value.
 *
 */
void
pgagroal_prefill_if_can(bool do_fork, bool initial);

#ifdef __cplusplus
}
#endif

#endif
