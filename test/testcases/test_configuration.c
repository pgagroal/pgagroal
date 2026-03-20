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
   MCTF_ASSERT_INT_EQ(t.s, 0, cleanup, "PGAGROAL_TIME_DISABLED should be 0");

   // Seconds
   t = PGAGROAL_TIME_SEC(10);
   MCTF_ASSERT_INT_EQ(t.s, 10, cleanup, "10 seconds should be 10");

   // Minutes
   t = PGAGROAL_TIME_MIN(2);
   MCTF_ASSERT_INT_EQ(t.s, 120, cleanup, "2 minutes should be 120 seconds");

   // Hours
   t = PGAGROAL_TIME_HOUR(1);
   MCTF_ASSERT_INT_EQ(t.s, 3600, cleanup, "1 hour should be 3600 seconds");

   // Days
   t = PGAGROAL_TIME_DAY(1);
   MCTF_ASSERT_INT_EQ(t.s, 86400, cleanup, "1 day should be 86400 seconds");

   // Infinite
   t = PGAGROAL_TIME_INFINITE;
   MCTF_ASSERT_INT_EQ(t.s, -1, cleanup, "PGAGROAL_TIME_INFINITE should be -1");

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
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h5s");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h 5s");

   // Space between number and unit
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10 s");

   // Non-numeric
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "abc");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_get_returns_set_values)
{
   // Seconds
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "45s");

   // Minutes
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m");

   // Hours
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h");

   // Days
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1d");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_time_format_output)
{
   pgagroal_time_t t;
   char* str = NULL;
   int ret;

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
   t = PGAGROAL_TIME_SEC(0);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp epoch 0 should return 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:00Z", cleanup, "epoch 0 timestamp");
   free(str);
   str = NULL;

   // Timestamp (1 second)
   t = PGAGROAL_TIME_SEC(1);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp 1s should return 0");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01Z", cleanup, "1s timestamp");
   free(str);
   str = NULL;

   // Timestamp for year 2000
   t = PGAGROAL_TIME_SEC(946684800LL);
   ret = pgagroal_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp y2000 should return 0");
   MCTF_ASSERT_STR_EQ(str, "2000-01-01T00:00:00Z", cleanup, "y2000 timestamp");
   free(str);
   str = NULL;

   // NULL output should return error
   ret = pgagroal_time_format(t, FORMAT_TIME_S, NULL);
   MCTF_ASSERT_INT_EQ(ret, 1, cleanup, "NULL output should return 1");

cleanup:
   free(str);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_bool)
{
   // Lowercase
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "true");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "false");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "on");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "off");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "yes");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "no");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "1");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "0");

   // Case insensitive
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "TRUE");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "True");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "FALSE");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "ON");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "Yes");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "NO");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_pipeline)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "auto");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "performance");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "session");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "transaction");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "AUTO");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PIPELINE, "Session");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_bool)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "2");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "maybe");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "enabled");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "disabled");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "y");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "n");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "t");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_KEEP_ALIVE, "f");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_pipeline)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PIPELINE, "fast");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PIPELINE, "none");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PIPELINE, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_bytes)
{
   // Bare integer (bytes)
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1024");

   // Byte suffix
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "512B");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "512b");

   // Kilobytes
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "256K");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "256k");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "256KB");

   // Megabytes
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1M");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1m");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1MB");

   // Gigabytes
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1G");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1g");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1GB");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_validation)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_VALIDATION, "off");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_VALIDATION, "foreground");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_VALIDATION, "background");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_VALIDATION, "OFF");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_VALIDATION, "Foreground");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_bytes)
{
   // Non-numeric
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "abc");

   // Negative value
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "-1");

   // Conflicting double suffix
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1MK");

   // 'BB' not allowed (bytes suffix cannot repeat)
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1BB");

   // Special characters
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "1 K");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_validation)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_VALIDATION, "on");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_VALIDATION, "true");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_VALIDATION, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_hugepage)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_HUGEPAGE, "off");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_HUGEPAGE, "try");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_HUGEPAGE, "on");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_HUGEPAGE, "OFF");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_HUGEPAGE, "Try");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_hugepage)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_HUGEPAGE, "yes");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_HUGEPAGE, "true");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_HUGEPAGE, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_log_type)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_TYPE, "console");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_TYPE, "file");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_TYPE, "syslog");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_TYPE, "Console");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_log_type)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_TYPE, "stdout");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_TYPE, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_log_mode)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_MODE, "a");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_MODE, "append");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_MODE, "c");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_MODE, "create");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_log_mode)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_MODE, "overwrite");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_MODE, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_update_process_title)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "never");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "off");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "strict");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "minimal");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "verbose");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "full");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_update_process_title)
{
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "invalid");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "yes");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_UPDATE_PROCESS_TITLE, "");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_prometheus_section_keys)
{
   // Metrics keys should be accepted in a [prometheus] section
   pgagroal_test_assert_conf_section_set_ok("prometheus", CONFIGURATION_ARGUMENT_METRICS, "9187");
   pgagroal_test_assert_conf_section_set_ok("prometheus", CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10s");
   pgagroal_test_assert_conf_section_set_ok("prometheus", CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_SIZE, "256K");

   // Same keys should still work in the main [pgagroal] section
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS, "9187");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10s");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_time_params)
{
   // blocking_timeout
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "30s");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "2m");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "1h");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "1d");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "2w");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "60");

   // idle_timeout
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_IDLE_TIMEOUT, "30s");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_IDLE_TIMEOUT, "5m");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_IDLE_TIMEOUT, "1w");

   // authentication_timeout
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, "5s");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, "10");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_time_params)
{
   // Non-numeric
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "abc");

   // Invalid suffix
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "10x");

   // Negative value
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_IDLE_TIMEOUT, "-5s");

   // Mixed units
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, "1h5s");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_log_rotation)
{
   // log_rotation_size (as_bytes path)
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "1M");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "512K");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "1G");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "1024");

   // log_rotation_age (as_seconds path)
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "1d");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "12h");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "1w");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_log_rotation)
{
   // log_rotation_size
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "abc");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "-1");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_SIZE, "1MK");

   // log_rotation_age
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "abc");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "10x");
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "-1s");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_accept_int)
{
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PORT, "5432");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PORT, "0");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_PORT, "65535");

   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_MAX_CONNECTIONS, "100");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_MAX_CONNECTIONS, "1");

   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_MAX_RETRIES, "5");
   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_MAX_RETRIES, "0");

   pgagroal_test_assert_conf_set_ok(CONFIGURATION_ARGUMENT_BACKLOG, "128");

   MCTF_FINISH();
}

MCTF_TEST(test_configuration_reject_invalid_int)
{
   // Non-numeric
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, "abc");

   // Decimal
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, "12.5");

   // Empty string
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, "");

   // Trailing characters
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, "100abc");

   // Embedded space
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, "12 34");

   // Whitespace only
   pgagroal_test_assert_conf_set_fail(CONFIGURATION_ARGUMENT_PORT, " ");

   MCTF_FINISH();
}
