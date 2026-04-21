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

#ifndef PGAGROAL_QUERIES_H
#define PGAGROAL_QUERIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * SQL for startup validation: cluster id and pg_control version.
 * @return Static query string (do not free)
 */
const char*
pgagroal_queries_system_identifier(void);

/**
 * SQL for standby recovery state (single scalar).
 * @return Static query string (do not free)
 */
const char*
pgagroal_queries_is_in_recovery(void);

/**
 * SQL for replication lag in bytes on a standby (single bigint).
 * @return Static query string (do not free)
 */
const char*
pgagroal_queries_replication_lag_bytes(void);

/**
 * Read PostgreSQL wire-protocol messages from fd until ReadyForQuery, and fill
 * @a value with the text of the first column of the first DataRow ('D').
 *
 * @param fd Open server connection after a query was sent
 * @param value Output buffer (NUL-terminated on success)
 * @param value_size Size of @a value including space for NUL
 * @return 0 if a value was read (possibly empty), 1 on error or no DataRow
 */
int
pgagroal_read_query_first_column_text(int fd, char* value, size_t value_size);

#ifdef __cplusplus
}
#endif

#endif
