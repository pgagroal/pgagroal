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
 *
 */
#include <pgagroal.h>
#include <message.h>
#include <utils.h>
#include <mctf.h>

#include <string.h>

/*
 * Regression tests for the message walker used by the transaction and session
 * pipelines. A message header (kind byte + int32 length) split across reads
 * previously desynchronized the parser: the length was read past the end of
 * the received bytes, the walker skipped a bogus number of payload bytes, and
 * a 'Z' inside a payload (e.g. the data of "select repeat('Z',4096)") could
 * be misparsed as a ReadyForQuery. The walker must buffer a split header in
 * its state until it is complete, and must keep the I/O-stop decision of the
 * pipelines (a 'Z' at the end of a chunk) working across multiple reads.
 */

#define D_PAYLOAD_SIZE 4096

/* Collection of messages reported by the walker callback */
struct walk_result
{
   int count;
   char kinds[64];
   int z_count;
   char z_state;
   int last_msglen;
};

/* Callback invoked by pgagroal_parse_message for each complete header */
static void
walk_cb(char kind, char* msg, int msglen, void* arg)
{
   struct walk_result* result = (struct walk_result*)arg;

   if (result->count < (int)sizeof(result->kinds))
   {
      result->kinds[result->count] = kind;
   }
   result->count++;
   result->last_msglen = msglen;

   if (kind == 'Z')
   {
      result->z_count++;
      result->z_state = pgagroal_read_byte(msg + 5);
   }
}

/* Append one wire message to buffer at offset, returning the new offset */
static int
append_message(char* buffer, int offset, char kind, const char* payload, int payload_len)
{
   pgagroal_write_byte(buffer + offset, kind);
   pgagroal_write_int32(buffer + offset + 1, payload_len + 4);
   if (payload_len > 0)
   {
      memcpy(buffer + offset + 5, payload, payload_len);
   }
   return offset + 5 + payload_len;
}

/* Build a server result: DataRow(4096 Z bytes) + CommandComplete + ReadyForQuery */
static int
build_server_result(char* buffer)
{
   char payload[D_PAYLOAD_SIZE];
   int offset = 0;

   memset(payload, 'Z', sizeof(payload));
   offset = append_message(buffer, offset, 'D', payload, sizeof(payload));
   offset = append_message(buffer, offset, 'C', NULL, 0);
   offset = append_message(buffer, offset, 'Z', "I", 1);

   return offset;
}

static bool
result_ok(int expected_count, const char* expected_kinds, int expected_z_count,
          struct walk_result* result, char expected_z_state, bool check_z_state)
{
   if (result->count != expected_count)
   {
      return false;
   }
   if (result->count >= (int)sizeof(result->kinds))
   {
      return false;
   }
   result->kinds[expected_count] = '\0';
   if (strcmp(result->kinds, expected_kinds) != 0)
   {
      return false;
   }
   if (result->z_count != expected_z_count)
   {
      return false;
   }
   if (check_z_state && result->z_state != expected_z_state)
   {
      return false;
   }
   return true;
}

/* The whole result buffer arrives in a single read. */
MCTF_TEST(test_message_aligned)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;

   length = build_server_result(buffer);
   memset(&state, 0, sizeof(state));
   memset(&result, 0, sizeof(result));

   pgagroal_parse_message(&state, buffer, length, walk_cb, &result);

   MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
               "aligned walk must report D, C and a single idle Z");

cleanup:
   MCTF_FINISH();
}

/* A message header split across reads at every possible position. */
MCTF_TEST(test_message_header_split)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;
   int split;

   length = build_server_result(buffer);

   for (split = 1; split <= 4; split++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      pgagroal_parse_message(&state, buffer, split, walk_cb, &result);
      pgagroal_parse_message(&state, buffer + split, length - split, walk_cb, &result);

      MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
                  "server header split at %d must not desynchronize the walker", split);
   }

cleanup:
   MCTF_FINISH();
}

