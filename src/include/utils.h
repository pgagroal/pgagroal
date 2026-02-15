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

#ifndef PGAGROAL_UTILS_H
#define PGAGROAL_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgagroal.h>
#include <ev.h>
#include <message.h>

/** @struct signal_info
 * Defines the signal structure
 */
struct signal_info
{
   struct signal_watcher sig_w; /**< The signal watcher (always first) */
   int slot;                    /**< The slot */
};

/** @struct accept_io
 * Defines the accept io structure
 */
struct accept_io
{
   struct io_watcher watcher; /**< The I/O (always first) */
   int socket;                /**< The socket */
   char** argv;               /**< The argv */
};

/** @struct client
 * Defines the client structure
 */
struct client
{
   pid_t pid;           /**< The process id */
   struct client* next; /**< The next client */
};

/** @struct pgagroal_command
 * Defines pgagroal commands.
 * The necessary fields are marked with an ">".
 *
 * Fields:
 * > command: The primary name of the command.
 * > subcommand: The subcommand name. If there is no subcommand, it should be filled with an empty literal string.
 * > accepted_argument_count: An array defining all the number of arguments this command accepts.
 *    Each entry represents a valid count of arguments, allowing the command to support overloads.
 * - default_argument: A default value for the command argument, used when no explicit argument is provided.
 * - log_message: A template string for logging command execution, which can include placeholders for dynamic values.
 * > action: A value indicating the specific action.
 * - mode: A value specifying the mode of operation or context in which the command applies.
 * > deprecated: A flag indicating whether this command is deprecated.
 * - deprecated_since_major: The major version number in which the command was deprecated.
 * - deprecated_since_minor: The minor version number in which the command was deprecated.
 * - deprecated_by: A string naming the command that replaces the deprecated command.
 *
 * This struct is key to extending and maintaining the command processing functionality in pgagroal,
 * allowing for clear definition and handling of all supported commands.
 */
struct pgagroal_command
{
   const char* command;                            /**< The command */
   const char* subcommand;                         /**< The sub-command */
   const int accepted_argument_count[MISC_LENGTH]; /**< The array of accepted arguments */

   const int action;             /**< The action */
   const int mode;               /**< The mode of the action */
   const char* default_argument; /**< The default argument */
   const char* log_message;      /**< The log message */

   /* Deprecation information */
   bool deprecated;                     /**< Is the command deprecated */
   unsigned int deprecated_since_major; /**< Deprecated since major */
   unsigned int deprecated_since_minor; /**< Deprecated since minor */
   const char* deprecated_by;           /**< Deprecated by */
};

/** @struct pgagroal_parsed_command
 * Holds parsed command data.
 *
 * Fields:
 * - cmd: A pointer to the command struct that was parsed.
 * - args: An array of pointers to the parsed arguments of the command (points to argv).
 */
struct pgagroal_parsed_command
{
   const struct pgagroal_command* cmd; /**< The command */
   char* args[MISC_LENGTH];            /**< The command arguments */
};

/**
 * Get the request identifier
 * @param msg The message
 * @return The identifier
 */
int32_t
pgagroal_get_request(struct message* msg);

/**
 * Extract the user name and database from a message
 * @param msg The message
 * @param username The resulting user name
 * @param database The resulting database
 * @param appname The resulting application_name
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_username_database(struct message* msg, char** username, char** database, char** appname);

/**
 * Extract a message from a message
 * @param type The message type to be extracted
 * @param msg The message
 * @param extracted The resulting message
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_message(char type, struct message* msg, struct message** extracted);

/**
 * Extract a message based on an offset
 * @param offset  The offset
 * @param data The data segment
 * @param extracted The resulting message
 * @return The next offset
 */
size_t
pgagroal_extract_message_offset(size_t offset, void* data, struct message** extracted);

/**
 * Extract an error message from a message
 * @param msg The message
 * @param error The error
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_extract_error_message(struct message* msg, char** error);

/**
 * Read a byte
 * @param data Pointer to the data
 * @return The byte
 */
signed char
pgagroal_read_byte(void* data);

/**
 * Read an uint8
 * @param data Pointer to the data
 * @return The uint8
 */
uint8_t
pgagroal_read_uint8(void* data);

