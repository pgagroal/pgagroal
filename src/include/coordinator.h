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

#ifndef PGAGROAL_COORDINATOR_H
#define PGAGROAL_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgagroal */
#include <pgagroal.h>
#include <json.h>

/* system */
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#define COORDINATOR_PROBE_TIMEOUT    10
#define COORDINATOR_SHUTDOWN_TIMEOUT 30

/**
 * Handle a management request. Takes ownership of the descriptor and of the
 * address, and exits the process.
 * @param client_fd The descriptor
 * @param address The client address
 */
void
pgagroal_coordinator_management(int client_fd, char* address);

/**
 * Report the state of the coordinator
 * @param ssl The SSL connection
 * @param client_fd The descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The payload
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_coordinator_status(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption,
                            struct json* payload);

/**
 * Switch the primary to another node
 * @param node The name of the node
 * @param old_primary The previous primary node, or -1
 * @param new_primary The new primary node
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_coordinator_switch(char* node, int* old_primary, int* new_primary);

/**
 * Probe all nodes: query each node's management endpoint, populate the
 * discovered role and data-plane port, and set the current primary.
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_coordinator_probe(void);

/**
 * Probe a single node: query its management endpoint and refresh its
 * role, data-plane port and reachability.
 * @param node The node index
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_coordinator_probe_node(int node);

/**
 * Get the index of a node by name
 * @param name The name of the node
 * @return The node index, otherwise -1
 */
int
pgagroal_coordinator_get_node(char* name);

/**
 * Get the node currently designated primary
 * @return The node index, otherwise -1
 */
int
pgagroal_coordinator_get_primary(void);

/**
 * Set the node currently designated primary
 * @param node The node index, or -1 to clear
 */
void
pgagroal_coordinator_set_primary(int node);

/**
 * Reserve a slot for a client connection before the proxy child is forked
 * @param node The node the child will proxy to
 * @return The slot, otherwise -1
 */
int
pgagroal_coordinator_client_reserve(int node);

/**
 * Assign the process identifier of the proxy child to a reserved slot
 * @param slot The slot
 * @param pid The process identifier
 */
void
pgagroal_coordinator_client_commit(int slot, pid_t pid);

/**
 * Release a reserved slot for which no proxy child was created
 * @param slot The slot
 */
void
pgagroal_coordinator_client_release_slot(int slot);

/**
 * Release the slot of a proxy child that has been reaped
 * @param pid The process identifier
 */
void
pgagroal_coordinator_client_release(pid_t pid);

/**
 * Terminate the proxy children of a node
 * @param node The node, or -1 for all the nodes
 */
void
pgagroal_coordinator_client_kill(int node);

/**
 * Get the number of client connections being proxied
 * @return The number of connections
 */
int
pgagroal_coordinator_client_count(void);

#ifdef __cplusplus
}
#endif

#endif
