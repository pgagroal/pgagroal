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
#include <bench.h>
#include <json.h>
#include <value.h>

/* system */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAX_FLAGS 32

static void add_bad_flag(char* arg, char** found, int* number_of_found);
static int compare_strings(const void* a, const void* b);
static bool bind_ok(int family, int port);

int
bench_check_build(char* compile_commands, char* src_dir)
{
   /* Clang builds get -O0 and coverage from the top CMakeLists.txt even in
    * Release, so check the real flags, not only the build type */
   struct json* commands = NULL;
   struct json_iterator* iter = NULL;
   struct json_iterator* args = NULL;
   char* found[MAX_FLAGS];
   int number_of_found = 0;
   int checked = 0;
   int rc = 1;

   if (pgagroal_json_read_file(compile_commands, &commands) || commands == NULL ||
       commands->type != JSONArray)
   {
      fprintf(stderr, "error: cannot read %s\n", compile_commands);
      goto done;
   }

   pgagroal_json_iterator_create(commands, &iter);
   while (pgagroal_json_iterator_next(iter))
   {
      struct json* entry = (struct json*)iter->value->data;
      char* file = NULL;

      if (iter->value->type != ValueJSON)
      {
         continue;
      }

      file = (char*)pgagroal_json_get(entry, "file");
      if (file == NULL || strncmp(file, src_dir, strlen(src_dir)))
      {
         continue;
      }
      checked++;

      if (pgagroal_json_contains_key(entry, "arguments"))
      {
         pgagroal_json_iterator_create((struct json*)pgagroal_json_get(entry, "arguments"), &args);
         while (pgagroal_json_iterator_next(args))
         {
            add_bad_flag((char*)args->value->data, &found[0], &number_of_found);
         }
         pgagroal_json_iterator_destroy(args);
         args = NULL;
      }
      else if (pgagroal_json_contains_key(entry, "command"))
      {
         char* command = strdup((char*)pgagroal_json_get(entry, "command"));
         char* saveptr = NULL;

         for (char* arg = strtok_r(command, " ", &saveptr); arg != NULL; arg = strtok_r(NULL, " ", &saveptr))
         {
            add_bad_flag(arg, &found[0], &number_of_found);
         }
         free(command);
      }
   }

   /* Nothing checked (e.g. a symlinked checkout): fail, don't pass */
   if (checked == 0)
   {
      fprintf(stderr, "error: no pgagroal sources found in compile_commands.json\n");
      goto done;
   }

   if (number_of_found > 0)
   {
      qsort(&found[0], number_of_found, sizeof(char*), compare_strings);
      fprintf(stderr, "error: the Release build is compiled with");
      for (int i = 0; i < number_of_found; i++)
      {
         fprintf(stderr, " %s", found[i]);
      }
      fprintf(stderr, "\n");
      goto done;
   }

   rc = 0;

done:

   for (int i = 0; i < number_of_found; i++)
   {
      free(found[i]);
   }
   pgagroal_json_iterator_destroy(iter);
   pgagroal_json_destroy(commands);

   return rc;
}

int
bench_port_free(int port)
{
   /* pg_isready is not enough: a leftover worker can hold the port without
    * answering */
   if (!bind_ok(AF_INET, port) || !bind_ok(AF_INET6, port))
   {
      return 1;
   }

   return 0;
}

int
bench_meta(char* path, int argc, char** argv)
{
   /* compare reads these as numbers and dirty as a boolean */
   char* numbers[] = {"cpus", "iterations", "duration", "warmup", "clients", "scale", "pgagroal_errors", NULL};
   struct json* meta = NULL;
   int rc = 1;

   if (pgagroal_json_create(&meta))
   {
      goto done;
   }

   for (int i = 0; i < argc; i++)
   {
      char* key = argv[i];
      char* value = strchr(argv[i], '=');

      if (value == NULL)
      {
         fprintf(stderr, "error: meta: '%s' is not KEY=VALUE\n", argv[i]);
         goto done;
      }
      *value++ = '\0';

      if (!strcmp(key, "dirty"))
      {
         pgagroal_json_put(meta, key, (uintptr_t)!strcmp(value, "true"), ValueBool);
      }
      else
      {
         pgagroal_json_put(meta, key, (uintptr_t)value, ValueString);
      }
   }

   for (int i = 0; numbers[i] != NULL; i++)
   {
      char* value = (char*)pgagroal_json_get(meta, numbers[i]);
      char* end = NULL;
      long long number;

      if (value == NULL)
      {
         fprintf(stderr, "error: meta: %s is missing\n", numbers[i]);
         goto done;
      }

      errno = 0;
      number = strtoll(value, &end, 10);
      if (*value == '\0' || *end != '\0' || errno != 0)
      {
         fprintf(stderr, "error: meta: %s is not a number ('%s')\n", numbers[i], value);
         goto done;
      }
      pgagroal_json_put(meta, numbers[i], (uintptr_t)number, ValueInt64);
   }

   if (pgagroal_json_write_file(path, meta))
   {
      fprintf(stderr, "error: cannot write %s\n", path);
      goto done;
   }

   rc = 0;

done:

   pgagroal_json_destroy(meta);

   return rc;
}

static void
add_bad_flag(char* arg, char** found, int* number_of_found)
{
   char* bad[] = {"-O0", "-fsanitize", "-fprofile-instr-generate", "-fcoverage-mapping", NULL};

   if (arg == NULL)
   {
      return;
   }

   for (int i = 0; bad[i] != NULL; i++)
   {
      if (!strncmp(arg, bad[i], strlen(bad[i])))
      {
         for (int j = 0; j < *number_of_found; j++)
         {
            if (!strcmp(found[j], arg))
            {
               return;
            }
         }
         if (*number_of_found < MAX_FLAGS)
         {
            found[(*number_of_found)++] = strdup(arg);
         }
         return;
      }
   }
}

static int
compare_strings(const void* a, const void* b)
{
   return strcmp(*(char* const*)a, *(char* const*)b);
}

static bool
bind_ok(int family, int port)
{
   struct sockaddr_storage addr;
   socklen_t length;
   int fd;
   int yes = 1;
   bool ok = true;

   memset(&addr, 0, sizeof(addr));
   if (family == AF_INET)
   {
      struct sockaddr_in* in = (struct sockaddr_in*)&addr;

      in->sin_family = AF_INET;
      in->sin_port = htons(port);
      in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      length = sizeof(struct sockaddr_in);
   }
   else
   {
      struct sockaddr_in6* in6 = (struct sockaddr_in6*)&addr;

      in6->sin6_family = AF_INET6;
      in6->sin6_port = htons(port);
      in6->sin6_addr = in6addr_loopback;
      length = sizeof(struct sockaddr_in6);
   }

   fd = socket(family, SOCK_STREAM, 0);
   if (fd == -1)
   {
      /* No such address family here: nothing can hold the port */
      return true;
   }

   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
   if (bind(fd, (struct sockaddr*)&addr, length) == -1)
   {
      /* No IPv6 here: nothing can hold the port */
      ok = family == AF_INET6 && errno == EADDRNOTAVAIL;
   }

   close(fd);

   return ok;
}