/**
 * Read an int16
 * @param data Pointer to the data
 * @return The int16
 */
int16_t
pgagroal_read_int16(void* data);

/**
 * Read an int32
 * @param data Pointer to the data
 * @return The int32
 */
int32_t
pgagroal_read_int32(void* data);

/**
 * Read an uint32
 * @param data Pointer to the data
 * @return The uint32
 */
uint32_t
pgagroal_read_uint32(void* data);

/**
 * Read a long
 * @param data Pointer to the data
 * @return The long
 */
long
pgagroal_read_long(void* data);

/**
 * Read a string
 * @param data Pointer to the data
 * @return The string
 */
char*
pgagroal_read_string(void* data);

/**
 * Write a byte
 * @param data Pointer to the data
 * @param b The byte
 */
void
pgagroal_write_byte(void* data, signed char b);

/**
 * Write a uint8
 * @param data Pointer to the data
 * @param b The uint8
 */
void
pgagroal_write_uint8(void* data, uint8_t b);

/**
 * Write an int32
 * @param data Pointer to the data
 * @param i The int32
 */
void
pgagroal_write_int32(void* data, int32_t i);

/**
 * Write an uint32
 * @param data Pointer to the data
 * @param i The uint32
 */
void
pgagroal_write_uint32(void* data, uint32_t i);

/**
 * Write a long
 * @param data Pointer to the data
 * @param l The long int
 */
void
pgagroal_write_long(void* data, long l);

/**
 * Write a string
 * @param data Pointer to the data
 * @param s The string
 */
void
pgagroal_write_string(void* data, char* s);

/**
 * Is the machine big endian ?
 * @return True if big, otherwise false for little
 */
bool
pgagroal_bigendian(void);

/**
 * Swap
 * @param i The value
 * @return The swapped value
 */
unsigned int
pgagroal_swap(unsigned int i);

/**
 * Get the home directory
 * @return The directory
 */
char*
pgagroal_get_home_directory(void);

/**
 * Get the user name
 * @return The user name
 */
char*
pgagroal_get_user_name(void);

/**
 * Get a password from stdin
 * @return The password
 */
char*
pgagroal_get_password(void);

/**
 * File/directory exists
 * @param f The file/directory
 * @return The result
 */
bool
pgagroal_exists(const char* f);

/**
 * Path is a regular file
 * @param f The path
 * @return The result
 */
bool
pgagroal_is_file(const char* f);

/**
 * BASE64 encode a string
 * @param raw The string
 * @param raw_length The length of the raw string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @return 0 if success, otherwise 1
 */
int
pgagroal_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length);

/**
 * BASE64 decode a string
 * @param encoded The encoded string
 * @param encoded_length The length of the encoded string
 * @param raw The raw string
 * @param raw_length The length of the raw string
 * @return 0 if success, otherwise 1
 */
int
pgagroal_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length);

/**
 * Set process title.
 *
 * The function will autonomously check the update policy set
 * via the configuration option `update_process_title` and
 * will do nothing if the setting is `never`.
 * In the case the policy is set to `strict`, the process title
 * will not overflow the initial command line length (i.e., strlen(argv[*]))
 * otherwise it will do its best to set the title to the desired string.
 *
 * The policies `strict` and `minimal` will be honored only on Linux platforms
 * where a native call to set the process title is not available.
 *
 *
 * The resulting process title will be set to either `s1` or `s1/s2` if there
 * both strings and the length is allowed by the policy.
 *
 * @param argc The number of arguments
 * @param argv The argv pointer
 * @param s1 The first string
 * @param s2 The second string
 */
void
pgagroal_set_proc_title(int argc, char** argv, char* s1, char* s2);

/**
 * Sets the process title for a given connection.
 *
 * Uses `pgagroal_set_proc_title` to build an information string
 * with the form
 *    user@host:port/database
 *
 * This means that all the policies honored by the latter function and
 * set via the `update_process_title` configuration paramter will be
 * honored.
 *
 * @param argc the number of arguments
 * @param argv command line arguments
 * @param connection the struct connection pointer for the established connection.
 */
void
pgagroal_set_connection_proc_title(int argc, char** argv, struct connection* connection);

