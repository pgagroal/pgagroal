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

/* pgagroal */
#include <pgagroal.h>
#include <coordinator.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utf8.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

static int connect_node(int node, SSL** s_ssl, int* fd);
static void disconnect_node(SSL* ssl, int fd);
static int connect_timeout(char* host, int port, int* fd);
static int set_probe_timeout(int fd);
static int extract_port(struct json* response, int node);
static void extract_role(struct json* response, int node);
static char* health_as_string(signed char health);

static char*
health_as_string(signed char health)
{
   if (health == SERVER_HEALTH_UP)
   {
      return "UP";
   }
   else if (health == SERVER_HEALTH_DOWN)
   {
      return "DOWN";
   }

   return "UNKNOWN";
}

/**
 * Connect to a node with a bounded timeout.
 *
 * pgagroal_connect() uses a blocking connect(), which costs the full TCP
 * retry budget when a node drops the packets rather than refusing them.
 */
static int
connect_timeout(char* host, int port, int* fd)
{
   struct addrinfo hints = {0};
   struct addrinfo* servinfo = NULL;
   struct addrinfo* p = NULL;
   struct pollfd pfd;
   char sport[6];
   int flags;
   int error;
   socklen_t error_length = sizeof(int);
   int rv;

   *fd = -1;

   memset(&sport, 0, sizeof(sport));
   pgagroal_snprintf(&sport[0], sizeof(sport), "%d", port);

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;

   if ((rv = getaddrinfo(host, &sport[0], &hints, &servinfo)) != 0)
   {
      pgagroal_log_debug("getaddrinfo: %s", gai_strerror(rv));
      return 1;
   }

   for (p = servinfo; *fd == -1 && p != NULL; p = p->ai_next)
   {
      if ((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
         continue;
      }

      flags = fcntl(*fd, F_GETFL, 0);
      if (flags == -1 || fcntl(*fd, F_SETFL, flags | O_NONBLOCK) == -1)
      {
         pgagroal_disconnect(*fd);
         *fd = -1;
         continue;
      }

      if (connect(*fd, p->ai_addr, p->ai_addrlen) == -1)
      {
         if (errno != EINPROGRESS)
         {
            pgagroal_disconnect(*fd);
            *fd = -1;
            errno = 0;
            continue;
         }

         memset(&pfd, 0, sizeof(pfd));
         pfd.fd = *fd;
         pfd.events = POLLOUT;

         rv = poll(&pfd, 1, COORDINATOR_PROBE_TIMEOUT * 1000);

         if (rv <= 0)
         {
            pgagroal_disconnect(*fd);
            *fd = -1;
            errno = 0;
            continue;
         }

         error = 0;
         if (getsockopt(*fd, SOL_SOCKET, SO_ERROR, &error, &error_length) == -1 || error != 0)
         {
            pgagroal_disconnect(*fd);
            *fd = -1;
            errno = 0;
            continue;
         }
      }

      if (fcntl(*fd, F_SETFL, flags) == -1)
      {
         pgagroal_disconnect(*fd);
         *fd = -1;
         continue;
      }
   }

   freeaddrinfo(servinfo);

   if (*fd == -1)
   {
      return 1;
   }

   return 0;
}

/**
 * Set a receive and send timeout on the management socket.
 *
 * The management protocol has no timeout of its own, and the remote status
 * handler runs a live pg_is_in_recovery() query, so a slow backend would
 * otherwise stall the probe.
 */
static int
set_probe_timeout(int fd)
{
   struct timeval tv;

   memset(&tv, 0, sizeof(struct timeval));
   tv.tv_sec = COORDINATOR_PROBE_TIMEOUT;

   if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval)) == -1)
   {
      return 1;
   }

   if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval)) == -1)
   {
      return 1;
   }

   return 0;
}

