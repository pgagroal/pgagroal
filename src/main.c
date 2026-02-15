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
#include <connection.h>
#include <json.h>
#include <ev.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <pipeline.h>
#include <pool.h>
#include <prometheus.h>
#include <remote.h>
#include <security.h>
#include <server.h>
#include <shmem.h>
#include <status.h>
#include <utils.h>
#include <worker.h>

/* system */
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define MAX_FDS        64
#define SIGNALS_NUMBER 8

static void accept_main_cb(struct io_watcher* watcher);
static void accept_mgt_cb(struct io_watcher* watcher);
static void accept_transfer_cb(struct io_watcher* watcher);
static void accept_metrics_cb(struct io_watcher* watcher);
static void accept_management_cb(struct io_watcher* watcher);
static void shutdown_cb(void);
static void reload_cb(void);
static void service_reload_cb(void);
static void graceful_cb(void);
static void coredump_cb(void);
static void sigchld_cb(void);
static void idle_timeout_cb(void);
static void max_connection_age_cb(void);
static void rotate_frontend_password_cb(void);
static void validation_cb(void);
static void disconnect_client_cb(void);
static void frontend_user_password_startup(struct main_configuration* config);
static bool accept_fatal(int error);
static void add_client(pid_t pid);
static void remove_client(pid_t pid);
static bool reload_configuration(void);
static bool reload_services_only(void);
static void reload_set_configuration(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);
static void create_pidfile_or_exit(void);
static void remove_pidfile(void);
static void shutdown_ports(void);

static char** argv_ptr;
static struct event_loop* main_loop = NULL;
static struct accept_io io_main[MAX_FDS];
static struct accept_io io_mgt;
static struct accept_io io_uds;
static int* main_fds = NULL;
static int main_fds_length = -1;
static int unix_management_socket = -1;
static int unix_transfer_socket = -1;
static int unix_pgsql_socket = -1;
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;
static struct pipeline main_pipeline;
static int known_fds[MAX_NUMBER_OF_CONNECTIONS];
static struct client* clients = NULL;
static struct accept_io io_transfer;

static void
start_mgt(void)
{
   memset(&io_mgt, 0, sizeof(struct accept_io));
   pgagroal_event_accept_init(&io_mgt.watcher, unix_management_socket, accept_mgt_cb);
   io_mgt.socket = unix_management_socket;
   io_mgt.argv = argv_ptr;
   pgagroal_io_start(&io_mgt.watcher);
}

static void
shutdown_mgt(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_disconnect(unix_management_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, MAIN_UDS);
   errno = 0;
}

static void
start_transfer(void)
{
   memset(&io_transfer, 0, sizeof(struct accept_io));
   pgagroal_event_accept_init(&io_transfer.watcher, unix_transfer_socket, accept_transfer_cb);
   io_transfer.socket = unix_transfer_socket;
   io_transfer.argv = argv_ptr;
   pgagroal_io_start(&io_transfer.watcher);
}

static void
shutdown_transfer(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_disconnect(unix_transfer_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, TRANSFER_UDS);
   errno = 0;
}

static void
start_uds(void)
{
   memset(&io_uds, 0, sizeof(struct accept_io));
   pgagroal_event_accept_init(&io_uds.watcher, unix_pgsql_socket, accept_main_cb);
   io_uds.socket = unix_pgsql_socket;
   io_uds.argv = argv_ptr;
   pgagroal_io_start(&io_uds.watcher);
}

static void
shutdown_uds(void)
{
   char pgsql[MISC_LENGTH];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

   pgagroal_disconnect(unix_pgsql_socket);
   errno = 0;
   pgagroal_remove_unix_socket(config->unix_socket_dir, &pgsql[0]);
   errno = 0;
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
      pgagroal_disconnect(io_main[i].socket);
      errno = 0;
   }
}

static void
start_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      int sockfd = *(metrics_fds + i);

      memset(&io_metrics[i], 0, sizeof(struct accept_io));
      pgagroal_event_accept_init(&io_metrics[i].watcher, sockfd, accept_metrics_cb);
      io_metrics[i].socket = sockfd;
      io_metrics[i].argv = argv_ptr;
      pgagroal_io_start(&io_metrics[i].watcher);
   }
}

static void
shutdown_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_disconnect(io_metrics[i].socket);
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
      pgagroal_disconnect(io_management[i].socket);
      errno = 0;
   }
}

