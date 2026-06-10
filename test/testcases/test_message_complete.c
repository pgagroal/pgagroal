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
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Regression tests for issue #834: an AuthenticationSASLContinue split across
 * multiple network segments was parsed after the first read(), so
 * sasl_continue->length - 9 underflowed (size_t) and crashed pgagroal-cli.
 *
 * pgagroal_read_complete_message() must keep reading until the byte count
 * declared in the message header has arrived, and must reject a header whose
 * declared length is malformed.
 */

/* AuthenticationSASLContinue: 'R' + int32 length + int32 11 + SCRAM payload */
static ssize_t
build_sasl_continue(char* buffer, size_t size)
{
   const char* payload = "r=clientnonceservernonce,s=c2FsdA==,i=4096";
   size_t total = 1 + 4 + 4 + strlen(payload);

   if (size < total)
   {
      return -1;
   }

   pgagroal_write_byte(buffer, 'R');
   pgagroal_write_int32(buffer + 1, (int32_t)(total - 1));
   pgagroal_write_int32(buffer + 5, 11);
   memcpy(buffer + 9, payload, strlen(payload));

   return (ssize_t)total;
}

/* Baseline: a message arriving in a single segment is returned as-is. */
MCTF_TEST(test_complete_message_single_segment)
{
   int sv[2] = {-1, -1};
   char buffer[128];
   ssize_t total;
   int status;
   struct message* msg = NULL;

   total = build_sasl_continue(buffer, sizeof(buffer));
   MCTF_ASSERT(total > 0, cleanup, "failed to build test message");

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   MCTF_ASSERT(write(sv[1], buffer, total) == total, cleanup, "write failed");

   status = pgagroal_read_complete_message(NULL, sv[0], &msg);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "single segment should succeed");
   MCTF_ASSERT(msg != NULL, cleanup, "message should be returned");
   MCTF_ASSERT(msg->kind == 'R', cleanup, "kind should be R");
   MCTF_ASSERT_INT_EQ((int)msg->length, (int)total, cleanup, "length should match the declared length");

cleanup:
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
   MCTF_FINISH();
}

/* #834: a message split across segments must be assembled before parsing. */
MCTF_TEST(test_complete_message_split_segments)
{
   int sv[2] = {-1, -1};
   char buffer[128];
   ssize_t total;
   int status;
   pid_t pid = -1;
   struct message* msg = NULL;

   total = build_sasl_continue(buffer, sizeof(buffer));
   MCTF_ASSERT(total > 0, cleanup, "failed to build test message");

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   pid = fork();
   MCTF_ASSERT(pid >= 0, cleanup, "fork failed");

   if (pid == 0)
   {
      /* writer: 3 bytes (shorter than the 5 byte header), pause, 4 more
       * bytes, pause, the rest -- forces partial reads on the reader */
      close(sv[0]);
      write(sv[1], buffer, 3);
      usleep(50 * 1000);
      write(sv[1], buffer + 3, 4);
      usleep(50 * 1000);
      write(sv[1], buffer + 7, total - 7);
      close(sv[1]);
      _exit(0);
   }

   close(sv[1]);
   sv[1] = -1;

   status = pgagroal_read_complete_message(NULL, sv[0], &msg);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_OK, cleanup, "split message should be assembled");
   MCTF_ASSERT(msg != NULL, cleanup, "message should be returned");
   MCTF_ASSERT(msg->kind == 'R', cleanup, "kind should be R");
   MCTF_ASSERT_INT_EQ((int)msg->length, (int)total, cleanup, "all declared bytes should have been read");

cleanup:
   if (pid > 0)
   {
      waitpid(pid, NULL, 0);
   }
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
   MCTF_FINISH();
}

/* A peer that disappears mid-message is an error, not a partial message. */
MCTF_TEST(test_complete_message_truncated_by_close)
{
   int sv[2] = {-1, -1};
   char buffer[128];
   ssize_t total;
   int status;
   struct message* msg = NULL;

   total = build_sasl_continue(buffer, sizeof(buffer));
   MCTF_ASSERT(total > 0, cleanup, "failed to build test message");

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   /* half the message, then close */
   MCTF_ASSERT(write(sv[1], buffer, total / 2) == total / 2, cleanup, "write failed");
   close(sv[1]);
   sv[1] = -1;

   status = pgagroal_read_complete_message(NULL, sv[0], &msg);

   MCTF_ASSERT(status != MESSAGE_STATUS_OK, cleanup, "truncated message must not be returned as complete");
   MCTF_ASSERT(msg == NULL, cleanup, "no message should be returned");

cleanup:
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
   MCTF_FINISH();
}

/* A header whose declared length cannot be valid is rejected. */
MCTF_TEST(test_complete_message_invalid_declared_length)
{
   int sv[2] = {-1, -1};
   char buffer[8];
   int status;
   struct message* msg = NULL;

   MCTF_ASSERT_INT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0, cleanup, "socketpair failed");

   /* 'R' declaring a length of 3: less than the length field itself */
   pgagroal_write_byte(buffer, 'R');
   pgagroal_write_int32(buffer + 1, 3);

   MCTF_ASSERT(write(sv[1], buffer, 5) == 5, cleanup, "write failed");

   status = pgagroal_read_complete_message(NULL, sv[0], &msg);

   MCTF_ASSERT_INT_EQ(status, MESSAGE_STATUS_ERROR, cleanup, "invalid declared length must be an error");
   MCTF_ASSERT(msg == NULL, cleanup, "no message should be returned");

cleanup:
   if (sv[0] >= 0)
   {
      close(sv[0]);
   }
   if (sv[1] >= 0)
   {
      close(sv[1]);
   }
   MCTF_FINISH();
}
