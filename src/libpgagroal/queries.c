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
#include <logging.h>
#include <message.h>
#include <pgagroal.h>
#include <queries.h>
#include <utils.h>

/* system */
#include <string.h>

const char*
pgagroal_queries_system_identifier(void)
{
   return "SELECT system_identifier, current_setting( 'server_version_num' ) FROM pg_control_system()";
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

char*
pgagroal_queries_wal_receiver_status(void)
{
   return "SELECT status, slot_name, sender_host, sender_port FROM pg_stat_wal_receiver;";
}

/* Note: Requires pg_monitor privilege to read from pg_stat_replication */

char*
pgagroal_queries_replication_slots_status(void)
{
   return "SELECT s.slot_name, r.client_addr FROM pg_stat_replication r LEFT JOIN pg_replication_slots s ON r.pid = s.active_pid;";
}

int
pgagroal_read_query_multiple_rows_text(int fd, int expected_cols, int max_rows,
                                       char** values, size_t* value_sizes, int* num_rows)
{
   int status;
   int offset = 0;
   int row = 0;
   struct message* msg = NULL;

   if (values == NULL || value_sizes == NULL || num_rows == NULL || expected_cols <= 0 || max_rows <= 0)
   {
      return 1;
   }

   for (int cell_idx = 0; cell_idx < max_rows * expected_cols; cell_idx++)
   {
      if (values[cell_idx] != NULL && value_sizes[cell_idx] > 0)
      {
         values[cell_idx][0] = '\0';
      }
      else
      {
         return 1;
      }
   }

   *num_rows = 0;

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
            goto error;

         if (kind == 'D')
         {
            int dr_offset = offset + 5;
            int num_cols = pgagroal_read_int16(msg->data + dr_offset);
            dr_offset += 2;

            if (num_cols != expected_cols)
            {
               goto error;
            }

            if (row < max_rows)
            {
               for (int i = 0; i < num_cols; i++)
               {
                  int col_len = pgagroal_read_int32(msg->data + dr_offset);
                  char* dest = values[row * expected_cols + i];
                  size_t dest_size = value_sizes[row * expected_cols + i];

                  dr_offset += 4;

                  if (col_len <= 0)
                  {
                     dest[0] = '\0';
                  }
                  else if ((size_t)col_len < dest_size)
                  {
                     memset(dest, 0, (size_t)col_len + 1);
                     memcpy(dest, msg->data + dr_offset, (size_t)col_len);
                     dr_offset += col_len;
                  }
                  else
                  {
                     goto error;
                  }
               }
               row++;
            }
            else
            {
               pgagroal_log_debug("pgagroal_read_query_multiple_rows_text: maximum number of rows (%d) reached, ignoring additional rows", max_rows);
            }
         }
         else if (kind == 'E')
         {
            goto error;
         }
         else if (kind == 'Z')
         {
            pgagroal_clear_message(msg);
            *num_rows = row;
            return 0;
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

int
pgagroal_read_query_multiple_columns_text(int fd, int expected_cols, char** values, size_t* value_sizes)
{
   int num_rows = 0;

   if (pgagroal_read_query_multiple_rows_text(fd, expected_cols, 1, values, value_sizes, &num_rows))
   {
      return 1;
   }

   return num_rows > 0 ? 0 : 1;
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
