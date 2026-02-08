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

#ifndef PGAGROAL_H
#define PGAGROAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#if HAVE_OPENBSD
#include <sys/limits.h>
#endif
#include <sys/types.h>
#include <openssl/ssl.h>

#define PGAGROAL_HOMEPAGE "https://pgagroal.github.io/"
#define PGAGROAL_ISSUES   "https://github.com/pgagroal/pgagroal/issues"

#define MAIN_UDS          ".s.pgagroal"
#define TRANSFER_UDS      ".s.pgagroal.tu"

#ifdef HAVE_FREEBSD
#define PGAGROAL_DEFAULT_CONFIGURATION_PATH "/usr/local/etc/pgagroal/"
#else
#define PGAGROAL_DEFAULT_CONFIGURATION_PATH "/etc/pgagroal/"
#endif

#define PGAGROAL_DEFAULT_CONF_FILE               PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal.conf"
#define PGAGROAL_DEFAULT_HBA_FILE                PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_hba.conf"
#define PGAGROAL_DEFAULT_LIMIT_FILE              PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_databases.conf"
#define PGAGROAL_DEFAULT_USERS_FILE              PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_users.conf"
#define PGAGROAL_DEFAULT_FRONTEND_USERS_FILE     PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_frontend_users.conf"
#define PGAGROAL_DEFAULT_ADMINS_FILE             PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_admins.conf"
#define PGAGROAL_DEFAULT_SUPERUSER_FILE          PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_superuser.conf"
#define PGAGROAL_DEFAULT_VAULT_CONF_FILE         PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_vault.conf"
#define PGAGROAL_DEFAULT_VAULT_USERS_FILE        PGAGROAL_DEFAULT_CONFIGURATION_PATH "pgagroal_vault_users.conf"

#define MAX_PROCESS_TITLE_LENGTH                 256

#define DEFAULT_BUFFER_SIZE                      131072
#define RECV_BUFFER_HEADROOM                     8
#define MESSAGE_PARSE_BUFFER_SIZE                (DEFAULT_BUFFER_SIZE - RECV_BUFFER_HEADROOM)
#define SECURITY_BUFFER_SIZE                     1024
#define HTTP_BUFFER_SIZE                         1024

#define DEFAULT_BLOCKING_TIMEOUT                 30
#define DEFAULT_IDLE_TIMEOUT                     0
#define DEFAULT_ROTATE_FRONTEND_PASSWORD_TIMEOUT 0
#define DEFAULT_MAX_CONNECTION_AGE               0
#define DEFAULT_BACKGROUND_INTERVAL              300
#define DEFAULT_AUTHENTICATION_TIMEOUT           5

#define MAX_USERNAME_LENGTH                      128
#define MAX_DATABASE_LENGTH                      256
#define MAX_TYPE_LENGTH                          16
#define MAX_ADDRESS_LENGTH                       64
#define DEFAULT_PASSWORD_LENGTH                  64
#define MIN_PASSWORD_LENGTH                      8
#define MAX_PASSWORD_LENGTH                      1024
#define MAX_PASSWORD_CHARS                       256
#define MAX_APPLICATION_NAME                     64
#define MAX_ALIASES                              8
#define MAX_CERTIFICATES                         70

#define MAX_PATH                                 1024
#define MISC_LENGTH                              128
#define NUMBER_OF_SERVERS                        64
#ifdef DEBUG
#define MAX_NUMBER_OF_CONNECTIONS 8
#else
#define MAX_NUMBER_OF_CONNECTIONS 10000
#endif
#define NUMBER_OF_HBAS                 64
#define NUMBER_OF_LIMITS               64
#define NUMBER_OF_USERS                64
#define NUMBER_OF_ADMINS               8
#define NUMBER_OF_DISABLED             64

#define NUMBER_OF_SECURITY_MESSAGES    5

#define STATE_NOTINIT                  -2
#define STATE_INIT                     -1
#define STATE_FREE                     0
#define STATE_IN_USE                   1
#define STATE_GRACEFULLY               2
#define STATE_FLUSH                    3
#define STATE_IDLE_CHECK               4
#define STATE_MAX_CONNECTION_AGE       5
#define STATE_VALIDATION               6
#define STATE_REMOVE                   7