/**
 * Get the timestramp difference as a string
 * @param start_time The start time
 * @param end_time The end time
 * @param seconds The number of seconds
 * @return The timestamp string
 */
char*
pgagroal_get_timestamp_string(time_t start_time, time_t end_time, int32_t* seconds);

/**
 * Provide the application version number as a unique value composed of the three
 * specified parts. For example, when invoked with (1,5,0) it returns 10500.
 * Every part of the number must be between 0 and 99, and the function
 * applies a restriction on the values. For example passing 1 or 101 as one of the part
 * will produce the same result.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns a number made by (patch + minor * 100 + major * 10000 )
 */
unsigned int
pgagroal_version_as_number(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Provides the current version number of the application.
 * It relies on `pgagroal_version_as_number` and invokes it with the
 * predefined constants.
 *
 * @returns the current version number
 */
unsigned int
pgagroal_version_number(void);

/**
 * Checks if the currently running version number is
 * greater or equal than the specied one.
 *
 * @param major the major version number
 * @param minor the minor version number
 * @param patch the patch level
 * @returns true if the current version is greater or equal to the specified one
 */
bool
pgagroal_version_ge(unsigned int major, unsigned int minor, unsigned int patch);

/**
 * Does a string start with another string
 * @param str The string
 * @param prefix The prefix
 * @return The result
 */
bool
pgagroal_starts_with(char* str, char* prefix);

/**
 * Does a string end with another string
 * @param str The string
 * @param suffix The suffix
 * @return The result
 */
bool
pgagroal_ends_with(char* str, char* suffix);

/**
 * snprintf-like formatter that builds the result using pgexporter_append
 * helpers. The output is clamped to the smaller of
 * (PGEXPORTER_SNPRINTF_MAX_LENGTH) and (n-1). Returns the number of characters
 * that would have been written (excluding the NUL byte), similar to snprintf.
 * If buf is not NULL and n > 0, the output is NUL-terminated.
 *
 * Supported format specifiers: %% %s %c %d %i %u %ld %lu %lld %llu %zu %zd %x
 * %X %p %f %g
 *
 * @param buf The destination buffer (may be NULL if n == 0)
 * @param n The size of the destination buffer
 * @param fmt The format string
 * @param ... The format arguments
 * @return Number of characters that would have been written (excluding the NUL
 * byte)
 */
int
pgagroal_snprintf(char* buf, size_t n, const char* fmt, ...);

/**
 * Append a string
 *
 * @param orig The original string
 * @param s The string
 * @returns The new string
 */
char*
pgagroal_append(char* orig, char* s);

/**
 * Format a string and append it to the original string
 * @param buf original string
 * @param format The string to be formatted and appended to buf
 * @param ... The arguments to be formatted
 * @return The resulting string
 */
char*
pgagroal_format_and_append(char* buf, char* format, ...);

/**
 * Append an int
 *
 * @param orig The original string
 * @param i The int
 * @returns The new string
 */
char*
pgagroal_append_int(char* orig, int i);

/**
 * Append an unsigned long
 *
 * @param orig The original string
 * @param l The long
 * @returns The new string
 */
char*
pgagroal_append_ulong(char* orig, unsigned long l);

/**
 * Append an unsigned long long
 *
 * @param orig The original string
 * @param l The long
 * @returns The new string
 */
char*
pgagroal_append_ullong(char* orig, unsigned long long l);

/**
 * Append a char
 * @param orig The original string
 * @param s The string
 * @return The resulting string
 */
char*
pgagroal_append_char(char* orig, char c);

/**
 * Indent a string
 * @param str The string
 * @param tag [Optional] The tag, which will be applied after indentation if not NULL
 * @param indent The indent
 * @return The indented string
 */
char*
pgagroal_indent(char* str, char* tag, int indent);

/**
 * Compare two strings
 * @param str1 The first string
 * @param str2 The second string
 * @return true if the strings are the same, otherwise false
 */
bool
pgagroal_compare_string(const char* str1, const char* str2);

/**
 * Escape a string
 * @param str The original string
 * @return The escaped string
 */
char*
pgagroal_escape_string(char* str);

/**
 * Resolve path.
 * The function will resolve the path by expanding environment
 * variables (e.g., $HOME) in subpaths that are either surrounded
 * by double quotes (") or not surrounded by any quotes.
 * @param orig_path The original path
 * @param new_path Reference to the resolved path
 * @return 0 if success, otherwise 1
 */
int
pgagroal_resolve_path(char* orig_path, char** new_path);

/**
 * Generate a backtrace in the log
 * @return 0 if success, otherwise 1
 */
int
pgagroal_backtrace(void);

/**
 * Utility function to parse the command line
 * and search for a command.
 *
 * The function tries to be smart, in helping to find out
 * a command with the possible subcommand.
 *
 * @param argc the command line counter
 * @param argv the command line as provided to the application
 * @param offset the position at which the next token out of `argv`
 * has to be read. This is usually the `optind` set by getopt_long().
 * @param parsed an `struct pgagroal_parsed_command` to hold the parsed
 * data. It is modified inside the function to be accessed outside.
 * @param command_table array containing one `struct pgagroal_command` for
 * every possible command.
 * @param command_count number of commands in `command_table`.
 * @return true if the parsing of the command line was succesful, false
 * otherwise
 *
 */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgagroal_parsed_command* parsed,
              const struct pgagroal_command command_table[],
              size_t command_count);

