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

#include <pgagroal.h>
#include <message.h>
#include <queries.h>
#include <utils.h>
#include <string.h>

const char*
pgagroal_queries_system_identifier(void)
{
   return "SELECT system_identifier, pg_control_version FROM pg_control_system()";
}

const char*
pgagroal_queries_is_in_recovery(void)
{
   return "SELECT pg_is_in_recovery();";
}

const char*
pgagroal_queries_replication_lag_bytes(void)
{
   return "SELECT COALESCE(pg_wal_lsn_diff(pg_last_wal_receive_lsn(), pg_last_wal_replay_lsn()), 0)::bigint;";
}

int
pgagroal_read_query_first_column_text(int fd, char* value, size_t value_size)
{
   int status;
   int offset = 0;
   bool has_value = false;
   struct message* msg = NULL;

   if (value == NULL || value_size == 0)
   {
      return 1;
   }

   value[0] = '\0';

   while (true)
   {
      status = pgagroal_read_timeout_message(NULL, fd, 5, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
      {
         goto error;
      }

      offset = 0;
      while (offset < msg->length)
      {
         char kind = pgagroal_read_byte(msg->data + offset);
         int len = pgagroal_read_int32(msg->data + offset + 1);

         if (len <= 0)
         {
            goto error;
         }

         if (kind == 'D')
         {
            int dr_offset = offset + 5;
            int num_cols = pgagroal_read_int16(msg->data + dr_offset);
            dr_offset += 2;

            if (num_cols >= 1)
            {
               int col_len = pgagroal_read_int32(msg->data + dr_offset);
               dr_offset += 4;

               if (col_len == -1)
               {
                  value[0] = '\0';
                  has_value = true;
               }
               else if (col_len == 0)
               {
                  value[0] = '\0';
                  has_value = true;
               }
               else if (col_len > 0)
               {
                  if ((size_t)col_len >= value_size)
                  {
                     goto error;
                  }

                  memcpy(value, msg->data + dr_offset, (size_t)col_len);
                  value[col_len] = '\0';
                  has_value = true;
               }
            }
         }
         else if (kind == 'E')
         {
            goto error;
         }
         else if (kind == 'Z')
         {
            pgagroal_clear_message(msg);
            return has_value ? 0 : 1;
         }

         offset += 1 + len;
      }

      pgagroal_clear_message(msg);
      msg = NULL;
   }

error:
   pgagroal_clear_message(msg);
   return 1;
}
