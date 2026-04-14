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

#ifndef PGAGROAL_FIPS_H
#define PGAGROAL_FIPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/ssl.h>

/**
 * Check and update the FIPS status for a server.
 *
 * PostgreSQL 18+: calls pgcrypto.fips_mode() for the actual runtime status.
 * PostgreSQL 14-17: confirms pgcrypto is installed, then probes MD5 via
 *   pgcrypto.digest(); an error response indicates FIPS mode is active
 *   (MD5 is a prohibited algorithm under FIPS).
 * PostgreSQL < 14: fips_enabled is set to false, no query is sent.
 *
 * Must be called AFTER pgagroal_update_server_state() so that
 * the server version is already populated.
 *
 * @param slot   The connection slot (used to derive the server index)
 * @param socket The socket descriptor for the server connection
 * @param ssl    The SSL context (may be NULL)
 * @return 0 upon success, 1 on error (fips_enabled is set to false on error)
 */
int
pgagroal_fips_check(int slot, int socket, SSL* ssl);

#ifdef __cplusplus
}
#endif

#endif /* PGAGROAL_FIPS_H */
