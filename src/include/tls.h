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

#include <openssl/ssl.h>

#define PGAGROAL_TLS_OK      0
#define PGAGROAL_TLS_WANT_IO 1
#define PGAGROAL_TLS_CLOSED  2
#define PGAGROAL_TLS_ERROR   (-1)

/**
 * A TLS context driven through memory BIOs instead of a socket, so the
 * cryptographic state is independent of the TCP connection lifecycle.
 * Ciphertext moves via feed()/drain(); plaintext via write()/read().
 */
struct tls
{
   SSL* ssl;                /**< The OpenSSL connection */
   BIO* rbio;               /**< Inbound ciphertext: network -> engine */
   BIO* wbio;               /**< Outbound ciphertext: engine -> network */
   bool server;             /**< true for the accepting side */
   bool handshake_complete; /**< true once the handshake has completed */
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

#ifdef __cplusplus
}
#endif

#endif
