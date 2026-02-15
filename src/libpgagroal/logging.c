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
#include <logging.h>
#include <prometheus.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define LINE_LENGTH 32
#define MAX_LENGTH  4096

static void output_log_line(char* l);

FILE* log_file;

time_t next_log_rotation_age; /* number of seconds at which the next location will happen */

char current_log_path[MAX_PATH]; /* the current log file */

static const char* levels[] =
   {
      "TRACE",
      "DEBUG",
      "INFO",
      "WARN",
      "ERROR",
      "FATAL"};

static const char* colors[] =
   {
      "\x1b[37m",
      "\x1b[36m",
      "\x1b[32m",
      "\x1b[91m",
      "\x1b[31m",
      "\x1b[35m"};

bool
log_rotation_enabled(void)
{
   struct configuration* config;
   config = (struct configuration*)shmem;

   // disable log rotation in the case
   // logging is not to a file
   if (config->log_type != PGAGROAL_LOGGING_TYPE_FILE)
   {
      log_rotation_disable();
      return false;
   }

   // log rotation is enabled if either log_rotation_age or
   // log_rotation_size is enabled
   return pgagroal_time_is_valid(config->log_rotation_age) || config->log_rotation_size != PGAGROAL_LOGGING_ROTATION_DISABLED;
}

void
log_rotation_disable(void)
{
   struct configuration* config;
   config = (struct configuration*)shmem;

   config->log_rotation_age = PGAGROAL_TIME_DISABLED;
   config->log_rotation_size = PGAGROAL_LOGGING_ROTATION_DISABLED;
   next_log_rotation_age = 0;
}

bool
log_rotation_required(void)
{
   struct stat log_stat;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (!log_rotation_enabled())
   {
      return false;
   }

   if (stat(current_log_path, &log_stat))
   {
      return false;
   }

   if (config->log_rotation_size > 0 && log_stat.st_size >= config->log_rotation_size)
   {
      return true;
   }

   if (pgagroal_time_is_valid(config->log_rotation_age) && next_log_rotation_age > 0 && next_log_rotation_age <= log_stat.st_ctime)
   {
      return true;
   }

   return false;
}

bool
log_rotation_set_next_rotation_age(void)
{
   struct configuration* config;
   time_t now;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE && pgagroal_time_is_valid(config->log_rotation_age))
   {
      now = time(NULL);
      if (!now)
      {
         config->log_rotation_age = PGAGROAL_TIME_DISABLED;
         return false;
      }

      next_log_rotation_age = now + pgagroal_time_convert(config->log_rotation_age, FORMAT_TIME_S);
      return true;
   }
   else
   {
      config->log_rotation_age = PGAGROAL_TIME_DISABLED;
      return false;
   }
}

/**
 *
 */
int
pgagroal_init_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      log_file_open();

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->log_path) > 0 ? config->log_path : config->default_log_path, strerror(errno));
         errno = 0;
         log_rotation_disable();
         return 1;
      }
   }

   return 0;
}

/**
 *
 */
int
pgagroal_start_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE && !log_file)
   {
      log_file_open();

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->log_path) > 0 ? config->log_path : config->default_log_path, strerror(errno));
         errno = 0;
         return 1;
      }
   }
   else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
   {
      openlog("pgagroal", LOG_CONS | LOG_PERROR | LOG_PID, LOG_USER);
   }

   return 0;
}

int
log_file_open(void)
{
   struct configuration* config;
   time_t htime;
   struct tm* tm;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      htime = time(NULL);
      if (!htime)
      {
         log_file = NULL;
         return 1;
      }

      tm = localtime(&htime);
      if (tm == NULL)
      {
         log_file = NULL;
         return 1;
      }

      if (strftime(current_log_path, sizeof(current_log_path), config->log_path, tm) <= 0)
      {
         // cannot parse the format string, fallback to default logging
         memcpy(current_log_path, config->default_log_path, strlen(config->default_log_path));
         log_rotation_disable();
      }

      log_file = fopen(current_log_path, config->log_mode == PGAGROAL_LOGGING_MODE_APPEND ? "a" : "w");

      if (!log_file)
      {
         return 1;
      }

      log_rotation_set_next_rotation_age();
      return 0;
   }

   return 1;
}

void
log_file_rotate(void)
{
   if (log_rotation_enabled())
   {
      fflush(log_file);
      fclose(log_file);
      log_file_open();
   }
}

/**
 *
 */