static void
version(void)
{
   printf("pgagroal %s\n", PGAGROAL_VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal %s\n", PGAGROAL_VERSION);
   printf("  High-performance connection pool for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE           Set the path to the pgagroal.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_CONF_FILE);
   printf("  -a, --hba HBA_FILE                 Set the path to the pgagroal_hba.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_HBA_FILE);
   printf("  -l, --limit LIMIT_FILE             Set the path to the pgagroal_databases.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_LIMIT_FILE);
   printf("  -u, --users USERS_FILE             Set the path to the pgagroal_users.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_USERS_FILE);
   printf("  -F, --frontend FRONTEND_USERS_FILE Set the path to the pgagroal_frontend_users.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_FRONTEND_USERS_FILE);
   printf("  -A, --admins ADMINS_FILE           Set the path to the pgagroal_admins.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_ADMINS_FILE);
   printf("  -S, --superuser SUPERUSER_FILE     Set the path to the pgagroal_superuser.conf file\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_SUPERUSER_FILE);
   printf("  -D, --directory DIRECTORY_PATH     Set the directory path to load configuration files\n");
   printf("                                     Default: %s\n", PGAGROAL_DEFAULT_CONFIGURATION_PATH);
   printf("                                     Can also be set via PGAGROAL_CONFIG_DIR environment variable\n");
   printf("  -d, --daemon                       Run as a daemon\n");
   printf("  -V, --version                      Display version information\n");
   printf("  -?, --help                         Display help\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
{
   char* configuration_path = NULL;
   char* hba_path = NULL;
   char* limit_path = NULL;
   char* users_path = NULL;
   char* frontend_users_path = NULL;
   char* admins_path = NULL;
   char* superuser_path = NULL;
   char* directory_path = NULL;
   bool daemon = false;
   pid_t pid, sid;
   char config_path_buffer[MAX_PATH];
   char hba_path_buffer[MAX_PATH];
   char limit_path_buffer[MAX_PATH];
   char users_path_buffer[MAX_PATH];
   char frontend_users_path_buffer[MAX_PATH];
   char admins_path_buffer[MAX_PATH];
   char superuser_path_buffer[MAX_PATH];
#ifdef HAVE_SYSTEMD
   int sds;
#endif
   bool has_unix_socket = false;
   bool has_main_sockets = false;
   void* tmp_shmem = NULL;
   struct signal_info signal_watcher[SIGNALS_NUMBER];
   struct periodic_watcher idle_timeout;
   struct periodic_watcher max_connection_age;
   struct periodic_watcher validation;
   struct periodic_watcher disconnect_client;
   struct periodic_watcher rotate_frontend_password;
   struct rlimit flimit;
   size_t shmem_size;
   size_t pipeline_shmem_size = 0;
   size_t prometheus_shmem_size = 0;
   size_t prometheus_cache_shmem_size = 0;
   size_t tmp_size;
   struct main_configuration* config = NULL;
   int ret;
   int c;
   char* os = NULL;

   int kernel_major, kernel_minor, kernel_patch;
   bool conf_file_mandatory;
   char message[MISC_LENGTH]; // a generic message used for errors
   argv_ptr = argv;

   struct stat path_stat = {0};
   char* adjusted_dir_path = NULL;

   while (1)
   {
      // clang-format off
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"hba", required_argument, 0, 'a'},
         {"limit", required_argument, 0, 'l'},
         {"users", required_argument, 0, 'u'},
         {"frontend", required_argument, 0, 'F'},
         {"admins", required_argument, 0, 'A'},
         {"superuser", required_argument, 0, 'S'},
         {"directory", required_argument, 0, 'D'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      // clang-format on
      int option_index = 0;

      c = getopt_long(argc, argv, "dV?a:c:l:u:F:A:S:D:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'a':
            hba_path = optarg;
            break;
         case 'c':
            configuration_path = optarg;
            break;
         case 'l':
            limit_path = optarg;
            break;
         case 'u':
            users_path = optarg;
            break;
         case 'F':
            frontend_users_path = optarg;
            break;
         case 'A':
            admins_path = optarg;
            break;
         case 'S':
            superuser_path = optarg;
            break;
         case 'D':
            directory_path = optarg;
            break;
         case 'd':
            daemon = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(1);
            break;
         default:
            break;
      }
   }

   if (getuid() == 0)
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      errx(1, "Using the root account is not allowed");
   }

   shmem_size = sizeof(struct main_configuration);
   if (pgagroal_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      errx(1, "Error in creating shared memory");
   }

   pgagroal_init_configuration(shmem);
   config = (struct main_configuration*)shmem;

   memset(&known_fds, 0, sizeof(known_fds));
   memset(message, 0, MISC_LENGTH);

   if (directory_path == NULL)
   {
      // Check for environment variable if no -D flag provided
      directory_path = getenv("PGAGROAL_CONFIG_DIR");
      if (directory_path != NULL)
      {
         pgagroal_log_info("Configuration directory set via PGAGROAL_CONFIG_DIR environment variable: %s", directory_path);
      }
   }

   if (directory_path != NULL)
   {
      if (!strcmp(directory_path, PGAGROAL_DEFAULT_CONFIGURATION_PATH))
      {
         pgagroal_log_warn("Using the default configuration directory %s, -D can be omitted.", directory_path);
      }

      if (access(directory_path, F_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Configuration directory not found: %s", directory_path);
#endif
         pgagroal_log_error("Configuration directory not found: %s", directory_path);
         exit(1);
      }

      if (stat(directory_path, &path_stat) == 0)
      {
         if (!S_ISDIR(path_stat.st_mode))
         {
#ifdef HAVE_SYSTEMD
            sd_notify(0, "STATUS=Path is not a directory: %s", directory_path);
#endif
            pgagroal_log_error("Path is not a directory: %s", directory_path);
            exit(1);
         }
      }

      if (access(directory_path, R_OK | X_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Insufficient permissions for directory: %s", directory_path);
#endif
         pgagroal_log_error("Insufficient permissions for directory: %s", directory_path);
         exit(1);
      }

      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         adjusted_dir_path = pgagroal_append(strdup(directory_path), "/");
      }
      else
      {
         adjusted_dir_path = strdup(directory_path);
      }

      if (adjusted_dir_path == NULL)
      {
         pgagroal_log_error("Memory allocation failed while copying directory path.");
         exit(1);
      }

      if (!configuration_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal.conf", PGAGROAL_DEFAULT_CONF_FILE, config_path_buffer, sizeof(config_path_buffer)) == 0 && strlen(config_path_buffer) > 0)
      {
         configuration_path = config_path_buffer;
      }

      if (!hba_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_hba.conf", PGAGROAL_DEFAULT_HBA_FILE, hba_path_buffer, sizeof(hba_path_buffer)) == 0 && strlen(hba_path_buffer) > 0)
      {
         hba_path = hba_path_buffer;
      }

      if (!limit_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_databases.conf", PGAGROAL_DEFAULT_LIMIT_FILE, limit_path_buffer, sizeof(limit_path_buffer)) == 0 && strlen(limit_path_buffer) > 0)
      {
         limit_path = limit_path_buffer;
      }

      if (!users_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_users.conf", PGAGROAL_DEFAULT_USERS_FILE, users_path_buffer, sizeof(users_path_buffer)) == 0 && strlen(users_path_buffer) > 0)
      {
         users_path = users_path_buffer;
      }

      if (!frontend_users_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_frontend_users.conf", PGAGROAL_DEFAULT_FRONTEND_USERS_FILE, frontend_users_path_buffer, sizeof(frontend_users_path_buffer)) == 0 && strlen(frontend_users_path_buffer) > 0)
      {
         frontend_users_path = frontend_users_path_buffer;
      }

      if (!admins_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_admins.conf", PGAGROAL_DEFAULT_ADMINS_FILE, admins_path_buffer, sizeof(admins_path_buffer)) == 0 && strlen(admins_path_buffer) > 0)
      {
         admins_path = admins_path_buffer;
      }

      if (!superuser_path && pgagroal_normalize_path(adjusted_dir_path, "pgagroal_superuser.conf", PGAGROAL_DEFAULT_SUPERUSER_FILE, superuser_path_buffer, sizeof(superuser_path_buffer)) == 0 && strlen(superuser_path_buffer) > 0)
      {
         superuser_path = superuser_path_buffer;
      }

      free(adjusted_dir_path);
   }

   // the main configuration file is mandatory!
   configuration_path = configuration_path != NULL ? configuration_path : PGAGROAL_DEFAULT_CONF_FILE;

   int cfg_ret = pgagroal_validate_config_file(configuration_path);

   if (cfg_ret)
   {
      switch (cfg_ret)
      {
         case ENOENT:
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Configuration file not found or not a regular file: %s", configuration_path);
#endif
            errx(1, "Configuration file not found or not a regular file: %s", configuration_path);
            break;

         case EACCES:
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Can't read configuration file: %s", configuration_path);
#endif
            errx(1, "Can't read configuration file: %s", configuration_path);
            break;

         case EINVAL:
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Configuration file contains binary data or invalid path: %s", configuration_path);
#endif
            errx(1, "Configuration file contains binary data or invalid path: %s", configuration_path);
            break;

         default:
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Configuration file validation failed: %s", configuration_path);
#endif
            errx(1, "Configuration file validation failed: %s", configuration_path);
      }
   }

   if ((ret = pgagroal_read_configuration(shmem, configuration_path, true)) != PGAGROAL_CONFIGURATION_STATUS_OK)
   {
      // the configuration has some problem, build up a descriptive message
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         snprintf(message, MISC_LENGTH, "Configuration file not found");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many sections");
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
         snprintf(message, MISC_LENGTH, "Invalid configuration file");
      }
      else if (ret > 0)
      {
         snprintf(message, MISC_LENGTH, "%d problematic or duplicated section%c",
                  ret,
                  ret > 1 ? 's' : ' ');
      }

#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=%s: %s", message, configuration_path);
#endif
      errx(1, "%s (file <%s>)", message, configuration_path);
   }

   memcpy(&config->common.configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   // the HBA file is mandatory!
   hba_path = hba_path != NULL ? hba_path : PGAGROAL_DEFAULT_HBA_FILE;
   memset(message, 0, MISC_LENGTH);
   ret = pgagroal_read_hba_configuration(shmem, hba_path);
   if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
   {
      snprintf(message, MISC_LENGTH, "HBA configuration file not found");
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=%s: %s", message, hba_path);
#endif
      errx(1, "%s (file <%s>)", message, hba_path);
   }
   else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
   {
      snprintf(message, MISC_LENGTH, "HBA too many entries (max %d)", NUMBER_OF_HBAS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=%s: %s", message, hba_path);
#endif

      errx(1, "%s (file <%s>)", message, hba_path);
   }

   memcpy(&config->hba_path[0], hba_path, MIN(strlen(hba_path), MAX_PATH - 1));

   conf_file_mandatory = true;
read_limit_path:
   if (limit_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_limit_configuration(shmem, limit_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->limit_path[0], limit_path, MIN(strlen(limit_path), MAX_PATH - 1));
      }
      else if (conf_file_mandatory && ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         snprintf(message, MISC_LENGTH, "LIMIT configuration file not found");
         printf("pgagroal: %s (file <%s>)\n", message, limit_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, limit_path);
#endif
         exit(1);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many limit entries");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, limit_path);
#endif
         errx(1, "%s (file <%s>)", message, limit_path);
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      limit_path = PGAGROAL_DEFAULT_LIMIT_FILE;
      conf_file_mandatory = false;
      goto read_limit_path;
   }

   conf_file_mandatory = true;
read_users_path:
   if (users_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_users_configuration(shmem, users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         snprintf(message, MISC_LENGTH, "USERS configuration file not found");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s : %s", message, users_path);
#endif
         errx(1, "%s  (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_KO || ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT)
      {
         snprintf(message, MISC_LENGTH, "Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: <%s>", message, users_path);
#endif
         errx(1, "%s (file <%s>)", message, users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "USERS: too many users defined (%d, max %d)", config->number_of_users, NUMBER_OF_USERS);

#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, users_path);
#endif
         errx(1, "%s (file <%s>)", message, users_path);
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      users_path = PGAGROAL_DEFAULT_USERS_FILE;
      conf_file_mandatory = false;
      goto read_users_path;
   }

   conf_file_mandatory = true;
read_frontend_users_path:
   if (frontend_users_path != NULL)
   {
      ret = pgagroal_read_frontend_users_configuration(shmem, frontend_users_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         memset(message, 0, MISC_LENGTH);
         snprintf(message, MISC_LENGTH, "FRONTEND USERS configuration file not found");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, frontend_users_path);
#endif
         errx(1, "%s (file <%s>)", message, frontend_users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Invalid master key file: <%s>", frontend_users_path);
#endif
         errx(1, "%s (file <%s>)", message, frontend_users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         memset(message, 0, MISC_LENGTH);
         snprintf(message, MISC_LENGTH, "FRONTEND USERS: Too many users defined %d (max %d)",
                  config->number_of_frontend_users, NUMBER_OF_USERS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, frontend_users_path);
#endif
         errx(1, "%s (file <%s>)", message, frontend_users_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->frontend_users_path[0], frontend_users_path, MIN(strlen(frontend_users_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      frontend_users_path = PGAGROAL_DEFAULT_FRONTEND_USERS_FILE;
      conf_file_mandatory = false;
      goto read_frontend_users_path;
   }

   conf_file_mandatory = true;
read_admins_path:
   if (admins_path != NULL)
   {
      memset(message, 0, MISC_LENGTH);
      ret = pgagroal_read_admins_configuration(shmem, admins_path);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         snprintf(message, MISC_LENGTH, "ADMINS configuration file not found");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, admins_path);
#endif
         errx(1, "%s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Invalid master key file: <%s>", admins_path);
#endif
         errx(1, "Invalid master key file (file <%s>)", admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s %s", message, admins_path);
#endif
         errx(1, "%s (file <%s>)", message, admins_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      admins_path = PGAGROAL_DEFAULT_ADMINS_FILE;
      conf_file_mandatory = false;
      goto read_admins_path;
   }

   conf_file_mandatory = true;
read_superuser_path:
   if (superuser_path != NULL)
   {
      ret = pgagroal_read_superuser_configuration(shmem, superuser_path);
      memset(message, 0, MISC_LENGTH);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND && conf_file_mandatory)
      {
         snprintf(message, MISC_LENGTH, "SUPERUSER configuration file not found");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, superuser_path);
#endif
         errx(1, "%s (file <%s>)", message, superuser_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_CANNOT_DECRYPT || ret == PGAGROAL_CONFIGURATION_STATUS_KO)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Invalid master key file: <%s>", superuser_path);
#endif
         errx(1, "Invalid master key file (file <%s>)", superuser_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         snprintf(message, MISC_LENGTH, "SUPERUSER: Too many superusers defined (max 1)");
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=%s: %s", message, superuser_path);
#endif
         errx(1, "%s (file <%s>)", message, superuser_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         memcpy(&config->superuser_path[0], superuser_path, MIN(strlen(superuser_path), MAX_PATH - 1));
      }
   }
   else
   {
      // the user did not specify a file on the command line
      // so try the default one and allow it to be missing
      superuser_path = PGAGROAL_DEFAULT_SUPERUSER_FILE;
      conf_file_mandatory = false;
      goto read_superuser_path;
   }

   /* systemd sockets */
#ifdef HAVE_SYSTEMD
   sds = sd_listen_fds(0);
   if (sds > 0)
   {
      int m = 0;

      main_fds_length = 0;

      for (int i = 0; i < sds; i++)
      {
         int fd = SD_LISTEN_FDS_START + i;

         if (sd_is_socket(fd, AF_INET, 0, -1) || sd_is_socket(fd, AF_INET6, 0, -1))
         {
            main_fds_length++;
         }
      }

      if (main_fds_length > 0)
      {
         main_fds = malloc(main_fds_length * sizeof(int));
      }

      for (int i = 0; i < sds; i++)
      {
         int fd = SD_LISTEN_FDS_START + i;

         if (sd_is_socket(fd, AF_UNIX, 0, -1))
         {
            unix_pgsql_socket = fd;
            has_unix_socket = true;
         }
         else if (sd_is_socket(fd, AF_INET, 0, -1) || sd_is_socket(fd, AF_INET6, 0, -1))
         {
            *(main_fds + (m * sizeof(int))) = fd;
            has_main_sockets = true;
            m++;
         }
      }
   }
#endif

   if (pgagroal_init_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to init logging");
#endif
      exit(1);
   }

   if (pgagroal_start_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      errx(1, "Failed to start logging");
   }

   if (config->common.metrics > 0)
   {
      if (pgagroal_init_prometheus(&prometheus_shmem_size, &prometheus_shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus shared memory");
#endif
         errx(1, "Error in creating and initializing prometheus shared memory");
      }

      if (pgagroal_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
         errx(1, "Error in creating and initializing prometheus cache shared memory");
      }
   }

   if (pgagroal_validate_configuration(shmem, has_unix_socket, has_main_sockets))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      errx(1, "Invalid configuration");
   }

   frontend_user_password_startup(config);

   if (pgagroal_validate_hba_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid HBA configuration");
#endif
      errx(1, "Invalid HBA configuration");
   }
   if (pgagroal_validate_limit_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid LIMIT configuration");
#endif
      errx(1, "Invalid LIMIT configuration");
   }
   if (pgagroal_validate_users_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      errx(1, "Invalid USERS configuration");
   }
   if (pgagroal_validate_frontend_users_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid FRONTEND USERS configuration");
#endif
      errx(1, "Invalid FRONTEND USERS configuration");
   }
   if (pgagroal_validate_admins_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      errx(1, "Invalid ADMINS configuration");
   }

   if (pgagroal_resize_shared_memory(shmem_size, shmem, &tmp_size, &tmp_shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      errx(1, "Error in creating shared memory");
   }
   if (pgagroal_destroy_shared_memory(shmem, shmem_size) == -1)
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in destroying shared memory");
#endif
      errx(1, "Error in destroying shared memory");
   }
   shmem_size = tmp_size;
   shmem = tmp_shmem;
   config = (struct main_configuration*)shmem;

   pgagroal_memory_init();

   if (getrlimit(RLIMIT_NOFILE, &flimit) == -1)
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Unable to find limit due to %s", strerror(errno));
#endif
      err(1, "Unable to find limit");
   }

   /* We are "reserving" 30 file descriptors for pgagroal main */
   if (config->max_connections > (flimit.rlim_cur - 30))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0,
                 "STATUS=max_connections is larger than the file descriptor limit (%ld available)",
                 (long)(flimit.rlim_cur - 30));
#endif
      errx(1, "max_connections is larger than the file descriptor limit (%ld available)", (long)(flimit.rlim_cur - 30));
   }

   if (daemon)
   {
      if (config->common.log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         errx(1, "Daemon mode can't be used with console logging");
      }

      pid = fork();

      if (pid < 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
         errx(1, "Daemon mode failed");
      }

      if (pid > 0)
      {
         exit(0);
      }

      /* We are a daemon now */
      umask(0);
      sid = setsid();

      if (sid < 0)
      {
         exit(1);
      }
   }

   create_pidfile_or_exit();

   pgagroal_pool_init();
   pgagroal_initialize_random();

   pgagroal_set_proc_title(argc, argv, "main", NULL);

   free(os);

   /* Bind Unix Domain Socket: Main */
   if (pgagroal_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      goto error;
   }

   /* Bind Unix Domain Socket: Transfer */
   if (pgagroal_bind_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &unix_transfer_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, TRANSFER_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, TRANSFER_UDS);
#endif
      goto error;
   }

   if (!has_unix_socket)
   {
      char pgsql[MISC_LENGTH];

      memset(&pgsql, 0, sizeof(pgsql));
      snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

      if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
#endif
         goto error;
      }
   }

   /* Bind main socket */
   if (!has_main_sockets)
   {
      if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->common.port);
#endif
         goto error;
      }
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Too many descriptors %d", main_fds_length);
#endif
      goto error;
   }
   pgagroal_event_set_context(PGAGROAL_CONTEXT_MAIN);
   main_loop = pgagroal_event_loop_init();
   if (!main_loop)
   {
      goto error;
   }

   pgagroal_signal_init(&signal_watcher[0].sig_w, shutdown_cb, SIGTERM);
   pgagroal_signal_init(&signal_watcher[1].sig_w, reload_cb, SIGHUP);
   pgagroal_signal_init(&signal_watcher[2].sig_w, shutdown_cb, SIGINT);
   pgagroal_signal_init(&signal_watcher[3].sig_w, graceful_cb, SIGTRAP);
   pgagroal_signal_init(&signal_watcher[4].sig_w, coredump_cb, SIGABRT);
   pgagroal_signal_init(&signal_watcher[5].sig_w, shutdown_cb, SIGALRM);
   pgagroal_signal_init(&signal_watcher[6].sig_w, sigchld_cb, SIGCHLD);
   pgagroal_signal_init(&signal_watcher[7].sig_w, service_reload_cb, SIGUSR1);

   signal_watcher[7].slot = -1;
   pgagroal_signal_start(&signal_watcher[7].sig_w);

   for (int i = 0; i < SIGNALS_NUMBER; i++)
   {
      signal_watcher[i].slot = -1;
      pgagroal_signal_start(&signal_watcher[i].sig_w);
   }

   if (config->pipeline == PIPELINE_PERFORMANCE)
   {
      main_pipeline = performance_pipeline();
   }
   else if (config->pipeline == PIPELINE_SESSION)
   {
      if (pgagroal_tls_valid())
      {
         pgagroal_log_fatal("pgagroal: Invalid TLS configuration");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
         goto error;
      }

      main_pipeline = session_pipeline();
   }
   else if (config->pipeline == PIPELINE_TRANSACTION)
   {
      if (pgagroal_tls_valid())
      {
         pgagroal_log_fatal("pgagroal: Invalid TLS configuration");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
         goto error;
      }

      main_pipeline = transaction_pipeline();
   }
   else
   {
      pgagroal_log_fatal("pgagroal: Unknown pipeline identifier (%d)", config->pipeline);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Unknown pipeline identifier (%d)", config->pipeline);
#endif
      goto error;
   }

   if (main_pipeline.initialize(shmem, &pipeline_shmem, &pipeline_shmem_size))
   {
      pgagroal_log_fatal("pgagroal: Pipeline initialize error (%d)", config->pipeline);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Pipeline initialize error (%d)", config->pipeline);
#endif
      goto error;
   }

   start_transfer();
   start_mgt();
   start_uds();
   start_io();

   if (pgagroal_time_is_valid(config->idle_timeout))
   {
      pgagroal_periodic_init(&idle_timeout, idle_timeout_cb,
                             1000 * MAX(1. * pgagroal_time_convert(config->idle_timeout, FORMAT_TIME_S) / 2., 5.));
      pgagroal_periodic_start(&idle_timeout);
   }

   if (pgagroal_time_is_valid(config->max_connection_age))
   {
      pgagroal_periodic_init(&max_connection_age, max_connection_age_cb,
                             1000 * MAX(1. * pgagroal_time_convert(config->max_connection_age, FORMAT_TIME_S) / 2., 5.));
      pgagroal_periodic_start(&max_connection_age);
   }

   if (config->validation == VALIDATION_BACKGROUND)
   {
      pgagroal_periodic_init(&validation, validation_cb,
                             1000 * MAX(1. * pgagroal_time_convert(config->background_interval, FORMAT_TIME_S), 5.));
      pgagroal_periodic_start(&validation);
   }

   if (config->disconnect_client > 0)
   {
      pgagroal_periodic_init(&disconnect_client, disconnect_client_cb,
                             1000 * MIN(300., MAX(1. * config->disconnect_client / 2., 1.)));
      pgagroal_periodic_start(&disconnect_client);
   }

   if (pgagroal_time_is_valid(config->rotate_frontend_password_timeout))
   {
      pgagroal_periodic_init(&rotate_frontend_password, rotate_frontend_password_cb,
                             config->rotate_frontend_password_timeout.ms);
      pgagroal_periodic_start(&rotate_frontend_password);
   }

   if (config->common.metrics > 0)
   {
      /* Bind metrics socket */
      if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->common.metrics);
#endif
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         goto error;
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->common.host, config->management);
#endif
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         goto error;
      }

      start_management();
   }

   pgagroal_log_info("pgagroal: %s started on %s:%d",
                     PGAGROAL_VERSION,
                     config->common.host,
                     config->common.port);
   for (int i = 0; i < main_fds_length; i++)
   {
      pgagroal_log_debug("Socket: %d", *(main_fds + i));
   }
   pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
   pgagroal_log_debug("Management: %d", unix_management_socket);
   pgagroal_log_debug("Transfer: %d", unix_transfer_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgagroal_log_debug("Remote management: %d", *(management_fds + i));
   }

   pgagroal_log_debug("Pipeline: %d", config->pipeline);
   pgagroal_log_debug("Pipeline size: %lu", pipeline_shmem_size);
   pgagroal_log_debug("%s", OpenSSL_version(OPENSSL_VERSION));
   pgagroal_log_debug("Configuration size: %lu", shmem_size);
   pgagroal_log_debug("Max connections: %d", config->max_connections);
   pgagroal_log_debug("Known users: %d", config->number_of_users);
   pgagroal_log_debug("Known frontend users: %d", config->number_of_frontend_users);
   pgagroal_log_debug("Known admins: %d", config->number_of_admins);
   pgagroal_log_debug("Known superuser: %s", strlen(config->superuser.username) > 0 ? "Yes" : "No");

   /* Retrieve and log the OS kernel version; this has no effect on Pgagroal's operation */
   pgagroal_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch);

   if (!config->allow_unknown_users && config->number_of_users == 0)
   {
      pgagroal_log_warn("No users allowed");
   }

   if (pgagroal_can_prefill())
   {
      if (!fork())
      {
         pgagroal_event_loop_fork();
         shutdown_ports();
         pgagroal_prefill_if_can(false, true);
      }
   }

#ifdef HAVE_SYSTEMD
   sd_notifyf(0,
              "READY=1\n"
              "STATUS=Running\n"
              "MAINPID=%lu",
              (unsigned long)getpid());
#endif

   pgagroal_event_loop_run();

   pgagroal_log_info("pgagroal: shutdown");
#ifdef HAVE_SYSTEMD
   sd_notify(0, "STOPPING=1");
#endif
   pgagroal_pool_shutdown();

   if (clients != NULL)
   {
      struct client* c = clients;
      while (c != NULL)
      {
         if (kill(c->pid, SIGQUIT))
         {
            pgagroal_log_debug("kill: %s", strerror(errno));
         }
         c = c->next;
      }
   }

   shutdown_management();
   shutdown_metrics();
   shutdown_mgt();
   shutdown_transfer();
   shutdown_io();
   shutdown_uds();

   pgagroal_event_loop_destroy();

   free(os);
   free(main_fds);
   free(metrics_fds);
   free(management_fds);

   main_pipeline.destroy(pipeline_shmem, pipeline_shmem_size);

   remove_pidfile();

   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(prometheus_shmem, prometheus_shmem_size);
   pgagroal_destroy_shared_memory(prometheus_cache_shmem, prometheus_cache_shmem_size);
   pgagroal_destroy_shared_memory(shmem, shmem_size);

   pgagroal_memory_destroy();

   return 0;

error:

   free(os);
   remove_pidfile();
   exit(1);
}

static void
accept_main_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   pid_t pid;
   struct accept_io* ai;
   struct main_configuration* config;

   ai = (struct accept_io*)watcher;
   config = (struct main_configuration*)shmem;

   errno = 0;

   memset(&address, 0, sizeof(address));

   client_fd = watcher->fds.main.client_fd;
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         char pgsql[MISC_LENGTH];

         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_io();
         shutdown_uds();

         memset(&pgsql, 0, sizeof(pgsql));
         snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
            exit(1);
         }

         free(main_fds);
         main_fds = NULL;
         main_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
            exit(1);
         }

         if (main_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
            exit(1);
         }

         if (!fork())
         {
            shutdown_ports();
            pgagroal_flush(FLUSH_GRACEFULLY, "*");
            exit(0);
         }

         start_io();
         start_uds();

         for (int i = 0; i < main_fds_length; i++)
         {
            pgagroal_log_debug("Socket: %d", *(main_fds + i));
         }
         pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   pgagroal_prometheus_client_sockets_add();

   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   pgagroal_log_trace("accept_main_cb: client address: %s", address);

   pid = fork();
   if (pid == -1)
   {
      /* No process */
      pgagroal_log_error("Cannot create process");
   }
   else if (pid > 0)
   {
      add_client(pid);
   }
   else
   {
      char* addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("Cannot allocate memory for client address");
         return;
      }
      memcpy(addr, address, strlen(address));

      /* Prevent SIGINT sent to the parent's controlling terminal
       * from irradiating to the children. pgagroal wants the
       * children processes to be independent. */
      if (setpgid(0, 0) == -1)
      {
         pgagroal_log_error("setpgid error: %s", __func__, strerror(errno));
         exit(1);
      }

      pgagroal_event_loop_fork();
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_worker(client_fd, addr, ai->argv);
   }
   pgagroal_disconnect(client_fd);
}

static void
accept_mgt_cb(struct io_watcher* watcher)
{
   int client_fd;
   int32_t id;
   pid_t pid;
   char* str = NULL;
   time_t start_time;
   time_t end_time;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   struct accept_io* ai;
   struct json* payload = NULL;
   struct json* header = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   ai = (struct accept_io*)watcher;

   errno = 0;

   client_fd = watcher->fds.main.client_fd;

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("Restarting management due to: %s (%d)", strerror(errno), client_fd);

         shutdown_mgt();

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s", config->unix_socket_dir);
            exit(1);
         }

         start_mgt();

         pgagroal_log_debug("Management: %d", unix_management_socket);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   /* Process management request */
   if (pgagroal_management_read_json(NULL, client_fd, &compression, &encryption, &payload))
   {
      pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, compression, encryption, NULL);
      pgagroal_log_error("Management: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
      goto error;
   }

   header = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   id = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);

   str = pgagroal_json_to_string(payload, FORMAT_JSON, NULL, 0);
   pgagroal_log_debug("Management %d: %s", id, str);

   if (id == MANAGEMENT_FLUSH)
   {
      pgagroal_log_debug("pgagroal: Management flush");

      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_FLUSH_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Flush: No fork (%d)", MANAGEMENT_ERROR_FLUSH_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "flush", NULL);
         pgagroal_request_flush(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_ENABLEDB)
   {
      struct json* req = NULL;
      struct json* res = NULL;
      struct json* databases = NULL;
      char* database = NULL;

      pgagroal_log_debug("pgagroal: Management enabledb: ");
      pgagroal_pool_status();

      start_time = time(NULL);

      req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      database = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_DATABASE);

      pgagroal_management_create_response(payload, -1, &res);
      pgagroal_json_create(&databases);

      if (!strcmp("*", database))
      {
         struct json* js = NULL;

         config->all_disabled = false;
         memset(&config->disabled, 0, sizeof(config->disabled));

         pgagroal_json_create(&js);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)true, ValueBool);

         pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);
      }
      else
      {
         bool found = false;
         for (int i = 0; !found && i < NUMBER_OF_DISABLED; i++)
         {
            struct json* js = NULL;

            if (!strcmp(config->disabled[i], database))
            {
               memset(&config->disabled[i], 0, MAX_DATABASE_LENGTH);

               pgagroal_json_create(&js);

               pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);
               pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)true, ValueBool);

               pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);

               found = true;
            }
         }
      }

      pgagroal_json_put(res, MANAGEMENT_ARGUMENT_DATABASES, (uintptr_t)databases, ValueJSON);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_DISABLEDB)
   {
      struct json* req = NULL;
      struct json* res = NULL;
      struct json* databases = NULL;
      char* database = NULL;

      pgagroal_log_debug("pgagroal: Management disabledb: ");
      pgagroal_pool_status();

      start_time = time(NULL);

      req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      database = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_DATABASE);

      pgagroal_management_create_response(payload, -1, &res);
      pgagroal_json_create(&databases);

      config->all_disabled = false;

      if (!strcmp("*", database))
      {
         struct json* js = NULL;

         config->all_disabled = true;
         memset(&config->disabled, 0, sizeof(config->disabled));

         pgagroal_json_create(&js);

         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);
         pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)false, ValueBool);

         pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);
      }
      else
      {
         bool inserted = false;
         for (int i = 0; !inserted && i < NUMBER_OF_DISABLED; i++)
         {
            struct json* js = NULL;

            if (strlen(config->disabled[i]) == 0)
            {
               memcpy(&config->disabled[i], database, strlen(database) > MAX_DATABASE_LENGTH ? MAX_DATABASE_LENGTH : strlen(database));

               pgagroal_json_create(&js);

               pgagroal_json_put(js, MANAGEMENT_ARGUMENT_DATABASE, (uintptr_t)database, ValueString);
               pgagroal_json_put(js, MANAGEMENT_ARGUMENT_ENABLED, (uintptr_t)false, ValueBool);

               pgagroal_json_append(databases, (uintptr_t)js, ValueJSON);

               inserted = true;
            }
         }
      }

      pgagroal_json_put(res, MANAGEMENT_ARGUMENT_DATABASES, (uintptr_t)databases, ValueJSON);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_GRACEFULLY)
   {
      pgagroal_log_debug("pgagroal: Management gracefully");
      pgagroal_pool_status();

      start_time = time(NULL);

      config->gracefully = true;

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_SHUTDOWN)
   {
      pgagroal_log_debug("pgagroal: Management shutdown");
      pgagroal_pool_status();

      start_time = time(NULL);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);

      config->keep_running = false;

      pgagroal_event_loop_break();
   }
   else if (id == MANAGEMENT_CANCEL_SHUTDOWN)
   {
      pgagroal_log_debug("pgagroal: Management cancel shutdown");
      pgagroal_pool_status();

      start_time = time(NULL);

      config->gracefully = false;

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_STATUS)
   {
      pgagroal_log_debug("pgagroal: Management status");
      pgagroal_pool_status();

      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Status: No fork %s (%d)", NULL, MANAGEMENT_ERROR_STATUS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "status", NULL);
         pgagroal_status(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_DETAILS)
   {
      pgagroal_log_debug("pgagroal: Management details");
      pgagroal_pool_status();

      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_STATUS_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Status: No fork %s (%d)", NULL, MANAGEMENT_ERROR_STATUS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "status", NULL);
         pgagroal_status_details(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_PING)
   {
      struct json* response = NULL;

      pgagroal_log_debug("pgagroal: Management ping");

      start_time = time(NULL);

      pgagroal_management_create_response(payload, -1, &response);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CLEAR)
   {
      pgagroal_log_debug("pgagroal: Management clear");

      start_time = time(NULL);

      pgagroal_prometheus_clear();

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CLEAR_SERVER)
   {
      pgagroal_log_debug("pgagroal: Management clear server");
      char* server = NULL;
      struct json* req = NULL;

      start_time = time(NULL);

      req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      server = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_SERVER);

      pgagroal_server_clear(server);
      pgagroal_prometheus_failed_servers();

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_SWITCH_TO)
   {
      pgagroal_log_debug("pgagroal: Management switch to");
      int old_primary = -1;
      signed char server_state;
      char* server = NULL;
      struct json* req = NULL;

      start_time = time(NULL);

      req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      server = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_SERVER);

      pgagroal_log_debug("pgagroal: Attempting to switch to server: %s", server);
      for (int i = 0; old_primary == -1 && i < config->number_of_servers; i++)
      {
         server_state = atomic_load(&config->servers[i].state);
         if (server_state == SERVER_PRIMARY)
         {
            old_primary = i;
         }
      }

      if (!pgagroal_server_switch(server))
      {
         pgagroal_log_info("pgagroal: Successfully switched to server: %s", server);
         if (!fork())
         {
            shutdown_ports();
            if (old_primary != -1)
            {
               pgagroal_flush_server(old_primary);
            }
            else
            {
               pgagroal_flush(FLUSH_GRACEFULLY, "*");
               exit(0);
            }
         }
         pgagroal_prometheus_failed_servers();
         end_time = time(NULL);
         pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
      }
      else
      {
         pgagroal_log_warn("pgagroal: Failed to switch to server: %s (server not found or invalid)", server);
         end_time = time(NULL);
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_SWITCH_TO_FAILED, compression, encryption, payload);
      }
   }
   else if (id == MANAGEMENT_RELOAD)
   {
      bool restart = false;
      struct json* res = NULL;

      pgagroal_log_debug("pgagroal: Management reload");

      start_time = time(NULL);

      restart = reload_configuration();

      pgagroal_management_create_response(payload, -1, &res);

      pgagroal_json_put(res, MANAGEMENT_ARGUMENT_RESTART, (uintptr_t)restart, ValueBool);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONFIG_LS)
   {
      struct json* response = NULL;

      start_time = time(NULL);

      pgagroal_management_create_response(payload, -1, &response);

      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)config->common.configuration_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_HBA_CONF_PATH, (uintptr_t)config->hba_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_LIMIT_CONF_PATH, (uintptr_t)config->limit_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)config->users_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_FRONTEND_USERS_CONF_PATH, (uintptr_t)config->frontend_users_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)config->admins_path, ValueString);
      pgagroal_json_put(response, CONFIGURATION_ARGUMENT_SUPERUSER_CONF_PATH, (uintptr_t)config->superuser_path, ValueString);

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONFIG_GET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_GET_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Conf Get: No fork %s (%d)", NULL, MANAGEMENT_ERROR_CONF_GET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "conf get", NULL);
         pgagroal_conf_get(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_CONFIG_SET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_SET_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Conf Set: No fork %s (%d)", NULL, MANAGEMENT_ERROR_CONF_SET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "conf set", NULL);
         reload_set_configuration(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_CONFIG_ALIAS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_CONF_ALIAS_NOFORK, compression, encryption, payload);
         pgagroal_log_error("Config Alias: No fork %s (%d)", NULL, MANAGEMENT_ERROR_CONF_ALIAS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgagroal_json_clone(payload, &pyl);

         pgagroal_set_proc_title(1, ai->argv, "conf alias", NULL);
         pgagroal_management_config_alias(NULL, client_fd, compression, encryption, pyl);
         // Add cleanup - this is needed because we're in a child process
         pgagroal_json_destroy(pyl);
         exit(0);
      }
   }
   else if (id == MANAGEMENT_GET_PASSWORD)
   {
      int index = -1;
      char* username = NULL;
      struct json* req = NULL;
      struct json* res = NULL;

      start_time = time(NULL);

      req = (struct json*)pgagroal_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
      username = (char*)pgagroal_json_get(req, MANAGEMENT_ARGUMENT_USERNAME);

      for (int i = 0; index == -1 && i < config->number_of_frontend_users; i++)
      {
         if (!strcmp(&config->frontend_users[i].username[0], username))
         {
            index = i;
         }
      }

      pgagroal_management_create_response(payload, -1, &res);

      pgagroal_json_put(res, MANAGEMENT_ARGUMENT_USERNAME, (uintptr_t)username, ValueString);

      if (index != -1)
      {
         pgagroal_json_put(res, MANAGEMENT_ARGUMENT_PASSWORD, (uintptr_t)config->frontend_users[index].password, ValueString);
      }
      else
      {
         pgagroal_json_put(res, MANAGEMENT_ARGUMENT_PASSWORD, (uintptr_t)NULL, ValueString);
      }

      end_time = time(NULL);

      pgagroal_management_response_ok(NULL, client_fd, start_time, end_time, compression, encryption, payload);
   }
   else
   {
      pgagroal_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_UNKNOWN_COMMAND, compression, encryption, payload);
      pgagroal_log_error("Unknown: %s (%d)", pgagroal_json_to_string(payload, FORMAT_JSON, NULL, 0), MANAGEMENT_ERROR_UNKNOWN_COMMAND);
      goto error;
   }

   if (config->gracefully)
   {
      if (config->keep_running)
      {
         if (atomic_load(&config->active_connections) == 0)
         {
            pgagroal_log_debug("pgagroal: graceful shutdown triggered  - connections=0, gracefully=true, keep_running=true");
            pgagroal_pool_status();
            pgagroal_event_loop_break();
         }
      }
      else
      {
         pgagroal_log_info("pgagroal: graceful shutdown check skipped - server shutdown already in progress (keep_running=false)");
      }
   }

   free(str);

   pgagroal_json_destroy(payload);

   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_self_sockets_sub();

   return;

