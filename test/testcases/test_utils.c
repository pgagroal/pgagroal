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

#include <limits.h>
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
   MCTF_ASSERT(pgagroal_strcmp(NULL, NULL), cleanup,
               "two NULLs should compare equal");
   MCTF_ASSERT(!pgagroal_strcmp("a", NULL), cleanup,
               "non-NULL vs NULL should differ");
   MCTF_ASSERT(!pgagroal_strcmp(NULL, "a"), cleanup,
               "NULL vs non-NULL should differ");
   MCTF_ASSERT(pgagroal_strcmp("same", "same"), cleanup,
               "identical strings should compare equal");
   MCTF_ASSERT(!pgagroal_strcmp("a", "b"), cleanup,
               "different strings should not compare equal");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_insensitive_compare_string)
{
   MCTF_ASSERT(pgagroal_strcasecmp(NULL, NULL), cleanup,
               "two NULLs should compare equal");
   MCTF_ASSERT(!pgagroal_strcasecmp("a", NULL), cleanup,
               "non-NULL vs NULL should differ");
   MCTF_ASSERT(!pgagroal_strcasecmp(NULL, "a"), cleanup,
               "NULL vs non-NULL should differ");
   MCTF_ASSERT(pgagroal_strcasecmp("Same", "same"), cleanup,
               "same strings with different case should compare equal");
   MCTF_ASSERT(!pgagroal_strcasecmp("a", "b"), cleanup,
               "different strings should not compare equal");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_numbers)
{
   char* s = NULL;

   /* The largest value of each type is the corner case: it needs every digit
      the buffer can hold, so a size argument that is one short truncates it
      silently rather than overflowing. */
   s = pgagroal_append_int(NULL, INT_MIN);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_int returned NULL");
   MCTF_ASSERT_STR_EQ(s, "-2147483648", cleanup, "append_int truncated INT_MIN");
   free(s);
   s = NULL;

   s = pgagroal_append_int(NULL, INT_MAX);
   MCTF_ASSERT_STR_EQ(s, "2147483647", cleanup, "append_int wrong for INT_MAX");
   free(s);
   s = NULL;

   s = pgagroal_append_ulong(NULL, ULONG_MAX);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_ulong returned NULL");
   MCTF_ASSERT_STR_EQ(s, "18446744073709551615", cleanup, "append_ulong truncated ULONG_MAX");
   free(s);
   s = NULL;

   s = pgagroal_append_ullong(NULL, ULLONG_MAX);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_ullong returned NULL");
   MCTF_ASSERT_STR_EQ(s, "18446744073709551615", cleanup, "append_ullong truncated ULLONG_MAX");
   free(s);
   s = NULL;

   /* Appending onto an existing string must concatenate, not replace */
   s = pgagroal_append(NULL, "n=");
   s = pgagroal_append_ulong(s, ULONG_MAX);
   MCTF_ASSERT_STR_EQ(s, "n=18446744073709551615", cleanup, "append_ulong did not concatenate");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_double)
{
   char* s = NULL;

   /* %lf writes the whole integer part, so a large double needs far more
      room than a small fixed buffer: 1e19 alone is 20 digits before the
      six decimals. */
   s = pgagroal_append_double(NULL, 1e19);
   MCTF_ASSERT_PTR_NONNULL(s, cleanup, "append_double returned NULL");
   MCTF_ASSERT_STR_EQ(s, "10000000000000000000.000000", cleanup, "append_double truncated 1e19");
   free(s);
   s = NULL;

   s = pgagroal_append_double(NULL, 0.5);
   MCTF_ASSERT_STR_EQ(s, "0.500000", cleanup, "append_double wrong for 0.5");
   free(s);
   s = NULL;

   s = pgagroal_append_double_precision(NULL, 1e19, 2);
   MCTF_ASSERT_STR_EQ(s, "10000000000000000000.00", cleanup, "append_double_precision truncated 1e19");
   free(s);
   s = NULL;

   s = pgagroal_append_double_precision(NULL, 3.14159, 3);
   MCTF_ASSERT_STR_EQ(s, "3.142", cleanup, "append_double_precision wrong for 3.14159");

cleanup:
   free(s);
   MCTF_FINISH();
}

MCTF_TEST(test_utils_append_bool)
{
   char* s = NULL;

   s = pgagroal_append_bool(NULL, true);
   MCTF_ASSERT_STR_EQ(s, "true", cleanup, "append_bool wrong for true");
   free(s);
   s = NULL;

   s = pgagroal_append_bool(NULL, false);
   MCTF_ASSERT_STR_EQ(s, "false", cleanup, "append_bool wrong for false");

cleanup:
   free(s);
   MCTF_FINISH();
}
