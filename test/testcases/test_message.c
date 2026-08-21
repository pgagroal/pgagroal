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
  if (result->count > (int)sizeof(result->kinds))
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
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
  struct postgresql_message_state state;
  int length;

  length = build_server_result(buffer);
  memset(&state, 0, sizeof(state));
  memset(&result, 0, sizeof(result));

  pgagroal_parse_message(&state, buffer, length - 1, walk_cb, &result);
  pgagroal_parse_message(&state, buffer + length - 1, 1, walk_cb, &result);

  MCTF_ASSERT(result_ok(3, "DCZ", 1, &result, 'I', true), cleanup,
              a               "a Z split at 5/1 must report its real state byte");

 cleanup:
  MCTF_FINISH();
}
