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
#include <utils.h>
#include <server.h>

/* system */
#include <ctype.h>
#include <err.h>
#include <ev.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#include <errno.h>
#include <inttypes.h>

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING 0x00000080U
#endif

extern char** environ;
#if defined(HAVE_LINUX) || defined(HAVE_OSX)
static bool env_changed = false;
static int max_process_title_size = 0;
#endif

int32_t
pgagroal_get_request(struct message* msg)
{
   if (msg == NULL || msg->data == NULL || msg->length < 8)
   {
      return -1;
   }

   return pgagroal_read_int32(msg->data + 4);
}

int
pgagroal_extract_username_database(struct message* msg, char** username, char** database, char** appname)
{
   int start, end;
   int counter = 0;
   signed char c;
   char** array = NULL;
   size_t size;
   char* un = NULL;
   char* db = NULL;
   char* an = NULL;

   *username = NULL;
   *database = NULL;
   *appname = NULL;

   /* We know where the parameters start, and we know that the message is zero terminated */
   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgagroal_read_byte(msg->data + i);
      if (c == 0)
      {
         counter++;
      }
   }

   array = (char**)malloc(sizeof(char*) * counter);

   counter = 0;
   start = 8;
   end = 8;

   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgagroal_read_byte(msg->data + i);
      end++;
      if (c == 0)
      {
         array[counter] = (char*)calloc(1, end - start);
         memcpy(array[counter], msg->data + start, end - start);

         start = end;
         counter++;
      }
   }

   for (int i = 0; i < counter; i++)
   {
      if (!strcmp(array[i], "user"))
      {
         size = strlen(array[i + 1]) + 1;
         un = calloc(1, size);
         memcpy(un, array[i + 1], size);

         *username = un;
      }
      else if (!strcmp(array[i], "database"))
      {
         size = strlen(array[i + 1]) + 1;
         db = calloc(1, size);
         memcpy(db, array[i + 1], size);

         *database = db;
      }
      else if (!strcmp(array[i], "application_name"))
      {
         size = strlen(array[i + 1]) + 1;
         an = calloc(1, size);
         memcpy(an, array[i + 1], size);

         *appname = an;
      }
   }

   if (*database == NULL)
   {
      *database = *username;
   }

   pgagroal_log_trace("Username: %s", *username);
   pgagroal_log_trace("Database: %s", *database);

   for (int i = 0; i < counter; i++)
   {
      free(array[i]);
   }
   free(array);

   return 0;
}

int
pgagroal_extract_message(char type, struct message* msg, struct message** extracted)
{
   int offset;
   int m_length;
   void* data = NULL;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < msg->length)
   {
      char t = (char)pgagroal_read_byte(msg->data + offset);

      if (type == t)
      {
         m_length = pgagroal_read_int32(msg->data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         data = (void*)malloc(1 + m_length);

         memcpy(data, msg->data + offset, 1 + m_length);

         result->kind = pgagroal_read_byte(data);
         result->length = 1 + m_length;
         result->data = data;

         *extracted = result;

         return 0;
      }
      else
      {
         offset += 1;
         offset += pgagroal_read_int32(msg->data + offset);
      }
   }

   return 1;
}

size_t
pgagroal_extract_message_offset(size_t offset, void* data, struct message** extracted)
{
   char type;
   int m_length;
   void* m_data;
   struct message* result = NULL;

   *extracted = NULL;

   type = (char)pgagroal_read_byte(data + offset);
   m_length = pgagroal_read_int32(data + offset + 1);

   result = (struct message*)malloc(sizeof(struct message));
   m_data = (void*)malloc(1 + m_length);

   memcpy(m_data, data + offset, 1 + m_length);

   result->kind = type;
   result->length = 1 + m_length;
   result->data = m_data;

   *extracted = result;

   return offset + 1 + m_length;
}

