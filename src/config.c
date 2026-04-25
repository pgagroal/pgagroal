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
#include <logging.h>
#include <utils.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ACTION_CONFIG_INIT 300
#define ACTION_CONFIG_SET  301
#define ACTION_CONFIG_GET  302
#define ACTION_CONFIG_DEL  303
#define ACTION_CONFIG_LS   304

#define INPUT_BUFFER_SIZE  1024
#define MAX_LINE_LENGTH    4096
#define MAX_LINES          8192

// clang-format off
struct pgagroal_command command_table[] =
{
   {
      .command = "init",
      .subcommand = "",
      .accepted_argument_count = {0},
      .deprecated = false,
      .action = ACTION_CONFIG_INIT,
      .log_message = "<init>",
   },
   {
      .command = "set",
      .subcommand = "",
      .accepted_argument_count = {4, 5},
      .deprecated = false,
      .action = ACTION_CONFIG_SET,
      .log_message = "<set>",
   },
   {
      .command = "get",
      .subcommand = "",
      .accepted_argument_count = {3},
      .deprecated = false,
      .action = ACTION_CONFIG_GET,
      .log_message = "<get>",
   },
   {
      .command = "del",
      .subcommand = "",
      .accepted_argument_count = {2, 3},
      .deprecated = false,
      .action = ACTION_CONFIG_DEL,
      .log_message = "<del>",
   },
   {
      .command = "ls",
      .subcommand = "",
      .accepted_argument_count = {1, 2},
      .deprecated = false,
      .action = ACTION_CONFIG_LS,
      .log_message = "<ls>",
   },
};
// clang-format on

