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

#ifndef PGAGROAL_CONNECTION_H
#define PGAGROAL_CONNECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

#define CONNECTION_TRANSFER    0
#define CONNECTION_RETURN      1
#define CONNECTION_KILL        2
#define CONNECTION_CLIENT_FD   3
#define CONNECTION_REMOVE_FD   4
#define CONNECTION_CLIENT_DONE 5
#define CONNECTION_GET_FD      6

/**
 * Connection: Get a connection
 * @param client_fd The client descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_get(int* client_fd);

/**
 * Connection: Get a connection based on a PID
 * @param client_fd The client descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_get_pid(pid_t pid, int* client_fd);

/**
 * Connection: Opetation id write
 * @param client_fd The client descriptor
 * @param id The identifier
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_id_write(int client_fd, int id);

/**
 * Connection: Opetation id read
 * @param client_fd The client descriptor
 * @param id The identifier
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_id_read(int client_fd, int* id);

/**
 * Connection: Transfer write
 * @param client_fd The client descriptor
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_transfer_write(int client_fd, int32_t slot);

/**
 * Connection: Transfer read
 * @param client_fd The client descriptor
 * @param slot The slot
 * @param fd The file descriptor
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_transfer_read(int client_fd, int32_t* slot, int* fd);

/**
 * Connection: Slot write
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_slot_write(int client_fd, int32_t slot);

/**
 * Connection: Slot read
 * @param slot The slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_slot_read(int client_fd, int32_t* slot);

/**
 * Connection: Generation write
 * @param generation The descriptor generation of the slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_generation_write(int client_fd, long long generation);

/**
 * Connection: Generation read
 * @param generation The descriptor generation of the slot
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_generation_read(int client_fd, long long* generation);

/**
 * Connection: PID write
 * @param pid The PID
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_pid_write(int client_fd, pid_t pid);

/**
 * Connection: PID read
 * @param pid The PID
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_connection_pid_read(int client_fd, pid_t* pid);

#ifdef __cplusplus
}
#endif

#endif