#define SECURITY_INVALID               -2
#define SECURITY_REJECT                -1
#define SECURITY_TRUST                 0
#define SECURITY_PASSWORD              3
#define SECURITY_MD5                   5
#define SECURITY_SCRAM256              10
#define SECURITY_ALL                   99

#define AUTH_SUCCESS                   0
#define AUTH_BAD_PASSWORD              1
#define AUTH_ERROR                     2
#define AUTH_TIMEOUT                   3

#define SERVER_NOTINIT                 -2
#define SERVER_NOTINIT_PRIMARY         -1
#define SERVER_PRIMARY                 0
#define SERVER_REPLICA                 1
#define SERVER_FAILOVER                2
#define SERVER_FAILED                  3

#define FLUSH_IDLE                     0
#define FLUSH_GRACEFULLY               1
#define FLUSH_ALL                      2

#define VALIDATION_OFF                 0
#define VALIDATION_FOREGROUND          1
#define VALIDATION_BACKGROUND          2

#define HISTOGRAM_BUCKETS              18

#define HUGEPAGE_OFF                   0
#define HUGEPAGE_TRY                   1
#define HUGEPAGE_ON                    2

#define TLS_CERT_AUTH_MODE_VERIFY_CA   0
#define TLS_CERT_AUTH_MODE_VERIFY_FULL 1

#define ENCRYPTION_NONE                0
#define ENCRYPTION_AES_256_CBC         1
#define ENCRYPTION_AES_192_CBC         2
#define ENCRYPTION_AES_128_CBC         3
#define ENCRYPTION_AES_256_CTR         4
#define ENCRYPTION_AES_192_CTR         5
#define ENCRYPTION_AES_128_CTR         6

#define COMPRESSION_NONE               0
#define COMPRESSION_CLIENT_GZIP        1
#define COMPRESSION_CLIENT_ZSTD        2
#define COMPRESSION_CLIENT_LZ4         3
#define COMPRESSION_CLIENT_BZIP2       4
#define COMPRESSION_SERVER_GZIP        5
#define COMPRESSION_SERVER_ZSTD        6
#define COMPRESSION_SERVER_LZ4         7

#define UPDATE_PROCESS_TITLE_NEVER     0
#define UPDATE_PROCESS_TITLE_STRICT    1
#define UPDATE_PROCESS_TITLE_MINIMAL   2
#define UPDATE_PROCESS_TITLE_VERBOSE   3

/**
 * Constants used to refer to an HBA entry field.
 */
#define PGAGROAL_HBA_ENTRY_TYPE     "type"
#define PGAGROAL_HBA_ENTRY_DATABASE "database"
#define PGAGROAL_HBA_ENTRY_USERNAME "username"
#define PGAGROAL_HBA_ENTRY_ADDRESS  "address"
#define PGAGROAL_HBA_ENTRY_METHOD   "method"

/**
 * Constants used to refer to the limit structure fields
 */
#define PGAGROAL_LIMIT_ENTRY_DATABASE          "database"
#define PGAGROAL_LIMIT_ENTRY_USERNAME          "username"
#define PGAGROAL_LIMIT_ENTRY_MAX_SIZE          "max_size"
#define PGAGROAL_LIMIT_ENTRY_MIN_SIZE          "min_size"
#define PGAGROAL_LIMIT_ENTRY_INITIAL_SIZE      "initial_size"
#define PGAGROAL_LIMIT_ENTRY_ALIASES           "aliases"
#define PGAGROAL_LIMIT_ENTRY_NUMBER_OF_ALIASES "number_of_aliases"
#define PGAGROAL_LIMIT_ENTRY_LINENO            "line_number"

// Key type enumeration
#define PGAGROAL_KEY_TYPE_UNKNOWN 0
#define PGAGROAL_KEY_TYPE_RSA     1
#define PGAGROAL_KEY_TYPE_ECDSA   2
#define PGAGROAL_KEY_TYPE_ED25519 3
#define PGAGROAL_KEY_TYPE_ED448   4
#define PGAGROAL_KEY_TYPE_DSA     5
#define PGAGROAL_KEY_TYPE_DH      6