/**
 * Given a server state, it returns a string that
 * described the state in a human-readable form.
 *
 * If the state cannot be determined, the numeric
 * form of the state is returned as a string.
 *
 * @param state the value of the sate for the server
 * @returns the string representing the state
 */
char*
pgagroal_server_state_as_string(signed char state);

/**
 * Utility function to convert the status of a connection
 * into a descriptive string. Useful to spurt the status
 * in command line output.
 *
 * @param state the actual state of the connection
 * @returns the (allocated) buffer with the string
 */
char*
pgagroal_connection_state_as_string(signed char state);

/**
 * Get the OS name and kernel version.
 *
 * @param os            Pointer to store the OS name (e.g., "Linux", "FreeBSD", "OpenBSD").
 *                      Memory will be allocated internally and should be freed by the caller.
 * @param kernel_major  Pointer to store the kernel major version.
 * @param kernel_minor  Pointer to store the kernel minor version.
 * @param kernel_patch  Pointer to store the kernel patch version.
 * @return              0 on success, 1 on error.
 */
int
pgagroal_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch);

/**
 * Remove all whitespace from a string
 * @param orig The original string
 * @return The new string with all whitespace removed
 */
char*
pgagroal_remove_all_whitespace(char* orig);

/**
 * Check and set directory path using caller-provided buffer
 * @param directory_path Directory to search for path
 * @param filename Filename to append
 * @param path_buffer Buffer to store the resulting path
 * @param buffer_size Size of the path_buffer
 * @return 0 upon success, otherwise 1
 */
int
pgagroal_normalize_path(char* directory_path, char* filename, char* default_path, char* path_buffer, size_t buffer_size);

/**
 * Time format specifiers for pgagroal_time_convert / pgagroal_time_format.
 */
enum pgagroal_time_format_t {
   FORMAT_TIME_MS = 0,   /**< Milliseconds */
   FORMAT_TIME_S,        /**< Seconds */
   FORMAT_TIME_MIN,      /**< Minutes */
   FORMAT_TIME_HOUR,     /**< Hours */
   FORMAT_TIME_DAY,      /**< Days */
   FORMAT_TIME_TIMESTAMP /**< ISO 8601 timestamp */
};

/**
 * Convert a pgagroal_time_t to a numeric value in the given unit.
 * @param t  The time value
 * @param fmt  The target unit
 * @return The value expressed in the target unit
 */
int64_t
pgagroal_time_convert(pgagroal_time_t t, enum pgagroal_time_format_t fmt);

/**
 * Check whether a pgagroal_time_t represents a valid (positive) duration.
 * @param t  The time value
 * @return true if ms > 0
 */
bool
pgagroal_time_is_valid(pgagroal_time_t t);

/**
 * Format a pgagroal_time_t into a human-readable string.
 * @param t       The time value
 * @param fmt     The target format
 * @param output  Receives a malloc'd string (caller must free)
 * @return 0 on success, 1 on error
 */
int
pgagroal_time_format(pgagroal_time_t t, enum pgagroal_time_format_t fmt, char** output);

#ifdef __cplusplus
}
#endif

#endif