int
pgagroal_extract_error_message(struct message* msg, char** error)
{
   int max = 0;
   int offset = 5;
   signed char type;
   char* s = NULL;
   char* result = NULL;

   *error = NULL;

   if (msg->kind == 'E')
   {
      max = pgagroal_read_int32(msg->data + 1);

      while (result == NULL && offset < max)
      {
         type = pgagroal_read_byte(msg->data + offset);
         s = pgagroal_read_string(msg->data + offset + 1);

         if (type == 'M')
         {
            result = (char*)calloc(1, strlen(s) + 1);
            memcpy(result, s, strlen(s));

            *error = result;
         }

         offset += 1 + strlen(s) + 1;
      }
   }
   else
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

char*
pgagroal_connection_state_as_string(signed char state)
{
   char* buf;
   int buf_size = strlen("Unknown") + 1 + 4 + 1; // 'unknown' + <space> + <number> + \0

   switch (state)
   {
      case STATE_NOTINIT:
         return "Not initialized";
      case STATE_INIT:
         return "Initializing";
      case STATE_FREE:
         return "Free";
      case STATE_IN_USE:
         return "Active";
      case STATE_GRACEFULLY:
         return "Graceful";
      case STATE_FLUSH:
         return "Flush";
      case STATE_IDLE_CHECK:
         return "Idle check";
      case STATE_MAX_CONNECTION_AGE:
         return "Max connection age check";
      case STATE_VALIDATION:
         return "Validating";
      case STATE_REMOVE:
         return "Removing";
      default:
         buf = malloc(buf_size);
         memset(buf, 0, buf_size);
         snprintf(buf, buf_size, "Unknown %02d", state);
         return buf;
   }
}

signed char
pgagroal_read_byte(void* data)
{
   return (signed char)*((char*)data);
}

uint8_t
pgagroal_read_uint8(void* data)
{
   return (uint8_t)*((char*)data);
}

int16_t
pgagroal_read_int16(void* data)
{
   int16_t val;
   memcpy(&val, data, sizeof(val));
   return ntohs(val);
}

int32_t
pgagroal_read_int32(void* data)
{
   int32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

uint32_t
pgagroal_read_uint32(void* data)
{
   uint32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

long
pgagroal_read_long(void* data)
{
   if (pgagroal_bigendian())
   {
      // If the host is big-endian, read directly into a 'long'.
      long val;
      memcpy(&val, data, sizeof(val));
      return val;
   }
   else
   {
      // If little-endian, reassemble the bytes in big-endian order.
      unsigned char bytes[] = {
         *((unsigned char*)data + 0),
         *((unsigned char*)data + 1),
         *((unsigned char*)data + 2),
         *((unsigned char*)data + 3),
         *((unsigned char*)data + 4),
         *((unsigned char*)data + 5),
         *((unsigned char*)data + 6),
         *((unsigned char*)data + 7)};

      long res = (long)(((long)bytes[0]) << 56) |
                 (((long)bytes[1]) << 48) |
                 (((long)bytes[2]) << 40) |
                 (((long)bytes[3]) << 32) |
                 (((long)bytes[4]) << 24) |
                 (((long)bytes[5]) << 16) |
                 (((long)bytes[6]) << 8) |
                 (((long)bytes[7]));

      return res;
   }
}

char*
pgagroal_read_string(void* data)
{
   return (char*)data;
}

void
pgagroal_write_byte(void* data, signed char b)
{
   *((char*)(data)) = b;
}

void
pgagroal_write_uint8(void* data, uint8_t b)
{
   *((uint8_t*)(data)) = b;
}

void
pgagroal_write_int16(void* data, int16_t i)
{
   int16_t n = htons(i);
   memcpy(data, &n, sizeof(n));
}

void
pgagroal_write_int32(void* data, int32_t i)
{
   int32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgagroal_write_uint32(void* data, uint32_t i)
{
   uint32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgagroal_write_long(void* data, long l)
{
   if (pgagroal_bigendian())
   {
      // If the host is big-endian, write as-is.
      memcpy(data, &l, sizeof(l));
   }
   else
   {
      // If little-endian, reverse the bytes as before.
      char* ptr = (char*)&l;
      *((char*)(data + 7)) = *ptr;
      ptr++;
      *((char*)(data + 6)) = *ptr;
      ptr++;
      *((char*)(data + 5)) = *ptr;
      ptr++;
      *((char*)(data + 4)) = *ptr;
      ptr++;
      *((char*)(data + 3)) = *ptr;
      ptr++;
      *((char*)(data + 2)) = *ptr;
      ptr++;
      *((char*)(data + 1)) = *ptr;
      ptr++;
      *((char*)(data + 0)) = *ptr;
   }
}

void
pgagroal_write_string(void* data, char* s)
{
   memcpy(data, s, strlen(s));
}

bool
pgagroal_bigendian(void)
{
   short int word = 0x0001;
   char* b = (char*)&word;
   return (b[0] ? false : true);
}

unsigned int
pgagroal_swap(unsigned int i)
{
   return ((i << 24) & 0xff000000) |
          ((i << 8) & 0x00ff0000) |
          ((i >> 8) & 0x0000ff00) |
          ((i >> 24) & 0x000000ff);
}

char*
pgagroal_get_timestamp_string(time_t start_time, time_t end_time, int32_t* seconds)
{
   int32_t total_seconds;
   int hours;
   int minutes;
   int sec;
   char elapsed[128];
   char* result = NULL;

   *seconds = 0;

   total_seconds = (int32_t)difftime(end_time, start_time);

   *seconds = total_seconds;

   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   sec = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, sec);

   result = pgagroal_append(result, &elapsed[0]);

   return result;
}

char*
pgagroal_get_home_directory(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_dir;
}

char*
pgagroal_get_user_name(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_name;
}

char*
pgagroal_get_password(void)
{
   char p[MAX_PASSWORD_LENGTH];
   struct termios oldt, newt;
   int i = 0;
   int c;
   char* result = NULL;

   memset(&p, 0, sizeof(p));

   tcgetattr(STDIN_FILENO, &oldt);
   newt = oldt;

   newt.c_lflag &= ~(ECHO);

   tcsetattr(STDIN_FILENO, TCSANOW, &newt);

   while ((c = getchar()) != '\n' && c != EOF && i < MAX_PASSWORD_LENGTH)
   {
      p[i++] = c;
   }
   p[i] = '\0';

   tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

   result = calloc(1, strlen(p) + 1);

   memcpy(result, &p, strlen(p));

   return result;
}

bool
pgagroal_exists(const char* f)
{
   if (access(f, F_OK) == 0)
   {
      return true;
   }

   return false;
}

bool
pgagroal_is_file(const char* f)
{
   struct stat st = {0};

   if (f == NULL)
   {
      return false;
   }

   if (stat(f, &st) != 0)
   {
      return false;
   }

   return S_ISREG(st.st_mode);
}

int
pgagroal_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;
   char* r = NULL;

   *encoded = NULL;
   *encoded_length = 0;

   if (raw == NULL)
   {
      goto error;
   }

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
   BIO_write(b64_bio, raw, raw_length);
   BIO_flush(b64_bio);

   BIO_get_mem_ptr(mem_bio, &mem_bio_mem_ptr);

   BIO_set_close(mem_bio, BIO_NOCLOSE);
   BIO_free_all(b64_bio);

   BUF_MEM_grow(mem_bio_mem_ptr, (*mem_bio_mem_ptr).length + 1);
   (*mem_bio_mem_ptr).data[(*mem_bio_mem_ptr).length] = '\0';

   r = calloc(1, strlen((*mem_bio_mem_ptr).data) + 1);
   memcpy(r, (*mem_bio_mem_ptr).data, strlen((*mem_bio_mem_ptr).data));

   BUF_MEM_free(mem_bio_mem_ptr);

   *encoded = r;
   *encoded_length = strlen(r);

   return 0;

error:

   *encoded = NULL;

   return 1;
}

int
pgagroal_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   size_t size;
   char* decoded;
   int index;

   *raw = NULL;
   *raw_length = 0;

   if (encoded == NULL)
   {
      goto error;
   }

   size = (encoded_length * 3) / 4 + 1;
   decoded = calloc(1, size);

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_write(mem_bio, encoded, encoded_length);
   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

   index = 0;
   while (0 < BIO_read(b64_bio, decoded + index, 1))
   {
      index++;
   }

   BIO_free_all(b64_bio);

   *raw = decoded;
   *raw_length = index;

   return 0;

error:

   *raw = NULL;
   *raw_length = 0;

   return 1;
}

void
pgagroal_set_proc_title(int argc, char** argv, char* s1, char* s2)
{
#if defined(HAVE_LINUX) || defined(HAVE_OSX)
   char title[MAX_PROCESS_TITLE_LENGTH];
   size_t size;
   char** env = environ;
   int es = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   // sanity check: if the user does not want to
   // update the process title, do nothing
   if (config->update_process_title == UPDATE_PROCESS_TITLE_NEVER)
   {
      return;
   }

   if (!env_changed)
   {
      for (int i = 0; env[i] != NULL; i++)
      {
         es++;
      }

      environ = (char**)malloc(sizeof(char*) * (es + 1));
      if (environ == NULL)
      {
         return;
      }

      for (int i = 0; env[i] != NULL; i++)
      {
         size = strlen(env[i]);
         environ[i] = (char*)calloc(1, size + 1);

         if (environ[i] == NULL)
         {
            return;
         }
         memcpy(environ[i], env[i], size);
      }
      environ[es] = NULL;
      env_changed = true;
   }

   // compute how long was the command line
   // when the application was started
   if (max_process_title_size == 0)
   {
      for (int i = 0; i < argc; i++)
      {
         max_process_title_size += strlen(argv[i]) + 1;
      }
   }

   // compose the new title
   memset(&title, 0, sizeof(title));
   snprintf(title, sizeof(title) - 1, "pgagroal: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");

   // nuke the command line info
   memset(*argv, 0, max_process_title_size);

   // copy the new title over argv checking
   // the update_process_title policy
   if (config->update_process_title == UPDATE_PROCESS_TITLE_STRICT)
   {
      size = max_process_title_size;
   }
   else
   {
      // here we can set the title to a full description
      size = strlen(title) + 1;
   }

   memcpy(*argv, title, size);
   memset(*argv + size, 0, 1);

   // keep track of how long is now the title
   max_process_title_size = size;

#else
   setproctitle("-pgagroal: %s%s%s",
                s1 != NULL ? s1 : "",
                s1 != NULL && s2 != NULL ? "/" : "",
                s2 != NULL ? s2 : "");

#endif
}

void
pgagroal_set_connection_proc_title(int argc, char** argv, struct connection* connection)
{
   struct main_configuration* config;
   int primary;
   char* info = NULL;

   config = (struct main_configuration*)shmem;

   if (pgagroal_get_primary(&primary))
   {
      // cannot find the primary, this is a problem!
      pgagroal_set_proc_title(argc, argv, connection->username, connection->database);
      return;
   }

   info = pgagroal_append(info, connection->username);
   info = pgagroal_append(info, "@");
   info = pgagroal_append(info, config->servers[primary].host);
   info = pgagroal_append(info, ":");
   info = pgagroal_append_int(info, config->servers[primary].port);

   pgagroal_set_proc_title(argc, argv, info, connection->database);
   free(info);
}

unsigned int
pgagroal_version_as_number(unsigned int major, unsigned int minor, unsigned int patch)
{
   return (patch % 100) + (minor % 100) * 100 + (major % 100) * 10000;
}

unsigned int
pgagroal_version_number(void)
{
   return pgagroal_version_as_number(PGAGROAL_MAJOR_VERSION,
                                     PGAGROAL_MINOR_VERSION,
                                     PGAGROAL_PATCH_VERSION);
}

bool
pgagroal_version_ge(unsigned int major, unsigned int minor, unsigned int patch)
{
   if (pgagroal_version_number() >= pgagroal_version_as_number(major, minor, patch))
   {
      return true;
   }
   else
   {
      return false;
   }
}

bool
pgagroal_starts_with(char* str, char* prefix)
{
   if (str == NULL)
   {
      return false;
   }
   return strncmp(prefix, str, strlen(prefix)) == 0;
}

bool
pgagroal_ends_with(char* str, char* suffix)
{
   int str_len = strlen(str);
   int suffix_len = strlen(suffix);

   return (str_len >= suffix_len) && (strcmp(str + (str_len - suffix_len), suffix) == 0);
}

char*
pgagroal_append(char* orig, char* s)
{
   size_t orig_length;
   size_t s_length;
   char* n = NULL;

   if (s == NULL)
   {
      return orig;
   }

   if (orig != NULL)
   {
      orig_length = strlen(orig);
   }
   else
   {
      orig_length = 0;
   }

   s_length = strlen(s);

   n = (char*)realloc(orig, orig_length + s_length + 1);

   memcpy(n + orig_length, s, s_length);

   n[orig_length + s_length] = '\0';

   return n;
}

char*
pgagroal_format_and_append(char* buf, char* format, ...)
{
   va_list args;
   va_start(args, format);

   // Determine the required buffer size
   int size_needed = vsnprintf(NULL, 0, format, args) + 1;
   va_end(args);

   // Allocate buffer to hold the formatted string
   char* formatted_str = malloc(size_needed);

   va_start(args, format);
   vsnprintf(formatted_str, size_needed, format, args);
   va_end(args);

   buf = pgagroal_append(buf, formatted_str);

   free(formatted_str);

   return buf;
}

char*
pgagroal_append_int(char* orig, int i)
{
   char number[12];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 11, "%d", i);
   orig = pgagroal_append(orig, number);

   return orig;
}

char*
pgagroal_append_ulong(char* orig, unsigned long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%lu", l);
   orig = pgagroal_append(orig, number);

   return orig;
}

char*
pgagroal_append_ullong(char* orig, unsigned long long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%llu", l);
   orig = pgagroal_append(orig, number);

   return orig;
}

__attribute__((unused)) static bool
calculate_offset(uint64_t addr, uint64_t* offset, char** filepath)
{
#if defined(HAVE_LINUX) && defined(HAVE_EXECINFO_H)
   char line[256];
   char *start, *end, *base_offset, *filepath_ptr;
   uint64_t start_addr, end_addr, base_offset_value;
   FILE* fp;
   bool success = false;

   fp = fopen("/proc/self/maps", "r");
   if (fp == NULL)
   {
      goto error;
   }

   while (fgets(line, sizeof(line), fp) != NULL)
   {
      // exmaple line:
      // 7fb60d1ea000-7fb60d20c000 r--p 00000000 103:02 120327460 /usr/lib/libc.so.6
      start = strtok(line, "-");
      end = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      base_offset = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      strtok(NULL, " "); // skip the next token
      filepath_ptr = strtok(NULL, " \n");
      if (start != NULL && end != NULL && base_offset != NULL && filepath_ptr != NULL)
      {
         start_addr = strtoul(start, NULL, 16);
         end_addr = strtoul(end, NULL, 16);
         if (addr >= start_addr && addr < end_addr)
         {
            success = true;
            break;
         }
      }
   }
   if (!success)
   {
      goto error;
   }

   base_offset_value = strtoul(base_offset, NULL, 16);
   *offset = addr - start_addr + base_offset_value;
   *filepath = pgagroal_append(*filepath, filepath_ptr);
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 0;

error:
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 1;

#else
   return 1;

#endif
}

int
pgagroal_backtrace(void)
{
#if defined(HAVE_LINUX) && defined(HAVE_EXECINFO_H)
   void* bt[1024];
   char* log_str = NULL;
   size_t bt_size;

   bt_size = backtrace(bt, 1024);
   if (bt_size == 0)
   {
      goto error;
   }

   log_str = pgagroal_append(log_str, "Backtrace:\n");

   // the first element is ___interceptor_backtrace, so we skip it
   for (int i = 1; i < bt_size; i++)
   {
      uint64_t addr = (uint64_t)bt[i];
      uint64_t offset;
      char* filepath = NULL;
      char cmd[256], buffer[256], log_buffer[64];
      bool found_main = false;
      FILE* pipe;

      if (calculate_offset(addr, &offset, &filepath))
      {
         continue;
      }

      snprintf(cmd, sizeof(cmd), "addr2line -e %s -fC 0x%" PRIx64, filepath, offset);
      free(filepath);
      filepath = NULL;

      pipe = popen(cmd, "r");
      if (pipe == NULL)
      {
         pgagroal_log_debug("Failed to run command: %s, reason: %s", cmd, strerror(errno));
         continue;
      }

      if (fgets(buffer, sizeof(buffer), pipe) == NULL)
      {
         pgagroal_log_debug("Failed to read from command output: %s", strerror(errno));
         pclose(pipe);
         continue;
      }
      buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
      if (strcmp(buffer, "main") == 0)
      {
         found_main = true;
      }
      snprintf(log_buffer, sizeof(log_buffer), "#%d  0x%" PRIx64 " in ", i - 1, addr);
      log_str = pgagroal_append(log_str, log_buffer);
      log_str = pgagroal_append(log_str, buffer);
      log_str = pgagroal_append(log_str, "\n");

      if (fgets(buffer, sizeof(buffer), pipe) == NULL)
      {
         log_str = pgagroal_append(log_str, "\tat ???:??\n");
      }
      else
      {
         buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
         log_str = pgagroal_append(log_str, "\tat ");
         log_str = pgagroal_append(log_str, buffer);
         log_str = pgagroal_append(log_str, "\n");
      }

      pclose(pipe);
      if (found_main)
      {
         break;
      }
   }

   pgagroal_log_debug("%s", log_str);
   free(log_str);
   return 0;

error:
   if (log_str != NULL)
   {
      free(log_str);
   }
   return 1;
#else
   return 1;
#endif
}

/* Parser for pgagroal-cli commands */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgagroal_parsed_command* parsed,
              const struct pgagroal_command command_table[],
              size_t command_count)
{
#define EMPTY_STR(_s) (_s[0] == 0)

   char* command = NULL;
   char* subcommand = NULL;
   bool command_match = false;
   int default_command_match = -1;
   int arg_count = -1;
   int command_index = -1;

   /* Parse command, and exit if there is no match */
   if (offset < argc)
   {
      command = argv[offset++];
   }
   else
   {
      warnx("A command is required\n");
      return false;
   }

   if (offset < argc)
   {
      subcommand = argv[offset];
   }

   for (size_t i = 0; i < command_count; i++)
   {
      if (strncmp(command, command_table[i].command, MISC_LENGTH) == 0)
      {
         command_match = true;
         if (subcommand && strncmp(subcommand, command_table[i].subcommand, MISC_LENGTH) == 0)
         {
            offset++;
            command_index = i;
            break;
         }
         else if (EMPTY_STR(command_table[i].subcommand))
         {
            /* Default command does not require a subcommand, might be followed by an argument */
            default_command_match = i;
         }
      }
   }

   if (command_match == false)
   {
      warnx("Unknown command '%s'\n", command);
      return false;
   }

   if (command_index == -1 && default_command_match >= 0)
   {
      command_index = default_command_match;
      subcommand = "";
   }
   else if (command_index == -1) /* Command was matched, but subcommand was not */
   {
      if (subcommand)
      {
         warnx("Unknown subcommand '%s' for command '%s'\n", subcommand, command);
      }
      else /* User did not type a subcommand */
      {
         warnx("Command '%s' requires a subcommand\n", command);
      }
      return false;
   }

   parsed->cmd = &command_table[command_index];

   /* Iterate until find an accepted_arg_count that is equal or greater than the typed command arg_count */
   arg_count = argc - offset;
   int j;
   for (j = 0; j < MISC_LENGTH; j++)
   {
      if (parsed->cmd->accepted_argument_count[j] >= arg_count)
      {
         break;
      }
   }
   if (arg_count < parsed->cmd->accepted_argument_count[0])
   {
      warnx("Too few arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }
   if (j == MISC_LENGTH || arg_count > parsed->cmd->accepted_argument_count[j])
   {
      warnx("Too many arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }

   /* Copy argv + offset pointers into parsed->args */
   for (int i = 0; i < arg_count; i++)
   {
      parsed->args[i] = argv[i + offset];
   }
   parsed->args[0] = parsed->args[0] ? parsed->args[0] : (char*)parsed->cmd->default_argument;

   /* Warn the user if there is enough information about deprecation */
   if (parsed->cmd->deprecated && pgagroal_version_ge(parsed->cmd->deprecated_since_major,
                                                      parsed->cmd->deprecated_since_minor, 0))
   {
      warnx("command <%s> has been deprecated by <%s> since version %d.%d",
            parsed->cmd->command,
            parsed->cmd->deprecated_by,
            parsed->cmd->deprecated_since_major,
            parsed->cmd->deprecated_since_minor);
   }

   return true;

#undef EMPTY_STR
}

char*
pgagroal_server_state_as_string(signed char state)
{
   char* buf;

   switch (state)
   {
      case SERVER_NOTINIT:
         return "Not init";
      case SERVER_NOTINIT_PRIMARY:
         return "Not init (primary)";
      case SERVER_PRIMARY:
         return "Primary";
      case SERVER_REPLICA:
         return "Replica";
      case SERVER_FAILOVER:
         return "Failover";
      case SERVER_FAILED:
         return "Failed";
      default:
         buf = malloc(5);
         memset(buf, 0, 5);
         snprintf(buf, 5, "%d", state);
         return buf;
   }
}

char*
pgagroal_append_char(char* orig, char c)
{
   char str[2];

   memset(&str[0], 0, sizeof(str));
   snprintf(&str[0], 2, "%c", c);
   orig = pgagroal_append(orig, str);

   return orig;
}

char*
pgagroal_indent(char* str, char* tag, int indent)
{
   for (int i = 0; i < indent; i++)
   {
      str = pgagroal_append(str, " ");
   }
   if (tag != NULL)
   {
      str = pgagroal_append(str, tag);
   }
   return str;
}

bool
pgagroal_compare_string(const char* str1, const char* str2)
{
   if (str1 == NULL && str2 == NULL)
   {
      return true;
   }
   if ((str1 == NULL && str2 != NULL) || (str1 != NULL && str2 == NULL))
   {
      return false;
   }
   return strcmp(str1, str2) == 0;
}

char*
pgagroal_escape_string(char* str)
{
   if (str == NULL)
   {
      return NULL;
   }

   char* translated_ec_string = NULL;
   int len = 0;
   int idx = 0;
   size_t translated_len = 0;

   len = strlen(str);
   for (int i = 0; i < len; i++)
   {
      if (str[i] == '\"' || str[i] == '\\' || str[i] == '\n' || str[i] == '\t' || str[i] == '\r')
      {
         translated_len++;
      }
      translated_len++;
   }
   translated_ec_string = (char*)malloc(translated_len + 1);

   for (int i = 0; i < len; i++, idx++)
   {
      switch (str[i])
      {
         case '\\':
         case '\"':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = str[i];
            break;
         case '\n':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'n';
            break;
         case '\t':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 't';
            break;
         case '\r':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'r';
            break;
         default:
            translated_ec_string[idx] = str[i];
            break;
      }
   }
   translated_ec_string[idx] = '\0'; // terminator

   return translated_ec_string;
}

int
pgagroal_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch)
{
   bool bsd = false;
   *os = NULL;
   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;

#if defined(HAVE_LINUX) || defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD) || defined(HAVE_OSX)
   struct utsname buffer;

   if (uname(&buffer) != 0)
   {
      pgagroal_log_debug("Failed to retrieve system information.");
      goto error;
   }

   // Copy system name using pgagroal_append (dynamically allocated)
   *os = pgagroal_append(NULL, buffer.sysname);
   if (*os == NULL)
   {
      pgagroal_log_debug("Failed to allocate memory for OS name.");
      goto error;
   }

   // Parse kernel version based on OS
#if defined(HAVE_LINUX)
   if (sscanf(buffer.release, "%d.%d.%d", kernel_major, kernel_minor, kernel_patch) < 2)
   {
      pgagroal_log_debug("Failed to parse Linux kernel version.");
      goto error;
   }
#elif defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD)
   if (sscanf(buffer.release, "%d.%d", kernel_major, kernel_minor) < 2)
   {
      pgagroal_log_debug("Failed to parse BSD OS kernel version.");
      goto error;
   }
   *kernel_patch = 0; // BSD doesn't use patch version
   bsd = true;
#elif defined(HAVE_OSX)
   if (sscanf(buffer.release, "%d.%d.%d", kernel_major, kernel_minor, kernel_patch) < 2)
   {
      pgagroal_log_debug("Failed to parse macOS kernel version.");
      goto error;
   }
#endif

   if (!bsd)
   {
      pgagroal_log_debug("OS: %s | Kernel Version: %d.%d.%d", *os, *kernel_major, *kernel_minor, *kernel_patch);
   }
   else
   {
      pgagroal_log_debug("OS: %s | Version: %d.%d", *os, *kernel_major, *kernel_minor);
   }

   return 0;

error:
   //Free memory if already allocated
   if (*os != NULL)
   {
      free(*os);
      *os = NULL;
   }

   *os = pgagroal_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgagroal_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgagroal_log_debug("Unable to retrieve OS and kernel version.");

   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;
   return 1;

#else
   *os = pgagroal_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgagroal_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgagroal_log_debug("Kernel version not available.");
   return 1;
#endif
}

int
pgagroal_resolve_path(char* orig_path, char** new_path)
{
#if defined(HAVE_DARWIN) || defined(HAVE_OSX)
#define GET_ENV(name) getenv(name)
#else
#define GET_ENV(name) secure_getenv(name)
#endif

   char* res = NULL;
   char* env_res = NULL;
   int len = strlen(orig_path);
   int res_len = 0;
   bool double_quote = false;
   bool single_quote = false;
   bool in_env = false;

   *new_path = NULL;

   if (orig_path == NULL)
   {
      goto error;
   }

   for (int idx = 0; idx < len; idx++)
   {
      char* ch = NULL;

      bool valid_env_char = orig_path[idx] == '_' || (orig_path[idx] >= 'A' && orig_path[idx] <= 'Z') || (orig_path[idx] >= 'a' && orig_path[idx] <= 'z') || (orig_path[idx] >= '0' && orig_path[idx] <= '9');
      if (in_env && !valid_env_char)
      {
         in_env = false;
         if (env_res == NULL)
         {
            return 1;
         }
         char* env_value = GET_ENV(env_res);
         free(env_res);
         if (env_value == NULL)
         {
            return 1;
         }
         res = pgagroal_append(res, env_value);
         res_len += strlen(env_value);
         env_res = NULL;
      }

      if (orig_path[idx] == '\"' && !single_quote)
      {
         double_quote = !double_quote;
         continue;
      }
      else if (orig_path[idx] == '\'' && !double_quote)
      {
         single_quote = !single_quote;
         continue;
      }

      if (orig_path[idx] == '\\')
      {
         if (idx + 1 < len)
         {
            ch = pgagroal_append_char(ch, orig_path[idx + 1]);
            idx++;
         }
         else
         {
            return 1;
         }
      }
      else if (orig_path[idx] == '$')
      {
         if (single_quote)
         {
            ch = pgagroal_append_char(ch, '$');
         }
         else
         {
            in_env = true;
         }
      }
      else
      {
         ch = pgagroal_append_char(ch, orig_path[idx]);
      }

      if (in_env)
      {
         env_res = pgagroal_append(env_res, ch);
      }
      else
      {
         res = pgagroal_append(res, ch);
         ++res_len;
      }

      free(ch);
   }

   if (res_len > MAX_PATH)
   {
      goto error;
   }
   *new_path = res;
   return 0;

error:
   return 1;
}

char*
pgagroal_remove_all_whitespace(char* orig)
{
   size_t length;
   char c = 0;
   char* result = NULL;

   if (orig == NULL || strlen(orig) == 0)
   {
      return orig;
   }

   length = strlen(orig);

   for (size_t i = 0; i < length; i++)
   {
      c = *(orig + i);
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      {
         /* Skip whitespace */
      }
      else
      {
         result = pgagroal_append_char(result, c);
      }
   }

   return result;
}

int
pgagroal_normalize_path(char* directory_path, char* filename, char* default_path, char* path_buffer, size_t buffer_size)
{
   char* temp_path = NULL;

   if (path_buffer == NULL || buffer_size == 0 || filename == NULL)
   {
      return 1; // Invalid input
   }

   memset(path_buffer, 0, buffer_size);

   // If directory_path is provided, try to find the file there first
   if (directory_path != NULL)
   {
      temp_path = pgagroal_append(NULL, directory_path);
      if (temp_path == NULL)
      {
         return 1;
      }

      // Add "/" if needed
      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         temp_path = pgagroal_append(temp_path, "/");
         if (temp_path == NULL)
         {
            return 1;
         }
      }

      // Add filename
      temp_path = pgagroal_append(temp_path, filename);
      if (temp_path == NULL)
      {
         return 1;
      }

      // Check if the result fits in our buffer
      if (strlen(temp_path) >= buffer_size)
      {
         pgagroal_log_error("Configuration directory path is too long: %s (maximum %zu characters)",
                            temp_path, buffer_size - 1);
         free(temp_path);
         return 1;
      }

      // Check if file exists in the specified directory
      if (access(temp_path, F_OK) == 0)
      {
         pgagroal_log_debug("Using config file: %s", temp_path);
         strcpy(path_buffer, temp_path);
         free(temp_path);
         return 0;
      }
      else
      {
         pgagroal_log_info("Config file %s not found in directory %s", filename, directory_path);
         free(temp_path);
      }
   }

   // Now check if default file exists
   if (default_path != NULL)
   {
      if (access(default_path, F_OK) == 0)
      {
         if (strlen(default_path) >= buffer_size)
         {
            pgagroal_log_error("Default configuration path is too long: %s (maximum %zu characters)",
                               default_path, buffer_size - 1);
            return 1;
         }
         pgagroal_log_info("Using default config file: %s", default_path);
         strcpy(path_buffer, default_path);
         return 0; // Default file exists and is being used
      }
      else
      {
         pgagroal_log_info("Default config file %s not found, continuing without %s", default_path, filename);
         return 0; // Default doesn't exist, but that's okay for optional files
      }
   }

   // No directory specified and no default path provided
   pgagroal_log_warn("No path specified for config file %s", filename);
   return 1;
}

static void
append_bounded(char** out, const char* s, size_t current_len, size_t cap)
{
   if (s == NULL || cap <= current_len)
   {
      return;
   }

   size_t remain = cap - current_len;
   size_t slen = strlen(s);
   size_t to_copy = (slen > remain) ? remain : slen;

   if (to_copy == 0)
   {
      return;
   }

   char* chunk = (char*)malloc(to_copy + 1);
   if (chunk == NULL)
   {
      return;
   }

   memcpy(chunk, s, to_copy);
   chunk[to_copy] = '\0';
   *out = pgagroal_append(*out, chunk);
   free(chunk);
}

static void
append_char_bounded(char** out, char c, size_t current_len, size_t cap)
{
   if (current_len < cap)
   {
      *out = pgagroal_append_char(*out, c);
   }
}

static int
hvsnprintf(char* buf, size_t n, const char* fmt, va_list ap)
{
   size_t cap = 8192;
   if (n > 0 && (n - 1) < cap)
   {
      cap = n - 1;
   }

   char* out = NULL;
   const char* p = (fmt != NULL) ? fmt : "";
   char scratch[128];

   while (*p != '\0')
   {
      if (*p != '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, *p, cur, cap);
         p++;
         continue;
      }

      p++;
      if (*p == '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, '%', cur, cap);
         p++;
         continue;
      }

      /* Parse flags (support '0' for zero-padding) */
      bool flag_zero = false;
      while (*p == '0')
      {
         flag_zero = true;
         p++;
      }

      /* Parse width */
      int width = -1;
      if (isdigit((unsigned char)*p))
      {
         width = 0;
         while (isdigit((unsigned char)*p))
         {
            width = width * 10 + (*p - '0');
            p++;
         }
      }

      /* Parse precision */
      int precision = -1;
      if (*p == '.')
      {
         p++;
         if (*p == '*')
         {
            precision = va_arg(ap, int);
            p++;
         }
         else
         {
            precision = 0;
            while (isdigit((unsigned char)*p))
            {
               precision = precision * 10 + (*p - '0');
               p++;
            }
         }
         if (precision < 0)
         {
            precision = -1;
         }
      }

      /* Length modifier */
      enum { LM_NONE,
             LM_L,
             LM_LL,
             LM_Z } lm = LM_NONE;
      if (*p == 'l')
      {
         p++;
         if (*p == 'l')
         {
            lm = LM_LL;
            p++;
         }
         else
         {
            lm = LM_L;
         }
      }
      else if (*p == 'z')
      {
         lm = LM_Z;
         p++;
      }

      char conv = *p;
      if (conv == '\0')
      {
         break;
      }
      p++;

      scratch[0] = '\0';

      switch (conv)
      {
         case 's':
         {
            char* s = va_arg(ap, char*);
            if (s == NULL)
            {
               s = "(null)";
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, s, cur, cap);
            break;
         }
         case 'c':
         {
            int ch = va_arg(ap, int);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, (char)ch, cur, cap);
            break;
         }
         case 'd':
         case 'i':
         {
            long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, long);
            }
            else if (lm == LM_Z)
            {
               v = (ssize_t)va_arg(ap, ssize_t);
            }
            else
            {
               v = va_arg(ap, int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*lld" : "%*lld", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%lld", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'u':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*llu" : "%*llu", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%llu", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'x':
         case 'X':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               if (conv == 'x')
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llx" : "%*llx", width, v);
               }
               else
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llX" : "%*llX", width, v);
               }
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch),
                              (conv == 'x') ? "%llx" : "%llX", v);
            }

            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'p':
         {
            void* ptr = va_arg(ap, void*);
            (void)snprintf(scratch, sizeof(scratch), "%p", ptr);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'f':
         case 'F':
         case 'g':
         case 'G':
         case 'e':
         case 'E':
         {
            double dv = va_arg(ap, double);
            (void)snprintf(scratch, sizeof(scratch), "%g", dv);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         default:
         {
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, '%', cur, cap);
            cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, conv, cur, cap);
            break;
         }
      }
   }

   size_t produced_len = (out != NULL) ? strlen(out) : 0;

   if (buf != NULL && n > 0)
   {
      size_t to_copy = (produced_len < (n - 1)) ? produced_len : (n - 1);
      if (to_copy > 0 && out != NULL)
      {
         memcpy(buf, out, to_copy);
      }
      if (n > 0)
      {
         buf[to_copy] = '\0';
      }
   }

   if (out != NULL)
   {
      free(out);
   }

   return (int)produced_len;
}

int
pgagroal_snprintf(char* buf, size_t n, const char* fmt, ...)
{
   va_list ap;
   va_list ap_copy;
   int ret;

   va_start(ap, fmt);
   va_copy(ap_copy, ap);
   ret = vsnprintf(buf, n, fmt, ap_copy);
   va_end(ap_copy);
   if (ret < 0)
   {
      ret = hvsnprintf(buf, n, fmt, ap);
   }
   va_end(ap);

   return ret;
}