static void
version(void)
{
   printf("pgagroal-config %s\n", PGAGROAL_VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgagroal-config %s\n", PGAGROAL_VERSION);
   printf("  Configuration utility for pgagroal\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgagroal-config [ OPTIONS ] [ COMMAND ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -o, --output FILE        Set the output file path (default: ./pgagroal.conf)\n");
   printf("  -q, --quiet              Generate default options without prompts (for init)\n");
   printf("  -F, --force              Force overwrite if the output file already exists\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  init                     Generate a pgagroal.conf interactively\n");
   printf("  get <file> <section> <key>\n");
   printf("                           Get a configuration value\n");
   printf("  set <file> <section> <key> <value> [comment]\n");
   printf("                           Set a configuration value (optional inline comment)\n");
   printf("  del <file> <section> [key]\n");
   printf("                           Delete a section or key\n");
   printf("  ls <file> [section]      List sections or keys in a section\n");
   printf("\n");
   printf("pgagroal: %s\n", PGAGROAL_HOMEPAGE);
   printf("Report bugs: %s\n", PGAGROAL_ISSUES);
}

/**
 * Prompt the user for input with a default value.
 * If the user presses Enter without input, the default is used.
 * @param prompt The prompt message
 * @param default_value The default value (can be NULL for required fields)
 * @param result The buffer to store the result
 * @param result_size The size of the result buffer
 * @return 0 upon success, otherwise 1
 */
static int
prompt_input(const char* prompt, const char* default_value, char* result, size_t result_size)
{
   char buf[INPUT_BUFFER_SIZE];

   if (default_value != NULL && strlen(default_value) > 0)
   {
      printf("%s [%s]: ", prompt, default_value);
   }
   else
   {
      printf("%s: ", prompt);
   }

   memset(buf, 0, sizeof(buf));
   if (fgets(buf, sizeof(buf), stdin) == NULL)
   {
      return 1;
   }

   size_t len = strlen(buf);
   if (len > 0 && buf[len - 1] == '\n')
   {
      buf[len - 1] = '\0';
      len--;
   }

   if (len == 0 && default_value != NULL)
   {
      memset(result, 0, result_size);
      memcpy(result, default_value, MIN(result_size - 1, strlen(default_value)));
   }
   else if (len == 0 && default_value == NULL)
   {
      return 1;
   }
   else
   {
      memset(result, 0, result_size);
      memcpy(result, buf, MIN(result_size - 1, len));
   }

   return 0;
}

/**
 * Prompt the user for a yes/no question.
 * @param prompt The prompt message
 * @param default_yes True if default is yes
 * @return true for yes, false for no
 */
static bool
prompt_yes_no(const char* prompt, bool default_yes)
{
   char buf[INPUT_BUFFER_SIZE];

   if (default_yes)
   {
      printf("%s [Y/n]: ", prompt);
   }
   else
   {
      printf("%s [y/N]: ", prompt);
   }

   memset(buf, 0, sizeof(buf));
   if (fgets(buf, sizeof(buf), stdin) == NULL)
   {
      return default_yes;
   }

   size_t len = strlen(buf);
   if (len > 0 && buf[len - 1] == '\n')
   {
      buf[len - 1] = '\0';
      len--;
   }

   if (len == 0)
   {
      return default_yes;
   }

   if (buf[0] == 'y' || buf[0] == 'Y')
   {
      return true;
   }
   else if (buf[0] == 'n' || buf[0] == 'N')
   {
      return false;
   }

   return default_yes;
}

/**
 * Write a section header to a file.
 * @param file The file pointer
 * @param section The section name
 */
static void
write_section(FILE* file, const char* section)
{
   fprintf(file, "[%s]\n", section);
}

/**
 * Write a key-value pair to a file.
 * @param file The file pointer
 * @param key The key
 * @param value The value
 */
static void
write_key_value(FILE* file, const char* key, const char* value)
{
   fprintf(file, "%s = %s\n", key, value);
}

/**
 * Interactive configuration generator.
 * @param output_path The output file path
 * @return 0 upon success, otherwise 1
 */
static int
config_init(const char* output_path, bool quiet, bool force)
{
   FILE* file = NULL;
   char host[MISC_LENGTH];
   char port[MISC_LENGTH];
   char unix_socket_dir[MISC_LENGTH];
   char metrics[MISC_LENGTH];
   char management[MISC_LENGTH];
   char log_type[MISC_LENGTH];
   char log_level[MISC_LENGTH];
   char log_path[MAX_PATH];
   char max_connections[MISC_LENGTH];
   char idle_timeout[MISC_LENGTH];
   char validation[MISC_LENGTH];
   char ev_backend[MISC_LENGTH];
   struct stat st;
   char tmp_path[MAX_PATH];
   time_t t;
   struct tm* tm_info;
   char date_str[64];

   pgagroal_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", output_path);

   if (!quiet)
   {
      printf("pgagroal configuration generator\n");
      printf("================================\n\n");
   }

   if (stat(output_path, &st) == 0)
   {
      if (!force)
      {
         if (quiet)
         {
            warnx("Output file '%s' already exists. Use --force to overwrite.", output_path);
            return 1;
         }

         if (!prompt_yes_no("Output file already exists. Overwrite?", false))
         {
            printf("Aborted.\n");
            return 1;
         }
      }
   }

   if (!quiet)
   {
      printf("--- [pgagroal] section ---\n\n");
   }

   if (quiet)
   {
      pgagroal_snprintf(host, sizeof(host), CONFIGURATION_DEFAULT_HOST);
      pgagroal_snprintf(port, sizeof(port), CONFIGURATION_DEFAULT_PORT);
      pgagroal_snprintf(unix_socket_dir, sizeof(unix_socket_dir), CONFIGURATION_DEFAULT_UNIX_SOCKET_DIR);
      pgagroal_snprintf(metrics, sizeof(metrics), CONFIGURATION_DEFAULT_METRICS);
      pgagroal_snprintf(management, sizeof(management), CONFIGURATION_DEFAULT_MANAGEMENT);
      pgagroal_snprintf(log_type, sizeof(log_type), CONFIGURATION_DEFAULT_LOG_TYPE);
      pgagroal_snprintf(log_level, sizeof(log_level), CONFIGURATION_DEFAULT_LOG_LEVEL);
      pgagroal_snprintf(log_path, sizeof(log_path), CONFIGURATION_DEFAULT_LOG_PATH);
      pgagroal_snprintf(max_connections, sizeof(max_connections), CONFIGURATION_DEFAULT_MAX_CONNECTIONS);
      pgagroal_snprintf(idle_timeout, sizeof(idle_timeout), CONFIGURATION_DEFAULT_IDLE_TIMEOUT);
      pgagroal_snprintf(validation, sizeof(validation), CONFIGURATION_DEFAULT_VALIDATION);
      pgagroal_snprintf(ev_backend, sizeof(ev_backend), CONFIGURATION_DEFAULT_EV_BACKEND);
   }
   else
   {
      if (prompt_input("Host (bind address)", CONFIGURATION_DEFAULT_HOST, host, sizeof(host)))
      {
         warnx("Invalid input for host");
         goto error;
      }

      while (1)
      {
         if (prompt_input("Port (required)", CONFIGURATION_DEFAULT_PORT, port, sizeof(port)) == 0)
         {
            break;
         }
         printf("  port is required. Please enter a value.\n");
      }

      if (prompt_input("Unix socket directory", CONFIGURATION_DEFAULT_UNIX_SOCKET_DIR, unix_socket_dir, sizeof(unix_socket_dir)))
      {
         warnx("Invalid input for unix_socket_dir");
         goto error;
      }

      if (prompt_input("Max connections", CONFIGURATION_DEFAULT_MAX_CONNECTIONS, max_connections, sizeof(max_connections)))
      {
         warnx("Invalid input for max_connections");
         goto error;
      }

      if (prompt_input("Idle timeout (seconds, 0 to disable)", CONFIGURATION_DEFAULT_IDLE_TIMEOUT, idle_timeout, sizeof(idle_timeout)))
      {
         warnx("Invalid input for idle_timeout");
         goto error;
      }

      if (prompt_input("Validation (off, foreground, background)", CONFIGURATION_DEFAULT_VALIDATION, validation, sizeof(validation)))
      {
         warnx("Invalid input for validation");
         goto error;
      }

      if (prompt_input("Event backend (auto, io_uring, epoll, kqueue)", CONFIGURATION_DEFAULT_EV_BACKEND, ev_backend, sizeof(ev_backend)))
      {
         warnx("Invalid input for ev_backend");
         goto error;
      }

      if (prompt_input("Metrics port (0 to disable)", CONFIGURATION_DEFAULT_METRICS, metrics, sizeof(metrics)))
      {
         warnx("Invalid input for metrics");
         goto error;
      }

      if (prompt_input("Management port (0 to disable)", CONFIGURATION_DEFAULT_MANAGEMENT, management, sizeof(management)))
      {
         warnx("Invalid input for management");
         goto error;
      }

      if (prompt_input("Log type (console, file, syslog)", CONFIGURATION_DEFAULT_LOG_TYPE, log_type, sizeof(log_type)))
      {
         warnx("Invalid input for log_type");
         goto error;
      }

      if (prompt_input("Log level (fatal, error, warn, info, debug, trace)", CONFIGURATION_DEFAULT_LOG_LEVEL, log_level, sizeof(log_level)))
      {
         warnx("Invalid input for log_level");
         goto error;
      }

      if (prompt_input("Log path", CONFIGURATION_DEFAULT_LOG_PATH, log_path, sizeof(log_path)))
      {
         warnx("Invalid input for log_path");
         goto error;
      }
   }

   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open temp file '%s'", tmp_path);
      goto error;
   }

   t = time(NULL);
   tm_info = localtime(&t);
   strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm_info);

   fprintf(file, "# generated by pgagroal-config version %s\n", PGAGROAL_VERSION);
   fprintf(file, "# on date %s\n\n", date_str);

   write_section(file, PGAGROAL_MAIN_INI_SECTION);
   write_key_value(file, CONFIGURATION_ARGUMENT_HOST, host);
   write_key_value(file, CONFIGURATION_ARGUMENT_PORT, port);
   fprintf(file, "\n");
   write_key_value(file, CONFIGURATION_ARGUMENT_MAX_CONNECTIONS, max_connections);
   write_key_value(file, CONFIGURATION_ARGUMENT_IDLE_TIMEOUT, idle_timeout);
   write_key_value(file, CONFIGURATION_ARGUMENT_VALIDATION, validation);
   write_key_value(file, CONFIGURATION_ARGUMENT_EV_BACKEND, ev_backend);
   fprintf(file, "\n");
   write_key_value(file, CONFIGURATION_ARGUMENT_METRICS, metrics);
   write_key_value(file, CONFIGURATION_ARGUMENT_MANAGEMENT, management);
   fprintf(file, "\n");
   write_key_value(file, CONFIGURATION_ARGUMENT_LOG_TYPE, log_type);
   write_key_value(file, CONFIGURATION_ARGUMENT_LOG_LEVEL, log_level);
   write_key_value(file, CONFIGURATION_ARGUMENT_LOG_PATH, log_path);
   fprintf(file, "\n");
   write_key_value(file, CONFIGURATION_ARGUMENT_UNIX_SOCKET_DIR, unix_socket_dir);
   fprintf(file, "\n");

   if (quiet)
   {
      write_section(file, CONFIGURATION_ARGUMENT_PRIMARY);
      write_key_value(file, CONFIGURATION_ARGUMENT_HOST, CONFIGURATION_DEFAULT_SERVER_HOST);
      write_key_value(file, CONFIGURATION_ARGUMENT_PORT, CONFIGURATION_DEFAULT_SERVER_PORT);
      fprintf(file, "\n");
   }
   else
   {
      while (prompt_yes_no("\nAdd a PostgreSQL server?", true))
      {
         char section_name[MISC_LENGTH];
         char server_host[MISC_LENGTH];
         char server_port[MISC_LENGTH];

         printf("\n--- Server section ---\n\n");

         if (prompt_input("Section name", CONFIGURATION_ARGUMENT_PRIMARY, section_name, sizeof(section_name)))
         {
            warnx("Invalid input for section name");
            continue;
         }

         if (prompt_input("Host", CONFIGURATION_DEFAULT_SERVER_HOST, server_host, sizeof(server_host)))
         {
            warnx("Invalid input for server host");
            continue;
         }

         if (prompt_input("Port", CONFIGURATION_DEFAULT_SERVER_PORT, server_port, sizeof(server_port)))
         {
            warnx("Invalid input for server port");
            continue;
         }

         write_section(file, section_name);
         write_key_value(file, CONFIGURATION_ARGUMENT_HOST, server_host);
         write_key_value(file, CONFIGURATION_ARGUMENT_PORT, server_port);
         fprintf(file, "\n");
      }
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);

   if (rename(tmp_path, output_path) != 0)
   {
      warn("Could not rename %s to %s", tmp_path, output_path);
      unlink(tmp_path);
      goto error;
   }

   if (!quiet)
   {
      printf("\nConfiguration written to: %s\n", output_path);
   }

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
      file = NULL;
      unlink(tmp_path);
   }

   return 1;
}

