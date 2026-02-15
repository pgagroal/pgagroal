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
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>

MCTF_TEST(test_configuration_accept_time)
{
   pgagroal_time_t t;

   // Zero / disabled
   t = PGAGROAL_TIME_DISABLED;
   MCTF_ASSERT_INT_EQ(t.ms, 0, cleanup, "PGAGROAL_TIME_DISABLED should be 0");

   // Seconds
   t = PGAGROAL_TIME_SEC(10);
   MCTF_ASSERT_INT_EQ(t.ms, 10000, cleanup, "10 seconds should be 10000ms");

   // Minutes
   t = PGAGROAL_TIME_MIN(2);
   MCTF_ASSERT_INT_EQ(t.ms, 120000, cleanup, "2 minutes should be 120000ms");

   // Hours
   t = PGAGROAL_TIME_HOUR(1);
   MCTF_ASSERT_INT_EQ(t.ms, 3600000, cleanup, "1 hour should be 3600000ms");

   // Days
   t = PGAGROAL_TIME_DAY(1);
   MCTF_ASSERT_INT_EQ(t.ms, 86400000LL, cleanup, "1 day should be 86400000ms");

   // Milliseconds
   t = PGAGROAL_TIME_MS(500);
   MCTF_ASSERT_INT_EQ(t.ms, 500, cleanup, "500ms should be 500");

   // Infinite
   t = PGAGROAL_TIME_INFINITE;
   MCTF_ASSERT_INT_EQ(t.ms, -1, cleanup, "PGAGROAL_TIME_INFINITE should be -1");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_time)
{
   // Invalid suffix
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10x");

   // Negative value
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "-1s");

   // Mixed units
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h5ms");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h 5ms");

   // Space between number and unit
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10 s");

   // Non-numeric
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "abc");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_get_returns_set_values)
{
   // Seconds
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "45s");

   // Minutes
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m");

   // Milliseconds
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "500ms");

   // Hours
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h");

   // Days
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1d");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_time_format_output)
{
   pgagroal_time_t t;
   char* str = NULL;
   int ret;

   // Milliseconds
   t = PGAGROAL_TIME_MS(500);
   ret = pgagroal_time_format(t, FORMAT_TIME_MS, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format ms should return 0");
   MCTF_ASSERT_STR_EQ(str, "500ms", cleanup, "500ms should format as '500ms'");
   free(str);
   str = NULL;

   // Seconds
   t = PGAGROAL_TIME_SEC(10);
   ret = pgagroal_time_format(t, FORMAT_TIME_S, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format sec should return 0");
   MCTF_ASSERT_STR_EQ(str, "10s", cleanup, "10s should format as '10s'");
   free(str);
   str = NULL;

   // Minutes
   t = PGAGROAL_TIME_MIN(5);
   ret = pgagroal_time_format(t, FORMAT_TIME_MIN, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format min should return 0");
   MCTF_ASSERT_STR_EQ(str, "5m", cleanup, "5m should format as '5m'");
   free(str);
   str = NULL;

   // Hours
   t = PGAGROAL_TIME_HOUR(2);
   ret = pgagroal_time_format(t, FORMAT_TIME_HOUR, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format hour should return 0");
   MCTF_ASSERT_STR_EQ(str, "2h", cleanup, "2h should format as '2h'");
   free(str);
   str = NULL;

   // Days
   t = PGAGROAL_TIME_DAY(1);
   ret = pgagroal_time_format(t, FORMAT_TIME_DAY, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format day should return 0");
   MCTF_ASSERT_STR_EQ(str, "1d", cleanup, "1d should format as '1d'");
   free(str);
   str = NULL;

   // Timestamp (epoch 0)
   t = PGAGROAL_TIME_MS(0);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp epoch 0 should return 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:00.000Z", cleanup, "epoch 0 timestamp");
   free(str);
   str = NULL;

   // Timestamp (1000ms = 1 second)
   t = PGAGROAL_TIME_MS(1000);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp 1s should return 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01.000Z", cleanup, "1s timestamp");
   free(str);
   str = NULL;

   // Timestamp with millisecond precision
   t = PGAGROAL_TIME_MS(1500);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp 1.5s should return 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01.500Z", cleanup, "1.5s timestamp");
   free(str);
   str = NULL;

   // Timestamp for year 2000
   t = PGAGROAL_TIME_MS(946684800000LL);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp y2000 should return 0");
   MCTF_ASSERT_STR_EQ(str, "2000-01-01T00:00:00.000Z", cleanup, "y2000 timestamp");
   free(str);
   str = NULL;

   // NULL output should return error
   ret = pgagroal_time_format(t, FORMAT_TIME_MS, NULL);
   MCTF_ASSERT_INT_EQ(ret, 1, cleanup, "NULL output should return 1");

cleanup:
   free(str);
   MCTF_FINISH();
}