// Signature algorithm enumeration
#define PGAGROAL_SIG_ALG_UNKNOWN           0
#define PGAGROAL_SIG_ALG_SHA256_WITH_RSA   1
#define PGAGROAL_SIG_ALG_SHA384_WITH_RSA   2
#define PGAGROAL_SIG_ALG_SHA512_WITH_RSA   3
#define PGAGROAL_SIG_ALG_SHA1_WITH_RSA     4
#define PGAGROAL_SIG_ALG_ECDSA_WITH_SHA256 5
#define PGAGROAL_SIG_ALG_ECDSA_WITH_SHA384 6
#define PGAGROAL_SIG_ALG_ECDSA_WITH_SHA512 7
#define PGAGROAL_SIG_ALG_ED25519           8
#define PGAGROAL_SIG_ALG_ED448             9
#define PGAGROAL_SIG_ALG_SHA256_WITH_PSS   10
#define PGAGROAL_SIG_ALG_SHA384_WITH_PSS   11
#define PGAGROAL_SIG_ALG_SHA512_WITH_PSS   12

// Key type name constants
#define PGAGROAL_KEY_TYPE_NAME_UNKNOWN "Unknown"
#define PGAGROAL_KEY_TYPE_NAME_RSA     "RSA"
#define PGAGROAL_KEY_TYPE_NAME_ECDSA   "ECDSA"
#define PGAGROAL_KEY_TYPE_NAME_ED25519 "Ed25519"
#define PGAGROAL_KEY_TYPE_NAME_ED448   "Ed448"
#define PGAGROAL_KEY_TYPE_NAME_DSA     "DSA"
#define PGAGROAL_KEY_TYPE_NAME_DH      "DH"

// Signature algorithm name constants
#define PGAGROAL_SIG_ALG_NAME_UNKNOWN           "Unknown"
#define PGAGROAL_SIG_ALG_NAME_SHA256_WITH_RSA   "SHA256WithRSA"
#define PGAGROAL_SIG_ALG_NAME_SHA384_WITH_RSA   "SHA384WithRSA"
#define PGAGROAL_SIG_ALG_NAME_SHA512_WITH_RSA   "SHA512WithRSA"
#define PGAGROAL_SIG_ALG_NAME_SHA1_WITH_RSA     "SHA1WithRSA"
#define PGAGROAL_SIG_ALG_NAME_ECDSA_WITH_SHA256 "ECDSAWithSHA256"
#define PGAGROAL_SIG_ALG_NAME_ECDSA_WITH_SHA384 "ECDSAWithSHA384"
#define PGAGROAL_SIG_ALG_NAME_ECDSA_WITH_SHA512 "ECDSAWithSHA512"
#define PGAGROAL_SIG_ALG_NAME_ED25519           "Ed25519"
#define PGAGROAL_SIG_ALG_NAME_ED448             "Ed448"
#define PGAGROAL_SIG_ALG_NAME_SHA256_WITH_PSS   "SHA256WithPSS"
#define PGAGROAL_SIG_ALG_NAME_SHA384_WITH_PSS   "SHA384WithPSS"
#define PGAGROAL_SIG_ALG_NAME_SHA512_WITH_PSS   "SHA512WithPSS"

/**
 * Constants used to manage the exit code
 * of a command sent over the socket in the
 * management stuff, e.g., `pgagroal-cli`.
 */
#define EXIT_STATUS_OK               0
#define EXIT_STATUS_CONNECTION_ERROR 1
#define EXIT_STATUS_DATA_ERROR       2

#define INDENT_PER_LEVEL             2
#define FORMAT_JSON                  0
#define FORMAT_TEXT                  1
#define FORMAT_JSON_COMPACT          2
#define BULLET_POINT                 "- "

#define likely(x)                    __builtin_expect(!!(x), 1)
#define unlikely(x)                  __builtin_expect(!!(x), 0)

#define EMPTY_STR(_s)                (_s[0] == 0)

#define MAX(a, b) \
   ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a > _b ? _a : _b; })

#define MIN(a, b) \
   ({ __typeof__ (a) _a = (a);  \
           __typeof__ (b) _b = (b);  \
           _a < _b ? _a : _b; })

/*
 * Common piece of code to perform a sleeping.
 *
 * @param zzz the amount of time to
 * sleep, expressed as nanoseconds.
 *
 * Example
   SLEEP(5000000L)
 *
 */
#define SLEEP(zzz)                  \
   do                               \
   {                                \
      struct timespec ts_private;   \
      ts_private.tv_sec = 0;        \
      ts_private.tv_nsec = zzz;     \
      nanosleep(&ts_private, NULL); \
   }                                \
   while (0);