static int
connect_node(int node, SSL** s_ssl, int* fd)
{
   SSL* s = NULL;
   char* username = NULL;
   char* password = NULL;
   size_t char_count;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   username = config->users[config->nodes[node].user_index].username;
   password = config->users[config->nodes[node].user_index].password;

   if (connect_timeout(config->nodes[node].host, config->nodes[node].management, fd))
   {
      pgagroal_log_debug("pgagroal-coordinator: Could not connect to node [%s] %s:%d",
                         config->nodes[node].name, config->nodes[node].host, config->nodes[node].management);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_DOWN);
      goto error;
   }

   if (set_probe_timeout(*fd))
   {
      pgagroal_log_debug("pgagroal-coordinator: Could not set the timeout for node [%s]", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_DOWN);
      goto error;
   }

   // Validate password is valid UTF-8
   if (!pgagroal_utf8_valid((const unsigned char*)password, strlen(password)))
   {
      pgagroal_log_debug("pgagroal-coordinator: Invalid UTF-8 encoding in password for node [%s]", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UNKNOWN);
      goto error;
   }

   // Check character length
   char_count = pgagroal_utf8_char_length((const unsigned char*)password, strlen(password));
   if (char_count == (size_t)-1)
   {
      pgagroal_log_debug("pgagroal-coordinator: Invalid UTF-8 encoding in password for node [%s]", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UNKNOWN);
      goto error;
   }

   if (strlen(password) >= MAX_PASSWORD_LENGTH)
   {
      pgagroal_log_debug("pgagroal-coordinator: Password too long (max %d bytes)", MAX_PASSWORD_LENGTH - 1);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UNKNOWN);
      goto error;
   }

   /* Authenticate */
   if (pgagroal_remote_management_scram_sha256(username, password, *fd, &s) != AUTH_SUCCESS)
   {
      pgagroal_log_error("pgagroal-coordinator: Bad credentials for %s on node [%s]", username, config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UNKNOWN);
      goto error;
   }

   *s_ssl = s;

   return 0;

error:

   disconnect_node(s, *fd);
   *fd = -1;

   return 1;
}

static void
disconnect_node(SSL* ssl, int fd)
{
   if (ssl != NULL)
   {
      SSL_shutdown(ssl);
      SSL_free(ssl);
   }

   if (fd != -1)
   {
      pgagroal_disconnect(fd);
   }
}

/**
 * Read the data plane port from the status response.
 *
 * pgagroal_json_get() cannot tell a missing key from a zero value, so the
 * typed variant is required here.
 */
static int
extract_port(struct json* response, int node)
{
   enum value_type type;
   uintptr_t value;
   int port;
   int management;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   value = pgagroal_json_get_typed(response, MANAGEMENT_ARGUMENT_PORT, &type);

   if (type == ValueNone)
   {
      pgagroal_log_warn("pgagroal-coordinator: Node [%s] did not report a port, upgrade pgagroal on that node",
                        config->nodes[node].name);
      return 1;
   }

   port = (int)(int64_t)value;

   if (port <= 0 || port > 65535)
   {
      pgagroal_log_warn("pgagroal-coordinator: Node [%s] reported an invalid port (%d)",
                        config->nodes[node].name, port);
      return 1;
   }

   management = (int)(int64_t)pgagroal_json_get(response, MANAGEMENT_ARGUMENT_MANAGEMENT);

   if (management > 0 && management != config->nodes[node].management)
   {
      pgagroal_log_warn("pgagroal-coordinator: Node [%s] reports management %d but was reached on %d, the reported port %d may not be reachable",
                        config->nodes[node].name, management, config->nodes[node].management, port);
   }

   atomic_store_explicit(&config->nodes[node].port, port, memory_order_release);

   return 0;
}

/**
 * Read the node role from the status response.
 *
 * Primary is a live pg_is_in_recovery() probe and is authoritative. It is
 * Unknown unless health_check_user is set on the node, in which case the
 * cached State is used instead.
 */