error:

   free(str);

   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_self_sockets_sub();
}

static void
accept_transfer_cb(struct io_watcher* watcher)
{
   int client_fd;
   int id = -1;
   pid_t pid = 0;
   int32_t slot = -1;
   int fd = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   errno = 0;

   client_fd = watcher->fds.main.client_fd;

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("Restarting transfer due to: %s (%d)", strerror(errno), client_fd);

         shutdown_mgt();

         if (pgagroal_bind_unix_socket(config->unix_socket_dir, TRANSFER_UDS, &unix_transfer_socket))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, TRANSFER_UDS);
            exit(1);
         }

         start_mgt();

         pgagroal_log_debug("Transfer: %d", unix_transfer_socket);
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   /* Process transfer request */
   if (pgagroal_connection_id_read(client_fd, &id))
   {
      goto error;
   }

   if (id == CONNECTION_TRANSFER)
   {
      pgagroal_log_trace("pgagroal: Transfer connection");

      if (pgagroal_connection_transfer_read(client_fd, &slot, &fd))
      {
         pgagroal_log_error("pgagroal: Transfer connection: Slot %d FD %d", slot, fd);
         goto error;
      }

      config->connections[slot].fd = fd;
      known_fds[slot] = config->connections[slot].fd;

      if (config->pipeline == PIPELINE_TRANSACTION)
      {
         struct client* c = clients;
         while (c != NULL)
         {
            int c_fd = -1;

            if (pgagroal_connection_get_pid(c->pid, &c_fd))
            {
               goto error;
            }

            if (pgagroal_connection_id_write(c_fd, CONNECTION_CLIENT_FD))
            {
               goto error;
            }

            if (pgagroal_connection_transfer_write(c_fd, slot))
            {
               goto error;
            }

            pgagroal_disconnect(c_fd);

            c = c->next;
         }
      }

      pgagroal_log_debug("pgagroal: Transfer connection: Slot %d FD %d", slot, fd);
   }
   else if (id == CONNECTION_RETURN)
   {
      pgagroal_log_trace("pgagroal: Transfer return connection");

      if (pgagroal_connection_slot_read(client_fd, &slot))
      {
         pgagroal_log_error("pgagroal: Transfer return connection: Slot %d", slot);
         goto error;
      }

      pgagroal_log_debug("pgagroal: Transfer return connection: Slot %d", slot);
   }
   else if (id == CONNECTION_KILL)
   {
      pgagroal_log_trace("pgagroal: Transfer kill connection");

      if (pgagroal_connection_transfer_read(client_fd, &slot, &fd))
      {
         pgagroal_log_error("pgagroal: Transfer kill connection: Slot %d FD %d", slot, fd);
         goto error;
      }

      if (known_fds[slot] == fd)
      {
         struct client* c = clients;
         while (c != NULL)
         {
            int c_fd = -1;

            if (pgagroal_connection_get_pid(c->pid, &c_fd))
            {
               goto error;
            }

            if (pgagroal_connection_id_write(c_fd, CONNECTION_REMOVE_FD))
            {
               goto error;
            }

            if (pgagroal_connection_transfer_write(c_fd, slot))
            {
               goto error;
            }

            pgagroal_disconnect(c_fd);

            c = c->next;
         }

         pgagroal_disconnect(fd);
         known_fds[slot] = 0;
      }

      pgagroal_log_debug("pgagroal: Transfer kill connection: Slot %d FD %d", slot, fd);
   }
   else if (id == CONNECTION_CLIENT_DONE)
   {
      pgagroal_log_debug("pgagroal: Transfer client done");

      if (pgagroal_connection_pid_read(client_fd, &pid))
      {
         pgagroal_log_error("pgagroal: Transfer client done: PID %d", (int)pid);
         goto error;
      }

      remove_client(pid);

      pgagroal_log_debug("pgagroal: Transfer client done: PID %d", (int)pid);
   }

   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_self_sockets_sub();

   return;

