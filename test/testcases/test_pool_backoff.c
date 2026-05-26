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
#include <configuration.h>
#include <pool.h>
#include <mctf.h>

#include <string.h>

/*
 * Unit tests for the blocking-acquisition retry back-off (issue #813).
 *
 * pgagroal_pool_next_retry_delay() is a pure function, so the back-off sequence
 * and the cap clamp can be verified deterministically without the PostgreSQL
 * saturation harness.
 */

/* R1-7-R1: 1ms seed, doubling, clamped to the cap (250ms), then holds. */
MCTF_TEST(test_backoff_sequence)
{
   long d = 0;                               /* seed */
   int cap = DEFAULT_CONNECTION_RETRY_DELAY; /* 250 ms */

   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 1000000L, cleanup, "1st retry should seed at 1ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 2000000L, cleanup, "2nd retry should be 2ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 4000000L, cleanup, "3rd retry should be 4ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 8000000L, cleanup, "4th retry should be 8ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 16000000L, cleanup, "5th retry should be 16ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 32000000L, cleanup, "6th retry should be 32ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 64000000L, cleanup, "7th retry should be 64ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 128000000L, cleanup, "8th retry should be 128ms");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 250000000L, cleanup, "9th retry should clamp to the 250ms cap (256 > 250)");
   d = pgagroal_pool_next_retry_delay(d, cap);
   MCTF_ASSERT_INT_EQ(d, 250000000L, cleanup, "10th retry should hold at the cap");

cleanup:
   MCTF_FINISH();
}

/* R1-7-R3 (helper layer): the cap is clamped to [MIN, MAX] defensively. */
MCTF_TEST(test_backoff_cap_clamped)
{
   long d;

   /* cap below the minimum is treated as MIN (1ms) */
   d = pgagroal_pool_next_retry_delay(0, 0);
   MCTF_ASSERT_INT_EQ(d, 1000000L, cleanup, "cap 0 -> seed 1ms (min cap)");
   d = pgagroal_pool_next_retry_delay(d, 0);
   MCTF_ASSERT_INT_EQ(d, 1000000L, cleanup, "cap clamped to 1ms should hold at 1ms");

   /* cap above the maximum is treated as MAX (999ms) */
   d = pgagroal_pool_next_retry_delay(998000000L, 5000);
   MCTF_ASSERT_INT_EQ(d, 999000000L, cleanup, "cap 5000 clamps to 999ms (998*2 > 999)");

cleanup:
   MCTF_FINISH();
}

/* R1-7-R3: connection_retry_delay parsing — valid, clamp low, clamp high, reject. */
MCTF_TEST(test_connection_retry_delay_validation)
{
   struct main_configuration config;
   int ret;

   /* valid value is stored verbatim */
   memset(&config, 0, sizeof(struct main_configuration));
   ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION,
                                           CONFIGURATION_ARGUMENT_CONNECTION_RETRY_DELAY, "100");
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "valid 100 should parse");
   MCTF_ASSERT_INT_EQ(config.connection_retry_delay, 100, cleanup, "100 should be stored verbatim");

   /* below minimum clamps to MIN */
   memset(&config, 0, sizeof(struct main_configuration));
   ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION,
                                           CONFIGURATION_ARGUMENT_CONNECTION_RETRY_DELAY, "0");
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "0 should parse then clamp");
   MCTF_ASSERT_INT_EQ(config.connection_retry_delay, MIN_CONNECTION_RETRY_DELAY, cleanup, "0 should clamp to MIN");

   /* above maximum clamps to MAX */
   memset(&config, 0, sizeof(struct main_configuration));
   ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION,
                                           CONFIGURATION_ARGUMENT_CONNECTION_RETRY_DELAY, "5000");
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "5000 should parse then clamp");
   MCTF_ASSERT_INT_EQ(config.connection_retry_delay, MAX_CONNECTION_RETRY_DELAY, cleanup, "5000 should clamp to MAX");

   /* non-numeric is rejected */
   memset(&config, 0, sizeof(struct main_configuration));
   ret = pgagroal_apply_main_configuration(&config, NULL, PGAGROAL_MAIN_INI_SECTION,
                                           CONFIGURATION_ARGUMENT_CONNECTION_RETRY_DELAY, "abc");
   MCTF_ASSERT(ret != 0, cleanup, "non-numeric should be rejected");

cleanup:
   MCTF_FINISH();
}
