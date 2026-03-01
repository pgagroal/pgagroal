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

#ifndef PGAGROAL_MANAGEMENT_H
#define PGAGROAL_MANAGEMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <json.h>

#include <stdbool.h>
#include <stdlib.h>

#include <openssl/ssl.h>

/**
 * Management header
 */
#define MANAGEMENT_COMPRESSION_NONE  0
#define MANAGEMENT_COMPRESSION_GZIP  1
#define MANAGEMENT_COMPRESSION_ZSTD  2
#define MANAGEMENT_COMPRESSION_LZ4   3
#define MANAGEMENT_COMPRESSION_BZIP2 4

#define MANAGEMENT_ENCRYPTION_NONE   0
#define MANAGEMENT_ENCRYPTION_AES256 1
#define MANAGEMENT_ENCRYPTION_AES192 2
#define MANAGEMENT_ENCRYPTION_AES128 3

/**
 * Management categories
 */
#define MANAGEMENT_CATEGORY_HEADER   "Header"
#define MANAGEMENT_CATEGORY_REQUEST  "Request"
#define MANAGEMENT_CATEGORY_RESPONSE "Response"
#define MANAGEMENT_CATEGORY_OUTCOME  "Outcome"

/**
 * Management commands
 */
#define MANAGEMENT_UNKNOWN         0
#define MANAGEMENT_CANCEL_SHUTDOWN 1
#define MANAGEMENT_CONFIG_LS       2
#define MANAGEMENT_CONFIG_GET      3
#define MANAGEMENT_CONFIG_SET      4
#define MANAGEMENT_DETAILS         5
#define MANAGEMENT_DISABLEDB       6
#define MANAGEMENT_ENABLEDB        7
#define MANAGEMENT_FLUSH           8
#define MANAGEMENT_GET_PASSWORD    9
#define MANAGEMENT_GRACEFULLY      10
#define MANAGEMENT_PING            11
#define MANAGEMENT_RELOAD          12
#define MANAGEMENT_CLEAR           13
#define MANAGEMENT_CLEAR_SERVER    14
#define MANAGEMENT_SHUTDOWN        15
#define MANAGEMENT_STATUS          16
#define MANAGEMENT_SWITCH_TO       17
#define MANAGEMENT_CONFIG_ALIAS    18

#define MANAGEMENT_MASTER_KEY      19
#define MANAGEMENT_ADD_USER        20
#define MANAGEMENT_UPDATE_USER     21
#define MANAGEMENT_REMOVE_USER     22
#define MANAGEMENT_LIST_USERS      23
/**
 * Management arguments
 */