error:

   pgagroal_disconnect(client_fd);

   pgagroal_prometheus_self_sockets_sub();
}

static void
accept_metrics_cb(struct io_watcher* watcher)
{
   int client_fd;
   struct main_configuration* config;
   SSL_CTX* ctx = NULL;
   SSL* client_ssl = NULL;

   config = (struct main_configuration*)shmem;

   errno = 0;

   client_fd = watcher->fds.main.client_fd;

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      if (strlen(config->common.metrics_cert_file) > 0 && strlen(config->common.metrics_key_file) > 0)
      {
         if (pgagroal_create_ssl_ctx(false, &ctx))
         {
            pgagroal_log_error("Could not create metrics SSL context");
            pgagroal_disconnect(client_fd);
            exit(1);
         }

         if (pgagroal_create_ssl_server(ctx, config->common.metrics_key_file, config->common.metrics_cert_file, config->common.metrics_ca_file, client_fd, &client_ssl))
         {
            pgagroal_log_error("Could not create metrics SSL server");
            pgagroal_disconnect(client_fd);
            exit(1);
         }
      }
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_prometheus(client_ssl, client_fd);
   }

   pgagroal_close_ssl(client_ssl);
   pgagroal_disconnect(client_fd);
   pgagroal_prometheus_self_sockets_sub();
}