/**
 * Trim leading and trailing whitespace from a string in-place.
 * @param str The string to trim
 * @return Pointer to the trimmed string (within the original buffer)
 */
static char*
trim(char* str)
{
   char* end;

   while (isspace((unsigned char)*str))
   {
      str++;
   }

   if (*str == '\0')
   {
      return str;
   }

   end = str + strlen(str) - 1;
   while (end > str && isspace((unsigned char)*end))
   {
      end--;
   }

   *(end + 1) = '\0';
   return str;
}

/**
 * Get a configuration value from a file.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name
 * @return 0 upon success, otherwise 1
 */
static int
config_get(const char* file_path, const char* section, const char* key)
{
   FILE* file = NULL;
   char line[MAX_LINE_LENGTH];
   bool in_section = false;
   char section_header[MISC_LENGTH];

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      goto error;
   }

   pgagroal_snprintf(section_header, sizeof(section_header), "[%s]", section);

   while (fgets(line, sizeof(line), file) != NULL)
   {
      char* trimmed = trim(line);

      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
      {
         continue;
      }

      if (trimmed[0] == '[')
      {
         char* nl = strchr(trimmed, '\n');
         if (nl)
         {
            *nl = '\0';
         }

         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }

         if (!strcmp(trimmed, section_header))
         {
            in_section = true;
         }
         else if (in_section)
         {
            /* We've moved past our target section */
            break;
         }
         continue;
      }

      if (in_section)
      {
         char* eq = strchr(trimmed, '=');
         if (eq != NULL)
         {
            *eq = '\0';
            char* found_key = trim(trimmed);
            char* found_value = trim(eq + 1);

            char* nl = strchr(found_value, '\n');
            if (nl)
            {
               *nl = '\0';
            }

            /* Strip inline comments */
            char* comment = strchr(found_value, '#');
            char* semicolon_comment = strchr(found_value, ';');

            if (semicolon_comment != NULL && (comment == NULL || semicolon_comment < comment))
            {
               comment = semicolon_comment;
            }

            if (comment != NULL)
            {
               *comment = '\0';
               found_value = trim(found_value);
            }

            if (!strcmp(found_key, key))
            {
               printf("%s\n", found_value);
               fclose(file);
               return 0;
            }
         }
      }
   }

   warnx("Key '%s' not found in section [%s]", key, section);