#define MANAGEMENT_ARGUMENT_ACTIVE_CONNECTIONS  "ActiveConnections"
#define MANAGEMENT_ARGUMENT_APPNAME             "AppName"
#define MANAGEMENT_ARGUMENT_CLIENT_VERSION      "ClientVersion"
#define MANAGEMENT_ARGUMENT_COMMAND             "Command"
#define MANAGEMENT_ARGUMENT_COMPRESSION         "Compression"
#define MANAGEMENT_ARGUMENT_CONFIG_KEY          "ConfigKey"
#define MANAGEMENT_ARGUMENT_CONFIG_VALUE        "ConfigValue"
#define MANAGEMENT_ARGUMENT_CONNECTIONS         "Connections"
#define MANAGEMENT_ARGUMENT_DATABASE            "Database"
#define MANAGEMENT_ARGUMENT_DATABASES           "Databases"
#define MANAGEMENT_ARGUMENT_ENABLED             "Enabled"
#define MANAGEMENT_ARGUMENT_ENCRYPTION          "Encryption"
#define MANAGEMENT_ARGUMENT_ERROR               "Error"
#define MANAGEMENT_ARGUMENT_FD                  "FD"
#define MANAGEMENT_ARGUMENT_HOST                "Host"
#define MANAGEMENT_ARGUMENT_INITIAL_CONNECTIONS "InitialConnections"
#define MANAGEMENT_ARGUMENT_LIMITS              "Limits"
#define MANAGEMENT_ARGUMENT_MAX_CONNECTIONS     "MaxConnections"
#define MANAGEMENT_ARGUMENT_MIN_CONNECTIONS     "MinConnections"
#define MANAGEMENT_ARGUMENT_MODE                "Mode"
#define MANAGEMENT_ARGUMENT_NUMBER_OF_SERVERS   "NumberOfServers"
#define MANAGEMENT_ARGUMENT_OUTPUT              "Output"
#define MANAGEMENT_ARGUMENT_PASSWORD            "Password"
#define MANAGEMENT_ARGUMENT_PID                 "PID"
#define MANAGEMENT_ARGUMENT_PORT                "Port"
#define MANAGEMENT_ARGUMENT_RESTART             "Restart"
#define MANAGEMENT_ARGUMENT_SERVER              "Server"
#define MANAGEMENT_ARGUMENT_SERVERS             "Servers"
#define MANAGEMENT_ARGUMENT_SERVER_VERSION      "ServerVersion"
#define MANAGEMENT_ARGUMENT_START_TIME          "StartTime"
#define MANAGEMENT_ARGUMENT_STATE               "State"
#define MANAGEMENT_ARGUMENT_HEALTH              "Health"
#define MANAGEMENT_ARGUMENT_STATUS              "Status"
#define MANAGEMENT_ARGUMENT_TIME                "Time"
#define MANAGEMENT_ARGUMENT_TIMESTAMP           "Timestamp"
#define MANAGEMENT_ARGUMENT_TIMESTAMP           "Timestamp"
#define MANAGEMENT_ARGUMENT_TOTAL_CONNECTIONS   "TotalConnections"
#define MANAGEMENT_ARGUMENT_USERNAME            "Username"

/**
 * Management error
 */
#define MANAGEMENT_ERROR_BAD_PAYLOAD                        1
#define MANAGEMENT_ERROR_UNKNOWN_COMMAND                    2
#define MANAGEMENT_ERROR_ALLOCATION                         3

#define MANAGEMENT_ERROR_METRICS_NOFORK                     100
#define MANAGEMENT_ERROR_METRICS_NETWORK                    101

#define MANAGEMENT_ERROR_FLUSH_NOFORK                       200
#define MANAGEMENT_ERROR_FLUSH_NETWORK                      201

#define MANAGEMENT_ERROR_STATUS_NOFORK                      700
#define MANAGEMENT_ERROR_STATUS_NETWORK                     701

#define MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK              800
#define MANAGEMENT_ERROR_STATUS_DETAILS_NETWORK             801

#define MANAGEMENT_ERROR_CONF_GET_NOFORK                    900
#define MANAGEMENT_ERROR_CONF_GET_NETWORK                   901
#define MANAGEMENT_ERROR_CONF_GET_ERROR                     902

#define MANAGEMENT_ERROR_CONF_SET_NOFORK                    1000
#define MANAGEMENT_ERROR_CONF_SET_NETWORK                   1001
#define MANAGEMENT_ERROR_CONF_SET_ERROR                     1002
#define MANAGEMENT_ERROR_CONF_SET_NOREQUEST                 1003
#define MANAGEMENT_ERROR_CONF_SET_NOCONFIG_KEY_OR_VALUE     1004
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_SERVER            1005
#define MANAGEMENT_ERROR_CONF_SET_UNKNOWN_CONFIGURATION_KEY 1006

#define MANAGEMENT_ERROR_CONF_ALIAS_NOFORK                  1200
#define MANAGEMENT_ERROR_CONF_ALIAS_NETWORK                 1201
#define MANAGEMENT_ERROR_CONF_ALIAS_ERROR                   1202

#define MANAGEMENT_ERROR_SWITCH_TO_FAILED                   1300

/**
 * Output formats
 */
#define MANAGEMENT_OUTPUT_FORMAT_TEXT 0
#define MANAGEMENT_OUTPUT_FORMAT_JSON 1
#define MANAGEMENT_OUTPUT_FORMAT_RAW  2

