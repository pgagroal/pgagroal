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
#include <configuration.h>
#include <coordinator.h>
#include <ev.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS        64
#define SIGNALS_NUMBER 3

static void accept_main_cb(struct io_watcher* watcher);
static void accept_management_cb(struct io_watcher* watcher);
static void shutdown_cb(void);
static void sigchld_cb(void);
static bool accept_fatal(int error);
static void start_io(void);
static void shutdown_io(void);
static void start_management(void);
static void shutdown_management(void);
static void shutdown_ports(void);
static void reset_signals(void);
static void wait_for_clients(void);
static int proxy(int client_fd, int node);
static int write_all(int fd, char* buffer, ssize_t length);
static void usage(void);

static char** argv_ptr;
static bool forked = false;
static struct event_loop* main_loop = NULL;
static struct accept_io io_main[MAX_FDS];
static struct accept_io io_management[MAX_FDS];
static int* main_fds = NULL;
static int main_fds_length = -1;
static int* management_fds = NULL;
static int management_fds_length = -1;

static void
usage(void)
{
   printf("pgagroal-coordinator %s\n", PGAGROAL_VERSION);
   printf("  Coordinator that fronts multiple pgagroal instances for high availability\n");
   printf("\n");
   printf("Usage:\n");
   printf("  pgagroal-coordinator [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -A ADMINS_FILE ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE   Set the path to the pgagroal-coordinator.conf file\n");
   printf("                             Default: %s\n", PGAGROAL_DEFAULT_COORDINATOR_CONF_FILE);
   printf("  -u, --users USERS_FILE     Set the path to the pgagroal-coordinator-users.conf file\n");
   printf("                             Default: %s\n", PGAGROAL_DEFAULT_COORDINATOR_USERS_FILE);
   printf("  -A, --admins ADMINS_FILE   Set the path to the pgagroal-coordinator-admins.conf file\n");
   printf("                             Default: %s\n", PGAGROAL_DEFAULT_COORDINATOR_ADMINS_FILE);
   printf("  -d, --daemon               Run as a daemon\n");
   printf("  -V, --version              Display version information\n");
   printf("  -?, --help                 Display help\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
{
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* users_path = NULL;
   char* admins_path = NULL;
   bool conf_file_mandatory;
   struct signal_info signal_watcher[SIGNALS_NUMBER];
   int c;
   int option_index = 0;
   size_t size;
   int daemon = 0;
   pid_t pid, sid;
   struct coordinator_configuration* config = NULL;
   char message[MISC_LENGTH];

   argv_ptr = argv;

   while (1)
   {
      static struct option long_options[] = {
         {"config", required_argument, 0, 'c'},
         {"users", required_argument, 0, 'u'},
         {"admins", required_argument, 0, 'A'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'},
         {0, 0, 0, 0}};

      c = getopt_long(argc, argv, "c:u:A:dV?", long_options, &option_index);
      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'u':
            users_path = optarg;
            break;
         case 'A':
            admins_path = optarg;
            break;
         case 'd':
            daemon = 1;
            break;
         case 'V':
            printf("pgagroal-coordinator %s\n", PGAGROAL_VERSION);
            exit(0);
            break;
         case '?':
            usage();
            exit(0);
            break;
         default:
            usage();
            exit(1);
            break;
      }
   }

   if (getuid() == 0)
   {
      errx(1, "pgagroal-coordinator: Using the root account is not allowed");
   }

   size = sizeof(struct coordinator_configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      errx(1, "pgagroal-coordinator: Error creating shared memory");
   }

   memset(message, 0, MISC_LENGTH);

   pgagroal_coordinator_init_configuration(shmem);
   config = (struct coordinator_configuration*)shmem;

   configuration_path = configuration_path != NULL ? configuration_path : PGAGROAL_DEFAULT_COORDINATOR_CONF_FILE;
   if ((ret = pgagroal_coordinator_read_configuration(shmem, configuration_path, true)) != PGAGROAL_CONFIGURATION_STATUS_OK)
   {
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "Configuration file not found");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "Too many sections");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "Invalid configuration file");
      }
      else if (ret > 0)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "%d problematic or duplicated section%c",
                           ret, ret > 1 ? 's' : ' ');
      }

      errx(1, "pgagroal-coordinator: %s (file <%s>)", message, configuration_path);
   }

   memcpy(config->common.configuration_path, configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   if (pgagroal_init_logging())
   {
      exit(1);
   }

   if (pgagroal_start_logging())
   {
      errx(1, "Failed to start logging");
   }

   conf_file_mandatory = true;
read_users_path:
   if (users_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_coordinator_read_users_configuration(shmem, users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "USERS configuration file not found");
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO || ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "Invalid master key file");
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "USERS: too many users defined (max %d)", NUMBER_OF_NODES);
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, users_path);
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      users_path = PGAGROAL_DEFAULT_COORDINATOR_USERS_FILE;
      conf_file_mandatory = false;
      goto read_users_path;
   }

   conf_file_mandatory = true;