error:

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

/**
 * Set a configuration value in a file.
 * If the section or key doesn't exist, it is created.
 * Preserves comments, blank lines, and formatting.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name
 * @param value The value to set
 * @param comment Optional inline comment (NULL if none)
 * @return 0 upon success, otherwise 1
 */
static int
config_set(const char* file_path, const char* section, const char* key, const char* value, const char* comment)
{
   FILE* file = NULL;
   char* lines[MAX_LINES];
   int line_count = 0;
   char line_buf[MAX_LINE_LENGTH];
   char section_header[MISC_LENGTH];
   int section_start = -1;
   int section_end = -1;
   int key_line = -1;
   bool key_found = false;
   bool section_found = false;

   memset(lines, 0, sizeof(lines));

   pgagroal_snprintf(section_header, sizeof(section_header), "[%s]", section);

   /* Read all lines */
   file = fopen(file_path, "r");
   if (file != NULL)
   {
      while (fgets(line_buf, sizeof(line_buf), file) != NULL && line_count < MAX_LINES)
      {
         lines[line_count] = strdup(line_buf);
         if (lines[line_count] == NULL)
         {
            warnx("Out of memory");
            goto error;
         }
         line_count++;
      }
      fclose(file);
      file = NULL;
   }
   /* If file doesn't exist, we'll create it */

   /* Find the section and key */
   for (int i = 0; i < line_count; i++)
   {
      char temp[MAX_LINE_LENGTH];
      memset(temp, 0, sizeof(temp));
      memcpy(temp, lines[i], MIN(sizeof(temp) - 1, strlen(lines[i])));

      char* trimmed = trim(temp);

      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
      {
         continue;
      }

      /* Check for section header */
      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            *(bracket_end + 1) = '\0';
         }

         if (!strcmp(trimmed, section_header))
         {
            section_found = true;
            section_start = i;
            section_end = line_count; /* default to end of file */
         }
         else if (section_found && section_end == line_count)
         {
            /* Found the next section, so the previous section ends here */
            section_end = i;
         }
      }

      /* Look for key in our section */
      if (section_found && !key_found && i > section_start && (section_end == line_count || i < section_end))
      {
         char key_temp[MAX_LINE_LENGTH];
         memset(key_temp, 0, sizeof(key_temp));
         memcpy(key_temp, lines[i], MIN(sizeof(key_temp) - 1, strlen(lines[i])));

         char* kt = trim(key_temp);
         if (kt[0] != '#' && kt[0] != ';' && kt[0] != '[' && kt[0] != '\0')
         {
            char* eq = strchr(kt, '=');
            if (eq != NULL)
            {
               *eq = '\0';
               char* found_key = trim(kt);
               if (!strcmp(found_key, key))
               {
                  key_found = true;
                  key_line = i;
               }
            }
         }
      }
   }

   if (key_found)
   {
      /* Replace the existing line */
      free(lines[key_line]);
      char new_line[MAX_LINE_LENGTH];
      if (comment != NULL && strlen(comment) > 0)
      {
         pgagroal_snprintf(new_line, sizeof(new_line), "%s = %s # %s\n", key, value, comment);
      }
      else
      {
         pgagroal_snprintf(new_line, sizeof(new_line), "%s = %s\n", key, value);
      }
      lines[key_line] = strdup(new_line);
      if (lines[key_line] == NULL)
      {
         warnx("Out of memory");
         goto error;
      }
   }
   else if (section_found)
   {
      /* Insert the key at the end of the section (before blank lines / next section) */
      if (line_count >= MAX_LINES)
      {
         warnx("Too many lines in configuration file");
         goto error;
      }

      /* Find the last content line in this section (skip trailing blank lines) */
      int insert_at = section_end;
      for (int i = section_end - 1; i > section_start; i--)
      {
         char check_temp[MAX_LINE_LENGTH];
         memset(check_temp, 0, sizeof(check_temp));
         memcpy(check_temp, lines[i], MIN(sizeof(check_temp) - 1, strlen(lines[i])));
         char* ct = trim(check_temp);
         if (ct[0] != '\0')
         {
            insert_at = i + 1;
            break;
         }
      }

      /* Shift lines down */
      for (int i = line_count; i > insert_at; i--)
      {
         lines[i] = lines[i - 1];
      }

      char new_line[MAX_LINE_LENGTH];
      if (comment != NULL && strlen(comment) > 0)
      {
         pgagroal_snprintf(new_line, sizeof(new_line), "%s = %s # %s\n", key, value, comment);
      }
      else
      {
         pgagroal_snprintf(new_line, sizeof(new_line), "%s = %s\n", key, value);
      }
      lines[insert_at] = strdup(new_line);
      if (lines[insert_at] == NULL)
      {
         warnx("Out of memory");
         goto error;
      }
      line_count++;
   }
   else
   {
      /* Add new section and key at the end */
      if (line_count + 3 >= MAX_LINES)
      {
         warnx("Too many lines in configuration file");
         goto error;
      }

      /* Add blank line separator if file is non-empty */
      if (line_count > 0)
      {
         lines[line_count] = strdup("\n");
         line_count++;
      }

      char sec_line[MAX_LINE_LENGTH];
      pgagroal_snprintf(sec_line, sizeof(sec_line), "[%s]\n", section);
      lines[line_count] = strdup(sec_line);
      line_count++;

      char kv_line[MAX_LINE_LENGTH];
      if (comment != NULL && strlen(comment) > 0)
      {
         pgagroal_snprintf(kv_line, sizeof(kv_line), "%s = %s # %s\n", key, value, comment);
      }
      else
      {
         pgagroal_snprintf(kv_line, sizeof(kv_line), "%s = %s\n", key, value);
      }
      lines[line_count] = strdup(kv_line);
      line_count++;
   }

   char tmp_path[MAX_PATH];
   pgagroal_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);

   /* Write all lines back */
   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open file for writing: %s", tmp_path);
      goto error;
   }

   for (int i = 0; i < line_count; i++)
   {
      fputs(lines[i], file);
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);

   if (rename(tmp_path, file_path) != 0)
   {
      warn("Could not rename %s to %s", tmp_path, file_path);
      unlink(tmp_path);
      goto error;
   }

   /* Free all lines */
   for (int i = 0; i < line_count; i++)
   {
      free(lines[i]);
      lines[i] = NULL;
   }

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
      unlink(tmp_path);
   }

   for (int i = 0; i < line_count; i++)
   {
      if (lines[i] != NULL)
      {
         free(lines[i]);
         lines[i] = NULL;
      }
   }

   return 1;
}