/*
 * Commonly used block of code to sleep
 * for a specified amount of time and
 * then jump back to a specified label.
 *
 * @param zzz how much time to sleep (as long nanoseconds)
 * @param goto_to the label to which jump to
 *
 * Example:
 *
     ...
     else
       SLEEP_AND_GOTO(100000L, retry)
 */
#define SLEEP_AND_GOTO(zzz, goto_to) \
   do                                \
   {                                 \
      struct timespec ts_private;    \
      ts_private.tv_sec = 0;         \
      ts_private.tv_nsec = zzz;      \
      nanosleep(&ts_private, NULL);  \
      goto goto_to;                  \
   }                                 \
   while (0);

/**
 * The shared memory segment
 */
extern void* shmem;

/**
 * The shared memory segment for a pipeline
 */
extern void* pipeline_shmem;

/**
 * The shared memory segment for the Prometheus data
 */
extern void* prometheus_shmem;

/**
 * Shared memory used to contain the Prometheus
 * response cache.
 */
extern void* prometheus_cache_shmem;

/** @struct server
 * Defines a server
 */
struct server
{
   char name[MISC_LENGTH];       /**< The name of the server */
   char host[MISC_LENGTH];       /**< The host name of the server */
   int port;                     /**< The port of the server */
   int version;                  /**< The major version of the server */
   int minor_version;            /**< The minor version of the server */
   bool tls;                     /**< Use TLS if possible */
   char tls_cert_file[MAX_PATH]; /**< TLS certificate path */
   char tls_key_file[MAX_PATH];  /**< TLS key path */
   char tls_ca_file[MAX_PATH];   /**< TLS CA certificate path */
   atomic_schar state;           /**< The state of the server */
   int lineno;                   /**< The line number within the configuration file */
} __attribute__((aligned(64)));

/** @struct connection
 * Defines a connection
 */
struct connection
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char database[MAX_DATABASE_LENGTH]; /**< The database */
   char appname[MAX_APPLICATION_NAME]; /**< The application_name */

   bool new;           /**< Is the connection new */
   signed char server; /**< The server identifier */
   bool tx_mode;       /**< Connection in transaction mode */

   signed char has_security;                                                  /**< The security identifier */
   ssize_t security_lengths[NUMBER_OF_SECURITY_MESSAGES];                     /**< The lengths of the security messages */
   char security_messages[NUMBER_OF_SECURITY_MESSAGES][SECURITY_BUFFER_SIZE]; /**< The security messages */

   int backend_pid;    /**< The backend process id */
   int backend_secret; /**< The backend secret */

   signed char limit_rule; /**< The limit rule used */
   time_t start_time;      /**< The start timestamp */
   time_t timestamp;       /**< The last used timestamp */
   pid_t pid;              /**< The associated process id */
   int fd;                 /**< The descriptor */
} __attribute__((aligned(64)));

/** @struct hba
 * Defines a HBA entry
 */
struct hba
{
   char type[MAX_TYPE_LENGTH];         /**< The type */
   char database[MAX_DATABASE_LENGTH]; /**< The database */
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char address[MAX_ADDRESS_LENGTH];   /**< The address / mask */
   char method[MAX_ADDRESS_LENGTH];    /**< The access method */
   int lineno;                         /**< The line number within the configuration file */
} __attribute__((aligned(64)));

/** @struct limit
 * Defines a limit entry
 */
struct limit
{
   char database[MAX_DATABASE_LENGTH];             /**< The database */
   char username[MAX_USERNAME_LENGTH];             /**< The user name */
   char aliases[MAX_ALIASES][MAX_DATABASE_LENGTH]; /**< The aliases for the database */
   int aliases_count;                              /**< The number of aliases */
   atomic_ushort active_connections;               /**< The active number of connections */
   int max_size;                                   /**< The maximum pool size */
   int initial_size;                               /**< The initial pool size */
   int min_size;                                   /**< The minimum pool size */
   int lineno;                                     /**< The line number within the configuration file */
} __attribute__((aligned(64)));

/** @struct user
 * Defines a user
 */
struct user
{
   char username[MAX_USERNAME_LENGTH]; /**< The user name */
   char password[MAX_PASSWORD_LENGTH]; /**< The password */
} __attribute__((aligned(64)));