read_admins_path:
   if (admins_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_coordinator_read_admins_configuration(shmem, admins_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "ADMINS configuration file not found");
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO || ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "Invalid master key file");
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         pgagroal_snprintf(message, MISC_LENGTH, "ADMINS: too many admins defined (max %d)", NUMBER_OF_ADMINS);
         errx(1, "pgagroal-coordinator: %s (file <%s>)", message, admins_path);
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      admins_path = PGAGROAL_DEFAULT_COORDINATOR_ADMINS_FILE;
      conf_file_mandatory = false;
      goto read_admins_path;
   }

   if (pgagroal_coordinator_validate_configuration(shmem))
   {
      errx(1, "pgagroal-coordinator: Invalid coordinator configuration");
   }

   config = (struct coordinator_configuration*)shmem;

   if (daemon)
   {
      if (config->common.log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
      {
         errx(1, "pgagroal-coordinator: Daemon mode can't be used with console logging");
      }

      pid = fork();
      if (pid < 0)
      {
         errx(1, "pgagroal-coordinator: Daemon mode failed");
      }

      if (pid > 0)
      {
         exit(0);
      }

      umask(0);
      sid = setsid();
      if (sid < 0)
      {
         exit(1);
      }
   }

   /* Bind the data-plane listener (clients connect here) */
   if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, false, config->backlog))
   {
      errx(1, "pgagroal-coordinator: Could not bind to %s:%d", config->common.host, config->common.port);
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal-coordinator: Too many descriptors %d", main_fds_length);
      exit(1);
   }

   /* Bind the management listener (pgagroal-cli connects here) */
   if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, false, config->backlog))
   {
      errx(1, "pgagroal-coordinator: Could not bind to %s:%d", config->common.host, config->management);
   }

   if (management_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal-coordinator: Too many descriptors %d", management_fds_length);
      exit(1);
   }

   /* Initialize the event loop */
   pgagroal_event_set_context(PGAGROAL_CONTEXT_COORDINATOR);
   main_loop = pgagroal_event_loop_init();
   if (!main_loop)
   {
      errx(1, "pgagroal-coordinator: No loop implementation");
   }

   /* A client that closes the connection while it is being refused would
    * otherwise terminate the coordinator, as the refusal is written from
    * this process and not from a child
    */
   signal(SIGPIPE, SIG_IGN);

   pgagroal_signal_init((struct signal_watcher*)&signal_watcher[0], shutdown_cb, SIGTERM);
   pgagroal_signal_init((struct signal_watcher*)&signal_watcher[1], shutdown_cb, SIGINT);
   pgagroal_signal_init((struct signal_watcher*)&signal_watcher[2], sigchld_cb, SIGCHLD);

   for (int i = 0; i < SIGNALS_NUMBER; i++)
   {
      signal_watcher[i].slot = -1;
      pgagroal_signal_start((struct signal_watcher*)&signal_watcher[i]);
   }

   start_io();
   start_management();

   pgagroal_log_info("pgagroal-coordinator %s: Started on %s:%d (management %d)",
                     PGAGROAL_VERSION,
                     config->common.host,
                     config->common.port,
                     config->management);

   /* Learn the roles / data-plane ports of the nodes and pick the primary.
    * The listeners are already up, so an unreachable node cannot keep the
    * management endpoint from accepting a switch-to.
    */
   if (pgagroal_coordinator_probe())
   {
      pgagroal_log_warn("pgagroal-coordinator: Initial probe did not establish a primary");
   }

   for (int i = 0; i < main_fds_length; i++)
   {
      pgagroal_log_debug("Client socket: %d", *(main_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgagroal_log_debug("Management socket: %d", *(management_fds + i));
   }

   pgagroal_event_loop_run();

   pgagroal_log_info("pgagroal-coordinator: shutdown");

   config->keep_running = false;

   shutdown_ports();

   if (config->gracefully)
   {
      wait_for_clients();
   }

   pgagroal_coordinator_client_kill(-1);

   /* the watchers have to be stopped before the shared memory goes away,
    * since the callbacks read it
    */
   for (int i = 0; i < SIGNALS_NUMBER; i++)
   {
      pgagroal_signal_stop((struct signal_watcher*)&signal_watcher[i]);
   }

   while (waitpid(-1, NULL, WNOHANG) > 0)
   {
      ;
   }

   pgagroal_event_loop_destroy();

   free(main_fds);
   free(management_fds);
   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(shmem, size);

   return exit_code;
}

/**
 * Wait for the client connections to end, so that a graceful shutdown does not
 * interrupt them. Bounded, since the coordinator cannot know when a session ends.
 */
static void
wait_for_clients(void)
{
   int count;

   for (int i = 0; i < COORDINATOR_SHUTDOWN_TIMEOUT; i++)
   {
      while (waitpid(-1, NULL, WNOHANG) > 0)
      {
         ;
      }

      count = pgagroal_coordinator_client_count();
      if (count == 0)
      {
         return;
      }

      if (i == 0)
      {
         pgagroal_log_info("pgagroal-coordinator: Waiting for %d client connection%s",
                           count, count > 1 ? "s" : "");
      }

      SLEEP(1000000000L);
   }

   pgagroal_log_warn("pgagroal-coordinator: Timeout while waiting for the client connections");
}

static void
shutdown_cb(void)
{
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   pgagroal_log_debug("pgagroal-coordinator: Shutdown requested");
   config->keep_running = false;
   pgagroal_event_loop_break();
}

static void
sigchld_cb(void)
{
   pid_t pid;

   while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
   {
      pgagroal_coordinator_client_release(pid);
   }
}

/*
 * The signal dispositions installed by the event loop are inherited across
 * fork, so a child would run the parent callbacks instead of terminating.
 */
static void
reset_signals(void)
{
   signal(SIGTERM, SIG_DFL);
   signal(SIGINT, SIG_DFL);
   signal(SIGCHLD, SIG_DFL);
   signal(SIGPIPE, SIG_IGN);
}

/*
 * Data-plane accept: route a client connection to the current primary.
 * v1 policy is route = read_write, so every client is proxied to the
 * pgagroal instance currently marked as primary.
 */
static void
accept_main_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   int primary;
   int slot;
   pid_t pid;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   memset(&address, 0, sizeof(address));

   client_fd = watcher->fds.main.client_fd;

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("accept_main_cb: Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_io();

         free(main_fds);
         main_fds = NULL;

         if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, false, config->backlog))
         {
            pgagroal_log_fatal("pgagroal-coordinator: Could not bind to %s:%d", config->common.host, config->common.port);
            exit(1);
         }

         start_io();
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   memset(&client_addr, 0, sizeof(struct sockaddr_in6));
   client_addr_length = sizeof(struct sockaddr_in6);
   getpeername(client_fd, (struct sockaddr*)&client_addr, &client_addr_length);
   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   pgagroal_log_trace("accept_main_cb: client address: %s", address);

   primary = pgagroal_coordinator_get_primary();
   if (primary < 0)
   {
      pgagroal_log_warn("accept_main_cb: No current primary to route %s to", address);
      pgagroal_write_connection_refused(NULL, client_fd);
      pgagroal_write_empty(NULL, client_fd);
      pgagroal_disconnect(client_fd);
      return;
   }

   /* the slot is claimed before the fork so that a concurrent switch-to
    * cannot miss a connection that has already been routed */
   slot = pgagroal_coordinator_client_reserve(primary);
   if (slot == -1)
   {
      pgagroal_log_warn("accept_main_cb: No available client slot for %s", address);
      pgagroal_write_connection_refused(NULL, client_fd);
      pgagroal_write_empty(NULL, client_fd);
      pgagroal_disconnect(client_fd);
      return;
   }

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("accept_main_cb: Couldn't create process");
      pgagroal_coordinator_client_release_slot(slot);
      pgagroal_disconnect(client_fd);
      return;
   }
   else if (pid == 0)
   {
      /* Prevent SIGINT sent to the parent's controlling terminal
       * from irradiating to the children
       */
      if (setpgid(0, 0) == -1)
      {
         pgagroal_log_error("accept_main_cb: setpgid: %s", strerror(errno));
         exit(1);
      }

      forked = true;
      pgagroal_event_loop_fork();
      shutdown_ports();
      reset_signals();

      exit(proxy(client_fd, primary));
   }

   pgagroal_coordinator_client_commit(slot, pid);

   /* the slot carried no process while the child was being created, so a
    * switch-to performed in that window could not terminate it
    */
   if (pgagroal_coordinator_get_primary() != primary)
   {
      pgagroal_coordinator_client_kill(primary);
   }

   pgagroal_disconnect(client_fd);
}

