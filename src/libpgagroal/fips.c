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
#include <fips.h>
#include <logging.h>
#include <message.h>
#include <utils.h>

/* system */
#include <string.h>

/*
 * Build a simple-query ('Q') wire message into buf[0..size-1].
 * size must equal 6 + strlen(query).
 */
static void
build_query(char* buf, size_t size, char* query)
{
   memset(buf, 0, size);
   pgagroal_write_byte(buf, 'Q');
   pgagroal_write_int32(&buf[1], (int32_t)(size - 1));
   pgagroal_write_string(&buf[5], query);
}

/*
 * Return true if the block message contains a DataRow ('D') immediately
 * after the first RowDescription ('T') frame.
 */
static bool
has_datarow(struct message* msg)
{
   int t_len;

   if (msg->length < 5)
   {
      return false;
   }

   if (pgagroal_read_byte(msg->data) != 'T')
   {
      return false;
   }

   t_len = pgagroal_read_int32(msg->data + 1);

   if (1 + t_len >= msg->length)
   {
      return false;
   }

   return (pgagroal_read_byte(msg->data + 1 + t_len) == 'D');
}

/*
 * Read the first column byte of a DataRow that follows a RowDescription
 * in msg.  Assumes has_datarow(msg) is true.
 *
 * DataRow layout immediately after the 'T' frame (offset 1+t_len):
 *   'D'       [1 byte  – message type]
 *   row_len   [int32   – includes these 4 bytes]
 *   n_cols    [int16]
 *   col_len   [int32]
 *   col_data  [col_len bytes]
 *
 * Returns 0 if the column is NULL (col_len == -1).
 */
static char
read_first_column_byte(struct message* msg)
{
   int t_len = pgagroal_read_int32(msg->data + 1);
   int base = 1 + t_len; /* offset of the 'D' byte */
   int32_t col_len;

   /* base+0='D'  base+1..4=row_len  base+5..6=n_cols  base+7..10=col_len */
   col_len = pgagroal_read_int32(msg->data + base + 7);
   if (col_len <= 0)
   {
      return 0;
   }

   return pgagroal_read_byte(msg->data + base + 11);
}

/*
 * Send a simple query and read the response block into *tmsg_out.
 * Returns MESSAGE_STATUS_OK on success.
 */
static int
run_query(int socket, SSL* ssl, char* query, struct message** tmsg_out)
{
   int status;
   size_t size = 6 + strlen(query);
   char buf[size];
   struct message qmsg;

   build_query(buf, size, query);

   memset(&qmsg, 0, sizeof(struct message));
   qmsg.kind = 'Q';
   qmsg.length = size;
   qmsg.data = buf;

   status = pgagroal_write_message(ssl, socket, &qmsg);
   if (status != MESSAGE_STATUS_OK)
   {
      return status;
   }

   return pgagroal_read_block_message(ssl, socket, tmsg_out);
}

int
pgagroal_fips_check(int slot, int socket, SSL* ssl)
{
   int status;
   int server;
   struct message* tmsg = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   server = config->connections[slot].server;

   config->servers[server].fips_enabled = false;

   /* Supported from PostgreSQL 14 onwards. */
   if (config->servers[server].version < 14)
   {
      return 0;
   }

   if (config->servers[server].version >= 18)
   {
      /*
       * PostgreSQL 18+: pgcrypto exposes fips_mode() which returns the
       * actual runtime FIPS status as a boolean ('t' or 'f').
       *
       * The FROM clause restricts evaluation to rows where pgcrypto is
       * installed, so no DataRow is produced when pgcrypto is absent (an
       * error response is received instead).  Both the no-pgcrypto error
       * and the 'f' result map to fips_enabled = false.
       */
      status = run_query(socket, ssl,
                         "SELECT fips_mode() FROM pg_extension WHERE extname = 'pgcrypto';",
                         &tmsg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (has_datarow(tmsg))
      {
         config->servers[server].fips_enabled = (read_first_column_byte(tmsg) == 't');
      }
      /* else: error or empty result → fips_enabled stays false */
   }
   else
   {
      /*
       * PostgreSQL 14-17: fips_mode() is not available.
       *
       * Detection strategy:
       *   1. Confirm pgcrypto is installed.  Without pgcrypto we cannot
       *      probe FIPS status, so we leave fips_enabled = false.
       *   2. Attempt an MD5 digest via pgcrypto.  In FIPS mode MD5 is a
       *      prohibited algorithm and pgcrypto raises an error.  A
       *      successful DataRow means MD5 is allowed (FIPS off); an error
       *      response means MD5 was rejected (FIPS on).
       */

      /* Step 1: verify pgcrypto is installed. */
      status = run_query(socket, ssl,
                         "SELECT 1 FROM pg_extension WHERE extname = 'pgcrypto';",
                         &tmsg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (!has_datarow(tmsg))
      {
         /* pgcrypto absent — cannot determine FIPS status. */
         pgagroal_clear_message(tmsg);
         tmsg = NULL;
         goto done;
      }

      pgagroal_clear_message(tmsg);
      tmsg = NULL;

      /* Step 2: probe MD5 — a FIPS system rejects it. */
      status = run_query(socket, ssl,
                         "SELECT digest('test'::bytea, 'md5');",
                         &tmsg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      /*
       * DataRow → MD5 succeeded → FIPS not active.
       * Error/no DataRow → MD5 rejected → FIPS active.
       */
      config->servers[server].fips_enabled = !has_datarow(tmsg);
   }

done:
   pgagroal_log_debug("pgagroal_fips_check: %s version=%d fips_enabled=%s",
                      config->servers[server].name,
                      config->servers[server].version,
                      config->servers[server].fips_enabled ? "true" : "false");

   pgagroal_clear_message(tmsg);
   return 0;

error:
   pgagroal_log_trace("pgagroal_fips_check: slot (%d) status (%d)", slot, status);

   config->servers[server].fips_enabled = false;
   pgagroal_clear_message(tmsg);
   return 1;
}