/** @struct vault_server
 * Defines a vault server
 */
struct vault_server
{
   struct server server; /**< The server */
   struct user user;     /**< The user */
} __attribute__((aligned(64)));

/** @struct certificate_info
 * Defines TLS certificate information for monitoring
 */
struct certificate_info
{
   char path[MAX_PATH];           /**< Certificate file path */
   time_t expiry_time;            /**< Certificate expiration timestamp (0 if invalid) */
   time_t not_before;             /**< Certificate valid from timestamp */
   bool is_valid;                 /**< Is certificate currently valid */
   bool is_accessible;            /**< Can we read the certificate file */
   bool parse_error;              /**< Did we encounter parsing errors */
   time_t last_checked;           /**< When we last checked this certificate */
   char type[32];                 /**< Certificate type: "main", "metrics", "server" */
   char server_name[MISC_LENGTH]; /**< Server name (for server certs) */

   // New fields for enhanced metrics
   char subject[256];                 /**< Certificate subject DN */
   char issuer[256];                  /**< Certificate issuer DN */
   char serial_number[64];            /**< Certificate serial number (hex) */
   char expires_date[32];             /**< Human readable expiry date */
   char valid_from_date[32];          /**< Human readable valid from date */
   char key_type_name[32];            /**< Key type name (RSA, ECDSA, etc.) */
   char signature_algorithm_name[64]; /**< Signature algorithm name */
   int key_size;                      /**< Key size in bits */
   bool is_ca;                        /**< Is this a CA certificate */
   int key_type;                      /**< Key type enum value */
   int signature_algorithm;           /**< Signature algorithm enum value */
} __attribute__((aligned(64)));

/** @struct certificate_metrics
 * Defines certificate metrics for Prometheus
 */
struct certificate_metrics
{
   atomic_ulong total;                              /**< Total certificates configured */
   atomic_ulong valid;                              /**< Number of valid certificates */
   atomic_ulong expired;                            /**< Number of expired certificates */
   atomic_ulong expiring_soon;                      /**< Expiring within 30 days */
   atomic_ulong parse_errors;                       /**< Number of parsing errors */
   atomic_ulong inaccessible;                       /**< Number of inaccessible certificate files */
   atomic_ulong configured;                         /**< Total number of certificates configured (including invalid) */
   struct certificate_info certs[MAX_CERTIFICATES]; /**< Certificate details */
   atomic_int cert_count;                           /**< Current number of certificates */
} __attribute__((aligned(64)));

/** @struct prometheus_connection
 * Defines the Prometheus connection metric
 */
struct prometheus_connection
{
   atomic_ullong query_count; /**< The number of queries per connection */
} __attribute__((aligned(64)));

/** @struct prometheus_cache
 * A structure to handle the Prometheus response
 * so that it is possible to serve the very same
 * response over and over depending on the cache
 * settings.
 *
 * The `valid_until` field stores the result
 * of `time(2)`.
 *
 * The cache is protected by the `lock` field.
 *
 * The `size` field stores the size of the allocated
 * `data` payload.
 */
struct prometheus_cache
{
   time_t valid_until; /**< when the cache will become not valid */
   atomic_schar lock;  /**< lock to protect the cache */
   size_t size;        /**< size of the cache */
   char data[];        /**< the payload */
} __attribute__((aligned(64)));

/** @struct prometheus
 * Defines the common Prometheus metrics
 */
struct prometheus
{
   // logging
   atomic_ulong logging_info;  /**< Logging: INFO */
   atomic_ulong logging_warn;  /**< Logging: WARN */
   atomic_ulong logging_error; /**< Logging: ERROR */
   atomic_ulong logging_fatal; /**< Logging: FATAL */

   // internal connections
   atomic_int client_sockets; /**< The number of sockets the client used */
   atomic_int self_sockets;   /**< The number of sockets used by pgagroal itself */

} __attribute__((aligned(64)));

/** @struct main_prometheus
 * Defines the Main Prometheus metrics
 */
struct main_prometheus
{
   struct prometheus prometheus_base;            /**< Common base class */
   atomic_ulong session_time[HISTOGRAM_BUCKETS]; /**< The histogram buckets */
   atomic_ulong session_time_sum;                /**< Total session time */