static void
accept_management_cb(struct io_watcher* watcher)
{
   struct sockaddr_in6 client_addr;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct main_configuration* config;

   memset(&address, 0, sizeof(address));

   config = (struct main_configuration*)shmem;

   client_fd = watcher->fds.main.client_fd;

   pgagroal_prometheus_self_sockets_add();

   if (client_fd == -1)
   {
      if (accept_fatal(errno) && config->keep_running)
      {
         pgagroal_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), client_fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->nodelay, config->backlog))
         {
            pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgagroal_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
      else
      {
         pgagroal_log_debug("accept: %s (%d)", strerror(errno), client_fd);
      }
      errno = 0;
      return;
   }

   pgagroal_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = calloc(1, strlen(address) + 1);
      if (addr == NULL)
      {
         pgagroal_log_fatal("Couldn't allocate address");
         return;
      }
      memcpy(addr, address, strlen(address));

      pgagroal_event_loop_fork();
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgagroal_remote_management(client_fd, addr);
   }

   pgagroal_disconnect(client_fd);
   pgagroal_prometheus_self_sockets_sub();
}

static void
shutdown_cb(void)
{
   struct main_configuration* config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal: shutdown requested");
   config->keep_running = false;
   pgagroal_pool_status();
   pgagroal_event_loop_break();
}