/* A message payload split across reads at every possible position. */
MCTF_TEST(test_message_payload_split)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;
   int split;

   length = build_server_result(buffer);

   for (split = 1; split < D_PAYLOAD_SIZE; split++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      pgagroal_parse_message(&state, buffer, 5 + split, walk_cb, &result);
      pgagroal_parse_message(&state, buffer + 5 + split, length - (5 + split), walk_cb, &result);

      MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
                  "payload split at %d must not desynchronize the walker", split);
   }

cleanup:
   MCTF_FINISH();
}

/* Every possible split point of the complete result buffer. */
MCTF_TEST(test_message_exhaustive_splits)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;
   int split;

   length = build_server_result(buffer);

   for (split = 1; split < length; split++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      pgagroal_parse_message(&state, buffer, split, walk_cb, &result);
      pgagroal_parse_message(&state, buffer + split, length - split, walk_cb, &result);

      MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
                  "split at %d must not desynchronize the walker", split);
   }

cleanup:
   MCTF_FINISH();
}

/* A client query split at every possible position. */
MCTF_TEST(test_message_client_query_split)
{
   char buffer[8192];
   const char* query = "SELECT 1";
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;
   int split;

   length = append_message(buffer, 0, 'Q', query, (int)strlen(query));

   for (split = 1; split < length; split++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      pgagroal_parse_message(&state, buffer, split, walk_cb, &result);
      pgagroal_parse_message(&state, buffer + split, length - split, walk_cb, &result);

      MCTF_ASSERT(result_ok(1, "Q", 0, &result, 0, false), cleanup,
                  "query split at %d must not desynchronize the walker", split);
   }

cleanup:
   MCTF_FINISH();
}

/* A header arriving one byte at a time, then a payload in chunks. */
MCTF_TEST(test_message_multi_split)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;
   int i;

   length = build_server_result(buffer);
   memset(&state, 0, sizeof(state));
   memset(&result, 0, sizeof(result));

   for (i = 0; i < 5; i++)
   {
      pgagroal_parse_message(&state, buffer + i, 1, walk_cb, &result);
   }
   for (i = 5; i < length; i += 100)
   {
      int n = MIN(100, length - i);
      pgagroal_parse_message(&state, buffer + i, n, walk_cb, &result);
   }

   MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
               "a header split into single bytes must be assembled");

cleanup:
   MCTF_FINISH();
}

/* The Z message split exactly between its length field and its state byte: the
 * walker must hold the completed header until the state byte arrives so the
 * callback reports the real transaction state, not a zero. */
MCTF_TEST(test_message_z_state_split)
{
   char buffer[8192];
   struct walk_result result;
   struct pgagroal_message_state state;
   int length;

   length = build_server_result(buffer);
   memset(&state, 0, sizeof(state));
   memset(&result, 0, sizeof(result));

   pgagroal_parse_message(&state, buffer, length - 1, walk_cb, &result);
   pgagroal_parse_message(&state, buffer + length - 1, 1, walk_cb, &result);

   MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
               "a Z split at 5/1 must report its real state byte");

cleanup:
   MCTF_FINISH();
}

/* Observation of callback-provided message data for the large-message test */
struct observation_result
{
   int count;
   char kind;                /* Kind of the last reported message */
   int msglen;               /* msglen of the last reported message */
   int d_msglen;             /* msglen of the first 'D' message seen */
   bool first_payload_ok;    /* msg[5] holds the expected first payload byte of 'D' */
   bool last_prefix_byte_ok; /* msg[PARSE_BUFFER_LIMIT - 1] holds the expected byte of 'D' */
};

static void
observation_cb(char kind, char* msg, int msglen, void* arg)
{
   struct observation_result* result = (struct observation_result*)arg;

   result->count++;
   result->kind = kind;
   result->msglen = msglen;

   if (kind == 'D')
   {
      result->d_msglen = msglen;

      /* The 'D' payload is a run of 'A's, so every prefix position must
       * be 'A' as well */
      if (msglen >= 6 && msg[5] == 'A')
      {
         result->first_payload_ok = true;
      }
      if (msglen > PARSE_BUFFER_LIMIT && msg[PARSE_BUFFER_LIMIT - 1] == 'A')
      {
         result->last_prefix_byte_ok = true;
      }
   }
}

