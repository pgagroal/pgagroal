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

#ifndef PGAGROAL_TSCLIENT_H
#define PGAGROAL_TSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE                  8192

#define PGBENCH_LOG_FILE_TRAIL       "/log/pgbench.log"
#define PGAGROAL_CONFIGURATION_TRAIL "/pgagroal-testsuite/conf/pgagroal.conf"

extern char project_directory[BUFFER_SIZE];
extern char* user;
extern char* database;

/**
 * Initialize the tsclient API
 * @param base_dir path to base
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tsclient_init(char* base_dir);

/**
 * Destroy the tsclient (must be used after pgagroal_tsclient_init)
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tsclient_destroy();

/**
 * A wrapper around pgbench specific to our usecase [benchmark options supported: '-c', '-j', '-t']
 * Execute a pgbench command for a set of instructions, assuming we are connecting to the 1st server
 * @param database name of the database
 * @param select_only true if we are only doing selects
 * @param client_count number of clients
 * @param thread_count number of threads
 * @param transaction_count number of transactions
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tsclient_execute_pgbench(char* user, char* database, bool select_only, int client_count, int thread_count, int transaction_count);

/**
 * Initialize a database using pgbench
 * @param user name of the user
 * @param database name of the database
 * @param scale scale factor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_tsclient_init_pgbench(char* user, char* database, int scale);

/**
 * Run client_count concurrent psql sessions through pgagroal, each holding a
 * connection by executing 'BEGIN; SELECT pg_sleep(hold_seconds); COMMIT;'.
 * Used to exercise the retry path of pgagroal_get_connection() under
 * saturation: when client_count > max_connections, the surplus clients must
 * acquire freed slots once early holders return their connections (issue #875).
 * @param user name of the user
 * @param database name of the database
 * @param client_count number of concurrent psql sessions to spawn
 * @param hold_seconds duration of the pg_sleep() hold per session
 * @return 0 if every client returned success, otherwise 1
 */
int
pgagroal_tsclient_execute_concurrent_holds(char* user, char* database, int client_count, int hold_seconds);

#ifdef __cplusplus
}
#endif

#endif