static void
extract_role(struct json* response, int node)
{
   struct json* servers = NULL;
   struct json_iterator* iter = NULL;
   int entries = 0;
   int strong = 0;
   signed char role = SERVER_NOTINIT;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   servers = (struct json*)pgagroal_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);

   if (servers == NULL)
   {
      atomic_store(&config->nodes[node].role, SERVER_NOTINIT);
      return;
   }

   if (pgagroal_json_iterator_create(servers, &iter))
   {
      atomic_store(&config->nodes[node].role, SERVER_NOTINIT);
      return;
   }

   while (pgagroal_json_iterator_next(iter))
   {
      struct json* server = (struct json*)iter->value->data;
      char* primary = NULL;
      char* state = NULL;
      signed char current = SERVER_NOTINIT;

      entries++;

      primary = (char*)pgagroal_json_get(server, MANAGEMENT_ARGUMENT_PRIMARY);
      state = (char*)pgagroal_json_get(server, MANAGEMENT_ARGUMENT_STATE);

      if (primary != NULL && !strcmp(primary, "Yes"))
      {
         current = SERVER_PRIMARY;
      }
      else if (primary != NULL && !strcmp(primary, "No"))
      {
         current = SERVER_REPLICA;
      }
      else if (state != NULL && !strcmp(state, "Primary"))
      {
         current = SERVER_PRIMARY;
      }
      else if (state != NULL && !strcmp(state, "Replica"))
      {
         current = SERVER_REPLICA;
      }
      else if (state != NULL && !strcmp(state, "Not init (primary)"))
      {
         current = SERVER_NOTINIT_PRIMARY;
      }
      else if (state != NULL && (!strcmp(state, "Failed") || !strcmp(state, "Failover")))
      {
         current = SERVER_FAILED;
      }

      if (current == SERVER_PRIMARY || current == SERVER_NOTINIT_PRIMARY)
      {
         strong++;
      }

      if (entries == 1 || current == SERVER_PRIMARY || current == SERVER_NOTINIT_PRIMARY)
      {
         role = current;
      }
   }

   pgagroal_json_iterator_destroy(iter);

   if (entries == 0)
   {
      pgagroal_log_warn("pgagroal-coordinator: Node [%s] has no servers", config->nodes[node].name);
      role = SERVER_NOTINIT;
   }
   else if (entries > 1)
   {
      pgagroal_log_warn("pgagroal-coordinator: Node [%s] has %d servers, expected one", config->nodes[node].name, entries);

      if (strong != 1)
      {
         role = SERVER_NOTINIT;
      }
   }

   atomic_store(&config->nodes[node].role, role);
}

int
pgagroal_coordinator_probe_node(int node)
{
   SSL* ssl = NULL;
   int fd = -1;
   struct json* read = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   if (node < 0 || node >= config->number_of_nodes || !config->nodes[node].valid)
   {
      return 1;
   }

   if (connect_node(node, &ssl, &fd))
   {
      return 1;
   }

   if (pgagroal_management_request_status(ssl, fd, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE,
                                          MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      pgagroal_log_debug("pgagroal-coordinator: Could not request the status of node [%s]", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_DOWN);
      goto error;
   }

   if (pgagroal_management_read_json(ssl, fd, NULL, NULL, &read))
   {
      pgagroal_log_debug("pgagroal-coordinator: Could not read the status of node [%s]", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_DOWN);
      goto error;
   }

   outcome = (struct json*)pgagroal_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   response = (struct json*)pgagroal_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);

   if (outcome == NULL || pgagroal_json_contains_key(outcome, MANAGEMENT_ARGUMENT_ERROR) || response == NULL)
   {
      pgagroal_log_debug("pgagroal-coordinator: Node [%s] reported a failure", config->nodes[node].name);
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_DOWN);
      goto error;
   }

   if (extract_port(response, node))
   {
      atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UNKNOWN);
      goto error;
   }

   extract_role(response, node);

   atomic_store(&config->nodes[node].reachable, SERVER_HEALTH_UP);

   pgagroal_json_destroy(read);
   disconnect_node(ssl, fd);

   return 0;

error:

   pgagroal_json_destroy(read);
   disconnect_node(ssl, fd);

   return 1;
}