static void
reload_cb(void)
{
   pgagroal_log_debug("pgagroal: reload requested");
   reload_configuration();
}

static void
graceful_cb(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgagroal_log_debug("pgagroal: gracefully requested");

   pgagroal_pool_status();
   config->gracefully = true;

   if (atomic_load(&config->active_connections) == 0)
   {
      pgagroal_pool_status();

      pgagroal_event_loop_break();
   }
}

static void
coredump_cb(void)
{
   pgagroal_log_info("pgagroal: core dump requested");
   pgagroal_pool_status();
   remove_pidfile();
   abort();
}

static void
sigchld_cb(void)
{
   while (waitpid(-1, NULL, WNOHANG) > 0)
   {
      ;
   }
}

static void
idle_timeout_cb(void)
{
   /* pgagroal_idle_timeout() is always in a fork() */
   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      pgagroal_idle_timeout();
   }
}

static void
max_connection_age_cb(void)
{
   /* max_connection_age() is always in a fork() */
   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      pgagroal_max_connection_age();
   }
}

static void
validation_cb(void)
{
   /* pgagroal_validation() is always in a fork() */
   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      pgagroal_validation();
   }
}

static void
disconnect_client_cb(void)
{
   /* main_pipeline.periodic is always in a fork() */
   if (!fork())
   {
      pgagroal_event_loop_fork();
      shutdown_ports();
      main_pipeline.periodic();
   }
}

