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

MCTF_TEST(test_secure_strcmp_both_null)
{
   MCTF_ASSERT(pgagroal_secure_strcmp(NULL, NULL), cleanup, "both NULL should be considered equal");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_secure_strcmp_one_null)
{
   MCTF_ASSERT(!pgagroal_secure_strcmp(NULL, "secret"), cleanup, "NULL vs a string should not match");
   MCTF_ASSERT(!pgagroal_secure_strcmp("secret", NULL), cleanup, "a string vs NULL should not match");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_secure_strcmp_exact_match)
{
   MCTF_ASSERT(pgagroal_secure_strcmp("hunter2", "hunter2"), cleanup, "identical strings should match");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_secure_strcmp_wrong_content)
{
   MCTF_ASSERT(!pgagroal_secure_strcmp("hunter3", "hunter2"), cleanup, "same length, different content should not match");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_secure_strcmp_length_mismatch)
{
   MCTF_ASSERT(!pgagroal_secure_strcmp("hunter", "hunter2"), cleanup, "a shorter guess should not match");
   MCTF_ASSERT(!pgagroal_secure_strcmp("hunter22", "hunter2"), cleanup, "a longer guess should not match");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_secure_strcmp_empty_strings)
{
   MCTF_ASSERT(pgagroal_secure_strcmp("", ""), cleanup, "two empty strings should match");
   MCTF_ASSERT(!pgagroal_secure_strcmp("", "x"), cleanup, "an empty string should not match a non-empty one");

cleanup:
   MCTF_FINISH();
}
