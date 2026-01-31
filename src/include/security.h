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

#ifndef PGAGROAL_SECURITY_H
#define PGAGROAL_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <deque.h>

#include <stdlib.h>

#include <openssl/ssl.h>

struct message;

/**
 * Authenticate a user
 * @param client_fd The descriptor
 * @param address The client address
 * @param slot The resulting slot
 * @param client_ssl The client SSL context
 * @param server_ssl The server SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_authenticate(int client_fd, char* address, int* slot, SSL** client_ssl, SSL** server_ssl);

/**
 * Authenticate a prefill connection
 * @param username The user name
 * @param password The password
 * @param database The database
 * @param slot The resulting slot
 * @param server_ssl The server SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_prefill_auth(char* username, char* password, char* database, int* slot, SSL** server_ssl);

/**
 * Authenticate a remote management user
 * @param client_fd The descriptor
 * @param address The client address
 * @param client_ssl The client SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_remote_management_auth(int client_fd, char* address, SSL** client_ssl);

/**
 * Connect using SCRAM-SHA256
 * @param username The user name
 * @param password The password
 * @param server_fd The descriptor
 * @param s_ssl The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl);

/**
 * Get the master key
 * @param masterkey The master key
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_get_master_key(char** masterkey);

/**
 * Get the password for a user
 * @param username The user name
 * @return The password
 */
char*
pgagroal_get_user_password(char* username);

/**
 * MD5 a string
 * @param str The string
 * @param md5 The MD5 string
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_md5(char* str, int length, char** md5);

/**
 * Is the user known to the system
 * @param user The user name
 * @return True if known, otherwise false
 */
bool
pgagroal_user_known(char* user);

/**
 * Is the TLS configuration valid
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tls_valid(void);

/**
 * @brief Validate the vault TLS configuration
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_vault_tls_valid(void);

/**
 * @brief Generate a random ASCII password have size of pwd_length
 * @param password the resultant password
 * @param password_length length of the password
 * @return 0 if success, otherwise 1
 */
int
pgagroal_generate_password(int password_length, char** password);

/**
 * @brief Accept the SSL connection for the vault from client (curl)
 * @param config the vault configuration
 * @param client_fd the descriptor
 * @param c_ssl the client SSL context
 * @return 0 if success, otherwise 1
 */
int
accept_ssl_vault(struct vault_configuration* config, int client_fd, SSL** c_ssl);

/**
 * @brief Initialize RNG
 *
 */
void
pgagroal_initialize_random(void);

/**
 * Create a SSL context
 * @param client True if client, false if server
 * @param ctx The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_ssl_ctx(bool client, SSL_CTX** ctx);

/**
 * Create a SSL server
 * @param ctx The SSL context
 * @param key_file The key file path
 * @param cert_file The certificate file path
 * @param ca_file The ca file path
 * @param socket The socket
 * @param ssl The SSL structure
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_ssl_server(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file, int socket, SSL** ssl);

/**
 * Close a SSL structure
 * @param ssl The SSL structure
 */
void
pgagroal_close_ssl(SSL* ssl);

/**
 * Check if a request is SSL request or not
 * @param client_fd The client file descriptor
 */
bool
pgagroal_is_ssl_request(int client_fd);

/**
 * Extract server parameters received during the latest authentication
 * @param slot The connection slot
 * @param server_parameters The resulting non-thread-safe deque
 * @return 0 on success, otherwise 1
 */
int
pgagroal_extract_server_parameters(int slot, struct deque** server_parameters);

/**
 * Extract client identity from SSL certificate
 * Checks SAN (Subject Alternative Name) first, then falls back to CN (Common Name)
 * @param ssl The SSL connection
 * @return Dynamically allocated string containing the identity (must be freed by caller), or NULL if not found
 */
char*
pgagroal_extract_cert_identity(SSL* ssl);

/**
 * Check if certificate identity is authorized for the requested username
 * @param cert_identity The identity extracted from the certificate
 * @param requested_username The username being requested
 * @return True if authorized, false otherwise
 */
bool
pgagroal_is_cert_authorized(const char* cert_identity, const char* requested_username);

/**
 * Authenticate using MD5
 * @param startup_response_msg The startup response message
 * @param username The user name
 * @param password The password
 * @param socket The descriptor
 * @param server_ssl The SSL context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_md5_client_auth(struct message* startup_response_msg, char* username, char* password, int socket, SSL* server_ssl, struct message** response_msg);

/**
 * Perform SCRAM-SHA-256 client authentication
 * @param username The username
 * @param password The password
 * @param socket The socket descriptor
 * @param server_ssl The SSL context (optional)
 * @return AUTH_SUCCESS on success, error code otherwise
 */
int
pgagroal_scram_client_auth(char* username, char* password, int socket, SSL* server_ssl, struct message** response_msg);

#ifdef __cplusplus
}
#endif

#endif