int
pgagroal_coordinator_probe(void)
{
   int incumbent;
   int strong = 0;
   int weak = 0;
   int strong_node = -1;
   int weak_node = -1;
   bool unknown_primary = false;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   incumbent = pgagroal_coordinator_get_primary();

   for (int i = 0; i < config->number_of_nodes; i++)
   {
      if (!config->nodes[i].valid)
      {
         continue;
      }

      pgagroal_coordinator_probe_node(i);

      pgagroal_log_debug("pgagroal-coordinator: Node [%s] %s:%d role=%s health=%s port=%d",
                         config->nodes[i].name,
                         config->nodes[i].host,
                         config->nodes[i].management,
                         pgagroal_server_state_as_string(atomic_load(&config->nodes[i].role)),
                         health_as_string(atomic_load(&config->nodes[i].reachable)),
                         atomic_load(&config->nodes[i].port));

      if (atomic_load(&config->nodes[i].reachable) != SERVER_HEALTH_UP ||
          atomic_load(&config->nodes[i].port) <= 0)
      {
         continue;
      }

      if (atomic_load(&config->nodes[i].role) == SERVER_PRIMARY)
      {
         strong++;
         strong_node = i;
      }
      else if (atomic_load(&config->nodes[i].role) == SERVER_NOTINIT_PRIMARY)
      {
         weak++;
         weak_node = i;
         unknown_primary = true;
      }
      else if (atomic_load(&config->nodes[i].role) == SERVER_NOTINIT)
      {
         unknown_primary = true;
      }
   }

   if (unknown_primary)
   {
      pgagroal_log_warn("pgagroal-coordinator: One or more nodes could not report a live primary status, set health_check_user in the pgagroal.conf of each node");
   }

   if (strong >= 2)
   {
      pgagroal_log_error("pgagroal-coordinator: Split brain, %d nodes report being the primary, a switch-to is required", strong);

      if (incumbent >= 0)
      {
         pgagroal_log_error("pgagroal-coordinator: Keeping node [%s] as the primary", config->nodes[incumbent].name);
         return 0;
      }

      pgagroal_coordinator_set_primary(-1);
      return 1;
   }

   if (strong == 1)
   {
      if (incumbent >= 0 && incumbent != strong_node)
      {
         pgagroal_log_error("pgagroal-coordinator: Node [%s] reports being the primary, but node [%s] is in use, a switch-to is required",
                            config->nodes[strong_node].name, config->nodes[incumbent].name);
         return 0;
      }

      if (incumbent != strong_node)
      {
         pgagroal_log_info("pgagroal-coordinator: Node [%s] is the primary", config->nodes[strong_node].name);
      }

      pgagroal_coordinator_set_primary(strong_node);
      return 0;
   }

   if (weak == 1)
   {
      if (incumbent >= 0 && incumbent != weak_node)
      {
         return 0;
      }

      pgagroal_log_warn("pgagroal-coordinator: Node [%s] is the primary based on its configuration, not on a live probe",
                        config->nodes[weak_node].name);
      pgagroal_coordinator_set_primary(weak_node);
      return 0;
   }

   if (incumbent >= 0)
   {
      pgagroal_log_warn("pgagroal-coordinator: No node reports being the primary, node [%s] remains in use",
                        config->nodes[incumbent].name);
      return 0;
   }

   if (weak >= 2)
   {
      pgagroal_log_error("pgagroal-coordinator: %d nodes are configured as the primary, a switch-to is required", weak);
   }
   else
   {
      pgagroal_log_error("pgagroal-coordinator: No primary established, client connections are refused until a switch-to is performed");
   }

   pgagroal_coordinator_set_primary(-1);

   return 1;
}

int
pgagroal_coordinator_status(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption,
                            struct json* payload)
{
   int primary;
   struct json* response = NULL;
   struct json* nodes = NULL;
   time_t start_time;
   time_t end_time;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   start_time = time(NULL);

   if (pgagroal_management_create_response(payload, -1, &response))
   {
      pgagroal_management_response_error(ssl, client_fd, NULL, MANAGEMENT_ERROR_ALLOCATION,
                                         compression, encryption, payload);
      return 1;
   }

   primary = pgagroal_coordinator_get_primary();

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_SERVER_VERSION, (uintptr_t)PGAGROAL_VERSION, ValueString);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)(config->keep_running ? "Running" : "Shutdown"), ValueString);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_MODE, (uintptr_t)"read_write", ValueString);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_PORT, (uintptr_t)config->common.port, ValueInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_MANAGEMENT, (uintptr_t)config->management, ValueInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_ACTIVE_CONNECTIONS, (uintptr_t)pgagroal_coordinator_client_count(), ValueInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_NUMBER_OF_NODES, (uintptr_t)config->number_of_nodes, ValueInt32);
   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_CURRENT_PRIMARY,
                     (uintptr_t)(primary >= 0 ? config->nodes[primary].name : "None"), ValueString);

   pgagroal_json_create(&nodes);

   for (int i = 0; i < config->number_of_nodes; i++)
   {
      struct json* js = NULL;

      if (!config->nodes[i].valid)
      {
         continue;
      }

      pgagroal_json_create(&js);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->nodes[i].name, ValueString);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_HOST, (uintptr_t)config->nodes[i].host, ValueString);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_PORT, (uintptr_t)atomic_load(&config->nodes[i].port), ValueInt32);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_MANAGEMENT, (uintptr_t)config->nodes[i].management, ValueInt32);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_STATE,
                        (uintptr_t)pgagroal_server_state_as_string(atomic_load(&config->nodes[i].role)), ValueString);
      pgagroal_json_put(js, MANAGEMENT_ARGUMENT_HEALTH,
                        (uintptr_t)health_as_string(atomic_load(&config->nodes[i].reachable)), ValueString);

      pgagroal_json_put(nodes, config->nodes[i].name, (uintptr_t)js, ValueJSON);
   }

   pgagroal_json_put(response, MANAGEMENT_ARGUMENT_NODES, (uintptr_t)nodes, ValueJSON);

   end_time = time(NULL);

   if (pgagroal_management_response_ok(ssl, client_fd, start_time, end_time, compression, encryption, payload))
   {
      pgagroal_log_error("pgagroal-coordinator: Could not send the status");
      return 1;
   }

   return 0;
}