/**
 * Delete a configuration section, or a key from a section.
 * @param file_path The config file path
 * @param section The section name
 * @param key The key name (or NULL to delete the whole section)
 * @return 0 upon success, otherwise 1
 */
static int
config_del(const char* file_path, const char* section, const char* key)
{
   FILE* file = NULL;
   char* lines[MAX_LINES];
   int line_count = 0;
   char line_buf[MAX_LINE_LENGTH];
   char section_header[MISC_LENGTH];
   int section_start = -1;
   int section_end = -1;
   bool section_found = false;
   int key_line = -1;
   char tmp_path[MAX_PATH];

   memset(lines, 0, sizeof(lines));
   pgagroal_snprintf(section_header, sizeof(section_header), "[%s]", section);

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      goto error;
   }

   while (fgets(line_buf, sizeof(line_buf), file) != NULL && line_count < MAX_LINES)
   {
      lines[line_count++] = strdup(line_buf);
   }
   fclose(file);
   file = NULL;

   for (int i = 0; i < line_count; i++)
   {
      char temp[MAX_LINE_LENGTH];
      memset(temp, 0, sizeof(temp));
      memcpy(temp, lines[i], MIN(sizeof(temp) - 1, strlen(lines[i])));
      char* trimmed = trim(temp);

      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
         continue;

      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
            *(bracket_end + 1) = '\0';

         if (!strcmp(trimmed, section_header))
         {
            section_found = true;
            section_start = i;
            section_end = line_count;
         }
         else if (section_found && section_end == line_count)
         {
            section_end = i;
         }
      }

      if (section_found && key != NULL && i > section_start && (section_end == line_count || i < section_end))
      {
         char key_temp[MAX_LINE_LENGTH];
         memset(key_temp, 0, sizeof(key_temp));
         memcpy(key_temp, lines[i], MIN(sizeof(key_temp) - 1, strlen(lines[i])));

         char* kt = trim(key_temp);
         if (kt[0] != '#' && kt[0] != ';' && kt[0] != '[' && kt[0] != '\0')
         {
            char* eq = strchr(kt, '=');
            if (eq != NULL)
            {
               *eq = '\0';
               if (!strcmp(trim(kt), key))
               {
                  key_line = i;
               }
            }
         }
      }
   }

   if (!section_found)
   {
      warnx("Section [%s] not found", section);
      goto error;
   }

   if (key != NULL && key_line == -1)
   {
      warnx("Key '%s' not found in section [%s]", key, section);
      goto error;
   }

   pgagroal_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);
   file = fopen(tmp_path, "w");
   if (file == NULL)
   {
      warn("Could not open temp file for writing: %s", tmp_path);
      goto error;
   }

   for (int i = 0; i < line_count; i++)
   {
      if (lines[i] == NULL)
         continue;

      if (key == NULL)
      {
         /* Delete section: skip lines in section bounds */
         if (i >= section_start && i < section_end)
            continue;
      }
      else
      {
         /* Delete key: skip key line */
         if (i == key_line)
            continue;
      }

      fputs(lines[i], file);
   }

   fflush(file);
