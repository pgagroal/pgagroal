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

#ifndef PGAGROAL_TLS_H
#define PGAGROAL_TLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

#define PGAGROAL_TLS_OK      0
#define PGAGROAL_TLS_WANT_IO 1
#define PGAGROAL_TLS_CLOSED  2
#define PGAGROAL_TLS_ERROR   (-1)

/* TLS 1.3 AEAD cipher suites for the owned record layer */
#define PGAGROAL_TLS_AEAD_AES_128_GCM 1
#define PGAGROAL_TLS_AEAD_AES_256_GCM 2

/* Maximum size of one TLS 1.3 record on the wire (header + 2^14 + expansion) */
#define PGAGROAL_TLS_RECORD_MAX 16645

/** @struct tls_record_dir
 * One direction of an owned TLS 1.3 record stream.
 */
struct tls_record_dir
{
   unsigned char secret[48]; /**< The application traffic secret (<= SHA-384 size) */
   size_t secret_len;        /**< The secret length (32 for SHA-256, 48 for SHA-384) */
   unsigned char key[32];    /**< The derived AEAD key */
   unsigned char iv[12];     /**< The derived AEAD static IV */
   uint64_t seq;             /**< The record sequence number */
};

/** @struct tls_record
 * A socket and process independent TLS 1.3 record context. It is serializable
 * so it can move between the pool and a worker without re-handshaking the backend.
 */
struct tls_record
{
   int aead;                    /**< The AEAD suite (PGAGROAL_TLS_AEAD_*) */
   struct tls_record_dir read;  /**< Records received from the peer */
   struct tls_record_dir write; /**< Records sent to the peer */
};

/** @struct tls
 * A TLS context driven through memory BIOs instead of a socket, so the
 * cryptographic state is independent of the TCP connection lifecycle.
 * Ciphertext moves via feed()/drain(); plaintext via write()/read().
 */
struct tls
{
   SSL* ssl;                                    /**< The OpenSSL connection */
   BIO* rbio;                                   /**< Inbound ciphertext: network -> engine */
   BIO* wbio;                                   /**< Outbound ciphertext: engine -> network */
   int fd;                                      /**< The socket bound to this context, or -1 if none */
   bool server;                                 /**< true for the accepting side */
   bool handshake_complete;                     /**< true once the handshake has completed */
   unsigned char client_secret[48];             /**< Captured TLS 1.3 client application traffic secret */
   unsigned char server_secret[48];             /**< Captured TLS 1.3 server application traffic secret */
   size_t secret_len;                           /**< Length of the captured secrets */
   int secret_mask;                             /**< 0x1 client + 0x2 server captured */
   bool owned;                                  /**< true once I/O is driven by our own record layer */
   struct tls_record record;                    /**< Owned record-layer state (valid when owned) */
   size_t rbuf_len;                             /**< Bytes buffered for inbound record framing */
   unsigned char rbuf[PGAGROAL_TLS_RECORD_MAX]; /**< Inbound TLS record framing buffer */
};

/**
 * Create a socket-decoupled TLS context backed by a pair of memory BIOs
 * @param ctx The OpenSSL context
 * @param server true for the accepting role, false for the connecting role
 * @param tls The resulting context
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_create(SSL_CTX* ctx, bool server, struct tls** tls);

/**
 * Free a TLS context and the OpenSSL resources it owns
 * @param tls The context
 */
void
pgagroal_tls_free(struct tls* tls);

/**
 * Feed ciphertext received from the network into the engine
 * @param tls The context
 * @param buf The ciphertext
 * @param len The number of bytes
 * @param consumed The number of bytes accepted
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_feed(struct tls* tls, const void* buf, size_t len, size_t* consumed);

/**
 * Drain ciphertext the engine has queued for the network
 * @param tls The context
 * @param buf The destination buffer
 * @param cap The buffer capacity
 * @param produced The number of bytes written (0 if nothing pending)
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_drain(struct tls* tls, void* buf, size_t cap, size_t* produced);

/**
 * Report whether the engine has outbound ciphertext queued
 * @param tls The context
 * @return true if there is pending ciphertext to drain
 */