int
pgagroal_coordinator_switch(char* node, int* old_primary, int* new_primary)
{
   int old;
   int new;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   *old_primary = -1;
   *new_primary = -1;

   new = pgagroal_coordinator_get_node(node);
   if (new == -1)
   {
      pgagroal_log_warn("pgagroal-coordinator: Unknown node [%s]", node != NULL ? node : "");
      return 1;
   }

   old = pgagroal_coordinator_get_primary();

   *old_primary = old;
   *new_primary = new;

   if (old == new)
   {
      pgagroal_log_info("pgagroal-coordinator: Node [%s] is already the primary", config->nodes[new].name);
      return 0;
   }

   /* the target is probed before the switch is committed, so that a node
    * that cannot be reached does not take the client traffic away from a
    * node that serves it
    */
   pgagroal_coordinator_probe_node(new);

   if (atomic_load(&config->nodes[new].reachable) != SERVER_HEALTH_UP ||
       atomic_load(&config->nodes[new].port) <= 0)
   {
      pgagroal_log_error("pgagroal-coordinator: Node [%s] cannot be reached, the primary is unchanged",
                         config->nodes[new].name);
      return 1;
   }

   pgagroal_log_info("pgagroal-coordinator: Switching the primary from [%s] to [%s]",
                     old >= 0 ? config->nodes[old].name : "None",
                     config->nodes[new].name);

   pgagroal_coordinator_set_primary(new);

   /* refresh the view of the remaining nodes */
   pgagroal_coordinator_probe();

   if (old >= 0)
   {
      pgagroal_coordinator_client_kill(old);
   }

   return 0;
}

void
pgagroal_coordinator_management(int client_fd, char* address)
{
   int exit_code = 0;
   SSL* client_ssl = NULL;
   struct json* payload = NULL;
   struct json* header = NULL;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   int32_t id;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   pgagroal_start_logging();
   pgagroal_memory_init();

   if (pgagroal_coordinator_management_auth(client_fd, address, &client_ssl) != AUTH_SUCCESS)
   {
      pgagroal_log_debug("pgagroal-coordinator: Bad credentials from %s", address);
      exit_code = 1;
      goto done;
   }

   if (pgagroal_management_read_json(client_ssl, client_fd, &compression, &encryption, &payload))
   {
      pgagroal_log_error("pgagroal-coordinator: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
      exit_code = 1;
      goto done;
   }

   header = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   id = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);

   if (id == MANAGEMENT_STATUS || id == MANAGEMENT_DETAILS)
   {
      pgagroal_log_debug("pgagroal-coordinator: Management status");

      if (pgagroal_coordinator_status(client_ssl, client_fd, compression, encryption, payload))
      {
         exit_code = 1;
      }
   }
   else if (id == MANAGEMENT_SWITCH_TO)
   {
      struct json* request = NULL;
      char* server = NULL;
      enum value_type type;
      int old_primary = -1;
      int new_primary = -1;
      time_t start_time;
      time_t end_time;

      pgagroal_log_debug("pgagroal-coordinator: Management switch to");

      start_time = time(NULL);

      request = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      server = (char*)pgagroal_json_get_typed(request, MANAGEMENT_ARGUMENT_SERVER, &type);

      if (type != ValueString)
      {
         server = NULL;
      }

      if (!pgagroal_coordinator_switch(server, &old_primary, &new_primary))
      {
         end_time = time(NULL);
         pgagroal_management_response_ok(client_ssl, client_fd, start_time, end_time,
                                         compression, encryption, payload);
      }
      else
      {
         pgagroal_management_response_error(client_ssl, client_fd, NULL, MANAGEMENT_ERROR_SWITCH_TO_FAILED,
                                            compression, encryption, payload);
         exit_code = 1;
      }
   }
   else if (id == MANAGEMENT_PING)
   {
      struct json* response = NULL;
      time_t start_time;
      time_t end_time;

      pgagroal_log_debug("pgagroal-coordinator: Management ping");

      start_time = time(NULL);

      pgagroal_management_create_response(payload, -1, &response);

      end_time = time(NULL);
      pgagroal_management_response_ok(client_ssl, client_fd, start_time, end_time,
                                      compression, encryption, payload);
   }
   else if (id == MANAGEMENT_SHUTDOWN || id == MANAGEMENT_GRACEFULLY)
   {
      struct json* response = NULL;
      time_t start_time;
      time_t end_time;

      pgagroal_log_debug("pgagroal-coordinator: Management shutdown");

      start_time = time(NULL);

      config->gracefully = (id == MANAGEMENT_GRACEFULLY);
      config->keep_running = false;

      pgagroal_management_create_response(payload, -1, &response);

      end_time = time(NULL);
      pgagroal_management_response_ok(client_ssl, client_fd, start_time, end_time,
                                      compression, encryption, payload);

      kill(getppid(), SIGTERM);
   }
   else
   {
      pgagroal_log_warn("pgagroal-coordinator: Unknown command %d", id);
      pgagroal_management_response_error(client_ssl, client_fd, NULL, MANAGEMENT_ERROR_UNKNOWN_COMMAND,
                                         compression, encryption, payload);
      exit_code = 1;
   }

done:

   pgagroal_json_destroy(payload);
   payload = NULL;

   if (client_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(client_ssl);
      res = SSL_shutdown(client_ssl);
      if (res == 0)
      {
         SSL_shutdown(client_ssl);
      }
      SSL_free(client_ssl);
      SSL_CTX_free(ctx);
   }

   pgagroal_disconnect(client_fd);

   free(address);

   pgagroal_memory_destroy();
   pgagroal_stop_logging();

   exit(exit_code);
}