#ifndef _WIN32
   fsync(fileno(file));
#endif
   fclose(file);
   file = NULL;

   chmod(tmp_path, S_IRUSR | S_IWUSR);

   if (rename(tmp_path, file_path) != 0)
   {
      warn("Could not rename %s to %s", tmp_path, file_path);
      unlink(tmp_path);
      goto error;
   }

   for (int i = 0; i < line_count; i++)
      free(lines[i]);
   return 0;

error:
   if (file != NULL)
      fclose(file);
   for (int i = 0; i < line_count; i++)
      if (lines[i])
         free(lines[i]);
   return 1;
}

/**
 * List configuration sections, or keys in a section.
 * @param file_path The config file path
 * @param section The section name (or NULL to list sections)
 * @return 0 upon success, otherwise 1
 */
static int
config_ls(const char* file_path, const char* section)
{
   FILE* file = NULL;
   char line[MAX_LINE_LENGTH];
   bool in_section = false;
   char section_header[MISC_LENGTH] = {0};

   file = fopen(file_path, "r");
   if (file == NULL)
   {
      warnx("Could not open file: %s", file_path);
      return 1;
   }

   if (section != NULL)
   {
      pgagroal_snprintf(section_header, sizeof(section_header), "[%s]", section);
   }

   while (fgets(line, sizeof(line), file) != NULL)
   {
      char* trimmed = trim(line);
      if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
         continue;

      if (trimmed[0] == '[')
      {
         char* bracket_end = strchr(trimmed, ']');
         if (bracket_end)
         {
            if (section == NULL)
            {
               /* Print section name without brackets */
               *bracket_end = '\0';
               printf("%s\n", trimmed + 1);
               continue;
            }
            else
            {
               *(bracket_end + 1) = '\0';
            }
         }

         if (section != NULL)
         {
            if (!strcmp(trimmed, section_header))
            {
               in_section = true;
            }
            else if (in_section)
            {
               break;
            }
         }
         continue;
      }

      if (section != NULL && in_section)
      {
         char* eq = strchr(trimmed, '=');
         if (eq != NULL)
         {
            *eq = '\0';
            printf("%s\n", trim(trimmed));
         }
      }
   }

   fclose(file);
   return 0;
}

