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
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <utf8.h>

/* system */
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <err.h>
#include <errno.h>

#include <openssl/ssl.h>

#define HELP                   99
#define DB_ALIAS_STRING_LENGTH 512

#define COMMAND_CANCELSHUTDOWN "cancel-shutdown"
#define COMMAND_CLEAR          "clear"
#define COMMAND_CLEAR_SERVER   "clear-server"
#define COMMAND_DISABLEDB      "disable-db"
#define COMMAND_ENABLEDB       "enable-db"
#define COMMAND_FLUSH          "flush"
#define COMMAND_GRACEFULLY     "shutdown-gracefully"
#define COMMAND_PING           "ping"
#define COMMAND_RELOAD         "reload"
#define COMMAND_SHUTDOWN       "shutdown"
#define COMMAND_STATUS         "status"
#define COMMAND_STATUS_DETAILS "status-details"
#define COMMAND_SWITCH_TO      "switch-to"
#define COMMAND_CONFIG_LS      "conf-ls"
#define COMMAND_CONFIG_GET     "conf-get"
#define COMMAND_CONFIG_SET     "conf-set"
#define COMMAND_CONFIG_ALIAS   "conf-alias"

#define OUTPUT_FORMAT_JSON     "json"
#define OUTPUT_FORMAT_TEXT     "text"

#define UNSPECIFIED            "Unspecified"

static void display_helper(char* command);
static void help_cancel_shutdown(void);
/* static void help_config(void); */
static void help_clear(void);
static void help_conf(void);
static void help_disabledb(void);
static void help_enabledb(void);
static void help_flush(void);
static void help_ping(void);
static void help_shutdown(void);
static void help_status_details(void);
static void help_switch_to(void);

static int cancel_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);
static int details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int disabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);
static int enabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);
static int flush(SSL* ssl, int socket, int32_t mode, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);
static int gracefully(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int pgagroal_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int clear(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int clear_server(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);
static int status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int switch_to(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);

static int process_result(SSL* ssl, int socket, int32_t output_format);
static int process_get_result(SSL* ssl, int socket, char* config_key, int32_t output_format);
static int process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format);

static int get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format);
static int conf_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int process_alias_result(SSL* ssl, int socket, int32_t output_format);

static char* translate_command(int32_t cmd_code);
static char* translate_output_format(int32_t out_code);
static char* translate_compression(int32_t compression_code);
static char* translate_encryption(int32_t encryption_code);
static void translate_json_object(struct json* j);

// clang-format off
const struct pgagroal_command command_table[] = {
   {
      .command = "flush",
      .subcommand = "",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_FLUSH,
      .mode = FLUSH_GRACEFULLY,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<flush gracefully> [%s]",
   },
   {
      .command = "ping",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_PING,
      .deprecated = false,
      .log_message = "<ping>"
   },
   {
      .command = "enable",
      .subcommand = "",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_ENABLEDB,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<enable> [%s]",
   },
   {
      .command = "disable",
      .subcommand = "",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_DISABLEDB,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<disable> [%s]",
   },
   {
      .command = "shutdown",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_GRACEFULLY,
      .deprecated = false,
      .log_message = "<shutdown gracefully>"
   },
   {
      .command = "status",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_STATUS,
      .deprecated = false,
      .log_message = "<status>"
   },
   {
      .command = "switch-to",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_SWITCH_TO,
      .deprecated = false,
      .log_message = "<switch-to> [%s]"
   },
   {
      .command = "clear",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_CLEAR_SERVER,
      .deprecated = false,
      .log_message = "<clear server [%s]>",
   },
   {
      .command = "shutdown",
      .subcommand = "gracefully",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_GRACEFULLY,
      .deprecated = false,
      .log_message = "<shutdown gracefully>"
   },
   {
      .command = "shutdown",
      .subcommand = "immediate",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_SHUTDOWN,
      .deprecated = false,
      .log_message = "<shutdown immediate>"
   },
   {
      .command = "shutdown",
      .subcommand = "cancel",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CANCEL_SHUTDOWN,
      .deprecated = false,
      .log_message = "<shutdown cancel>"
   },
   {
      .command = "conf",
      .subcommand = "reload",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_RELOAD,
      .deprecated = false,
      .log_message = "<conf reload>"
   },
   {
      .command = "conf",
      .subcommand = "ls",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CONFIG_LS,
      .deprecated = false,
      .log_message = "<conf ls>"
   },
   {
      .command = "conf",
      .subcommand = "get",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_CONFIG_GET,
      .deprecated = false,
      .log_message = "<conf get> [%s]"
   },
   {
      .command = "conf",
      .subcommand = "set",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_CONFIG_SET,
      .deprecated = false,
      .log_message = "<conf set> [%s] = [%s]"
   },
   {
      .command = "conf",
      .subcommand = "alias",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CONFIG_ALIAS,
      .deprecated = false,
      .log_message = "<conf alias>"
   },
   {
      .command = "clear",
      .subcommand = "server",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_CLEAR_SERVER,
      .default_argument = "server",
      .deprecated = false,
      .log_message = "<clear server> [%s]",
   },
   {
      .command = "flush",
      .subcommand = "idle",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_FLUSH,
      .mode = FLUSH_IDLE,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<flush idle> [%s]",
   },
   {
      .command = "flush",
      .subcommand = "gracefully",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_FLUSH,
      .mode = FLUSH_GRACEFULLY,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<flush gracefully> [%s]",
   },
   {
      .command = "flush",
      .subcommand = "all",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_FLUSH,
      .mode = FLUSH_ALL,
      .default_argument = "*",
      .deprecated = false,
      .log_message = "<flush all> [%s]",
   },
   {
      .command = "clear",
      .subcommand = "prometheus",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CLEAR,
      .deprecated = false,
      .log_message = "<clear prometheus>"
   },
   {
      .command = "status",
      .subcommand = "details",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_DETAILS,
      .deprecated = false,
      .log_message = "<status details>"
   },
};
// clang-format on