   atomic_ulong connection_error;              /**< The number of error calls */
   atomic_ulong connection_kill;               /**< The number of kill calls */
   atomic_ulong connection_remove;             /**< The number of remove calls */
   atomic_ulong connection_timeout;            /**< The number of timeout calls */
   atomic_ulong connection_return;             /**< The number of return calls */
   atomic_ulong connection_invalid;            /**< The number of invalid calls */
   atomic_ulong connection_get;                /**< The number of get calls */
   atomic_ulong connection_idletimeout;        /**< The number of idle timeout calls */
   atomic_ulong connection_max_connection_age; /**< The number of max connection age calls */
   atomic_ulong connection_flush;              /**< The number of flush calls */
   atomic_ulong connection_success;            /**< The number of success calls */

   /**< The number of connection awaiting due to `blocking_timeout` */
   atomic_ulong connections_awaiting[NUMBER_OF_LIMITS]; /**< The number of connection waiting per limit */
   atomic_ulong connections_awaiting_total;             /**< The number of connection waiting in total */

   atomic_ulong auth_user_success;      /**< The number of AUTH_SUCCESS calls */
   atomic_ulong auth_user_bad_password; /**< The number of AUTH_BAD_PASSWORD calls */
   atomic_ulong auth_user_error;        /**< The number of AUTH_ERROR calls */

   atomic_ulong client_wait;      /**< The number of waiting clients */
   atomic_ulong client_active;    /**< The number of active clients */
   atomic_ulong client_wait_time; /**< The time the client waits */

   atomic_ullong query_count; /**< The number of queries */
   atomic_ullong tx_count;    /**< The number of transactions */

   atomic_ullong network_sent;     /**< The bytes sent by clients */
   atomic_ullong network_received; /**< The bytes received from servers */

   atomic_ulong server_error[NUMBER_OF_SERVERS];          /**< The number of errors for a server */
   atomic_ulong failed_servers;                           /**< The number of failed servers */
   struct certificate_metrics cert_metrics;               /**< TLS certificate metrics */
   struct prometheus_connection prometheus_connections[]; /**< The number of prometheus connections (FMA) */

} __attribute__((aligned(64)));

/** @struct vault_prometheus
 * Defines the Vault Prometheus metrics
 */
struct vault_prometheus
{
   struct prometheus prometheus_base; /**< The Prometheus base */
} __attribute__((aligned(64)));

/** @struct configuration
 * Defines the common configurations between pgagroal and vault
 */
struct configuration
{
   char configuration_path[MAX_PATH];   /**< The configuration path */
   char host[MISC_LENGTH];              /**< The host */
   int port;                            /**< The port */
   unsigned int authentication_timeout; /**< The authentication timeout in seconds */

   // Logging
   int log_type;                       /**< The logging type */
   int log_level;                      /**< The logging level */
   char log_path[MISC_LENGTH];         /**< The logging path */
   bool log_connections;               /**< Log successful logins */
   bool log_disconnections;            /**< Log disconnects */
   int log_mode;                       /**< The logging mode */
   unsigned int log_rotation_size;     /**< bytes to force log rotation */
   unsigned int log_rotation_age;      /**< minutes for log rotation */
   char log_line_prefix[MISC_LENGTH];  /**< The logging prefix */
   atomic_schar log_lock;              /**< The logging lock */
   char default_log_path[MISC_LENGTH]; /**< The default logging path */

   // TLS support
   bool tls;                     /**< Is TLS enabled */
   char tls_cert_file[MAX_PATH]; /**< TLS certificate path */
   char tls_key_file[MAX_PATH];  /**< TLS key path */
   char tls_ca_file[MAX_PATH];   /**< TLS CA certificate path */
   // Prometheus
   unsigned char hugepage;              /**< Huge page support */
   int metrics;                         /**< The metrics port */
   unsigned int metrics_cache_max_age;  /**< Number of seconds to cache the Prometheus response */
   unsigned int metrics_cache_max_size; /**< Number of bytes max to cache the Prometheus response */
   char metrics_cert_file[MAX_PATH];    /**< Metrics TLS certificate path */
   char metrics_key_file[MAX_PATH];     /**< Metrics TLS key path */
   char metrics_ca_file[MAX_PATH];      /**< Metrics TLS CA certificate path */
};

/** @struct vault_configuration
 * Defines the configuration of pgagroal-vault
 */