static void
rotate_frontend_password_cb(void)
{
   char* pwd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->number_of_frontend_users; i++)
   {
      if (pgagroal_generate_password(config->rotate_frontend_password_length, &pwd))
      {
         pgagroal_log_debug("rotate_frontend_password_cb: unable to rotate password");
         return;
      }
      memcpy(&config->frontend_users[i].password, pwd, strlen(pwd) + 1);
      pgagroal_log_trace("rotate_frontend_password_cb: current pass for username=%s:%s", config->frontend_users[i].username, config->frontend_users[i].password);
      free(pwd);
   }
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

static void
frontend_user_password_startup(struct main_configuration* config)
{
   char* pwd = NULL;

   if (config->number_of_frontend_users == 0 && pgagroal_time_is_valid(config->rotate_frontend_password_timeout))
   {
      for (int i = 0; i < config->number_of_users; i++)
      {
         memcpy(&config->frontend_users[i].username, config->users[i].username, strlen(config->users[i].username));
         if (pgagroal_generate_password(config->rotate_frontend_password_length, &pwd))
         {
            pgagroal_log_debug("frontend_user_password_startup: unable to generate random password at startup");
            return;
         }
         memcpy(&config->frontend_users[i].password, pwd, strlen(pwd) + 1);
         pgagroal_log_trace("frontend_user_password_startup: frontend user with username=%s initiated", config->frontend_users[i].username);
         free(pwd);
      }
      config->number_of_frontend_users = config->number_of_users;
   }
}