bool
pgagroal_tls_pending(struct tls* tls);

/**
 * Advance the TLS handshake by one step
 * @param tls The context
 * @return PGAGROAL_TLS_OK when complete, PGAGROAL_TLS_WANT_IO when more I/O is
 *         required, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_handshake(struct tls* tls);

/**
 * Encrypt application plaintext, queueing the ciphertext for draining
 * @param tls The context
 * @param buf The plaintext
 * @param len The number of bytes
 * @param written The number of bytes accepted on PGAGROAL_TLS_OK
 * @return PGAGROAL_TLS_OK upon success, PGAGROAL_TLS_WANT_IO when I/O is
 *         required, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_write(struct tls* tls, const void* buf, size_t len, size_t* written);

/**
 * Decrypt application plaintext from previously fed ciphertext
 * @param tls The context
 * @param buf The destination buffer
 * @param cap The buffer capacity
 * @param nread The number of bytes produced (0 unless PGAGROAL_TLS_OK)
 * @return PGAGROAL_TLS_OK upon success, PGAGROAL_TLS_WANT_IO when more
 *         ciphertext is required, PGAGROAL_TLS_CLOSED on a clean peer
 *         shutdown, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_read(struct tls* tls, void* buf, size_t cap, size_t* nread);

/**
 * Associate a socket descriptor with the context so transparent I/O routing
 * can recover it from the SSL object
 * @param tls The context
 * @param fd The socket descriptor
 */
void
pgagroal_tls_set_fd(struct tls* tls, int fd);

/**
 * Recover the wrapper that owns an SSL object, if any
 * @param ssl The OpenSSL connection
 * @return The owning context, or NULL if ssl is not driven by a wrapper
 */
struct tls*
pgagroal_tls_from_ssl(SSL* ssl);

