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

#include <pgagroal.h>
#include <configuration.h>
#include <server.h>
#include <shmem.h>
#include <tsclient.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
build_test_conf_path(const char* subdir, char* path, size_t path_size)
{
   int n;

   n = snprintf(path, path_size, "%s/test/conf/%s/pgagroal.conf",
                project_directory, subdir);

   if (n <= 0 || (size_t)n >= path_size)
   {
      return 1;
   }

   return 0;
}

// duplicate server detection via getaddrinfo (localhost vs 127.0.0.1)
MCTF_TEST(test_server_validation_duplicate_server_getaddrinfo)
{
   struct main_configuration config;
   char path[MAX_PATH];
   int ret;

   memset(&config, 0, sizeof(struct main_configuration));
   pgagroal_init_configuration(&config);

   MCTF_ASSERT(build_test_conf_path("07", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   ret = pgagroal_read_configuration(&config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_validate_configuration(&config, true, true);
   MCTF_ASSERT(ret != 0, cleanup,
               "validation should fail for duplicate servers (localhost vs 127.0.0.1)");

cleanup:
   MCTF_FINISH();
}

// duplicate server name detection
MCTF_TEST(test_server_validation_duplicate_server_name)
{
   struct main_configuration config;
   char path[MAX_PATH];
   int ret;

   memset(&config, 0, sizeof(struct main_configuration));
   pgagroal_init_configuration(&config);

   MCTF_ASSERT(build_test_conf_path("08", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   ret = pgagroal_read_configuration(&config, path, false);
   MCTF_ASSERT(ret != 0, cleanup,
               "read_configuration should fail for duplicate server names");

cleanup:
   MCTF_FINISH();
}

// multiple primary server detection
MCTF_TEST(test_server_validation_multiple_primaries)
{
   struct main_configuration config;
   char path[MAX_PATH];
   int ret;

   memset(&config, 0, sizeof(struct main_configuration));
   pgagroal_init_configuration(&config);

   MCTF_ASSERT(build_test_conf_path("09", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   ret = pgagroal_read_configuration(&config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_validate_configuration(&config, true, true);
   MCTF_ASSERT(ret != 0, cleanup,
               "validation should fail for multiple primary servers");

cleanup:
   MCTF_FINISH();
}

// valid configuration should pass all checks
MCTF_TEST(test_server_validation_valid_config)
{
   struct main_configuration config;
   char path[MAX_PATH];
   int ret;

   memset(&config, 0, sizeof(struct main_configuration));
   pgagroal_init_configuration(&config);

   MCTF_ASSERT(build_test_conf_path("10", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   ret = pgagroal_read_configuration(&config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_validate_configuration(&config, true, true);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup,
                      "validation should pass for a valid configuration");

cleanup:
   MCTF_FINISH();
}

// duplicate system_identifier with primary=on + primary=off: should pass
MCTF_TEST(test_server_validation_duplicate_system_identifier_primary_on_off)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("15", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   /* Point both servers to the same live instance to force equal system_identifier */
   snprintf(config->servers[0].host, MISC_LENGTH, "%s", backup.servers[0].host);
   config->servers[0].port = backup.servers[0].port;
   snprintf(config->servers[1].host, MISC_LENGTH, "%s", backup.servers[0].host);
   config->servers[1].port = backup.servers[0].port;

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT_INT_EQ(ret, 0, cleanup,
                      "check_server_identifiers should pass when only one duplicate is primary");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}

// duplicate system_identifier with primary=off + primary=off: should fail
MCTF_TEST(test_server_validation_duplicate_system_identifier_primary_off_off)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("16", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   /* Point both servers to the same live instance to force equal system_identifier */
   snprintf(config->servers[0].host, MISC_LENGTH, "%s", backup.servers[0].host);
   config->servers[0].port = backup.servers[0].port;
   snprintf(config->servers[1].host, MISC_LENGTH, "%s", backup.servers[0].host);
   config->servers[1].port = backup.servers[0].port;

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT(ret != 0, cleanup,
               "check_server_identifiers should fail when duplicate non-primary servers share identifier");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}

// startup_validation = off: identifier check should be skipped entirely
MCTF_TEST(test_startup_validation_off)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("11", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT_INT_EQ(ret, 0, cleanup,
                      "check_server_identifiers should return 0 when startup_validation is off");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}

// startup_validation = try with no health_check_user: should skip gracefully
MCTF_TEST(test_startup_validation_try_no_user)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("12", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT_INT_EQ(ret, 0, cleanup,
                      "check_server_identifiers should return 0 in try mode without health_check_user");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}

// startup_validation = on with no health_check_user: should fail
MCTF_TEST(test_startup_validation_on_no_user)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("13", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT(ret != 0, cleanup,
               "check_server_identifiers should fail in on mode without health_check_user");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}

// single server: verify pg_control_version and system_identifier are populated after startup validation
MCTF_TEST(test_startup_validation_single_server_pg_control_version_and_system_identifier)
{
   struct main_configuration* config;
   struct main_configuration backup;
   char path[MAX_PATH];
   int ret;

   config = (struct main_configuration*)shmem;

   /* Save original state */
   memcpy(&backup, config, sizeof(struct main_configuration));

   MCTF_ASSERT(build_test_conf_path("14", path, sizeof(path)) == 0,
               cleanup, "Failed to build config path");

   pgagroal_init_configuration(config);
   ret = pgagroal_read_configuration(config, path, false);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "pgagroal_read_configuration should succeed");

   /* Override server host/port to point to the live test instance */
   snprintf(config->servers[0].host, MISC_LENGTH, "%s", backup.servers[0].host);
   config->servers[0].port = backup.servers[0].port;

   ret = pgagroal_check_server_identifiers();

   MCTF_ASSERT_INT_EQ(ret, 0, cleanup,
                      "check_server_identifiers should succeed for a single valid server");

   MCTF_ASSERT(config->servers[0].version > 0, cleanup,
               "server version should be populated from pg_control_version after startup validation");

   MCTF_ASSERT(strlen(config->servers[0].system_identifier) > 0, cleanup,
               "server system_identifier should be populated alongside version after startup validation");

cleanup:
   /* Restore original config */
   memcpy(config, &backup, sizeof(struct main_configuration));
   MCTF_FINISH();
}