static void
version(void)
{
   printf("pgagroal-cli %s\n", PGAGROAL_VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal-cli %s\n", PGAGROAL_VERSION);
   printf("  Command line utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-cli [ OPTIONS ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE                     Set the path to the pgagroal.conf file\n");
   printf("                                                 Default: %s\n", PGAGROAL_DEFAULT_CONF_FILE);
   printf("  -h, --host HOST                              Set the host name\n");
   printf("  -p, --port PORT                              Set the port number\n");
   printf("  -U, --user USERNAME                          Set the user name\n");
   printf("  -P, --password PASSWORD                      Set the password\n");
   printf("  -L, --logfile FILE                           Set the log file\n");
   printf("  -F, --format text|json|raw                   Set the output format\n");
   printf("  -C, --compress none|gz|zstd|lz4|bz2          Compress the wire protocol\n");
   printf("  -E, --encrypt none|aes|aes256|aes192|aes128  Encrypt the wire protocol\n");
   printf("  -v, --verbose                                Output text string of result\n");
   printf("  -V, --version                                Display version information\n");
   printf("  -?, --help                                   Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  flush [mode] [database]  Flush connections according to [mode].\n");
   printf("                           Allowed modes are:\n");
   printf("                           - 'gracefully' (default) to flush all connections gracefully\n");
   printf("                           - 'idle' to flush only idle connections\n");
   printf("                           - 'all' to flush all connections. USE WITH CAUTION!\n");
   printf("                           If no [database] name is specified, applies to all databases.\n");
   printf("  ping                     Verifies if pgagroal is up and checks PostgreSQL server connectivity\n");
   printf("  enable   [database]      Enables the specified databases (or all databases)\n");
   printf("  disable  [database]      Disables the specified databases (or all databases)\n");
   printf("  shutdown [mode]          Stops pgagroal pooler. The [mode] can be:\n");
   printf("                           - 'gracefully' (default) waits for active connections to quit\n");
   printf("                           - 'immediate' forces connections to close and terminate\n");
   printf("                           - 'cancel' avoid a previously issued 'shutdown gracefully'\n");
   printf("  status [details]         Status of pgagroal, with optional details\n");
   printf("  switch-to <server>       Switches to the specified primary server\n");
   printf("  conf <action>            Manages the configuration (e.g., reloads the configuration\n");
   printf("                           The subcommand <action> can be:\n");
   printf("                           - 'reload' to issue a configuration reload;\n");
   printf("                           - 'ls'  lists the configuration files used.\n");
   printf("                           - 'get' to obtain information about a runtime configuration value;\n");
   printf("                                   conf get <parameter_name>\n");
   printf("                           - 'set' to modify a configuration value;\n");
   printf("                                   conf set <parameter_name> <parameter_value>;\n");
   printf("                           - 'alias' to list all database aliases;\n");
   printf("                                   conf alias\n");
   printf("  clear <what>             Resets either the Prometheus statistics or the specified server.\n");
   printf("                           <what> can be\n");
   printf("                           - 'server' (default) followed by a server name\n");
   printf("                           - a server name on its own\n");
   printf("                           - 'prometheus' to reset the Prometheus metrics\n");
   printf("\n");
   printf("pgagroal: <%s>\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: <%s>\n", PGAGROAL_ISSUES);
}

int
main(int argc, char** argv)
{
   int socket = -1;
   SSL* s_ssl = NULL;
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* host = NULL;
   char* port = NULL;
   char* username = NULL;
   char* password = NULL;
   bool verbose = false;
   char* logfile = NULL;
   int c;
   int option_index = 0;
   size_t size;
   char un[MAX_USERNAME_LENGTH];
   struct main_configuration* config = NULL;
   bool remote_connection = false;
   long l_port;
   int32_t output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
   int32_t compression = MANAGEMENT_COMPRESSION_NONE;
   int32_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   size_t command_count = sizeof(command_table) / sizeof(struct pgagroal_command);
   struct pgagroal_parsed_command parsed = {.cmd = NULL, .args = {0}};

   while (1)
   {
      static struct option long_options[] =
         {
            {"config", required_argument, 0, 'c'},
            {"host", required_argument, 0, 'h'},
            {"port", required_argument, 0, 'p'},
            {"user", required_argument, 0, 'U'},
            {"password", required_argument, 0, 'P'},
            {"logfile", required_argument, 0, 'L'},
            {"format", required_argument, 0, 'F'},
            {"compress", required_argument, 0, 'C'},
            {"encrypt", required_argument, 0, 'E'},
            {"verbose", no_argument, 0, 'v'},
            {"version", no_argument, 0, 'V'},
            {"help", no_argument, 0, '?'}};

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:F:C:E:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'h':
            host = optarg;
            break;
         case 'p':
            port = optarg;
            break;
         case 'U':
            username = optarg;
            break;
         case 'P':
            password = strdup(optarg);
            if (password == NULL)
            {
               errx(1, "Error allocating memory for password");
            }
            break;
         case 'L':
            logfile = optarg;
            break;
         case 'F':
            if (!strncmp(optarg, "json", MISC_LENGTH))
            {
               output_format = MANAGEMENT_OUTPUT_FORMAT_JSON;
            }
            else if (!strncmp(optarg, "raw", MISC_LENGTH))
            {
               output_format = MANAGEMENT_OUTPUT_FORMAT_RAW;
            }
            else if (!strncmp(optarg, "text", MISC_LENGTH))
            {
               output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
            }
            else
            {
               warnx("pgagroal-cli: Format type is not correct");
               exit(1);
            }
            break;
         case 'C':
            if (!strncmp(optarg, "gz", MISC_LENGTH))
            {
               compression = MANAGEMENT_COMPRESSION_GZIP;
            }
            else if (!strncmp(optarg, "zstd", MISC_LENGTH))
            {
               compression = MANAGEMENT_COMPRESSION_ZSTD;
            }
            else if (!strncmp(optarg, "lz4", MISC_LENGTH))
            {
               compression = MANAGEMENT_COMPRESSION_LZ4;
            }
            else if (!strncmp(optarg, "bz2", MISC_LENGTH))
            {
               compression = MANAGEMENT_COMPRESSION_BZIP2;
            }
            else if (!strncmp(optarg, "none", MISC_LENGTH))
            {
               break;
            }
            else
            {
               warnx("pgagroal-cli: Compress method is not correct");
               exit(1);
            }
            break;
         case 'E':
            if (!strncmp(optarg, "aes", MISC_LENGTH))
            {
               encryption = MANAGEMENT_ENCRYPTION_AES256;
            }
            else if (!strncmp(optarg, "aes256", MISC_LENGTH))
            {
               encryption = MANAGEMENT_ENCRYPTION_AES256;
            }
            else if (!strncmp(optarg, "aes192", MISC_LENGTH))
            {
               encryption = MANAGEMENT_ENCRYPTION_AES192;
            }
            else if (!strncmp(optarg, "aes128", MISC_LENGTH))
            {
               encryption = MANAGEMENT_ENCRYPTION_AES128;
            }
            else if (!strncmp(optarg, "none", MISC_LENGTH))
            {
               break;
            }
            else
            {
               warnx("pgagroal-cli: Encrypt method is not correct");
               exit(1);
            }
            break;
         case 'v':
            verbose = true;
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
      errx(1, "Using the root account is not allowed");
   }

   // if the user has specified the host and port
   // options, she wants a remote connection
   // but both remote connection parameters have to be set
   if (host != NULL || port != NULL)
   {
      remote_connection = host != NULL && port != NULL;
      if (!remote_connection)
      {
         printf("pgagroal-cli: you need both -h and -p options to perform a remote connection\n");
         exit(1);
      }
   }

   // if the user has specified either a username or a password
   // there must be all the other pieces for a remote connection
   if ((username != NULL || password != NULL) && !remote_connection)
   {
      errx(1, "you need also -h and -p options to perform a remote connection");
   }

   // and she cannot use "local" and "remote" connections at the same time
   if (configuration_path != NULL && remote_connection)
   {
      errx(1, "Use either -c or -h/-p to define endpoint");
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   size = sizeof(struct main_configuration);
   if (pgagroal_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      errx(1, "Error creating shared memory");
   }
   pgagroal_init_configuration(shmem);

   if (configuration_path != NULL)
   {
      ret = pgagroal_read_configuration(shmem, configuration_path, false);
      if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_NOT_FOUND)
      {
         errx(1, "Configuration not found: <%s>", configuration_path);
      }
      else if (ret == PGAGROAL_CONFIGURATION_STATUS_FILE_TOO_BIG)
      {
         errx(1, "Too many sections in the configuration file <%s>", configuration_path);
      }

      if (logfile)
      {
         config = (struct main_configuration*)shmem;

         config->common.log_type = PGAGROAL_LOGGING_TYPE_FILE;
         memset(&config->common.log_path[0], 0, MISC_LENGTH);
         memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgagroal_start_logging())
      {
         errx(1, "Cannot start the logging subsystem");
      }

      config = (struct main_configuration*)shmem;
   }
   else
   {
      ret = pgagroal_read_configuration(shmem, PGAGROAL_DEFAULT_CONF_FILE, false);
      if (ret != PGAGROAL_CONFIGURATION_STATUS_OK)
      {
         if (!remote_connection)
         {
            errx(1, "Host (-h) and port (-p) must be specified to connect to the remote host");
         }
      }
      else
      {
         configuration_path = PGAGROAL_DEFAULT_CONF_FILE;

         if (logfile)
         {
            config = (struct main_configuration*)shmem;

            config->common.log_type = PGAGROAL_LOGGING_TYPE_FILE;
            memset(&config->common.log_path[0], 0, MISC_LENGTH);
            memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgagroal_start_logging())
         {
            errx(1, "Cannot start the logging subsystem");
         }
      }
   }

   if (!parse_command(argc, argv, optind, &parsed, command_table, command_count))
   {
      if (argc > optind)
      {
         char* command = argv[optind];
         display_helper(command);
      }
      else
      {
         usage();
      }
      exit_code = 1;
      goto done;
   }

   config = (struct main_configuration*)shmem;

   if (!remote_connection)
   {
      /* Local connection */
      if (pgagroal_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
      {
         exit_code = 1;
         goto done;
      }
   }
   else
   {
      /* Remote connection */
      if (pgagroal_connect(host, atoi(port), &socket, config->keep_alive, config->nodelay))
      {
         /* Remote connection */
         l_port = strtol(port, NULL, 10);
         if ((errno == ERANGE && (l_port == LONG_MAX || l_port == LONG_MIN)) || (errno != 0 && l_port == 0))
         {
            warnx("Specified port %s out of range", port);
            goto done;
         }

         // cannot connect to port less than 1024 because pgagroal
         // cannot be run as root!
         if (l_port <= 1024)
         {
            warnx("Not allowed port %ld", l_port);
            goto done;
         }

         if (pgagroal_connect(host, (int)l_port, &socket, config->keep_alive, config->nodelay))
         {
            warnx("No route to host: %s:%ld\n", host, l_port);
            goto done;
         }
      }

      /* User name */
      if (username == NULL)
      {
username:
         printf("User name: ");

         memset(&un, 0, sizeof(un));
         if (fgets(&un[0], sizeof(un), stdin) == NULL)
         {
            exit_code = 1;
            goto done;
         }
         un[strlen(un) - 1] = 0;
         username = &un[0];
      }

      if (username == NULL || strlen(username) == 0)
      {
         goto username;
      }

      /* Password */
      if (password == NULL)
      {
         printf("Password : ");
         password = pgagroal_get_password();
         printf("\n");
      }

      // Validate password is valid UTF-8
      if (!pgagroal_utf8_valid((const unsigned char*)password, strlen(password)))
      {
         warnx("pgagroal-cli: Invalid UTF-8 encoding in password");
         goto done;
      }
      // Check character length
      size_t char_count = pgagroal_utf8_char_length((const unsigned char*)password, strlen(password));
      if (char_count == (size_t)-1)
      {
         warnx("pgagroal-cli: Invalid UTF-8 encoding in password");
         goto done;
      }
      if (char_count > MAX_PASSWORD_CHARS)
      {
         warnx("pgagroal-cli: Password too long (max %d characters)", MAX_PASSWORD_CHARS);
         goto done;
      }

      /* Authenticate */
      if (pgagroal_remote_management_scram_sha256(username, password, socket, &s_ssl) != AUTH_SUCCESS)
      {
         printf("pgagroal-cli: Bad credentials for %s\n", username);
         goto done;
      }
   }

   if (parsed.cmd->action == MANAGEMENT_FLUSH)
   {
      exit_code = flush(s_ssl, socket, parsed.cmd->mode, parsed.args[0], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_ENABLEDB)
   {
      exit_code = enabledb(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_DISABLEDB)
   {
      exit_code = disabledb(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_GRACEFULLY)
   {
      exit_code = gracefully(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_SHUTDOWN)
   {
      exit_code = pgagroal_shutdown(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CANCEL_SHUTDOWN)
   {
      exit_code = cancel_shutdown(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_STATUS)
   {
      exit_code = status(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_DETAILS)
   {
      exit_code = details(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_PING)
   {
      exit_code = ping(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CLEAR)
   {
      exit_code = clear(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CLEAR_SERVER)
   {
      exit_code = clear_server(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_SWITCH_TO)
   {
      exit_code = switch_to(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RELOAD)
   {
      exit_code = reload(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONFIG_LS)
   {
      exit_code = conf_ls(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONFIG_GET)
   {
      if (parsed.args[0])
      {
         exit_code = conf_get(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = conf_get(s_ssl, socket, NULL, compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_CONFIG_SET)
   {
      exit_code = conf_set(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONFIG_ALIAS)
   {
      exit_code = conf_alias(s_ssl, socket, compression, encryption, output_format);
   }

done:

   if (s_ssl != NULL)
   {
      int res;
      res = SSL_shutdown(s_ssl);
      if (res == 0)
      {
         SSL_shutdown(s_ssl);
      }
      SSL_free(s_ssl);
   }

   pgagroal_disconnect(socket);
   pgagroal_stop_logging();
   pgagroal_destroy_shared_memory(shmem, size);

   free(password);

   if (verbose)
   {
      warnx("%s (%d)", exit_code == 0 ? "Success" : "Error", exit_code);
   }

   return exit_code;
}

static void
help_cancel_shutdown(void)
{
   printf("Cancel shutdown of pgagroal\n");
   printf("  pgagroal-cli cancel-shutdown\n");
}

static void
help_shutdown(void)
{
   printf("Shutdown pgagroal\n");
   printf("  pgagroal-cli shutdown\n");
}

static void
help_ping(void)
{
   printf("Check if pgagroal is alive\n");
   printf("  pgagroal-cli ping\n");
}

static void
help_status_details(void)
{
   printf("Status of pgagroal\n");
   printf("  pgagroal-cli status [details]\n");
}

static void
help_disabledb(void)
{
   printf("Disable a database\n");
   printf("  pgagroal-cli disabledb <database>|*\n");
}

static void
help_enabledb(void)
{
   printf("Enable a database\n");
   printf("  pgagroal-cli enabledb <database>|*\n");
}

static void
help_conf(void)
{
   printf("Manage the configuration\n");
   printf("  pgagroal-cli conf [reload]\n");
   printf("  pgagroal-cli conf [ls]\n");
   printf("  pgagroal-cli conf [get] <parameter_name>\n");
   printf("  pgagroal-cli conf [set] <parameter_name> <parameter_value>\n");
   printf("  pgagroal-cli conf [alias]\n");
}

static void
help_clear(void)
{
   printf("Reset data\n");
   printf("  pgagroal-cli clear [prometheus]\n");
}

static void
help_flush(void)
{
   printf("Flush connections\n");
   printf("  pgagroal-cli flush [gracefully|idle|all] [*|<database>]\n");
}

static void
help_switch_to(void)
{
   printf("Switch to another primary server\n");
   printf("  pgagroal-cli switch-to <server>\n");
}

static void
display_helper(char* command)
{
   if (!strcmp(command, COMMAND_CANCELSHUTDOWN))
   {
      help_cancel_shutdown();
   }
   else if (!strcmp(command, COMMAND_CONFIG_GET) ||
            !strcmp(command, COMMAND_CONFIG_LS) ||
            !strcmp(command, COMMAND_CONFIG_SET) ||
            !strcmp(command, COMMAND_CONFIG_ALIAS) ||
            !strcmp(command, COMMAND_RELOAD))
   {
      help_conf();
   }
   else if (!strcmp(command, COMMAND_DISABLEDB))
   {
      help_disabledb();
   }
   else if (!strcmp(command, COMMAND_ENABLEDB))
   {
      help_enabledb();
   }
   else if (!strcmp(command, COMMAND_FLUSH))
   {
      help_flush();
   }
   else if (!strcmp(command, COMMAND_PING))
   {
      help_ping();
   }
   else if (!strcmp(command, COMMAND_CLEAR) ||
            !strcmp(command, COMMAND_CLEAR_SERVER))
   {
      help_clear();
   }
   else if (!strcmp(command, COMMAND_SHUTDOWN))
   {
      help_shutdown();
   }
   else if (!strcmp(command, COMMAND_STATUS))
   {
      help_status_details();
   }
   else if (!strcmp(command, COMMAND_SWITCH_TO))
   {
      help_switch_to();
   }
   else
   {
      usage();
   }
}

static int
flush(SSL* ssl, int socket, int32_t mode, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_flush(ssl, socket, mode, database, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
enabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_enabledb(ssl, socket, database, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
disabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_disabledb(ssl, socket, database, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
gracefully(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_gracefully(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
pgagroal_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_shutdown(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
cancel_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_cancel_shutdown(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_status(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_details(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_ping(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
clear(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_clear(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
clear_server(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_clear_server(ssl, socket, server, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
switch_to(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_switch_to(ssl, socket, server, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_reload(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_conf_ls(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_conf_get(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_get_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_conf_set(ssl, socket, config_key, config_value, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_set_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgagroal_management_request_conf_alias(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   return process_alias_result(ssl, socket, output_format);

error:

   return 1;
}

static int
process_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* read = NULL;

   if (pgagroal_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
   {
      translate_json_object(read);
   }

   if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
   {
      pgagroal_json_print(read, FORMAT_TEXT);
   }
   else
   {
      pgagroal_json_print(read, FORMAT_JSON);
   }

   pgagroal_json_destroy(read);

   return 0;

error:

   pgagroal_json_destroy(read);

   return 1;
}

static int
process_get_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   bool is_char = false;
   char* char_res = NULL;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgagroal_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (get_config_key_result(config_key, read, &res, output_format))
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgagroal_json_print(json_res, FORMAT_JSON);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
      goto error;
   }

   if (!config_key) // error response | complete configuration
   {
      json_res = (struct json*)res;

      if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
      {
         pgagroal_json_print(json_res, FORMAT_TEXT);
      }
      else
      {
         pgagroal_json_print(json_res, FORMAT_JSON);
      }
   }
   else
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgagroal_json_print(json_res, FORMAT_JSON);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
   }

   pgagroal_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgagroal_json_destroy(json_res);
      }
   }

   return 0;

error:

   pgagroal_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgagroal_json_destroy(json_res);
      }
   }

   return 1;
}

static int
process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   char* conf_status = NULL;
   char* old_value = NULL;
   char* new_value = NULL;
   char* current_value = NULL;
   char* requested_value = NULL;

   if (pgagroal_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      pgagroal_log_fatal("Failed to read response from server\n");
      goto error;
   }

   if (!read)
   {
      pgagroal_log_fatal("No response received\n");
      goto error;
   }

   // For JSON output, just print the raw response
   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
   {
      pgagroal_json_print(read, FORMAT_JSON);
      pgagroal_json_destroy(read);
      return 0;
   }

   // Check for standard pgagroal management response structure
   outcome = (struct json*)pgagroal_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   response = (struct json*)pgagroal_json_get(read, MANAGEMENT_CATEGORY_RESPONSE);

   // Check for errors first
   if (outcome)
   {
      bool status = (bool)pgagroal_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS);
      int32_t error_code = (int32_t)pgagroal_json_get(outcome, MANAGEMENT_ARGUMENT_ERROR);

      if (!status || error_code != 0)
      {
         pgagroal_log_error("Configuration change failed\n");
         printf("   Invalid key format: '%s'\n", config_key ? config_key : "unknown");
         printf("   Valid formats: 'key', 'section.key', or 'section.context.key'\n");
         goto error;
      }
   }

   // Parse response for success cases
   if (response)
   {
      conf_status = (char*)pgagroal_json_get(response, CONFIGURATION_RESPONSE_STATUS);
      old_value = (char*)pgagroal_json_get(response, CONFIGURATION_RESPONSE_OLD_VALUE);
      new_value = (char*)pgagroal_json_get(response, CONFIGURATION_RESPONSE_NEW_VALUE);
      current_value = (char*)pgagroal_json_get(response, CONFIGURATION_RESPONSE_CURRENT_VALUE);
      requested_value = (char*)pgagroal_json_get(response, CONFIGURATION_RESPONSE_REQUESTED_VALUE);
   }

   // Handle success cases with accurate messaging
   if (conf_status && !strcmp(conf_status, CONFIGURATION_STATUS_SUCCESS))
   {
      printf("Configuration change applied successfully\n");
      printf("   Parameter: %s\n", config_key ? config_key : "unknown");
      printf("   Old value: %s\n", old_value ? old_value : "unknown");
      printf("   New value: %s\n", new_value ? new_value : "unknown");
      printf("   Status: Active (applied to running instance)\n");
   }
   else if (conf_status && !strcmp(conf_status, CONFIGURATION_STATUS_RESTART_REQUIRED))
   {
      printf("Configuration change requires manual restart\n");
      printf("   Parameter: %s\n", config_key ? config_key : "unknown");
      printf("   Current value: %s (unchanged in running instance)\n", current_value ? current_value : "unknown");
      printf("   Requested value: %s (cannot be applied to live instance)\n", requested_value ? requested_value : "unknown");
      printf("   Status: Requires full service restart\n");
   }
   else
   {
      printf("Configuration operation completed\n");
      printf("   Parameter: %s\n", config_key ? config_key : "unknown");
      printf("   Check server logs for details\n");
   }

   pgagroal_json_destroy(read);
   return 0;

error:
   if (read)
   {
      pgagroal_json_destroy(read);
   }
   return 1;
}

static int
get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format)
{
   char section[MISC_LENGTH];
   char context[MISC_LENGTH];
   char key[MISC_LENGTH];

   struct json* configuration_js = NULL;
   struct json* filtered_response = NULL;
   struct json* full_response = NULL;
   struct json* outcome_section = NULL;
   struct json* response = NULL;
   struct json* outcome = NULL;
   struct json_iterator* iter;
   char* config_value = NULL;
   enum value_type section_type;
   uintptr_t section_data;
   int part_count = 0;
   char* parts[4] = {NULL, NULL, NULL, NULL}; // Allow max 4 to detect invalid input
   char* token;
   char* config_key_copy = NULL;

   if (!config_key)
   {
      *r = (uintptr_t)j;
      return 0;
   }

   if (pgagroal_json_clone(j, &full_response))
   {
      goto error;
   }

   if (pgagroal_json_create(&filtered_response))
   {
      pgagroal_json_destroy(full_response);
      goto error;
   }

   memset(section, 0, MISC_LENGTH);
   memset(context, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   size_t config_key_len = strlen(config_key);
   config_key_copy = malloc(config_key_len + 1);
   if (!config_key_copy)
   {
      goto error;
   }
   memcpy(config_key_copy, config_key, config_key_len);
   config_key_copy[config_key_len] = '\0'; // Null terminate

   if (!config_key_copy)
   {
      goto error;
   }

   // Parse the config_key by dots and count them
   token = strtok(config_key_copy, ".");
   while (token != NULL && part_count < 4) // Allow parsing up to 4 parts to detect invalid input
   {
      size_t token_len = strlen(token);
      parts[part_count] = malloc(token_len + 1);
      if (!parts[part_count])
      {
         goto error;
      }
      memcpy(parts[part_count], token, token_len);
      parts[part_count][token_len] = '\0';
      part_count++;
      token = strtok(NULL, ".");
   }

   // Validate the number of parts - only 1, 2, or 3 parts are valid
   if (part_count < 1 || part_count > 3)
   {
      pgagroal_log_warn("Invalid configuration key format: %s (only 1-3 dot-separated parts are allowed)", config_key);
      goto error;
   }

   // Assign parts based on count
   if (part_count == 1)
   {
      // Single key: config_key
      strncpy(key, parts[0], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';
   }
   else if (part_count == 2)
   {
      // Two parts: section.key
      strncpy(section, parts[0], MISC_LENGTH - 1);
      section[MISC_LENGTH - 1] = '\0';
      strncpy(key, parts[1], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';

      // Treat "pgagroal" as the main section (empty)
      if (!strcasecmp(section, "pgagroal"))
      {
         memset(section, 0, MISC_LENGTH);
      }
   }
   else if (part_count == 3)
   {
      // Three parts: section.context.key
      strncpy(section, parts[0], MISC_LENGTH - 1);
      section[MISC_LENGTH - 1] = '\0';
      strncpy(context, parts[1], MISC_LENGTH - 1);
      context[MISC_LENGTH - 1] = '\0';
      strncpy(key, parts[2], MISC_LENGTH - 1);
      key[MISC_LENGTH - 1] = '\0';
   }

   response = (struct json*)pgagroal_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);
   outcome = (struct json*)pgagroal_json_get(j, MANAGEMENT_CATEGORY_OUTCOME);
   if (!response || !outcome)
   {
      goto error;
   }

   // Check if error response
   if (pgagroal_json_contains_key(outcome, MANAGEMENT_ARGUMENT_ERROR))
   {
      goto error;
   }

   if (strlen(section) > 0)
   {
      section_data = pgagroal_json_get_typed(response, section, &section_type);

      pgagroal_log_debug("Section '%s' has type: %s", section, pgagroal_value_type_to_string(section_type));

      if (section_type != ValueJSON)
      {
         goto error;
      }

      configuration_js = (struct json*)section_data;
      if (!configuration_js)
      {
         goto error;
      }
   }
   else
   {
      configuration_js = response;
   }

   if (pgagroal_json_iterator_create(configuration_js, &iter))
   {
      goto error;
   }

   while (pgagroal_json_iterator_next(iter))
   {
      // Handle three-part keys: section.context.key
      if (strlen(context) > 0)
      {
         // Looking for a specific context (like "mydb" in "limit.mydb.username")
         if (!strcmp(context, iter->key) && iter->value->type == ValueJSON)
         {
            struct json* nested_obj = (struct json*)iter->value->data;
            struct json_iterator* nested_iter;
            pgagroal_json_iterator_create(nested_obj, &nested_iter);
            while (pgagroal_json_iterator_next(nested_iter))
            {
               if (!strcmp(key, nested_iter->key))
               {
                  config_value = pgagroal_value_to_string(nested_iter->value, FORMAT_TEXT, NULL, 0);
                  if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
                  {
                     pgagroal_json_put(filtered_response, key, (uintptr_t)nested_iter->value->data, nested_iter->value->type);
                  }
                  break;
               }
            }
            pgagroal_json_iterator_destroy(nested_iter);
            break;
         }
      }
      else if (!strcmp(key, iter->key))
      {
         // Handle single or two-part keys
         config_value = pgagroal_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         if (iter->value->type == ValueJSON)
         {
            struct json* server_data = NULL;
            pgagroal_json_clone((struct json*)iter->value->data, &server_data);
            pgagroal_json_put(filtered_response, key, (uintptr_t)server_data, iter->value->type);
         }
         else
         {
            pgagroal_json_put(filtered_response, key, (uintptr_t)iter->value->data, iter->value->type);
         }
         break;
      }
   }

   pgagroal_json_iterator_destroy(iter);

   if (!config_value) // if key doesn't match with any field in configuration
   {
      goto error;
   }

   pgagroal_json_put(full_response, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)filtered_response, ValueJSON);

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON || !config_key)
   {
      *r = (uintptr_t)full_response;
      free(config_value);
      config_value = NULL;
   }
   else
   {
      *r = (uintptr_t)config_value;
      pgagroal_json_destroy(full_response);
   }

   // Clean up parts
   for (int i = 0; i < part_count; i++)
   {
      free(parts[i]);
      parts[i] = NULL;
   }

   free(config_key_copy);
   config_key_copy = NULL;

   return 0;

error:

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
   {
      // Update the Outcome section to indicate failure
      if (full_response)
      {
         outcome_section = (struct json*)pgagroal_json_get(full_response, MANAGEMENT_CATEGORY_OUTCOME);
         if (outcome_section)
         {
            pgagroal_json_put(outcome_section, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)false, ValueBool);
         }
         // Set empty response section
         pgagroal_json_put(full_response, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)filtered_response, ValueJSON);
         *r = (uintptr_t)full_response;
      }
      else
      {
         pgagroal_json_put(filtered_response, "Outcome", (uintptr_t)false, ValueBool);
         *r = (uintptr_t)filtered_response;
      }

      free(config_value);
      config_value = NULL;
   }
   else
   {
      free(config_value);
      config_value = NULL;

      config_value = (char*)malloc(6);
      if (config_value)
      {
         snprintf(config_value, 6, "Error");
      }
      *r = (uintptr_t)config_value;
      pgagroal_json_destroy(filtered_response);
      if (full_response)
      {
         pgagroal_json_destroy(full_response);
      }
   }

   // Clean up parts on error
   for (int i = 0; i < part_count; i++)
   {
      free(parts[i]);
      parts[i] = NULL;
   }

   free(config_key_copy);
   config_key_copy = NULL;

   return 1;
}

static int
process_alias_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* json = NULL;
   struct json* output = NULL;
   struct json* aliases_obj = NULL;
   struct json_iterator* iter = NULL;
   int result = 0;

   if (pgagroal_management_read_json(ssl, socket, NULL, NULL, &json))
   {
      result = 1;
      goto cleanup;
   }

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
   {
      pgagroal_json_print(json, FORMAT_JSON);
      goto cleanup;
   }

   // Text format output
   output = (struct json*)pgagroal_json_get(json, MANAGEMENT_CATEGORY_RESPONSE);
   if (!output)
   {
      printf("No alias data found.\n");
      pgagroal_log_debug("No response object found in JSON response");
      result = 1;
      goto cleanup;
   }

   aliases_obj = (struct json*)pgagroal_json_get(output, MANAGEMENT_ARGUMENT_DATABASES);
   if (!aliases_obj)
   {
      printf("No alias data found.\n");
      result = 1;
      pgagroal_log_debug("No databases object found in response");
      goto cleanup;
   }

   printf("# DATABASE=ALIASES                           USER           MAX   INIT   MIN\n");
   printf("#--------------------------------------------------------------------------\n");

   // Use JSON iterator to traverse the array
   if (pgagroal_json_iterator_create(aliases_obj, &iter) == 0)
   {
      while (pgagroal_json_iterator_next(iter))
      {
         struct json* entry = (struct json*)iter->value->data;
         struct json_iterator* alias_iter = NULL;

         char* database = iter->key;
         char* username = (char*)pgagroal_json_get(entry, CONFIGURATION_ARGUMENT_LIMIT_USERNAME);
         int max_size = (int)(uintptr_t)pgagroal_json_get(entry, CONFIGURATION_ARGUMENT_LIMIT_MAX_SIZE);
         int initial_size = (int)(uintptr_t)pgagroal_json_get(entry, CONFIGURATION_ARGUMENT_LIMIT_INITIAL_SIZE);
         int min_size = (int)(uintptr_t)pgagroal_json_get(entry, CONFIGURATION_ARGUMENT_LIMIT_MIN_SIZE);

         if (!database || !username)
         {
            pgagroal_log_debug("Corrupted alias entry - missing required fields (database or username)");
            result = 1;
            goto cleanup;
         }

         // Build the database=aliases string
         char db_alias_string[DB_ALIAS_STRING_LENGTH];
         snprintf(db_alias_string, sizeof(db_alias_string), "%s", database);

         struct json* alias_list = (struct json*)pgagroal_json_get(entry, CONFIGURATION_ARGUMENT_LIMIT_ALIASES);

         if (alias_list)
         {
            if (pgagroal_json_iterator_create(alias_list, &alias_iter) == 0)
            {
               bool first = true;
               while (pgagroal_json_iterator_next(alias_iter))
               {
                  if (first)
                  {
                     strcat(db_alias_string, "=");
                  }

                  // Aliases are simple strings
                  char* alias = (char*)alias_iter->value->data;

                  if (!alias)
                  {
                     pgagroal_log_debug("Error: Corrupted alias data - missing alias field");
                     pgagroal_json_iterator_destroy(alias_iter);
                     result = 1;
                     goto cleanup;
                  }

                  if (!first)
                  {
                     strcat(db_alias_string, ",");
                  }
                  strcat(db_alias_string, alias);
                  first = false;
               }
               pgagroal_json_iterator_destroy(alias_iter);
               alias_iter = NULL;
            }
         }

         // Print in compact configuration format
         if (max_size > 0)
         {
            // If we have limit configuration data, show it
            printf("%-40s    %-10s    %4d   %4d   %3d\n",
                   db_alias_string, username, max_size, initial_size, min_size);
         }
         else
         {
            // If no limit data, just show database=aliases and username
            printf("%-40s %-10s\n", db_alias_string, username);
         }
      }
   }
   else
   {
      pgagroal_log_debug("Failed to create JSON iterator for aliases");
      printf("No alias data found.\n");
      goto cleanup;
   }

cleanup:
   if (iter)
   {
      pgagroal_json_iterator_destroy(iter);
   }
   if (json)
   {
      pgagroal_json_destroy(json);
   }

   return result;
}
static char*
translate_command(int32_t cmd_code)
{
   char* command_output = NULL;
   switch (cmd_code)
   {
      case MANAGEMENT_CANCEL_SHUTDOWN:
         command_output = pgagroal_append(command_output, COMMAND_CANCELSHUTDOWN);
         break;
      case MANAGEMENT_DETAILS:
         command_output = pgagroal_append(command_output, COMMAND_STATUS_DETAILS);
         break;
      case MANAGEMENT_DISABLEDB:
         command_output = pgagroal_append(command_output, COMMAND_DISABLEDB);
         break;
      case MANAGEMENT_ENABLEDB:
         command_output = pgagroal_append(command_output, COMMAND_ENABLEDB);
         break;
      case MANAGEMENT_FLUSH:
         command_output = pgagroal_append(command_output, COMMAND_FLUSH);
         break;
      case MANAGEMENT_GRACEFULLY:
         command_output = pgagroal_append(command_output, COMMAND_GRACEFULLY);
         break;
      case MANAGEMENT_PING:
         command_output = pgagroal_append(command_output, COMMAND_PING);
         break;
      case MANAGEMENT_RELOAD:
         command_output = pgagroal_append(command_output, COMMAND_RELOAD);
         break;
      case MANAGEMENT_CONFIG_LS:
         command_output = pgagroal_append(command_output, COMMAND_CONFIG_LS);
         break;
      case MANAGEMENT_CONFIG_GET:
         command_output = pgagroal_append(command_output, COMMAND_CONFIG_GET);
         break;
      case MANAGEMENT_CONFIG_SET:
         command_output = pgagroal_append(command_output, COMMAND_CONFIG_SET);
         break;
      case MANAGEMENT_CONFIG_ALIAS:
         command_output = pgagroal_append(command_output, COMMAND_CONFIG_ALIAS);
         break;
      case MANAGEMENT_CLEAR:
         command_output = pgagroal_append(command_output, COMMAND_CLEAR);
         break;
      case MANAGEMENT_CLEAR_SERVER:
         command_output = pgagroal_append(command_output, COMMAND_CLEAR_SERVER);
         break;
      case MANAGEMENT_SHUTDOWN:
         command_output = pgagroal_append(command_output, COMMAND_SHUTDOWN);
         break;
      case MANAGEMENT_STATUS:
         command_output = pgagroal_append(command_output, COMMAND_STATUS);
         break;
      case MANAGEMENT_SWITCH_TO:
         command_output = pgagroal_append(command_output, COMMAND_SWITCH_TO);
         break;
      default:
         break;
   }
   return command_output;
}

static char*
translate_output_format(int32_t out_code)
{
   char* output_format_output = NULL;
   switch (out_code)
   {
      case MANAGEMENT_OUTPUT_FORMAT_JSON:
         output_format_output = pgagroal_append(output_format_output, OUTPUT_FORMAT_JSON);
         break;
      case MANAGEMENT_OUTPUT_FORMAT_TEXT:
         output_format_output = pgagroal_append(output_format_output, OUTPUT_FORMAT_TEXT);
         break;
      default:
         break;
   }
   return output_format_output;
}

static char*
translate_compression(int32_t compression_code)
{
   char* compression_output = NULL;
   switch (compression_code)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         compression_output = pgagroal_append(compression_output, "gzip");
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         compression_output = pgagroal_append(compression_output, "zstd");
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         compression_output = pgagroal_append(compression_output, "lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         compression_output = pgagroal_append(compression_output, "bzip2");
         break;
      default:
         compression_output = pgagroal_append(compression_output, "none");
         break;
   }
   return compression_output;
}

static char*
translate_encryption(int32_t encryption_code)
{
   char* encryption_output = NULL;
   switch (encryption_code)
   {
      case ENCRYPTION_AES_256_CBC:
         encryption_output = pgagroal_append(encryption_output, "aes-256-cbc");
         break;
      case ENCRYPTION_AES_192_CBC:
         encryption_output = pgagroal_append(encryption_output, "aes-192-cbc");
         break;
      case ENCRYPTION_AES_128_CBC:
         encryption_output = pgagroal_append(encryption_output, "aes-128-cbc");
         break;
      case ENCRYPTION_AES_256_CTR:
         encryption_output = pgagroal_append(encryption_output, "aes-256-ctr");
         break;
      case ENCRYPTION_AES_192_CTR:
         encryption_output = pgagroal_append(encryption_output, "aes-192-ctr");
         break;
      case ENCRYPTION_AES_128_CTR:
         encryption_output = pgagroal_append(encryption_output, "aes-128-ctr");
         break;
      default:
         encryption_output = pgagroal_append(encryption_output, "none");
         break;
   }
   return encryption_output;
}

static void
translate_json_object(struct json* j)
{
   struct json* header = NULL;
   int32_t command = 0;
   char* translated_command = NULL;
   int32_t out_format = -1;
   char* translated_out_format = NULL;
   int32_t out_compression = -1;
   char* translated_compression = NULL;
   int32_t out_encryption = -1;
   char* translated_encryption = NULL;

   // Translate arguments of header
   header = (struct json*)pgagroal_json_get(j, MANAGEMENT_CATEGORY_HEADER);

   if (header)
   {
      command = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);
      translated_command = translate_command(command);
      if (translated_command)
      {
         pgagroal_json_put(header, MANAGEMENT_ARGUMENT_COMMAND, (uintptr_t)translated_command, ValueString);
      }

      out_format = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_OUTPUT);
      translated_out_format = translate_output_format(out_format);
      if (translated_out_format)
      {
         pgagroal_json_put(header, MANAGEMENT_ARGUMENT_OUTPUT, (uintptr_t)translated_out_format, ValueString);
      }

      out_compression = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_COMPRESSION);
      translated_compression = translate_compression(out_compression);
      if (translated_compression)
      {
         pgagroal_json_put(header, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)translated_compression, ValueString);
      }

      out_encryption = (int32_t)pgagroal_json_get(header, MANAGEMENT_ARGUMENT_ENCRYPTION);
      translated_encryption = translate_encryption(out_encryption);
      if (translated_encryption)
      {
         pgagroal_json_put(header, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)translated_encryption, ValueString);
      }

      free(translated_command);
      free(translated_out_format);
      free(translated_compression);
      free(translated_encryption);
   }
}