/**
 * Create header for management command
 * @param command The command
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @param json The target json
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_create_header(int32_t command, uint8_t compression, uint8_t encryption, int32_t output_format, struct json** json);

/**
 * Create request for management command
 * @param json The target json
 * @param json The request json
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_create_request(struct json* json, struct json** request);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param start_t The start time of the command
 * @param end_t The end time of the command
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_create_outcome_success(struct json* json, time_t start_t, time_t end_t, struct json** outcome);

/**
 * Create success outcome for management command
 * @param json The target json
 * @param error The error code
 * @param outcome The outcome json
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_create_outcome_failure(struct json* json, int32_t error, struct json** outcome);

/**
 * Management operation: Flush the pool
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param mode The flush mode
 * @param database The database
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_flush(SSL* ssl, int socket, int32_t mode, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Enable database
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param database The database name
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_enabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Disable database
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param database The database name
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_disabledb(SSL* ssl, int socket, char* database, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Gracefully
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_gracefully(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Stop
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Cancel shutdown
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_cancel_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Status
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Details
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: isalive
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Clear
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_clear(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Clear server
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_clear_server(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Switch to
 * @param ssl The SSL connection
 * @param socket The socket
 * @param server The server
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_switch_to(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Management operation: Reload
 * @param ssl The SSL connection
 * @param socket The socket
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/* /\** */
/*  * Management operation: get a configuration setting. */
/*  * This function sends over the socket the message to get a configuration */
/*  * value. */
/*  * In particular, the message block for the action config_get is sent, */
/*  * then the size of the configuration parameter to get (e.g., `max_connections`) */
/*  * and last the parameter name itself. */
/*  * */
/*  * @param ssl the SSL connection */
/*  * @param socket the socket file descriptor */
/*  * @param config_key the name of the configuration parameter to get back */
/*  * @param compression The compress method for wire protocol */
/*  * @param encryption The encrypt method for wire protocol */
/*  * @param output_format The output format */
/*  * @return 0 on success, 1 on error */
/*  *\/ */
/* int */
/* pgagroal_management_request_config_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format); */

/* /\** */
/*  * Management operation: set a configuration setting. */
/*  * This function sends over the socket the message to set a configuration */
/*  * value. */
/*  * In particular, the message block for the action config_set is sent, */
/*  * then the size of the configuration parameter to set (e.g., `max_connections`), */
/*  * then the parameter name. At this point another couple of "size" and "value" is */
/*  * sent with the size of the value to set and its value. */
/*  * */
/*  * @param ssl the SSL connection */
/*  * @param socket the socket file descriptor */
/*  * @param config_key the name of the configuration parameter to set */
/*  * @param config_value the value to set for the new parameter */
/*  * @param compression The compress method for wire protocol */
/*  * @param encryption The encrypt method for wire protocol */
/*  * @param output_format The output format */
/*  * @return 0 on success, 1 on error */
/*  *\/ */
/* int */
/* pgagroal_management_request_config_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format); */

/**
 * Create a conf ls request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf ls request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_conf_get(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create a conf get request
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param config_key The configuration key
 * @param config_value The configuration value
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);

int
pgagroal_management_config_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Get the frontend password of a user
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param user The frontend user
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param output_format The output format
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_request_get_password(SSL* ssl, int socket, char* username, uint8_t compression, uint8_t encryption, int32_t output_format);

// Add around line 170:
int
pgagroal_management_request_conf_alias(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);

/**
 * Create an ok response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param start_time The start time
 * @param end_time The end time
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_response_ok(SSL* ssl, int socket, time_t start_time, time_t end_time, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create an error response
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param server The server
 * @param error The error code
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param payload The full payload
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_response_error(SSL* ssl, int socket, char* server, int32_t error, uint8_t compression, uint8_t encryption, struct json* payload);

/**
 * Create a response
 * @param json The JSON structure
 * @param server The server
 * @param response The response
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_create_response(struct json* json, int server, struct json** response);

/**
 * Read the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The pointer to an integer that will store the compress method
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_read_json(SSL* ssl, int socket, uint8_t* compression, uint8_t* encryption, struct json** json);

/**
 * Write the management JSON
 * @param ssl The SSL connection
 * @param socket The socket descriptor
 * @param compression The compress method for wire protocol
 * @param encryption The encrypt method for wire protocol
 * @param json The JSON structure
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_management_write_json(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, struct json* json);

#ifdef __cplusplus
}
#endif

#endif
