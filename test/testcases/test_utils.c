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
#include <mctf.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_utils_append_basic)
{
   char* s = NULL;

   s = pgagroal_append(s, "foo");
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append to NULL should allocate a new buffer");
   MCTF_ASSERT_STR_EQ(s, "foo", cleanup, "expected 'foo'");

   s = pgagroal_append(s, "bar");
   MCTF_ASSERT_STR_EQ(s, "foobar", cleanup, "expected 'foobar' after second append");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_null_source_is_noop)
{
   char* s = NULL;

   s = pgagroal_append(s, "abc");
   /* Appending a NULL string must return the original buffer unchanged. */
   s = pgagroal_append(s, NULL);
   MCTF_ASSERT_STR_EQ(s, "abc", cleanup, "appending NULL should be a no-op");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_int_and_char)
{
   char* s = NULL;

   s = pgagroal_append(s, "n=");
   s = pgagroal_append_int(s, 42);
   s = pgagroal_append_char(s, '!');
   MCTF_ASSERT_STR_EQ(s, "n=42!", cleanup, "expected 'n=42!'");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_int_negative)
{
   char* s = NULL;

   s = pgagroal_append_int(s, -7);
   MCTF_ASSERT_STR_EQ(s, "-7", cleanup, "expected '-7'");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_compare_string)
{
   MCTF_ASSERT(pgagroal_compare_string(NULL, NULL), cleanup,
               "two NULLs should compare equal");
   MCTF_ASSERT(!pgagroal_compare_string("a", NULL), cleanup,
               "non-NULL vs NULL should differ");
   MCTF_ASSERT(!pgagroal_compare_string(NULL, "a"), cleanup,
               "NULL vs non-NULL should differ");
   MCTF_ASSERT(pgagroal_compare_string("same", "same"), cleanup,
               "identical strings should compare equal");
   MCTF_ASSERT(!pgagroal_compare_string("a", "b"), cleanup,
               "different strings should not compare equal");

cleanup:
   MCTF_FINISH();
}