int
pgagroal_stop_logging(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      if (log_file != NULL)
      {
         return fclose(log_file);
      }
      else
      {
         return 1;
      }
   }
   else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
   {
      closelog();
   }

   return 0;
}

void
pgagroal_log_line(int level, char* file, int line, char* fmt, ...)
{
   signed char isfree;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config == NULL)
   {
      return;
   }

   if (level >= config->log_level)
   {
      switch (level)
      {
         case PGAGROAL_LOGGING_LEVEL_INFO:
            pgagroal_prometheus_logging(PGAGROAL_LOGGING_LEVEL_INFO);
            break;
         case PGAGROAL_LOGGING_LEVEL_WARN:
            pgagroal_prometheus_logging(PGAGROAL_LOGGING_LEVEL_WARN);
            break;
         case PGAGROAL_LOGGING_LEVEL_ERROR:
            pgagroal_prometheus_logging(PGAGROAL_LOGGING_LEVEL_ERROR);
            break;
         case PGAGROAL_LOGGING_LEVEL_FATAL:
            pgagroal_prometheus_logging(PGAGROAL_LOGGING_LEVEL_FATAL);
            break;
         default:
            break;
      }

retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->log_lock, &isfree, STATE_IN_USE))
      {
         char buf[256];
         va_list vl;
         struct tm* tm;
         time_t t;
         char* filename;

         t = time(NULL);
         tm = localtime(&t);

         filename = strrchr(file, '/');
         if (filename != NULL)
         {
            filename = filename + 1;
         }
         else
         {
            filename = file;
         }

         if (strlen(config->log_line_prefix) == 0)
         {
            memcpy(config->log_line_prefix, PGAGROAL_LOGGING_DEFAULT_LOG_LINE_PREFIX, strlen(PGAGROAL_LOGGING_DEFAULT_LOG_LINE_PREFIX));
         }

         va_start(vl, fmt);

         if (config->log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
         {
            buf[strftime(buf, sizeof(buf), config->log_line_prefix, tm)] = '\0';
            fprintf(stdout, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    buf, colors[level - 1], levels[level - 1],
                    filename, line);
            vfprintf(stdout, fmt, vl);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_FILE)
         {
            buf[strftime(buf, sizeof(buf), config->log_line_prefix, tm)] = '\0';
            fprintf(log_file, "%s %-5s %s:%d ",
                    buf, levels[level - 1], filename, line);
            vfprintf(log_file, fmt, vl);
            fprintf(log_file, "\n");
            fflush(log_file);

            if (log_rotation_required())
            {
               log_file_rotate();
            }
         }
         else if (config->log_type == PGAGROAL_LOGGING_TYPE_SYSLOG)
         {
            switch (level)
            {
               case PGAGROAL_LOGGING_LEVEL_DEBUG5:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_DEBUG1:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_INFO:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_WARN:
                  vsyslog(LOG_WARNING, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_ERROR:
                  vsyslog(LOG_ERR, fmt, vl);
                  break;
               case PGAGROAL_LOGGING_LEVEL_FATAL:
                  vsyslog(LOG_CRIT, fmt, vl);
                  break;
               default:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
            }
         }

         va_end(vl);

         atomic_store(&config->log_lock, STATE_FREE);
      }
      else
      {
         SLEEP_AND_GOTO(1000000L, retry)
      }
   }
}

