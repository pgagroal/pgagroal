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

#ifndef PGAGROAL_MESSAGE_H
#define PGAGROAL_MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <ev.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define MESSAGE_STATUS_ZERO  0
#define MESSAGE_STATUS_OK    1
#define MESSAGE_STATUS_ERROR 2

/** @struct message
 * Defines a message
 */
struct message
{
   signed char kind; /**< The kind of the message */
   ssize_t length;   /**< The length of the message */
   void* data;       /**< The message data */
} __attribute__((aligned(64)));

/**
 * Read a message in blocking mode
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_block_message(SSL* ssl, int socket, struct message** msg);

/**
 * Read a complete typed message in blocking mode. The message must carry the
 * standard header (1 byte kind + 4 byte length); the read continues until the
 * number of bytes declared in the header has arrived, so a message split
 * across multiple network segments is never returned partially. A message
 * whose declared length is malformed (less than 4 bytes or larger than the
 * parse buffer) is an error.
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_complete_message(SSL* ssl, int socket, struct message** msg);

/**
 * Read a message with a timeout
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param timeout The timeout in seconds
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_timeout_message(SSL* ssl, int socket, int timeout, struct message** msg);

/**
 * Write a message using a socket
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_write_message(SSL* ssl, int socket, struct message* msg);

/**
 * Create a message
 * @param data A pointer to the data
 * @param length The length of the message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_message(void* data, ssize_t length, struct message** msg);

/**
 * Clear a message
 * @param msg The resulting message
 */
void
pgagroal_clear_message(struct message* msg);

/**
 * Copy a message
 * @param msg The resulting message
 * @return The copy
 */
struct message*
pgagroal_copy_message(struct message* msg);

/**
 * Free a message
 * @param msg The resulting message
 */
void
pgagroal_free_message(struct message* msg);

/**
 * Callback invoked by pgagroal_parse_message once per complete message boundary.
 * For messages with a payload it fires when both the header and the first payload
 * byte have arrived, so the callback can safely read msg[5] (e.g. the transaction
 * state byte of a ReadyForQuery 'Z' message). A NULL arg is valid; the callback
 * is responsible for guarding against it if needed.
 * @param kind The message type byte (e.g. 'Z', 'E', 'D')
 * @param msg Pointer to the message start (kind byte included); at least 6 bytes
 *            are valid when msglen > 5
 * @param msglen Declared total length of the message (kind byte + 4-byte length field + payload)
 * @param arg Caller-supplied context passed through unchanged from pgagroal_parse_message
 */
typedef void (*pgagroal_message_callback)(char kind, char* msg, int msglen, void* arg);

/**
 * Parser state for pgagroal_parse_message. Zero-initialize before the first
 * call and pass the same instance on every subsequent call for the same
 * connection so the parser can resume exactly where the previous read stopped.
 * All fields are private to the parser; callers must not modify them directly.
 */
struct pgagroal_message_state
{
   char header[5];          /**< Partial header buffer (kind byte + 4-byte length field) */
   char first_payload_byte; /**< First byte of the payload, e.g. transaction state for 'Z' */
   int header_len;          /**< Bytes of the header received so far (0-5); 5 means complete */
   int payload_remaining;   /**< Payload bytes not yet consumed from the current message */
};

/**
 * Read a buffer holding one or more concatenated PostgreSQL protocol messages
 * that may be split across multiple reads. The caller keeps the parser state
 * in `state` (zero it before the first call); it is updated so a subsequent
 * call can continue exactly where this one stopped.
 *
 * The callback is invoked once per message. For a message without a payload
 * it fires as soon as the header (kind byte + length field) is complete; for
 * a message with a payload it fires when the header and the first payload
 * byte are present, so the callback can read past the header (e.g. the
 * transaction state byte of a Z message). A header split across reads is
 * buffered in the state until it can be parsed, so a split never
 * desynchronizes the parser.
 * @param state Parser state, zero-initialized before the first call
 * @param data The buffer holding the received bytes
 * @param length The number of received bytes
 * @param callback The callback to invoke per message
 * @param arg An argument passed through to the callback
 */
void
pgagroal_parse_message(struct pgagroal_message_state* state,
                       char* data,
                       int length,
                       pgagroal_message_callback callback, void* arg);

/**
 * Caller-supplied context passed to pgagroal_pipeline_server_rfq via the
 * pgagroal_parse_message arg parameter. All pointer fields are optional;
 * pass NULL for any field that does not need tracking.
 */
struct pipeline_server_state
{
   bool* in_tx;   /**< Updated with the transaction state from 'Z' messages */
   bool* saw_rfq; /**< Set to true when a genuine idle ReadyForQuery is seen */
   bool* fatal;   /**< Set to true when an 'E' message carries FATAL or PANIC */
};