int
main(int argc, char** argv)
{
   char* output_path = NULL;
   bool quiet = false;
   bool force = false;
   int c;
   int option_index = 0;
   size_t command_count = sizeof(command_table) / sizeof(struct pgagroal_command);
   struct pgagroal_parsed_command parsed = {.cmd = NULL, .args = {0}};

   /* Disable stdout buffering */
   setbuf(stdout, NULL);

   while (1)
   {
      static struct option long_options[] =
         {
            {"output", required_argument, 0, 'o'},
            {"quiet", no_argument, 0, 'q'},
            {"force", no_argument, 0, 'F'},
            {"version", no_argument, 0, 'V'},
            {"help", no_argument, 0, '?'},
         };

      c = getopt_long(argc, argv, "o:qFV?",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'o':
            output_path = optarg;
            break;
         case 'q':
            quiet = true;
            break;
         case 'F':
            force = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(0);
            break;
         default:
            break;
      }
   }

   if (getuid() == 0)
   {
      errx(1, "pgagroal-config: Using the root account is not allowed");
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   if (!parse_command(argc, argv, optind, &parsed, command_table, command_count))
   {
      usage();
      goto error;
   }

   if (parsed.cmd->action == ACTION_CONFIG_INIT)
   {
      if (output_path == NULL)
      {
         output_path = "pgagroal.conf";
      }

      if (config_init(output_path, quiet, force))
      {
         errx(1, "Error generating configuration");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_GET)
   {
      char* cfg_file = parsed.args[0];
      char* section = parsed.args[1];
      char* key = parsed.args[2];

      if (cfg_file == NULL || section == NULL || key == NULL)
      {
         warnx("Usage: pgagroal-config get <file> <section> <key>");
         goto error;
      }

      if (config_get(cfg_file, section, key))
      {
         exit(1);
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_SET)
   {
      char* cfg_file = parsed.args[0];
      char* section = parsed.args[1];
      char* key = parsed.args[2];
      char* value = parsed.args[3];
      char* comment = parsed.args[4];

      if (cfg_file == NULL || section == NULL || key == NULL || value == NULL)
      {
         warnx("Usage: pgagroal-config set <file> <section> <key> <value> [comment]");
         goto error;
      }

      if (config_set(cfg_file, section, key, value, comment))
      {
         errx(1, "Error setting configuration value");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_DEL)
   {
      char* cfg_file = parsed.args[0];
      char* section = parsed.args[1];
      char* key = parsed.args[2];

      if (cfg_file == NULL || section == NULL)
      {
         warnx("Usage: pgagroal-config del <file> <section> [key]");
         goto error;
      }

      if (config_del(cfg_file, section, key))
      {
         errx(1, "Error deleting configuration");
      }
   }
   else if (parsed.cmd->action == ACTION_CONFIG_LS)
   {
      char* cfg_file = parsed.args[0];
      char* section = parsed.args[1];

      if (cfg_file == NULL)
      {
         warnx("Usage: pgagroal-config ls <file> [section]");
         goto error;
      }

      if (config_ls(cfg_file, section))
      {
         exit(1);
      }
   }

   exit(0);

error:

   exit(1);
}