void
pgagroal_log_mem(void* data, size_t size)
{
   signed char isfree;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config == NULL)
   {
      return;
   }

   if (size > 0)
   {
      if (config->common.log_level == PGAGROAL_LOGGING_LEVEL_DEBUG5 &&
          (config->common.log_type == PGAGROAL_LOGGING_TYPE_CONSOLE || config->common.log_type == PGAGROAL_LOGGING_TYPE_FILE))
      {
retry:
         isfree = STATE_FREE;

         if (atomic_compare_exchange_strong(&config->common.log_lock, &isfree, STATE_IN_USE))
         {
            if (size > MAX_LENGTH)
            {
               int index = 0;
               size_t count = 0;

               /* Display the first 1024 bytes */
               index = 0;
               count = 1024;
               while (count > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;

                  for (int i = 0; i < LINE_LENGTH; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + index + i);
                     pgagroal_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgagroal_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgagroal_append_char(n, c);
                     }
                     else
                     {
                        n = pgagroal_append_char(n, '?');
                     }
                  }

                  t = pgagroal_append(t, l);
                  t = pgagroal_append_char(t, ' ');
                  t = pgagroal_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  count -= LINE_LENGTH;
                  index += LINE_LENGTH;
               }

               output_log_line("---------------------------------------------------------------- --------------------------------");

               /* Display the last 1024 bytes */
               index = size - 1024;
               count = 1024;
               while (count > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;

                  for (int i = 0; i < LINE_LENGTH; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + index + i);
                     pgagroal_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgagroal_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgagroal_append_char(n, c);
                     }
                     else
                     {
                        n = pgagroal_append_char(n, '?');
                     }
                  }

                  t = pgagroal_append(t, l);
                  t = pgagroal_append_char(t, ' ');
                  t = pgagroal_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  count -= LINE_LENGTH;
                  index += LINE_LENGTH;
               }
            }
            else
            {
               size_t offset = 0;
               size_t remaining = size;
               bool full_line = false;

               while (remaining > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;
                  size_t count = MIN((int)remaining, (int)LINE_LENGTH);

                  for (size_t i = 0; i < count; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + offset + i);
                     pgagroal_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgagroal_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgagroal_append_char(n, c);
                     }
                     else
                     {
                        n = pgagroal_append_char(n, '?');
                     }
                  }

                  if (strlen(l) == LINE_LENGTH * 2)
                  {
                     full_line = true;
                  }
                  else if (full_line)
                  {
                     if (strlen(l) < LINE_LENGTH * 2)
                     {
                        int chars_missing = (LINE_LENGTH * 2) - strlen(l);
                        for (int i = 0; i < chars_missing; i++)
                        {
                           l = pgagroal_append_char(l, ' ');
                        }
                     }
                  }

                  t = pgagroal_append(t, l);
                  t = pgagroal_append_char(t, ' ');
                  t = pgagroal_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  remaining -= count;
                  offset += count;
               }
            }

            atomic_store(&config->common.log_lock, STATE_FREE);
         }
         else
         {
            SLEEP_AND_GOTO(1000000L, retry)
         }
      }
   }
}

static void*
dynamic_buffer_append(void* orig, size_t orig_size,
                      void* append, size_t append_size,
                      size_t* new_size)
{
   void* d = NULL;
   size_t s;

   if (append != NULL)
   {
      s = orig_size + append_size;
      d = realloc(orig, s);
      memcpy((char*)d + orig_size, append, append_size);
   }
   else
   {
      s = orig_size;
      d = orig;
   }

   *new_size = s;
   return d;
}

static void* partial_buf = NULL;
static size_t partial_buf_size = 0;

void
pgagroal_log_postgres(struct message* msg)
{
   size_t offset = 0;
   size_t remaining = 0;
   uint8_t* buf = NULL;
   void* new_buf = NULL;

   uint32_t msg_len = 0;
   uint8_t msg_type = ' ';

   if (!msg)
   {
      return;
   }

   partial_buf = dynamic_buffer_append(partial_buf, partial_buf_size, msg->data, msg->length, &partial_buf_size);

   buf = (uint8_t*)partial_buf;

   while (offset + 5 <= partial_buf_size)
   {
      memcpy(&msg_len, buf + offset + 1, 4);
      msg_len = ntohl(msg_len);

      if (msg_len < 4)
      {
         partial_buf_size = 0;
         break;
      }

      if (offset + 1 + msg_len > partial_buf_size)
      {
         break;
      }

      msg_type = buf[offset];
      pgagroal_log_trace("Message type: %c len: %u", msg_type, msg_len);

      offset += (1 + msg_len);
   }

   if (offset > 0)
   {
      remaining = partial_buf_size - offset;
      if (remaining > 0)
      {
         memmove(partial_buf, buf + offset, remaining);

         new_buf = realloc(partial_buf, remaining);
         if (new_buf)
         {
            partial_buf = new_buf;
         }
      }
      else
      {
         free(partial_buf);
         partial_buf = NULL;
      }

      partial_buf_size = remaining;
   }
}

static void
output_log_line(char* l)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGAGROAL_LOGGING_TYPE_CONSOLE)
   {
      fprintf(stdout, "%s", l);
      fprintf(stdout, "\n");
      fflush(stdout);
   }
   else if (config->common.log_type == PGAGROAL_LOGGING_TYPE_FILE)
   {
      fprintf(log_file, "%s", l);
      fprintf(log_file, "\n");
      fflush(log_file);
   }
}

bool
pgagroal_log_is_enabled(int level)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (level >= config->log_level)
   {
      return true;
   }

   return false;
}