static void
add_client(pid_t pid)
{
   struct client* c = NULL;

   c = (struct client*)malloc(sizeof(struct client));
   c->pid = pid;
   c->next = NULL;

   if (clients == NULL)
   {
      clients = c;
   }
   else
   {
      struct client* last = NULL;

      last = clients;

      while (last->next != NULL)
      {
         last = last->next;
      }

      last->next = c;
   }
}

static void
remove_client(pid_t pid)
{
   struct client* c = NULL;
   struct client* p = NULL;

   c = clients;
   p = NULL;

   if (c != NULL)
   {
      while (c->pid != pid)
      {
         p = c;
         c = c->next;

         if (c == NULL)
         {
            return;
         }
      }

      if (c == clients)
      {
         clients = c->next;
      }
      else
      {
         p->next = c->next;
      }

      free(c);
   }
}

static bool
reload_configuration(void)
{
   bool restart = false;
   char pgsql[MISC_LENGTH];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   shutdown_io();
   shutdown_uds();
   shutdown_metrics();
   shutdown_management();

   pgagroal_reload_configuration(&restart);

   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

   if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
      goto error;
   }

   free(main_fds);
   main_fds = NULL;
   main_fds_length = 0;

   if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->nodelay, config->backlog))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
      goto error;
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
      goto error;
   }

   start_io();
   start_uds();

   if (config->common.metrics > 0)
   {
      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      /* Bind metrics socket */
      if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
         goto error;
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      /* Bind management socket */
      if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->management);
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", management_fds_length);
         goto error;
      }

      start_management();
   }

   for (int i = 0; i < main_fds_length; i++)
   {
      pgagroal_log_debug("Socket: %d", *(main_fds + i));
   }
   pgagroal_log_debug("Unix Domain Socket: %d", unix_pgsql_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgagroal_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgagroal_log_debug("Remote management: %d", *(management_fds + i));
   }

   if (restart)
   {
      remove_pidfile();
      exit(0);
   }

   return true;

error:
   remove_pidfile();
   exit(1);
}

static bool
reload_services_only(void)
{
   char pgsql[MISC_LENGTH];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   // Shutdown services
   shutdown_io();
   shutdown_uds();
   shutdown_metrics();
   shutdown_management();

   // Instead, restart services with current memory configuration
   pgagroal_log_debug("conf set: unix socket Bound to %s/.s.PGSQL.%d", config->unix_socket_dir, config->common.port);

   // Restart Unix Domain Socket with NEW port from memory
   memset(&pgsql, 0, sizeof(pgsql));
   snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->common.port);

   if (pgagroal_bind_unix_socket(config->unix_socket_dir, &pgsql[0], &unix_pgsql_socket))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s/%s", config->unix_socket_dir, &pgsql[0]);
      goto error;
   }

   pgagroal_log_debug("conf set: main port and host Bound to %s:%d", config->common.host, config->common.port);

   // Restart main sockets with NEW port from memory
   free(main_fds);
   main_fds = NULL;
   main_fds_length = 0;

   if (pgagroal_bind(config->common.host, config->common.port, &main_fds, &main_fds_length, config->nodelay, config->backlog))
   {
      pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.port);
      goto error;
   }

   if (main_fds_length > MAX_FDS)
   {
      pgagroal_log_fatal("pgagroal: Too many descriptors %d", main_fds_length);
      goto error;
   }

   start_io();
   start_uds();

   // Restart metrics if enabled
   if (config->common.metrics > 0)
   {
      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      if (pgagroal_bind(config->common.host, config->common.metrics, &metrics_fds, &metrics_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_fatal("pgagroal: Could not bind to %s:%d", config->common.host, config->common.metrics);
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgagroal_log_fatal("pgagroal: Too many descriptors %d", metrics_fds_length);
         goto error;
      }

      start_metrics();
   }

   // Restart management
   if (config->management > 0)
   {
      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      if (pgagroal_bind(config->common.host, config->management, &management_fds, &management_fds_length, config->nodelay, config->backlog))
      {
         pgagroal_log_warn("pgagroal: Could not rebind management port %s:%d, continuing without management", config->common.host, config->management);
         config->management = 0;
      }
      else
      {
         if (management_fds_length <= MAX_FDS)
         {
            start_management();
         }
      }
   }

   pgagroal_log_info("conf set: Services restarted successfully");
   return true;

error:
   return false;
}

static void
reload_set_configuration(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   bool restart_required = false;
   bool config_success = false;

   // Apply configuration changes to shared memory
   pgagroal_conf_set(ssl, client_fd, compression, encryption, payload, &restart_required, &config_success);

   // Only restart services if config change succeeded AND no restart required
   if (config_success && !restart_required)
   {
      pgagroal_log_info("Configuration applied successfully, reloading services");
      kill(getppid(), SIGUSR1); // Signal parent to restart services
   }
   else if (config_success && restart_required)
   {
      pgagroal_log_info("Configuration requires restart - continuing with old configuration");
   }
   else
   {
      pgagroal_log_error("Configuration change failed, not applying changes");
      exit(1);
   }

   exit(0);
}

static void
service_reload_cb(void)
{
   pgagroal_log_debug("pgagroal: service restart requested");
   reload_services_only(); // Parent process restarts services
}

/**
 * Creates the pid file for the running pooler.
 * If a pid file already exists, or if the file cannot be written,
 * the function kills (exits) the current process.
 *
 */
static void
create_pidfile_or_exit(void)
{
   char buffer[64];
   pid_t pid;
   int r;
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0640);
      if (errno == EEXIST)
      {
         errx(1, "PID file <%s> exists, is there another instance running ?", config->pidfile);
      }
      else if (errno == EACCES)
      {
         errx(1, "PID file <%s> cannot be created due to lack of permissions", config->pidfile);
      }
      else if (fd < 0)
      {
         err(1, "Could not create PID file <%s>", config->pidfile);
      }

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         errx(1, "Could not write into PID file <%s>", config->pidfile);
      }

      close(fd);
   }
}

static void
remove_pidfile(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) > 0)
   {
      if (unlink(config->pidfile))
      {
         warn("Cannot remove PID file <%s>", config->pidfile);
      }
   }
}

static void
shutdown_ports(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   shutdown_io();

   if (config->common.metrics > 0)
   {
      shutdown_metrics();
   }

   if (config->management > 0)
   {
      shutdown_management();
   }
}