int
pgagroal_coordinator_get_node(char* name)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   if (name == NULL || strlen(name) == 0)
   {
      return -1;
   }

   for (int i = 0; i < config->number_of_nodes; i++)
   {
      if (config->nodes[i].valid && !strcmp(config->nodes[i].name, name))
      {
         return i;
      }
   }

   return -1;
}

int
pgagroal_coordinator_get_primary(void)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   return atomic_load_explicit(&config->current_primary, memory_order_acquire);
}

void
pgagroal_coordinator_set_primary(int node)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   atomic_store_explicit(&config->current_primary, node, memory_order_release);
}

int
pgagroal_coordinator_client_reserve(int node)
{
   int expected;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_COORDINATOR_CLIENTS; i++)
   {
      expected = 0;
      if (atomic_compare_exchange_strong(&config->clients[i].pid, &expected, -1))
      {
         atomic_store(&config->clients[i].node, node);
         return i;
      }
   }

   return -1;
}

void
pgagroal_coordinator_client_commit(int slot, pid_t pid)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   if (slot < 0 || slot >= NUMBER_OF_COORDINATOR_CLIENTS)
   {
      return;
   }

   atomic_store(&config->clients[slot].pid, (int)pid);
}

void
pgagroal_coordinator_client_release_slot(int slot)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   if (slot < 0 || slot >= NUMBER_OF_COORDINATOR_CLIENTS)
   {
      return;
   }

   atomic_store(&config->clients[slot].node, -1);
   atomic_store(&config->clients[slot].pid, 0);
}

void
pgagroal_coordinator_client_release(pid_t pid)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_COORDINATOR_CLIENTS; i++)
   {
      if (atomic_load(&config->clients[i].pid) == (int)pid)
      {
         atomic_store(&config->clients[i].node, -1);
         atomic_store(&config->clients[i].pid, 0);
         return;
      }
   }
}

void
pgagroal_coordinator_client_kill(int node)
{
   int pid;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_COORDINATOR_CLIENTS; i++)
   {
      pid = atomic_load(&config->clients[i].pid);

      if (pid > 0 && (node < 0 || atomic_load(&config->clients[i].node) == node))
      {
         pgagroal_log_debug("pgagroal-coordinator: Terminating client %d", pid);
         kill((pid_t)pid, SIGTERM);
      }
   }
}

int
pgagroal_coordinator_client_count(void)
{
   int count = 0;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_COORDINATOR_CLIENTS; i++)
   {
      if (atomic_load(&config->clients[i].pid) > 0)
      {
         count++;
      }
   }

   return count;
}