/**
 * Shared server-side message callback for the session and transaction
 * pipelines. Updates the pipeline transaction state and prometheus counters
 * when a genuine ReadyForQuery ('Z') boundary is seen, and sets the fatal
 * flag when a genuine ErrorResponse ('E') carries FATAL or PANIC.
 * Safe to call with a NULL arg; returns immediately in that case.
 * @param kind The message type byte
 * @param msg Pointer to the message start (kind byte included)
 * @param msglen Declared total length of the message
 * @param arg Pointer to a pipeline_server_state, or NULL
 */
void
pgagroal_pipeline_server_rfq(char kind, char* msg, int msglen, void* arg);

/**
 * Write an empty message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_empty(SSL* ssl, int socket);

/**
 * Write a notice message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_notice(SSL* ssl, int socket);

/**
 * Write a pool is full message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_pool_full(SSL* ssl, int socket);

/**
 * Write a connection refused message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_connection_refused(SSL* ssl, int socket);

/**
 * Write a connection refused message (protocol 1 or 2)
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_connection_refused_old(SSL* ssl, int socket);

/**
 * Write a bad password message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param username The user name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_bad_password(SSL* ssl, int socket, char* username);

/**
 * Write an unsupported security model message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param username The user name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_unsupported_security_model(SSL* ssl, int socket, char* username);

/**
 * Write a no HBA entry message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param username The user name
 * @param database The database
 * @param address The client address
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_no_hba_entry(SSL* ssl, int socket, char* username, char* database, char* address);

/**
 * Write a deallocate all message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_deallocate_all(SSL* ssl, int socket);

/**
 * Write the configured connection reset query (server_reset_query) as a simple query
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_reset_query(SSL* ssl, int socket);

/**
 * Write TLS response
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_tls(SSL* ssl, int socket);

/**
 * Write a terminate message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_terminate(SSL* ssl, int socket);

/**
 * Write a failover message to the client
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_client_failover(SSL* ssl, int socket);

/**
 * Write an auth password message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_password(SSL* ssl, int socket);

/**
 * Write a rollback message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_rollback(SSL* ssl, int socket);

/**
 * Create an auth password response message
 * @param password The password
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_password_response(char* password, struct message** msg);

/**
 * Write an auth SCRAM-SHA-256 message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @param channel_binding Advertise SCRAM-SHA-256-PLUS as well (requires a TLS frontend)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_scram256(SSL* ssl, int socket, bool channel_binding);

/**
 * Create an auth SCRAM-SHA-256 response message
 * @param nounce The nounce
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_response(char* nounce, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256-PLUS response message (tls-server-end-point channel binding)
 * @param nounce The nounce
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_plus_response(char* nounce, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue message
 * @param cn The client nounce
 * @param sn The server nounce
 * @param salt The salt
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_continue(char* cn, char* sn, char* salt, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Continue response message
 * @param wp The without proff
 * @param p The proff
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_continue_response(char* wp, char* p, struct message** msg);

/**
 * Create an auth SCRAM-SHA-256/Final message
 * @param ss The server signature (BASE64)
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_auth_scram256_final(char* ss, struct message** msg);

/**
 * Write an auth success message
 * @param ssl The SSL struct
 * @param socket The socket descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_write_auth_success(SSL* ssl, int socket);

/**
 * Create a SSL message
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_ssl_message(struct message** msg);

/**
 * Create a startup message
 * @param username The user name
 * @param database The database
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_startup_message(char* username, char* database, struct message** msg);

/**
 * Create a cancel request message
 * @param pid The pid
 * @param secret The secret
 * @param msg The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_cancel_request_message(int pid, int secret, struct message** msg);

/**
 * Is the connection valid
 * @param socket The socket descriptor
 * @return true upon success, otherwise false
 */
bool
pgagroal_connection_isvalid(int socket);

/**
 * Log a message
 * @param msg The message
 */
void
pgagroal_log_message(struct message* msg);

/**
 * Read a message from a buffer (io_uring backend)
 * @param watcher The io_wacher used to recv
 * @param msg The message received
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_recv_message(struct io_watcher* watcher, struct message** msg);

/**
 * Get the message buffer for a watcher
 * @param watcher The watcher
 * @return The message buffer
 */
struct message*
pgagroal_get_watcher_message(struct io_watcher* watcher);

/**
 * Write a message to a buffer (io_uring backend)
 * @param watcher The io_wacher used to send
 * @param msg The message to send
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_send_message(struct io_watcher* watcher, struct message* msg);

/**
 * Read a message using a socket
 * @param socket The socket descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_socket_message(int socket, struct message** msg);

/**
 * Write a message using a socket
 * @param socket The socket descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_write_socket_message(int socket, struct message* msg);

/**
 * Read a message using SSL
 * @param ssl The SSL descriptor
 * @param msg The resulting message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_read_ssl_message(SSL* ssl, struct message** msg);

/**
 * Write a message using SSL
 * @param ssl The SSL descriptor
 * @param msg The message
 * @return One of MESSAGE_STATUS_ZERO, MESSAGE_STATUS_OK or MESSAGE_STATUS_ERROR
 */
int
pgagroal_write_ssl_message(SSL* ssl, struct message* msg);

#ifdef __cplusplus
}
#endif

#endif