/**
 * Drive a handshake to completion over a blocking socket, pumping ciphertext
 * between the engine and the descriptor
 * @param tls The context
 * @param fd The connected socket descriptor
 * @return PGAGROAL_TLS_OK when the handshake completes, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_socket_handshake(struct tls* tls, int fd);

/**
 * Read decrypted application data over a blocking socket, pumping ciphertext as needed
 * @param tls The context
 * @param fd The connected socket descriptor
 * @param buf The destination buffer
 * @param cap The buffer capacity
 * @param nread The number of plaintext bytes produced
 * @return PGAGROAL_TLS_OK on success, PGAGROAL_TLS_CLOSED on a clean peer
 *         shutdown, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_socket_read(struct tls* tls, int fd, void* buf, size_t cap, size_t* nread);

/**
 * Write application data over a blocking socket, draining the resulting ciphertext
 * @param tls The context
 * @param fd The connected socket descriptor
 * @param buf The plaintext
 * @param len The number of bytes
 * @return PGAGROAL_TLS_OK on success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_socket_write(struct tls* tls, int fd, const void* buf, size_t len);

/**
 * Configure a server-role context with its certificate, private key and optional CA
 * @param ctx The OpenSSL context
 * @param key_file The private key file
 * @param cert_file The certificate file
 * @param ca_file The CA file, or an empty string for none
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tls_configure_server_ctx(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file);

/**
 * Create a socket-decoupled server-role context from configuration files
 * @param ctx The OpenSSL context
 * @param key_file The private key file
 * @param cert_file The certificate file
 * @param ca_file The CA file, or an empty string for none
 * @param tls The resulting context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tls_create_server(SSL_CTX* ctx, char* key_file, char* cert_file, char* ca_file, struct tls** tls);

/**
 * Create a socket-decoupled client-role context from configuration files
 * @param ctx The OpenSSL context
 * @param key The private key file
 * @param cert The certificate file
 * @param root The root certificate file, or an empty string for none
 * @param tls The resulting context
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tls_create_client(SSL_CTX* ctx, char* key, char* cert, char* root, struct tls** tls);

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
 * Create a SSL client
 * @param ctx The SSL context
 * @param key The key file path
 * @param cert The certificate file path
 * @param root The root file path
 * @param socket The socket
 * @param ssl The SSL structure
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl);

/**
 * Harvest the negotiated TLS 1.3 record state (cipher + application traffic
 * secrets) from a completed handshake into a serializable record context. The
 * read/write directions are assigned according to the context's role. Requires
 * TLS 1.3; fails otherwise.
 * @param tls The context, with a completed handshake
 * @param record The resulting serializable record context
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_harvest(struct tls* tls, struct tls_record* record);

/**
 * Switch a context to its own serializable record layer after the handshake:
 * harvest the negotiated TLS 1.3 state, capture any buffered ciphertext, and
 * route all further socket I/O through our record layer instead of OpenSSL.
 * Requires TLS 1.3.
 * @param tls The context, with a completed handshake
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_own(struct tls* tls);

/**
 * Compute the tls-server-end-point channel binding hash (RFC 5929 4.1) for a
 * connection's certificate: our own when acting as the TLS server, the peer's
 * when acting as the client
 * @param ssl The OpenSSL connection
 * @param peer true to hash the peer certificate, false for our own
 * @param out The destination buffer (>= EVP_MAX_MD_SIZE)
 * @param cap The buffer capacity
 * @param out_len The produced hash length
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_cert_endpoint_hash(SSL* ssl, bool peer, unsigned char* out, size_t cap, size_t* out_len);

/**
 * Get the Common Name (CN) from the subject of a certificate
 * @param cert The certificate
 * @param cn The resulting Common Name, which must be freed by the caller
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_cert_common_name(X509* cert, char** cn);

/**
 * Load a certificate revocation list (CRL) into a context, so a revoked
 * certificate fails verification
 * @param ctx The OpenSSL context
 * @param crl_file The path of the CRL in PEM format, or an empty string for none
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_load_crl(SSL_CTX* ctx, char* crl_file);

/**
 * Initialize one record direction from its TLS 1.3 application traffic secret
 * @param dir The direction
 * @param aead The AEAD suite (PGAGROAL_TLS_AEAD_*)
 * @param secret The traffic secret
 * @param secret_len The secret length
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_record_dir_init(struct tls_record_dir* dir, int aead, const unsigned char* secret, size_t secret_len);

/**
 * Seal application plaintext into a TLS 1.3 record (advances the write sequence)
 * @param record The record context
 * @param plaintext The plaintext
 * @param plaintext_len The plaintext length
 * @param out The destination buffer (>= plaintext_len + 22)
 * @param cap The buffer capacity
 * @param out_len The produced record length
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_record_seal(struct tls_record* record, const unsigned char* plaintext, size_t plaintext_len, unsigned char* out, size_t cap, size_t* out_len);

/**
 * Open a TLS 1.3 record into application plaintext (advances the read sequence)
 * @param record The record context
 * @param rec The record bytes
 * @param rec_len The record length
 * @param out The destination buffer
 * @param cap The buffer capacity
 * @param out_len The produced plaintext length
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_record_open(struct tls_record* record, const unsigned char* rec, size_t rec_len, unsigned char* out, size_t cap, size_t* out_len);

/**
 * Serialize a record context to bytes so it can move between processes
 * @param record The record context
 * @param buf The destination buffer
 * @param cap The buffer capacity
 * @param len The produced length
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_record_export(const struct tls_record* record, unsigned char* buf, size_t cap, size_t* len);

/**
 * Restore a record context from its serialized bytes
 * @param buf The serialized bytes
 * @param len The length
 * @param record The resulting context
 * @return PGAGROAL_TLS_OK upon success, otherwise PGAGROAL_TLS_ERROR
 */
int
pgagroal_tls_record_import(const unsigned char* buf, size_t len, struct tls_record* record);

#ifdef __cplusplus
}
#endif

#endif