/*
 * Management accept: the coordinator speaks the same management protocol as
 * pgagroal core, so switch-to / status are handled here by the endpoint being
 * hit - there are no coordinator-specific commands.
 */
static void
accept_management_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   pid_t pid;
   struct coordinator_configuration* config;

   config = (struct coordinator_configuration*)shmem;

   memset(&address, 0, sizeof(address));

   client_fd = watcher->fds.main.client_fd;

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("accept_management_cb: Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, false, config->backlog))
         {
            pgagroal_log_fatal("pgagroal-coordinator: Could not bind to %s:%d", config->common.host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal-coordinator: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   memset(&client_addr, 0, sizeof(struct sockaddr_in6));
   client_addr_length = sizeof(struct sockaddr_in6);
   getpeername(client_fd, (struct sockaddr*)&client_addr, &client_addr_length);
   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   pid = fork();
   if (pid == -1)
   {
      pgagroal_log_error("accept_management_cb: Couldn't create process");
      pgagroal_disconnect(client_fd);
      return;
   }
   else if (pid == 0)
   {
      char* addr = NULL;

      forked = true;
      pgagroal_event_loop_fork();
      shutdown_ports();
      reset_signals();

      addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("pgagroal-coordinator: Couldn't allocate memory for the address");
         pgagroal_disconnect(client_fd);
         exit(1);
      }
      memcpy(addr, address, strlen(address));

      /* takes ownership of the descriptor and of the address, and exits */
      pgagroal_coordinator_management(client_fd, addr);

      exit(1);
   }

   pgagroal_disconnect(client_fd);
}

/*
 * Transparent byte proxy between the client and the current primary's pgagroal.
 * v1 does not parse the PostgreSQL protocol - it forwards bytes both ways until
 * either side closes.
 */
static int
proxy(int client_fd, int node)
{
   struct coordinator_configuration* config;
   char host[MISC_LENGTH];
   int port;
   int upstream_fd = -1;
   struct pollfd fds[2];
   bool client_eof = false;
   bool upstream_eof = false;
   uint64_t sent = 0;
   uint64_t received = 0;
   char* reason = "client_eof";
   static char buffer[DEFAULT_BUFFER_SIZE];

   config = (struct coordinator_configuration*)shmem;

   if (node < 0 || node >= config->number_of_nodes)
   {
      pgagroal_log_warn("proxy: No current primary to route to");
      pgagroal_write_connection_refused(NULL, client_fd);
      pgagroal_write_empty(NULL, client_fd);
      pgagroal_disconnect(client_fd);
      return 1;
   }

   memset(&host, 0, sizeof(host));
   memcpy(&host, config->nodes[node].host, strlen(config->nodes[node].host));
   port = atomic_load(&config->nodes[node].port);

   if (port <= 0)
   {
      pgagroal_log_warn("proxy: Node [%s] has no data plane port", config->nodes[node].name);
      pgagroal_write_connection_refused(NULL, client_fd);
      pgagroal_write_empty(NULL, client_fd);
      pgagroal_disconnect(client_fd);
      return 1;
   }

   pgagroal_tcp_nodelay(client_fd);

   if (pgagroal_connect(host, port, &upstream_fd, false, true))
   {
      pgagroal_log_error("proxy: Could not connect to node [%s] %s:%d",
                         config->nodes[node].name, host, port);
      pgagroal_write_connection_refused(NULL, client_fd);
      pgagroal_write_empty(NULL, client_fd);
      pgagroal_disconnect(client_fd);
      return 1;
   }

   if (config->common.log_connections)
   {
      pgagroal_log_info("coordinator: connect node=%s upstream=%s:%d pid=%d",
                        config->nodes[node].name, host, port, (int)getpid());
   }

   while (!client_eof || !upstream_eof)
   {
      memset(fds, 0, sizeof(fds));
      fds[0].fd = client_eof ? -1 : client_fd;
      fds[0].events = POLLIN;
      fds[1].fd = upstream_eof ? -1 : upstream_fd;
      fds[1].events = POLLIN;

      if (poll(fds, 2, -1) < 0)
      {
         if (errno == EINTR)
         {
            continue;
         }
         reason = "poll_error";
         break;
      }

      if ((fds[0].revents | fds[1].revents) & (POLLERR | POLLNVAL))
      {
         reason = "socket_error";
         break;
      }

      /* client -> node */
      if (fds[0].revents & (POLLIN | POLLHUP))
      {
         ssize_t r = read(client_fd, buffer, sizeof(buffer));

         if (r > 0)
         {
            if (write_all(upstream_fd, buffer, r))
            {
               reason = "write_error";
               break;
            }
            sent += (uint64_t)r;
         }
         else if (r == 0)
         {
            client_eof = true;
            reason = "client_eof";
            shutdown(upstream_fd, SHUT_WR);
         }
         else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
         {
            reason = "read_error";
            break;
         }
      }

      /* node -> client */
      if (fds[1].revents & (POLLIN | POLLHUP))
      {
         ssize_t r = read(upstream_fd, buffer, sizeof(buffer));

         if (r > 0)
         {
            if (write_all(client_fd, buffer, r))
            {
               reason = "write_error";
               break;
            }
            received += (uint64_t)r;
         }
         else if (r == 0)
         {
            upstream_eof = true;
            reason = "upstream_eof";
            shutdown(client_fd, SHUT_WR);
         }
         else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
         {
            reason = "read_error";
            break;
         }
      }
   }

   if (config->common.log_disconnections)
   {
      pgagroal_log_info("coordinator: disconnect node=%s sent=%" PRIu64 " received=%" PRIu64 " reason=%s",
                        config->nodes[node].name, sent, received, reason);
   }

   pgagroal_disconnect(upstream_fd);
   pgagroal_disconnect(client_fd);

   return 0;
}

/* Write all bytes to fd, handling partial writes. Returns 0 on success, 1 on error. */
static int
write_all(int fd, char* buffer, ssize_t length)
{
   ssize_t offset = 0;

   while (offset < length)
   {
      ssize_t w = write(fd, buffer + offset, length - offset);
      if (w <= 0)
      {
         if (w < 0 && errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      offset += w;
   }

   return 0;
}

static void
start_io(void)
{
   for (int i = 0; i < main_fds_length; i++)
   {
      int sockfd = *(main_fds + i);

      memset(&io_main[i], 0, sizeof(struct accept_io));
      pgagroal_event_accept_init(&io_main[i].watcher, sockfd, accept_main_cb);
      io_main[i].socket = sockfd;
      io_main[i].argv = argv_ptr;
      pgagroal_io_start(&io_main[i].watcher);
   }
}

static void
shutdown_io(void)
{
   for (int i = 0; i < main_fds_length; i++)
   {
      if (!forked)
      {
         pgagroal_io_stop(&io_main[i].watcher);
      }
      pgagroal_disconnect(io_main[i].socket);
      errno = 0;
   }
}

static void
start_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      int sockfd = *(management_fds + i);

      memset(&io_management[i], 0, sizeof(struct accept_io));
      pgagroal_event_accept_init(&io_management[i].watcher, sockfd, accept_management_cb);
      io_management[i].socket = sockfd;
      io_management[i].argv = argv_ptr;
      pgagroal_io_start(&io_management[i].watcher);
   }
}

static void
shutdown_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      if (!forked)
      {
         pgagroal_io_stop(&io_management[i].watcher);
      }
      pgagroal_disconnect(io_management[i].socket);
      errno = 0;
   }
}

static void
shutdown_ports(void)
{
   shutdown_io();
   shutdown_management();
}

static bool
accept_fatal(int error)
{
   switch (error)
   {
      case EAGAIN:
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
#ifdef HAVE_LINUX
      case ENONET:
#endif
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
         return false;
         break;
   }

   return true;
}