struct vault_configuration
{
   struct configuration common;      /**< Common base class */
   char users_path[MAX_PATH];        /**< The configuration path */
   int number_of_users;              /**< The number of users */
   int ev_backend;                   /**< Selected ev backend */
   int tls_cert_auth_mode;           /**< TLS certificate authentication mode: verify-ca (0) or verify-full (1) */
   struct vault_server vault_server; /**< The vault servers */
} __attribute__((aligned(64)));

/** @struct main_configuration
 * Defines the configuration and state of pgagroal
 */
struct main_configuration
{
   struct configuration common;        /**< Common configurations */
   char hba_path[MAX_PATH];            /**< The HBA path */
   char limit_path[MAX_PATH];          /**< The limit path */
   char users_path[MAX_PATH];          /**< The users path */
   char frontend_users_path[MAX_PATH]; /**< The frontend users path */
   char admins_path[MAX_PATH];         /**< The admins path */
   char superuser_path[MAX_PATH];      /**< The superuser path */

   int management;    /**< The management port */
   bool gracefully;   /**< Is pgagroal in gracefully mode */
   bool keep_running; /**< Is pgagroal still running */

   bool all_disabled;                                      /**< Are all databases disabled */
   char disabled[NUMBER_OF_DISABLED][MAX_DATABASE_LENGTH]; /**< Which databases are disabled */

   int pipeline; /**< The pipeline type */

   bool failover;                            /**< Is failover enabled */
   char failover_script[MISC_LENGTH];        /**< The failover script */
   char failover_notify_script[MISC_LENGTH]; /**< Failover notify script */

   unsigned int update_process_title; /**< Behaviour for updating the process title */

   bool authquery; /**< Is authentication query enabled */

   atomic_ushort active_connections; /**< The active number of connections */
   int max_connections;              /**< The maximum number of connections */
   bool allow_unknown_users;         /**< Allow unknown users */

   unsigned int blocking_timeout;                 /**< The blocking timeout in seconds */
   unsigned int idle_timeout;                     /**< The idle timeout in seconds */
   unsigned int rotate_frontend_password_timeout; /**< The rotation frontend password timeout in seconds */
   int rotate_frontend_password_length;           /**< Length of randomised passwords */
   unsigned int max_connection_age;               /**< The max connection age in seconds */
   int validation;                                /**< Validation mode */
   unsigned int background_interval;              /**< Background validation timer in seconds */
   int max_retries;                               /**< The maximum number of retries */
   int disconnect_client;                         /**< Disconnect client if idle for more than the specified seconds */
   bool disconnect_client_force;                  /**< Force a disconnect client if active for more than the specified seconds */
   char pidfile[MAX_PATH];                        /**< File containing the PID */

   int ev_backend;                 /**< Selected ev backend */
   bool keep_alive;                /**< Use keep alive */
   bool nodelay;                   /**< Use NODELAY */
   int backlog;                    /**< The backlog for listen */
   bool tracker;                   /**< Tracker support */
   bool track_prepared_statements; /**< Track prepared statements (transaction pooling) */

   char unix_socket_dir[MISC_LENGTH]; /**< The directory for the Unix Domain Socket */

   atomic_schar su_connection; /**< The superuser connection */

   int number_of_servers;        /**< The number of servers */
   int number_of_hbas;           /**< The number of HBA entries */
   int number_of_limits;         /**< The number of limit entries */
   int number_of_users;          /**< The number of users */
   int number_of_frontend_users; /**< The number of users */
   int number_of_admins;         /**< The number of admins */

   atomic_schar states[MAX_NUMBER_OF_CONNECTIONS]; /**< The states */
   struct server servers[NUMBER_OF_SERVERS];       /**< The servers */
   struct hba hbas[NUMBER_OF_HBAS];                /**< The HBA entries */
   struct limit limits[NUMBER_OF_LIMITS];          /**< The limit entries */
   struct user users[NUMBER_OF_USERS];             /**< The users */
   struct user frontend_users[NUMBER_OF_USERS];    /**< The frontend users */
   struct user admins[NUMBER_OF_ADMINS];           /**< The admins */
   struct user superuser;                          /**< The superuser */
   struct connection connections[];                /**< The connections (FMA) */
} __attribute__((aligned(64)));

#ifdef __cplusplus
}
#endif

#endif