/*
 * A message larger than PARSE_BUFFER_LIMIT must still reach the callback
 * exactly once, when the message is complete, with the declared msglen and
 * with the first PARSE_BUFFER_LIMIT bytes contiguous. The next message in
 * the buffer must still be parsed afterwards.
 */
MCTF_TEST(test_message_large_message_prefix)
{
   static char buffer[2 * PARSE_BUFFER_LIMIT + 64];
   static char payload[PARSE_BUFFER_LIMIT + 808];
   struct pgagroal_message_state state;
   struct observation_result result;
   int length;
   int offset;
   int split;

   memset(payload, 'A', sizeof(payload));
   memset(buffer, 0, sizeof(buffer));
   offset = append_message(buffer, 0, 'D', payload, sizeof(payload));
   length = append_message(buffer, offset, 'Z', "I", 1);

   for (split = 0; split < 2; split++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      if (split == 0)
      {
         pgagroal_parse_message(&state, buffer, length, observation_cb, &result);
      }
      else
      {
         /* The large message split inside its payload, halfway through
          * the buffer */
         pgagroal_parse_message(&state, buffer, 5000, observation_cb, &result);
         pgagroal_parse_message(&state, buffer + 5000, length - 5000, observation_cb, &result);
      }

      MCTF_ASSERT(result.count == 2, cleanup,
                  "split %d: the large message and the trailing Z must be reported",
                  split);
      MCTF_ASSERT(result.kind == 'Z', cleanup,
                  "split %d: the last reported message must be the trailing Z",
                  split);
      MCTF_ASSERT(result.msglen == 6, cleanup,
                  "split %d: the trailing Z must report its total length of 6",
                  split);
      MCTF_ASSERT(result.d_msglen == PARSE_BUFFER_LIMIT + 808 + 5, cleanup,
                  "split %d: the large 'D' must report its declared total length",
                  split);
      MCTF_ASSERT(result.first_payload_ok, cleanup,
                  "split %d: the first payload byte of the large 'D' must be readable",
                  split);
      MCTF_ASSERT(result.last_prefix_byte_ok, cleanup,
                  "split %d: the first PARSE_BUFFER_LIMIT bytes of the large 'D' must be contiguous",
                  split);
      MCTF_ASSERT(state.msg_target_len == 0, cleanup,
                  "split %d: the state must be clean after the chunk",
                  split);
   }

cleanup:
   MCTF_FINISH();
}

/* A malformed length field must not hang the parser, must not invoke the
 * callback, must discard the rest of the chunk, and must leave the state
 * clean so the next chunk is parsed correctly. Before the split-header fix
 * this input spun the parser forever. */
MCTF_TEST(test_message_bogus_length)
{
   static char buffer[64];
   struct pgagroal_message_state state;
   struct walk_result result;
   int bogus[] = {0, 1, 3, -1, 0x7FFFFFFF};
   int i;
   int length;

   for (i = 0; i < 5; i++)
   {
      memset(&state, 0, sizeof(state));
      memset(&result, 0, sizeof(result));

      /* 'Z' with a bogus declared length */
      pgagroal_write_byte(buffer, 'Z');
      pgagroal_write_int32(buffer + 1, bogus[i]);

      pgagroal_parse_message(&state, buffer, 6, walk_cb, &result);

      MCTF_ASSERT(result.count == 0, cleanup,
                  "bogus length %d must not fire the callback", i);
      MCTF_ASSERT(state.header_len == 0, cleanup,
                  "bogus length %d must reset the header state", i);
      MCTF_ASSERT(state.payload_remaining == 0, cleanup,
                  "bogus length %d must reset the payload state", i);
   }

   /* After a bogus chunk the state must still parse a good message */
   length = append_message(buffer, 0, 'Z', "I", 1);
   pgagroal_parse_message(&state, buffer, length, walk_cb, &result);
   MCTF_ASSERT(result_ok(1, "Z", 1, &result, 'I', true), cleanup,
               "the state must be usable after a bogus length chunk");

cleanup:
   MCTF_FINISH();
}
